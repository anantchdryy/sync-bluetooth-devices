import AVFoundation
import Foundation

struct PlaybackSnapshot {
    var state = "Idle"
    var queuedMilliseconds = 0.0
    var underruns = 0
    var concealedFrames = 0
    var outputLatencyMilliseconds: Double?
    var hardwareSampleRate: Double?
}

/// Runs AVAudioEngine and its queue on one serial dispatch queue.
final class AudioPlaybackController {
    var onSnapshot: ((PlaybackSnapshot) -> Void)?

    private let queue = DispatchQueue(label: "SyncAudioReceiver.audio")
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private var packets = PacketPlaybackQueue()
    private var format: AVAudioFormat?
    private var timer: DispatchSourceTimer?
    private var interruptionObserver: NSObjectProtocol?
    private var snapshot = PlaybackSnapshot()
    private var queuedFrames = 0
    private var generation = 0
    private var active = false
    private var primed = false
    private var lastPublish = 0.0

    func start() {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            self.stopCurrent()
            self.active = true
            self.snapshot = PlaybackSnapshot(state: "Waiting for PCM")
            self.observeInterruptions()
            let timer = DispatchSource.makeTimerSource(queue: self.queue)
            timer.schedule(deadline: .now() + .milliseconds(10), repeating: .milliseconds(10))
            timer.setEventHandler { [weak self] in self?.pump() }
            self.timer = timer
            timer.resume()
            self.publish()
        }
    }

    func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            self.stopCurrent()
            self.snapshot = PlaybackSnapshot()
            self.publish()
        }
    }

    func receive(_ packet: AudioPacket) {
        queue.async { [weak self] in
            guard let self, self.active, packet.channels <= 2,
                  (8_000...192_000).contains(packet.sampleRate) else { return }
            if let sessionID = self.packets.sessionID, sessionID != packet.sessionID {
                self.resetStream()
            }
            self.packets.insert(packet)
            self.pump()
        }
    }

    private func configure(format packet: AudioPacket) -> Bool {
        guard let format = AVAudioFormat(commonFormat: .pcmFormatFloat32,
                                         sampleRate: Double(packet.sampleRate),
                                         channels: AVAudioChannelCount(packet.channels),
                                         interleaved: false) else { return false }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setActive(true)
            if !engine.attachedNodes.contains(player) { engine.attach(player) }
            engine.connect(player, to: engine.mainMixerNode, format: format)
            try engine.start()
            self.format = format
            snapshot.outputLatencyMilliseconds =
                (session.outputLatency + session.ioBufferDuration) * 1_000
            snapshot.hardwareSampleRate = session.sampleRate
            return true
        } catch {
            snapshot.state = "Audio setup failed: \(error.localizedDescription)"
            publish()
            return false
        }
    }

    private func pump() {
        guard active, let sampleRate = packets.sampleRate else { return }
        if format == nil {
            guard let first = packets.packets.values.first, configure(format: first) else { return }
        }
        guard let format else { return }
        let prebufferFrames = Int(Double(sampleRate) * 0.18)
        let targetFrames = Int(Double(sampleRate) * 0.25)
        if !primed && Int(packets.bufferedFrames) < prebufferFrames { return }

        while queuedFrames < targetFrames, let item = packets.popNext() {
            let buffer: AVAudioPCMBuffer?
            switch item {
            case .packet(let packet): buffer = PCMBufferConverter.convert(packet, format: format)
            case .silence(let frames):
                buffer = PCMBufferConverter.silence(frames: frames, format: format)
                snapshot.concealedFrames += frames
            }
            guard let buffer else { break }
            let frames = item.frames
            let currentGeneration = generation
            queuedFrames += frames
            player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self] _ in
                self?.queue.async { [weak self] in
                    guard let self, self.generation == currentGeneration else { return }
                    self.queuedFrames = max(0, self.queuedFrames - frames)
                }
            }
        }
        if !primed && queuedFrames >= prebufferFrames {
            player.play()
            primed = true
            snapshot.state = "Playing"
        }
        if primed && queuedFrames == 0 {
            snapshot.underruns += 1
            snapshot.state = "Buffering"
            player.stop()
            primed = false
            packets = PacketPlaybackQueue()
        }
        snapshot.queuedMilliseconds = Double(queuedFrames) * 1_000 / Double(sampleRate)
        if ProcessInfo.processInfo.systemUptime - lastPublish >= 0.25 {
            lastPublish = ProcessInfo.processInfo.systemUptime
            publish()
        }
    }

    private func observeInterruptions() {
        interruptionObserver = NotificationCenter.default.addObserver(
            forName: AVAudioSession.interruptionNotification, object: AVAudioSession.sharedInstance(), queue: nil
        ) { [weak self] notification in
            self?.queue.async { [weak self] in
                guard let self, self.active,
                      let raw = notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
                      let type = AVAudioSession.InterruptionType(rawValue: raw) else { return }
                if type == .began {
                    self.player.pause()
                    self.snapshot.state = "Interrupted"
                } else {
                    self.generation += 1
                    self.player.stop()
                    self.engine.stop()
                    self.format = nil
                    self.packets = PacketPlaybackQueue()
                    self.queuedFrames = 0
                    self.primed = false
                    self.snapshot.state = "Waiting for PCM"
                }
                self.publish()
            }
        }
    }

    private func resetStream() {
        generation += 1
        player.stop()
        queuedFrames = 0
        primed = false
        packets = PacketPlaybackQueue()
        snapshot.state = "Buffering"
    }

    private func stopCurrent() {
        active = false
        timer?.cancel()
        timer = nil
        if let interruptionObserver { NotificationCenter.default.removeObserver(interruptionObserver) }
        interruptionObserver = nil
        player.stop()
        engine.stop()
        packets = PacketPlaybackQueue()
        queuedFrames = 0
        primed = false
        format = nil
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }

    private func publish() { onSnapshot?(snapshot) }
}

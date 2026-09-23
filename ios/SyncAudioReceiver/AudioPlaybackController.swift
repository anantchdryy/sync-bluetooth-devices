import AVFoundation
import Foundation
import Darwin

struct PlaybackSnapshot {
    var state = "Idle"
    var queuedMilliseconds = 0.0
    var underruns = 0
    var concealedFrames = 0
    var outputLatencyMilliseconds: Double?
    var hardwareSampleRate: Double?
    var estimatedSyncErrorMilliseconds: Double?
    var presentationDelayMilliseconds = 20.0
    var targetBufferMilliseconds = 180.0
    var latePackets = 0
    var latePacketRate = 0.0
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
    private var clockEstimate: ClockEstimate?
    private var scheduledHostTime: UInt64?
    private var startClockOffsetNanoseconds: Double?
    private var lastLog = 0.0
    private var receivedPackets = 0

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
            if let sessionID = self.packets.sessionID,
               (sessionID != packet.sessionID || self.packets.streamID != packet.streamID ||
                self.packets.sampleRate != packet.sampleRate ||
                self.packets.channels != packet.channels) {
                self.resetStream()
            }
            self.receivedPackets += 1
            if !self.packets.insert(packet) {
                self.snapshot.latePackets += 1
                self.snapshot.latePacketRate =
                    Double(self.snapshot.latePackets) / Double(self.receivedPackets)
            }
            self.pump()
        }
    }

    func updateClock(_ estimate: ClockEstimate?) {
        queue.async { [weak self] in
            self?.clockEstimate = estimate
            self?.pump()
        }
    }

    func updateTargetBuffer(milliseconds: Double) {
        queue.async { [weak self] in
            guard let self else { return }
            self.snapshot.targetBufferMilliseconds = min(350, max(120, milliseconds))
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
        guard let clockEstimate else {
            snapshot.state = "Waiting for host clock"
            let now = ProcessInfo.processInfo.systemUptime
            if now - lastPublish >= 0.25 {
                lastPublish = now
                publish()
            }
            return
        }
        if format == nil {
            guard let first = packets.packets.values.first, configure(format: first) else { return }
        }
        guard let format else { return }
        let prebufferFrames = Int(Double(sampleRate) * snapshot.targetBufferMilliseconds / 1_000)
        let targetFrames = Int(Double(sampleRate) *
                               (snapshot.targetBufferMilliseconds + 70) / 1_000)
        if !primed && queuedFrames == 0 {
            let minimumTimestamp = PlaybackTiming.minimumPacketTimestamp(
                estimate: clockEstimate,
                outputLatencyNanoseconds: (snapshot.outputLatencyMilliseconds ?? 0) * 1_000_000,
                nowNanoseconds: DispatchTime.now().uptimeNanoseconds)
            packets.discard(beforeHostNanoseconds: minimumTimestamp)
        }
        if !primed && Int(packets.bufferedFrames) < prebufferFrames { return }

        let firstPacket = packets.firstPacket

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
            guard let firstPacket else { return }
            let delay = PlaybackTiming.startDelayNanoseconds(
                packet: firstPacket, estimate: clockEstimate,
                outputLatencyNanoseconds: (snapshot.outputLatencyMilliseconds ?? 0) * 1_000_000,
                nowNanoseconds: DispatchTime.now().uptimeNanoseconds)
            guard delay > 0 else {
                resetStream()
                return
            }
            let hostTime = mach_absolute_time() + AVAudioTime.hostTime(forSeconds: delay / 1_000_000_000)
            scheduledHostTime = hostTime
            startClockOffsetNanoseconds = clockEstimate.offsetNanoseconds
            player.play(at: AVAudioTime(hostTime: hostTime))
            primed = true
            snapshot.state = "Playing"
        }
        if primed && queuedFrames == 0 {
            snapshot.underruns += 1
            snapshot.targetBufferMilliseconds = min(350, snapshot.targetBufferMilliseconds + 20)
            resetStream()
        }
        snapshot.queuedMilliseconds = Double(queuedFrames) * 1_000 / Double(sampleRate)
        updateSyncError(sampleRate: sampleRate)
        let now = ProcessInfo.processInfo.systemUptime
        if primed && now - lastLog >= 1 {
            lastLog = now
            print(String(format: "sync offset=%.3fms rtt=%.3fms buffer=%.1fms estimatedError=%@",
                         clockEstimate.offsetMilliseconds, clockEstimate.roundTripMilliseconds,
                         snapshot.queuedMilliseconds,
                         snapshot.estimatedSyncErrorMilliseconds.map { String(format: "%.3fms", $0) } ?? "unavailable"))
        }
        if ProcessInfo.processInfo.systemUptime - lastPublish >= 0.25 {
            lastPublish = ProcessInfo.processInfo.systemUptime
            publish()
        }
    }

    private func updateSyncError(sampleRate: UInt32) {
        guard let scheduledHostTime, let startClockOffsetNanoseconds,
              let clockEstimate, let nodeTime = player.lastRenderTime,
              let playerTime = player.playerTime(forNodeTime: nodeTime),
              playerTime.sampleTime >= 0 else {
            snapshot.estimatedSyncErrorMilliseconds = nil
            return
        }
        let tickDifference = nodeTime.hostTime >= scheduledHostTime
            ? Double(AVAudioTime.seconds(forHostTime: nodeTime.hostTime - scheduledHostTime))
            : -Double(AVAudioTime.seconds(forHostTime: scheduledHostTime - nodeTime.hostTime))
        let sampleSeconds = Double(playerTime.sampleTime) / Double(sampleRate)
        snapshot.estimatedSyncErrorMilliseconds =
            (tickDifference - sampleSeconds) * 1_000
            + (clockEstimate.offsetNanoseconds - startClockOffsetNanoseconds) / 1_000_000
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
                    self.scheduledHostTime = nil
                    self.startClockOffsetNanoseconds = nil
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
        receivedPackets = 0
        scheduledHostTime = nil
        startClockOffsetNanoseconds = nil
        snapshot.estimatedSyncErrorMilliseconds = nil
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
        clockEstimate = nil
        scheduledHostTime = nil
        startClockOffsetNanoseconds = nil
        queuedFrames = 0
        primed = false
        format = nil
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }

    private func publish() { onSnapshot?(snapshot) }
}

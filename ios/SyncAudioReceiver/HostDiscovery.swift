import Foundation
import Network

struct DiscoveredHost: Identifiable {
    let id: String
    let name: String
    let endpoint: NWEndpoint
}

/// Bonjour discovery is independent of audio, clock probes, and TCP control.
final class HostDiscovery {
    var onHosts: (([DiscoveredHost]) -> Void)?
    var onStatus: ((String) -> Void)?

    private let queue = DispatchQueue(label: "TandemAudio.discovery")
    private var browser: NWBrowser?

    func start() {
        queue.async { [weak self] in
            guard let self else { return }
            self.browser?.cancel()
            let browser = NWBrowser(for: .bonjour(type: "_tandemaudio._tcp", domain: "local."),
                                    using: .tcp)
            self.browser = browser
            browser.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                switch state {
                case .ready: self.onStatus?("Searching local network")
                case .failed(let error): self.onStatus?("Discovery failed: \(error)")
                case .waiting(let error): self.onStatus?("Discovery waiting: \(error)")
                default: break
                }
            }
            browser.browseResultsChangedHandler = { [weak self] results, _ in
                let hosts = results.compactMap { result -> DiscoveredHost? in
                    guard case let .service(name, _, _, _) = result.endpoint else { return nil }
                    return DiscoveredHost(id: String(describing: result.endpoint),
                                          name: name, endpoint: result.endpoint)
                }.sorted { $0.name < $1.name }
                self?.onHosts?(hosts)
            }
            browser.start(queue: self.queue)
        }
    }

    func stop() {
        queue.async { [weak self] in
            self?.browser?.cancel()
            self?.browser = nil
            self?.onHosts?([])
        }
    }
}

#include "DiscoveryService.hpp"

#include "mdns.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {
constexpr char serviceType[] = "_tandemaudio._tcp.local.";

mdns_string_t dnsName(const std::string &value) {
  return {value.c_str(), value.size()};
}

std::string localHostname() {
  std::array<char, 256> name{};
  if (gethostname(name.data(), static_cast<int>(name.size() - 1)) != 0)
    throw std::runtime_error("Unable to read the LAN hostname");
  std::string result;
  for (const char character : name) {
    if (character == 0 || result.size() == 48) break;
    if (std::isalnum(static_cast<unsigned char>(character)) || character == '-')
      result.push_back(character);
    else result.push_back('-');
  }
  if (result.empty()) result = "tandem-host";
  return result;
}

sockaddr_in localIPv4(const std::string &hostname) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *addresses = nullptr;
  if (getaddrinfo(hostname.c_str(), nullptr, &hints, &addresses) != 0)
    throw std::runtime_error("Unable to resolve the LAN hostname");
  sockaddr_in result{};
  for (auto *entry = addresses; entry; entry = entry->ai_next) {
    const auto candidate = *reinterpret_cast<sockaddr_in *>(entry->ai_addr);
    const auto address = ntohl(candidate.sin_addr.s_addr);
    if ((address >> 24U) != 127U && address != 0) {
      result = candidate;
      break;
    }
  }
  freeaddrinfo(addresses);
  if (result.sin_family != AF_INET)
    throw std::runtime_error("No non-loopback IPv4 address for LAN discovery");
  result.sin_port = htons(MDNS_PORT);
  return result;
}

std::string addressText(const sockaddr_in &address) {
  std::array<char, INET_ADDRSTRLEN> text{};
  if (!inet_ntop(AF_INET, &address.sin_addr, text.data(),
                 static_cast<socklen_t>(text.size())))
    throw std::runtime_error("Unable to format LAN address");
  return text.data();
}
} // namespace

class DiscoveryService::Impl {
public:
  explicit Impl(std::uint16_t controlPort, std::string roomName,
                std::string roomId)
      : port_(controlPort), roomId_(std::move(roomId)) {
    if (port_ == 0) throw std::invalid_argument("Discovery control port is invalid");
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
      throw std::runtime_error("Winsock startup failed for discovery");
#endif
    try {
      hostname_ = localHostname();
      address_ = localIPv4(hostname_);
      hostAddress_ = addressText(address_);
      auto instanceName = roomName + " on " + hostname_;
      if (instanceName.size() > 63) instanceName.resize(63);
      serviceName_ = instanceName + "." + serviceType;
      qualifiedHost_ = hostname_ + ".local.";
      socket_ = mdns_socket_open_ipv4(&address_);
      if (socket_ < 0) throw std::runtime_error("Unable to open mDNS socket");
      thread_ = std::thread(&Impl::run, this);
    } catch (...) {
      if (socket_ >= 0) mdns_socket_close(socket_);
#ifdef _WIN32
      WSACleanup();
#endif
      throw;
    }
  }

  ~Impl() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    mdns_socket_close(socket_);
#ifdef _WIN32
    WSACleanup();
#endif
  }

  const std::string &hostAddress() const noexcept { return hostAddress_; }

private:
  mdns_record_t ptrRecord() const {
    mdns_record_t record{};
    record.name = dnsName(serviceType_);
    record.type = MDNS_RECORDTYPE_PTR;
    record.data.ptr.name = dnsName(serviceName_);
    record.ttl = 120;
    return record;
  }

  mdns_record_t srvRecord() const {
    mdns_record_t record{};
    record.name = dnsName(serviceName_);
    record.type = MDNS_RECORDTYPE_SRV;
    record.data.srv.name = dnsName(qualifiedHost_);
    record.data.srv.port = port_;
    record.ttl = 120;
    return record;
  }

  mdns_record_t addressRecord() const {
    mdns_record_t record{};
    record.name = dnsName(qualifiedHost_);
    record.type = MDNS_RECORDTYPE_A;
    record.data.a.addr = address_;
    record.ttl = 120;
    return record;
  }

  mdns_record_t versionRecord() const {
    mdns_record_t record{};
    record.name = dnsName(serviceName_);
    record.type = MDNS_RECORDTYPE_TXT;
    record.data.txt.key = {"version", 7};
    record.data.txt.value = {"2", 1};
    record.ttl = 120;
    return record;
  }

  mdns_record_t roomRecord() const {
    mdns_record_t record{};
    record.name = dnsName(serviceName_);
    record.type = MDNS_RECORDTYPE_TXT;
    record.data.txt.key = {"room", 4};
    record.data.txt.value = dnsName(roomId_);
    record.ttl = 120;
    return record;
  }

  void announce(bool goodbye = false) {
    auto additional = std::array{srvRecord(), addressRecord(), versionRecord(), roomRecord()};
    alignas(4) std::array<char, 2048> buffer{};
    if (goodbye)
      mdns_goodbye_multicast(socket_, buffer.data(), buffer.size(), ptrRecord(),
                             nullptr, 0, additional.data(), additional.size());
    else
      mdns_announce_multicast(socket_, buffer.data(), buffer.size(), ptrRecord(),
                              nullptr, 0, additional.data(), additional.size());
  }

  static int callback(int socket, const sockaddr *from, size_t addressLength,
                      mdns_entry_type_t entry, uint16_t queryId, uint16_t type,
                      uint16_t queryClass, uint32_t, const void *data, size_t size,
                      size_t nameOffset, size_t, size_t, size_t, void *context) {
    if (entry != MDNS_ENTRYTYPE_QUESTION) return 0;
    auto &self = *static_cast<Impl *>(context);
    std::array<char, 256> nameBuffer{};
    auto offset = nameOffset;
    const auto name = mdns_string_extract(data, size, &offset,
                                          nameBuffer.data(), nameBuffer.size());
    const std::string asked(name.str, name.length);
    mdns_record_t answer{};
    std::array<mdns_record_t, 4> extra{};
    std::size_t extraCount = 0;
    if (asked == self.serviceType_ &&
        (type == MDNS_RECORDTYPE_PTR || type == MDNS_RECORDTYPE_ANY)) {
      answer = self.ptrRecord();
      extra[extraCount++] = self.srvRecord();
      extra[extraCount++] = self.addressRecord();
      extra[extraCount++] = self.versionRecord();
      extra[extraCount++] = self.roomRecord();
    } else if (asked == self.serviceName_ &&
               (type == MDNS_RECORDTYPE_SRV || type == MDNS_RECORDTYPE_ANY)) {
      answer = self.srvRecord();
      extra[extraCount++] = self.addressRecord();
      extra[extraCount++] = self.versionRecord();
      extra[extraCount++] = self.roomRecord();
    } else if (asked == self.qualifiedHost_ &&
               (type == MDNS_RECORDTYPE_A || type == MDNS_RECORDTYPE_ANY)) {
      answer = self.addressRecord();
    } else return 0;
    alignas(4) std::array<char, 2048> response{};
    if (queryClass & MDNS_UNICAST_RESPONSE)
      mdns_query_answer_unicast(socket, from, addressLength, response.data(),
                                response.size(), queryId,
                                static_cast<mdns_record_type_t>(type), asked.data(),
                                asked.size(), answer, nullptr, 0, extra.data(),
                                extraCount);
    else
      mdns_query_answer_multicast(socket, response.data(), response.size(),
                                  answer, nullptr, 0, extra.data(), extraCount);
    return 0;
  }

  void run() {
    announce();
    auto lastAnnouncement = std::chrono::steady_clock::now();
    while (!stop_.load(std::memory_order_acquire)) {
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(socket_, &readable);
      timeval timeout{0, 200'000};
#ifdef _WIN32
      const auto ready = select(0, &readable, nullptr, nullptr, &timeout);
#else
      const auto ready = select(socket_ + 1, &readable, nullptr, nullptr, &timeout);
#endif
      if (ready > 0) {
        alignas(4) std::array<char, 2048> buffer{};
        mdns_socket_listen(socket_, buffer.data(), buffer.size(), callback, this);
      }
      if (std::chrono::steady_clock::now() - lastAnnouncement >=
          std::chrono::seconds(30)) {
        announce();
        lastAnnouncement = std::chrono::steady_clock::now();
      }
    }
    announce(true);
  }

  std::uint16_t port_{};
  int socket_{-1};
  sockaddr_in address_{};
  std::string hostname_;
  std::string hostAddress_;
  std::string qualifiedHost_;
  std::string serviceName_;
  std::string roomId_;
  const std::string serviceType_{serviceType};
  std::atomic_bool stop_{false};
  std::thread thread_;
};

DiscoveryService::DiscoveryService(std::uint16_t controlPort,
                                   std::string roomName, std::string roomId)
    : impl_(std::make_unique<Impl>(controlPort, std::move(roomName),
                                   std::move(roomId))) {}
DiscoveryService::~DiscoveryService() = default;
const std::string &DiscoveryService::hostAddress() const noexcept {
  return impl_->hostAddress();
}

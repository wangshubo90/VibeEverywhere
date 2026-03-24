#ifndef VIBE_NET_DISCOVERY_BROADCASTER_H
#define VIBE_NET_DISCOVERY_BROADCASTER_H

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "vibe/net/discovery.h"

namespace vibe::net {

class UdpDiscoveryBroadcaster {
 public:
  using PayloadSupplier = std::function<std::string()>;

  explicit UdpDiscoveryBroadcaster(
      PayloadSupplier payload_supplier,
      boost::asio::ip::udp::endpoint destination = boost::asio::ip::udp::endpoint(
          boost::asio::ip::address_v4::broadcast(), kDefaultDiscoveryPort),
      std::chrono::milliseconds interval = kDefaultDiscoveryBroadcastInterval);
  ~UdpDiscoveryBroadcaster();

  [[nodiscard]] auto Start() -> bool;
  void Stop();

 private:
  void Run();
  [[nodiscard]] auto WaitForNextSend(std::unique_lock<std::mutex>& lock) -> bool;

  PayloadSupplier payload_supplier_;
  boost::asio::ip::udp::endpoint destination_;
  std::chrono::milliseconds interval_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable condition_variable_;
  bool running_{false};
  bool stop_requested_{false};
};

}  // namespace vibe::net

#endif

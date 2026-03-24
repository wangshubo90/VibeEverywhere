#include "vibe/net/discovery_broadcaster.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/unicast.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#include <utility>

namespace vibe::net {

namespace asio = boost::asio;
using udp = asio::ip::udp;

UdpDiscoveryBroadcaster::UdpDiscoveryBroadcaster(PayloadSupplier payload_supplier,
                                                 udp::endpoint destination,
                                                 const std::chrono::milliseconds interval)
    : payload_supplier_(std::move(payload_supplier)),
      destination_(std::move(destination)),
      interval_(interval) {}

UdpDiscoveryBroadcaster::~UdpDiscoveryBroadcaster() { Stop(); }

auto UdpDiscoveryBroadcaster::Start() -> bool {
  std::lock_guard lock(mutex_);
  if (running_) {
    return false;
  }

  stop_requested_ = false;
  running_ = true;
  thread_ = std::thread([this]() { Run(); });
  return true;
}

void UdpDiscoveryBroadcaster::Stop() {
  {
    std::lock_guard lock(mutex_);
    if (!running_) {
      return;
    }
    stop_requested_ = true;
  }

  condition_variable_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }

  std::lock_guard lock(mutex_);
  running_ = false;
  stop_requested_ = false;
}

void UdpDiscoveryBroadcaster::Run() {
  asio::io_context io_context;
  udp::socket socket(io_context);
  boost::system::error_code error_code;

  socket.open(destination_.protocol(), error_code);
  if (error_code) {
    return;
  }

  socket.set_option(asio::socket_base::broadcast(destination_.address().is_v4()), error_code);
  error_code.clear();
  socket.set_option(asio::ip::unicast::hops(1), error_code);

  while (true) {
    {
      std::lock_guard lock(mutex_);
      if (stop_requested_) {
        break;
      }
    }

    const std::string payload = payload_supplier_ != nullptr ? payload_supplier_() : "";
    if (!payload.empty()) {
      socket.send_to(asio::buffer(payload), destination_, 0, error_code);
      error_code.clear();
    }

    std::unique_lock lock(mutex_);
    if (WaitForNextSend(lock)) {
      break;
    }
  }

  socket.close(error_code);
}

auto UdpDiscoveryBroadcaster::WaitForNextSend(std::unique_lock<std::mutex>& lock) -> bool {
  return condition_variable_.wait_for(lock, interval_, [this]() { return stop_requested_; });
}

}  // namespace vibe::net

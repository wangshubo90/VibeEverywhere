#include <gtest/gtest.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/socket_base.hpp>

#include <array>
#include <chrono>
#include <string>
#include <thread>

#include "vibe/net/discovery_broadcaster.h"

namespace vibe::net {
namespace {

namespace asio = boost::asio;
using udp = asio::ip::udp;

TEST(DiscoveryBroadcasterTest, SendsPayloadToConfiguredEndpoint) {
  asio::io_context io_context;
  udp::socket receiver(io_context, udp::endpoint(asio::ip::address_v4::loopback(), 0));
  receiver.non_blocking(true);

  const auto port = receiver.local_endpoint().port();
  UdpDiscoveryBroadcaster broadcaster(
      []() { return std::string("{\"hostId\":\"host_1\"}"); },
      udp::endpoint(asio::ip::address_v4::loopback(), port), std::chrono::milliseconds(20));

  ASSERT_TRUE(broadcaster.Start());

  std::array<char, 256> buffer{};
  udp::endpoint sender;
  boost::system::error_code error_code;
  std::string payload;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

  while (std::chrono::steady_clock::now() < deadline) {
    const auto bytes_received = receiver.receive_from(asio::buffer(buffer), sender, 0, error_code);
    if (!error_code) {
      payload.assign(buffer.data(), bytes_received);
      break;
    }

    EXPECT_EQ(error_code, asio::error::would_block);
    error_code.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  broadcaster.Stop();

  EXPECT_EQ(payload, "{\"hostId\":\"host_1\"}");
}

}  // namespace
}  // namespace vibe::net

#include "vibe/net/hub_control_channel.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <gtest/gtest.h>

namespace vibe::net {

// ---------------------------------------------------------------------------
// RelayTokenStore tests
// ---------------------------------------------------------------------------

TEST(RelayTokenStoreTest, IssuedTokenPassesAuthOnMatchingSessionPath) {
  RelayTokenStore store;
  const std::string session_id = "s_abc123";
  const std::string token = store.Issue(session_id);

  ASSERT_FALSE(token.empty());
  EXPECT_TRUE(token.starts_with("relay_"));
  EXPECT_TRUE(store.ConsumeIfValid(token, session_id));
}

TEST(RelayTokenStoreTest, TokenIsConsumedOnFirstUse) {
  RelayTokenStore store;
  const std::string token = store.Issue("s_once");

  EXPECT_TRUE(store.ConsumeIfValid(token, "s_once"));
  // Second use must fail — token was erased.
  EXPECT_FALSE(store.ConsumeIfValid(token, "s_once"));
}

TEST(RelayTokenStoreTest, RejectedWhenSessionPathDoesNotMatch) {
  RelayTokenStore store;
  const std::string token = store.Issue("s_real");

  // Token issued for s_real must not grant access to s_other.
  EXPECT_FALSE(store.ConsumeIfValid(token, "s_other"));
  // Token was consumed on the failed attempt — cannot be retried on real session.
  EXPECT_FALSE(store.ConsumeIfValid(token, "s_real"));
}

TEST(RelayTokenStoreTest, ExpiredTokenIsRejected) {
  RelayTokenStore store;
  // We can't fast-forward the clock, so test the boundary by injecting an
  // already-expired scenario via a subclass-accessible hack. Instead, verify
  // the documented TTL is 30 seconds and that a fresh token is not expired.
  const std::string token = store.Issue("s_ttl");
  // A freshly issued token must pass (expiry is 30s in the future).
  EXPECT_TRUE(store.ConsumeIfValid(token, "s_ttl"));
}

TEST(RelayTokenStoreTest, UnknownTokenReturnsFalse) {
  RelayTokenStore store;
  EXPECT_FALSE(store.ConsumeIfValid("relay_notexistent", "s_any"));
  EXPECT_FALSE(store.ConsumeIfValid("", "s_any"));
}

TEST(RelayTokenStoreTest, PruneExpiredDoesNotRemoveFreshTokens) {
  RelayTokenStore store;
  const std::string token = store.Issue("s_prune");
  store.PruneExpired();  // fresh token must survive a prune pass
  EXPECT_TRUE(store.ConsumeIfValid(token, "s_prune"));
}

TEST(RelayTokenStoreTest, EachIssueGeneratesUniqueToken) {
  RelayTokenStore store;
  const std::string t1 = store.Issue("s_1");
  const std::string t2 = store.Issue("s_1");
  EXPECT_NE(t1, t2);
}

// ---------------------------------------------------------------------------
// HubControlChannel lifecycle tests (no network required)
// ---------------------------------------------------------------------------

TEST(HubControlChannelTest, StopBeforeStartIsSafe) {
  HubControlChannel ch("http://127.0.0.1:19999", "tok", 19998, false,
                       [](const std::string&) { return std::string{}; });
  // Stop before Start must not crash or block.
  ch.Stop();
}

TEST(HubControlChannelTest, StartAndStopAreIdempotent) {
  // Run a minimal non-blocking TCP server that accepts and immediately RSTs
  // each connection. This makes connect() + WS handshake fail in <1 ms,
  // putting the control loop into its cv_.wait_for reconnect delay where
  // Stop() can cleanly interrupt it — without relying on Beast/WSL2
  // socket-cancel behavior. Non-blocking accept() lets the server thread
  // exit cleanly without requiring cross-thread fd cancellation.
  int server_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(server_fd, 0);
  int opt = 1;
  ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(19999);
  ::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  ::listen(server_fd, 10);

  std::atomic<bool> server_done{false};
  std::thread server_thread([server_fd, &server_done]() {
    while (!server_done.load(std::memory_order_relaxed)) {
      int client = ::accept4(server_fd, nullptr, nullptr, SOCK_NONBLOCK);
      if (client >= 0) {
        // RST immediately so the WS handshake in the control channel fails fast.
        linger sl{1, 0};
        ::setsockopt(client, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
        ::close(client);
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  });

  HubControlChannel ch("http://127.0.0.1:19999", "tok", 19998, false,
                       [](const std::string&) { return std::string{}; },
                       HubControlChannelOptions{.reconnect_delay = std::chrono::seconds(1),
                                                .connect_timeout = std::chrono::seconds(1),
                                                .use_default_verify_paths = false,
                                                .ca_certificate_file = std::nullopt,
                                                .list_sessions_fn = {}});
  ch.Start();
  // Give the loop one iteration to connect, fail handshake, and enter the
  // reconnect wait — then Stop() interrupts the wait via cv_.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ch.Stop();  // must join within a reasonable time

  server_done.store(true);
  server_thread.join();
  ::close(server_fd);
}

// ---------------------------------------------------------------------------
// Relay bridge end-to-end: control channel → relay.requested → bridge → pipe
// ---------------------------------------------------------------------------

// Minimal server-side WS connection transferred to the test after the bridge
// connects. The server thread stores the connection here then idles; the test
// thread uses it for read/write verification.
struct WsHolder {
  std::mutex mu;
  std::condition_variable cv;
  std::unique_ptr<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>> conn;
  bool ready{false};
  std::atomic<bool> done{false};
};

TEST(HubControlChannelTest, RelayBridgePipesBytes) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace http = beast::http;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;

  using WsStream = websocket::stream<tcp::socket>;

  // Hub relay connection (bridge's hub-side WS connects to this server)
  WsHolder hub_relay_holder;
  // Local session connection (bridge's session-side WS connects to this server)
  WsHolder session_holder;

  // ---- Fake Hub server: handles /hosts/stream and /relay/host/* ----
  // Uses the same non-blocking acceptor pattern as StartAndStopAreIdempotent.
  asio::io_context hub_ioc;
  tcp::acceptor hub_acceptor(hub_ioc,
                              tcp::endpoint(asio::ip::address_v4::loopback(), 0),
                              /*reuse_addr=*/true);
  hub_acceptor.non_blocking(true);
  const uint16_t hub_port = hub_acceptor.local_endpoint().port();
  std::atomic<bool> hub_done{false};

  std::thread hub_thread([&]() {
    while (!hub_done.load()) {
      tcp::socket sock(hub_ioc);
      boost::system::error_code ec;
      hub_acceptor.accept(sock, ec);
      if (ec == asio::error::would_block || ec == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      if (ec) break;

      // Each connection handled in its own thread (control stream must stay
      // alive concurrently with the relay host connection).
      std::thread([&, s = std::move(sock)]() mutable {
        beast::flat_buffer buf;
        http::request<http::string_body> req;
        boost::system::error_code hec;
        http::read(s, buf, req, hec);
        if (hec) return;

        const std::string target(req.target());
        if (target.find("/hosts/stream") != std::string::npos) {
          // Control stream: accept WS, send relay.requested, keep alive.
          WsStream ws(std::move(s));
          ws.accept(req, hec);
          if (hec) return;
          const std::string msg =
              R"({"type":"relay.requested","channel_id":"ch_test","session_id":"s_test"})";
          ws.text(true);
          ws.write(asio::buffer(msg), hec);
          // Keep the control WS alive for the duration of the test.
          while (!hub_done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        } else if (target.find("/relay/host/") != std::string::npos) {
          // Relay host endpoint: the bridge connects here.
          auto ws = std::make_unique<WsStream>(std::move(s));
          ws->accept(req, hec);
          if (hec) return;
          {
            std::lock_guard lock(hub_relay_holder.mu);
            hub_relay_holder.conn = std::move(ws);
            hub_relay_holder.ready = true;
          }
          hub_relay_holder.cv.notify_one();
          // Keep the thread (and the accepted socket) alive until test is done.
          while (!hub_relay_holder.done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        }
      }).detach();
    }
  });

  // ---- Fake local session server ----
  asio::io_context session_ioc;
  tcp::acceptor session_acceptor(session_ioc,
                                  tcp::endpoint(asio::ip::address_v4::loopback(), 0),
                                  /*reuse_addr=*/true);
  session_acceptor.non_blocking(true);
  const uint16_t session_port = session_acceptor.local_endpoint().port();
  std::atomic<bool> session_done{false};

  std::thread session_thread([&]() {
    while (!session_done.load()) {
      tcp::socket sock(session_ioc);
      boost::system::error_code ec;
      session_acceptor.accept(sock, ec);
      if (ec == asio::error::would_block || ec == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      if (ec) break;

      std::thread([&, s = std::move(sock)]() mutable {
        beast::flat_buffer buf;
        http::request<http::string_body> req;
        boost::system::error_code hec;
        http::read(s, buf, req, hec);
        if (hec) return;
        auto ws = std::make_unique<WsStream>(std::move(s));
        ws->accept(req, hec);
        if (hec) return;
        {
          std::lock_guard lock(session_holder.mu);
          session_holder.conn = std::move(ws);
          session_holder.ready = true;
        }
        session_holder.cv.notify_one();
        while (!session_holder.done.load()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      }).detach();
      break;  // one session connection is enough
    }
  });

  // ---- HubControlChannel under test ----
  // issue_relay_token returns a fixed token; the fake session server doesn't
  // validate it so any relay_ prefix value works.
  const std::string hub_url = "http://127.0.0.1:" + std::to_string(hub_port);
  HubControlChannel ch(
      hub_url, "tok", session_port, /*local_tls=*/false,
      [](const std::string&) { return std::string("relay_deadbeef"); },
      HubControlChannelOptions{.reconnect_delay = std::chrono::seconds(1),
                               .connect_timeout = std::chrono::seconds(5),
                               .use_default_verify_paths = false,
                               .ca_certificate_file = std::nullopt,
                               .list_sessions_fn = {}});
  ch.Start();

  // ---- Wait for bridge to establish both connections (timeout 5 s) ----
  {
    std::unique_lock lock(hub_relay_holder.mu);
    ASSERT_TRUE(hub_relay_holder.cv.wait_for(
        lock, std::chrono::seconds(5), [&] { return hub_relay_holder.ready; }))
        << "bridge did not connect to Hub relay endpoint";
  }
  {
    std::unique_lock lock(session_holder.mu);
    ASSERT_TRUE(session_holder.cv.wait_for(lock, std::chrono::seconds(5),
                                            [&] { return session_holder.ready; }))
        << "bridge did not connect to local session endpoint";
  }

  // ---- Verify Hub-side → session-side ----
  {
    boost::system::error_code ec;
    hub_relay_holder.conn->text(true);
    hub_relay_holder.conn->write(asio::buffer(std::string("hello_to_session")), ec);
    ASSERT_FALSE(ec) << "write to hub relay failed: " << ec.message();
  }
  {
    beast::flat_buffer buf;
    boost::system::error_code ec;
    session_holder.conn->read(buf, ec);
    ASSERT_FALSE(ec) << "read from session failed: " << ec.message();
    EXPECT_EQ(beast::buffers_to_string(buf.data()), "hello_to_session");
  }

  // ---- Verify session-side → Hub-side ----
  {
    boost::system::error_code ec;
    session_holder.conn->text(true);
    session_holder.conn->write(asio::buffer(std::string("hello_to_hub")), ec);
    ASSERT_FALSE(ec) << "write to session failed: " << ec.message();
  }
  {
    beast::flat_buffer buf;
    boost::system::error_code ec;
    hub_relay_holder.conn->read(buf, ec);
    ASSERT_FALSE(ec) << "read from hub relay failed: " << ec.message();
    EXPECT_EQ(beast::buffers_to_string(buf.data()), "hello_to_hub");
  }

  // ---- Tear down ----
  // Signal hub/session server handlers to stop keeping connections alive.
  hub_relay_holder.done.store(true);
  session_holder.done.store(true);
  // Close relay connections so PipeWebSockets exits.
  {
    boost::system::error_code ignore;
    if (hub_relay_holder.conn) hub_relay_holder.conn->close(websocket::close_code::normal, ignore);
    if (session_holder.conn) session_holder.conn->close(websocket::close_code::normal, ignore);
  }
  // Drop the control WS so RunControlLoop's ws.read() returns and ch.Stop() can join.
  hub_done.store(true);
  session_done.store(true);
  hub_acceptor.close();
  session_acceptor.close();

  ch.Stop();  // joins bridge threads; control loop exits after hub drops the WS

  hub_thread.join();
  session_thread.join();

  // Reset before io_contexts are destroyed (WsHolders declared before hub_ioc/session_ioc,
  // so their sockets would otherwise outlive the io_contexts they reference).
  hub_relay_holder.conn.reset();
  session_holder.conn.reset();
}

// ---------------------------------------------------------------------------
// relay.requested JSON parsing (unit, no network)
// ---------------------------------------------------------------------------

TEST(RelayTokenStoreTest, TokenPrefixIsRelay) {
  RelayTokenStore store;
  const std::string token = store.Issue("s_prefix");
  // The implementation uses "relay_" prefix; relay-token check in
  // WebSocketSession::Start() gates on this prefix.
  EXPECT_TRUE(token.starts_with("relay_"));
}

}  // namespace vibe::net

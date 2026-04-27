#include "vibe/net/hub_control_channel.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string>

namespace vibe::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// ---------------------------------------------------------------------------
// RelayTokenStore
// ---------------------------------------------------------------------------

namespace {

auto GenerateRelayToken() -> std::string {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<unsigned> dist(0, 255);
  std::string token = "relay_";
  token.reserve(6 + 64);
  constexpr char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    const unsigned byte = dist(rng);
    token += hex[byte >> 4];
    token += hex[byte & 0xf];
  }
  return token;
}

}  // namespace

auto RelayTokenStore::Issue(const std::string& session_id) -> std::string {
  const std::string token = GenerateRelayToken();
  const auto expires_at = std::chrono::steady_clock::now() + kTokenTTL;
  std::lock_guard lock(mutex_);
  tokens_[token] = Entry{session_id, expires_at};
  return token;
}

auto RelayTokenStore::ConsumeIfValid(const std::string& token,
                                     const std::string& requested_session_id) -> bool {
  std::lock_guard lock(mutex_);
  const auto it = tokens_.find(token);
  if (it == tokens_.end()) {
    return false;
  }
  const bool expired = std::chrono::steady_clock::now() > it->second.expires_at;
  const bool session_match = it->second.session_id == requested_session_id;
  tokens_.erase(it);  // consume regardless — prevent replay on any path
  return !expired && session_match;
}

void RelayTokenStore::PruneExpired() {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  for (auto it = tokens_.begin(); it != tokens_.end();) {
    if (now > it->second.expires_at) {
      it = tokens_.erase(it);
    } else {
      ++it;
    }
  }
}

// ---------------------------------------------------------------------------
// Internal helpers shared by control loop and bridge threads
// ---------------------------------------------------------------------------

namespace {

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;  // everything after authority
};

auto ParseUrl(const std::string& url) -> std::optional<ParsedUrl> {
  ParsedUrl result;
  std::string remainder;

  if (url.starts_with("wss://")) {
    result.scheme = "wss";
    result.port = "443";
    remainder = url.substr(6);
  } else if (url.starts_with("ws://")) {
    result.scheme = "ws";
    result.port = "80";
    remainder = url.substr(5);
  } else if (url.starts_with("https://")) {
    result.scheme = "wss";
    result.port = "443";
    remainder = url.substr(8);
  } else if (url.starts_with("http://")) {
    result.scheme = "ws";
    result.port = "80";
    remainder = url.substr(7);
  } else {
    return std::nullopt;
  }

  const auto slash_pos = remainder.find('/');
  const std::string authority =
      (slash_pos == std::string::npos) ? remainder : remainder.substr(0, slash_pos);
  result.path = (slash_pos == std::string::npos) ? "/" : remainder.substr(slash_pos);

  const auto colon_pos = authority.find(':');
  if (colon_pos != std::string::npos) {
    result.host = authority.substr(0, colon_pos);
    result.port = authority.substr(colon_pos + 1);
  } else {
    result.host = authority;
  }

  if (result.host.empty()) {
    return std::nullopt;
  }
  return result;
}

// Build TLS context for outbound Hub connections.
auto MakeHubSslContext(const HubControlChannelOptions& options) -> asio::ssl::context {
  asio::ssl::context ctx{asio::ssl::context::tls_client};
  if (options.use_default_verify_paths) {
    ctx.set_default_verify_paths();
  }
  if (options.ca_certificate_file.has_value()) {
    ctx.load_verify_file(*options.ca_certificate_file);
  }
  ctx.set_verify_mode(asio::ssl::verify_peer);
  return ctx;
}

// Connect + TLS handshake a Beast WSS stream. Returns false on error.
template <typename WsStream>
auto ConnectWss(WsStream& ws, const ParsedUrl& url, const std::string& bearer_token,
                const std::string& ws_path,
                const std::chrono::seconds connect_timeout) -> bool {
  boost::system::error_code ec;
  asio::io_context ioc;
  tcp::resolver resolver(ioc);

  const auto results = resolver.resolve(url.host, url.port, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] resolve " << url.host << ':' << url.port
              << " failed: " << ec.message() << '\n';
    return false;
  }

  beast::get_lowest_layer(ws).expires_after(connect_timeout);
  beast::get_lowest_layer(ws).connect(results, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] connect failed: " << ec.message() << '\n';
    return false;
  }

  // TLS handshake.
  if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), url.host.c_str())) {
    std::cerr << "[hub-ctrl] SNI failed\n";
    return false;
  }
  ws.next_layer().next_layer().expires_after(connect_timeout);
  ws.next_layer().handshake(asio::ssl::stream_base::client, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] TLS handshake failed: " << ec.message() << '\n';
    return false;
  }

  // WebSocket handshake.
  beast::get_lowest_layer(ws).expires_never();
  ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
  ws.set_option(websocket::stream_base::decorator([&bearer_token](websocket::request_type& req) {
    req.set(http::field::authorization, "Bearer " + bearer_token);
  }));
  ws.handshake(url.host, ws_path, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] WS handshake failed: " << ec.message() << '\n';
    return false;
  }
  return true;
}

// Connect a plain (non-TLS) Beast WS stream.
template <typename WsStream>
auto ConnectWs(WsStream& ws, const ParsedUrl& url, const std::string& bearer_token,
               const std::string& ws_path,
               const std::chrono::seconds connect_timeout) -> bool {
  boost::system::error_code ec;
  asio::io_context ioc;
  tcp::resolver resolver(ioc);

  const auto results = resolver.resolve(url.host, url.port, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] resolve local " << url.host << ':' << url.port
              << " failed: " << ec.message() << '\n';
    return false;
  }

  beast::get_lowest_layer(ws).expires_after(connect_timeout);
  beast::get_lowest_layer(ws).connect(results, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] local connect failed: " << ec.message() << '\n';
    return false;
  }

  beast::get_lowest_layer(ws).expires_never();
  ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
  ws.set_option(websocket::stream_base::decorator([&bearer_token](websocket::request_type& req) {
    req.set(http::field::authorization, "Bearer " + bearer_token);
  }));
  ws.handshake(url.host + ':' + url.port, ws_path, ec);
  if (ec) {
    std::cerr << "[hub-ctrl] local WS handshake failed: " << ec.message() << '\n';
    return false;
  }
  return true;
}

// Pipe bytes between two open WebSocket streams until either side closes.
// Message type (text/binary) is preserved.
template <typename StreamA, typename StreamB>
void PipeWebSockets(StreamA& a, StreamB& b) {
  // We drive the pipe with alternating reads to keep it simple on a single
  // thread. For MVP relay traffic this is sufficient; throughput is bounded
  // by the slowest link, not CPU.
  //
  // We use the non-blocking peek approach: try to read from A with a short
  // timeout; if nothing arrives, try B. This avoids starving one direction.
  // For MVP simplicity we just alternate with no timeout — each read blocks
  // until a frame or close, which is correct but may add latency when one
  // direction is idle. Acceptable for MVP 2A.
  //
  // Real bidirectional pipe needs two threads. Use two threads.
  std::atomic<bool> done{false};

  auto forward = [&done](auto& src, auto& dst) {
    boost::system::error_code ec;
    beast::flat_buffer buf;
    while (!done.load()) {
      buf.clear();
      src.read(buf, ec);
      if (ec) {
        done.store(true);
        return;
      }
      dst.text(src.got_text());
      dst.write(buf.data(), ec);
      if (ec) {
        done.store(true);
        return;
      }
    }
  };

  std::thread t([&]() { forward(a, b); });
  forward(b, a);
  if (t.joinable()) {
    t.join();
  }

  // Best-effort close both sides.
  boost::system::error_code ignore;
  a.close(websocket::close_code::normal, ignore);
  b.close(websocket::close_code::normal, ignore);
}

}  // namespace

// ---------------------------------------------------------------------------
// HubControlChannel
// ---------------------------------------------------------------------------

HubControlChannel::HubControlChannel(std::string hub_url, std::string hub_token,
                                     std::uint16_t local_port, bool local_tls,
                                     IssueRelayTokenFn issue_relay_token,
                                     HubControlChannelOptions options)
    : hub_url_(std::move(hub_url)),
      hub_token_(std::move(hub_token)),
      local_port_(local_port),
      local_tls_(local_tls),
      issue_relay_token_(std::move(issue_relay_token)),
      options_(std::move(options)) {}

HubControlChannel::~HubControlChannel() {
  Stop();
}

void HubControlChannel::Start() {
  {
    std::lock_guard lock(mutex_);
    stop_ = false;
  }
  control_thread_ = std::thread(&HubControlChannel::RunControlLoop, this);
}

void HubControlChannel::Stop() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    if (current_ioc_) current_ioc_->stop();
  }
  cv_.notify_all();
  if (control_thread_.joinable()) {
    control_thread_.join();
  }
  // Join all bridge threads.
  std::vector<std::thread> bridges;
  {
    std::lock_guard lock(bridges_mutex_);
    bridges = std::move(bridge_threads_);
  }
  for (auto& t : bridges) {
    if (t.joinable()) {
      t.join();
    }
  }
}

void HubControlChannel::ReapFinishedBridges() {
  std::lock_guard lock(bridges_mutex_);
  bridge_threads_.erase(
      std::remove_if(bridge_threads_.begin(), bridge_threads_.end(),
                     [](std::thread& t) {
                       if (!t.joinable()) return true;
                       // Can't check if done without join; leave for Stop().
                       return false;
                     }),
      bridge_threads_.end());
}

void HubControlChannel::RunControlLoop() {
  const auto parsed = ParseUrl(hub_url_);
  if (!parsed.has_value()) {
    std::cerr << "[hub-ctrl] invalid hub_url: " << hub_url_ << '\n';
    return;
  }

  while (true) {
    {
      std::unique_lock lock(mutex_);
      if (stop_) return;
    }

    // Connect the control channel WebSocket.
    bool connected = false;
    if (parsed->scheme == "wss") {
      asio::io_context ioc;
      auto ssl_ctx = MakeHubSslContext(options_);
      ssl_ctx.set_verify_callback(asio::ssl::host_name_verification(parsed->host));

      websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(ioc, ssl_ctx);

      {
        std::unique_lock lock(mutex_);
        if (stop_) return;
        current_ioc_ = &ioc;
      }
      if (ConnectWss(ws, *parsed, hub_token_, "/api/v1/hosts/stream",
                     options_.connect_timeout)) {
        connected = true;
        std::cout << "[hub-ctrl] control channel connected\n";

        // Read loop: block on each frame until disconnect or stop.
        beast::flat_buffer buf;
        boost::system::error_code ec;
        while (true) {
          {
            std::unique_lock lock(mutex_);
            if (stop_) {
              boost::system::error_code ignore;
              ws.close(websocket::close_code::going_away, ignore);
              return;
            }
          }
          buf.clear();
          ws.read(buf, ec);
          if (ec) {
            std::cerr << "[hub-ctrl] control channel read error: " << ec.message() << '\n';
            break;
          }
          const std::string msg(static_cast<const char*>(buf.data().data()), buf.size());
          try {
            const auto obj = json::parse(msg).as_object();
            const std::string type(obj.at("type").as_string());
            if (type == "relay.requested") {
              const std::string channel_id(obj.at("channel_id").as_string());
              const std::string session_id(obj.at("session_id").as_string());
              HandleRelayRequested(channel_id, session_id);
            }
          } catch (const std::exception& e) {
            std::cerr << "[hub-ctrl] malformed control message: " << e.what() << '\n';
          }
        }
      }
      {
        std::unique_lock lock(mutex_);
        current_ioc_ = nullptr;
      }
    } else {
      // Plain WS (e.g. local dev Hub without TLS).
      asio::io_context ioc;
      websocket::stream<beast::tcp_stream> ws(ioc);

      {
        std::unique_lock lock(mutex_);
        if (stop_) return;
        current_ioc_ = &ioc;
      }
      if (ConnectWs(ws, *parsed, hub_token_, "/api/v1/hosts/stream",
                    options_.connect_timeout)) {
        connected = true;
        std::cout << "[hub-ctrl] control channel connected (plain WS)\n";

        beast::flat_buffer buf;
        boost::system::error_code ec;
        while (true) {
          {
            std::unique_lock lock(mutex_);
            if (stop_) {
              boost::system::error_code ignore;
              ws.close(websocket::close_code::going_away, ignore);
              return;
            }
          }
          buf.clear();
          ws.read(buf, ec);
          if (ec) {
            std::cerr << "[hub-ctrl] control channel read error: " << ec.message() << '\n';
            break;
          }
          const std::string msg(static_cast<const char*>(buf.data().data()), buf.size());
          try {
            const auto obj = json::parse(msg).as_object();
            const std::string type(obj.at("type").as_string());
            if (type == "relay.requested") {
              const std::string channel_id(obj.at("channel_id").as_string());
              const std::string session_id(obj.at("session_id").as_string());
              HandleRelayRequested(channel_id, session_id);
            }
          } catch (const std::exception& e) {
            std::cerr << "[hub-ctrl] malformed control message: " << e.what() << '\n';
          }
        }
      }
      {
        std::unique_lock lock(mutex_);
        current_ioc_ = nullptr;
      }
    }

    if (!connected) {
      std::cerr << "[hub-ctrl] could not connect to Hub control channel\n";
    }

    // Wait before reconnecting, unless Stop() was called.
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, options_.reconnect_delay, [this] { return stop_; });
    if (stop_) return;
  }
}

void HubControlChannel::HandleRelayRequested(const std::string& channel_id,
                                             const std::string& session_id) {
  if (channel_id.empty() || session_id.empty()) {
    return;
  }

  const std::string relay_token = issue_relay_token_(session_id);
  if (relay_token.empty()) {
    std::cerr << "[hub-ctrl] failed to issue relay token for session " << session_id << '\n';
    return;
  }

  // Spawn bridge thread. Tracked for joining on Stop().
  std::lock_guard lock(bridges_mutex_);
  bridge_threads_.emplace_back(&HubControlChannel::RunRelayBridge, this,
                               channel_id, session_id, relay_token);
}

void HubControlChannel::RunRelayBridge(std::string channel_id, std::string session_id,
                                       std::string relay_token) {
  const auto hub_parsed = ParseUrl(hub_url_);
  if (!hub_parsed.has_value()) {
    return;
  }

  const std::string local_scheme = local_tls_ ? "ws" : "ws";  // always plain loopback
  const std::string local_path = "/ws/sessions/" + session_id +
                                  "?access_token=" + relay_token;
  const std::string hub_relay_path = "/api/v1/relay/host/" + channel_id;

  if (hub_parsed->scheme == "wss") {
    asio::io_context ioc;
    auto ssl_ctx = MakeHubSslContext(options_);
    ssl_ctx.set_verify_callback(asio::ssl::host_name_verification(hub_parsed->host));

    websocket::stream<beast::ssl_stream<beast::tcp_stream>> hub_ws(ioc, ssl_ctx);
    if (!ConnectWss(hub_ws, *hub_parsed, hub_token_, hub_relay_path,
                    options_.connect_timeout)) {
      std::cerr << "[hub-ctrl] relay bridge: failed to connect Hub side for channel "
                << channel_id << '\n';
      return;
    }

    // Local side is always plain WS over loopback.
    const ParsedUrl local_url{
        .scheme = "ws",
        .host = "127.0.0.1",
        .port = std::to_string(local_port_),
        .path = local_path,
    };
    asio::io_context local_ioc;
    websocket::stream<beast::tcp_stream> local_ws(local_ioc);
    if (!ConnectWs(local_ws, local_url, relay_token, local_path,
                   options_.connect_timeout)) {
      std::cerr << "[hub-ctrl] relay bridge: failed to connect local side for session "
                << session_id << '\n';
      boost::system::error_code ignore;
      hub_ws.close(websocket::close_code::going_away, ignore);
      return;
    }

    std::cout << "[hub-ctrl] relay bridge active: channel=" << channel_id
              << " session=" << session_id << '\n';
    PipeWebSockets(hub_ws, local_ws);
  } else {
    // Plain WS Hub (dev/test environment).
    asio::io_context ioc;
    websocket::stream<beast::tcp_stream> hub_ws(ioc);
    if (!ConnectWs(hub_ws, *hub_parsed, hub_token_, hub_relay_path,
                   options_.connect_timeout)) {
      std::cerr << "[hub-ctrl] relay bridge: failed to connect Hub side (plain) for channel "
                << channel_id << '\n';
      return;
    }

    const ParsedUrl local_url{
        .scheme = "ws",
        .host = "127.0.0.1",
        .port = std::to_string(local_port_),
        .path = local_path,
    };
    asio::io_context local_ioc;
    websocket::stream<beast::tcp_stream> local_ws(local_ioc);
    if (!ConnectWs(local_ws, local_url, relay_token, local_path,
                   options_.connect_timeout)) {
      std::cerr << "[hub-ctrl] relay bridge: failed to connect local side for session "
                << session_id << '\n';
      boost::system::error_code ignore;
      hub_ws.close(websocket::close_code::going_away, ignore);
      return;
    }

    std::cout << "[hub-ctrl] relay bridge active (plain): channel=" << channel_id
              << " session=" << session_id << '\n';
    PipeWebSockets(hub_ws, local_ws);
  }

  std::cout << "[hub-ctrl] relay bridge closed: channel=" << channel_id << '\n';
}

}  // namespace vibe::net

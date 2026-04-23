#include "vibe/net/hub_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"

namespace vibe::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = asio::ip::tcp;

namespace {

constexpr auto kHeartbeatInterval = std::chrono::seconds(30);
constexpr auto kRequestTimeout = std::chrono::seconds(5);

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
};

auto ParseUrl(const std::string& url) -> std::optional<ParsedUrl> {
  ParsedUrl result;
  std::string remainder;

  if (url.starts_with("https://")) {
    result.scheme = "https";
    result.port = "443";
    remainder = url.substr(8);
  } else if (url.starts_with("http://")) {
    result.scheme = "http";
    result.port = "80";
    remainder = url.substr(7);
  } else {
    return std::nullopt;
  }

  // Strip any path component — hub_url is always a bare base URL.
  const auto slash_pos = remainder.find('/');
  const std::string authority =
      (slash_pos == std::string::npos) ? remainder : remainder.substr(0, slash_pos);

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

// Sends the request and reads the response over `stream`. The stream must
// already be connected (and for HTTPS, the TLS handshake must be complete).
template <typename Stream>
auto DoPost(Stream& stream, const std::string& host, const std::string& target,
            const std::string& bearer_token, const std::string& body) -> bool {
  boost::system::error_code ec;

  http::request<http::string_body> req{http::verb::post, target, 11};
  req.set(http::field::host, host);
  req.set(http::field::authorization, "Bearer " + bearer_token);
  req.set(http::field::content_type, "application/json");
  req.body() = body;
  req.prepare_payload();

  http::write(stream, req, ec);
  if (ec) {
    std::cerr << "[hub] write failed: " << ec.message() << '\n';
    return false;
  }

  beast::flat_buffer buf;
  http::response<http::string_body> res;
  http::read(stream, buf, res, ec);
  if (ec && ec != http::error::end_of_stream) {
    std::cerr << "[hub] read failed: " << ec.message() << '\n';
    return false;
  }

  if (res.result_int() < 200 || res.result_int() >= 300) {
    std::cerr << "[hub] heartbeat returned HTTP " << res.result_int() << '\n';
    return false;
  }
  return true;
}

auto PerformPost(const ParsedUrl& url, const std::string& bearer_token,
                 const std::string& target, const std::string& body) -> bool {
  boost::system::error_code ec;
  asio::io_context ioc;
  tcp::resolver resolver(ioc);

  const auto results = resolver.resolve(url.host, url.port, ec);
  if (ec) {
    std::cerr << "[hub] resolve " << url.host << ":" << url.port
              << " failed: " << ec.message() << '\n';
    return false;
  }

  if (url.scheme == "https") {
    asio::ssl::context ssl_ctx{asio::ssl::context::tls_client};
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(asio::ssl::verify_peer);

    beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str())) {
      std::cerr << "[hub] failed to set SNI hostname\n";
      return false;
    }
    beast::get_lowest_layer(stream).expires_after(kRequestTimeout);
    beast::get_lowest_layer(stream).connect(results, ec);
    if (ec) {
      std::cerr << "[hub] connect failed: " << ec.message() << '\n';
      return false;
    }
    stream.handshake(asio::ssl::stream_base::client, ec);
    if (ec) {
      std::cerr << "[hub] TLS handshake failed: " << ec.message() << '\n';
      return false;
    }
    beast::get_lowest_layer(stream).expires_after(kRequestTimeout);
    const bool ok = DoPost(stream, url.host, target, bearer_token, body);
    stream.shutdown(ec);  // TLS close-notify; ignore errors
    return ok;
  }

  // Plain HTTP.
  beast::tcp_stream stream(ioc);
  stream.expires_after(kRequestTimeout);
  stream.connect(results, ec);
  if (ec) {
    std::cerr << "[hub] connect failed: " << ec.message() << '\n';
    return false;
  }
  const bool ok = DoPost(stream, url.host, target, bearer_token, body);
  boost::system::error_code shutdown_ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, shutdown_ec);
  return ok;
}

}  // namespace

HubClient::HubClient(std::string hub_url, std::string hub_token,
                     vibe::service::SessionManager& session_manager)
    : hub_url_(std::move(hub_url)),
      hub_token_(std::move(hub_token)),
      session_manager_(session_manager) {}

HubClient::~HubClient() {
  Stop();
}

void HubClient::Start() {
  {
    std::lock_guard lock(mutex_);
    stop_ = false;
  }
  thread_ = std::thread(&HubClient::RunLoop, this);
}

void HubClient::Stop() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void HubClient::RunLoop() {
  SendHeartbeat();
  while (true) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, kHeartbeatInterval, [this] { return stop_; });
    if (stop_) {
      break;
    }
    lock.unlock();
    SendHeartbeat();
  }
}

void HubClient::SendHeartbeat() {
  const auto parsed = ParseUrl(hub_url_);
  if (!parsed.has_value()) {
    std::cerr << "[hub] invalid hub_url: " << hub_url_ << '\n';
    return;
  }

  const auto sessions = session_manager_.ListSessions();

  json::array sessions_array;
  for (const auto& s : sessions) {
    json::object obj;
    obj["session_id"] = s.id.value();
    obj["title"] = s.title;
    obj["lifecycle_state"] = std::string(vibe::session::ToString(s.status));
    obj["attention_state"] = std::string(vibe::session::ToString(s.attention_state));
    obj["attention_reason"] = std::string(vibe::session::ToString(s.attention_reason));
    sessions_array.push_back(std::move(obj));
  }

  json::object payload;
  payload["sessions"] = std::move(sessions_array);
  const std::string body = json::serialize(payload);

  if (!PerformPost(*parsed, hub_token_, "/api/v1/hosts/heartbeat", body)) {
    std::cerr << "[hub] heartbeat failed (will retry in "
              << kHeartbeatInterval.count() << "s)\n";
  }
}

}  // namespace vibe::net

#include "vibe/net/http_server.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <unistd.h>

#include "vibe/base/debug_trace.h"
#include "vibe/net/discovery_broadcaster.h"
#include "vibe/net/http_shared.h"
#include "vibe/net/json.h"
#include "vibe/net/local_auth.h"
#include "vibe/net/request_parsing.h"
#include "vibe/net/websocket_shared.h"

namespace vibe::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace json = boost::json;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using local_stream = asio::local::stream_protocol;

namespace {

class WebSocketTraceLogger {
 public:
  static auto Instance() -> WebSocketTraceLogger& {
    static WebSocketTraceLogger instance;
    return instance;
  }

  void Log(const std::string_view event, const std::string_view session_id, const std::size_t value = 0) {
    if (output_ == nullptr) {
      return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_)
            .count();
    std::lock_guard<std::mutex> lock(mutex_);
    (*output_) << elapsed << ' ' << session_id << ' ' << event << ' ' << value << '\n';
    output_->flush();
  }

 private:
  WebSocketTraceLogger() {
    const char* path = std::getenv("VIBE_WS_TRACE_PATH");
    if (path == nullptr || *path == '\0') {
      return;
    }

    output_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (output_ == nullptr || !output_->is_open()) {
      output_.reset();
      return;
    }
    start_time_ = std::chrono::steady_clock::now();
  }

  std::unique_ptr<std::ofstream> output_;
  std::chrono::steady_clock::time_point start_time_{};
  std::mutex mutex_;
};

constexpr auto kMaintenancePollInterval = std::chrono::seconds(1);

using SslStream = beast::ssl_stream<tcp::socket>;

class WebSocketSessionBase;
using WebSocketRegistry = std::unordered_map<std::string, std::vector<std::weak_ptr<WebSocketSessionBase>>>;
using OverviewWebSocketRegistry = std::vector<std::weak_ptr<WebSocketSessionBase>>;

struct EffectiveRemoteTlsConfig {
  bool enabled{false};
  std::string certificate_pem_path;
  std::string private_key_pem_path;
};

auto LoadConfiguredIdentity(const vibe::store::HostConfigStore& host_config_store)
    -> std::optional<vibe::store::HostIdentity> {
  return host_config_store.LoadHostIdentity();
}

auto ResolveRemoteTlsConfig(const vibe::store::HostConfigStore& host_config_store,
                            const std::optional<RemoteTlsFiles>& remote_tls_override)
    -> std::variant<EffectiveRemoteTlsConfig, std::string> {
  if (remote_tls_override.has_value()) {
    const bool has_certificate = !remote_tls_override->certificate_pem_path.empty();
    const bool has_key = !remote_tls_override->private_key_pem_path.empty();
    if (has_certificate != has_key) {
      return "invalid remote TLS override: both --remote-cert and --remote-key are required";
    }
    if (!has_certificate) {
      return EffectiveRemoteTlsConfig{};
    }

    return EffectiveRemoteTlsConfig{
        .enabled = true,
        .certificate_pem_path = remote_tls_override->certificate_pem_path,
        .private_key_pem_path = remote_tls_override->private_key_pem_path,
    };
  }

  const auto identity = LoadConfiguredIdentity(host_config_store);
  if (!identity.has_value()) {
    return EffectiveRemoteTlsConfig{};
  }

  const bool has_certificate = !identity->certificate_pem_path.empty();
  const bool has_key = !identity->private_key_pem_path.empty();
  if (has_certificate != has_key) {
    return "invalid persisted remote TLS config: both certificatePemPath and privateKeyPemPath are required";
  }
  if (!has_certificate) {
    return EffectiveRemoteTlsConfig{};
  }

  return EffectiveRemoteTlsConfig{
      .enabled = true,
      .certificate_pem_path = identity->certificate_pem_path,
      .private_key_pem_path = identity->private_key_pem_path,
  };
}

auto MakeServerTlsContext(const EffectiveRemoteTlsConfig& config)
    -> std::variant<std::shared_ptr<ssl::context>, std::string> {
  if (!config.enabled) {
    return std::make_shared<ssl::context>(ssl::context::tls_server);
  }

  if (!std::filesystem::exists(config.certificate_pem_path)) {
    return "remote TLS certificate file not found: " + config.certificate_pem_path;
  }
  if (!std::filesystem::exists(config.private_key_pem_path)) {
    return "remote TLS private key file not found: " + config.private_key_pem_path;
  }

  try {
    auto context = std::make_shared<ssl::context>(ssl::context::tls_server);
    context->set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                         ssl::context::no_sslv3 | ssl::context::single_dh_use);
    context->use_certificate_chain_file(config.certificate_pem_path);
    context->use_private_key_file(config.private_key_pem_path, ssl::context::file_format::pem);
    return context;
  } catch (const std::exception& exception) {
    return "failed to load remote TLS certificate/key: " + std::string(exception.what());
  }
}

auto GetRemoteEndpoint(const tcp::socket& socket) -> tcp::endpoint {
  return socket.remote_endpoint();
}

auto GetRemoteEndpoint(const SslStream& stream) -> tcp::endpoint {
  return stream.next_layer().remote_endpoint();
}

auto CurrentUnixTimeMs() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

auto ToActivityStateLabel(const vibe::service::SessionSummary& summary) -> const char* {
  return vibe::session::ToString(summary.supervision_state).data();
}

auto AttachClientCounts(std::vector<vibe::service::SessionSummary> summaries,
                        const WebSocketRegistry& websocket_registry)
    -> std::vector<vibe::service::SessionSummary> {
  for (auto& summary : summaries) {
    const auto it = websocket_registry.find(summary.id.value());
    if (it == websocket_registry.end()) {
      summary.attached_client_count = 0;
      continue;
    }

    summary.attached_client_count = static_cast<std::size_t>(
        std::count_if(it->second.begin(), it->second.end(),
                      [](const std::weak_ptr<WebSocketSessionBase>& entry) { return !entry.expired(); }));
  }
  return summaries;
}

auto ExtractBearerToken(const HttpRequest& request) -> std::string {
  const auto it = request.base().find(http::field::authorization);
  if (it == request.base().end()) {
    return ExtractAccessTokenFromWebSocketTarget(std::string(request.target()));
  }

  const std::string header = std::string(it->value());
  constexpr std::string_view prefix = "Bearer ";
  if (!header.starts_with(prefix)) {
    return ExtractAccessTokenFromWebSocketTarget(std::string(request.target()));
  }

  return header.substr(prefix.size());
}

auto MakeWebSocketAuthResponse(const HttpRequest& request, const http::status status,
                               const std::string& message) -> HttpResponse {
  HttpResponse response{status, request.version()};
  response.keep_alive(false);
  response.set(http::field::content_type, "application/json; charset=utf-8");
  response.body() = "{\"error\":\"" + JsonEscape(message) + "\"}";
  response.prepare_payload();
  return response;
}

auto MakeControllerReadyEvent(const std::string& session_id,
                              const std::string& controller_client_id) -> std::string {
  json::object object;
  object["type"] = "controller.ready";
  object["sessionId"] = session_id;
  object["controllerKind"] = "remote";
  object["controllerClientId"] = controller_client_id;
  return json::serialize(object);
}

auto MakeControllerRejectedEvent(const std::string& session_id, const std::string& code,
                                 const std::string& message) -> std::string {
  json::object object;
  object["type"] = "controller.rejected";
  object["sessionId"] = session_id;
  object["code"] = code;
  object["message"] = message;
  return json::serialize(object);
}

auto MakeControllerReleasedEvent(const std::string& session_id) -> std::string {
  json::object object;
  object["type"] = "controller.released";
  object["sessionId"] = session_id;
  return json::serialize(object);
}

void PruneRegistry(WebSocketRegistry& websocket_registry) {
  for (auto registry_it = websocket_registry.begin(); registry_it != websocket_registry.end();) {
    auto& sessions = registry_it->second;
    sessions.erase(
        std::remove_if(sessions.begin(), sessions.end(),
                       [](const std::weak_ptr<WebSocketSessionBase>& entry) { return entry.expired(); }),
        sessions.end());
    if (sessions.empty()) {
      registry_it = websocket_registry.erase(registry_it);
    } else {
      ++registry_it;
    }
  }
}

void PruneOverviewRegistry(OverviewWebSocketRegistry& websocket_registry) {
  websocket_registry.erase(
      std::remove_if(websocket_registry.begin(), websocket_registry.end(),
                     [](const std::weak_ptr<WebSocketSessionBase>& entry) { return entry.expired(); }),
      websocket_registry.end());
}

class WebSocketSessionBase {
 public:
  virtual ~WebSocketSessionBase() = default;

  virtual void Start(HttpRequest request) = 0;
  virtual void SendPendingOutput() = 0;
  virtual void ForceClose() = 0;

  [[nodiscard]] virtual auto session_id() const -> const std::string& = 0;
  [[nodiscard]] virtual auto client_id() const -> const std::string& = 0;
  [[nodiscard]] virtual auto client_address() const -> const std::string& = 0;
  [[nodiscard]] virtual auto is_local_request() const -> bool = 0;
  [[nodiscard]] virtual auto claimed_kind() const -> vibe::session::ControllerKind = 0;
  [[nodiscard]] virtual auto connected_at_unix_ms() const -> std::int64_t = 0;
};

template <typename Stream>
class OverviewWebSocketSession final : public WebSocketSessionBase,
                                       public std::enable_shared_from_this<OverviewWebSocketSession<Stream>> {
 public:
  OverviewWebSocketSession(Stream&& stream, vibe::service::SessionManager& session_manager,
                           const vibe::auth::Authorizer& authorizer,
                           std::shared_ptr<OverviewWebSocketRegistry> websocket_registry,
                           std::shared_ptr<WebSocketRegistry> session_websocket_registry,
                           const std::string& client_address, const bool is_local_request,
                           const ListenerRole listener_role)
      : websocket_(std::forward<Stream>(stream)),
        session_manager_(session_manager),
        authorizer_(authorizer),
        websocket_registry_(std::move(websocket_registry)),
        session_websocket_registry_(std::move(session_websocket_registry)),
        client_address_(client_address),
        is_local_request_(is_local_request),
        listener_role_(listener_role) {}

  void Start(HttpRequest request) override {
    target_ = std::string(request.target());
    vibe::auth::AuthResult auth_result;
    if (listener_role_ == ListenerRole::AdminLocal && is_local_request_) {
      auth_result = vibe::auth::AuthResult{
          .authenticated = true,
          .authorized = true,
          .device_id = std::nullopt,
          .reason = "",
      };
    } else {
      auth_result = authorizer_.Authorize(
          vibe::auth::RequestContext{
              .bearer_token = ExtractBearerToken(request),
              .client_address = client_address_,
              .target = target_,
              .is_websocket = true,
              .is_local_request = is_local_request_,
          },
          vibe::auth::AuthorizationAction::ObserveSessions);
    }
    if (!auth_result.authorized) {
      beast::error_code error_code;
      http::write(websocket_.next_layer(),
                  MakeWebSocketAuthResponse(
                      request, auth_result.authenticated ? http::status::forbidden
                                                         : http::status::unauthorized,
                      auth_result.reason.empty() ? "request rejected" : auth_result.reason),
                  error_code);
      return;
    }

    websocket_.async_accept(
        request,
        [self = this->shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code) {
            return;
          }

          self->client_id_ = "ws_overview_" +
                             std::to_string(reinterpret_cast<std::uintptr_t>(self.get()));
          self->connected_at_unix_ms_ = CurrentUnixTimeMs();
          self->websocket_registry_->push_back(self);
          self->QueueInventorySnapshot();
          self->DoRead();
        });
  }

  void SendPendingOutput() override { QueueInventorySnapshot(); }
  [[nodiscard]] auto session_id() const -> const std::string& override { return empty_; }
  [[nodiscard]] auto client_id() const -> const std::string& override { return client_id_; }
  [[nodiscard]] auto client_address() const -> const std::string& override { return client_address_; }
  [[nodiscard]] auto is_local_request() const -> bool override { return is_local_request_; }
  [[nodiscard]] auto claimed_kind() const -> vibe::session::ControllerKind override {
    return vibe::session::ControllerKind::None;
  }
  [[nodiscard]] auto connected_at_unix_ms() const -> std::int64_t override {
    return connected_at_unix_ms_;
  }

  void ForceClose() override {
    if (closed_) {
      return;
    }
    closed_ = true;
    boost::system::error_code error_code;
    beast::get_lowest_layer(websocket_).cancel(error_code);
    error_code.clear();
    beast::get_lowest_layer(websocket_).close(error_code);
  }

 private:
  struct PendingFrame {
    std::string payload;
  };

  void QueueInventorySnapshot() {
    const auto summaries = AttachClientCounts(session_manager_.ListSessions(), *session_websocket_registry_);
    const std::string payload = ToJson(SessionInventoryEvent{.sessions = summaries});
    if (last_inventory_payload_.has_value() && *last_inventory_payload_ == payload) {
      return;
    }
    last_inventory_payload_ = payload;
    pending_frames_.push_back(PendingFrame{.payload = payload});
    if (!write_in_progress_) {
      DoWrite();
    }
  }

  void DoWrite() {
    if (pending_frames_.empty()) {
      write_in_progress_ = false;
      return;
    }
    write_in_progress_ = true;
    websocket_.text(true);
    websocket_.async_write(
        asio::buffer(pending_frames_.front().payload),
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            return;
          }
          self->pending_frames_.pop_front();
          self->DoWrite();
        });
  }

  void DoRead() {
    websocket_.async_read(
        read_buffer_,
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            return;
          }
          self->read_buffer_.consume(self->read_buffer_.size());
          self->DoRead();
        });
  }

  websocket::stream<Stream> websocket_;
  vibe::service::SessionManager& session_manager_;
  const vibe::auth::Authorizer& authorizer_;
  std::shared_ptr<OverviewWebSocketRegistry> websocket_registry_;
  std::shared_ptr<WebSocketRegistry> session_websocket_registry_;
  beast::flat_buffer read_buffer_;
  std::deque<PendingFrame> pending_frames_;
  std::string target_;
  std::string client_id_;
  std::string client_address_;
  std::optional<std::string> last_inventory_payload_;
  bool write_in_progress_{false};
  bool closed_{false};
  bool is_local_request_{false};
  ListenerRole listener_role_{ListenerRole::RemoteClient};
  std::int64_t connected_at_unix_ms_{0};
  const std::string empty_;
};

template <typename Stream>
class WebSocketSession final : public WebSocketSessionBase,
                               public std::enable_shared_from_this<WebSocketSession<Stream>> {
 public:
  ~WebSocketSession() { ReleaseControlIfHeld(); }

  WebSocketSession(Stream&& stream, vibe::service::SessionManager& session_manager,
                   const vibe::auth::Authorizer& authorizer,
                   std::shared_ptr<WebSocketRegistry> websocket_registry,
                   const std::string& client_address, const bool is_local_request,
                   const ListenerRole listener_role)
      : websocket_(std::forward<Stream>(stream)),
        session_manager_(session_manager),
        authorizer_(authorizer),
        websocket_registry_(std::move(websocket_registry)),
        client_address_(client_address),
        is_local_request_(is_local_request),
        listener_role_(listener_role) {}

  void Start(HttpRequest request) override {
    target_ = std::string(request.target());
    session_id_ = ExtractSessionIdFromWebSocketTarget(target_);
    vibe::auth::AuthResult auth_result;
    if (listener_role_ == ListenerRole::AdminLocal && is_local_request_) {
      auth_result = vibe::auth::AuthResult{
          .authenticated = true,
          .authorized = true,
          .device_id = std::nullopt,
          .reason = "",
      };
    } else {
      auth_result = authorizer_.Authorize(
          vibe::auth::RequestContext{
              .bearer_token = ExtractBearerToken(request),
              .client_address = client_address_,
              .target = target_,
              .is_websocket = true,
              .is_local_request = is_local_request_,
          },
          vibe::auth::AuthorizationAction::ObserveSessions);
    }
    if (!auth_result.authorized) {
      beast::error_code error_code;
      http::write(websocket_.next_layer(),
                  MakeWebSocketAuthResponse(
                      request, auth_result.authenticated ? http::status::forbidden
                                                         : http::status::unauthorized,
                      auth_result.reason.empty() ? "request rejected" : auth_result.reason),
                  error_code);
      ReleaseControlIfHeld();
      return;
    }

    websocket_.async_accept(
        request,
        [self = this->shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code) {
            self->ReleaseControlIfHeld();
            return;
          }

          self->client_id_ = "ws_" + self->session_id_ + "_" +
                             std::to_string(reinterpret_cast<std::uintptr_t>(self.get()));
          self->connected_at_unix_ms_ = CurrentUnixTimeMs();
          self->raw_output_mode_ = IsRawTerminalStreamRequested(self->target_);
          self->sequence_window_ = {};
          if (const auto summary = self->session_manager_.GetSession(self->session_id_); summary.has_value()) {
            const bool viewport_updated = self->session_manager_.UpdateViewport(
                self->session_id_, self->client_id_,
                vibe::session::TerminalSize{
                    .columns = summary->pty_columns.value_or(120),
                    .rows = summary->pty_rows.value_or(40),
                });
            static_cast<void>(viewport_updated);
          }
          boost::system::error_code socket_error;
          beast::get_lowest_layer(self->websocket_).set_option(tcp::no_delay(true), socket_error);
          (*self->websocket_registry_)[self->session_id_].push_back(self);
          self->QueueInitialEvents();
          self->DoRead();
        });
  }

  void SendPendingOutput() override {
    if (session_id_.empty()) {
      return;
    }

    QueueStatusEvents();

    const auto output =
        session_manager_.GetOutputSince(session_id_, sequence_window_.next_request_sequence());
    if (!output.has_value() || output->data.empty()) {
      return;
    }

    QueueOutputFrame(*output, output->seq_end + 1);
  }

  [[nodiscard]] auto session_id() const -> const std::string& override { return session_id_; }
  [[nodiscard]] auto client_id() const -> const std::string& override { return client_id_; }
  [[nodiscard]] auto client_address() const -> const std::string& override { return client_address_; }
  [[nodiscard]] auto is_local_request() const -> bool override { return is_local_request_; }
  [[nodiscard]] auto claimed_kind() const -> vibe::session::ControllerKind override {
    return claimed_kind_;
  }
  [[nodiscard]] auto connected_at_unix_ms() const -> std::int64_t override {
    return connected_at_unix_ms_;
  }

  void ForceClose() override {
    if (closed_) {
      return;
    }

    closed_ = true;
    boost::system::error_code error_code;
    beast::get_lowest_layer(websocket_).cancel(error_code);
    error_code.clear();
    beast::get_lowest_layer(websocket_).close(error_code);
    ReleaseControlIfHeld();
  }

 private:
  struct PendingFrame {
    std::string payload;
    std::uint64_t next_sequence{1};
    bool is_text{true};
  };

  void QueueInitialEvents() {
    if (initial_events_sent_) {
      return;
    }

    initial_events_sent_ = true;
    QueueStatusEvents();
    const auto tail = session_manager_.GetTail(session_id_, 64U * 1024U);
    const vibe::session::OutputSlice initial_slice = tail.value_or(vibe::session::OutputSlice{});
    const std::uint64_t next_sequence =
        initial_slice.seq_end > 0 ? initial_slice.seq_end + 1 : sequence_window_.delivered_next();
    QueueOutputFrame(initial_slice, next_sequence);
  }

  void QueueStatusEvents() {
    const auto summary = session_manager_.GetSession(session_id_);
    if (!summary.has_value()) {
      return;
    }

    const bool status_changed =
        !last_status_.has_value() || *last_status_ != summary->status ||
        last_group_tags_ != summary->group_tags ||
        last_controller_client_id_ != summary->controller_client_id ||
        last_controller_kind_ != summary->controller_kind;
    if (status_changed) {
      last_status_ = summary->status;
      last_group_tags_ = summary->group_tags;
      last_controller_client_id_ = summary->controller_client_id;
      last_controller_kind_ = summary->controller_kind;
      QueueFrame(ToJson(SessionUpdatedEvent{.summary = *summary}),
                 sequence_window_.next_request_sequence());
    }

    const bool activity_changed =
        !last_activity_state_.has_value() || *last_activity_state_ != std::string(ToActivityStateLabel(*summary)) ||
        !last_attention_state_.has_value() ||
        *last_attention_state_ != std::string(vibe::session::ToString(summary->attention_state)) ||
        !last_attention_reason_.has_value() ||
        *last_attention_reason_ != std::string(vibe::session::ToString(summary->attention_reason)) ||
        last_output_at_unix_ms_ != summary->last_output_at_unix_ms ||
        last_activity_at_unix_ms_ != summary->last_activity_at_unix_ms ||
        last_attention_since_unix_ms_ != summary->attention_since_unix_ms ||
        last_pty_columns_ != summary->pty_columns ||
        last_pty_rows_ != summary->pty_rows ||
        last_current_sequence_ != summary->current_sequence ||
        last_recent_file_change_count_ != summary->recent_file_change_count ||
        last_git_dirty_ != summary->git_dirty ||
        last_git_branch_ != summary->git_branch;
    if (activity_changed) {
      last_activity_state_ = std::string(ToActivityStateLabel(*summary));
      last_attention_state_ = std::string(vibe::session::ToString(summary->attention_state));
      last_attention_reason_ = std::string(vibe::session::ToString(summary->attention_reason));
      last_output_at_unix_ms_ = summary->last_output_at_unix_ms;
      last_activity_at_unix_ms_ = summary->last_activity_at_unix_ms;
      last_attention_since_unix_ms_ = summary->attention_since_unix_ms;
      last_pty_columns_ = summary->pty_columns;
      last_pty_rows_ = summary->pty_rows;
      last_current_sequence_ = summary->current_sequence;
      last_recent_file_change_count_ = summary->recent_file_change_count;
      last_git_dirty_ = summary->git_dirty;
      last_git_branch_ = summary->git_branch;
      if (!status_changed) {
        QueueFrame(ToJson(SessionActivityEvent{.summary = *summary}),
                   sequence_window_.next_request_sequence());
      }
    }

    if (!exit_event_sent_ &&
        (summary->status == vibe::session::SessionStatus::Exited ||
         summary->status == vibe::session::SessionStatus::Error)) {
      exit_event_sent_ = true;
      QueueFrame(ToJson(SessionExitedEvent{
                     .session_id = summary->id.value(),
                     .status = summary->status,
                 }),
                 sequence_window_.next_request_sequence());
    }
  }

  void QueueFrame(std::string payload, const std::uint64_t next_sequence) {
    QueueFrame(std::move(payload), next_sequence, true);
  }

  void QueueOutputFrame(const vibe::session::OutputSlice& slice, const std::uint64_t next_sequence) {
    if (raw_output_mode_) {
      if (slice.data.empty()) {
        return;
      }
      QueueFrame(std::string(slice.data), next_sequence, false);
      return;
    }

    QueueFrame(ToJson(TerminalOutputEvent{
                   .session_id = session_id_,
                   .slice = slice,
               }),
               next_sequence, true);
  }

  void QueueFrame(std::string payload, const std::uint64_t next_sequence, const bool is_text) {
    sequence_window_.ReserveThrough(next_sequence);
    pending_frames_.push_back(PendingFrame{
        .payload = std::move(payload),
        .next_sequence = next_sequence,
        .is_text = is_text,
    });

    if (!write_in_progress_) {
      DoWrite();
    }
  }

  void DoWrite() {
    if (pending_frames_.empty()) {
      write_in_progress_ = false;
      return;
    }

    write_in_progress_ = true;
    WebSocketTraceLogger::Instance().Log(pending_frames_.front().is_text ? "ws.write.text" : "ws.write.binary",
                                         session_id_, pending_frames_.front().payload.size());
    websocket_.text(pending_frames_.front().is_text);
    websocket_.async_write(
        asio::buffer(pending_frames_.front().payload),
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ReleaseControlIfHeld();
            return;
          }

          self->sequence_window_.MarkDelivered(self->pending_frames_.front().next_sequence);
          self->pending_frames_.pop_front();
          self->DoWrite();
        });
  }

  void DoRead() {
    websocket_.async_read(
        read_buffer_,
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ReleaseControlIfHeld();
            return;
          }

          const std::string payload = beast::buffers_to_string(self->read_buffer_.data());
          const bool got_text = self->websocket_.got_text();
          self->read_buffer_.consume(self->read_buffer_.size());
          if (got_text) {
            self->HandleClientCommand(payload);
          } else {
            self->HandleClientBinary(payload);
          }
          self->DoRead();
        });
  }

  void HandleClientBinary(const std::string& payload) {
    if (!raw_output_mode_) {
      QueueFrame(ToJson(ErrorEvent{
                     .session_id = session_id_,
                     .code = "invalid_command",
                     .message = "binary websocket frames are not supported for this session",
                 }),
                 sequence_window_.next_request_sequence());
      return;
    }

    if (!session_manager_.HasControl(session_id_, client_id_)) {
      QueueFrame(ToJson(ErrorEvent{
                     .session_id = session_id_,
                     .code = "command_rejected",
                     .message = "command rejected for current session state",
                 }),
                 sequence_window_.next_request_sequence());
      return;
    }

    if (!session_manager_.SendInput(session_id_, payload)) {
      QueueFrame(ToJson(ErrorEvent{
                     .session_id = session_id_,
                     .code = "command_rejected",
                     .message = "command rejected for current session state",
                 }),
                 sequence_window_.next_request_sequence());
      return;
    }
  }

  void HandleClientCommand(const std::string& payload) {
    const auto command = ParseWebSocketCommand(payload);
    if (!command.has_value()) {
      QueueFrame(ToJson(ErrorEvent{
                     .session_id = session_id_,
                     .code = "invalid_command",
                     .message = "invalid websocket command",
                 }),
                 sequence_window_.next_request_sequence());
      return;
    }

    const bool handled = std::visit(
        [this](const auto& value) -> bool {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, WebSocketInputCommand>) {
            if (!session_manager_.HasControl(session_id_, client_id_)) {
              return false;
            }
            return session_manager_.SendInput(session_id_, value.data);
          } else if constexpr (std::is_same_v<T, WebSocketResizeCommand>) {
            if (session_manager_.HasControl(session_id_, client_id_)) {
              return session_manager_.ResizeSession(session_id_, value.terminal_size);
            }
            return session_manager_.UpdateViewport(session_id_, client_id_, value.terminal_size);
          } else if constexpr (std::is_same_v<T, WebSocketStopCommand>) {
            if (!session_manager_.HasControl(session_id_, client_id_)) {
              return false;
            }
            return session_manager_.StopSession(session_id_);
          } else if constexpr (std::is_same_v<T, WebSocketRequestControlCommand>) {
            const bool granted =
                session_manager_.RequestControl(session_id_, client_id_, value.controller_kind);
            if (granted) {
              claimed_kind_ = value.controller_kind;
            }
            return granted;
          } else if constexpr (std::is_same_v<T, WebSocketReleaseControlCommand>) {
            const bool released = session_manager_.ReleaseControl(session_id_, client_id_);
            if (released) {
              claimed_kind_ = vibe::session::ControllerKind::None;
            }
            return released;
          }

          return false;
        },
        *command);

    if (handled) {
      QueueStatusEvents();
    }

    if (!handled) {
      QueueFrame(ToJson(ErrorEvent{
                     .session_id = session_id_,
                     .code = "command_rejected",
                     .message = "command rejected for current session state",
                 }),
                 sequence_window_.next_request_sequence());
    }
  }

  void ReleaseControlIfHeld() {
    if (session_id_.empty() || client_id_.empty()) {
      return;
    }

    const bool released = session_manager_.ReleaseControl(session_id_, client_id_);
    static_cast<void>(released);
    session_manager_.RemoveViewport(session_id_, client_id_);
  }

  websocket::stream<Stream> websocket_;
  vibe::service::SessionManager& session_manager_;
  const vibe::auth::Authorizer& authorizer_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  beast::flat_buffer read_buffer_;
  std::deque<PendingFrame> pending_frames_;
  std::string target_;
  std::string session_id_;
  std::string client_id_;
  std::string client_address_;
  StreamSequenceWindow sequence_window_;
  std::optional<vibe::session::SessionStatus> last_status_;
  std::vector<std::string> last_group_tags_;
  std::optional<std::string> last_controller_client_id_;
  vibe::session::ControllerKind last_controller_kind_{vibe::session::ControllerKind::None};
  std::optional<std::string> last_activity_state_;
  std::optional<std::string> last_attention_state_;
  std::optional<std::string> last_attention_reason_;
  std::optional<std::int64_t> last_output_at_unix_ms_;
  std::optional<std::int64_t> last_activity_at_unix_ms_;
  std::optional<std::int64_t> last_attention_since_unix_ms_;
  std::optional<std::uint16_t> last_pty_columns_;
  std::optional<std::uint16_t> last_pty_rows_;
  std::uint64_t last_current_sequence_{0};
  std::size_t last_recent_file_change_count_{0};
  bool last_git_dirty_{false};
  std::string last_git_branch_;
  vibe::session::ControllerKind claimed_kind_{vibe::session::ControllerKind::None};
  bool initial_events_sent_{false};
  bool exit_event_sent_{false};
  bool write_in_progress_{false};
  bool closed_{false};
  bool raw_output_mode_{false};
  bool is_local_request_{false};
  ListenerRole listener_role_{ListenerRole::RemoteClient};
  std::int64_t connected_at_unix_ms_{0};
};

template <typename Stream>
class RemoteControllerWebSocketSession final
    : public WebSocketSessionBase,
      public std::enable_shared_from_this<RemoteControllerWebSocketSession<Stream>> {
 public:
  ~RemoteControllerWebSocketSession() { ReleaseControlIfHeld(); }

  RemoteControllerWebSocketSession(Stream&& stream, vibe::service::SessionManager& session_manager,
                                   const vibe::auth::Authorizer& authorizer,
                                   std::shared_ptr<WebSocketRegistry> websocket_registry,
                                   const std::string& client_address, const bool is_local_request,
                                   const ListenerRole listener_role)
      : websocket_(std::forward<Stream>(stream)),
        session_manager_(session_manager),
        authorizer_(authorizer),
        websocket_registry_(std::move(websocket_registry)),
        client_address_(client_address),
        is_local_request_(is_local_request),
        listener_role_(listener_role) {}

  void Start(HttpRequest request) override {
    target_ = std::string(request.target());
    session_id_ = ExtractSessionIdFromWebSocketTarget(target_);
    const auto auth_result = authorizer_.Authorize(
        vibe::auth::RequestContext{
            .bearer_token = ExtractBearerToken(request),
            .client_address = client_address_,
            .target = target_,
            .is_websocket = true,
            .is_local_request = is_local_request_,
        },
        vibe::auth::AuthorizationAction::ControlSession);
    if (!auth_result.authorized) {
      beast::error_code error_code;
      http::write(websocket_.next_layer(),
                  MakeWebSocketAuthResponse(
                      request, auth_result.authenticated ? http::status::forbidden
                                                         : http::status::unauthorized,
                      auth_result.reason.empty() ? "request rejected" : auth_result.reason),
                  error_code);
      return;
    }

    websocket_.async_accept(
        request,
        [self = this->shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code) {
            return;
          }

          self->client_id_ = "remote-controller-" + std::to_string(CurrentUnixTimeMs()) + "-" +
                             std::to_string(reinterpret_cast<std::uintptr_t>(self.get()));
          self->connected_at_unix_ms_ = CurrentUnixTimeMs();
          boost::system::error_code socket_error;
          beast::get_lowest_layer(self->websocket_).set_option(tcp::no_delay(true), socket_error);
          if (socket_error) {
            self->ForceClose();
            return;
          }

          const auto summary = self->session_manager_.GetSession(self->session_id_);
          if (!summary.has_value()) {
            self->QueueTextFrame(
                MakeControllerRejectedEvent(self->session_id_, "session_not_found", "session not found"));
            self->close_after_writes_ = true;
            return;
          }

          if (!self->session_manager_.RequestControl(self->session_id_, self->client_id_,
                                                     vibe::session::ControllerKind::Remote)) {
            self->QueueTextFrame(MakeControllerRejectedEvent(
                self->session_id_, "control_unavailable", "session already has an active controller"));
            self->close_after_writes_ = true;
            return;
          }

          self->has_control_ = true;
          (*self->websocket_registry_)[self->session_id_].push_back(self);
          self->next_output_sequence_ = summary->current_sequence + 1;
          self->QueueTextFrame(MakeControllerReadyEvent(self->session_id_, self->client_id_));
          self->DoRead();
          self->SyncReadableDescriptor();
          self->ArmReadableWait();
        });
  }

  void SendPendingOutput() override {
    if (closed_ || session_id_.empty()) {
      return;
    }

    const auto summary = session_manager_.GetSession(session_id_);
    if (!summary.has_value()) {
      close_after_writes_ = true;
      if (!write_in_progress_) {
        ForceClose();
      }
      return;
    }

    if (has_control_ && !session_manager_.HasControl(session_id_, client_id_)) {
      has_control_ = false;
      QueueTextFrame(MakeControllerReleasedEvent(session_id_));
      close_after_writes_ = true;
      return;
    }

    if (!exit_event_sent_ &&
        (summary->status == vibe::session::SessionStatus::Exited ||
         summary->status == vibe::session::SessionStatus::Error)) {
      exit_event_sent_ = true;
      QueueTextFrame(ToJson(SessionExitedEvent{
          .session_id = summary->id.value(),
          .status = summary->status,
      }));
      close_after_writes_ = true;
    }
  }

  [[nodiscard]] auto session_id() const -> const std::string& override { return session_id_; }
  [[nodiscard]] auto client_id() const -> const std::string& override { return client_id_; }
  [[nodiscard]] auto client_address() const -> const std::string& override { return client_address_; }
  [[nodiscard]] auto is_local_request() const -> bool override { return is_local_request_; }
  [[nodiscard]] auto claimed_kind() const -> vibe::session::ControllerKind override {
    return vibe::session::ControllerKind::Remote;
  }
  [[nodiscard]] auto connected_at_unix_ms() const -> std::int64_t override {
    return connected_at_unix_ms_;
  }

  void ForceClose() override {
    if (closed_) {
      return;
    }

    closed_ = true;
    boost::system::error_code error_code;
    if (pty_readable_) {
      pty_readable_->cancel(error_code);
      pty_readable_->close(error_code);
      pty_readable_.reset();
    }
    beast::get_lowest_layer(websocket_).cancel(error_code);
    error_code.clear();
    beast::get_lowest_layer(websocket_).close(error_code);
    ReleaseControlIfHeld();
  }

 private:
  struct PendingFrame {
    std::string payload;
    bool is_text{true};
  };

  void DoRead() {
    websocket_.async_read(
        read_buffer_,
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ForceClose();
            return;
          }

          const std::string payload = beast::buffers_to_string(self->read_buffer_.data());
          const bool got_text = self->websocket_.got_text();
          self->read_buffer_.consume(self->read_buffer_.size());
          if (got_text) {
            self->HandleTextFrame(payload);
          } else {
            self->HandleBinaryFrame(payload);
          }

          if (!self->closed_) {
            self->DoRead();
          }
        });
  }

  void HandleBinaryFrame(const std::string& payload) {
    if (!has_control_ || !session_manager_.HasControl(session_id_, client_id_)) {
      QueueTextFrame(MakeControllerReleasedEvent(session_id_));
      close_after_writes_ = true;
      return;
    }

    if (!session_manager_.SendInput(session_id_, payload)) {
      close_after_writes_ = true;
    }
  }

  void HandleTextFrame(const std::string& payload) {
    const auto command = ParseWebSocketCommand(payload);
    if (!command.has_value()) {
      ForceClose();
      return;
    }

    const bool handled = std::visit(
        [this](const auto& value) -> bool {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, WebSocketResizeCommand>) {
            return session_manager_.ResizeSession(session_id_, value.terminal_size);
          } else if constexpr (std::is_same_v<T, WebSocketStopCommand>) {
            return session_manager_.StopSession(session_id_);
          } else if constexpr (std::is_same_v<T, WebSocketReleaseControlCommand>) {
            const bool released = session_manager_.ReleaseControl(session_id_, client_id_);
            if (released) {
              has_control_ = false;
              QueueTextFrame(MakeControllerReleasedEvent(session_id_));
              close_after_writes_ = true;
            }
            return released;
          }

          return false;
        },
        *command);

    if (!handled) {
      ForceClose();
    }
  }

  void SyncReadableDescriptor() {
    const auto readable_fd = session_manager_.GetReadableFd(session_id_);
    if (!readable_fd.has_value()) {
      pty_readable_.reset();
      return;
    }

    if (pty_readable_) {
      return;
    }

    const int duplicated_fd = dup(*readable_fd);
    if (duplicated_fd < 0) {
      return;
    }

    pty_readable_ = std::make_unique<asio::posix::stream_descriptor>(websocket_.get_executor());
    pty_readable_->assign(duplicated_fd);
  }

  void ArmReadableWait() {
    if (!pty_readable_ || closed_) {
      return;
    }

    pty_readable_->async_wait(
        asio::posix::stream_descriptor::wait_read,
        [self = this->shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code == asio::error::operation_aborted) {
            return;
          }

          if (error_code) {
            self->ForceClose();
            return;
          }

          static_cast<void>(self->session_manager_.PollSession(self->session_id_, 0));
          self->FlushOutput();
          self->SendPendingOutput();

          self->SyncReadableDescriptor();
          if (self->pty_readable_ != nullptr && !self->closed_) {
            self->ArmReadableWait();
          } else if (self->close_after_writes_ && !self->write_in_progress_ &&
                     self->pending_frames_.empty()) {
            self->ForceClose();
          }
        });
  }

  void FlushOutput() {
    const auto slice = session_manager_.GetOutputSince(session_id_, next_output_sequence_);
    if (!slice.has_value() || slice->data.empty()) {
      return;
    }

    next_output_sequence_ = slice->seq_end + 1;
    QueueBinaryFrame(std::string(slice->data));
  }

  void QueueTextFrame(std::string payload) {
    QueueFrame(std::move(payload), true);
  }

  void QueueBinaryFrame(std::string payload) {
    QueueFrame(std::move(payload), false);
  }

  void QueueFrame(std::string payload, const bool is_text) {
    if (payload.empty() || closed_) {
      return;
    }

    pending_frames_.push_back(PendingFrame{
        .payload = std::move(payload),
        .is_text = is_text,
    });
    if (!write_in_progress_) {
      DoWrite();
    }
  }

  void DoWrite() {
    if (pending_frames_.empty() || closed_) {
      write_in_progress_ = false;
      if (close_after_writes_) {
        ForceClose();
      }
      return;
    }

    write_in_progress_ = true;
    websocket_.text(pending_frames_.front().is_text);
    websocket_.async_write(
        asio::buffer(pending_frames_.front().payload),
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ForceClose();
            return;
          }

          self->pending_frames_.pop_front();
          self->DoWrite();
        });
  }

  void ReleaseControlIfHeld() {
    if (!has_control_) {
      return;
    }
    const bool released = session_manager_.ReleaseControl(session_id_, client_id_);
    static_cast<void>(released);
    has_control_ = false;
  }

  websocket::stream<Stream> websocket_;
  vibe::service::SessionManager& session_manager_;
  const vibe::auth::Authorizer& authorizer_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  beast::flat_buffer read_buffer_;
  std::deque<PendingFrame> pending_frames_;
  std::unique_ptr<asio::posix::stream_descriptor> pty_readable_;
  std::string target_;
  std::string session_id_;
  std::string client_id_;
  std::string client_address_;
  std::uint64_t next_output_sequence_{1};
  bool has_control_{false};
  bool write_in_progress_{false};
  bool close_after_writes_{false};
  bool exit_event_sent_{false};
  bool closed_{false};
  bool is_local_request_{false};
  ListenerRole listener_role_{ListenerRole::RemoteClient};
  std::int64_t connected_at_unix_ms_{0};
};

void CloseAllWebSockets(WebSocketRegistry& websocket_registry) {
  PruneRegistry(websocket_registry);
  for (auto& [session_id, sessions] : websocket_registry) {
    static_cast<void>(session_id);
    for (const auto& entry : sessions) {
      if (const auto session = entry.lock()) {
        session->ForceClose();
      }
    }
  }
}

void CloseAllOverviewWebSockets(OverviewWebSocketRegistry& websocket_registry) {
  PruneOverviewRegistry(websocket_registry);
  for (const auto& entry : websocket_registry) {
    if (const auto session = entry.lock()) {
      session->ForceClose();
    }
  }
}

class HostAdminService final : public vibe::net::HostAdmin {
 public:
  HostAdminService(vibe::service::SessionManager& session_manager, WebSocketRegistry& websocket_registry)
      : session_manager_(session_manager), websocket_registry_(&websocket_registry) {}

  [[nodiscard]] auto ListAttachedClients() const -> std::vector<vibe::net::AttachedClientInfo> override {
    std::vector<vibe::net::AttachedClientInfo> clients;

    PruneRegistry(*websocket_registry_);
    for (const auto& [session_id, sessions] : *websocket_registry_) {
      const auto summary = session_manager_.GetSession(session_id);
      const auto controller_client_id = summary.has_value() ? summary->controller_client_id : std::nullopt;

      for (const auto& entry : sessions) {
        if (const auto session = entry.lock()) {
          clients.push_back(vibe::net::AttachedClientInfo{
              .client_id = session->client_id(),
              .session_id = session->session_id(),
              .session_title = summary.has_value() ? summary->title : "",
              .client_address = session->client_address(),
              .session_status = summary.has_value() ? summary->status
                                                    : vibe::session::SessionStatus::Created,
              .session_is_recovered = summary.has_value() && summary->is_recovered,
              .claimed_kind = session->claimed_kind(),
              .is_local = session->is_local_request(),
              .has_control = controller_client_id.has_value() && *controller_client_id == session->client_id(),
              .connected_at_unix_ms = session->connected_at_unix_ms(),
          });
        }
      }
    }

    std::sort(clients.begin(), clients.end(),
              [](const vibe::net::AttachedClientInfo& left, const vibe::net::AttachedClientInfo& right) {
                if (left.session_id != right.session_id) {
                  return left.session_id < right.session_id;
                }
                return left.client_id < right.client_id;
              });
    return clients;
  }

  [[nodiscard]] auto DisconnectClient(const std::string& client_id) -> bool override {
    PruneRegistry(*websocket_registry_);
    for (auto& [session_id, sessions] : *websocket_registry_) {
      static_cast<void>(session_id);
      for (const auto& entry : sessions) {
        if (const auto session = entry.lock()) {
          if (session->client_id() == client_id) {
            session->ForceClose();
            return true;
          }
        }
      }
    }
    return false;
  }

 private:
  vibe::service::SessionManager& session_manager_;
  WebSocketRegistry* websocket_registry_;
};

class SessionPump : public std::enable_shared_from_this<SessionPump> {
 public:
  SessionPump(asio::io_context& io_context, vibe::service::SessionManager& session_manager,
              std::shared_ptr<WebSocketRegistry> websocket_registry,
              std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry)
      : poll_timer_(io_context),
        session_manager_(session_manager),
        websocket_registry_(std::move(websocket_registry)),
        overview_websocket_registry_(std::move(overview_websocket_registry)) {}

  void Start() { DoPoll(); }
  void Stop() {
    stopped_ = true;
    const std::size_t canceled = poll_timer_.cancel();
    static_cast<void>(canceled);
  }

 private:
  void NotifySessionClients(const std::string& session_id) {
    const auto registry_it = websocket_registry_->find(session_id);
    if (registry_it == websocket_registry_->end()) {
      return;
    }

    auto& sessions = registry_it->second;
    for (auto it = sessions.begin(); it != sessions.end();) {
      if (const auto session = it->lock()) {
        session->SendPendingOutput();
        ++it;
      } else {
        it = sessions.erase(it);
      }
    }
  }

  void SyncReadableWatchers() {
    std::unordered_set<std::string> active_session_ids;
    for (auto it = websocket_registry_->begin(); it != websocket_registry_->end();) {
      auto& sessions = it->second;
      for (auto session_it = sessions.begin(); session_it != sessions.end();) {
        if (session_it->expired()) {
          session_it = sessions.erase(session_it);
        } else {
          ++session_it;
        }
      }

      if (sessions.empty()) {
        it = websocket_registry_->erase(it);
        continue;
      }

      if (session_manager_.HasPrivilegedController(it->first)) {
        ++it;
        continue;
      }

      active_session_ids.insert(it->first);
      ++it;
    }

    for (const auto& session_id : active_session_ids) {
      const auto readable_fd = session_manager_.GetReadableFd(session_id);
      if (!readable_fd.has_value()) {
        readable_watchers_.erase(session_id);
        continue;
      }

      if (readable_watchers_.contains(session_id)) {
        continue;
      }

      const int duplicated_fd = dup(*readable_fd);
      if (duplicated_fd < 0) {
        continue;
      }

      auto descriptor =
          std::make_unique<asio::posix::stream_descriptor>(poll_timer_.get_executor());
      descriptor->assign(duplicated_fd);
      readable_watchers_.emplace(session_id, std::move(descriptor));
      ArmReadableWait(session_id);
    }

    for (auto it = readable_watchers_.begin(); it != readable_watchers_.end();) {
      if (!active_session_ids.contains(it->first) ||
          !session_manager_.GetReadableFd(it->first).has_value()) {
        it = readable_watchers_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void ArmReadableWait(const std::string& session_id) {
    const auto watcher_it = readable_watchers_.find(session_id);
    if (watcher_it == readable_watchers_.end()) {
      return;
    }

    watcher_it->second->async_wait(
        asio::posix::stream_descriptor::wait_read,
        [self = shared_from_this(), session_id](const boost::system::error_code& error_code) {
          if (error_code == asio::error::operation_aborted) {
            return;
          }

          if (!self->readable_watchers_.contains(session_id)) {
            return;
          }

          if (error_code) {
            self->readable_watchers_.erase(session_id);
            return;
          }

          static_cast<void>(self->session_manager_.PollSession(session_id, 0));
          self->NotifySessionClients(session_id);

          if (self->session_manager_.GetReadableFd(session_id).has_value() &&
              self->readable_watchers_.contains(session_id)) {
            self->ArmReadableWait(session_id);
          } else {
            self->readable_watchers_.erase(session_id);
          }
        });
  }

  void DoPoll() {
    if (stopped_) {
      return;
    }

    SyncReadableWatchers();
    poll_timer_.expires_after(kMaintenancePollInterval);
    poll_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error_code) {
      if (error_code == asio::error::operation_aborted) {
        return;
      }
      if (error_code) {
        std::cerr << "poll timer failed: " << error_code.message() << '\n';
        self->DoPoll();
        return;
      }

      self->session_manager_.PollAll(0);
      for (auto& [session_id, sessions] : *self->websocket_registry_) {
        static_cast<void>(session_id);
        for (auto it = sessions.begin(); it != sessions.end();) {
          if (const auto session = it->lock()) {
            session->SendPendingOutput();
            ++it;
          } else {
            it = sessions.erase(it);
          }
        }
      }
      for (auto it = self->overview_websocket_registry_->begin();
           it != self->overview_websocket_registry_->end();) {
        if (const auto session = it->lock()) {
          session->SendPendingOutput();
          ++it;
        } else {
          it = self->overview_websocket_registry_->erase(it);
        }
      }
      self->DoPoll();
    });
  }

  asio::steady_timer poll_timer_;
  vibe::service::SessionManager& session_manager_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry_;
  std::unordered_map<std::string, std::unique_ptr<asio::posix::stream_descriptor>> readable_watchers_;
  bool stopped_{false};
};

template <typename Stream>
class HttpSession : public std::enable_shared_from_this<HttpSession<Stream>> {
 public:
  HttpSession(Stream&& stream, vibe::service::SessionManager& session_manager,
              const vibe::auth::Authorizer& authorizer,
              vibe::auth::PairingService& pairing_service,
              vibe::store::PairingStore& pairing_store,
              vibe::store::HostConfigStore& host_config_store,
              std::shared_ptr<vibe::net::HostAdmin> host_admin,
              std::shared_ptr<WebSocketRegistry> websocket_registry,
              std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry,
              const ListenerRole listener_role, const bool remote_tls_enabled,
              std::string remote_listener_host, const std::uint16_t remote_listener_port,
              std::string remote_tls_certificate_path)
      : stream_(std::forward<Stream>(stream)),
        session_manager_(session_manager),
        authorizer_(authorizer),
        pairing_service_(pairing_service),
        pairing_store_(pairing_store),
        host_config_store_(host_config_store),
        host_admin_(std::move(host_admin)),
        websocket_registry_(std::move(websocket_registry)),
        overview_websocket_registry_(std::move(overview_websocket_registry)),
        listener_role_(listener_role),
        remote_tls_enabled_(remote_tls_enabled),
        remote_listener_host_(std::move(remote_listener_host)),
        remote_listener_port_(remote_listener_port),
        remote_tls_certificate_path_(std::move(remote_tls_certificate_path)) {}

  void Start() {
    if constexpr (std::is_same_v<Stream, SslStream>) {
      stream_.async_handshake(
          ssl::stream_base::server,
          [self = this->shared_from_this()](const boost::system::error_code& error_code) {
            if (error_code) {
              return;
            }
            self->DoRead();
          });
    } else {
      DoRead();
    }
  }

 private:
  void DoRead() {
    request_ = {};
    http::async_read(
        stream_, buffer_, request_,
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            return;
          }

          if (websocket::is_upgrade(self->request_) &&
              IsControllerWebSocketTarget(std::string(self->request_.target()))) {
            const auto endpoint = self->RemoteEndpoint();
            std::make_shared<RemoteControllerWebSocketSession<Stream>>(
                std::move(self->stream_), self->session_manager_, self->authorizer_,
                self->websocket_registry_, endpoint.address().to_string(),
                endpoint.address().is_loopback(), self->listener_role_)
                ->Start(std::move(self->request_));
            return;
          }

          if (websocket::is_upgrade(self->request_) &&
              IsSessionWebSocketTarget(std::string(self->request_.target()))) {
            const auto endpoint = self->RemoteEndpoint();
            std::make_shared<WebSocketSession<Stream>>(
                std::move(self->stream_), self->session_manager_, self->authorizer_,
                self->websocket_registry_, endpoint.address().to_string(),
                endpoint.address().is_loopback(), self->listener_role_)
                ->Start(std::move(self->request_));
            return;
          }

          if (websocket::is_upgrade(self->request_) &&
              IsOverviewWebSocketTarget(std::string(self->request_.target()))) {
            const auto endpoint = self->RemoteEndpoint();
            std::make_shared<OverviewWebSocketSession<Stream>>(
                std::move(self->stream_), self->session_manager_, self->authorizer_,
                self->overview_websocket_registry_, self->websocket_registry_,
                endpoint.address().to_string(),
                endpoint.address().is_loopback(), self->listener_role_)
                ->Start(std::move(self->request_));
            return;
          }

          const auto endpoint = self->RemoteEndpoint();
          self->response_ = HandleRequest(
              self->request_, self->session_manager_,
              HttpRouteContext{
                  .authorizer = &self->authorizer_,
                  .pairing_service = &self->pairing_service_,
                  .pairing_store = &self->pairing_store_,
                  .host_config_store = &self->host_config_store_,
                  .host_admin = self->host_admin_.get(),
                  .storage_root = self->host_config_store_.storage_root(),
                  .client_address = endpoint.address().to_string(),
                  .is_local_request = endpoint.address().is_loopback(),
                  .remote_tls_enabled = self->remote_tls_enabled_,
                  .remote_listener_host = self->remote_listener_host_,
                  .remote_listener_port = self->remote_listener_port_,
                  .remote_tls_certificate_path = self->remote_tls_certificate_path_,
                  .listener_role = self->listener_role_,
              });
          self->DoWrite();
        });
  }

  void DoWrite() {
    http::async_write(
        stream_, response_,
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            return;
          }
          self->ShutdownTransport();
        });
  }

  void ShutdownTransport() {
    if constexpr (std::is_same_v<Stream, SslStream>) {
      stream_.async_shutdown([self = this->shared_from_this()](const boost::system::error_code&) {
        boost::system::error_code close_error;
        self->stream_.next_layer().close(close_error);
      });
    } else {
      boost::system::error_code shutdown_error;
      stream_.shutdown(tcp::socket::shutdown_send, shutdown_error);
      boost::system::error_code close_error;
      stream_.close(close_error);
    }
  }

  [[nodiscard]] auto RemoteEndpoint() const -> tcp::endpoint {
    return GetRemoteEndpoint(stream_);
  }

  Stream stream_;
  beast::flat_buffer buffer_;
  HttpRequest request_;
  HttpResponse response_;
  vibe::service::SessionManager& session_manager_;
  const vibe::auth::Authorizer& authorizer_;
  vibe::auth::PairingService& pairing_service_;
  vibe::store::PairingStore& pairing_store_;
  vibe::store::HostConfigStore& host_config_store_;
  std::shared_ptr<vibe::net::HostAdmin> host_admin_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry_;
  ListenerRole listener_role_{ListenerRole::RemoteClient};
  bool remote_tls_enabled_{false};
  std::string remote_listener_host_;
  std::uint16_t remote_listener_port_{0};
  std::string remote_tls_certificate_path_;
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
 public:
  HttpListener(asio::io_context& io_context, const asio::ip::address& address,
               const std::uint16_t port, vibe::service::SessionManager& session_manager,
               const vibe::auth::Authorizer& authorizer,
               vibe::auth::PairingService& pairing_service,
               vibe::store::PairingStore& pairing_store,
               vibe::store::HostConfigStore& host_config_store,
               std::shared_ptr<vibe::net::HostAdmin> host_admin,
               std::shared_ptr<WebSocketRegistry> websocket_registry,
               std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry,
               const ListenerRole listener_role, const bool remote_tls_enabled,
               std::string remote_listener_host, const std::uint16_t remote_listener_port,
               std::string remote_tls_certificate_path)
      : acceptor_(io_context),
        socket_(io_context),
        session_manager_(session_manager),
        authorizer_(authorizer),
        pairing_service_(pairing_service),
        pairing_store_(pairing_store),
        host_config_store_(host_config_store),
        host_admin_(std::move(host_admin)),
        websocket_registry_(std::move(websocket_registry)),
        overview_websocket_registry_(std::move(overview_websocket_registry)),
        listener_role_(listener_role),
        remote_tls_enabled_(remote_tls_enabled),
        remote_listener_host_(std::move(remote_listener_host)),
        remote_listener_port_(remote_listener_port),
        remote_tls_certificate_path_(std::move(remote_tls_certificate_path)) {
    boost::system::error_code error_code;
    const tcp::endpoint endpoint(address, port);

    acceptor_.open(endpoint.protocol(), error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }

    acceptor_.set_option(asio::socket_base::reuse_address(true), error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }

    acceptor_.bind(endpoint, error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }

    acceptor_.listen(asio::socket_base::max_listen_connections, error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }
  }

  void Start() {
    DoAccept();
  }
  void Stop() {
    stopped_ = true;

    boost::system::error_code error_code;
    acceptor_.cancel(error_code);
    error_code.clear();
    acceptor_.close(error_code);
    error_code.clear();
    socket_.close(error_code);
  }

 private:
  void DoAccept() {
    if (stopped_) {
      return;
    }

    acceptor_.async_accept(
        socket_, [self = shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code == asio::error::operation_aborted || self->stopped_) {
            return;
          }

          if (!error_code) {
            std::make_shared<HttpSession<tcp::socket>>(
                std::move(self->socket_), self->session_manager_, self->authorizer_,
                self->pairing_service_, self->pairing_store_, self->host_config_store_,
                self->host_admin_, self->websocket_registry_, self->overview_websocket_registry_,
                self->listener_role_,
                self->remote_tls_enabled_, self->remote_listener_host_,
                self->remote_listener_port_, self->remote_tls_certificate_path_)
                ->Start();
          }

          self->DoAccept();
        });
  }

  tcp::acceptor acceptor_;
  tcp::socket socket_;
  vibe::service::SessionManager& session_manager_;
  const vibe::auth::Authorizer& authorizer_;
  vibe::auth::PairingService& pairing_service_;
  vibe::store::PairingStore& pairing_store_;
  vibe::store::HostConfigStore& host_config_store_;
  std::shared_ptr<vibe::net::HostAdmin> host_admin_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry_;
  ListenerRole listener_role_{ListenerRole::RemoteClient};
  bool remote_tls_enabled_{false};
  std::string remote_listener_host_;
  std::uint16_t remote_listener_port_{0};
  std::string remote_tls_certificate_path_;
  bool stopped_{false};
};

class HttpsListener : public std::enable_shared_from_this<HttpsListener> {
 public:
  HttpsListener(asio::io_context& io_context, ssl::context& ssl_context, const asio::ip::address& address,
                const std::uint16_t port, vibe::service::SessionManager& session_manager,
                const vibe::auth::Authorizer& authorizer,
                vibe::auth::PairingService& pairing_service,
                vibe::store::PairingStore& pairing_store,
                vibe::store::HostConfigStore& host_config_store,
                std::shared_ptr<vibe::net::HostAdmin> host_admin,
                std::shared_ptr<WebSocketRegistry> websocket_registry,
                std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry,
                std::string remote_listener_host, const std::uint16_t remote_listener_port,
                std::string remote_tls_certificate_path)
      : acceptor_(io_context),
        socket_(io_context),
        ssl_context_(ssl_context),
        session_manager_(session_manager),
        authorizer_(authorizer),
        pairing_service_(pairing_service),
        pairing_store_(pairing_store),
        host_config_store_(host_config_store),
        host_admin_(std::move(host_admin)),
        websocket_registry_(std::move(websocket_registry)),
        overview_websocket_registry_(std::move(overview_websocket_registry)),
        remote_listener_host_(std::move(remote_listener_host)),
        remote_listener_port_(remote_listener_port),
        remote_tls_certificate_path_(std::move(remote_tls_certificate_path)) {
    boost::system::error_code error_code;
    const tcp::endpoint endpoint(address, port);

    acceptor_.open(endpoint.protocol(), error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }

    acceptor_.set_option(asio::socket_base::reuse_address(true), error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }

    acceptor_.bind(endpoint, error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }

    acceptor_.listen(asio::socket_base::max_listen_connections, error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }
  }

  void Start() { DoAccept(); }
  void Stop() {
    stopped_ = true;

    boost::system::error_code error_code;
    acceptor_.cancel(error_code);
    error_code.clear();
    acceptor_.close(error_code);
    error_code.clear();
    socket_.close(error_code);
  }

 private:
  void DoAccept() {
    if (stopped_) {
      return;
    }

    acceptor_.async_accept(
        socket_, [self = shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code == asio::error::operation_aborted || self->stopped_) {
            return;
          }

          if (!error_code) {
            std::make_shared<HttpSession<SslStream>>(
                SslStream(std::move(self->socket_), self->ssl_context_), self->session_manager_,
                self->authorizer_, self->pairing_service_, self->pairing_store_,
                self->host_config_store_, self->host_admin_, self->websocket_registry_,
                self->overview_websocket_registry_,
                ListenerRole::RemoteClient, true, self->remote_listener_host_,
                self->remote_listener_port_, self->remote_tls_certificate_path_)
                ->Start();
          }

          self->DoAccept();
        });
  }

  tcp::acceptor acceptor_;
  tcp::socket socket_;
  ssl::context& ssl_context_;
  vibe::service::SessionManager& session_manager_;
  const vibe::auth::Authorizer& authorizer_;
  vibe::auth::PairingService& pairing_service_;
  vibe::store::PairingStore& pairing_store_;
  vibe::store::HostConfigStore& host_config_store_;
  std::shared_ptr<vibe::net::HostAdmin> host_admin_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  std::shared_ptr<OverviewWebSocketRegistry> overview_websocket_registry_;
  std::string remote_listener_host_;
  std::uint16_t remote_listener_port_{0};
  std::string remote_tls_certificate_path_;
  bool stopped_{false};
};

class LocalControllerSession final : public std::enable_shared_from_this<LocalControllerSession> {
 public:
  LocalControllerSession(local_stream::socket socket, vibe::service::SessionManager& session_manager)
      : socket_(std::move(socket)), session_manager_(session_manager) {}

  void Start() { ReadHandshake(); }

  void ForceClose() {
    if (closed_) {
      return;
    }
    closed_ = true;
    boost::system::error_code error_code;
    if (pty_readable_) {
      pty_readable_->cancel(error_code);
      pty_readable_->close(error_code);
      pty_readable_.reset();
    }
    socket_.cancel(error_code);
    socket_.close(error_code);
    if (!session_id_.empty() && !client_id_.empty()) {
      session_manager_.RemoveViewport(session_id_, client_id_);
    }
    ReleaseControlIfHeld();
  }

 private:
  void ReadHandshake() {
    asio::async_read_until(
        socket_, handshake_buffer_, '\n',
        [self = shared_from_this()](const boost::system::error_code& error_code,
                                    const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ForceClose();
            return;
          }

          std::istream stream(&self->handshake_buffer_);
          std::string line;
          std::getline(stream, line);
          self->HandleHandshake(line);
        });
  }

  void HandleHandshake(const std::string& line) {
    boost::system::error_code error_code;
    const json::value parsed = json::parse(line, error_code);
    if (error_code || !parsed.is_object()) {
      WriteHandshakeResponse("ERROR invalid local controller handshake\n", false);
      return;
    }

    const json::object& object = parsed.as_object();
    const auto* session_id = object.if_contains("sessionId");
    const auto* columns = object.if_contains("columns");
    const auto* rows = object.if_contains("rows");
    if (session_id == nullptr || !session_id->is_string()) {
      WriteHandshakeResponse("ERROR missing sessionId\n", false);
      return;
    }

    session_id_ = std::string(session_id->as_string());
    initial_terminal_size_ = vibe::session::TerminalSize{
        .columns = static_cast<std::uint16_t>(columns != nullptr && columns->is_int64()
                                                  ? columns->as_int64()
                                                  : 120),
        .rows = static_cast<std::uint16_t>(rows != nullptr && rows->is_int64()
                                               ? rows->as_int64()
                                               : 40),
    };
    current_terminal_size_ = initial_terminal_size_;
    client_id_ = "local-controller-" + std::to_string(CurrentUnixTimeMs()) + "-" +
                 std::to_string(socket_.native_handle());

    const auto summary = session_manager_.GetSession(session_id_);
    if (!summary.has_value()) {
      WriteHandshakeResponse("ERROR session not found\n", false);
      return;
    }

    if (!session_manager_.RequestControl(session_id_, client_id_, vibe::session::ControllerKind::Host)) {
      WriteHandshakeResponse("ERROR failed to claim control\n", false);
      return;
    }
    has_control_ = true;
    if (!session_manager_.ResizeSession(session_id_, initial_terminal_size_)) {
      WriteHandshakeResponse("ERROR failed to resize session\n", false);
      return;
    }

    std::ostringstream handshake_trace;
    handshake_trace << "session=" << session_id_ << " client=" << client_id_
                    << " size=" << initial_terminal_size_.columns << "x" << initial_terminal_size_.rows
                    << " currentSequence=" << summary->current_sequence;
    vibe::base::DebugTrace("core.focus", "local.handshake", handshake_trace.str());

    // Send a screen clear rather than replaying raw tail bytes or a bootstrap snapshot.
    //
    // Raw tail bytes were written at the old PTY size and cause ghosting when replayed
    // on a terminal that is now a different size. The bootstrap_ansi from the snapshot
    // is designed for fresh web terminals (xterm.js starting from empty state) — sending
    // it to a real local terminal pollutes its scrollback with the scrollback dump and
    // shifts cursor baselines, making things worse.
    //
    // The right recovery is: clear the visible screen, then let the subprocess's
    // SIGWINCH redraw (triggered by ResizeSession → TIOCSWINSZ above) repaint
    // the correct state at the new dimensions. TIOCSWINSZ always sends SIGWINCH
    // regardless of whether the size actually changed, so the redraw always arrives.
    initial_tail_ = "\x1b[0m\x1b[H\x1b[2J";
    const auto tail = session_manager_.GetTail(session_id_, 64U * 1024U);
    if (tail.has_value()) {
      next_output_sequence_ = tail->seq_end > 0 ? tail->seq_end + 1 : 1;
    } else {
      next_output_sequence_ = summary->current_sequence + 1;
    }

    std::ostringstream bootstrap_trace;
    bootstrap_trace << "session=" << session_id_ << " client=" << client_id_
                    << " bootstrapMode=clear-only"
                    << " nextSequence=" << next_output_sequence_;
    vibe::base::DebugTrace("core.focus", "local.bootstrap", bootstrap_trace.str());

    WriteHandshakeResponse("OK\n", true);
  }

  void WriteHandshakeResponse(const std::string& response, const bool success) {
    asio::async_write(
        socket_, asio::buffer(response),
        [self = shared_from_this(), success](const boost::system::error_code& error_code,
                                             const std::size_t /*bytes_transferred*/) {
          if (error_code || !success) {
            self->ForceClose();
            return;
          }

          if (!self->initial_tail_.empty()) {
            self->QueueOutput(std::move(self->initial_tail_));
          }
          self->ReadFrameHeader();
          self->SyncReadableDescriptor();
          self->ArmReadableWait();
        });
  }

  void ReadFrameHeader() {
    asio::async_read(
        socket_, asio::buffer(frame_header_),
        [self = shared_from_this()](const boost::system::error_code& error_code,
                                    const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ForceClose();
            return;
          }

          const char type = self->frame_header_[0];
          const std::uint32_t size =
              (static_cast<std::uint32_t>(static_cast<unsigned char>(self->frame_header_[1])) << 24) |
              (static_cast<std::uint32_t>(static_cast<unsigned char>(self->frame_header_[2])) << 16) |
              (static_cast<std::uint32_t>(static_cast<unsigned char>(self->frame_header_[3])) << 8) |
              static_cast<std::uint32_t>(static_cast<unsigned char>(self->frame_header_[4]));
          if (size > 1024U * 1024U) {
            self->ForceClose();
            return;
          }

          self->ReadFramePayload(type, size);
        });
  }

  void ReadFramePayload(const char type, const std::uint32_t size) {
    frame_payload_.assign(size, '\0');
    if (size == 0) {
      HandleFrame(type, {});
      return;
    }

    asio::async_read(
        socket_, asio::buffer(frame_payload_),
        [self = shared_from_this(), type](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ForceClose();
            return;
          }

          self->HandleFrame(type, std::string_view(self->frame_payload_.data(),
                                                   self->frame_payload_.size()));
        });
  }

  void HandleFrame(const char type, const std::string_view payload) {
    if (type == 'C') {
      if (!EnsureControlForInput()) {
        ReadFrameHeader();
        return;
      }
    } else if (type == 'I') {
      if (!has_control_ || !session_manager_.HasControl(session_id_, client_id_)) {
        has_control_ = false;
        ReadFrameHeader();
        return;
      }
      if (!session_manager_.SendInput(session_id_, std::string(payload))) {
        ForceClose();
        return;
      }
    } else if (type == 'R') {
      if (payload.size() != 4U) {
        ForceClose();
        return;
      }
      current_terminal_size_ = DecodeTerminalSize(payload);
      {
        std::ostringstream resize_trace;
        resize_trace << "session=" << session_id_ << " client=" << client_id_
                     << " size=" << current_terminal_size_.columns << "x" << current_terminal_size_.rows
                     << " hasControl=" << (has_control_ ? "true" : "false");
        vibe::base::DebugTrace("core.focus", "local.resize", resize_trace.str());
      }
      if (!has_control_ || !session_manager_.HasControl(session_id_, client_id_)) {
        has_control_ = false;
        vibe::base::DebugTrace("core.focus", "local.resize.ignored",
                               "reason=observer_mode viewportUpdateDeferred=true");
        ReadFrameHeader();
        return;
      }
      if (!session_manager_.ResizeSession(session_id_, current_terminal_size_)) {
        ForceClose();
        return;
      }
    } else {
      ForceClose();
      return;
    }

    ReadFrameHeader();
  }

  void SyncReadableDescriptor() {
    const auto readable_fd = session_manager_.GetReadableFd(session_id_);
    if (!readable_fd.has_value()) {
      pty_readable_.reset();
      return;
    }

    if (pty_readable_) {
      return;
    }

    const int duplicated_fd = dup(*readable_fd);
    if (duplicated_fd < 0) {
      return;
    }

    pty_readable_ = std::make_unique<asio::posix::stream_descriptor>(socket_.get_executor());
    pty_readable_->assign(duplicated_fd);
  }

  void ArmReadableWait() {
    if (!pty_readable_ || closed_) {
      return;
    }

    pty_readable_->async_wait(
        asio::posix::stream_descriptor::wait_read,
        [self = shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code == asio::error::operation_aborted) {
            return;
          }

          if (error_code) {
            self->ForceClose();
            return;
          }

          static_cast<void>(self->session_manager_.PollSession(self->session_id_, 0));

          // Check control state BEFORE flushing output. If we just lost control to a
          // remote client the bytes that just arrived are sized for the remote client's
          // terminal, not ours. Flushing them raw would bleed their SGR colors into our
          // wider terminal columns.
          const bool had_control = self->has_control_;
          if (self->has_control_ && !self->session_manager_.HasControl(self->session_id_, self->client_id_)) {
            self->has_control_ = false;
            vibe::base::DebugTrace("core.focus", "local.control.lost",
                                   "session=" + self->session_id_ + " client=" + self->client_id_);
          }

          const auto summary = self->session_manager_.GetSession(self->session_id_);
          if (!summary.has_value() || summary->status == vibe::session::SessionStatus::Exited ||
              summary->status == vibe::session::SessionStatus::Error) {
            self->close_after_writes_ = true;
          } else if (!self->has_control_ &&
                     summary->controller_kind == vibe::session::ControllerKind::Host &&
                     !summary->controller_client_id.has_value()) {
            if (self->session_manager_.RequestControl(self->session_id_, self->client_id_,
                                                      vibe::session::ControllerKind::Host)) {
              self->has_control_ = true;
              self->session_manager_.RemoveViewport(self->session_id_, self->client_id_);
              vibe::base::DebugTrace("core.focus", "local.control.reacquired",
                                     "session=" + self->session_id_ + " client=" + self->client_id_);
              // Sequence is kept current by FlushViewportSnapshot while in observer
              // mode, so raw output resumes from the right position automatically.
              static_cast<void>(
                  self->session_manager_.ResizeSession(self->session_id_, self->current_terminal_size_));
            }
          }

          if (self->has_control_) {
            self->FlushOutput();
          } else {
            if (had_control) {
              // Just lost control: register our viewport at local terminal size so
              // GetViewportSnapshot can adapt content to our dimensions, then clear
              // the screen so the remote client's wrong-sized bytes don't ghost.
              static_cast<void>(self->session_manager_.UpdateViewport(
                  self->session_id_, self->client_id_, self->current_terminal_size_));
              self->QueueOutput("\x1b[0m\x1b[H\x1b[2J");
              self->last_observer_render_revision_ = 0;
              {
                std::ostringstream observer_trace;
                observer_trace << "session=" << self->session_id_ << " client=" << self->client_id_
                               << " size=" << self->current_terminal_size_.columns << "x"
                               << self->current_terminal_size_.rows
                               << " action=clear-and-bootstrap";
                vibe::base::DebugTrace("core.focus", "local.observer.enter", observer_trace.str());
              }
            }
            self->FlushViewportSnapshot(summary);
          }

          self->SyncReadableDescriptor();
          if (self->pty_readable_ != nullptr && !self->closed_) {
            self->ArmReadableWait();
          } else if (self->close_after_writes_ && !self->write_in_progress_ &&
                     self->pending_output_.empty()) {
            self->ForceClose();
          }
        });
  }

  void FlushOutput() {
    const auto slice = session_manager_.GetOutputSince(session_id_, next_output_sequence_);
    if (!slice.has_value() || slice->data.empty()) {
      return;
    }

    next_output_sequence_ = slice->seq_end + 1;
    QueueOutput(std::string(slice->data));
  }

  void FlushViewportSnapshot(const std::optional<vibe::service::SessionSummary>& summary) {
    // Keep next_output_sequence_ current so raw mode resumes cleanly when we
    // regain control, without replaying any of the wrong-sized observer bytes.
    if (summary.has_value()) {
      next_output_sequence_ = summary->current_sequence + 1;
    }

    const auto viewport = session_manager_.GetViewportSnapshot(session_id_, client_id_);
    if (!viewport.has_value() || viewport->bootstrap_ansi.empty()) {
      vibe::base::DebugTrace("core.focus", "local.viewport.skip",
                             "session=" + session_id_ + " client=" + client_id_ + " reason=missing_bootstrap");
      return;
    }
    if (viewport->render_revision == last_observer_render_revision_) {
      std::ostringstream skip_trace;
      skip_trace << "session=" << session_id_ << " client=" << client_id_
                 << " renderRevision=" << viewport->render_revision << " reason=unchanged";
      vibe::base::DebugTrace("core.focus", "local.viewport.skip", skip_trace.str());
      return;
    }

    last_observer_render_revision_ = viewport->render_revision;
    std::ostringstream viewport_trace;
    viewport_trace << "session=" << session_id_ << " client=" << client_id_
                   << " cols=" << viewport->columns << " rows=" << viewport->rows
                   << " renderRevision=" << viewport->render_revision
                   << " top=" << viewport->viewport_top_line
                   << " cursorRow=" << (viewport->cursor_viewport_row.has_value()
                                            ? std::to_string(*viewport->cursor_viewport_row)
                                            : "nil")
                   << " cursorCol=" << (viewport->cursor_viewport_column.has_value()
                                            ? std::to_string(*viewport->cursor_viewport_column)
                                            : "nil")
                   << " bootstrapBytes=" << viewport->bootstrap_ansi.size();
    vibe::base::DebugTrace("core.focus", "local.viewport.flush", viewport_trace.str());
    QueueOutput(std::string(viewport->bootstrap_ansi));
  }

  void QueueOutput(std::string payload) {
    if (payload.empty() || closed_) {
      return;
    }

    pending_output_.push_back(std::move(payload));
    if (!write_in_progress_) {
      DoWrite();
    }
  }

  void DoWrite() {
    if (pending_output_.empty() || closed_) {
      write_in_progress_ = false;
      if (close_after_writes_) {
        ForceClose();
      }
      return;
    }

    write_in_progress_ = true;
    asio::async_write(
        socket_, asio::buffer(pending_output_.front()),
        [self = shared_from_this()](const boost::system::error_code& error_code,
                                    const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ForceClose();
            return;
          }

          self->pending_output_.pop_front();
          self->DoWrite();
        });
  }

  void ReleaseControlIfHeld() {
    if (!has_control_) {
      return;
    }
    const bool released = session_manager_.ReleaseControl(session_id_, client_id_);
    static_cast<void>(released);
    has_control_ = false;
  }

  static auto DecodeTerminalSize(const std::string_view payload) -> vibe::session::TerminalSize {
    return vibe::session::TerminalSize{
        .columns = static_cast<std::uint16_t>(
            (static_cast<unsigned char>(payload[0]) << 8) |
            static_cast<unsigned char>(payload[1])),
        .rows = static_cast<std::uint16_t>(
            (static_cast<unsigned char>(payload[2]) << 8) |
            static_cast<unsigned char>(payload[3])),
    };
  }

  auto EnsureControlForInput() -> bool {
    if (has_control_ && session_manager_.HasControl(session_id_, client_id_)) {
      return true;
    }

    if (!session_manager_.RequestControl(session_id_, client_id_, vibe::session::ControllerKind::Host)) {
      has_control_ = false;
      return false;
    }

    has_control_ = true;
    if (!session_manager_.ResizeSession(session_id_, current_terminal_size_)) {
      has_control_ = false;
      return false;
    }
    return true;
  }

  local_stream::socket socket_;
  vibe::service::SessionManager& session_manager_;
  asio::streambuf handshake_buffer_;
  std::array<char, 5> frame_header_{};
  std::vector<char> frame_payload_;
  std::deque<std::string> pending_output_;
  std::unique_ptr<asio::posix::stream_descriptor> pty_readable_;
  std::string session_id_;
  std::string client_id_;
  std::string initial_tail_;
  vibe::session::TerminalSize initial_terminal_size_{};
  vibe::session::TerminalSize current_terminal_size_{};
  std::uint64_t next_output_sequence_{1};
  std::uint64_t last_observer_render_revision_{0};
  bool has_control_{false};
  bool write_in_progress_{false};
  bool close_after_writes_{false};
  bool closed_{false};
};

class LocalControllerListener final : public std::enable_shared_from_this<LocalControllerListener> {
 public:
  LocalControllerListener(asio::io_context& io_context, std::filesystem::path socket_path,
                          vibe::service::SessionManager& session_manager)
      : acceptor_(io_context),
        socket_(io_context),
        socket_path_(std::move(socket_path)),
        session_manager_(session_manager) {
    std::filesystem::create_directories(socket_path_.parent_path());
    std::error_code remove_error;
    std::filesystem::remove(socket_path_, remove_error);

    local_stream::endpoint endpoint(socket_path_.string());
    acceptor_.open(endpoint.protocol());
    acceptor_.bind(endpoint);
    acceptor_.listen();
    std::filesystem::permissions(
        socket_path_,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, remove_error);
  }

  ~LocalControllerListener() { Stop(); }

  void Start() { DoAccept(); }

  void Stop() {
    boost::system::error_code error_code;
    acceptor_.cancel(error_code);
    acceptor_.close(error_code);
    socket_.close(error_code);
    std::error_code remove_error;
    std::filesystem::remove(socket_path_, remove_error);
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(socket_, [self = shared_from_this()](const boost::system::error_code& error_code) {
      if (!self->acceptor_.is_open()) {
        return;
      }

      if (!error_code) {
        std::make_shared<LocalControllerSession>(std::move(self->socket_), self->session_manager_)->Start();
      }

      self->DoAccept();
    });
  }

  local_stream::acceptor acceptor_;
  local_stream::socket socket_;
  std::filesystem::path socket_path_;
  vibe::service::SessionManager& session_manager_;
};

}  // namespace

HttpServer::HttpServer(std::string admin_bind_address, const std::uint16_t admin_port,
                       std::string remote_bind_address, const std::uint16_t remote_port,
                       std::optional<RemoteTlsFiles> remote_tls_override,
                       const bool enable_discovery)
    : HttpServer(std::move(admin_bind_address), admin_port, std::move(remote_bind_address),
                 remote_port, DefaultStorageRoot(), std::move(remote_tls_override),
                 enable_discovery) {}

HttpServer::HttpServer(std::string admin_bind_address, const std::uint16_t admin_port,
                       std::string remote_bind_address, const std::uint16_t remote_port,
                       std::filesystem::path storage_root,
                       std::optional<RemoteTlsFiles> remote_tls_override,
                       const bool enable_discovery)
    : admin_bind_address_(std::move(admin_bind_address)),
      admin_port_(admin_port),
      remote_bind_address_(std::move(remote_bind_address)),
      remote_port_(remote_port),
      storage_root_(std::move(storage_root)),
      remote_tls_override_(std::move(remote_tls_override)),
      enable_discovery_(enable_discovery),
      session_store_(storage_root_),
      session_manager_(&session_store_) {
  auto auth_services = CreateLocalAuthServices(storage_root_);
  authorizer_ = std::move(auth_services.authorizer);
  pairing_service_ = std::move(auth_services.pairing_service);
  pairing_store_ = std::move(auth_services.pairing_store);
  host_config_store_ = std::move(auth_services.host_config_store);
}

HttpServer::~HttpServer() { Stop(); }

auto HttpServer::Run() -> bool {
  io_context_ = std::make_unique<asio::io_context>(1);
  {
    std::lock_guard lock(state_mutex_);
    stopping_ = false;
    stop_callback_ = nullptr;
  }
  const std::size_t recovered_sessions = session_manager_.LoadPersistedSessions();
  static_cast<void>(recovered_sessions);
  boost::system::error_code error_code;
  const auto admin_address = asio::ip::make_address(admin_bind_address_, error_code);
  if (error_code) {
    std::cerr << "invalid admin bind address " << admin_bind_address_ << ": "
              << error_code.message() << '\n';
    return false;
  }

  const auto remote_address = asio::ip::make_address(remote_bind_address_, error_code);
  if (error_code) {
    std::cerr << "invalid remote bind address " << remote_bind_address_ << ": "
              << error_code.message() << '\n';
    return false;
  }

  const auto resolved_remote_tls = ResolveRemoteTlsConfig(*host_config_store_, remote_tls_override_);
  if (const auto* error = std::get_if<std::string>(&resolved_remote_tls); error != nullptr) {
    std::cerr << *error << '\n';
    return false;
  }

  const auto remote_tls_config = std::get<EffectiveRemoteTlsConfig>(resolved_remote_tls);
  std::shared_ptr<ssl::context> remote_ssl_context;
  if (remote_tls_config.enabled) {
    const auto ssl_context_result = MakeServerTlsContext(remote_tls_config);
    if (const auto* error = std::get_if<std::string>(&ssl_context_result); error != nullptr) {
      std::cerr << *error << '\n';
      return false;
    }
    remote_ssl_context = std::get<std::shared_ptr<ssl::context>>(std::move(ssl_context_result));
  }

  try {
    auto websocket_registry = std::make_shared<WebSocketRegistry>();
    auto overview_websocket_registry = std::make_shared<OverviewWebSocketRegistry>();
    auto host_admin = std::shared_ptr<vibe::net::HostAdmin>(
        std::make_shared<HostAdminService>(session_manager_, *websocket_registry));
    auto session_pump =
        std::make_shared<SessionPump>(*io_context_, session_manager_, websocket_registry,
                                      overview_websocket_registry);
    auto local_controller_listener = std::make_shared<LocalControllerListener>(
        *io_context_, DefaultControllerSocketPath(storage_root_), session_manager_);
    std::shared_ptr<UdpDiscoveryBroadcaster> discovery_broadcaster;
    if (enable_discovery_) {
      discovery_broadcaster = std::make_shared<UdpDiscoveryBroadcaster>(
          [this, remote_tls_enabled = remote_tls_config.enabled]() {
            return ToJson(ResolveDiscoveryInfo(
                host_config_store_->LoadHostIdentity(), remote_bind_address_, remote_port_,
                remote_tls_enabled));
          });
    }
    auto admin_listener =
        std::make_shared<HttpListener>(*io_context_, admin_address, admin_port_, session_manager_,
                                       *authorizer_, *pairing_service_, *pairing_store_,
                                       *host_config_store_, host_admin, websocket_registry,
                                       overview_websocket_registry,
                                       ListenerRole::AdminLocal, remote_tls_config.enabled,
                                       remote_bind_address_, remote_port_,
                                       remote_tls_config.certificate_pem_path);
    std::shared_ptr<HttpListener> remote_http_listener;
    std::shared_ptr<HttpsListener> remote_https_listener;
    session_pump->Start();
    local_controller_listener->Start();
    if (discovery_broadcaster) {
      const bool discovery_started = discovery_broadcaster->Start();
      static_cast<void>(discovery_started);
    }
    admin_listener->Start();
    if (remote_tls_config.enabled) {
      remote_https_listener =
          std::make_shared<HttpsListener>(*io_context_, *remote_ssl_context, remote_address,
                                          remote_port_, session_manager_, *authorizer_,
                                          *pairing_service_, *pairing_store_, *host_config_store_,
                                          host_admin, websocket_registry, overview_websocket_registry,
                                          remote_bind_address_, remote_port_,
                                          remote_tls_config.certificate_pem_path);
      remote_https_listener->Start();
    } else {
      remote_http_listener =
          std::make_shared<HttpListener>(*io_context_, remote_address, remote_port_, session_manager_,
                                         *authorizer_, *pairing_service_, *pairing_store_,
                                         *host_config_store_, host_admin, websocket_registry,
                                         overview_websocket_registry,
                                         ListenerRole::RemoteClient, false,
                                         remote_bind_address_, remote_port_,
                                         remote_tls_config.certificate_pem_path);
      remote_http_listener->Start();
    }

    asio::io_context* io_context = io_context_.get();
    {
      std::lock_guard lock(state_mutex_);
      stop_callback_ =
          [this, io_context, session_pump, admin_listener, remote_http_listener,
           remote_https_listener, local_controller_listener, discovery_broadcaster, websocket_registry,
           overview_websocket_registry]() {
            asio::post(*io_context, [this, io_context, session_pump, admin_listener,
                                     remote_http_listener, remote_https_listener,
                                     local_controller_listener,
                                     discovery_broadcaster, websocket_registry,
                                     overview_websocket_registry]() {
              if (discovery_broadcaster) {
                discovery_broadcaster->Stop();
              }
              session_pump->Stop();
              local_controller_listener->Stop();
              admin_listener->Stop();
              if (remote_http_listener) {
                remote_http_listener->Stop();
              }
              if (remote_https_listener) {
                remote_https_listener->Stop();
              }
              CloseAllWebSockets(*websocket_registry);
              CloseAllOverviewWebSockets(*overview_websocket_registry);
              const std::size_t shutdown_count = session_manager_.Shutdown();
              static_cast<void>(shutdown_count);
              PruneRegistry(*websocket_registry);
              PruneOverviewRegistry(*overview_websocket_registry);
              io_context->stop();
            });
          };
    }
  } catch (const boost::system::system_error& exception) {
    std::cerr << "failed to bind listeners: "
              << exception.code().message() << '\n';
    {
      std::lock_guard lock(state_mutex_);
      stop_callback_ = nullptr;
      stopping_ = false;
    }
    io_context_.reset();
    return false;
  }

  std::cout << "host admin listening on " << admin_bind_address_ << ":" << admin_port_ << '\n';
  std::cout << "remote listener listening on " << remote_bind_address_ << ":" << remote_port_
            << (remote_tls_config.enabled ? " with TLS (HTTPS/WSS)" : " without TLS (HTTP/WS)")
            << '\n';
  io_context_->run();
  {
    std::lock_guard lock(state_mutex_);
    stop_callback_ = nullptr;
    stopping_ = false;
  }
  io_context_.reset();
  return true;
}

void HttpServer::Stop() {
  std::function<void()> stop_callback;
  boost::asio::io_context* io_context = nullptr;

  {
    std::lock_guard lock(state_mutex_);
    if (stopping_) {
      return;
    }

    stopping_ = true;
    stop_callback = stop_callback_;
    io_context = io_context_.get();
  }

  if (stop_callback) {
    stop_callback();
    return;
  }

  if (io_context != nullptr) {
    io_context->stop();
  }
}

}  // namespace vibe::net

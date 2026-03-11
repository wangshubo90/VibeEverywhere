#include "vibe/net/http_server.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "vibe/net/http_shared.h"
#include "vibe/net/json.h"
#include "vibe/net/local_auth.h"
#include "vibe/net/request_parsing.h"
#include "vibe/net/websocket_shared.h"

namespace vibe::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

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
    const auto summaries = session_manager_.ListSessions();
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
          self->last_sequence_ = 1;
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

    const auto output = session_manager_.GetOutputSince(session_id_, last_sequence_);
    if (!output.has_value() || output->data.empty()) {
      return;
    }

    QueueFrame(ToJson(TerminalOutputEvent{
                   .session_id = session_id_,
                   .slice = *output,
               }),
               output->seq_end + 1);
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
        initial_slice.seq_end > 0 ? initial_slice.seq_end + 1 : last_sequence_;
    QueueFrame(ToJson(TerminalOutputEvent{
                   .session_id = session_id_,
                   .slice = initial_slice,
               }),
               next_sequence);
  }

  void QueueStatusEvents() {
    const auto summary = session_manager_.GetSession(session_id_);
    if (!summary.has_value()) {
      return;
    }

    const bool status_changed =
        !last_status_.has_value() || *last_status_ != summary->status ||
        last_controller_client_id_ != summary->controller_client_id ||
        last_controller_kind_ != summary->controller_kind;
    if (status_changed) {
      last_status_ = summary->status;
      last_controller_client_id_ = summary->controller_client_id;
      last_controller_kind_ = summary->controller_kind;
      QueueFrame(ToJson(SessionUpdatedEvent{.summary = *summary}), last_sequence_);
    }

    const bool activity_changed =
        !last_activity_state_.has_value() || *last_activity_state_ != std::string(ToActivityStateLabel(*summary)) ||
        last_output_at_unix_ms_ != summary->last_output_at_unix_ms ||
        last_activity_at_unix_ms_ != summary->last_activity_at_unix_ms ||
        last_current_sequence_ != summary->current_sequence ||
        last_recent_file_change_count_ != summary->recent_file_change_count ||
        last_git_dirty_ != summary->git_dirty ||
        last_git_branch_ != summary->git_branch;
    if (activity_changed) {
      last_activity_state_ = std::string(ToActivityStateLabel(*summary));
      last_output_at_unix_ms_ = summary->last_output_at_unix_ms;
      last_activity_at_unix_ms_ = summary->last_activity_at_unix_ms;
      last_current_sequence_ = summary->current_sequence;
      last_recent_file_change_count_ = summary->recent_file_change_count;
      last_git_dirty_ = summary->git_dirty;
      last_git_branch_ = summary->git_branch;
      if (!status_changed) {
        QueueFrame(ToJson(SessionActivityEvent{.summary = *summary}), last_sequence_);
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
                 last_sequence_);
    }
  }

  void QueueFrame(std::string payload, const std::uint64_t next_sequence) {
    pending_frames_.push_back(PendingFrame{
        .payload = std::move(payload),
        .next_sequence = next_sequence,
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
    websocket_.text(true);
    websocket_.async_write(
        asio::buffer(pending_frames_.front().payload),
        [self = this->shared_from_this()](const boost::system::error_code& error_code,
                                          const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            self->ReleaseControlIfHeld();
            return;
          }

          self->last_sequence_ = self->pending_frames_.front().next_sequence;
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
          self->read_buffer_.consume(self->read_buffer_.size());
          self->HandleClientCommand(payload);
          self->DoRead();
        });
  }

  void HandleClientCommand(const std::string& payload) {
    const auto command = ParseWebSocketCommand(payload);
    if (!command.has_value()) {
      QueueFrame(ToJson(ErrorEvent{
                     .session_id = session_id_,
                     .code = "invalid_command",
                     .message = "invalid websocket command",
                 }),
                 last_sequence_);
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
            if (!session_manager_.HasControl(session_id_, client_id_)) {
              return false;
            }
            return session_manager_.ResizeSession(session_id_, value.terminal_size);
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
                 last_sequence_);
    }
  }

  void ReleaseControlIfHeld() {
    if (session_id_.empty() || client_id_.empty()) {
      return;
    }

    const bool released = session_manager_.ReleaseControl(session_id_, client_id_);
    static_cast<void>(released);
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
  std::uint64_t last_sequence_{1};
  std::optional<vibe::session::SessionStatus> last_status_;
  std::optional<std::string> last_controller_client_id_;
  vibe::session::ControllerKind last_controller_kind_{vibe::session::ControllerKind::None};
  std::optional<std::string> last_activity_state_;
  std::optional<std::int64_t> last_output_at_unix_ms_;
  std::optional<std::int64_t> last_activity_at_unix_ms_;
  std::uint64_t last_current_sequence_{0};
  std::size_t last_recent_file_change_count_{0};
  bool last_git_dirty_{false};
  std::string last_git_branch_;
  vibe::session::ControllerKind claimed_kind_{vibe::session::ControllerKind::None};
  bool initial_events_sent_{false};
  bool exit_event_sent_{false};
  bool write_in_progress_{false};
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
  void DoPoll() {
    if (stopped_) {
      return;
    }

    poll_timer_.expires_after(std::chrono::milliseconds(50));
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
                self->overview_websocket_registry_, endpoint.address().to_string(),
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
                  .client_address = endpoint.address().to_string(),
                  .is_local_request = endpoint.address().is_loopback(),
                  .remote_tls_enabled = self->remote_tls_enabled_,
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
                self->remote_tls_enabled_, self->remote_tls_certificate_path_)
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
                ListenerRole::RemoteClient, true, self->remote_tls_certificate_path_)
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
  std::string remote_tls_certificate_path_;
  bool stopped_{false};
};

}  // namespace

HttpServer::HttpServer(std::string admin_bind_address, const std::uint16_t admin_port,
                       std::string remote_bind_address, const std::uint16_t remote_port,
                       std::optional<RemoteTlsFiles> remote_tls_override)
    : HttpServer(std::move(admin_bind_address), admin_port, std::move(remote_bind_address),
                 remote_port, DefaultStorageRoot(), std::move(remote_tls_override)) {}

HttpServer::HttpServer(std::string admin_bind_address, const std::uint16_t admin_port,
                       std::string remote_bind_address, const std::uint16_t remote_port,
                       std::filesystem::path storage_root,
                       std::optional<RemoteTlsFiles> remote_tls_override)
    : admin_bind_address_(std::move(admin_bind_address)),
      admin_port_(admin_port),
      remote_bind_address_(std::move(remote_bind_address)),
      remote_port_(remote_port),
      storage_root_(std::move(storage_root)),
      remote_tls_override_(std::move(remote_tls_override)),
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
    auto admin_listener =
        std::make_shared<HttpListener>(*io_context_, admin_address, admin_port_, session_manager_,
                                       *authorizer_, *pairing_service_, *pairing_store_,
                                       *host_config_store_, host_admin, websocket_registry,
                                       overview_websocket_registry,
                                       ListenerRole::AdminLocal, remote_tls_config.enabled,
                                       remote_tls_config.certificate_pem_path);
    std::shared_ptr<HttpListener> remote_http_listener;
    std::shared_ptr<HttpsListener> remote_https_listener;
    session_pump->Start();
    admin_listener->Start();
    if (remote_tls_config.enabled) {
      remote_https_listener =
          std::make_shared<HttpsListener>(*io_context_, *remote_ssl_context, remote_address,
                                          remote_port_, session_manager_, *authorizer_,
                                          *pairing_service_, *pairing_store_, *host_config_store_,
                                          host_admin, websocket_registry, overview_websocket_registry,
                                          remote_tls_config.certificate_pem_path);
      remote_https_listener->Start();
    } else {
      remote_http_listener =
          std::make_shared<HttpListener>(*io_context_, remote_address, remote_port_, session_manager_,
                                         *authorizer_, *pairing_service_, *pairing_store_,
                                         *host_config_store_, host_admin, websocket_registry,
                                         overview_websocket_registry,
                                         ListenerRole::RemoteClient, false,
                                         remote_tls_config.certificate_pem_path);
      remote_http_listener->Start();
    }

    asio::io_context* io_context = io_context_.get();
    {
      std::lock_guard lock(state_mutex_);
      stop_callback_ =
          [this, io_context, session_pump, admin_listener, remote_http_listener,
           remote_https_listener, websocket_registry, overview_websocket_registry]() {
            asio::post(*io_context, [this, io_context, session_pump, admin_listener,
                                     remote_http_listener, remote_https_listener,
                                     websocket_registry, overview_websocket_registry]() {
              session_pump->Stop();
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

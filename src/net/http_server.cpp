#include "vibe/net/http_server.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
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
#include <vector>

#include "vibe/net/http_shared.h"
#include "vibe/net/json.h"
#include "vibe/net/local_auth.h"
#include "vibe/net/request_parsing.h"
#include "vibe/net/websocket_shared.h"

namespace vibe::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

class WebSocketSession;
using WebSocketRegistry = std::unordered_map<std::string, std::vector<std::weak_ptr<WebSocketSession>>>;

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
                       [](const std::weak_ptr<WebSocketSession>& entry) { return entry.expired(); }),
        sessions.end());
    if (sessions.empty()) {
      registry_it = websocket_registry.erase(registry_it);
    } else {
      ++registry_it;
    }
  }
}

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
 public:
  ~WebSocketSession() { ReleaseControlIfHeld(); }

  WebSocketSession(tcp::socket socket, vibe::service::SessionManager& session_manager,
                   const vibe::auth::Authorizer& authorizer,
                   std::shared_ptr<WebSocketRegistry> websocket_registry,
                   const std::string& client_address, const bool is_local_request,
                   const ListenerRole listener_role)
      : websocket_(std::move(socket)),
        session_manager_(session_manager),
        authorizer_(authorizer),
        websocket_registry_(std::move(websocket_registry)),
        client_address_(client_address),
        is_local_request_(is_local_request),
        listener_role_(listener_role) {}

  void Start(HttpRequest request) {
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
        [self = shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code) {
            self->ReleaseControlIfHeld();
            return;
          }

          self->client_id_ = "ws_" + self->session_id_ + "_" +
                             std::to_string(reinterpret_cast<std::uintptr_t>(self.get()));
          self->last_sequence_ = 1;
          (*self->websocket_registry_)[self->session_id_].push_back(self);
          self->QueueInitialEvents();
          self->DoRead();
        });
  }

  void SendPendingOutput() {
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

  [[nodiscard]] auto session_id() const -> const std::string& { return session_id_; }
  [[nodiscard]] auto client_id() const -> const std::string& { return client_id_; }
  [[nodiscard]] auto client_address() const -> const std::string& { return client_address_; }
  [[nodiscard]] auto is_local_request() const -> bool { return is_local_request_; }
  [[nodiscard]] auto claimed_kind() const -> vibe::session::ControllerKind { return claimed_kind_; }

  void ForceClose() {
    if (closed_) {
      return;
    }

    closed_ = true;
    boost::system::error_code error_code;
    websocket_.next_layer().shutdown(tcp::socket::shutdown_both, error_code);
    error_code.clear();
    websocket_.next_layer().close(error_code);
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

    if (!last_status_.has_value() || *last_status_ != summary->status ||
        last_controller_client_id_ != summary->controller_client_id ||
        last_controller_kind_ != summary->controller_kind) {
      last_status_ = summary->status;
      last_controller_client_id_ = summary->controller_client_id;
      last_controller_kind_ = summary->controller_kind;
      QueueFrame(ToJson(SessionUpdatedEvent{.summary = *summary}), last_sequence_);
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
        [self = shared_from_this()](const boost::system::error_code& error_code,
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
        [self = shared_from_this()](const boost::system::error_code& error_code,
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

  websocket::stream<tcp::socket> websocket_;
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
  vibe::session::ControllerKind claimed_kind_{vibe::session::ControllerKind::None};
  bool initial_events_sent_{false};
  bool exit_event_sent_{false};
  bool write_in_progress_{false};
  bool closed_{false};
  bool is_local_request_{false};
  ListenerRole listener_role_{ListenerRole::RemoteClient};
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
              .client_address = session->client_address(),
              .claimed_kind = session->claimed_kind(),
              .is_local = session->is_local_request(),
              .has_control = controller_client_id.has_value() && *controller_client_id == session->client_id(),
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
              std::shared_ptr<WebSocketRegistry> websocket_registry)
      : poll_timer_(io_context),
        session_manager_(session_manager),
        websocket_registry_(std::move(websocket_registry)) {}

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
      self->DoPoll();
    });
  }

  asio::steady_timer poll_timer_;
  vibe::service::SessionManager& session_manager_;
  std::shared_ptr<WebSocketRegistry> websocket_registry_;
  bool stopped_{false};
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession(tcp::socket socket, vibe::service::SessionManager& session_manager,
              const vibe::auth::Authorizer& authorizer,
              vibe::auth::PairingService& pairing_service,
              vibe::store::PairingStore& pairing_store,
              vibe::store::HostConfigStore& host_config_store,
              std::shared_ptr<vibe::net::HostAdmin> host_admin,
              std::shared_ptr<WebSocketRegistry> websocket_registry,
              const ListenerRole listener_role)
      : socket_(std::move(socket)),
        session_manager_(session_manager),
        authorizer_(authorizer),
        pairing_service_(pairing_service),
        pairing_store_(pairing_store),
        host_config_store_(host_config_store),
        host_admin_(std::move(host_admin)),
        websocket_registry_(std::move(websocket_registry)),
        listener_role_(listener_role) {}

  void Start() { DoRead(); }

 private:
  void DoRead() {
    request_ = {};
    http::async_read(
        socket_, buffer_, request_,
        [self = shared_from_this()](const boost::system::error_code& error_code,
                                    const std::size_t /*bytes_transferred*/) {
          if (error_code) {
            return;
          }

          if (websocket::is_upgrade(self->request_) &&
              IsSessionWebSocketTarget(std::string(self->request_.target()))) {
            const auto endpoint = self->socket_.remote_endpoint();
            const std::string client_address = endpoint.address().to_string();
            std::make_shared<WebSocketSession>(std::move(self->socket_), self->session_manager_,
                                               self->authorizer_, self->websocket_registry_,
                                               client_address, endpoint.address().is_loopback(),
                                               self->listener_role_)
                ->Start(std::move(self->request_));
            return;
          }

          const auto endpoint = self->socket_.remote_endpoint();
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
                  .listener_role = self->listener_role_,
              });
          self->DoWrite();
        });
  }

  void DoWrite() {
    http::async_write(
        socket_, response_,
        [self = shared_from_this()](const boost::system::error_code& error_code,
                                    const std::size_t /*bytes_transferred*/) {
          boost::system::error_code shutdown_error;
          self->socket_.shutdown(tcp::socket::shutdown_send, shutdown_error);
          if (error_code) {
            return;
          }
        });
  }

  tcp::socket socket_;
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
  ListenerRole listener_role_{ListenerRole::RemoteClient};
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
               const ListenerRole listener_role)
      : acceptor_(io_context),
        socket_(io_context),
        session_manager_(session_manager),
        authorizer_(authorizer),
        pairing_service_(pairing_service),
        pairing_store_(pairing_store),
        host_config_store_(host_config_store),
        host_admin_(std::move(host_admin)),
        websocket_registry_(std::move(websocket_registry)),
        listener_role_(listener_role) {
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
            std::make_shared<HttpSession>(std::move(self->socket_), self->session_manager_,
                                          self->authorizer_, self->pairing_service_,
                                          self->pairing_store_,
                                          self->host_config_store_, self->host_admin_,
                                          self->websocket_registry_, self->listener_role_)
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
  ListenerRole listener_role_{ListenerRole::RemoteClient};
  bool stopped_{false};
};

}  // namespace

HttpServer::HttpServer(std::string admin_bind_address, const std::uint16_t admin_port,
                       std::string remote_bind_address, const std::uint16_t remote_port)
    : HttpServer(std::move(admin_bind_address), admin_port, std::move(remote_bind_address),
                 remote_port, DefaultStorageRoot()) {}

HttpServer::HttpServer(std::string admin_bind_address, const std::uint16_t admin_port,
                       std::string remote_bind_address, const std::uint16_t remote_port,
                       std::filesystem::path storage_root)
    : admin_bind_address_(std::move(admin_bind_address)),
      admin_port_(admin_port),
      remote_bind_address_(std::move(remote_bind_address)),
      remote_port_(remote_port),
      storage_root_(std::move(storage_root)),
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

  try {
    auto websocket_registry = std::make_shared<WebSocketRegistry>();
    auto host_admin = std::shared_ptr<vibe::net::HostAdmin>(
        std::make_shared<HostAdminService>(session_manager_, *websocket_registry));
    auto session_pump = std::make_shared<SessionPump>(*io_context_, session_manager_, websocket_registry);
    auto admin_listener =
        std::make_shared<HttpListener>(*io_context_, admin_address, admin_port_, session_manager_,
                                       *authorizer_, *pairing_service_, *pairing_store_,
                                       *host_config_store_, host_admin, websocket_registry,
                                       ListenerRole::AdminLocal);
    auto remote_listener =
        std::make_shared<HttpListener>(*io_context_, remote_address, remote_port_, session_manager_,
                                       *authorizer_, *pairing_service_, *pairing_store_,
                                       *host_config_store_, host_admin, websocket_registry,
                                       ListenerRole::RemoteClient);
    session_pump->Start();
    admin_listener->Start();
    remote_listener->Start();

    asio::io_context* io_context = io_context_.get();
    {
      std::lock_guard lock(state_mutex_);
      stop_callback_ =
          [this, io_context, session_pump, admin_listener, remote_listener, websocket_registry]() {
            asio::post(*io_context, [this, io_context, session_pump, admin_listener, remote_listener,
                                     websocket_registry]() {
              session_pump->Stop();
              admin_listener->Stop();
              remote_listener->Stop();
              CloseAllWebSockets(*websocket_registry);
              const std::size_t shutdown_count = session_manager_.Shutdown();
              static_cast<void>(shutdown_count);
              PruneRegistry(*websocket_registry);
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

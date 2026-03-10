#include "vibe/net/http_server.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vibe/net/http_shared.h"
#include "vibe/net/json.h"
#include "vibe/net/websocket_shared.h"

namespace vibe::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

class WebSocketSession;
using WebSocketRegistry = std::unordered_map<std::string, std::vector<std::weak_ptr<WebSocketSession>>>;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
 public:
  WebSocketSession(tcp::socket socket, vibe::service::SessionManager& session_manager,
                   WebSocketRegistry& websocket_registry)
      : websocket_(std::move(socket)),
        session_manager_(session_manager),
        websocket_registry_(websocket_registry) {}

  void Start(HttpRequest request) {
    target_ = std::string(request.target());
    session_id_ = ExtractSessionIdFromWebSocketTarget(target_);
    websocket_.async_accept(
        request,
        [self = shared_from_this()](const boost::system::error_code& error_code) {
          if (error_code) {
            return;
          }

          self->last_sequence_ = 1;
          self->websocket_registry_[self->session_id_].push_back(self);
          self->QueueInitialFrame();
          self->DoRead();
        });
  }

  void SendPendingOutput() {
    if (session_id_.empty()) {
      return;
    }

    const auto output = session_manager_.GetOutputSince(session_id_, last_sequence_);
    if (!output.has_value() || output->data.empty()) {
      return;
    }

    QueueFrame(ToJson(*output), output->seq_end + 1);
  }

  [[nodiscard]] auto session_id() const -> const std::string& { return session_id_; }

 private:
  struct PendingFrame {
    std::string payload;
    std::uint64_t next_sequence{1};
  };

  void QueueInitialFrame() {
    if (initial_frame_sent_) {
      return;
    }

    initial_frame_sent_ = true;
    QueueFrame(ToJson(vibe::session::OutputSlice{}), last_sequence_);
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
            return;
          }

          self->read_buffer_.consume(self->read_buffer_.size());
          self->DoRead();
        });
  }

  websocket::stream<tcp::socket> websocket_;
  vibe::service::SessionManager& session_manager_;
  WebSocketRegistry& websocket_registry_;
  beast::flat_buffer read_buffer_;
  std::deque<PendingFrame> pending_frames_;
  std::string target_;
  std::string session_id_;
  std::uint64_t last_sequence_{1};
  bool initial_frame_sent_{false};
  bool write_in_progress_{false};
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession(tcp::socket socket, vibe::service::SessionManager& session_manager,
              WebSocketRegistry& websocket_registry)
      : socket_(std::move(socket)),
        session_manager_(session_manager),
        websocket_registry_(websocket_registry) {}

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
            std::make_shared<WebSocketSession>(std::move(self->socket_), self->session_manager_,
                                               self->websocket_registry_)
                ->Start(std::move(self->request_));
            return;
          }

          self->response_ = HandleRequest(self->request_, self->session_manager_);
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
  WebSocketRegistry& websocket_registry_;
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
 public:
  HttpListener(asio::io_context& io_context, const asio::ip::address& address,
               const std::uint16_t port, vibe::service::SessionManager& session_manager)
      : acceptor_(io_context),
        socket_(io_context),
        poll_timer_(io_context),
        session_manager_(session_manager) {
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
    DoPoll();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        socket_, [self = shared_from_this()](const boost::system::error_code& error_code) {
            if (!error_code) {
            std::make_shared<HttpSession>(std::move(self->socket_), self->session_manager_,
                                          self->websocket_registry_)
                ->Start();
          }

          self->DoAccept();
        });
  }

  void DoPoll() {
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
      for (auto& [session_id, sessions] : self->websocket_registry_) {
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

  tcp::acceptor acceptor_;
  tcp::socket socket_;
  asio::steady_timer poll_timer_;
  vibe::service::SessionManager& session_manager_;
  WebSocketRegistry websocket_registry_;
};

}  // namespace

HttpServer::HttpServer(std::string bind_address, const std::uint16_t port)
    : bind_address_(std::move(bind_address)),
      port_(port) {}

HttpServer::~HttpServer() { Stop(); }

auto HttpServer::Run() -> bool {
  io_context_ = std::make_unique<asio::io_context>(1);
  boost::system::error_code error_code;
  const auto address = asio::ip::make_address(bind_address_, error_code);
  if (error_code) {
    std::cerr << "invalid bind address " << bind_address_ << ": " << error_code.message() << '\n';
    return false;
  }

  try {
    std::make_shared<HttpListener>(*io_context_, address, port_, session_manager_)->Start();
  } catch (const boost::system::system_error& exception) {
    std::cerr << "failed to bind " << bind_address_ << ":" << port_ << ": "
              << exception.code().message() << '\n';
    io_context_.reset();
    return false;
  }

  std::cout << "HTTP server listening on " << bind_address_ << ":" << port_ << '\n';
  io_context_->run();
  io_context_.reset();
  return true;
}

void HttpServer::Stop() {
  if (io_context_ != nullptr) {
    io_context_->stop();
  }
}

}  // namespace vibe::net

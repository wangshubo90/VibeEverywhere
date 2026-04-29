#ifndef VIBE_NET_HTTP_SHARED_H
#define VIBE_NET_HTTP_SHARED_H

#include <boost/beast/http.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "vibe/auth/authorizer.h"
#include "vibe/net/host_admin.h"
#include "vibe/auth/pairing.h"
#include "vibe/service/observation_store.h"
#include "vibe/service/session_manager.h"
#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"

namespace vibe::net {

namespace http = boost::beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

enum class ListenerRole {
  AdminLocal,
  RemoteClient,
};

struct HttpRouteContext {
  const vibe::auth::Authorizer* authorizer{nullptr};
  vibe::auth::PairingService* pairing_service{nullptr};
  vibe::store::PairingStore* pairing_store{nullptr};
  vibe::store::HostConfigStore* host_config_store{nullptr};
  vibe::net::HostAdmin* host_admin{nullptr};
  vibe::service::ObservationStore* observation_store{nullptr};
  std::function<void(const vibe::service::ObservationEvent&)> observation_event_sink{};
  std::filesystem::path storage_root{};
  std::string client_address{};
  bool is_local_request{false};
  bool remote_tls_enabled{false};
  std::string remote_listener_host{};
  std::uint16_t remote_listener_port{0};
  std::string remote_tls_certificate_path{};
  ListenerRole listener_role{ListenerRole::RemoteClient};
};

[[nodiscard]] auto HandleRequest(const HttpRequest& request,
                                 vibe::service::SessionManager& session_manager,
                                 const HttpRouteContext& context = {}) -> HttpResponse;

}  // namespace vibe::net

#endif

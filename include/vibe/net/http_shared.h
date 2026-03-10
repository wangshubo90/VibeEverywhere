#ifndef VIBE_NET_HTTP_SHARED_H
#define VIBE_NET_HTTP_SHARED_H

#include <boost/beast/http.hpp>

#include <string>

#include "vibe/auth/authorizer.h"
#include "vibe/net/host_admin.h"
#include "vibe/auth/pairing.h"
#include "vibe/service/session_manager.h"
#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"

namespace vibe::net {

namespace http = boost::beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

struct HttpRouteContext {
  const vibe::auth::Authorizer* authorizer{nullptr};
  vibe::auth::PairingService* pairing_service{nullptr};
  vibe::store::PairingStore* pairing_store{nullptr};
  vibe::store::HostConfigStore* host_config_store{nullptr};
  vibe::net::HostAdmin* host_admin{nullptr};
  std::string client_address;
  bool is_local_request{false};
};

[[nodiscard]] auto HandleRequest(const HttpRequest& request,
                                 vibe::service::SessionManager& session_manager,
                                 const HttpRouteContext& context = {}) -> HttpResponse;

}  // namespace vibe::net

#endif

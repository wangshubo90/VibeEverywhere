#include "vibe/net/http_shared.h"

#include <string>

namespace vibe::net {

auto HandleRequest(const HttpRequest& request) -> HttpResponse {
  HttpResponse response;
  response.version(request.version());
  response.keep_alive(false);

  if (request.method() == http::verb::get && request.target() == "/health") {
    response.result(http::status::ok);
    response.set(http::field::content_type, "text/plain; charset=utf-8");
    response.body() = "ok\n";
    response.prepare_payload();
    return response;
  }

  response.result(http::status::not_found);
  response.set(http::field::content_type, "text/plain; charset=utf-8");
  response.body() = "not found\n";
  response.prepare_payload();
  return response;
}

}  // namespace vibe::net

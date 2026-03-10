#ifndef VIBE_NET_HTTP_SHARED_H
#define VIBE_NET_HTTP_SHARED_H

#include <boost/beast/http.hpp>

namespace vibe::net {

namespace http = boost::beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

[[nodiscard]] auto HandleRequest(const HttpRequest& request) -> HttpResponse;

}  // namespace vibe::net

#endif

#include <gtest/gtest.h>

#include "vibe/net/http_shared.h"

namespace vibe::net {
namespace {

TEST(HttpSharedTest, ReturnsHealthResponse) {
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/health");
  request.version(11);

  const HttpResponse response = HandleRequest(request);
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "ok\n");
}

TEST(HttpSharedTest, ReturnsNotFoundForUnknownRoute) {
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/missing");
  request.version(11);

  const HttpResponse response = HandleRequest(request);
  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_EQ(response.body(), "not found\n");
}

}  // namespace
}  // namespace vibe::net

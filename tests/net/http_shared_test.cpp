#include <gtest/gtest.h>

#include "vibe/net/http_shared.h"

namespace vibe::net {
namespace {

auto MakeManager() -> vibe::service::SessionManager { return vibe::service::SessionManager(); }

TEST(HttpSharedTest, ReturnsHealthResponse) {
  auto session_manager = MakeManager();
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/health");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager);
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "ok\n");
  EXPECT_EQ(response[http::field::access_control_allow_origin], "*");
}

TEST(HttpSharedTest, ReturnsNotFoundForUnknownRoute) {
  auto session_manager = MakeManager();
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/missing");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager);
  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_NE(response.body().find("not found"), std::string::npos);
}

TEST(HttpSharedTest, ReturnsHostInfo) {
  auto session_manager = MakeManager();
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/host/info");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager);
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"hostId\":\"local-dev-host\""), std::string::npos);
  EXPECT_EQ(response[http::field::access_control_allow_origin], "*");
}

TEST(HttpSharedTest, ReturnsCorsPreflightResponse) {
  auto session_manager = MakeManager();
  HttpRequest request;
  request.method(http::verb::options);
  request.target("/sessions");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager);
  EXPECT_EQ(response.result(), http::status::no_content);
  EXPECT_EQ(response[http::field::access_control_allow_origin], "*");
  EXPECT_EQ(response[http::field::access_control_allow_methods], "GET, POST, OPTIONS");
  EXPECT_EQ(response[http::field::access_control_allow_headers], "content-type");
}

TEST(HttpSharedTest, CanCreateAndListSessions) {
  auto session_manager = MakeManager();

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();

  const HttpResponse create_response = HandleRequest(create_request, session_manager);
  EXPECT_EQ(create_response.result(), http::status::created);
  EXPECT_NE(create_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);

  HttpRequest list_request;
  list_request.method(http::verb::get);
  list_request.target("/sessions");
  list_request.version(11);

  const HttpResponse list_response = HandleRequest(list_request, session_manager);
  EXPECT_EQ(list_response.result(), http::status::ok);
  EXPECT_NE(list_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);
}

TEST(HttpSharedTest, ReturnsSessionDetailAndSnapshot) {
  auto session_manager = MakeManager();

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();
  const HttpResponse create_response = HandleRequest(create_request, session_manager);
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest detail_request;
  detail_request.method(http::verb::get);
  detail_request.target("/sessions/s_1");
  detail_request.version(11);
  const HttpResponse detail_response = HandleRequest(detail_request, session_manager);
  EXPECT_EQ(detail_response.result(), http::status::ok);
  EXPECT_NE(detail_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);

  HttpRequest snapshot_request;
  snapshot_request.method(http::verb::get);
  snapshot_request.target("/sessions/s_1/snapshot");
  snapshot_request.version(11);
  const HttpResponse snapshot_response = HandleRequest(snapshot_request, session_manager);
  EXPECT_EQ(snapshot_response.result(), http::status::ok);
  EXPECT_NE(snapshot_response.body().find("\"currentSequence\":0"), std::string::npos);
}

TEST(HttpSharedTest, RejectsInvalidCreateSessionBody) {
  auto session_manager = MakeManager();

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.body() = "{\"provider\":\"unknown\"}";
  create_request.prepare_payload();

  const HttpResponse response = HandleRequest(create_request, session_manager);
  EXPECT_EQ(response.result(), http::status::bad_request);
}

TEST(HttpSharedTest, CanFetchTailForExistingSession) {
  auto session_manager = MakeManager();

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();
  const HttpResponse create_response = HandleRequest(create_request, session_manager);
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest tail_request;
  tail_request.method(http::verb::get);
  tail_request.target("/sessions/s_1/tail?bytes=64");
  tail_request.version(11);

  const HttpResponse tail_response = HandleRequest(tail_request, session_manager);
  EXPECT_EQ(tail_response.result(), http::status::ok);
  EXPECT_NE(tail_response.body().find("\"seqStart\""), std::string::npos);
}

TEST(HttpSharedTest, RejectsInvalidInputRequest) {
  auto session_manager = MakeManager();

  HttpRequest input_request;
  input_request.method(http::verb::post);
  input_request.target("/sessions/s_1/input");
  input_request.version(11);
  input_request.body() = "{}";
  input_request.prepare_payload();

  const HttpResponse input_response = HandleRequest(input_request, session_manager);
  EXPECT_EQ(input_response.result(), http::status::bad_request);
}

TEST(HttpSharedTest, StopIsIdempotentForExitedSession) {
  auto session_manager = MakeManager();

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();
  const HttpResponse create_response = HandleRequest(create_request, session_manager);
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest stop_request;
  stop_request.method(http::verb::post);
  stop_request.target("/sessions/s_1/stop");
  stop_request.version(11);

  const HttpResponse first_stop = HandleRequest(stop_request, session_manager);
  EXPECT_EQ(first_stop.result(), http::status::ok);

  const HttpResponse second_stop = HandleRequest(stop_request, session_manager);
  EXPECT_EQ(second_stop.result(), http::status::ok);
}

}  // namespace
}  // namespace vibe::net

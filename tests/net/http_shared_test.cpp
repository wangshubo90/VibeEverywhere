#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "vibe/auth/authorizer.h"
#include "vibe/auth/pairing.h"
#include "vibe/net/http_shared.h"
#include "vibe/store/host_config_store.h"

namespace vibe::net {
namespace {

auto MakeManager() -> vibe::service::SessionManager { return {}; }

class FakeAuthorizer final : public vibe::auth::Authorizer {
 public:
  [[nodiscard]] auto AuthenticateBearerToken(const std::string& bearer_token) const
      -> vibe::auth::AuthResult override {
    if (bearer_token == "good-token") {
      return vibe::auth::AuthResult{
          .authenticated = true,
          .authorized = true,
          .device_id = vibe::auth::DeviceId{.value = "d_1"},
          .reason = "",
      };
    }

    return vibe::auth::AuthResult{
        .authenticated = false,
        .authorized = false,
        .device_id = std::nullopt,
        .reason = "missing token",
    };
  }

  [[nodiscard]] auto Authorize(const vibe::auth::RequestContext& request_context,
                               const vibe::auth::AuthorizationAction action) const
      -> vibe::auth::AuthResult override {
    if (request_context.is_local_request &&
        (action == vibe::auth::AuthorizationAction::ApprovePairing ||
         action == vibe::auth::AuthorizationAction::ConfigureHost)) {
      return vibe::auth::AuthResult{
          .authenticated = true,
          .authorized = true,
          .device_id = std::nullopt,
          .reason = "",
      };
    }

    return AuthenticateBearerToken(request_context.bearer_token);
  }
};

class FakePairingService final : public vibe::auth::PairingService {
 public:
  [[nodiscard]] auto StartPairing(const std::string& device_name,
                                  const vibe::auth::DeviceType device_type)
      -> std::optional<vibe::auth::PairingRequest> override {
    pending_pairings.push_back(vibe::auth::PairingRequest{
        .pairing_id = "p_1",
        .device_name = device_name,
        .device_type = device_type,
        .code = "123456",
        .requested_at_unix_ms = 1,
    });
    return pending_pairings.back();
  }

  [[nodiscard]] auto ListPendingPairings() const -> std::vector<vibe::auth::PairingRequest> override {
    return pending_pairings;
  }

  [[nodiscard]] auto ApprovePairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<vibe::auth::PairingRecord> override {
    if (pairing_id != "p_1" || code != "123456") {
      return std::nullopt;
    }

    pending_pairings.clear();
    return vibe::auth::PairingRecord{
        .device_id = vibe::auth::DeviceId{.value = "d_1"},
        .device_name = "browser",
        .device_type = vibe::auth::DeviceType::Browser,
        .bearer_token = "good-token",
        .approved_at_unix_ms = 2,
    };
  }

  [[nodiscard]] auto RejectPairing(const std::string& pairing_id) -> bool override {
    pending_pairings.clear();
    return pairing_id == "p_1";
  }

  mutable std::vector<vibe::auth::PairingRequest> pending_pairings;
};

class FakeHostConfigStore final : public vibe::store::HostConfigStore {
 public:
  [[nodiscard]] auto LoadHostIdentity() const -> std::optional<vibe::store::HostIdentity> override {
    return identity;
  }

  [[nodiscard]] auto SaveHostIdentity(const vibe::store::HostIdentity& new_identity) -> bool override {
    identity = new_identity;
    return true;
  }

  std::optional<vibe::store::HostIdentity> identity{
      vibe::store::HostIdentity{
          .host_id = "host_1",
          .display_name = "Dev Host",
          .certificate_pem_path = "/tmp/cert.pem",
          .private_key_pem_path = "/tmp/key.pem",
      },
  };
};

auto MakeAuthContext(FakeAuthorizer& authorizer,
                     FakePairingService& pairing_service,
                     FakeHostConfigStore& host_config_store) -> HttpRouteContext {
  return HttpRouteContext{
      .authorizer = &authorizer,
      .pairing_service = &pairing_service,
      .host_config_store = &host_config_store,
      .client_address = "127.0.0.1",
      .is_local_request = true,
  };
}

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
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/host/info");
  request.version(11);

  const HttpResponse response =
      HandleRequest(request, session_manager, MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"hostId\":\"host_1\""), std::string::npos);
  EXPECT_NE(response.body().find("\"pairingMode\":\"approval\""), std::string::npos);
  EXPECT_NE(response.body().find("\"tls\":{\"enabled\":false"), std::string::npos);
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
  EXPECT_EQ(response[http::field::access_control_allow_headers], "content-type, authorization");
}

TEST(HttpSharedTest, CanCreateAndListSessions) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();

  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);
  EXPECT_NE(create_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);

  HttpRequest list_request;
  list_request.method(http::verb::get);
  list_request.target("/sessions");
  list_request.version(11);
  list_request.set(http::field::authorization, "Bearer good-token");

  const HttpResponse list_response =
      HandleRequest(list_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(list_response.result(), http::status::ok);
  EXPECT_NE(list_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);
}

TEST(HttpSharedTest, ReturnsSessionDetailAndSnapshot) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest detail_request;
  detail_request.method(http::verb::get);
  detail_request.target("/sessions/s_1");
  detail_request.version(11);
  detail_request.set(http::field::authorization, "Bearer good-token");
  const HttpResponse detail_response =
      HandleRequest(detail_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(detail_response.result(), http::status::ok);
  EXPECT_NE(detail_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);

  HttpRequest snapshot_request;
  snapshot_request.method(http::verb::get);
  snapshot_request.target("/sessions/s_1/snapshot");
  snapshot_request.version(11);
  snapshot_request.set(http::field::authorization, "Bearer good-token");
  const HttpResponse snapshot_response =
      HandleRequest(snapshot_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
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
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest tail_request;
  tail_request.method(http::verb::get);
  tail_request.target("/sessions/s_1/tail?bytes=64");
  tail_request.version(11);
  tail_request.set(http::field::authorization, "Bearer good-token");

  const HttpResponse tail_response =
      HandleRequest(tail_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
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
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\"}";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest stop_request;
  stop_request.method(http::verb::post);
  stop_request.target("/sessions/s_1/stop");
  stop_request.version(11);
  stop_request.set(http::field::authorization, "Bearer good-token");

  const HttpResponse first_stop =
      HandleRequest(stop_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(first_stop.result(), http::status::ok);

  const HttpResponse second_stop =
      HandleRequest(stop_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(second_stop.result(), http::status::ok);
}

TEST(HttpSharedTest, RejectsUnauthorizedSessionRoute) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest request;
  request.method(http::verb::get);
  request.target("/sessions");
  request.version(11);

  const HttpResponse response =
      HandleRequest(request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(response.result(), http::status::unauthorized);
  EXPECT_NE(response.body().find("missing token"), std::string::npos);
}

TEST(HttpSharedTest, ServesLocalUiAndPairingRoutes) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest ui_request;
  ui_request.method(http::verb::get);
  ui_request.target("/ui");
  ui_request.version(11);

  const HttpResponse ui_response =
      HandleRequest(ui_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(ui_response.result(), http::status::ok);
  EXPECT_EQ(ui_response[http::field::content_type], "text/html; charset=utf-8");
  EXPECT_NE(ui_response.body().find("Host Approval"), std::string::npos);

  HttpRequest start_pairing_request;
  start_pairing_request.method(http::verb::post);
  start_pairing_request.target("/pairing/request");
  start_pairing_request.version(11);
  start_pairing_request.body() = R"({"deviceName":"Safari","deviceType":"browser"})";
  start_pairing_request.prepare_payload();

  const HttpResponse start_pairing_response =
      HandleRequest(start_pairing_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(start_pairing_response.result(), http::status::created);
  EXPECT_NE(start_pairing_response.body().find("\"pairingId\":\"p_1\""), std::string::npos);

  HttpRequest pending_request;
  pending_request.method(http::verb::get);
  pending_request.target("/pairing/pending");
  pending_request.version(11);

  const HttpResponse pending_response =
      HandleRequest(pending_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(pending_response.result(), http::status::ok);
  EXPECT_NE(pending_response.body().find("\"pairingId\":\"p_1\""), std::string::npos);

  HttpRequest approve_request;
  approve_request.method(http::verb::post);
  approve_request.target("/pairing/approve");
  approve_request.version(11);
  approve_request.body() = R"({"pairingId":"p_1","code":"123456"})";
  approve_request.prepare_payload();

  const HttpResponse approve_response =
      HandleRequest(approve_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(approve_response.result(), http::status::ok);
  EXPECT_NE(approve_response.body().find("\"token\":\"good-token\""), std::string::npos);
}

TEST(HttpSharedTest, RejectsHostUiRoutesForNonLocalRequests) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  const auto remote_context = HttpRouteContext{
      .authorizer = &authorizer,
      .pairing_service = &pairing_service,
      .host_config_store = &host_config_store,
      .client_address = "10.0.0.8",
      .is_local_request = false,
  };

  HttpRequest request;
  request.method(http::verb::get);
  request.target("/pairing/pending");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager, remote_context);
  EXPECT_EQ(response.result(), http::status::forbidden);
}

}  // namespace
}  // namespace vibe::net

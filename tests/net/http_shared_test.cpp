#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "vibe/auth/authorizer.h"
#include "vibe/auth/pairing.h"
#include "vibe/net/host_admin.h"
#include "vibe/net/json.h"
#include "vibe/net/http_shared.h"
#include "vibe/store/host_config_store.h"

namespace vibe::net {
namespace {

auto MakeManager() -> vibe::service::SessionManager { return vibe::service::SessionManager(); }

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
    approved_record = vibe::auth::PairingRecord{
        .device_id = vibe::auth::DeviceId{.value = "d_1"},
        .device_name = "browser",
        .device_type = vibe::auth::DeviceType::Browser,
        .bearer_token = "good-token",
        .approved_at_unix_ms = 2,
    };
    return approved_record;
  }

  [[nodiscard]] auto ClaimApprovedPairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<vibe::auth::PairingRecord> override {
    if (pairing_id != "p_1" || code != "123456") {
      return std::nullopt;
    }
    return approved_record;
  }

  [[nodiscard]] auto RejectPairing(const std::string& pairing_id) -> bool override {
    pending_pairings.clear();
    return pairing_id == "p_1";
  }

  mutable std::vector<vibe::auth::PairingRequest> pending_pairings;
  std::optional<vibe::auth::PairingRecord> approved_record;
};

class FakePairingStore final : public vibe::store::PairingStore {
 public:
  [[nodiscard]] auto LoadPendingPairings() const -> std::vector<vibe::auth::PairingRequest> override {
    return pending_pairings;
  }

  [[nodiscard]] auto LoadApprovedPairings() const -> std::vector<vibe::auth::PairingRecord> override {
    return approved_pairings;
  }

  [[nodiscard]] auto UpsertPendingPairing(const vibe::auth::PairingRequest& request) -> bool override {
    pending_pairings.push_back(request);
    return true;
  }

  [[nodiscard]] auto UpsertApprovedPairing(const vibe::auth::PairingRecord& record) -> bool override {
    approved_pairings.push_back(record);
    return true;
  }

  [[nodiscard]] auto RemovePendingPairing(const std::string& pairing_id) -> bool override {
    pending_pairings.erase(
        std::remove_if(pending_pairings.begin(), pending_pairings.end(),
                       [&](const vibe::auth::PairingRequest& pending) { return pending.pairing_id == pairing_id; }),
        pending_pairings.end());
    return true;
  }

  [[nodiscard]] auto RemoveApprovedPairing(const std::string& device_id) -> bool override {
    approved_pairings.erase(
        std::remove_if(approved_pairings.begin(), approved_pairings.end(),
                       [&](const vibe::auth::PairingRecord& record) { return record.device_id.value == device_id; }),
        approved_pairings.end());
    return true;
  }

  mutable std::vector<vibe::auth::PairingRequest> pending_pairings;
  mutable std::vector<vibe::auth::PairingRecord> approved_pairings;
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
      [] {
        auto identity = vibe::store::MakeDefaultHostIdentity();
        identity.host_id = "host_1";
        identity.display_name = "Dev Host";
        identity.certificate_pem_path = "/tmp/cert.pem";
        identity.private_key_pem_path = "/tmp/key.pem";
        return identity;
      }(),
  };
};

class FakeHostAdmin final : public vibe::net::HostAdmin {
 public:
  [[nodiscard]] auto ListAttachedClients() const -> std::vector<vibe::net::AttachedClientInfo> override {
    return clients;
  }

  [[nodiscard]] auto DisconnectClient(const std::string& client_id) -> bool override {
    last_disconnected_client_id = client_id;
    return client_id == "ws_s_1_1";
  }

  mutable std::vector<vibe::net::AttachedClientInfo> clients{
      vibe::net::AttachedClientInfo{
          .client_id = "ws_s_1_1",
          .session_id = "s_1",
          .session_title = "managed",
          .client_address = "127.0.0.1",
          .session_status = vibe::session::SessionStatus::Running,
          .session_is_recovered = false,
          .claimed_kind = vibe::session::ControllerKind::Remote,
          .is_local = false,
          .has_control = true,
          .connected_at_unix_ms = 123,
      },
  };
  mutable std::string last_disconnected_client_id;
};

auto MakeAuthContext(FakeAuthorizer& authorizer,
                     FakePairingService& pairing_service,
                     FakeHostConfigStore& host_config_store,
                     FakeHostAdmin* host_admin = nullptr,
                     FakePairingStore* pairing_store = nullptr,
                     const ListenerRole listener_role = ListenerRole::AdminLocal) -> HttpRouteContext {
  return HttpRouteContext{
      .authorizer = &authorizer,
      .pairing_service = &pairing_service,
      .pairing_store = pairing_store,
      .host_config_store = &host_config_store,
      .host_admin = host_admin,
      .client_address = "127.0.0.1",
      .is_local_request = true,
      .remote_listener_host = "127.0.0.1",
      .remote_listener_port = 18086,
      .remote_tls_certificate_path = "",
      .listener_role = listener_role,
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

TEST(HttpSharedTest, ReturnsHostInfoWithRemoteTlsEnabled) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/host/info");
  request.version(11);

  auto context = MakeAuthContext(authorizer, pairing_service, host_config_store);
  context.remote_tls_enabled = true;
  const HttpResponse response = HandleRequest(request, session_manager, context);
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"tls\":{\"enabled\":true"), std::string::npos);
}

TEST(HttpSharedTest, ReturnsDiscoveryInfo) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/discovery/info");
  request.version(11);

  const HttpResponse response =
      HandleRequest(request, session_manager, MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"hostId\":\"host_1\""), std::string::npos);
  EXPECT_NE(response.body().find("\"displayName\":\"Dev Host\""), std::string::npos);
  EXPECT_NE(response.body().find("\"remoteHost\":\"127.0.0.1\""), std::string::npos);
  EXPECT_NE(response.body().find("\"remotePort\":18086"), std::string::npos);
  EXPECT_NE(response.body().find("\"protocolVersion\":\"1\""), std::string::npos);
  EXPECT_NE(response.body().find("\"tls\":false"), std::string::npos);
}

TEST(HttpSharedTest, ReturnsDiscoveryInfoWithTlsEnabled) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  HttpRequest request;
  request.method(http::verb::get);
  request.target("/discovery/info");
  request.version(11);

  auto context = MakeAuthContext(authorizer, pairing_service, host_config_store);
  context.remote_tls_enabled = true;
  const HttpResponse response = HandleRequest(request, session_manager, context);
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"tls\":true"), std::string::npos);
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
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\",\"groupTags\":[\" Frontend \",\"mvp\",\"frontend\"]}";
  create_request.prepare_payload();

  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);
  EXPECT_NE(create_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(create_response.body().find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);

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
  EXPECT_NE(list_response.body().find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);
}

TEST(HttpSharedTest, AdminLocalCanCreateSessionWithoutBearerToken) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"local-admin\"}";
  create_request.prepare_payload();

  const HttpResponse create_response = HandleRequest(
      create_request, session_manager,
      MakeAuthContext(authorizer, pairing_service, host_config_store, nullptr, nullptr,
                      ListenerRole::AdminLocal));
  EXPECT_EQ(create_response.result(), http::status::created);
  EXPECT_NE(create_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);
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
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"new-session\",\"groupTags\":[\"frontend\"]}";
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
  EXPECT_NE(detail_response.body().find("\"groupTags\":[\"frontend\"]"), std::string::npos);

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
  EXPECT_NE(snapshot_response.body().find("\"groupTags\":[\"frontend\"]"), std::string::npos);
}

TEST(HttpSharedTest, MutatesSessionGroupTagsAndReadsBackNormalizedTags) {
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
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"tag-target\",\"groupTags\":[\"frontend\"]}";
  create_request.prepare_payload();
  EXPECT_EQ(HandleRequest(create_request, session_manager,
                          MakeAuthContext(authorizer, pairing_service, host_config_store))
                .result(),
            http::status::created);

  HttpRequest groups_request;
  groups_request.method(http::verb::post);
  groups_request.target("/sessions/s_1/groups");
  groups_request.version(11);
  groups_request.set(http::field::authorization, "Bearer good-token");
  groups_request.body() = R"({"mode":"add","tags":[" MVP ","frontend"]})";
  groups_request.prepare_payload();

  const HttpResponse groups_response =
      HandleRequest(groups_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(groups_response.result(), http::status::ok);
  EXPECT_NE(groups_response.body().find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);

  HttpRequest detail_request;
  detail_request.method(http::verb::get);
  detail_request.target("/sessions/s_1");
  detail_request.version(11);
  detail_request.set(http::field::authorization, "Bearer good-token");
  const HttpResponse detail_response =
      HandleRequest(detail_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(detail_response.result(), http::status::ok);
  EXPECT_NE(detail_response.body().find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);
}

TEST(HttpSharedTest, RejectsInvalidSessionGroupTagsMutationRequest) {
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
      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"tag-target\"}";
  create_request.prepare_payload();
  EXPECT_EQ(HandleRequest(create_request, session_manager,
                          MakeAuthContext(authorizer, pairing_service, host_config_store))
                .result(),
            http::status::created);

  HttpRequest groups_request;
  groups_request.method(http::verb::post);
  groups_request.target("/sessions/s_1/groups");
  groups_request.version(11);
  groups_request.set(http::field::authorization, "Bearer good-token");
  groups_request.body() = R"({"mode":"add","tags":["   "]})";
  groups_request.prepare_payload();

  const HttpResponse groups_response =
      HandleRequest(groups_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(groups_response.result(), http::status::bad_request);
  EXPECT_NE(groups_response.body().find("invalid session group tags request"), std::string::npos);
}

TEST(HttpSharedTest, ReturnsSessionFileContentWithinWorkspaceRoot) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-http-file-route-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root / "src");
  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 0; }\n";
  }

  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  vibe::service::SessionManager session_manager;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\"" +
      JsonEscape(temp_root.string()) +
      "\",\"title\":\"new-session\",\"command\":[\"/bin/sh\",\"-c\",\"sleep 30\"]}";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest file_request;
  file_request.method(http::verb::get);
  file_request.target("/sessions/s_1/file?path=src%2Fmain.cpp&bytes=1024");
  file_request.version(11);
  file_request.set(http::field::authorization, "Bearer good-token");
  const HttpResponse file_response =
      HandleRequest(file_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(file_response.result(), http::status::ok);
  EXPECT_NE(file_response.body().find("\"workspacePath\":\"src/main.cpp\""), std::string::npos);
  EXPECT_NE(file_response.body().find("\"contentBase64\":\"aW50IG1haW4oKSB7IHJldHVybiAwOyB9Cg==\""),
            std::string::npos);

  EXPECT_EQ(session_manager.Shutdown(), 1U);
  std::filesystem::remove_all(temp_root);
}

TEST(HttpSharedTest, RejectsInvalidSessionFilePath) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-http-file-invalid-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root);

  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  vibe::service::SessionManager session_manager;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\"" +
      JsonEscape(temp_root.string()) +
      "\",\"title\":\"new-session\",\"command\":[\"/bin/sh\",\"-c\",\"sleep 30\"]}";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest file_request;
  file_request.method(http::verb::get);
  file_request.target("/sessions/s_1/file?path=..%2Fsecret.txt");
  file_request.version(11);
  file_request.set(http::field::authorization, "Bearer good-token");
  const HttpResponse file_response =
      HandleRequest(file_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(file_response.result(), http::status::bad_request);
  EXPECT_NE(file_response.body().find("invalid file path"), std::string::npos);

  EXPECT_EQ(session_manager.Shutdown(), 1U);
  std::filesystem::remove_all(temp_root);
}

TEST(HttpSharedTest, RejectsMalformedFileByteLimit) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-http-file-bytes-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root / "src");
  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 0; }\n";
  }

  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  vibe::service::SessionManager session_manager;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() =
      "{\"provider\":\"codex\",\"workspaceRoot\":\"" +
      JsonEscape(temp_root.string()) +
      "\",\"title\":\"new-session\",\"command\":[\"/bin/sh\",\"-c\",\"sleep 30\"]}";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(create_response.result(), http::status::created);

  HttpRequest file_request;
  file_request.method(http::verb::get);
  file_request.target("/sessions/s_1/file?path=src%2Fmain.cpp&bytes=abc");
  file_request.version(11);
  file_request.set(http::field::authorization, "Bearer good-token");
  const HttpResponse file_response =
      HandleRequest(file_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(file_response.result(), http::status::bad_request);
  EXPECT_NE(file_response.body().find("invalid byte limit"), std::string::npos);

  EXPECT_EQ(session_manager.Shutdown(), 1U);
  std::filesystem::remove_all(temp_root);
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

TEST(HttpSharedTest, RejectsMalformedTailByteLimit) {
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
  tail_request.target("/sessions/s_1/tail?bytes=abc");
  tail_request.version(11);
  tail_request.set(http::field::authorization, "Bearer good-token");

  const HttpResponse tail_response =
      HandleRequest(tail_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(tail_response.result(), http::status::bad_request);
  EXPECT_NE(tail_response.body().find("invalid byte limit"), std::string::npos);
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
  const auto remote_context = MakeAuthContext(authorizer, pairing_service, host_config_store, nullptr,
                                              nullptr, ListenerRole::RemoteClient);

  HttpRequest request;
  request.method(http::verb::get);
  request.target("/sessions");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager, remote_context);
  EXPECT_EQ(response.result(), http::status::unauthorized);
  EXPECT_NE(response.body().find("missing token"), std::string::npos);
}

TEST(HttpSharedTest, ServesLocalUiAndPairingRoutes) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakePairingStore pairing_store;
  FakeHostConfigStore host_config_store;
  FakeHostAdmin host_admin;

  HttpRequest ui_request;
  ui_request.method(http::verb::get);
  ui_request.target("/ui");
  ui_request.version(11);

  const HttpResponse ui_response =
      HandleRequest(ui_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(ui_response.result(), http::status::ok);
  EXPECT_EQ(ui_response[http::field::content_type], "text/html; charset=utf-8");
  EXPECT_NE(ui_response.body().find("Host Admin"), std::string::npos);

  HttpRequest root_ui_request;
  root_ui_request.method(http::verb::get);
  root_ui_request.target("/");
  root_ui_request.version(11);

  const HttpResponse root_ui_response =
      HandleRequest(root_ui_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(root_ui_response.result(), http::status::ok);
  EXPECT_EQ(root_ui_response[http::field::content_type], "text/html; charset=utf-8");
  EXPECT_NE(root_ui_response.body().find("Host Admin"), std::string::npos);

  HttpRequest terminal_ui_request;
  terminal_ui_request.method(http::verb::get);
  terminal_ui_request.target("/ui/terminal?sessionId=s_1");
  terminal_ui_request.version(11);
  const HttpResponse terminal_ui_response =
      HandleRequest(terminal_ui_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(terminal_ui_response.result(), http::status::ok);
  EXPECT_NE(terminal_ui_response.body().find("Host Terminal"), std::string::npos);

  HttpRequest terminal_script_request;
  terminal_script_request.method(http::verb::get);
  terminal_script_request.target("/ui/terminal.js");
  terminal_script_request.version(11);
  const HttpResponse terminal_script_response =
      HandleRequest(terminal_script_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(terminal_script_response.result(), http::status::ok);
  EXPECT_EQ(terminal_script_response[http::field::content_type], "application/javascript; charset=utf-8");
  EXPECT_NE(terminal_script_response.body().find("host/local-token"), std::string::npos);

  HttpRequest vendor_css_request;
  vendor_css_request.method(http::verb::get);
  vendor_css_request.target("/assets/xterm/xterm.css");
  vendor_css_request.version(11);
  const HttpResponse vendor_css_response =
      HandleRequest(vendor_css_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(vendor_css_response.result(), http::status::ok);
  EXPECT_EQ(vendor_css_response[http::field::content_type], "text/css; charset=utf-8");
  EXPECT_NE(vendor_css_response.body().find(".xterm"), std::string::npos);

  HttpRequest local_token_request;
  local_token_request.method(http::verb::get);
  local_token_request.target("/host/local-token");
  local_token_request.version(11);
  const HttpResponse local_token_response =
      HandleRequest(local_token_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(local_token_response.result(), http::status::ok);
  EXPECT_NE(local_token_response.body().find("\"deviceId\":\"local_browser_host_ui\""), std::string::npos);
  EXPECT_NE(local_token_response.body().find("\"token\":\""), std::string::npos);

  HttpRequest start_pairing_request;
  start_pairing_request.method(http::verb::post);
  start_pairing_request.target("/pairing/request");
  start_pairing_request.version(11);
  start_pairing_request.body() = R"({"deviceName":"Safari","deviceType":"browser"})";
  start_pairing_request.prepare_payload();

  const HttpResponse start_pairing_response =
      HandleRequest(start_pairing_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(start_pairing_response.result(), http::status::created);
  EXPECT_NE(start_pairing_response.body().find("\"pairingId\":\"p_1\""), std::string::npos);

  HttpRequest pending_request;
  pending_request.method(http::verb::get);
  pending_request.target("/pairing/pending");
  pending_request.version(11);

  const HttpResponse pending_response =
      HandleRequest(pending_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
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
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(approve_response.result(), http::status::ok);
  EXPECT_NE(approve_response.body().find("\"token\":\"good-token\""), std::string::npos);

  HttpRequest claim_request;
  claim_request.method(http::verb::post);
  claim_request.target("/pairing/claim");
  claim_request.version(11);
  claim_request.body() = R"({"pairingId":"p_1","code":"123456"})";
  claim_request.prepare_payload();

  const HttpResponse claim_response =
      HandleRequest(claim_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin, &pairing_store));
  EXPECT_EQ(claim_response.result(), http::status::ok);
  EXPECT_NE(claim_response.body().find("\"token\":\"good-token\""), std::string::npos);
}

TEST(HttpSharedTest, ServesRemoteClientUiFromRemoteListener) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest request;
  request.method(http::verb::get);
  request.target("/");
  request.version(11);

  const HttpResponse response = HandleRequest(
      request, session_manager,
      MakeAuthContext(authorizer, pairing_service, host_config_store, nullptr, nullptr,
                      ListenerRole::RemoteClient));
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "text/html; charset=utf-8");
  EXPECT_NE(response.body().find("Remote Client"), std::string::npos);

  HttpRequest stylesheet_request;
  stylesheet_request.method(http::verb::get);
  stylesheet_request.target("/remote/app.css");
  stylesheet_request.version(11);
  const HttpResponse stylesheet_response = HandleRequest(
      stylesheet_request, session_manager,
      MakeAuthContext(authorizer, pairing_service, host_config_store, nullptr, nullptr,
                      ListenerRole::RemoteClient));
  EXPECT_EQ(stylesheet_response.result(), http::status::ok);
  EXPECT_EQ(stylesheet_response[http::field::content_type], "text/css; charset=utf-8");
  EXPECT_NE(stylesheet_response.body().find(".terminal-shell"), std::string::npos);

  HttpRequest script_request;
  script_request.method(http::verb::get);
  script_request.target("/remote/app.js");
  script_request.version(11);
  const HttpResponse script_response = HandleRequest(
      script_request, session_manager,
      MakeAuthContext(authorizer, pairing_service, host_config_store, nullptr, nullptr,
                      ListenerRole::RemoteClient));
  EXPECT_EQ(script_response.result(), http::status::ok);
  EXPECT_EQ(script_response[http::field::content_type], "application/javascript; charset=utf-8");
  EXPECT_NE(script_response.body().find("loadSelectedSnapshot"), std::string::npos);

  HttpRequest vendor_script_request;
  vendor_script_request.method(http::verb::get);
  vendor_script_request.target("/assets/xterm/xterm.js");
  vendor_script_request.version(11);
  const HttpResponse vendor_script_response = HandleRequest(
      vendor_script_request, session_manager,
      MakeAuthContext(authorizer, pairing_service, host_config_store, nullptr, nullptr,
                      ListenerRole::RemoteClient));
  EXPECT_EQ(vendor_script_response.result(), http::status::ok);
  EXPECT_EQ(vendor_script_response[http::field::content_type], "application/javascript; charset=utf-8");
  EXPECT_NE(vendor_script_response.body().find("Terminal"), std::string::npos);
}

TEST(HttpSharedTest, ServesHostManagementRoutes) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  FakeHostAdmin host_admin;

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/sessions");
  create_request.version(11);
  create_request.set(http::field::authorization, "Bearer good-token");
  create_request.body() = R"({"provider":"codex","workspaceRoot":".","title":"managed"})";
  create_request.prepare_payload();
  EXPECT_EQ(HandleRequest(create_request, session_manager,
                          MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin))
                .result(),
            http::status::created);

  HttpRequest sessions_request;
  sessions_request.method(http::verb::get);
  sessions_request.target("/host/sessions");
  sessions_request.version(11);
  const HttpResponse sessions_response =
      HandleRequest(sessions_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin));
  EXPECT_EQ(sessions_response.result(), http::status::ok);
  EXPECT_NE(sessions_response.body().find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(sessions_response.body().find("\"activityState\":\"quiet\""), std::string::npos);
  EXPECT_NE(sessions_response.body().find("\"supervisionState\":\"quiet\""), std::string::npos);
  EXPECT_NE(sessions_response.body().find("\"isRecovered\":false"), std::string::npos);
  EXPECT_NE(sessions_response.body().find("\"archivedRecord\":false"), std::string::npos);
  EXPECT_NE(sessions_response.body().find("\"inventoryState\":\"live\""), std::string::npos);
  EXPECT_NE(sessions_response.body().find("\"attachedClientCount\":1"), std::string::npos);

  HttpRequest clients_request;
  clients_request.method(http::verb::get);
  clients_request.target("/host/clients");
  clients_request.version(11);
  const HttpResponse clients_response =
      HandleRequest(clients_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin));
  EXPECT_EQ(clients_response.result(), http::status::ok);
  EXPECT_NE(clients_response.body().find("\"clientId\":\"ws_s_1_1\""), std::string::npos);
  EXPECT_NE(clients_response.body().find("\"sessionTitle\":\"managed\""), std::string::npos);
  EXPECT_NE(clients_response.body().find("\"sessionStatus\":\"Running\""), std::string::npos);
  EXPECT_NE(clients_response.body().find("\"connectedAtUnixMs\":123"), std::string::npos);

  HttpRequest disconnect_request;
  disconnect_request.method(http::verb::post);
  disconnect_request.target("/host/clients/ws_s_1_1/disconnect");
  disconnect_request.version(11);
  const HttpResponse disconnect_response =
      HandleRequest(disconnect_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin));
  EXPECT_EQ(disconnect_response.result(), http::status::ok);
  EXPECT_EQ(host_admin.last_disconnected_client_id, "ws_s_1_1");

  HttpRequest stop_request;
  stop_request.method(http::verb::post);
  stop_request.target("/host/sessions/s_1/stop");
  stop_request.version(11);
  const HttpResponse stop_response =
      HandleRequest(stop_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin));
  EXPECT_EQ(stop_response.result(), http::status::ok);
  EXPECT_NE(stop_response.body().find("\"status\":\"Exited\""), std::string::npos);

  HttpRequest clear_request;
  clear_request.method(http::verb::post);
  clear_request.target("/host/sessions/clear-inactive");
  clear_request.version(11);
  const HttpResponse clear_response =
      HandleRequest(clear_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin));
  EXPECT_EQ(clear_response.result(), http::status::ok);
  EXPECT_NE(clear_response.body().find("\"removedCount\":1"), std::string::npos);

  const HttpResponse sessions_after_clear =
      HandleRequest(sessions_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin));
  EXPECT_EQ(sessions_after_clear.result(), http::status::ok);
  EXPECT_EQ(sessions_after_clear.body(), "[]");
}

TEST(HttpSharedTest, ServesHostTrustedDeviceRoutesAndLocalSessionCreation) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;
  FakeHostAdmin host_admin;
  FakePairingStore pairing_store;
  pairing_store.approved_pairings = {
      vibe::auth::PairingRecord{
          .device_id = vibe::auth::DeviceId{.value = "device_a"},
          .device_name = "Alice Phone",
          .device_type = vibe::auth::DeviceType::Mobile,
          .bearer_token = "token-a",
          .approved_at_unix_ms = 100,
      },
      vibe::auth::PairingRecord{
          .device_id = vibe::auth::DeviceId{.value = "device_b"},
          .device_name = "Dev Browser",
          .device_type = vibe::auth::DeviceType::Browser,
          .bearer_token = "token-b",
          .approved_at_unix_ms = 200,
      },
  };

  HttpRequest trusted_devices_request;
  trusted_devices_request.method(http::verb::get);
  trusted_devices_request.target("/host/trusted-devices");
  trusted_devices_request.version(11);
  const HttpResponse trusted_devices_response =
      HandleRequest(trusted_devices_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin,
                                    &pairing_store));
  EXPECT_EQ(trusted_devices_response.result(), http::status::ok);
  EXPECT_NE(trusted_devices_response.body().find("\"deviceId\":\"device_a\""), std::string::npos);
  EXPECT_NE(trusted_devices_response.body().find("\"deviceName\":\"Alice Phone\""), std::string::npos);

  HttpRequest create_request;
  create_request.method(http::verb::post);
  create_request.target("/host/sessions");
  create_request.version(11);
  create_request.body() =
      R"({"provider":"codex","workspaceRoot":".","title":"local-host-ui","conversationId":"conv_hash"})";
  create_request.prepare_payload();
  const HttpResponse create_response =
      HandleRequest(create_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin,
                                    &pairing_store));
  EXPECT_EQ(create_response.result(), http::status::created);
  EXPECT_NE(create_response.body().find("\"title\":\"local-host-ui\""), std::string::npos);
  EXPECT_NE(create_response.body().find("\"conversationId\":\"conv_hash\""), std::string::npos);

  HttpRequest revoke_request;
  revoke_request.method(http::verb::post);
  revoke_request.target("/host/trusted-devices/device_a/expire");
  revoke_request.version(11);
  const HttpResponse revoke_response =
      HandleRequest(revoke_request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store, &host_admin,
                                    &pairing_store));
  EXPECT_EQ(revoke_response.result(), http::status::ok);
  EXPECT_EQ(pairing_store.LoadApprovedPairings().size(), 1U);
  EXPECT_EQ(pairing_store.LoadApprovedPairings()[0].device_id.value, "device_b");
}

TEST(HttpSharedTest, SavesExpandedHostConfig) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  HttpRequest request;
  request.method(http::verb::post);
  request.target("/host/config");
  request.version(11);
  request.body() =
      R"({"displayName":"Ops Host","adminHost":"127.0.0.1","adminPort":19085,"remoteHost":"192.168.1.10","remotePort":19086,"providerCommands":{"codex":["/opt/bin/codex","--fast"],"claude":["/opt/bin/claude","--print"]}})";
  request.prepare_payload();

  const HttpResponse response =
      HandleRequest(request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"displayName\":\"Ops Host\""), std::string::npos);
  EXPECT_NE(response.body().find("\"adminHost\":\"127.0.0.1\""), std::string::npos);
  EXPECT_NE(response.body().find("\"remoteHost\":\"192.168.1.10\""), std::string::npos);
  EXPECT_NE(response.body().find("\"providerCommands\""), std::string::npos);

  ASSERT_TRUE(host_config_store.identity.has_value());
  EXPECT_EQ(host_config_store.identity->admin_port, 19085);
  EXPECT_EQ(host_config_store.identity->remote_port, 19086);
  EXPECT_EQ(host_config_store.identity->codex_command.executable, "/opt/bin/codex");
  EXPECT_EQ(host_config_store.identity->claude_command.executable, "/opt/bin/claude");
}

TEST(HttpSharedTest, ServesLocalTlsCertificateDownload) {
  auto session_manager = MakeManager();
  FakeAuthorizer authorizer;
  FakePairingService pairing_service;
  FakeHostConfigStore host_config_store;

  const auto certificate_path =
      std::filesystem::temp_directory_path() / "vibe-http-shared-test-cert.pem";
  {
    std::ofstream output(certificate_path);
    ASSERT_TRUE(output.is_open());
    output << "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";
  }

  host_config_store.identity->certificate_pem_path = certificate_path.string();

  HttpRequest request;
  request.method(http::verb::get);
  request.target("/host/tls/certificate");
  request.version(11);

  const HttpResponse response =
      HandleRequest(request, session_manager,
                    MakeAuthContext(authorizer, pairing_service, host_config_store));
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "application/x-pem-file");
  EXPECT_NE(response.body().find("BEGIN CERTIFICATE"), std::string::npos);

  std::filesystem::remove(certificate_path);
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
      .remote_listener_host = "",
      .remote_listener_port = 0,
      .remote_tls_certificate_path = "",
      .listener_role = ListenerRole::RemoteClient,
  };

  HttpRequest request;
  request.method(http::verb::get);
  request.target("/pairing/pending");
  request.version(11);

  const HttpResponse response = HandleRequest(request, session_manager, remote_context);
  EXPECT_EQ(response.result(), http::status::not_found);
}

}  // namespace
}  // namespace vibe::net

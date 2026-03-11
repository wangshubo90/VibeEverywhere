#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "vibe/auth/authorizer.h"
#include "vibe/auth/pairing.h"
#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"
#include "vibe/store/session_store.h"

namespace {

class FakePairingService final : public vibe::auth::PairingService {
 public:
  [[nodiscard]] auto StartPairing(const std::string& device_name,
                                  const vibe::auth::DeviceType device_type)
      -> std::optional<vibe::auth::PairingRequest> override {
    return vibe::auth::PairingRequest{
        .pairing_id = "p_1",
        .device_name = device_name,
        .device_type = device_type,
        .code = "123456",
        .requested_at_unix_ms = 1,
    };
  }

  [[nodiscard]] auto ListPendingPairings() const -> std::vector<vibe::auth::PairingRequest> override {
    return {};
  }

  [[nodiscard]] auto ApprovePairing(const std::string& /*pairing_id*/,
                                    const std::string& /*code*/)
      -> std::optional<vibe::auth::PairingRecord> override {
    return vibe::auth::PairingRecord{
        .device_id = vibe::auth::DeviceId{.value = "d_1"},
        .device_name = "phone",
        .device_type = vibe::auth::DeviceType::Mobile,
        .bearer_token = "token",
        .approved_at_unix_ms = 2,
    };
  }

  [[nodiscard]] auto ClaimApprovedPairing(const std::string& /*pairing_id*/,
                                          const std::string& /*code*/)
      -> std::optional<vibe::auth::PairingRecord> override {
    return vibe::auth::PairingRecord{
        .device_id = vibe::auth::DeviceId{.value = "d_1"},
        .device_name = "phone",
        .device_type = vibe::auth::DeviceType::Mobile,
        .bearer_token = "token",
        .approved_at_unix_ms = 2,
    };
  }

  [[nodiscard]] auto RejectPairing(const std::string& /*pairing_id*/) -> bool override { return true; }
};

class FakeAuthorizer final : public vibe::auth::Authorizer {
 public:
  [[nodiscard]] auto AuthenticateBearerToken(const std::string& bearer_token) const
      -> vibe::auth::AuthResult override {
    return vibe::auth::AuthResult{
        .authenticated = !bearer_token.empty(),
        .authorized = !bearer_token.empty(),
        .device_id = bearer_token.empty() ? std::nullopt
                                          : std::optional<vibe::auth::DeviceId>{{.value = "d_1"}},
        .reason = bearer_token.empty() ? "missing token" : "",
    };
  }

  [[nodiscard]] auto Authorize(const vibe::auth::RequestContext& request_context,
                               const vibe::auth::AuthorizationAction /*action*/) const
      -> vibe::auth::AuthResult override {
    return AuthenticateBearerToken(request_context.bearer_token);
  }
};

class FakeSessionStore final : public vibe::store::SessionStore {
 public:
  [[nodiscard]] auto LoadSessions() const -> std::vector<vibe::store::PersistedSessionRecord> override {
    return sessions;
  }

  [[nodiscard]] auto UpsertSessionRecord(const vibe::store::PersistedSessionRecord& record) -> bool override {
    sessions = {record};
    return true;
  }

  [[nodiscard]] auto RemoveSessionRecord(const std::string& session_id) -> bool override {
    sessions.clear();
    return session_id == "s_1";
  }

  mutable std::vector<vibe::store::PersistedSessionRecord> sessions;
};

class FakePairingStore final : public vibe::store::PairingStore {
 public:
  [[nodiscard]] auto LoadPendingPairings() const -> std::vector<vibe::auth::PairingRequest> override {
    return pending;
  }

  [[nodiscard]] auto LoadApprovedPairings() const -> std::vector<vibe::auth::PairingRecord> override {
    return approved;
  }

  [[nodiscard]] auto UpsertPendingPairing(const vibe::auth::PairingRequest& request) -> bool override {
    pending = {request};
    return true;
  }

  [[nodiscard]] auto UpsertApprovedPairing(const vibe::auth::PairingRecord& record) -> bool override {
    approved = {record};
    return true;
  }

  [[nodiscard]] auto RemovePendingPairing(const std::string& pairing_id) -> bool override {
    pending.clear();
    return pairing_id == "p_1";
  }

  [[nodiscard]] auto RemoveApprovedPairing(const std::string& device_id) -> bool override {
    approved.clear();
    return device_id == "d_1";
  }

  mutable std::vector<vibe::auth::PairingRequest> pending;
  mutable std::vector<vibe::auth::PairingRecord> approved;
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

  std::optional<vibe::store::HostIdentity> identity;
};

TEST(InterfaceContractTest, PairingAndStoreSeamsSupportBasicUsage) {
  FakePairingService pairing_service;
  FakeAuthorizer authorizer;
  FakeSessionStore session_store;
  FakePairingStore pairing_store;
  FakeHostConfigStore host_config_store;

  const auto pairing_request =
      pairing_service.StartPairing("browser", vibe::auth::DeviceType::Browser);
  ASSERT_TRUE(pairing_request.has_value());
  EXPECT_EQ(pairing_request->pairing_id, "p_1");

  const auto auth_result = authorizer.Authorize(
      vibe::auth::RequestContext{
          .bearer_token = "token",
          .client_address = "127.0.0.1",
          .target = "/sessions",
          .is_websocket = false,
          .is_local_request = false,
      },
      vibe::auth::AuthorizationAction::ObserveSessions);
  EXPECT_TRUE(auth_result.authenticated);
  EXPECT_TRUE(auth_result.authorized);

  const auto persisted = vibe::store::PersistedSessionRecord{
      .session_id = "s_1",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = "/tmp/project",
      .title = "demo",
      .status = vibe::session::SessionStatus::Running,
      .conversation_id = std::nullopt,
      .current_sequence = 12,
      .recent_terminal_tail = "tail",
  };
  EXPECT_TRUE(session_store.UpsertSessionRecord(persisted));
  EXPECT_EQ(session_store.LoadSessions(), std::vector<vibe::store::PersistedSessionRecord>{persisted});

  EXPECT_TRUE(pairing_store.UpsertPendingPairing(*pairing_request));
  EXPECT_EQ(pairing_store.LoadPendingPairings().size(), 1U);

  const auto pairing_record = pairing_service.ApprovePairing("p_1", "123456");
  ASSERT_TRUE(pairing_record.has_value());
  EXPECT_TRUE(pairing_store.UpsertApprovedPairing(*pairing_record));
  EXPECT_EQ(pairing_store.LoadApprovedPairings().size(), 1U);

  auto host_identity = vibe::store::MakeDefaultHostIdentity();
  host_identity.host_id = "host_1";
  host_identity.display_name = "Dev Host";
  host_identity.certificate_pem_path = "/tmp/cert.pem";
  host_identity.private_key_pem_path = "/tmp/key.pem";
  EXPECT_TRUE(host_config_store.SaveHostIdentity(host_identity));
  EXPECT_EQ(host_config_store.LoadHostIdentity(), std::optional<vibe::store::HostIdentity>{host_identity});
}

}  // namespace

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "vibe/auth/default_authorizer.h"
#include "vibe/auth/default_pairing_service.h"
#include "vibe/store/pairing_store.h"

namespace vibe::auth {
namespace {

class FakePairingStore final : public vibe::store::PairingStore {
 public:
  [[nodiscard]] auto LoadPendingPairings() const -> std::vector<PairingRequest> override {
    return pending_pairings;
  }

  [[nodiscard]] auto LoadApprovedPairings() const -> std::vector<PairingRecord> override {
    return approved_pairings;
  }

  [[nodiscard]] auto UpsertPendingPairing(const PairingRequest& request) -> bool override {
    if (!allow_pending_upsert) {
      return false;
    }

    for (auto& pending : pending_pairings) {
      if (pending.pairing_id == request.pairing_id) {
        pending = request;
        return true;
      }
    }

    pending_pairings.push_back(request);
    return true;
  }

  [[nodiscard]] auto UpsertApprovedPairing(const PairingRecord& record) -> bool override {
    if (!allow_approved_upsert) {
      return false;
    }

    for (auto& approved : approved_pairings) {
      if (approved.device_id == record.device_id) {
        approved = record;
        return true;
      }
    }

    approved_pairings.push_back(record);
    return true;
  }

  [[nodiscard]] auto RemovePendingPairing(const std::string& pairing_id) -> bool override {
    if (!allow_pending_remove) {
      return false;
    }

    const auto original_size = pending_pairings.size();
    pending_pairings.erase(
        std::remove_if(pending_pairings.begin(), pending_pairings.end(),
                       [&](const PairingRequest& pending) { return pending.pairing_id == pairing_id; }),
        pending_pairings.end());
    return pending_pairings.size() != original_size;
  }

  [[nodiscard]] auto RemoveApprovedPairing(const std::string& device_id) -> bool override {
    const auto original_size = approved_pairings.size();
    approved_pairings.erase(
        std::remove_if(approved_pairings.begin(), approved_pairings.end(),
                       [&](const PairingRecord& approved) {
                         return approved.device_id.value == device_id;
                       }),
        approved_pairings.end());
    return approved_pairings.size() != original_size;
  }

  bool allow_pending_upsert{true};
  bool allow_approved_upsert{true};
  bool allow_pending_remove{true};
  std::vector<PairingRequest> pending_pairings;
  std::vector<PairingRecord> approved_pairings;
};

class AuthCoreTest : public ::testing::Test {
 protected:
  AuthCoreTest()
      : pairing_service(
            pairing_store,
            [] { return 1111; },
            [] { return "p_123"; },
            [] { return "481923"; },
            [] { return "d_123"; },
            [] { return "token_abc"; }),
        authorizer(pairing_store) {}

  FakePairingStore pairing_store;
  DefaultPairingService pairing_service;
  DefaultAuthorizer authorizer;
};

TEST_F(AuthCoreTest, StartsPairingAndListsPendingRequest) {
  const auto request = pairing_service.StartPairing("Shubo iPhone", DeviceType::Mobile);

  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->pairing_id, "p_123");
  EXPECT_EQ(request->device_name, "Shubo iPhone");
  EXPECT_EQ(request->device_type, DeviceType::Mobile);
  EXPECT_EQ(request->code, "481923");
  EXPECT_EQ(request->requested_at_unix_ms, 1111);

  EXPECT_EQ(pairing_service.ListPendingPairings(), std::vector<PairingRequest>{*request});
}

TEST_F(AuthCoreTest, ApprovesPairingAndAuthenticatesIssuedBearerToken) {
  ASSERT_TRUE(pairing_service.StartPairing("Shubo iPhone", DeviceType::Mobile).has_value());

  const auto record = pairing_service.ApprovePairing("p_123", "481923");

  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->device_id.value, "d_123");
  EXPECT_EQ(record->device_name, "Shubo iPhone");
  EXPECT_EQ(record->device_type, DeviceType::Mobile);
  EXPECT_EQ(record->bearer_token, "token_abc");
  EXPECT_EQ(record->approved_at_unix_ms, 1111);
  EXPECT_TRUE(pairing_service.ListPendingPairings().empty());
  ASSERT_EQ(pairing_store.approved_pairings.size(), 1U);

  const auto auth_result = authorizer.AuthenticateBearerToken("token_abc");
  EXPECT_TRUE(auth_result.authenticated);
  EXPECT_TRUE(auth_result.authorized);
  ASSERT_TRUE(auth_result.device_id.has_value());
  EXPECT_EQ(auth_result.device_id->value, "d_123");
  EXPECT_TRUE(auth_result.reason.empty());
}

TEST_F(AuthCoreTest, RejectsPendingPairingRequest) {
  ASSERT_TRUE(pairing_service.StartPairing("Browser", DeviceType::Browser).has_value());

  EXPECT_TRUE(pairing_service.RejectPairing("p_123"));
  EXPECT_TRUE(pairing_service.ListPendingPairings().empty());
}

TEST_F(AuthCoreTest, RejectsApprovalForMismatchedCodeAndLeavesPendingRequest) {
  ASSERT_TRUE(pairing_service.StartPairing("Browser", DeviceType::Browser).has_value());

  const auto record = pairing_service.ApprovePairing("p_123", "000000");

  EXPECT_FALSE(record.has_value());
  ASSERT_EQ(pairing_service.ListPendingPairings().size(), 1U);
  EXPECT_TRUE(pairing_store.approved_pairings.empty());
}

TEST_F(AuthCoreTest, FailsPairingStartWhenStoreCannotPersistPendingRequest) {
  pairing_store.allow_pending_upsert = false;

  const auto request = pairing_service.StartPairing("Desktop", DeviceType::Desktop);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(pairing_service.ListPendingPairings().empty());
}

TEST_F(AuthCoreTest, RollsBackApprovedRecordIfPendingRemovalFails) {
  ASSERT_TRUE(pairing_service.StartPairing("Desktop", DeviceType::Desktop).has_value());
  pairing_store.allow_pending_remove = false;

  const auto record = pairing_service.ApprovePairing("p_123", "481923");

  EXPECT_FALSE(record.has_value());
  ASSERT_EQ(pairing_service.ListPendingPairings().size(), 1U);
  EXPECT_TRUE(pairing_store.approved_pairings.empty());
}

TEST_F(AuthCoreTest, DeniesMissingOrUnknownBearerTokens) {
  const auto missing_result = authorizer.AuthenticateBearerToken("");
  EXPECT_FALSE(missing_result.authenticated);
  EXPECT_FALSE(missing_result.authorized);
  EXPECT_EQ(missing_result.reason, "missing bearer token");

  const auto unknown_result = authorizer.AuthenticateBearerToken("nope");
  EXPECT_FALSE(unknown_result.authenticated);
  EXPECT_FALSE(unknown_result.authorized);
  EXPECT_EQ(unknown_result.reason, "invalid bearer token");
}

TEST_F(AuthCoreTest, RestrictsAuthorizationByActionAndRequestOrigin) {
  pairing_store.approved_pairings.push_back(PairingRecord{
      .device_id = DeviceId{.value = "d_123"},
      .device_name = "Shubo iPhone",
      .device_type = DeviceType::Mobile,
      .bearer_token = "token_abc",
      .approved_at_unix_ms = 1111,
  });

  const auto observe_result =
      authorizer.Authorize(RequestContext{
                               .bearer_token = "token_abc",
                               .client_address = "203.0.113.7",
                               .target = "/sessions",
                               .is_websocket = false,
                               .is_local_request = false,
                           },
                           AuthorizationAction::ObserveSessions);
  EXPECT_TRUE(observe_result.authenticated);
  EXPECT_TRUE(observe_result.authorized);

  const auto remote_approval =
      authorizer.Authorize(RequestContext{
                               .bearer_token = "token_abc",
                               .client_address = "203.0.113.7",
                               .target = "/pairing/approve",
                               .is_websocket = false,
                               .is_local_request = false,
                           },
                           AuthorizationAction::ApprovePairing);
  EXPECT_TRUE(remote_approval.authenticated);
  EXPECT_FALSE(remote_approval.authorized);
  EXPECT_EQ(remote_approval.reason, "local request required");

  const auto local_approval =
      authorizer.Authorize(RequestContext{
                               .bearer_token = "",
                               .client_address = "127.0.0.1",
                               .target = "/pairing/approve",
                               .is_websocket = false,
                               .is_local_request = true,
                           },
                           AuthorizationAction::ApprovePairing);
  EXPECT_TRUE(local_approval.authenticated);
  EXPECT_TRUE(local_approval.authorized);
  EXPECT_EQ(local_approval.reason, "");
}

}  // namespace
}  // namespace vibe::auth

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include "vibe/store/file_stores.h"

namespace vibe::store {
namespace {

class FileStoresTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() /
                ("vibe-store-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  [[nodiscard]] auto storage_root() const -> const std::filesystem::path& { return test_dir_; }

 private:
  std::filesystem::path test_dir_;
};

TEST_F(FileStoresTest, HostIdentityRoundTripsAcrossReload) {
  const HostIdentity expected{
      .host_id = "host_123",
      .display_name = "Local Dev Host",
      .certificate_pem_path = "/tmp/cert.pem",
      .private_key_pem_path = "/tmp/key.pem",
  };

  {
    FileHostConfigStore store(storage_root());
    EXPECT_TRUE(store.SaveHostIdentity(expected));
  }

  {
    FileHostConfigStore store(storage_root());
    EXPECT_EQ(store.LoadHostIdentity(), std::optional<HostIdentity>{expected});
  }
}

TEST_F(FileStoresTest, PairingsRoundTripAcrossReloadAndSupportUpsertRemoval) {
  const vibe::auth::PairingRequest pending_a{
      .pairing_id = "pairing_a",
      .device_name = "Alice Browser",
      .device_type = vibe::auth::DeviceType::Browser,
      .code = "123456",
      .requested_at_unix_ms = 100,
  };
  const vibe::auth::PairingRequest pending_b{
      .pairing_id = "pairing_a",
      .device_name = "Alice Browser Updated",
      .device_type = vibe::auth::DeviceType::Desktop,
      .code = "999999",
      .requested_at_unix_ms = 101,
  };
  const vibe::auth::PairingRecord approved_a{
      .device_id = vibe::auth::DeviceId{.value = "device_a"},
      .device_name = "Alice Phone",
      .device_type = vibe::auth::DeviceType::Mobile,
      .bearer_token = "token_a",
      .approved_at_unix_ms = 200,
  };
  const vibe::auth::PairingRecord approved_b{
      .device_id = vibe::auth::DeviceId{.value = "device_a"},
      .device_name = "Alice Phone Updated",
      .device_type = vibe::auth::DeviceType::Desktop,
      .bearer_token = "token_b",
      .approved_at_unix_ms = 201,
  };

  {
    FilePairingStore store(storage_root());
    EXPECT_TRUE(store.UpsertPendingPairing(pending_a));
    EXPECT_TRUE(store.UpsertPendingPairing(pending_b));
    EXPECT_TRUE(store.UpsertApprovedPairing(approved_a));
    EXPECT_TRUE(store.UpsertApprovedPairing(approved_b));
  }

  {
    FilePairingStore store(storage_root());
    EXPECT_EQ(store.LoadPendingPairings(),
              std::vector<vibe::auth::PairingRequest>{pending_b});
    EXPECT_EQ(store.LoadApprovedPairings(),
              std::vector<vibe::auth::PairingRecord>{approved_b});

    EXPECT_TRUE(store.RemovePendingPairing("pairing_a"));
    EXPECT_TRUE(store.RemoveApprovedPairing("device_a"));
    EXPECT_FALSE(store.RemovePendingPairing("missing"));
    EXPECT_FALSE(store.RemoveApprovedPairing("missing"));
  }

  {
    FilePairingStore store(storage_root());
    EXPECT_TRUE(store.LoadPendingPairings().empty());
    EXPECT_TRUE(store.LoadApprovedPairings().empty());
  }
}

TEST_F(FileStoresTest, SessionRecordsRoundTripAcrossReloadAndPreserveTerminalTailBytes) {
  const PersistedSessionRecord initial{
      .session_id = "session_1",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/project-a",
      .title = "First Session",
      .status = vibe::session::SessionStatus::Running,
      .current_sequence = 42,
      .recent_terminal_tail = std::string("tail\0bytes\n", 11),
  };
  const PersistedSessionRecord updated{
      .session_id = "session_1",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/project-b",
      .title = "Recovered Session",
      .status = vibe::session::SessionStatus::Exited,
      .current_sequence = 84,
      .recent_terminal_tail = std::string("\x1b[31merror\x1b[0m\n", 15),
  };
  const PersistedSessionRecord other{
      .session_id = "session_2",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = "/tmp/project-c",
      .title = "Other Session",
      .status = vibe::session::SessionStatus::Error,
      .current_sequence = 7,
      .recent_terminal_tail = "fatal\n",
  };

  {
    FileSessionStore store(storage_root());
    EXPECT_TRUE(store.UpsertSessionRecord(initial));
    EXPECT_TRUE(store.UpsertSessionRecord(updated));
    EXPECT_TRUE(store.UpsertSessionRecord(other));
  }

  {
    FileSessionStore store(storage_root());
    EXPECT_EQ(store.LoadSessions(),
              (std::vector<PersistedSessionRecord>{updated, other}));

    EXPECT_TRUE(store.RemoveSessionRecord("session_1"));
    EXPECT_FALSE(store.RemoveSessionRecord("missing"));
  }

  {
    FileSessionStore store(storage_root());
    EXPECT_EQ(store.LoadSessions(),
              (std::vector<PersistedSessionRecord>{other}));
  }
}

TEST_F(FileStoresTest, MissingFilesLoadAsEmptyState) {
  FileHostConfigStore host_store(storage_root());
  FilePairingStore pairing_store(storage_root());
  FileSessionStore session_store(storage_root());

  EXPECT_FALSE(host_store.LoadHostIdentity().has_value());
  EXPECT_TRUE(pairing_store.LoadPendingPairings().empty());
  EXPECT_TRUE(pairing_store.LoadApprovedPairings().empty());
  EXPECT_TRUE(session_store.LoadSessions().empty());
}

}  // namespace
}  // namespace vibe::store

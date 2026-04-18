#ifndef VIBE_STORE_FILE_STORES_H
#define VIBE_STORE_FILE_STORES_H

#include <filesystem>

#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"
#include "vibe/store/session_store.h"

namespace vibe::store {

class FileHostConfigStore final : public HostConfigStore {
 public:
  explicit FileHostConfigStore(std::filesystem::path storage_root);

  [[nodiscard]] auto LoadHostIdentity() const -> std::optional<HostIdentity> override;
  [[nodiscard]] auto SaveHostIdentity(const HostIdentity& identity) -> bool override;
  [[nodiscard]] auto storage_root() const -> std::filesystem::path override;

 private:
  std::filesystem::path storage_root_;
};

class FilePairingStore final : public PairingStore {
 public:
  explicit FilePairingStore(std::filesystem::path storage_root);

  [[nodiscard]] auto LoadPendingPairings() const -> std::vector<vibe::auth::PairingRequest> override;
  [[nodiscard]] auto LoadApprovedPairings() const -> std::vector<vibe::auth::PairingRecord> override;
  [[nodiscard]] auto UpsertPendingPairing(const vibe::auth::PairingRequest& request) -> bool override;
  [[nodiscard]] auto UpsertApprovedPairing(const vibe::auth::PairingRecord& record) -> bool override;
  [[nodiscard]] auto RemovePendingPairing(const std::string& pairing_id) -> bool override;
  [[nodiscard]] auto RemoveApprovedPairing(const std::string& device_id) -> bool override;

 private:
  std::filesystem::path storage_root_;
};

class FileSessionStore final : public SessionStore {
 public:
  explicit FileSessionStore(std::filesystem::path storage_root);

  [[nodiscard]] auto LoadSessions() const -> std::vector<PersistedSessionRecord> override;
  [[nodiscard]] auto UpsertSessionRecord(const PersistedSessionRecord& record) -> bool override;
  [[nodiscard]] auto RemoveSessionRecord(const std::string& session_id) -> bool override;

 private:
  std::filesystem::path storage_root_;
};

}  // namespace vibe::store

#endif

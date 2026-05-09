#ifndef VIBE_STORE_FILE_STORES_H
#define VIBE_STORE_FILE_STORES_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

struct PromptTemplateSummary {
  std::string id;
  std::string title;
  std::int64_t updated_at_unix_ms = 0;
  std::size_t size_bytes = 0;
};

struct PromptTemplateRecord {
  PromptTemplateSummary summary;
  std::string content;
};

class FilePromptTemplateStore final {
 public:
  explicit FilePromptTemplateStore(std::filesystem::path storage_root);

  [[nodiscard]] auto ListTemplates() const -> std::vector<PromptTemplateSummary>;
  [[nodiscard]] auto LoadTemplate(const std::string& template_id) const
      -> std::optional<PromptTemplateRecord>;
  [[nodiscard]] auto UpsertTemplate(const std::string& template_id,
                                    const std::string& title,
                                    const std::string& content,
                                    std::int64_t updated_at_unix_ms) const -> bool;
  [[nodiscard]] auto RemoveTemplate(const std::string& template_id) const -> bool;

 private:
  std::filesystem::path storage_root_;
};

struct StoredEvidenceSummary {
  std::string evidence_id;
  std::string session_id;
  std::string kind;
  std::string title;
  std::string content_type;
  std::int64_t created_at_unix_ms = 0;
  std::size_t size_bytes = 0;
};

struct StoredEvidenceRecord {
  StoredEvidenceSummary summary;
  std::optional<std::string> markdown_content;
  std::optional<std::string> evidence_json;
};

class FileStoredEvidenceStore final {
 public:
  explicit FileStoredEvidenceStore(std::filesystem::path storage_root);

  [[nodiscard]] auto ListEvidence(const std::string& session_id) const -> std::vector<StoredEvidenceSummary>;
  [[nodiscard]] auto LoadEvidence(const std::string& session_id,
                                  const std::string& evidence_id) const
      -> std::optional<StoredEvidenceRecord>;
  [[nodiscard]] auto CreateMarkdownEvidence(const std::string& session_id,
                                            const std::string& evidence_id,
                                            const std::string& title,
                                            const std::string& content,
                                            std::int64_t created_at_unix_ms) const
      -> std::optional<StoredEvidenceSummary>;
  [[nodiscard]] auto CreateLogSnapshotEvidence(const std::string& session_id,
                                               const std::string& evidence_id,
                                               const std::string& title,
                                               const std::string& evidence_json,
                                               std::int64_t created_at_unix_ms) const
      -> std::optional<StoredEvidenceSummary>;
  [[nodiscard]] auto RemoveEvidence(const std::string& session_id,
                                    const std::string& evidence_id) const -> bool;

 private:
  std::filesystem::path storage_root_;
};

}  // namespace vibe::store

#endif

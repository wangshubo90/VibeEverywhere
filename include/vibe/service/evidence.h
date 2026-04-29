#ifndef VIBE_SERVICE_EVIDENCE_H
#define VIBE_SERVICE_EVIDENCE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include "vibe/session/session_types.h"

namespace vibe::service {

enum class EvidenceSourceKind {
  ManagedLogSession,
};

[[nodiscard]] constexpr auto ToString(EvidenceSourceKind kind) -> std::string_view {
  switch (kind) {
    case EvidenceSourceKind::ManagedLogSession:
      return "managed_log_session";
  }
  return "managed_log_session";
}

struct EvidenceSourceRef {
  EvidenceSourceKind kind;
  vibe::session::SessionId session_id;
};

enum class LogStream {
  Stdout,
  Stderr,
};

[[nodiscard]] constexpr auto ToString(LogStream stream) -> std::string_view {
  switch (stream) {
    case LogStream::Stdout:
      return "stdout";
    case LogStream::Stderr:
      return "stderr";
  }
  return "stdout";
}

enum class EvidenceHighlightKind {
  Match,
  Warning,
  Error,
  Preset,
};

[[nodiscard]] constexpr auto ToString(EvidenceHighlightKind kind) -> std::string_view {
  switch (kind) {
    case EvidenceHighlightKind::Match:
      return "match";
    case EvidenceHighlightKind::Warning:
      return "warning";
    case EvidenceHighlightKind::Error:
      return "error";
    case EvidenceHighlightKind::Preset:
      return "preset";
  }
  return "match";
}

enum class EvidenceOperation {
  Tail,
  Search,
  Context,
  Range,
};

[[nodiscard]] constexpr auto ToString(EvidenceOperation operation) -> std::string_view {
  switch (operation) {
    case EvidenceOperation::Tail:
      return "tail";
    case EvidenceOperation::Search:
      return "search";
    case EvidenceOperation::Context:
      return "context";
    case EvidenceOperation::Range:
      return "range";
  }
  return "tail";
}

struct EvidenceEntry {
  std::string entry_id;
  EvidenceSourceRef source;
  std::uint64_t revision = 0;
  std::uint64_t byte_start = 0;
  std::uint64_t byte_end = 0;
  std::int64_t timestamp_unix_ms = 0;
  LogStream stream = LogStream::Stdout;
  std::string text;
  bool partial = false;
};

struct EvidenceHighlight {
  std::string entry_id;
  std::size_t start = 0;
  std::size_t length = 0;
  EvidenceHighlightKind kind = EvidenceHighlightKind::Match;
};

struct EvidenceResult {
  EvidenceSourceRef source;
  EvidenceOperation operation = EvidenceOperation::Tail;
  std::string query{};
  std::uint64_t revision_start = 0;
  std::uint64_t revision_end = 0;
  std::uint64_t oldest_revision = 0;
  std::uint64_t latest_revision = 0;
  std::vector<EvidenceEntry> entries{};
  std::vector<EvidenceHighlight> highlights{};
  bool truncated = false;
  bool buffer_exhausted = false;
  std::uint64_t dropped_entries = 0;
  std::uint64_t dropped_bytes = 0;
  std::string error_code{};
  std::string replay_token{};
};

struct ObservationEvent {
  std::string id{};
  std::int64_t timestamp_unix_ms = 0;
  std::string actor_session_id;
  std::string actor_title;
  std::string actor_id;
  pid_t pid = 0;
  uid_t uid = 0;
  gid_t gid = 0;
  EvidenceOperation operation = EvidenceOperation::Tail;
  EvidenceSourceRef source;
  std::string source_title;
  std::string query;
  std::uint64_t revision_start = 0;
  std::uint64_t revision_end = 0;
  std::size_t result_count = 0;
  bool truncated = false;
  std::string summary;
  std::string replay_token;
};

}  // namespace vibe::service

#endif

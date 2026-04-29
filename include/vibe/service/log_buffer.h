#ifndef VIBE_SERVICE_LOG_BUFFER_H
#define VIBE_SERVICE_LOG_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "vibe/service/evidence.h"

namespace vibe::service {

struct LogBufferLimits {
  std::size_t max_bytes = 16U * 1024U * 1024U;
  std::size_t max_entries = 50000U;
};

struct LogBufferStats {
  std::uint64_t oldest_revision = 1;
  std::uint64_t latest_revision = 0;
  std::uint64_t next_revision = 1;
  std::size_t retained_entries = 0;
  std::size_t retained_bytes = 0;
  std::uint64_t dropped_entries = 0;
  std::uint64_t dropped_bytes = 0;
};

struct LogBufferSearchResult {
  std::vector<EvidenceEntry> entries;
  std::vector<EvidenceHighlight> highlights;
  bool truncated = false;
};

class LogBuffer {
 public:
  LogBuffer(EvidenceSourceRef source, LogBufferLimits limits);

  void AppendStdout(std::string data, std::int64_t timestamp_unix_ms);
  void AppendStderr(std::string data, std::int64_t timestamp_unix_ms);

  [[nodiscard]] auto Tail(std::size_t limit, bool include_partial = true) const
      -> std::vector<EvidenceEntry>;
  [[nodiscard]] auto Range(std::uint64_t revision_start,
                           std::uint64_t revision_end,
                           std::size_t limit) const -> std::vector<EvidenceEntry>;
  [[nodiscard]] auto Search(std::string_view query, std::size_t limit) const
      -> LogBufferSearchResult;
  [[nodiscard]] auto Context(std::uint64_t revision, std::size_t before, std::size_t after) const
      -> std::vector<EvidenceEntry>;
  [[nodiscard]] auto ContainsRevision(std::uint64_t revision) const -> bool;
  [[nodiscard]] auto stats() const -> LogBufferStats;

 private:
  void Append(LogStream stream, std::string data, std::int64_t timestamp_unix_ms);
  void CompleteLine(LogStream stream, std::string text, std::int64_t timestamp_unix_ms);
  void EvictIfNeeded();
  [[nodiscard]] auto MakeEntryId(std::uint64_t revision) const -> std::string;
  [[nodiscard]] auto MakePartialEntry(LogStream stream,
                                      const std::string& text,
                                      std::int64_t timestamp_unix_ms) const -> EvidenceEntry;

  EvidenceSourceRef source_;
  LogBufferLimits limits_;
  std::deque<EvidenceEntry> entries_;
  std::size_t retained_bytes_ = 0;
  std::uint64_t next_revision_ = 1;
  std::uint64_t dropped_entries_ = 0;
  std::uint64_t dropped_bytes_ = 0;
  std::uint64_t next_byte_offset_ = 0;
  std::string stdout_partial_;
  std::string stderr_partial_;
  std::int64_t stdout_partial_timestamp_unix_ms_ = 0;
  std::int64_t stderr_partial_timestamp_unix_ms_ = 0;
  bool stdout_pending_cr_ = false;
  bool stderr_pending_cr_ = false;
};

}  // namespace vibe::service

#endif

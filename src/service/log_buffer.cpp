#include "vibe/service/log_buffer.h"

#include <algorithm>
#include <utility>

namespace vibe::service {

namespace {

auto PartialForStream(LogStream stream, std::string& stdout_partial, std::string& stderr_partial)
    -> std::string& {
  return stream == LogStream::Stderr ? stderr_partial : stdout_partial;
}

auto PartialTimestampForStream(LogStream stream,
                               std::int64_t& stdout_timestamp,
                               std::int64_t& stderr_timestamp) -> std::int64_t& {
  return stream == LogStream::Stderr ? stderr_timestamp : stdout_timestamp;
}

auto PendingCrForStream(LogStream stream, bool& stdout_pending_cr, bool& stderr_pending_cr) -> bool& {
  return stream == LogStream::Stderr ? stderr_pending_cr : stdout_pending_cr;
}

}  // namespace

LogBuffer::LogBuffer(EvidenceSourceRef source, LogBufferLimits limits)
    : source_(std::move(source)), limits_(limits) {}

void LogBuffer::AppendStdout(std::string data, const std::int64_t timestamp_unix_ms) {
  Append(LogStream::Stdout, std::move(data), timestamp_unix_ms);
}

void LogBuffer::AppendStderr(std::string data, const std::int64_t timestamp_unix_ms) {
  Append(LogStream::Stderr, std::move(data), timestamp_unix_ms);
}

auto LogBuffer::Tail(const std::size_t limit, const bool include_partial) const
    -> std::vector<EvidenceEntry> {
  if (limit == 0) {
    return {};
  }

  std::vector<EvidenceEntry> tail;
  const std::size_t partial_count =
      include_partial ? static_cast<std::size_t>(!stdout_partial_.empty()) +
                            static_cast<std::size_t>(!stderr_partial_.empty())
                      : 0U;
  const std::size_t completed_limit = limit > partial_count ? limit - partial_count : 0U;
  const std::size_t start =
      entries_.size() > completed_limit ? entries_.size() - completed_limit : 0U;

  tail.reserve(std::min(limit, entries_.size() + partial_count));
  for (std::size_t index = start; index < entries_.size(); ++index) {
    tail.push_back(entries_[index]);
  }

  if (include_partial && !stdout_partial_.empty() && tail.size() < limit) {
    tail.push_back(MakePartialEntry(LogStream::Stdout, stdout_partial_, stdout_partial_timestamp_unix_ms_));
  }
  if (include_partial && !stderr_partial_.empty() && tail.size() < limit) {
    tail.push_back(MakePartialEntry(LogStream::Stderr, stderr_partial_, stderr_partial_timestamp_unix_ms_));
  }

  return tail;
}

auto LogBuffer::Range(const std::uint64_t revision_start,
                      const std::uint64_t revision_end,
                      const std::size_t limit) const -> std::vector<EvidenceEntry> {
  if (limit == 0 || entries_.empty() || revision_start > revision_end) {
    return {};
  }

  const std::uint64_t oldest_revision = entries_.front().revision;
  const std::uint64_t latest_revision = entries_.back().revision;
  if (revision_end < oldest_revision || revision_start > latest_revision) {
    return {};
  }

  const std::uint64_t clamped_start = std::max(revision_start, oldest_revision);
  const std::uint64_t clamped_end = std::min(revision_end, latest_revision);
  const std::size_t start_index = static_cast<std::size_t>(clamped_start - oldest_revision);
  const std::size_t end_index = static_cast<std::size_t>(clamped_end - oldest_revision);

  std::vector<EvidenceEntry> entries;
  const std::size_t count = std::min(limit, end_index - start_index + 1U);
  entries.reserve(count);
  for (std::size_t offset = 0; offset < count; ++offset) {
    entries.push_back(entries_[start_index + offset]);
  }
  return entries;
}

auto LogBuffer::Search(const std::string_view query, const std::size_t limit) const
    -> LogBufferSearchResult {
  LogBufferSearchResult result;
  if (query.empty() || limit == 0) {
    return result;
  }

  for (const EvidenceEntry& entry : entries_) {
    std::size_t offset = entry.text.find(query);
    if (offset == std::string::npos) {
      continue;
    }

    if (result.entries.size() >= limit) {
      result.truncated = true;
      break;
    }

    result.entries.push_back(entry);
    while (offset != std::string::npos) {
      result.highlights.push_back(EvidenceHighlight{
          .entry_id = entry.entry_id,
          .start = offset,
          .length = query.size(),
          .kind = EvidenceHighlightKind::Match,
      });
      offset = entry.text.find(query, offset + query.size());
    }
  }

  return result;
}

auto LogBuffer::Context(const std::uint64_t revision,
                        const std::size_t before,
                        const std::size_t after) const -> std::vector<EvidenceEntry> {
  if (!ContainsRevision(revision) || entries_.empty()) {
    return {};
  }

  const std::uint64_t oldest_revision = entries_.front().revision;
  // Revisions are contiguous because entries are assigned sequentially and only evicted from the front.
  const std::size_t anchor_index = static_cast<std::size_t>(revision - oldest_revision);
  const std::size_t start = anchor_index > before ? anchor_index - before : 0U;
  const std::size_t end = std::min(entries_.size(), anchor_index + after + 1U);

  std::vector<EvidenceEntry> context;
  context.reserve(end - start);
  for (std::size_t index = start; index < end; ++index) {
    context.push_back(entries_[index]);
  }
  return context;
}

auto LogBuffer::ContainsRevision(const std::uint64_t revision) const -> bool {
  if (entries_.empty()) {
    return false;
  }
  return revision >= entries_.front().revision && revision <= entries_.back().revision;
}

auto LogBuffer::stats() const -> LogBufferStats {
  return LogBufferStats{
      .oldest_revision = entries_.empty() ? next_revision_ : entries_.front().revision,
      .latest_revision = entries_.empty() ? next_revision_ - 1U : entries_.back().revision,
      .next_revision = next_revision_,
      .retained_entries = entries_.size(),
      .retained_bytes = retained_bytes_,
      .dropped_entries = dropped_entries_,
      .dropped_bytes = dropped_bytes_,
  };
}

void LogBuffer::Append(LogStream stream, std::string data, const std::int64_t timestamp_unix_ms) {
  if (data.empty()) {
    return;
  }

  std::string& partial = PartialForStream(stream, stdout_partial_, stderr_partial_);
  std::int64_t& partial_timestamp =
      PartialTimestampForStream(stream, stdout_partial_timestamp_unix_ms_, stderr_partial_timestamp_unix_ms_);
  bool& pending_cr = PendingCrForStream(stream, stdout_pending_cr_, stderr_pending_cr_);
  if (partial.empty()) {
    partial_timestamp = timestamp_unix_ms;
  }

  for (std::size_t index = 0; index < data.size(); ++index) {
    const char ch = data[index];
    ++next_byte_offset_;
    if (pending_cr) {
      pending_cr = false;
      if (ch == '\n') {
        CompleteLine(stream, std::exchange(partial, ""), timestamp_unix_ms);
        partial_timestamp = 0;
        continue;
      }
      partial.clear();
      partial_timestamp = timestamp_unix_ms;
    }
    if (ch == '\r' && index + 1U < data.size() && data[index + 1U] == '\n') {
      continue;
    }
    if (ch == '\n') {
      if (!partial.empty() && partial.back() == '\r') {
        partial.pop_back();
      }
      CompleteLine(stream, std::exchange(partial, ""), timestamp_unix_ms);
      partial_timestamp = 0;
      continue;
    }
    if (ch == '\r') {
      if (index + 1U == data.size()) {
        pending_cr = true;
        continue;
      }
      partial.clear();
      partial_timestamp = timestamp_unix_ms;
      continue;
    }
    partial.push_back(ch);
  }
}

void LogBuffer::CompleteLine(LogStream stream,
                             std::string text,
                             const std::int64_t timestamp_unix_ms) {
  const std::uint64_t revision = next_revision_++;
  const std::uint64_t byte_end = next_byte_offset_;
  // Diagnostic byte range: terminators are included in byte_end but not text.
  const std::uint64_t byte_start =
      byte_end >= text.size() ? byte_end - static_cast<std::uint64_t>(text.size()) : 0U;
  retained_bytes_ += text.size();
  entries_.push_back(EvidenceEntry{
      .entry_id = MakeEntryId(revision),
      .source = source_,
      .revision = revision,
      .byte_start = byte_start,
      .byte_end = byte_end,
      .timestamp_unix_ms = timestamp_unix_ms,
      .stream = stream,
      .text = std::move(text),
      .partial = false,
  });
  EvictIfNeeded();
}

void LogBuffer::EvictIfNeeded() {
  while (!entries_.empty() &&
         ((limits_.max_entries > 0 && entries_.size() > limits_.max_entries) ||
          (limits_.max_bytes > 0 && retained_bytes_ > limits_.max_bytes))) {
    retained_bytes_ -= entries_.front().text.size();
    dropped_bytes_ += entries_.front().text.size();
    ++dropped_entries_;
    entries_.pop_front();
  }
}

auto LogBuffer::MakeEntryId(const std::uint64_t revision) const -> std::string {
  return "log:" + source_.session_id.value() + ":rev:" + std::to_string(revision);
}

auto LogBuffer::MakePartialEntry(LogStream stream,
                                 const std::string& text,
                                 const std::int64_t timestamp_unix_ms) const -> EvidenceEntry {
  const std::uint64_t revision = next_revision_;
  return EvidenceEntry{
      .entry_id = MakeEntryId(revision),
      .source = source_,
      .revision = revision,
      .byte_start = next_byte_offset_ >= text.size()
                        ? next_byte_offset_ - static_cast<std::uint64_t>(text.size())
                        : 0U,
      .byte_end = next_byte_offset_,
      .timestamp_unix_ms = timestamp_unix_ms,
      .stream = stream,
      .text = text,
      .partial = true,
  };
}

}  // namespace vibe::service

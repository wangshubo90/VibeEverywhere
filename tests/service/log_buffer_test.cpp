#include <gtest/gtest.h>

#include "vibe/service/log_buffer.h"

namespace vibe::service {
namespace {

auto MakeSource() -> EvidenceSourceRef {
  const auto id = vibe::session::SessionId::TryCreate("log_test");
  EXPECT_TRUE(id.has_value());
  return EvidenceSourceRef{
      .kind = EvidenceSourceKind::ManagedLogSession,
      .session_id = *id,
  };
}

TEST(LogBufferTest, CapturesStdoutAndStderrLinesWithRevisions) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStderr("two\n", 1001);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 2U);
  EXPECT_EQ(tail[0].entry_id, "log:log_test:rev:1");
  EXPECT_EQ(tail[0].revision, 1U);
  EXPECT_EQ(tail[0].stream, LogStream::Stdout);
  EXPECT_EQ(tail[0].text, "one");
  EXPECT_EQ(tail[0].timestamp_unix_ms, 1000);
  EXPECT_FALSE(tail[0].partial);
  EXPECT_EQ(tail[1].entry_id, "log:log_test:rev:2");
  EXPECT_EQ(tail[1].stream, LogStream::Stderr);
  EXPECT_EQ(tail[1].text, "two");

  const LogBufferStats stats = buffer.stats();
  EXPECT_EQ(stats.oldest_revision, 1U);
  EXPECT_EQ(stats.latest_revision, 2U);
  EXPECT_EQ(stats.next_revision, 3U);
  EXPECT_EQ(stats.retained_entries, 2U);
}

TEST(LogBufferTest, JoinsChunksBeforeCompletingLine) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("hel", 1000);
  buffer.AppendStdout("lo\n", 1005);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 1U);
  EXPECT_EQ(tail[0].text, "hello");
  EXPECT_EQ(tail[0].timestamp_unix_ms, 1005);
}

TEST(LogBufferTest, IncludesPartialFinalLineInTail) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("complete\npartial", 2000);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 2U);
  EXPECT_EQ(tail[0].revision, 1U);
  EXPECT_EQ(tail[0].text, "complete");
  EXPECT_FALSE(tail[0].partial);
  EXPECT_EQ(tail[1].revision, 2U);
  EXPECT_EQ(tail[1].entry_id, "log:log_test:rev:2");
  EXPECT_EQ(tail[1].text, "partial");
  EXPECT_TRUE(tail[1].partial);
}

TEST(LogBufferTest, NormalizesCrLfAndTreatsBareCrAsRewrite) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("first\r\nspin 10%\rspin 20%\n", 3000);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 2U);
  EXPECT_EQ(tail[0].text, "first");
  EXPECT_EQ(tail[1].text, "spin 20%");
}

TEST(LogBufferTest, HandlesCrLfSplitAcrossChunks) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("first\r", 3000);
  buffer.AppendStdout("\nsecond\n", 3001);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 2U);
  EXPECT_EQ(tail[0].text, "first");
  EXPECT_EQ(tail[1].text, "second");
}

TEST(LogBufferTest, EvictsOldestEntriesByEntryLimitAndTracksDroppedCounters) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 2});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStdout("two\n", 1001);
  buffer.AppendStdout("three\n", 1002);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 2U);
  EXPECT_EQ(tail[0].revision, 2U);
  EXPECT_EQ(tail[0].text, "two");
  EXPECT_EQ(tail[1].revision, 3U);
  EXPECT_EQ(tail[1].text, "three");

  const LogBufferStats stats = buffer.stats();
  EXPECT_EQ(stats.oldest_revision, 2U);
  EXPECT_EQ(stats.latest_revision, 3U);
  EXPECT_EQ(stats.dropped_entries, 1U);
  EXPECT_EQ(stats.dropped_bytes, 3U);
}

TEST(LogBufferTest, EvictsOldestEntriesByByteLimit) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 5, .max_entries = 100});

  buffer.AppendStdout("abc\n", 1000);
  buffer.AppendStdout("def\n", 1001);

  const std::vector<EvidenceEntry> tail = buffer.Tail(10);
  ASSERT_EQ(tail.size(), 1U);
  EXPECT_EQ(tail[0].text, "def");

  const LogBufferStats stats = buffer.stats();
  EXPECT_EQ(stats.retained_bytes, 3U);
  EXPECT_EQ(stats.dropped_entries, 1U);
  EXPECT_EQ(stats.dropped_bytes, 3U);
}

TEST(LogBufferTest, ContextUsesRevisionWindow) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStdout("two\n", 1001);
  buffer.AppendStdout("three\n", 1002);
  buffer.AppendStdout("four\n", 1003);

  const std::vector<EvidenceEntry> context = buffer.Context(3, 1, 1);
  ASSERT_EQ(context.size(), 3U);
  EXPECT_EQ(context[0].revision, 2U);
  EXPECT_EQ(context[1].revision, 3U);
  EXPECT_EQ(context[2].revision, 4U);
  EXPECT_TRUE(buffer.ContainsRevision(3));
  EXPECT_FALSE(buffer.ContainsRevision(9));
}

TEST(LogBufferTest, RangeReturnsClampedRevisionWindow) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 3});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStdout("two\n", 1001);
  buffer.AppendStdout("three\n", 1002);
  buffer.AppendStdout("four\n", 1003);

  const std::vector<EvidenceEntry> range = buffer.Range(1, 4, 10);
  ASSERT_EQ(range.size(), 3U);
  EXPECT_EQ(range[0].revision, 2U);
  EXPECT_EQ(range[1].revision, 3U);
  EXPECT_EQ(range[2].revision, 4U);
}

TEST(LogBufferTest, RangeRespectsLimit) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStdout("two\n", 1001);
  buffer.AppendStdout("three\n", 1002);

  const std::vector<EvidenceEntry> range = buffer.Range(1, 3, 2);
  ASSERT_EQ(range.size(), 2U);
  EXPECT_EQ(range[0].revision, 1U);
  EXPECT_EQ(range[1].revision, 2U);
}

TEST(LogBufferTest, SearchReturnsMatchingEntriesAndHighlights) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("alpha beta alpha\n", 1000);
  buffer.AppendStdout("gamma\n", 1001);
  buffer.AppendStderr("beta failure\n", 1002);

  const LogBufferSearchResult result = buffer.Search("alpha", 10);
  EXPECT_FALSE(result.truncated);
  ASSERT_EQ(result.entries.size(), 1U);
  EXPECT_EQ(result.entries[0].entry_id, "log:log_test:rev:1");
  EXPECT_EQ(result.entries[0].text, "alpha beta alpha");
  ASSERT_EQ(result.highlights.size(), 2U);
  EXPECT_EQ(result.highlights[0].entry_id, "log:log_test:rev:1");
  EXPECT_EQ(result.highlights[0].start, 0U);
  EXPECT_EQ(result.highlights[0].length, 5U);
  EXPECT_EQ(result.highlights[1].start, 11U);
}

TEST(LogBufferTest, SearchLimitTruncatesByMatchingEntry) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("match one\n", 1000);
  buffer.AppendStdout("skip\n", 1001);
  buffer.AppendStdout("match two\n", 1002);

  const LogBufferSearchResult result = buffer.Search("match", 1);
  EXPECT_TRUE(result.truncated);
  ASSERT_EQ(result.entries.size(), 1U);
  EXPECT_EQ(result.entries[0].revision, 1U);
  ASSERT_EQ(result.highlights.size(), 1U);
  EXPECT_EQ(result.highlights[0].entry_id, "log:log_test:rev:1");
}

TEST(LogBufferTest, ContextClampsWhenBeforeExceedsAvailableEntries) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 100});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStdout("two\n", 1001);
  buffer.AppendStdout("three\n", 1002);

  const std::vector<EvidenceEntry> context = buffer.Context(2, 99, 0);
  ASSERT_EQ(context.size(), 2U);
  EXPECT_EQ(context[0].revision, 1U);
  EXPECT_EQ(context[1].revision, 2U);
}

TEST(LogBufferTest, ContextReturnsEmptyForEvictedAnchor) {
  LogBuffer buffer(MakeSource(), LogBufferLimits{.max_bytes = 1024, .max_entries = 2});

  buffer.AppendStdout("one\n", 1000);
  buffer.AppendStdout("two\n", 1001);
  buffer.AppendStdout("three\n", 1002);

  EXPECT_TRUE(buffer.Context(1, 1, 1).empty());
  EXPECT_FALSE(buffer.ContainsRevision(1));
}

}  // namespace
}  // namespace vibe::service

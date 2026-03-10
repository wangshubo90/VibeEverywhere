#include <gtest/gtest.h>

#include "vibe/session/session_output_buffer.h"

namespace vibe::session {
namespace {

TEST(SessionOutputBufferTest, AppendsDataAndTracksSequenceNumbers) {
  SessionOutputBuffer buffer(32);

  buffer.Append("hello");
  buffer.Append(" world");

  EXPECT_EQ(buffer.size_bytes(), 11U);
  EXPECT_EQ(buffer.next_sequence(), 3U);
  ASSERT_TRUE(buffer.latest_sequence().has_value());
  EXPECT_EQ(*buffer.latest_sequence(), 2U);

  const OutputSlice tail = buffer.Tail(32);
  EXPECT_EQ(tail.seq_start, 1U);
  EXPECT_EQ(tail.seq_end, 2U);
  EXPECT_EQ(tail.data, "hello world");
}

TEST(SessionOutputBufferTest, EvictsOldestDataWhenCapacityExceeded) {
  SessionOutputBuffer buffer(10);

  buffer.Append("abc");
  buffer.Append("defg");
  buffer.Append("hijkl");

  EXPECT_EQ(buffer.size_bytes(), 9U);
  const OutputSlice tail = buffer.Tail(16);
  EXPECT_EQ(tail.seq_start, 2U);
  EXPECT_EQ(tail.seq_end, 3U);
  EXPECT_EQ(tail.data, "defghijkl");
}

TEST(SessionOutputBufferTest, TailRespectsRequestedByteLimit) {
  SessionOutputBuffer buffer(64);

  buffer.Append("12345");
  buffer.Append("67890");

  const OutputSlice tail = buffer.Tail(6);
  EXPECT_EQ(tail.seq_start, 1U);
  EXPECT_EQ(tail.seq_end, 2U);
  EXPECT_EQ(tail.data, "567890");
}

TEST(SessionOutputBufferTest, OversizedChunkIsTrimmedToCapacity) {
  SessionOutputBuffer buffer(4);

  buffer.Append("abcdef");

  EXPECT_EQ(buffer.size_bytes(), 4U);
  const OutputSlice tail = buffer.Tail(8);
  EXPECT_EQ(tail.seq_start, 1U);
  EXPECT_EQ(tail.seq_end, 1U);
  EXPECT_EQ(tail.data, "cdef");
}

TEST(SessionOutputBufferTest, SliceFromSequenceReturnsChunksFromRequestedPoint) {
  SessionOutputBuffer buffer(64);

  buffer.Append("abc");
  buffer.Append("def");
  buffer.Append("ghi");

  const OutputSlice slice = buffer.SliceFromSequence(2);
  EXPECT_EQ(slice.seq_start, 2U);
  EXPECT_EQ(slice.seq_end, 3U);
  EXPECT_EQ(slice.data, "defghi");
}

TEST(SessionOutputBufferTest, SliceFromSequenceStartsFromOldestAvailableChunkWhenEarlierDataIsGone) {
  SessionOutputBuffer buffer(5);

  buffer.Append("abc");
  buffer.Append("def");

  const OutputSlice slice = buffer.SliceFromSequence(1);
  EXPECT_EQ(slice.seq_start, 2U);
  EXPECT_EQ(slice.seq_end, 2U);
  EXPECT_EQ(slice.data, "def");
}

}  // namespace
}  // namespace vibe::session

#include <gtest/gtest.h>

#include "vibe/service/observation_store.h"

namespace vibe::service {
namespace {

auto MakeSource(const char* id) -> EvidenceSourceRef {
  const auto session_id = vibe::session::SessionId::TryCreate(id);
  EXPECT_TRUE(session_id.has_value());
  return EvidenceSourceRef{
      .kind = EvidenceSourceKind::ManagedLogSession,
      .session_id = *session_id,
  };
}

auto MakeEvent(const std::string& actor,
               const EvidenceOperation operation,
               const std::uint64_t revision_start,
               const std::uint64_t revision_end) -> ObservationEvent {
  return ObservationEvent{
      .actor_session_id = actor,
      .actor_id = actor,
      .operation = operation,
      .source = MakeSource("log_test"),
      .revision_start = revision_start,
      .revision_end = revision_end,
      .result_count = 2,
  };
}

TEST(ObservationStoreTest, AddsEventAndAssignsId) {
  ObservationStore store;

  const ObservationEvent& event = store.Add(MakeEvent("agent_one", EvidenceOperation::Search, 1, 3));

  EXPECT_EQ(event.id, "obs:1");
  EXPECT_EQ(store.size(), 1U);
  const std::vector<ObservationEvent> listed = store.ListNewestFirst(10);
  ASSERT_EQ(listed.size(), 1U);
  EXPECT_EQ(listed[0].actor_session_id, "agent_one");
}

TEST(ObservationStoreTest, RingEvictsOldestAndListsNewestFirst) {
  ObservationStore store(ObservationStoreLimits{.max_events = 2});

  static_cast<void>(store.Add(MakeEvent("agent_one", EvidenceOperation::Search, 1, 1)));
  static_cast<void>(store.Add(MakeEvent("agent_two", EvidenceOperation::Search, 2, 2)));
  static_cast<void>(store.Add(MakeEvent("agent_three", EvidenceOperation::Search, 3, 3)));

  EXPECT_EQ(store.size(), 2U);
  const std::vector<ObservationEvent> listed = store.ListNewestFirst(10);
  ASSERT_EQ(listed.size(), 2U);
  EXPECT_EQ(listed[0].actor_session_id, "agent_three");
  EXPECT_EQ(listed[0].id, "obs:3");
  EXPECT_EQ(listed[1].actor_session_id, "agent_two");
  EXPECT_EQ(listed[1].id, "obs:2");
}

TEST(ObservationStoreTest, ListLimitIsApplied) {
  ObservationStore store;

  static_cast<void>(store.Add(MakeEvent("agent_one", EvidenceOperation::Search, 1, 1)));
  static_cast<void>(store.Add(MakeEvent("agent_two", EvidenceOperation::Search, 2, 2)));

  const std::vector<ObservationEvent> listed = store.ListNewestFirst(1);
  ASSERT_EQ(listed.size(), 1U);
  EXPECT_EQ(listed[0].actor_session_id, "agent_two");
}

TEST(ObservationStoreTest, DeduplicatesIdenticalConsecutiveTailReads) {
  ObservationStore store;

  const ObservationStoreAddResult first =
      store.AddWithStatus(MakeEvent("agent_one", EvidenceOperation::Tail, 1, 3));
  const ObservationStoreAddResult second =
      store.AddWithStatus(MakeEvent("agent_one", EvidenceOperation::Tail, 1, 3));

  EXPECT_TRUE(first.inserted);
  EXPECT_FALSE(second.inserted);
  EXPECT_EQ(first.event.id, second.event.id);
  EXPECT_EQ(store.size(), 1U);
}

TEST(ObservationStoreTest, KeepsDistinctTailReads) {
  ObservationStore store;

  static_cast<void>(store.Add(MakeEvent("agent_one", EvidenceOperation::Tail, 1, 3)));
  static_cast<void>(store.Add(MakeEvent("agent_one", EvidenceOperation::Tail, 2, 4)));
  static_cast<void>(store.Add(MakeEvent("agent_two", EvidenceOperation::Tail, 2, 4)));

  EXPECT_EQ(store.size(), 3U);
}

TEST(ObservationStoreTest, DifferentActorSameParamsProducesTwoEvents) {
  ObservationStore store;

  const ObservationEvent& first = store.Add(MakeEvent("agent_one", EvidenceOperation::Tail, 1, 3));
  const ObservationEvent& second = store.Add(MakeEvent("agent_two", EvidenceOperation::Tail, 1, 3));

  EXPECT_NE(first.id, second.id);
  EXPECT_EQ(store.size(), 2U);
}

}  // namespace
}  // namespace vibe::service

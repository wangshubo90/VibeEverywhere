#include <gtest/gtest.h>

#include "vibe/service/evidence_response_assembler.h"

namespace vibe::service {
namespace {

auto MakeSource() -> EvidenceSourceRef {
  const auto session_id = vibe::session::SessionId::TryCreate("s_42");
  EXPECT_TRUE(session_id.has_value());
  return EvidenceSourceRef{
      .kind = EvidenceSourceKind::ManagedLogSession,
      .session_id = *session_id,
  };
}

auto MakeResult(EvidenceOperation operation = EvidenceOperation::Tail) -> EvidenceResult {
  const EvidenceSourceRef source = MakeSource();
  return EvidenceResult{
      .source = source,
      .operation = operation,
      .query = operation == EvidenceOperation::Search ? "error" : "",
      .revision_start = 4,
      .revision_end = 5,
      .oldest_revision = 1,
      .latest_revision = 5,
      .entries =
          {
              EvidenceEntry{
                  .entry_id = "log:s_42:rev:4",
                  .source = source,
                  .revision = 4,
                  .text = "first",
              },
              EvidenceEntry{
                  .entry_id = "log:s_42:rev:5",
                  .source = source,
                  .revision = 5,
                  .text = "second",
              },
          },
  };
}

TEST(EvidenceResponseAssemblerTest, AddsReplayTokenToResult) {
  EvidenceResponseAssembler assembler;

  const EvidenceAssembly assembly = assembler.Assemble(EvidenceAssemblyRequest{
      .result = MakeResult(EvidenceOperation::Search),
      .source_title = "app.log",
      .actor = std::nullopt,
      .timestamp_unix_ms = 1234,
  });

  EXPECT_FALSE(assembly.result.replay_token.empty());
  EXPECT_EQ(assembly.result.replay_token.find('='), std::string::npos);
  EXPECT_FALSE(assembly.observation.has_value());
}

TEST(EvidenceResponseAssemblerTest, EmitsObservationWhenActorIsPresent) {
  EvidenceResponseAssembler assembler;

  const EvidenceAssembly assembly = assembler.Assemble(EvidenceAssemblyRequest{
      .result = MakeResult(EvidenceOperation::Tail),
      .source_title = "app.log",
      .actor =
          EvidenceActorContext{
              .actor_session_id = "agent_1",
              .actor_title = "Agent",
              .pid = 11,
              .uid = 22,
              .gid = 33,
          },
      .timestamp_unix_ms = 1234,
  });

  ASSERT_TRUE(assembly.observation.has_value());
  EXPECT_EQ(assembly.observation->actor_session_id, "agent_1");
  EXPECT_EQ(assembly.observation->actor_id, "agent_1");
  EXPECT_EQ(assembly.observation->actor_title, "Agent");
  EXPECT_EQ(assembly.observation->pid, 11);
  EXPECT_EQ(assembly.observation->uid, 22);
  EXPECT_EQ(assembly.observation->gid, 33);
  EXPECT_EQ(assembly.observation->operation, EvidenceOperation::Tail);
  EXPECT_EQ(assembly.observation->source.session_id.value(), "s_42");
  EXPECT_EQ(assembly.observation->source_title, "app.log");
  EXPECT_EQ(assembly.observation->revision_start, 4U);
  EXPECT_EQ(assembly.observation->revision_end, 5U);
  EXPECT_EQ(assembly.observation->result_count, 2U);
  EXPECT_EQ(assembly.observation->replay_token, assembly.result.replay_token);
}

TEST(EvidenceResponseAssemblerTest, DoesNotEmitObservationForClientViewerRead) {
  EvidenceResponseAssembler assembler;

  const EvidenceAssembly assembly = assembler.Assemble(EvidenceAssemblyRequest{
      .result = MakeResult(EvidenceOperation::Tail),
      .source_title = "app.log",
      .actor =
          EvidenceActorContext{
              .actor_session_id = "",
              .actor_title = "",
          },
      .timestamp_unix_ms = 1234,
  });

  EXPECT_FALSE(assembly.observation.has_value());
}

}  // namespace
}  // namespace vibe::service

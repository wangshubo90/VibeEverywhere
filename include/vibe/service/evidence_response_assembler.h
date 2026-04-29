#ifndef VIBE_SERVICE_EVIDENCE_RESPONSE_ASSEMBLER_H
#define VIBE_SERVICE_EVIDENCE_RESPONSE_ASSEMBLER_H

#include <cstdint>
#include <optional>
#include <string>

#include "vibe/service/evidence.h"

namespace vibe::service {

struct EvidenceActorContext {
  std::string actor_session_id;
  std::string actor_title;
  pid_t pid = 0;
  uid_t uid = 0;
  gid_t gid = 0;
};

struct EvidenceAssemblyRequest {
  EvidenceResult result;
  std::string source_title;
  std::optional<EvidenceActorContext> actor;
  std::int64_t timestamp_unix_ms = 0;
};

struct EvidenceAssembly {
  EvidenceResult result;
  std::optional<ObservationEvent> observation;
};

class EvidenceResponseAssembler {
 public:
  [[nodiscard]] auto Assemble(EvidenceAssemblyRequest request) const -> EvidenceAssembly;
};

}  // namespace vibe::service

#endif

#include "vibe/service/observation_store.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

namespace vibe::service {

ObservationStore::ObservationStore(ObservationStoreLimits limits) : limits_(limits) {
  assert(limits_.max_events >= 1 && "max_events must be at least 1");
}

auto ObservationStore::Add(ObservationEvent event) -> const ObservationEvent& {
  static_cast<void>(AddWithStatus(std::move(event)));
  return events_.back();
}

auto ObservationStore::AddWithStatus(ObservationEvent event) -> ObservationStoreAddResult {
  if (IsDuplicateTailObservation(event)) {
    return ObservationStoreAddResult{
        .event = events_.back(),
        .inserted = false,
    };
  }

  if (event.id.empty()) {
    event.id = MakeEventId();
  }
  events_.push_back(std::move(event));
  EvictIfNeeded();
  return ObservationStoreAddResult{
      .event = events_.back(),
      .inserted = true,
  };
}

auto ObservationStore::ListNewestFirst(const std::size_t limit) const -> std::vector<ObservationEvent> {
  std::vector<ObservationEvent> listed;
  if (limit == 0 || events_.empty()) {
    return listed;
  }

  const std::size_t count = std::min(limit, events_.size());
  listed.reserve(count);
  for (std::size_t offset = 0; offset < count; ++offset) {
    listed.push_back(events_[events_.size() - 1U - offset]);
  }
  return listed;
}

auto ObservationStore::size() const -> std::size_t {
  return events_.size();
}

auto ObservationStore::IsDuplicateTailObservation(const ObservationEvent& event) const -> bool {
  if (events_.empty() || event.operation != EvidenceOperation::Tail) {
    return false;
  }

  const ObservationEvent& previous = events_.back();
  return previous.operation == EvidenceOperation::Tail &&
         previous.actor_session_id == event.actor_session_id &&
         previous.source.kind == event.source.kind &&
         previous.source.session_id == event.source.session_id &&
         previous.query == event.query &&
         previous.revision_start == event.revision_start &&
         previous.revision_end == event.revision_end &&
         previous.result_count == event.result_count &&
         previous.truncated == event.truncated;
}

auto ObservationStore::MakeEventId() -> std::string {
  return "obs:" + std::to_string(next_event_sequence_++);
}

void ObservationStore::EvictIfNeeded() {
  while (limits_.max_events > 0 && events_.size() > limits_.max_events) {
    events_.pop_front();
  }
}

}  // namespace vibe::service

#ifndef VIBE_SERVICE_OBSERVATION_STORE_H
#define VIBE_SERVICE_OBSERVATION_STORE_H

#include <cstddef>
#include <deque>
#include <vector>

#include "vibe/service/evidence.h"

namespace vibe::service {

struct ObservationStoreLimits {
  std::size_t max_events = 1000;
};

struct ObservationStoreAddResult {
  ObservationEvent event;
  bool inserted = false;
};

class ObservationStore {
 public:
  explicit ObservationStore(ObservationStoreLimits limits = {});

  [[nodiscard]] auto Add(ObservationEvent event) -> const ObservationEvent&;
  [[nodiscard]] auto AddWithStatus(ObservationEvent event) -> ObservationStoreAddResult;
  [[nodiscard]] auto ListNewestFirst(std::size_t limit) const -> std::vector<ObservationEvent>;
  [[nodiscard]] auto size() const -> std::size_t;

 private:
  [[nodiscard]] auto IsDuplicateTailObservation(const ObservationEvent& event) const -> bool;
  [[nodiscard]] auto MakeEventId() -> std::string;
  void EvictIfNeeded();

  ObservationStoreLimits limits_;
  std::deque<ObservationEvent> events_;
  std::uint64_t next_event_sequence_ = 1;
};

}  // namespace vibe::service

#endif

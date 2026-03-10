#ifndef VIBE_STORE_PAIRING_STORE_H
#define VIBE_STORE_PAIRING_STORE_H

#include <string>
#include <vector>

#include "vibe/auth/pairing.h"

namespace vibe::store {

class PairingStore {
 public:
  virtual ~PairingStore() = default;

  [[nodiscard]] virtual auto LoadPendingPairings() const -> std::vector<vibe::auth::PairingRequest> = 0;
  [[nodiscard]] virtual auto LoadApprovedPairings() const -> std::vector<vibe::auth::PairingRecord> = 0;
  [[nodiscard]] virtual auto UpsertPendingPairing(const vibe::auth::PairingRequest& request) -> bool = 0;
  [[nodiscard]] virtual auto UpsertApprovedPairing(const vibe::auth::PairingRecord& record) -> bool = 0;
  [[nodiscard]] virtual auto RemovePendingPairing(const std::string& pairing_id) -> bool = 0;
  [[nodiscard]] virtual auto RemoveApprovedPairing(const std::string& device_id) -> bool = 0;
};

}  // namespace vibe::store

#endif

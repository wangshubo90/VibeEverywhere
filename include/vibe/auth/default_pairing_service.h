#ifndef VIBE_AUTH_DEFAULT_PAIRING_SERVICE_H
#define VIBE_AUTH_DEFAULT_PAIRING_SERVICE_H

#include <functional>
#include <unordered_map>
#include <optional>
#include <string>

#include "vibe/auth/pairing.h"
#include "vibe/store/pairing_store.h"

namespace vibe::auth {

class DefaultPairingService final : public PairingService {
 public:
  using TimestampProvider = std::function<std::int64_t()>;
  using StringGenerator = std::function<std::string()>;
  using DurationMs = std::int64_t;

  explicit DefaultPairingService(vibe::store::PairingStore& pairing_store);
  DefaultPairingService(vibe::store::PairingStore& pairing_store,
                        TimestampProvider timestamp_provider,
                        StringGenerator pairing_id_generator,
                        StringGenerator code_generator,
                        StringGenerator device_id_generator,
                        StringGenerator token_generator,
                        DurationMs pairing_request_ttl_ms = 10 * 60 * 1000);

  [[nodiscard]] auto StartPairing(const std::string& device_name,
                                  DeviceType device_type) -> std::optional<PairingRequest> override;
  [[nodiscard]] auto ListPendingPairings() const -> std::vector<PairingRequest> override;
  [[nodiscard]] auto ApprovePairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<PairingRecord> override;
  [[nodiscard]] auto ClaimApprovedPairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<PairingRecord> override;
  [[nodiscard]] auto RejectPairing(const std::string& pairing_id) -> bool override;

 private:
  struct ApprovedClaim {
    std::string code;
    PairingRecord record;
  };

  vibe::store::PairingStore& pairing_store_;
  TimestampProvider timestamp_provider_;
  StringGenerator pairing_id_generator_;
  StringGenerator code_generator_;
  StringGenerator device_id_generator_;
  StringGenerator token_generator_;
  DurationMs pairing_request_ttl_ms_{10 * 60 * 1000};
  std::unordered_map<std::string, ApprovedClaim> approved_claims_;
};

}  // namespace vibe::auth

#endif

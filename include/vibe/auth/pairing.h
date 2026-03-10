#ifndef VIBE_AUTH_PAIRING_H
#define VIBE_AUTH_PAIRING_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vibe::auth {

struct DeviceId {
  std::string value;

  [[nodiscard]] auto operator==(const DeviceId& other) const -> bool = default;
};

enum class DeviceType {
  Unknown,
  Mobile,
  Desktop,
  Browser,
};

struct PairingRequest {
  std::string pairing_id;
  std::string device_name;
  DeviceType device_type{DeviceType::Unknown};
  std::string code;
  std::int64_t requested_at_unix_ms{0};

  [[nodiscard]] auto operator==(const PairingRequest& other) const -> bool = default;
};

struct PairingRecord {
  DeviceId device_id;
  std::string device_name;
  DeviceType device_type{DeviceType::Unknown};
  std::string bearer_token;
  std::int64_t approved_at_unix_ms{0};

  [[nodiscard]] auto operator==(const PairingRecord& other) const -> bool = default;
};

class PairingService {
 public:
  virtual ~PairingService() = default;

  [[nodiscard]] virtual auto StartPairing(const std::string& device_name,
                                          DeviceType device_type) -> std::optional<PairingRequest> = 0;
  [[nodiscard]] virtual auto ListPendingPairings() const -> std::vector<PairingRequest> = 0;
  [[nodiscard]] virtual auto ApprovePairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<PairingRecord> = 0;
  [[nodiscard]] virtual auto RejectPairing(const std::string& pairing_id) -> bool = 0;
};

}  // namespace vibe::auth

#endif

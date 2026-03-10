#include "vibe/net/local_auth.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

namespace vibe::net {

namespace {

struct LocalAuthState {
  mutable std::mutex mutex;
  std::vector<vibe::auth::PairingRequest> pending_pairings;
  std::vector<vibe::auth::PairingRecord> approved_pairings;
};

auto CurrentUnixMillis() -> std::int64_t {
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
  return now.time_since_epoch().count();
}

auto MakePairingCode(const std::size_t index) -> std::string {
  std::ostringstream stream;
  stream << std::setw(6) << std::setfill('0') << (100000 + static_cast<int>(index));
  return stream.str();
}

class InMemoryPairingService final : public vibe::auth::PairingService {
 public:
  explicit InMemoryPairingService(std::shared_ptr<LocalAuthState> state) : state_(std::move(state)) {}

  [[nodiscard]] auto StartPairing(const std::string& device_name,
                                  const vibe::auth::DeviceType device_type)
      -> std::optional<vibe::auth::PairingRequest> override {
    std::scoped_lock lock(state_->mutex);
    const std::size_t next_index = state_->pending_pairings.size() + state_->approved_pairings.size() + 1U;
    const auto request = vibe::auth::PairingRequest{
        .pairing_id = "p_" + std::to_string(next_index),
        .device_name = device_name,
        .device_type = device_type,
        .code = MakePairingCode(next_index),
        .requested_at_unix_ms = CurrentUnixMillis(),
    };
    state_->pending_pairings.push_back(request);
    return request;
  }

  [[nodiscard]] auto ListPendingPairings() const -> std::vector<vibe::auth::PairingRequest> override {
    std::scoped_lock lock(state_->mutex);
    return state_->pending_pairings;
  }

  [[nodiscard]] auto ApprovePairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<vibe::auth::PairingRecord> override {
    std::scoped_lock lock(state_->mutex);
    for (auto it = state_->pending_pairings.begin(); it != state_->pending_pairings.end(); ++it) {
      if (it->pairing_id != pairing_id || it->code != code) {
        continue;
      }

      const auto record = vibe::auth::PairingRecord{
          .device_id = vibe::auth::DeviceId{.value = "d_" + std::to_string(state_->approved_pairings.size() + 1U)},
          .device_name = it->device_name,
          .device_type = it->device_type,
          .bearer_token = "token_" + pairing_id,
          .approved_at_unix_ms = CurrentUnixMillis(),
      };
      state_->approved_pairings.push_back(record);
      state_->pending_pairings.erase(it);
      return record;
    }

    return std::nullopt;
  }

  [[nodiscard]] auto RejectPairing(const std::string& pairing_id) -> bool override {
    std::scoped_lock lock(state_->mutex);
    for (auto it = state_->pending_pairings.begin(); it != state_->pending_pairings.end(); ++it) {
      if (it->pairing_id != pairing_id) {
        continue;
      }

      state_->pending_pairings.erase(it);
      return true;
    }

    return false;
  }

 private:
  std::shared_ptr<LocalAuthState> state_;
};

class InMemoryAuthorizer final : public vibe::auth::Authorizer {
 public:
  explicit InMemoryAuthorizer(std::shared_ptr<LocalAuthState> state) : state_(std::move(state)) {}

  [[nodiscard]] auto AuthenticateBearerToken(const std::string& bearer_token) const
      -> vibe::auth::AuthResult override {
    if (bearer_token.empty()) {
      return vibe::auth::AuthResult{
          .authenticated = false,
          .authorized = false,
          .device_id = std::nullopt,
          .reason = "missing token",
      };
    }

    std::scoped_lock lock(state_->mutex);
    for (const auto& record : state_->approved_pairings) {
      if (record.bearer_token == bearer_token) {
        return vibe::auth::AuthResult{
            .authenticated = true,
            .authorized = true,
            .device_id = record.device_id,
            .reason = "",
        };
      }
    }

    return vibe::auth::AuthResult{
        .authenticated = false,
        .authorized = false,
        .device_id = std::nullopt,
        .reason = "invalid token",
    };
  }

  [[nodiscard]] auto Authorize(const vibe::auth::RequestContext& request_context,
                               const vibe::auth::AuthorizationAction action) const
      -> vibe::auth::AuthResult override {
    if (action == vibe::auth::AuthorizationAction::ApprovePairing ||
        action == vibe::auth::AuthorizationAction::ConfigureHost) {
      return vibe::auth::AuthResult{
          .authenticated = request_context.is_local_request,
          .authorized = request_context.is_local_request,
          .device_id = std::nullopt,
          .reason = request_context.is_local_request ? "" : "local access required",
      };
    }

    return AuthenticateBearerToken(request_context.bearer_token);
  }

 private:
  std::shared_ptr<LocalAuthState> state_;
};

class InMemoryHostConfigStore final : public vibe::store::HostConfigStore {
 public:
  InMemoryHostConfigStore()
      : identity_(vibe::store::HostIdentity{
            .host_id = "local-dev-host",
            .display_name = "VibeEverywhere Dev Host",
            .certificate_pem_path = "",
            .private_key_pem_path = "",
        }) {}

  [[nodiscard]] auto LoadHostIdentity() const -> std::optional<vibe::store::HostIdentity> override {
    std::scoped_lock lock(mutex_);
    return identity_;
  }

  [[nodiscard]] auto SaveHostIdentity(const vibe::store::HostIdentity& identity) -> bool override {
    std::scoped_lock lock(mutex_);
    identity_ = identity;
    return true;
  }

 private:
  mutable std::mutex mutex_;
  std::optional<vibe::store::HostIdentity> identity_;
};

}  // namespace

auto CreateLocalAuthServices() -> LocalAuthServices {
  auto state = std::make_shared<LocalAuthState>();
  return LocalAuthServices{
      .authorizer = std::make_shared<InMemoryAuthorizer>(state),
      .pairing_service = std::make_shared<InMemoryPairingService>(std::move(state)),
      .host_config_store = std::make_shared<InMemoryHostConfigStore>(),
  };
}

}  // namespace vibe::net

#include "vibe/auth/default_pairing_service.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <utility>

namespace vibe::auth {
namespace {

auto DefaultTimestampProvider() -> std::int64_t {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

auto RandomHexToken(const std::size_t bytes) -> std::string {
  static constexpr std::array<char, 16> kHexDigits{
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };

  thread_local std::random_device random_device;
  thread_local std::mt19937 generator(random_device());
  std::uniform_int_distribution<int> distribution(0, 255);

  std::string token;
  token.reserve(bytes * 2);
  for (std::size_t index = 0; index < bytes; ++index) {
    const auto value = static_cast<unsigned>(distribution(generator));
    token.push_back(kHexDigits[(value >> 4U) & 0x0FU]);
    token.push_back(kHexDigits[value & 0x0FU]);
  }

  return token;
}

auto MakeRandomIdentifier(const std::string& prefix) -> std::string {
  return prefix + RandomHexToken(8);
}

auto MakePairingCode() -> std::string {
  thread_local std::random_device random_device;
  thread_local std::mt19937 generator(random_device());
  std::uniform_int_distribution<int> distribution(0, 999999);

  const auto code_value = distribution(generator);
  std::string code = std::to_string(code_value);
  if (code.size() < 6) {
    code.insert(0, 6 - code.size(), '0');
  }
  return code;
}

}  // namespace

DefaultPairingService::DefaultPairingService(vibe::store::PairingStore& pairing_store)
    : DefaultPairingService(pairing_store,
                            DefaultTimestampProvider,
                            [] { return MakeRandomIdentifier("p_"); },
                            MakePairingCode,
                            [] { return MakeRandomIdentifier("d_"); },
                            [] { return RandomHexToken(24); }) {}

DefaultPairingService::DefaultPairingService(vibe::store::PairingStore& pairing_store,
                                             TimestampProvider timestamp_provider,
                                             StringGenerator pairing_id_generator,
                                             StringGenerator code_generator,
                                             StringGenerator device_id_generator,
                                             StringGenerator token_generator)
    : pairing_store_(pairing_store),
      timestamp_provider_(std::move(timestamp_provider)),
      pairing_id_generator_(std::move(pairing_id_generator)),
      code_generator_(std::move(code_generator)),
      device_id_generator_(std::move(device_id_generator)),
      token_generator_(std::move(token_generator)) {}

auto DefaultPairingService::StartPairing(const std::string& device_name, const DeviceType device_type)
    -> std::optional<PairingRequest> {
  if (device_name.empty()) {
    return std::nullopt;
  }

  PairingRequest request{
      .pairing_id = pairing_id_generator_(),
      .device_name = device_name,
      .device_type = device_type,
      .code = code_generator_(),
      .requested_at_unix_ms = timestamp_provider_(),
  };

  if (request.pairing_id.empty() || request.code.size() != 6) {
    return std::nullopt;
  }

  if (!pairing_store_.UpsertPendingPairing(request)) {
    return std::nullopt;
  }

  return request;
}

auto DefaultPairingService::ListPendingPairings() const -> std::vector<PairingRequest> {
  auto pending_pairings = pairing_store_.LoadPendingPairings();
  std::sort(pending_pairings.begin(), pending_pairings.end(),
            [](const PairingRequest& left, const PairingRequest& right) {
              if (left.requested_at_unix_ms != right.requested_at_unix_ms) {
                return left.requested_at_unix_ms < right.requested_at_unix_ms;
              }
              return left.pairing_id < right.pairing_id;
            });
  return pending_pairings;
}

auto DefaultPairingService::ApprovePairing(const std::string& pairing_id, const std::string& code)
    -> std::optional<PairingRecord> {
  const auto pending_pairings = pairing_store_.LoadPendingPairings();
  const auto pending_it =
      std::find_if(pending_pairings.begin(), pending_pairings.end(),
                   [&](const PairingRequest& pending) { return pending.pairing_id == pairing_id; });
  if (pending_it == pending_pairings.end() || pending_it->code != code) {
    return std::nullopt;
  }

  PairingRecord record{
      .device_id = DeviceId{.value = device_id_generator_()},
      .device_name = pending_it->device_name,
      .device_type = pending_it->device_type,
      .bearer_token = token_generator_(),
      .approved_at_unix_ms = timestamp_provider_(),
  };

  if (record.device_id.value.empty() || record.bearer_token.empty()) {
    return std::nullopt;
  }

  if (!pairing_store_.UpsertApprovedPairing(record)) {
    return std::nullopt;
  }

  if (!pairing_store_.RemovePendingPairing(pairing_id)) {
    static_cast<void>(pairing_store_.RemoveApprovedPairing(record.device_id.value));
    return std::nullopt;
  }

  return record;
}

auto DefaultPairingService::RejectPairing(const std::string& pairing_id) -> bool {
  return pairing_store_.RemovePendingPairing(pairing_id);
}

}  // namespace vibe::auth

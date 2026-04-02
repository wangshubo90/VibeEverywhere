#include "vibe/store/host_config_store.h"

#include <array>
#include <random>

namespace vibe::store {
namespace {

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

auto GenerateHostId() -> std::string { return "h_" + RandomHexToken(16); }

}  // namespace

auto EnsureHostIdentity(HostConfigStore& store) -> std::optional<HostIdentity> {
  HostIdentity identity = store.LoadHostIdentity().value_or(MakeDefaultHostIdentity());
  if (!identity.host_id.empty()) {
    return identity;
  }

  if (identity.display_name.empty()) {
    identity.display_name = std::string(kDefaultDisplayName);
  }
  identity.host_id = GenerateHostId();
  if (!store.SaveHostIdentity(identity)) {
    return std::nullopt;
  }

  return identity;
}

}  // namespace vibe::store

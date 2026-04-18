#ifndef VIBE_SESSION_BOOTSTRAPPED_ENV_CACHE_H
#define VIBE_SESSION_BOOTSTRAPPED_ENV_CACHE_H

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace vibe::session {

// C++20-compatible result type: holds either a value or an error string.
template <typename T>
struct EnvResult {
  std::variant<T, std::string> data;

  [[nodiscard]] bool has_value() const { return std::holds_alternative<T>(data); }
  [[nodiscard]] const T& value() const { return std::get<T>(data); }
  [[nodiscard]] T& value() { return std::get<T>(data); }
  [[nodiscard]] const std::string& error() const { return std::get<std::string>(data); }

  static auto Ok(T val) -> EnvResult<T> {
    EnvResult<T> r;
    r.data = std::move(val);
    return r;
  }
  static auto Err(std::string msg) -> EnvResult<T> {
    EnvResult<T> r;
    r.data = std::move(msg);
    return r;
  }
};

using EnvMapResult = EnvResult<std::unordered_map<std::string, std::string>>;

// Daemon-level cache for bootstrap-captured environments.
// Keyed on shell path. TTL: 300 seconds by default.
// Call Invalidate() on SIGHUP to force re-capture.
class BootstrappedEnvCache {
 public:
  // Returns cached env or runs bootstrap. Blocking (called before fork).
  // Hard timeout: 5 seconds. Returns error string on failure -- does NOT
  // silently fall back to anything.
  [[nodiscard]] auto Get(const std::string& shell_path) -> EnvMapResult;
  [[nodiscard]] auto TakeLastWarning() -> std::optional<std::string>;

  // Invalidate all cached entries (call on SIGHUP).
  void Invalidate();

 private:
  struct Entry {
    std::unordered_map<std::string, std::string> env;
    std::chrono::steady_clock::time_point captured_at;
  };

  [[nodiscard]] auto RunBootstrap(const std::string& shell_path) -> EnvMapResult;

  std::unordered_map<std::string, Entry> cache_;
  std::chrono::seconds ttl_{300};
  std::optional<std::string> last_warning_;
};

}  // namespace vibe::session

#endif

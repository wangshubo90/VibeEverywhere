#ifndef VIBE_BASE_DEBUG_TRACE_H
#define VIBE_BASE_DEBUG_TRACE_H

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>

namespace vibe::base {

inline auto DebugTraceEnabled() -> bool {
#if defined(NDEBUG)
  return false;
#else
  static const bool enabled = [] {
    const char* value = std::getenv("SENTRITS_DEBUG_TRACE");
    return value != nullptr && std::string_view(value) != "0" && std::string_view(value) != "false";
  }();
  return enabled;
#endif
}

inline void DebugTrace(std::string_view scope, std::string_view event, std::string_view detail) {
#if !defined(NDEBUG)
  if (!DebugTraceEnabled()) {
    return;
  }
  static std::mutex mutex;
  const auto now = std::chrono::system_clock::now();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::lock_guard<std::mutex> lock(mutex);
  std::cerr << "[sentrits-debug][" << millis << "][" << scope << "][" << event << "] " << detail
            << '\n';
#else
  (void)scope;
  (void)event;
  (void)detail;
#endif
}

}  // namespace vibe::base

#endif

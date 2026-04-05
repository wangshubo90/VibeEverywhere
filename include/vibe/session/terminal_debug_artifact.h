#ifndef VIBE_SESSION_TERMINAL_DEBUG_ARTIFACT_H
#define VIBE_SESSION_TERMINAL_DEBUG_ARTIFACT_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "vibe/session/terminal_multiplexer.h"

namespace vibe::session {

[[nodiscard]] auto ToDebugJson(const TerminalScreenSnapshot& snapshot) -> std::string;
[[nodiscard]] auto ToDebugJson(const TerminalViewportSnapshot& snapshot) -> std::string;

class TerminalDebugRecorder {
 public:
  explicit TerminalDebugRecorder(std::string_view session_id);

  [[nodiscard]] auto enabled() const -> bool;

  void RecordPtyOutput(std::string_view raw_chunk, std::uint64_t sequence,
                       const TerminalScreenSnapshot& screen_snapshot);
  void RecordViewport(std::string_view view_id, const TerminalViewportSnapshot& viewport_snapshot);

 private:
  void WriteFile(std::string_view file_name, std::string_view content) const;

  std::optional<std::string> root_dir_;
  std::string session_id_;
};

}  // namespace vibe::session

#endif

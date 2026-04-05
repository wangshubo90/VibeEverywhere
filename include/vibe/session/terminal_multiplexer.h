#ifndef VIBE_SESSION_TERMINAL_MULTIPLEXER_H
#define VIBE_SESSION_TERMINAL_MULTIPLEXER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vibe/session/launch_spec.h"

namespace vibe::session {

struct TerminalScreenSnapshot {
  std::uint16_t columns{0};
  std::uint16_t rows{0};
  std::uint64_t render_revision{0};
  std::size_t cursor_row{0};
  std::size_t cursor_column{0};
  std::vector<std::string> visible_lines;
  std::vector<std::string> scrollback_lines;
  std::string bootstrap_ansi;
};

struct TerminalViewportState {
  std::uint16_t columns{0};
  std::uint16_t rows{0};
  std::size_t horizontal_offset{0};
  bool follow_cursor{true};

  [[nodiscard]] auto operator==(const TerminalViewportState& other) const -> bool = default;
};

struct TerminalViewportSnapshot {
  std::string view_id;
  std::uint16_t columns{0};
  std::uint16_t rows{0};
  std::uint64_t render_revision{0};
  std::size_t total_line_count{0};
  std::size_t viewport_top_line{0};
  std::size_t horizontal_offset{0};
  std::optional<std::size_t> cursor_viewport_row;
  std::optional<std::size_t> cursor_viewport_column;
  std::vector<std::string> visible_lines;
  std::string bootstrap_ansi;

  [[nodiscard]] auto operator==(const TerminalViewportSnapshot& other) const -> bool = default;
};

class TerminalMultiplexer {
 public:
  explicit TerminalMultiplexer(TerminalSize terminal_size = {},
                               std::size_t max_scrollback_lines = 2000U);
  ~TerminalMultiplexer();
  TerminalMultiplexer(TerminalMultiplexer&&) noexcept;
  auto operator=(TerminalMultiplexer&&) noexcept -> TerminalMultiplexer&;

  TerminalMultiplexer(const TerminalMultiplexer&) = delete;
  auto operator=(const TerminalMultiplexer&) -> TerminalMultiplexer& = delete;

  void Resize(TerminalSize terminal_size);
  void Append(std::string_view data);
  void UpdateViewport(std::string_view view_id, TerminalSize viewport_size);
  void RemoveViewport(std::string_view view_id);

  [[nodiscard]] auto terminal_size() const -> TerminalSize;
  [[nodiscard]] auto snapshot() const -> TerminalScreenSnapshot;
  [[nodiscard]] auto viewport_snapshot(std::string_view view_id) const
      -> std::optional<TerminalViewportSnapshot>;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vibe::session

#endif

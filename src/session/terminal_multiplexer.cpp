#include "vibe/session/terminal_multiplexer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "vterm.h"
}

namespace vibe::session {
namespace {

auto TrimRight(std::string value) -> std::string {
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  return value;
}

void AppendUtf8(std::string& out, const uint32_t codepoint) {
  if (codepoint == 0U) {
    return;
  }

  if (codepoint <= 0x7FU) {
    out.push_back(static_cast<char>(codepoint));
    return;
  }

  if (codepoint <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | ((codepoint >> 6U) & 0x1FU)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    return;
  }

  if (codepoint <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | ((codepoint >> 12U) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    return;
  }

  out.push_back(static_cast<char>(0xF0U | ((codepoint >> 18U) & 0x07U)));
  out.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
  out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
  out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
}

auto CellsToUtf8Line(const VTermScreenCell* cells, const int cols) -> std::string {
  std::string line;
  for (int col = 0; col < cols; ++col) {
    const VTermScreenCell& cell = cells[col];
    if (cell.width == 0) {
      continue;
    }

    bool emitted = false;
    for (const uint32_t codepoint : cell.chars) {
      if (codepoint == 0U) {
        break;
      }
      AppendUtf8(line, codepoint);
      emitted = true;
    }

    if (!emitted) {
      line.push_back(' ');
    }
  }
  return TrimRight(std::move(line));
}

auto NextUtf8CodepointLength(const std::string_view input, const std::size_t offset) -> std::size_t {
  if (offset >= input.size()) {
    return 0;
  }

  const unsigned char lead = static_cast<unsigned char>(input[offset]);
  if ((lead & 0x80U) == 0U) {
    return 1U;
  }
  if ((lead & 0xE0U) == 0xC0U) {
    return 2U;
  }
  if ((lead & 0xF0U) == 0xE0U) {
    return 3U;
  }
  if ((lead & 0xF8U) == 0xF0U) {
    return 4U;
  }
  return 1U;
}

auto ClipUtf8Columns(const std::string& line, const std::size_t start_column,
                     const std::size_t max_columns) -> std::string {
  if (max_columns == 0 || line.empty()) {
    return "";
  }

  std::size_t byte_offset = 0;
  std::size_t current_column = 0;
  while (byte_offset < line.size() && current_column < start_column) {
    byte_offset += NextUtf8CodepointLength(line, byte_offset);
    current_column += 1U;
  }

  std::string clipped;
  std::size_t emitted_columns = 0;
  while (byte_offset < line.size() && emitted_columns < max_columns) {
    const std::size_t codepoint_bytes = NextUtf8CodepointLength(line, byte_offset);
    clipped.append(line, byte_offset, codepoint_bytes);
    byte_offset += codepoint_bytes;
    emitted_columns += 1U;
  }

  return clipped;
}

auto HasAnyStyle(const VTermScreenCell& cell) -> bool {
  return cell.attrs.bold || cell.attrs.underline != VTERM_UNDERLINE_OFF || cell.attrs.italic ||
         cell.attrs.blink || cell.attrs.reverse || cell.attrs.conceal || cell.attrs.strike ||
         !VTERM_COLOR_IS_DEFAULT_FG(&cell.fg) || !VTERM_COLOR_IS_DEFAULT_BG(&cell.bg);
}

auto SameStyle(const VTermScreenCell& left, const VTermScreenCell& right) -> bool {
  return left.attrs.bold == right.attrs.bold && left.attrs.underline == right.attrs.underline &&
         left.attrs.italic == right.attrs.italic && left.attrs.blink == right.attrs.blink &&
         left.attrs.reverse == right.attrs.reverse && left.attrs.conceal == right.attrs.conceal &&
         left.attrs.strike == right.attrs.strike && vterm_color_is_equal(&left.fg, &right.fg) &&
         vterm_color_is_equal(&left.bg, &right.bg);
}

void AppendColorSgr(std::string& out, VTermScreen* screen, const VTermColor& color, const bool foreground) {
  if ((foreground && VTERM_COLOR_IS_DEFAULT_FG(&color)) || (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color))) {
    return;
  }

  VTermColor rgb = color;
  vterm_screen_convert_color_to_rgb(screen, &rgb);
  out.append(foreground ? ";38;2;" : ";48;2;");
  out.append(std::to_string(rgb.rgb.red));
  out.push_back(';');
  out.append(std::to_string(rgb.rgb.green));
  out.push_back(';');
  out.append(std::to_string(rgb.rgb.blue));
}

void AppendFullStyleSgr(std::string& out, VTermScreen* screen, const VTermScreenCell& cell) {
  out.append("\x1b[0");
  if (cell.attrs.bold) {
    out.append(";1");
  }
  if (cell.attrs.italic) {
    out.append(";3");
  }
  switch (cell.attrs.underline) {
    case VTERM_UNDERLINE_SINGLE:
      out.append(";4");
      break;
    case VTERM_UNDERLINE_DOUBLE:
      out.append(";21");
      break;
    case VTERM_UNDERLINE_CURLY:
      out.append(";4:3");
      break;
    default:
      break;
  }
  if (cell.attrs.blink) {
    out.append(";5");
  }
  if (cell.attrs.reverse) {
    out.append(";7");
  }
  if (cell.attrs.conceal) {
    out.append(";8");
  }
  if (cell.attrs.strike) {
    out.append(";9");
  }
  AppendColorSgr(out, screen, cell.fg, true);
  AppendColorSgr(out, screen, cell.bg, false);
  out.push_back('m');
}

}  // namespace

class TerminalMultiplexer::Impl {
 public:
  Impl(const TerminalSize terminal_size, const std::size_t max_scrollback_lines)
      : terminal_size_(NormalizeSize(terminal_size)),
        max_scrollback_lines_(max_scrollback_lines),
        vt_(vterm_new(static_cast<int>(terminal_size_.rows), static_cast<int>(terminal_size_.columns))),
        state_(vterm_obtain_state(vt_)),
        screen_(vterm_obtain_screen(vt_)) {
    vterm_set_utf8(vt_, 1);

    screen_callbacks_ = VTermScreenCallbacks{
        .damage = &Impl::OnDamage,
        .moverect = nullptr,
        .movecursor = &Impl::OnMoveCursor,
        .settermprop = nullptr,
        .bell = nullptr,
        .resize = &Impl::OnResize,
        .sb_pushline = &Impl::OnScrollbackPush,
        .sb_popline = &Impl::OnScrollbackPop,
        .sb_clear = &Impl::OnScrollbackClear,
    };

    vterm_screen_set_callbacks(screen_, &screen_callbacks_, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_enable_reflow(screen_, false);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCREEN);
    vterm_screen_reset(screen_, 1);
    RefreshSnapshot();
    dirty_ = false;
  }

  ~Impl() {
    if (vt_ != nullptr) {
      vterm_free(vt_);
      vt_ = nullptr;
    }
  }

  void Resize(const TerminalSize terminal_size) {
    terminal_size_ = NormalizeSize(terminal_size);
    dirty_ = false;
    vterm_set_size(vt_, static_cast<int>(terminal_size_.rows), static_cast<int>(terminal_size_.columns));
    vterm_screen_flush_damage(screen_);
    RefreshSnapshot();
    if (dirty_) {
      render_revision_ += 1U;
      snapshot_.render_revision = render_revision_;
      dirty_ = false;
    }
  }

  void Append(const std::string_view data) {
    if (data.empty()) {
      return;
    }

    dirty_ = false;
    static_cast<void>(vterm_input_write(vt_, data.data(), data.size()));
    vterm_screen_flush_damage(screen_);
    RefreshSnapshot();
    if (dirty_) {
      render_revision_ += 1U;
      snapshot_.render_revision = render_revision_;
      dirty_ = false;
    }
  }

  [[nodiscard]] auto terminal_size() const -> TerminalSize { return terminal_size_; }

  [[nodiscard]] auto snapshot() const -> TerminalScreenSnapshot { return snapshot_; }

  void UpdateViewport(const std::string_view view_id, const TerminalSize viewport_size) {
    if (view_id.empty()) {
      return;
    }

    auto& state = viewports_[std::string(view_id)];
    state.columns = NormalizeSize(viewport_size).columns;
    state.rows = NormalizeSize(viewport_size).rows;
  }

  void RemoveViewport(const std::string_view view_id) {
    if (view_id.empty()) {
      return;
    }

    viewports_.erase(std::string(view_id));
  }

  [[nodiscard]] auto ViewportSnapshot(const std::string_view view_id) const
      -> std::optional<TerminalViewportSnapshot> {
    const auto it = viewports_.find(std::string(view_id));
    if (it == viewports_.end()) {
      return std::nullopt;
    }

    const TerminalViewportState& state = it->second;
    const std::size_t viewport_rows = std::max<std::size_t>(1U, state.rows);
    const std::size_t viewport_columns = std::max<std::size_t>(1U, state.columns);
    const std::size_t scrollback_count = snapshot_.scrollback_lines.size();
    const std::size_t total_line_count = scrollback_count + snapshot_.visible_lines.size();
    const std::size_t cursor_absolute_row = scrollback_count + snapshot_.cursor_row;

    std::size_t viewport_top_line = 0;
    if (total_line_count > viewport_rows) {
      const std::size_t max_top_line = total_line_count - viewport_rows;
      if (state.follow_cursor) {
        viewport_top_line = cursor_absolute_row + 1U > viewport_rows
                                ? cursor_absolute_row + 1U - viewport_rows
                                : 0U;
      } else {
        viewport_top_line = max_top_line;
      }
      viewport_top_line = std::min(viewport_top_line, max_top_line);
    }

    std::size_t horizontal_offset = state.horizontal_offset;
    if (state.follow_cursor) {
      if (snapshot_.cursor_column < horizontal_offset) {
        horizontal_offset = snapshot_.cursor_column;
      } else if (snapshot_.cursor_column >= horizontal_offset + viewport_columns) {
        horizontal_offset = snapshot_.cursor_column + 1U - viewport_columns;
      }
    }

    std::vector<std::string> all_lines;
    all_lines.reserve(total_line_count);
    all_lines.insert(all_lines.end(), snapshot_.scrollback_lines.begin(), snapshot_.scrollback_lines.end());
    all_lines.insert(all_lines.end(), snapshot_.visible_lines.begin(), snapshot_.visible_lines.end());

    TerminalViewportSnapshot viewport_snapshot{
        .view_id = std::string(view_id),
        .columns = static_cast<std::uint16_t>(viewport_columns),
        .rows = static_cast<std::uint16_t>(viewport_rows),
        .render_revision = snapshot_.render_revision,
        .total_line_count = total_line_count,
        .viewport_top_line = viewport_top_line,
        .horizontal_offset = horizontal_offset,
        .cursor_viewport_row = std::nullopt,
        .cursor_viewport_column = std::nullopt,
        .visible_lines = {},
        .bootstrap_ansi = {},
    };

    viewport_snapshot.visible_lines.reserve(viewport_rows);
    for (std::size_t index = 0; index < viewport_rows; ++index) {
      const std::size_t line_index = viewport_top_line + index;
      if (line_index >= all_lines.size()) {
        viewport_snapshot.visible_lines.emplace_back();
        continue;
      }

      viewport_snapshot.visible_lines.push_back(
          ClipUtf8Columns(all_lines[line_index], horizontal_offset, viewport_columns));
    }

    if (cursor_absolute_row >= viewport_top_line && cursor_absolute_row < viewport_top_line + viewport_rows) {
      viewport_snapshot.cursor_viewport_row = cursor_absolute_row - viewport_top_line;
      if (snapshot_.cursor_column >= horizontal_offset &&
          snapshot_.cursor_column < horizontal_offset + viewport_columns) {
        viewport_snapshot.cursor_viewport_column = snapshot_.cursor_column - horizontal_offset;
      }
    }

    viewport_snapshot.bootstrap_ansi =
        BuildViewportBootstrapAnsi(viewport_top_line, horizontal_offset, viewport_columns, viewport_rows,
                                   scrollback_count, total_line_count, viewport_snapshot.cursor_viewport_row,
                                   viewport_snapshot.cursor_viewport_column);

    return viewport_snapshot;
  }

 private:
  static auto NormalizeSize(const TerminalSize terminal_size) -> TerminalSize {
    return TerminalSize{
        .columns = std::max<std::uint16_t>(1, terminal_size.columns),
        .rows = std::max<std::uint16_t>(1, terminal_size.rows),
    };
  }

  void RefreshSnapshot() {
    snapshot_.columns = terminal_size_.columns;
    snapshot_.rows = terminal_size_.rows;
    snapshot_.render_revision = render_revision_;
    snapshot_.visible_lines.clear();
    snapshot_.visible_lines.reserve(terminal_size_.rows);
    snapshot_.scrollback_lines.assign(scrollback_lines_.begin(), scrollback_lines_.end());

    for (int row = 0; row < static_cast<int>(terminal_size_.rows); ++row) {
      std::vector<VTermScreenCell> cells(static_cast<std::size_t>(terminal_size_.columns));
      for (int col = 0; col < static_cast<int>(terminal_size_.columns); ++col) {
        VTermScreenCell cell{};
        static_cast<void>(vterm_screen_get_cell(screen_, VTermPos{.row = row, .col = col}, &cell));
        cells[static_cast<std::size_t>(col)] = cell;
      }
      snapshot_.visible_lines.push_back(
          CellsToUtf8Line(cells.data(), row >= 0 ? static_cast<int>(terminal_size_.columns) : 0));
    }

    VTermPos cursor{};
    vterm_state_get_cursorpos(state_, &cursor);
    snapshot_.cursor_row = cursor.row >= 0 ? static_cast<std::size_t>(cursor.row) : 0U;
    snapshot_.cursor_column = cursor.col >= 0 ? static_cast<std::size_t>(cursor.col) : 0U;
    snapshot_.bootstrap_ansi = BuildScreenBootstrapAnsi();
  }

  [[nodiscard]] auto BuildStyledVisibleRow(const std::size_t row_index, const std::size_t start_column,
                                           const std::size_t max_columns) const -> std::string {
    if (row_index >= terminal_size_.rows || max_columns == 0) {
      return "";
    }

    const std::size_t end_column =
        std::min<std::size_t>(terminal_size_.columns, start_column + max_columns);
    std::string out;
    bool have_style = false;
    VTermScreenCell active_style{};

    for (std::size_t column = start_column; column < end_column; ++column) {
      VTermScreenCell cell{};
      static_cast<void>(vterm_screen_get_cell(
          screen_, VTermPos{.row = static_cast<int>(row_index), .col = static_cast<int>(column)}, &cell));
      if (cell.width == 0) {
        continue;
      }

      if (!have_style || !SameStyle(active_style, cell)) {
        if (HasAnyStyle(cell)) {
          AppendFullStyleSgr(out, screen_, cell);
        } else if (have_style) {
          out.append("\x1b[0m");
        }
        active_style = cell;
        have_style = true;
      }

      bool emitted = false;
      for (const uint32_t codepoint : cell.chars) {
        if (codepoint == 0U) {
          break;
        }
        AppendUtf8(out, codepoint);
        emitted = true;
      }
      if (!emitted) {
        out.push_back(' ');
      }
    }

    if (have_style) {
      out.append("\x1b[0m");
    }
    return out;
  }

  [[nodiscard]] auto BuildScreenBootstrapAnsi() const -> std::string {
    std::string out;
    if (!snapshot_.scrollback_lines.empty()) {
      out.append(snapshot_.scrollback_lines.front());
      for (std::size_t index = 1; index < snapshot_.scrollback_lines.size(); ++index) {
        out.append("\r\n");
        out.append(snapshot_.scrollback_lines[index]);
      }
      out.append("\r\n");
    }

    out.append("\x1b[2J\x1b[H");
    for (std::size_t row = 0; row < terminal_size_.rows; ++row) {
      out.append(BuildStyledVisibleRow(row, 0U, terminal_size_.columns));
      if (row + 1U < terminal_size_.rows) {
        out.append("\x1b[E");
      }
    }
    out.append("\x1b[");
    out.append(std::to_string(snapshot_.cursor_row + 1U));
    out.push_back(';');
    out.append(std::to_string(snapshot_.cursor_column + 1U));
    out.push_back('H');
    return out;
  }

  [[nodiscard]] auto BuildViewportBootstrapAnsi(
      const std::size_t viewport_top_line, const std::size_t horizontal_offset, const std::size_t viewport_columns,
      const std::size_t viewport_rows, const std::size_t scrollback_count, const std::size_t total_line_count,
      const std::optional<std::size_t>& cursor_row, const std::optional<std::size_t>& cursor_column) const
      -> std::string {
    std::string out;
    if (viewport_top_line > 0) {
      const std::size_t history_count = std::min(viewport_top_line, total_line_count);
      for (std::size_t index = 0; index < history_count; ++index) {
        if (index > 0) {
          out.append("\r\n");
        }
        if (index < snapshot_.scrollback_lines.size()) {
          out.append(ClipUtf8Columns(snapshot_.scrollback_lines[index], horizontal_offset, viewport_columns));
          continue;
        }
        const std::size_t screen_row = index - scrollback_count;
        out.append(BuildStyledVisibleRow(screen_row, horizontal_offset, viewport_columns));
      }
      out.append("\r\n");
    }

    out.append("\x1b[2J\x1b[H");
    for (std::size_t row = 0; row < viewport_rows; ++row) {
      const std::size_t line_index = viewport_top_line + row;
      if (line_index < scrollback_count) {
        out.append(ClipUtf8Columns(snapshot_.scrollback_lines[line_index], horizontal_offset, viewport_columns));
      } else if (line_index < total_line_count) {
        out.append(BuildStyledVisibleRow(line_index - scrollback_count, horizontal_offset, viewport_columns));
      }
      if (row + 1U < viewport_rows) {
        out.append("\x1b[E");
      }
    }

    if (cursor_row.has_value() && cursor_column.has_value()) {
      out.append("\x1b[");
      out.append(std::to_string(*cursor_row + 1U));
      out.push_back(';');
      out.append(std::to_string(*cursor_column + 1U));
      out.push_back('H');
    }
    return out;
  }

  void PushScrollbackLine(const std::string& line) {
    scrollback_lines_.push_back(line);
    while (scrollback_lines_.size() > max_scrollback_lines_) {
      scrollback_lines_.pop_front();
    }
  }

  static auto OnDamage(VTermRect /*rect*/, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    self->dirty_ = true;
    return 1;
  }

  static auto OnMoveCursor(VTermPos /*pos*/, VTermPos /*oldpos*/, int /*visible*/, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    self->dirty_ = true;
    return 1;
  }

  static auto OnResize(int rows, int cols, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    self->terminal_size_ = NormalizeSize(TerminalSize{
        .columns = static_cast<std::uint16_t>(std::max(cols, 1)),
        .rows = static_cast<std::uint16_t>(std::max(rows, 1)),
    });
    self->dirty_ = true;
    return 1;
  }

  static auto OnScrollbackPush(int cols, const VTermScreenCell* cells, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    self->PushScrollbackLine(CellsToUtf8Line(cells, cols));
    self->dirty_ = true;
    return 1;
  }

  static auto OnScrollbackPop(int cols, VTermScreenCell* cells, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    if (self->scrollback_lines_.empty()) {
      return 0;
    }

    const std::string line = self->scrollback_lines_.back();
    self->scrollback_lines_.pop_back();
    std::vector<uint32_t> codepoints;
    codepoints.reserve(line.size());
    for (const char ch : line) {
      const unsigned char byte = static_cast<unsigned char>(ch);
      codepoints.push_back(static_cast<uint32_t>(byte));
    }

    for (int col = 0; col < cols; ++col) {
      std::memset(&cells[col], 0, sizeof(VTermScreenCell));
      if (col < static_cast<int>(codepoints.size())) {
        cells[col].chars[0] = codepoints[static_cast<std::size_t>(col)];
        cells[col].width = 1;
      } else {
        cells[col].chars[0] = static_cast<uint32_t>(' ');
        cells[col].width = 1;
      }
    }

    self->dirty_ = true;
    return 1;
  }

  static auto OnScrollbackClear(void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    self->scrollback_lines_.clear();
    self->dirty_ = true;
    return 1;
  }

  TerminalSize terminal_size_;
  std::size_t max_scrollback_lines_;
  VTerm* vt_{nullptr};
  VTermState* state_{nullptr};
  VTermScreen* screen_{nullptr};
  VTermScreenCallbacks screen_callbacks_{};
  std::deque<std::string> scrollback_lines_;
  std::unordered_map<std::string, TerminalViewportState> viewports_;
  TerminalScreenSnapshot snapshot_{};
  std::uint64_t render_revision_{0};
  bool dirty_{false};
};

TerminalMultiplexer::TerminalMultiplexer(const TerminalSize terminal_size,
                                         const std::size_t max_scrollback_lines)
    : impl_(std::make_unique<Impl>(terminal_size, max_scrollback_lines)) {}

TerminalMultiplexer::~TerminalMultiplexer() = default;

TerminalMultiplexer::TerminalMultiplexer(TerminalMultiplexer&&) noexcept = default;

auto TerminalMultiplexer::operator=(TerminalMultiplexer&&) noexcept -> TerminalMultiplexer& = default;

void TerminalMultiplexer::Resize(const TerminalSize terminal_size) { impl_->Resize(terminal_size); }

void TerminalMultiplexer::Append(const std::string_view data) { impl_->Append(data); }

auto TerminalMultiplexer::terminal_size() const -> TerminalSize { return impl_->terminal_size(); }

auto TerminalMultiplexer::snapshot() const -> TerminalScreenSnapshot { return impl_->snapshot(); }

void TerminalMultiplexer::UpdateViewport(const std::string_view view_id,
                                         const TerminalSize viewport_size) {
  impl_->UpdateViewport(view_id, viewport_size);
}

void TerminalMultiplexer::RemoveViewport(const std::string_view view_id) { impl_->RemoveViewport(view_id); }

auto TerminalMultiplexer::viewport_snapshot(const std::string_view view_id) const
    -> std::optional<TerminalViewportSnapshot> {
  return impl_->ViewportSnapshot(view_id);
}

}  // namespace vibe::session

#include "vibe/session/terminal_multiplexer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vibe/base/debug_trace.h"

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

auto ContainsSequence(const std::string_view input, const std::string_view needle) -> bool {
  return input.find(needle) != std::string_view::npos;
}

auto SummarizeBootstrapColors(const std::string& bootstrap) -> std::string {
  std::ostringstream trace;
  trace << "bytes=" << bootstrap.size()
        << " hasIndexedFg=" << (ContainsSequence(bootstrap, ";38;5;") ? "true" : "false")
        << " hasIndexedBg=" << (ContainsSequence(bootstrap, ";48;5;") ? "true" : "false")
        << " hasRgbFg=" << (ContainsSequence(bootstrap, ";38;2;") ? "true" : "false")
        << " hasRgbBg=" << (ContainsSequence(bootstrap, ";48;2;") ? "true" : "false")
        << " hasReset=" << (ContainsSequence(bootstrap, "\x1b[0m") ? "true" : "false");
  return trace.str();
}

auto DescribeColor(const VTermColor& color, const bool foreground) -> std::string {
  if ((foreground && VTERM_COLOR_IS_DEFAULT_FG(&color)) || (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color))) {
    return "default";
  }
  if (VTERM_COLOR_IS_INDEXED(&color)) {
    return "idx:" + std::to_string(color.indexed.idx);
  }
  return "rgb:" + std::to_string(color.rgb.red) + "," + std::to_string(color.rgb.green) + "," +
         std::to_string(color.rgb.blue);
}

auto BuildAppendTraceSummary(const std::string_view data) -> std::optional<std::string> {
  std::vector<std::string_view> tags;
  if (ContainsSequence(data, "\x1b[?1049h")) {
    tags.emplace_back("alt-enter-1049");
  }
  if (ContainsSequence(data, "\x1b[?1049l")) {
    tags.emplace_back("alt-exit-1049");
  }
  if (ContainsSequence(data, "\x1b[?1047h")) {
    tags.emplace_back("alt-enter-1047");
  }
  if (ContainsSequence(data, "\x1b[?1047l")) {
    tags.emplace_back("alt-exit-1047");
  }
  if (ContainsSequence(data, "\x1b[?47h")) {
    tags.emplace_back("alt-enter-47");
  }
  if (ContainsSequence(data, "\x1b[?47l")) {
    tags.emplace_back("alt-exit-47");
  }
  if (ContainsSequence(data, "\x1b[2J")) {
    tags.emplace_back("clear-2J");
  }
  if (ContainsSequence(data, "\x1b[3J")) {
    tags.emplace_back("clear-3J");
  }
  if (ContainsSequence(data, "\x1b[H")) {
    tags.emplace_back("cursor-home");
  }

  if (tags.empty()) {
    return std::nullopt;
  }

  std::ostringstream summary;
  summary << "bytes=" << data.size() << " tags=";
  for (std::size_t index = 0; index < tags.size(); ++index) {
    if (index > 0) {
      summary << ',';
    }
    summary << tags[index];
  }
  return summary.str();
}

auto HasAltScreenEnter(const std::string_view data) -> bool {
  return ContainsSequence(data, "\x1b[?1049h") || ContainsSequence(data, "\x1b[?1047h") ||
         ContainsSequence(data, "\x1b[?47h");
}

auto HasAltScreenExit(const std::string_view data) -> bool {
  return ContainsSequence(data, "\x1b[?1049l") || ContainsSequence(data, "\x1b[?1047l") ||
         ContainsSequence(data, "\x1b[?47l");
}

// Returns true only for bare \r not followed by \n.
// \r\n is a standard PTY CRLF newline and must not be treated as an in-place overwrite.
auto HasBareCarriageReturn(const std::string_view data) -> bool {
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '\r' && (i + 1 >= data.size() || data[i + 1] != '\n')) {
      return true;
    }
  }
  return false;
}

auto HasLineErase(const std::string_view data) -> bool {
  return ContainsSequence(data, "\x1b[2K") || ContainsSequence(data, "\x1b[K");
}

auto IsTrivialVisibleCharacter(const char ch) -> bool {
  switch (ch) {
    case '.':
    case ',':
    case ':':
    case ';':
    case '\'':
    case '"':
    case '`':
    case '-':
    case '_':
    case '=':
    case '+':
    case '|':
    case '/':
    case '\\':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '<':
    case '>':
    case '*':
    case '#':
    case '!':
    case '?':
      return true;
    default:
      break;
  }
  return false;
}

auto CountVisibleTailGrowth(const std::string& before, const std::string& after) -> std::size_t {
  if (after.size() <= before.size() || after.compare(0, before.size(), before) != 0) {
    return 0;
  }

  std::size_t visible_count = 0;
  for (std::size_t index = before.size(); index < after.size(); ++index) {
    const unsigned char ch = static_cast<unsigned char>(after[index]);
    if (ch < 0x20U || ch == 0x7FU) {
      continue;
    }
    visible_count += 1U;
  }
  return visible_count;
}

auto HasOnlyTrivialTailGrowth(const std::string& before, const std::string& after) -> bool {
  if (after.size() <= before.size() || after.compare(0, before.size(), before) != 0) {
    return false;
  }

  bool saw_visible = false;
  for (std::size_t index = before.size(); index < after.size(); ++index) {
    const unsigned char ch = static_cast<unsigned char>(after[index]);
    if (ch < 0x20U || ch == 0x7FU) {
      continue;
    }
    saw_visible = true;
    if (!IsTrivialVisibleCharacter(static_cast<char>(ch))) {
      return false;
    }
  }

  return saw_visible;
}

auto IsScreenBlank(const TerminalScreenSnapshot& snapshot) -> bool {
  return std::all_of(snapshot.visible_lines.begin(), snapshot.visible_lines.end(),
                     [](const std::string& line) { return line.empty(); });
}

auto FindSingleChangedVisibleLine(const TerminalScreenSnapshot& before, const TerminalScreenSnapshot& after)
    -> std::optional<std::pair<std::string_view, std::string_view>> {
  const std::size_t compared_visible_lines =
      std::max(before.visible_lines.size(), after.visible_lines.size());
  std::optional<std::pair<std::string_view, std::string_view>> changed_line;
  for (std::size_t index = 0; index < compared_visible_lines; ++index) {
    const std::string_view before_line =
        index < before.visible_lines.size() ? std::string_view(before.visible_lines[index]) : std::string_view();
    const std::string_view after_line =
        index < after.visible_lines.size() ? std::string_view(after.visible_lines[index]) : std::string_view();
    if (before_line == after_line) {
      continue;
    }
    if (changed_line.has_value()) {
      return std::nullopt;
    }
    changed_line = std::make_pair(before_line, after_line);
  }
  return changed_line;
}

auto NormalizeUnicodeScalar(const uint32_t codepoint) -> uint32_t {
  if (codepoint == 0U) {
    return 0U;
  }

  // libvterm can surface out-of-range values for unsupported glyphs. Replace
  // invalid Unicode scalars so snapshot JSON always remains valid UTF-8.
  if (codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
    return 0xFFFDU;
  }

  return codepoint;
}

void AppendUtf8(std::string& out, const uint32_t codepoint) {
  const uint32_t scalar = NormalizeUnicodeScalar(codepoint);
  if (scalar == 0U) {
    return;
  }

  if (scalar <= 0x7FU) {
    out.push_back(static_cast<char>(scalar));
    return;
  }

  if (scalar <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | ((scalar >> 6U) & 0x1FU)));
    out.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    return;
  }

  if (scalar <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | ((scalar >> 12U) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    return;
  }

  out.push_back(static_cast<char>(0xF0U | ((scalar >> 18U) & 0x07U)));
  out.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
  out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
  out.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
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

  if (VTERM_COLOR_IS_INDEXED(&color)) {
    // Preserve indexed color references so the receiving terminal uses its own
    // palette, matching the raw PTY output.  Converting to RGB via libvterm's
    // palette would produce different values than the terminal's own palette
    // (especially for colors 0-15), causing visible color changes in the
    // viewport snapshot vs. the raw output.
    out.append(foreground ? ";38;5;" : ";48;5;");
    out.append(std::to_string(color.indexed.idx));
    return;
  }

  // Already an explicit RGB color — emit as 24-bit true color.
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

    const TerminalScreenSnapshot snapshot_before = snapshot_;
    const std::size_t scrollback_before = scrollback_lines_.size();
    const std::uint64_t render_revision_before = render_revision_;
    if (const auto summary = BuildAppendTraceSummary(data); summary.has_value()) {
      vibe::base::DebugTrace("core.terminal", "append.escape", *summary);
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

    last_semantic_change_ = ClassifySemanticChange(data, snapshot_before, snapshot_);

    std::ostringstream trace;
    trace << "bytes=" << data.size() << " scrollbackBefore=" << scrollback_before
          << " scrollbackAfter=" << scrollback_lines_.size()
          << " renderRevisionBefore=" << render_revision_before
          << " renderRevisionAfter=" << render_revision_
          << " cursorRow=" << snapshot_.cursor_row << " cursorColumn=" << snapshot_.cursor_column
          << " semanticKind=" << static_cast<int>(last_semantic_change_.kind)
          << " changedLines=" << last_semantic_change_.changed_visible_line_count
          << " scrollbackAdded=" << last_semantic_change_.scrollback_lines_added
          << " appendedChars=" << last_semantic_change_.appended_visible_character_count;
    vibe::base::DebugTrace("core.terminal", "append.state", trace.str());
  }

  [[nodiscard]] auto terminal_size() const -> TerminalSize { return terminal_size_; }

  [[nodiscard]] auto snapshot() const -> TerminalScreenSnapshot { return snapshot_; }

  [[nodiscard]] auto last_semantic_change() const -> TerminalSemanticChange {
    return last_semantic_change_;
  }

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

    // Always anchor the viewport to the tail of the terminal buffer.
    // Vertical cursor-following causes the viewport_top_line to oscillate
    // when an observer's viewport size differs from the active PTY size:
    // a screen redraw sweeps the cursor from row 0 down to its final
    // position, and each intermediate observer snapshot fires at a
    // different cursor row, producing alternating views. Showing the last
    // viewport_rows lines of all content is stable and what an observer
    // expects when watching a live session.
    std::size_t viewport_top_line = 0;
    if (total_line_count > viewport_rows) {
      viewport_top_line = total_line_count - viewport_rows;
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

  [[nodiscard]] auto ClassifySemanticChange(const std::string_view data,
                                            const TerminalScreenSnapshot& before,
                                            const TerminalScreenSnapshot& after) const
      -> TerminalSemanticChange {
    const bool saw_carriage_return = HasBareCarriageReturn(data);
    const bool saw_line_erase = HasLineErase(data);
    const bool before_screen_blank = IsScreenBlank(before);
    TerminalSemanticChange change{
        .kind = TerminalSemanticChangeKind::None,
        .changed_visible_line_count = 0,
        .scrollback_lines_added =
            after.scrollback_lines.size() > before.scrollback_lines.size()
                ? after.scrollback_lines.size() - before.scrollback_lines.size()
                : 0U,
        .appended_visible_character_count = 0,
        .cursor_moved = before.cursor_row != after.cursor_row || before.cursor_column != after.cursor_column,
        .alt_screen_entered = HasAltScreenEnter(data),
        .alt_screen_exited = HasAltScreenExit(data),
    };

    const std::size_t compared_visible_lines =
        std::max(before.visible_lines.size(), after.visible_lines.size());
    for (std::size_t index = 0; index < compared_visible_lines; ++index) {
      const std::string_view before_line =
          index < before.visible_lines.size() ? std::string_view(before.visible_lines[index]) : std::string_view();
      const std::string_view after_line =
          index < after.visible_lines.size() ? std::string_view(after.visible_lines[index]) : std::string_view();
      if (before_line != after_line) {
        change.changed_visible_line_count += 1U;
      }
    }

    const std::size_t compared_line_count =
        std::min(before.visible_lines.size(), after.visible_lines.size());
    for (std::size_t index = 0; index < compared_line_count; ++index) {
      change.appended_visible_character_count +=
          CountVisibleTailGrowth(before.visible_lines[index], after.visible_lines[index]);
    }

    if (change.alt_screen_entered || change.alt_screen_exited) {
      change.kind = TerminalSemanticChangeKind::AltScreenTransition;
      return change;
    }

    if (after.render_revision == before.render_revision) {
      return change;
    }

    if (change.scrollback_lines_added > 0U) {
      change.kind = TerminalSemanticChangeKind::MeaningfulOutput;
      return change;
    }

    if (change.changed_visible_line_count == 0U && change.cursor_moved) {
      change.kind = TerminalSemanticChangeKind::CursorOnly;
      return change;
    }

    if (change.scrollback_lines_added == 0U && change.changed_visible_line_count == 1U &&
        (saw_carriage_return || saw_line_erase)) {
      if (const auto changed_line = FindSingleChangedVisibleLine(before, after); changed_line.has_value()) {
        const auto& [before_line, after_line] = *changed_line;
        if (saw_line_erase && change.appended_visible_character_count == 0U && !before_line.empty() &&
            after_line.empty()) {
          change.kind = TerminalSemanticChangeKind::CursorOnly;
          return change;
        }

        if (before_screen_blank || !before_line.empty() || !after_line.empty()) {
          change.kind = TerminalSemanticChangeKind::CosmeticChurn;
          return change;
        }
      }
    }

    if (change.changed_visible_line_count <= 1U && change.appended_visible_character_count > 0U &&
        change.appended_visible_character_count <= 4U) {
      for (std::size_t index = 0; index < compared_line_count; ++index) {
        if (HasOnlyTrivialTailGrowth(before.visible_lines[index], after.visible_lines[index])) {
          change.kind = TerminalSemanticChangeKind::CosmeticChurn;
          return change;
        }
      }
    }

    if (change.changed_visible_line_count > 0U) {
      change.kind = TerminalSemanticChangeKind::MeaningfulOutput;
    }

    return change;
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
    if (snapshot_.render_revision > 0) {
      vibe::base::DebugTrace("core.terminal", "screen.bootstrap",
                             SummarizeBootstrapColors(snapshot_.bootstrap_ansi));
    }
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

  [[nodiscard]] auto SummarizeVisibleRowStyle(const std::size_t row_index) const -> std::string {
    if (row_index >= terminal_size_.rows) {
      return "row=out-of-range";
    }

    std::string text;
    bool saw_non_default_bg = false;
    bool saw_non_default_fg = false;
    std::set<std::string> bg_colors;
    std::set<std::string> fg_colors;

    for (std::size_t column = 0; column < terminal_size_.columns; ++column) {
      VTermScreenCell cell{};
      static_cast<void>(vterm_screen_get_cell(
          screen_, VTermPos{.row = static_cast<int>(row_index), .col = static_cast<int>(column)}, &cell));
      if (cell.width == 0) {
        continue;
      }

      bool emitted = false;
      for (const uint32_t codepoint : cell.chars) {
        if (codepoint == 0U) {
          break;
        }
        AppendUtf8(text, codepoint);
        emitted = true;
      }
      if (!emitted) {
        text.push_back(' ');
      }

      const std::string fg = DescribeColor(cell.fg, true);
      const std::string bg = DescribeColor(cell.bg, false);
      if (fg != "default") {
        saw_non_default_fg = true;
        fg_colors.insert(fg);
      }
      if (bg != "default") {
        saw_non_default_bg = true;
        bg_colors.insert(bg);
      }
    }

    while (!text.empty() && text.back() == ' ') {
      text.pop_back();
    }

    std::ostringstream trace;
    trace << "row=" << row_index << " text=\"" << text << "\""
          << " nonDefaultFg=" << (saw_non_default_fg ? "true" : "false")
          << " nonDefaultBg=" << (saw_non_default_bg ? "true" : "false");
    if (!fg_colors.empty()) {
      trace << " fg={";
      bool first = true;
      for (const auto& fg : fg_colors) {
        if (!first) {
          trace << ",";
        }
        first = false;
        trace << fg;
      }
      trace << "}";
    }
    if (!bg_colors.empty()) {
      trace << " bg={";
      bool first = true;
      for (const auto& bg : bg_colors) {
        if (!first) {
          trace << ",";
        }
        first = false;
        trace << bg;
      }
      trace << "}";
    }
    return trace.str();
  }

  void TraceRowsAroundCursor(const std::string_view event) const {
    if (terminal_size_.rows == 0) {
      return;
    }

    const std::size_t last_row = static_cast<std::size_t>(terminal_size_.rows - 1U);
    const std::size_t cursor_row = std::min(snapshot_.cursor_row, last_row);
    const std::size_t start = cursor_row > 0 ? cursor_row - 1U : 0U;
    const std::size_t end = std::min(last_row, cursor_row + 1U);
    for (std::size_t row = start; row <= end; ++row) {
      vibe::base::DebugTrace("core.terminal", event, SummarizeVisibleRowStyle(row));
    }
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

    out.append("\x1b[0m\x1b[2J\x1b[H");
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
    TraceRowsAroundCursor("screen.rows");
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

    out.append("\x1b[0m\x1b[2J\x1b[H");
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
    TraceRowsAroundCursor("viewport.rows");
    vibe::base::DebugTrace("core.terminal", "viewport.bootstrap",
                           SummarizeBootstrapColors(out));
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
    std::ostringstream trace;
    trace << "scrollbackBefore=" << self->scrollback_lines_.size()
          << " renderRevision=" << self->render_revision_
          << " terminalRows=" << self->terminal_size_.rows
          << " terminalCols=" << self->terminal_size_.columns;
    vibe::base::DebugTrace("core.terminal", "scrollback.clear", trace.str());
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
  TerminalSemanticChange last_semantic_change_{};
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

auto TerminalMultiplexer::last_semantic_change() const -> TerminalSemanticChange {
  return impl_->last_semantic_change();
}

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

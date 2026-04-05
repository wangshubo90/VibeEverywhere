#include <gtest/gtest.h>

#include "vibe/session/terminal_multiplexer.h"

namespace vibe::session {
namespace {

TEST(TerminalMultiplexerTest, TracksVisibleScreenAndScrollback) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 8, .rows = 3}, 4);

  multiplexer.Append("one\r\ntwo\r\nthree\r\nfour");

  const TerminalScreenSnapshot snapshot = multiplexer.snapshot();
  EXPECT_EQ(snapshot.columns, 8);
  EXPECT_EQ(snapshot.rows, 3);
  EXPECT_EQ(snapshot.visible_lines, (std::vector<std::string>{"two", "three", "four"}));
  EXPECT_EQ(snapshot.scrollback_lines, (std::vector<std::string>{"one"}));
}

TEST(TerminalMultiplexerTest, SupportsBasicCursorAndEraseSequences) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 12, .rows = 2}, 4);

  multiplexer.Append("hello");
  multiplexer.Append("\r\x1b[2Kworld");

  const TerminalScreenSnapshot snapshot = multiplexer.snapshot();
  ASSERT_EQ(snapshot.visible_lines.size(), 2U);
  EXPECT_EQ(snapshot.visible_lines[0], "world");
  EXPECT_EQ(snapshot.cursor_row, 0U);
  EXPECT_EQ(snapshot.cursor_column, 5U);
}

TEST(TerminalMultiplexerTest, PreservesUtf8BoxDrawingCharacters) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 8, .rows = 2}, 4);

  multiplexer.Append("╭──╮\r\n│ok│");

  const TerminalScreenSnapshot snapshot = multiplexer.snapshot();
  ASSERT_EQ(snapshot.visible_lines.size(), 2U);
  EXPECT_EQ(snapshot.visible_lines[0], "╭──╮");
  EXPECT_EQ(snapshot.visible_lines[1], "│ok│");
}

TEST(TerminalMultiplexerTest, EmitsStyledBootstrapAnsiForVisibleScreen) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 8, .rows = 2}, 4);

  multiplexer.Append("\x1b[31mred\x1b[0m");

  const TerminalScreenSnapshot snapshot = multiplexer.snapshot();
  EXPECT_NE(snapshot.bootstrap_ansi.find("\x1b[2J\x1b[H"), std::string::npos);
  EXPECT_NE(snapshot.bootstrap_ansi.find("red"), std::string::npos);
  EXPECT_NE(snapshot.bootstrap_ansi.find("38;2;"), std::string::npos);
}

TEST(TerminalMultiplexerTest, ResizePreservesBottomVisibleRegionAndIncrementsRevision) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 10, .rows = 4}, 10);
  multiplexer.Append("l1\r\nl2\r\nl3\r\nl4");

  const std::uint64_t before_revision = multiplexer.snapshot().render_revision;
  multiplexer.Resize(TerminalSize{.columns = 6, .rows = 2});

  const TerminalScreenSnapshot snapshot = multiplexer.snapshot();
  EXPECT_GT(snapshot.render_revision, before_revision);
  EXPECT_EQ(snapshot.rows, 2);
  EXPECT_EQ(snapshot.visible_lines, (std::vector<std::string>{"l3", "l4"}));
  EXPECT_EQ(snapshot.scrollback_lines, (std::vector<std::string>{"l1", "l2"}));
}

TEST(TerminalMultiplexerTest, ViewportTracksCursorAndClipsHorizontallyWithoutResizingPty) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 10, .rows = 6}, 10);
  multiplexer.Append("0123456789\r\n");
  multiplexer.Append("line-2\r\n");
  multiplexer.Append("line-3\r\n");
  multiplexer.Append("line-4\r\n");
  multiplexer.Append("line-5\r\n");
  multiplexer.Append("line-6");

  multiplexer.UpdateViewport("observer-1", TerminalSize{.columns = 4, .rows = 3});

  const auto viewport = multiplexer.viewport_snapshot("observer-1");
  ASSERT_TRUE(viewport.has_value());
  EXPECT_EQ(multiplexer.terminal_size(), (TerminalSize{.columns = 10, .rows = 6}));
  EXPECT_EQ(viewport->columns, 4);
  EXPECT_EQ(viewport->rows, 3);
  EXPECT_EQ(viewport->viewport_top_line, 3U);
  EXPECT_EQ(viewport->horizontal_offset, 3U);
  EXPECT_EQ(viewport->visible_lines, (std::vector<std::string>{"e-4", "e-5", "e-6"}));
  ASSERT_TRUE(viewport->cursor_viewport_row.has_value());
  EXPECT_EQ(*viewport->cursor_viewport_row, 2U);
}

TEST(TerminalMultiplexerTest, ViewportFollowsCursorColumnWhenNarrowerThanPty) {
  TerminalMultiplexer multiplexer(TerminalSize{.columns = 12, .rows = 2}, 10);
  multiplexer.Append("abcdefghijkl");
  multiplexer.UpdateViewport("observer-1", TerminalSize{.columns = 4, .rows = 2});

  const auto viewport = multiplexer.viewport_snapshot("observer-1");
  ASSERT_TRUE(viewport.has_value());
  EXPECT_EQ(viewport->horizontal_offset, 8U);
  EXPECT_EQ(viewport->visible_lines.front(), "ijkl");
  ASSERT_TRUE(viewport->cursor_viewport_column.has_value());
  EXPECT_EQ(*viewport->cursor_viewport_column, 3U);
}

}  // namespace
}  // namespace vibe::session

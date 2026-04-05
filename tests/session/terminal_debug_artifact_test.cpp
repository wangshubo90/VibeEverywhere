#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "vibe/session/terminal_debug_artifact.h"

namespace vibe::session {
namespace {

auto ReadFile(const std::filesystem::path& path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

TEST(TerminalDebugArtifactTest, SerializesScreenSnapshotToJson) {
  const std::string json = ToDebugJson(TerminalScreenSnapshot{
      .columns = 80,
      .rows = 24,
      .render_revision = 7,
      .cursor_row = 3,
      .cursor_column = 14,
      .visible_lines = {"abc", "def"},
      .scrollback_lines = {"one"},
      .bootstrap_ansi = "\x1b[2J\x1b[Habc",
  });

  EXPECT_NE(json.find("\"ptyCols\":80"), std::string::npos);
  EXPECT_NE(json.find("\"ptyRows\":24"), std::string::npos);
  EXPECT_NE(json.find("\"renderRevision\":7"), std::string::npos);
  EXPECT_NE(json.find("\"cursorRow\":3"), std::string::npos);
  EXPECT_NE(json.find("\"cursorColumn\":14"), std::string::npos);
  EXPECT_NE(json.find("\"visibleLines\":[\"abc\",\"def\"]"), std::string::npos);
  EXPECT_NE(json.find("\"scrollbackLines\":[\"one\"]"), std::string::npos);
  EXPECT_NE(json.find("\"bootstrapAnsi\":\"\\u001b[2J\\u001b[Habc\""), std::string::npos);
}

TEST(TerminalDebugArtifactTest, SerializesViewportSnapshotToJson) {
  const std::string json = ToDebugJson(TerminalViewportSnapshot{
      .view_id = "observer-1",
      .columns = 40,
      .rows = 12,
      .render_revision = 9,
      .total_line_count = 18,
      .viewport_top_line = 6,
      .horizontal_offset = 3,
      .cursor_viewport_row = 5,
      .cursor_viewport_column = 7,
      .visible_lines = {"abc", "def"},
      .bootstrap_ansi = "\x1b[2J\x1b[Habc",
  });

  EXPECT_NE(json.find("\"viewId\":\"observer-1\""), std::string::npos);
  EXPECT_NE(json.find("\"cols\":40"), std::string::npos);
  EXPECT_NE(json.find("\"rows\":12"), std::string::npos);
  EXPECT_NE(json.find("\"renderRevision\":9"), std::string::npos);
  EXPECT_NE(json.find("\"viewportTopLine\":6"), std::string::npos);
  EXPECT_NE(json.find("\"horizontalOffset\":3"), std::string::npos);
  EXPECT_NE(json.find("\"cursorRow\":5"), std::string::npos);
  EXPECT_NE(json.find("\"cursorColumn\":7"), std::string::npos);
  EXPECT_NE(json.find("\"bootstrapAnsi\":\"\\u001b[2J\\u001b[Habc\""), std::string::npos);
}

TEST(TerminalDebugArtifactTest, RecorderWritesArtifactsWhenTraceDirConfigured) {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "sentrits-terminal-debug-artifact-test";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);
  ASSERT_EQ(::setenv("VIBE_TERMINAL_TRACE_DIR", temp_root.c_str(), 1), 0);

  TerminalDebugRecorder recorder("session:one");
  ASSERT_TRUE(recorder.enabled());

  recorder.RecordPtyOutput("abc\x1b[31m", 12, TerminalScreenSnapshot{
                                               .columns = 80,
                                               .rows = 24,
                                           .render_revision = 5,
                                           .cursor_row = 0,
                                           .cursor_column = 3,
                                           .visible_lines = {"abc"},
                                           .scrollback_lines = {},
                                           .bootstrap_ansi = "\x1b[2J\x1b[Habc",
                                       });
  recorder.RecordViewport("observer/1", TerminalViewportSnapshot{
                                            .view_id = "observer/1",
                                            .columns = 10,
                                            .rows = 4,
                                            .render_revision = 5,
                                            .total_line_count = 4,
                                            .viewport_top_line = 0,
                                            .horizontal_offset = 0,
                                            .cursor_viewport_row = 0,
                                            .cursor_viewport_column = 3,
                                            .visible_lines = {"abc"},
                                            .bootstrap_ansi = "\x1b[2J\x1b[Habc",
                                        });

  const auto session_dir = temp_root / "session_one";
  EXPECT_TRUE(std::filesystem::exists(session_dir / "12-raw.bin"));
  EXPECT_TRUE(std::filesystem::exists(session_dir / "12-screen.json"));
  EXPECT_TRUE(std::filesystem::exists(session_dir / "observer_1-5-viewport.json"));
  EXPECT_EQ(ReadFile(session_dir / "12-raw.bin"), "abc\x1b[31m");

  std::filesystem::remove_all(temp_root);
  ASSERT_EQ(::unsetenv("VIBE_TERMINAL_TRACE_DIR"), 0);
}

}  // namespace
}  // namespace vibe::session

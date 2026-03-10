#include "vibe/net/json.h"

#include <boost/json.hpp>

namespace vibe::net {

namespace json = boost::json;

auto JsonEscape(const std::string_view input) -> std::string {
  return json::serialize(json::value(input)).substr(1, json::serialize(json::value(input)).size() - 2);
}

auto ToJson(const vibe::service::SessionSummary& summary) -> std::string {
  json::object object;
  object["sessionId"] = summary.id.value();
  object["provider"] = std::string(vibe::session::ToString(summary.provider));
  object["workspaceRoot"] = summary.workspace_root;
  object["title"] = summary.title;
  object["status"] = std::string(vibe::session::ToString(summary.status));
  if (summary.controller_client_id.has_value()) {
    object["controllerClientId"] = *summary.controller_client_id;
  }
  object["controllerKind"] = std::string(vibe::session::ToString(summary.controller_kind));
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::service::SessionSummary>& summaries) -> std::string {
  json::array array;
  for (const auto& summary : summaries) {
    array.emplace_back(json::parse(ToJson(summary)));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::session::SessionSnapshot& snapshot) -> std::string {
  json::object git;
  git["branch"] = snapshot.git_summary.branch;
  git["modifiedFiles"] = json::value_from(snapshot.git_summary.modified_files);
  git["stagedFiles"] = json::value_from(snapshot.git_summary.staged_files);
  git["untrackedFiles"] = json::value_from(snapshot.git_summary.untracked_files);

  json::object object;
  object["sessionId"] = snapshot.metadata.id.value();
  object["provider"] = std::string(vibe::session::ToString(snapshot.metadata.provider));
  object["workspaceRoot"] = snapshot.metadata.workspace_root;
  object["title"] = snapshot.metadata.title;
  object["status"] = std::string(vibe::session::ToString(snapshot.metadata.status));
  object["currentSequence"] = snapshot.current_sequence;
  object["recentTerminalTail"] = snapshot.recent_terminal_tail;
  object["recentFileChanges"] = json::value_from(snapshot.recent_file_changes);
  object["git"] = std::move(git);
  return json::serialize(object);
}

auto ToJson(const vibe::session::OutputSlice& slice) -> std::string {
  json::object object;
  object["seqStart"] = slice.seq_start;
  object["seqEnd"] = slice.seq_end;
  object["data"] = slice.data;
  return json::serialize(object);
}

auto ToJson(const TerminalOutputEvent& event) -> std::string {
  json::object object;
  object["type"] = "terminal.output";
  object["sessionId"] = event.session_id;
  object["seqStart"] = event.slice.seq_start;
  object["seqEnd"] = event.slice.seq_end;
  object["data"] = event.slice.data;
  return json::serialize(object);
}

auto ToJson(const SessionUpdatedEvent& event) -> std::string {
  json::object object;
  object["type"] = "session.updated";
  object["sessionId"] = event.summary.id.value();
  object["provider"] = std::string(vibe::session::ToString(event.summary.provider));
  object["workspaceRoot"] = event.summary.workspace_root;
  object["title"] = event.summary.title;
  object["status"] = std::string(vibe::session::ToString(event.summary.status));
  if (event.summary.controller_client_id.has_value()) {
    object["controllerClientId"] = *event.summary.controller_client_id;
  }
  object["controllerKind"] = std::string(vibe::session::ToString(event.summary.controller_kind));
  return json::serialize(object);
}

auto ToJson(const SessionExitedEvent& event) -> std::string {
  json::object object;
  object["type"] = "session.exited";
  object["sessionId"] = event.session_id;
  object["status"] = std::string(vibe::session::ToString(event.status));
  return json::serialize(object);
}

auto ToJson(const ErrorEvent& event) -> std::string {
  json::object object;
  object["type"] = "error";
  if (!event.session_id.empty()) {
    object["sessionId"] = event.session_id;
  }
  object["code"] = event.code;
  object["message"] = event.message;
  return json::serialize(object);
}

auto ToJsonHostInfo() -> std::string {
  json::object object;
  object["hostId"] = "local-dev-host";
  object["displayName"] = "VibeEverywhere Dev Host";
  object["version"] = "0.1.0";
  object["capabilities"] = {"sessions", "rest", "websocket"};
  return json::serialize(object);
}

}  // namespace vibe::net

#include "vibe/net/json.h"

#include <algorithm>
#include <boost/json.hpp>

#include "vibe/session/env_config.h"

namespace vibe::net {

namespace json = boost::json;

namespace {

#ifndef SENTRITS_VERSION
#define SENTRITS_VERSION "0.0.0"
#endif

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

auto ToString(const vibe::auth::DeviceType device_type) -> const char* {
  switch (device_type) {
    case vibe::auth::DeviceType::Mobile:
      return "mobile";
    case vibe::auth::DeviceType::Desktop:
      return "desktop";
    case vibe::auth::DeviceType::Browser:
      return "browser";
    case vibe::auth::DeviceType::Unknown:
    default:
      return "unknown";
  }
}

auto ToActivityState(const vibe::service::SessionSummary& summary) -> const char* {
  return vibe::session::ToString(summary.supervision_state).data();
}

auto ToAttentionState(const vibe::service::SessionSummary& summary) -> const char* {
  return vibe::session::ToString(summary.attention_state).data();
}

auto ToAttentionReason(const vibe::service::SessionSummary& summary) -> const char* {
  return vibe::session::ToString(summary.attention_reason).data();
}

auto MakeTerminalSemanticChangeJson(const vibe::session::TerminalSemanticChange& change) -> json::object {
  json::object object;
  object["kind"] = std::string(vibe::session::ToString(change.kind));
  object["changedVisibleLineCount"] = change.changed_visible_line_count;
  object["scrollbackLinesAdded"] = change.scrollback_lines_added;
  object["appendedVisibleCharacterCount"] = change.appended_visible_character_count;
  object["cursorMoved"] = change.cursor_moved;
  object["altScreenEntered"] = change.alt_screen_entered;
  object["altScreenExited"] = change.alt_screen_exited;
  return object;
}

auto MakeSessionModeJson(const vibe::session::SessionModeSummary& mode) -> json::object {
  json::object object;
  object["lifecycle"] = std::string(vibe::session::ToString(mode.lifecycle_status));
  object["interaction"] = std::string(vibe::session::ToString(mode.interaction_kind));
  object["activity"] = std::string(vibe::session::ToString(mode.activity_state));
  return object;
}

auto MakeSessionAttentionJson(const vibe::session::SessionAttentionSummary& attention) -> json::object {
  json::object object;
  object["level"] = std::string(vibe::session::ToString(attention.level));
  object["cause"] = std::string(vibe::session::ToString(attention.cause));
  if (attention.since_unix_ms.has_value()) {
    object["sinceUnixMs"] = *attention.since_unix_ms;
  }
  object["summary"] = attention.summary;
  return object;
}

auto ToInventoryState(const vibe::service::SessionSummary& summary) -> const char* {
  if (summary.is_active) {
    return "live";
  }
  if (summary.is_recovered) {
    return "archived";
  }
  return "ended";
}

auto MakeNodeSummaryJson(const vibe::session::SessionNodeSummary& summary) -> json::object {
  json::object object;
  object["sessionId"] = summary.session_id;
  object["lifecycleStatus"] = std::string(vibe::session::ToString(summary.lifecycle_status));
  object["interactionKind"] = std::string(vibe::session::ToString(summary.interaction_kind));
  object["attentionState"] = std::string(vibe::session::ToString(summary.attention_state));
  object["semanticPreview"] = summary.semantic_preview;
  object["recentFileChangeCount"] = summary.recent_file_change_count;
  object["gitDirty"] = summary.git_dirty;
  if (summary.last_activity_at_unix_ms.has_value()) {
    object["lastActivityAtUnixMs"] = *summary.last_activity_at_unix_ms;
  }
  return object;
}

auto MakeEvidenceSourceRefJson(const vibe::service::EvidenceSourceRef& source) -> json::object {
  json::object object;
  object["kind"] = std::string(vibe::service::ToString(source.kind));
  object["sessionId"] = source.session_id.value();
  return object;
}

auto MakeEvidenceEntryJson(const vibe::service::EvidenceEntry& entry) -> json::object {
  json::object object;
  object["entryId"] = entry.entry_id;
  object["source"] = MakeEvidenceSourceRefJson(entry.source);
  object["revision"] = entry.revision;
  object["byteStart"] = entry.byte_start;
  object["byteEnd"] = entry.byte_end;
  object["timestampUnixMs"] = entry.timestamp_unix_ms;
  object["stream"] = std::string(vibe::service::ToString(entry.stream));
  object["text"] = entry.text;
  object["partial"] = entry.partial;
  return object;
}

auto MakeEvidenceHighlightJson(const vibe::service::EvidenceHighlight& highlight) -> json::object {
  json::object object;
  object["entryId"] = highlight.entry_id;
  object["start"] = highlight.start;
  object["length"] = highlight.length;
  object["kind"] = std::string(vibe::service::ToString(highlight.kind));
  return object;
}

auto MakeEvidenceResultJson(const vibe::service::EvidenceResult& result) -> json::object {
  json::object object;
  object["source"] = MakeEvidenceSourceRefJson(result.source);
  object["operation"] = std::string(vibe::service::ToString(result.operation));
  object["query"] = result.query;
  object["revisionStart"] = result.revision_start;
  object["revisionEnd"] = result.revision_end;
  object["oldestRevision"] = result.oldest_revision;
  object["latestRevision"] = result.latest_revision;
  json::array entries;
  for (const auto& entry : result.entries) {
    entries.emplace_back(MakeEvidenceEntryJson(entry));
  }
  object["entries"] = std::move(entries);
  json::array highlights;
  for (const auto& highlight : result.highlights) {
    highlights.emplace_back(MakeEvidenceHighlightJson(highlight));
  }
  object["highlights"] = std::move(highlights);
  object["truncated"] = result.truncated;
  object["bufferExhausted"] = result.buffer_exhausted;
  object["droppedEntries"] = result.dropped_entries;
  object["droppedBytes"] = result.dropped_bytes;
  if (!result.error_code.empty()) {
    object["errorCode"] = result.error_code;
  }
  object["replayToken"] = result.replay_token;
  return object;
}

auto MakeObservationEventJson(const vibe::service::ObservationEvent& event) -> json::object {
  json::object object;
  object["id"] = event.id;
  object["timestampUnixMs"] = event.timestamp_unix_ms;
  object["actorSessionId"] = event.actor_session_id;
  object["actorTitle"] = event.actor_title;
  object["actorId"] = event.actor_id;
  if (event.pid != 0) {
    object["pid"] = event.pid;
  }
  if (event.uid != 0) {
    object["uid"] = event.uid;
  }
  if (event.gid != 0) {
    object["gid"] = event.gid;
  }
  object["operation"] = std::string(vibe::service::ToString(event.operation));
  object["source"] = MakeEvidenceSourceRefJson(event.source);
  object["sourceTitle"] = event.source_title;
  object["query"] = event.query;
  object["revisionStart"] = event.revision_start;
  object["revisionEnd"] = event.revision_end;
  object["resultCount"] = event.result_count;
  object["truncated"] = event.truncated;
  object["summary"] = event.summary;
  object["replayToken"] = event.replay_token;
  return object;
}

}  // namespace

auto JsonEscape(const std::string_view input) -> std::string {
  return json::serialize(json::value(input)).substr(1, json::serialize(json::value(input)).size() - 2);
}

auto Base64Encode(const std::string_view input) -> std::string {
  std::string encoded;
  encoded.reserve(((input.size() + 2U) / 3U) * 4U);

  std::size_t index = 0;
  while (index + 3U <= input.size()) {
    const auto a = static_cast<unsigned char>(input[index]);
    const auto b = static_cast<unsigned char>(input[index + 1U]);
    const auto c = static_cast<unsigned char>(input[index + 2U]);
    encoded.push_back(kBase64Alphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[((a & 0x03U) << 4U) | ((b >> 4U) & 0x0FU)]);
    encoded.push_back(kBase64Alphabet[((b & 0x0FU) << 2U) | ((c >> 6U) & 0x03U)]);
    encoded.push_back(kBase64Alphabet[c & 0x3FU]);
    index += 3U;
  }

  const std::size_t remaining = input.size() - index;
  if (remaining == 1U) {
    const auto a = static_cast<unsigned char>(input[index]);
    encoded.push_back(kBase64Alphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(a & 0x03U) << 4U]);
    encoded.append("==");
  } else if (remaining == 2U) {
    const auto a = static_cast<unsigned char>(input[index]);
    const auto b = static_cast<unsigned char>(input[index + 1U]);
    encoded.push_back(kBase64Alphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[((a & 0x03U) << 4U) | ((b >> 4U) & 0x0FU)]);
    encoded.push_back(kBase64Alphabet[(b & 0x0FU) << 2U]);
    encoded.push_back('=');
  }

  return encoded;
}

auto ToJson(const vibe::service::SessionSummary& summary) -> std::string {
  json::object object;
  object["sessionId"] = summary.id.value();
  object["sessionCategory"] = std::string(vibe::service::ToString(summary.category));
  object["provider"] = std::string(vibe::session::ToString(summary.provider));
  object["workspaceRoot"] = summary.workspace_root;
  object["title"] = summary.title;
  object["status"] = std::string(vibe::session::ToString(summary.status));
  if (summary.conversation_id.has_value()) {
    object["conversationId"] = *summary.conversation_id;
  }
  object["groupTags"] = json::value_from(summary.group_tags);
  if (summary.controller_client_id.has_value()) {
    object["controllerClientId"] = *summary.controller_client_id;
  }
  object["controllerKind"] = std::string(vibe::session::ToString(summary.controller_kind));
  object["isRecovered"] = summary.is_recovered;
  object["archivedRecord"] = summary.is_recovered;
  object["isActive"] = summary.is_active;
  object["inventoryState"] = ToInventoryState(summary);
  object["activityState"] = ToActivityState(summary);
  object["supervisionState"] = std::string(vibe::session::ToString(summary.supervision_state));
  object["attentionState"] = ToAttentionState(summary);
  object["attentionReason"] = ToAttentionReason(summary);
  if (summary.created_at_unix_ms.has_value()) {
    object["createdAtUnixMs"] = *summary.created_at_unix_ms;
  }
  if (summary.last_status_at_unix_ms.has_value()) {
    object["lastStatusAtUnixMs"] = *summary.last_status_at_unix_ms;
  }
  if (summary.last_output_at_unix_ms.has_value()) {
    object["lastOutputAtUnixMs"] = *summary.last_output_at_unix_ms;
  }
  if (summary.last_activity_at_unix_ms.has_value()) {
    object["lastActivityAtUnixMs"] = *summary.last_activity_at_unix_ms;
  }
  if (summary.last_file_change_at_unix_ms.has_value()) {
    object["lastFileChangeAtUnixMs"] = *summary.last_file_change_at_unix_ms;
  }
  if (summary.last_git_change_at_unix_ms.has_value()) {
    object["lastGitChangeAtUnixMs"] = *summary.last_git_change_at_unix_ms;
  }
  if (summary.last_controller_change_at_unix_ms.has_value()) {
    object["lastControllerChangeAtUnixMs"] = *summary.last_controller_change_at_unix_ms;
  }
  if (summary.attention_since_unix_ms.has_value()) {
    object["attentionSinceUnixMs"] = *summary.attention_since_unix_ms;
  }
  if (summary.pty_columns.has_value()) {
    object["ptyCols"] = *summary.pty_columns;
  }
  if (summary.pty_rows.has_value()) {
    object["ptyRows"] = *summary.pty_rows;
  }
  object["currentSequence"] = summary.current_sequence;
  object["attachedClientCount"] = summary.attached_client_count;
  object["recentFileChangeCount"] = summary.recent_file_change_count;
  object["interactionKind"] = std::string(vibe::session::ToString(summary.interaction_kind));
  object["semanticPreview"] = summary.semantic_preview;
  object["mode"] = MakeSessionModeJson(summary.mode);
  object["attention"] = MakeSessionAttentionJson(summary.attention);
  object["gitDirty"] = summary.git_dirty;
  object["gitBranch"] = summary.git_branch;
  object["gitModifiedCount"] = summary.git_modified_count;
  object["gitStagedCount"] = summary.git_staged_count;
  object["gitUntrackedCount"] = summary.git_untracked_count;
  object["nodeSummary"] = MakeNodeSummaryJson(summary.node_summary);
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::service::SessionSummary>& summaries) -> std::string {
  json::array array;
  for (const auto& summary : summaries) {
    array.emplace_back(json::parse(ToJson(summary)));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::service::SessionFileReadResult& file) -> std::string {
  json::object object;
  object["workspacePath"] = file.workspace_path;
  object["contentBase64"] = Base64Encode(file.content);
  object["contentEncoding"] = "base64";
  object["sizeBytes"] = file.size_bytes;
  object["truncated"] = file.truncated;
  return json::serialize(object);
}

auto ToJson(const vibe::session::SessionSnapshot& snapshot) -> std::string {
  json::object git;
  git["branch"] = snapshot.git_summary.branch;
  git["modifiedCount"] = snapshot.git_summary.modified_count;
  git["stagedCount"] = snapshot.git_summary.staged_count;
  git["untrackedCount"] = snapshot.git_summary.untracked_count;
  git["modifiedFiles"] = json::value_from(snapshot.git_summary.modified_files);
  git["stagedFiles"] = json::value_from(snapshot.git_summary.staged_files);
  git["untrackedFiles"] = json::value_from(snapshot.git_summary.untracked_files);

  json::object object;
  object["sessionId"] = snapshot.metadata.id.value();
  object["provider"] = std::string(vibe::session::ToString(snapshot.metadata.provider));
  object["workspaceRoot"] = snapshot.metadata.workspace_root;
  object["title"] = snapshot.metadata.title;
  object["status"] = std::string(vibe::session::ToString(snapshot.metadata.status));
  if (snapshot.metadata.conversation_id.has_value()) {
    object["conversationId"] = *snapshot.metadata.conversation_id;
  }
  object["groupTags"] = json::value_from(snapshot.metadata.group_tags);
  object["currentSequence"] = snapshot.current_sequence;
  object["recentTerminalTail"] = snapshot.recent_terminal_tail;
  object["interactionKind"] = std::string(vibe::session::ToString(snapshot.node_summary.interaction_kind));
  object["semanticPreview"] = snapshot.node_summary.semantic_preview;
  object["mode"] = MakeSessionModeJson(snapshot.signals.mode);
  object["attention"] = MakeSessionAttentionJson(snapshot.signals.attention);
  object["nodeSummary"] = MakeNodeSummaryJson(snapshot.node_summary);
  if (snapshot.terminal_screen.has_value()) {
    json::object terminal_screen;
    terminal_screen["ptyCols"] = snapshot.terminal_screen->columns;
    terminal_screen["ptyRows"] = snapshot.terminal_screen->rows;
    terminal_screen["renderRevision"] = snapshot.terminal_screen->render_revision;
    terminal_screen["cursorRow"] = snapshot.terminal_screen->cursor_row;
    terminal_screen["cursorColumn"] = snapshot.terminal_screen->cursor_column;
    terminal_screen["visibleLines"] = json::value_from(snapshot.terminal_screen->visible_lines);
    terminal_screen["scrollbackLines"] = json::value_from(snapshot.terminal_screen->scrollback_lines);
    terminal_screen["bootstrapAnsi"] = snapshot.terminal_screen->bootstrap_ansi;
    object["terminalScreen"] = std::move(terminal_screen);
  }
  object["recentFileChanges"] = json::value_from(snapshot.recent_file_changes);
  json::object signals;
  if (snapshot.signals.last_output_at_unix_ms.has_value()) {
    signals["lastOutputAtUnixMs"] = *snapshot.signals.last_output_at_unix_ms;
  }
  if (snapshot.signals.last_raw_output_at_unix_ms.has_value()) {
    signals["lastRawOutputAtUnixMs"] = *snapshot.signals.last_raw_output_at_unix_ms;
  }
  if (snapshot.signals.last_meaningful_output_at_unix_ms.has_value()) {
    signals["lastMeaningfulOutputAtUnixMs"] = *snapshot.signals.last_meaningful_output_at_unix_ms;
  }
  if (snapshot.signals.last_activity_at_unix_ms.has_value()) {
    signals["lastActivityAtUnixMs"] = *snapshot.signals.last_activity_at_unix_ms;
  }
  if (snapshot.signals.last_file_change_at_unix_ms.has_value()) {
    signals["lastFileChangeAtUnixMs"] = *snapshot.signals.last_file_change_at_unix_ms;
  }
  if (snapshot.signals.last_git_change_at_unix_ms.has_value()) {
    signals["lastGitChangeAtUnixMs"] = *snapshot.signals.last_git_change_at_unix_ms;
  }
  if (snapshot.signals.last_controller_change_at_unix_ms.has_value()) {
    signals["lastControllerChangeAtUnixMs"] = *snapshot.signals.last_controller_change_at_unix_ms;
  }
  if (snapshot.signals.attention_since_unix_ms.has_value()) {
    signals["attentionSinceUnixMs"] = *snapshot.signals.attention_since_unix_ms;
  }
  if (snapshot.signals.pty_columns.has_value()) {
    signals["ptyCols"] = *snapshot.signals.pty_columns;
  }
  if (snapshot.signals.pty_rows.has_value()) {
    signals["ptyRows"] = *snapshot.signals.pty_rows;
  }
  signals["currentSequence"] = snapshot.signals.current_sequence;
  signals["recentFileChangeCount"] = snapshot.signals.recent_file_change_count;
  signals["supervisionState"] = std::string(vibe::session::ToString(snapshot.signals.supervision_state));
  signals["attentionState"] = std::string(vibe::session::ToString(snapshot.signals.attention_state));
  signals["attentionReason"] = std::string(vibe::session::ToString(snapshot.signals.attention_reason));
  signals["interactionKind"] = std::string(vibe::session::ToString(snapshot.signals.interaction_kind));
  signals["terminalSemanticChange"] = MakeTerminalSemanticChangeJson(snapshot.signals.terminal_semantic_change);
  signals["gitDirty"] = snapshot.signals.git_dirty;
  signals["gitBranch"] = snapshot.signals.git_branch;
  signals["gitModifiedCount"] = snapshot.signals.git_modified_count;
  signals["gitStagedCount"] = snapshot.signals.git_staged_count;
  signals["gitUntrackedCount"] = snapshot.signals.git_untracked_count;
  signals["mode"] = MakeSessionModeJson(snapshot.signals.mode);
  signals["attention"] = MakeSessionAttentionJson(snapshot.signals.attention);
  object["signals"] = std::move(signals);
  object["git"] = std::move(git);
  return json::serialize(object);
}

auto ToJson(const vibe::session::OutputSlice& slice) -> std::string {
  json::object object;
  object["seqStart"] = slice.seq_start;
  object["seqEnd"] = slice.seq_end;
  object["dataBase64"] = Base64Encode(slice.data);
  object["dataEncoding"] = "base64";
  return json::serialize(object);
}

auto ToJson(const vibe::service::EvidenceEntry& entry) -> std::string {
  return json::serialize(MakeEvidenceEntryJson(entry));
}

auto ToJson(const vibe::service::EvidenceHighlight& highlight) -> std::string {
  return json::serialize(MakeEvidenceHighlightJson(highlight));
}

auto ToJson(const vibe::service::EvidenceResult& result) -> std::string {
  return json::serialize(MakeEvidenceResultJson(result));
}

auto ToJson(const vibe::service::ObservationEvent& event) -> std::string {
  return json::serialize(MakeObservationEventJson(event));
}

auto ToJson(const std::vector<vibe::service::ObservationEvent>& events) -> std::string {
  json::array array;
  for (const auto& event : events) {
    array.emplace_back(MakeObservationEventJson(event));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::auth::PairingRequest& request) -> std::string {
  json::object object;
  object["pairingId"] = request.pairing_id;
  object["deviceName"] = request.device_name;
  object["deviceType"] = ToString(request.device_type);
  object["code"] = request.code;
  object["requestedAtUnixMs"] = request.requested_at_unix_ms;
  object["status"] = "pending";
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::auth::PairingRequest>& requests) -> std::string {
  json::array array;
  for (const auto& request : requests) {
    array.emplace_back(json::parse(ToJson(request)));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::auth::PairingRecord& record) -> std::string {
  json::object object;
  object["deviceId"] = record.device_id.value;
  object["deviceName"] = record.device_name;
  object["deviceType"] = ToString(record.device_type);
  object["token"] = record.bearer_token;
  object["status"] = "approved";
  object["approvedAtUnixMs"] = record.approved_at_unix_ms;
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::auth::PairingRecord>& records) -> std::string {
  json::array array;
  for (const auto& record : records) {
    array.emplace_back(json::parse(ToJson(record)));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::net::AttachedClientInfo& info) -> std::string {
  json::object object;
  object["clientId"] = info.client_id;
  object["sessionId"] = info.session_id;
  object["sessionTitle"] = info.session_title;
  object["sessionStatus"] = std::string(vibe::session::ToString(info.session_status));
  object["sessionIsRecovered"] = info.session_is_recovered;
  object["sessionArchivedRecord"] = info.session_is_recovered;
  object["clientAddress"] = info.client_address;
  object["claimedKind"] = std::string(vibe::session::ToString(info.claimed_kind));
  object["isLocal"] = info.is_local;
  object["hasControl"] = info.has_control;
  if (info.connected_at_unix_ms.has_value()) {
    object["connectedAtUnixMs"] = *info.connected_at_unix_ms;
  }
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::net::AttachedClientInfo>& infos) -> std::string {
  json::array array;
  for (const auto& info : infos) {
    array.emplace_back(json::parse(ToJson(info)));
  }
  return json::serialize(array);
}

auto ToJson(const TerminalOutputEvent& event) -> std::string {
  json::object object;
  object["type"] = "terminal.output";
  object["sessionId"] = event.session_id;
  object["seqStart"] = event.slice.seq_start;
  object["seqEnd"] = event.slice.seq_end;
  object["dataBase64"] = Base64Encode(event.slice.data);
  object["dataEncoding"] = "base64";
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
  if (event.summary.conversation_id.has_value()) {
    object["conversationId"] = *event.summary.conversation_id;
  }
  object["groupTags"] = json::value_from(event.summary.group_tags);
  if (event.summary.controller_client_id.has_value()) {
    object["controllerClientId"] = *event.summary.controller_client_id;
  }
  object["controllerKind"] = std::string(vibe::session::ToString(event.summary.controller_kind));
  object["isRecovered"] = event.summary.is_recovered;
  object["archivedRecord"] = event.summary.is_recovered;
  object["isActive"] = event.summary.is_active;
  object["inventoryState"] = ToInventoryState(event.summary);
  object["activityState"] = ToActivityState(event.summary);
  object["supervisionState"] = std::string(vibe::session::ToString(event.summary.supervision_state));
  object["attentionState"] = ToAttentionState(event.summary);
  object["attentionReason"] = ToAttentionReason(event.summary);
  if (event.summary.created_at_unix_ms.has_value()) {
    object["createdAtUnixMs"] = *event.summary.created_at_unix_ms;
  }
  if (event.summary.last_status_at_unix_ms.has_value()) {
    object["lastStatusAtUnixMs"] = *event.summary.last_status_at_unix_ms;
  }
  if (event.summary.last_output_at_unix_ms.has_value()) {
    object["lastOutputAtUnixMs"] = *event.summary.last_output_at_unix_ms;
  }
  if (event.summary.last_activity_at_unix_ms.has_value()) {
    object["lastActivityAtUnixMs"] = *event.summary.last_activity_at_unix_ms;
  }
  if (event.summary.last_file_change_at_unix_ms.has_value()) {
    object["lastFileChangeAtUnixMs"] = *event.summary.last_file_change_at_unix_ms;
  }
  if (event.summary.last_git_change_at_unix_ms.has_value()) {
    object["lastGitChangeAtUnixMs"] = *event.summary.last_git_change_at_unix_ms;
  }
  if (event.summary.last_controller_change_at_unix_ms.has_value()) {
    object["lastControllerChangeAtUnixMs"] = *event.summary.last_controller_change_at_unix_ms;
  }
  if (event.summary.attention_since_unix_ms.has_value()) {
    object["attentionSinceUnixMs"] = *event.summary.attention_since_unix_ms;
  }
  if (event.summary.pty_columns.has_value()) {
    object["ptyCols"] = *event.summary.pty_columns;
  }
  if (event.summary.pty_rows.has_value()) {
    object["ptyRows"] = *event.summary.pty_rows;
  }
  object["currentSequence"] = event.summary.current_sequence;
  object["attachedClientCount"] = event.summary.attached_client_count;
  object["recentFileChangeCount"] = event.summary.recent_file_change_count;
  object["interactionKind"] = std::string(vibe::session::ToString(event.summary.interaction_kind));
  object["semanticPreview"] = event.summary.semantic_preview;
  object["gitDirty"] = event.summary.git_dirty;
  object["gitBranch"] = event.summary.git_branch;
  object["gitModifiedCount"] = event.summary.git_modified_count;
  object["gitStagedCount"] = event.summary.git_staged_count;
  object["gitUntrackedCount"] = event.summary.git_untracked_count;
  object["nodeSummary"] = MakeNodeSummaryJson(event.summary.node_summary);
  return json::serialize(object);
}

auto ToJson(const SessionExitedEvent& event) -> std::string {
  json::object object;
  object["type"] = "session.exited";
  object["sessionId"] = event.session_id;
  object["status"] = std::string(vibe::session::ToString(event.status));
  return json::serialize(object);
}

auto ToJson(const SessionActivityEvent& event) -> std::string {
  json::object object;
  object["type"] = "session.activity";
  object["sessionId"] = event.summary.id.value();
  object["activityState"] = ToActivityState(event.summary);
  object["groupTags"] = json::value_from(event.summary.group_tags);
  object["isActive"] = event.summary.is_active;
  object["supervisionState"] = std::string(vibe::session::ToString(event.summary.supervision_state));
  object["attentionState"] = ToAttentionState(event.summary);
  object["attentionReason"] = ToAttentionReason(event.summary);
  if (event.summary.last_output_at_unix_ms.has_value()) {
    object["lastOutputAtUnixMs"] = *event.summary.last_output_at_unix_ms;
  }
  if (event.summary.last_activity_at_unix_ms.has_value()) {
    object["lastActivityAtUnixMs"] = *event.summary.last_activity_at_unix_ms;
  }
  if (event.summary.last_file_change_at_unix_ms.has_value()) {
    object["lastFileChangeAtUnixMs"] = *event.summary.last_file_change_at_unix_ms;
  }
  if (event.summary.last_git_change_at_unix_ms.has_value()) {
    object["lastGitChangeAtUnixMs"] = *event.summary.last_git_change_at_unix_ms;
  }
  if (event.summary.last_controller_change_at_unix_ms.has_value()) {
    object["lastControllerChangeAtUnixMs"] = *event.summary.last_controller_change_at_unix_ms;
  }
  if (event.summary.attention_since_unix_ms.has_value()) {
    object["attentionSinceUnixMs"] = *event.summary.attention_since_unix_ms;
  }
  if (event.summary.pty_columns.has_value()) {
    object["ptyCols"] = *event.summary.pty_columns;
  }
  if (event.summary.pty_rows.has_value()) {
    object["ptyRows"] = *event.summary.pty_rows;
  }
  object["currentSequence"] = event.summary.current_sequence;
  object["attachedClientCount"] = event.summary.attached_client_count;
  object["recentFileChangeCount"] = event.summary.recent_file_change_count;
  object["interactionKind"] = std::string(vibe::session::ToString(event.summary.interaction_kind));
  object["semanticPreview"] = event.summary.semantic_preview;
  object["gitDirty"] = event.summary.git_dirty;
  object["nodeSummary"] = MakeNodeSummaryJson(event.summary.node_summary);
  object["gitBranch"] = event.summary.git_branch;
  object["gitModifiedCount"] = event.summary.git_modified_count;
  object["gitStagedCount"] = event.summary.git_staged_count;
  object["gitUntrackedCount"] = event.summary.git_untracked_count;
  return json::serialize(object);
}

auto ToJson(const SessionInventoryEvent& event) -> std::string {
  json::object object;
  object["type"] = "sessions.snapshot";
  json::array sessions;
  for (const auto& summary : event.sessions) {
    sessions.push_back(json::parse(ToJson(summary)));
  }
  object["sessions"] = std::move(sessions);
  return json::serialize(object);
}

auto ToJson(const ObservationCreatedEvent& event) -> std::string {
  json::object object;
  object["type"] = "observation.created";
  object["event"] = MakeObservationEventJson(event.event);
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

auto ToJson(const DiscoveryInfo& info) -> std::string {
  json::object object;
  object["hostId"] = info.host_id;
  object["displayName"] = info.display_name;
  object["remoteHost"] = info.remote_host;
  object["remotePort"] = info.remote_port;
  object["protocolVersion"] = info.protocol_version;
  object["tls"] = info.tls;
  return json::serialize(object);
}

auto ShouldRedactEnvKey(const std::string& key) -> bool {
  // Suffix matches (case-insensitive via upper-casing): *_KEY, *_TOKEN, *_SECRET, *_PASSWORD
  const auto ends_with_ci = [&key](const std::string_view suffix) -> bool {
    if (key.size() < suffix.size()) {
      return false;
    }
    const std::string upper_key = [&key]() {
      std::string s = key;
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      return s;
    }();
    return upper_key.substr(upper_key.size() - suffix.size()) == suffix;
  };
  const auto starts_with_ci = [&key](const std::string_view prefix) -> bool {
    if (key.size() < prefix.size()) {
      return false;
    }
    const std::string upper_key = [&key]() {
      std::string s = key;
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      return s;
    }();
    return upper_key.substr(0, prefix.size()) == prefix;
  };

  if (ends_with_ci("_KEY") || ends_with_ci("_TOKEN") || ends_with_ci("_SECRET") ||
      ends_with_ci("_PASSWORD")) {
    return true;
  }
  if (key == "DATABASE_URL") {
    return true;
  }
  if (starts_with_ci("AUTH_") || starts_with_ci("AWS_")) {
    return true;
  }
  return false;
}

auto ToJson(const vibe::session::EffectiveEnvironment& env, const bool redact) -> std::string {
  json::object object;
  object["mode"] = std::string(vibe::session::ToString(env.mode));
  if (env.bootstrap_shell_path.has_value()) {
    object["bootstrapShellPath"] = *env.bootstrap_shell_path;
  }
  if (env.env_file_path.has_value()) {
    object["envFilePath"] = *env.env_file_path;
  }
  if (env.bootstrap_warning.has_value()) {
    object["bootstrapWarning"] = *env.bootstrap_warning;
  }

  json::array entries_array;
  for (const auto& entry : env.entries) {
    json::object entry_object;
    entry_object["key"] = entry.key;
    const bool should_redact = redact && ShouldRedactEnvKey(entry.key);
    entry_object["value"] = should_redact ? "<redacted>" : entry.value;
    entry_object["source"] = std::string(vibe::session::ToString(entry.source));
    if (should_redact) {
      entry_object["redacted"] = true;
    }
    entries_array.push_back(std::move(entry_object));
  }
  object["entries"] = std::move(entries_array);
  return json::serialize(object);
}

auto ToJson(const vibe::store::LaunchRecord& record) -> std::string {
  json::object object;
  object["recordId"] = record.record_id;
  object["provider"] = std::string(vibe::session::ToString(record.provider));
  object["workspaceRoot"] = record.workspace_root;
  object["title"] = record.title;
  object["launchedAtUnixMs"] = record.launched_at_unix_ms;
  if (record.conversation_id.has_value()) {
    object["conversationId"] = *record.conversation_id;
  }
  object["groupTags"] = json::value_from(record.group_tags);
  if (record.command_argv.has_value()) {
    json::array argv;
    for (const auto& token : *record.command_argv) {
      argv.emplace_back(token);
    }
    object["commandArgv"] = std::move(argv);
  }
  if (record.command_shell.has_value()) {
    object["commandShell"] = *record.command_shell;
  }
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::store::LaunchRecord>& records) -> std::string {
  json::array array;
  for (const auto& record : records) {
    array.emplace_back(json::parse(ToJson(record)));
  }
  return json::serialize(array);
}

auto ToJsonHostInfo() -> std::string {
  return ToJsonHostInfo(std::nullopt, false);
}

auto ToJsonHostInfo(const std::optional<vibe::store::HostIdentity>& host_identity,
                    const bool tls_enabled) -> std::string {
  const vibe::store::HostIdentity resolved_identity =
      host_identity.value_or(vibe::store::MakeDefaultHostIdentity());
  json::object object;
  object["hostId"] = resolved_identity.host_id;
  object["displayName"] = resolved_identity.display_name;
  object["adminHost"] = resolved_identity.admin_host;
  object["adminPort"] = resolved_identity.admin_port;
  object["remoteHost"] = resolved_identity.remote_host;
  object["remotePort"] = resolved_identity.remote_port;
  json::object provider_commands;
  auto append_command = [&provider_commands](std::string_view key,
                                             const vibe::store::ProviderCommandOverride& command) {
    json::array array;
    if (!command.executable.empty()) {
      array.push_back(json::value(command.executable));
      for (const auto& arg : command.args) {
        array.push_back(json::value(arg));
      }
    }
    provider_commands[std::string(key)] = std::move(array);
  };
  append_command("codex", resolved_identity.codex_command);
  append_command("claude", resolved_identity.claude_command);
  object["providerCommands"] = std::move(provider_commands);
  object["launchRecordCount"] = resolved_identity.launch_records.size();
  object["version"] = SENTRITS_VERSION;
  object["capabilities"] = {"sessions", "rest", "websocket", "launchRecords"};
  object["pairingMode"] = "approval";
  json::object tls;
  tls["enabled"] = tls_enabled;
  tls["mode"] = tls_enabled ? "self-signed-planned" : "disabled";
  object["tls"] = std::move(tls);
  return json::serialize(object);
}

}  // namespace vibe::net

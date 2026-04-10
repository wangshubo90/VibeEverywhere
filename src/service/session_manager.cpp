#include "vibe/service/session_manager.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

#include "vibe/service/git_inspector.h"
#include "vibe/service/workspace_file_watcher.h"
#include "vibe/base/debug_trace.h"
#include "vibe/session/provider_config.h"
#include "vibe/session/session_record.h"

namespace vibe::service {

namespace {

constexpr std::string_view kPrivilegedLocalControllerPrefix = "local-controller-";
constexpr std::string_view kPrivilegedRemoteControllerPrefix = "remote-controller-";

auto IsPrivilegedControllerClientId(const std::optional<std::string>& controller_client_id) -> bool {
  return controller_client_id.has_value() &&
         (controller_client_id->starts_with(kPrivilegedLocalControllerPrefix) ||
          controller_client_id->starts_with(kPrivilegedRemoteControllerPrefix));
}

class ServerTraceLogger {
 public:
  static auto Instance() -> ServerTraceLogger& {
    static ServerTraceLogger instance;
    return instance;
  }

  void Log(const std::string_view event, const std::string_view session_id,
           const std::size_t value = 0) {
    if (output_ == nullptr) {
      return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                              start_time_)
            .count();
    std::lock_guard<std::mutex> lock(mutex_);
    (*output_) << elapsed << ' ' << session_id << ' ' << event << ' ' << value << '\n';
    output_->flush();
  }

 private:
  ServerTraceLogger() {
    const char* path = std::getenv("VIBE_SERVER_TRACE_PATH");
    if (path == nullptr || *path == '\0') {
      return;
    }

    output_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (output_ == nullptr || !output_->is_open()) {
      output_.reset();
      return;
    }
    start_time_ = std::chrono::steady_clock::now();
  }

  std::unique_ptr<std::ofstream> output_;
  std::chrono::steady_clock::time_point start_time_{};
  std::mutex mutex_;
};

class SessionNodeTraceLogger {
 public:
  static auto Instance() -> SessionNodeTraceLogger& {
    static SessionNodeTraceLogger instance;
    return instance;
  }

  void Log(const std::string_view detail) {
    if (!enabled_) {
      return;
    }

    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    {
      std::ostringstream message;
      message << "ts=" << millis << ' ' << detail;
      vibe::base::DebugTrace("core.node", "summary.transition", message.str());
    }

    if (output_ == nullptr) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    (*output_) << millis << ' ' << detail << '\n';
    output_->flush();
  }

 private:
  SessionNodeTraceLogger() {
    enabled_ = vibe::base::DebugTraceEnabled();
    const char* path = std::getenv("SENTRITS_SESSION_SIGNAL_TRACE_PATH");
    if (path == nullptr || *path == '\0') {
      return;
    }

    output_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (output_ == nullptr || !output_->is_open()) {
      output_.reset();
      return;
    }
    enabled_ = true;
  }

  bool enabled_{false};
  std::unique_ptr<std::ofstream> output_;
  std::mutex mutex_;
};

auto IsInteractiveStatus(const vibe::session::SessionStatus status) -> bool {
  return status == vibe::session::SessionStatus::Running ||
         status == vibe::session::SessionStatus::AwaitingInput;
}

auto DefaultTerminalSizeForMetadata(const vibe::session::SessionMetadata& metadata)
    -> vibe::session::TerminalSize {
  const auto provider_config = vibe::session::DefaultProviderConfig(metadata.provider);
  return vibe::session::BuildLaunchSpec(metadata, provider_config).terminal_size;
}

constexpr std::int64_t kRecentOutputWindowMs = 5'000;
constexpr std::int64_t kWorkspaceChangedAttentionMs = 30'000;
constexpr std::int64_t kGitChangedAttentionMs = 30'000;
constexpr std::int64_t kControllerChangedAttentionMs = 20'000;
constexpr std::int64_t kExitedAttentionMs = 120'000;

auto CurrentUnixTimeMs() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

auto NormalizeRecoveredStatus(const vibe::session::SessionStatus status) -> vibe::session::SessionStatus {
  if (status == vibe::session::SessionStatus::Exited ||
      status == vibe::session::SessionStatus::Error) {
    return status;
  }

  return vibe::session::SessionStatus::Exited;
}

auto IsGitDirty(const vibe::session::GitSummary& summary) -> bool {
  return std::max(summary.modified_count, summary.modified_files.size()) > 0U ||
         std::max(summary.staged_count, summary.staged_files.size()) > 0U ||
         std::max(summary.untracked_count, summary.untracked_files.size()) > 0U;
}

auto GitModifiedCount(const vibe::session::GitSummary& summary) -> std::size_t {
  return std::max(summary.modified_count, summary.modified_files.size());
}

auto GitStagedCount(const vibe::session::GitSummary& summary) -> std::size_t {
  return std::max(summary.staged_count, summary.staged_files.size());
}

auto GitUntrackedCount(const vibe::session::GitSummary& summary) -> std::size_t {
  return std::max(summary.untracked_count, summary.untracked_files.size());
}

auto MakeRecoveredTailSlice(const vibe::session::SessionSnapshot& snapshot,
                            const std::size_t max_bytes) -> vibe::session::OutputSlice {
  if (max_bytes == 0 || snapshot.recent_terminal_tail.empty()) {
    return {};
  }

  const std::string tail =
      snapshot.recent_terminal_tail.size() > max_bytes
          ? snapshot.recent_terminal_tail.substr(snapshot.recent_terminal_tail.size() - max_bytes)
          : snapshot.recent_terminal_tail;
  const std::uint64_t seq =
      snapshot.current_sequence == 0 ? 1 : snapshot.current_sequence;
  return vibe::session::OutputSlice{
      .seq_start = seq,
      .seq_end = seq,
      .data = tail,
  };
}

auto MakeRecoveredOutputSlice(const vibe::session::SessionSnapshot& snapshot,
                              const std::uint64_t seq) -> vibe::session::OutputSlice {
  if (snapshot.recent_terminal_tail.empty() || snapshot.current_sequence == 0 ||
      seq > snapshot.current_sequence) {
    return {};
  }

  return vibe::session::OutputSlice{
      .seq_start = snapshot.current_sequence,
      .seq_end = snapshot.current_sequence,
      .data = snapshot.recent_terminal_tail,
  };
}

auto ParseSessionSequenceNumber(const std::string& session_id) -> std::optional<std::size_t> {
  constexpr std::string_view prefix = "s_";
  if (!session_id.starts_with(prefix) || session_id.size() == prefix.size()) {
    return std::nullopt;
  }

  std::size_t parsed_value = 0;
  for (const char ch : session_id.substr(prefix.size())) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }

    parsed_value = (parsed_value * 10U) + static_cast<std::size_t>(ch - '0');
  }

  return parsed_value;
}

auto IsPathWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) -> bool {
  auto root_it = root.begin();
  auto candidate_it = candidate.begin();
  for (; root_it != root.end() && candidate_it != candidate.end(); ++root_it, ++candidate_it) {
    if (*root_it != *candidate_it) {
      return false;
    }
  }

  return root_it == root.end();
}

auto ContainsParentTraversal(const std::filesystem::path& path) -> bool {
  return std::any_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
    return component == "..";
  });
}

auto ApplyGroupTagMutation(const std::vector<std::string>& existing_tags,
                           const SessionGroupTagsUpdateMode mode,
                           const std::vector<std::string>& tags) -> std::vector<std::string> {
  const std::vector<std::string> normalized_existing =
      vibe::session::NormalizeGroupTags(existing_tags);
  const std::vector<std::string> normalized_input = vibe::session::NormalizeGroupTags(tags);

  if (mode == SessionGroupTagsUpdateMode::Set) {
    return normalized_input;
  }

  std::vector<std::string> result = normalized_existing;
  if (mode == SessionGroupTagsUpdateMode::Add) {
    for (const auto& tag : normalized_input) {
      if (std::ranges::find(result, tag) == result.end()) {
        result.push_back(tag);
      }
    }
    return result;
  }

  result.erase(std::remove_if(result.begin(), result.end(),
                              [&normalized_input](const std::string& tag) {
                                return std::ranges::find(normalized_input, tag) != normalized_input.end();
                              }),
               result.end());
  return result;
}

struct AttentionInference {
  vibe::session::AttentionState state{vibe::session::AttentionState::None};
  vibe::session::AttentionReason reason{vibe::session::AttentionReason::None};
  std::optional<std::int64_t> since_unix_ms;
};

auto IsWithinWindow(const std::optional<std::int64_t> timestamp_unix_ms,
                    const std::int64_t now_unix_ms,
                    const std::int64_t window_ms) -> bool {
  return timestamp_unix_ms.has_value() && now_unix_ms >= *timestamp_unix_ms &&
         now_unix_ms - *timestamp_unix_ms <= window_ms;
}

auto InferAttention(const vibe::session::SessionStatus status,
                    const std::optional<std::int64_t> last_status_at_unix_ms,
                    const std::optional<std::int64_t> last_file_change_at_unix_ms,
                    const std::optional<std::int64_t> last_git_change_at_unix_ms,
                    const std::optional<std::int64_t> last_controller_change_at_unix_ms,
                    const std::int64_t now_unix_ms) -> AttentionInference {
  using vibe::session::AttentionReason;
  using vibe::session::AttentionState;
  using vibe::session::SessionStatus;

  if (status == SessionStatus::Error) {
    return AttentionInference{
        .state = AttentionState::Intervention,
        .reason = AttentionReason::SessionError,
        .since_unix_ms = last_status_at_unix_ms,
    };
  }

  if (status == SessionStatus::AwaitingInput) {
    return AttentionInference{
        .state = AttentionState::ActionRequired,
        .reason = AttentionReason::AwaitingInput,
        .since_unix_ms = last_status_at_unix_ms,
    };
  }

  if (IsWithinWindow(last_file_change_at_unix_ms, now_unix_ms, kWorkspaceChangedAttentionMs)) {
    return AttentionInference{
        .state = AttentionState::Info,
        .reason = AttentionReason::WorkspaceChanged,
        .since_unix_ms = last_file_change_at_unix_ms,
    };
  }

  if (IsWithinWindow(last_git_change_at_unix_ms, now_unix_ms, kGitChangedAttentionMs)) {
    return AttentionInference{
        .state = AttentionState::Info,
        .reason = AttentionReason::GitStateChanged,
        .since_unix_ms = last_git_change_at_unix_ms,
    };
  }

  if (IsWithinWindow(last_controller_change_at_unix_ms, now_unix_ms, kControllerChangedAttentionMs)) {
    return AttentionInference{
        .state = AttentionState::Info,
        .reason = AttentionReason::ControllerChanged,
        .since_unix_ms = last_controller_change_at_unix_ms,
    };
  }

  if (status == SessionStatus::Exited &&
      IsWithinWindow(last_status_at_unix_ms, now_unix_ms, kExitedAttentionMs)) {
    return AttentionInference{
        .state = AttentionState::Info,
        .reason = AttentionReason::SessionExitedCleanly,
        .since_unix_ms = last_status_at_unix_ms,
    };
  }

  return {};
}

auto InferInteractionKind(const vibe::session::SessionStatus status,
                          const vibe::session::ControllerKind controller_kind,
                          const std::uint64_t current_sequence)
    -> vibe::session::SessionInteractionKind {
  using vibe::session::ControllerKind;
  using vibe::session::SessionInteractionKind;
  using vibe::session::SessionStatus;

  if (status == SessionStatus::AwaitingInput) {
    return SessionInteractionKind::InteractiveLineMode;
  }

  if (status == SessionStatus::Running && controller_kind != ControllerKind::None) {
    return SessionInteractionKind::InteractiveLineMode;
  }

  if (status == SessionStatus::Running && current_sequence > 0) {
    return SessionInteractionKind::RunningNonInteractive;
  }

  if ((status == SessionStatus::Exited || status == SessionStatus::Error) && current_sequence > 0) {
    return SessionInteractionKind::CompletedQuickly;
  }

  return SessionInteractionKind::Unknown;
}

auto BuildSemanticPreview(const vibe::session::SessionStatus status,
                          const vibe::session::AttentionReason attention_reason,
                          const bool git_dirty) -> std::string {
  using vibe::session::AttentionReason;
  using vibe::session::SessionStatus;

  switch (attention_reason) {
    case AttentionReason::AwaitingInput:
      return "Awaiting input";
    case AttentionReason::SessionError:
      return "Session error";
    case AttentionReason::WorkspaceChanged:
      return "Workspace changed";
    case AttentionReason::GitStateChanged:
      return "Git state changed";
    case AttentionReason::ControllerChanged:
      return "Controller changed";
    case AttentionReason::SessionExitedCleanly:
      return "Session exited cleanly";
    case AttentionReason::None:
      break;
  }

  if (status == SessionStatus::Running && git_dirty) {
    return "Workspace dirty";
  }

  return "";
}

auto BuildNodeSummary(const vibe::session::SessionSnapshot& snapshot,
                      const vibe::session::SessionSignals& signals) -> vibe::session::SessionNodeSummary {
  return vibe::session::SessionNodeSummary{
      .session_id = snapshot.metadata.id.value(),
      .lifecycle_status = snapshot.metadata.status,
      .interaction_kind = signals.interaction_kind,
      .attention_state = signals.attention_state,
      .semantic_preview = BuildSemanticPreview(snapshot.metadata.status, signals.attention_reason,
                                               signals.git_dirty),
      .recent_file_change_count = snapshot.recent_file_changes.size(),
      .git_dirty = signals.git_dirty,
      .last_activity_at_unix_ms = signals.last_activity_at_unix_ms,
  };
}

auto BuildNodeSummaryTraceKey(const vibe::service::SessionSummary& summary) -> std::string {
  std::ostringstream stream;
  stream << "session=" << summary.id.value() << " status=" << vibe::session::ToString(summary.status)
         << " controller=" << vibe::session::ToString(summary.controller_kind)
         << " interaction=" << vibe::session::ToString(summary.interaction_kind)
         << " attention=" << vibe::session::ToString(summary.attention_state)
         << " preview=" << std::quoted(summary.semantic_preview)
         << " files=" << summary.recent_file_change_count
         << " gitDirty=" << (summary.git_dirty ? "true" : "false")
         << " seq=" << summary.current_sequence;
  if (summary.last_activity_at_unix_ms.has_value()) {
    stream << " lastActivityAt=" << *summary.last_activity_at_unix_ms;
  }
  return stream.str();
}

}  // namespace

SessionManager::SessionManager(vibe::store::SessionStore* session_store,
                               PtyProcessFactory pty_process_factory)
    : session_store_(session_store),
      pty_process_factory_(std::move(pty_process_factory)) {}

auto InferSupervisionState(const vibe::session::SessionStatus status,
                           const std::optional<std::int64_t> last_output_at_unix_ms,
                           const std::int64_t now_unix_ms) -> vibe::session::SupervisionState {
  if (status == vibe::session::SessionStatus::Exited ||
      status == vibe::session::SessionStatus::Error) {
    return vibe::session::SupervisionState::Stopped;
  }

  if (IsInteractiveStatus(status) && last_output_at_unix_ms.has_value() &&
      now_unix_ms - *last_output_at_unix_ms <= kRecentOutputWindowMs) {
    return vibe::session::SupervisionState::Active;
  }

  return vibe::session::SupervisionState::Quiet;
}

auto SessionManager::CreateSession(const CreateSessionRequest& request)
    -> std::optional<SessionSummary> {
  const auto session_id = MakeSessionId();
  if (!session_id.has_value()) {
    return std::nullopt;
  }

  vibe::session::SessionMetadata metadata{
      .id = *session_id,
      .provider = request.provider,
      .workspace_root = request.workspace_root,
      .title = request.title,
      .status = vibe::session::SessionStatus::Created,
      .conversation_id = request.conversation_id,
      .group_tags = vibe::session::NormalizeGroupTags(request.group_tags),
  };

  const vibe::session::ProviderConfig provider_config =
      vibe::session::DefaultProviderConfig(request.provider);
  vibe::session::ProviderConfig launch_provider_config = provider_config;
  if (request.command_argv.has_value()) {
    if (request.command_argv->empty() || request.command_argv->front().empty()) {
      return std::nullopt;
    }

    launch_provider_config.executable = request.command_argv->front();
    launch_provider_config.default_args.assign(request.command_argv->begin() + 1, request.command_argv->end());
  }

  const vibe::session::LaunchSpec launch_spec =
      vibe::session::BuildLaunchSpec(metadata, launch_provider_config);

  auto process = pty_process_factory_ ? pty_process_factory_() : nullptr;
  if (process == nullptr) {
    return std::nullopt;
  }

  auto runtime = std::make_unique<vibe::session::SessionRuntime>(
      vibe::session::SessionRecord(metadata), launch_spec, *process);
  auto git_inspector = std::make_unique<vibe::service::GitInspector>(request.workspace_root);
  auto file_watcher = std::make_unique<vibe::service::WorkspaceFileWatcher>(request.workspace_root);

  const auto now_unix_ms = CurrentUnixTimeMs();
  sessions_.push_back(SessionEntry{
      .id = *session_id,
      .process = std::move(process),
      .runtime = std::move(runtime),
      .git_inspector = std::move(git_inspector),
      .file_watcher = std::move(file_watcher),
      .recovered_snapshot = std::nullopt,
      .controller_client_id = std::nullopt,
      .controller_kind = vibe::session::ControllerKind::Host,
      .is_recovered = false,
      .created_at_unix_ms = now_unix_ms,
      .last_status_at_unix_ms = now_unix_ms,
      .last_output_at_unix_ms = std::nullopt,
      .last_activity_at_unix_ms = now_unix_ms,
      .last_file_change_at_unix_ms = std::nullopt,
      .last_git_change_at_unix_ms = std::nullopt,
      .last_controller_change_at_unix_ms = std::nullopt,
      .current_terminal_size = launch_spec.terminal_size,
      .last_observed_status = vibe::session::SessionStatus::Created,
      .last_observed_sequence = 0,
      .last_traced_node_summary_key = std::nullopt,
  });

  SessionEntry& entry = sessions_.back();
  const bool started = entry.runtime->Start();
  static_cast<void>(started);
  entry.last_observed_status = entry.runtime->record().metadata().status;
  entry.last_observed_sequence = entry.runtime->record().snapshot().current_sequence;

  PersistEntry(entry);
  MaybeTraceNodeSummaryTransition(entry, "create");
  return BuildSummary(entry);
}

auto SessionManager::LoadPersistedSessions() -> std::size_t {
  if (session_store_ == nullptr) {
    return 0;
  }

  std::size_t loaded_count = 0;
  for (const auto& persisted : session_store_->LoadSessions()) {
    if (FindEntry(persisted.session_id) != nullptr) {
      continue;
    }

    const auto session_id = vibe::session::SessionId::TryCreate(persisted.session_id);
    if (!session_id.has_value()) {
      continue;
    }

    vibe::session::SessionSnapshot snapshot{
        .metadata =
            vibe::session::SessionMetadata{
                .id = *session_id,
                .provider = persisted.provider,
                .workspace_root = persisted.workspace_root,
                .title = persisted.title,
                .status = NormalizeRecoveredStatus(persisted.status),
                .conversation_id = persisted.conversation_id,
                .group_tags = vibe::session::NormalizeGroupTags(persisted.group_tags),
            },
        .current_sequence = persisted.current_sequence,
        .recent_terminal_tail = persisted.recent_terminal_tail,
        .terminal_screen = std::nullopt,
        .signals =
            vibe::session::SessionSignals{
                .last_output_at_unix_ms = std::nullopt,
                .last_activity_at_unix_ms = std::nullopt,
                .last_file_change_at_unix_ms = std::nullopt,
                .last_git_change_at_unix_ms = std::nullopt,
                .last_controller_change_at_unix_ms = std::nullopt,
                .attention_since_unix_ms = std::nullopt,
                .pty_columns = std::nullopt,
                .pty_rows = std::nullopt,
                .current_sequence = persisted.current_sequence,
                .recent_file_change_count = 0,
                .attention_state = vibe::session::AttentionState::None,
                .attention_reason = vibe::session::AttentionReason::None,
                .interaction_kind = vibe::session::SessionInteractionKind::Unknown,
                .git_dirty = false,
                .git_branch = "",
            },
        .node_summary =
            vibe::session::SessionNodeSummary{
                .session_id = persisted.session_id,
                .lifecycle_status = NormalizeRecoveredStatus(persisted.status),
                .interaction_kind = vibe::session::SessionInteractionKind::Unknown,
                .attention_state = vibe::session::AttentionState::None,
                .semantic_preview = "",
                .recent_file_change_count = 0,
                .git_dirty = false,
                .last_activity_at_unix_ms = std::nullopt,
            },
        .recent_file_changes = {},
        .git_summary = {},
    };
    const auto recovered_status = snapshot.metadata.status;

    sessions_.push_back(SessionEntry{
        .id = *session_id,
        .process = nullptr,
        .runtime = nullptr,
        .git_inspector = nullptr,
        .file_watcher = nullptr,
        .recovered_snapshot = std::move(snapshot),
        .controller_client_id = std::nullopt,
        .controller_kind = vibe::session::ControllerKind::Host,
        .is_recovered = true,
        .created_at_unix_ms = std::nullopt,
        .last_status_at_unix_ms = std::nullopt,
        .last_output_at_unix_ms = std::nullopt,
        .last_activity_at_unix_ms = std::nullopt,
        .last_file_change_at_unix_ms = std::nullopt,
        .last_git_change_at_unix_ms = std::nullopt,
        .last_controller_change_at_unix_ms = std::nullopt,
        .current_terminal_size = DefaultTerminalSizeForMetadata(snapshot.metadata),
        .last_observed_status = recovered_status,
        .last_observed_sequence = persisted.current_sequence,
        .last_traced_node_summary_key = std::nullopt,
    });
    MaybeTraceNodeSummaryTransition(sessions_.back(), "load_persisted");
    loaded_count += 1;
  }

  return loaded_count;
}

auto SessionManager::ListSessions() const -> std::vector<SessionSummary> {
  std::vector<SessionSummary> summaries;
  summaries.reserve(sessions_.size());

  for (const SessionEntry& entry : sessions_) {
    summaries.push_back(BuildSummary(entry));
  }

  return summaries;
}

auto SessionManager::GetSession(const std::string& session_id) const
    -> std::optional<SessionSummary> {
  for (const SessionEntry& entry : sessions_) {
    if (entry.id.value() != session_id) {
      continue;
    }

    return BuildSummary(entry);
  }

  return std::nullopt;
}

auto SessionManager::GetSnapshot(const std::string& session_id) const
    -> std::optional<vibe::session::SessionSnapshot> {
  const auto now_unix_ms = CurrentUnixTimeMs();
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime) {
      auto snapshot = entry->runtime->record().snapshot();
      const auto attention = InferAttention(snapshot.metadata.status, entry->last_status_at_unix_ms,
                                            entry->last_file_change_at_unix_ms,
                                            entry->last_git_change_at_unix_ms,
                                            entry->last_controller_change_at_unix_ms, now_unix_ms);
      snapshot.signals = vibe::session::SessionSignals{
          .last_output_at_unix_ms = entry->last_output_at_unix_ms,
          .last_activity_at_unix_ms = entry->last_activity_at_unix_ms,
          .last_file_change_at_unix_ms = entry->last_file_change_at_unix_ms,
          .last_git_change_at_unix_ms = entry->last_git_change_at_unix_ms,
          .last_controller_change_at_unix_ms = entry->last_controller_change_at_unix_ms,
          .attention_since_unix_ms = attention.since_unix_ms,
          .pty_columns = entry->current_terminal_size.columns,
          .pty_rows = entry->current_terminal_size.rows,
          .current_sequence = snapshot.current_sequence,
          .recent_file_change_count = snapshot.recent_file_changes.size(),
          .supervision_state =
              InferSupervisionState(snapshot.metadata.status, entry->last_output_at_unix_ms, now_unix_ms),
          .attention_state = attention.state,
          .attention_reason = attention.reason,
          .interaction_kind = InferInteractionKind(snapshot.metadata.status, entry->controller_kind,
                                                   snapshot.current_sequence),
          .git_dirty = IsGitDirty(snapshot.git_summary),
          .git_branch = snapshot.git_summary.branch,
          .git_modified_count = GitModifiedCount(snapshot.git_summary),
          .git_staged_count = GitStagedCount(snapshot.git_summary),
          .git_untracked_count = GitUntrackedCount(snapshot.git_summary),
      };
      snapshot.node_summary = BuildNodeSummary(snapshot, snapshot.signals);
      return snapshot;
    }
    auto snapshot = *entry->recovered_snapshot;
    const auto attention = InferAttention(snapshot.metadata.status, entry->last_status_at_unix_ms,
                                          entry->last_file_change_at_unix_ms,
                                          entry->last_git_change_at_unix_ms,
                                          entry->last_controller_change_at_unix_ms, now_unix_ms);
    snapshot.signals = vibe::session::SessionSignals{
        .last_output_at_unix_ms = entry->last_output_at_unix_ms,
        .last_activity_at_unix_ms = entry->last_activity_at_unix_ms,
        .last_file_change_at_unix_ms = entry->last_file_change_at_unix_ms,
        .last_git_change_at_unix_ms = entry->last_git_change_at_unix_ms,
        .last_controller_change_at_unix_ms = entry->last_controller_change_at_unix_ms,
        .attention_since_unix_ms = attention.since_unix_ms,
        .pty_columns = entry->current_terminal_size.columns,
        .pty_rows = entry->current_terminal_size.rows,
        .current_sequence = snapshot.current_sequence,
        .recent_file_change_count = snapshot.recent_file_changes.size(),
        .supervision_state =
            InferSupervisionState(snapshot.metadata.status, entry->last_output_at_unix_ms, now_unix_ms),
        .attention_state = attention.state,
        .attention_reason = attention.reason,
        .interaction_kind = InferInteractionKind(snapshot.metadata.status, entry->controller_kind,
                                                 snapshot.current_sequence),
        .git_dirty = IsGitDirty(snapshot.git_summary),
        .git_branch = snapshot.git_summary.branch,
        .git_modified_count = GitModifiedCount(snapshot.git_summary),
        .git_staged_count = GitStagedCount(snapshot.git_summary),
        .git_untracked_count = GitUntrackedCount(snapshot.git_summary),
    };
    snapshot.node_summary = BuildNodeSummary(snapshot, snapshot.signals);
    return snapshot;
  }

  return std::nullopt;
}

auto SessionManager::GetTail(const std::string& session_id, const std::size_t bytes) const
    -> std::optional<vibe::session::OutputSlice> {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime) {
      return entry->runtime->output_buffer().Tail(bytes);
    }
    if (entry->recovered_snapshot.has_value()) {
      return MakeRecoveredTailSlice(*entry->recovered_snapshot, bytes);
    }
  }

  return std::nullopt;
}

auto SessionManager::GetOutputSince(const std::string& session_id, const std::uint64_t seq) const
    -> std::optional<vibe::session::OutputSlice> {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime) {
      return entry->runtime->output_buffer().SliceFromSequence(seq);
    }
    if (entry->recovered_snapshot.has_value()) {
      return MakeRecoveredOutputSlice(*entry->recovered_snapshot, seq);
    }
  }

  return std::nullopt;
}

auto SessionManager::GetReadableFd(const std::string& session_id) const -> std::optional<int> {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr && entry->runtime != nullptr) {
    return entry->runtime->readable_fd();
  }

  return std::nullopt;
}

auto SessionManager::ReadFile(const std::string& session_id, const std::string& workspace_path,
                              const std::size_t max_bytes) const -> SessionFileReadResult {
  const SessionEntry* entry = FindEntry(session_id);
  if (entry == nullptr) {
    return SessionFileReadResult{
        .status = FileReadStatus::SessionNotFound,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  if (workspace_path.empty()) {
    return SessionFileReadResult{
        .status = FileReadStatus::InvalidPath,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  const auto snapshot = entry->runtime ? entry->runtime->record().snapshot() : *entry->recovered_snapshot;
  const std::filesystem::path requested_path(workspace_path);
  if (requested_path.empty() || requested_path.is_absolute() || ContainsParentTraversal(requested_path)) {
    return SessionFileReadResult{
        .status = FileReadStatus::InvalidPath,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  std::error_code error_code;
  const std::filesystem::path workspace_root =
      std::filesystem::weakly_canonical(std::filesystem::absolute(snapshot.metadata.workspace_root), error_code);
  if (error_code || workspace_root.empty() || !std::filesystem::exists(workspace_root) ||
      !std::filesystem::is_directory(workspace_root)) {
    return SessionFileReadResult{
        .status = FileReadStatus::WorkspaceUnavailable,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  const std::filesystem::path resolved_path =
      std::filesystem::weakly_canonical(workspace_root / requested_path, error_code);
  if (error_code) {
    return SessionFileReadResult{
        .status = FileReadStatus::NotFound,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }
  if (!IsPathWithinRoot(workspace_root, resolved_path)) {
    return SessionFileReadResult{
        .status = FileReadStatus::InvalidPath,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }
  if (!std::filesystem::exists(resolved_path)) {
    return SessionFileReadResult{
        .status = FileReadStatus::NotFound,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }
  if (!std::filesystem::is_regular_file(resolved_path)) {
    return SessionFileReadResult{
        .status = FileReadStatus::NotRegularFile,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  const auto file_size = std::filesystem::file_size(resolved_path, error_code);
  if (error_code) {
    return SessionFileReadResult{
        .status = FileReadStatus::IoError,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  std::ifstream stream(resolved_path, std::ios::binary);
  if (!stream.is_open()) {
    return SessionFileReadResult{
        .status = FileReadStatus::IoError,
        .workspace_path = "",
        .content = "",
        .size_bytes = 0,
        .truncated = false,
    };
  }

  const std::size_t bytes_to_read = static_cast<std::size_t>(
      std::min<std::uint64_t>(file_size, static_cast<std::uint64_t>(max_bytes)));
  std::string content(bytes_to_read, '\0');
  if (bytes_to_read > 0) {
    stream.read(content.data(), static_cast<std::streamsize>(bytes_to_read));
    if (!stream && !stream.eof()) {
      return SessionFileReadResult{
          .status = FileReadStatus::IoError,
          .workspace_path = "",
          .content = "",
          .size_bytes = 0,
          .truncated = false,
      };
    }
    content.resize(static_cast<std::size_t>(stream.gcount()));
  }

  return SessionFileReadResult{
      .status = FileReadStatus::Ok,
      .workspace_path = requested_path.generic_string(),
      .content = std::move(content),
      .size_bytes = file_size,
      .truncated = file_size > static_cast<std::uint64_t>(max_bytes),
  };
}

auto SessionManager::SendInput(const std::string& session_id, const std::string& input) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime == nullptr) {
      return false;
    }
    ServerTraceLogger::Instance().Log("send_input.begin", session_id, input.size());
    const bool wrote = entry->runtime->WriteInput(input);
    ServerTraceLogger::Instance().Log(wrote ? "send_input.ok" : "send_input.fail", session_id,
                                      input.size());
    return wrote;
  }

  return false;
}

auto SessionManager::GetViewportSnapshot(const std::string& session_id, const std::string& view_id) const
    -> std::optional<vibe::session::TerminalViewportSnapshot> {
  const SessionEntry* entry = FindEntry(session_id);
  if (entry == nullptr || entry->runtime == nullptr || view_id.empty()) {
    return std::nullopt;
  }

  return entry->runtime->viewport_snapshot(view_id);
}

auto SessionManager::UpdateSessionGroupTags(const std::string& session_id,
                                            const SessionGroupTagsUpdateMode mode,
                                            const std::vector<std::string>& tags)
    -> std::optional<SessionSummary> {
  SessionEntry* entry = FindEntry(session_id);
  if (entry == nullptr) {
    return std::nullopt;
  }

  if (entry->runtime != nullptr) {
    const auto snapshot = entry->runtime->record().snapshot();
    entry->runtime->UpdateGroupTags(ApplyGroupTagMutation(snapshot.metadata.group_tags, mode, tags));
  } else if (entry->recovered_snapshot.has_value()) {
    entry->recovered_snapshot->metadata.group_tags =
        ApplyGroupTagMutation(entry->recovered_snapshot->metadata.group_tags, mode, tags);
  }

  PersistEntry(*entry);
  MaybeTraceNodeSummaryTransition(*entry, "group_tags");
  return BuildSummary(*entry);
}

auto SessionManager::ResizeSession(const std::string& session_id,
                                   const vibe::session::TerminalSize terminal_size) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime == nullptr) {
      return false;
    }
    const bool resized = entry->runtime->ResizeTerminal(terminal_size);
    if (resized) {
      entry->current_terminal_size = terminal_size;
    }
    return resized;
  }

  return false;
}

auto SessionManager::UpdateViewport(const std::string& session_id, const std::string& view_id,
                                    const vibe::session::TerminalSize viewport_size) -> bool {
  SessionEntry* entry = FindEntry(session_id);
  if (entry == nullptr || entry->runtime == nullptr || view_id.empty()) {
    return false;
  }

  {
    std::ostringstream trace;
    trace << "session=" << session_id << " viewId=" << view_id << " cols=" << viewport_size.columns
          << " rows=" << viewport_size.rows;
    vibe::base::DebugTrace("core.focus", "viewport.update", trace.str());
  }
  entry->runtime->UpdateViewport(view_id, viewport_size);
  return true;
}

void SessionManager::RemoveViewport(const std::string& session_id, const std::string& view_id) {
  SessionEntry* entry = FindEntry(session_id);
  if (entry == nullptr || entry->runtime == nullptr || view_id.empty()) {
    return;
  }

  entry->runtime->RemoveViewport(view_id);
}

auto SessionManager::StopSession(const std::string& session_id) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    const auto status = BuildSummary(*entry).status;
    if (status == vibe::session::SessionStatus::Exited ||
        status == vibe::session::SessionStatus::Error) {
      ResetControllerState(*entry);
      PersistEntry(*entry);
      return true;
    }

    if (entry->runtime == nullptr) {
      return false;
    }

    const bool stopped = entry->runtime->Shutdown();
    if (stopped) {
      const auto current_status = entry->runtime->record().metadata().status;
      if (current_status != entry->last_observed_status) {
        const auto now_unix_ms = CurrentUnixTimeMs();
        entry->last_status_at_unix_ms = now_unix_ms;
        entry->last_activity_at_unix_ms = now_unix_ms;
        entry->last_observed_status = current_status;
      }
      ResetControllerState(*entry);
      PersistEntry(*entry);
      MaybeTraceNodeSummaryTransition(*entry, "stop");
    }
    return stopped;
  }

  return false;
}

auto SessionManager::ClearInactiveSessions() -> std::size_t {
  std::size_t removed_count = 0;

  auto it = sessions_.begin();
  while (it != sessions_.end()) {
    const SessionSummary summary = BuildSummary(*it);
    if (summary.is_active) {
      ++it;
      continue;
    }

    if (session_store_ != nullptr) {
      const bool removed = session_store_->RemoveSessionRecord(summary.id.value());
      static_cast<void>(removed);
    }

    it = sessions_.erase(it);
    removed_count += 1;
  }

  return removed_count;
}

auto SessionManager::RequestControl(const std::string& session_id, const std::string& client_id,
                                    const vibe::session::ControllerKind controller_kind) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    const SessionSummary summary = BuildSummary(*entry);
    if (!IsInteractiveStatus(summary.status) || controller_kind == vibe::session::ControllerKind::None) {
      return false;
    }

    if (entry->controller_kind == vibe::session::ControllerKind::Host ||
        entry->controller_kind == vibe::session::ControllerKind::None) {
      entry->controller_client_id = client_id;
      entry->controller_kind = controller_kind;
      entry->last_activity_at_unix_ms = CurrentUnixTimeMs();
      entry->last_controller_change_at_unix_ms = entry->last_activity_at_unix_ms;
      MaybeTraceNodeSummaryTransition(*entry, "request_control");
      return true;
    }

    if (entry->controller_kind == vibe::session::ControllerKind::Remote &&
        controller_kind == vibe::session::ControllerKind::Host) {
      entry->controller_client_id = client_id;
      entry->controller_kind = controller_kind;
      entry->last_activity_at_unix_ms = CurrentUnixTimeMs();
      entry->last_controller_change_at_unix_ms = entry->last_activity_at_unix_ms;
      MaybeTraceNodeSummaryTransition(*entry, "request_control");
      return true;
    }

    return *entry->controller_client_id == client_id;
  }

  return false;
}

auto SessionManager::ReleaseControl(const std::string& session_id, const std::string& client_id) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->controller_kind == vibe::session::ControllerKind::None) {
      return true;
    }

    if (!entry->controller_client_id.has_value()) {
      return entry->controller_kind == vibe::session::ControllerKind::Host;
    }

    if (*entry->controller_client_id != client_id) {
      return false;
    }

    entry->controller_client_id.reset();
    entry->controller_kind = vibe::session::ControllerKind::Host;
    entry->last_activity_at_unix_ms = CurrentUnixTimeMs();
    entry->last_controller_change_at_unix_ms = entry->last_activity_at_unix_ms;
    MaybeTraceNodeSummaryTransition(*entry, "release_control");
    return true;
  }

  return false;
}

auto SessionManager::HasControl(const std::string& session_id, const std::string& client_id) const -> bool {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->controller_client_id.has_value() && *entry->controller_client_id == client_id &&
           entry->controller_kind != vibe::session::ControllerKind::None;
  }

  return false;
}

auto SessionManager::HasPrivilegedController(const std::string& session_id) const -> bool {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->controller_kind != vibe::session::ControllerKind::None &&
           IsPrivilegedControllerClientId(entry->controller_client_id);
  }

  return false;
}

auto SessionManager::Shutdown() -> std::size_t {
  std::size_t shutdown_count = 0;

  for (SessionEntry& entry : sessions_) {
    bool changed = false;
    if (entry.runtime != nullptr) {
      const vibe::session::SessionStatus previous_status = entry.runtime->record().metadata().status;
      const bool shutdown = entry.runtime->Shutdown();
      if (!shutdown) {
        continue;
      }
      changed = true;
      if (previous_status != entry.runtime->record().metadata().status ||
          previous_status == vibe::session::SessionStatus::Running ||
          previous_status == vibe::session::SessionStatus::AwaitingInput) {
        entry.last_status_at_unix_ms = CurrentUnixTimeMs();
        entry.last_activity_at_unix_ms = entry.last_status_at_unix_ms;
        shutdown_count += 1;
      }
    }

    if (entry.controller_client_id.has_value() ||
        entry.controller_kind != vibe::session::ControllerKind::Host) {
      ResetControllerState(entry);
      changed = true;
    }

    if (changed) {
      PersistEntry(entry);
    }
  }

  return shutdown_count;
}

auto SessionManager::PollSession(const std::string& session_id, const int read_timeout_ms) -> bool {
  SessionEntry* entry = FindEntry(session_id);
  if (entry == nullptr || entry->runtime == nullptr) {
    return false;
  }

  const std::uint64_t previous_sequence = entry->last_observed_sequence;
  entry->runtime->PollOnce(read_timeout_ms);
  const auto snapshot = entry->runtime->record().snapshot();
  if (snapshot.current_sequence != entry->last_observed_sequence) {
    const auto now_unix_ms = CurrentUnixTimeMs();
    entry->last_observed_sequence = snapshot.current_sequence;
    entry->last_output_at_unix_ms = now_unix_ms;
    entry->last_activity_at_unix_ms = now_unix_ms;
    ServerTraceLogger::Instance().Log(
        "poll.output", session_id,
        static_cast<std::size_t>(snapshot.current_sequence - previous_sequence));
  }

  const auto current_status = entry->runtime->record().metadata().status;
  if (current_status != entry->last_observed_status) {
    const auto now_unix_ms = CurrentUnixTimeMs();
    entry->last_status_at_unix_ms = now_unix_ms;
    entry->last_activity_at_unix_ms = now_unix_ms;
    entry->last_observed_status = current_status;
  }

  PersistEntry(*entry);
  MaybeTraceNodeSummaryTransition(*entry, "poll");
  return true;
}

void SessionManager::PollAll(const int read_timeout_ms) {
  poll_count_ += 1;
  const bool should_poll_git = (poll_count_ % 100 == 0);
  const bool should_poll_files = (poll_count_ % 20 == 0);

  for (SessionEntry& entry : sessions_) {
    if (entry.runtime == nullptr) {
      continue;
    }

    if (entry.controller_kind != vibe::session::ControllerKind::None &&
        IsPrivilegedControllerClientId(entry.controller_client_id)) {
      continue;
    }

    static_cast<void>(PollSession(entry.id.value(), read_timeout_ms));
    auto snapshot = entry.runtime->record().snapshot();

    if (should_poll_git && entry.git_inspector) {
      const auto git_summary = entry.git_inspector->Inspect();
      if (git_summary != snapshot.git_summary) {
        entry.runtime->UpdateGitSummary(git_summary);
        entry.last_activity_at_unix_ms = CurrentUnixTimeMs();
        entry.last_git_change_at_unix_ms = entry.last_activity_at_unix_ms;
        PersistEntry(entry);
        MaybeTraceNodeSummaryTransition(entry, "git_change");
      }
    }

    if (should_poll_files && entry.file_watcher) {
      const auto changed_files = entry.file_watcher->PollChangedFiles();
      if (!changed_files.empty()) {
        entry.runtime->UpdateRecentFileChanges(changed_files);
        entry.last_activity_at_unix_ms = CurrentUnixTimeMs();
        entry.last_file_change_at_unix_ms = entry.last_activity_at_unix_ms;
        PersistEntry(entry);
        MaybeTraceNodeSummaryTransition(entry, "file_change");
      }
    }
  }
}

auto SessionManager::BuildSummary(const SessionEntry& entry) const -> SessionSummary {
  const auto now_unix_ms = CurrentUnixTimeMs();
  const auto attention = InferAttention(entry.runtime ? entry.runtime->record().metadata().status
                                                      : entry.recovered_snapshot->metadata.status,
                                        entry.last_status_at_unix_ms, entry.last_file_change_at_unix_ms,
                                        entry.last_git_change_at_unix_ms,
                                        entry.last_controller_change_at_unix_ms, now_unix_ms);
  if (entry.runtime) {
    const auto snapshot = entry.runtime->record().snapshot();
    const auto& metadata = snapshot.metadata;
    const auto interaction_kind =
        InferInteractionKind(metadata.status, entry.controller_kind, snapshot.current_sequence);
    const auto semantic_preview =
        BuildSemanticPreview(metadata.status, attention.reason, IsGitDirty(snapshot.git_summary));
    return SessionSummary{
        .id = metadata.id,
        .provider = metadata.provider,
        .workspace_root = metadata.workspace_root,
        .title = metadata.title,
        .status = metadata.status,
        .conversation_id = metadata.conversation_id,
        .group_tags = metadata.group_tags,
        .controller_client_id = entry.controller_client_id,
        .controller_kind = entry.controller_kind,
        .is_recovered = entry.is_recovered,
        .is_active = IsInteractiveStatus(metadata.status),
        .supervision_state =
            InferSupervisionState(metadata.status, entry.last_output_at_unix_ms, now_unix_ms),
        .attention_state = attention.state,
        .attention_reason = attention.reason,
        .created_at_unix_ms = entry.created_at_unix_ms,
        .last_status_at_unix_ms = entry.last_status_at_unix_ms,
        .last_output_at_unix_ms = entry.last_output_at_unix_ms,
        .last_activity_at_unix_ms = entry.last_activity_at_unix_ms,
        .last_file_change_at_unix_ms = entry.last_file_change_at_unix_ms,
        .last_git_change_at_unix_ms = entry.last_git_change_at_unix_ms,
        .last_controller_change_at_unix_ms = entry.last_controller_change_at_unix_ms,
        .attention_since_unix_ms = attention.since_unix_ms,
        .pty_columns = entry.current_terminal_size.columns,
        .pty_rows = entry.current_terminal_size.rows,
        .current_sequence = snapshot.current_sequence,
        .recent_file_change_count = snapshot.recent_file_changes.size(),
        .interaction_kind = interaction_kind,
        .semantic_preview = semantic_preview,
        .node_summary =
            vibe::session::SessionNodeSummary{
                .session_id = metadata.id.value(),
                .lifecycle_status = metadata.status,
                .interaction_kind = interaction_kind,
                .attention_state = attention.state,
                .semantic_preview = semantic_preview,
                .recent_file_change_count = snapshot.recent_file_changes.size(),
                .git_dirty = IsGitDirty(snapshot.git_summary),
                .last_activity_at_unix_ms = entry.last_activity_at_unix_ms,
            },
        .git_dirty = IsGitDirty(snapshot.git_summary),
        .git_branch = snapshot.git_summary.branch,
        .git_modified_count = GitModifiedCount(snapshot.git_summary),
        .git_staged_count = GitStagedCount(snapshot.git_summary),
        .git_untracked_count = GitUntrackedCount(snapshot.git_summary),
    };
  }

  const auto& snapshot = *entry.recovered_snapshot;
  const auto& metadata = snapshot.metadata;
  const auto interaction_kind =
      InferInteractionKind(metadata.status, entry.controller_kind, snapshot.current_sequence);
  const auto semantic_preview =
      BuildSemanticPreview(metadata.status, attention.reason, IsGitDirty(snapshot.git_summary));
  return SessionSummary{
      .id = metadata.id,
      .provider = metadata.provider,
      .workspace_root = metadata.workspace_root,
      .title = metadata.title,
      .status = metadata.status,
      .conversation_id = metadata.conversation_id,
      .group_tags = metadata.group_tags,
      .controller_client_id = entry.controller_client_id,
      .controller_kind = entry.controller_kind,
      .is_recovered = entry.is_recovered,
      .is_active = IsInteractiveStatus(metadata.status),
      .supervision_state = InferSupervisionState(metadata.status, entry.last_output_at_unix_ms, now_unix_ms),
      .attention_state = attention.state,
      .attention_reason = attention.reason,
      .created_at_unix_ms = entry.created_at_unix_ms,
      .last_status_at_unix_ms = entry.last_status_at_unix_ms,
      .last_output_at_unix_ms = entry.last_output_at_unix_ms,
      .last_activity_at_unix_ms = entry.last_activity_at_unix_ms,
      .last_file_change_at_unix_ms = entry.last_file_change_at_unix_ms,
      .last_git_change_at_unix_ms = entry.last_git_change_at_unix_ms,
      .last_controller_change_at_unix_ms = entry.last_controller_change_at_unix_ms,
      .attention_since_unix_ms = attention.since_unix_ms,
      .pty_columns = entry.current_terminal_size.columns,
      .pty_rows = entry.current_terminal_size.rows,
      .current_sequence = snapshot.current_sequence,
      .recent_file_change_count = snapshot.recent_file_changes.size(),
      .interaction_kind = interaction_kind,
      .semantic_preview = semantic_preview,
      .node_summary =
          vibe::session::SessionNodeSummary{
              .session_id = metadata.id.value(),
              .lifecycle_status = metadata.status,
              .interaction_kind = interaction_kind,
              .attention_state = attention.state,
              .semantic_preview = semantic_preview,
              .recent_file_change_count = snapshot.recent_file_changes.size(),
              .git_dirty = IsGitDirty(snapshot.git_summary),
              .last_activity_at_unix_ms = entry.last_activity_at_unix_ms,
          },
      .git_dirty = IsGitDirty(snapshot.git_summary),
      .git_branch = snapshot.git_summary.branch,
      .git_modified_count = GitModifiedCount(snapshot.git_summary),
      .git_staged_count = GitStagedCount(snapshot.git_summary),
      .git_untracked_count = GitUntrackedCount(snapshot.git_summary),
  };
}

void SessionManager::MaybeTraceNodeSummaryTransition(SessionEntry& entry,
                                                     const std::string_view reason) {
  const SessionSummary summary = BuildSummary(entry);
  const std::string trace_key = BuildNodeSummaryTraceKey(summary);
  if (entry.last_traced_node_summary_key.has_value() &&
      *entry.last_traced_node_summary_key == trace_key) {
    return;
  }

  entry.last_traced_node_summary_key = trace_key;
  std::ostringstream detail;
  detail << "reason=" << reason << ' ' << trace_key;
  SessionNodeTraceLogger::Instance().Log(detail.str());
}

void SessionManager::ResetControllerState(SessionEntry& entry) {
  entry.controller_client_id.reset();
  entry.controller_kind = vibe::session::ControllerKind::Host;
}

void SessionManager::PersistEntry(const SessionEntry& entry) {
  if (session_store_ == nullptr) {
    return;
  }

  const auto snapshot = entry.runtime ? entry.runtime->record().snapshot() : *entry.recovered_snapshot;
  const vibe::store::PersistedSessionRecord record{
      .session_id = snapshot.metadata.id.value(),
      .provider = snapshot.metadata.provider,
      .workspace_root = snapshot.metadata.workspace_root,
      .title = snapshot.metadata.title,
      .status = snapshot.metadata.status,
      .conversation_id = snapshot.metadata.conversation_id,
      .group_tags = snapshot.metadata.group_tags,
      .current_sequence = snapshot.current_sequence,
      .recent_terminal_tail = snapshot.recent_terminal_tail,
  };
  const bool persisted = session_store_->UpsertSessionRecord(record);
  static_cast<void>(persisted);
}

auto SessionManager::MakeSessionId() const -> std::optional<vibe::session::SessionId> {
  std::vector<std::size_t> used_sequence_numbers;
  used_sequence_numbers.reserve(sessions_.size());
  for (const SessionEntry& entry : sessions_) {
    const auto parsed_value = ParseSessionSequenceNumber(entry.id.value());
    if (!parsed_value.has_value()) {
      continue;
    }

    used_sequence_numbers.push_back(*parsed_value);
  }

  std::sort(used_sequence_numbers.begin(), used_sequence_numbers.end());
  used_sequence_numbers.erase(
      std::unique(used_sequence_numbers.begin(), used_sequence_numbers.end()),
      used_sequence_numbers.end());

  std::size_t next_sequence_number = 1;
  for (const std::size_t sequence_number : used_sequence_numbers) {
    if (sequence_number != next_sequence_number) {
      break;
    }
    next_sequence_number += 1;
  }

  const std::string next_value = "s_" + std::to_string(next_sequence_number);
  return vibe::session::SessionId::TryCreate(next_value);
}

auto SessionManager::FindEntry(const std::string& session_id) -> SessionEntry* {
  for (SessionEntry& entry : sessions_) {
    if (entry.id.value() == session_id) {
      return &entry;
    }
  }

  return nullptr;
}

auto SessionManager::FindEntry(const std::string& session_id) const -> const SessionEntry* {
  for (const SessionEntry& entry : sessions_) {
    if (entry.id.value() == session_id) {
      return &entry;
    }
  }

  return nullptr;
}

}  // namespace vibe::service

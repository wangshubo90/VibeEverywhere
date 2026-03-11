#include "vibe/service/session_manager.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "vibe/service/git_inspector.h"
#include "vibe/session/provider_config.h"
#include "vibe/session/session_record.h"

namespace vibe::service {

namespace {

auto IsInteractiveStatus(const vibe::session::SessionStatus status) -> bool {
  return status == vibe::session::SessionStatus::Running ||
         status == vibe::session::SessionStatus::AwaitingInput;
}

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

}  // namespace

SessionManager::SessionManager(vibe::store::SessionStore* session_store)
    : session_store_(session_store) {}

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

  auto process = std::make_unique<vibe::session::PosixPtyProcess>();
  auto runtime = std::make_unique<vibe::session::SessionRuntime>(
      vibe::session::SessionRecord(metadata), launch_spec, *process);
  auto git_inspector = std::make_unique<vibe::service::GitInspector>(request.workspace_root);

  const auto now_unix_ms = CurrentUnixTimeMs();
  sessions_.push_back(SessionEntry{
      .id = *session_id,
      .process = std::move(process),
      .runtime = std::move(runtime),
      .git_inspector = std::move(git_inspector),
      .recovered_snapshot = std::nullopt,
      .controller_client_id = std::nullopt,
      .controller_kind = vibe::session::ControllerKind::Host,
      .is_recovered = false,
      .created_at_unix_ms = now_unix_ms,
      .last_status_at_unix_ms = now_unix_ms,
      .last_observed_status = vibe::session::SessionStatus::Created,
  });

  SessionEntry& entry = sessions_.back();
  const bool started = entry.runtime->Start();
  static_cast<void>(started);
  entry.last_observed_status = entry.runtime->record().metadata().status;

  PersistEntry(entry);
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
            },
        .current_sequence = persisted.current_sequence,
        .recent_terminal_tail = persisted.recent_terminal_tail,
        .recent_file_changes = {},
        .git_summary = {},
    };
    const auto recovered_status = snapshot.metadata.status;

    sessions_.push_back(SessionEntry{
        .id = *session_id,
        .process = nullptr,
        .runtime = nullptr,
        .git_inspector = nullptr,
        .recovered_snapshot = std::move(snapshot),
        .controller_client_id = std::nullopt,
        .controller_kind = vibe::session::ControllerKind::Host,
        .is_recovered = true,
        .created_at_unix_ms = std::nullopt,
        .last_status_at_unix_ms = std::nullopt,
        .last_observed_status = recovered_status,
    });
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
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime) {
      return entry->runtime->record().snapshot();
    }
    return entry->recovered_snapshot;
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

auto SessionManager::SendInput(const std::string& session_id, const std::string& input) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime == nullptr) {
      return false;
    }
    return entry->runtime->WriteInput(input);
  }

  return false;
}

auto SessionManager::ResizeSession(const std::string& session_id,
                                   const vibe::session::TerminalSize terminal_size) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (entry->runtime == nullptr) {
      return false;
    }
    return entry->runtime->ResizeTerminal(terminal_size);
  }

  return false;
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
        entry->last_status_at_unix_ms = CurrentUnixTimeMs();
        entry->last_observed_status = current_status;
      }
      ResetControllerState(*entry);
      PersistEntry(*entry);
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
      return true;
    }

    if (entry->controller_kind == vibe::session::ControllerKind::Remote &&
        controller_kind == vibe::session::ControllerKind::Host) {
      entry->controller_client_id = client_id;
      entry->controller_kind = controller_kind;
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

void SessionManager::PollAll(const int read_timeout_ms) {
  poll_count_ += 1;
  const bool should_poll_git = (poll_count_ % 100 == 0);

  for (SessionEntry& entry : sessions_) {
    if (entry.runtime == nullptr) {
      continue;
    }

    entry.runtime->PollOnce(read_timeout_ms);
    const auto current_status = entry.runtime->record().metadata().status;
    if (current_status != entry.last_observed_status) {
      entry.last_status_at_unix_ms = CurrentUnixTimeMs();
      entry.last_observed_status = current_status;
    }
    PersistEntry(entry);

    if (should_poll_git && entry.git_inspector) {
      entry.runtime->UpdateGitSummary(entry.git_inspector->Inspect());
      PersistEntry(entry);
    }
  }
}

auto SessionManager::BuildSummary(const SessionEntry& entry) const -> SessionSummary {
  if (entry.runtime) {
    const auto& metadata = entry.runtime->record().metadata();
    return SessionSummary{
        .id = metadata.id,
        .provider = metadata.provider,
        .workspace_root = metadata.workspace_root,
        .title = metadata.title,
        .status = metadata.status,
        .controller_client_id = entry.controller_client_id,
        .controller_kind = entry.controller_kind,
        .is_recovered = entry.is_recovered,
        .is_active = IsInteractiveStatus(metadata.status),
        .created_at_unix_ms = entry.created_at_unix_ms,
        .last_status_at_unix_ms = entry.last_status_at_unix_ms,
    };
  }

  const auto& metadata = entry.recovered_snapshot->metadata;
  return SessionSummary{
      .id = metadata.id,
      .provider = metadata.provider,
      .workspace_root = metadata.workspace_root,
      .title = metadata.title,
      .status = metadata.status,
      .controller_client_id = entry.controller_client_id,
      .controller_kind = entry.controller_kind,
      .is_recovered = entry.is_recovered,
      .is_active = IsInteractiveStatus(metadata.status),
      .created_at_unix_ms = entry.created_at_unix_ms,
      .last_status_at_unix_ms = entry.last_status_at_unix_ms,
  };
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
      .current_sequence = snapshot.current_sequence,
      .recent_terminal_tail = snapshot.recent_terminal_tail,
  };
  const bool persisted = session_store_->UpsertSessionRecord(record);
  static_cast<void>(persisted);
}

auto SessionManager::MakeSessionId() const -> std::optional<vibe::session::SessionId> {
  std::size_t max_sequence_number = 0;
  for (const SessionEntry& entry : sessions_) {
    const auto parsed_value = ParseSessionSequenceNumber(entry.id.value());
    if (!parsed_value.has_value()) {
      continue;
    }

    max_sequence_number = std::max(max_sequence_number, *parsed_value);
  }

  const std::string next_value = "s_" + std::to_string(max_sequence_number + 1U);
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

#include "vibe/service/session_manager.h"

#include <utility>

#include "vibe/service/git_inspector.h"
#include "vibe/session/provider_config.h"
#include "vibe/session/session_record.h"

namespace vibe::service {

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
  const vibe::session::LaunchSpec launch_spec =
      vibe::session::BuildLaunchSpec(metadata, provider_config);

  auto process = std::make_unique<vibe::session::PosixPtyProcess>();
  auto runtime = std::make_unique<vibe::session::SessionRuntime>(
      vibe::session::SessionRecord(metadata), launch_spec, *process);
  auto git_inspector = std::make_unique<vibe::service::GitInspector>(request.workspace_root);

  sessions_.push_back(SessionEntry{
      .id = *session_id,
      .process = std::move(process),
      .runtime = std::move(runtime),
      .git_inspector = std::move(git_inspector),
      .controller_client_id = std::nullopt,
      .controller_kind = vibe::session::ControllerKind::None,
  });

  SessionEntry& entry = sessions_.back();
  const bool started = entry.runtime->Start();
  static_cast<void>(started);

  const auto& started_metadata = entry.runtime->record().metadata();
  return SessionSummary{
      .id = started_metadata.id,
      .provider = started_metadata.provider,
      .workspace_root = started_metadata.workspace_root,
      .title = started_metadata.title,
      .status = started_metadata.status,
      .controller_client_id = entry.controller_client_id,
      .controller_kind = entry.controller_kind,
  };
}

auto SessionManager::ListSessions() const -> std::vector<SessionSummary> {
  std::vector<SessionSummary> summaries;
  summaries.reserve(sessions_.size());

  for (const SessionEntry& entry : sessions_) {
    const auto& metadata = entry.runtime->record().metadata();
    summaries.push_back(SessionSummary{
        .id = metadata.id,
        .provider = metadata.provider,
        .workspace_root = metadata.workspace_root,
        .title = metadata.title,
        .status = metadata.status,
        .controller_client_id = entry.controller_client_id,
        .controller_kind = entry.controller_kind,
    });
  }

  return summaries;
}

auto SessionManager::GetSession(const std::string& session_id) const
    -> std::optional<SessionSummary> {
  for (const SessionEntry& entry : sessions_) {
    if (entry.id.value() != session_id) {
      continue;
    }

    const auto& metadata = entry.runtime->record().metadata();
    return SessionSummary{
        .id = metadata.id,
        .provider = metadata.provider,
        .workspace_root = metadata.workspace_root,
        .title = metadata.title,
        .status = metadata.status,
        .controller_client_id = entry.controller_client_id,
        .controller_kind = entry.controller_kind,
    };
  }

  return std::nullopt;
}

auto SessionManager::GetSnapshot(const std::string& session_id) const
    -> std::optional<vibe::session::SessionSnapshot> {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->runtime->record().snapshot();
  }

  return std::nullopt;
}

auto SessionManager::GetTail(const std::string& session_id, const std::size_t bytes) const
    -> std::optional<vibe::session::OutputSlice> {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->runtime->output_buffer().Tail(bytes);
  }

  return std::nullopt;
}

auto SessionManager::GetOutputSince(const std::string& session_id, const std::uint64_t seq) const
    -> std::optional<vibe::session::OutputSlice> {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->runtime->output_buffer().SliceFromSequence(seq);
  }

  return std::nullopt;
}

auto SessionManager::SendInput(const std::string& session_id, const std::string& input) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->runtime->WriteInput(input);
  }

  return false;
}

auto SessionManager::ResizeSession(const std::string& session_id,
                                   const vibe::session::TerminalSize terminal_size) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->runtime->ResizeTerminal(terminal_size);
  }

  return false;
}

auto SessionManager::StopSession(const std::string& session_id) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    const auto status = entry->runtime->record().metadata().status;
    if (status == vibe::session::SessionStatus::Exited ||
        status == vibe::session::SessionStatus::Error) {
      return true;
    }

    return entry->runtime->TerminateAndMarkExited();
  }

  return false;
}

auto SessionManager::RequestControl(const std::string& session_id, const std::string& client_id,
                                    const vibe::session::ControllerKind controller_kind) -> bool {
  if (SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    if (!entry->controller_client_id.has_value()) {
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
    if (!entry->controller_client_id.has_value()) {
      return true;
    }

    if (*entry->controller_client_id != client_id) {
      return false;
    }

    entry->controller_client_id.reset();
    entry->controller_kind = vibe::session::ControllerKind::None;
    return true;
  }

  return false;
}

auto SessionManager::HasControl(const std::string& session_id, const std::string& client_id) const -> bool {
  if (const SessionEntry* entry = FindEntry(session_id); entry != nullptr) {
    return entry->controller_client_id.has_value() && *entry->controller_client_id == client_id;
  }

  return false;
}

void SessionManager::PollAll(const int read_timeout_ms) {
  poll_count_ += 1;
  const bool should_poll_git = (poll_count_ % 100 == 0);

  for (SessionEntry& entry : sessions_) {
    entry.runtime->PollOnce(read_timeout_ms);

    if (should_poll_git && entry.git_inspector) {
      entry.runtime->UpdateGitSummary(entry.git_inspector->Inspect());
    }
  }
}

auto SessionManager::MakeSessionId() const -> std::optional<vibe::session::SessionId> {
  const std::string next_value = "s_" + std::to_string(sessions_.size() + 1);
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

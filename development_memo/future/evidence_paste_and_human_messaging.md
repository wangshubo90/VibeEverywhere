# Evidence, Paste, And Human-Mediated Session Messaging

Status: future roadmap.

This document tightens the next evidence workflow direction around one safety rule:

```text
Sessions do not message each other directly.
Humans use the client to move evidence or text between sessions.
```

That keeps inter-session collaboration useful without creating invisible agent-to-agent control channels.

## Goal

Expand evidence from transient observation results into a persistent workflow object that can be previewed, copied, dragged, and explicitly sent by a human to another session.

The related terminal work is paste ergonomics: large or multiline pasted text should be safe and readable for coding CLIs, especially Codex and Claude.

## Core Principle

Evidence and messaging are host-mediated, client-initiated actions.

```text
Host owns sessions.
Host owns evidence metadata.
Client initiates evidence movement.
Human chooses receiver.
Session receives only normal terminal input.
```

The session runtime should not gain an API that lets one session directly send text to another session. If agents later need collaboration APIs, that should be a separate, audited feature with explicit permissions.

## Product Model

### Evidence

Evidence is a persistent host object.

Examples:

- terminal output selection
- captured log excerpt
- filtered evidence result
- file path
- screenshot
- camera image
- short video clip
- future stream reference

MVP evidence does not need rich media processing. Large files can be viewed in the evidence panel and represented to agents as a copied path or stable evidence reference.

### Message

A message is a human action that sends either:

- inline text
- a host path
- an evidence reference
- a formatted snippet derived from evidence

The receiver is a host session selected by the human from the client UI.

### Scope

Group tags can be the first receiver filter.

Use group tags as collaboration scope, not as strong security authority.

```text
eligible receiver = same host + same group tag + active session
```

Later, this can become explicit workspace/session policy.

## Evidence Data Model

Recommended Core model:

```cpp
enum class EvidenceKind {
    Text,
    TerminalSelection,
    LogExcerpt,
    FilePath,
    Image,
    Video,
    StreamReference
};

struct EvidenceRecord {
    std::string evidence_id;
    std::optional<std::string> source_session_id;
    std::optional<std::string> workspace_id;
    std::vector<std::string> group_tags;
    EvidenceKind kind;
    std::string mime_type;
    uint64_t size_bytes;
    uint64_t created_at_unix_ms;
    std::string title;
    std::string preview_text;
    std::optional<std::filesystem::path> local_path;
    std::optional<std::string> blob_ref;
    std::optional<std::string> content_sha256;
};
```

Initial APIs should be additive:

- `GET /evidence`
- `GET /evidence/{evidence_id}`
- `GET /evidence/{evidence_id}/content`
- `POST /evidence`
- `DELETE /evidence/{evidence_id}` or archive equivalent
- `POST /sessions/{session_id}/messages`

The existing observation/evidence APIs can feed this model, but persistent evidence should become its own concept.

## Storage Location

Open question: evidence payloads can live in the host datadir, workspace datadir, or a configurable external path.

Recommendation:

1. Default to host datadir.
2. Support configurable evidence storage root.
3. Do not write evidence into the workspace by default.

Reasoning:

- host datadir is private runtime state
- workspace writes may pollute git status
- evidence may include cross-workspace or sensitive material
- cleanup and retention are easier from one host-owned location

Suggested layout:

```text
<datadir>/
  evidence/
    records.jsonl
    blobs/
      ab/
        <sha256>
    previews/
```

Config options later:

- `--evidence-dir <path>`
- per-host config field `evidenceStorageRoot`
- retention policy by size, age, or workspace

Workspace-local evidence should be an explicit user action, such as "export evidence to workspace."

## Client UX

### Evidence Panel

The evidence panel should behave like a small inbox/library:

- list evidence items
- preview text or media
- copy text
- copy path
- copy evidence reference
- mark read
- archive/remove
- send to session
- drag evidence into a prompt editor or receiver

### Send To Session

Human-mediated send flow:

1. User selects terminal output, evidence row, file path, or media item.
2. User chooses `Send to Session`.
3. Client shows eligible receiver sessions filtered by group tag.
4. User previews the exact payload.
5. Client calls host API to send terminal input to receiver.
6. Host records an activity/audit event.

No automatic agent-to-agent send in MVP.

Audit event shape:

```cpp
struct HumanMessageEvent {
    std::string event_id;
    std::string actor_client_id;
    std::optional<std::string> source_session_id;
    std::string target_session_id;
    std::optional<std::string> evidence_id;
    std::string payload_kind;
    uint64_t payload_size_bytes;
    uint64_t timestamp_unix_ms;
};
```

## Paste Ergonomics

There are two paste surfaces:

1. Focused terminal.
2. Prompt editor / compose surfaces.

### Focused Terminal

Patch SwiftTerm or our subclass so large and multiline paste enters the terminal as bracketed paste when the terminal has mode 2004 enabled.

Expected byte shape:

```text
ESC [ 200 ~
<paste payload>
ESC [ 201 ~
```

This lets Codex, Claude, and similar CLIs decide how to display pasted content. Many coding CLIs collapse large paste into a placeholder, keeping terminal state clean.

This is terminal correctness, not just UX polish.

### Prompt Editor

The prompt editor is a compose box, so it can offer formatting helpers before sending:

- paste as plain text
- paste as shell single-quoted string
- paste as shell double-quoted string
- paste as triple-quoted block
- paste as markdown code block
- paste as markdown link when payload is a path or URL
- paste as evidence path
- paste as evidence reference

Do not auto-format without user choice for large payloads. Show a preview and let the user pick the format.

Current safety rule should remain:

- strip trailing CR/LF on send so copied terminal selections do not accidentally submit
- preserve internal newlines for intentional multiline prompts

## Drag And Drop

Drag and drop should be client-side only.

Good MVP drag sources:

- selected terminal text
- evidence rows
- evidence preview text
- file path evidence

Good MVP drop targets:

- prompt editor
- focused terminal with confirmation for large payloads
- receiver session picker
- evidence panel import area

When dropping onto another session, use the same human-mediated `Send to Session` flow. The drag gesture is only a UI shortcut.

## API Shape For Human Messaging

Recommended endpoint:

```http
POST /sessions/{target_session_id}/messages
```

Request:

```json
{
  "actorClientId": "ios:<device-id>",
  "sourceSessionId": "s_1",
  "evidenceId": "ev_123",
  "payloadKind": "evidence_path",
  "payload": "/Users/me/.sentrits/evidence/blobs/ab/...",
  "format": "plain",
  "requiresBracketedPaste": true
}
```

Host behavior:

1. Authorize client for control/message action.
2. Verify target session exists.
3. Verify receiver is eligible by same group tag for MVP.
4. Convert payload into terminal input bytes.
5. Use bracketed paste when requested and target terminal mode supports it, or when explicitly configured.
6. Send through normal session input path.
7. Record activity/audit event.

The target session sees normal input only. It does not gain a hidden messaging API.

## MVP Cut

### Core

- persistent evidence metadata in host datadir
- text evidence records
- file path evidence records
- evidence list/detail/content endpoints
- human message endpoint to send inline text or evidence path to a target session
- group-tag receiver eligibility
- activity/audit event for sends

### iOS

- evidence inbox persists across refreshes because it reads host evidence
- evidence detail supports copy text/path
- send evidence/text to same-group session
- receiver picker with exact payload preview
- drag selected evidence into prompt editor

### SwiftTerm

- bracketed paste for large/multiline iOS paste paths
- logging to verify whether paste entered `paste(_:)` or `insertText(_:)`
- no raw multiline paste into active CLI without bracket markers when possible

## Deferred

- agent-initiated messaging
- cross-host messaging
- media streaming
- workspace-local evidence storage by default
- strong policy engine
- encryption-at-rest
- evidence sharing links
- automatic agent consumption of new evidence

## Risks And Guardrails

### Risk: group tags look like security

Group tags are only MVP collaboration scope. Label them clearly in code and UI. Do not call them authorization policy.

### Risk: hidden agent cross-talk

Prevent this by keeping messaging client-initiated and human-confirmed. Sessions cannot call the messaging API as sessions.

### Risk: workspace pollution

Default evidence storage must not write into the repo. Export to workspace should be explicit.

### Risk: accidental terminal submit

Strip trailing CR/LF for prompt sends and use bracketed paste for terminal sends.

### Risk: giant payloads overload clients

Store large payloads on host. Send previews and paths/references to clients by default.

## Implementation Order

1. Persistent evidence records in Core.
2. Evidence list/detail/content APIs.
3. iOS evidence panel reads persistent evidence.
4. SwiftTerm bracketed paste fix for iOS paste paths.
5. Human `Send to Session` API for inline text and evidence path.
6. iOS receiver picker filtered by same group tag.
7. Drag and drop shortcuts.
8. Media evidence as file/path records.

This order keeps the data model stable before adding richer UI gestures.

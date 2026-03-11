# Vibe Session Management – Architecture & Design (v1)

## 1. Overview

Vibe is designed as a **runtime for supervising AI coding sessions**.

It sits between:

- AI coding CLIs (Codex, Claude Code, Aider, etc.)
- the operating system (PTY, filesystem, processes)
- remote clients (mobile, web, CLI)

Its purpose is **not to replace SSH or agent runtimes**, but to provide a **session-aware runtime layer** that enables:

- session discovery
- remote observation
- intervention and control
- progress inference
- notifications and supervision

In essence:

> Vibe acts as a **session runtime and control plane for AI coding agents.**

---

# 2. Core Concept: Session

A **Session** represents one running AI coding process.

Example:

```
codex refactor rendering pipeline
```

Each session corresponds to:

- one PTY
- one main agent process
- zero or more child processes
- a working directory (workspace)

A session is the **primary abstraction** exposed to clients.

Clients never connect to machines — they connect to **sessions**.

---

# 3. System Architecture

```
                  ┌──────────────────────┐
                  │   Mobile Client      │
                  │   Web Client         │
                  │   CLI Client         │
                  └──────────┬───────────┘
                             │
                      Session API / WS
                             │
                    ┌────────▼────────┐
                    │    vibe-hostd   │
                    │ Session Runtime │
                    └────────┬────────┘
                             │
        ┌────────────────────┼─────────────────────┐
        │                    │                     │
    PTY Monitor        Filesystem Watch      Process Monitor
        │                    │                     │
        └───────────────┬────┴────┬────────────────┘
                        │         │
                        ▼         ▼
                   AI Coding CLI / Agent
             (Codex, Claude Code, Aider, etc.)
```

---

# 4. Host Responsibilities (vibe-hostd)

`vibe-hostd` acts as the **Session Runtime**.

Responsibilities include:

### Session lifecycle

- create session
- attach to session
- observe session
- stop session
- clean up session

### IO transport

- PTY byte stream
- terminal output streaming
- input forwarding

### System observability

- filesystem activity
- PTY output rate
- subprocess tree
- CPU/memory usage

### State inference

Host infers high-level session state from system signals.

### Event generation

Host emits structured events used by clients and notifications.

---

# 5. Session Model

Each session tracks:

```
Session {
  id
  workspace
  task
  start_time

  phase
  files_changed
  output_rate
  last_activity

  running_processes
  cpu_usage
  memory_usage

  client_connections
}
```

Sessions are **observable objects** exposed via API and event streams.

---

# 6. Session Phases

The host infers the current session phase using system signals.

```
THINKING
CODING
RUNNING_TASK
WAITING_INPUT
IDLE
COMPLETED
FAILED
DISCONNECTED
```

### Phase descriptions

#### THINKING

Agent producing output but not modifying files.

Signals:

- PTY output active
- filesystem activity low

---

#### CODING

Agent actively modifying files.

Signals:

- filesystem writes detected
- multiple files changing

---

#### RUNNING_TASK

Agent waiting for subprocess completion.

Examples:

- running tests
- compiling
- long builds

Signals:

- subprocess tree active
- CPU high
- filesystem mostly idle

---

#### WAITING_INPUT

Agent blocked waiting for user decision.

Examples:

```
Continue? (y/n)
Approve changes?
```

Signals:

- PTY idle
- prompt detection
- no file activity

---

#### IDLE

No activity detected.

Signals:

- no PTY output
- no filesystem writes
- no subprocess activity

---

#### COMPLETED

Agent finished the task.

---

#### FAILED

Agent process exited with failure.

---

#### DISCONNECTED

Session transport lost or host unreachable.

---

# 7. Signal Sources

Session state is inferred using **provider-agnostic signals**.

The runtime does **not rely on agent APIs**.

Primary signals:

## PTY Activity

Measures:

- bytes/sec
- lines/sec
- last output timestamp

Used to detect:

- thinking
- explanation generation
- active output

---

## Filesystem Activity

Monitored via OS APIs:

- inotify (Linux)
- FSEvents (macOS)
- ReadDirectoryChangesW (Windows)

Tracked metrics:

- files modified
- files created
- directories touched

Used to detect:

- coding activity
- refactors
- file generation

---

## Process Tree

Tracks:

- child processes
- spawned commands

Examples:

```
codex
 ├─ pytest
 ├─ gcc
 └─ npm
```

Used to detect:

- long-running tasks
- build/test execution

---

## Resource Usage

Optional signals:

- CPU usage
- memory usage
- IO activity

Used for:

- abnormal resource detection
- heavy workloads

---

# 8. Session Event System

The host emits structured **session events**.

Examples:

```
SESSION_STARTED
SESSION_COMPLETED
SESSION_FAILED
SESSION_DISCONNECTED
SESSION_WAITING_INPUT
SESSION_IDLE
SESSION_ACTIVITY
SESSION_RESOURCE_ALERT
SESSION_FILES_CHANGED
```

Clients subscribe to these events via WebSocket.

---

# 9. Session Watch

Clients can **watch sessions**.

Watching a session means:

> Notify me when attention is required.

Watch scopes:

- watch this session
- watch selected sessions
- watch all active sessions

Initial version may only support:

```
watch(session_id)
```

---

# 10. Notification System

Notifications are triggered by session events.

High-value notifications:

### Waiting for input

```
Agent is waiting for approval
```

---

### Session completed

```
Task finished
```

---

### Session failed

```
Agent failed
```

---

### Disconnected

```
Session connection lost
```

---

Optional notifications:

### Idle too long

```
Session idle for > N minutes
```

---

### Long-running task

```
Build/test running too long
```

---

Resource alerts are optional and lower priority.

---

# 11. Client Responsibilities

Clients do **not perform session inference**.

Clients simply:

- subscribe to session events
- render session state
- send user actions

Clients may include:

- mobile app
- web UI
- CLI tool

---

# 12. Client Capabilities

Clients can:

### Discover sessions

```
GET /sessions
```

---

### Attach to session

```
attach(session_id)
```

Provides:

- terminal stream
- session state

---

### Observe session

Read-only mode.

---

### Take control

Send terminal input.

---

### Watch session

Enable notifications.

---

# 13. Input Modes (Client Side)

Clients support two input modes.

## Terminal Mode

Interactive control.

Input is sent as **raw PTY signals**.

Examples:

- arrow keys
- Ctrl+C
- Esc
- Tab

Used for:

- navigation
- interactive shell control
- agent prompts

---

## Prompt Mode

Text editing mode.

User composes text locally, then sends:

```
prompt + newline
```

Used for:

- instructions
- questions
- follow-up tasks

---

# 14. Suggestion System (Client)

Clients may provide lightweight suggestions.

Sources:

- recent PTY output
- file names
- variables
- commands
- paths

Matching:

- simple fuzzy matching
- local token matching

Suggestions are UI-level features and do not affect runtime logic.

---

# 15. Design Principles

### Provider-agnostic

The runtime does not depend on:

- Codex API
- Claude API
- agent internals

Instead it relies on:

```
PTY
Filesystem
Processes
```

---

### Session-first abstraction

Users interact with **sessions**, not machines.

---

### Observability-first design

Primary value:

```
observe
intervene
supervise
```

not merely:

```
terminal access
```

---

# 16. Product Interpretation

Vibe is best understood as:

> A runtime for supervising AI coding sessions.

Comparable conceptual layers:

| System | Runtime |
|------|------|
| Docker | container runtime |
| Kubernetes | container orchestration |
| Vibe | agent session runtime |

---

# 17. Future Extensions

Possible future directions:

### Multi-session dashboard

Observe multiple agents simultaneously.

---

### Timeline view

Example:

```
10:01 session started
10:03 edited renderer.cpp
10:05 running tests
10:08 tests failed
```

---

### Multi-agent orchestration

```
Agent A: refactor
Agent B: tests
Agent C: documentation
```

---

### Team collaboration

Multiple observers per session.

---

# 18. Summary

Vibe provides:

```
AI agent
     │
PTY / FS / Process signals
     │
Session Runtime (vibe-hostd)
     │
Session events + inference
     │
Clients (mobile/web/cli)
```

The runtime enables:

- session management
- observability
- remote supervision
- intervention
- notifications

Without requiring changes to existing AI coding CLIs.
# Sentrits

<p align="center">
  <img src="assets/Sentrits-logo.png" alt="Sentrits logo" width="220" />
</p>

**Persistent CLI coding sessions across devices — with supervision, control handoff, and real execution continuity.**

Sentrits is a session runtime and control plane for AI coding CLIs.

It keeps real Codex, Claude Code, Aider, Gemini CLI, and similar terminal workflows alive on a host machine while letting you:

- attach and detach without losing the running session
- observe the same session from multiple devices
- hand control between desktop, web, and mobile clients
- reconnect with useful terminal state instead of waiting for luck or repaint timing

Sentrits is built for **supervision and intervention**, not for turning your phone into a full IDE.

---

## Why Sentrits Exists

Modern AI coding workflows are no longer simple request/response interactions.

They are often:

- long-running
- stateful
- CLI-native
- spread across build/test/fix loops
- worth watching even when you are away from your desk

Today, most tools solve only part of that problem:

- **SSH** gives you machine access, but not a session-aware supervision model
- **cloud/web AI tools** preserve conversation and context, but usually not the exact live execution environment you started locally
- **Sentrits** focuses on a different layer: keeping the **actual CLI execution** alive, inspectable, and controllable across devices

In short:

> SSH preserves access.  
> Claude/Cursor-style remote experiences preserve context.  
> **Sentrits preserves the running session itself.**

---

## What Sentrits Does Today

Sentrits currently provides:

- **persistent PTY-backed sessions**
  - start once, reattach later, keep the live process running

- **multi-observer, single-controller runtime**
  - many clients may watch a session
  - exactly one principal owns terminal input at a time

- **cross-device control handoff**
  - switch active control between host, web, and iOS without restarting the workflow

- **pairing and host identity**
  - discover and trust hosts, then reconnect to known sessions

- **session snapshot and bootstrap state**
  - reconnect with useful state instead of relying entirely on fresh repaint behavior from the app inside the PTY

- **CLI-agnostic compatibility**
  - works with real terminal-based tools rather than one provider’s private execution model

- **coarse runtime supervision**
  - activity/quiet state, session updates, and attention-oriented signals

- **across-device agent session network**
  - the architecture already supports a network of real running agent sessions across devices and hosts
  - current work is focused on making that network more legible through stronger session-node, supervision, and semantic representation

---

## What Makes Sentrits Different

| System | Primary abstraction | What it preserves |
|---|---|---|
| SSH | machine connection | remote access |
| tmux | terminal multiplexing | terminal continuity on one host |
| Claude/Cursor remote-style workflows | model/context continuity | cloud/web/mobile AI context |
| **Sentrits** | **persistent session runtime** | **live CLI execution across devices** |

Sentrits is not another remote shell.

It is a **session system** for long-lived CLI execution.

---

## Typical Use Cases

### Continue a coding session from your phone
- Start a Codex or Claude Code session on your laptop
- Leave your desk
- Open the same session on iPhone
- Observe progress
- Send a short command
- Take control when needed

### Watch long-running work without staying attached
- Start a test/build/fix loop
- Detach
- Reattach later from web or mobile
- Keep the same live process and recent terminal state

### Observe first, intervene only when needed
- Use mobile as a supervision device
- Avoid heavy editing on small screens
- Step in only for short commands, approvals, or recovery

### Manage multiple active sessions
- inspect session inventory
- reconnect to focused sessions
- observe without disturbing the active controller

---

## Product Shape

Sentrits is centered on:

- one daemon-managed PTY-backed runtime per session
- host-local CLI management and low-latency local attach/control
- remote observe and remote control through REST and WebSocket APIs
- session supervision data that remains useful even when no client is actively attached

Clients do not connect to machines as their primary abstraction.

They connect to **sessions** exposed by a host runtime.

---

## Architecture At A Glance

```text
                  ┌─────────────────────────────────────┐
                  │              Clients                │
                  │                                     │
                  │  CLI         Web         iOS        │
                  │  local       remote      remote     │
                  │  attach      observe/    observe/   │
                  │  observe     control     control    │
                  └────────────────┬────────────────────┘
                                   │
                      REST + WebSocket observer/control
                                   │
                     ┌─────────────▼─────────────┐
                     │      sentrits daemon      │
                     │                           │
                     │ session manager           │
                     │ auth + pairing            │
                     │ inventory + snapshots     │
                     │ terminal multiplexer      │
                     │ supervision + attention   │
                     └─────────────┬─────────────┘
                                   │
                            one PTY per session
                                   │
                      ┌────────────▼────────────┐
                      │ AI coding CLI process   │
                      │ Codex / Claude / Aider  │
                      └────────────┬────────────┘
                                   │
                    PTY output + workspace/git/process signals
```

More detail:

- `development_memo/system_architecture.md`
- `development_memo/architecture_refined.md`

---

## Observe / Control Model

Sentrits uses one real PTY per live session.

The key rules are:

- many observers may attach
- one active controller owns terminal input
- the active controller owns PTY resize
- host-local attach is a privileged low-latency control path
- remote active control uses a dedicated controller WebSocket
- canonical snapshot/bootstrap data helps clients reconnect cleanly

This preserves PTY correctness while still allowing:

- remote supervision
- focused intervention
- cross-device continuity
- truthful session inventory

---

## Maintained Repos

Core runtime:

- `Sentrits-Core` — this repository

Maintained clients:

- Web: `https://github.com/shubow-sentrits/Sentrits-Web`
- iOS: `https://github.com/shubow-sentrits/Sentrits-IOS`

---

## Current Scope

Sentrits is currently focused on:

- persistent PTY-backed session runtime
- cross-device observe/control
- pairing, identity, and inventory
- mobile-first supervision rather than heavy editing
- packaging the daemon/runtime as the product core

Sentrits is **not** currently trying to solve:

- internet relay/brokered connectivity as a first-class managed service
- full IDE-style editing on mobile
- perfect semantic understanding of every terminal workflow
- complete multi-user account systems

---

## Platform Direction

Current packaging direction is daemon-first.

Target packaging shape:

- daemon/runtime as the product core
- CLI as the operator-facing management surface
- static web assets built separately and served by the runtime
- macOS
- Linux (Debian first)
- WSL/Ubuntu

See:

- `development_memo/packaging_architecture.md`

---

## Start Here

- `get_started.md`
- `VIBING.md`
- `development_memo/README.md`
- `development_memo/system_architecture.md`
- `development_memo/api_and_event_schema.md`
- `development_memo/mvp_checklist.md`
- `development_memo/known_limitations.md`

---

## Longer-Term Direction

Near-term, Sentrits is a daemon-managed PTY runtime with pairing, inventory, observe/control, and cross-client continuity.

Longer-term, the system is moving toward:

- richer session-node information
- stronger supervision and attention models
- better notifications and watch workflows
- semantic extraction beyond raw terminal activity
- eventually, more agent-oriented session orchestration built on persistent CLI execution

Relevant directional docs:

- `development_memo/future/session_terminal_multiplexer_and_semantic_runtime.md`
- `development_memo/future/session_signal_map.md`
- `development_memo/future/pty_semantic_extractor.md`

These future docs are directional and should not be read as claims about the full current implementation.

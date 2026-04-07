# Sentrits Session Signal Map

## 1. Purpose

This document defines the **signal model** used by sentrits to infer session lifecycle and attention state.

The goal is to unify signals coming from different sources:

- PTY stream
- filesystem
- process tree
- git workspace
- controller events
- client attachment
- system metrics

These signals are then transformed into:

- lifecycle status
- attention state
- session observability data

The model is designed to remain **provider-agnostic** and **terminal-agnostic**.

---

# 2. Signal Layers

Signals are grouped into five layers.

```
system signals
    ↓
derived metrics
    ↓
session activity model
    ↓
lifecycle inference
    ↓
attention inference
```

Each layer should be clearly separated in implementation.

---

# 3. Signal Sources

## 3.1 PTY Signals

Source:
```
pty master stream
```

Collected fields:

```
ptyBytesOut
ptyBytesIn
ptyLinesOut
ptyLastOutputAt
ptyLastInputAt
```

Meaning:

| Signal | Description |
|------|-------------|
ptyBytesOut | raw stdout bytes from the session process |
ptyBytesIn | raw stdin bytes sent to session |
ptyLinesOut | newline-delimited output lines |
ptyLastOutputAt | timestamp of last stdout event |
ptyLastInputAt | timestamp of last user input |

Important:

PTY output is **unstructured** and should not be interpreted aggressively.

Use it primarily for:

- activity detection
- idle timing
- output bursts

Not for semantic understanding in v1.

---

## 3.2 Filesystem Signals

Source:

```
workspace filesystem watcher
```

Collected fields:

```
lastFileChangeAt
recentFileChangeCount
recentFilePaths
recentFileExtensions
```

Meaning:

| Signal | Description |
|------|-------------|
lastFileChangeAt | timestamp of last file modification |
recentFileChangeCount | number of changed files in rolling window |
recentFilePaths | sample of modified files |
recentFileExtensions | extension distribution |

Uses:

- coding activity detection
- workspace mutation detection
- info attention triggers

---

## 3.3 Process Signals

Source:

```
session root process tree
```

Collected fields:

```
isProcessAlive
rootPid
subprocessCount
recentSubprocessSpawnCount
runningCommand
lastSubprocessSpawnAt
```

Meaning:

| Signal | Description |
|------|-------------|
isProcessAlive | main agent process alive |
subprocessCount | number of active subprocesses |
recentSubprocessSpawnCount | spawns within time window |
runningCommand | current foreground command if detectable |
lastSubprocessSpawnAt | last spawn event |

Uses:

- running task detection
- build/test detection
- stall detection

---

## 3.4 Git Signals

Source:

```
workspace git repository
```

Collected fields:

```
gitBranch
gitDirty
gitDirtyChangedAt
gitHeadChangedAt
```

Meaning:

| Signal | Description |
|------|-------------|
gitBranch | current branch |
gitDirty | working tree dirty |
gitDirtyChangedAt | dirty state transition |
gitHeadChangedAt | commit transition |

Uses:

- workspace change detection
- coding phase hints
- informational attention events

---

## 3.5 Controller Signals

Source:

```
sentrits control layer
```

Collected fields:

```
controllerKind
controllerClientId
lastControllerChangeAt
recentAttachDetachCount
lastAttachAt
lastDetachAt
```

controllerKind:

```
none
local
remote
```

Meaning:

| Signal | Description |
|------|-------------|
controllerKind | who currently has control |
controllerClientId | controlling client id |
lastControllerChangeAt | control handoff timestamp |
recentAttachDetachCount | attach/detach churn |
lastAttachAt | last client attach |
lastDetachAt | last client detach |

Uses:

- collaboration detection
- connection instability detection
- info attention triggers

---

## 3.6 Lifecycle Signals

Source:

```
session runtime
```

Collected fields:

```
lifecycleStatus
lastLifecycleTransitionAt
exitCode
```

Possible values:

```
Running
AwaitingInput
Exited
Error
Disconnected
```

Uses:

- base truth for attention
- client UI rendering
- notification eligibility

---

## 3.7 Resource Signals (Optional)

Source:

```
system metrics
```

Collected fields:

```
cpuPercent
memoryBytes
networkRxBytes
networkTxBytes
```

Uses:

- future anomaly detection
- diagnostics
- advanced supervision

These should not drive v1 attention decisions.

---

# 4. Derived Metrics

Signals alone are not used directly.

They must first be converted into derived metrics.

---

## 4.1 Idle Durations

```
outputIdleMs = now - ptyLastOutputAt
inputIdleMs = now - ptyLastInputAt
fileIdleMs = now - lastFileChangeAt
controllerIdleMs = now - lastControllerChangeAt
```

These durations are the most important inference inputs.

---

## 4.2 Activity Flags

```
hasRecentOutput
hasRecentInput
hasRecentFileChange
hasRecentSubprocessSpawn
hasRecentControllerChange
```

Recommended windows:

```
recentOutputWindow = 15s
recentFileWindow = 30s
recentProcessWindow = 30s
```

---

## 4.3 Workspace Activity Score

Optional heuristic:

```
workspaceActivityScore =
    fileChangeWeight * recentFileChangeCount
  + gitChangeWeight * gitTransitions
```

Used later for coding detection.

---

## 4.4 Session Quietness

```
sessionQuiet =
    outputIdleMs > quietThreshold
    AND fileIdleMs > quietThreshold
```

Recommended threshold:

```
quietThreshold = 120s
```

---

# 5. Session Activity Model

The system may optionally infer a coarse activity phase:

```
Thinking
Coding
RunningTask
WaitingInput
Idle
```

Signals used:

| Phase | Signals |
|------|--------|
Thinking | frequent PTY output but no file change |
Coding | recent filesystem activity |
RunningTask | subprocess spawn + output |
WaitingInput | lifecycle AwaitingInput |
Idle | quiet session with no activity |

This phase is **optional** but useful for UI.

---

# 6. Lifecycle Inference

Lifecycle is mostly determined by runtime events.

Mapping:

```
process alive → Running
runtime waiting state → AwaitingInput
process exit 0 → Exited
process exit nonzero → Error
host lost process → Disconnected
```

Lifecycle should never depend on PTY parsing.

---

# 7. Attention Inference Inputs

Attention inference uses these derived inputs:

```
lifecycleStatus
outputIdleMs
fileIdleMs
recentFileChangeCount
recentAttachDetachCount
controllerKind
gitDirtyChangedAt
lastLifecycleTransitionAt
```

PTY raw text should **not** be used in v1.

---

# 8. Signal Reliability Ranking

Signals should be weighted by reliability.

```
1 lifecycle signals
2 process signals
3 filesystem signals
4 controller signals
5 git signals
6 PTY signals
7 resource metrics
```

PTY text interpretation should be considered **least reliable**.

---

# 9. Example Signal Flow

Example: agent editing files

Signals:

```
pty output spike
filesystem changes
git dirty
```

Derived:

```
hasRecentFileChange = true
```

Inference:

```
activityPhase = Coding
attention = info
reason = workspace_changed
```

---

Example: agent waiting for approval

Signals:

```
lifecycleStatus = AwaitingInput
pty quiet
```

Inference:

```
attention = action_required
reason = awaiting_input
```

---

Example: stalled build

Signals:

```
process alive
no output
no file changes
cpu low
long idle
```

Inference:

```
attention = intervention
reason = stalled_no_progress
```

---

# 10. Implementation Guidance

Signal collection should be implemented in **separate modules**.

Recommended components:

```
pty_monitor
filesystem_monitor
process_monitor
git_monitor
controller_monitor
resource_monitor
```

Each module emits structured events.

These events update the session signal state.

A separate component performs:

```
derived metrics computation
attention inference
lifecycle reconciliation
```

This separation prevents tight coupling.

---

# 11. Evolution Plan

The signal map should evolve in stages.

Phase 1:

```
PTY activity
filesystem watcher
lifecycle
controller events
```

Phase 2:

```
process tree tracking
git state tracking
idle detection
```

Phase 3:

```
resource monitoring
network monitoring
advanced stall detection
```

Phase 4:

```
provider-specific signal enrichments
PTY semantic hints
tool usage detection
```

---

# 12. Key Design Principle

Signals must remain **generic and provider-agnostic**.

The system should work equally with:

```
Claude CLI
Gemini CLI
Codex CLI
Aider
future agents
```

No single provider's terminal output format should be required.

The system should infer behavior from **system-level signals**, not **LLM-specific APIs**.

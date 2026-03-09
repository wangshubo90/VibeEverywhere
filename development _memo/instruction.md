# Vibe Coding Remote Session System
## Architecture Design Document (Draft for AI-assisted Design)

Author: Shubo Wang

This document describes the architecture for a system that allows mobile or web clients to attach to coding CLI sessions running on a developer machine.

The goal is NOT remote desktop.

The goal is NOT SSH terminal mirroring.

The goal is to remotely observe and interact with AI coding sessions (Codex CLI, Claude CLI, etc.).

This document defines the architecture and responsibilities of the system components.

The document is meant to guide AI agents (Codex / Claude) to refine architecture and produce implementation plans, not to immediately generate code.

---

# 1 System Overview

The system enables a mobile or web client to connect to coding sessions running on a developer machine within a local network.

A coding session is typically an interactive CLI such as:

- Codex CLI
- Claude Code CLI
- Other AI coding agents

These CLIs typically run inside a terminal and maintain a conversational context with the codebase.

The system must allow a remote client to:

- view terminal output
- send input to the CLI
- observe file changes
- observe git state
- observe session status

without remote desktop streaming.

---

# 2 Core Design Principle

The system revolves around sessions, not machines.

Client connects to a session, not directly to the machine.

A host machine may run multiple sessions simultaneously.

---

# 3 Key Design Decisions

## Session is bound to a workspace

Each session is associated with a workspace directory.

session = workspace + provider

Example

workspace

~/projects/myapp

session

workspace: ~/projects/myapp  
provider: codex  

This allows the system to:

- monitor file changes
- track git state
- generate diffs
- maintain stable session metadata

The CLI may still change directories internally, but the workspace root remains the monitoring root.

---

## Session persistence model

The system implements **lightweight recovery**.

Session metadata persists across host restart.

However the CLI process does NOT survive restart.

Example after restart

session: "refactor-ui"

status: exited

workspace: ~/projects/myapp

The user may choose to start a new session.

Full terminal state recovery is NOT required.

---

# 4 System Components

## 4.1 Host Daemon

Name

vibe-hostd

This is a persistent service running on the developer machine.

Responsibilities

- manage coding sessions
- spawn CLI processes
- maintain PTY connections
- provide REST API
- provide WebSocket event stream
- manage device pairing
- track workspace changes
- track git state
- maintain session metadata

---

## 4.2 Client

Initial implementation

Web client

Future clients

- iOS
- Android

Responsibilities

- list hosts
- list sessions
- attach to session
- display terminal
- send input
- view changed files
- view git state
- display session state

---

## 4.3 Session Runtime

Each coding session is managed by a Session Runtime object.

A session corresponds to one CLI process running inside a pseudo terminal.

Session runtime stores

- session id
- provider type
- workspace root
- process id
- PTY file descriptor
- terminal buffer
- recent file changes
- git state
- pending approvals (future)
- current status

---

# 5 Session Lifecycle

Session states

Created  
Starting  
Running  
AwaitingInput  
Exited  
Error  

Transitions

Created -> Starting -> Running  
Running -> AwaitingInput  
AwaitingInput -> Running  
Running -> Exited  
Running -> Error  

---

# 6 PTY Execution Model

Each session runs a CLI inside a pseudo terminal.

Example command

codex

or

claude

The host daemon does

1 create pty  
2 fork process  
3 attach child stdin stdout stderr to pty slave  
4 parent reads pty master  
5 parent forwards output to clients  

All CLI interaction happens via the PTY.

The system does NOT depend on official CLI APIs.

---

# 7 Provider Adapter

Providers define how a session is launched.

Example providers

Codex  
Claude  

Provider adapter defines

providerType  
executable  
defaultArgs  
environmentVariables  

Example

Codex adapter

executable: codex  

Claude adapter

executable: claude  

Adapters may optionally define output parsers in future versions.

---

# 8 Communication Model

The host daemon exposes two interfaces.

REST API

and

WebSocket event stream.

---

# 9 REST API (Initial Draft)

Host info

GET /host/info

Session list

GET /sessions

Create session

POST /sessions

Session detail

GET /sessions/{sessionId}

Send input

POST /sessions/{sessionId}/input

Resize terminal

POST /sessions/{sessionId}/resize

Stop session

POST /sessions/{sessionId}/stop

Session snapshot

GET /sessions/{sessionId}/snapshot

File changes

GET /sessions/{sessionId}/changes

Git summary

GET /sessions/{sessionId}/git

---

# 10 WebSocket Event Stream

Clients subscribe to event streams.

Example events

session.updated  
terminal.output  
files.changed  
git.changed  
session.exited  

Example message

{
 type: terminal.output  
 sessionId: abc  
 data: "Running tests..."  
}

---

# 11 File Monitoring

The host daemon monitors workspace files.

Possible implementation

- inotify (Linux)
- FSEvents (macOS)
- cross platform file watcher

Events

create  
modify  
delete  

Events are aggregated before sending to clients.

---

# 12 Git State Monitoring

The host daemon periodically inspects git status.

Information

- current branch
- staged files
- modified files
- untracked files
- ahead/behind status

---

# 13 Multi Client Behavior

Multiple clients may attach to a session.

Design constraint

multiple observers allowed

only one active controller

Controller rules

only controller can send input

others are read only

Controller may transfer control.

---

# 14 Device Pairing

Device pairing occurs at host level.

Connection occurs at session level.

Pairing process

Client requests pairing

Host generates pairing code

Client confirms pairing

Device is registered

After pairing

client can discover host and list sessions.

---

# 15 Session Attachment

Client attaches using sessionId.

Client receives

- terminal output
- file changes
- git state
- session status

Client may send input if it has control permission.

---

# 16 Input Model

All inputs are terminal inputs.

Example inputs

fix this bug  
run tests  
git status  

The system does not distinguish natural prompt vs shell command.

The CLI handles interpretation.

---

# 17 Initial UI Model (Web Client)

Session view contains

Terminal panel  
Changed files panel  
Git state panel  
Session summary  
Input box  

---

# 18 Security Model

Initial version targets local network usage.

Requirements

TLS communication  
paired device authentication  
session level authorization  

future extension may support WAN access.

---

# 19 Language Choice

Host daemon may be implemented in

C++  
Rust  
Go  

C++ is acceptable.

Possible libraries

boost asio  
websocketpp  
cpp httplib  

---

# 20 Internal Architecture of vibe-hostd

The host daemon consists of several internal modules.

Architecture diagram

```
                +----------------------+
                |      Web Client      |
                +----------+-----------+
                           |
                           |
                    WebSocket / REST
                           |
                           v
                +----------------------+
                |      API Layer       |
                |  REST + WebSocket    |
                +----------+-----------+
                           |
                           v
                 +--------------------+
                 |    SessionManager  |
                 +---------+----------+
                           |
            +--------------+--------------+
            |                             |
            v                             v
     +-------------+              +---------------+
     |  PTYManager |              |  SnapshotStore |
     +------+------+              +-------+-------+
            |                             |
            v                             |
     +--------------+                     |
     | ProviderExec |                     |
     +------+-------+                     |
            |                             |
            v                             |
     +--------------+                     |
     |  CLI Process |                     |
     +--------------+                     |
                                          |
            +-----------------------------+
            |
            v
       +----------+
       | EventBus |
       +----+-----+
            |
      +-----+--------+------------------+
      |              |                  |
      v              v                  v
 FileWatcher     GitInspector      AuthManager
```

---

# 21 Module Responsibilities

## SessionManager

Central session registry.

Responsibilities

- create session
- stop session
- attach clients
- track session state
- maintain session metadata

---

## PTYManager

Responsible for terminal execution.

Responsibilities

- create PTY
- fork CLI process
- stream output
- receive input
- handle resize

---

## ProviderExec

Responsible for launching CLI providers.

Example

codex  
claude  

Defines executable and environment.

---

## EventBus

Internal event routing system.

Events include

terminal output  
session state change  
file change  
git update  

Subscribers

API layer  
clients  
logging

---

## FileWatcher

Monitors workspace file changes.

Emits events

file modified  
file created  
file deleted  

---

## GitInspector

Periodically inspects git state.

Provides

branch  
modified files  
staged files  
untracked files  

---

## SnapshotStore

Persists lightweight session metadata.

Includes

session title  
workspace  
last activity  
recent events  

Used for lightweight recovery.

---

## AuthManager

Handles device pairing and authentication.

Responsibilities

- device registration
- token validation
- session permission checks

---

# 22 Data Flow Example

User input flow

Client -> REST API -> SessionManager -> PTYManager -> CLI

Terminal output flow

CLI -> PTYManager -> EventBus -> WebSocket -> Client

File change flow

FileWatcher -> EventBus -> WebSocket -> Client

Git state flow

GitInspector -> EventBus -> WebSocket -> Client

---

# 23 Non Goals (Initial Version)

The system does NOT aim to

support attaching to existing terminal sessions

provide full IDE functionality

edit files remotely

replace local development environment

---

# 24 Next Task for AI Agent

The AI agent should refine

1 detailed module interfaces  
2 session runtime data structures  
3 PTY management design  
4 event bus implementation strategy  
5 REST API schema  
6 WebSocket event schema  
7 workspace monitoring strategy  

The AI agent should produce

detailed architecture diagrams

and

development milestones.

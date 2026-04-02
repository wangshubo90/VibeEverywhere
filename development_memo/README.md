# Development Memo Index

This directory now tracks the implementation-facing documents that still reflect the current runtime and client architecture.

## Current Product Baseline

Current maintained baseline:

- runtime:
  - one PTY per session
  - many observers and one active controller
  - privileged local host controller lane
  - dedicated remote controller WebSocket for web and iOS
  - supervision state exposed as `active`, `quiet`, or `stopped`
  - stable persisted `hostId` is the canonical host identity; `displayName` is descriptive only and may duplicate across hosts
- web:
  - focused view is the live control surface
  - explorer mini views are preview-only
- iOS:
  - Pairing, Inventory, Explorer, Activity, and Config tabs are live
  - focused session view is the only interactive terminal surface
  - local client-side notifications support subscribed `quiet` and `stopped` events

If a document disagrees with the running code, the code is the source of truth.

## Core Runtime Docs

- [architecture_refined.md](Sentrits-Core/development_memo/architecture_refined.md)
  - current runtime component boundaries and control model
- [session_runtime_and_pty.md](Sentrits-Core/development_memo/session_runtime_and_pty.md)
  - PTY ownership, output buffering, replay, and control semantics
- [api_and_event_schema.md](Sentrits-Core/development_memo/api_and_event_schema.md)
  - current REST and WebSocket contract, including the remote controller lane
- [build_and_test.md](Sentrits-Core/development_memo/build_and_test.md)
  - canonical local build and test workflow
- [packaging_architecture.md](Sentrits-Core/development_memo/packaging_architecture.md)
  - current host packaging and bundled-client direction
- [cli_vnext_plan.md](Sentrits-Core/development_memo/cli_vnext_plan.md)
  - planned headless-friendly CLI structure for runtime administration
- [known_limitations.md](Sentrits-Core/development_memo/known_limitations.md)
  - current documented limitations in attach rendering, host reclaim behavior, and first-frame fidelity

## Current Client Docs

- [client_api_ios.md](Sentrits-Core/development_memo/client_api_ios.md)
- [ios_client_architecture.md](Sentrits-Core/development_memo/ios_client_architecture.md)
- [remote_web_client_v1.md](Sentrits-Core/development_memo/remote_web_client_v1.md)
- [privileged_remote_controller_plan.md](Sentrits-Core/development_memo/privileged_remote_controller_plan.md)

## Historical Or Still-Useful Reference Docs

- [implementation_plan.md](Sentrits-Core/development_memo/implementation_plan.md)
- [implementation_milestones.md](Sentrits-Core/development_memo/implementation_milestones.md)
- [mvp_checklist.md](Sentrits-Core/development_memo/mvp_checklist.md)
- [frontend_workspace.md](Sentrits-Core/development_memo/frontend_workspace.md)
- [frontend_strategy.md](Sentrits-Core/development_memo/frontend_strategy.md)
- [active_discovery_plan.md](Sentrits-Core/development_memo/active_discovery_plan.md)
- [session_attention_inference_v1.md](Sentrits-Core/development_memo/session_attention_inference_v1.md)

## Future Refactor Drafts

- [future/session_terminal_multiplexer_and_semantic_runtime.md](Sentrits-Core/development_memo/future/session_terminal_multiplexer_and_semantic_runtime.md)
  - target-state refactor from shared PTY fanout to stdin ownership, multiplexed terminal views, replay, and semantic session state
- [future/session_terminal_multiplexer_v1.md](Sentrits-Core/development_memo/future/session_terminal_multiplexer_v1.md)
  - implementation-facing v1 multiplexer plan: one stdin owner, one canonical headless emulator, many observer viewports, current screen plus bounded scrollback
- [future/session_terminal_multiplexer_wire_compatibility.md](Sentrits-Core/development_memo/future/session_terminal_multiplexer_wire_compatibility.md)
  - additive migration rules that preserve current REST and WebSocket contracts for Sentrits-Web and Sentrits-IOS during rollout
- [future/pty_semantic_extractor.md](Sentrits-Core/development_memo/future/pty_semantic_extractor.md)
  - draft semantic extraction layer for terminal-derived hints

## Notes

- Deprecated daemon-served plain HTML browser assets now live under [deprecated/](Sentrits-Core/deprecated/README.md).
- The maintained browser remote client is no longer in this repo. It lives in `Sentrits-Web`.
- The maintained iOS client lives in `Sentrits-IOS`.
- Remove or rewrite docs here when code changes invalidate them; implementation should remain the ground truth.

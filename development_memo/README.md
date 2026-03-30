# Development Memo Index

This directory now tracks the implementation-facing documents that still reflect the current runtime and client architecture.

## Core Runtime Docs

- [architecture_refined.md](/Users/shubow/dev/VibeEverywhere/development_memo/architecture_refined.md)
  - current runtime component boundaries and control model
- [session_runtime_and_pty.md](/Users/shubow/dev/VibeEverywhere/development_memo/session_runtime_and_pty.md)
  - PTY ownership, output buffering, replay, and control semantics
- [api_and_event_schema.md](/Users/shubow/dev/VibeEverywhere/development_memo/api_and_event_schema.md)
  - current REST and WebSocket contract, including the remote controller lane
- [build_and_test.md](/Users/shubow/dev/VibeEverywhere/development_memo/build_and_test.md)
  - canonical local build and test workflow
- [packaging_architecture.md](/Users/shubow/dev/VibeEverywhere/development_memo/packaging_architecture.md)
  - current host packaging and bundled-client direction
- [known_limitations.md](/Users/shubow/dev/VibeEverywhere/development_memo/known_limitations.md)
  - current documented limitations in attach rendering and host reclaim behavior

## Current Client Docs

- [client_api_ios.md](/Users/shubow/dev/VibeEverywhere/development_memo/client_api_ios.md)
- [ios_client_architecture.md](/Users/shubow/dev/VibeEverywhere/development_memo/ios_client_architecture.md)
- [remote_web_client_v1.md](/Users/shubow/dev/VibeEverywhere/development_memo/remote_web_client_v1.md)
- [privileged_remote_controller_plan.md](/Users/shubow/dev/VibeEverywhere/development_memo/privileged_remote_controller_plan.md)

## Historical Or Still-Useful Reference Docs

- [implementation_plan.md](/Users/shubow/dev/VibeEverywhere/development_memo/implementation_plan.md)
- [implementation_milestones.md](/Users/shubow/dev/VibeEverywhere/development_memo/implementation_milestones.md)
- [mvp_checklist.md](/Users/shubow/dev/VibeEverywhere/development_memo/mvp_checklist.md)
- [frontend_workspace.md](/Users/shubow/dev/VibeEverywhere/development_memo/frontend_workspace.md)
- [frontend_strategy.md](/Users/shubow/dev/VibeEverywhere/development_memo/frontend_strategy.md)
- [active_discovery_plan.md](/Users/shubow/dev/VibeEverywhere/development_memo/active_discovery_plan.md)
- [session_attention_inference_v1.md](/Users/shubow/dev/VibeEverywhere/development_memo/session_attention_inference_v1.md)

## Notes

- Deprecated daemon-served plain HTML browser assets now live under [deprecated/](/Users/shubow/dev/VibeEverywhere/deprecated/README.md).
- The maintained browser remote client is no longer in this repo. It lives in `/Users/shubow/dev/VibeEverywhere-Client`.
- Remove or rewrite docs here when code changes invalidate them; implementation should remain the ground truth.

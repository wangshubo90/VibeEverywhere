# Development Memo Index

This folder contains initiative documents and the implementation-ready design set for VibeEverywhere.

## Read in This Order

1. [instruction.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/instruction.md)
2. [design_details1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/design_details1.md)
3. [implementation_plan.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_plan.md)
4. [architecture_refined.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/architecture_refined.md)
5. [session_runtime_and_pty.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/session_runtime_and_pty.md)
6. [api_and_event_schema.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/api_and_event_schema.md)
7. [implementation_milestones.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_milestones.md)
8. [mvp_checklist.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/mvp_checklist.md)
9. [build_and_test.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/build_and_test.md)
10. [tdd_policy.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/tdd_policy.md)
11. [client_api_ios.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/client_api_ios.md)
12. [ios_client_mvp.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/ios_client_mvp.md)
13. [ios_client_architecture.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/ios_client_architecture.md)
14. [frontend_strategy.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/frontend_strategy.md)
15. [frontend_workspace.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/frontend_workspace.md)
16. [host_ui_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/host_ui_v1.md)
17. [host_ui_behaviors_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/host_ui_behaviors_v1.md)
18. [remote_web_client_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/remote_web_client_v1.md)
19. [remote_client_behaviors_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/remote_client_behaviors_v1.md)
20. [packaging_architecture.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/packaging_architecture.md)

## Purpose of Each Document

- `instruction.md`: original initiative architecture
- `design_details1.md`: detailed PTY output buffering initiative
- `implementation_plan.md`: execution order and phase boundaries
- `architecture_refined.md`: implementation-level component boundaries
- `session_runtime_and_pty.md`: runtime ownership, PTY model, replay, and delivery rules
- `api_and_event_schema.md`: REST and WebSocket contract draft
- `implementation_milestones.md`: milestone breakdown and acceptance criteria
- `mvp_checklist.md`: MVP acceptance flows and non-goals
- `build_and_test.md`: canonical local build and test workflow
- `tdd_policy.md`: test-first expectations and exceptions
- `client_api_ios.md`: iOS-facing REST and WebSocket contract
- `ios_client_mvp.md`: first Swift/iPhone client milestone scope
- `ios_client_architecture.md`: practical SwiftUI/network/terminal architecture notes
- `frontend_strategy.md`: v1 decision for host UI and remote web client structure
- `frontend_workspace.md`: Angular workspace structure and frontend commands
- `host_ui_v1.md`: frozen host admin UI v1 scope and layout
- `host_ui_behaviors_v1.md`: behavior-first host admin UI requirements
- `remote_web_client_v1.md`: frozen remote web client v1 scope and layout
- `remote_client_behaviors_v1.md`: behavior-first remote client requirements
- `packaging_architecture.md`: host app packaging direction, bundled client/helper policy, and `--no-client` deployment mode
- `session_attention_inference_v1.md`: conservative attention model and rollout plan

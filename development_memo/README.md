# Development Memo Index

This directory holds the current implementation-facing documentation for `Sentrits-Core`.

Use code as the source of truth. Update or remove docs here when they drift away from implementation.

## Core Runtime Docs

- [system_architecture.md](system_architecture.md)
  - current end-to-end runtime, CLI, and client architecture
- [architecture_refined.md](architecture_refined.md)
  - runtime component boundaries and control model
- [session_runtime_and_pty.md](session_runtime_and_pty.md)
  - PTY ownership, buffering, replay, and control semantics
- [api_and_event_schema.md](api_and_event_schema.md)
  - current REST and WebSocket contract
- [build_and_test.md](build_and_test.md)
  - canonical build and test workflow
- [packaging_architecture.md](packaging_architecture.md)
  - current daemon-first packaging direction
- [mvp_checklist.md](mvp_checklist.md)
  - current working baseline and next-release MVP priorities
- [known_limitations.md](known_limitations.md)
  - current known runtime/client limitations

## Future Design Docs

- [future/session_terminal_multiplexer_and_semantic_runtime.md](future/session_terminal_multiplexer_and_semantic_runtime.md)
- [future/session_terminal_multiplexer_v1.md](future/session_terminal_multiplexer_v1.md)
- [future/session_terminal_multiplexer_wire_compatibility.md](future/session_terminal_multiplexer_wire_compatibility.md)
- [future/session_signal_map.md](future/session_signal_map.md)
- [future/pty_semantic_extractor.md](future/pty_semantic_extractor.md)

These future docs are directional. They are not promises that every described subsystem already exists in the current codebase.

## Supporting Docs Kept For Runtime Context

- [active_discovery_plan.md](active_discovery_plan.md)
- [cli_vnext_plan.md](cli_vnext_plan.md)
- [implementation_milestones.md](implementation_milestones.md)
- [implementation_plan.md](implementation_plan.md)
- [session_attention_inference_v1.md](session_attention_inference_v1.md)

## Client Documentation Boundary

Maintained client docs live in their own repos:

- Web: https://github.com/shubow-sentrits/Sentrits-Web
- iOS: https://github.com/shubow-sentrits/Sentrits-IOS

Client shadow memos should not be kept here once better current docs exist in those repos.

## Notes

- Deprecated browser assets and older surfaces live under [deprecated/](../deprecated/README.md).
- Root-level onboarding belongs in `README.md` and `get_started.md`.

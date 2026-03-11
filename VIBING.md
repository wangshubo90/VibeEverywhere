# VibeEverywhere Agent Guide

This repository is implementing `vibe-hostd`, a local-network session runtime and supervision/control plane for AI coding CLIs.

## Working Baseline

- Language: C++20
- Build system: CMake + Ninja
- Preferred compiler: Clang/LLVM
- Secondary scripting language: Python, only for tooling or test helpers when it materially simplifies support work
- Development style: test-driven development by default

## Primary References

- Product and initiative docs live in [development _memo](/Users/shubow/dev/VibeEverywhere/development%20_memo).
- Start with [development _memo/implementation_plan.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_plan.md).
- MVP acceptance flows are defined in [development _memo/mvp_checklist.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/mvp_checklist.md).
- Web frontend direction is defined in [development _memo/frontend_strategy.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/frontend_strategy.md), [development _memo/host_ui_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/host_ui_v1.md), and [development _memo/remote_web_client_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/remote_web_client_v1.md).
- iOS client API and scope are defined in [development _memo/client_api_ios.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/client_api_ios.md), [development _memo/ios_client_mvp.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/ios_client_mvp.md), and [development _memo/ios_client_architecture.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/ios_client_architecture.md).
- Build and test workflow is defined in [development _memo/build_and_test.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/build_and_test.md).
- TDD expectations are defined in [development _memo/tdd_policy.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/tdd_policy.md).

## Build Expectations

- Prefer out-of-source builds in `build/`.
- Configure with CMake using the Ninja generator.
- Prefer Clang explicitly when configuring:

```bash
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

- Build with:

```bash
cmake --build build
```

## Test Expectations

- New behavior should start with a failing automated test whenever practical.
- Every feature should add or update unit, integration, or end-to-end coverage at the appropriate layer.
- Run the full test suite with:

```bash
ctest --test-dir build --output-on-failure
```

- For targeted development, run a narrowed subset first, then rerun the full suite before handing off.

## Implementation Bias

- Keep platform-specific code isolated behind narrow interfaces.
- Favor explicit ownership and bounded-memory structures for PTY and output buffering work.
- Treat terminal output as raw bytes; preserve ANSI data and defer rendering semantics to clients.
- Avoid designs where slow network clients can interfere with PTY ingestion.
- Treat the host terminal as another daemon-attached session client, not as a special out-of-band runtime.
- Preserve the invariant: one session PTY, many views, one controller.
- Bias new work toward runtime observability, supervision events, and session-state quality before investing in larger frontend rewrites.
- Keep `SessionPhase` support extensible but conservative; do not hard-code provider-specific heuristics too early.
- Treat file watching and git inspection as both observability inputs and read-only client data surfaces.
- Prioritize truthful inventory data and watch flows over deeper terminal/client polish.

## Expected Early Project Layout

The repository is still being bootstrapped. As code is added, prefer this shape unless a better reason emerges:

- `CMakeLists.txt`
- `cmake/`
- `src/`
- `include/`
- `tests/`
- `tools/`
- `development _memo/`

## Before Changing Architecture

- Check whether the change affects session identity, PTY handling, buffering, client control semantics, host-attach behavior, or recovery guarantees.
- Update the relevant detailed design markdown in [development _memo](/Users/shubow/dev/VibeEverywhere/development%20_memo) when the implementation direction changes materially.

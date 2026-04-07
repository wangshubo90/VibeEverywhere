# Sentrits Agent Guide

This repository implements `sentrits`, a local-network session runtime and supervision/control plane for AI coding CLIs.

## Working Baseline

- Language: C++20
- Build system: CMake + Ninja
- Preferred compiler: Clang/LLVM
- Python is acceptable only for tooling, fixtures, and helper scripts when it clearly reduces complexity
- Test-driven development is the default engineering style

## Primary References

- Product and implementation docs live in [development_memo/README.md](development_memo/README.md).
- Full runtime/client architecture lives in [development_memo/system_architecture.md](development_memo/system_architecture.md).
- Runtime internals live in [development_memo/architecture_refined.md](development_memo/architecture_refined.md) and [development_memo/session_runtime_and_pty.md](development_memo/session_runtime_and_pty.md).
- REST and WebSocket contracts live in [development_memo/api_and_event_schema.md](development_memo/api_and_event_schema.md).
- Build and test workflow lives in [development_memo/build_and_test.md](development_memo/build_and_test.md).
- Packaging direction lives in [development_memo/packaging_architecture.md](development_memo/packaging_architecture.md).
- MVP scope lives in [development_memo/mvp_checklist.md](development_memo/mvp_checklist.md).
- TDD expectations live in [development_memo/tdd_policy.md](development_memo/tdd_policy.md).

Maintained client docs live in their own repos:

- Web: https://github.com/shubow-sentrits/Sentrits-Web
- iOS: https://github.com/shubow-sentrits/Sentrits-IOS

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
- Every change should add or update coverage at the right layer.
- Run the full suite with:

```bash
ctest --test-dir build --output-on-failure
```

- For targeted development, run a narrowed subset first, then rerun the full suite before handoff.

## Implementation Bias

- Keep platform-specific code isolated behind narrow interfaces.
- Preserve the invariant: one session PTY, many observers, one active controller.
- Keep PTY ingestion independent from slow clients.
- Treat the runtime as the source of truth for session inventory and control ownership.
- Prefer improving runtime truthfulness and supervision signals before large client-side rewrites.
- Keep file watching and git inspection as observability inputs and read-only client data surfaces.
- Treat future semantic-monitoring layers as additive over runtime truth, not replacements for it.

## Before Changing Architecture

- Check whether the change affects session identity, PTY handling, buffering, snapshot/bootstrap generation, controller semantics, host attach behavior, or recovery guarantees.
- Update the relevant docs in [development_memo/README.md](development_memo/README.md) when implementation direction changes materially.

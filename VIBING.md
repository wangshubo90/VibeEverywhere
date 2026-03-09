# VibeEverywhere Agent Guide

This repository is implementing `vibe-hostd`, a local-network remote session system for AI coding CLIs.

## Working Baseline

- Language: C++20
- Build system: CMake + Ninja
- Preferred compiler: Clang/LLVM
- Secondary scripting language: Python, only for tooling or test helpers when it materially simplifies support work
- Development style: test-driven development by default

## Primary References

- Product and initiative docs live in [development _memo](/Users/shubow/dev/VibeEverywhere/development%20_memo).
- Start with [development _memo/implementation_plan.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_plan.md).
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

- Check whether the change affects session identity, PTY handling, buffering, client control semantics, or recovery guarantees.
- Update the relevant detailed design markdown in [development _memo](/Users/shubow/dev/VibeEverywhere/development%20_memo) when the implementation direction changes materially.

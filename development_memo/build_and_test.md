# Build and Test Workflow

This document defines the standard build and test workflow for Sentrits-Core.

## Toolchain Baseline

- C++ standard: C++20
- Generator: Ninja
- Build system: CMake
- Preferred compiler: Clang/LLVM
- Optional support language: Python for tooling, fixtures, or local test harnesses

Current host platform target:

- macOS and Linux only
- PTY/session runtime paths should compile on both through explicit platform seams
- Linux-specific PTY libraries should be linked in CMake rather than assumed transitively
- on WSL, use a native package-manager install of CMake rather than a Snap-packaged `cmake`

## Configure

Use an out-of-source build directory:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
```

On WSL specifically, make sure `cmake` resolves to a native binary such as `/usr/bin/cmake`.
If `which cmake` returns `/snap/bin/cmake`, install a non-Snap CMake first because Snap confinement breaks both configure and build steps under WSL.

If a debug profile is needed:

```bash
cmake -S . -B build-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
```

## Build

```bash
cmake --build build
```

Common target-specific builds:

```bash
cmake --build build --target sentrits
cmake --build build --target sentrits_tests
```

## Test

Run all registered tests:

```bash
ctest --test-dir build --output-on-failure
```

Run a subset by label or regex once labels exist:

```bash
ctest --test-dir build --output-on-failure -L unit
ctest --test-dir build --output-on-failure -R session
```

## Frontend Workspace

The in-repo frontend workspace is the host-admin surface under `frontend/`.

Recommended baseline:

- use an LTS Node release for day-to-day frontend work

Run from `frontend/`:

```bash
npm install
npm run build:libs
npm run build:host-admin
```

For local development:

```bash
npm run start:host-admin
```

Notes:

- the maintained browser remote client lives in https://github.com/shubow-sentrits/Sentrits-Web
- the daemon-served plain HTML browser assets are deprecated and now live under `deprecated/web/`
- keep `frontend/` documentation aligned with the host-admin role that still exists in this repo

## TDD Workflow

Expected loop:

1. Add or update a failing test for the behavior change.
2. Implement the smallest change needed to satisfy the test.
3. Refactor while keeping tests green.
4. Run the relevant narrowed suite.
5. Run the full suite before handing off.

When test-first is impractical, document the reason in the change summary and add the missing coverage immediately after the implementation seam exists.

## Recommended Test Layers

- Unit tests for bounded buffers, state machines, parsers, and policy objects
- Integration tests for PTY/session lifecycle, API routes, and event delivery
- End-to-end tests for attach, input, tail replay, and multi-client control behavior

## Initial Quality Gates

Before a change is considered ready:

- project configures cleanly with CMake + Ninja
- project builds under Clang
- platform-facing assumptions remain explicit enough to review
- all relevant automated tests pass
- full `ctest` run passes locally
- any design-impacting change updates the corresponding markdown under [development_memo/README.md](README.md)

Platform guardrails for this repo:

- do not widen platform work into Windows unless a separate task requires it
- keep PTY/process runtime mechanics behind interfaces or factories
- prefer compile-safe fallbacks or explicit unsupported paths over hidden platform assumptions
- describe Linux support only for seams that actually build or run today

## Python Usage Guidance

Python is allowed for:

- local developer scripts
- log/fixture generation
- protocol simulation helpers
- integration test harnesses when C++ alone would add disproportionate complexity

Python should not become the core runtime for host daemon responsibilities that belong in `sentrits`.

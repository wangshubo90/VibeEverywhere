# Getting Started

This document is the clean operator and developer quickstart for Sentrits-Core.

It focuses on:

- build
- test
- running the daemon
- basic CLI usage

For architecture and product docs, start from `README.md` or `development_memo/README.md`.

## Platform Baseline

Supported host platforms today:

- macOS
- Linux

Toolchain baseline:

- C++20
- CMake
- Ninja
- Clang/LLVM preferred

## Build

Configure:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
```

Build:

```bash
cmake --build build
```

Common target-specific builds:

```bash
cmake --build build --target sentrits
cmake --build build --target sentrits_tests
```

Linux note:

- the PTY runtime depends on standard PTY headers and `libutil`
- if your distro splits PTY development headers from the base libc toolchain, install the corresponding development package before configuring

## Test

Run the full registered test suite:

```bash
ctest --test-dir build --output-on-failure
```

Run a narrowed subset:

```bash
ctest --test-dir build --output-on-failure -R session
ctest --test-dir build --output-on-failure -R http
```

More build/test detail:

- `development_memo/build_and_test.md`

## Run The Daemon

Start the daemon with default listeners:

```bash
./build/sentrits serve
```

Default listeners:

- host admin: `127.0.0.1:18085`
- remote client/API: `0.0.0.0:18086`
- UDP discovery: `18087`

Explicit listener example:

```bash
./build/sentrits serve \
  --admin-host 127.0.0.1 --admin-port 18085 \
  --remote-host 0.0.0.0 --remote-port 18086
```

Disable UDP discovery if needed:

```bash
./build/sentrits serve --no-udp-discovery
```

## Basic Reachability Checks

Local:

```bash
curl http://127.0.0.1:18085/health
curl http://127.0.0.1:18086/health
```

Expected response:

```text
ok
```

Remote discovery info check:

```bash
curl http://HOST_IP:18086/discovery/info
```

## CLI Usage

Show the current CLI help:

```bash
./build/sentrits
```

Current commonly used commands:

```bash
./build/sentrits serve
./build/sentrits host status
./build/sentrits session list
./build/sentrits session show s_1
./build/sentrits session start --title demo --attach
./build/sentrits session observe s_1
./build/sentrits session attach s_1
./build/sentrits session stop s_1
./build/sentrits session clear-inactive
```

## Host Admin UI

Open the host-local admin surface:

- `http://127.0.0.1:18085/`

Use it for:

- pairing approval
- trusted device management
- host configuration
- session creation and cleanup
- session and client supervision

## Remote Clients

Maintained clients live in separate repos:

- Web: https://github.com/shubow-sentrits/Sentrits-Web
- iOS: https://github.com/shubow-sentrits/Sentrits-IOS

The runtime remains the source of truth for:

- pairing
- session inventory
- observe/control APIs
- terminal snapshot/bootstrap data

## Troubleshooting Basics

If another machine cannot reach the remote listener:

- confirm the daemon is bound to `0.0.0.0` or the LAN IP, not `127.0.0.1`
- confirm host firewall rules allow inbound TCP on `18086`
- test:

```bash
curl http://HOST_IP:18086/health
```

If local attach or remote control behaves unexpectedly:

- check the current session/controller state with:

```bash
./build/sentrits session list
./build/sentrits session show s_1
```

Further docs:

- `development_memo/system_architecture.md`
- `development_memo/api_and_event_schema.md`
- `development_memo/known_limitations.md`

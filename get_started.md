# Getting Started

This document is the clean operator and developer quickstart for Sentrits-Core.

It focuses on:

- build
- test
- running the daemon
- basic CLI usage
- Debian packaging, install, smoke test, and uninstall

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

## Debian Package

The current Linux packaging path builds a `.deb` from this repo and stages the maintained browser remote client from the neighboring `../Sentrits-Web` checkout.

Prerequisites:

- `../Sentrits-Web` exists beside this repo
- that checkout is on `main`
- its production assets are built into `dist/`

Build the maintained web client bundle:

```bash
cd ../Sentrits-Web
npm install
npm run build
cd ../core-packaging
```

Configure the runtime build:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
```

Build the Debian package:

```bash
cmake --build build --target sentrits_package_deb
```

Expected package artifact:

- `build/sentrits_0.1.0_amd64.deb`

## Debian Package Contents

The Debian package installs:

- binary: `/usr/bin/sentrits`
- packaged web assets: `/usr/lib/sentrits/www`
- user unit template: `/usr/lib/systemd/user/sentrits.service`

Packaged web UI layout:

- host admin UI: `/usr/lib/sentrits/www/host-admin`
- maintained browser remote client: `/usr/lib/sentrits/www/remote-client`
- staged web revision marker: `/usr/lib/sentrits/www/_metadata/sentrits-web-revision.txt`

Runtime serving model after install:

- host-local admin UI is served on the admin listener:
  - `http://127.0.0.1:18085/`
- remote browser client is served on the remote listener:
  - `http://HOST_IP:18086/`
  - `http://HOST_IP:18086/remote`

## Debian Install

Install the generated package:

```bash
sudo dpkg -i ./build/sentrits_0.1.0_amd64.deb
```

Install and enable the user-scoped service file:

```bash
sentrits service install
systemctl --user daemon-reload
systemctl --user enable sentrits.service
systemctl --user start sentrits.service
```

## Debian Smoke Test

Check binary and service state:

```bash
which sentrits
systemctl --user status sentrits.service --no-pager
```

Check local daemon reachability:

```bash
curl http://127.0.0.1:18085/health
curl http://127.0.0.1:18085/host/info
curl http://127.0.0.1:18085/
```

Check the packaged web client and packaged web revision:

```bash
curl http://127.0.0.1:18086/
curl http://127.0.0.1:18086/remote
cat /usr/lib/sentrits/www/_metadata/sentrits-web-revision.txt
```

Optional package inventory check:

```bash
dpkg -L sentrits
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

If the Debian service does not start:

- re-run:

```bash
systemctl --user daemon-reload
systemctl --user status sentrits.service --no-pager
journalctl --user -u sentrits.service --no-pager
```

## Debian Uninstall

Stop and disable the user service:

```bash
systemctl --user stop sentrits.service
systemctl --user disable sentrits.service
rm -f ~/.config/systemd/user/sentrits.service
systemctl --user daemon-reload
```

Remove the package:

```bash
sudo dpkg -r sentrits
```

Further docs:

- `development_memo/system_architecture.md`
- `development_memo/api_and_event_schema.md`
- `development_memo/known_limitations.md`

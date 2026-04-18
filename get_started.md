# Getting Started

This is the practical quickstart for Sentrits-Core.

It is organized in four parts:

- Quick Start
- Development
- Configuration Notes
- Troubleshooting

For deeper architecture and product material, start from `README.md` or `development_memo/README.md`.

## Contents

- [Quick Start](#quick-start)
- [Development](#development)
- [Configuration Notes](#configuration-notes)
- [Troubleshooting](#troubleshooting)

## Quick Start

This section is for installing and using Sentrits on a machine.

### Platform Baseline

Supported host platforms today:

- macOS
- Linux

### Install

#### macOS

Install the packaged tarball into a stable user-owned location, then bootstrap the per-user `launchd` agent.

Detailed macOS package build, install, smoke-test, and uninstall notes:

- `packaging/macos.md`

#### Linux

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

Detailed Debian install, package contents, and smoke-test notes:

- `packaging/debian.md`

### CLI Usage

Show the current CLI help:

```bash
sentrits
```

Common installed commands:

- `sentrits host status`
  Print basic host runtime status and listener information.
- `sentrits setup list`
  List host-owned saved session setups.
- `sentrits setup show setup_abc123`
  Show one saved setup in more detail.
- `sentrits session list`
  List known sessions, including recovered stopped sessions if they still exist in persisted state.
- `sentrits session show s_1`
  Show one session in more detail.
- `sentrits session start --title demo --attach`
  Start a new session and attach to it immediately.
- `sentrits session start --setup setup_abc123`
  Start a new session from a saved host-owned setup.
- `sentrits session start --workspace /path/to/repo --shell-command 'codex "$(cat prompt.md)"'`
  Start a new session from a shell-expanded command.
- `sentrits session observe s_1`
  Observe a running session without taking control.
- `sentrits session attach s_1`
  Attach interactively to a session.
- `sentrits session stop s_1`
  Stop a session.
- `sentrits session clear`
  Remove inactive persisted session records.

Notes:

- These commands target the configured local daemon by default.
- `--host` and `--port` are advanced daemon-endpoint override flags, not session settings.
- Use provider selection when you want the runtime to start the provider default command for that setup.
- Use `--shell-command` only when you need shell behavior such as command substitution or quoting-sensitive expansion.

Example session starts:

```bash
# Start from the codex provider default in the current workspace
sentrits session start --provider codex --title "Codex Session" --attach

# Start from the claude provider default in an explicit repo
sentrits session start --provider claude --workspace /path/to/repo --title "Claude Review"

# Start a coding CLI with flags and args
sentrits session start --workspace /path/to/repo --shell-command 'codex resume --all'

# Start a plain shell
sentrits session start --shell-command '/bin/bash -l' --title "Shell"

# Start a non-coding CLI program
sentrits session start --shell-command 'htop' --title "Host Monitor"

# Start an ordinary app with flags and args
sentrits session start --shell-command 'python -m http.server 8080' --workspace /tmp --title "HTTP Server"

# Start from a saved reusable setup
sentrits session start --setup setup_abc123

# Start from a saved setup but override the title
sentrits session start --setup setup_abc123 --title "One-off Run"

# Start with shell expansion
sentrits session start --workspace /path/to/repo --shell-command 'codex "$(cat prompt.md)"'
```

### Web UI

There are two different web surfaces:

- Host Admin UI
- Remote Web Client

Host Admin UI:

- served by the local admin listener
- intended for use on the host machine
- operational and administrative surface only
- available at `http://127.0.0.1:18085/`

The Host Admin UI is used for:

- pairing approval
- trusted device management
- host configuration
- session creation and cleanup
- session and client supervision

Remote Web Client:

- served by the remote listener, or run standalone from `Sentrits-Web`
- intended as the full browser client
- used for connecting to hosts and interacting with sessions

The Remote Web Client supports:

- connecting to one or more Sentrits instances
- session inventory and multi-host browsing
- attaching to sessions with a terminal view
- pairing and host verification flows

Packaged Remote Web Client on the same machine:

- `http://127.0.0.1:18086/`
- `http://127.0.0.1:18086/remote`

Packaged Remote Web Client from another machine on the same network:

- `http://HOST_IP:18086/`
- `http://HOST_IP:18086/remote`

### Linux Uninstall

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

Detailed Linux package install, smoke-test, uninstall, and state notes:

- `packaging/debian.md`

### macOS Uninstall

Unload and remove the per-user `launchd` agent:

```bash
launchctl unload ~/Library/LaunchAgents/io.sentrits.agent.plist 2>/dev/null || true
rm -f ~/Library/LaunchAgents/io.sentrits.agent.plist
```

Remove the installed package root:

```bash
rm -rf ~/Applications/Sentrits
```

Detailed macOS package install, smoke-test, uninstall, and state notes:

- `packaging/macos.md`

## Development

This section is for building, testing, debugging, and packaging from source.

### Toolchain Baseline

- C++20
- CMake
- Ninja
- Clang/LLVM preferred

Linux note:

- the PTY runtime depends on standard PTY headers and `libutil`
- if your distro splits PTY development headers from the base libc toolchain, install the corresponding development package before configuring

### Build

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

### Test

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
- `development_memo/tracing_and_debugging.md`

### Run The Daemon

Start the daemon with default listeners:

```bash
./build/sentrits serve
```

Default listeners:

- host admin: `127.0.0.1:18085`
- remote client/API: `0.0.0.0:18086`
- UDP discovery broadcast: `18087`

Explicit listener example:

```bash
./build/sentrits serve \
  --admin-host 127.0.0.1 --admin-port 18085 \
  --remote-host 0.0.0.0 --remote-port 18086
```

Disable UDP discovery broadcast if needed:

```bash
./build/sentrits serve --no-udp-discovery
```

Enable maintainer tracing in a `Debug` build:

```bash
SENTRITS_DEBUG_TRACE=1 ./build/sentrits serve
```

More trace/debug detail:

- `development_memo/tracing_and_debugging.md`

### Basic Reachability Checks

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

### Build Debian Package

The current Linux packaging path builds a `.deb` from this repo and stages:

- the maintained browser remote client from `../Sentrits-Web`
- the in-repo host admin frontend from `./frontend`
- the pinned browser client revision recorded in `packaging/sentrits-web-revision.txt`

Prerequisites:

- `../Sentrits-Web` exists beside this repo
- that checkout matches `packaging/sentrits-web-revision.txt`
- its production assets are built into `dist/`
- `./frontend` dependencies are installed
- `./frontend` host-admin production assets are built into `frontend/dist/host-admin/browser`

Build the maintained remote web client bundle:

```bash
cd ../Sentrits-Web
git checkout "$(cat ../Sentrits-Core/packaging/sentrits-web-revision.txt)"
npm install
npm run build
cd ../core-packaging
```

Build the in-repo host admin frontend bundle:

```bash
cd frontend
npm install
npm run build:host-admin
cd ..
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

## Configuration Notes

### Runtime Config Comes From Two Places

There are two configuration layers:

- service file: `~/.config/systemd/user/sentrits.service`
- persistent user state: `~/.sentrits/`

Important persistent files:

- `~/.sentrits/host_identity.json`
- `~/.sentrits/pairings.json`
- `~/.sentrits/sessions.json`

### Current Source Of Truth

With the current packaged user service:

- admin bind IP comes from `sentrits.service`
- remote bind IP comes from `sentrits.service`
- ports can be changed from the Host UI and are persisted in `~/.sentrits/host_identity.json`
- host display name is persisted in `~/.sentrits/host_identity.json`

Current default packaged service behavior:

- admin host is pinned to `127.0.0.1`
- remote host is pinned to `0.0.0.0`

### Restart Behavior

Configuration changes are persisted immediately, but listener sockets are created at daemon startup.

What requires restart:

- changing admin IP in `sentrits.service`
- changing remote IP in `sentrits.service`
- changing admin port from the Host UI
- changing remote port from the Host UI

In practice, after changing bind IPs or ports:

```bash
systemctl --user daemon-reload
systemctl --user restart sentrits.service
```

Note:

- the Host UI may show newly saved values before the daemon is actually listening on them
- bind IP changes in the Host UI are effectively an opt-out from the packaged service defaults for now

### Persistent State Behavior

Reinstalling or uninstalling the `.deb` does not clear `~/.sentrits/`.

What this means in practice:

- reinstalling the `.deb` does not reset Sentrits state
- a newly installed binary may still list previously stopped or recovered sessions
- uninstalling the package does not remove `~/.sentrits/`

If you want to clear stopped session records through the daemon:

```bash
sentrits session clear
```

If you want a full local reset for that user:

```bash
rm -rf ~/.sentrits
```

### UDP Discovery Helper

The Debian package does not currently install the web discovery helper.

Current state:

- `sentrits` can broadcast UDP discovery on port `18087`
- the maintained web client cannot consume UDP directly
- browser discovery over UDP requires the separate discovery helper from `Sentrits-Web`

If you want browser-based UDP discovery today:

```bash
git clone https://github.com/shubow-sentrits/Sentrits-Web.git
cd Sentrits-Web
npm install
npm run discovery-helper
```

That helper listens for UDP discovery and serves an HTTP discovery feed for the web client on port `18088`.

## Troubleshooting

### Service And Reachability

If the Debian service does not start:

```bash
systemctl --user daemon-reload
systemctl --user status sentrits.service --no-pager
journalctl --user -u sentrits.service --no-pager
```

If session creation fails because the daemon cannot reproduce the shell environment you expect:

- Sentrits bootstraps provider and direct-exec session environment from a login shell by default
- bootstrap warnings are logged to the daemon log and service journal when the shell prints to stderr during environment capture
- those warnings are emitted when the daemon creates a session with the bootstrapped environment path, regardless of whether the request came from the CLI, Host Admin UI, or a remote client
- inspect:

```bash
journalctl --user -u sentrits.service --no-pager
tail -n 200 ~/.sentrits/logs/sentrits.log
```

- common causes:
  - shell init files reference missing tools
  - service `PATH` does not include version-manager shims like `nvm`, `pnpm`, or `bun`
  - a login shell prints warnings even though it exits successfully

If another machine cannot reach the remote listener:

- confirm the daemon is bound to `0.0.0.0` or the LAN IP, not `127.0.0.1`
- confirm host firewall rules allow inbound TCP on `18086`
- test:

```bash
curl http://HOST_IP:18086/health
```

### Config Changes

If you changed ports in the Host UI but nothing moved:

- the new ports were likely saved successfully
- the running daemon is still bound to the old listener ports
- restart the user service:

```bash
systemctl --user restart sentrits.service
```

If you changed `sentrits.service` directly:

```bash
systemctl --user daemon-reload
systemctl --user restart sentrits.service
```

### Session State

If a newly installed build still shows old stopped sessions:

- Sentrits persists per-user state under `~/.sentrits/`
- old stopped sessions can be reloaded from `~/.sentrits/sessions.json`
- clear them with `sentrits session clear`
- remove `~/.sentrits/` only if you want a full local reset

### Further Docs

- `development_memo/system_architecture.md`
- `development_memo/api_and_event_schema.md`
- `development_memo/known_limitations.md`

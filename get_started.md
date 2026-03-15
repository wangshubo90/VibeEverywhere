# Getting Started

This project currently provides:

- `vibe-hostd` daemon
- local host admin UI
- Angular host-admin workspace scaffold and first live data pass
- local terminal attach/start commands
- browser smoke client for remote attach/control

## Build

Supported host platforms today:

- macOS
- Linux

Current platform boundary:

- session execution and `local-pty` use a platform-selected `IPtyProcess` factory
- macOS and Linux both route through the current POSIX `forkpty` backend
- file watching, process-tree inspection, and resource monitoring remain planned seams rather than completed platform integrations

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

## Frontend Workspace

An Angular workspace now lives under `frontend/`.

Install and build it with:

```bash
cd frontend
npm install
npm run build:libs
npm run build:host-admin
npm run build:remote-client
```

For local frontend development:

```bash
cd frontend
npm run start:host-admin
npm run start:remote-client
```

Node note:

- an LTS Node release is the intended baseline
- the current workspace also builds on newer odd-numbered Node releases, but that is not the preferred long-term setup

Linux build note:

- the PTY runtime uses `<pty.h>` and links `libutil` through CMake on Linux
- if your distro splits PTY development headers from the base toolchain, install the corresponding libc/pty development package before configuring

## Start The Daemon

```bash
./build/vibe-hostd serve
```

Default listeners:

- host admin: `127.0.0.1:18085`
- remote client/API: `0.0.0.0:18086`

To override them explicitly:

```bash
./build/vibe-hostd serve \
  --admin-host 127.0.0.1 --admin-port 18085 \
  --remote-host 0.0.0.0 --remote-port 18086
```

The host admin listener should stay localhost-only. Expose only the remote listener to other devices.

## Host Network Setup

If a remote browser or device cannot reach the daemon, check host firewall rules first.

### macOS

If macOS prompts for incoming connections when you first run `vibe-hostd`, allow it.

To inspect the Application Firewall state:

```bash
/usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate
```

To list app firewall rules:

```bash
/usr/libexec/ApplicationFirewall/socketfilterfw --listapps
```

If needed, add the built daemon binary explicitly:

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add ./build/vibe-hostd
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp ./build/vibe-hostd
```

Also make sure you are binding to `0.0.0.0` or the host LAN IP, not `127.0.0.1`.

### Linux

Common issue: the daemon is listening, but `ufw` or another host firewall blocks the port.

If you use `ufw`:

```bash
sudo ufw status
sudo ufw allow 18086/tcp
```

If you use `firewalld`:

```bash
sudo firewall-cmd --list-ports
sudo firewall-cmd --add-port=18086/tcp --permanent
sudo firewall-cmd --reload
```

If you use raw `iptables`/`nftables`, allow inbound TCP on the daemon port before testing.

### Quick Reachability Check

From another machine on the same network:

```bash
curl http://HOST_IP:18086/health
```

Expected response:

```text
ok
```

If that fails, fix binding/firewall before debugging pairing or WebSocket behavior.

## Host Admin UI

Open:

- `http://127.0.0.1:18085/`

Use it to:

- review host config
- approve pending pairings
- inspect live sessions
- inspect attached clients
- stop sessions
- disconnect clients

## Try The Angular Host Admin

The Angular host-admin app currently runs separately from the daemon-served UI.

Start the daemon first:

```bash
./build/vibe-hostd serve
```

Then in another shell:

```bash
cd frontend
npm run start:host-admin
```

Open the Angular app at:

- `http://localhost:4200/`

Current dev behavior:

- when served from Angular dev server, the host-admin app talks to `http://127.0.0.1:18085`
- when eventually served by the daemon, it should use same-origin requests

Useful smoke actions in the Angular host-admin:

1. refresh host state
2. edit and save host config
3. create a session locally from the UI
4. approve a pending pairing
5. stop a session
6. clear ended/archived sessions
7. revoke a trusted device
8. disconnect an attached client

## Pair A Remote Browser Client

On another device, open:

- `http://HOST_IP:18086/`

In the browser smoke client:

1. set host/port
2. click `Start Pairing`

In the host admin UI:

1. copy the `pairingId` and `code` from the pending list
2. approve the pairing
3. copy the returned bearer token

Back in the browser smoke client:

1. paste the token
2. click `Save Token`

Pending pairing requests expire automatically after a short timeout and then cannot be approved.

## Start A Session From The Host Terminal

```bash
./build/vibe-hostd session-start my-session
```

This creates a daemon-managed session and attaches the current terminal as the host client.

To attach to an existing session:

```bash
./build/vibe-hostd session-attach s_1
```

## Create A Session From The Browser Client

In the smoke client:

1. choose `Provider`
2. set `Session Title`
3. set `Workspace Root`
4. optionally set `Explicit Command`
5. click `Create Session`

Then:

1. click `List Sessions`
2. click `Use`
3. click `Connect`
4. click `Request Control`

## Explicit Command Override

If the default provider executable is wrong for your machine, provide an explicit command.

Example:

```text
/opt/homebrew/bin/claude --print
```

The smoke client parses that into argv and sends it as an explicit session command override.

## Current Notes

- The daemon now uses two listeners by default:
- host admin on `127.0.0.1:18085`
- remote client/API on `0.0.0.0:18086`
- macOS and Linux are the only intended runtime targets right now.
- The current PTY/session path is shared across macOS and Linux through a POSIX backend selected by a factory seam.
- Linux readiness is currently strongest for configure/build/test and PTY session lifecycle behavior.
- File watching, process-tree inspection, and resource monitoring are not implemented yet on either platform.
- Sessions are still selected by `sessionId`, not by port.
- A session can outlive any particular client attachment.
- Stopping a session stops the real host-side PTY process.
- Host admin UI is localhost-only and should not be exposed.
- The served remote browser page is still a smoke client, not the final remote product client.

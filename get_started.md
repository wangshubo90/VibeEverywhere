# Getting Started

This repository currently provides:

- `vibe-hostd` daemon
- localhost-only host admin surface
- daemon-served browser smoke client for remote attach/control
- in-repo host-admin frontend workspace under `frontend/`
- runtime APIs used by the maintained remote client in `~/dev/VibeEverywhere-Client`
- UDP discovery support in the runtime

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

Linux build note:

- the PTY runtime uses `<pty.h>` and links `libutil` through CMake on Linux
- if your distro splits PTY development headers from the base toolchain, install the corresponding libc/pty development package before configuring

## Frontend Layout

There are currently two frontend surfaces.

### 1. In-Repo Host Admin Workspace

This repository still contains the host-admin frontend workspace under `frontend/`.

```bash
cd frontend
npm install
npm run build:libs
npm run build:host-admin
npm run start:host-admin
```

Default dev URL:

- `http://127.0.0.1:4200`

### 2. Separate Remote Client Repo

The maintained remote client now lives in:

- `~/dev/VibeEverywhere-Client`

```bash
cd ~/dev/VibeEverywhere-Client
npm install
npm test
npm run lint
npm run build
npm run dev
```

Default dev URL:

- `http://127.0.0.1:3000`

Node note for both frontend workspaces:

- an LTS Node release is the intended baseline
- newer odd-numbered Node releases may also work, but that is not the preferred long-term setup

## Start The Daemon

```bash
./build/vibe-hostd serve
```

Default listeners:

- host admin: `127.0.0.1:18085`
- remote client/API: `0.0.0.0:18086`
- UDP discovery: `18087`

To override the HTTP listeners explicitly:

```bash
./build/vibe-hostd serve \
  --admin-host 127.0.0.1 --admin-port 18085 \
  --remote-host 0.0.0.0 --remote-port 18086
```

The host admin listener should stay localhost-only. Expose only the remote listener to other devices.

Discovery is advisory only. Pairing and authorization still gate real access.

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
- clear ended or archived sessions

## Try The Host Admin Dev UI

Start the daemon first:

```bash
./build/vibe-hostd serve
```

Then in another shell:

```bash
cd frontend
npm run start:host-admin
```

Open:

- `http://127.0.0.1:4200/`

Current dev behavior:

- when served from the dev server, the host-admin app talks to `http://127.0.0.1:18085`
- when eventually served by the daemon, it should use same-origin requests

Useful smoke actions:

1. refresh host state
2. edit and save host config
3. create a session locally from the UI
4. approve a pending pairing
5. stop a session
6. clear ended or archived sessions
7. revoke a trusted device
8. disconnect an attached client

## Remote Client Choices

There are currently two remote client paths.

### 1. Daemon-Served Smoke Page

Open on another device:

- `http://HOST_IP:18086/`

Use it as the simplest runtime reference client.

### 2. Separate Maintained Remote Client

Start the daemon first:

```bash
./build/vibe-hostd serve
```

Then in another shell:

```bash
cd ~/dev/VibeEverywhere-Client
npm run dev
```

Open:

- `http://127.0.0.1:3000/`

Current dev behavior:

- the separate client talks directly to the daemon remote listener
- it should target daemon port `18086`
- the daemon-served page remains the simplest runtime reference client

## Pairing Flow

From a remote client:

1. request pairing
2. switch to host admin
3. approve the pending pairing
4. return to the remote client
5. confirm token claim succeeds
6. confirm sessions can be listed

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

## Create A Session From A Remote Client

Typical flow:

1. choose a provider
2. set session title
3. set workspace root
4. optionally set conversation id
5. optionally set explicit command
6. create the session
7. open a session tab or view
8. attach websocket
9. request control

## Discovery Quick Check

The runtime now exposes discovery metadata over HTTP.

From another machine on the same network:

```bash
curl http://HOST_IP:18086/discovery/info
```

Expected fields include:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tls`

The runtime also broadcasts the same discovery payload over UDP on port `18087`.

Optional UDP spot-check if you have a listener tool:

```bash
nc -luk 18087
```

## Smoke Checklist

Use the current smoke checklist in:

- [tests_smoke/mvp_smoke_checklist.md](/Users/shubow/dev/VibeEverywhere/tests_smoke/mvp_smoke_checklist.md)

## Current Notes

- the daemon uses two HTTP listeners by default
- host admin remains localhost-only and should not be exposed
- macOS and Linux are the only intended runtime targets right now
- the current PTY/session path is shared across macOS and Linux through a POSIX backend selected by a factory seam
- sessions are still selected by `sessionId`, not by port
- a session can outlive any particular client attachment
- stopping a session stops the real host-side PTY process
- the daemon-served remote browser page is still a smoke client, not the final product client

# Getting Started

This project currently provides:

- `vibe-hostd` daemon
- local host admin UI
- local terminal attach/start commands
- browser smoke client for remote attach/control

## Build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

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
- Sessions are still selected by `sessionId`, not by port.
- A session can outlive any particular client attachment.
- Stopping a session stops the real host-side PTY process.
- Host admin UI is localhost-only and should not be exposed.
- The served remote browser page is still a smoke client, not the final remote product client.

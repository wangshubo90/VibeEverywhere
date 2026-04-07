# MVP Smoke Checklist

This smoke checklist validates the current runtime-centered MVP surface.

## 1. Runtime Boot

Start the daemon:

```bash
./build/sentrits serve
```

Confirm:

- `http://127.0.0.1:18085/health` returns `ok`
- `http://127.0.0.1:18086/health` returns `ok`

## 2. Host Identity And Discovery

Check host info:

```bash
curl http://127.0.0.1:18086/host/info
curl http://127.0.0.1:18086/discovery/info
```

Confirm the responses include:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`

Optional UDP spot-check:

```bash
nc -luk 18087
```

## 3. Host Admin

Open:

- `http://127.0.0.1:18085/`

Smoke:

1. refresh host state
2. create a session
3. stop a session
4. clear inactive sessions
5. review pending pairings

## 4. Pairing

Use a maintained client:

- Web: https://github.com/shubow-sentrits/Sentrits-Web
- iOS: https://github.com/shubow-sentrits/Sentrits-IOS

Smoke:

1. request pairing
2. approve pairing from host admin
3. confirm token claim succeeds
4. confirm the client can list sessions

## 5. Observe / Control

Smoke:

1. create a session
2. attach as observer
3. confirm terminal output appears
4. request control
5. confirm typing reaches the PTY
6. confirm resize updates the PTY
7. release or reclaim control
8. stop the session

## 6. Snapshot And Inventory Truth

Smoke:

1. fetch `/sessions`
2. fetch `/sessions/{sessionId}/snapshot`
3. confirm controller state appears in inventory
4. confirm snapshot includes terminal seed data
5. confirm ended sessions remain inspectable until cleared

## 7. Current Known Boundary

Not yet considered complete in this smoke pass:

- internet relay / non-LAN connectivity
- multi-user account systems
- full semantic-monitoring layer

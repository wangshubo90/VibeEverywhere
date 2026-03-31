# MVP Smoke Checklist

This checklist covers the current runtime milestone:

- daemon start
- host admin
- pairing
- remote attach
- terminal control
- UDP discovery info

It does not assume multi-host grouping is complete yet.

## 1. Runtime Boot

Start the daemon:

```bash
./build/sentrits serve
```

Confirm:

- `http://127.0.0.1:18085/health` returns `ok`
- `http://127.0.0.1:18086/health` returns `ok`

## 2. Host Admin

Open:

- `http://127.0.0.1:18085/`

Smoke:

1. refresh host state
2. edit and save host config
3. create a session
4. stop a session
5. clear ended or archived sessions
6. review pending pairings

## 3. Discovery

From another machine or terminal:

```bash
curl http://HOST_IP:18086/discovery/info
```

Confirm the response includes:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tls`

Optional UDP spot-check if you have a listener tool:

```bash
nc -luk 18087
```

Confirm periodic discovery payloads arrive while `sentrits` is running.

## 4. Pairing

Use either:

- the daemon-served smoke client at `http://HOST_IP:18086/`
- the separate remote client repo at `http://HOST_IP_OR_LOCALHOST:3000/`

Smoke:

1. request pairing
2. approve pairing from host admin
3. confirm token claim succeeds
4. confirm the client can list sessions

## 5. Session Attach

Smoke:

1. create a session
2. open a session view or tab
3. attach websocket
4. confirm terminal output appears
5. request control
6. confirm typing reaches the PTY
7. confirm terminal resize updates the PTY correctly
8. release control
9. stop the session

## 6. Snapshot And File Read

Smoke:

1. select a live session
2. confirm snapshot metadata loads
3. confirm recent file list loads when available
4. open a recent file
5. confirm file content displays correctly

## 7. Session Inventory

Smoke:

1. confirm session list updates after create and stop
2. confirm controller changes appear in inventory
3. confirm attention and lifecycle values look truthful
4. confirm ended sessions remain inspectable until cleared

## 8. Current Known Boundary

Not yet considered complete in this smoke pass:

- multi-host client discovery UI
- session grouping by tags
- tag mutation from client
- notification or watch workflows

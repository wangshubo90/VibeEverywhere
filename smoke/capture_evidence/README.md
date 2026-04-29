# Capture Evidence Smoke Test

This smoke test validates the MVP actor-observes-capture path end-to-end:

- a normal PTY session is started through `sentrits session start`
- that PTY session sleeps first so you can attach/open it from iOS
- the PTY session then starts a managed capture session with `sentrits capture start`
- stdout and stderr from the capture session are captured as line evidence
- the PTY session reads evidence with `SENTRITS_SESSION_ID` set to its own session id
- the host emits `observation.created`
- a remote iOS controller connected to `/ws/overview` should show `Evidence observed`

## Prerequisites

1. Build Core:

   ```bash
   cmake --build build
   ```

2. Start an isolated smoke host in another terminal:

   ```bash
   smoke/capture_evidence/start_smoke_host.sh
   ```

   This uses:

   - admin endpoint: `127.0.0.1:19085`
   - remote endpoint: `0.0.0.0:19086`
   - display name: `Sentrits-Smoke-Mac`
   - data directory: `smoke/capture_evidence/.runtime/data`

   The script starts the host in the background, writes logs to
   `smoke/capture_evidence/.runtime/smoke-host.log`, then returns to the shell.
   Stop it with:

   ```bash
   smoke/capture_evidence/stop_smoke_host.sh
   ```

3. Open the iOS app and pair it to your Mac IP on remote port `19086`. Leave it on any tab long enough for the overview websocket to connect.

## Automated Smoke

Run:

```bash
smoke/capture_evidence/run_capture_evidence_smoke.sh
```

The driver creates a PTY actor session and then returns. The actor session waits 30 seconds before creating the capture session and reading evidence. Use that window to open/attach the actor session from iOS.

Useful overrides:

```bash
SENTRITS_BIN=./build/sentrits \
SENTRITS_HOST=127.0.0.1 \
SENTRITS_PORT=19085 \
SMOKE_PREPARE_SECONDS=30 \
smoke/capture_evidence/run_capture_evidence_smoke.sh
```

Do not point this fixture at your installed production daemon unless that is intentional. The default uses `./build/sentrits` and the isolated smoke admin port `19085`.

## Expected Output

The driver prints:

- the created actor PTY session id
- the time window before the actor starts capture/evidence work

Inside the actor PTY session you should see:

- the created capture session id
- tail evidence containing `BOOT`, `READY`, and `STDERR`
- search evidence for `marker=beta`
- recent observations containing the actor PTY session id

## iOS Manual Checks

After the actor session performs the evidence read:

1. Open the iOS Activity tab.
2. Confirm an `Evidence observed` entry appears.
3. Confirm the message references the capture/log source, operation, and actor session.
4. Confirm the managed capture session does not appear as a terminal session in Explorer.

## Files

- `producer.sh`: deterministic stdout/stderr producer kept alive briefly for manual inspection.
- `actor_session.sh`: script run inside the PTY actor session; it starts capture and reads evidence.
- `run_capture_evidence_smoke.sh`: creates the PTY actor session and writes its session id for actor attribution.

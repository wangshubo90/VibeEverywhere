For the current runtime-side session-node tracing, smoke it like this.

  Setup
  Run the host with these env vars:

SENTRITS_DEBUG_TRACE=1 \
SENTRITS_SESSION_SIGNAL_TRACE_PATH=/tmp/sentrits-session-signal-trace.log \
./build/sentrits serve

  What each does:

  - SENTRITS_DEBUG_TRACE=1
      - prints gated debug traces to stderr / console
  - SENTRITS_SESSION_SIGNAL_TRACE_PATH=/tmp/sentrits-session-signal-trace.log
      - writes the derived session-node transition trace to a file
  - optional existing low-level attach trace:
      - VIBE_SERVER_TRACE_PATH=/tmp/sentrits-server-trace.log
      - this is older server-flow tracing, not the new semantic/node-summary trace

  What to look for in console
  Filter for:

  - core.node
  - summary.transition

  Example shape:

  [sentrits-debug][...][core.node][summary.transition] ts=... reason=poll session=s_1 status=Running controller=host
  interaction=running_non_interactive attention=none preview="" files=0 gitDirty=false seq=42 lastActivityAt=...

  This is the high-level derived signal trace.

  What to look for in the file
  Open:

  - /tmp/sentrits-session-signal-trace.log

  It contains one line per derived transition, with:

  - session id
  - lifecycle status
  - controller kind
  - interaction kind
  - attention state
  - semantic preview
  - recent file changes
  - git dirty
  - sequence
  - last activity
  - transition reason

  Suggested smoke flow
  For a real coding CLI like Codex:

  1. start host with the env vars above
  2. create or attach to a Codex session
  3. let it idle at prompt
  4. submit a prompt
  5. let it enter working / thinking
  6. claim/release control from another client if relevant
  7. wait for file changes or git changes if the run edits files
  8. inspect:
      - console core.node transitions
      - /tmp/sentrits-session-signal-trace.log

  Useful companion commands
  Watch live file trace:

  tail -f /tmp/sentrits-session-signal-trace.log

  Filter console logs if you launch from shell:

  ... ./build/sentrits host serve 2>&1 | rg 'core.node|summary.transition'

  If you also want the older server trace:

  tail -f /tmp/sentrits-server-trace.log

  What this does not log yet
  It does not yet log:

  - alt-screen transitions
  - raw semantic extraction
  - raw PTY snapshots in a structured way

  So right now this trace is best for:

  - “what did Sentrits think the session state became?”
    not:
  - “what exact terminal bytes caused it?”

  If you want, next I can give you a very short Codex-specific smoke script: prompt, wait states, file-change
  trigger, and what transitions you should expect to see.
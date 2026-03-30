# Known Limitations

This file tracks current, intentional limitations in the runtime and client control model so they are easy to find without overloading the main architecture docs.

## Active Session Initial Rendering

When a client attaches to an already-running interactive session, the first render may be incomplete or visually wrong until the controlled program produces another repaint.

Why this happens:

- the current attach model provides recent terminal output bytes plus live streaming
- it does not provide a canonical server-side terminal screen snapshot
- shells and TUIs often need a fresh repaint to reconstruct the visible screen cleanly

What this means in practice:

- host local attach can still show an imperfect first frame after connection
- web and iOS focused control can also show an imperfect first frame after control is granted
- typing something or otherwise causing the controlled program to repaint often makes the screen become correct

Current policy:

- one redraw is allowed on initial local host attach
- automatic redraw on every control handoff is intentionally avoided

Why handoff redraw is not forced:

- repeated `Ctrl+L`-style redraws can produce unstable scrollback
- some shells and TUIs respond by inserting large blank regions
- automatic redraw during control switching caused more inconsistency than it solved

Proper long-term fix:

- maintain a server-side terminal screen model per session
- seed new controller/observer clients from that screen snapshot
- continue live streaming after the snapshot seed

## Host Reclaim Shortcut Coverage

Host-local attach uses an explicit reclaim gesture instead of inferring reclaim from arbitrary stdin.

Current reclaim gesture:

- `Ctrl-]`

Current support level:

- supported for the classic raw control-byte path
- supported for common enhanced encodings such as CSI-u and `modifyOtherKeys`
- not guaranteed for every terminal-specific keyboard protocol

Known limitation:

- some terminals may encode `Ctrl-]` in a way the runtime does not currently recognize
- Ghostty is a known example where reclaim may still fail depending on active keyboard protocol mode

Why this is acceptable for now:

- implicit reclaim from arbitrary stdin is incorrect because focus and terminal protocol responses can look like user input
- explicit reclaim is the right model even if terminal-encoding coverage is still incomplete

Possible future improvement:

- broaden reclaim-sequence recognition for additional common macOS and Linux terminals
- or expose a second explicit reclaim path outside raw terminal stdin

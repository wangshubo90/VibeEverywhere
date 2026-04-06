# Packaging Option 1 Prompt

Work in `Sentrits-Core` with awareness of:

- Web client repo: https://github.com/shubow-sentrits/Sentrits-Web
- iOS client repo: https://github.com/shubow-sentrits/Sentrits-IOS

Goal:

- Package Sentrits as a daemon-first product for macOS and Debian.
- Keep the existing runtime architecture: background service, CLI management, and static web assets served by the runtime itself.
- Do not pivot to a GUI-app-first packaging model in this phase.

Product shape:

1. Runtime daemon

- Long-running background service.
- Owns session lifecycle, pairing, auth, discovery, HTTP API, WebSocket observer/control, and static asset serving.
- Existing runtime remains the source of truth for host state and remote access.

2. CLI

- Primary operator-facing management surface.
- Used to start/stop/status the daemon, inspect sessions, manage pairing/auth, and perform local attach/control flows.
- CLI should work cleanly against the local daemon rather than duplicating daemon behavior inside ad hoc commands.

3. Web client delivery

- Build the maintained web client into static assets ahead of packaging.
- Install those assets with the runtime package.
- Serve them using the existing Sentrits runtime web layer.
- Host admin UI remains local to the host.
- Remote client UI remains exposed by the host runtime over the existing HTTP surface.

Packaging direction:

1. macOS

- Install runtime binary plus static web assets in a stable app/support location.
- Provide `launchd` service integration for background startup and restart.
- Keep CLI available on `PATH` or via a documented install path.
- Treat any future dock/menu-bar shell as optional convenience only, not the core deployment target.

2. Debian

- Produce a `.deb` package first.
- Provide a `systemd` service unit for the daemon.
- Install static web assets under a stable runtime-owned directory.
- Separate config, state, and logs into conventional system locations.

Suggested filesystem model:

- binary:
  - macOS: app/support or packaged runtime install path
  - Debian: `/usr/bin/sentrits`
- static assets:
  - Debian example: `/usr/lib/sentrits/www`
- config:
  - Debian example: `/etc/sentrits`
- persistent state:
  - Debian example: `/var/lib/sentrits`
- logs:
  - Debian example: journald by default, file logging only if explicitly configured

Release/build integration:

- Do not add `Sentrits-Web` as a git submodule for this packaging path.
- Prefer a release/staging step that:
  1. builds `Sentrits-Web`
  2. copies the built static assets into a packaging/staging directory for `Sentrits-Core`
  3. builds the daemon package with those staged assets

Why this option:

- Aligns with the current architecture and codebase.
- Works for both headless and interactive host deployments.
- Avoids early GUI lifecycle complexity.
- Keeps remote access, pairing, and asset serving in the runtime where they already belong.

Non-goals for this packaging phase:

- No GUI-app-first packaging as the primary product surface.
- No taskbar/dock wrapper as a required dependency.
- No internet relay packaging changes.
- No redesign of discovery, pairing, or terminal transport just for packaging.

Deliverable:

- A daemon-first packaging plan and implementation path for macOS and Debian.
- Static web asset build/staging integrated into runtime packaging.
- Service definitions for `launchd` and `systemd`.
- CLI and daemon responsibilities kept explicit and separate.

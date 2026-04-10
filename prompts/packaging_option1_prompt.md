# Packaging Option 1 Prompt

Work in `Sentrits-Core` with awareness of:

- Web client repo: https://github.com/shubow-sentrits/Sentrits-Web
- iOS client repo: https://github.com/shubow-sentrits/Sentrits-IOS

Goal:

- Package Sentrits as a daemon-first product for macOS and Debian.
- Keep the existing runtime architecture: background service, CLI management, and static web assets served by the runtime itself.
- Do not pivot to a GUI-app-first packaging model in this phase.
- Target an unprivileged, logged-in-user service model on both macOS and Linux, not a root-owned system service.
- Service lifetime is session-bound: the daemon starts in the logged-in user context and does not persist beyond logout.

Product shape:

1. Runtime daemon

- Long-running background service.
- Runs as the local logged-in user rather than as `root`.
- Owns session lifecycle, pairing, auth, discovery, HTTP API, WebSocket observer/control, and static asset serving.
- Existing runtime remains the source of truth for host state and remote access.

2. CLI

- Primary operator-facing management surface.
- Used to start/stop/status the daemon, inspect sessions, manage pairing/auth, and perform local attach/control flows.
- CLI should work cleanly against the local daemon rather than duplicating daemon behavior inside ad hoc commands.

3. Web client delivery

- Build the maintained web client into static assets ahead of packaging.
- Pin the web client input for each packaging run to an explicit revision. For now, use the current `main` head from `Sentrits-Web` and record that revision in the packaging/staging flow.
- Install those assets with the runtime package.
- Serve them using the existing Sentrits runtime web layer.
- Host admin UI must remain local to the host by binding only on loopback.
- Remote client UI remains exposed by the host runtime over the existing HTTP surface.

Packaging direction:

1. macOS

- Install runtime binary plus static web assets in a stable app/support location.
- Provide per-user `launchd` integration for background startup and restart.
- Do not model macOS setup as a root-owned system daemon install.
- Define an explicit bootstrap path where the logged-in user installs or enables the `launchd` agent, preferably via the CLI.
- Keep CLI available on `PATH` or via a documented install path.
- Treat any future dock/menu-bar shell as optional convenience only, not the core deployment target.

2. Debian

- Produce a `.deb` package first.
- Provide a user-scoped `systemd` service unit for the daemon rather than a system service.
- The Linux user service runs only for the logged-in session. Do not require or enable linger.
- The `.deb` should install shared binaries, assets, and service templates, but should not assume it can directly enable a user service for every account at package install time.
- Define an explicit bootstrap path where the local user installs or enables the user service after package installation, preferably via the CLI.
- Install static web assets under a stable runtime-owned directory.
- Separate config, state, and logs into conventional user-scoped locations.

Suggested filesystem model:

- binary:
  - macOS: app/support or packaged runtime install path
  - Debian: `/usr/bin/sentrits`
- static assets:
  - Debian example: `/usr/lib/sentrits/www`
- launchd agent:
  - macOS example: packaged per-user agent plist installed or linked under `~/Library/LaunchAgents`
- service template:
  - Debian example: packaged user unit template under `/usr/lib/systemd/user/sentrits.service`
- config:
  - Debian example: user-scoped config under `~/.config/sentrits`
- persistent state:
  - Debian example: user-scoped state under `~/.local/share/sentrits`
- logs:
  - Debian example: user journal by default, file logging only if explicitly configured

Release/build integration:

- Do not add `Sentrits-Web` as a git submodule for this packaging path.
- Prefer a release/staging step that:
  1. resolves and records the `Sentrits-Web` revision to package
  2. builds `Sentrits-Web` at that revision
  3. copies the built static assets into a packaging/staging directory for `Sentrits-Core`
  4. builds the daemon package with those staged assets

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
- Service definitions for per-user `launchd` and user-scoped `systemd`.
- Explicit session-lifetime behavior with no persistence after logout.
- CLI and daemon responsibilities kept explicit and separate.
- Explicit binding behavior showing host admin UI on loopback only.
- Explicit package/runtime assumptions for user-scoped config, state, and logs.
- Explicit macOS bootstrap/install behavior showing how the per-user `launchd` agent is installed, enabled, and started by the logged-in user.
- Explicit Debian bootstrap/install behavior showing how the packaged user unit is installed, enabled, and started by the local user after `.deb` installation.

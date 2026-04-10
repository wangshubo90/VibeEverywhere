# Packaging Architecture

This document defines the current packaging direction for Sentrits.

It is intentionally focused on the real near-term packaging shape, not on speculative desktop-shell ideas.

## Current Direction

Sentrits is packaging toward a daemon-first product.

The runtime daemon is the product core.

Packaging should center around:

- the `sentrits` daemon
- the CLI as the operator-facing management surface
- static web assets built separately and served by the runtime

This direction matches the current codebase and the current runtime/client split.

## Product Shape

### 1. Runtime Daemon

Responsibilities:

- own session lifecycle
- own pairing and authorization
- own session inventory and snapshots
- own observer and controller WebSocket surfaces
- serve runtime-facing HTTP APIs
- serve static client assets packaged with the host install

### 2. CLI

Responsibilities:

- start and inspect the daemon
- manage sessions locally
- provide host-local observe/control paths
- act as the primary operator/admin surface outside the host web UI

### 3. Web Assets

The maintained Remote Web Client is built outside this repo:

- https://github.com/shubow-sentrits/Sentrits-Web

Packaging direction:

- build the Remote Web Client into static assets
- stage those assets into the runtime package layout
- let the runtime serve them

The in-repo `frontend/` workspace remains the Host Admin UI surface.

Current web-surface split:

- Host Admin UI:
  - host-local administration surface
  - pairing approval, trusted-device management, host configuration, session cleanup and supervision
- Remote Web Client:
  - full browser client
  - host discovery/manual host entry, pairing, session inventory, session attach, and terminal interaction

## Why This Direction

This shape is the best fit for the current system because:

- the daemon already owns the real runtime truth
- the CLI already exists as a real management surface
- remote observe/control already depends on runtime-served APIs
- static client assets are a deployment concern, not a separate product core

It also avoids prematurely coupling runtime lifecycle to a GUI shell.

## Deployment Targets

Primary near-term targets:

- macOS
- Debian first on Linux

### macOS

Target shape:

- installed runtime binary
- packaged static assets
- per-user `launchd` agent integration
- optional higher-level app shell later, but not required for the core product

Bootstrap model:

- install shared runtime bits
- install or link a per-user plist under `~/Library/LaunchAgents`
- let the logged-in user enable and start it explicitly, preferably through the CLI
- current CLI bootstrap path: `sentrits service install`
- service is session-bound and does not persist after logout

### Debian

Target shape:

- installed runtime binary
- packaged static assets
- user-scoped `systemd` service unit
- `.deb` package first

Bootstrap model:

- the `.deb` installs shared files, including the user-unit template
- package install does not try to enable the unit for arbitrary accounts
- the local user enables and starts the user unit explicitly, preferably through the CLI
- current CLI bootstrap path: `sentrits service install`
- service is session-bound and does not use linger

## Filesystem Model

Representative install layout:

- binary:
  - `/usr/bin/sentrits` on Debian
- static web assets:
  - runtime-owned packaged asset directory such as `/usr/lib/sentrits/www`
- macOS launch agent:
  - `~/Library/LaunchAgents/io.sentrits.agent.plist`
- Debian user unit template:
  - `/usr/lib/systemd/user/sentrits.service`
- config:
  - user-scoped config location
- state:
  - user-scoped persistent state location
- logs:
  - user-scoped journal or explicitly configured file logging

Exact install paths can vary by platform, but the split between binary, assets, config, and state should stay explicit.

## CI / Release Flow

The recommended release pipeline is owned by `Sentrits-Core`.

High-level flow:

1. build and test `Sentrits-Core`
2. fetch the pinned `Sentrits-Web` revision
3. build the web client static assets
4. copy those assets into the runtime packaging/staging tree
5. generate platform installer artifacts

Current repo support:

- the runtime can load packaged assets from a compiled default web root
- build integration generates service templates for `launchd` and `systemd --user`
- a `sentrits_stage_web_assets` target stages the neighboring `../Sentrits-Web/dist` output into `build/packaging/www`
- staged packaging records the exact `Sentrits-Web` revision used for the asset bundle
- a Linux `sentrits_package_deb` target can generate a first-pass `.deb` from the current build tree

Recommended revision model:

- keep a manifest or pinned ref in `Sentrits-Core`
- package a specific `Sentrits-Web` revision deliberately

Do not make GitHub release zips or submodules the required default packaging dependency.

## What Packaging Is Not Doing Yet

This packaging direction is not choosing:

- a GUI-app-first desktop shell as the primary product
- a browser-only product model
- multi-user browser account management
- internet relay packaging

Those may come later, but they are not the current packaging center of gravity.

## Relationship To Maintained Clients

Maintained client repos:

- Web: https://github.com/shubow-sentrits/Sentrits-Web
- iOS: https://github.com/shubow-sentrits/Sentrits-IOS

Current packaging implication:

- the web client is part of host packaging through staged static assets
- the Host Admin UI and Remote Web Client remain intentionally separate surfaces even when both are packaged with the host
- the iOS client remains independently packaged and distributed

## Bottom Line

The near-term Sentrits package should be:

- daemon-first
- CLI-managed
- static-web-asset-backed
- service-friendly on macOS and Debian

That is the cleanest path aligned with the current code and current product architecture.

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

The maintained browser remote client is built outside this repo:

- https://github.com/shubow-sentrits/Sentrits-Web

Packaging direction:

- build the web client into static assets
- stage those assets into the runtime package layout
- let the runtime serve them

The in-repo `frontend/` workspace remains the host-admin surface.

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
- `launchd` service integration
- optional higher-level app shell later, but not required for the core product

### Debian

Target shape:

- installed runtime binary
- packaged static assets
- `systemd` service unit
- `.deb` package first

## Filesystem Model

Representative install layout:

- binary:
  - `/usr/bin/sentrits` on Debian
- static web assets:
  - runtime-owned packaged asset directory
- config:
  - daemon-owned config location
- state:
  - daemon-owned persistent state location
- logs:
  - system-managed logging by default

Exact install paths can vary by platform, but the split between binary, assets, config, and state should stay explicit.

## CI / Release Flow

The recommended release pipeline is owned by `Sentrits-Core`.

High-level flow:

1. build and test `Sentrits-Core`
2. fetch the pinned `Sentrits-Web` revision
3. build the web client static assets
4. copy those assets into the runtime packaging/staging tree
5. generate platform installer artifacts

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
- the iOS client remains independently packaged and distributed

## Bottom Line

The near-term Sentrits package should be:

- daemon-first
- CLI-managed
- static-web-asset-backed
- service-friendly on macOS and Debian

That is the cleanest path aligned with the current code and current product architecture.

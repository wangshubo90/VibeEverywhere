# Packaging Architecture

This document defines how VibeEverywhere should be packaged across host and client surfaces.

It is not the final distribution spec for every platform. It is the product and systems decision that should guide the next implementation phase.

## Current Decision

For now, package the host-side system as one installable host application that bundles:

- `sentrits`
- the host admin frontend
- the maintained web client frontend
- the browser discovery helper

Also support a daemon-focused deployment mode:

- `sentrits --no-client`

That mode should suppress launching or serving bundled client-facing browser surfaces when the deployment only needs the runtime daemon.

## Why This Is The Right Near-Term Shape

### 1. iOS Is Already The Clean Client Story

The iOS client is the easiest standalone client:

- true native app
- App Store distribution
- native UDP discovery
- direct remote session control

That means we do not need to over-optimize the browser client as a long-term universal answer right now.

### 2. Browser Discovery Is Still Architecturally Awkward

The web client cannot receive UDP discovery directly.

That means browser active discovery needs a helper bridge.

Bundling that helper into the host-side package is acceptable for now because:

- it hides operational complexity from users
- it preserves the current web client workflow
- it avoids pretending the browser can do true native discovery

### 3. Single-User Home Network Is The Primary Near-Term Case

For home or personal LAN use, the easiest operational model is:

- install one host app on each host device
- run one client surface from one machine when needed
- pair once and reuse

This does not require a multi-user browser account system yet.

### 4. Multi-User Intranet Browser Identity Is Expensive

If multiple users share one browser client surface, we would need one of these:

- per-user login and credential model
- per-user device auth and token management inside the browser client
- stronger host/user separation across sessions

That is much more setup and policy complexity than we need right now.

So the near-term choice should avoid forcing full browser multi-user management too early.

### 5. Internet Or Tunnel Support Likely Changes Discovery Anyway

If we later add:

- internet access
- tunnels
- brokered connectivity

then UDP active discovery becomes less important for the browser client.

At that point the browser client may evolve toward:

- known-host management
- paired-host inventory
- remote control over authenticated network routes

That is another reason not to over-invest in browser-local discovery semantics as a permanent architecture.

## Product Packaging Model

## Host Package

The host package should be the main installable desktop product.

Responsibilities:

- launch and supervise `sentrits`
- expose host admin UI
- expose bundled web client UI
- run the browser discovery helper if the bundled web client needs it
- own logs, storage, certificates, pairing store, and daemon lifecycle

User mental model:

- one app per host machine
- not separate daemon, helper, and frontend processes the user must manage manually

## iOS Client

The iOS client remains separate.

Responsibilities:

- discover hosts directly over UDP
- pair and reconnect
- observe and control sessions remotely

The iOS app should not depend on the browser helper or the bundled web client.

## Web Client

The bundled web client should be treated as:

- a practical remote or host-local browser surface
- useful for current intranet workflows
- potentially temporary or secondary long-term compared with native clients

It should still remain a maintained product surface, but we should avoid forcing the whole system architecture to revolve around browser constraints.

Current repo split:

- maintained browser remote client: `/Users/shubow/dev/VibeEverywhere-Client`
- maintained in-repo host-admin workspace: `frontend/`
- deprecated daemon-served plain HTML browser assets: `deprecated/web/`

## Deployment Modes

### Mode A: Full Host App

Default mode.

Runs:

- `sentrits`
- host admin UI
- bundled web client UI
- discovery helper if enabled

Best for:

- personal machines
- home LAN
- development
- demos

### Mode B: Daemon Only

Enabled with:

- `sentrits --no-client`

Runs:

- `sentrits` only

Suppresses:

- bundled client UI launch
- bundled browser helper launch
- any host-side browser auto-open behavior

Best for:

- intranet deployments where one separate client machine is used
- headless or service-style setups
- future brokered/tunneled environments

## Process Model

Recommended host package process structure:

- host launcher process or desktop app shell
- `sentrits` runtime daemon
- optional bundled discovery helper child process
- browser window or embedded webview for host admin and web client

Important rule:

- the launcher owns process supervision
- users should not have to manually run the helper as a visible tool

## Frontend Serving Model

Near-term recommendation:

- keep host admin UI and bundled web client assets shipped with the host package
- let the host package expose them through the daemon or a tightly coupled local server

Practical behavior:

- host admin remains localhost-oriented
- bundled web client can still connect to remote listeners and paired hosts
- `--no-client` disables client-facing bundle behavior for daemon-only deployments

## Discovery Helper Position

For now:

- keep the helper
- bundle it with the host package
- hide it from users

Do not treat it as a standalone end-user product.

Longer-term possibilities:

- remove it if browser active discovery becomes less important
- replace it if the host package gains a better integrated discovery bridge

## Multi-User Position

We are explicitly not solving full browser multi-user auth yet.

Near-term assumptions:

- one trusted user or one small trusted operator group
- per-device pairing remains the primary auth model
- browser client identity management stays simple

If we later need:

- true per-user roles
- shared browser client deployments
- centrally managed identities

that should be a separate auth and product milestone, not hidden inside packaging work.

## Installer Direction

Platform direction for later implementation:

- macOS:
  - `.app` bundle first
  - optional `.dmg` wrapper
- Linux:
  - service plus AppImage, deb, or rpm later

The packaging architecture decision here is more important than the first installer format.

## Immediate Follow-Up Work

1. define the host launcher/app-shell process model
2. document storage/layout for bundled runtime, helper, and frontend assets
3. decide whether host admin and bundled web client are opened in browser tabs or an embedded webview
4. keep iOS fully independent from any browser-helper assumptions

## Bottom Line

For now:

- bundle `sentrits`, host admin UI, web client, and discovery helper together
- keep the helper hidden inside the host package
- support `sentrits --no-client` for daemon-only deployments

This fits the current product reality:

- iOS is the clean native client
- browser discovery is still a compromise
- single-user and small-LAN operation matter more than enterprise multi-user browser setup today

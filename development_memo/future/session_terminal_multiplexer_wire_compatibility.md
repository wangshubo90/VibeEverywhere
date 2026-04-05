# Session Terminal Multiplexer Wire Compatibility

## Purpose

This document defines the compatibility contract for the terminal multiplexer refactor.

The runtime will evolve internally, but current clients must continue to work during migration.

## Rule

The multiplexer refactor is additive first.

No existing client should need to switch all at once from:

- current REST endpoints
- current observer WebSocket events
- current controller WebSocket events

to a brand-new protocol.

## Current Compatibility Baseline

During migration, the runtime must preserve the current externally visible contract used by Sentrits-Web and Sentrits-IOS.

That includes:

### REST

- `GET /sessions`
- `GET /sessions/{sessionId}`
- `GET /sessions/{sessionId}/snapshot`
- `GET /sessions/{sessionId}/tail`
- `POST /sessions/{sessionId}/input`
- `POST /sessions/{sessionId}/resize`
- `POST /sessions/{sessionId}/stop`
- `POST /sessions/{sessionId}/control`

### Observer WebSocket events

- `terminal.output`
- `session.updated`
- `session.activity`
- `session.exited`
- `error`

### Controller WebSocket events

- `controller.ready`
- `controller.rejected`
- `controller.released`
- `error`

### Metadata fields that current clients depend on

- `controllerKind`
- `controllerClientId`
- `ptyCols`
- `ptyRows`
- `currentSequence`
- supervision and attention fields

## Terminology Migration Rule

Internally, runtime code may move from `controller` terminology to `stdin owner`.

Externally, wire-level controller vocabulary must remain valid until both web and iOS clients are migrated.

That means these current semantics remain true during migration:

- `controller.ready` still means the client has obtained the exclusive input path
- `controller.released` still means it no longer has that path
- `controllerKind` and `controllerClientId` remain populated for current clients

If new `stdinOwner*` fields are added later, they should be additive, not replacement, until old clients are retired.

## Snapshot Migration Rule

Current `/snapshot` behavior is compatibility-sensitive.

Today it is centered on:

- session metadata
- sequence watermark
- bounded recent terminal tail
- current git summary
- recent file changes

The multiplexer refactor may add:

- canonical screen snapshot
- bounded scrollback
- render revision metadata

But it should not silently remove or redefine existing fields while current clients still consume them.

Recommended approach:

- keep existing `/snapshot` fields
- add new fields alongside them
- document which fields are legacy compatibility fields

## Live Update Migration Rule

The current observer stream is based on raw `terminal.output` plus metadata events.

The multiplexer refactor may add new rendered-state events.

Recommended approach:

- dual-publish during migration
- keep `terminal.output` for compatibility
- add new render-oriented events under distinct event types
- migrate clients one by one

Do not replace `terminal.output` in place during the first rollout.

## Client Migration Order

Recommended order:

1. runtime adds canonical emulator state and additive APIs
2. web focused view migrates first
3. iOS focused view migrates second
4. preview surfaces migrate after focused views are stable
5. legacy compatibility events are removed only after both clients no longer depend on them

## Required Compatibility Tests

The multiplexer rollout should add compatibility tests for:

1. old observer attach still receives `terminal.output`
2. old controller lane still receives `controller.ready`
3. `controllerKind` and `controllerClientId` remain stable across handoff
4. `ptyCols` and `ptyRows` stay populated
5. old `/snapshot` consumers still parse and recover

## Non-Goals For V1 Migration

The rollout does not need to:

- migrate every client surface at once
- remove the old controller vocabulary immediately
- expose raw replay as a new guaranteed public API
- make semantic monitor outputs part of the required compatibility story

## Summary

The runtime may become multiplexer-based internally.

But externally, V1 migration should preserve the current Sentrits-Web and Sentrits-IOS contract until the new snapshot/render path is proven and adopted.

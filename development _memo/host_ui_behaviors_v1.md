# Host UI Behaviors V1

This document freezes the behavior-first design for the localhost host admin UI.

## Purpose

The host UI is the local control room.

It exists to let the host owner:

- inspect host status
- approve pairing requests
- manage trusted devices
- edit host config
- create sessions
- stop or clear sessions
- inspect session/client state

It is not a session terminal.

## Core Objects

The host UI operates on:

- host config
- pending pairing requests
- trusted devices
- sessions
- attached clients

## Required Behaviors

### Host Status and Config

The UI must:

- show host name and listener endpoints
- show remote TLS status
- allow editing host config fields
- allow downloading the remote certificate when configured

Saving config should not require leaving the page.

### Pairing

The UI must:

- show pending pairing requests
- show enough metadata to identify the requesting device
- allow approving a request

Future reject support is optional, not required for v1.

### Trusted Device Management

The UI must:

- list all trusted devices
- allow removing a trusted device
- allow expiring its token/session access

The host should not need to edit files manually to revoke access.

### Session Creation

The UI must allow creating a session directly.

Required inputs:

- provider
- title
- workspace root
- optional conversation id
- optional explicit command override

On success:

- the session appears immediately in inventory
- the host can copy a CLI attach/resume helper

### Session Inventory

The UI must:

- show all sessions
- keep the list live via subscription
- distinguish live, ended, and archived records
- show lifecycle, attention, controller, client count, and recent activity

It should support operational actions:

- stop session
- clear ended/archived records
- inspect files
- copy session id
- copy CLI command

### Client Management

The UI must:

- list attached clients
- show which session each client belongs to
- show controller vs observer role
- allow disconnecting a client

## Interaction Rules

- destructive actions must be clearly labeled
- session stop should affect runtime, not just detach clients
- clearing ended/archived must never remove live sessions
- browser-based host attach is out of scope
- local terminal workflows stay in the native CLI

## Information Priority

Highest priority:

- pending pairings
- trusted device safety
- sessions needing attention

Secondary:

- file inspection
- event logs
- copy helpers

## Layout Implications

- host status/config and trust management can live in the left/upper utility area
- session inventory is the primary panel
- attached clients and file inspection are secondary operational panels
- event log stays visible but de-emphasized

## Non-Goals

- browser terminal attach
- mobile-first optimization
- workspace tree browsing
- editing files or git state

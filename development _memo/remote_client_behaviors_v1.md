# Remote Client Behaviors V1

This document freezes the behavior-first design for the remote web client.

## Purpose

The remote client is for supervision and intervention from non-host devices.

It exists to let a paired remote user:

- browse sessions
- sort and inspect them
- open one or more session connections in-page
- watch terminal output
- request or release control
- inspect file and git state read-only

It is not a host admin console.

## Core Objects

The remote client operates on:

- trusted host identity/token
- session inventory
- open session connection tabs
- selected session detail

## Required Behaviors

### Trust and Reconnect

The UI must:

- request pairing when no token exists
- reuse saved token when present
- avoid repeated manual token copy/paste

After trust is established, pairing should become background/setup behavior.

### Session Inventory

The UI must:

- subscribe to live session inventory updates
- show compact session cards
- support user-selected sort mode
- default to attention-first sorting

Required sort modes:

- attention
- recent activity
- recent output
- created time
- title
- provider

### In-Page Multi-Session Tabs

The UI must:

- allow opening multiple session tabs within the page
- keep each tab independently connectable/disconnectable
- allow closing a tab without affecting the session itself

Browser tabs should not be the primary multi-session mechanism.

### Session Connection

The UI must:

- connect as observer first
- show connection state clearly
- request control explicitly
- release control explicitly
- stop the session if allowed

### Session Detail

The UI must show compact but useful detail:

- lifecycle
- attention state and reason
- controller state
- recent output/activity ages
- file change summary
- git summary

Detailed inspection should be available without overwhelming the inventory card.

### Terminal

The UI must:

- render terminal output correctly
- work on narrow/mobile layouts
- keep controls accessible without excessive scrolling

Terminal remains important, but supervision information should remain readable even before connecting.

## Interaction Rules

- connection and control are separate actions
- control should never be implied by merely opening a session tab
- remote control is normal behavior, not a warning by itself
- read-only inspection should remain separate from mutating terminal actions

## Information Priority

Highest priority:

- sessions needing attention
- selected session status/control
- open connection tabs

Secondary:

- full event log
- expanded file content
- expanded git detail

## Layout Implications

- mobile-first vertical layout
- compact inventory cards
- sticky or easy-to-reach selected session header
- visible in-page session tab strip
- detail and inspection below the primary session state/control area

## Non-Goals

- host configuration
- pairing approval
- trusted-device management
- browser-tab-centric multi-session flow
- editing files or git state

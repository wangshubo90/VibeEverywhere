# Frontend Workspace

This directory contains the Angular workspace for the maintained web frontends.

## Projects

- `projects/host-admin`
  - localhost-only host control room
  - pairing, trusted devices, config, session creation, session management

- `projects/remote-client`
  - paired remote supervision client
  - compact inventory, in-page session tabs, terminal observe/control, read-only inspection

- `projects/shared-ui`
  - shared presentation helpers and later reusable UI primitives

- `projects/session-model`
  - shared frontend-facing types and temporary mock data

## Commands

Run from `frontend/`.

```bash
npm install
npm run build:libs
npm run build:host-admin
npm run build:remote-client
```

Development servers:

```bash
npm run start:host-admin
npm run start:remote-client
```

Tests:

```bash
npm run test:host-admin
npm run test:remote-client
```

## Notes

- use an LTS Node release for normal frontend development
- the current scaffold builds on newer odd-numbered Node releases, but that is not the preferred baseline
- the existing static smoke pages remain the runtime reference until Angular reaches feature parity

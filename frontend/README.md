# Frontend Workspace

This directory contains the in-repo host-admin frontend workspace.

It is not the maintained browser remote client product.

Maintained browser client repo:

- https://github.com/shubow-sentrits/Sentrits-Web

## Current Role

This workspace is for the host-local admin surface:

- pairing approval
- trusted device management
- host configuration
- session creation and cleanup
- host-side session supervision

## Commands

Run from `frontend/`:

```bash
npm install
npm run build:libs
npm run build:host-admin
```

Development server:

```bash
npm run start:host-admin
```

## Notes

- use an LTS Node release for normal frontend development
- deprecated daemon-served plain HTML browser assets live under `../deprecated/web/`
- the packaged remote browser client should be built from `https://github.com/shubow-sentrits/Sentrits-Web`, not from this workspace

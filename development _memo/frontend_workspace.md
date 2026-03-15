# Frontend Workspace

This document describes the initial Angular workspace layout and the intended role of each project.

## Workspace Root

`frontend/`

## Projects

### Applications

- `host-admin`
  - localhost-only admin console
  - pairing, trusted devices, config, session creation, session management

- `remote-client`
  - paired remote supervision client
  - compact inventory, in-page session tabs, terminal observe/control, read-only inspection

### Libraries

- `session-model`
  - shared frontend-facing session/device/tab interfaces
  - mock data and stable UI-facing types during early development

- `shared-ui`
  - shared presentation helpers and later reusable components

## Commands

Run from `frontend/`.

- `npm install`
- `npm run start:host-admin`
- `npm run start:remote-client`
- `npm run build:host-admin`
- `npm run build:remote-client`
- `npm run build:libs`
- `npm run test:host-admin`
- `npm run test:remote-client`

## Development Notes

- keep runtime truth on the server
- keep client-side state focused on view composition and interaction state
- do not duplicate lifecycle or attention inference logic in Angular
- keep host-admin and remote-client separate apps even if they share libraries

## Next Steps

1. replace mock data with typed API services
2. wire host-admin forms and inventory to real endpoints
3. wire remote-client inventory and connection tabs to real REST/WebSocket flows
4. add reusable shared-ui primitives only when repetition becomes real

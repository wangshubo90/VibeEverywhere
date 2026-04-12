# Device Inventory Cleanup Prompt

Work across the maintained clients:

- Web client repo: `https://github.com/shubow-sentrits/Sentrits-Web` or `../Sentrits-Web`
- iOS client repo: `https://github.com/shubow-sentrits/Sentrits-IOS` or `../Sentrits-IOS`

Use the runtime API only as needed for truth about discovery, trusted devices, and host identity:

- Runtime repo: `https://github.com/shubow-sentrits/Sentrits-Core` or `./Sentrits-Core`

Goal:

Clean up how devices/hosts are presented in inventory and discovery flows.

## Desired Product Behavior

If a device/host is already saved in the local store:

- do not show it again as a "newly discovered" item
- show it in the saved device list
- attach an online/offline badge or equivalent status signal

Active discovery should show only unsaved devices.

Consider renaming the active discovery section to something clearer, such as:

- `New Devices`
- `Unsaved Devices`

The exact label can be decided after checking the existing UX.

## What To Audit

1. Web client
- host store
- discovery results
- trusted/saved host model
- inventory grouping / badges / duplicate rendering paths

2. iOS client
- saved host/device store
- active discovery list
- merge/dedup logic between saved and discovered devices
- online/offline presentation for already known devices

3. Runtime
- discovery payload fields
- host identity fields
- any stable ids that clients should use for deduplication

## Existing Runtime Anchors To Inspect

At minimum inspect:

- `src/net/discovery.cpp`
- `src/net/http_shared.cpp`
- `src/store/file_stores.cpp`
- `development_memo/active_discovery_plan.md`

## Deliverables

Produce:

1. Current-state audit
- how web currently handles saved vs discovered hosts
- how iOS currently handles saved vs discovered hosts
- what runtime fields are available for stable deduplication

2. Concrete deduplication model
- what key identifies "same device/host"
- when a discovered item should merge into a saved item
- when it should remain in the active discovery list

3. UI behavior plan
- saved devices section behavior
- active discovery / new devices behavior
- online/offline badge behavior
- empty-state behavior

4. Implementation plan split by repo
- Sentrits-Web
- Sentrits-IOS
- Sentrits-Core only if needed

## Constraints

- Do not treat this as a broad discovery redesign.
- Use the current host/device model first.
- Keep the user-facing distinction simple:
  - saved known devices
  - newly discovered unsaved devices

## Execution Instructions

- Create a dedicated git worktree before implementation work starts.
- Validate locally with relevant build and test commands before finishing.
- When finished, create a PR using the repo's PR template.

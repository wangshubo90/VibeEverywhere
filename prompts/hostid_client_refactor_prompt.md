# HostId Client Refactor Prompt

Work across these repos on macOS:

- `https://github.com/shubow-sentrits/Sentrits-Web`
- `https://github.com/shubow-sentrits/Sentrits-IOS`
- `https://github.com/shubow-sentrits/Sentrits-Core` only if runtime API or docs need small follow-up changes

Context:

- Runtime `hostId` is now generated and persisted on first boot in Sentrits-Core.
- `hostId` is the canonical stable host identity.
- `displayName` is descriptive metadata only and may duplicate across hosts.
- Paired client/device identity is separate from host identity and is returned as `deviceId` from pairing claim.

Goal:

- Make both maintained clients fully `hostId`-first for discovery, saved-host persistence, pairing state, and host deduplication.
- Remove remaining endpoint-first assumptions where they can cause host duplication, wrong pairing attribution, or stale selection state.

What to inspect first:

- Web:
  - `https://github.com/shubow-sentrits/Sentrits-Web/blob/main/src/lib/host-store.ts`
  - `https://github.com/shubow-sentrits/Sentrits-Web/blob/main/src/lib/app-flows.ts`
  - `https://github.com/shubow-sentrits/Sentrits-Web/blob/main/src/types/index.ts`
  - `https://github.com/shubow-sentrits/Sentrits-Web/blob/main/scripts/discovery-helper.ts`
- iOS:
  - `https://github.com/shubow-sentrits/Sentrits-IOS/blob/main/SentritsIOS/Models/HostModels.swift`
  - `https://github.com/shubow-sentrits/Sentrits-IOS/blob/main/SentritsIOS/Services/HostsStore.swift`
  - `https://github.com/shubow-sentrits/Sentrits-IOS/blob/main/SentritsIOS/Services/KeychainTokenStore.swift`
  - `https://github.com/shubow-sentrits/Sentrits-IOS/blob/main/SentritsIOS/ViewModels/PairingViewModel.swift`
  - `https://github.com/shubow-sentrits/Sentrits-IOS/blob/main/SentritsIOS/Services/DiscoveryListener.swift`

Required outcomes:

1. Web client

- Treat `hostId` as the primary identity key whenever present.
- Audit whether `recordId` should remain endpoint-based or become stable-identity-based.
- Ensure host merge, active-host selection, saved host updates, discovery reconciliation, and paired-token attribution do not collapse distinct hosts that share `displayName`.
- Ensure discovery helper and browser store never use `displayName` as identity.
- Preserve sensible endpoint fallback behavior only when `hostId` is absent.

2. iOS client

- Treat `hostId` as the primary identity for discovered and saved hosts.
- Ensure `UserDefaults` saved-host reconciliation prefers `hostId`.
- Ensure Keychain token persistence keys remain stable once `hostId` is known.
- Decide whether to persist returned `deviceId` for operator/debug visibility and future trust management.
- Ensure duplicate `displayName` values across different hosts remain safe.

3. Cross-client behavior

- Same-name hosts on different IPs must remain distinct if `hostId` differs.
- Same host discovered at a changed IP or through manual entry must merge if `hostId` matches.
- Pairing state and stored token must follow the host by `hostId`, not by mutable display name.

4. Tests

- Add or update tests that cover:
  - same `displayName`, different `hostId`
  - same `hostId`, changed endpoint
  - discovery-to-manual merge by `hostId`
  - pairing/token association by `hostId`
  - endpoint fallback only when `hostId` is missing

Constraints:

- Do not redesign runtime pairing or discovery protocol in this task.
- Do not add a new client-generated host identity.
- Prefer small, explicit changes over broad architectural churn.

Deliverable:

- Code changes in Web and iOS clients
- Updated tests
- Short note listing any remaining endpoint-based assumptions that should be revisited later

# Release Packaging

This document is for maintainers cutting Sentrits releases.

User install and bootstrap docs live in:

- `get_started.md`
- `packaging/debian.md`
- `packaging/macos.md`

## Release Inputs

Sentrits release packaging is driven by:

- the version in `CMakeLists.txt`
- the pinned web client revision in `packaging/sentrits-web-revision.txt`
- the tag pushed from this repo

The packaged web client revision must match the exact `Sentrits-Web` commit you want bundled into the release.

## Release Workflow

The GitHub Actions workflow is:

- `.github/workflows/release-packaging.yml`

It does the following on tag push:

1. reads `packaging/sentrits-web-revision.txt`
2. checks out `Sentrits-Web` at that exact revision
3. builds host-admin assets from this repo
4. builds packaged Debian and macOS artifacts
5. runs install/bootstrap smoke checks
6. generates `SHA256SUMS` covering all release artifacts
7. optionally signs `SHA256SUMS` with GPG (if `GPG_PRIVATE_KEY` secret is configured)
8. uploads all artifacts to the GitHub Release for the tag

## Release Artifacts

Every release produces:

| Artifact | Platform | Notes |
|---|---|---|
| `sentrits-<version>-macos-<arch>.dmg` | macOS | Primary macOS artifact |
| `sentrits-<version>-macos-<arch>.tar.gz` | macOS | Secondary; same install root as DMG |
| `sentrits_<version>_<arch>.deb` | Debian/Ubuntu | Debian package |
| `SHA256SUMS` | All | SHA-256 checksums for all artifacts |
| `SHA256SUMS.asc` | All | Detached GPG signature (optional, when signing key is configured) |

Example for version 0.2.0 on arm64:

```
sentrits-0.2.0-macos-arm64.dmg
sentrits-0.2.0-macos-arm64.tar.gz
sentrits_0.2.0_amd64.deb
SHA256SUMS
SHA256SUMS.asc        (if GPG key configured)
```

## Artifact Verification

```bash
# Verify all downloaded artifacts at once:
sha256sum --check --ignore-missing SHA256SUMS

# If the release is GPG-signed:
gpg --import <sentrits-release-key.asc>
gpg --verify SHA256SUMS.asc SHA256SUMS
```

## Maintainer Steps

1. Update `packaging/sentrits-web-revision.txt` if the bundled web client should move.
2. Ensure `main` contains the release workflow and packaging changes you want.
3. Confirm the version in `CMakeLists.txt` matches the release you intend to cut.
4. Create and push a version tag:

```bash
git tag -a v0.2.0 -m "Sentrits v0.2.0"
git push origin v0.2.0
```

5. Watch the `Release Packaging` workflow in GitHub Actions.
6. Verify the GitHub Release contains all artifacts including `SHA256SUMS`.

## Secrets And Access

`Sentrits-Web` is private, so the release workflow expects:

- `DEPLOY_KEY_SENTRITS_WEB` — SSH deploy key for read access to the `Sentrits-Web` repo (required)

Optional signing secrets:

- `GPG_PRIVATE_KEY` — armored GPG private key for signing `SHA256SUMS`
- `GPG_PASSPHRASE` — passphrase for the GPG key (omit if the key has no passphrase)

If `GPG_PRIVATE_KEY` is absent or empty, the signing step skips cleanly and no
`SHA256SUMS.asc` is produced.

## Future: Notarization

Apple notarization is intentionally not implemented. The hook is already in place in
`cmake/Packaging.cmake` inside the `sentrits_package_macos_dmg` target. When ready:

1. Add `xcrun notarytool submit ... --wait` and `xcrun stapler staple` commands after
   the `hdiutil create` step in `cmake/Packaging.cmake`.
2. Add an optional notarization step in `.github/workflows/release-packaging.yml`
   after "Build macOS packages", gated on an `APPLE_NOTARIZATION_PASSWORD` secret —
   the same conditional pattern used for GPG signing.
3. A Developer ID Application certificate must be available in the CI keychain.

## Notes

- Release packaging uses tag-triggered GitHub Actions only; there is no local release script.
- The macOS DMG is not code-signed or notarized in the current pipeline. Users may need
  to clear Gatekeeper quarantine after downloading. See `packaging/macos.md`.

# macOS Packaging Guide

This document covers the macOS packaging flow, install/bootstrap steps, smoke checks, and uninstall path.

Use `get_started.md` for the shorter quickstart.

## Package Shape

The primary macOS release artifact is a DMG:

- `sentrits-<version>-macos-<arch>.dmg`

The DMG volume contains a self-contained `Sentrits/` install root:

- `Sentrits/bin/sentrits`
- `Sentrits/lib/sentrits/www`
- `Sentrits/share/sentrits/launchd/io.sentrits.agent.plist`

Packaged web UI layout inside the install root:

- `Sentrits/lib/sentrits/www/host-admin`
- `Sentrits/lib/sentrits/www/remote-client`
- `Sentrits/lib/sentrits/www/_metadata/sentrits-web-revision.txt`

A secondary tar.gz archive is also produced alongside the DMG:

- `sentrits-<version>-macos-<arch>.tar.gz`

The tar.gz contains the same `Sentrits/` install root and is retained as a convenience artifact.

## Build The Package

Build the pinned web client revision first so packaged remote assets exist:

```bash
cd ../Sentrits-Web
git checkout "$(cat ../Sentrits-Core/packaging/sentrits-web-revision.txt)"
npm run build
```

Build the host admin assets from this repo if needed:

```bash
cd ../Sentrits-Core/frontend
npm run build:host-admin
```

Build both the DMG and tar.gz from the Core build tree:

```bash
cmake --build build --target sentrits_package_macos_dmg
```

This produces:

```
build/sentrits-<version>-macos-<arch>.dmg
build/sentrits-<version>-macos-<arch>.tar.gz
```

To build the tar.gz only:

```bash
cmake --build build --target sentrits_package_macos
```

## Install From DMG

Open the DMG:

```bash
open build/sentrits-0.2.0-macos-$(uname -m).dmg
```

Or mount it manually:

```bash
hdiutil attach build/sentrits-0.2.0-macos-$(uname -m).dmg
```

Copy the `Sentrits/` folder to a stable user-owned location. Recommended:

```bash
mkdir -p ~/Applications
cp -r "/Volumes/Sentrits 0.2.0/Sentrits" ~/Applications/
hdiutil detach "/Volumes/Sentrits 0.2.0"
```

Optional convenience symlink for `PATH`:

```bash
mkdir -p ~/bin
ln -sf ~/Applications/Sentrits/bin/sentrits ~/bin/sentrits
```

## Alternative: Install From tar.gz

```bash
mkdir -p ~/Applications
tar -xzf ./build/sentrits-0.2.0-macos-$(uname -m).tar.gz -C ~/Applications
```

## Bootstrap The Per-User Agent

Install the per-user `launchd` agent:

```bash
~/Applications/Sentrits/bin/sentrits service install
```

Load it:

```bash
launchctl unload ~/Library/LaunchAgents/io.sentrits.agent.plist 2>/dev/null || true
launchctl load ~/Library/LaunchAgents/io.sentrits.agent.plist
```

## Smoke Test

Check the binary:

```bash
~/Applications/Sentrits/bin/sentrits host status
```

Check daemon reachability:

```bash
curl http://127.0.0.1:18085/health
curl http://127.0.0.1:18085/host/info
curl http://127.0.0.1:18085/
curl http://127.0.0.1:18086/
curl http://127.0.0.1:18086/remote
```

Check the packaged web revision:

```bash
cat ~/Applications/Sentrits/lib/sentrits/www/_metadata/sentrits-web-revision.txt
```

## Uninstall

Unload and remove the agent:

```bash
launchctl unload ~/Library/LaunchAgents/io.sentrits.agent.plist 2>/dev/null || true
rm -f ~/Library/LaunchAgents/io.sentrits.agent.plist
```

Remove the optional CLI symlink:

```bash
rm -f ~/bin/sentrits
```

Remove the installed package root:

```bash
rm -rf ~/Applications/Sentrits
```

## Persistent State

Removing the package root does not clear user state under:

```text
~/.sentrits/
```

If you want a full local reset for that user:

```bash
rm -rf ~/.sentrits
```

## Code Signing and Notarization

The current release artifacts are **not code-signed or notarized**. macOS Gatekeeper may
show a warning on first launch. To bypass:

```bash
xattr -dr com.apple.quarantine ~/Applications/Sentrits/bin/sentrits
```

Or right-click the binary in Finder and choose Open.

### Future: Notarization

Notarization is intentionally deferred. When it is added, the insertion point is in
`cmake/Packaging.cmake` immediately after the `hdiutil create` step in the
`sentrits_package_macos_dmg` target (the comment is already in place):

```cmake
# TODO (notarization): insert after hdiutil create:
#   xcrun notarytool submit "${SENTRITS_MACOS_DMG_PATH}" \
#     --apple-id <APPLE_ID> --team-id <TEAM_ID> --password <APP_PASSWORD> --wait
#   xcrun stapler staple "${SENTRITS_MACOS_DMG_PATH}"
```

In CI, the corresponding step would go in `.github/workflows/release-packaging.yml`
after "Build macOS packages", conditioned on an `APPLE_NOTARIZATION_PASSWORD` secret
being present — the same pattern used for the optional GPG signing step.

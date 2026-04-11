# macOS Packaging Guide

This document covers the current macOS packaging flow, install/bootstrap steps, smoke checks, and uninstall path.

Use `get_started.md` for the shorter quickstart.

## Package Shape

The macOS packaging target currently produces a tarball:

- `sentrits-<version>-macos-<arch>.tar.gz`

The archive contains a self-contained `Sentrits/` install root with:

- `Sentrits/bin/sentrits`
- `Sentrits/lib/sentrits/www`
- `Sentrits/share/sentrits/launchd/io.sentrits.agent.plist`

Packaged web UI layout:

- `Sentrits/lib/sentrits/www/host-admin`
- `Sentrits/lib/sentrits/www/remote-client`
- `Sentrits/lib/sentrits/www/_metadata/sentrits-web-revision.txt`

## Build The Package

Build the maintained web client first so packaged remote assets exist:

```bash
cd ../Sentrits-Web
npm run build
```

Build the host admin assets from this repo if needed:

```bash
cd ../Sentrits-Core/frontend
npm run build:host-admin
```

Then build the macOS package target from the Core build tree:

```bash
cmake --build build --target sentrits_package_macos
```

The generated tarball is written into the build directory.

## Install

Extract the archive into a stable user-owned location. Recommended:

```bash
mkdir -p ~/Applications
tar -xzf ./build/sentrits-0.1.0-macos-$(uname -m).tar.gz -C ~/Applications
```

This yields:

```text
~/Applications/Sentrits/bin/sentrits
```

Optional convenience symlink for `PATH`:

```bash
mkdir -p ~/bin
ln -sf ~/Applications/Sentrits/bin/sentrits ~/bin/sentrits
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

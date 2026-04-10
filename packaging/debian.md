# Debian Packaging Guide

This document covers Debian-specific packaging, install, smoke-test, and uninstall details.

Use `get_started.md` for the shorter product quickstart.

## Package Contents

The Debian package installs:

- binary: `/usr/bin/sentrits`
- packaged web assets: `/usr/lib/sentrits/www`
- user unit template: `/usr/lib/systemd/user/sentrits.service`

Packaged web UI layout:

- host-admin surface: `/usr/lib/sentrits/www/host-admin`
- remote web client: `/usr/lib/sentrits/www/remote-client`
- staged web revision marker: `/usr/lib/sentrits/www/_metadata/sentrits-web-revision.txt`

Runtime serving model after install:

- Host Admin UI is served on the admin listener:
  - `http://127.0.0.1:18085/`
- Remote Web Client is served on the remote listener:
  - `http://127.0.0.1:18086/`
  - `http://127.0.0.1:18086/remote`
  - `http://HOST_IP:18086/`
  - `http://HOST_IP:18086/remote`

Role split:

- Host Admin UI is the host-local administration surface
- Remote Web Client is the full browser client for connecting to hosts and interacting with sessions

## Install

Install the generated package:

```bash
sudo dpkg -i ./build/sentrits_0.1.0_amd64.deb
```

Install and enable the user-scoped service file:

```bash
sentrits service install
systemctl --user daemon-reload
systemctl --user enable sentrits.service
systemctl --user start sentrits.service
```

## Smoke Test

Check binary and service state:

```bash
which sentrits
systemctl --user status sentrits.service --no-pager
```

Check local daemon reachability:

```bash
curl http://127.0.0.1:18085/health
curl http://127.0.0.1:18085/host/info
curl http://127.0.0.1:18085/
```

Check the packaged Remote Web Client and packaged web revision:

```bash
curl http://127.0.0.1:18086/
curl http://127.0.0.1:18086/remote
cat /usr/lib/sentrits/www/_metadata/sentrits-web-revision.txt
```

Optional package inventory check:

```bash
dpkg -L sentrits
```

## Persistent State

There are two configuration layers:

- service file: `~/.config/systemd/user/sentrits.service`
- persistent user state: `~/.sentrits/`

Important persistent files:

- `~/.sentrits/host_identity.json`
- `~/.sentrits/pairings.json`
- `~/.sentrits/sessions.json`

Reinstalling or uninstalling the `.deb` does not clear `~/.sentrits/`.

What this means in practice:

- reinstalling the `.deb` does not reset Sentrits state
- a newly installed binary may still list previously stopped or recovered sessions
- uninstalling the package does not remove `~/.sentrits/`

If you want to clear stopped session records through the daemon:

```bash
sentrits session clear
```

If you want a full local reset for that user:

```bash
rm -rf ~/.sentrits
```

## Uninstall

Stop and disable the user service:

```bash
systemctl --user stop sentrits.service
systemctl --user disable sentrits.service
rm -f ~/.config/systemd/user/sentrits.service
systemctl --user daemon-reload
```

Remove the package:

```bash
sudo dpkg -r sentrits
```

Note:

- `dpkg -r sentrits` removes the package, but leaves `~/.sentrits/` in place
- remove `~/.sentrits/` manually only if you want to wipe host identity, pairings, and persisted session records

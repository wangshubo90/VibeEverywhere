## Packaging Scaffold

This directory holds the first packaging-oriented scaffold for the daemon-first Sentrits layout.

Current intent:

- install the `sentrits` binary as a shared runtime artifact
- stage packaged web assets under a single web root
- keep the Host Admin UI and Remote Web Client as distinct web surfaces
- ship user-service templates for macOS `launchd` and Debian `systemd --user`
- let the logged-in user enable the service explicitly after install

Representative staged layout:

- `www/host-admin` for the host-local admin surface
- `www/remote-client` for the full browser client
- `www/vendor`
- `macos/io.sentrits.agent.plist.in`
- `systemd/sentrits.service.in`

Build integration:

- `cmake` now generates configured service files under `build/generated/packaging`
- `install` places the macOS plist under `share/sentrits/launchd`
- `install` places the Linux user unit under `lib/systemd/user`
- `install` places packaged web assets under `lib/sentrits/www`
- `sentrits_stage_web_assets` stages packaged web assets from `../Sentrits-Web/dist` into `build/packaging/www`
- `sentrits_stage_dev_web_assets` remains available only as a local fallback/dev helper
- `sentrits_package_deb` builds a Debian package from the current Linux build tree
- `sentrits_package_macos` builds a macOS tarball package from the current macOS build tree
- `sentrits service print` renders the per-user service file content for the current platform
- `sentrits service install` writes the per-user service file into the logged-in user's home directory

Linux packaging flow today:

1. configure the build tree
2. build `../Sentrits-Web` on `main` so `dist/` exists
3. build `sentrits_package_deb`
4. collect the generated `.deb` from the build directory

macOS packaging flow today:

1. configure the build tree on macOS
2. build `../Sentrits-Web` on `main` so `dist/` exists
3. build `sentrits_package_macos`
4. collect the generated `.tar.gz` from the build directory

Web staging behavior:

- the stage reads from `../Sentrits-Web`
- the checkout must be on `main`
- the staged package records the exact `Sentrits-Web` revision in `www/_metadata/sentrits-web-revision.txt`
- host-admin assets still come from this repo
- remote web client assets come from `Sentrits-Web`
- vendor assets come from this repo

The runtime now supports packaged asset lookup via:

- `SENTRITS_WEB_ROOT`
- `SENTRITS_HOST_UI_ROOT`
- `SENTRITS_REMOTE_UI_ROOT`
- `SENTRITS_VENDOR_ROOT`

If those are unset, the compiled default packaged web root is used, followed by source-tree development fallbacks.

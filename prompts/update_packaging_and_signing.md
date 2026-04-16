You are working on the Sentrits release pipeline and packaging system.

Project context:
- Sentrits currently produces:
  - macOS archive: sentrits-<version>-macos-<arch>.tar.gz
  - Debian package: sentrits_<version>_<arch>.deb
- Existing packaging docs:
  - packaging/macos.md
  - packaging/debian.md
- Current priorities:
  1. Improve CI/CD and release artifacts
  2. Upgrade macOS release artifact from tar.gz to DMG
  3. Add SHA256 checksums for Debian artifacts
  4. Optionally support GPG signing of release checksums/artifacts
  5. Do NOT implement Apple notarization yet, but design the workflow so notarization can be added later with minimal churn

Your mission:
Design and implement a practical first-stage release pipeline for Sentrits that improves artifact quality and release trust without overengineering.

High-level goals:
1. macOS:
   - Replace the primary release artifact with a DMG-based distribution flow
   - Keep the internal packaged install root shape centered around the existing Sentrits/ directory
   - The DMG should be suitable for GitHub Releases
   - Notarization is explicitly deferred for now
   - However, structure the pipeline and docs so notarization can be inserted later as an optional post-processing stage

2. Debian:
   - Keep producing the .deb package
   - Add SHA256 checksum generation for all release artifacts
   - Add optional GPG signing support for the checksum manifest or artifacts
   - Make GPG signing configurable so CI can run without it unless secrets are present

3. CI/CD:
   - Improve the release workflow so outputs are clearly organized and easy to publish
   - Produce release artifacts and integrity metadata in a predictable layout
   - Avoid blocking the whole pipeline on optional signing steps unless explicitly required
   - Prefer incremental change over big-bang rewrite

Important constraints:
- Do not implement notarization yet
- Do not require a full PKG installer for macOS
- Do not break the current install root assumptions unless necessary
- Keep the design simple and practical for an early-stage product
- Preserve room for future Developer ID signing / notarization on macOS
- Preserve room for future APT repository signing on Debian, but do not implement repo hosting now

What I want from you:
1. Inspect the existing packaging and CI layout in the repo
2. Propose a concrete implementation plan
3. Then implement it
4. Update documentation accordingly

Detailed deliverables:

A. macOS packaging upgrade
- Add a DMG packaging path that wraps the existing Sentrits install root
- The resulting artifact should look like:
  - sentrits-<version>-macos-<arch>.dmg
- Decide what should live inside the DMG. Favor a simple, user-friendly layout such as:
  - Sentrits/
  - optional README / install instructions
  - optional helper install script if appropriate
- Keep the install instructions aligned with the current per-user install/bootstrap model
- If useful, retain tar.gz temporarily as a secondary artifact, but DMG should become the primary macOS release artifact
- Do not add notarization, but clearly mark where it would later fit in the build/release flow

B. Debian release integrity improvements
- Generate SHA256SUMS covering all release artifacts
- Include the .deb and any macOS artifacts in the checksum manifest if the workflow is centralized
- Add optional detached signature or signed checksum manifest support using GPG
- The GPG signing step should:
  - activate only when CI secrets/materials are available
  - otherwise skip cleanly without failing the build
- Prefer signing SHA256SUMS rather than inventing a custom scheme

C. CI/CD workflow improvements
- Update GitHub Actions or the current CI system so release outputs are gathered in a clean artifact directory
- Ensure the release job emits at least:
  - macOS DMG
  - Debian .deb
  - SHA256SUMS
  - optional SHA256SUMS.asc or SHA256SUMS.sig when signing is enabled
- Make the workflow readable and maintainable
- Use clear step names and comments
- Separate build, package, checksum, and optional signing responsibilities as much as practical

D. Documentation updates
- Update packaging/macos.md:
  - describe the new DMG artifact
  - explain install flow from the DMG
  - mention that notarization is not yet enabled
  - add a short “future notarization hook” note so the path is obvious later
- Update packaging/debian.md:
  - add release verification instructions
  - document SHA256 verification
  - document optional GPG verification if signature files are produced
- If needed, add a release-process document for maintainers summarizing:
  - artifact list
  - checksum generation
  - optional signing behavior
  - future notarization insertion point

Implementation guidance:
- Favor practical shell/CMake/GitHub Actions changes over theoretical discussion
- Reuse existing build outputs and packaging structure where possible
- Avoid introducing unnecessary tooling unless it clearly simplifies release management
- If new scripts are added, place them in sensible packaging or CI helper locations
- Keep naming consistent with existing Sentrits packaging conventions

Expected output format:
1. First, give me a concise implementation plan after inspecting the repo
2. Then make the code/doc/workflow changes
3. Summarize:
   - what changed
   - any assumptions you made
   - what is intentionally deferred
   - exactly how notarization can be added later
4. Include example output artifact names and verification commands

Definition of done:
- macOS primary artifact is DMG-based
- Debian/package release flow includes SHA256SUMS
- Optional GPG signing path exists and skips safely when not configured
- Docs are updated
- Notarization is not implemented, but its future integration point is explicitly prepared
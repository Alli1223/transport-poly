# Releasing transport-poly

Releases are built and published by GitHub Actions (`.github/workflows/ci.yml`).
Every push to any branch, and every pull request, is built and tested on Windows
(MSVC) and Linux (GCC 14, plus a headless server-and-tests build with warnings as
errors), and packaged as a zip / tar.gz downloadable from the run's artifacts.
Pushing a version tag also publishes a GitHub release with those packages.

## Cutting a release

1. Check that the latest run on `main` is green.
2. Choose the version, following [semantic versioning](https://semver.org): bump
   the minor version for new features, the patch version for fixes only.
3. Set it in both places that carry it:
   - `project(transport_poly VERSION x.y.z ...)` in `CMakeLists.txt`
   - `kVersionString` in `src/core/version.hpp`
4. In `CHANGELOG.md`, move the entries under **Unreleased** into a new
   `## [x.y.z] - YYYY-MM-DD` section and update the compare links at the bottom.
   This section becomes the release notes.
5. Run `bash scripts/check-version.sh` (Git Bash on Windows): it prints the version,
   or says what disagrees.
6. Commit (`Release x.y.z`) and push `main`.
7. Tag the commit and push the tag:

   ```bash
   git tag -a vx.y.z -m "transport-poly x.y.z"
   git push origin vx.y.z
   ```

CI then builds and tests both platforms again, checks that the tag matches the
version in the code and the changelog, and publishes the release **transport-poly
x.y.z** with these assets:

| File | Contents |
| --- | --- |
| `transport-poly-x.y.z-windows-x64-setup.exe` | Windows installer, folder and install-scope selection, shortcuts and uninstaller |
| `transport-poly-x.y.z-windows-x64.zip` | Portable game, dedicated server, font and the MSVC runtime |
| `transport-poly-x.y.z-linux-x64.tar.gz` | Game and dedicated server (C++ runtime linked in) |
| `SHA256SUMS.txt` | Checksums of the packages |

A tag with a suffix, such as `vx.y.z-rc1`, is published as a pre-release; its
notes come from the `x.y.z` changelog section.

## Building the Windows installer locally

Install Inno Setup 6 or 7, configure `windows-msvc`, then run this from a VS
developer prompt:

```powershell
powershell -File scripts/package-windows.ps1
```

Use `-BuildDir build/another-tree` for a different build tree or `-Iscc` to specify
the compiler. The script builds, stages the installation, compiles Inno Setup,
creates the portable ZIP and writes `SHA256SUMS.txt` in that tree's `packages/`.
CI performs the same steps and publishes both Windows assets on a version tag.

Presets set `TP_UPDATE_FEED` to the public repository's latest-release API. An
empty value disables checks for development builds; the Windows packaging script
requires the production feed. Each update needs its exact versioned platform asset
and `SHA256SUMS.txt`. Release notes come from the GitHub release body. Drafts and
pre-releases are not offered by the stable feed. Keep releases public so players
can check and download without signing in. Upload all packages and checksums before
making the release the latest stable release.

The installer keeps its app ID across versions. Upgrades retain the destination
and per-user/all-user scope. Saves, settings and screenshots are separate from an
installed game and are preserved by uninstalling. Test silent installs can use
`/CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOICONS /DIR="..."`;
only the updater adds `/RESTARTGAME=1` to launch the game after a silent upgrade.

## If the release job fails

Fix the problem on `main`, then move the tag to the fixed commit:

```bash
git push --delete origin vx.y.z
git tag -d vx.y.z
git tag -a vx.y.z -m "transport-poly x.y.z"
git push origin vx.y.z
```

If a release was already created, delete it on GitHub first.

## Save games and multiplayer

The release version is separate from the save format and network protocol.
Bump `kSaveVersion` in `src/sim/world.hpp` whenever saved state changes (gating new
fields on the new version so older saves still load), and `kProtocolVersion` in
`src/core/version.hpp` whenever commands or simulation rules change; clients and
servers must match exactly.

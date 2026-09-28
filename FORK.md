# MakerMylo/helixscreen-tc

A fork of [prestonbrown/helixscreen](https://github.com/prestonbrown/helixscreen)
carrying the Tools panel for tool changers (see `docs/user/guide/tools.md`).

## Installing on the printer

Same as upstream, from this fork's releases:

```bash
curl -sSL https://github.com/MakerMylo/helixscreen-tc/releases/latest/download/install.sh | sh -s -- --update
```

The bundled installer, the in-app updater (Settings > Updates) and Moonraker's
`[update_manager helixscreen]` stanza all point at this fork's GitHub Releases.
Mainsail's update button needs this in `moonraker.conf`:

```ini
[update_manager helixscreen]
type: web
channel: stable
repo: MakerMylo/helixscreen-tc
path: ~/helixscreen
```

## Cutting a release

Releases are built by GitHub Actions (`.github/workflows/release.yml`, Pi 64-bit
only, about two hours cold). Bump `VERSION.txt`, commit, tag the commit with the
same version prefixed by `v`, push the tag:

```bash
echo 1.1.0-tc.2 > VERSION.txt
git commit -am "Release 1.1.0-tc.2"
git tag -a v1.1.0-tc.2 -m "What changed"
git push origin main v1.1.0-tc.2
```

The tag's message becomes the release notes. Versions follow semver, so keep
them above whatever upstream tag the fork is merged with (`1.1.0-tc.N` sorts
above upstream's `1.1.0-beta.N` and below its `1.1.0`).

## Keeping up with upstream

```bash
git remote add upstream https://github.com/prestonbrown/helixscreen.git   # once
git fetch upstream && git merge upstream/main
```

Expect conflicts in `.github/workflows/release.yml` (keep ours) and in the few
files that name the repository: `scripts/bundle-installer.sh`,
`scripts/install-dev.sh`, `scripts/lib/installer/{common,release,moonraker}.sh`,
`mk/cross.mk` (release_info), `src/system/update_checker.cpp`,
`include/system/update_checker.h`. After resolving, regenerate the bundled
installer: `bash scripts/bundle-installer.sh -o scripts/install.sh`.

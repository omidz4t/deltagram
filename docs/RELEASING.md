# Version preparation

`scripts/semantic-release.py` is a dependency-free Python 3 script that prepares
local releases from committed Conventional Commits. It follows these rules:

| Commit | Version change |
| --- | --- |
| `feat:` or `feat(scope):` | Minor; reset patch to zero |
| `fix:` or `perf:` | Patch |
| Any conventional type with `!` or a `BREAKING CHANGE:` / `BREAKING-CHANGE:` footer | Major; reset minor and patch to zero |
| Other conventional types, including `docs`, `build`, `ci`, `test`, `chore` and `refactor` | No release by themselves |

The highest required change wins. Maintenance commits appear in the changelog
when accompanied by a release-triggering commit. Non-conventional messages are
reported and ignored. This implements the rules above, rather than all of
semantic-release's plugin, prerelease, revert or publishing behavior.

## Usage

Commit the source changes first using your identity, a conventional message and
`git commit -S`. The script reads committed history, not staged or working-tree
changes. Run from any directory:

```sh
python3 scripts/semantic-release.py --dry-run
python3 scripts/semantic-release.py
# Or use the project's Python toolchain:
make version
make release
```

`.version` contains the current plain `MAJOR.MINOR.PATCH` version. A hidden
`release-commit` marker in `CHANGELOG.md` identifies the last processed commit.
Only subsequent commits are analyzed. The initial 7.2.42 baseline is anchored to
the signed, squashed source commit. If there is no marker, the script requires
a local `v<current-version>` tag. `--since <revision>` explicitly overrides this
boundary. Complete Git history is required; use `git fetch --unshallow` when
starting from a shallow clone. Fetch tags too when using a tag boundary.

Examples:

```sh
python3 scripts/semantic-release.py --since v7.2.42 --dry-run
python3 scripts/semantic-release.py --date 2026-10-07
```

The date defaults to UTC; `--date YYYY-MM-DD` makes release notes reproducible.
`--root PATH` selects a different checkout, primarily for isolated tests.

A release updates these files together:

- `.version`: canonical project version.
- `CHANGELOG.md`: dated notes with commit identifiers and the next history boundary.
- `tdesktop/Telegram/build/version`: numeric/string build metadata, preserving beta status.
- `tdesktop/Telegram/SourceFiles/core/version.h`: version compiled into the main app.
- `tdesktop/changelog.txt`: app release notes in the donor/AppStream format, preserving earlier entries.

Validation happens before writing. Inconsistent app metadata, invalid versions,
unavailable history, closed-alpha versions and app version component overflow
are rejected. The donor encodes components as three decimal digits, so each
component must be at most 999. Repeating the command without new release-worthy
commits leaves files unchanged. Release-file edits already in the working tree
must be reviewed: the script prepends to existing changelogs and requires version
files to agree.

After reviewing the generated files, commit them together using a signed message
such as `chore(release): prepare 7.3.0`. Optional release tags must also be signed,
for example `git tag -s v7.3.0 -m 'Release 7.3.0'`. The script does not create
commits or tags, push, enable Actions or publish a release.

Local builds and CI no longer increment the version on every compilation. They
build the prepared version, retaining incremental records and compiler caches.
`make release` packages that version; it does not analyze commits automatically.
The old `nix/bump-version.py` helper remains for its historical regression tests
but is no longer called by builds.

# Single-file Linux release and packages

```sh
make init
make release      # dist/deltagram, executable; version and SHA-256 sidecars
make release-all  # same build plus dist/packages/{*.deb,*.rpm,*.pkg.tar.zst,*.tar.gz}
```

Docker must be installed and running. The compiler and application dependencies
come from the locked Nix toolchain. GUI compilation uses four jobs; Core builds
first. One release lock covers compilation, runtime staging and packaging.
Existing Ninja records and compiler caches are preserved.
The release target checks version, Core RPC, GUI startup and keyboard input in
an isolated Debian container before generating optional distribution packages.

`nix/stage-release.py` collects the current stripped GUI, Core server, matching
loaders/libraries, Qt plugins and PipeWire runtime into ignored
`data/release-stage/`. The container adds WebKitGTK, fonts, certificates and
keyboard data. It does not rebuild the GUI. The Nix-built GUI keeps its matching
libc and loader; Debian's older libc must not replace them.

The final lowercase `deltagram` is a static musl launcher with an embedded gzip
SquashFS image and pinned AppImageKit extractor. It starts the bundled GUI/Core
without a Nix installation, Qt installation or neighboring runtime files. FUSE
is not required. The runtime is extracted once per payload into
`$XDG_CACHE_HOME/deltagram/runtime` (default `~/.cache/deltagram/runtime`).
Subsequent launches run directly from that cache without copying or unpacking
the embedded image. First extraction needs space for both the compressed image
and extracted runtime; afterward only the extracted runtime remains. Concurrent
launches share one extraction, incomplete extractions are retried, and inactive
older versions are removed. Versions still running retain their cache until a
later launch can safely remove it. Account data is stored separately.
The gzip payload trades a larger download for faster first extraction.
Diagnostic options:

```sh
./dist/deltagram --bundle-version
./dist/deltagram --bundle-rpc --version
bash docker/verify.sh              # isolated Debian WebKit, GUI, keyboard and RPC checks
DELTA_TEST_IMAGE=ubuntu:22.04 bash docker/verify.sh # same checks on another image
```

## Supported target

This build targets **Linux x86-64** with X11 or XWayland, a compatible kernel,
glibc 2.17 or newer, zlib for the extractor and a POSIX shell. Display services and graphics
drivers remain host responsibilities; software rendering is the default.
The runtime cache must allow execution; set `XDG_CACHE_HOME` to an executable
filesystem if the default cache is mounted `noexec`. Windows, macOS, ARM, and musl-only distributions are not supported by
this artifact. Each needs a separate native build. Identical behavior on every
system is not guaranteed; record tested distributions in
[VALIDATION.md](../docs/VALIDATION.md).

The GUI work directory defaults to `~/.local/share/deltagram/desktop`;
`DELTA_TEL_WORKDIR` overrides it. Core accounts keep the existing default
`~/.local/share/delta-tel/accounts` (or the configured account path).
`--delta-accounts PATH` selects a different account directory.
Close other processes using the same profiles;
never remove `accounts.lock` to bypass a running process.

## Distribution packages

`release-all` builds these from the same untouched single-file executable:

| Output | Installation |
| --- | --- |
| `deltagram_VERSION-1_amd64.deb` | `sudo apt install ./FILE.deb` |
| `deltagram-VERSION-1.x86_64.rpm` | `sudo dnf install ./FILE.rpm` |
| `deltagram-VERSION-1-x86_64.pkg.tar.zst` | `sudo pacman -U ./FILE.pkg.tar.zst` |
| `deltagram-VERSION-linux-x86_64.tar.gz` | Extract, then run `./deltagram/deltagram`. |

Native packages install `/usr/bin/deltagram`, a desktop menu entry and component
license notices. They declare the extractor's libc/zlib dependencies. They do
not start the app, modify accounts or enable services. Packaging inspects package
metadata and compares each extracted executable with the original; it writes
`dist/packages/SHA256SUMS`. RPM stripping and automatic dependency scanning are
disabled because the executable contains an appended filesystem.

To repackage an existing release without compilation:

```sh
bash docker/build.sh               # single file from data/release-stage
bash docker/package-formats.sh     # native packages from dist/deltagram
```

Container staging and downloads live under ignored `data/container-build/`;
`DELTA_CONTAINER_BUILD_DIR` overrides this. Docker images use Docker's own data
directory. `make clean` removes distribution output while preserving compiler
and Nix caches. All artifacts remain ignored and no publishing occurs.

Format references: [AppImage portability concepts](https://docs.appimage.org/introduction/concepts.html),
[RPM spec files](https://rpm.org/docs/4.20.x/manual/spec.html), and
[Arch package metadata](https://man.archlinux.org/man/PKGINFO.5.en).

## WebKit runtime

The portable executable includes WebKitGTK's network, web and GPU processes,
injected bundle and bubblewrap. Production WebKitGTK uses absolute helper paths;
the runtime shim redirects those paths into the extracted bundle and makes that
bundle available read-only inside WebKit's sandbox. WebKit's namespace, seccomp
and IPC settings remain enabled.

Verification loads an offline HTML page and requires its JavaScript to update a
window title, in addition to the Qt keyboard and Core RPC checks. This exercises
real WebKit subprocess startup without contacting a server. `Telegram.stripped`
is the GUI executable alone; use `deltagram` for the complete portable runtime.

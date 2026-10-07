#!/bin/bash
set -euo pipefail
test "$(. /etc/os-release; echo "$VERSION_ID")" = 13
mkdir -p /build/archives/partial /build/lists/partial /build/debian /output
apt_options=(-o Dir::State::lists=/build/lists -o Dir::Cache::archives=/build/archives
  -o Dir::Cache::pkgcache=/build/pkgcache -o Dir::Cache::srcpkgcache=/build/srcpkgcache)
apt-get "${apt_options[@]}" update
apt-get "${apt_options[@]}" -y --download-only --reinstall --no-install-recommends install \
  libc6 musl-dev binutils patchelf squashfs-tools python3 \
  libwebkitgtk-6.0-4 libgtk-4-1 libegl1 libglx0 libgl1 libopengl0 libgbm1 libxkbcommon-x11-0 \
  glib-networking libglib2.0-bin ca-certificates fonts-dejavu-core xvfb xauth \
  libbz2-1.0 libselinux1 libpcre2-8-0 libaudit1 libcap2 libcap-ng0 \
  libsystemd0 liblzma5 libzstd1 zlib1g libmd0 libbsd0 libnettle8t64 libgmp10
for package in /build/archives/*.deb; do dpkg-deb -x "$package" /build/debian; done
export LD_LIBRARY_PATH=/build/debian/usr/lib/x86_64-linux-gnu
export PYTHONHOME=/build/debian/usr
/build/debian/usr/bin/python3 /builder/package.py
unset LD_LIBRARY_PATH PYTHONHOME
musl=/build/debian/usr/lib/x86_64-linux-musl
"$CC" -Os -fno-pie -ffunction-sections -fdata-sections -nostdinc \
  -isystem /build/debian/usr/include/x86_64-linux-musl \
  -c /builder/launcher.c -o /build/launcher.o
"$CC" -static -no-pie -nostdlib -Wl,--gc-sections -o /build/launcher \
  "$musl/crt1.o" "$musl/crti.o" /build/launcher.o -L"$musl" -lc \
  "$("$CC" -print-libgcc-file-name)" "$musl/crtn.o"
export LD_LIBRARY_PATH=/build/debian/usr/lib/x86_64-linux-gnu
export PYTHONHOME=/build/debian/usr
/build/debian/usr/bin/strip /build/launcher
export PATH=/usr/bin:/bin
if /build/debian/usr/bin/readelf -l /build/launcher | grep -q INTERP; then
  echo 'Launcher unexpectedly requires a dynamic interpreter' >&2; exit 1
fi
echo '328e0d745c5c6817048c27bc3e8314871703f8f47ffa81a37cb06cd95a94b323  /runtime/runtime-x86_64' | sha256sum -c -
/build/debian/usr/bin/mksquashfs /build/AppDir /build/payload.squashfs \
  -noappend -comp xz -Xbcj x86 -b 1048576 -processors 4 -all-root -no-xattrs
/build/debian/usr/bin/python3 - <<'PY'
import hashlib, pathlib, shutil, struct
version = pathlib.Path('/input/Telegram.version').read_text().strip()
target = pathlib.Path('/output') / '.deltagram.tmp'
with target.open('wb') as out:
    for name in ('/build/launcher', '/runtime/runtime-x86_64', '/build/payload.squashfs'):
        with open(name, 'rb') as source: shutil.copyfileobj(source, out)
    length = pathlib.Path('/runtime/runtime-x86_64').stat().st_size + pathlib.Path('/build/payload.squashfs').stat().st_size
    out.write(struct.pack('<Q', length))
target.chmod(0o755)
size = target.stat().st_size
digest = hashlib.file_digest(target.open('rb'), 'sha256').hexdigest()
target = target.replace(target.with_name('deltagram'))
target.with_suffix('.sha256').write_text(f'{digest}  {target.name}\n')
target.with_suffix('.version').write_text(version + '\n')
print(f'BUILT {target.name}: {size} bytes ({size / 1_000_000:.2f} MB)')
PY

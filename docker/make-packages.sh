#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
VERSION=$(cat /input/deltagram.version)
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid release version.' >&2; exit 1; }
test "$(/input/deltagram --bundle-version)" = "$VERSION"
EPOCH=${SOURCE_DATE_EPOCH:-$(date +%s)}
PAYLOAD=/tmp/packages/payload
mkdir -p "$PAYLOAD/usr/bin" "$PAYLOAD/usr/share/applications" \
  "$PAYLOAD/usr/share/doc/deltagram" /output
install -m 755 /input/deltagram "$PAYLOAD/usr/bin/deltagram"
install -m 644 /licenses/* "$PAYLOAD/usr/share/doc/deltagram/"
cat > "$PAYLOAD/usr/share/applications/deltagram.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Deltagram
Comment=Experimental Delta Chat client
Exec=deltagram %U
Icon=internet-chat
Terminal=false
Categories=Network;InstantMessaging;
StartupNotify=true
DESKTOP
SIZE=$(du -sk "$PAYLOAD" | cut -f1)
find "$PAYLOAD" -exec touch -h -d "@$EPOCH" {} +

# Debian: the AppImage extractor needs host libc and zlib. GUI/Core are bundled.
DEB=/tmp/packages/deb
mkdir -p "$DEB/DEBIAN"
cp -a "$PAYLOAD/usr" "$DEB/"
cat > "$DEB/DEBIAN/control" <<CONTROL
Package: deltagram
Version: $VERSION-1
Architecture: amd64
Maintainer: Deltagram contributors <noreply@github.com>
Section: net
Priority: optional
Installed-Size: $SIZE
Depends: libc6 (>= 2.17), zlib1g, dash | bash
Description: Experimental Qt client for Delta Chat
 Bundles the Qt interface, Chatmail Core and their runtime dependencies.
 Requires X11 or XWayland and executable temporary storage.
CONTROL
dpkg-deb --root-owner-group -Zxz --build "$DEB" "/output/deltagram_${VERSION}-1_amd64.deb"

# RPM: do not strip or scan the appended payload as a normal standalone ELF.
mkdir -p /tmp/packages/rpm/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
cat > /tmp/packages/rpm/SPECS/deltagram.spec <<SPEC
%global _enable_debug_packages 0
%global debug_package %{nil}
%global __os_install_post %{nil}
%global _build_id_links none
Name: deltagram
Version: $VERSION
Release: 1
Summary: Experimental Qt client for Delta Chat
License: GPL-3.0-or-later AND MPL-2.0
BuildArch: x86_64
AutoReqProv: no
Requires: glibc >= 2.17
Requires: zlib
Requires: /bin/sh
%description
Bundles the Qt interface, Chatmail Core and their runtime dependencies.
Requires X11 or XWayland and executable temporary storage.
%install
mkdir -p %{buildroot}
cp -a $PAYLOAD/usr %{buildroot}/
%files
%attr(0755,root,root) /usr/bin/deltagram
/usr/share/applications/deltagram.desktop
%doc /usr/share/doc/deltagram
SPEC
rpmbuild --define '_topdir /tmp/packages/rpm' -bb /tmp/packages/rpm/SPECS/deltagram.spec
cp /tmp/packages/rpm/RPMS/x86_64/*.rpm /output/

# Arch binary package: metadata plus an mtree and the same untouched executable.
ARCH=/tmp/packages/arch
mkdir -p "$ARCH"
cp -a "$PAYLOAD/usr" "$ARCH/"
cat > "$ARCH/.PKGINFO" <<PKGINFO
pkgname = deltagram
pkgbase = deltagram
xdata = pkgtype=pkg
pkgver = $VERSION-1
pkgdesc = Experimental Qt client for Delta Chat
url = https://github.com/themadorg/deltagram
builddate = $EPOCH
packager = Deltagram contributors <noreply@github.com>
size = $(du -sb "$PAYLOAD/usr" | cut -f1)
arch = x86_64
license = GPL-3.0-or-later
license = MPL-2.0
depend = glibc>=2.17
depend = zlib
depend = bash
PKGINFO
find "$ARCH" -exec touch -h -d "@$EPOCH" {} +
(cd "$ARCH" && bsdtar --format=mtree --exclude=.MTREE \
  --options='!all,use-set,type,uid,gid,mode,time,size,sha256,link' -cf - . | gzip -n > .MTREE)
tar --sort=name --mtime="@$EPOCH" --owner=0 --group=0 --numeric-owner \
  -C "$ARCH" -cf - .PKGINFO .MTREE usr | zstd -q -T4 -o "/output/deltagram-$VERSION-1-x86_64.pkg.tar.zst" -f

# Portable archive for distributions without these package managers.
PORTABLE=/tmp/packages/portable/deltagram
mkdir -p "$PORTABLE"
cp /input/deltagram "$PORTABLE/"
cp /licenses/* "$PORTABLE/"
tar --sort=name --mtime="@$EPOCH" --owner=0 --group=0 --numeric-owner \
  -C /tmp/packages/portable -cf - deltagram | gzip -n > "/output/deltagram-$VERSION-linux-x86_64.tar.gz"

# Inspect package metadata and verify the executable survived every packager.
dpkg-deb --info "/output/deltagram_${VERSION}-1_amd64.deb"
rpm -qp --queryformat '%{NAME} %{VERSION} %{ARCH}\n' "/output/deltagram-$VERSION-1.x86_64.rpm"
test "$(bsdtar -xOf "/output/deltagram-$VERSION-1-x86_64.pkg.tar.zst" .PKGINFO | sed -n 's/^pkgname = //p')" = deltagram
mkdir -p /tmp/packages/check-deb /tmp/packages/check-rpm /tmp/packages/check-arch
dpkg-deb -x "/output/deltagram_${VERSION}-1_amd64.deb" /tmp/packages/check-deb
(cd /tmp/packages/check-rpm && rpm2cpio "/output/deltagram-$VERSION-1.x86_64.rpm" | bsdtar -xf -)
bsdtar -xf "/output/deltagram-$VERSION-1-x86_64.pkg.tar.zst" -C /tmp/packages/check-arch
for format in deb rpm arch; do
  cmp /input/deltagram "/tmp/packages/check-$format/usr/bin/deltagram"
done
(cd /output && sha256sum "deltagram_${VERSION}-1_amd64.deb" \
  "deltagram-$VERSION-1.x86_64.rpm" "deltagram-$VERSION-1-x86_64.pkg.tar.zst" \
  "deltagram-$VERSION-linux-x86_64.tar.gz" > SHA256SUMS)

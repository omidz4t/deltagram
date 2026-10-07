import os
import pathlib
import re
import shutil
import subprocess

source = pathlib.Path('/input')
debian = pathlib.Path('/build/debian')
app = pathlib.Path('/build/AppDir')
if app.exists(): shutil.rmtree(app)  # Only this disposable packaging stage.
lib = app / 'telegram-lib'
lib.mkdir(parents=True)

def copy(origin, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(origin, target, follow_symlinks=True)
    target.chmod(target.stat().st_mode | 0o200)

for name in ('Telegram', 'Telegram.version'):
    copy(source / name, app / name)
shutil.copytree(source / 'licenses', app / 'licenses', symlinks=False)
copy('/builder/AppRun', app / 'AppRun')
(app / 'AppRun').chmod(0o755)
for path in (source / 'telegram-lib').iterdir():
    if path.is_file() and ('.so' in path.name or path.name == 'deltachat-rpc-server'):
        if not (lib / path.name).exists():
            copy(path, lib / path.name)
for name in ('rpc-rt', 'spa-0.2', 'pipewire-0.3', 'pipewire-conf'):
    shutil.copytree(source / 'telegram-lib' / name, lib / name, symlinks=False)
shutil.copytree(source / 'telegram-plugins', app / 'telegram-plugins', symlinks=False)
contexts = pathlib.Path(os.environ['DELTA_QT_PLUGIN_ROOT']) / 'platforminputcontexts'
assert contexts.is_dir(), 'Missing Qt text-input plugins in the release toolchain'
shutil.copytree(contexts, app / 'telegram-plugins/platforminputcontexts', symlinks=False, dirs_exist_ok=True)

# Debian's GTK/WebKit, GL entry points and their ELF dependency closure.
# Graphics drivers are supplied by the desktop; Qt defaults to software rendering.
available = {}
for folder in ('usr/lib/x86_64-linux-gnu', 'lib/x86_64-linux-gnu'):
    for path in (debian / folder).glob('*'):
        if path.is_file(): available[path.name] = path
for path in pathlib.Path('/usr/lib/x86_64-linux-gnu').glob('*'):
    if path.is_file(): available.setdefault(path.name, path)
queue = [available[name] for name in (
    'libwebkitgtk-6.0.so.4', 'libgtk-4.so.1', 'libEGL.so.1', 'libGLX.so.0',
    'libGL.so.1', 'libgbm.so.1', 'libc.so.6', 'libresolv.so.2',
    'libxkbcommon.so.0', 'libxkbcommon-x11.so.0')]
webkit = debian / 'usr/lib/x86_64-linux-gnu/webkitgtk-6.0'
for path in webkit.rglob('*'):
    if path.is_file():
        target = app / path.relative_to(debian)
        copy(path, target)
        if path.read_bytes()[:4] == b'\x7fELF': queue.append(path)
gio = debian / 'usr/lib/x86_64-linux-gnu/gio/modules'
if gio.exists():
    for path in gio.iterdir():
        if path.is_file():
            copy(path, app / path.relative_to(debian))
            if path.suffix == '.so': queue.append(path)
# Also check every existing GUI library and plugin, including late-loaded plugins.
queue.extend(path for path in app.rglob('*')
             if path.is_file() and 'rpc-rt' not in path.parts
             and path.name != 'deltachat-rpc-server'
             and path.read_bytes()[:4] == b'\x7fELF')
seen = set()
while queue:
    path = queue.pop()
    if path in seen: continue
    seen.add(path)
    if path.parent in (debian / 'usr/lib/x86_64-linux-gnu', debian / 'lib/x86_64-linux-gnu', pathlib.Path('/usr/lib/x86_64-linux-gnu')):
        if not (lib / path.name).exists():
            copy(path, lib / path.name)
    dynamic = subprocess.check_output(['/build/debian/usr/bin/readelf', '-d', path], text=True)
    for name in re.findall(r'\(NEEDED\).*Shared library: \[([^]]+)\]', dynamic):
        if (lib / name).exists():
            queue.append(lib / name)
        elif name in available:
            queue.append(available[name])
        else:
            raise SystemExit(f'Missing dependency: {name} required by {path}')

# Keep the loader/libc paired with the current Nix-built GUI. Replacing it with
# Debian's older libc can break binaries requiring newer GLIBC symbol versions.
assert (lib / 'ld-linux-x86-64.so.2.real').is_file()
for folder in ('usr/share/fonts', 'usr/share/glib-2.0/schemas', 'usr/share/webkitgtk-6.0', 'usr/share/X11/xkb', 'usr/share/X11/locale'):
    if (debian / folder).exists():
        shutil.copytree(debian / folder, app / folder, symlinks=False, dirs_exist_ok=True)
certificates = sorted((debian / 'usr/share/ca-certificates').rglob('*.crt'))
assert certificates, 'Missing CA trust store'
(app / 'usr/share/ca-bundle.crt').write_bytes(b'\n'.join(path.read_bytes() for path in certificates))
for notice in (debian / 'usr/share/doc').glob('*/copyright'):
    copy(notice, app / 'licenses/debian' / notice.parent.name / 'copyright')
(app / 'fonts.conf').write_text('''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
 <dir prefix="relative">usr/share/fonts</dir>
 <dir>/usr/share/fonts</dir><dir>/usr/local/share/fonts</dir>
 <cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
''')

for path in app.rglob('*'):
    if not path.is_file() or path.read_bytes()[:4] != b'\x7fELF': continue
    path.chmod(path.stat().st_mode | 0o200)
    subprocess.run(['/build/debian/usr/bin/strip', '--strip-unneeded', path], check=True)
    if path.name.startswith('ld-linux') or 'rpc-rt' in path.parts: continue
    relative = os.path.relpath(lib, path.parent)
    subprocess.run(['/build/debian/usr/bin/patchelf', '--set-rpath', '$ORIGIN/' + relative, path], check=True)
# WebKit starts its helper executables directly. Their absolute ELF interpreter
# would otherwise select the host loader against our newer bundled libc.
helper_dir = app / 'usr/lib/x86_64-linux-gnu/webkitgtk-6.0'
for name in ('WebKitWebProcess', 'WebKitNetworkProcess', 'WebKitGPUProcess'):
    helper = helper_dir / name
    if not helper.is_file():
        continue
    helper.rename(helper.with_name(name + '.real'))
    helper.write_text('''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
exec "$ROOT/telegram-lib/ld-linux-x86-64.so.2.real" \\
  --library-path "$ROOT/telegram-lib" --argv0 "$0" "$0.real" "$@"
''')
    helper.chmod(0o755)
(app / 'deltagram.desktop').write_text('''[Desktop Entry]
Type=Application
Name=Deltagram
Exec=AppRun
Icon=deltagram
Categories=Network;InstantMessaging;
''')
print('Staged runtime bytes:', sum(p.stat().st_size for p in app.rglob('*') if p.is_file()))

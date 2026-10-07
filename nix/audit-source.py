#!/usr/bin/env python3
"""Check publishable Git files; report locations without printing secret values."""
import ipaddress
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
files = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).split(b'\0')
failures = []
ips = {}
for raw in files:
    if not raw:
        continue
    path = root / raw.decode()
    if path.is_symlink():
        if path.readlink().is_absolute() or not path.resolve().is_relative_to(root):
            failures.append((path.relative_to(root), 'external symlink'))
        continue
    if not path.is_file():
        continue
    data = path.read_bytes()
    relative = path.relative_to(root)
    if len(data) >= 100_000_000 or data.startswith(b'\x7fELF'):
        failures.append((relative, 'large file or ELF binary'))
    patterns = [
        (rb'(?i)\bpiker\b', 'private marker'),
        (rb'/home/(?!user/|me/|remote/)[A-Za-z0-9_.-]+/', 'local home path'),
        (rb'(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|xox[baprs]-[A-Za-z0-9-]{20,})', 'possible token'),
        (rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\s+[A-Za-z0-9+/=]{40,}', 'private key payload'),
    ]
    for pattern, reason in patterns:
        if re.search(pattern, data):
            failures.append((relative, reason))
    for match in re.finditer(rb'(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])', data):
        try:
            address = ipaddress.ip_address(match.group().decode())
        except ValueError:
            continue
        ips.setdefault(str(relative), set()).add(str(address))
for path, reason in failures:
    print(f'FAIL {path}: {reason}')
print(f'Checked {len(files)-1} files; {len(failures)} blocking findings; IPv4 literals in {len(ips)} files.')
if '--ips' in sys.argv:
    for path, addresses in sorted(ips.items()):
        print(f'{path}: {", ".join(sorted(addresses))}')
sys.exit(bool(failures))

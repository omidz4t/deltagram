#!/usr/bin/env python3
"""Increment the application patch version before an incremental build."""
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
source = path.read_text()
fields = dict(line.split() for line in source.splitlines() if line.strip())
major, minor, patch = map(int, fields['AppVersionStr'].split('.'))
if patch >= 999:
    raise SystemExit('Patch version exhausted; choose the next minor version.')
patch += 1
version = f'{major}.{minor}.{patch}'
suffix = '.beta' if fields['BetaChannel'] == '1' else ''
values = {
    'AppVersion': str(major * 1000000 + minor * 1000 + patch),
    'AppVersionStrMajor': f'{major}.{minor}',
    'AppVersionStrSmall': version,
    'AppVersionStr': version,
    'AppVersionOriginal': version + suffix,
}
if fields['AlphaVersion'] != '0':
    raise SystemExit('Alpha builds need an explicit version policy.')
for key, value in values.items():
    source = re.sub(rf'^{key}(\s+)\S+', lambda m: key + m[1] + value,
                    source, flags=re.MULTILINE)
path.write_text(source)
if len(sys.argv) > 2:
    header = pathlib.Path(sys.argv[2])
    content = header.read_text()
    content = re.sub(r'constexpr auto AppVersion = \d+;',
                     f"constexpr auto AppVersion = {values['AppVersion']};", content)
    content = re.sub(r'constexpr auto AppVersionStr = "[^"]+";',
                     f'constexpr auto AppVersionStr = "{version}";', content)
    header.write_text(content)
print(version)

#!/usr/bin/env python3
"""Prepare a local semantic release using Git and Python's standard library."""
import argparse
import datetime
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

VERSION = re.compile(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)\Z')
SUBJECT = re.compile(r'(?P<type>[a-z]+)(?:\((?P<scope>[^()\n]+)\))?(?P<breaking>!)?: (?P<summary>\S.*)\Z')
MARKER = re.compile(r'<!-- release-commit: ([0-9a-f]{40,64}) -->')
LEVELS = {'patch': 1, 'minor': 2, 'major': 3}


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True,
                                   encoding='utf-8', errors='replace').strip()


def analyze(message):
    lines = message.splitlines()
    match = SUBJECT.fullmatch(lines[0]) if lines else None
    if not match:
        return None
    change = match.groupdict()
    breaking = bool(change['breaking']) or bool(re.search(
        r'^BREAKING(?: CHANGE|-CHANGE):\s*\S', '\n'.join(lines[1:]), re.MULTILINE))
    change['level'] = ('major' if breaking else 'minor' if change['type'] == 'feat'
                       else 'patch' if change['type'] in ('fix', 'perf') else None)
    change['breaking'] = breaking
    return change


def parse_version(text):
    match = VERSION.fullmatch(text.strip())
    if not match:
        raise ValueError('.version must contain one MAJOR.MINOR.PATCH version')
    parts = tuple(map(int, match.groups()))
    # The donor encodes each component in three decimal digits.
    if any(part > 999 for part in parts):
        raise ValueError('App version components must be between 0 and 999')
    return parts


def replace_once(pattern, replacement, text):
    result, count = re.subn(pattern, replacement, text, flags=re.MULTILINE)
    if count != 1:
        raise ValueError(f'Expected exactly one app version field: {pattern}')
    return result


def app_versions(metadata, header, version):
    major, minor, patch = parse_version(version)
    fields = dict(line.split() for line in metadata.splitlines() if line.strip())
    if fields.get('AlphaVersion') != '0':
        raise ValueError('Closed alpha versions need a separate release policy')
    if fields.get('BetaChannel') not in ('0', '1'):
        raise ValueError('Invalid BetaChannel in app metadata')
    number = major * 1000000 + minor * 1000 + patch
    values = {'AppVersion': str(number), 'AppVersionStrMajor': f'{major}.{minor}',
              'AppVersionStrSmall': version, 'AppVersionStr': version,
              'AppVersionOriginal': version + ('.beta' if fields['BetaChannel'] == '1' else '')}
    for name, value in values.items():
        metadata = replace_once(rf'^{name}(\s+)\S+$',
                                lambda m, n=name, v=value: n + m[1] + v, metadata)
    header = replace_once(r'^constexpr auto AppVersion = \d+;$',
                          f'constexpr auto AppVersion = {number};', header)
    header = replace_once(r'^constexpr auto AppVersionStr = "[^"]+";$',
                          f'constexpr auto AppVersionStr = "{version}";', header)
    return metadata, header, fields['BetaChannel'] == '1'


def write_files(updates):
    """Stage every file first; restore original contents if replacement fails."""
    originals = {path: path.read_bytes() for path in updates}
    staged = {}
    replaced = []
    try:
        for path, content in updates.items():
            descriptor, name = tempfile.mkstemp(prefix='.' + path.name + '.', dir=path.parent)
            staged[path] = Path(name)
            with os.fdopen(descriptor, 'w', encoding='utf-8', newline='\n') as stream:
                stream.write(content)
            staged[path].chmod(path.stat().st_mode & 0o777)
        for path, temporary in staged.items():
            os.replace(temporary, path)
            replaced.append(path)
    except BaseException:
        for path in replaced:
            path.write_bytes(originals[path])
        raise
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)


def release(root, since=None, dry_run=False, date=None):
    root = root.resolve()
    if git(root, 'rev-parse', '--is-shallow-repository') == 'true':
        raise ValueError('Full Git history is required; fetch with --unshallow first')
    paths = [root / '.version', root / 'CHANGELOG.md',
             root / 'tdesktop/Telegram/build/version',
             root / 'tdesktop/Telegram/SourceFiles/core/version.h',
             root / 'tdesktop/changelog.txt']
    for path in paths:
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Missing or unsafe release file: {path.relative_to(root)}')
    current, changelog, metadata, header, app_log = [p.read_text(encoding='utf-8') for p in paths]
    parts = parse_version(current)
    current = '.'.join(map(str, parts))
    expected_metadata, expected_header, beta = app_versions(metadata, header, current)
    if (metadata, header) != (expected_metadata, expected_header):
        raise ValueError('.version and app metadata disagree; synchronize them before releasing')
    marker = MARKER.search(changelog)
    base = since or (marker[1] if marker else f'v{current}')
    base = git(root, 'rev-parse', '--verify', '--end-of-options', base + '^{commit}')
    head = git(root, 'rev-parse', 'HEAD')
    subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor', base, head], check=True)
    raw = git(root, 'log', '--reverse', '--format=%H%x00%B%x00', f'{base}..{head}')
    chunks = raw.split('\0')
    changes = []
    for index in range(0, len(chunks) - 1, 2):
        sha, message = chunks[index].strip(), chunks[index + 1]
        change = analyze(message)
        if change:
            changes.append((sha, change))
        elif sha:
            print(f'Ignoring non-conventional commit {sha[:7]}', file=sys.stderr)
    level = max((change['level'] for _, change in changes if change['level']),
                key=LEVELS.get, default=None)
    if level is None:
        print(f'No release needed; version remains {current}.')
        return None
    major, minor, patch = parts
    next_parts = ((major + 1, 0, 0) if level == 'major' else
                  (major, minor + 1, 0) if level == 'minor' else (major, minor, patch + 1))
    version = '.'.join(map(str, next_parts))
    parse_version(version)
    metadata, header, _ = app_versions(metadata, header, version)
    today = date or datetime.datetime.now(datetime.timezone.utc).date()
    lines = []
    app_lines = []
    for sha, change in changes:
        scope = f"{change['scope']}: " if change['scope'] else ''
        summary = scope + change['summary']
        category = 'BREAKING CHANGE' if change['breaking'] else change['type']
        lines.append(f'- **{category}**: {summary} (`{sha[:7]}`)')
        app_lines.append(f'- {category}: {summary}')
    if not changelog.startswith('# Changelog\n'):
        raise ValueError('CHANGELOG.md must start with # Changelog')
    body = MARKER.sub('', changelog[len('# Changelog\n'):]).lstrip()
    changelog = (f'# Changelog\n\n<!-- release-commit: {head} -->\n\n'
                 f'## {version} ({today.isoformat()})\n\n' + '\n'.join(lines) + '\n\n' + body)
    app_log = (f'{version}{" beta" if beta else ""} ({today.strftime("%d.%m.%y")})\n\n'
               + '\n'.join(app_lines) + '\n\n' + app_log)
    updates = dict(zip(paths, [version + '\n', changelog, metadata, header, app_log]))
    print(f'{current} -> {version} ({level}; {len(changes)} conventional commits)')
    if dry_run:
        print('\n'.join(lines))
        print('Dry run: no files changed.')
    else:
        write_files(updates)
        for path in paths:
            print(f'Updated {path.relative_to(root)}')
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--since', help='Override the changelog commit marker with a Git revision')
    parser.add_argument('--dry-run', action='store_true', help='Preview without modifying files')
    parser.add_argument('--date', type=datetime.date.fromisoformat, help='Release date, YYYY-MM-DD (default UTC)')
    args = parser.parse_args()
    try:
        release(args.root, args.since, args.dry_run, args.date)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Release failed: {error}\n')


if __name__ == '__main__':
    main()

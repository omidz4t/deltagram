#!/usr/bin/env python3
"""Prepare release artifacts from signed source commits without creating CI commits."""
import argparse
import gzip
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('semantic_release', ROOT / 'scripts/semantic-release.py')
SEMANTIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SEMANTIC)


def release_tags(root):
    tags = SEMANTIC.git(root, 'tag', '--merged', 'HEAD').splitlines()
    versions = []
    for tag in tags:
        if tag.startswith('v') and SEMANTIC.VERSION.fullmatch(tag[1:]):
            versions.append((SEMANTIC.parse_version(tag[1:]), tag))
    return sorted(versions)


def prepare(root=ROOT):
    tags = release_tags(root)
    if tags:
        latest, tag = tags[-1]
        version = '.'.join(map(str, latest))
        metadata_path = root / 'tdesktop/Telegram/build/version'
        header_path = root / 'tdesktop/Telegram/SourceFiles/core/version.h'
        metadata, header, _ = SEMANTIC.app_versions(
            metadata_path.read_text(), header_path.read_text(), version)
        SEMANTIC.write_files({root / '.version': version + '\n',
                              metadata_path: metadata, header_path: header})
        version = SEMANTIC.release(root, since=tag)
        if version is None:
            return None
    else:
        version = (root / '.version').read_text().strip()
        SEMANTIC.parse_version(version)
        metadata_path = root / 'tdesktop/Telegram/build/version'
        header_path = root / 'tdesktop/Telegram/SourceFiles/core/version.h'
        metadata, header, _ = SEMANTIC.app_versions(
            metadata_path.read_text(), header_path.read_text(), version)
        if metadata != metadata_path.read_text() or header != header_path.read_text():
            raise ValueError('Initial version and app metadata disagree')
    out = root / 'data/release-plan'
    out.mkdir(parents=True, exist_ok=True)
    changelog = (root / 'CHANGELOG.md').read_text()
    heading = re.search(r'^## ' + re.escape(version) + r' \([^\n]+\)\n', changelog, re.M)
    if heading:
        notes = changelog[heading.end():].split('\n## ', 1)[0].strip()
    else:
        notes = 'Initial experimental release.'
    (out / 'notes.md').write_text('Experimental Linux x86_64 release.\n\n' + notes + '\n')
    (out / 'version').write_text(version + '\n')
    return version


def source_archive(root=ROOT):
    version = (root / 'data/release-plan/version').read_text().strip()
    SEMANTIC.parse_version(version)
    paths = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).split(b'\0')
    timestamp = int(SEMANTIC.git(root, 'show', '-s', '--format=%ct', 'HEAD'))
    destination = root / 'dist' / f'deltagram-{version}-source.tar.gz'
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=timestamp) as zipped:
        with tarfile.open(fileobj=zipped, mode='w|') as archive:
            for raw_path in paths:
                if not raw_path:
                    continue
                path = raw_path.decode()
                full_path = root / path
                if not full_path.is_file() and not full_path.is_symlink():
                    raise ValueError(f'Unsupported source entry: {path}')
                info = archive.gettarinfo(str(full_path), arcname=f'deltagram-{version}/{path}')
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                info.mtime = timestamp
                if info.isfile():
                    with full_path.open('rb') as content:
                        archive.addfile(info, content)
                else:
                    archive.addfile(info)
    print(destination.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-archive', action='store_true')
    args = parser.parse_args()
    if args.source_archive:
        source_archive()
        return
    version = prepare()
    values = {'publish': str(version is not None).lower(), 'version': version or ''}
    output = os.environ.get('GITHUB_OUTPUT')
    if output:
        with open(output, 'a') as stream:
            for key, value in values.items():
                stream.write(f'{key}={value}\n')
    print('Release planned: ' + version if version else 'No release-worthy commits.')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Export working source, including edited submodules, without Git history/data."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = ('.gitignore', 'AGENTS.md', 'flake.nix', 'flake.lock', 'README.md', 'Makefile')
SOURCE_DIRS = ('.github', 'docs', 'nix', 'docker')
REPOS = ('tdesktop',)
PRIVATE_DIRS = {'.git', 'context', 'data', 'accounts', 'tdata', 'node_modules', 'target', '__pycache__', '.delta-shell-runtime'}
PRIVATE_NAMES = {'accounts.lock', 'CMakeCache.txt', 'build.ninja', '.ninja_deps', '.ninja_log', 'compile_commands.json'}
PRIVATE_SUFFIXES = ('.log', '.pyc', '.pem', '.key', '.p12', '.pfx', '.db', '.sqlite', '.sqlite3', '.db-wal', '.db-shm', '.dmp', '.so', '.a', '.o', '.run', '.AppImage', '.dll', '.exe', '.dylib', '.lib', '.wasm', '.class', '.jar')

def permitted(relative):
    if relative == Path("tdesktop/Telegram/Resources/update/root-public.pem"):
        return True
    return (not any(part in PRIVATE_DIRS or part.startswith('dc.db') for part in relative.parts)
            and relative.name not in PRIVATE_NAMES
            and not relative.name.startswith('dc.db')
            and (not relative.name.startswith('.env') or relative.name == '.env.example')
            and not re.search(r'\.so\.[0-9]', relative.name)
            and not relative.name.endswith(PRIVATE_SUFFIXES))

def git_files(repo):
    if not (repo / '.git').exists():
        return [p.relative_to(repo) for p in repo.rglob('*') if p.is_file() or p.is_symlink()]
    output = subprocess.check_output(['git', '-C', str(repo), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    return [Path(os.fsdecode(p)) for p in output.split(b'\0') if p]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination == ROOT or ROOT in destination.parents:
        parser.error('Choose a destination outside the working project.')
    if destination.exists():
        parser.error('Destination already exists; choose a new empty location.')
    files = set()
    def collect_repo(repo):
        for relative in git_files(repo):
            source = repo / relative
            if source.is_dir():
                if (source / '.git').exists(): collect_repo(source)
            elif source.exists() or source.is_symlink():
                files.add(source.relative_to(ROOT))
    for name in REPOS: collect_repo(ROOT / name)
    for name in ROOT_FILES:
        if (ROOT / name).is_file(): files.add(Path(name))
    for name in SOURCE_DIRS:
        for folder, dirs, names in os.walk(ROOT / name):
            dirs[:] = [d for d in dirs if d not in PRIVATE_DIRS]
            for filename in names: files.add((Path(folder) / filename).relative_to(ROOT))
    selected = sorted(p for p in files if permitted(p))
    username = os.environ.get('USER', '')
    home = str(Path.home()).encode()
    personal = re.compile(re.escape(username.encode()), re.I) if len(username) >= 4 else None
    errors = []
    for relative in selected:
        source = ROOT / relative
        if source.is_symlink():
            target = os.readlink(source)
            resolved = source.resolve()
            if os.path.isabs(target) or (resolved != ROOT and ROOT not in resolved.parents):
                errors.append(f'{relative}: external or absolute symlink')
            continue
        data = source.read_bytes()
        if home in data or (personal and personal.search(data)):
            errors.append(f'{relative}: local home path or username')
        if re.search(rb'(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|xox[baprs]-[A-Za-z0-9-]{20,})', data):
            errors.append(f'{relative}: possible access token')
        if source.stat().st_size >= 100_000_000:
            errors.append(f'{relative}: exceeds GitHub single-file size limit')
        if data.startswith(b'\x7fELF'):
            errors.append(f'{relative}: generated executable')
    if errors:
        raise SystemExit('Export refused:\n' + '\n'.join(errors))
    destination.mkdir(parents=True)
    for relative in selected:
        source, target = ROOT / relative, destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_symlink(): target.symlink_to(os.readlink(source))
        else: shutil.copy2(source, target)
    print(f'Exported {len(selected)} source files to {destination.name}; no Git history included.')

if __name__ == '__main__': main()

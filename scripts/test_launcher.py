"""Portable launcher cache checks, using disposable synthetic runtimes."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('cc'), 'A C compiler is required')
class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.env = {**os.environ, 'HOME': str(self.root),
                    'XDG_CACHE_HOME': str(self.root / 'cache'),
                    'EXTRACTION_LOG': str(self.root / 'extractions')}

    def bundle(self, name='one', broken=False):
        payload = (f'#!/bin/sh\n# {name}\n'
                   'mkdir squashfs-root\n'
                   'echo extraction >> "$EXTRACTION_LOG"\n')
        if not broken:
            payload += ('cat > squashfs-root/AppRun <<\'APP\'\n#!/bin/sh\n'
                        'sleep "${APP_DELAY:-0}"\necho bundle-ready\nAPP\n'
                        'chmod +x squashfs-root/AppRun\n')
        payload = payload.encode()
        digest = hashlib.sha256(payload).hexdigest()
        binary = self.root / name
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        f'-DDELTA_TEL_RUNTIME_ID="{digest}"',
                        str(ROOT / 'docker/launcher.c'), '-o', str(binary)], check=True)
        with binary.open('ab') as out:
            out.write(payload + struct.pack('<Q', len(payload)))
        return binary, self.root / 'cache/deltagram/runtime' / ('v2-' + digest)

    def run_bundle(self, binary, **env):
        return subprocess.run([binary], env={**self.env, **env},
                              capture_output=True, text=True, timeout=10)

    def test_reuse_and_recover_incomplete_cache(self):
        binary, cache = self.bundle()
        for _ in range(2):
            result = self.run_bundle(binary)
            self.assertEqual((result.returncode, result.stdout, result.stderr),
                             (0, 'bundle-ready\n', ''))
        self.assertEqual((self.root / 'extractions').read_text().count('extraction'), 1)
        (cache / 'AppRun').unlink()
        self.assertEqual(self.run_bundle(binary).returncode, 0)
        self.assertEqual((self.root / 'extractions').read_text().count('extraction'), 2)

    def test_concurrent_first_launches_extract_once(self):
        binary, _ = self.bundle()
        processes = [subprocess.Popen([binary], env={**self.env, 'APP_DELAY': '0.2'},
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(6)]
        for process in processes:
            self.assertEqual(process.communicate(timeout=5), ('bundle-ready\n', ''))
            self.assertEqual(process.returncode, 0)
        self.assertEqual((self.root / 'extractions').read_text().count('extraction'), 1)

    def test_pruning_preserves_running_runtime_and_unrelated_files(self):
        old, old_cache = self.bundle()
        new, new_cache = self.bundle('two')
        marker = self.root / 'account-sentinel'; marker.write_text('preserve')
        active = subprocess.Popen([old], env={**self.env, 'APP_DELAY': '1'},
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: active.poll() is None and active.kill())
        for _ in range(100):
            if (old_cache / '.runtime-ready').exists(): break
            import time; time.sleep(.01)
        self.assertEqual(self.run_bundle(new).returncode, 0)
        self.assertTrue(old_cache.exists())
        active.communicate(timeout=5)
        self.assertEqual(self.run_bundle(new).returncode, 0)
        self.assertFalse(old_cache.exists())
        self.assertTrue(new_cache.exists()); self.assertEqual(marker.read_text(), 'preserve')

    def test_failed_extraction_is_not_cached(self):
        binary, cache = self.bundle(broken=True)
        self.assertNotEqual(self.run_bundle(binary).returncode, 0)
        self.assertFalse(cache.exists())

    def test_cache_symlink_does_not_escape(self):
        binary, _ = self.bundle()
        outside = self.root / 'outside'; outside.mkdir()
        parent = self.root / 'cache/deltagram'; parent.mkdir(parents=True)
        (parent / 'runtime').symlink_to(outside, target_is_directory=True)
        self.assertNotEqual(self.run_bundle(binary).returncode, 0)
        self.assertEqual(list(outside.iterdir()), [])

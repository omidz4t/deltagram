"""Offline checks for GUI build-provenance write/verify without Docker, CMake, or secrets."""
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HELPER_PATH = ROOT / 'nix/build-provenance.py'
STAGE_PATH = ROOT / 'nix/stage-release.py'
FAKE_API_ID = '24680'
FAKE_API_HASH = '0123456789abcdef0123456789abcdef'
CI_SHA = 'dddddddddddddddddddddddddddddddddddddddd'
GUI_BYTES = b'stripped-gui-fixture'
OTHER_GUI = b'stale-gui-fixture'


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PROVENANCE = load(HELPER_PATH, 'build_provenance')
STAGE = load(STAGE_PATH, 'stage_release')


def digest(payload):
    return hashlib.sha256(payload).hexdigest()


class ReleaseTree:
    def __init__(self, directory, mode='production', gui=GUI_BYTES, cache=None):
        self.root = Path(directory)
        self.release = self.root / 'tdesktop-release'
        self.assets = self.root / 'release-upload'
        self.dest = self.root / 'release-stage'
        bin_dir = self.release / 'bin'
        bin_dir.mkdir(parents=True)
        (bin_dir / 'deltagram.stripped').write_bytes(gui)
        (bin_dir / 'deltagram.version').write_text('7.2.10\n')
        if cache is None:
            if mode == 'production':
                cache = (
                    'TDESKTOP_API_TEST:BOOL=OFF\n'
                    f'TDESKTOP_API_ID:STRING={FAKE_API_ID}\n'
                    f'TDESKTOP_API_HASH:STRING={FAKE_API_HASH}\n'
                )
            else:
                cache = (
                    'TDESKTOP_API_TEST:BOOL=ON\n'
                    'TDESKTOP_API_ID:STRING=0\n'
                    'TDESKTOP_API_HASH:STRING=\n'
                )
        (self.release / 'CMakeCache.txt').write_text(cache)

    def gui(self):
        return self.release / 'bin/deltagram.stripped'

    def sidecar(self):
        return self.release / 'bin' / PROVENANCE.SIDECAR_NAME

    def write(self, mode, extra_env=None):
        env = os.environ.copy()
        for name in ('GITHUB_SHA', 'TDESKTOP_API_ID', 'TDESKTOP_API_HASH', 'DELTA_TEL_API_MODE'):
            env.pop(name, None)
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            [sys.executable, str(HELPER_PATH), 'write', '--release-dir', str(self.release),
             '--mode', mode],
            check=False,
            env=env,
            capture_output=True,
            text=True,
        )

    def payload(self):
        return json.loads(self.sidecar().read_text())


class BuildProvenanceTest(unittest.TestCase):
    def test_write_test_mode_sidecar_is_explicit_test(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='test')
            result = tree.write('test')
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = tree.payload()
            self.assertEqual(payload['api_mode'], 'test')
            self.assertEqual(payload['tdesktop_api_test'], 'ON')
            self.assertEqual(payload['gui_sha256'], digest(GUI_BYTES))
            self.assertEqual(payload['gui_name'], 'deltagram.stripped')
            self.assertEqual(payload['version'], '7.2.10')
            self.assertNotIn('source_sha', payload)
            self.assertIn('Wrote GUI build provenance: test', result.stdout)

    def test_write_production_binds_sha256_and_omits_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            result = tree.write('production', extra_env={'GITHUB_SHA': CI_SHA})
            self.assertEqual(result.returncode, 0, result.stderr)
            text = tree.sidecar().read_text()
            combined = result.stdout + result.stderr + text
            self.assertNotIn(FAKE_API_ID, combined)
            self.assertNotIn(FAKE_API_HASH, combined)
            payload = json.loads(text)
            self.assertEqual(payload['api_mode'], 'production')
            self.assertEqual(payload['tdesktop_api_test'], 'OFF')
            self.assertEqual(payload['gui_sha256'], digest(GUI_BYTES))
            self.assertEqual(payload['source_sha'], CI_SHA)
            self.assertEqual(set(payload), {
                'schema', 'api_mode', 'tdesktop_api_test', 'gui_sha256',
                'gui_name', 'version', 'source_sha',
            })

    def test_write_omits_invalid_github_sha(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            result = tree.write('production', extra_env={'GITHUB_SHA': 'HEAD'})
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('source_sha', tree.payload())

    def test_write_production_refuses_cache_on(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='test')
            result = tree.write('production')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('does not match the GUI build mode', result.stderr)
            self.assertFalse(tree.sidecar().exists())
            self.assertNotIn(FAKE_API_HASH, result.stdout + result.stderr)

    def test_write_test_refuses_cache_off(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            result = tree.write('test')
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(tree.sidecar().exists())

    def test_write_refuses_missing_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            tree.gui().unlink()
            result = tree.write('production')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing stripped GUI binary', result.stderr)
            self.assertFalse(tree.sidecar().exists())

    def test_verify_stage_production_success(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            PROVENANCE.verify_stage_release(tree.release)

    def test_verify_stage_refuses_cache_off_with_test_sidecar(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='test')
            self.assertEqual(tree.write('test').returncode, 0)
            (tree.release / 'CMakeCache.txt').write_text(
                'TDESKTOP_API_TEST:BOOL=OFF\n'
                f'TDESKTOP_API_ID:STRING={FAKE_API_ID}\n'
                f'TDESKTOP_API_HASH:STRING={FAKE_API_HASH}\n')
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_stage_release(tree.release)
            self.assertIn('release packaging requires production Telegram API provenance',
                          str(raised.exception))

    def test_verify_stage_production_sidecar_refuses_cache_on(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            (tree.release / 'CMakeCache.txt').write_text(
                'TDESKTOP_API_TEST:BOOL=ON\n'
                'TDESKTOP_API_ID:STRING=0\n'
                'TDESKTOP_API_HASH:STRING=\n')
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_stage_release(tree.release)
            self.assertIn('does not match GUI build provenance', str(raised.exception))

    def test_load_rejects_credential_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            payload = tree.payload()
            payload['tdesktop_api_hash'] = FAKE_API_HASH
            tree.sidecar().write_text(json.dumps(payload))
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_stage_release(tree.release)
            self.assertIn('unsupported fields', str(raised.exception))

    def test_verify_stage_refuses_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            tree.gui().write_bytes(OTHER_GUI)
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_stage_release(tree.release)
            self.assertIn('GUI SHA256 does not match build provenance', str(raised.exception))

    def test_verify_stage_refuses_missing_sidecar(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_stage_release(tree.release)
            self.assertIn('missing GUI build provenance sidecar', str(raised.exception))

    def test_verify_stage_allow_test_api_success(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='test')
            self.assertEqual(tree.write('test').returncode, 0)
            PROVENANCE.verify_stage_release(tree.release, allow_test_api=True)

    def test_verify_stage_allow_test_api_refuses_production(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_stage_release(tree.release, allow_test_api=True)
            self.assertIn('validation packaging requires test Telegram API provenance',
                          str(raised.exception))

    def test_verify_stage_does_not_touch_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            tree.dest.mkdir()
            canary = tree.dest / 'canary'
            canary.write_text('keep')
            PROVENANCE.verify_stage_release(tree.release)
            self.assertEqual(canary.read_text(), 'keep')

    def test_verify_publish_requires_production_and_source_sha(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production', extra_env={'GITHUB_SHA': CI_SHA}).returncode, 0)
            tree.assets.mkdir()
            (tree.assets / 'deltagram.stripped').write_bytes(GUI_BYTES)
            (tree.assets / 'deltagram.version').write_text('7.2.10\n')
            (tree.assets / PROVENANCE.SIDECAR_NAME).write_text(tree.sidecar().read_text())
            PROVENANCE.verify_publish_assets(
                tree.assets, source_sha=CI_SHA, require_source_sha=True)

    def test_verify_publish_refuses_test_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='test')
            self.assertEqual(tree.write('test', extra_env={'GITHUB_SHA': CI_SHA}).returncode, 0)
            tree.assets.mkdir()
            (tree.assets / 'deltagram.stripped').write_bytes(GUI_BYTES)
            (tree.assets / PROVENANCE.SIDECAR_NAME).write_text(tree.sidecar().read_text())
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_publish_assets(
                    tree.assets, source_sha=CI_SHA, require_source_sha=True)
            self.assertIn('release packaging requires production Telegram API provenance',
                          str(raised.exception))

    def test_verify_publish_refuses_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production', extra_env={'GITHUB_SHA': CI_SHA}).returncode, 0)
            tree.assets.mkdir()
            (tree.assets / 'deltagram.stripped').write_bytes(OTHER_GUI)
            (tree.assets / PROVENANCE.SIDECAR_NAME).write_text(tree.sidecar().read_text())
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_publish_assets(
                    tree.assets, source_sha=CI_SHA, require_source_sha=True)
            self.assertIn('GUI SHA256 does not match build provenance', str(raised.exception))

    def test_verify_publish_refuses_source_sha_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production', extra_env={'GITHUB_SHA': CI_SHA}).returncode, 0)
            tree.assets.mkdir()
            (tree.assets / 'deltagram.stripped').write_bytes(GUI_BYTES)
            (tree.assets / PROVENANCE.SIDECAR_NAME).write_text(tree.sidecar().read_text())
            other = 'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_publish_assets(
                    tree.assets, source_sha=other, require_source_sha=True)
            self.assertIn('source commit does not match the CI commit', str(raised.exception))

    def test_verify_publish_refuses_missing_source_sha_when_required(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            tree.assets.mkdir()
            (tree.assets / 'deltagram.stripped').write_bytes(GUI_BYTES)
            (tree.assets / PROVENANCE.SIDECAR_NAME).write_text(tree.sidecar().read_text())
            with self.assertRaises(SystemExit) as raised:
                PROVENANCE.verify_publish_assets(
                    tree.assets, source_sha=CI_SHA, require_source_sha=True)
            self.assertIn('missing the CI source commit', str(raised.exception))


    def test_write_rejects_empty_or_multiline_version(self):
        for version in ('', '\n', '7.2.10\n7.2.11\n'):
            with self.subTest(version=repr(version)), tempfile.TemporaryDirectory() as directory:
                tree = ReleaseTree(directory, mode='production')
                (tree.release / 'bin/deltagram.version').write_text(version)
                result = tree.write('production')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('GUI version file is invalid', result.stderr)
                self.assertFalse(tree.sidecar().exists())

    def test_verify_stage_refuses_changed_version(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            self.assertEqual(tree.write('production').returncode, 0)
            (tree.release / 'bin/deltagram.version').write_text('7.2.11\n')
            with self.assertRaisesRegex(SystemExit, 'GUI version does not match'):
                PROVENANCE.verify_stage_release(tree.release)

    def test_verify_publish_requires_matching_version(self):
        for version in (None, '7.2.11\n'):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as directory:
                tree = ReleaseTree(directory, mode='production')
                self.assertEqual(tree.write('production', extra_env={'GITHUB_SHA': CI_SHA}).returncode, 0)
                tree.assets.mkdir()
                (tree.assets / 'deltagram.stripped').write_bytes(GUI_BYTES)
                (tree.assets / PROVENANCE.SIDECAR_NAME).write_text(tree.sidecar().read_text())
                if version is not None:
                    (tree.assets / 'deltagram.version').write_text(version)
                with self.assertRaises(SystemExit):
                    PROVENANCE.verify_publish_assets(
                        tree.assets, source_sha=CI_SHA, require_source_sha=True)


class StageReleaseGuardTest(unittest.TestCase):
    def setUp(self):
        self.original = (STAGE.RELEASE, STAGE.DEST, STAGE.TARGET)

    def tearDown(self):
        STAGE.RELEASE, STAGE.DEST, STAGE.TARGET = self.original

    def test_missing_sidecar_leaves_destination_intact(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='production')
            tree.dest.mkdir()
            canary = tree.dest / 'canary'
            canary.write_text('keep')
            STAGE.RELEASE = tree.release
            STAGE.DEST = tree.dest
            STAGE.TARGET = tree.root / 'cargo-target'
            with self.assertRaises(SystemExit) as raised:
                STAGE.stage()
            self.assertIn('missing GUI build provenance sidecar', str(raised.exception))
            self.assertTrue(canary.is_file())
            self.assertEqual(canary.read_text(), 'keep')

    def test_stale_test_binary_with_cache_off_leaves_destination_intact(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = ReleaseTree(directory, mode='test')
            self.assertEqual(tree.write('test').returncode, 0)
            (tree.release / 'CMakeCache.txt').write_text('TDESKTOP_API_TEST:BOOL=OFF\n')
            tree.dest.mkdir()
            canary = tree.dest / 'canary'
            canary.write_text('keep')
            STAGE.RELEASE = tree.release
            STAGE.DEST = tree.dest
            STAGE.TARGET = tree.root / 'cargo-target'
            with self.assertRaises(SystemExit):
                STAGE.stage()
            self.assertTrue(canary.is_file())
            self.assertEqual(canary.read_text(), 'keep')


class ReleaseScriptGuardTest(unittest.TestCase):
    def _env(self, directory, extra=None):
        env = os.environ.copy()
        for name in (
            'DELTA_TEL_API_MODE', 'TDESKTOP_API_ID', 'TDESKTOP_API_HASH',
            'DELTA_TEL_STAGE_ALLOW_TEST_API', 'DELTA_TEL_DATA',
        ):
            env.pop(name, None)
        env['DELTA_TEL_DATA'] = directory
        if extra:
            env.update(extra)
        return env

    def _run(self, directory, args=None, extra=None):
        return subprocess.run(
            ['bash', str(ROOT / 'nix/release.sh'), *(args or [])],
            check=False,
            env=self._env(directory, extra),
            capture_output=True,
            text=True,
        )

    def test_release_sh_requires_production_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self._run(directory)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('DELTA_TEL_API_MODE=production', result.stderr)
            self.assertIn('--allow-test-api', result.stderr)
            self.assertNotIn(FAKE_API_HASH, result.stdout + result.stderr)

    def test_release_sh_production_without_credentials_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self._run(directory, extra={'DELTA_TEL_API_MODE': 'production'})
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('nonempty TDESKTOP_API_ID and TDESKTOP_API_HASH', result.stderr)
            self.assertNotIn(FAKE_API_HASH, result.stdout + result.stderr)

    def test_release_sh_rejects_allow_test_api_with_production(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self._run(directory, args=['--allow-test-api'], extra={
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
            })
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('cannot be combined with DELTA_TEL_API_MODE=production', result.stderr)
            self.assertNotIn(FAKE_API_HASH, result.stdout + result.stderr)
            self.assertNotIn(FAKE_API_ID, result.stdout + result.stderr)

    def test_release_sh_unknown_argument(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self._run(directory, args=['publish'])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Usage: nix/release.sh [all] [--allow-test-api]', result.stderr)


if __name__ == '__main__':
    unittest.main()

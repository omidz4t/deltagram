import datetime
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('semantic_release', ROOT / 'scripts/semantic-release.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
FILES = ('.version', 'CHANGELOG.md', 'tdesktop/Telegram/build/version',
         'tdesktop/Telegram/SourceFiles/core/version.h', 'tdesktop/changelog.txt')
FIXTURES = (
    '7.2.42\n',
    '# Changelog\n\n## 7.2.42 (2026-10-07)\n\n- Baseline.\n',
    'AppVersion 7002042\nAppVersionStrMajor 7.2\nAppVersionStrSmall 7.2.42\n'
    'AppVersionStr 7.2.42\nBetaChannel 1\nAlphaVersion 0\nAppVersionOriginal 7.2.42.beta\n',
    'constexpr auto AppVersion = 7002042;\nconstexpr auto AppVersionStr = "7.2.42";\n',
    '7.2.42 beta (07.10.26)\n\n- Baseline.\n',
)


class SemanticReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name, contents in zip(FILES, FIXTURES):
            destination = self.root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(contents)
        self.git('init', '--quiet')
        self.git('config', 'user.name', 'Release Test')
        self.git('config', 'user.email', 'release@example.org')
        self.git('config', 'commit.gpgsign', 'false')
        self.git('add', '.')
        self.commit('chore: establish baseline')
        self.base = self.git('rev-parse', 'HEAD')
        changelog = self.root / 'CHANGELOG.md'
        changelog.write_text(changelog.read_text().replace(
            '# Changelog\n', f'# Changelog\n\n<!-- release-commit: {self.base} -->'))

    def git(self, *args):
        return MODULE.git(self.root, *args)

    def commit(self, message):
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '--quiet', '--allow-empty', '-m', message)

    def snapshot(self):
        return {name: (self.root / name).read_bytes() for name in FILES}

    def run_release(self, **kwargs):
        return MODULE.release(self.root, date=datetime.date(2026, 10, 7), **kwargs)

    def test_analysis_rules(self):
        for message, expected in (
            ('feat(chat): add accounts', 'minor'), ('fix: repair RPC', 'patch'),
            ('perf: reduce allocations', 'patch'), ('docs: describe setup', None),
            ('ci: cache compilation', None), ('chore(release): 7.2.43', None),
            ('feat!: remove old RPC', 'major'),
            ('refactor(core): change API\n\nBREAKING CHANGE: remove old API', 'major'),
            ('fix: change API\n\nBREAKING-CHANGE: remove old API', 'major'),
        ):
            with self.subTest(message=message):
                self.assertEqual(MODULE.analyze(message)['level'], expected)
        self.assertIsNone(MODULE.analyze('old non-conventional message'))

    def test_no_release_for_maintenance(self):
        self.commit('docs: document setup')
        before = self.snapshot()
        self.assertIsNone(self.run_release())
        self.assertEqual(self.snapshot(), before)

    def test_patch_synchronizes_and_repeat_is_noop(self):
        self.commit('fix(rpc): repair transport')
        self.assertEqual(self.run_release(), '7.2.43')
        self.assertEqual((self.root / '.version').read_text(), '7.2.43\n')
        header = (self.root / FILES[3]).read_text()
        self.assertIn('AppVersion = 7002043;', header)
        self.assertIn('AppVersionStr = "7.2.43";', header)
        metadata = (self.root / FILES[2]).read_text()
        self.assertRegex(metadata, r'AppVersionOriginal\s+7\.2\.43\.beta')
        self.assertTrue((self.root / FILES[4]).read_text().startswith('7.2.43 beta (07.10.26)'))
        self.assertIn('rpc: repair transport', (self.root / FILES[1]).read_text())
        before = self.snapshot()
        self.assertIsNone(self.run_release())
        self.assertEqual(self.snapshot(), before)
        self.git('add', '.')
        self.commit('chore(release): prepare 7.2.43')
        self.assertIsNone(self.run_release())

    def test_highest_level_and_dry_run(self):
        self.commit('fix: first fix')
        self.commit('feat: add accounts')
        before = self.snapshot()
        self.assertEqual(self.run_release(dry_run=True), '7.3.0')
        self.assertEqual(self.snapshot(), before)
        self.commit('feat!: incompatible accounts')
        self.assertEqual(self.run_release(), '8.0.0')
        self.assertIn('AppVersion = 8000000;', (self.root / FILES[3]).read_text())

    def test_validation_failure_does_not_modify_files(self):
        self.commit('fix: repair RPC')
        (self.root / '.version').write_text('7.2.999\n')
        before = self.snapshot()
        with self.assertRaises(ValueError):
            self.run_release()
        self.assertEqual(self.snapshot(), before)

    def test_overflow_is_rejected(self):
        for value in ('1.1000.0', '1000.0.0', '1.2.1000', '01.2.3', '1.2.3-beta'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                MODULE.parse_version(value)

    def test_tag_fallback_and_explicit_boundary(self):
        changelog = self.root / 'CHANGELOG.md'
        changelog.write_text(MODULE.MARKER.sub('', changelog.read_text()))
        self.git('tag', 'v7.2.42', self.base)
        self.commit('fix: repair transport')
        self.assertEqual(self.run_release(dry_run=True), '7.2.43')
        self.assertEqual(self.run_release(since=self.base, dry_run=True), '7.2.43')

    def test_component_overflow_preserves_files(self):
        version = '7.2.999'
        metadata, header, _ = MODULE.app_versions(
            (self.root / FILES[2]).read_text(), (self.root / FILES[3]).read_text(), version)
        (self.root / '.version').write_text(version + '\n')
        (self.root / FILES[2]).write_text(metadata)
        (self.root / FILES[3]).write_text(header)
        self.commit('fix: repair transport')
        before = self.snapshot()
        with self.assertRaises(ValueError):
            self.run_release()
        self.assertEqual(self.snapshot(), before)

    def test_failed_replace_restores_all_files(self):
        self.commit('fix: repair RPC')
        before = self.snapshot()
        real_replace = MODULE.os.replace
        calls = 0

        def fail_second(source, destination):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('simulated failure')
            real_replace(source, destination)

        with patch.object(MODULE.os, 'replace', side_effect=fail_second):
            with self.assertRaises(OSError):
                self.run_release()
        self.assertEqual(self.snapshot(), before)


if __name__ == '__main__':
    unittest.main()

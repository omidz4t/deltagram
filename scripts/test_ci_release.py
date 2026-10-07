import importlib.util
from pathlib import Path
import tarfile
import unittest

import test_semantic_release as fixtures

SPEC = importlib.util.spec_from_file_location('ci_release', Path(__file__).with_name('ci-release.py'))
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


class AutomatedReleaseTest(unittest.TestCase):
    def setUp(self):
        fixture = fixtures.SemanticReleaseTest('test_analysis_rules')
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        self.root = fixture.root
        self.git = fixture.git
        self.commit = fixture.commit
        metadata = self.root / 'tdesktop/Telegram/build/version'
        header = self.root / 'tdesktop/Telegram/SourceFiles/core/version.h'
        a, b, _ = CI.SEMANTIC.app_versions(metadata.read_text(), header.read_text(), '0.1.0')
        metadata.write_text(a)
        header.write_text(b)
        (self.root / '.version').write_text('0.1.0\n')
        changelog = self.root / 'CHANGELOG.md'
        changelog.write_text(changelog.read_text().replace('7.2.42', '0.1.0'))
        self.git('add', '.')
        self.commit('chore: prepare experimental baseline')

    def test_initial_release_keeps_requested_version(self):
        self.commit('feat: initial client')
        self.assertEqual(CI.prepare(self.root), '0.1.0')
        self.assertIn('Experimental', (self.root / 'data/release-plan/notes.md').read_text())

    def test_fix_after_tag_updates_app_and_source_archive(self):
        self.git('tag', 'v0.1.0')
        self.commit('fix: repair attachments')
        head = self.git('rev-parse', 'HEAD')
        self.assertEqual(CI.prepare(self.root), '0.1.1')
        self.assertEqual(self.git('rev-parse', 'HEAD'), head)
        self.assertIn('AppVersion 1001', (self.root / 'tdesktop/Telegram/build/version').read_text())
        CI.source_archive(self.root)
        with tarfile.open(self.root / 'dist/deltagram-0.1.1-source.tar.gz') as source:
            self.assertEqual(source.extractfile('deltagram-0.1.1/.version').read(), b'0.1.1\n')
            self.assertTrue(all(m.uid == m.gid == 0 and not m.uname and not m.gname for m in source))

    def test_docs_do_not_release(self):
        self.git('tag', 'v0.1.0')
        self.commit('docs: update instructions')
        self.assertIsNone(CI.prepare(self.root))

    def test_future_release_uses_latest_tag_not_stale_baseline(self):
        self.git('tag', 'v0.1.0')
        self.commit('feat: add channels')
        self.git('tag', 'v0.2.0')
        self.commit('fix: repair channel rendering')
        self.assertEqual(CI.prepare(self.root), '0.2.1')
        self.assertIn('repair channel rendering', (self.root / 'data/release-plan/notes.md').read_text())
        self.assertNotIn('add channels', (self.root / 'data/release-plan/notes.md').read_text())


if __name__ == '__main__':
    unittest.main()

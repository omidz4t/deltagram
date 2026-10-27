"""Offline checks for unpublished PR and manual validation artifacts."""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'nix/stage-validation-artifacts.py'
HEAD = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
BASE = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
CHECKOUT = 'cccccccccccccccccccccccccccccccccccccccc'


def write_executable(path, contents):
    path.write_text(contents)
    path.chmod(0o755)


def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ValidationArtifactTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.data = root / 'data'
        (self.data / 'tdesktop-release/bin').mkdir(parents=True)
        (self.data / 'cargo-target/debug').mkdir(parents=True)
        (self.data / 'delta-shell/Testing/Temporary').mkdir(parents=True)
        (self.data / 'tdesktop-release/bin/deltagram.stripped').write_bytes(b'stripped-gui')
        (self.data / 'tdesktop-release/bin/deltagram.version').write_text('0.3.0\n')
        (self.data / 'tdesktop-release/bin/Telegram').write_bytes(b'unstripped-gui')
        (self.data / 'cargo-target/debug/deltachat-rpc-server').write_bytes(b'core-rpc')
        (self.data / 'delta-shell/Testing/Temporary/LastTest.log').write_text('100% tests passed\n')
        tools = root / 'tools'
        tools.mkdir()
        write_executable(tools / 'strip', '#!/bin/bash\ncp "$4" "$3"\n')
        write_executable(tools / 'nm', '#!/bin/bash\nprintf "00000000 T fixture_symbol\\n"\n')
        self.env = os.environ.copy()
        for name in (
            'VALIDATION_INCLUDE_SYMBOLS', 'VALIDATION_HEAD_SHA',
            'VALIDATION_BASE_SHA', 'VALIDATION_PR_NUMBER', 'GITHUB_SHA',
            'GITHUB_EVENT_NAME', 'GITHUB_REF', 'GITHUB_REPOSITORY',
            'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT',
            'RELEASE_DIR', 'CARGO_TARGET_DIR',
        ):
            self.env.pop(name, None)
        self.env.update(
            PATH=str(tools) + os.pathsep + self.env['PATH'],
            DELTA_TEL_DATA=str(self.data),
            GITHUB_SHA=CHECKOUT,
            GITHUB_EVENT_NAME='pull_request',
            GITHUB_REF='refs/pull/2/merge',
            GITHUB_REPOSITORY='mamaru-a/deltagram',
            GITHUB_RUN_ID='40248542298',
            GITHUB_RUN_ATTEMPT='1',
            VALIDATION_HEAD_SHA=HEAD,
            VALIDATION_BASE_SHA=BASE,
            VALIDATION_PR_NUMBER='2',
        )

    def run_stage(self, extra=None, check=True):
        env = self.env.copy()
        if extra:
            env.update(extra)
        return subprocess.run(
            [sys.executable, str(SCRIPT)], env=env, capture_output=True, text=True,
            check=check)

    def artifact(self, name):
        return self.data / 'validation-artifacts' / name

    def test_pull_request_omits_symbols_and_records_merge_provenance(self):
        self.run_stage()
        dest = self.data / 'validation-artifacts'
        self.assertEqual(sorted(path.name for path in dest.iterdir()), [
            'SHA256SUMS',
            'deltachat-rpc-server',
            'deltagram.stripped',
            'deltagram.version',
            'integrity-tests.log',
            'provenance.json',
        ])
        provenance = json.loads(self.artifact('provenance.json').read_text())
        self.assertEqual(provenance['checkout_sha'], CHECKOUT)
        self.assertEqual(provenance['head_sha'], HEAD)
        self.assertEqual(provenance['base_sha'], BASE)
        self.assertEqual(provenance['event_name'], 'pull_request')
        self.assertEqual(provenance['pr_number'], 2)
        self.assertEqual(provenance['ref'], 'refs/pull/2/merge')
        self.assertEqual(provenance['repository'], 'mamaru-a/deltagram')
        self.assertEqual(provenance['run_id'], '40248542298')
        self.assertNotIn('deltagram.symbols', provenance)
        manifest = self.artifact('SHA256SUMS').read_text().splitlines()
        self.assertEqual([line.split()[-1] for line in manifest], [
            'deltagram.stripped',
            'deltachat-rpc-server',
            'deltagram.version',
            'integrity-tests.log',
            'provenance.json',
        ])
        for line in manifest:
            digest, name = line.split('  ')
            self.assertEqual(digest, checksum(self.artifact(name)))

    def test_manual_validation_keeps_symbol_map(self):
        self.run_stage({
            'VALIDATION_INCLUDE_SYMBOLS': 'true',
            'GITHUB_EVENT_NAME': 'workflow_dispatch',
            'GITHUB_REF': 'refs/heads/fix/client-integrity',
            'VALIDATION_HEAD_SHA': '',
            'VALIDATION_BASE_SHA': '',
            'VALIDATION_PR_NUMBER': '',
        })
        self.assertEqual(self.artifact('deltagram.symbols').read_text(),
                         '00000000 T fixture_symbol\n')
        names = [line.split()[-1] for line in self.artifact('SHA256SUMS').read_text().splitlines()]
        self.assertEqual(names[:3], [
            'deltagram.stripped', 'deltachat-rpc-server', 'deltagram.symbols'])
        provenance = json.loads(self.artifact('provenance.json').read_text())
        self.assertEqual(provenance['event_name'], 'workflow_dispatch')
        self.assertNotIn('head_sha', provenance)
        self.assertNotIn('base_sha', provenance)
        self.assertNotIn('pr_number', provenance)
        self.assertNotIn('title', provenance)
        self.assertNotIn('actor', provenance)

    def test_replaces_leftover_symbol_file_on_pull_request(self):
        dest = self.data / 'validation-artifacts'
        dest.mkdir()
        (dest / 'deltagram.symbols').write_text('stale symbols\n')
        self.run_stage()
        self.assertFalse(self.artifact('deltagram.symbols').exists())

    def test_rejects_untrusted_revision_text(self):
        self.run_stage({
            'VALIDATION_HEAD_SHA': '$(reboot); abcdef',
            'VALIDATION_BASE_SHA': HEAD,
            'VALIDATION_PR_NUMBER': '2; echo pwned',
            'GITHUB_EVENT_NAME': 'pull_request\nworkflow_dispatch',
            'GITHUB_REF': 'refs/heads/' + 'x' * 300,
        })
        provenance = json.loads(self.artifact('provenance.json').read_text())
        self.assertNotIn('head_sha', provenance)
        self.assertEqual(provenance['base_sha'], HEAD)
        self.assertNotIn('pr_number', provenance)
        self.assertEqual(provenance['event_name'], '')
        self.assertEqual(provenance['ref'], '')
        self.assertNotIn('$(reboot)', self.artifact('provenance.json').read_text())

    def test_missing_binary_fails_before_writing_artifacts(self):
        (self.data / 'tdesktop-release/bin/deltagram.stripped').unlink()
        result = self.run_stage(check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing validation input', result.stderr)
        self.assertFalse((self.data / 'validation-artifacts').exists())


if __name__ == '__main__':
    unittest.main()

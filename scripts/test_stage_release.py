"""Regression for packaging copies of read-only Nix runtime directories."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'stage_release', Path(__file__).resolve().parents[1] / 'nix/stage-release.py')
STAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGE)


class RuntimeCopyTest(unittest.TestCase):
    def test_readonly_runtime_copy_can_be_patched_without_changing_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            (source / 'plugins').mkdir(parents=True)
            binary = source / 'plugins/runtime.so'
            binary.write_bytes(b'runtime fixture')
            binary.chmod(0o444)
            (source / 'plugins').chmod(0o555)
            source.chmod(0o555)
            try:
                target = root / 'stage'
                shutil.copytree(source, target)
                STAGE.writable_tree(target)
                for path in (target, target / 'plugins'):
                    self.assertEqual(path.stat().st_mode & 0o700, 0o700)
                self.assertTrue((target / 'plugins/runtime.so').stat().st_mode & 0o200)
                (target / 'plugins/runtime.so').write_bytes(b'patched runtime')
                self.assertEqual(binary.read_bytes(), b'runtime fixture')
                self.assertEqual(binary.stat().st_mode & 0o777, 0o444)
                self.assertEqual((source / 'plugins').stat().st_mode & 0o777, 0o555)
                shutil.rmtree(target)
            finally:
                STAGE.writable_tree(source)

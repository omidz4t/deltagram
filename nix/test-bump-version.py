import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).with_name('bump-version.py')
SOURCE = '''AppVersion         7002010
AppVersionStrMajor 7.2
AppVersionStrSmall 7.2.10
AppVersionStr      7.2.10
BetaChannel        1
AlphaVersion       0
AppVersionOriginal 7.2.10.beta
'''


class VersionTest(unittest.TestCase):
    def test_consecutive_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'version'
            path.write_text(SOURCE)
            header = pathlib.Path(directory) / 'version.h'
            header.write_text('constexpr auto AppVersion = 7002010;\n'
                              'constexpr auto AppVersionStr = "7.2.10";\n')
            for patch in (11, 12):
                result = subprocess.check_output(
                    [sys.executable, str(SCRIPT), str(path), str(header)], text=True)
                self.assertEqual(result.strip(), f'7.2.{patch}')
                fields = dict(line.split() for line in path.read_text().splitlines())
                self.assertEqual(fields['AppVersion'], str(7002000 + patch))
                self.assertEqual(fields['AppVersionOriginal'], f'7.2.{patch}.beta')
                self.assertEqual(fields['AppVersionStrSmall'], f'7.2.{patch}')
                self.assertEqual(fields['BetaChannel'], '1')
                self.assertIn(f'AppVersionStr = "7.2.{patch}";', header.read_text())
                self.assertIn(f'AppVersion = {7002000 + patch};', header.read_text())

    def test_overflow_does_not_modify_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'version'
            original = SOURCE.replace('7.2.10', '7.2.999')
            path.write_text(original)
            result = subprocess.run([sys.executable, str(SCRIPT), str(path)],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_text(), original)


if __name__ == '__main__':
    unittest.main()

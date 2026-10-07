"""Check release version stability without compiling Qt."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReleaseModeTest(unittest.TestCase):
    def test_version_modes_preserve_incremental_records(self):
        for bump, expected in (('0', '7.2.10'), (None, '7.2.10')):
            with self.subTest(bump=bump), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                (root / 'nix').mkdir()
                for name in ('build-release.sh', 'paths.sh', 'bump-version.py'):
                    shutil.copyfile(ROOT / 'nix' / name, root / 'nix' / name)
                version = root / 'tdesktop/Telegram/build/version'
                version.parent.mkdir(parents=True)
                version.write_text('AppVersion 7002010\nAppVersionStrMajor 7.2\n'
                                   'AppVersionStrSmall 7.2.10\nAppVersionStr 7.2.10\n'
                                   'BetaChannel 0\nAlphaVersion 0\n'
                                   'AppVersionOriginal 7.2.10\n')
                header = root / 'tdesktop/Telegram/SourceFiles/core/version.h'
                header.parent.mkdir(parents=True)
                header.write_text('constexpr auto AppVersion = 7002010;\n'
                                  'constexpr auto AppVersionStr = "7.2.10";\n')
                original = (version.read_bytes(), header.read_bytes())
                release = root / 'data/tdesktop-release'
                (release / 'bin').mkdir(parents=True)
                (release / 'bin/Telegram').write_text('fixture executable')
                records = ('build.ninja', '.ninja_deps', '.ninja_log')
                for name in records:
                    (release / name).write_text('preserved incremental record')
                tools = root / 'tools'
                tools.mkdir()
                commands = {
                    'cmake': '#!/bin/bash\nprintf "%s\\n" "$*" > "$RELEASE_DIR/arguments"\n',
                    'strip': '#!/bin/bash\ncp "$4" "$3"\n',
                }
                for name, contents in commands.items():
                    path = tools / name
                    path.write_text(contents)
                    path.chmod(0o755)
                env = os.environ.copy()
                for name in ('DELTA_TEL_DATA', 'RELEASE_DIR', 'CCACHE_DIR',
                             'TMPDIR', 'DELTA_TEL_BUMP_VERSION', 'DELTA_TEL_PACK_UPX'):
                    env.pop(name, None)
                env.update(PATH=str(tools) + os.pathsep + env['PATH'], DELTA_TEL_JOBS='4')
                if bump is not None:
                    env['DELTA_TEL_BUMP_VERSION'] = bump
                subprocess.run(['bash', str(root / 'nix/build-release.sh')],
                               env=env, check=True, capture_output=True)
                self.assertEqual((release / 'bin/Telegram.version').read_text(), expected + '\n')
                self.assertIn('--target Telegram -j 4', (release / 'arguments').read_text())
                for name in records:
                    self.assertEqual((release / name).read_text(), 'preserved incremental record')
                self.assertEqual((version.read_bytes(), header.read_bytes()), original)


if __name__ == '__main__':
    unittest.main()

"""Check release version stability and Telegram API configure modes without compiling Qt."""
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
NIX_SCRIPTS = (
    'build-release.sh',
    'build-tdesktop.sh',
    'paths.sh',
    'bump-version.py',
    'telegram-api-config.sh',
    'build-provenance.py',
)
FAKE_API_ID = '24680'
FAKE_API_HASH = '0123456789abcdef0123456789abcdef'
OTHER_API_ID = '13579'
OTHER_API_HASH = 'abcdef0123456789abcdef0123456789'
NINJA_RECORDS = ('build.ninja', '.ninja_deps', '.ninja_log')
CMAKE_STUB = r'''#!/bin/bash
set -euo pipefail
outdir=""
prev=""
is_build=0
for arg in "$@"; do
  if [ "$prev" = "--build" ]; then
    outdir="$arg"
    is_build=1
  fi
  if [ "$prev" = "-B" ]; then
    outdir="$arg"
  fi
  if [ "$arg" = "--target" ]; then
    is_build=1
  fi
  prev="$arg"
done
if [ -z "$outdir" ]; then
  outdir="${RELEASE_DIR:-.}"
fi
mkdir -p "$outdir"
redacted=""
for arg in "$@"; do
  case "$arg" in
    -DTDESKTOP_API_ID=*|-DTDESKTOP_API_HASH=*)
      arg="${arg%%=*}=<redacted>"
      ;;
  esac
  if [ -z "$redacted" ]; then
    redacted="$arg"
  else
    redacted="$redacted $arg"
  fi
done
printf '%s\n' "$redacted" >> "$outdir/arguments"
if [ "$is_build" -eq 1 ]; then
  exit 0
fi
cache="$outdir/CMakeCache.txt"
touch "$cache"
prev=""
for arg in "$@"; do
  if [ "$prev" = "-U" ]; then
    tmp="$(mktemp)"
    awk -v key="$arg" 'index($0, key ":") != 1' "$cache" > "$tmp"
    mv "$tmp" "$cache"
  fi
  case "$arg" in
    -UTDESKTOP_API_ID|-UTDESKTOP_API_HASH|-UTDESKTOP_API_TEST)
      key="${arg#-U}"
      tmp="$(mktemp)"
      awk -v key="$key" 'index($0, key ":") != 1' "$cache" > "$tmp"
      mv "$tmp" "$cache"
      ;;
    -DTDESKTOP_API_TEST=*|-DTDESKTOP_API_ID=*|-DTDESKTOP_API_HASH=*)
      key="${arg%%=*}"
      key="${key#-D}"
      value="${arg#*=}"
      tmp="$(mktemp)"
      awk -v key="$key" 'index($0, key ":") != 1' "$cache" > "$tmp"
      if [ "$key" = "TDESKTOP_API_TEST" ]; then
        printf '%s:BOOL=%s\n' "$key" "$value" >> "$tmp"
      else
        printf '%s:STRING=%s\n' "$key" "$value" >> "$tmp"
      fi
      mv "$tmp" "$cache"
      ;;
  esac
  prev="$arg"
done
'''


def donor_test_pair():
    text = (ROOT / 'tdesktop/Telegram/cmake/telegram_options.cmake').read_text()
    match = re.search(
        r'if \(TDESKTOP_API_TEST\)\s+set\(TDESKTOP_API_ID (\d+)\)\s+'
        r'set\(TDESKTOP_API_HASH ([0-9a-fA-F]+)\)',
        text)
    if match is None:
        raise AssertionError('TDESKTOP_API_TEST pair missing from telegram_options.cmake')
    return match.group(1), match.group(2)


def configure_lines(arguments):
    return [line for line in arguments.splitlines() if '-G Ninja' in line]


def build_lines(arguments):
    return [line for line in arguments.splitlines() if '--target' in line]


class ReleaseFixture:
    def __init__(self, directory, script='build-release.sh'):
        self.root = pathlib.Path(directory)
        self.script = script
        self.data = self.root / 'data'
        if script == 'build-release.sh':
            self.build_dir = self.data / 'tdesktop-release'
        else:
            self.build_dir = self.data / 'tdesktop-out'
        (self.root / 'nix').mkdir()
        for name in NIX_SCRIPTS:
            shutil.copyfile(ROOT / 'nix' / name, self.root / 'nix' / name)
        options = self.root / 'tdesktop/Telegram/cmake/telegram_options.cmake'
        options.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / 'tdesktop/Telegram/cmake/telegram_options.cmake', options)
        cmake_lists = self.root / 'tdesktop/cmake/CMakeLists.txt'
        cmake_lists.parent.mkdir(parents=True, exist_ok=True)
        cmake_lists.write_text('# fixture\n')
        version = self.root / 'tdesktop/Telegram/build/version'
        version.parent.mkdir(parents=True, exist_ok=True)
        version.write_text('AppVersion 7002010\nAppVersionStrMajor 7.2\n'
                           'AppVersionStrSmall 7.2.10\nAppVersionStr 7.2.10\n'
                           'BetaChannel 0\nAlphaVersion 0\n'
                           'AppVersionOriginal 7.2.10\n')
        header = self.root / 'tdesktop/Telegram/SourceFiles/core/version.h'
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('constexpr auto AppVersion = 7002010;\n'
                          'constexpr auto AppVersionStr = "7.2.10";\n')
        self.version_bytes = (version.read_bytes(), header.read_bytes())
        (self.build_dir / 'bin').mkdir(parents=True)
        (self.build_dir / 'bin/Telegram').write_text('fixture executable')
        tools = self.root / 'tools'
        tools.mkdir()
        (tools / 'cmake').write_text(CMAKE_STUB)
        (tools / 'cmake').chmod(0o755)
        (tools / 'strip').write_text('#!/bin/bash\ncp "$4" "$3"\n')
        (tools / 'strip').chmod(0o755)
        self.donor_id, self.donor_hash = donor_test_pair()
        self.secrets = (FAKE_API_HASH, OTHER_API_HASH, self.donor_hash)

    def write_ninja_records(self):
        for name in NINJA_RECORDS:
            (self.build_dir / name).write_text('preserved incremental record')

    def write_cache(self, mode, api_id='', api_hash=''):
        if mode == 'test':
            body = ('TDESKTOP_API_TEST:BOOL=ON\n'
                    'TDESKTOP_API_ID:STRING=0\n'
                    'TDESKTOP_API_HASH:STRING=\n')
        else:
            body = (f'TDESKTOP_API_TEST:BOOL=OFF\n'
                    f'TDESKTOP_API_ID:STRING={api_id}\n'
                    f'TDESKTOP_API_HASH:STRING={api_hash}\n')
        (self.build_dir / 'CMakeCache.txt').write_text(body)

    def ninja_text(self):
        return {name: (self.build_dir / name).read_text() for name in NINJA_RECORDS}

    def env(self, extra=None):
        env = os.environ.copy()
        for name in (
            'DELTA_TEL_DATA', 'RELEASE_DIR', 'CMAKE_BUILD_DIR', 'CCACHE_DIR',
            'TMPDIR', 'DELTA_TEL_BUMP_VERSION', 'DELTA_TEL_PACK_UPX',
            'DELTA_TEL_API_MODE', 'TDESKTOP_API_ID', 'TDESKTOP_API_HASH',
            'TDESKTOP_API_TEST', 'GITHUB_SHA', 'GITHUB_EVENT_NAME',
            'GITHUB_REF', 'GITHUB_REPOSITORY', 'GITHUB_RUN_ID',
            'GITHUB_RUN_ATTEMPT', 'GITHUB_OUTPUT',
        ):
            env.pop(name, None)
        env.update(
            PATH=str(self.root / 'tools') + os.pathsep + env['PATH'],
            DELTA_TEL_JOBS='4',
            NIX_BUILD_CORES='4',
        )
        if extra:
            env.update(extra)
        return env

    def run(self, extra=None, check=True):
        result = subprocess.run(
            ['bash', str(self.root / 'nix' / self.script)],
            check=False,
            env=self.env(extra),
            capture_output=True,
            text=True,
        )
        combined = result.stdout + result.stderr
        for secret in self.secrets:
            if secret and secret in combined:
                raise AssertionError('API credential leaked to stdout/stderr')
        if check and result.returncode != 0:
            raise AssertionError(result.stderr or result.stdout or 'command failed')
        return result

    def arguments(self):
        path = self.build_dir / 'arguments'
        if not path.is_file():
            return ''
        text = path.read_text()
        for secret in self.secrets:
            if secret and secret in text:
                raise AssertionError('API credential retained in cmake argument log')
        return text

    def provenance_path(self):
        return self.build_dir / 'bin/deltagram.build-provenance.json'

    def provenance(self):
        path = self.provenance_path()
        if not path.is_file():
            return None
        text = path.read_text()
        for secret in self.secrets:
            if secret and secret in text:
                raise AssertionError('API credential retained in build provenance')
        if FAKE_API_ID in text or OTHER_API_ID in text:
            raise AssertionError('API identifier retained in build provenance')
        return json.loads(text)

    def gui_digest(self):
        return hashlib.sha256((self.build_dir / 'bin/deltagram.stripped').read_bytes()).hexdigest()


class ReleaseModeTest(unittest.TestCase):
    def test_version_modes_preserve_incremental_records(self):
        for bump, expected in (('0', '7.2.10'), (None, '7.2.10')):
            with self.subTest(bump=bump), tempfile.TemporaryDirectory() as directory:
                fixture = ReleaseFixture(directory)
                fixture.write_ninja_records()
                fixture.write_cache('test')
                extra = {}
                if bump is not None:
                    extra['DELTA_TEL_BUMP_VERSION'] = bump
                fixture.run(extra)
                release = fixture.build_dir
                self.assertEqual((release / 'bin/deltagram.version').read_text(), expected + '\n')
                builds = build_lines(fixture.arguments())
                self.assertEqual(configure_lines(fixture.arguments()), [])
                self.assertEqual(len(builds), 2)
                self.assertIn('--target Telegram/CMakeFiles/Telegram.dir/SourceFiles/'
                              'delta/delta_bridge.cpp.o -j 4', builds[0])
                self.assertIn('--target Telegram -j 4', builds[1])
                for name in NINJA_RECORDS:
                    self.assertEqual((release / name).read_text(), 'preserved incremental record')
                self.assertEqual(
                    ((fixture.root / 'tdesktop/Telegram/build/version').read_bytes(),
                     (fixture.root / 'tdesktop/Telegram/SourceFiles/core/version.h').read_bytes()),
                    fixture.version_bytes)


class TelegramApiConfigTest(unittest.TestCase):
    def test_default_test_mode_configures_without_ninja(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            result = fixture.run()
            configures = configure_lines(fixture.arguments())
            self.assertEqual(len(configures), 1)
            self.assertIn('-DTDESKTOP_API_TEST=ON', configures[0])
            self.assertIn('-U TDESKTOP_API_ID', configures[0])
            self.assertIn('-U TDESKTOP_API_HASH', configures[0])
            self.assertNotIn('-DTDESKTOP_API_ID=', configures[0])
            self.assertNotIn('-DTDESKTOP_API_HASH=', configures[0])
            self.assertIn('Telegram API mode: test', result.stdout)
            self.assertIn('Wrote GUI build provenance: test', result.stdout)
            self.assertEqual(len(build_lines(fixture.arguments())), 2)
            payload = fixture.provenance()
            self.assertEqual(payload['api_mode'], 'test')
            self.assertEqual(payload['tdesktop_api_test'], 'ON')
            self.assertEqual(payload['gui_sha256'], fixture.gui_digest())
            self.assertNotIn('source_sha', payload)

    def test_matching_test_cache_skips_configure(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            fixture.write_ninja_records()
            fixture.write_cache('test')
            fixture.run()
            self.assertEqual(configure_lines(fixture.arguments()), [])
            self.assertEqual(len(build_lines(fixture.arguments())), 2)

    def test_production_requires_credentials_before_compile(self):
        cases = (
            {'DELTA_TEL_API_MODE': 'production'},
            {'DELTA_TEL_API_MODE': 'production', 'TDESKTOP_API_ID': FAKE_API_ID},
            {'DELTA_TEL_API_MODE': 'production', 'TDESKTOP_API_HASH': FAKE_API_HASH},
        )
        for extra in cases:
            with self.subTest(extra=sorted(extra)), tempfile.TemporaryDirectory() as directory:
                fixture = ReleaseFixture(directory)
                fixture.write_ninja_records()
                fixture.write_cache('production', FAKE_API_ID, FAKE_API_HASH)
                result = fixture.run(extra, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('production Telegram API mode requires nonempty TDESKTOP_API_ID',
                              result.stderr)
                self.assertEqual(fixture.arguments(), '')
                self.assertFalse(fixture.provenance_path().exists())
                self.assertEqual(fixture.ninja_text(), dict.fromkeys(
                    NINJA_RECORDS, 'preserved incremental record'))

    def test_production_rejects_malformed_credentials(self):
        cases = (
            {'TDESKTOP_API_ID': '0', 'TDESKTOP_API_HASH': FAKE_API_HASH},
            {'TDESKTOP_API_ID': 'api-id', 'TDESKTOP_API_HASH': FAKE_API_HASH},
            {'TDESKTOP_API_ID': FAKE_API_ID, 'TDESKTOP_API_HASH': 'short'},
            {'TDESKTOP_API_ID': FAKE_API_ID, 'TDESKTOP_API_HASH': 'not-a-hex-hash-value-0000000000'},
        )
        for extra in cases:
            extra = dict(extra, DELTA_TEL_API_MODE='production')
            with self.subTest(extra=sorted(extra)), tempfile.TemporaryDirectory() as directory:
                fixture = ReleaseFixture(directory)
                result = fixture.run(extra, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(
                    'malformed' in result.stderr,
                    result.stderr)
                self.assertEqual(fixture.arguments(), '')
                self.assertFalse(fixture.provenance_path().exists())

    def test_production_rejects_donor_test_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            result = fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': fixture.donor_id,
                'TDESKTOP_API_HASH': fixture.donor_hash,
            }, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('rejects the donor TDESKTOP_API_TEST application credentials',
                          result.stderr)
            self.assertEqual(fixture.arguments(), '')
            self.assertFalse(fixture.provenance_path().exists())

    def test_production_configures_explicit_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            result = fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
            })
            configures = configure_lines(fixture.arguments())
            self.assertEqual(len(configures), 1)
            self.assertIn('-DTDESKTOP_API_TEST=OFF', configures[0])
            self.assertIn('-DTDESKTOP_API_ID=<redacted>', configures[0])
            self.assertIn('-DTDESKTOP_API_HASH=<redacted>', configures[0])
            self.assertIn('Telegram API mode: production', result.stdout)
            self.assertIn('Wrote GUI build provenance: production', result.stdout)
            cache = (fixture.build_dir / 'CMakeCache.txt').read_text()
            self.assertIn('TDESKTOP_API_TEST:BOOL=OFF', cache)
            self.assertIn(f'TDESKTOP_API_ID:STRING={FAKE_API_ID}', cache)
            payload = fixture.provenance()
            self.assertEqual(payload['api_mode'], 'production')
            self.assertEqual(payload['tdesktop_api_test'], 'OFF')
            self.assertEqual(payload['gui_sha256'], fixture.gui_digest())
            self.assertNotIn('source_sha', payload)
            self.assertNotIn(FAKE_API_HASH, json.dumps(payload))

    def test_test_cache_reconfigures_for_production_without_deleting_ninja(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            fixture.write_ninja_records()
            fixture.write_cache('test')
            result = fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
            })
            self.assertIn('Reconfiguring CMake Telegram API settings', result.stdout)
            self.assertEqual(len(configure_lines(fixture.arguments())), 1)
            self.assertEqual(fixture.ninja_text(), dict.fromkeys(
                NINJA_RECORDS, 'preserved incremental record'))

    def test_test_prod_test_drops_production_flags_from_later_configure(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            fixture.write_ninja_records()
            fixture.write_cache('test')
            fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
            })
            fixture.run()
            configures = configure_lines(fixture.arguments())
            self.assertEqual(len(configures), 2)
            self.assertIn('-DTDESKTOP_API_TEST=OFF', configures[0])
            self.assertIn('-DTDESKTOP_API_ID=<redacted>', configures[0])
            self.assertIn('-DTDESKTOP_API_TEST=ON', configures[1])
            self.assertIn('-U TDESKTOP_API_ID', configures[1])
            self.assertIn('-U TDESKTOP_API_HASH', configures[1])
            self.assertNotIn('-DTDESKTOP_API_ID=', configures[1])
            self.assertNotIn('-DTDESKTOP_API_HASH=', configures[1])
            self.assertEqual(fixture.ninja_text(), dict.fromkeys(
                NINJA_RECORDS, 'preserved incremental record'))
            cache = (fixture.build_dir / 'CMakeCache.txt').read_text()
            self.assertIn('TDESKTOP_API_TEST:BOOL=ON', cache)
            self.assertNotIn(FAKE_API_HASH, cache)
            self.assertNotIn(FAKE_API_ID, cache)

    def test_matching_production_cache_skips_configure(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            fixture.write_ninja_records()
            fixture.write_cache('production', FAKE_API_ID, FAKE_API_HASH)
            fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
            })
            self.assertEqual(configure_lines(fixture.arguments()), [])
            self.assertEqual(len(build_lines(fixture.arguments())), 2)

    def test_missing_or_invalid_cached_api_mode_reconfigures(self):
        for token in ('', 'invalid'):
            with self.subTest(token=token), tempfile.TemporaryDirectory() as directory:
                fixture = ReleaseFixture(directory)
                fixture.write_ninja_records()
                fixture.write_cache('production', FAKE_API_ID, FAKE_API_HASH)
                cache = fixture.build_dir / 'CMakeCache.txt'
                content = cache.read_text()
                content = content.replace('TDESKTOP_API_TEST:BOOL=OFF\n',
                                          f'TDESKTOP_API_TEST:BOOL={token}\n' if token else '')
                cache.write_text(content)
                fixture.run({
                    'DELTA_TEL_API_MODE': 'production',
                    'TDESKTOP_API_ID': FAKE_API_ID,
                    'TDESKTOP_API_HASH': FAKE_API_HASH,
                })
                self.assertEqual(len(configure_lines(fixture.arguments())), 1)
                self.assertEqual(fixture.ninja_text(), dict.fromkeys(
                    NINJA_RECORDS, 'preserved incremental record'))

    def test_changed_production_credentials_reconfigure(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            fixture.write_ninja_records()
            fixture.write_cache('production', FAKE_API_ID, FAKE_API_HASH)
            fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': OTHER_API_ID,
                'TDESKTOP_API_HASH': OTHER_API_HASH,
            })
            configures = configure_lines(fixture.arguments())
            self.assertEqual(len(configures), 1)
            self.assertIn('-DTDESKTOP_API_TEST=OFF', configures[0])
            self.assertEqual(fixture.ninja_text(), dict.fromkeys(
                NINJA_RECORDS, 'preserved incremental record'))

    def test_test_mode_ignores_credential_env_and_unrelated_dc_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            result = fixture.run({
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
                'TDESKTOP_API_TEST': 'OFF',
                'MTP_ENVIRONMENT': 'Test',
                'TELEGRAM_TEST_DC': '1',
                'DELTA_TEL_TEST_DC': '1',
                'GLOBAL_BRIDGE': '1',
            })
            configures = configure_lines(fixture.arguments())
            self.assertEqual(len(configures), 1)
            self.assertIn('-DTDESKTOP_API_TEST=ON', configures[0])
            self.assertNotIn('-DTDESKTOP_API_ID=', configures[0])
            self.assertIn('Telegram API mode: test', result.stdout)

    def test_provenance_records_ci_source_sha_without_secrets(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            sha = 'cccccccccccccccccccccccccccccccccccccccc'
            result = fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
                'GITHUB_SHA': sha,
            })
            payload = fixture.provenance()
            self.assertEqual(payload['source_sha'], sha)
            self.assertNotIn(FAKE_API_HASH, result.stdout + result.stderr + json.dumps(payload))
            self.assertNotIn(FAKE_API_ID, json.dumps(payload))

    def test_matching_production_cache_still_rewrites_provenance_after_strip(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            fixture.write_ninja_records()
            fixture.write_cache('production', FAKE_API_ID, FAKE_API_HASH)
            (fixture.build_dir / 'bin/deltagram.build-provenance.json').write_text(
                '{"schema":"stale"}\n')
            fixture.run({
                'DELTA_TEL_API_MODE': 'production',
                'TDESKTOP_API_ID': FAKE_API_ID,
                'TDESKTOP_API_HASH': FAKE_API_HASH,
            })
            self.assertEqual(configure_lines(fixture.arguments()), [])
            payload = fixture.provenance()
            self.assertEqual(payload['api_mode'], 'production')
            self.assertEqual(payload['gui_sha256'], fixture.gui_digest())

    def test_invalid_mode_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory)
            result = fixture.run({'DELTA_TEL_API_MODE': 'testdc'}, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('DELTA_TEL_API_MODE must be test or production', result.stderr)
            self.assertEqual(fixture.arguments(), '')
            self.assertFalse(fixture.provenance_path().exists())

    def test_build_tdesktop_default_test_and_cache_skip(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = ReleaseFixture(directory, script='build-tdesktop.sh')
            fixture.run()
            first = configure_lines(fixture.arguments())
            self.assertEqual(len(first), 1)
            self.assertIn('-DTDESKTOP_API_TEST=ON', first[0])
            self.assertIn('--target Telegram -j 4', build_lines(fixture.arguments())[0])
            (fixture.build_dir / 'arguments').unlink()
            fixture.write_ninja_records()
            fixture.run()
            self.assertEqual(configure_lines(fixture.arguments()), [])
            self.assertEqual(len(build_lines(fixture.arguments())), 1)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Non-secret GUI build provenance: Telegram API mode bound to the stripped binary SHA256.

Write this sidecar only after a successful GUI build and strip. Stage and publish
paths refuse packaging when the live binary hash, recorded mode, and CMake cache
disagree. This file must not read, store, or print TDESKTOP_API_ID or
TDESKTOP_API_HASH.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path

SCHEMA = "deltagram.gui-build-provenance/v1"
SIDECAR_NAME = "deltagram.build-provenance.json"
GUI_NAME = "deltagram.stripped"
SOURCE_SHA = re.compile(r"^[0-9a-f]{40}(?:[0-9a-f]{24})?$")
GUI_SHA = re.compile(r"^[0-9a-f]{64}$")
ALLOWED_FIELDS = {
    "schema",
    "api_mode",
    "tdesktop_api_test",
    "gui_sha256",
    "gui_name",
    "version",
    "source_sha",
}
MODE_CACHE = {"test": "ON", "production": "OFF"}
TRUE_TOKENS = {"ON", "TRUE", "YES", "1"}
FALSE_TOKENS = {"OFF", "FALSE", "NO", "0"}


def fail(message):
    raise SystemExit(message)


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def cmake_cache_value(cache, key):
    if not cache.is_file():
        return None
    prefix = key + ":"
    for raw in cache.read_text(errors="replace").splitlines():
        line = raw.strip().replace("\r", "")
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1].strip()
    return None


def cache_test_token(value):
    if value is None or value == "":
        return None
    token = value.strip().upper()
    if token in TRUE_TOKENS:
        return "ON"
    if token in FALSE_TOKENS:
        return "OFF"
    return None


def ci_source_sha():
    value = os.environ.get("GITHUB_SHA", "").strip().lower()
    return value if SOURCE_SHA.fullmatch(value) else None


def sidecar_path(release_dir):
    return Path(release_dir) / "bin" / SIDECAR_NAME


def gui_path(release_dir):
    return Path(release_dir) / "bin" / GUI_NAME


def read_version(path):
    try:
        lines = path.read_text().splitlines()
    except (OSError, UnicodeError):
        fail("GUI version file is missing or unreadable")
    if len(lines) != 1 or not lines[0].strip() or len(lines[0].strip()) > 64:
        fail("GUI version file is invalid")
    return lines[0].strip()


def dump(payload):
    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def load_payload(path):
    if not path.is_file():
        fail("missing GUI build provenance sidecar")
    try:
        payload = json.loads(path.read_text())
    except json.JSONDecodeError:
        fail("GUI build provenance sidecar is not valid JSON")
    if not isinstance(payload, dict):
        fail("GUI build provenance sidecar is not an object")
    extra = set(payload) - ALLOWED_FIELDS
    if extra:
        fail("GUI build provenance sidecar has unsupported fields")
    if payload.get("schema") != SCHEMA:
        fail("GUI build provenance sidecar schema is unsupported")
    mode = payload.get("api_mode")
    if mode not in MODE_CACHE:
        fail("GUI build provenance sidecar api_mode is invalid")
    token = payload.get("tdesktop_api_test")
    if token not in {"ON", "OFF"}:
        fail("GUI build provenance sidecar tdesktop_api_test is invalid")
    if token != MODE_CACHE[mode]:
        fail("GUI build provenance sidecar api_mode does not match tdesktop_api_test")
    digest = payload.get("gui_sha256")
    if not isinstance(digest, str) or not GUI_SHA.fullmatch(digest):
        fail("GUI build provenance sidecar gui_sha256 is invalid")
    if payload.get("gui_name") != GUI_NAME:
        fail("GUI build provenance sidecar gui_name is invalid")
    version = payload.get("version")
    if not isinstance(version, str) or not version or "\n" in version or len(version) > 64:
        fail("GUI build provenance sidecar version is invalid")
    if "source_sha" in payload:
        source = payload["source_sha"]
        if not isinstance(source, str) or not SOURCE_SHA.fullmatch(source):
            fail("GUI build provenance sidecar source_sha is invalid")
    return payload


def write_release(release_dir, mode):
    if mode not in MODE_CACHE:
        fail("DELTA_TEL_API_MODE must be test or production.")
    release = Path(release_dir)
    gui = gui_path(release)
    version_path = release / "bin/deltagram.version"
    cache = release / "CMakeCache.txt"
    if not gui.is_file():
        fail("missing stripped GUI binary; build the release first")
    if not version_path.is_file():
        fail("missing GUI version file; build the release first")
    token = cache_test_token(cmake_cache_value(cache, "TDESKTOP_API_TEST"))
    if token is None:
        fail("CMake cache TDESKTOP_API_TEST is missing or invalid")
    if token != MODE_CACHE[mode]:
        fail("CMake cache TDESKTOP_API_TEST does not match the GUI build mode")
    version = read_version(version_path)
    payload = {
        "schema": SCHEMA,
        "api_mode": mode,
        "tdesktop_api_test": token,
        "gui_sha256": sha256_file(gui),
        "gui_name": GUI_NAME,
        "version": version,
    }
    source = ci_source_sha()
    if source:
        payload["source_sha"] = source
    path = sidecar_path(release)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(SIDECAR_NAME + ".tmp")
    tmp.write_text(dump(payload))
    tmp.replace(path)
    print(f"Wrote GUI build provenance: {mode}")
    return payload


def verify_payload(payload, binary, expected_mode, source_sha=None, require_source_sha=False):
    if payload["api_mode"] != expected_mode:
        if expected_mode == "production":
            fail(
                "release packaging requires production Telegram API provenance; "
                "use --allow-test-api only for validation packages"
            )
        fail("validation packaging requires test Telegram API provenance")
    if payload["tdesktop_api_test"] != MODE_CACHE[expected_mode]:
        fail("GUI build provenance tdesktop_api_test does not match the requested mode")
    if not binary.is_file():
        fail("missing stripped GUI binary")
    digest = sha256_file(binary)
    if digest != payload["gui_sha256"]:
        fail("GUI SHA256 does not match build provenance")
    recorded = payload.get("source_sha")
    if require_source_sha and not recorded:
        fail("GUI build provenance is missing the CI source commit")
    if source_sha:
        wanted = source_sha.strip().lower()
        if not SOURCE_SHA.fullmatch(wanted):
            fail("expected source commit is not a Git SHA")
        if recorded != wanted:
            fail("GUI build provenance source commit does not match the CI commit")
    return payload


def verify_stage_release(release_dir, allow_test_api=False):
    release = Path(release_dir)
    expected = "test" if allow_test_api else "production"
    payload = load_payload(sidecar_path(release))
    verify_payload(payload, gui_path(release), expected)
    token = cache_test_token(cmake_cache_value(release / "CMakeCache.txt", "TDESKTOP_API_TEST"))
    if token is None:
        fail("CMake cache TDESKTOP_API_TEST is missing or invalid")
    if token != payload["tdesktop_api_test"]:
        fail("CMake cache TDESKTOP_API_TEST does not match GUI build provenance")
    if token != MODE_CACHE[expected]:
        fail("CMake cache TDESKTOP_API_TEST does not match the requested packaging mode")
    if read_version(release / "bin/deltagram.version") != payload["version"]:
        fail("GUI version does not match build provenance")
    return payload


def verify_publish_assets(assets_dir, source_sha=None, require_source_sha=False):
    assets = Path(assets_dir)
    payload = load_payload(assets / SIDECAR_NAME)
    verify_payload(
        payload,
        assets / GUI_NAME,
        "production",
        source_sha=source_sha,
        require_source_sha=require_source_sha,
    )
    if read_version(assets / "deltagram.version") != payload["version"]:
        fail("GUI version does not match build provenance")
    return payload


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    write_cmd = sub.add_parser("write", help="Write sidecar after a successful GUI strip")
    write_cmd.add_argument("--release-dir", required=True)
    write_cmd.add_argument("--mode", required=True, choices=("test", "production"))

    stage_cmd = sub.add_parser("verify-stage", help="Verify live GUI, cache, and sidecar")
    stage_cmd.add_argument("--release-dir", required=True)
    stage_cmd.add_argument("--allow-test-api", action="store_true")

    publish_cmd = sub.add_parser("verify-publish", help="Verify uploaded production evidence")
    publish_cmd.add_argument("--assets-dir", required=True)
    publish_cmd.add_argument("--source-sha")
    publish_cmd.add_argument("--require-source-sha", action="store_true")

    args = parser.parse_args(argv)
    if args.command == "write":
        write_release(args.release_dir, args.mode)
    elif args.command == "verify-stage":
        verify_stage_release(args.release_dir, allow_test_api=args.allow_test_api)
    else:
        verify_publish_assets(
            args.assets_dir,
            source_sha=args.source_sha,
            require_source_sha=args.require_source_sha,
        )


if __name__ == "__main__":
    main()

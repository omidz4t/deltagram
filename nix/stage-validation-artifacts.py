#!/usr/bin/env python3
"""Stage unpublished validation GUI/Core artifacts from a completed native build."""

import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get("DELTA_TEL_DATA", ROOT / "data"))
RELEASE = Path(os.environ.get("RELEASE_DIR", DATA / "tdesktop-release"))
TARGET = Path(os.environ.get("CARGO_TARGET_DIR", DATA / "cargo-target"))
DEST = DATA / "validation-artifacts"

SHA = re.compile(r"^[0-9a-f]{40}(?:[0-9a-f]{24})?$")
EVENTS = {"pull_request", "push", "workflow_dispatch"}
SYMBOL_FLAG = {"1", "true", "yes"}


def env_text(name, limit=256):
    value = os.environ.get(name, "")
    return value if value and len(value) <= limit else ""


def env_sha(name):
    value = os.environ.get(name, "").strip().lower()
    return value if SHA.fullmatch(value) else None


def include_symbols():
    return (
        os.environ.get("VALIDATION_INCLUDE_SYMBOLS", "").strip().lower() in SYMBOL_FLAG
    )


def provenance():
    event = env_text("GITHUB_EVENT_NAME", 64)
    payload = {
        "checkout_sha": env_sha("GITHUB_SHA"),
        "event_name": event if event in EVENTS else "",
        "ref": env_text("GITHUB_REF"),
        "repository": env_text("GITHUB_REPOSITORY", 128),
        "run_attempt": env_text("GITHUB_RUN_ATTEMPT", 16),
        "run_id": env_text("GITHUB_RUN_ID", 32),
    }
    head = env_sha("VALIDATION_HEAD_SHA")
    base = env_sha("VALIDATION_BASE_SHA")
    if head:
        payload["head_sha"] = head
    if base:
        payload["base_sha"] = base
    number = os.environ.get("VALIDATION_PR_NUMBER", "").strip()
    if number.isdigit() and len(number) <= 16:
        payload["pr_number"] = int(number)
    return payload


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def checksums(names):
    lines = []
    for name in names:
        lines.append(f"{sha256(DEST / name)}  {name}")
    return "\n".join(lines) + "\n"


def stage():
    gui = RELEASE / "bin/deltagram.stripped"
    version = RELEASE / "bin/deltagram.version"
    telegram = RELEASE / "bin/Telegram"
    rpc = TARGET / "debug/deltachat-rpc-server"
    log = DATA / "delta-shell/Testing/Temporary/LastTest.log"
    required = [gui, version, rpc, log]
    if include_symbols():
        required.append(telegram)
    missing = [path for path in required if not path.is_file()]
    if missing:
        raise SystemExit("Missing validation input: " + ", ".join(map(str, missing)))
    if DEST.exists():
        shutil.rmtree(DEST)
    DEST.mkdir(parents=True)
    shutil.copy2(gui, DEST / "deltagram.stripped")
    shutil.copy2(version, DEST / "deltagram.version")
    subprocess.run(
        ["strip", "--strip-all", "-o", str(DEST / "deltachat-rpc-server"), str(rpc)],
        check=True,
    )
    shutil.copy2(log, DEST / "integrity-tests.log")
    names = ["deltagram.stripped", "deltachat-rpc-server"]
    if include_symbols():
        with (DEST / "deltagram.symbols").open("wb") as symbols:
            subprocess.run(
                ["nm", "--numeric-sort", "--demangle", str(telegram)],
                stdout=symbols,
                check=True,
            )
        names.append("deltagram.symbols")
    names.extend(["deltagram.version", "integrity-tests.log"])
    (DEST / "provenance.json").write_text(
        json.dumps(provenance(), indent=2, sort_keys=True) + "\n"
    )
    names.append("provenance.json")
    (DEST / "SHA256SUMS").write_text(checksums(names))


if __name__ == "__main__":
    stage()

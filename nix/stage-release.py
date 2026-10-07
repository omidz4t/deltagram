#!/usr/bin/env python3
"""Stage the current GUI/Core release and its Nix runtime for single-file packaging."""
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get("DELTA_TEL_DATA", ROOT / "data"))
RELEASE = Path(os.environ.get("RELEASE_DIR", DATA / "tdesktop-release"))
TARGET = Path(os.environ.get("CARGO_TARGET_DIR", DATA / "cargo-target"))
DEST = DATA / "release-stage"


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target, follow_symlinks=True)
    target.chmod(target.stat().st_mode | 0o200)


def dependencies(binary, destination):
    listing = output("ldd", str(binary))
    if "not found" in listing:
        raise RuntimeError(f"Unresolved runtime dependency of {binary}:\n{listing}")
    for library in re.findall(r"(?:=>\s+|^\s*)(/\S+)\s+\(", listing, re.M):
        path = Path(library)
        copy(path, destination / path.name)


def stage():
    gui = RELEASE / "bin/Telegram.stripped"
    rpc = TARGET / "debug/deltachat-rpc-server"
    for required in (gui, rpc, RELEASE / "bin/Telegram.version"):
        if not required.is_file():
            raise SystemExit(f"Missing release input: {required}. Build the release first.")
    # This directory is disposable packaging output, never an incremental tree.
    if DEST.exists():
        shutil.rmtree(DEST)
    lib = DEST / "telegram-lib"
    lib.mkdir(parents=True)
    copy(gui, DEST / "Telegram")
    copy(RELEASE / "bin/Telegram.version", DEST / "Telegram.version")
    dependencies(gui, lib)
    loader = Path(output("patchelf", "--print-interpreter", str(gui)))
    copy(loader, lib / "ld-linux-x86-64.so.2.real")
    copy(rpc, lib / "deltachat-rpc-server")
    rpc_lib = lib / "rpc-rt"
    rpc_lib.mkdir()
    dependencies(rpc, rpc_lib)
    copy(output("patchelf", "--print-interpreter", str(rpc)), rpc_lib / "ld-linux-x86-64.so.2")
    subprocess.run(["cc", "-shared", "-fPIC", "-Os", str(ROOT / "nix/spawnfix.c"),
                    "-ldl", "-o", str(lib / "libspawnfix.so")], check=True)
    dependencies(lib / "libspawnfix.so", lib)
    plugins = Path(output("pkg-config", "--variable=libdir", "Qt6Core")) / "qt-6/plugins"
    plugin_roots = [plugins] + [Path(p) for p in os.environ.get(
        "DELTA_TEL_EXTRA_QT_PLUGIN_ROOTS", "").split(":") if p]
    for plugin_root in plugin_roots:
        for category in ("platforms", "platforminputcontexts", "xcbglintegrations",
                         "imageformats", "iconengines", "styles"):
            for plugin in sorted((plugin_root / category).glob("*.so")):
                copy(plugin, DEST / "telegram-plugins" / category / plugin.name)
                dependencies(plugin, lib)
    for name in ("jpeg", "webp", "svg"):
        if not (DEST / f"telegram-plugins/imageformats/libq{name}.so").is_file():
            raise SystemExit(f"Required Qt image codec missing: {name}")
    pipewire = Path(output("pkg-config", "--variable=libdir", "libpipewire-0.3"))
    for source, name in ((pipewire / "spa-0.2", "spa-0.2"),
                         (pipewire / "pipewire-0.3", "pipewire-0.3"),
                         (pipewire.parent / "share/pipewire", "pipewire-conf")):
        if not source.is_dir():
            raise SystemExit(f"Missing PipeWire runtime: {source}")
        shutil.copytree(source, lib / name, symlinks=False)
        for plugin in (lib / name).rglob("*.so"):
            dependencies(plugin, lib)
    # Keep license notices alongside the embedded components.
    copy(ROOT / "tdesktop/LICENSE", DEST / "licenses/Telegram-LICENSE")
    copy(ROOT / "tdesktop/LEGAL", DEST / "licenses/Telegram-LEGAL")
    copy(ROOT / "context/core/LICENSE", DEST / "licenses/Core-LICENSE")
    for binary in DEST.rglob("*"):
        if not binary.is_file():
            continue
        with binary.open("rb") as stream:
            elf = stream.read(4) == b"\x7fELF"
        if not elf:
            continue
        subprocess.run(["strip", "--strip-unneeded", str(binary)], check=True)
        if binary.name.startswith("ld-linux"):
            continue
        for needed in output("patchelf", "--print-needed", str(binary)).splitlines():
            if needed.startswith("/"):
                subprocess.run(["patchelf", "--replace-needed", needed,
                                Path(needed).name, str(binary)], check=True)
        relative = os.path.relpath(rpc_lib if "rpc-rt" in binary.parts else lib, binary.parent)
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/" + relative, str(binary)], check=True)
    print(f"Staged current release in {DEST}")


if __name__ == "__main__":
    stage()

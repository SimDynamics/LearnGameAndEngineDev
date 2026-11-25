"""Compile the production ledger alone and exercise the real Windows filesystem.

Uses only already installed, hash-locked compiler jars. Every generated source,
class, log and cache stays inside a fresh C:\\LGAED-QA subtree. No runtime jar,
model, application or personal profile is changed or started.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WORKER = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qa-root", type=Path, default=Path(r"C:\LGAED-QA"))
    parser.add_argument("--read-only-cache", type=Path,
                        help="Only inspect an existing absolute cache; no filesystem fixtures are created there")
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("This regression must run against the real Windows filesystem")
    qa = args.qa_root.resolve()
    if not qa.is_relative_to(Path(r"C:\LGAED-QA").resolve()):
        raise RuntimeError("The ledger regression must stay inside C:\\LGAED-QA")
    qa.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="audio-storage-ledger-", dir=qa))
    runtime = json.loads((ROOT / "out/narration/runtime.json").read_text(encoding="utf-8"))
    java = Path(runtime.get("javaExecutable") or
                str(Path((ROOT / "out/narration/jdk-root.txt").read_text(encoding="utf-8").strip()) / "bin/java.exe"))
    if not java.is_absolute() or not java.is_file():
        raise RuntimeError("Build the existing project-selected JDK/runtime configuration first")
    lock = json.loads((WORKER / "dependencies.lock.json").read_text(encoding="utf-8"))
    archives = []
    for relative, expected in lock["sha256"].items():
        if not relative.endswith(".jar"):
            continue
        archive = ROOT / "out/deps/windows-maven" / relative
        if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Required installed compiler dependency is missing or changed: {archive}")
        archives.append(archive)
    standard = [path for path in archives if "/kotlin-stdlib/" in path.as_posix()]
    if len(standard) != 1:
        raise RuntimeError("The locked Kotlin standard library must be unambiguous")
    if args.read_only_cache is not None:
        if not args.read_only_cache.is_absolute() or not args.read_only_cache.is_dir():
            raise RuntimeError("The read-only cache must be an explicit, existing absolute directory")
        template, name = "test_audio_storage_probe.kt.in", "WindowsAudioStorageReadOnlyProbe"
        target_directory = args.read_only_cache
    else:
        template, name = "test_audio_storage_ledger.kt.in", "WindowsAudioStorageLedgerTest"
        target_directory = folder / "files"
    source = folder / (name + ".kt")
    source.write_text((WORKER / template).read_text(encoding="utf-8"), encoding="utf-8")
    jar = folder / "storage-ledger-tests.jar"
    # Read the declared compiler target; never write generated worker sources.
    import re
    target = re.search(r'^JVM_TARGET = "(\d+)"$', (WORKER / "build_worker.py").read_text(encoding="utf-8"), re.M)
    if target is None:
        raise RuntimeError("The worker compiler target changed; inspect its declaration")
    command = [str(java), "-cp", os.pathsep.join(map(str, archives)),
               "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
               "-Xjdk-release=" + target[1], "-classpath", str(standard[0]), "-d", str(jar),
               str(WORKER / "WindowsAudioStorageLedger.kt"), str(source)]
    def run(label, arguments, timeout):
        result = subprocess.run(arguments, cwd=folder, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW,
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        if result.stdout:
            print(result.stdout, end="", flush=True)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr, flush=True)
        (folder / (label + ".stdout.log")).write_text(result.stdout, encoding="utf-8")
        (folder / (label + ".stderr.log")).write_text(result.stderr, encoding="utf-8")
        result.check_returncode()
    run("compile", command, 120)
    run("probe" if args.read_only_cache else "filesystem",
        [str(java), "-cp", str(jar) + os.pathsep + str(standard[0]),
         "lgaed.narration.windows." + name + "Kt", str(target_directory)], 120 if args.read_only_cache else 40)
    print("QA_LOG_DIRECTORY=" + str(folder), flush=True)


if __name__ == "__main__":
    main()

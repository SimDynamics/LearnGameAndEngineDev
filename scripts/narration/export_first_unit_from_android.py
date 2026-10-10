#!/usr/bin/env python3
"""Verify every Kokoro sentence in course 1 / unit 1, export WAVs from a
connected debug APK with run-as, and package compressed Opus/Ogg Android
assets. No root or Clang installation required. An incomplete unit fails
before packaging, never silently ships a partial initial voice pack.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = "kokoro-first-unit-manifest.json"
PATTERN = re.compile(r"^[0-9a-f]{64}$")
LESSONS = {
    "cpp_fundamentals__intro_programming__what_is_programming",
    "cpp_fundamentals__intro_programming__first_contact_cpp",
    "cpp_fundamentals__intro_programming__program_structure",
}


def adb_read(adb: str, package: str, relative: str) -> bytes | None:
    result = subprocess.run(
        [adb, "exec-out", "run-as", package, "cat", relative],
        capture_output=True,
        check=False,
    )
    if result.returncode:
        return None
    return result.stdout


def valid_wav(path: pathlib.Path) -> bool:
    try:
        with wave.open(str(path), "rb") as source:
            return (
                source.getnchannels() == 1
                and source.getframerate() == 24000
                and source.getsampwidth() == 2
                and source.getnframes() > 0
            )
    except (OSError, EOFError, wave.Error):
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--package", default="com.learngraphicsprogramming.app")
    parser.add_argument("--workspace", type=pathlib.Path,
                        default=ROOT / "build" / "first-unit-speech-export")
    parser.add_argument("--output", type=pathlib.Path,
                        default=ROOT / "app/src/main/assets/narration/prerendered")
    args = parser.parse_args()

    raw = adb_read(args.adb, args.package, f"files/{MANIFEST}")
    if not raw:
        print(
            "No manifest found. Install/open the new debug APK, wait until "
            "it finishes initializing, and try again. 'run-as' requires a "
            "debuggable application.",
            file=sys.stderr,
        )
        return 2
    try:
        manifest = json.loads(raw)
        entries = manifest["entries"]
        ids = {entry["lesson"] for entry in entries}
        if (manifest.get("format") != 1 or ids != LESSONS or not entries or
                len({entry["hash"] for entry in entries}) != len(entries) or
                any(not PATTERN.fullmatch(entry["hash"]) for entry in entries)):
            raise ValueError("Invalid or incomplete three-lesson manifest")
    except (ValueError, KeyError, TypeError) as error:
        print(f"Invalid Android first-unit manifest: {error}", file=sys.stderr)
        return 2

    args.workspace.mkdir(parents=True, exist_ok=True)
    missing: list[str] = []
    extracted = 0
    for entry in entries:
        source = args.workspace / f"{entry['hash']}.wav"
        if source.is_file() and valid_wav(source):
            continue
        data = adb_read(
            args.adb, args.package,
            f"files/kokoro-narration-cache/{entry['hash']}.wav",
        )
        if not data:
            missing.append(f"{entry['lesson']}: {entry['hash'][:12]}")
            continue
        source.write_bytes(data)
        if not valid_wav(source):
            source.unlink(missing_ok=True)
            print(f"Invalid WAV on device: {entry['hash']}", file=sys.stderr)
            return 3
        extracted += 1

    if missing:
        print(
            f"First-unit audio is NOT complete: {len(missing)} of "
            f"{len(entries)} unique sentences still need Kokoro inference. "
            "Let the automatic queue finish on the phone and run this "
            "script again. Existing WAV exports were preserved.",
            file=sys.stderr,
        )
        for name in missing[:15]:
            print(f"  missing {name}", file=sys.stderr)
        return 4

    command = [
        sys.executable,
        str(ROOT / "scripts/narration/package_prerendered_opus.py"),
        str(args.workspace),
        "--output", str(args.output),
    ]
    status = subprocess.run(command, check=False)
    if status.returncode:
        return status.returncode
    for entry in entries:
        ogg = args.output / f"{entry['hash']}.ogg"
        if not ogg.is_file():
            raise RuntimeError(f"Packaged Ogg missing: {ogg}")
        header = ogg.open("rb").read(128)
        if not header.startswith(b"OggS") or b"OpusHead" not in header:
            raise RuntimeError(f"Invalid compressed Opus audio: {ogg}")
    print(
        f"Verified all {len(entries)} unique sentences across all "
        f"three first-unit lessons; exported {extracted} new WAVs."
    )
    print(f"Assets ready for APK bundling: {args.output}")
    print("NEXT: build the APK and verify the included Ogg files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Verify every Kokoro sentence in course 1 / unit 1, export WAVs from a
connected debug APK with run-as, and package compressed Opus/Ogg Android
assets. No root or Clang installation required. An incomplete unit fails
before packaging, never silently ships a partial initial voice pack.
"""
from __future__ import annotations

import argparse
import io
import json
import pathlib
import re
import subprocess
import sys
import uuid
import wave

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = "kokoro-first-unit-manifest.json"
PATTERN = re.compile(r"^[0-9a-f]{64}$")
LESSONS = {
    "cpp_fundamentals__intro_programming__what_is_programming",
    "cpp_fundamentals__intro_programming__first_contact_cpp",
    "cpp_fundamentals__intro_programming__program_structure",
}


def adb_read(adb: str, package: str, relative: str) -> tuple[bytes, int, str]:
    """Read app-private bytes without terminal newline conversion.

    Capturing stderr and the real exit status distinguishes absent files,
    run-as errors and incomplete ADB transfers from corrupt audio.
    """
    try:
        result = subprocess.run(
            [adb, "exec-out", "run-as", package, "cat", relative],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as error:
        raise RuntimeError(f"Cannot launch ADB: {error}") from error
    return result.stdout, result.returncode, result.stderr.decode("utf-8", errors="replace").strip()


def missing_file_reply(data: bytes) -> bool:
    """Android's run-as/exec-out can return 'cat: ... No such file...' on
    stdout with exit code ZERO even though no source file exists.
    That means 'not synthesized yet', not a damaged WAV.
    """
    if not data.startswith(b"cat: "):
        return False
    low = data.lower()
    return b"no such file or directory" in low or b"not found" in low


def inspect_wav(data: bytes) -> str | None:
    """Match the exact RIFF/PCM16/24k mono format used by Android's cache."""
    if len(data) < 44:
        return f"Only {len(data)} bytes received; expected at least 44"
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        excerpt = repr(data[:72])
        return f"Not a RIFF/WAVE stream; first bytes: {excerpt}"
    riff_length = int.from_bytes(data[4:8], "little") + 8
    if riff_length != len(data):
        return f"Truncated/inconsistent RIFF: expected {riff_length} bytes; got {len(data)}"
    if data[12:16] != b"fmt " or data[36:40] != b"data":
        return f"Unexpected WAV chunk layout (first 44 bytes: {data[:44].hex()})"
    audio_length = int.from_bytes(data[40:44], "little")
    if audio_length <= 0 or 44 + audio_length != len(data):
        return f"PCM data length {audio_length}; transferred {len(data)} bytes"
    try:
        with wave.open(io.BytesIO(data), "rb") as source:
            if (source.getnchannels(), source.getframerate(), source.getsampwidth()) != (1, 24000, 2):
                return (
                    "Unexpected audio format: channels="
                    f"{source.getnchannels()}, rate={source.getframerate()}, "
                    f"sample_width={source.getsampwidth()}"
                )
            if source.getnframes() == 0:
                return "No PCM frames"
    except (OSError, EOFError, wave.Error) as error:
        return f"Python WAV decoder failed: {error}"
    return None


def valid_wav(path: pathlib.Path) -> bool:
    try:
        return inspect_wav(path.read_bytes()) is None
    except OSError:
        return False


def write_verified_wav(path: pathlib.Path, data: bytes) -> bool:
    """Publish verified bytes without discarding any existing local export.

    An earlier failed ADB transfer may have left shell text under a WAV name.
    Preserve that original for diagnosis, and only rename it after a complete
    valid replacement is available. A valid recording is always reused.
    """
    reason = inspect_wav(data)
    if reason is not None:
        raise ValueError(reason)
    if valid_wav(path):
        return False
    staging = path.with_name(f".{path.name}-{uuid.uuid4().hex}.part")
    try:
        with staging.open("xb") as output:
            output.write(data)
        if path.exists():
            backup = path.with_name(f"{path.stem}.unverified-{uuid.uuid4().hex}.wav")
            path.rename(backup)
        staging.rename(path)
        return True
    finally:
        # Only a partial file created by this invocation; recordings stay intact.
        staging.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--package", default="com.learngraphicsprogramming.app")
    parser.add_argument("--workspace", type=pathlib.Path,
                        default=ROOT / "build" / "first-unit-speech-export")
    parser.add_argument("--output", type=pathlib.Path,
                        default=ROOT / "app/src/main/assets/narration/prerendered")
    args = parser.parse_args()

    try:
        raw, manifest_status, manifest_error = adb_read(
            args.adb, args.package, f"files/{MANIFEST}"
        )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 2
    if manifest_status != 0 or not raw:
        print(
            f"Cannot read the first-unit manifest (ADB exit {manifest_status}): "
            f"{manifest_error or 'no data returned'}. "
            "Open the installed debug app first; run-as requires a debug build.",
            file=sys.stderr,
        )
        return 2
    try:
        manifest = json.loads(raw)
        entries = manifest["entries"]
        # Deduplication is intentional: two lessons can say exactly the same
        # sentence with the same voice. It is then exported ONCE and reused.
        # Verify all lesson IDs from the manifest, not only the first owner
        # attached to each unique audio hash.
        lessons = manifest["lessons"]
        if (manifest.get("format") != 1 or set(lessons) != LESSONS or
                len(lessons) != len(LESSONS) or not entries or
                manifest.get("uniqueSentences") != len(entries) or
                len({entry["hash"] for entry in entries}) != len(entries) or
                any(entry["lesson"] not in LESSONS or
                    not PATTERN.fullmatch(entry["hash"]) for entry in entries)):
            raise ValueError("Invalid or incomplete three-lesson manifest")
    except (ValueError, KeyError, TypeError) as error:
        print(f"Invalid Android first-unit manifest: {error}", file=sys.stderr)
        return 2

    args.workspace.mkdir(parents=True, exist_ok=True)
    missing: list[str] = []
    invalid: list[str] = []
    extracted = 0
    print(
        f"First-unit manifest: {len(entries)} unique sentences, "
        f"voice(s): {', '.join(sorted({e['voice'] for e in entries}))}",
        flush=True,
    )
    for entry in entries:
        source = args.workspace / f"{entry['hash']}.wav"
        if source.is_file() and valid_wav(source):
            continue
        data, status, stderr = adb_read(
            args.adb, args.package,
            f"files/kokoro-narration-cache/{entry['hash']}.wav",
        )
        label = f"{entry['lesson']}: {entry['hash'][:12]}"
        if missing_file_reply(data):
            missing.append(f"{label} (not synthesized yet)")
            continue
        if status != 0 or not data:
            missing.append(f"{label} (ADB {status}: {stderr or 'file absent or empty'})")
            continue
        reason = inspect_wav(data)
        if reason is not None:
            invalid.append(f"{label} ({len(data)} bytes, ADB {status}: {reason})")
            continue
        if write_verified_wav(source, data):
            extracted += 1

    if invalid:
        print(
            f"Cannot verify {len(invalid)} exported WAV(s); {extracted} valid "
            "new WAVs were preserved on the PC. Nothing was changed on Android.",
            file=sys.stderr,
        )
        for label in invalid[:10]:
            print(f"  INVALID {label}", file=sys.stderr)
        if missing:
            print(f"Also missing: {len(missing)} WAV(s)", file=sys.stderr)
        return 3

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

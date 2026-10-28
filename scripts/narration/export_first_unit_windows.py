#!/usr/bin/env python3
"""Export the three historical Spanish/Dora lesson keys from the Windows worker.

Planning uses the shared Kotlin extractor/segmenter without models, synthesis,
learner preferences or the live catalogue. Optional coverage verification reads
existing recordings only; missing audio never becomes a complete voice pack.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import uuid

from export_first_unit_from_android import LESSONS, inspect_wav

ROOT = Path(__file__).resolve().parents[2]
HASH = re.compile(r"[0-9a-f]{64}")


def validate_manifest(manifest: dict) -> list[dict]:
    """Reject a drifting language, voice, lesson set or recording identity."""
    try:
        if not isinstance(manifest, dict):
            raise ValueError("First-unit manifest must be an object")
        lessons = manifest["lessons"]
        entries = manifest["entries"]
        if (manifest.get("format") != 1 or manifest.get("course") != "cpp_fundamentals"
                or manifest.get("unit") != "intro_programming"
                or not isinstance(lessons, list) or len(lessons) != 3 or set(lessons) != LESSONS
                or not isinstance(entries, list) or not entries
                or manifest.get("uniqueSentences") != len(entries)):
            raise ValueError("Invalid three-lesson manifest")
        seen: set[str] = set()
        for entry in entries:
            if not isinstance(entry, dict):
                raise ValueError("Recording entry must be an object")
            key = entry["hash"]
            lesson = entry["lesson"]
            prefix = f"programming/cpp_fundamentals/cpp_fundamentals__intro_programming/{lesson}/"
            if (not isinstance(key, str) or not HASH.fullmatch(key) or key in seen
                    or lesson not in LESSONS or entry["language"] != "es" or entry["voice"] != "ef_dora"
                    or not isinstance(entry["location"], str) or not entry["location"].startswith(prefix)
                    or not re.fullmatch(r"[1-9][0-9]*", entry["location"][len(prefix):])):
                raise ValueError("Invalid Spanish/Dora recording entry")
            seen.add(key)
        return entries
    except (KeyError, TypeError) as error:
        raise ValueError("Invalid first-unit manifest structure") from error


def read_manifest(runtime_path: Path) -> dict:
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    # A one-shot read-only request. No prepare command or output cache is passed.
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as errors:
        process = subprocess.Popen([runtime["java"], *runtime["arguments"]],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors,
            text=True, encoding="utf-8",
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            responses: queue.Queue[str] = queue.Queue()
            reader = threading.Thread(target=lambda: responses.put(process.stdout.readline()), daemon=True)
            reader.start()
            process.stdin.write(json.dumps({"id": "manifest", "command": "first_unit_manifest"}) + "\n")
            process.stdin.flush()
            try:
                line = responses.get(timeout=90)
            except queue.Empty as error:
                raise ValueError("Windows manifest response timed out") from error
            if not line:
                errors.seek(0)
                raise ValueError(f"Windows worker closed before manifest: {errors.read()[-2000:]}")
            reply = json.loads(line)
            if (not isinstance(reply, dict) or reply.get("id") != "manifest" or reply.get("command") != "first_unit_manifest"
                    or "error" in reply):
                raise ValueError(f"Windows manifest failed: {reply}")
            manifest = reply["manifest"]
            validate_manifest(manifest)
            # shutdown and EOF cancel active jobs. Send them only after the
            # manifest has completed, rather than racing the worker executor.
            process.stdin.write(json.dumps({"id": "shutdown", "command": "shutdown"}) + "\n")
            process.stdin.flush()
            process.communicate(timeout=30)
            if process.returncode:
                raise ValueError(f"Windows worker exited with code {process.returncode}")
            return manifest
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=10)
            for stream in (process.stdin, process.stdout):
                stream.close()


def missing_recordings(manifest: dict, folders: list[Path], *, ffmpeg: str | None = None) -> list[str]:
    """Validate existing audio without writing or regenerating recordings."""
    decoder = ffmpeg or shutil.which("ffmpeg")
    missing = []
    for entry in validate_manifest(manifest):
        key = entry["hash"]
        present = False
        for folder in folders:
            wav = folder / f"{key}.wav"
            if wav.is_file() and inspect_wav(wav.read_bytes()) is None:
                present = True
                break
            ogg = folder / f"{key}.ogg"
            if ogg.is_file():
                with ogg.open("rb") as stream:
                    header = stream.read(128)
                if header.startswith(b"OggS") and b"OpusHead" in header and decoder:
                    # A header alone also matches truncated files. Decode to a
                    # null sink before calling an external recording complete.
                    try:
                        checked = subprocess.run([decoder, "-nostdin", "-v", "error", "-xerror",
                            "-i", str(ogg), "-map", "0:a:0", "-f", "null", "-"],
                            capture_output=True, timeout=30, check=False,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                        present = checked.returncode == 0
                    except (OSError, subprocess.SubprocessError):
                        present = False
                    if present:
                        break
        if not present:
            missing.append(key)
    return missing


def write_manifest(path: Path, manifest: dict) -> None:
    validate_manifest(manifest)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}-{uuid.uuid4().hex}.part")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(manifest, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=ROOT / "out/narration/runtime.json")
    parser.add_argument("--output", type=Path,
        default=ROOT / "out/narration/first-unit/kokoro-first-unit-manifest.json")
    parser.add_argument("--verify-recordings", action="store_true")
    parser.add_argument("--recordings", type=Path, action="append",
        help="Additional existing recording folder (repeatable, read only)")
    args = parser.parse_args()
    try:
        manifest = read_manifest(args.runtime)
        write_manifest(args.output, manifest)
        print(f"Exported {manifest['uniqueSentences']} unique Spanish/ef_dora keys across three lessons: {args.output}")
        if args.verify_recordings:
            folders = [ROOT / "app/src/main/assets/narration/prerendered", *(args.recordings or [])]
            missing = missing_recordings(manifest, folders)
            if missing:
                print(f"Audio pack incomplete: {len(missing)} of {manifest['uniqueSentences']} recordings missing or invalid; existing audio preserved.", file=sys.stderr)
                return 4
            print("All manifest recordings are present. No synthesis or audio replacement performed.")
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Cannot export Windows first-unit manifest: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

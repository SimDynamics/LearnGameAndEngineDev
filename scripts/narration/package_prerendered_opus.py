#!/usr/bin/env python3
"""
Package already-synthesized Kokoro narration for inclusion in an Android APK.

Input: a folder of <64-hex-hash>.wav files exported from the app's private
'kokoro-narration-cache' directory. These WAVs were synthesized using the
EXACT Kotlin G2P, model revision, text and voice settings expected by the app.

Output: app/src/main/assets/narration/prerendered/<same-hash>.ogg

Requires ffmpeg with libopus on the development computer.
It does NOT download or rebuild Clang, Kokoro weights or the Android app.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


NAME = re.compile(r"^[0-9a-f]{64}\.wav$")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path, help="Directory of cached Kokoro WAVs")
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("app/src/main/assets/narration/prerendered"),
    )
    parser.add_argument("--bitrate", default="40k")
    args = parser.parse_args()

    if not args.source.is_dir():
        parser.error(f"Source directory not found: {args.source}")
    files = sorted(file for file in args.source.iterdir() if file.is_file() and NAME.fullmatch(file.name))
    if not files:
        parser.error("No hashed Kokoro WAVs found; generate the narration first")
    args.output.mkdir(parents=True, exist_ok=True)
    done = 0
    for source in files:
        destination = args.output / f"{source.stem}.ogg"
        if destination.is_file():
            continue
        temporary = args.output / f"{source.stem}.partial.ogg"
        cmd = [
            "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
            "-y", "-i", str(source), "-map", "0:a:0", "-vn",
            "-c:a", "libopus", "-b:a", args.bitrate,
            "-ac", "1", "-ar", "48000", str(temporary),
        ]
        try:
            subprocess.run(cmd, check=True)
            if temporary.stat().st_size < 64:
                raise ValueError(f"Empty Opus audio: {source}")
            temporary.replace(destination)
            done += 1
        except (OSError, subprocess.CalledProcessError, ValueError) as error:
            temporary.unlink(missing_ok=True)
            print(f"Error packaging {source.name}: {error}", file=sys.stderr)
            return 1
    print(f"Packaged {done} new Opus files; {len(files) - done} already existed")
    print(f"Included on next Android build from: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Deterministic protocol fixture for the native reader revision-order test.

The native LessonView, LessonReader, persistence and Windows MediaPlayer remain
real. This transport returns one segment per source region and a 640 ms PCM tone
so the test can observe every segment transition without models or a network.
Its output is sequencing evidence only, never neural voice or speech evidence.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import wave


DURATION_MS = 640
SAMPLE_RATE = 24000


def original_voice_catalog(source: str) -> list[dict]:
    # The same authoritative extraction as build_worker.py. No invented voice
    # names, hashes or downloadable resources are introduced by this fixture.
    languages = {"SPANISH": "es", "FRENCH": "fr", "AMERICAN_ENGLISH": "en-US", "BRITISH_ENGLISH": "en-GB"}
    voices = [{"id": name, "language": languages[language], "sha256": sha}
              for name, language, sha in re.findall(
                  r'KokoroVoice\("([a-z_]+)",\s*NarrationLanguage\.([A-Z_]+),\s*"([0-9a-f]{64})"\)', source)
              if language in languages]
    if len(voices) != 32 or len({voice["id"] for voice in voices}) != 32:
        raise ValueError("The original Windows voice catalogue changed; review its explicit language coverage")
    return voices


def prepare_catalog(source_root: Path) -> None:
    # LessonReader resolves preference metadata at its ordinary source-root
    # location even when given a controlled transport. Only the isolated
    # hosted QA checkout may receive this metadata-only, non-launchable file.
    if os.environ.get("GITHUB_ACTIONS") != "true" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise RuntimeError("Metadata-only reader catalogue preparation is restricted to isolated hosted QA")
    source_root = source_root.resolve(strict=True)
    authority = source_root / "app/src/main/java/com/learngraphicsprogramming/app/narration/KokoroVoiceCatalog.kt"
    voices = original_voice_catalog(authority.read_text(encoding="utf-8-sig"))
    runtime = source_root / "out/narration/runtime.json"
    if runtime.exists():
        if json.loads(runtime.read_text(encoding="utf-8-sig")).get("voices") != voices:
            raise ValueError("Existing runtime metadata differs from the original voice catalogue; it was not overwritten")
        print("Reusing existing authentic voice metadata; runtime bytes unchanged")
        return
    runtime.parent.mkdir(parents=True, exist_ok=True)
    with runtime.open("x", encoding="utf-8") as output:
        json.dump({"fixtureOnly": "reader sequencing metadata; no synthesis runtime or voice weights", "voices": voices},
                  output, ensure_ascii=False, indent=2)
        output.write("\n")
    print("Prepared original voice catalogue metadata only; no models, synthesis runtime or speech evidence")


def digest(value: object) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def run(log_path: Path, cache_path: Path) -> None:
    cache_path.mkdir(exist_ok=True)
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    # A run owns a new directory; reusing a transport log is an error.
    with log_path.open("x", encoding="utf-8", buffering=1) as log:
        for line in sys.stdin:
            request = json.loads(line)
            command = request["command"]
            log.write(json.dumps(request, ensure_ascii=False) + "\n")
            fields: dict = {}
            if command == "plan":
                text = request["text"]
                fields["segments"] = [] if not text else [{
                    "text": text, "preparedText": text, "spokenText": text,
                    "sourceStart": 0, "sourceEnd": len(text.encode("utf-16-le")) // 2,
                }]
            elif command == "identity":
                fields["key"] = digest({key: value for key, value in request.items()
                                        if key not in {"id", "command"}})
            elif command == "prepare":
                text = request["text"]
                key = digest({name: request[name] for name in
                              ("text", "preparedText", "language", "voice")})
                wav = cache_path / (key + ".wav")
                cache_hit = wav.is_file()
                if not cache_hit:
                    # Exclusive creation: existing cached evidence is never
                    # rewritten, even when the same lesson is reopened.
                    with wav.open("xb") as raw, wave.open(raw, "wb") as output:
                        output.setparams((1, 2, SAMPLE_RATE, 0, "NONE", "not compressed"))
                        output.writeframes(b"".join(
                            struct.pack("<h", int(3000 * math.sin(index * math.tau * 220 / SAMPLE_RATE)))
                            for index in range(SAMPLE_RATE * DURATION_MS // 1000)))
                fields.update(
                    key=key, sourceText=text, spokenText=text,
                    provider="deterministic-reader-revision-fixture", wav=str(wav),
                    durationMs=DURATION_MS, cacheHit=cache_hit,
                    timings=[{"start": 0, "end": len(text.encode("utf-16-le")) // 2,
                              "startMs": 0, "endMs": DURATION_MS}],
                )
                log.write(json.dumps({"command": "fixture_prepared", "targetId": request["id"],
                                      "key": key, "cacheHit": cache_hit}) + "\n")
            elif command == "curriculum_plan":
                fields["segments"] = []
            elif command == "cancel":
                # Requests complete synchronously. There is no held inference
                # operation to cancel or acknowledge in this fixture.
                continue
            elif command == "shutdown":
                break
            else:
                raise RuntimeError(f"Unexpected reader fixture command: {command}")
            print(json.dumps({"id": request["id"], "command": command, **fields}), flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: lesson_reader_edit_order_fixture.py LOG_PATH CACHE_PATH | --prepare-catalog SOURCE_ROOT")
    if sys.argv[1] == "--prepare-catalog":
        prepare_catalog(Path(sys.argv[2]))
    else:
        run(Path(sys.argv[1]), Path(sys.argv[2]))

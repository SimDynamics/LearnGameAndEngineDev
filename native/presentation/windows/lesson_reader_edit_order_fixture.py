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
import time
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


def recording_key(request: dict, text: str) -> str:
    # A controlled identity, not the Kokoro pronunciation/cache recipe. Both
    # planning paths and preparation must describe the same fixture recording.
    return digest(dict(text=text, preparedText=text,
                       language=request["language"], voice=request.get("voice", "ef_dora")))


def curriculum_plan(request: dict) -> dict:
    lesson = json.loads(request["lessonJson"])
    index = request["page"]
    page = lesson["pages"][index]
    owners = lesson.get("stateLayout", {}).get("current", [])
    owner = owners[index] if index < len(owners) else {}
    stable = owner.get("id") or page.get("id") or str(index)
    language = request.get("contentLanguage", request["language"])
    texts = [lesson["title"]] if index == 0 else []
    if lesson["id"].endswith("__disenar_probar_y_presentar_variante_propia"):
        # Only a controlled transport oracle for the authentic large document.
        # The actual Kokoro worker separately plans every one of its 18 pages.
        if len(lesson["pages"]) != 18:
            raise ValueError("The historical AssetCheck publication changed")
        texts = [next((block["text"] for block in page["blocks"]
                       if block["type"] == "paragraph"), lesson["title"])]
    else:
        for block in page["blocks"]:
            if block["type"] == "paragraph":
                texts.append(block["text"])
            elif block["type"] == "fill_code":
                texts.extend((block["question"], "Ejercicio de código C más más."))
                if block.get("explanation"):
                    texts.append("✓ Correcto\n\n" + block["explanation"])
            else:
                raise ValueError("Review the controlled reader fixture for this new block type")
    # Bind the strict native page session to the current authored source and
    # selection. The production revision/hash algorithm remains independently
    # exercised by the real-worker acceptance tests.
    proof = dict(page=page, owner=owner, contentLanguage=language,
                 language=request["language"], voice=request["voice"],
                 uiLanguage=request["uiLanguage"], readingContext=request.get("readingContext", "auto"))
    if index == 0:
        proof["title"] = lesson["title"]
    return dict(pageId=f"{lesson['id']}/{language}/{stable}", revision=digest(proof),
                segments=[dict(text=text, preparedText=text, spokenText=text,
                               key=recording_key(request, text)) for text in texts if text])


def run(log_path: Path, cache_path: Path) -> None:
    cache_path.mkdir(exist_ok=True)
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    # The C++ harness exclusively creates the runtime directory. A replacement
    # generation must append evidence there and reuse its existing PCM.
    directory = log_path.parent
    with log_path.open("a", encoding="utf-8", buffering=1) as log:
        pid = os.getpid()
        def event(command, **fields):
            log.write(json.dumps(dict(command=command, processId=pid, **fields),
                                 ensure_ascii=False) + "\n")
        event("fixture_started")
        for line in sys.stdin:
            request = json.loads(line)
            command = request["command"]
            large = (command == "curriculum_plan" and
                     json.loads(request["lessonJson"])["id"].endswith(
                         "__disenar_probar_y_presentar_variante_propia"))
            if large:
                raw = request["lessonJson"]
                event("fixture_large_request", targetId=request["id"],
                      sha256=hashlib.sha256(raw.encode("utf-8")).hexdigest(),
                      page=request["page"])
                fault = directory / "fault-large"
                if fault.is_file():
                    fault.unlink()
                    (directory / "large-entered").write_text(str(pid), encoding="ascii")
                    deadline = time.monotonic() + 20
                    while not (directory / "release-large").is_file():
                        if time.monotonic() >= deadline:
                            raise RuntimeError("The reader did not release the observed large request")
                        time.sleep(0.01)
                    event("fixture_exit", kind="background", targetId=request["id"])
                    sys.exit(23)
            else:
                log.write(json.dumps(request, ensure_ascii=False) + "\n")
            fault = directory / "fault-feedback"
            if (command == "plan" and request.get("text") == "✓ Correcto\n\nA_FEEDBACK"
                    and fault.is_file()):
                fault.unlink()
                event("fixture_exit", kind="feedback", targetId=request["id"])
                sys.exit(24)
            fields: dict = {}
            if command == "plan":
                text = request["text"]
                fields["segments"] = [] if not text else [{
                    "text": text, "preparedText": text, "spokenText": text,
                    "sourceStart": 0, "sourceEnd": len(text.encode("utf-16-le")) // 2,
                    "key": recording_key(request, text),
                }]
            elif command == "identity":
                fields["key"] = digest({key: value for key, value in request.items()
                                        if key not in {"id", "command"}})
            elif command == "prepare":
                text = request["text"]
                base_key = recording_key(request, text)
                namespace = request.get("cacheNamespace", "")
                if namespace and not re.fullmatch(r"[0-9a-f]{64}", namespace):
                    raise ValueError("Invalid controlled page cache namespace")
                key = digest(dict(recording=base_key, namespace=namespace)) if namespace else base_key
                output_directory = Path(request["outputDirectory"])
                if not output_directory.is_absolute():
                    raise ValueError("Controlled playback needs an absolute owned cache")
                output_directory.mkdir(parents=True, exist_ok=True)
                wav = output_directory / (key + ".wav")
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
                    durationMs=DURATION_MS, cacheHit=cache_hit, cacheNamespace=namespace,
                    timings=[{"start": 0, "end": len(text.encode("utf-16-le")) // 2,
                              "startMs": 0, "endMs": DURATION_MS}],
                )
                log.write(json.dumps({"command": "fixture_prepared", "targetId": request["id"],
                                      "key": key, "cacheHit": cache_hit}) + "\n")
            elif command == "curriculum_plan":
                fields = curriculum_plan(request)
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

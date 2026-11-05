"""Authenticated U11L22 one-space editorial proposal; never edits live assets.

The complete published ES/EN/FR documents come only from immutable local Git
bytes. The new files are QA inputs under out/build, not curricular integration.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "63962a88e6a6bd425b688971a6789d873a4ac316"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
LESSON = "texto_interpretacion_y_flujos/lesson_construir_una_sesion_con_nombres_y_estado.json"
IDENTITY = "block_0001_0004"
HISTORICAL = (1, 4)
ACTIVITY_ID = "cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado__e01"
BEFORE = "La creación completa guardó cero. El literal100 se rechaza y su candidato no sustituye ese estado."
AFTER = "La creación completa guardó cero. El literal 100 se rechaza y su candidato no sustituye ese estado."
TEXT_HASHES = ("e2103b09c4b3c8793d2d70fca2451232cc0aa61a7825effc26e48715aa0beb51",
               "1219b910c0c8284002699f2dda3e8e79ee650460373d0236b894cdba87814b8f")
SOURCE_HASHES = {
    "es": "35c2ec10d50202f99fd957d77410f2fa987575f6b97c995d5354c1ed8be53d3f",
    "en": "81fed9759a9ae4b27492606ebb6f5817c28d02d65b958d8d0692166fcc4a71f6",
    "fr": "ef937cf84651d974e8b01a8dc93c52a83b7e49bc433bd6e834a9dd3f2ea7165b",
}
BLOCK_HASHES = ("7246b74a0c6b46f2c14b26ea813cfc85a0d691348aea8ada18c279d46ce19156",
                "09a016cfe0637a8be64ccd67e0ec8f3bd57e8d63ca5e03c39ecbe760d7bacad1")
AFTER_SHA256 = "a44d7467cdd2f4aa0aa12b6f428ba6b595b20a9019c8d5976ea6c7be69c96f49"


def verify_bytes(locale: str, payload: bytes) -> bytes:
    if locale not in SOURCE_HASHES or hashlib.sha256(payload).hexdigest() != SOURCE_HASHES[locale]:
        raise ValueError("Session editorial fixture differs from its independently pinned Git bytes")
    return payload


def historical_bytes(locale: str = "es") -> bytes:
    relative = PREFIX.format(locale=locale) + LESSON
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def candidate() -> tuple[dict, dict, dict[str, dict]]:
    family = {locale: json.loads(historical_bytes(locale)) for locale in ("es", "en", "fr")}
    before = family["es"]
    original = layout._blocks(before)[IDENTITY]
    if (layout.validate(before).get(HISTORICAL) != HISTORICAL
            or before["stateLayout"]["current"][1]["blocks"][4] != IDENTITY
            or original.get("type") != "multiple_choice" or original.get("id") != ACTIVITY_ID
            or original.get("correctIndex") != 1 or original.get("explanation") != BEFORE
            or layout.fingerprint(original) != BLOCK_HASHES[0]
            or tuple(hashlib.sha256(text.encode("utf-8")).hexdigest() for text in (BEFORE, AFTER)) != TEXT_HASHES):
        raise ValueError("Session editorial fixture changed its exact published owner or text pair")
    draft = deepcopy(before)
    layout._blocks(draft)[IDENTITY]["explanation"] = AFTER
    after = layout.prepare_v2(draft, "es", family, before)
    layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])
    if (hashlib.sha256(json_bytes(after)).hexdigest() != AFTER_SHA256
            or layout.fingerprint(layout._blocks(after)[IDENTITY]) != BLOCK_HASHES[1]):
        raise ValueError("Session editorial fixture differs from its pinned complete candidate")
    return before, after, family


def export(output: Path) -> Path:
    """Authenticate the whole pair before writing only new QA files in out/build."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("Session editorial QA output cannot follow links or reparse points")
    output = output.resolve()
    if not any(output.is_relative_to(ROOT / name) for name in ("out", "build")):
        raise ValueError("Session editorial QA output must stay inside this project's out/build")
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("Session editorial QA output cannot write another checkout")
    before, after, _ = candidate()
    payloads = {"session-before.json": historical_bytes(), "session-after.json": json_bytes(after)}
    manifest = {
        "commit": COMMIT, "coordinateBase": 0, "relative": PREFIX.format(locale="es") + LESSON,
        "stableId": IDENTITY, "historical": list(HISTORICAL), "type": "multiple_choice",
        "activityId": ACTIVITY_ID, "field": "explanation", "sourceSha256": SOURCE_HASHES,
        "textSha256": list(TEXT_HASHES),
        "before": {"file": "session-before.json", "sha256": SOURCE_HASHES["es"], "blockSha256": BLOCK_HASHES[0]},
        "after": {"file": "session-after.json", "sha256": AFTER_SHA256, "blockSha256": BLOCK_HASHES[1]},
    }
    payloads["manifest.json"] = json_bytes(manifest)
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing session editorial QA document")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        target = output / name
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    export(arguments.output_dir)
    print(f"Verified the exact U11L22 spacing proposal from {COMMIT}.")

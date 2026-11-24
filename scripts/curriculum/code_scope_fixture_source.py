"""Export the exact pre-overlay code-scope lesson from its authenticated Git publication.

The native reader creates its own real answer and bookmark using these bytes.
No state, receipt, intermediate lesson or historical narration is synthesized.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "e338c6de2ac3479b44d67a95a57cb8bf734312d5"
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json")
FILENAME = "lesson_alternativas_y_fronteras.json"
BLOB = "e17824cda99abb7b863fc08ad60eb0d172e2b03b"
SHA256 = "5fc581a2a302af14c2ba2c03790e16891ab4a98c0e2cc3ff6974cff0a9642f9a"
OWNER = "u3l4_alcance_codigo_en_recuadros"


def verify_payload(payload: bytes) -> dict:
    frame = b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload
    if hashlib.sha1(frame).hexdigest() != BLOB or hashlib.sha256(payload).hexdigest() != SHA256:
        raise ValueError("The code-scope baseline differs from the authenticated Git publication")
    document = json.loads(payload)
    block = document["pages"][1]["blocks"][13]
    addition = next(entry for entry in document["stateLayout"]["additions"] if entry["id"] == OWNER)
    if (document.get("codeScopePresentations") is not None
            or block["type"] != "math_puzzle" or block["id"] != OWNER
            or document["stateLayout"]["current"][1]["blocks"][13] != OWNER
            or addition["storage"] != {"page": 7, "block": 30}
            or block["representations"][0]["type"] != "euclidean_diagram"
            or block["responses"][0]["id"] != "posicion"
            or block["responses"][0]["validator"]["expected"] != ["antes_de_return"]):
        raise ValueError("The pinned code-scope baseline lost its original presentation, owner or answer contract")
    return document


def historical_bytes() -> bytes:
    reference = f"{COMMIT}:{RELATIVE}"
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"Pinned local code-scope baseline unavailable: {reference}. "
                           "Complete local Git history is required; no fetch or live-asset fallback is allowed.") from error
    verify_payload(payload)
    return payload


def export(directory: Path) -> None:
    # Verify the entire immutable publication before creating any output.
    payload = historical_bytes()
    document = json.loads(payload)
    manifest = {
        "commit": COMMIT, "path": RELATIVE, "blob": BLOB, "sha256": SHA256,
        "file": FILENAME, "bytes": len(payload), "lessonId": document["id"],
        "owner": OWNER, "display": {"page": 1, "block": 13}, "storage": {"page": 7, "block": 30},
        "scope": "Exact published lesson bytes; the Windows test creates the answer and playback bookmark through real controls",
    }
    outputs = {FILENAME: payload,
               "manifest.json": (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")}
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        target = directory / name
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    export(options.output_dir)
    print(f"Authenticated original code scope: commit={COMMIT} blob={BLOB} sha256={SHA256}.")


if __name__ == "__main__":
    main()

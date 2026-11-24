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
BLOB = "e17824cda99abb7b863fc08ad60eb0d172e2b03b"
SHA256 = "5fc581a2a302af14c2ba2c03790e16891ab4a98c0e2cc3ff6974cff0a9642f9a"
OWNER = "u3l4_alcance_codigo_en_recuadros"
CONTENT_PREFIX = "app/src/main/assets/"
COURSE_RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/course_cpp_fundamentals.json"
COURSE_BLOB = "a26324ca155efcbece1cf731348b0ff7b2367cab"
COURSE_SHA256 = "f7557d5dfdcf2b644f6a511e128729f9d5a2cd85ade8bdbce7393c423f442576"
UNIT_RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                 "edition_es_1__expresiones_y_decisiones/unit_edition_es_1__expresiones_y_decisiones.json")
UNIT_BLOB = "bf7eb7157995ffe259005340735c89e5db29c357"
UNIT_SHA256 = "a9f21b994150697f4a47f8b2b5f6880e0d3ceb37b881850a17df8d15caa4b693"


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


def authenticated_bytes(relative: str, expected_blob: str, expected_sha256: str) -> bytes:
    reference = f"{COMMIT}:{relative}"
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"Pinned local code-scope baseline unavailable: {reference}. "
                           "Complete local Git history is required; no fetch or live-asset fallback is allowed.") from error
    frame = b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload
    if hashlib.sha1(frame).hexdigest() != expected_blob or hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise ValueError(f"The pinned course-edition fixture changed: {relative}")
    return payload


def historical_bytes() -> bytes:
    payload = authenticated_bytes(RELATIVE, BLOB, SHA256)
    verify_payload(payload)
    return payload


def export(directory: Path) -> None:
    # Native course-edition loading checks the physical course/language/unit
    # route and both active indexes. Export their exact bytes from the SAME
    # publication; flattening the lesson would correctly fail that guard.
    # Authenticate all three files before creating any output.
    payload = historical_bytes()
    course_payload = authenticated_bytes(COURSE_RELATIVE, COURSE_BLOB, COURSE_SHA256)
    unit_payload = authenticated_bytes(UNIT_RELATIVE, UNIT_BLOB, UNIT_SHA256)
    document = json.loads(payload)
    course, unit = json.loads(course_payload), json.loads(unit_payload)
    unit_id = "cpp_fundamentals__edition_es_1__expresiones_y_decisiones"
    if (course["id"] != "cpp_fundamentals" or unit["courseId"] != course["id"]
            or unit["unitId"] != unit_id or course["courseEdition"] != document["courseEdition"]
            or unit["courseEdition"] != document["courseEdition"]
            or unit_id not in {entry["id"] for entry in course["units"]}
            or document["id"] not in {entry["id"] for entry in unit["lessons"]}):
        raise ValueError("The pinned indexes do not own the exact baseline course-edition lesson")
    lesson_file = RELATIVE.removeprefix(CONTENT_PREFIX)
    course_file, unit_file = COURSE_RELATIVE.removeprefix(CONTENT_PREFIX), UNIT_RELATIVE.removeprefix(CONTENT_PREFIX)
    manifest = {
        "commit": COMMIT, "path": RELATIVE, "blob": BLOB, "sha256": SHA256,
        "file": lesson_file, "bytes": len(payload), "lessonId": document["id"],
        "indexes": [
            {"path": COURSE_RELATIVE, "file": course_file, "blob": COURSE_BLOB, "sha256": COURSE_SHA256},
            {"path": UNIT_RELATIVE, "file": unit_file, "blob": UNIT_BLOB, "sha256": UNIT_SHA256},
        ],
        "owner": OWNER, "display": {"page": 1, "block": 13}, "storage": {"page": 7, "block": 30},
        "scope": "Exact published lesson bytes; the Windows test creates the answer and playback bookmark through real controls",
    }
    outputs = {lesson_file: payload, course_file: course_payload, unit_file: unit_payload,
               "manifest.json": (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")}
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    export(options.output_dir)
    print(f"Authenticated original code scope: commit={COMMIT} blob={BLOB} sha256={SHA256}; "
          f"course-index={COURSE_BLOB} unit-index={UNIT_BLOB}; original physical edition route preserved.")


if __name__ == "__main__":
    main()

"""Export authenticated code-scope publications and source-bound QA reading overlays.

The native reader creates its own real answer and bookmark using these bytes.
No state, receipt, intermediate lesson or historical narration is synthesized.
"""
from __future__ import annotations

import argparse
import copy
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
READING_COMMIT = "aae31e3d0ff202d700d3f3dd5ba27854eebe088d"
READING_BLOB = "420f3650e24d417cfac73f6ebde1678f353d4b3f"
READING_SHA256 = "59262d6b7cb6721d3de2aff581a02722bb5d82a37402c1ceefdce66c991a9808"
READING_OWNER = "u3l4_alcance_bolsas"
NOTE_OWNER = "u3l4_limites_nombre_bolsas"
READING_VARIANTS = ("published-reading", "reading-notes", "reading-notes-no-icon", "reading-notes-editorial")
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


def authenticated_bytes(relative: str, expected_blob: str, expected_sha256: str, *, commit: str = COMMIT) -> bytes:
    reference = f"{commit}:{relative}"
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


def stable_blocks(document: dict) -> dict:
    return {identity: document["pages"][page]["blocks"][block]
            for page, row in enumerate(document["stateLayout"]["current"])
            for block, identity in enumerate(row["blocks"])}


def utf16_length(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def reading_overlay(document: dict, *, icon: bool = True) -> dict:
    """QA-only projection of authentic fields; every source byte remains intact."""
    result = copy.deepcopy(document)
    blocks = stable_blocks(result)
    program, notes = blocks[READING_OWNER]["code"], blocks[NOTE_OWNER]["text"]
    lines = program.splitlines(keepends=True)
    if (len(lines) != 12 or lines[0] != "#include <iostream>\n"
            or lines[2] != "int main() {\n" or lines[5] != "    if (fichas >= 4) {\n"
            or lines[7] != "        std::cout << bolsas;\n"
            or lines[8] != "    }\n" or lines[10] != "    return 0;\n" or lines[11] != "}\n"
            or len(notes.split("\n\n")) != 2):
        raise ValueError("The pinned reading program or its two authored notes changed")
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + utf16_length(line))
    code_hash, note_hash = (hashlib.sha256(value.encode("utf-8")).hexdigest() for value in (program, notes))

    def code_node(key, first, last, children=None):
        node = {"id": key, "source": "/code", "sourceSha256": code_hash,
                "range": [offsets[first], offsets[last]], "role": "code"}
        if children is not None:
            node["children"] = children
        return node

    first_note, second_note = notes.split("\n\n")
    note_start = utf16_length(first_note + "\n\n")
    inside = {"id": "nota_dentro", "sourceBlock": NOTE_OWNER, "source": "/text",
              "sourceSha256": note_hash, "range": [0, utf16_length(first_note)], "role": "text"}
    if icon:
        inside["icon"] = "check"
    outside = {"id": "nota_fuera", "sourceBlock": NOTE_OWNER, "source": "/text",
               "sourceSha256": note_hash, "range": [note_start, note_start + utf16_length(second_note)], "role": "text"}
    entry = {"block": READING_OWNER, "language": "cpp", "root": {
        "id": "programa", "title": "Programa completo", "children": [
            code_node("cabecera", 0, 2),
            code_node("main", 2, 3, [
                code_node("fichas", 3, 5),
                code_node("if", 5, 6, [code_node("bolsas", 6, 7), code_node("salida", 7, 8),
                                        inside, code_node("if_cierre", 8, 10)]),
                outside, code_node("return", 10, 11), code_node("main_cierre", 11, 12)
            ])
        ]}}
    result["codeScopePresentations"]["version"] = 2
    result["codeScopePresentations"]["entries"].append(entry)
    from code_scope_presentations import validate
    validate(result, blocks=stable_blocks(result))
    if result["pages"] != document["pages"] or result["stateLayout"] != document["stateLayout"]:
        raise ValueError("The reading projection rewrote authentic content or evaluation history")
    return result


def editorial_reading_overlay(document: dict) -> dict:
    """Exercise the new public-wording API without editing the published lesson."""
    import stable_state_layout as layout
    authored = [
        ("/responses/0/options/0/label", "En main, antes de la declaración de fichas", ("main", "fichas")),
        ("/responses/0/options/1/label", "Después del bloque del if, justo antes de return 0;", ("if", "return 0;")),
        ("/responses/0/options/2/label", "Dentro del if, inmediatamente después de la declaración de bolsas", ("if", "bolsas")),
        ("/feedback/correct", "La posición antes_de_return conserva la respuesta original.", ("antes_de_return",)),
        ("/feedback/incorrect", "Revisa el alcance de fichas antes de volver a elegir.", ("fichas",)),
    ]
    edits = {}
    for pointer, text, fragments in authored:
        spans = []
        for fragment in fragments:
            at = text.index(fragment)
            spans.append({"start": utf16_length(text[:at]), "end": utf16_length(text[:at + len(fragment)]),
                          "text": fragment, "role": "cpp", "reading": "technical"})
        edits[pointer] = {"text": text, "spans": spans}
    result = copy.deepcopy(document)
    return layout.revise_math_text_editorial(result, OWNER, edits)


def reading_outputs() -> dict[str, bytes]:
    # The current main publication is consulted through immutable Git objects;
    # no branch checkout, merge, network fetch or live-content fallback occurs.
    payload = authenticated_bytes(RELATIVE, READING_BLOB, READING_SHA256, commit=READING_COMMIT)
    course = authenticated_bytes(COURSE_RELATIVE, COURSE_BLOB, COURSE_SHA256, commit=READING_COMMIT)
    unit = authenticated_bytes(UNIT_RELATIVE, UNIT_BLOB, UNIT_SHA256, commit=READING_COMMIT)
    document = json.loads(payload)
    blocks = stable_blocks(document)
    if (blocks[READING_OWNER]["type"] != "code" or blocks[NOTE_OWNER]["type"] != "paragraph"
            or blocks[OWNER]["responses"][0]["validator"]["expected"] != ["antes_de_return"]):
        raise ValueError("The authenticated reading source lost its code, notes or original answer")
    with_notes = reading_overlay(document)
    without_icon = reading_overlay(document, icon=False)
    editorial = editorial_reading_overlay(with_notes)
    variants = {"published-reading": payload,
                "reading-notes": (json.dumps(with_notes, ensure_ascii=False, indent=2) + "\n").encode("utf-8"),
                "reading-notes-no-icon": (json.dumps(without_icon, ensure_ascii=False, indent=2) + "\n").encode("utf-8"),
                "reading-notes-editorial": (json.dumps(editorial, ensure_ascii=False, indent=2) + "\n").encode("utf-8")}
    result = {}
    for name, lesson in variants.items():
        result[name + "/" + RELATIVE.removeprefix(CONTENT_PREFIX)] = lesson
        result[name + "/" + COURSE_RELATIVE.removeprefix(CONTENT_PREFIX)] = course
        result[name + "/" + UNIT_RELATIVE.removeprefix(CONTENT_PREFIX)] = unit
    manifest = {"commit": READING_COMMIT, "path": RELATIVE, "blob": READING_BLOB,
                "sha256": READING_SHA256, "readingOwner": READING_OWNER, "noteOwner": NOTE_OWNER,
                "variants": list(variants), "scope": "Authentic published code and prose with QA-only overlays; no curriculum write"}
    result["reading-source-manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    return result


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
    outputs.update(reading_outputs())
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
    print(f"Authenticated reading source: commit={READING_COMMIT} blob={READING_BLOB} sha256={READING_SHA256}; "
          f"QA variants={','.join(READING_VARIANTS)}; original code, notes, answer and indexes retained.")


if __name__ == "__main__":
    main()

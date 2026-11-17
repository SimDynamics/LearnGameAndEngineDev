"""Produce datos pequeños para native.course_edition en el PC Windows autorizado.

No modifica recursos curriculares. La edición usa la misma operación pública
de autoría que Work; el test nativo copia estos datos a un perfil QA nuevo.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

from course_edition import prepare_edition_lesson
import stable_state_layout

MARKER = "LearnGameAndEngineDev.course-edition-fixture.v1"
COURSE = "qa_edition"
OLD_UNIT = COURSE + "__original"
OLD_LESSON = OLD_UNIT + "__primera"
NEW_UNIT = COURSE + "__edition_es_1__inicio"
NEW_LESSON = NEW_UNIT + "__primera"
NEXT_LESSON = NEW_UNIT + "__segunda"
OTHER_UNIT = "qa_other__independiente"
OTHER_LESSON = OTHER_UNIT + "__primera"
DESCRIPTOR = {
    "version": 1, "courseId": COURSE, "locale": "es",
    "id": "qa-edition-1", "generation": 1,
}
SOURCE = "#include <iostream>\nint main() {\n    std::cout << 42 << '\\n';\n}\n"


def quiz(correct: int) -> dict:
    return {
        "type": "multiple_choice",
        "question": "Elige el resultado de 2 + 2." if correct == 0
                    else "Elige el resultado de 3 + 4.",
        "options": ["4", "7"], "correctIndex": correct,
        "explanation": "2 + 2 es 4." if correct == 0 else "3 + 4 es 7.",
    }


def compiler() -> dict:
    # El mismo contrato en ambas ediciones hace visible cualquier fuga de
    # recibos, aun cuando la fuente y su salida sigan siendo compatibles.
    return {
        "type": "cpp_compiler", "initialCode": SOURCE,
        "outputCheck": {
            "inputComparison": "exact",
            "cases": [{"standardInput": "", "outputs": ["42\n"]}],
        },
    }


def documents() -> tuple[dict[str, object], dict[str, str]]:
    old = stable_state_layout.prepare({
        "id": OLD_LESSON, "title": "Edición anterior QA",
        "compilerPuzzlesRequired": True,
        "pages": [
            {"blocks": [quiz(0)]},
            {"blocks": [compiler(), {"type": "code", "language": "cpp", "code": SOURCE}]},
        ],
    })
    old["referenceAfter"] = [
        {"activity": "block_0001_0000", "blocks": ["block_0001_0001"]}
    ]
    stable_state_layout.validate(old)
    current = prepare_edition_lesson({
        "id": NEW_LESSON, "title": "Nueva edición QA",
        "compilerPuzzlesRequired": True,
        "pages": [
            {"blocks": [quiz(1)]},
            {"blocks": [compiler()]},
            {"blocks": [{"type": "code", "language": "cpp", "code": SOURCE}]},
        ],
        "referenceAfter": [
            {"activity": "block_0001_0000", "blocks": ["block_0002_0000"]}
        ],
    }, DESCRIPTOR)
    following = prepare_edition_lesson({
        "id": NEXT_LESSON, "title": "Continuación QA",
        "pages": [{"blocks": [{"type": "paragraph", "text": "Página siguiente QA."}]}],
    }, DESCRIPTOR)
    other = {
        "id": OTHER_LESSON, "title": "Curso ajeno QA",
        "pages": [{"blocks": [{"type": "paragraph", "text": "Contenido ajeno conservado."}]}],
    }
    paths = {
        "course": "content/es/programming/qa_edition/course_qa_edition.json",
        "legacyCourse": "legacy-course.json",
        "legacyUnit": "content/es/programming/qa_edition/original/unit_original.json",
        "legacyLesson": "content/es/programming/qa_edition/original/lesson_primera.json",
        "unit": "content/es/programming/qa_edition/edition_es_1__inicio/unit_edition_es_1__inicio.json",
        "lesson": "content/es/programming/qa_edition/edition_es_1__inicio/lesson_primera.json",
        "nextLesson": "content/es/programming/qa_edition/edition_es_1__inicio/lesson_segunda.json",
        "otherLesson": "content/es/programming/qa_other/independiente/lesson_primera.json",
    }
    files = {
        "content/es/programming/courses.json": [
            {"id": COURSE, "number": 1, "title": "Curso de edición QA"},
            {"id": "qa_other", "number": 2, "title": "Curso ajeno QA"},
        ],
        paths["legacyCourse"]: {
            "id": COURSE, "title": "Curso de edición QA",
            "units": [{"id": OLD_UNIT, "number": 1, "title": "Unidad anterior QA"}],
        },
        paths["course"]: {
            "id": COURSE, "title": "Curso de edición QA",
            "courseEdition": deepcopy(DESCRIPTOR),
            "units": [{"id": NEW_UNIT, "number": 1, "title": "Unidad nueva QA"}],
        },
        paths["legacyUnit"]: {
            "courseId": COURSE, "unitId": OLD_UNIT, "title": "Unidad anterior QA",
            "lessons": [{"id": OLD_LESSON, "number": 1, "title": "Edición anterior QA"}],
        },
        paths["unit"]: {
            "courseId": COURSE, "unitId": NEW_UNIT, "title": "Unidad nueva QA",
            "courseEdition": deepcopy(DESCRIPTOR),
            "lessons": [
                {"id": NEW_LESSON, "number": 1, "title": "Nueva edición QA"},
                {"id": NEXT_LESSON, "number": 2, "title": "Continuación QA"},
            ],
        },
        paths["legacyLesson"]: old,
        paths["lesson"]: current,
        paths["nextLesson"]: following,
        "content/es/programming/qa_other/course_qa_other.json": {
            "id": "qa_other", "title": "Curso ajeno QA",
            "units": [{"id": OTHER_UNIT, "number": 1, "title": "Unidad ajena QA"}],
        },
        "content/es/programming/qa_other/independiente/unit_independiente.json": {
            "courseId": "qa_other", "unitId": OTHER_UNIT, "title": "Unidad ajena QA",
            "lessons": [{"id": OTHER_LESSON, "number": 1, "title": "Curso ajeno QA"}],
        },
        paths["otherLesson"]: other,
    }
    return files, paths


def produce(destination: Path) -> None:
    if destination.is_symlink():
        raise ValueError("El destino QA no puede ser un enlace.")
    destination = destination.resolve()
    if destination.exists() and any(destination.iterdir()):
        marker = destination / "manifest.json"
        if not marker.is_file() or json.loads(marker.read_text(encoding="utf-8")).get("fixture") != MARKER:
            raise ValueError("El destino no es un árbol de pruebas de edición reconocido.")
    files, paths = documents()
    # No escrituras hasta completar y validar todas las fuentes en memoria.
    payloads = {
        name: (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        for name, value in files.items()
    }
    manifest = {
        "fixture": MARKER, "paths": paths,
        "files": {name: hashlib.sha256(payload).hexdigest() for name, payload in payloads.items()},
    }
    for name in [*payloads, "manifest.json"]:
        target = destination / name
        if target.is_symlink() or any(parent.is_symlink() for parent in target.parents):
            raise ValueError("El destino QA contiene un enlace; se conserva sin cambios.")
    for name, payload in payloads.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    (destination / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8", newline="\n",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Las comprobaciones de este proyecto se realizan en el PC Windows autorizado.")
    produce(args.output_dir)


if __name__ == "__main__":
    main()

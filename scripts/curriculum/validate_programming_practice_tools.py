"""Contrato estructural de Programación 2, reutilizando el validador habitual.

No cambia JSON, no ejecuta C++, no crea otro criterio para respuestas ni
hereda excepciones editoriales históricas exclusivas de Programación 1.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path


COURSE_ID = "programming_practice_tools"


def validate(contract, root: Path, base_commit: str | None, *,
             course_id: str = COURSE_ID, structure_only: bool = False) -> dict:
    """Reuse exact block/index validators for full validation or a bounded audit.

    structure_only deliberately does NOT authenticate stateLayout, editorial
    history or translations; its result must never be reported as accepted.
    """
    if course_id not in ("cpp_fundamentals", "programming_practice_tools"):
        raise ValueError("Curso no reconocido en auditoría estructural")
    content = contract.CONTENT.parent / course_id
    state = contract._state_layout
    require = contract.require
    load = contract.load
    if not content.is_dir():
        require(False, content, f"[{course_id}] no existe el directorio del curso")
        return {"course": course_id, "lessons": 0, "units": 0, "blocks": 0,
                "files": [], "historical": 0}

    lesson_paths = sorted(content.glob("*/lesson_*.json"))
    unit_paths = sorted(content.glob("*/unit_*.json"))
    lessons = {}
    lesson_sources = {}
    blocks = 0
    for path in lesson_paths:
        document = load(path)
        if not structure_only:
            contract.validate_lesson_state_contract(document, path)
        expected = "__".join((course_id, path.parent.name, path.stem[7:]))
        require(document["id"] == expected, path, f"[{course_id}] ID de lección distinto de la ruta")
        require(document["id"] not in lessons, path, f"[{course_id}] ID de lección duplicado")
        lessons[document["id"]] = document
        lesson_sources[document["id"]] = path
        require(bool(document["pages"]), path, f"[{course_id}] lección sin páginas")
        for pn, page in enumerate(document["pages"], 1):
            require(bool(page["blocks"]), path, f"[{course_id}] página {pn} vacía")
            for bn, block in enumerate(page["blocks"], 1):
                blocks += 1
                where = f"{course_id}:{path.name} p{pn} b{bn}"
                kind = block["type"]
                require(kind in contract.FIELDS, where, "tipo de bloque no soportado")
                if kind not in contract.FIELDS:
                    continue
                require(set(block) <= contract.FIELDS[kind], where, "campo ajeno al contrato comprobado")
                contract.validate_review_feedback_contract(block, where)
                contract.validate_code_presentation_contract(block, where)
                contract.validate_paragraph_emphasis_contract(block, where)
                if kind == "math_puzzle":
                    contract.validate_math_puzzle_contract(block, where)
                if kind == "cpp_compiler":
                    contract.validate_cpp_compiler(block, where)
                if kind == "image":
                    contract.validate_image_contract(block, where, root / "app/src/main/assets/content")
                if kind in {"heading", "paragraph", "note"}:
                    require(bool(block.get("text", "").strip()), where, "texto vacío")
                if kind == "multiple_choice":
                    options = block["options"]
                    require(len(options) >= 2 and len(set(options)) == len(options),
                            where, "opciones insuficientes o duplicadas")
                    require(0 <= block["correctIndex"] < len(options),
                            where, "índice de respuesta fuera de rango")
                if kind in {"fill_code", "output_prediction", "inline_fill_code"}:
                    answers = block.get("answers")
                    require(isinstance(answers, list) and bool(answers)
                            and all(isinstance(a, str) and a.strip() for a in answers),
                            where, "respuesta vacía o no textual")
                    contract.validate_answer_contract(block, where)
                if kind == "order_code":
                    require(sorted(block["correctOrder"]) == list(range(len(block["fragments"]))),
                            where, "orden no es una permutación")
                if kind in {"drag_fill_code", "inline_fill_code"}:
                    key = "correctOrder" if kind == "drag_fill_code" else "answers"
                    require(block["template"].count("[]") == len(block[key]),
                            where, "huecos y respuestas no coinciden")
                    if kind == "drag_fill_code":
                        order = block[key]
                        require(all(0 <= i < len(block["choices"]) for i in order),
                                where, "ficha fuera de rango")
                        require(len(set(order)) == len(order), where, "una ficha física se reutiliza")

    units = {}
    for path in unit_paths:
        unit = load(path)
        expected = course_id + "__" + path.parent.name
        require(unit["courseId"] == course_id and unit["unitId"] == expected,
                path, f"[{course_id}] ID de unidad no corresponde al loader")
        require(unit["unitId"] not in units, path, f"[{course_id}] ID de unidad duplicado")
        require(bool(unit["lessons"]), path, f"[{course_id}] unidad sin lecciones")
        units[unit["unitId"]] = unit
        numbers = [lesson["number"] for lesson in unit["lessons"]]
        require(numbers == list(range(1, len(numbers) + 1)),
                path, f"[{course_id}] numeración de lecciones discontinua")
        seen = set()
        for lesson in unit["lessons"]:
            require(lesson["id"] not in seen, path, f"[{course_id}] lección repetida en índice")
            seen.add(lesson["id"])
            require(lesson["id"] in lessons, path, f"[{course_id}] referencia a lección inexistente")
            if lesson["id"] in lessons:
                require(lesson["title"] == lessons[lesson["id"]]["title"], path,
                        f"[{course_id}] título no coincide con la lección")

    course_path = content / f"course_{course_id}.json"
    course = load(course_path)
    require(course["id"] == course_id, course_path, f"[{course_id}] ID de curso distinto del loader")
    require([unit["number"] for unit in course["units"]]
            == list(range(1, len(course["units"]) + 1)),
            course_path, f"[{course_id}] numeración de unidades discontinua")
    indexed = set()
    for item in course["units"]:
        require(item["id"] not in indexed, course_path,
                f"[{course_id}] unidad repetida en índice del curso")
        indexed.add(item["id"])
        require(item["id"] in units, course_path,
                f"[{course_id}] referencia a unidad inexistente")
        if item["id"] in units:
            require(item["title"] == units[item["id"]]["title"], course_path,
                    f"[{course_id}] título de unidad no coincide")

    # In indexed units, every physical lesson must be indexed once and
    # only there. Historical/fixture units outside the course index are
    # deliberately not renamed, deleted or treated as missing publication.
    indexed_lessons = set()
    for unit_id in indexed:
        if unit_id not in units:
            continue  # The missing-unit diagnostic was already recorded.
        folder = unit_id.removeprefix(course_id + "__")
        unit_path = content / folder / f"unit_{folder}.json"
        unit = units[unit_id]
        referenced = {entry["id"] for entry in unit["lessons"]}
        actual = {lesson_id for lesson_id, source in lesson_sources.items()
                  if source.parent == unit_path.parent}
        for missing in sorted(actual - referenced):
            require(False, unit_path,
                    f"[{course_id}] lección publicada sin índice en unidad activa: {missing}")
        for stray in sorted(referenced - actual):
            require(False, unit_path,
                    f"[{course_id}] lección de otra carpeta o ausente en unidad activa: {stray}")
        for duplicate in sorted(referenced & indexed_lessons):
            require(False, unit_path,
                    f"[{course_id}] lección indexada en dos unidades activas: {duplicate}")
        indexed_lessons.update(referenced)

    previous_examined = 0
    # The global check_repository already authenticates the complete
    # multilingual state family; these individual historical documents come
    # only from a pinned Git commit. Do not accept baselines from draft files.
    if not structure_only and (root / ".git").exists():
        prefix = content.relative_to(root).as_posix() + "/"
        publication = sorted(state._lesson_paths_at(root, base_commit))
        for relative in publication:
            if not relative.startswith(prefix):
                continue
            previous_examined += 1
            previous = json.loads(subprocess.check_output(
                ["git", "show", f"{base_commit}:{relative}"],
                cwd=root, text=True, encoding="utf-8"
            ))
            current_path = root / relative
            if not current_path.is_file():
                require(False, relative, f"[{course_id}] lección previa perdida")
                continue
            delivered = load(current_path)
            try:
                positions = state.validate(previous)
                aligned = state.align_for_previous_validation(previous, delivered)
            except (ValueError, KeyError, TypeError) as error:
                require(False, relative, f"[{course_id}] transición de estado no verificada: {error}")
                continue
            require(aligned["id"] == previous["id"], relative, f"[{course_id}] ID previo modificado")
            for pn, page in enumerate(previous["pages"]):
                for bn, old in enumerate(page["blocks"]):
                    kind = old["type"]
                    if kind not in contract.EXERCISES | {"code", "cpp_compiler"}:
                        continue
                    try:
                        new = aligned["pages"][pn]["blocks"][bn]
                    except IndexError:
                        require(False, relative, f"[{course_id}] bloque previo perdido p{pn+1} b{bn+1}")
                        continue
                    require(new["type"] == kind and new.get("id") == old.get("id"),
                            relative, f"[{course_id}] posición/ID previo modificado p{pn+1} b{bn+1}")
                    origin_p, origin_b = positions[pn, bn]
                    if kind in {"code", "cpp_compiler"}:
                        valid = new == old
                    else:
                        # Ordinary /explanation is the only new permission.
                        # It checks all other fields byte-for-byte, owner,
                        # language family, full editorial proof chain and
                        # the authored /explanation semantic descriptor.
                        valid = (new == old
                                 or contract.preserves_ordinary_explanation_revision(
                                     relative, origin_p, origin_b, old, new,
                                     previous, delivered, course_id=course_id))
                    require(valid, relative,
                            f"[{course_id}] campos previos modificados sin autorización: "
                            f"{contract.previous_exercise_difference(old, new)} p{pn+1} b{bn+1}")

    return {
        "course": course_id,
        "lessons": len(lesson_paths),
        "units": len(unit_paths),
        "blocks": blocks,
        "files": [*(path.relative_to(root).as_posix() for path in lesson_paths),
                  *(path.relative_to(root).as_posix() for path in unit_paths),
                  course_path.relative_to(root).as_posix()],
        "historical": previous_examined,
        "scope": ("STRUCTURE_ONLY_NOT_APPROVED" if structure_only else
                  "FULL_CURRICULAR_CONTRACT_WITH_GIT_HISTORY"),
        "stateLayoutValidated": not structure_only,
        "historicalProtectionChecked": (not structure_only and
                                         (root / ".git").exists()),
    }

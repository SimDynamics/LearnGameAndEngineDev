"""Contrato estructural de cursos de Programación con carpeta e índice reales.

Reutiliza el validador habitual sin cambiar JSON ni ejecutar C++, y no hereda
excepciones editoriales históricas exclusivas de Programación 1.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path


COURSE_ID = "programming_practice_tools"


def _document(contract, path, course_id):
    try:
        value = contract.load(path)
    except (OSError, UnicodeError, ValueError, TypeError, RecursionError) as error:
        contract.require(False, path, f"[{course_id}] JSON no disponible o inválido: {error}")
        return None
    contract.require(isinstance(value, dict), path, f"[{course_id}] JSON requiere un objeto")
    return value if isinstance(value, dict) else None


def _validate(contract, where, function, *args, **kwargs):
    # The shared validators retain all their checks. Malformed nested input is
    # reported at its source instead of terminating the other course checks.
    try:
        function(*args, **kwargs)
    except (ValueError, KeyError, TypeError, IndexError, AttributeError,
            OverflowError, RecursionError) as error:
        contract.require(False, where, f"configuración inválida: {error}")


def _text(value):
    return isinstance(value, str) and bool(value.strip())


def _texts(value):
    return isinstance(value, list) and bool(value) and all(_text(item) for item in value)


def _safe_file(contract, path, content, course_id):
    try:
        relative = path.relative_to(content)
        safe = (path.resolve() == content.resolve() / relative
                and path.is_file())
    except (OSError, ValueError, RuntimeError):
        safe = False
    contract.require(safe, path,
                     f"[{course_id}] archivo inexistente o ruta fuera del directorio del curso")
    return safe


def _block(contract, block, where, *, root, document, position, structure_only):
    require = contract.require
    require(isinstance(block, dict), where, "bloque debe ser un objeto")
    if not isinstance(block, dict):
        return
    kind = block.get("type")
    valid_kind = isinstance(kind, str) and kind in contract.FIELDS
    require(valid_kind, where, "tipo de bloque no soportado")
    if not valid_kind:
        return
    require(set(block) <= contract.FIELDS[kind], where, "campo ajeno al contrato comprobado")
    for field in ("id", "language", "question", "explanation"):
        if field in block:
            require(_text(block[field]), where, f"{field} debe ser texto no vacío")
    if kind in {"heading", "paragraph", "note"}:
        require(_text(block.get("text")), where, "texto vacío o no textual")
    if kind in {"multiple_choice", "fill_code", "output_prediction", "order_code",
                "drag_fill_code", "inline_fill_code"}:
        require(_text(block.get("question")), where, "question debe ser texto no vacío")
    if kind in {"code", "fill_code", "output_prediction"}:
        require(isinstance(block.get("code"), str), where, "code debe ser texto")
    for function in (contract.validate_review_feedback_contract,
                     contract.validate_code_presentation_contract,
                     contract.validate_paragraph_emphasis_contract):
        _validate(contract, where, function, block, where)
    if kind == "math_puzzle":
        _validate(contract, where, contract.validate_math_puzzle_contract, block, where)
    if kind == "simulation":
        _validate(contract, where, contract.validate_simulation_contract, block, where)
    if kind == "cpp_compiler":
        kwargs = {} if structure_only else {"document": document, "position": position}
        _validate(contract, where, contract.validate_cpp_compiler, block, where, **kwargs)
    if kind == "image":
        _validate(contract, where, contract.validate_image_contract, block, where,
                  root / "app/src/main/assets/content")
    if kind == "multiple_choice":
        options, correct = block.get("options"), block.get("correctIndex")
        valid_options = _texts(options)
        require(valid_options and len(options) >= 2 and len(set(options)) == len(options),
                where, "opciones insuficientes, no textuales o duplicadas")
        require(valid_options and type(correct) is int and 0 <= correct < len(options),
                where, "índice de respuesta fuera de rango o no entero")
    if kind in {"fill_code", "output_prediction", "inline_fill_code"}:
        answers = block.get("answers")
        require(_texts(answers), where, "respuesta vacía o no textual")
        if _texts(answers):
            _validate(contract, where, contract.validate_answer_contract, block, where)
    if kind == "order_code":
        fragments, order = block.get("fragments"), block.get("correctOrder")
        # Authoring rule: graded cards contain code. The native parser can
        # load blank strings for legacy compatibility, but optional visual
        # separators belong in orderVisualSeparators, not fragments.
        valid_fragments = (isinstance(fragments, list) and bool(fragments)
                           and all(_text(item) for item in fragments))
        require(valid_fragments, where, "fragments debe ser una lista de textos no vacíos")
        require(valid_fragments and isinstance(order, list)
                and all(type(index) is int for index in order)
                and sorted(order) == list(range(len(fragments))),
                where, "orden no es una permutación de índices enteros")
    if kind in {"drag_fill_code", "inline_fill_code"}:
        template = block.get("template")
        key = "correctOrder" if kind == "drag_fill_code" else "answers"
        values = block.get(key)
        require(isinstance(template, str), where, "template debe ser texto")
        require(isinstance(template, str) and isinstance(values, list)
                and template.count("[]") == len(values),
                where, "huecos y respuestas no coinciden")
        if kind == "drag_fill_code":
            choices = block.get("choices")
            require(_texts(choices), where, "choices debe ser una lista de textos no vacía")
            valid_order = (isinstance(values, list) and bool(values)
                           and all(type(index) is int for index in values))
            require(valid_order and _texts(choices)
                    and all(0 <= index < len(choices) for index in values),
                    where, "ficha fuera de rango o no entera")
            if valid_order:
                require(len(set(values)) == len(values), where, "una ficha física se reutiliza")


def _index(contract, document, key, path, course_id):
    entries = document.get(key)
    contract.require(isinstance(entries, list) and bool(entries), path,
                     f"[{course_id}] {key} debe ser una lista no vacía")
    if not isinstance(entries, list):
        return []
    result = []
    for number, entry in enumerate(entries, 1):
        valid = (isinstance(entry, dict) and _text(entry.get("id"))
                 and _text(entry.get("title")))
        contract.require(valid, path,
                         f"[{course_id}] entrada {number} de {key} requiere id y title textuales")
        if not valid:
            continue
        # The native index contract has exactly these three authored fields.
        # Unknown metadata must not be silently ignored on future courses.
        contract.require(set(entry) == {"id", "number", "title"}, path,
                         f"[{course_id}] entrada {number} de {key} contiene campos no admitidos")
        label = "lecciones" if key == "lessons" else "unidades"
        contract.require(type(entry.get("number")) is int and entry["number"] == number,
                         path, f"[{course_id}] numeración de {label} discontinua o no entera")
        result.append(entry)
    return result


def validate(contract, root: Path, base_commit: str | None, *,
             course_id: str = COURSE_ID, structure_only: bool = False) -> dict:
    """Reuse exact block/index validators for full validation or a bounded audit.

    structure_only deliberately does NOT authenticate stateLayout, editorial
    history or translations; its result must never be reported as accepted.
    """
    state = contract._state_layout
    require = contract.require
    result = {"course": course_id, "lessons": 0, "units": 0, "blocks": 0,
              "files": [], "historical": 0,
              "scope": ("STRUCTURE_ONLY_NOT_APPROVED" if structure_only else
                        "FULL_CURRICULAR_CONTRACT_WITH_GIT_HISTORY"),
              "stateLayoutValidated": not structure_only,
              "historicalProtectionChecked": False}
    if not contract.is_structural_course_id(course_id):
        require(False, root, "ID de curso inválido: use minúsculas, cifras y guiones bajos simples")
        return result
    try:
        root = Path(root).resolve()
        programming = Path(contract.CONTENT).parent.resolve()
        expected = root / "app/src/main/assets/content/es/programming"
        valid_root = (programming.resolve() == expected.resolve()
                      and programming.resolve().is_relative_to(root))
        content = programming / course_id
        valid_course = (content.resolve() == programming.resolve() / course_id
                        and content.is_dir())
    except (OSError, ValueError, TypeError, AttributeError, RuntimeError) as error:
        require(False, root, f"[{course_id}] configuración de rutas inválida: {error}")
        return result
    require(valid_root, programming, f"[{course_id}] ruta curricular fuera de la raíz del proyecto")
    if not valid_root:
        return result
    require(valid_course, content, f"[{course_id}] no existe el directorio del curso o su ruta escapa")
    if not valid_course:
        return result
    course_path = content / f"course_{course_id}.json"
    course = (_document(contract, course_path, course_id)
              if _safe_file(contract, course_path, content, course_id) else None)
    if course is None:
        return result
    require(course.get("id") == course_id, course_path, f"[{course_id}] ID de curso distinto del loader")
    require(_text(course.get("title")), course_path, f"[{course_id}] título de curso vacío o no textual")
    if "developmentContent" in course:
        require(type(course["developmentContent"]) is bool, course_path,
                f"[{course_id}] developmentContent debe ser booleano")
    course_entries = _index(contract, course, "units", course_path, course_id)

    lesson_paths = sorted(content.glob("*/lesson_*.json"))
    unit_paths = sorted(content.glob("*/unit_*.json"))
    result["lessons"], result["units"] = len(lesson_paths), len(unit_paths)
    result["files"] = [*(path.relative_to(root).as_posix() for path in lesson_paths),
                       *(path.relative_to(root).as_posix() for path in unit_paths),
                       course_path.relative_to(root).as_posix()]
    lessons, lesson_sources = {}, {}
    for path in lesson_paths:
        if not _safe_file(contract, path, content, course_id):
            continue
        document = _document(contract, path, course_id)
        if document is None:
            continue
        if not structure_only:
            _validate(contract, path, contract.validate_lesson_state_contract, document, path)
        expected = "__".join((course_id, path.parent.name, path.stem[7:]))
        identity = document.get("id")
        require(identity == expected, path, f"[{course_id}] ID de lección distinto de la ruta")
        require(_text(document.get("title")), path, f"[{course_id}] título de lección vacío o no textual")
        if _text(identity):
            require(identity not in lessons, path, f"[{course_id}] ID de lección duplicado")
            lessons[identity], lesson_sources[identity] = document, path
        pages = document.get("pages")
        require(isinstance(pages, list) and bool(pages), path, f"[{course_id}] lección sin páginas válidas")
        if not isinstance(pages, list):
            continue
        for pn, page in enumerate(pages, 1):
            values = page.get("blocks") if isinstance(page, dict) else None
            require(isinstance(values, list) and bool(values), path,
                    f"[{course_id}] página {pn} vacía o sin lista de bloques")
            if not isinstance(values, list):
                continue
            for bn, block in enumerate(values, 1):
                result["blocks"] += 1
                where = f"{course_id}:{path.name} p{pn} b{bn}"
                _block(contract, block, where, root=root, document=document,
                       position=(pn - 1, bn - 1), structure_only=structure_only)

    units, unit_entries = {}, {}
    for path in unit_paths:
        if not _safe_file(contract, path, content, course_id):
            continue
        unit = _document(contract, path, course_id)
        if unit is None:
            continue
        expected = course_id + "__" + path.parent.name
        identity = unit.get("unitId")
        require(path.name == f"unit_{path.parent.name}.json", path,
                f"[{course_id}] nombre de índice de unidad distinto de su carpeta")
        require(unit.get("courseId") == course_id and identity == expected,
                path, f"[{course_id}] ID de unidad no corresponde al loader")
        require(_text(unit.get("title")), path, f"[{course_id}] título de unidad vacío o no textual")
        entries = _index(contract, unit, "lessons", path, course_id)
        if _text(identity):
            require(identity not in units, path, f"[{course_id}] ID de unidad duplicado")
            units[identity], unit_entries[identity] = unit, entries
        seen = set()
        for lesson in entries:
            identifier = lesson["id"]
            require(identifier not in seen, path, f"[{course_id}] lección repetida en índice")
            seen.add(identifier)
            require(identifier in lessons, path, f"[{course_id}] referencia a lección inexistente")
            if identifier in lessons:
                require(lesson["title"] == lessons[identifier].get("title"), path,
                        f"[{course_id}] título no coincide con la lección")
                require(lesson_sources[identifier].parent == path.parent, path,
                        f"[{course_id}] lección de otra carpeta o ausente en unidad activa: {identifier}")

    indexed = set()
    for item in course_entries:
        identifier = item["id"]
        require(identifier not in indexed, course_path,
                f"[{course_id}] unidad repetida en índice del curso")
        indexed.add(identifier)
        require(identifier in units, course_path,
                f"[{course_id}] referencia a unidad inexistente")
        if identifier in units:
            require(item["title"] == units[identifier].get("title"), course_path,
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
        referenced = {entry["id"] for entry in unit_entries[unit_id]}
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
        try:
            publication = sorted(state._lesson_paths_at(root, base_commit))
        except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
            require(False, content, f"[{course_id}] publicación previa no verificable: {error}")
            return result
        for relative in publication:
            if not relative.startswith(prefix):
                continue
            previous_examined += 1
            try:
                previous = json.loads(subprocess.check_output(
                    ["git", "show", f"{base_commit}:{relative}"],
                    cwd=root, text=True, encoding="utf-8"
                ))
            except (ValueError, OSError, UnicodeError, subprocess.SubprocessError) as error:
                require(False, relative, f"[{course_id}] lección previa no verificable: {error}")
                continue
            current_path = root / relative
            if not _safe_file(contract, current_path, content, course_id):
                require(False, relative, f"[{course_id}] lección previa perdida")
                continue
            delivered = _document(contract, current_path, course_id)
            if delivered is None:
                continue
            try:
                positions = state.validate(previous)
                aligned = state.align_for_previous_validation(previous, delivered)
            except (ValueError, KeyError, TypeError, IndexError, AttributeError) as error:
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
                        # Generic authenticated revisions retain exact owner,
                        # family and publication proofs. The shared guard
                        # excludes legacy exceptions for every other course.
                        valid = (new == old
                                 or contract.preserves_previous_exercise(
                                     relative, origin_p, origin_b, old, new,
                                     previous_document=previous, current_document=delivered, course_id=course_id))
                    if not valid:
                        valid = contract.preserves_source_format_revision(
                            relative, origin_p, origin_b, old, new,
                            previous_document=previous, current_document=delivered,
                            course_id=course_id)
                    require(valid, relative,
                            f"[{course_id}] campos previos modificados sin autorización: "
                            f"{contract.previous_exercise_difference(old, new)} p{pn+1} b{bn+1}")

    result["historical"] = previous_examined
    result["historicalProtectionChecked"] = not structure_only and (root / ".git").exists()
    return result

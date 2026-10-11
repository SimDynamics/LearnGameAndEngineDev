"""Comprueba el contrato de assets y conserva las posiciones del contenido previo.

No compila C++, no modifica archivos y no certifica las respuestas pedagógicas.
"""

import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
FIELDS = {
    "heading": {"type", "text"},
    "paragraph": {"type", "text"},
    "note": {"type", "text"},
    "code": {"type", "language", "code"},
    "multiple_choice": {"type", "id", "question", "options", "correctIndex", "explanation"},
    "fill_code": {"type", "id", "question", "language", "code", "answers", "explanation"},
    "output_prediction": {"type", "id", "question", "language", "code", "answers", "explanation"},
    "order_code": {"type", "id", "question", "fragments", "correctOrder", "explanation"},
    "drag_fill_code": {"type", "id", "question", "language", "template", "choices", "correctOrder"},
    "inline_fill_code": {"type", "id", "question", "language", "template", "answers"},
    # language existe en fixtures previos; el renderer sólo lee initialCode.
    "cpp_compiler": {"type", "language", "initialCode"},
}
EXERCISES = set(FIELDS) - {"heading", "paragraph", "note", "code", "cpp_compiler"}
ERRORS = []


def require(condition, where, message):
    if not condition:
        ERRORS.append(f"{where}: {message}")


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    lessons = {}
    blocks = 0
    for path in sorted(CONTENT.glob("*/lesson_*.json")):
        doc = load(path)
        expected = "__".join(("cpp_fundamentals", path.parent.name, path.stem[7:]))
        require(doc["id"] == expected, path, "ID distinto de la ruta del loader")
        require(doc["id"] not in lessons, path, "ID de lección duplicado")
        lessons[doc["id"]] = doc
        require(bool(doc["pages"]), path, "lección sin páginas")
        for pn, page in enumerate(doc["pages"], 1):
            require(bool(page["blocks"]), path, f"página {pn} vacía")
            for bn, block in enumerate(page["blocks"], 1):
                blocks += 1
                where = f"{path.name} p{pn} b{bn}"
                kind = block["type"]
                require(kind in FIELDS, where, "tipo de bloque no soportado")
                if kind not in FIELDS:
                    continue
                require(set(block) <= FIELDS[kind], where, "campo ajeno al contrato comprobado")
                if kind in {"heading", "paragraph", "note"}:
                    require(bool(block.get("text", "").strip()), where, "texto vacío")
                if kind == "multiple_choice":
                    options = block["options"]
                    require(len(options) >= 2 and len(set(options)) == len(options), where, "opciones insuficientes o duplicadas")
                    require(0 <= block["correctIndex"] < len(options), where, "índice de respuesta fuera de rango")
                if kind in {"fill_code", "output_prediction", "inline_fill_code"}:
                    require(bool(block["answers"]) and all(isinstance(a, str) and a.strip() for a in block["answers"]), where, "respuesta vacía o no textual")
                if kind == "order_code":
                    require(sorted(block["correctOrder"]) == list(range(len(block["fragments"]))), where, "orden no es una permutación")
                if kind in {"drag_fill_code", "inline_fill_code"}:
                    key = "correctOrder" if kind == "drag_fill_code" else "answers"
                    require(block["template"].count("[]") == len(block[key]), where, "huecos y respuestas no coinciden")
                    if kind == "drag_fill_code":
                        order = block[key]
                        require(all(0 <= i < len(block["choices"]) for i in order), where, "ficha fuera de rango")
                        require(len(set(order)) == len(order), where, "una ficha física se reutiliza")

    units = {}
    for path in sorted(CONTENT.glob("*/unit_*.json")):
        unit = load(path)
        expected = "cpp_fundamentals__" + path.parent.name
        require(unit["courseId"] == "cpp_fundamentals" and unit["unitId"] == expected, path, "ID de unidad no corresponde al loader")
        require(bool(unit["lessons"]), path, "unidad sin lecciones")
        units[unit["unitId"]] = unit
        numbers = [lesson["number"] for lesson in unit["lessons"]]
        require(numbers == list(range(1, len(numbers) + 1)), path, "numeración de lecciones discontinua")
        seen = set()
        for item in unit["lessons"]:
            require(item["id"] not in seen, path, "lección repetida en índice")
            seen.add(item["id"])
            require(item["id"] in lessons, path, "referencia a lección inexistente")
            if item["id"] in lessons:
                require(item["title"] == lessons[item["id"]]["title"], path, "título no coincide con la lección")

    course = load(CONTENT / "course_cpp_fundamentals.json")
    require([u["number"] for u in course["units"]] == list(range(1, len(course["units"]) + 1)), "curso", "numeración de unidades discontinua")
    for item in course["units"]:
        require(item["id"] in units, "curso", "referencia a unidad inexistente")
        if item["id"] in units:
            require(item["title"] == units[item["id"]]["title"], "curso", "título de unidad no coincide")

    # Mantener páginas/bloques evita trasladar las claves de progreso existentes.
    git = ROOT / ".git"
    if git.exists():
        files = subprocess.check_output(["git", "ls-files", "app/src/main/assets/content/es/programming/cpp_fundamentals/*/lesson_*.json"], cwd=ROOT, text=True).splitlines()
        for relative in files:
            old = json.loads(subprocess.check_output(["git", "show", "HEAD:" + relative], cwd=ROOT, text=True, encoding="utf-8"))
            new = load(ROOT / relative)
            require(new["id"] == old["id"], relative, "ID previo modificado")
            for pn, page in enumerate(old["pages"]):
                for bn, block in enumerate(page["blocks"]):
                    kind = block["type"]
                    if kind not in EXERCISES | {"code", "cpp_compiler"}:
                        continue
                    try:
                        current = new["pages"][pn]["blocks"][bn]
                    except IndexError:
                        require(False, relative, f"bloque previo perdido p{pn+1} b{bn+1}")
                        continue
                    require(current["type"] == kind and current.get("id") == block.get("id"), relative, f"posición/ID previo modificado p{pn+1} b{bn+1}")
                    if kind != "multiple_choice":
                        require(current == block, relative, f"código/respuesta previa modificada p{pn+1} b{bn+1}")

    if ERRORS:
        print("\n".join(ERRORS))
        raise SystemExit(1)
    print(f"Contrato, referencias y posiciones previas correctos: {len(lessons)} lecciones, {blocks} bloques. No certifica semántica C++ ni ejecución Android.")


if __name__ == "__main__":
    main()

"""Comprueba el contrato de assets y conserva las posiciones del contenido previo.

No compila C++, no modifica archivos y no certifica las respuestas pedagógicas.
"""

import json
import subprocess
from pathlib import Path, PurePosixPath


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
    # Los proyectos sustituyen initialCode al ejecutar; se conserva el campo legacy.
    "cpp_compiler": {"type", "language", "initialCode", "initialStandardInput", "projectFiles", "sourceFiles"},
}
EXERCISES = set(FIELDS) - {"heading", "paragraph", "note", "code", "cpp_compiler"}
COMPILER_EXTENSIONS = {"initialStandardInput", "projectFiles", "sourceFiles"}
SOURCE_SUFFIXES = {"cpp", "cc", "cxx", "c++"}
STDIN_LESSON = "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json"
# Precisiones revisadas de consigna; todos los demás campos del ejercicio,
# incluidos código, respuestas, identificador y explicación, siguen intactos.
QUESTION_CLARIFICATIONS = {
    ("colecciones_y_recorridos/lesson_extremos_sin_inventar_datos.json", 3, 4):
        "Completa sólo el dígito del índice para iniciar mayor con el primer elemento de esta colección no vacía.",
    ("colecciones_y_recorridos/lesson_mediana_y_posiciones_centrales.json", 2, 5):
        "Completa sólo el dígito del divisor que localiza el índice central para cualquier tamaño impar positivo, no sólo para los tres datos de esta muestra.",
    ("colecciones_y_recorridos/lesson_ordenar_y_conservar_el_original.json", 2, 4):
        "Completa sólo el nombre de la operación de ordenación utilizada en el ejemplo anterior.",
    ("colecciones_y_recorridos/lesson_recorrer_indices_con_seguridad.json", 2, 3):
        "Completa sólo el operador de comparación para exigir que i sea estrictamente menor que datos.size().",
    ("errores_y_construccion_gradual/lesson_limpiar_sin_cambiar_el_comportamiento.json", 2, 5):
        "Extraes la comprobación de dominio. Completa sólo el operador de comparación para aceptar piezas de cero a diez, incluidas ambas fronteras, y rechazar los demás enteros.",
    ("datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json", 3, 5):
        "Completa sólo el símbolo de negación que hace != coherente con ==.",
}
ERRORS = []


def require(condition, where, message):
    if not condition:
        ERRORS.append(f"{where}: {message}")


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def validate_project_path(value, where):
    """Rutas virtuales relativas y portables, sin normalización que oculte escapes."""
    valid = (
        isinstance(value, str)
        and bool(value.strip())
        and value == value.strip()
        and not any(ord(char) < 32 or char in '\\:<>"|?*' for char in value)
        and all(part not in {"", ".", ".."} for part in value.split("/"))
    )
    require(valid, where, "ruta de proyecto inválida: debe ser relativa, sin escapes ni separadores de Windows")
    return valid


def is_source_file(path):
    return "." in path and path.rsplit(".", 1)[1].lower() in SOURCE_SUFFIXES


def validate_cpp_compiler(block, where):
    require(isinstance(block.get("initialCode"), str), where, "initialCode debe ser texto")
    if "initialStandardInput" in block:
        require(isinstance(block["initialStandardInput"], str), where, "initialStandardInput debe ser texto")

    if "projectFiles" not in block:
        require("sourceFiles" not in block, where, "sourceFiles requiere projectFiles")
        return

    files = block["projectFiles"]
    require(isinstance(files, list) and bool(files), where, "projectFiles debe ser una lista no vacía")
    if not isinstance(files, list):
        return
    paths = {}
    for index, item in enumerate(files, 1):
        file_where = f"{where} archivo {index}"
        require(isinstance(item, dict), file_where, "archivo debe ser un objeto")
        if not isinstance(item, dict):
            continue
        require(set(item) == {"path", "initialCode"}, file_where, "archivo requiere sólo path e initialCode")
        require(isinstance(item.get("initialCode"), str), file_where, "initialCode de archivo debe ser texto")
        path = item.get("path")
        if validate_project_path(path, file_where):
            require(path not in paths, file_where, "ruta de archivo duplicada")
            paths[path] = path

    # También se rechaza archivo/carpeta: a.cpp y a.cpp/util.h no pueden coexistir.
    for path in paths:
        for parent in PurePosixPath(path).parents:
            require(str(parent) not in paths, where, "una ruta de archivo también se usa como carpeta")

    inferred = [path for path in paths if is_source_file(path)]
    sources = block.get("sourceFiles", inferred)
    require(isinstance(sources, list) and bool(sources), where, "sourceFiles debe seleccionar al menos una fuente")
    if not isinstance(sources, list):
        return
    selected = set()
    for index, source in enumerate(sources, 1):
        source_where = f"{where} fuente {index}"
        if not validate_project_path(source, source_where):
            continue
        require(source not in selected, source_where, "fuente seleccionada más de una vez")
        selected.add(source)
        require(source in paths, source_where, "fuente no existe en projectFiles con ese nombre exacto")
        require(is_source_file(source), source_where, "se seleccionó una cabecera o archivo no fuente")


def preserves_previous_compiler(relative, pn, bn, old, current, old_lesson):
    """Permite nuevos campos, manteniendo el programa anterior salvo la migración cin."""
    base = {key: value for key, value in current.items() if key not in COMPILER_EXTENSIONS or key in old}
    if relative == STDIN_LESSON and (pn, bn) == (6, 2):
        # Única sustitución autorizada: p7 b3 pasa del esqueleto a p8 b2 existente.
        expected = dict(old)
        expected["initialCode"] = old_lesson["pages"][7]["blocks"][1]["code"]
        return base == old or (
            base == expected
            and current.get("initialStandardInput") == "Rita 4"
            and "projectFiles" not in current
            and "sourceFiles" not in current
        )
    return base == old


def preserves_previous_exercise(relative, pn, bn, old, current):
    if current == old:
        return True
    lesson_path = Path(relative).relative_to(CONTENT.relative_to(ROOT)).as_posix()
    question = QUESTION_CLARIFICATIONS.get((lesson_path, pn + 1, bn + 1))
    if (
        question is not None
        and old["type"] == "fill_code"
        and current == dict(old, question=question)
    ):
        return True
    if (
        lesson_path == "datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json"
        and (pn, bn) == (1, 1)
        and old["type"] == "output_prediction"
    ):
        # Sólo p2 b2 retira estas definiciones aún no explicadas y no usadas.
        # El resto de la fuente y todos los demás campos deben coincidir con HEAD.
        unused_operators = (
            "bool operator==(const Pedido& a, const Pedido& b) { return mismo_valor(a, b); }\n"
            "bool operator!=(const Pedido& a, const Pedido& b) { return !(a == b); }\n"
        )
        return (
            old["code"].count(unused_operators) == 1
            and current == dict(old, code=old["code"].replace(unused_operators, "", 1))
        )
    return False


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
                if kind == "cpp_compiler":
                    validate_cpp_compiler(block, where)
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
                    if kind == "cpp_compiler":
                        require(preserves_previous_compiler(relative, pn, bn, block, current, old), relative, f"código legacy previo modificado p{pn+1} b{bn+1}")
                    elif kind != "multiple_choice":
                        require(preserves_previous_exercise(relative, pn, bn, block, current), relative, f"código/respuesta previa modificada p{pn+1} b{bn+1}")

    if ERRORS:
        print("\n".join(ERRORS))
        raise SystemExit(1)
    print(f"Contrato, referencias y posiciones previas correctos: {len(lessons)} lecciones, {blocks} bloques. No certifica semántica C++ ni ejecución Android.")


if __name__ == "__main__":
    main()

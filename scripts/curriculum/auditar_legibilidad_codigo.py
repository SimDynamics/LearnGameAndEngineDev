"""Audita los saltos FISICOS de codigo en todos los cursos y traducciones.

El reporte prioriza separaciones semanticas (no una linea entre instrucciones).
No reescribe JSON, codigo fuente, respuestas, proyectos, borradores ni estado.
Antes de corregir una fuente historica, Work debe acreditar la transicion de
stateLayout con la capacidad de compatibilidad implementada por Codex.

Uso: python scripts/curriculum/auditar_legibilidad_codigo.py
     python scripts/curriculum/auditar_legibilidad_codigo.py --output-json /tmp/codigo.json
"""
from __future__ import annotations

import argparse
import json
from collections import Counter
from hashlib import sha256
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CONTENT = Path("app/src/main/assets/content")
SOURCE_FIELDS = ("code", "initialCode", "template")
PROJECT_FIELDS = ("initialCode", "code", "content")
DECLARATION = re.compile(
    r"^(?:(?:const|constexpr|static|unsigned|signed|int|double|float|bool|char|long|short|auto|size_t)\b"
    r"|std::(?:string|vector|array|optional|size_t)\b|(?:[A-Za-z_]\w*::)*[A-Z][\w:<>]*\s+\w+)"
)
CONTROL = re.compile(r"^(?:if|for|while|switch)\s*\(")
FUNCTION = re.compile(r"^(?:(?:int|void|bool|double|float|auto|std::\w+)\s+\w+\s*\(|(?:class|struct|enum)\s+\w+)")
OUTPUT = re.compile(r"^(?:std::(?:cout|cerr|clog)\b|printf\s*\(|fprintf\s*\(|return\b)")
INPUT = re.compile(r"^(?:std::cin\b|scanf\s*\(|std::getline\s*\()")
CMAKE_SECTION = re.compile(r"^(?:add_executable|add_library|add_subdirectory|target_\w+|set_target_properties|install|enable_testing|add_test)\s*\(", re.I)


def source_blocks(block: dict):
    """Devuelve solo FUENTES autenticas, nunca salidas ni textos explicativos."""
    for field in SOURCE_FIELDS:
        source = block.get(field)
        if isinstance(source, str) and source.strip():
            yield field, source, "source"
    if block.get("type") == "order_code":
        fragments, order = block.get("fragments"), block.get("correctOrder")
        if (isinstance(fragments, list) and all(isinstance(x, str) for x in fragments)
                and isinstance(order, list) and len(order) == len(fragments)
                and all(type(i) is int for i in order) and sorted(order) == list(range(len(order)))):
            # Solo vista compuesta: las tarjetas evaluadas JAMAS se reescriben.
            yield "fragments/correctOrder", "\n".join(fragments[i] for i in order), "composition"
    project = block.get("projectFiles")
    if isinstance(project, list):
        for index, file in enumerate(project):
            if not isinstance(file, dict):
                continue
            filename = file.get("path") if isinstance(file.get("path"), str) else str(index)
            for field in PROJECT_FIELDS:
                source = file.get(field)
                if isinstance(source, str) and source.strip():
                    yield f"projectFiles/{index}:{filename}/{field}", source, "source"


def code_language(field: str, block: dict) -> str:
    if field.startswith("projectFiles/"):
        name = field.split(":", 1)[-1].rsplit("/", 1)[0].split("/")[-1].lower()
        if name == "cmakelists.txt" or name.endswith(".cmake"):
            return "cmake"
        if name.endswith((".cpp", ".cxx", ".cc", ".h", ".hpp", ".c")):
            return "cpp"
    value = block.get("language", "cpp")
    return "cmake" if isinstance(value, str) and value.lower() == "cmake" else "cpp"


def suggestions(source: str, language: str) -> list[dict]:
    """Sugiere FRONTERAS entre renglones; jamas modifica ni normaliza el texto."""
    # splitlines() preserva las lineas verdaderas, no el escape escrito '\\n'.
    lines = source.splitlines()
    if len(lines) < (3 if language == "cmake" else 5):
        return []
    candidate = []
    for index in range(1, len(lines)):
        before, current = lines[index - 1].strip(), lines[index].strip()
        if not before or not current:
            continue
        # No autorizar una frontera dentro de preprocesado continuado, macros,
        # comentarios multilínea, cadenas crudas o plantillas de huecos.
        if before.endswith("\\") or before.startswith(("//", "*", "/*")) or current.startswith(("*", "*/")):
            continue
        reason = ""
        if language == "cmake":
            if before.startswith("project(") and CMAKE_SECTION.match(current):
                reason = "configuracion_y_objetivos"
            elif before.startswith(("add_library(", "add_executable(")) and current.startswith("target_"):
                reason = "objetivos_y_propiedades"
        else:
            if before.startswith("#include") and not current.startswith(("#include", "#")):
                reason = "cabeceras_y_codigo"
            elif before == "}" and FUNCTION.match(current):
                reason = "funciones_o_tipos"
            elif before == "}" and current.startswith("return "):
                reason = "decision_y_retorno"
            elif before.endswith(";") and CONTROL.match(current):
                # Separar la preparacion de una decision solo si contiene
                # al menos dos declaraciones/cálculos anteriores contiguos.
                span = [line.strip() for line in lines[max(0, index - 5):index]]
                run = 0
                for statement in reversed(span):
                    if statement and statement.endswith(";") and (DECLARATION.match(statement) or "=" in statement):
                        run += 1
                    else:
                        break
                if run >= 2:
                    reason = "preparacion_y_decision"
            elif before.endswith(";") and INPUT.match(current):
                prior = [line.strip() for line in lines[max(0, index - 4):index]]
                if sum(bool(DECLARATION.match(line)) for line in prior) >= 2:
                    reason = "declaraciones_y_lectura"
            elif before.startswith("std::cin") and DECLARATION.match(current):
                reason = "lectura_y_calculo"
            elif before.endswith(";") and OUTPUT.match(current) and DECLARATION.match(before):
                # Evita el falso positivo en declaraciones seguidas de return,
                # salvo si hay dos lineas de preparacion consecutivas.
                if index >= 2 and (DECLARATION.match(lines[index - 2].strip())
                                    or lines[index - 2].strip().startswith("std::cin")):
                    reason = "calculos_y_salida"
        if reason:
            candidate.append({"beforeLine": index, "afterLine": index + 1, "reason": reason,
                              "before": before[:100], "after": current[:100]})
    return candidate


def audit(content_root: Path) -> dict:
    """Inspecciona todos los idiomas/cursos, no solo los ejemplos reportados."""
    content_root = Path(content_root)
    rows, counts, languages, reasons = [], Counter(), Counter(), Counter()
    for file in sorted(content_root.glob("**/lesson_*.json")):
        relative = file.relative_to(content_root)
        if len(relative.parts) < 4:
            continue
        locale, area = relative.parts[:2]
        if locale not in ("es", "en", "fr"):
            continue
        doc = json.loads(file.read_text(encoding="utf-8-sig"))
        if not isinstance(doc, dict) or not isinstance(doc.get("pages"), list):
            continue
        counts["lessons"] += 1
        counts[f"lessons.{locale}.{area}"] += 1
        state = doc.get("stateLayout", {})
        current = state.get("current", []) if isinstance(state, dict) else []
        for pi, page in enumerate(doc["pages"]):
            for bi, block in enumerate(page.get("blocks", [])):
                if not isinstance(block, dict):
                    continue
                stable = (current[pi].get("blocks", [])[bi]
                          if pi < len(current) and bi < len(current[pi].get("blocks", [])) else None)
                for field, source, kind in source_blocks(block):
                    counts["sources"] += 1
                    counts[f"sources.{locale}.{area}"] += 1
                    languages[code_language(field, block)] += 1
                    observations = suggestions(source, code_language(field, block))
                    if not observations:
                        continue
                    for item in observations:
                        reasons[item["reason"]] += 1
                    counts["sourcesWithSuggestions"] += 1
                    rows.append({"lesson":relative.as_posix(), "locale":locale,
                                 "course": relative.parts[2] if len(relative.parts) > 2 else area,
                                 "page":pi+1, "block":bi+1,
                                 "owner": stable, "type":block.get("type"),
                                 "field":field, "mode":kind,
                                 "protectedByStateLayout":bool(stable),
                                 "sha256":sha256(source.encode("utf-8")).hexdigest(),
                                 "suggestions":observations})
    return {"scope":"ALL_LOCALES_ALL_LESSONS_READ_ONLY",
            "changesMade":False,"stateTransitionsVerified":False,
            "counts":dict(sorted(counts.items())),
            "languages":dict(languages),"reasons":dict(reasons),
            "findings":rows}


def main(argv: list[str] | None = None) -> int:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--root",type=Path,default=ROOT)
    cli.add_argument("--output-json",type=Path)
    cli.add_argument("--limit",type=int,default=12)
    cli.add_argument("--fail-on-findings",action="store_true",
                     help="Activar solo cuando las revisiones estén acreditadas")
    args = cli.parse_args(argv)
    result = audit(args.root / CONTENT)
    print(f"Lecciones: {result['counts'].get('lessons',0)}; fuentes: "
          f"{result['counts'].get('sources',0)}; con separaciones por revisar: "
          f"{result['counts'].get('sourcesWithSuggestions',0)}")
    print("Ubicaciones por idioma/curso:",json.dumps({k:v for k,v in result["counts"].items()
          if k.startswith("lessons.")},ensure_ascii=False,sort_keys=True))
    print("Motivos:",json.dumps(result["reasons"],ensure_ascii=False,sort_keys=True))
    for issue in result["findings"][:max(0,args.limit)]:
        print(f"{issue['lesson']} p{issue['page']} b{issue['block']} "
              f"{issue['field']} {issue['suggestions'][0]['reason']}")
    if args.output_json:
        args.output_json.parent.mkdir(parents=True,exist_ok=True)
        args.output_json.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    return 1 if args.fail_on_findings and result["findings"] else 0


if __name__ == "__main__":
    raise SystemExit(main())

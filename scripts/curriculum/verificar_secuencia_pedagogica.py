"""Revisa señales comprobables del recorrido de las lecciones en español.

La comprobación usa el orden real página/bloque. Son errores una orden explícita
de volver al editor de una página anterior y una predicción que se exige antes
de ejecutar el mismo programa, pero se encuentra después de su editor. También
se comprueba la remisión explícita a un puzzle posterior antes de ejecutar un
editor interpuesto. No se interpreta una mención conceptual al pasado como una
orden de navegación, ni un reintento como una actividad nueva.

Los posibles objetivos múltiples, los ejercicios de diagnóstico y los editores
sin contrato producen avisos para revisión humana. No se infieren salidas de la
prosa, las soluciones ni los puzzles. Ausencia de errores no certifica claridad,
prerrequisitos, práctica, corrección C++, ejecución Android ni calidad pedagógica.
Este control complementa la lectura completa y no modifica ningún archivo.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es"
PASSIVE = {"heading", "paragraph", "note", "code"}
BACK_TO_EDITOR = re.compile(
    r"\b(?:vuelve|regresa|retorna)\s+(?:al|a\s+este|a\s+ese)\s+editor"
    r"(?:\s+de\s+código)?\s+(?:de\s+)?(?:la\s+)?página\s+anterior\b",
    re.I,
)
BEFORE_RUNNING = re.compile(r"\bantes\s+de\s+(?:compilar(?:\s+y\s+ejecutar)?|ejecutar)\b", re.I)
ANSWER_ACTION = re.compile(r"\b(?:responde|resuelve|completa|registra|elige|selecciona)\b", re.I)
LATER_PUZZLE = re.compile(
    r"\b(?:puzzle|predicción|ejercicio)\b[^.\n]{0,100}"
    r"\b(?:al\s+final|del\s+final|debajo\s+del\s+editor)\b", re.I,
)
ANOTHER_VERSION = re.compile(
    r"\b(?:en\s+una\s+segunda\s+versión|en\s+el\s+mismo\s+editor|"
    r"después\s+cambia|a\s+continuación,?\s+modifica|"
    r"después,?\s+cambia\s+únicamente)\b", re.I,
)
DIAGNOSTIC = re.compile(r"\b(?:diagnóstico|diagnósticos|error\s+de\s+compilación|no\s+compila)\b", re.I)


@dataclass(frozen=True)
class Finding:
    severity: str
    code: str
    lesson: str
    page: int
    block: int
    message: str
    related_page: int | None = None
    related_block: int | None = None


def public_text(block: dict) -> str:
    """Only authored instructions; hidden answers and feedback are not a task order."""
    if block.get("type") in {"heading", "paragraph", "note"}:
        return str(block.get("text", ""))
    return str(block.get("question", block.get("prompt", "")))


def normalized_source(value: str) -> str:
    # No removal of comments, tokens, spaces inside literals or semantic changes.
    return value.replace("\r\n", "\n").strip("\n")


def review_lesson(document: dict, label: str = "lesson") -> list[Finding]:
    entries = [(pn, bn, block) for pn, page in enumerate(document.get("pages", []), 1)
               for bn, block in enumerate(page.get("blocks", []), 1)]
    findings: list[Finding] = []
    compilers: list[tuple[int, int, dict]] = []

    def add(severity: str, code: str, entry: tuple, message: str, related: tuple | None = None):
        findings.append(Finding(severity, code, label, entry[0], entry[1], message,
                                related[0] if related else None, related[1] if related else None))

    for index, entry in enumerate(entries):
        pn, bn, block = entry
        text = public_text(block)
        if BACK_TO_EDITOR.search(text):
            add("ERROR", "REGRESO_EDITOR_ANTERIOR", entry,
                "La consigna obliga a volver al editor de una página anterior; revisar el recorrido y conservar sus claves de estado.")

        if block.get("type") == "output_prediction" and BEFORE_RUNNING.search(text) and "editor" in text.lower():
            source = normalized_source(block.get("code", ""))
            previous = next((item for item in reversed(compilers)
                             if source and normalized_source(item[2].get("initialCode", "")) == source), None)
            if previous:
                add("ERROR", "PREDICCION_DESPUES_EDITOR", entry,
                    "La predicción se exige antes de ejecutar el mismo programa, pero su editor aparece primero.", previous)

        if block.get("type") in {"paragraph", "note"}:
            # A clause must explicitly require the later response before running.
            # Ordinary mentions of a future exercise do not establish that dependency.
            for clause in re.split(r"(?<=[.!?])\s+|\n\n", text):
                if not (BEFORE_RUNNING.search(clause) and ANSWER_ACTION.search(clause) and LATER_PUZZLE.search(clause)):
                    continue
                following = entries[index + 1:]
                editor = next((item for item in following if item[0] == pn and item[2].get("type") == "cpp_compiler"), None)
                puzzle = next((item for item in following if item[0] == pn and item[2].get("type") not in PASSIVE | {"cpp_compiler"}), None)
                if editor and puzzle and (editor[0], editor[1]) < (puzzle[0], puzzle[1]):
                    add("ERROR", "RESPUESTA_POSTERIOR_ANTES_EDITOR", entry,
                        "Se exige resolver un puzzle posterior antes de ejecutar un editor interpuesto.", puzzle)
                    break

        if block.get("type") != "cpp_compiler":
            continue
        compilers.append(entry)
        preceding: list[str] = []
        for candidate in reversed(entries[:index]):
            if candidate[0] != pn or candidate[2].get("type") not in {"paragraph", "note"}:
                break
            preceding.append(public_text(candidate[2]))
        instructions = "\n\n".join(reversed(preceding))
        if ANOTHER_VERSION.search(instructions):
            add("AVISO", "POSIBLES_OBJETIVOS_MULTIPLES", entry,
                "La consigna puede encadenar versiones en un editor: distinguir una actividad nueva de un reintento del mismo objetivo.")
        if DIAGNOSTIC.search(instructions):
            add("AVISO", "OBJETIVO_DIAGNOSTICO", entry,
                "Verificar el objetivo observable de diagnóstico; un fallo técnico no debe convertirse en una salida incorrecta ficticia.")
        if "outputCheck" not in block:
            add("AVISO", "EDITOR_SIN_CONTRATO", entry,
                "Revisar si el objetivo requiere comprobar stdout o diagnosticar/explorar; el esperado no se infiere automáticamente.")
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--content", type=Path, default=CONTENT,
                        help="Carpeta ES o lección JSON que se revisará sin modificaciones.")
    parser.add_argument("--json", action="store_true", help="Imprimir resultados estructurados.")
    args = parser.parse_args(argv)
    paths = [args.content] if args.content.is_file() else sorted(args.content.rglob("lesson_*.json"))
    if not paths:
        parser.error("No se encontraron lecciones para revisar.")
    findings: list[Finding] = []
    for path in paths:
        label = path.name if args.content.is_file() else path.relative_to(args.content).as_posix()
        findings.extend(review_lesson(json.loads(path.read_text(encoding="utf-8-sig")), label))
    errors = sum(item.severity == "ERROR" for item in findings)
    warnings = len(findings) - errors
    if args.json:
        print(json.dumps({"lessons": len(paths), "errors": errors, "warnings": warnings,
                          "findings": [asdict(item) for item in findings]}, ensure_ascii=False, indent=2))
    else:
        for item in findings:
            related = f" -> p{item.related_page} b{item.related_block}" if item.related_page else ""
            print(f"{item.severity} {item.lesson} p{item.page} b{item.block}{related} [{item.code}] {item.message}")
        print(f"Recorrido revisado: {len(paths)} lecciones; {errors} errores y {warnings} avisos. "
              "Requiere además revisión pedagógica, semántica y aceptación Android.")
    return int(errors > 0)


if __name__ == "__main__":
    raise SystemExit(main())

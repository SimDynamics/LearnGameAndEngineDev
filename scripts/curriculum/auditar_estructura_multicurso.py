"""Auditoría de SOLO ESTRUCTURA para los cursos españoles de Programación.

Utiliza los contratos de la validación habitual, incluidos los bloques,
los casos de compilación y los índices. No autentica stateLayout ni las
transiciones históricas. No sustituye validar_json_programacion.py.
No modifica el árbol de contenido.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import validar_json_programacion as contract
from validate_programming_practice_tools import validate


COURSES = ("cpp_fundamentals", "programming_practice_tools")


def inspect(root: Path, courses: tuple[str, ...] = COURSES) -> dict:
    """Compare real structural assets with the shared contracts, without writes."""
    if not courses or len(set(courses)) != len(courses) or any(
            course not in COURSES for course in courses):
        raise ValueError("La selección de cursos estructurales es inválida")

    root = root.resolve()
    previous_root, previous_content, previous_errors = (
        contract.ROOT, contract.CONTENT, contract.ERRORS)
    rows, issues = [], []
    try:
        # Validators are imported from the original module, and their error
        # collector is scoped to this invocation. Restore everything below.
        contract.ROOT = root
        contract.CONTENT = root / "app/src/main/assets/content/es/programming/cpp_fundamentals"
        contract.ERRORS = []
        helpers = SimpleNamespace(**vars(contract))
        for course in courses:
            start = len(contract.ERRORS)
            row = {"course": course, "lessons": 0, "units": 0, "blocks": 0,
                   "files": [], "errors": 0}
            try:
                result = validate(helpers, root, None, course_id=course,
                                  structure_only=True)
                if (result["scope"] != "STRUCTURE_ONLY_NOT_APPROVED"
                        or result["stateLayoutValidated"]
                        or result["historicalProtectionChecked"]
                        or result["historical"] != 0):
                    raise ValueError("El auditor estructural obtuvo una autorización indebida")
                row.update({key: result[key] for key in ("lessons", "units", "blocks", "files")})
            except (ValueError, KeyError, TypeError, IndexError, OSError) as error:
                issues.append({"course": course, "message": str(error),
                               "kind": type(error).__name__})
                row["errors"] += 1
            new_errors = contract.ERRORS[start:]
            for message in new_errors:
                issues.append({"course": course, "message": message, "kind": "Contract"})
            row["errors"] += len(new_errors)
            rows.append(row)
    finally:
        contract.ROOT, contract.CONTENT, contract.ERRORS = (
            previous_root, previous_content, previous_errors)
    return {
        "scope": "STRUCTURE_ONLY_NOT_APPROVED",
        "historicalOriginVerified": False,
        "publishedTransitionsVerified": False,
        "compilerProgramsExecuted": False,
        "changesMadeToCurriculum": False,
        "courses": rows,
        "issues": issues,
        "errors": len(issues),
        "lessons": sum(row["lessons"] for row in rows),
        "blocks": sum(row["blocks"] for row in rows),
        "files": sum(len(row["files"]) for row in rows),
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--course", choices=("all", *COURSES), default="all")
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    result = inspect(root, COURSES if args.course == "all" else (args.course,))
    for row in result["courses"]:
        print(f"ESTRUCTURA {row['course']}: {row['lessons']} lecciones, "
              f"{row['units']} unidades, {row['blocks']} bloques, "
              f"{len(row['files'])} JSON; {row['errors']} incidencias")
    for issue in result["issues"]:
        print(f"ERROR [{issue['course']}] {issue['message']}")
    print("SOLO ESTRUCTURA: NO certifica mapas históricos, familias de idiomas, "
          "transiciones, publicación Git ni ejecución C++.")
    if args.output_json is not None:
        destination = args.output_json.resolve()
        content_root = (root / "app/src/main/assets/content").resolve()
        if (destination == content_root or content_root in destination.parents
                or destination == root / ".git" or
                (root / ".git").resolve() in destination.parents):
            parser.error("El informe no puede sobrescribir assets ni metadatos Git")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())

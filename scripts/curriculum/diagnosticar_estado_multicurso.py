"""Diagnóstico exhaustivo por lección de mapas de estado de Programación 1/2.

SOLO DIAGNÓSTICO: verifica cada JSON visible con el validador de producción,
pero NO sustituye check_repository ni autoriza publicar o corregir metadatos.
Nunca reescribe lecciones, índices, respuestas ni mapas de origen.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import stable_state_layout as layout


COURSES = ("cpp_fundamentals", "programming_practice_tools")
RELATIVE = Path("app/src/main/assets/content/es/programming")


def inspect(root: Path, courses: tuple[str, ...] = COURSES) -> dict:
    if not courses or any(course not in COURSES for course in courses):
        raise ValueError("Curso de diagnóstico desconocido")
    root = root.resolve()
    report = {"scope": "diagnostic-only; not a full repository or publication approval",
              "courses": [], "issues": [], "checked": 0,
              "globallyValidated": False, "filesChanged": False}
    for course in courses:
        directory = root / RELATIVE / course
        files = sorted(directory.glob("*/lesson_*.json")) if directory.is_dir() else []
        record = {"course": course, "lessonsChecked": len(files),
                  "errors": 0, "paths": []}
        if not files:
            report["issues"].append(
                {"course": course, "path": directory.relative_to(root).as_posix(),
                 "category": "missing-content",
                 "message": "El curso no contiene lecciones verificables"})
            record["errors"] += 1
        for path in files:
            relative = path.relative_to(root).as_posix()
            report["checked"] += 1
            record["paths"].append(relative)
            try:
                document = layout.load(path)
                layout.validate(document)
            except (ValueError, TypeError, KeyError, IndexError, UnicodeError, OSError) as error:
                record["errors"] += 1
                report["issues"].append({
                    "course": course, "path": relative,
                    "category": "state-layout-or-text-semantics",
                    "exception": type(error).__name__,
                    "message": str(error),
                })
        report["courses"].append(record)
    report["errors"] = len(report["issues"])
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2],
                        help="Raíz del checkout, únicamente para lectura")
    parser.add_argument("--course", choices=("all", *COURSES), default="all")
    parser.add_argument("--output-json", type=Path,
                        help="Ruta explícita de salida diagnóstica JSON, nunca un JSON curricular")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    chosen = COURSES if args.course == "all" else (args.course,)
    result = inspect(root, chosen)

    for course in result["courses"]:
        print(f"DIAGNÓSTICO {course['course']}: "
              f"{course['lessonsChecked']} lecciones examinadas, "
              f"{course['errors']} con problemas")
    for issue in result["issues"]:
        print(f"ERROR [{issue['course']}] {issue['path']}: {issue['message']}")
    print("ALCANCE: diagnóstico por archivo, sin certificar el repositorio global, "
          "los contratos históricos completos, C++ ni la ejecución de la aplicación.")
    if args.output_json is not None:
        destination = args.output_json.resolve()
        content_tree = (root / "app/src/main/assets/content").resolve()
        if destination == content_tree or content_tree in destination.parents:
            parser.error("El informe nunca puede escribirse dentro de los assets curriculares")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8")
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())

"""Compile and execute the four *curricular* AssetCheck C++23 references.

Reads the original editor and the after-attempt reference code from the lesson
JSON, builds the stitched source with CMake in an isolated temporary directory,
and checks every declared stdin/stdout pair. This does NOT compile or certify
the separate AssetCheck project, its libraries or its two CLI clients.
"""

from __future__ import annotations

import argparse
import json
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTENT = (
    ROOT
    / "app/src/main/assets/content/es/programming/"
    / "programming_practice_tools/proyecto_assetcheck"
)

# Function and marker are semantic anchors, not fixed page/array offsets.
LESSONS = (
    ("lesson_leer_y_validar_un_registro.json", "tamano_valido", "\nint main()"),
    ("lesson_procesar_un_lote_y_detectar_duplicados.json", "procesar_linea", "\nint main()"),
    ("lesson_leer_manifiesto_y_publicar_informe.json", "contar_lineas", "\nstd::string datos("),
    ("lesson_disenar_probar_y_presentar_variante_propia.json", "contar_tipos", "\nint main()"),
)


def run_command(args: list[str], *, cwd: Path | None = None) -> str:
    print("+", " ".join(args), flush=True)
    result = subprocess.run(
        args, cwd=cwd, capture_output=True, text=True, encoding="utf-8",
        errors="replace", timeout=240, check=False
    )
    if result.stdout:
        print(result.stdout, flush=True)
    if result.stderr:
        print(result.stderr, file=sys.stderr, flush=True)
    if result.returncode:
        raise RuntimeError(f"Command exited with {result.returncode}: {args[0]}")
    return result.stdout


def id_for_block(page: int, index: int) -> str:
    return f"block_{page:04d}_{index:04d}"


def lesson_reference(filename: str, function: str, end_marker: str):
    lesson = json.loads((CONTENT / filename).read_text(encoding="utf-8"))
    blocks = {
        id_for_block(p, i): block
        for p, page in enumerate(lesson["pages"])
        for i, block in enumerate(page["blocks"])
    }
    editors = [
        (ident, block)
        for ident, block in blocks.items()
        if block["type"] == "cpp_compiler"
    ]
    if len(editors) != 1:
        raise ValueError(f"{filename}: expected exactly one C++ editor")
    editor_id, editor = editors[0]
    reference_groups = [
        entry for entry in lesson.get("referenceAfter", [])
        if entry["activity"] == editor_id
    ]
    if len(reference_groups) != 1:
        raise ValueError(f"{filename}: missing unique after-attempt reference")
    solutions = [
        blocks[ident]["code"]
        for ident in reference_groups[0]["blocks"]
        if blocks[ident]["type"] == "code" and
        blocks[ident].get("language") == "cpp"
    ]
    if len(solutions) != 1:
        raise ValueError(f"{filename}: expected one C++ solution")
    initial = editor["initialCode"]
    solution = solutions[0].rstrip("\n")
    if function == "procesar_linea":
        # U4L5 gives just the missing loop and admission, not a whole function.
        anchor = "    // TODO: busca un ID idéntico"
        if initial.count(anchor) != 1:
            raise ValueError(f"{filename}: cannot find the original TODO")
        start = initial.index(anchor)
        end = initial.index("\n}\n\nint main()", start)
        source = initial[:start] + "\n".join(
            ("    " + line if line else line)
            for line in solution.splitlines()
        ) + initial[end:]
    else:
        # The other three references contain complete function definitions.
        return_type = {
            "tamano_valido": "bool",
            "contar_lineas": "Conteo",
            "contar_tipos": "Totales",
        }[function]
        starts = list(re.finditer(
            r"(?m)^" + return_type + r"\s+" + re.escape(function) + r"\s*\(",
            initial,
        ))
        if len(starts) != 1:
            raise ValueError(f"{filename}: expected one function declaration")
        line_start = starts[0].start()
        end = initial.index(end_marker, starts[0].end())
        if not solution.startswith(
            {"tamano_valido": "bool ", "contar_lineas": "Conteo ",
             "contar_tipos": "Totales "}[function] + function + "("
        ):
            raise ValueError(f"{filename}: reference is not the expected function")
        source = initial[:line_start] + solution + "\n" + initial[end:]

    check = editor["outputCheck"]
    if check["inputComparison"] != "exact" or len(check["cases"]) < 4:
        raise ValueError(f"{filename}: expected at least four exact-output cases")
    if any(len(case.get("outputs", [])) != 1 for case in check["cases"]):
        raise ValueError(f"{filename}: only independently checked output is allowed")
    return source, check["cases"]


def run_all(generator: str, report: Path | None) -> None:
    with tempfile.TemporaryDirectory(prefix="assetcheck-editor-cpp23-") as temp:
        source_dir = Path(temp) / "source"
        build_dir = Path(temp) / "build"
        source_dir.mkdir()
        cmake = [
            "cmake_minimum_required(VERSION 3.25)",
            "project(AssetCheckCurricularReference LANGUAGES CXX)",
            "set(CMAKE_CXX_STANDARD 23)",
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)",
            "set(CMAKE_CXX_EXTENSIONS OFF)",
            'if(WIN32 AND NOT MSVC)',
            '    message(FATAL_ERROR "The Windows job must use MSVC")',
            "endif()",
        ]
        targets = []
        for index, (filename, function, end_marker) in enumerate(LESSONS, start=4):
            target = f"assetcheck_u4l{index}"
            code, cases = lesson_reference(filename, function, end_marker)
            (source_dir / f"{target}.cpp").write_text(code, encoding="utf-8")
            cmake.append(f"add_executable({target} {target}.cpp)")
            targets.append((target, filename, cases))
        (source_dir / "CMakeLists.txt").write_text(
            "\n".join(cmake) + "\n", encoding="utf-8"
        )
        run_command(["cmake", "--version"])
        configure = [
            "cmake", "-S", str(source_dir), "-B", str(build_dir),
            "-G", generator,
        ]
        multi_config = generator.startswith("Visual Studio") or generator == "Ninja Multi-Config"
        if generator.startswith("Visual Studio"):
            configure += ["-A", "x64"]
        if not multi_config:
            configure += ["-DCMAKE_BUILD_TYPE=Release"]
        run_command(configure)
        run_command(["cmake", "--build", str(build_dir), "--config", "Release", "--parallel", "2"])

        checked = 0
        for target, filename, cases in targets:
            exe = (build_dir / ("Release" if multi_config else "") /
                   (target + (".exe" if platform.system() == "Windows" else "")))
            if not exe.is_file():
                raise FileNotFoundError(f"{filename}: no executable at {exe}")
            for number, case in enumerate(cases, start=1):
                result = subprocess.run(
                    [str(exe)], input=case["standardInput"],
                    capture_output=True, text=True, encoding="utf-8",
                    errors="replace", timeout=10, check=False,
                )
                expected = case["outputs"][0]
                if result.returncode != 0 or result.stderr or result.stdout != expected:
                    raise AssertionError(
                        f"{filename}, case {number}\n"
                        f"return code: {result.returncode}\n"
                        f"stderr: {result.stderr!r}\n"
                        f"expected stdout: {expected!r}\n"
                        f"actual stdout: {result.stdout!r}"
                    )
                print(f"PASS {target}, curricular case {number}", flush=True)
                checked += 1
        summary = {
            "scope": "Curricular after-attempt C++ references only; NOT standalone AssetCheck",
            "system": platform.platform(),
            "generator": generator,
            "standard": "C++23; extensions disabled; CMake required",
            "reference_executables": len(targets),
            "output_cases_passed": checked,
        }
        print("SUMMARY " + json.dumps(summary, ensure_ascii=False), flush=True)
        if report is not None:
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text(
                json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        run_all(args.generator, args.report)
    except (OSError, RuntimeError, ValueError, AssertionError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

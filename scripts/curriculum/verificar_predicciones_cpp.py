"""Verifica predicciones indexadas de C++ en escritorio, conservando QA anterior.

Por defecto sólo inventaría fuentes y evidencia; --execute autoriza el lote.
La comparación de stdout se registra por separado de compile/link/run y nunca
acredita Android. Cada ejecución nueva recibe un directorio vacío exclusivo.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
QA_ROOT = ROOT / ".gradle/cpp-content-qa"
APP_EVIDENCE = ROOT.parent / "LearnGraphicsProgramming-App/.gradle/cpp-content-qa/respuestas-tecnicas"
FLAGS = ["-std=c++23", "-Wall", "-Wextra", "-Wpedantic"]
DIAGNOSTIC_ONLY_FLAGS = {"-Werror", "-Wconversion"}
INPUT_LESSON = "objetos_y_estado/lesson_leer_una_entrada_conocida.json"
# Datos explícitos de las consignas; no se interpretan preguntas automáticamente.
INPUTS = {
    (INPUT_LESSON, 2, 3): "12",
    (INPUT_LESSON, 3, 3): "2.5",
    (INPUT_LESSON, 4, 3): "Ana Maria",
    (INPUT_LESSON, 4, 4): "   Norte  6",
    (INPUT_LESSON, 5, 3): "4B",
    (INPUT_LESSON, 5, 4): "Lia\n5",
    (INPUT_LESSON, 8, 5): "6",
    (INPUT_LESSON, 12, 2): "Kiro 3\n1",
}
EXPECTED_NONZERO_EXITS = {
    ("archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json", 3, 2): 1,
}
INTERFACES_LESSON = "funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json"
ESTANTE_LESSON = "funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json"
# Fragmentos completos publicados; nunca los esqueletos de cpp_compiler.
PROJECT_DEPENDENCIES = {
    (INTERFACES_LESSON, 7, 2): [("sobres.h", 2, 3), ("sobres.cpp", 3, 3)],
    (INTERFACES_LESSON, 11, 2): [("serie.h", 10, 3), ("serie.cpp", 10, 4)],
    (ESTANTE_LESSON, 8, 3): [("estante.h", 6, 3), ("conteo.cpp", 6, 5), ("estado.cpp", 7, 3)],
    (ESTANTE_LESSON, 8, 4): [("estante.h", 6, 3), ("conteo.cpp", 6, 5), ("estado.cpp", 7, 3)],
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def serialized(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def inside(path: Path, root: Path) -> bool:
    return path.resolve().is_relative_to(root.resolve())


def atomic_json(path: Path, value: object) -> None:
    # Sólo se reemplaza el índice de esta corrida nueva; no evidencia histórica.
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("xb") as stream:
        stream.write(serialized(value))
    os.replace(temporary, path)


def logical_stdout(raw: bytes) -> str:
    # Transporte del runtime MinGW: conservar raw; no eliminar CR aislados.
    return raw.replace(b"\r\n", b"\n").decode("utf-8", errors="strict")


def trimmed(value: str) -> str:
    # Kotlin Char.isWhitespace combina Unicode whitespace y space characters.
    # Python añade NEL U+0085, que se excluye para no ampliar el contrato.
    def whitespace(char: str) -> bool:
        return char.isspace() and char != "\u0085"
    start, end = 0, len(value)
    while start < end and whitespace(value[start]):
        start += 1
    while end > start and whitespace(value[end - 1]):
        end -= 1
    return value[start:end]


def comparison(raw: bytes, answers: list[str], mode: str) -> dict:
    try:
        output = logical_stdout(raw)
    except UnicodeDecodeError as error:
        return {"matched": False, "mode": mode, "decode_error": str(error),
                "raw_sha256": sha(raw), "raw_hex": raw.hex()}
    normalize = (lambda text: text) if mode == "exact" else trimmed
    matches = [index for index, answer in enumerate(answers)
               if normalize(output) == normalize(answer)]
    return {"matched": bool(matches), "matching_answer_indices": matches,
            "mode": mode, "stdout_lf": output, "raw_sha256": sha(raw),
            "transport": "MinGW CRLF -> LF only; isolated CR and internal whitespace preserved"}


@dataclass(frozen=True)
class Case:
    origin: dict
    source: str
    source_sha256: str
    stdin: str
    answers: list[str]
    mode: str
    review: tuple[str, ...]
    file_names: tuple[str, ...]
    dependencies: tuple[dict, ...] = ()

    @property
    def location(self) -> tuple[str, int, int]:
        return self.origin["path"], self.origin["page"], self.origin["block"]

    @property
    def source_files(self) -> dict | None:
        if not self.dependencies:
            return None
        return {"source.cpp": self.source_sha256,
                **{file["name"]: file["source_sha256"] for file in self.dependencies}}


def project_fingerprint(source_files: dict | None) -> str | None:
    return sha(serialized(source_files)) if source_files else None


def compile_units(source_files: dict | None) -> list[tuple[str, str, str]]:
    names = ["source.cpp", *sorted(name for name in source_files or {}
                                  if name.endswith(".cpp") and name != "source.cpp")]
    return [("compile" if name == "source.cpp" else "compile_" + Path(name).stem,
             name, Path(name).stem + ".o") for name in names]


def source_review(source: str, explicit_input: bool) -> tuple[list[str], list[str]]:
    """Guardia conservadora, no sandbox: omite APIs o rutas no revisadas."""
    issues = []
    names = []
    if re.search(r"\b(?:system|popen|_popen|exec\w*|CreateFile\w*|DeleteFile\w*|ShellExecute\w*|remove|rename|chdir|_chdir)\s*\(", source):
        issues.append("API externa, modificación de cwd o de archivos requiere revisión manual")
    if "<filesystem>" in source:
        issues.append("filesystem requiere revisión manual de rutas y operaciones")
    if re.search(r'"(?:[A-Za-z]:[\\/]|/|\\\\|\.\.[\\/])', source):
        issues.append("Literal con ruta absoluta o ascenso requiere revisión manual")
    if re.search(r"\b(?:cin|stdin|scanf|getchar|gets|fopen|freopen)\b", source) and not explicit_input:
        # Sólo std::cin tiene datos autorizados. APIs C quedan para revisión.
        issues.append("Entrada externa no mapeada explícitamente")
    # Los cinco programas actuales abren archivos mediante literales relativos.
    for match in re.finditer(r'\b(?:ifstream|ofstream|fstream)\s+\w+\s*([({])\s*([^;\n]+)', source):
        literal = re.match(r'"([^"\\]*)"', match.group(2))
        if not literal:
            issues.append("Nombre calculado de archivo requiere revisión manual")
            continue
        name = literal.group(1)
        if not name or Path(name).name != name or any(mark in name for mark in ("/", "\\", ":", "..")):
            issues.append("Archivo fuera del cwd aislado requiere revisión manual")
        else:
            names.append(name)
    return sorted(set(issues)), sorted(set(names))


def inventory() -> tuple[list[Case], dict]:
    cases = []
    lesson_count = 0
    unit_counts = Counter()
    snapshot = {}
    course_path = CONTENT / "course_cpp_fundamentals.json"
    snapshot[str(course_path.relative_to(ROOT))] = sha(course_path.read_bytes())
    for unit_item in load_json(course_path)["units"]:
        unit = unit_item["id"].split("__")[-1]
        unit_path = CONTENT / unit / f"unit_{unit}.json"
        snapshot[str(unit_path.relative_to(ROOT))] = sha(unit_path.read_bytes())
        for lesson_item in load_json(unit_path)["lessons"]:
            filename = "lesson_" + lesson_item["id"].split("__")[-1] + ".json"
            path = CONTENT / unit / filename
            lesson = load_json(path)
            if lesson["id"] != lesson_item["id"]:
                raise ValueError(f"ID distinto del índice: {path}")
            snapshot[str(path.relative_to(ROOT))] = sha(path.read_bytes())
            lesson_count += 1
            for page, data in enumerate(lesson["pages"], 1):
                for block, item in enumerate(data["blocks"], 1):
                    if item["type"] != "output_prediction":
                        continue
                    relative = path.relative_to(CONTENT).as_posix()
                    location = relative, page, block
                    source = item["code"]
                    mode = item.get("outputComparison", "trimmed")
                    answers = item["answers"]
                    if mode not in {"exact", "trimmed"} or not answers or not all(isinstance(answer, str) for answer in answers):
                        raise ValueError(f"Contrato de respuestas inválido: {location}")
                    if not re.search(r"\bmain\s*\(", source):
                        raise ValueError(f"Fuente sin main: {location}")
                    review, file_names = source_review(source, location in INPUTS)
                    dependencies = []
                    for name, dependency_page, dependency_block in PROJECT_DEPENDENCIES.get(location, []):
                        dependency = lesson["pages"][dependency_page - 1]["blocks"][dependency_block - 1]
                        if dependency["type"] != "code" or not isinstance(dependency.get("code"), str):
                            raise ValueError(f"Dependencia publicada incompleta: {location}, {name}")
                        dependency_code = dependency["code"]
                        dependency_review, dependency_files = source_review(dependency_code, False)
                        review.extend(dependency_review)
                        file_names.extend(dependency_files)
                        dependencies.append({"name": name, "code": dependency_code,
                                             "source_sha256": sha(dependency_code.encode("utf-8")),
                                             "origin": {"path": relative, "page": dependency_page, "block": dependency_block}})
                    known_headers = {file["name"] for file in dependencies}
                    for program in [source, *[file["code"] for file in dependencies]]:
                        missing_headers = set(re.findall(r'#\s*include\s*"([^"]+)"', program)) - known_headers
                        if missing_headers:
                            review.append(f"Cabeceras publicadas sin contexto de proyecto: {sorted(missing_headers)}")
                    origin = {"path": relative, "lesson": lesson["id"], "page": page,
                              "block": block, "id": item.get("id"), "unit": unit}
                    cases.append(Case(origin, source, sha(source.encode("utf-8")),
                                      INPUTS.get(location, ""), answers, mode,
                                      tuple(sorted(set(review))), tuple(sorted(set(file_names))), tuple(dependencies)))
                    unit_counts[unit] += 1
    missing_inputs = set(INPUTS) - {case.location for case in cases}
    if missing_inputs:
        raise ValueError(f"Coordenadas de entrada ausentes: {missing_inputs}")
    return cases, {"indexed_lessons": lesson_count, "predictions": len(cases),
                   "by_unit": dict(unit_counts), "snapshot_sha256": snapshot}


def compiler_info(compiler: Path) -> dict:
    compiler = compiler.resolve(strict=True)
    result = subprocess.run([str(compiler), "--version"], capture_output=True,
                            timeout=15, check=True)
    return {"path": str(compiler), "sha256": sha(compiler.read_bytes()),
            "version": result.stdout.decode("utf-8", errors="replace").splitlines()[0]}


def matching_flags(previous: object) -> bool:
    return (isinstance(previous, list)
            and [flag for flag in previous if flag not in DIAGNOSTIC_ONLY_FLAGS] == FLAGS)


def evidence_records(value: object):
    if isinstance(value, dict):
        if isinstance(value.get("config"), dict) and value["config"].get("source_sha256"):
            yield value
        else:
            for child in value.values():
                yield from evidence_records(child)
    elif isinstance(value, list):
        for child in value:
            yield from evidence_records(child)


class EvidenceIndex:
    """Reuso estricto; evidencia incompleta se inventaría sin declararla validada."""
    def __init__(self, info: dict, roots: list[Path]):
        self.info = info
        self.by_source = defaultdict(list)
        self.source_files = defaultdict(list)
        self.notes = []
        self.json_files = 0
        self.candidate_records = 0
        for root in roots:
            if not root.is_dir():
                self.notes.append({"root": str(root), "status": "absent"})
                continue
            for source in root.rglob("*.cpp"):
                if source.is_file():
                    self.source_files[sha(source.read_bytes())].append(str(source))
            for path in root.rglob("*.json"):
                if any(part in {"android", "jvm", "jvm-focused"} for part in path.relative_to(root).parts):
                    continue
                try:
                    value = load_json(path)
                except (OSError, UnicodeDecodeError, json.JSONDecodeError):
                    continue
                self.json_files += 1
                for record in evidence_records(value):
                    self.candidate_records += 1
                    config = record["config"]
                    directory = path.parent if path.name == "evidence.json" else None
                    if directory is None and record.get("directory"):
                        raw_path = Path(record["directory"])
                        base = root.parents[2] if root.name == "respuestas-tecnicas" else ROOT
                        directory = raw_path if raw_path.is_absolute() else base / raw_path
                    if directory is None or not inside(directory, root):
                        continue
                    reason = self.validate(record, directory)
                    if reason:
                        continue
                    key = (config["source_sha256"], self.stdin_bytes(record, directory),
                           project_fingerprint(config.get("source_files")))
                    entry = {"record": record, "path": path, "directory": directory}
                    if not any(old["directory"] == directory for old in self.by_source[key]):
                        self.by_source[key].append(entry)

    def stdin_bytes(self, record: dict, directory: Path) -> bytes | None:
        config = record["config"]
        if isinstance(config.get("stdin"), str):
            return config["stdin"].encode("utf-8")
        stdin_path = directory / "stdin.txt"
        if stdin_path.is_file() and "stdin.txt" in record.get("artifacts", {}):
            return stdin_path.read_bytes()
        source = (directory / "source.cpp").read_text(encoding="utf-8")
        if not re.search(r"\b(?:cin|stdin|scanf|getchar|gets)\b", source):
            # El verificador respuestas-tecnicas no entrega stdin a subprocess.
            if "respuestas-tecnicas" in directory.parts:
                return b""
        return None

    def validate(self, record: dict, directory: Path) -> str | None:
        config = record["config"]
        compiler = config.get("compiler", {})
        if not isinstance(compiler, dict) or compiler.get("sha256") != self.info["sha256"] or compiler.get("version") != self.info["version"]:
            return "compiler identity absent or different"
        if not matching_flags(config.get("flags")):
            return "non-equivalent flags"
        artifacts = record.get("artifacts", {})
        required = {"source.cpp", "program.exe", "compile.command.json", "link.command.json",
                    "run.command.json", "compile.stdout.txt", "compile.stderr.txt",
                    "link.stdout.txt", "link.stderr.txt", "run.stdout.txt", "run.stderr.txt"}
        source_files = config.get("source_files")
        if source_files is not None:
            if not isinstance(source_files, dict) or source_files.get("source.cpp") != config["source_sha256"]:
                return "invalid project manifest"
            for name, checksum in source_files.items():
                if artifacts.get(name) != checksum:
                    return "project source absent or hash mismatch"
            required.update(source_files)
        units = compile_units(source_files)
        for stage, source_name, object_name in units:
            required.update({object_name, f"{stage}.command.json", f"{stage}.stdout.txt", f"{stage}.stderr.txt"})
        if not required.issubset(artifacts):
            return "missing raw artifacts"
        for name, checksum in artifacts.items():
            artifact = directory / name
            if not inside(artifact, directory) or not artifact.is_file() or sha(artifact.read_bytes()) != checksum:
                return "artifact absent or hash mismatch"
        if artifacts["source.cpp"] != config["source_sha256"]:
            return "source mismatch"
        link_command = load_json(directory / "link.command.json")
        for stage, source_name, object_name in units:
            compile_command = load_json(directory / f"{stage}.command.json")
            if (not isinstance(compile_command, list)
                    or compile_command[1:-4] != config["flags"]
                    or compile_command[-4:] != ["-c", source_name, "-o", object_name]
                    or Path(compile_command[0]).resolve() != Path(self.info["path"]).resolve()):
                return "compile command does not support its recorded config"
        if (not isinstance(link_command, list) or link_command[1:] != [*[unit[2] for unit in units], "-o", "program.exe"]
                or Path(link_command[0]).resolve() != Path(self.info["path"]).resolve()):
            return "link command does not support its recorded config"
        stages = record.get("stages", {})
        for name in (*[unit[0] for unit in units], "link"):
            stage = stages.get(name, {})
            if stage.get("returncode") != 0 or stage.get("timeout"):
                return "compile/link did not succeed"
        if not isinstance(stages.get("run", {}).get("returncode"), int) or stages["run"].get("timeout"):
            return "no completed execution"
        stdin = self.stdin_bytes(record, directory)
        if stdin is None:
            return "stdin not established"
        if config.get("stdin_sha256") not in {None, sha(stdin)}:
            return "stdin identity hash mismatch"
        if (directory / "stdin.txt").is_file() and (directory / "stdin.txt").read_bytes() != stdin:
            return "stdin mismatch"
        return None

    def find(self, case: Case) -> dict | None:
        entries = self.by_source.get((case.source_sha256, case.stdin.encode("utf-8"),
                                      project_fingerprint(case.source_files)), [])
        return entries[0] if entries else None

    def summary(self, cases: list[Case]) -> dict:
        return {"json_files_read": self.json_files, "structured_candidate_records": self.candidate_records,
                "verified_records": sum(len(rows) for rows in self.by_source.values()),
                "cpp_files": sum(len(rows) for rows in self.source_files.values()),
                "exact_source_candidates": sum(case.source_sha256 in self.source_files for case in cases),
                "reusable_predictions": sum(self.find(case) is not None for case in cases),
                "limits": ["Bare passed/expectedStdout or unhashed compiler/output is not proof for reuse",
                           "Only -Werror/-Wconversion differences are diagnostic-equivalent"],
                "roots": self.notes}


def outcome(case: Case, stages: dict, compare: dict) -> str:
    if any(stages.get(name, {}).get("timeout") for name in stages):
        return "timeout"
    if stages.get("compile", {}).get("returncode") != 0 or any(
            stage.get("returncode") != 0 for name, stage in stages.items() if name.startswith("compile_")):
        return "compile_failed"
    if stages.get("link", {}).get("returncode") != 0:
        return "link_failed"
    run_exit = stages.get("run", {}).get("returncode")
    if not compare.get("matched"):
        return "stdout_mismatch"
    expected_exit = EXPECTED_NONZERO_EXITS.get(case.location, 0)
    return "matched" if run_exit == expected_exit else "stdout_matched_exit_requires_review"


class Runner:
    def __init__(self, info: dict, evidence: EvidenceIndex, directory: Path, compile_timeout: float, run_timeout: float):
        self.info, self.evidence, self.directory = info, evidence, directory
        self.compile_timeout, self.run_timeout = compile_timeout, run_timeout
        self.env = dict(os.environ, LANG="C", LC_ALL="C")
        self.env["PATH"] = str(Path(info["path"]).parent) + os.pathsep + self.env.get("PATH", "")

    def stage(self, directory: Path, name: str, command: list[str], stdin: bytes | None = None) -> dict:
        (directory / f"{name}.command.json").write_bytes(serialized(command))
        started = time.monotonic()
        try:
            process = subprocess.run(command, cwd=directory, env=self.env, input=stdin,
                                     capture_output=True, timeout=self.run_timeout if name == "run" else self.compile_timeout)
            stdout, stderr, code, timeout = process.stdout, process.stderr, process.returncode, False
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code, timeout = error.stdout or b"", error.stderr or b"", None, True
        except OSError as error:
            stdout, stderr, code, timeout = b"", str(error).encode("utf-8"), None, False
        (directory / f"{name}.stdout.txt").write_bytes(stdout)
        (directory / f"{name}.stderr.txt").write_bytes(stderr)
        return {"returncode": code, "timeout": timeout, "elapsed_seconds": round(time.monotonic() - started, 3),
                "stdout_sha256": sha(stdout), "stderr_sha256": sha(stderr),
                "stderr": stderr.decode("utf-8", errors="replace")}

    def check(self, case: Case) -> dict:
        record = {"origin": case.origin, "source_sha256": case.source_sha256,
                  "stdin_sha256": sha(case.stdin.encode("utf-8")), "comparison_mode": case.mode,
                  "answers": case.answers, "expected_run_exit": EXPECTED_NONZERO_EXITS.get(case.location, 0),
                  "host_only": True, "android_certified": False}
        if case.source_files:
            record["project_context"] = {"source_files": case.source_files,
                                         "fingerprint": project_fingerprint(case.source_files),
                                         "dependency_origins": [{key: value for key, value in file.items() if key != "code"}
                                                                for file in case.dependencies]}
        if case.review:
            return {**record, "status": "omitted_requires_review", "reasons": list(case.review)}
        cached = self.evidence.find(case)
        if cached:
            previous = cached["record"]
            compare = comparison((cached["directory"] / "run.stdout.txt").read_bytes(), case.answers, case.mode)
            stages = previous["stages"]
            return {**record, "reused": True, "evidence": str(cached["path"]),
                    "evidence_directory": str(cached["directory"]), "evidence_sha256": sha(cached["path"].read_bytes()),
                    "previous_flags": previous["config"]["flags"], "requested_flags": FLAGS,
                    "reuse_reason": "Exact source/stdin/raw artifact hashes/compiler; only diagnostic flags may differ",
                    "stages": stages, "comparison": compare, "status": outcome(case, stages, compare)}
        identity = {"compiler": self.info, "flags": FLAGS, "source_sha256": case.source_sha256,
                    "stdin_sha256": sha(case.stdin.encode("utf-8"))}
        if case.source_files:
            identity["source_files"] = case.source_files
        key = sha(serialized(identity))
        identity_directory = self.directory / "cases" / key
        identity_directory.mkdir(parents=True, exist_ok=True)
        # Dos actividades pueden compartir fuente; nunca comparten un cwd usado.
        directory = identity_directory / ("run-" + uuid.uuid4().hex)
        directory.mkdir(exist_ok=False)
        (directory / "source.cpp").write_bytes(case.source.encode("utf-8"))
        for dependency in case.dependencies:
            (directory / dependency["name"]).write_bytes(dependency["code"].encode("utf-8"))
        (directory / "stdin.txt").write_bytes(case.stdin.encode("utf-8"))
        (directory / "answers.json").write_bytes(serialized({"answers": case.answers, "mode": case.mode}))
        atomic_json(directory / "identity.json", identity)
        units = compile_units(case.source_files)
        stages = {name: self.stage(directory, name, [self.info["path"], *FLAGS, "-c", source_name, "-o", object_name])
                  for name, source_name, object_name in units}
        if all(stage["returncode"] == 0 for stage in stages.values()):
            stages["link"] = self.stage(directory, "link", [self.info["path"], *[unit[2] for unit in units], "-o", "program.exe"])
        if stages.get("link", {}).get("returncode") == 0:
            # No archivo curricular puede existir antes de esta ejecución.
            occupied = [name for name in case.file_names if (directory / name).exists()]
            if occupied:
                raise RuntimeError(f"cwd nuevo contiene archivos curriculares: {occupied}")
            stages["run"] = self.stage(directory, "run", [str(directory / "program.exe")], case.stdin.encode("utf-8"))
        compare = comparison((directory / "run.stdout.txt").read_bytes(), case.answers, case.mode) if "run" in stages else {"matched": False, "not_executed": True}
        result = {**record, "reused": False, "directory": str(directory), "config": identity,
                  "stages": stages, "comparison": compare, "status": outcome(case, stages, compare)}
        result["artifacts"] = {str(path.relative_to(directory)): sha(path.read_bytes())
                               for path in directory.rglob("*") if path.is_file()}
        atomic_json(directory / "evidence.json", result)
        return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execute", action="store_true", help="Compilar/ejecutar; por defecto sólo plan de lectura")
    parser.add_argument("--compiler", type=Path, default=Path(shutil.which("g++") or "C:/msys64/mingw64/bin/g++.exe"))
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--compile-timeout", type=float, default=60)
    parser.add_argument("--run-timeout", type=float, default=10)
    parser.add_argument("--plan-output", type=Path, help="Guardar plan QA nuevo, exclusivamente dentro de .gradle/cpp-content-qa")
    parser.add_argument("--output", type=Path, help="Raíz QA nueva y vacía; predeterminado con timestamp y UUID")
    parser.add_argument("--limit", type=int, help="Sólo ensayo focal explícito; no acredita todas las predicciones")
    args = parser.parse_args()
    if args.jobs < 1 or args.compile_timeout <= 0 or args.run_timeout <= 0 or (args.limit is not None and args.limit < 1):
        parser.error("Paralelismo, límites de tiempo y limit deben ser positivos")
    cases, metadata = inventory()
    info = compiler_info(args.compiler)
    evidence = EvidenceIndex(info, [QA_ROOT, APP_EVIDENCE])
    plan = {"scope": "Indexed output_prediction only; desktop evidence, no Android",
            "compiler": info, "flags": FLAGS, "inventory": metadata, "reuse": evidence.summary(cases),
            "explicit_stdin_cases": [{"origin": case.origin, "stdin": case.stdin} for case in cases if case.location in INPUTS],
            "file_cases": [{"origin": case.origin, "files": list(case.file_names)} for case in cases if case.file_names],
            "project_cases": [{"origin": case.origin, "source_files": case.source_files,
                               "fingerprint": project_fingerprint(case.source_files),
                               "dependencies": list(case.dependencies)} for case in cases if case.dependencies],
            "omitted": [{"origin": case.origin, "reasons": list(case.review)} for case in cases if case.review],
            "cases": [{"origin": case.origin, "source_sha256": case.source_sha256,
                       "mode": case.mode, "answers": case.answers,
                       "reusable": evidence.find(case) is not None,
                       "source_candidates": evidence.source_files.get(case.source_sha256, [])}
                      for case in cases]}
    if args.plan_output:
        target = args.plan_output.resolve()
        if not inside(target, QA_ROOT) or target.exists():
            parser.error("plan-output debe ser un archivo nuevo dentro de la QA ignorada")
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(serialized(plan))
    display = {key: value for key, value in plan.items() if key != "cases"}
    display["inventory"] = {key: value for key, value in metadata.items() if key != "snapshot_sha256"}
    display["project_cases"] = [{key: value for key, value in project.items() if key != "dependencies"}
                                for project in plan["project_cases"]]
    print(json.dumps(display, ensure_ascii=False, indent=2), flush=True)
    if not args.execute:
        return 0
    chosen = cases[:args.limit] if args.limit else cases
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = args.output.resolve() if args.output else QA_ROOT / ("predicciones-indexadas-" + stamp + "-" + uuid.uuid4().hex[:8])
    if not inside(output, QA_ROOT) or output.exists():
        parser.error("output debe ser una carpeta nueva dentro de la QA ignorada")
    output.mkdir(parents=True, exist_ok=False)
    atomic_json(output / "plan.json", plan)
    runner = Runner(info, evidence, output, args.compile_timeout, args.run_timeout)
    records = []
    run_metadata = {"scope": plan["scope"], "compiler": info, "flags": FLAGS, "inventory": metadata,
                    "planned": len(chosen), "entire_index": len(chosen) == len(cases),
                    "output": str(output), "host_only": True, "android_certified": False}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(runner.check, case): case for case in chosen}
        for future in as_completed(futures):
            case = futures[future]
            try:
                result = future.result()
            except Exception as error:
                result = {"origin": case.origin, "source_sha256": case.source_sha256,
                          "status": "verifier_error", "error": repr(error)}
            records.append(result)
            counts = dict(Counter(record["status"] for record in records))
            atomic_json(output / "resultados.json", {**run_metadata, "completed": len(records),
                                                     "statuses": counts, "records": records})
            if len(records) % 20 == 0 or len(records) == len(chosen):
                print(f"{len(records)}/{len(chosen)}: {counts}; guardado en {output}", flush=True)
    return 0 if all(record["status"] == "matched" for record in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())

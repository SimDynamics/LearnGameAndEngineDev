"""Contrasta las tres capacidades de respuestas con el C++ curricular publicado.

Esta QA de escritorio no ejecuta el evaluador Kotlin ni acredita Android. Lee los
assets, conserva sus fuentes y canales de compilación/enlace/ejecución, y comprueba
que frente a Git sólo se hayan añadido los dos contratos optativos acordados.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTENT_REL = "app/src/main/assets/content/es/programming/cpp_fundamentals"
CONTENT = ROOT / CONTENT_REL
QA = ROOT / ".gradle/cpp-content-qa/respuestas-tecnicas"
FLAGS = ["-std=c++23", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
PREDICTIONS = [
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 2, 4),
    ("repeticion_y_terminacion/lesson_recorridos_anidados.json", 10, 3),
    ("repeticion_y_terminacion/lesson_recorridos_anidados.json", 12, 6),
    ("datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json", 12, 3),
    ("datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json", 15, 3),
]
LEGACY_PREDICTIONS = [
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 2, 5),
    ("archivos_y_programa_completo/lesson_ancho_relleno_y_alineacion_de_campos.json", 2, 2),
]
INLINE = {
    ("expresiones_y_decisiones/lesson_combinar_condiciones.json", 2, 4): [["&&", "and"], ["||", "or"]],
    ("expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json", 3, 6): [["=="], ["!=", "not_eq"]],
    ("colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json", 4, 5): [["push_back", "emplace_back"], ["9"]],
    ("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", 5, 4): [["push_back", "emplace_back"], ["valor"]],
}
LITERALS = [
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 2, 5),
    ("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 1, 4),
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 5, 2),
]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          timeout=30, check=True).stdout


def published(location: tuple[str, int, int]) -> tuple[dict, dict]:
    relative, page, block = location
    lesson = json.loads((CONTENT / relative).read_text(encoding="utf-8-sig"))
    item = lesson["pages"][page - 1]["blocks"][block - 1]
    origin = {"lesson": lesson["id"], "path": relative, "page": page,
              "block": block, "type": item["type"], "id": item.get("id")}
    return item, origin


def structural_audit(baseline: str) -> dict:
    """Whole JSON equality after removing only the nine approved optional fields."""
    current = {path.relative_to(ROOT).as_posix(): path for path in CONTENT.rglob("*.json")}
    previous = set(git("ls-tree", "-r", "--name-only", baseline, "--", CONTENT_REL)
                   .decode("utf-8").splitlines())
    if set(current) != previous:
        raise ValueError(f"Inventario curricular distinto: {sorted(set(current) ^ previous)}")
    approved: list[dict] = []
    pages_count = blocks_count = 0
    for relative, path in sorted(current.items()):
        old = json.loads(git("show", f"{baseline}:{relative}").decode("utf-8-sig"))
        new = json.loads(path.read_text(encoding="utf-8-sig"))
        short = path.relative_to(CONTENT).as_posix()
        for page, data in enumerate(new.get("pages", []), 1):
            pages_count += 1
            for block, item in enumerate(data["blocks"], 1):
                blocks_count += 1
                location = (short, page, block)
                old_item = old["pages"][page - 1]["blocks"][block - 1]
                if location in PREDICTIONS:
                    if item.get("outputComparison") != "exact":
                        raise ValueError(f"Falta outputComparison exact: {location}")
                    if "outputComparison" not in old_item:
                        item.pop("outputComparison")
                        approved.append({"path": short, "page": page, "block": block,
                                         "field": "outputComparison", "value": "exact"})
                elif "outputComparison" in item and item.get("outputComparison") != old_item.get("outputComparison"):
                    raise ValueError(f"Comparación cambiada fuera de las cinco predicciones: {location}")
                if location in INLINE:
                    expected = INLINE[location]
                    if item.get("answerAlternatives") != expected:
                        raise ValueError(f"Alternativas por hueco distintas: {location}")
                    if item["answers"] != [options[0] for options in expected]:
                        raise ValueError(f"Respuestas previas o primer equivalente distintos: {location}")
                    if len(expected) != item["template"].count("[]"):
                        raise ValueError(f"Alternativas confundidas con huecos: {location}")
                    if "answerAlternatives" not in old_item:
                        item.pop("answerAlternatives")
                        approved.append({"path": short, "page": page, "block": block,
                                         "field": "answerAlternatives", "value": expected})
                elif "answerAlternatives" in item and item.get("answerAlternatives") != old_item.get("answerAlternatives"):
                    raise ValueError(f"Alternativas cambiadas fuera de los cuatro bloques: {location}")
        if new != old:
            raise ValueError(f"Contenido previo modificado, desplazado o eliminado: {short}")
    return {"passed": True, "baseline": git("rev-parse", baseline).decode().strip(),
            "json_files": len(current), "pages": pages_count, "blocks": blocks_count,
            "preserved": ["identifiers", "positions", "answers", "templates", "all_other_content"],
            "approved_optional_additions": approved}


def substitute(template: str, answers: list[str], marker: str = "[]") -> str:
    if template.count(marker) != len(answers):
        raise ValueError("Número de huecos y respuestas distinto")
    for answer in answers:
        template = template.replace(marker, answer, 1)
    return template


def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise ValueError(f"Se esperaba un ancla única: {before!r}")
    return source.replace(before, after, 1)


@dataclass
class Case:
    name: str
    source: str
    expected: str
    origin: dict
    compile_ok: bool = True
    # For pedagogically wrong but compilable responses, compare against the
    # correct concrete output as well as their independently stated wrong output.
    differs_from: str | None = None


def plans() -> list[Case]:
    cases: list[Case] = []
    for number, location in enumerate(PREDICTIONS, 1):
        item, origin = published(location)
        cases.append(Case(f"prediccion_{number}_fuente_publicada", item["code"],
                          item["answers"][0], origin))
    for number, location in enumerate(LEGACY_PREDICTIONS, 1):
        item, origin = published(location)
        if "outputComparison" in item:
            raise ValueError(f"La regresión debe conservar el criterio anterior: {location}")
        cases.append(Case(f"regresion_prediccion_{number}_caracteres_internos", item["code"],
                          item["answers"][0], origin))

    logic, equality, empty, selection = list(INLINE)
    item, origin = published(logic)
    for number, answers in enumerate(itertools.product(*INLINE[logic]), 1):
        filled = substitute(item["template"], list(answers))
        for first, second, output in [("false", "false", "0 0"), ("false", "true", "0 1"),
                                      ("true", "false", "0 1"), ("true", "true", "1 1")]:
            source = replace_once(filled, "conoce_clave = true", f"conoce_clave = {first}")
            source = replace_once(source, "trae_tarjeta = false", f"trae_tarjeta = {second}")
            cases.append(Case(f"logica_{number}_{first}_{second}", source, output,
                              {**origin, "fill": answers, "boolean_inputs": [first, second]}))
    for number, answers in enumerate([["||", "&&"], ["or", "and"]], 1):
        cases.append(Case(f"logica_invertida_{number}", substitute(item["template"], answers),
                          "1 0", {**origin, "fill": answers, "negative_semantic_probe": True},
                          differs_from="0 1"))

    item, origin = published(equality)
    for number, answers in enumerate(itertools.product(*INLINE[equality]), 1):
        for observed, output in [(5, "1 0"), (7, "0 1"), (3, "0 1")]:
            source = replace_once(substitute(item["template"], list(answers)),
                                  "observado = 7", f"observado = {observed}")
            cases.append(Case(f"comparacion_{number}_{observed}", source, output,
                              {**origin, "fill": answers, "observed": observed}))
    for number, answers in enumerate([["!=", "=="], ["==", "=="]], 1):
        cases.append(Case(f"comparacion_erronea_{number}", substitute(item["template"], answers),
                          "1 0" if number == 1 else "0 0",
                          {**origin, "fill": answers, "negative_semantic_probe": True},
                          differs_from="0 1"))

    item, origin = published(empty)
    for operation in INLINE[empty][0]:
        source = substitute(item["template"], [operation, "9"])
        cases.append(Case(f"vector_vacio_{operation}_publicado", source, "1", {**origin, "fill": [operation, "9"]}))
        probe = replace_once(source, "std::cout << datos.size();",
                             "std::cout << datos.size(); for (int dato : datos) { std::cout << ':' << dato; }")
        cases.append(Case(f"vector_vacio_{operation}_contenido", probe, "1:9",
                          {**origin, "fill": [operation, "9"], "instrumentation": "size and all values"}))
    for operation, value, output in [("resize", "9", "9:0:0:0:0:0:0:0:0:0"),
                                      ("reserve", "9", "0"), ("push_back", "8", "1:8")]:
        source = substitute(item["template"], [operation, value])
        source = replace_once(source, "std::cout << datos.size();",
                              "std::cout << datos.size(); for (int dato : datos) { std::cout << ':' << dato; }")
        cases.append(Case(f"vector_vacio_erroneo_{operation}_{value}", source, output,
                          {**origin, "fill": [operation, value], "negative_semantic_probe": True},
                          differs_from="1:9"))

    item, origin = published(selection)
    output_statement = ("std::cout << elegidos.size(); for (int dato : elegidos) { std::cout << ':' << dato; }"
                        " std::cout << '|'; for (int dato : origen) { std::cout << dato << ':'; }")
    for operation in INLINE[selection][0]:
        filled = substitute(item["template"], [operation, "valor"])
        cases.append(Case(f"seleccion_{operation}_publicado", filled, "1", {**origin, "fill": [operation, "valor"]}))
        probe = replace_once(filled, "std::cout << elegidos.size();", output_statement)
        cases.append(Case(f"seleccion_{operation}_contenido_y_origen", probe, "1:4|1:4:",
                          {**origin, "fill": [operation, "valor"], "instrumentation": "all selected and source values"}))
        extended = replace_once(probe, "origen{1, 4}", "origen{1, 4, 7, 3}")
        cases.append(Case(f"seleccion_{operation}_orden", extended, "3:4:7:3|1:4:7:3:",
                          {**origin, "fill": [operation, "valor"], "instrumentation": "extended source {1,4,7,3}"}))
    for operation, value, output in [("resize", "valor", "4:0:0:0:0|1:4:"),
                                      ("push_back", "valor + 1", "1:5|1:4:")]:
        source = replace_once(substitute(item["template"], [operation, value]),
                              "std::cout << elegidos.size();", output_statement)
        cases.append(Case(f"seleccion_erronea_{operation}", source, output,
                          {**origin, "fill": [operation, value], "negative_semantic_probe": True},
                          differs_from="1:4|1:4:"))
    reversed_order = replace_once(extended, "elegidos.emplace_back(valor);",
                                  "elegidos.insert(elegidos.begin(), valor);")
    cases.append(Case("seleccion_insercion_al_principio_cambia_orden", reversed_order,
                      "3:3:7:4|1:4:7:3:", {**origin, "negative_semantic_probe": True,
                      "instrumentation": "insert at beginning; extended source {1,4,7,3}"},
                      differs_from="3:4:7:3|1:4:7:3:"))
    wrong_variable = substitute(item["template"], ["push_back", "origen"])
    cases.append(Case("seleccion_variable_de_tipo_incorrecto", wrong_variable, "",
                      {**origin, "fill": ["push_back", "origen"], "negative_semantic_probe": True},
                      compile_ok=False))

    for number, location in enumerate(LITERALS, 1):
        item, origin = published(location)
        literal = item["answers"][-1]
        expected = ["Muelle cerrado", "Estacion abierta\nAcceso por el norte", "Carga lista"][number - 1]
        for variant, content, output, compile_ok in [
            ("correcto", literal, expected, True),
            ("espacios_fuera", " \t " + literal + " \t ", expected, True),
            ("espacio_duplicado", literal.replace(" ", "  ", 1), expected.replace(" ", "  ", 1), True),
            ("tabulador_real", literal.replace(" ", "\t", 1), expected.replace(" ", "\t", 1), True),
            ("salto_real", literal.replace(" ", "\n", 1), "", False),
        ]:
            if item["type"] == "fill_code":
                source = substitute(item["code"], [content], "___")
            else:
                source = substitute(item["template"], [" \t << \t ", content])
            cases.append(Case(f"literal_{number}_{variant}", source, output,
                              {**origin, "fill_literal": content, "variant": variant}, compile_ok,
                              expected if variant in {"espacio_duplicado", "tabulador_real"} else None))
    return cases


class HostVerifier:
    def __init__(self, compiler: Path, reuse: bool):
        self.compiler = compiler.resolve()
        self.reuse = reuse
        self.env = dict(os.environ, LANG="C", LC_ALL="C")
        self.env["PATH"] = str(self.compiler.parent) + os.pathsep + self.env.get("PATH", "")
        version = subprocess.run([str(self.compiler), "--version"], capture_output=True,
                                 timeout=15, check=True, env=self.env)
        self.info = {"path": str(self.compiler), "sha256": digest(self.compiler.read_bytes()),
                     "version": version.stdout.decode("utf-8", errors="replace").splitlines()[0]}

    def stage(self, directory: Path, name: str, command: list[str]) -> dict:
        (directory / f"{name}.command.json").write_bytes(json_bytes(command))
        result = subprocess.run(command, capture_output=True, timeout=45 if name != "run" else 10,
                                cwd=directory, env=self.env)
        (directory / f"{name}.stdout.txt").write_bytes(result.stdout)
        (directory / f"{name}.stderr.txt").write_bytes(result.stderr)
        return {"returncode": result.returncode,
                "stdout": result.stdout.decode("utf-8", errors="replace"),
                "stderr": result.stderr.decode("utf-8", errors="replace"),
                "stdout_sha256": digest(result.stdout), "stderr_sha256": digest(result.stderr)}

    def check(self, case: Case) -> dict:
        config = {"source_sha256": digest(case.source.encode("utf-8")), "compiler": self.info,
                  "flags": FLAGS, "expected_stdout_lf": case.expected,
                  "compile_ok": case.compile_ok, "differs_from_correct": case.differs_from}
        directory = QA / "cases" / digest(json_bytes(config))
        directory.mkdir(parents=True, exist_ok=True)
        evidence_path = directory / "evidence.json"
        if self.reuse and evidence_path.is_file():
            cached = json.loads(evidence_path.read_text(encoding="utf-8"))
            if (cached.get("config") == config and cached.get("passed") is True
                    and cached.get("artifacts") and all((directory / name).is_file()
                        and digest((directory / name).read_bytes()) == checksum
                        for name, checksum in cached["artifacts"].items())):
                return {"name": case.name, "origin": case.origin, "directory": str(directory.relative_to(ROOT)),
                        "reused": True, **cached}
        (directory / "source.cpp").write_bytes(case.source.encode("utf-8"))
        (directory / "expected.stdout.txt").write_bytes(case.expected.encode("utf-8"))
        stages = {"compile": self.stage(directory, "compile",
                  [str(self.compiler), *FLAGS, "-c", "source.cpp", "-o", "source.o"])}
        compiled = stages["compile"]["returncode"] == 0
        passed = False
        if not case.compile_ok:
            # Real LF inside an ordinary literal must cause a literal diagnostic,
            # whereas the wrong vector variable must fail due to its type.
            diagnostic = stages["compile"]["stderr"]
            required = "missing terminating" if "salto_real" in case.name else "no matching function"
            passed = not compiled and required in diagnostic
        elif compiled and not stages["compile"]["stderr"]:
            stages["link"] = self.stage(directory, "link", [str(self.compiler), "source.o", "-o", "program.exe"])
            if stages["link"]["returncode"] == 0 and not stages["link"]["stderr"]:
                stages["run"] = self.stage(directory, "run", [str(directory / "program.exe")])
                raw = stages["run"]["stdout"]
                # MinGW's stdout translates LF to CRLF. Preserve raw bytes above
                # and explicitly undo only this host transport translation.
                logical = raw.replace("\r\n", "\n") if os.name == "nt" else raw
                stages["run"]["stdout_lf"] = logical
                (directory / "run.stdout.lf.txt").write_bytes(logical.encode("utf-8"))
                passed = (stages["run"]["returncode"] == 0 and not stages["run"]["stderr"]
                          and logical == case.expected
                          and (case.differs_from is None or logical != case.differs_from))
        artifacts = {path.name: digest(path.read_bytes()) for path in directory.iterdir()
                     if path.is_file() and path.name != "evidence.json"}
        evidence = {"config": config, "stages": stages, "passed": passed, "artifacts": artifacts}
        evidence_path.write_bytes(json_bytes(evidence))
        return {"name": case.name, "origin": case.origin, "directory": str(directory.relative_to(ROOT)),
                "reused": False, **evidence}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="HEAD", help="Git ref whose curricular content must be preserved")
    parser.add_argument("--compiler", default=shutil.which("g++") or shutil.which("clang++") or shutil.which("c++"))
    parser.add_argument("--no-reuse", action="store_true")
    args = parser.parse_args()
    QA.mkdir(parents=True, exist_ok=True)
    report = {"scope": "host C++ semantics and structural content; not Kotlin comparator or Android renderer",
              "flags": FLAGS, "records": []}
    try:
        branch = git("branch", "--show-current").decode().strip()
        if branch != "work/mejoras-app":
            raise ValueError(f"Rama distinta de la autorizada: {branch}")
        report["structural_audit"] = structural_audit(args.baseline)
        if not args.compiler:
            raise ValueError("No hay compilador C++ en PATH; use --compiler sin instalar otro")
        verifier = HostVerifier(Path(args.compiler), not args.no_reuse)
        report["compiler"] = verifier.info
        report["host_stdout_transport"] = "MinGW CRLF preserved raw and converted to logical LF for comparison" if os.name == "nt" else "unchanged"
        for case in plans():
            record = verifier.check(case)
            report["records"].append(record)
            print(f"{'OK' if record['passed'] else 'FALLO'} {case.name}", flush=True)
            (QA / "resultados.json").write_bytes(json_bytes(report))
        report["passed"] = sum(record["passed"] for record in report["records"])
        report["failed"] = len(report["records"]) - report["passed"]
        (QA / "resultados.json").write_bytes(json_bytes(report))
        print(f"{report['passed']}/{len(report['records'])} casos; {QA / 'resultados.json'}")
        return 0 if report["failed"] == 0 else 1
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        report["incomplete"] = str(error)
        (QA / "resultados.json").write_bytes(json_bytes(report))
        print(f"No se completó la QA: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

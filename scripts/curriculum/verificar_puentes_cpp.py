"""Verifica en el host los puentes puntuales fallthrough y explicit.

Lee las fuentes publicadas sin modificar los assets. Conserva compilación,
enlace, stdout y stderr por separado dentro de la carpeta ignorada .gradle.
Esta comprobación no acredita el ejecutor ni el renderer de Android.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
QA = ROOT / ".gradle/cpp-content-qa/puentes-lenguaje"
FLAGS = ["-std=c++23", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Werror"]
LESSONS = {
    "fallthrough": "repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json",
    "explicit": "datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded_json(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


@dataclass
class Case:
    name: str
    source: str
    stdout: str = ""
    compile_ok: bool = True
    diagnostic_any: tuple[str, ...] = ()
    stdin: str = ""
    origin: dict = field(default_factory=dict)
    extra_flags: tuple[str, ...] = ()


class PublishedLesson:
    def __init__(self, topic: str):
        self.topic = topic
        self.relative = LESSONS[topic]
        self.path = CONTENT / self.relative
        self.data = json.loads(self.path.read_text(encoding="utf-8-sig"))
        self.used: set[tuple[int, int]] = set()

    def block(self, page: int, block: int, kind: str | None = None) -> dict:
        item = self.data["pages"][page - 1]["blocks"][block - 1]
        if kind is not None and item["type"] != kind:
            raise ValueError(f"{self.relative} p{page}b{block}: se esperaba {kind}")
        self.used.add((page, block))
        return item

    def require_coverage(self, first_page: int) -> None:
        complete = {(page, block) for page, data in enumerate(self.data["pages"], 1)
                    if page >= first_page for block, item in enumerate(data["blocks"], 1)
                    if item["type"] in {"code", "fill_code", "output_prediction", "cpp_compiler"}}
        if complete != self.used:
            raise ValueError(f"{self.relative}: fuentes sin cubrir o fuera del puente: {complete ^ self.used}")

    def case(self, page: int, block: int, stdout: str = "", *, kind: str | None = None,
             fill: str | None = None, compile_ok: bool = True,
             diagnostic_any: tuple[str, ...] = (),
             extra_flags: tuple[str, ...] = ()) -> Case:
        item = self.block(page, block, kind)
        key = "initialCode" if item["type"] == "cpp_compiler" else "code"
        source = item[key]
        if source.count("int main() {") != 1:
            raise ValueError(f"{self.relative} p{page}b{block}: se necesita una fuente completa con un main")
        origin = {"lesson": self.data["id"], "path": self.relative,
                  "page": page, "block": block, "type": item["type"],
                  "id": item.get("id"), "published_source_sha256": digest(source.encode("utf-8"))}
        if item["type"] == "output_prediction":
            # LessonActivity compara las respuestas de predicción con trim().
            # La ejecución conserva y compara todos los caracteres, incluido LF final.
            if stdout.strip() not in [answer.strip() for answer in item["answers"]]:
                raise ValueError(f"{self.relative} p{page}b{block}: oráculo concreto distinto de answers")
            origin["answer_agrees_after_renderer_trim"] = True
        if fill is not None:
            if item["type"] != "fill_code" or source.count("___") != 1:
                raise ValueError("El relleno debe corresponder al único hueco publicado")
            if fill not in item["answers"]:
                raise ValueError("El relleno concreto no coincide con answers")
            source = source.replace("___", fill, 1)
            origin["fill"] = fill
        if "___" in source:
            raise ValueError(f"{self.relative} p{page}b{block}: queda un hueco sin resolver")
        return Case(f"{self.topic}_p{page}b{block}", source, stdout, compile_ok,
                    diagnostic_any, item.get("initialStandardInput", ""), origin, extra_flags)


def variant(base: Case, name: str, before: str, after: str, stdout: str = "", *,
            compile_ok: bool = True, diagnostic_any: tuple[str, ...] = ()) -> Case:
    if base.source.count(before) != 1:
        raise ValueError(f"{name}: la sustitución debe tener un único ancla exacta")
    return Case(name, base.source.replace(before, after, 1), stdout, compile_ok,
                diagnostic_any, base.stdin,
                {**base.origin, "derived_from": base.name,
                 "replacements": [*base.origin.get("replacements", []),
                                  {"before": before, "after": after}]}, base.extra_flags)


def fallthrough_plan() -> list[Case]:
    lesson = PublishedLesson("fallthrough")
    example = lesson.case(7, 4, "Preparar;Revisar;Fin", kind="code")
    cases = [example,
             lesson.case(7, 6, "PR:1", kind="output_prediction"),
             lesson.case(8, 2, "Preparar;Revisar;", kind="fill_code", fill="[[fallthrough]]"),
             lesson.case(8, 4, "PR1;R2;Fin", kind="output_prediction"),
             # El editor mínimo deja estas dos constantes deliberadamente sin uso.
             # No se desactivan las demás advertencias en la solución ni variantes.
             lesson.case(9, 4, "", kind="cpp_compiler", extra_flags=("-Wno-unused-variable",)),
             lesson.case(10, 6, "1:2:1", kind="output_prediction")]
    reference = lesson.case(10, 3, "Preparaciones=3;Revisiones=3;Desconocidas=0", kind="code")
    cases.append(reference)
    for order, output in [(2, "Revisar;Fin"), (-1, "Desconocida;Fin"), (9, "Desconocida;Fin")]:
        cases.append(variant(example, f"fallthrough_ejemplo_orden_{order}",
                             "int orden{1};", f"int orden{{{order}}};", output))

    # Oráculos concretos por consigna: columnas de cada fila son los cinco
    # resúmenes para repeticiones 0,1,2,3,4. No se implementa otro switch.
    output_rows = {
        "preparar": ["0;0;0", "1;1;0", "2;2;0", "3;3;0", "4;4;0"],
        "revisar": ["0;0;0", "0;1;0", "0;2;0", "0;3;0", "0;4;0"],
        "desconocida": ["0;0;0", "0;0;1", "0;0;2", "0;0;3", "0;0;4"],
    }
    for order, category in [(-1, "desconocida"), (0, "desconocida"), (1, "preparar"),
                            (2, "revisar"), (9, "desconocida")]:
        for repetitions, values in enumerate(output_rows[category]):
            first, second, third = values.split(";")
            output = f"Preparaciones={first};Revisiones={second};Desconocidas={third}"
            changed = variant(reference, f"fallthrough_orden_{order}",
                              "int orden{1};", f"int orden{{{order}}};", output)
            cases.append(variant(changed, f"fallthrough_orden_{order}_repeticiones_{repetitions}",
                                 "int repeticiones{3};", f"int repeticiones{{{repetitions}}};", output))

    # La retirada del atributo conserva el flujo. Sólo este ensayo permite la
    # advertencia sobre caída implícita; su diagnóstico queda conservado.
    removed = variant(reference, "fallthrough_sin_atributo_mismo_flujo",
                      "        [[fallthrough]];\n", "",
                      "Preparaciones=3;Revisiones=3;Desconocidas=0")
    removed.extra_flags = ("-Wno-implicit-fallthrough",)
    cases.append(removed)
    lesson.require_coverage(7)
    return cases


def explicit_plan() -> list[Case]:
    lesson = PublishedLesson("explicit")
    scalar_diagnostic = ("conversion from", "non-scalar type")
    argument_diagnostic = ("invalid initialization of reference of type",)
    implicit = lesson.case(8, 3, "3:4", kind="code")
    direct = lesson.case(9, 3, "0:5:5", kind="output_prediction")
    rejected_scalar = lesson.case(10, 3, kind="code", compile_ok=False,
                                  diagnostic_any=scalar_diagnostic)
    rejected_argument = lesson.case(11, 3, kind="code", compile_ok=False,
                                    diagnostic_any=argument_diagnostic)
    calls = lesson.case(11, 5, "4:1:1", kind="output_prediction")
    domain = lesson.case(12, 3, "Valor:0\nRechazado:6\nValor:5\nRechazado:-1\n",
                         kind="output_prediction")
    reference_output = ("Predeterminado:0\nDirecto:5\nArgumento:1\nCopia:5\n"
                        "Rechazado:-1\nRechazado:6\n")
    reference = lesson.case(14, 3, reference_output, kind="code")
    cases = [implicit, direct, rejected_scalar,
             lesson.case(10, 5, "2", kind="fill_code", fill="explicit"),
             rejected_argument, calls, domain,
             lesson.case(13, 4, "", kind="cpp_compiler"), reference]

    # Reparar sólo la construcción implícita conserva el argumento y la salida.
    cases.append(variant(rejected_scalar, "explicit_entero_con_construccion_directa",
                         "Contador copia = 3;", "Contador copia{3};", "3"))
    cases.append(variant(rejected_scalar, "explicit_entero_con_parentesis",
                         "Contador copia = 3;", "Contador copia(3);", "3"))
    cases.append(variant(rejected_scalar, "sin_explicit_conversion_entero_permitida",
                         "explicit Contador(int inicial)", "Contador(int inicial)", "3"))
    cases.append(variant(rejected_argument, "explicit_argumento_construido_permitido",
                         "leer(3)", "leer(Contador{3})", "3"))
    cases.append(variant(rejected_argument, "sin_explicit_argumento_entero_permitido",
                         "explicit Contador(int inicial)", "Contador(int inicial)", "3"))

    # Una copia recibe un Contador existente. Su valor() sigue siendo int.
    cases.append(variant(direct, "explicit_copiar_valor_int_rechazado",
                         "const Contador copia = directo;",
                         "const Contador copia = directo.valor();", compile_ok=False,
                         diagnostic_any=scalar_diagnostic))
    cases.append(variant(calls, "explicit_objeto_existente_valor_int_rechazado",
                         "leer(existente)", "leer(existente.valor())", compile_ok=False,
                         diagnostic_any=argument_diagnostic))
    cases.append(variant(direct, "explicit_frontera_cero_y_copia",
                         "const Contador directo{5};", "const Contador directo{0};", "0:0:0"))
    cases.append(variant(domain, "explicit_todos_los_valores_del_dominio",
                         "std::vector<int>{0, 6, 5, -1}", "std::vector<int>{0, 1, 2, 3, 4, 5}",
                         "Valor:0\nValor:1\nValor:2\nValor:3\nValor:4\nValor:5\n"))
    cases.append(variant(domain, "explicit_rechazos_repetidos_sin_cero_inventado",
                         "std::vector<int>{0, 6, 5, -1}", "std::vector<int>{-1, 6, -1, 0, 5, 6}",
                         "Rechazado:-1\nRechazado:6\nRechazado:-1\nValor:0\nValor:5\nRechazado:6\n"))

    # El intento de p13 usa datos distintos de la solución posterior p14.
    attempt = variant(reference, "explicit_intento_directo_cuatro",
                      "const Contador directo{5};", "const Contador directo{4};")
    cases.append(variant(attempt, "explicit_datos_concretos_del_intento",
                         "leer(Contador{1})", "leer(Contador{2})",
                         "Predeterminado:0\nDirecto:4\nArgumento:2\nCopia:4\n"
                         "Rechazado:-1\nRechazado:6\n"))
    cases.append(variant(reference, "explicit_referencia_activar_error_conversion",
                         "    // Contador no_permitido = 3;", "    Contador no_permitido = 3;",
                         compile_ok=False, diagnostic_any=scalar_diagnostic))
    cases.append(variant(reference, "explicit_referencia_activar_error_argumento",
                         "    // leer(3);", "    leer(3);",
                         compile_ok=False, diagnostic_any=argument_diagnostic))

    # Recuperación posterior: ambos objetos ya existen antes de asignar.
    assignment = lesson.case(15, 3, "2:4:4", kind="code")
    assignment_output = ("Inicio:2\nDesde objeto:4\nDesde construccion:1\n"
                         "Rechazado:6\nConservado:1\n")
    assignment_reference = lesson.case(16, 3, assignment_output, kind="code")
    cases.extend([assignment, lesson.case(15, 8, "", kind="cpp_compiler"),
                  assignment_reference,
                  lesson.case(16, 5, "2:2:3:3", kind="output_prediction")])
    assignment_diagnostic = ("no match for 'operator='",)
    implicit_assignment = variant(assignment, "explicit_asignar_entero_sin_conversion_rechazado",
                                  "    destino = origen;", "    destino = 3;", compile_ok=False,
                                  diagnostic_any=assignment_diagnostic)
    cases.append(implicit_assignment)
    cases.append(variant(implicit_assignment, "explicit_asignar_rhs_construido",
                         "    destino = 3;", "    destino = Contador{3};", "2:3:4"))
    cases.append(variant(implicit_assignment, "sin_explicit_asignacion_entero_permitida",
                         "explicit Contador(int inicial)", "Contador(int inicial)", "2:3:4"))
    cases.append(variant(assignment_reference, "explicit_referencia_activar_error_asignacion",
                         "    // destino = 3;", "    destino = 3;", compile_ok=False,
                         diagnostic_any=assignment_diagnostic))
    observation = "    std::cout << \"Conservado:\" << destino.valor() << '\\n';"
    cases.append(variant(assignment_reference, "explicit_rhs_construido_antes_de_consultar",
                         observation, "    destino = Contador{3};\n" + observation,
                         "Inicio:2\nDesde objeto:4\nDesde construccion:1\nRechazado:6\nConservado:3\n"))
    for boundary, output in [(0, "2:0:4"), (5, "2:5:4")]:
        cases.append(variant(assignment, f"explicit_asignar_frontera_{boundary}",
                             "    destino = origen;", f"    destino = Contador{{{boundary}}};", output))
    lesson.require_coverage(8)
    return cases


def plans(selected: list[str]) -> list[Case]:
    cases = []
    for topic in selected:
        cases.extend(fallthrough_plan() if topic == "fallthrough" else explicit_plan())
    return cases


class HostVerifier:
    def __init__(self, compiler: Path, reuse: bool):
        self.compiler = compiler.resolve()
        self.reuse = reuse
        self.env = dict(os.environ, LC_ALL="C", LANG="C")
        # Los ejecutables MinGW necesitan sus DLL junto al compilador.
        self.env["PATH"] = str(self.compiler.parent) + os.pathsep + self.env.get("PATH", "")
        version = subprocess.run([str(self.compiler), "--version"], capture_output=True,
                                 timeout=15, env=self.env, check=True)
        self.compiler_info = {"path": str(self.compiler),
                              "sha256": digest(self.compiler.read_bytes()),
                              "version": version.stdout.decode("utf-8", errors="replace").splitlines()[0]}
        self.records: list[dict] = []
        QA.mkdir(parents=True, exist_ok=True)

    def save(self) -> None:
        report = {"scope": "host; no acredita Android", "compiler": self.compiler_info,
                  "flags": FLAGS, "cases": self.records,
                  "passed": sum(record.get("passed", False) for record in self.records),
                  "failed": sum(not record.get("passed", False) for record in self.records)}
        (QA / "resultados.json").write_bytes(encoded_json(report))

    def process(self, directory: Path, stage: str, command: list[str], stdin: bytes = b"") -> dict:
        (directory / f"{stage}.command.json").write_bytes(encoded_json(command))
        try:
            result = subprocess.run(command, input=stdin, capture_output=True,
                                    timeout=45 if stage != "run" else 10,
                                    cwd=directory, env=self.env)
            stdout, stderr, code, timeout = result.stdout, result.stderr, result.returncode, False
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code, timeout = error.stdout or b"", error.stderr or b"", None, True
        (directory / f"{stage}.stdout.txt").write_bytes(stdout)
        (directory / f"{stage}.stderr.txt").write_bytes(stderr)
        return {"returncode": code, "timeout": timeout,
                "stdout": stdout.decode("utf-8", errors="replace"),
                "stderr": stderr.decode("utf-8", errors="replace")}

    def check(self, case: Case) -> bool:
        config = {"source_sha256": digest(case.source.encode("utf-8")),
                  "compiler": self.compiler_info, "flags": [*FLAGS, *case.extra_flags],
                  "expected_stdout": case.stdout, "stdin": case.stdin,
                  "compile_ok": case.compile_ok, "diagnostic_any": list(case.diagnostic_any)}
        key = digest(encoded_json(config))
        directory = QA / "cases" / key
        record = {"name": case.name, "origin": case.origin, **config,
                  "directory": str(directory.relative_to(ROOT)), "reused": False, "passed": False}
        self.records.append(record)
        self.save()
        directory.mkdir(parents=True, exist_ok=True)
        evidence_path = directory / "evidence.json"
        if self.reuse and evidence_path.exists():
            evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
            files = evidence.get("artifacts", {})
            if (evidence.get("config") == config and evidence.get("passed") is True and files
                    and all((directory / name).is_file()
                            and digest((directory / name).read_bytes()) == checksum
                            for name, checksum in files.items())):
                record.update(evidence["stages"], reused=True, passed=True)
                self.save()
                return True

        source_path = directory / "source.cpp"
        source_bytes = case.source.encode("utf-8")
        if source_path.exists() and source_path.read_bytes() != source_bytes:
            record["failure"] = "La fuente conservada difiere del hash de este caso"
            self.save()
            return False
        source_path.write_bytes(source_bytes)
        (directory / "stdin.txt").write_bytes(case.stdin.encode("utf-8"))
        (directory / "expected.stdout.txt").write_bytes(case.stdout.encode("utf-8"))
        stages = {}
        compile_result = self.process(directory, "compile",
                                      [str(self.compiler), *config["flags"], "-c", "source.cpp", "-o", "source.o"])
        stages["compile"] = compile_result
        failure = None
        if compile_result["timeout"]:
            failure = "Tiempo de compilación agotado"
        elif not case.compile_ok:
            if compile_result["returncode"] == 0:
                failure = "La construcción que debe rechazarse compiló"
            elif not case.diagnostic_any or not any(fragment in compile_result["stderr"]
                                                   for fragment in case.diagnostic_any):
                failure = "El rechazo no corresponde al diagnóstico de conversión previsto"
        elif compile_result["returncode"] != 0 or compile_result["stderr"]:
            failure = "La fuente válida produjo un error o advertencia de compilación"
        else:
            link_result = self.process(directory, "link",
                                      [str(self.compiler), "source.o", "-o", "program.exe"])
            stages["link"] = link_result
            if link_result["returncode"] != 0 or link_result["stderr"] or link_result["timeout"]:
                failure = "Falló el enlace de la fuente válida"
            else:
                run_result = self.process(directory, "run", [str(directory / "program.exe")],
                                          case.stdin.encode("utf-8"))
                stages["run"] = run_result
                canonical = run_result["stdout"].replace("\r\n", "\n")
                run_result["stdout_lf"] = canonical
                if (run_result["returncode"] != 0 or run_result["stderr"] or run_result["timeout"]
                        or canonical != case.stdout):
                    failure = "La ejecución no coincide con salida, stderr y código previstos"
        record.update(stages, passed=failure is None)
        if failure:
            record["failure"] = failure
        artifacts = {path.name: digest(path.read_bytes()) for path in directory.iterdir()
                     if path.is_file() and path.name != "evidence.json"}
        evidence_path.write_bytes(encoded_json({"config": config, "stages": stages,
                                               "passed": failure is None, "artifacts": artifacts}))
        self.save()
        return failure is None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--topic", choices=["fallthrough", "explicit", "all"], default="all")
    parser.add_argument("--compiler", default=shutil.which("g++") or "C:/msys64/mingw64/bin/g++.exe")
    parser.add_argument("--no-reuse", action="store_true", help="Reejecutar fuentes y expectativas idénticas")
    args = parser.parse_args()
    selected = list(LESSONS) if args.topic == "all" else [args.topic]
    try:
        cases = plans(selected)
        verifier = HostVerifier(Path(args.compiler), reuse=not args.no_reuse)
        for case in cases:
            passed = verifier.check(case)
            print(f"{'OK' if passed else 'FALLO'} {case.name}")
        print(f"Evidencia: {QA / 'resultados.json'}")
        return 0 if all(record["passed"] for record in verifier.records) else 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"No se pudo completar la verificación: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

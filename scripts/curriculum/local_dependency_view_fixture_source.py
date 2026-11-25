"""Nine authentic publications and isolated dependency plans for real View QA.

Only the declared private-check roots are added to the lesson documents. The
manifest carries runnable solutions and deliberately fixed-output contrasts;
those programs are QA inputs, never replacements for authored curriculum.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import code_format_compat as source_format
import input_dependency_fixture_source as input_fixture
import local_role_fixture_source as role_fixture
import local_scalar_fixture_source as scalar_fixture
import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches


ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "2ca32b2337178d1effa3a2c81759e2493cd746d0"
CONTENT = "app/src/main/assets/content/es/programming/cpp_fundamentals/"
PUBLICATIONS = {
    "u2l1": {
        "path": CONTENT + "objetos_y_estado/lesson_objetos_valores_y_tipos.json",
        "sha256": "e2e956d112c2a665bb10f500a3e2261566c24dc43ae9212c40ffc6d80a09739a",
        "references": {"block_0004_0002": "block_0005_0001",
                       "block_0004_0006": "block_0006_0003"},
    },
    "u2l2": {
        "path": CONTENT + "objetos_y_estado/lesson_declarar_e_inicializar.json",
        "sha256": "17961975d2753a71c08b1b020c477cd6b12ed2337a84337403b484735f872b94",
        "references": {"block_0004_0002": "block_0005_0001",
                       "block_0004_0006": "block_0006_0003"},
    },
    "u2l3": {
        "path": CONTENT + "objetos_y_estado/lesson_estado_asignacion_y_copia.json",
        "sha256": "e98e86041d3d59543a991f4a49a3b6fdf350ebfab97fd4644f81aef7a4d989ff",
        "references": {"block_0006_0002": "block_0007_0001",
                       "block_0006_0006": "block_0008_0003"},
    },
    "u2l7": {
        "path": CONTENT + "objetos_y_estado/lesson_cadenas_y_texto.json",
        "sha256": "ef6543b6ac183d4d7fff933c443d3633f899c2877559caff209a19e47f521445",
        "references": {"block_0006_0002": "block_0007_0001",
                       "block_0008_0007": "p1u2l7_referencia_actualizada"},
    },
    "u2l8": {
        "path": CONTENT + "objetos_y_estado/lesson_caracteres_y_valores_logicos.json",
        "sha256": "d44ea3aca4844b169373abd9cc5bf31d59fa03ec53b3ea9b4cd686f6ddca4b6c",
        "references": {"block_0006_0002": "block_0007_0001"},
    },
    "u2l9": {
        "path": CONTENT + "objetos_y_estado/lesson_leer_una_entrada_conocida.json",
        "sha256": "37b473d7e9b66ab8232812e4e2c9a25e0aaa207bcfc92059fcd6fc675401d94f",
        "references": {"p1u2l9_reparar_lectura_de_cajas": "block_0007_0001",
                       "block_0009_0003": "block_0010_0001"},
    },
    "u2l10": {
        "path": CONTENT + "objetos_y_estado/lesson_representar_procesar_y_recuperar.json",
        "sha256": "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0",
        "references": {"block_0004_0002": "p1u2l10_ejemplo_informe",
                       "block_0007_0002": "block_0008_0001",
                       "p1u2l10_reparar_registro": "p1u2l10_ejemplo_copia_reparada"},
    },
    "u3l1": {
        "path": CONTENT + "expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json",
        "sha256": "0c4f3884ae261d3f27015de5e91d776efb4bf1dd521b725f3fc3203134fef716",
        "references": {"p1u3l1_reparar_fichas": "block_0005_0002",
                       "p1u3l1_construir_puntuacion": "p1u3l1_ejemplo_puntuacion"},
    },
    "u3l2": {
        "path": CONTENT + "expresiones_y_decisiones/lesson_division_y_resto.json",
        "sha256": "0f0fccd697fc9bd24ac1087bb735cdb89ecf362b9b8b35964790b94a5c48b596",
        "references": {"p1u3l2_reparar_conversion_minutos": "p1u3l2_ejemplo_minutos",
                       "p1u3l2_construir_informe_bandejas": "p1u3l2_ejemplo_bandejas"},
    },
}

# These four references teach the surrounding concept rather than solving the
# owner's precise repair. Apply only the stated unique edits to its authentic
# starter. In U3L1 the starter's fichas_sueltas must remain the variable named by
# the already-published private checks; the reference instead calls it sueltas.
STARTER_REPAIRS = {
    ("u2l1", "block_0004_0006"): (("int llaves = 7;", "int llaves = 11;"),),
    ("u2l2", "block_0004_0006"): (("int cajas{4};", "int cajas{0};"),
                                     ("int sobres = 9;", "int sobres = 2;")),
    ("u2l3", "block_0006_0006"): (
        ("    int anterior = aforo;\n    aforo += 3;",
         "    aforo += 3;\n    int anterior = aforo;"),),
    ("u3l1", "p1u3l1_reparar_fichas"): (
        ("(sobres + fichas_sueltas) * fichas_por_sobre",
         "sobres * fichas_por_sobre + fichas_sueltas"),),
}


ACTIVE_PUBLISHED_COMMIT = "39ba3cb196e196bac99c2d891b488f765679ad6d"
ACTIVE_EDITION = {"version": 1, "courseId": "cpp_fundamentals", "locale": "es",
                  "generation": 1, "id": "cpp_desde_cero_2026"}
# Explicit publication maps, never inferred from current physical positions.
# Each legacy label selects only the reusable QA declaration; the source,
# reference, saved address and edition always belong to the active document.
ACTIVE_PUBLICATIONS = {
    "u2l1": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_objetos_valores_y_tipos.json",
        "sha256": "5e5ac952489a5ad754745cbeeba5e10b388588c925d742fe25900990839dee7c",
        "owners": {"block_0004_0002": ("block_0004_0002", "block_0005_0001"),
                   "block_0004_0006": ("block_0004_0006", "block_0006_0003")},
    },
    "u2l2": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_declarar_e_inicializar.json",
        "sha256": "81bb8d942357ee473e585297d9b775a522fabc9edbdba376b45976a5257550cc",
        "owners": {"block_0004_0002": ("block_0004_0003", "block_0005_0001"),
                   "block_0004_0006": ("block_0004_0006", "block_0006_0003")},
    },
    "u2l3": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_estado_asignacion_y_copia.json",
        "sha256": "623acef3aa43c7385e7cbf46bc34799c8be37b6aad3a6271f07ff0b95f6849b9",
        "owners": {"block_0006_0002": ("block_0006_0002", "block_0007_0001"),
                   "block_0006_0006": ("block_0006_0006", "block_0008_0003")},
    },
    "u2l7": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_cadenas_y_texto.json",
        "sha256": "1c343997748ab89d8f01929109b3940d0c94ee97de547d9f2270090d6a8857de",
        "owners": {"block_0006_0002": ("block_0006_0002", "block_0007_0001"),
                   "block_0008_0007": ("block_0008_0006", "block_0008_0008")},
    },
    "u2l8": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_caracteres_y_valores_logicos.json",
        "sha256": "cdbeed5a950ee3106912f0b9656a0f9aa7c22d25c440b600b38e36ac82bdd57e",
        "owners": {"block_0006_0002": ("block_0006_0002", "block_0007_0001")},
    },
    "u2l9": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_leer_una_entrada_conocida.json",
        "sha256": "08b1a8f994ccaae36231af6dec48ecc4362c6f902a33e0f718b92a5210436669",
        "owners": {"p1u2l9_reparar_lectura_de_cajas": ("block_0006_0004", "block_0006_0006"),
                   "block_0009_0003": ("block_0009_0004", "block_0009_0006")},
    },
    "u2l10": {
        "path": CONTENT + "edition_es_1__objetos_y_estado/lesson_representar_procesar_y_recuperar.json",
        "sha256": "d216ae0c0fa8c07348b93e7d019168faac93de487a363adc8cf51955ae932437",
        "owners": {"block_0004_0002": ("block_0004_0005", "block_0004_0007"),
                   "block_0007_0002": ("block_0007_0004", "block_0007_0006"),
                   "p1u2l10_reparar_registro": ("block_0010_0002", "block_0010_0006")},
    },
    "u3l1": {
        "path": CONTENT + "edition_es_1__expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json",
        "sha256": "a2f8afaa94fc297774adbc4a84c135dc674723b541fab5c9201a41844f572287",
        "owners": {"p1u3l1_reparar_fichas": ("block_0003_0002", "block_0003_0006"),
                   "p1u3l1_construir_puntuacion": ("block_0005_0002", "block_0005_0005")},
    },
    "u3l2": {
        "path": CONTENT + "edition_es_1__expresiones_y_decisiones/lesson_division_y_resto.json",
        "sha256": "6844d3265b1ea1cbdb692c3b321199baa06416fc58fd5ce11ba4247ec837e5d4",
        "owners": {"p1u3l2_reparar_conversion_minutos": ("block_0004_0003", "block_0004_0006"),
                   "p1u3l2_construir_informe_bandejas": ("block_0005_0002", "block_0005_0005")},
    },
}
ACTIVE_STARTER_REPAIRS = {
    **STARTER_REPAIRS,
    ("u2l3", "block_0006_0006"): (
        ("    int anterior = aforo;\n\n    aforo += 3;",
         "    aforo += 3;\n\n    int anterior = aforo;"),),
}


def alternatives(stdout):
    return [stdout, stdout + "\n"]


def int_declarations(name):
    """The six probes from test_local_int_probe_authoring, including +3."""
    def probe(variable, replacement, stdout, after_update=False):
        result = {"variable": variable, "replacement": replacement,
                  "outputs": alternatives(stdout)}
        if after_update:
            result.update(afterUpdate=True, expectedIncrease=3)
        return result

    return {
        "u2l1": [
            {"activity": "block_0004_0002", "probes": [probe("llaves", 13, "Llaves: 13")]},
            {"activity": "block_0004_0006", "probes": [probe("llaves", 19, "Llaves: 19")]},
        ],
        "u2l2": [
            {"activity": "block_0004_0002", "probes": [
                probe("cajas", 13, "Cajas: 13\nSobres: 9"),
                probe("sobres", 17, "Cajas: 4\nSobres: 17")]},
            {"activity": "block_0004_0006", "probes": [
                probe("cajas", 6, "Cajas: 6\nSobres: 2"),
                probe("sobres", 17, "Cajas: 0\nSobres: 17")]},
        ],
        "u2l3": [
            {"activity": "block_0006_0002", "probes": [
                probe("aforo", 10, "Antes: 10\nAhora: 13"),
                probe("anterior", 11, "Antes: 11\nAhora: 8"),
                probe("aforo", 4, "Antes: 5\nAhora: 12", True)]},
            {"activity": "block_0006_0006", "probes": [
                probe("aforo", 10, "Antes: 13\nAhora: 13"),
                probe("anterior", 11, "Antes: 11\nAhora: 8"),
                probe("aforo", 4, "Antes: 12\nAhora: 12", True)]},
        ],
    }[name]


def string_declarations():
    """The two U2L7 contracts already exercised by local_string_state_tests."""
    def probe(variable, replacement, moment, stdout):
        return {"variable": variable, "replacement": replacement,
                "moment": moment, "outputs": alternatives(stdout)}

    return [
        {"activity": "block_0006_0002", "probes": [
            probe("edificio", "Sala Norte", "afterInitialization", "Sala Norte-04"),
            probe("codigo", "003", "afterInitialization", "Aula-003"),
            probe("etiqueta", "CONTROL QA", "beforeOutput", "CONTROL QA")]},
        {"activity": "block_0008_0007", "probes": [
            probe("codigo", "B2", "afterFirstAssignment", "Aula-B2"),
            probe("etiqueta", "CONTROL QA", "beforeOutput", "CONTROL QA")]},
    ]


def declarations(name):
    if name in {"u2l1", "u2l2", "u2l3"}:
        return {"compilerLocalIntChecks": int_declarations(name)}
    if name == "u2l7":
        return {"compilerLocalStringChecks": string_declarations()}
    if name == "u2l8":
        return {"compilerLocalIntChecks": scalar_fixture.declarations()}
    if name == "u2l9":
        return {"compilerInputChecks": input_fixture.declarations()}
    if name == "u2l10":
        return {"compilerLocalRoleChecks": role_fixture.declarations(),
                "compilerInputChecks": input_fixture.extended_declarations(name)}
    if name in {"u3l1", "u3l2"}:
        return {"compilerInputChecks": input_fixture.extended_declarations(name)}
    raise ValueError(f"Unknown dependency View publication: {name}")


def sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def verify_published_bytes(name, payload):
    if sha256(payload) != PUBLICATIONS[name]["sha256"]:
        raise ValueError(f"Dependency View QA differs from its exact main publication: {name}")
    return payload


def published_bytes(name):
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(name, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{PUBLISHED_COMMIT}:{PUBLICATIONS[name]['path']}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def verify_active_published_bytes(name, payload):
    spec = ACTIVE_PUBLICATIONS[name]
    if sha256(payload) != spec["sha256"]:
        raise ValueError(f"Active dependency View QA differs from its exact publication: {name}")
    document = state.parse_json(payload.decode("utf-8"))
    source = Path(spec["path"])
    expected_id = "cpp_fundamentals__" + source.parent.name + "__" + source.stem[7:]
    if document.get("courseEdition") != ACTIVE_EDITION or document.get("id") != expected_id:
        raise ValueError(f"Active dependency View QA lost its exact course edition: {name}")
    return payload


def active_published_bytes(name):
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_active_published_bytes(name, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{ACTIVE_PUBLISHED_COMMIT}:{ACTIVE_PUBLICATIONS[name]['path']}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def active_declarations(name):
    result = deepcopy(declarations(name))
    owners = ACTIVE_PUBLICATIONS[name]["owners"]
    for entries in result.values():
        for entry in entries:
            entry["activity"] = owners[entry["activity"]][0]
    return result


def solution_source(name, activity, blocks):
    reference = PUBLICATIONS[name]["references"][activity]
    source = blocks[reference]["code"]
    if (name, activity) in STARTER_REPAIRS:
        source = blocks[activity]["initialCode"]
        for previous, revised in STARTER_REPAIRS[name, activity]:
            if source.count(previous) != 1:
                raise ValueError(f"Dependency View starter repair is not unique: {name}/{activity}")
            source = source.replace(previous, revised, 1)
    return source


def active_solution_source(name, activity, blocks):
    owners = ACTIVE_PUBLICATIONS[name]["owners"]
    legacy = next((old for old, (current, _) in owners.items() if current == activity), None)
    if legacy is None:
        raise ValueError(f"Unknown active dependency View owner: {name}/{activity}")
    reference = owners[legacy][1]
    source = blocks[reference]["code"]
    if (name, legacy) in ACTIVE_STARTER_REPAIRS:
        source = blocks[activity]["initialCode"]
        for previous, revised in ACTIVE_STARTER_REPAIRS[name, legacy]:
            if source.count(previous) != 1:
                raise ValueError(f"Active dependency View starter repair is not unique: {name}/{activity}")
            source = source.replace(previous, revised, 1)
    return source


def cout_ranges(source):
    """Locate only simple output statements, preserving all surrounding bytes.

    Tokenization keeps comments, string/character literals and directives
    opaque. The authenticated programs use side-effect-free literal/identifier
    chains; reject other expressions instead of deleting an update or call.
    """
    positioned = []
    cursor = 0
    for token in source_format._cpp_tokens(source):
        spelling = token.partition(":")[2]
        at = source.find(spelling, cursor)
        if at < 0 or source[cursor:at].strip():
            raise ValueError("Cannot locate an exact C++ token in a View solution")
        positioned.append((token, at, at + len(spelling)))
        cursor = at + len(spelling)
    tokens = [token for token, _, _ in positioned]
    ranges = []
    index = 0
    while index < len(tokens):
        if tokens[index:index + 4] != ["id:std", "op:::", "id:cout", "op:<<"]:
            index += 1
            continue
        previous = index - 1
        while previous >= 0 and tokens[previous].startswith("comment:"):
            previous -= 1
        if previous >= 0 and tokens[previous] not in {"op:{", "op:;", "op:}"}:
            raise ValueError("View output contrast requires a standalone cout statement")
        end = index + 4
        while True:
            if end >= len(tokens) or not tokens[end].startswith(("id:", "literal:")):
                raise ValueError("View output contrast cannot remove output side effects")
            end += 1
            if end < len(tokens) and tokens[end] == "op:;":
                break
            if end >= len(tokens) or tokens[end] != "op:<<":
                raise ValueError("View output contrast accepts only simple cout chains")
            end += 1
        ranges.append((positioned[index][1], positioned[end][2]))
        index = end + 1
    if not ranges:
        raise ValueError("View solution has no supported output statements")
    return ranges


def fixed_output_source(source, stdout, output_reads=()):
    parts = []
    previous = 0
    for index, (begin, end) in enumerate(cout_ranges(source)):
        parts.append(source[previous:begin])
        # beforeOutput string probes require an actual variable read in cout.
        # Its empty substring preserves the valid insertion point without
        # making any private replacement affect this fixed public output.
        reads = "" if index else "".join(f"{name}.substr(0, 0) << " for name in output_reads)
        parts.append("std::cout << " + reads + json.dumps(stdout if index == 0 else "") + ";")
        previous = end
    parts.append(source[previous:])
    return "".join(parts)


def no_plan_source(stdout):
    # An explicit QA counterexample, not authored code or private evidence.
    return ("#include <iostream>\n\nint main() {\n    std::cout << " +
            json.dumps(stdout) + ";\n    return 0;\n}\n")


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def lesson_payloads(name, *, active=False):
    original = active_published_bytes(name) if active else published_bytes(name)
    before = state.parse_json(original.decode("utf-8"))
    added = active_declarations(name) if active else declarations(name)
    if any(root in before for root in added):
        raise ValueError(f"The exact {name} publication already contains an added QA root")
    after = deepcopy(before)
    after.update(added)
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    for root in added:
        del preserved[root]
    if preserved != before:
        raise ValueError(f"Dependency View QA changed an existing field: {name}")

    blocks = state._blocks(before)
    references = ({activity: reference for activity, reference in
                   ACTIVE_PUBLICATIONS[name]["owners"].values()} if active else
                  PUBLICATIONS[name]["references"])
    compilers = {identity for identity, block in blocks.items()
                 if block.get("type") == "cpp_compiler"}
    if compilers != set(references):
        raise ValueError(f"Dependency View solutions do not cover every editor: {name}")
    authored_references = {item["activity"]: item["blocks"]
                           for item in before.get("referenceAfter", [])}
    solutions = []
    sources = {}
    for activity, reference in references.items():
        block = blocks[reference]
        if (reference not in authored_references.get(activity, ()) or
                block.get("type") != "code" or block.get("language") != "cpp"):
            raise ValueError(f"Dependency View solution lost its authored reference: {name}/{activity}")
        source = (active_solution_source(name, activity, blocks) if active else
                  solution_source(name, activity, blocks))
        sources[activity] = source
        solutions.append({"activity": activity, "source": source,
                          "sourceSha256": sha256(source.encode("utf-8"))})

    plans = []
    for root, entries in added.items():
        for entry in entries:
            activity = entry["activity"]
            first_case = blocks[activity]["outputCheck"]["cases"][0]
            output_reads = tuple(dict.fromkeys(
                probe["variable"] for probe in entry.get("probes", [])
                if root == "compilerLocalStringChecks" and probe.get("moment") == "beforeOutput"))
            fixed = fixed_output_source(sources[activity], first_case["outputs"][0], output_reads)
            plan = {"activity": activity, "reference": references[activity],
                    "fixedSource": fixed, "fixedSourceSha256": sha256(fixed.encode("utf-8"))}
            if name == "u2l10" and activity == ("block_0004_0005" if active else "block_0004_0002"):
                no_plan = no_plan_source(first_case["outputs"][0])
                plan.update(noPlanSource=no_plan, noPlanSourceSha256=sha256(no_plan.encode("utf-8")))
            plans.append(plan)

    before_name, after_name = f"{name}/before.json", f"{name}/after.json"
    revised = json_bytes(after)
    return {before_name: original, after_name: revised}, {
        "id": name, "before": before_name, "after": after_name,
        "beforeSha256": sha256(original), "afterSha256": sha256(revised),
        "solutions": solutions, "plans": plans,
    }


def export_payloads():
    payloads = {}
    lessons = []
    for name in PUBLICATIONS:
        files, lesson = lesson_payloads(name)
        payloads.update(files)
        lessons.append(lesson)
    if (sum(len(item["solutions"]) for item in lessons) != 18 or
            sum(len(item["plans"]) for item in lessons) != 16):
        raise ValueError("Dependency View QA requires exactly 18 solutions and 16 focal owners")
    payloads["manifest.json"] = json_bytes({
        "version": 1, "publishedCommit": PUBLISHED_COMMIT, "lessons": lessons,
    })
    return payloads


def active_payloads():
    payloads = {}
    lessons = []
    for name in ACTIVE_PUBLICATIONS:
        files, lesson = lesson_payloads(name, active=True)
        payloads.update(files)
        lessons.append(lesson)
    if (len(lessons) != 9 or
            sum(len(item["solutions"]) for item in lessons) != 18 or
            sum(len(item["plans"]) for item in lessons) != 16):
        raise ValueError("Active dependency View QA requires nine lessons, 18 solutions and 16 focal owners")
    payloads["manifest.json"] = json_bytes({
        "version": 1, "publishedCommit": ACTIVE_PUBLISHED_COMMIT,
        "kind": "active-local-dependency", "lessons": lessons,
    })
    return payloads


def export(output_dir, *, active=False):
    output = checked_output(output_dir)
    payloads = active_payloads() if active else export_payloads()
    # Every source and destination is checked before the first new directory
    # or file, so a late conflict cannot leave earlier lesson copies behind.
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    for name, payload in payloads.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        if not existing_matches(target, payload):
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--active", action="store_true",
                        help="Use the nine authenticated active-edition publications")
    arguments = parser.parse_args()
    print(export(arguments.output_dir, active=arguments.active))

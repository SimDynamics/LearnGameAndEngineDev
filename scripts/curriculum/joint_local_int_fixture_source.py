"""Complete authenticated U3L7/U3L8 publications for joint-variable Win32 QA.

The original lesson bytes are immutable. The isolated AFTER copy appends one
joint observation to one existing owner; no authored block or history changes.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches


ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "a92895f2b00242dbd338ab0a386ea8ceeda9edf8"
CONTENT = "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/"
PUBLICATIONS = {
    "u3l7": {
        "path": CONTENT + "lesson_decisiones_compuestas_y_pruebas.json",
        "sha256": "132e947149b4388fcb0881792dc4054e2592f1a15c64a5c0f491d0f570ad541f",
        "activity": "p1u3l7_reparar_prioridad", "originalProbeCount": 3,
        "references": {
            "p1u3l7_reparar_prioridad": "p1u3l7_ejemplo_prioridad",
            "p1u3l7_construir_despacho": "p1u3l7_ejemplo_despacho",
        },
        "probe": {"variable": "solicitadas", "replacement": 6,
                  "additionalReplacements": [{"variable": "codigo_permiso", "replacement": 1}],
                  "outputs": ["Pendiente: 2", "Pendiente: 2\n"]},
    },
    "u3l8": {
        "path": CONTENT + "lesson_construir_decisiones_desde_un_problema.json",
        "sha256": "faf26c9f5443a650c06fd5b73c882de3ba7cf03885cb0758202cd58257ec98a1",
        "activity": "block_0002_0002", "originalProbeCount": 8,
        "references": {
            "block_0002_0002": "p1u3l8_ejemplo_original",
            "p1u3l8_cambiar_minimo": "p1u3l8_ejemplo_minimo",
        },
        "probe": {"variable": "fichas", "replacement": 0,
                  "additionalReplacements": [{"variable": "capacidad", "replacement": 0}],
                  "outputs": ["Datos invalidos", "Datos invalidos\n"]},
    },
}


def sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_published_bytes(name, payload):
    if sha256(payload) != PUBLICATIONS[name]["sha256"]:
        raise ValueError(f"Joint-variable QA differs from its complete authenticated publication: {name}")
    return payload


def published_bytes(name):
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(name, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{PUBLISHED_COMMIT}:{PUBLICATIONS[name]['path']}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def replace_once(source, previous, revised):
    if source.count(previous) != 1:
        raise ValueError("The authenticated joint-variable counterexample is not a unique source edit")
    return source.replace(previous, revised, 1)


def negative_source(name, source):
    if name == "u3l7":
        return replace_once(source,
            '        const int faltantes = solicitadas - disponibles;\n\n'
            '        std::cout << "Pendiente: " << faltantes;',
            '        std::cout << "Completo";')
    if name == "u3l8":
        return replace_once(source,
            '    if ((fichas < 0) || (fichas > 30) || (capacidad < 1) || (capacidad > 6)) {\n'
            '        std::cout << "Datos invalidos";\n'
            '    } else if (fichas == 0) {\n'
            '        std::cout << "Sin trabajo";',
            '    if (fichas == 0) {\n'
            '        std::cout << "Sin trabajo";\n'
            '    } else if ((fichas < 0) || (fichas > 30) || (capacidad < 1) || (capacidad > 6)) {\n'
            '        std::cout << "Datos invalidos";')
    raise ValueError(f"Unknown joint-variable counterexample: {name}")


def lesson_payloads(name):
    publication = PUBLICATIONS[name]
    original = published_bytes(name)
    before = state.parse_json(original.decode("utf-8"))
    state.validate(before)
    after = deepcopy(before)
    activity = publication["activity"]
    checks = after.get("compilerLocalIntChecks", [])
    matches = [entry for entry in checks if entry.get("activity") == activity]
    if (len(matches) != 1 or len(matches[0]["probes"]) != publication["originalProbeCount"]
            or any("additionalReplacements" in probe for probe in matches[0]["probes"])):
        raise ValueError(f"Joint-variable QA lost its original independent observations: {name}")
    matches[0]["probes"].append(deepcopy(publication["probe"]))
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    next(entry for entry in preserved["compilerLocalIntChecks"]
         if entry["activity"] == activity)["probes"].pop()
    if preserved != before:
        raise ValueError(f"Joint-variable QA changed an existing field: {name}")

    blocks = state._blocks(before)
    references = publication["references"]
    if ({identity for identity, block in blocks.items() if block.get("type") == "cpp_compiler"}
            != set(references)):
        raise ValueError(f"Joint-variable QA does not enumerate every original editor: {name}")
    authored = {entry["activity"]: entry["blocks"] for entry in before["referenceAfter"]}
    solutions = []
    for owner, reference in references.items():
        block = blocks[reference]
        if (reference not in authored.get(owner, ()) or block.get("type") != "code"
                or block.get("language") != "cpp"):
            raise ValueError(f"Joint-variable QA lost its authentic reference: {name}/{owner}")
        source = block["code"]
        solutions.append({"activity": owner, "source": source,
                          "sourceSha256": sha256(source.encode("utf-8"))})
    source = blocks[references[activity]]["code"]
    negative = negative_source(name, source)
    if source == negative:
        raise ValueError("The joint-variable counterexample is unchanged")
    first = blocks[activity]["outputCheck"]["cases"]
    if (not before.get("compilerPuzzlesRequired") or len(first) != 1
            or first[0]["standardInput"] != ""):
        raise ValueError(f"Joint-variable QA changed its original public case: {name}")
    before_name, after_name = f"{name}/before.json", f"{name}/after.json"
    revised = json_bytes(after)
    return {before_name: original, after_name: revised}, {
        "id": name, "before": before_name, "after": after_name,
        "beforeSha256": sha256(original), "afterSha256": sha256(revised),
        "solutions": solutions,
        "plans": [{"activity": activity, "reference": references[activity],
                   "fixedSource": negative,
                   "fixedSourceSha256": sha256(negative.encode("utf-8"))}],
    }


def export_payloads():
    payloads, lessons = {}, []
    for name in PUBLICATIONS:
        files, lesson = lesson_payloads(name)
        payloads.update(files)
        lessons.append(lesson)
    payloads["manifest.json"] = json_bytes({
        "version": 1, "kind": "joint-local-int", "publishedCommit": PUBLISHED_COMMIT,
        "lessons": lessons,
    })
    return payloads


def export(output_dir):
    output = checked_output(output_dir)
    payloads = export_payloads()
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
    arguments = parser.parse_args()
    print(export(arguments.output_dir))

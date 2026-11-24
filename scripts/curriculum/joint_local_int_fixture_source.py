"""Complete authenticated U3L7/U3L8/U3L5 publications for joint-variable QA.

The original lesson bytes are immutable. Each isolated AFTER copy appends
joint observations to one existing owner; no authored block or history changes.
U3L7/U3L8 retain their original fixture bytes and publication pin. U3L5 has its
own explicit publication pin and two complementary decision counterexamples.
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
    "u3l5": {
        "publishedCommit": "08a11877a2d2c0bf96a846660e3baea69e53f758",
        "path": CONTENT + "lesson_combinar_condiciones.json",
        "sha256": "d5d696e9f88ccdaa9b997d00bcd93c677f8e6af861eaef82e9409d3374725c67",
        "blobSha": "c0e67f1c87127e974decb0396bc3adfe33526a9a",
        "activity": "p1u3l5_reparar_parentesis", "originalProbeCount": 3,
        "references": {
            "p1u3l5_reparar_parentesis": "p1u3l5_ejemplo_reparacion",
            "p1u3l5_construir_acceso": "p1u3l5_ejemplo_acceso",
        },
        "referenceSha256": {
            "p1u3l5_reparar_parentesis": "f26acbb7524bf3761fd46912e517228359308ce4c50946a783f291db2d772d45",
            "p1u3l5_construir_acceso": "f48459e28178c9d73f92c617a8a637fbe3fe9a1e3805f245ad7978935a6f0e4d",
        },
        "probes": (
            {"variable": "pase", "replacement": 0,
             "additionalReplacements": [
                 {"variable": "invitacion", "replacement": 1},
                 {"variable": "apertura", "replacement": 1}],
             "outputs": ["Entrada permitida\nDatos: 0 1 1", "Entrada permitida\nDatos: 0 1 1\n"]},
            {"variable": "pase", "replacement": 0,
             "additionalReplacements": [{"variable": "apertura", "replacement": 1}],
             "outputs": ["Entrada denegada\nDatos: 0 0 1", "Entrada denegada\nDatos: 0 0 1\n"]},
        ),
    },
}

U3L5_COUNTEREXAMPLES = (
    {"label": "omits_invitation", "expression": "tiene_pase && sala_abierta",
     "sourceSha256": "f7496334ce93aa0daf943cd962a11659b50df77f3d231aea7e3742fc9ae8ae1a",
     "failedProbeIndex": 3},
    {"label": "open_only", "expression": "sala_abierta",
     "sourceSha256": "66951ed1a36227f4723585864efb290d2c35a8576db57d124bc3f05a97f7b8f1",
     "failedProbeIndex": 4},
)


def sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_published_bytes(name, payload):
    if sha256(payload) != PUBLICATIONS[name]["sha256"]:
        raise ValueError(f"Joint-variable QA differs from its complete authenticated publication: {name}")
    if name == "u3l5":
        blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
        if blob != PUBLICATIONS[name].get("blobSha"):
            raise ValueError("Joint-variable U3L5 QA differs from its pinned Git blob")
    return payload


def publication_commit(name):
    # The two historical lessons keep the original global pin. U3L5 must not
    # silently fall back to that older publication if its explicit pin is lost.
    return PUBLICATIONS[name]["publishedCommit"] if name == "u3l5" else PUBLISHED_COMMIT


def published_bytes(name):
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(name, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{publication_commit(name)}:{PUBLICATIONS[name]['path']}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def replace_once(source, previous, revised):
    if source.count(previous) != 1:
        raise ValueError("The authenticated joint-variable counterexample is not a unique source edit")
    return source.replace(previous, revised, 1)


def u3l5_counterexamples(source):
    publication = PUBLICATIONS["u3l5"]
    if sha256(source.encode("utf-8")) != publication["referenceSha256"][publication["activity"]]:
        raise ValueError("Joint-variable U3L5 counterexamples require the exact published reference")
    declaration = "    const bool permitido = (tiene_pase || tiene_invitacion) && sala_abierta;"
    examples = []
    for pin in U3L5_COUNTEREXAMPLES:
        negative = replace_once(source, declaration, "    const bool permitido = " + pin["expression"] + ";")
        digest = sha256(negative.encode("utf-8"))
        if digest != pin["sourceSha256"]:
            raise ValueError("Joint-variable U3L5 counterexample differs from its independent source pin")
        examples.append({"label": pin["label"], "source": negative, "sourceSha256": digest,
                         "failedProbeIndex": pin["failedProbeIndex"]})
    return examples


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
    if name == "u3l5":
        return next(example["source"] for example in u3l5_counterexamples(source)
                    if example["label"] == "open_only")
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
    additions = publication["probes"] if "probes" in publication else (publication["probe"],)
    matches[0]["probes"].extend(deepcopy(additions))
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    preserved_probes = next(entry for entry in preserved["compilerLocalIntChecks"]
                            if entry["activity"] == activity)["probes"]
    del preserved_probes[publication["originalProbeCount"]:]
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
        digest = sha256(source.encode("utf-8"))
        if name == "u3l5" and digest != publication["referenceSha256"][owner]:
            raise ValueError(f"Joint-variable U3L5 QA changed an authentic reference: {owner}")
        solutions.append({"activity": owner, "source": source,
                          "sourceSha256": digest})
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
    plan = {"activity": activity, "reference": references[activity],
            "fixedSource": negative, "fixedSourceSha256": sha256(negative.encode("utf-8"))}
    if name == "u3l5":
        plan["originalProbeCount"] = publication["originalProbeCount"]
        plan["counterexamples"] = u3l5_counterexamples(source)
    lesson = {
        "id": name, "before": before_name, "after": after_name,
        "beforeSha256": sha256(original), "afterSha256": sha256(revised),
        "solutions": solutions, "plans": [plan],
    }
    if name == "u3l5":
        lesson["publishedCommit"] = publication_commit(name)
    return {before_name: original, after_name: revised}, lesson


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

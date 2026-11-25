"""Authenticate the complete U3L6 publication for guarded-arithmetic Win32 QA.

The original publication is read only from its pinned Git object. The isolated
AFTER copy appends two explicit observations to the existing local-int owner;
all public exercises, histories, semantic ranges and references stay intact.
The separate --active cohort authenticates the current edition without
relabeling or changing the default historical fixture.
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
PUBLISHED_COMMIT = "08a11877a2d2c0bf96a846660e3baea69e53f758"
PUBLISHED_PATH = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_validez_y_cortocircuito.json")
PUBLISHED_SHA256 = "9d7ca6b5d82b2dac1809ab01b8b308f9e81027c4c4c6ce796502b42cda7ca8ea"
PUBLISHED_BLOB_SHA = "c126b16c872d80f85f20c41d7636d9240d911e1d"
ACTIVITY = "p1u3l6_reparar_resto"
REFERENCE = "p1u3l6_ejemplo_resto"
REFERENCES = {
    ACTIVITY: REFERENCE,
    "p1u3l6_construir_reparto": "p1u3l6_ejemplo_reparto",
}
REFERENCE_SHA256 = {
    ACTIVITY: "134ba6d6c94ba286a38d7db970af6df951668d3dee629225c977364c9561bbce",
    "p1u3l6_construir_reparto": "cc7b0a8c1bd48f6066cbbc53077ee5e28a2df63c672f16289a13a4b9422e1224",
}
INITIAL_SHA256 = "f3a1789f7ed26069d308ec6095346203fcf5b669097eeb1138cf77949764cb21"
ORIGINAL_PROBES = (
    {"variable": "total", "replacement": 30,
     "outputs": ["1\nGrupos: 5", "1\nGrupos: 5\n"]},
    {"variable": "capacidad", "replacement": 4,
     "outputs": ["1\nGrupos: 7", "1\nGrupos: 7\n"]},
    {"variable": "capacidad", "replacement": -1,
     "outputs": ["0\nCapacidad invalida", "0\nCapacidad invalida\n"]},
)

ACTIVE_COMMIT = "39ba3cb196e196bac99c2d891b488f765679ad6d"
ACTIVE_PATH = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "edition_es_1__expresiones_y_decisiones/lesson_validez_y_cortocircuito.json")
ACTIVE_SHA256 = "5c60bcac3bf8fd75ceb379dcce348553d90dbb27373ac5bffb01beea72a4853e"
ACTIVE_BLOB_SHA = "064ddec6a1b9915b6f3cf2352268a8cf4f50fbd2"
ACTIVE_ACTIVITY = "block_0003_0002"
ACTIVE_REFERENCE = "block_0003_0005"
ACTIVE_REFERENCES = {
    ACTIVE_ACTIVITY: ACTIVE_REFERENCE,
    "block_0006_0002": "block_0006_0006",
}
ACTIVE_REFERENCE_SHA256 = {
    ACTIVE_ACTIVITY: "134ba6d6c94ba286a38d7db970af6df951668d3dee629225c977364c9561bbce",
    "block_0006_0002": "cc7b0a8c1bd48f6066cbbc53077ee5e28a2df63c672f16289a13a4b9422e1224",
}


def sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_published_bytes(payload, active=False):
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    digest, blob_pin = (ACTIVE_SHA256, ACTIVE_BLOB_SHA) if active else (PUBLISHED_SHA256, PUBLISHED_BLOB_SHA)
    if sha256(payload) != digest or blob != blob_pin:
        raise ValueError("Guarded-arithmetic QA differs from the complete authenticated U3L6 publication")
    return payload


def published_bytes(active=False):
    commit, path = (ACTIVE_COMMIT, ACTIVE_PATH) if active else (PUBLISHED_COMMIT, PUBLISHED_PATH)
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
             "show", f"{commit}:{path}"],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        raise RuntimeError("Guarded-arithmetic QA requires its pinned Git source; "
                           "no fetch or current-asset fallback is permitted") from error
    return verify_published_bytes(payload, active=active)


def guarded_probes():
    return [{"variable": "capacidad", "replacement": value,
             "outputs": ["0\nCapacidad invalida", "0\nCapacidad invalida\n"],
             "forbidArithmetic": True} for value in (0, -2)]


def replace_once(source, previous, revised):
    if source.count(previous) != 1:
        raise ValueError("The guarded-arithmetic variation lost its unique authenticated source edit")
    return source.replace(previous, revised, 1)


def source_variants(reference, initial):
    if (sha256(reference.encode("utf-8")) != REFERENCE_SHA256[ACTIVITY]
            or sha256(initial.encode("utf-8")) != INITIAL_SHA256):
        raise ValueError("Guarded-arithmetic source variants require both exact published programs")
    declaration = "    const bool valido_y_exacto = (capacidad > 0) && (total % capacidad == 0);"
    variants = [
        ("reference", reference, True, None),
        ("initial", initial, False, None),
        ("reversed_and", replace_once(reference, declaration,
            "    const bool valido_y_exacto = (total % capacidad == 0) && (capacidad > 0);"), False, 91),
        ("eager_and", replace_once(reference, declaration,
            "    const bool valido_y_exacto = (capacidad > 0) & (total % capacidad == 0);"), False, 91),
        ("precomputed", replace_once(replace_once(reference, declaration,
            "    const int grupos = total / capacidad;\n"
            "    const int resto = total % capacidad;\n"
            "    const bool valido_y_exacto = (capacidad > 0) && (resto == 0);"),
            '        std::cout << "Grupos: " << total / capacidad;',
            '        std::cout << "Grupos: " << grupos;'), False, 91),
        ("guarded_if", replace_once(reference, declaration,
            "    bool valido_y_exacto = false;\n"
            "    if (capacidad > 0) {\n"
            "        valido_y_exacto = (total % capacidad == 0);\n"
            "    }"), True, None),
        ("guarded_ternary", replace_once(reference, declaration,
            "    const bool valido_y_exacto = (capacidad > 0) ? (total % capacidad == 0) : false;"), True, None),
        ("negated_or", replace_once(reference, declaration,
            "    const bool valido_y_exacto = !((capacidad <= 0) || (total % capacidad != 0));"), True, None),
        ("keyword_and", replace_once(reference, declaration,
            "    const bool valido_y_exacto = (capacidad > 0) and (total % capacidad == 0);"), True, None),
        ("primitive_casts", replace_once(reference, declaration,
            "    const bool valido_y_exacto = static_cast<bool>(capacidad > 0) && "
            "(int(total) % static_cast<int>(capacidad) == 0);"), True, None),
    ]
    return [{"label": label, "source": source, "sourceSha256": sha256(source.encode("utf-8")),
             "accepted": accepted, **({"guardExit": guard_exit} if guard_exit is not None else {})}
            for label, source, accepted, guard_exit in variants]


def lesson_payloads(active=False):
    activity, reference = (ACTIVE_ACTIVITY, ACTIVE_REFERENCE) if active else (ACTIVITY, REFERENCE)
    references = ACTIVE_REFERENCES if active else REFERENCES
    reference_sha256 = ACTIVE_REFERENCE_SHA256 if active else REFERENCE_SHA256
    original = published_bytes(active=active)
    before = state.parse_json(original.decode("utf-8"))
    state.validate(before)
    if before.get("compilerLocalIntChecks") != [
            {"activity": activity, "probes": list(ORIGINAL_PROBES)}]:
        raise ValueError("Guarded-arithmetic QA lost its exact three original local observations")
    after = deepcopy(before)
    after["compilerLocalIntChecks"][0]["probes"].extend(guarded_probes())
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    del preserved["compilerLocalIntChecks"][0]["probes"][len(ORIGINAL_PROBES):]
    if preserved != before:
        raise ValueError("Guarded-arithmetic QA changed a field outside the explicit two-probe suffix")

    blocks = state._blocks(before)
    if {identity for identity, block in blocks.items() if block.get("type") == "cpp_compiler"} != set(references):
        raise ValueError("Guarded-arithmetic QA does not enumerate both authentic editors")
    relations = {entry["activity"]: entry["blocks"] for entry in before["referenceAfter"]}
    solutions = []
    for owner, reference_id in references.items():
        block = blocks[reference_id]
        if (reference_id not in relations.get(owner, ()) or block.get("type") != "code"
                or block.get("language") != "cpp"):
            raise ValueError("Guarded-arithmetic QA lost an authentic reference relation")
        source = block["code"]
        digest = sha256(source.encode("utf-8"))
        if digest != reference_sha256[owner]:
            raise ValueError("Guarded-arithmetic QA changed an authentic reference program")
        solutions.append({"activity": owner, "source": source, "sourceSha256": digest})
    owner = blocks[activity]
    if (not before.get("compilerPuzzlesRequired") or owner["outputCheck"] != {"cases": [
            {"standardInput": "", "outputs": ["0\nGrupos: 4", "0\nGrupos: 4\n"]}]}):
        raise ValueError("Guarded-arithmetic QA changed the single original public case")
    variants = source_variants(blocks[reference]["code"], owner["initialCode"])
    false_positive = next(item for item in variants if item["label"] == "reversed_and")
    revised = json_bytes(after)
    lesson = {
        "id": "u3l6", "before": "u3l6/before.json", "after": "u3l6/after.json",
        "beforeSha256": sha256(original), "afterSha256": sha256(revised),
        "solutions": solutions,
        "plans": [{"activity": activity, "reference": reference,
                   "originalProbeCount": len(ORIGINAL_PROBES),
                   "fixedSource": false_positive["source"],
                   "fixedSourceSha256": false_positive["sourceSha256"],
                   "sources": variants}],
    }
    if active:
        lesson["publishedCommit"] = ACTIVE_COMMIT
    return {"u3l6/before.json": original, "u3l6/after.json": revised}, lesson


def export_payloads():
    payloads, lesson = lesson_payloads()
    payloads["manifest.json"] = json_bytes({
        "version": 1, "kind": "guarded-local-arithmetic", "publishedCommit": PUBLISHED_COMMIT,
        "lessons": [lesson],
    })
    return payloads


def active_payloads():
    payloads, lesson = lesson_payloads(active=True)
    payloads["manifest.json"] = json_bytes({
        "version": 1, "kind": "active-guarded-arithmetic", "publishedCommit": ACTIVE_COMMIT,
        "lessons": [lesson],
    })
    return payloads


def export(output_dir, active=False):
    output = checked_output(output_dir)
    payloads = active_payloads() if active else export_payloads()
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
    parser.add_argument("--active", action="store_true", help="Export the pinned active course edition")
    arguments = parser.parse_args()
    print(export(arguments.output_dir, active=arguments.active))

"""Pinned U2L9 bytes and isolated private-input authoring for Windows QA."""
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
PUBLISHED_COMMIT = "51fc2709cf802acaa9445150fb33acc93bdf3eeb"
PUBLISHED_RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "objetos_y_estado/lesson_leer_una_entrada_conocida.json"
)
PUBLISHED_SHA256 = "37b473d7e9b66ab8232812e4e2c9a25e0aaa207bcfc92059fcd6fc675401d94f"
OWNERS = ("p1u2l9_reparar_lectura_de_cajas", "block_0009_0003")

# The three additional editors belong to one authentic main publication.
# U3L1/U3L2 are not copied into Codex curriculum merely to exercise the runner.
EXTENDED_COMMIT = "2ca32b2337178d1effa3a2c81759e2493cd746d0"
EXTENDED_PUBLICATIONS = {
    "u2l10": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_representar_procesar_y_recuperar.json",
        "sha256": "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0",
        "activity": "p1u2l10_reparar_registro",
        "reference": "p1u2l10_ejemplo_copia_reparada",
        "referenceSha256": "ed1c83bff583431507d0260a596f080294f65561d5b155d8497a91f1aec6e6ed",
        "publicCases": (("4 5 2", "Inicio: 4\nAhora: 7"),
                        ("0 2 1", "Inicio: 0\nAhora: 1"),
                        ("6 0 2", "Inicio: 6\nAhora: 4")),
        "privateCases": (("6 5 2", "Inicio: 6\nAhora: 9"),
                         ("4 2 2", "Inicio: 4\nAhora: 4"),
                         ("4 5 1", "Inicio: 4\nAhora: 8")),
    },
    "u3l1": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json",
        "sha256": "0c4f3884ae261d3f27015de5e91d776efb4bf1dd521b725f3fc3203134fef716",
        "activity": "p1u3l1_construir_puntuacion",
        "reference": "p1u3l1_ejemplo_puntuacion",
        "referenceSha256": "8373f6490814b65622005006be6cafbae12b6cb1fc27b64db3812a5878fc100d",
        "publicCases": (("10 3 4", "Puntos: 18"),
                        ("7 0 2", "Puntos: 16"),
                        ("5 5 3", "Puntos: 3")),
        "privateCases": (("7 0 2", "Puntos: 16"),
                         ("5 5 3", "Puntos: 3")),
    },
    "u3l2": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_division_y_resto.json",
        "sha256": "0f0fccd697fc9bd24ac1087bb735cdb89ecf362b9b8b35964790b94a5c48b596",
        "activity": "p1u3l2_construir_informe_bandejas",
        "reference": "p1u3l2_ejemplo_bandejas",
        "referenceSha256": "36637787dc00da0ce5874a7974398768cf0d820d979a7aa2fc58fe59751eb588",
        "publicCases": (("29 6", "Bandejas completas: 4\nSobrantes: 5"),
                        ("24 6", "Bandejas completas: 4\nSobrantes: 0"),
                        ("3 6", "Bandejas completas: 0\nSobrantes: 3"),
                        ("0 6", "Bandejas completas: 0\nSobrantes: 0"),
                        ("29 4", "Bandejas completas: 7\nSobrantes: 1")),
        "privateCases": (("24 6", "Bandejas completas: 4\nSobrantes: 0"),
                         ("3 6", "Bandejas completas: 0\nSobrantes: 3"),
                         ("0 6", "Bandejas completas: 0\nSobrantes: 0"),
                         ("29 4", "Bandejas completas: 7\nSobrantes: 1")),
    },
}


def extended_declarations(name):
    source = EXTENDED_PUBLICATIONS[name]
    return [{"activity": source["activity"], "probes": [
        {"standardInput": stdin, "outputs": [stdout, stdout + "\n"]}
        for stdin, stdout in source["privateCases"]
    ]}]


def verify_extended_bytes(name, payload):
    if hashlib.sha256(payload).hexdigest() != EXTENDED_PUBLICATIONS[name]["sha256"]:
        raise ValueError(f"Extended input QA differs from its exact main publication: {name}")
    return payload


def extended_published_bytes(name):
    source = EXTENDED_PUBLICATIONS[name]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_extended_bytes(name, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{EXTENDED_COMMIT}:{source['path']}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def extended_pair(name):
    source = EXTENDED_PUBLICATIONS[name]
    before = state.parse_json(extended_published_bytes(name).decode("utf-8"))
    if "compilerInputChecks" in before:
        raise ValueError(f"The authentic {name} source already has input checks")
    blocks = state._blocks(before)
    owner, reference = blocks[source["activity"]], blocks[source["reference"]]
    expected = [{"standardInput": stdin, "outputs": [stdout, stdout + "\n"]}
                for stdin, stdout in source["publicCases"]]
    if (not before.get("compilerPuzzlesRequired") or
            owner.get("initialStandardInput") != expected[0]["standardInput"] or
            owner.get("outputCheck") != {"inputComparison": "tokens", "cases": expected} or
            reference.get("type") != "code" or reference.get("language") != "cpp" or
            hashlib.sha256(reference["code"].encode("utf-8")).hexdigest() != source["referenceSha256"]):
        raise ValueError(f"The authentic {name} public cases or reference changed")
    after = deepcopy(before)
    after["compilerInputChecks"] = extended_declarations(name)
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    del preserved["compilerInputChecks"]
    if preserved != before:
        raise ValueError(f"Extended input QA changed an old field: {name}")
    return before, after


def declarations():
    return [
        {"activity": OWNERS[0], "probes": [
            {"standardInput": "Leo 2", "outputs": ["Leo: 5", "Leo: 5\n"]},
            {"standardInput": "Rita 2", "outputs": ["Rita: 5", "Rita: 5\n"]},
            {"standardInput": "Leo 4", "outputs": ["Leo: 7", "Leo: 7\n"]},
        ]},
        {"activity": OWNERS[1], "probes": [
            {"standardInput": "Leo 4 2", "outputs": ["Leo: 22", "Leo: 22\n"]},
            {"standardInput": "Iris 3 2", "outputs": ["Iris: 17", "Iris: 17\n"]},
            {"standardInput": "Iris 4 1", "outputs": ["Iris: 21", "Iris: 21\n"]},
        ]},
    ]


def verify_published_bytes(payload):
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("Input dependency QA differs from its exact published U2L9 source")
    return payload


def published_bytes():
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{PUBLISHED_COMMIT}:{PUBLISHED_RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def published_pair():
    before = state.parse_json(published_bytes().decode("utf-8"))
    if "compilerInputChecks" in before:
        raise ValueError("The original U2L9 source unexpectedly has private input checks")
    after = deepcopy(before)
    after["compilerInputChecks"] = declarations()
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    del preserved["compilerInputChecks"]
    if preserved != before:
        raise ValueError("Input QA changed something other than its new root contract")
    return before, after


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def export(output_dir):
    output = checked_output(output_dir)
    before, after = published_pair()
    payloads = {"before.json": published_bytes(), "after.json": json_bytes(after)}
    if state.parse_json(payloads["before.json"].decode("utf-8")) != before:
        raise ValueError("The exported historical input source differs from its validated source")
    payloads["manifest.json"] = json_bytes({
        "publishedCommit": PUBLISHED_COMMIT, "publishedPath": PUBLISHED_RELATIVE,
        "activities": list(OWNERS),
        "files": {name: hashlib.sha256(payload).hexdigest()
                  for name, payload in payloads.items()},
    })
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    for name, payload in payloads.items():
        if not existing_matches(output / name, payload):
            with (output / name).open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


def export_extended(output_dir):
    output = checked_output(output_dir)
    payloads = {}
    for name, source in EXTENDED_PUBLICATIONS.items():
        before, after = extended_pair(name)
        files = {"before.json": extended_published_bytes(name),
                 "after.json": json_bytes(after)}
        if state.parse_json(files["before.json"].decode("utf-8")) != before:
            raise ValueError(f"The exported {name} bytes differ from their validated source")
        files["manifest.json"] = json_bytes({
            "publishedCommit": EXTENDED_COMMIT, "publishedPath": source["path"],
            "activities": [source["activity"]], "reference": source["reference"],
            "referenceSha256": source["referenceSha256"],
            "files": {filename: hashlib.sha256(payload).hexdigest()
                      for filename, payload in files.items()},
        })
        payloads.update({Path(name) / filename: payload for filename, payload in files.items()})
    # Authenticate all sources and reject any conflict before creating files.
    for relative, payload in payloads.items():
        existing_matches(output / relative, payload)
    for relative, payload in payloads.items():
        target = output / relative
        checked_output(target.parent)
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        if existing_matches(target, payload):
            continue
        try:
            with target.open("xb") as stream:
                stream.write(payload)
        except FileExistsError:
            if not existing_matches(target, payload):
                raise
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--extended-input", action="store_true",
                        help="Export U2L10/U3L1/U3L2 from the same pinned main publication")
    arguments = parser.parse_args()
    print(export_extended(arguments.output_dir) if arguments.extended_input else export(arguments.output_dir))

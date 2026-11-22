"""Pinned U2L10 publication and isolated name-independent local-role authoring."""
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
PUBLISHED_COMMIT = "2ca32b2337178d1effa3a2c81759e2493cd746d0"
PUBLISHED_RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "objetos_y_estado/lesson_representar_procesar_y_recuperar.json"
)
PUBLISHED_SHA256 = "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0"
OWNERS = ("block_0004_0002", "block_0007_0002")
REFERENCE_SHA256 = {
    "p1u2l10_ejemplo_informe": "aa4eb33b3a076e673d44fe5b4bcdbdb56705134f0eacfae81289f9fd90c805b8",
    "block_0008_0001": "342e5e70b2e85f3292bcbb3eb35c85a7ed7ca474cd371b5bf6069dd03dc3977e",
}
ARCHIVED_SHA256 = {
    "block_0005_0001": "5e1ddfa0a4bb94990baf862a34d0e1883cb4ded5e9911d59ce6ac5a1e6b6d617",
    "block_0009_0003": "5ee352e21ab022e4e9f726ee99ff7e8575ef60eb28c2312a6c738f16d97903e8",
    "block_0010_0003": "5aa2b60aef1f6366931ef348e06a67db596ec4c54fcce033a7ecc3671d6eb8c6",
}


def alternatives(output):
    return [output, output + "\n"]


def declarations():
    station = "Puesto 03\nMesas: 6\nCable: "
    library = "Modulo 02\nInicio: "
    return [
        {"activity": OWNERS[0], "roles": [
            {"id": "first", "valueType": "double", "initialValue": 1.5,
             "probes": [{"replacement": 2.5, "outputs": alternatives(station + "4.75 m")}]},
            {"id": "second", "valueType": "double", "initialValue": 2.25,
             "probes": [{"replacement": 4.25, "outputs": alternatives(station + "5.75 m")}]},
            {"id": "total", "valueType": "double", "initialValue": 3.75,
             "probes": [{"replacement": 9.25, "outputs": alternatives(station + "9.25 m")}]},
        ]},
        {"activity": OWNERS[1], "roles": [
            {"id": "initial", "valueType": "int", "initialValue": 7,
             "probes": [{"replacement": 11, "outputs": alternatives(library + "11\nAhora: 14")}]},
            {"id": "current", "valueType": "int", "initialValue": 7,
             "probes": [{"replacement": 2, "afterUpdate": True, "expectedIncrease": 3,
                         "outputs": alternatives(library + "7\nAhora: 12")}]},
            {"id": "saved", "valueType": "int", "initialValue": 7,
             "probes": [{"replacement": 19, "beforeUpdateOf": "current",
                         "outputs": alternatives(library + "19\nAhora: 10")}]},
        ]},
    ]


def verify_published_bytes(payload):
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("Local-role QA differs from the exact published main U2L10 source")
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
    if "compilerLocalRoleChecks" in before:
        raise ValueError("The original U2L10 publication unexpectedly has local roles")
    after = deepcopy(before)
    after["compilerLocalRoleChecks"] = declarations()
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    del preserved["compilerLocalRoleChecks"]
    if preserved != before:
        raise ValueError("Local-role QA changed something other than its new root contract")
    return before, after


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def export(output_dir):
    output = checked_output(output_dir)
    before, after = published_pair()
    payloads = {"before.json": published_bytes(), "after.json": json_bytes(after)}
    if state.parse_json(payloads["before.json"].decode("utf-8")) != before:
        raise ValueError("The exported historical source differs from its validated publication")
    payloads["manifest.json"] = json_bytes({
        "publishedCommit": PUBLISHED_COMMIT, "publishedPath": PUBLISHED_RELATIVE,
        "activities": list(OWNERS), "referenceSourceSha256": REFERENCE_SHA256,
        "archivedSourceSha256": ARCHIVED_SHA256,
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    print(export(arguments.output_dir))

"""Exact published U2L8 input and isolated char/bool authoring for Windows QA."""
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
PUBLISHED_COMMIT = "19d83972a85beac36ce4c0b45cf40d383f002393"
PUBLISHED_RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "objetos_y_estado/lesson_caracteres_y_valores_logicos.json"
)
PUBLISHED_SHA256 = "d44ea3aca4844b169373abd9cc5bf31d59fa03ec53b3ea9b4cd686f6ddca4b6c"
OWNER = "block_0006_0002"


def declarations():
    return [{"activity": OWNER, "probes": [
        {"variable": "pasillo", "valueType": "char", "replacement": "D",
         "outputs": ["Pasillo: D\nAbierta: 1", "Pasillo: D\nAbierta: 1\n"]},
        {"variable": "abierta", "valueType": "bool", "replacement": False,
         "outputs": ["Pasillo: C\nAbierta: 0", "Pasillo: C\nAbierta: 0\n"]},
    ]}]


def verify_published_bytes(payload):
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("Local scalar QA differs from its exact published U2L8 source")
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
    if "compilerLocalIntChecks" in before:
        raise ValueError("The original U2L8 source unexpectedly has private probes")
    after = deepcopy(before)
    after["compilerLocalIntChecks"] = declarations()
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    del preserved["compilerLocalIntChecks"]
    if preserved != before:
        raise ValueError("Scalar QA changed something other than its new root contract")
    return before, after


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def export(output_dir):
    output = checked_output(output_dir)
    before, after = published_pair()
    payloads = {"before.json": published_bytes(), "after.json": json_bytes(after)}
    if state.parse_json(payloads["before.json"].decode("utf-8")) != before:
        raise ValueError("The exported historical source differs from the validated source")
    payloads["manifest.json"] = json_bytes({
        "publishedCommit": PUBLISHED_COMMIT, "publishedPath": PUBLISHED_RELATIVE,
        "activity": OWNER,
        "files": {name: hashlib.sha256(payload).hexdigest()
                  for name, payload in payloads.items()},
    })
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    output.mkdir(parents=True, exist_ok=True)
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

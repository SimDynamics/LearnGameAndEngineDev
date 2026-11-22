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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    print(export(arguments.output_dir))

"""Export one authentic U2L9 publication for Windows current/history QA."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess

from diagnostic_fixture_source import checked_output, existing_matches


ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "d90d315681ddbe7f66632ff11271283eae6f7565"
PUBLISHED_PATH = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "objetos_y_estado/lesson_leer_una_entrada_conocida.json"
)
PUBLISHED_SHA256 = "61ff67f148ad78c31036763889ebdb85e31ce315cb1103de516ee975819fd0e1"
FILENAME = "u2l9-archived.json"


def verify_bytes(payload: bytes) -> bytes:
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("Historical U2L9 SHA-256 mismatch; no current-source fallback")
    return payload


def historical_bytes() -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show",
         f"{PUBLISHED_COMMIT}:{PUBLISHED_PATH}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def export(output_dir: Path) -> Path:
    output = checked_output(output_dir)
    target = output / FILENAME
    payload = historical_bytes()
    if existing_matches(target, payload):
        return target
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    try:
        with target.open("xb") as stream:
            stream.write(payload)
    except FileExistsError:
        if not existing_matches(target, payload):
            raise
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    print(export(arguments.output_dir))

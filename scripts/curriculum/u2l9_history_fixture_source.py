"""Export authentic U2L9 publications for Windows reference/history QA."""
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
SAME_PAGE_COMMIT = "a658efea1d75f0fb017d484ae3936b8b4001cb12"
SAME_PAGE_SHA256 = "7a5eaa190463976dc686e288abe5f61eed0574849ca51e33a0bc5a762c569208"
SAME_PAGE_FILENAME = "u2l9-same-page.json"


def verify_bytes(payload: bytes, *, same_page: bool = False) -> bytes:
    expected = SAME_PAGE_SHA256 if same_page else PUBLISHED_SHA256
    if hashlib.sha256(payload).hexdigest() != expected:
        raise ValueError("Published U2L9 SHA-256 mismatch; no current-source fallback")
    return payload


def historical_bytes(*, same_page: bool = False) -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    commit = SAME_PAGE_COMMIT if same_page else PUBLISHED_COMMIT
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show",
         f"{commit}:{PUBLISHED_PATH}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), same_page=same_page)


def export(output_dir: Path, *, same_page: bool = False) -> Path:
    output = checked_output(output_dir)
    target = output / (SAME_PAGE_FILENAME if same_page else FILENAME)
    payload = historical_bytes(same_page=same_page)
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
    parser.add_argument("--same-page", action="store_true",
                        help="Export the separately authenticated a658efea publication")
    arguments = parser.parse_args()
    print(export(arguments.output_dir, same_page=arguments.same_page))

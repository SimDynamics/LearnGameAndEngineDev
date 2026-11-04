"""Export the authenticated pre-migration U11L17 document for native QA only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "90e510fd147b407093d203e57aa26b4bec887bb5"
LESSON = "cpp_fundamentals__texto_interpretacion_y_flujos__nombres_del_lenguaje_y_tokens_con_texto"
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "texto_interpretacion_y_flujos/lesson_nombres_del_lenguaje_y_tokens_con_texto.json")
SHA256 = "8a62de3022c0507a12d3cc372e12a92dc69801c7313a657516a7287b343d3003"


def historical_bytes(git: str) -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        [git, "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if len(payload) != 36663 or hashlib.sha256(payload).hexdigest() != SHA256:
        raise ValueError("U11L17 historical fixture differs from the pinned publication")
    document = json.loads(payload)
    if (document["id"] != LESSON or "stateLayout" in document or len(document["pages"]) != 8
            or sum(len(page["blocks"]) for page in document["pages"]) != 31):
        raise ValueError("U11L17 historical fixture lost its original unmapped layout")
    return payload


def export(output: Path, git: str) -> None:
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("U11L17 QA output cannot follow links or reparse points")
    output = output.resolve()
    if not any(output.is_relative_to(ROOT / folder) for folder in ("out", "build")):
        raise ValueError("U11L17 fixture output must stay in this checkout's generated QA/build area")
    for parent in (output.parent, *output.parent.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("U11L17 QA output cannot write another checkout")
    payload = historical_bytes(git)
    if output.exists():
        if not output.is_file() or output.read_bytes() != payload:
            raise FileExistsError("Refusing to replace a different U11L17 QA document")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(payload)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--git", required=True)
    arguments = parser.parse_args()
    export(arguments.output, arguments.git)
    print(f"Verified unmapped U11L17 fixture from {COMMIT}.")

"""Export the exact local pre-markup U2L3 document for Windows return tests."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[2]
COMMIT = "1fe097550c458d9267113d2b2409863d21c75d99"
RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_estado_asignacion_y_copia.json"
SHA256 = "1f88d26abb2b93108257f62a6e7914744bae8fe874f6664aac2fb7ad31ba8dcb"


def export(output: Path) -> Path:
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("Historical QA output cannot follow links or reparse points")
    output = output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "out") and not output.is_relative_to(ROOT / "build"):
        raise ValueError("Historical QA output cannot replace source files")
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("Historical QA output cannot write another checkout")
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
                                      "show", f"{COMMIT}:{RELATIVE}"], cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if hashlib.sha256(payload).hexdigest() != SHA256:
        raise ValueError("The local pre-markup document differs from its pinned Git bytes")
    target = output / Path(RELATIVE).name
    if target.exists() or target.is_symlink():
        attributes = target.lstat()
        if not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400 or target.read_bytes() != payload:
            raise FileExistsError("Refusing to replace an existing historical QA document")
        return target
    output.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as stream:
        stream.write(payload)
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    export(args.output_dir)
    print(f"Verified immutable pre-markup U2L3 fixture from local {COMMIT}.")

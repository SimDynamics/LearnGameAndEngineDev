"""Export authenticated A and intermediate B U2L3 documents for Windows return tests."""
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
INTERMEDIATE_COMMIT = "df2f9fa44737f857ffbe06fadb4843110b06d579"
INTERMEDIATE_SHA256 = "7c3e963deba83299f771924ec2c0d1d06ef1eae3db4763bdffe2457aa47f2c81"
INTERMEDIATE_NAME = "lesson_estado_asignacion_y_copia-df2f9fa.json"


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
    snapshots = []
    for commit, expected, name in ((COMMIT, SHA256, Path(RELATIVE).name),
                                    (INTERMEDIATE_COMMIT, INTERMEDIATE_SHA256, INTERMEDIATE_NAME)):
        payload = subprocess.check_output(["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
                                          "show", f"{commit}:{RELATIVE}"], cwd=ROOT, env=environment, stderr=subprocess.PIPE)
        if hashlib.sha256(payload).hexdigest() != expected:
            raise ValueError(f"The local historical document {commit} differs from its pinned Git bytes")
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400 or target.read_bytes() != payload:
                raise FileExistsError("Refusing to replace an existing historical QA document")
        snapshots.append((target, payload))
    # Authenticate and inspect BOTH outputs before writing either. An incorrect
    # B source must not leave a partial new archive or overwrite the valid A.
    output.mkdir(parents=True, exist_ok=True)
    for target, payload in snapshots:
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(payload)
    return snapshots[0][0]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    export(args.output_dir)
    print(f"Verified immutable U2L3 A from {COMMIT} and intermediate B from {INTERMEDIATE_COMMIT}.")

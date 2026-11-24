"""Export the authentic three-editor U2L5 publication to a disposable QA tree.

The complete Git file is byte-pinned and is never reconstructed from today's
retirement metadata. No fetch, branch change or fallback to live assets occurs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "1c6d202f98b13fdebcb3a0b80dbfef4432ce97e5"
RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_enteros_y_aproximaciones.json"
FILENAME = "lesson_enteros_y_aproximaciones.json"
BLOB = "6d0de470fd51b92d4ab3f82d706d7586b052a123"
SHA256 = "95a745294c792de7206360adecfb3b5e3b75e1a0db099ce6ba77fa5193c83826"
OWNERS = ((6, 2, "block_0006_0002"), (6, 6, "block_0006_0006"),
          (7, 9, "block_0007_0009"))


def historical_bytes() -> bytes:
    reference = f"{COMMIT}:{RELATIVE}"
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
             "show", reference], cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"Pinned local U2L5 fixture unavailable: {reference}. "
                           "Complete local history is required; no live-asset fallback is permitted.") from error
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    if blob != BLOB or hashlib.sha256(payload).hexdigest() != SHA256:
        raise ValueError("The archived U2L5 bytes differ from the pinned Git publication")
    return payload


def export(directory: Path) -> None:
    # Authenticate everything before creating or modifying any output.
    payload = historical_bytes()
    document = json.loads(payload)
    actual = tuple((page, block, document["stateLayout"]["current"][page]["blocks"][block])
                   for page, value in enumerate(document["pages"])
                   for block, entry in enumerate(value["blocks"])
                   if entry["type"] == "cpp_compiler")
    if actual != OWNERS or not document["compilerPuzzlesRequired"]:
        raise ValueError("The pinned U2L5 publication lost its original three-editor chain")
    manifest = {
        "commit": COMMIT, "path": RELATIVE, "blob": BLOB, "sha256": SHA256,
        "file": FILENAME, "bytes": len(payload), "lessonId": document["id"],
        "compilerOwners": [{"page": page, "block": block, "id": identity}
                           for page, block, identity in OWNERS],
        "scope": "Real rejected/accepted first editor; second-editor barrier and navigation to third; not three compiler executions",
        "syntheticOrdinaryPrefix": 12,
    }
    outputs = {FILENAME: payload,
               "manifest.json": (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")}
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        target = directory / name
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    export(options.output_dir)
    print(f"Authenticated complete archived U2L5: commit={COMMIT} blob={BLOB} sha256={SHA256}.")


if __name__ == "__main__":
    main()

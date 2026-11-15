"""Pinned AssetCheck source and in-memory prompt proposals for Windows QA.

This generator never modifies the curricular source. The source publication and
independent byte hash authenticate the complete predecessor used by Python and
the native worker. Output belongs only to the caller's test-fixtures directory.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "24a8fae8b2e464803e1a4b6016128497e8c48618"
RELATIVE = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
            "proyecto_assetcheck/lesson_leer_manifiesto_y_publicar_informe.json")
SOURCE_SHA256 = "666deef21b08dc09222738460c0cc041d2fe7554e2607d988e0f3ae1b43b3acb"
OWNER = "block_0002_0005"
ACTIVITY = "assetcheck_archivo_marcas_de_linea"
POSITION = (2, 5)
ORIGINAL_BLOCK_SHA256 = "185fef17d8f7300749d74909accad2f9b89136a16310eb00513e83f8d59b3048"
PROMPT = ("Relaciona la secuencia de bytes con la cantidad de líneas físicas. "
          "En las etiquetas, las secuencias con barra inversa representan LF o CR; "
          "no son una barra y una letra escritas en el manifiesto.")


def verify_bytes(payload: bytes) -> bytes:
    if hashlib.sha256(payload).hexdigest() != SOURCE_SHA256:
        raise ValueError("Math prompt fixture differs from its pinned publication")
    return payload


@lru_cache(maxsize=1)
def historical_bytes() -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{COMMIT}:{RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def authored_spans() -> list:
    # Explicit QA annotations exercise the real UTF-16 reader. Work remains
    # responsible for the final curriculum wording and semantic classifications.
    return [layout.text_semantics.select(PROMPT, token, "data", reading="natural")
            for token in ("LF", "CR")]


def candidate() -> tuple[dict, dict]:
    before = json.loads(historical_bytes())
    addresses = layout.validate(before)
    block = layout._blocks(before)[OWNER]
    if (before["id"] != "programming_practice_tools__proyecto_assetcheck__leer_manifiesto_y_publicar_informe"
            or block["type"] != "math_puzzle" or block["id"] != ACTIVITY
            or layout.fingerprint(block) != ORIGINAL_BLOCK_SHA256
            or addresses[POSITION] != POSITION
            or before["stateLayout"]["current"][POSITION[0]]["blocks"][POSITION[1]] != OWNER
            or set(before["stateLayout"]["family"]) != {"es"}):
        raise ValueError("Math prompt fixture identity, address or original block changed")
    after = layout.revise_math_prompt_editorial(before, OWNER, PROMPT, spans=authored_spans())
    layout.validate_transition(before, after)
    return deepcopy(before), after


def export_fixtures(output_dir: Path) -> tuple[Path, Path]:
    before, after = candidate()
    output_dir.mkdir(parents=True, exist_ok=True)
    paths = (output_dir / "assetcheck-prompt-before.json",
             output_dir / "assetcheck-prompt-after.json")
    # Re-running CMake may refresh generated QA files; never an asset source.
    for path, document in zip(paths, (before, after)):
        path.write_text(json.dumps(document, ensure_ascii=False, indent=2,
                                   allow_nan=False) + "\n",
                        encoding="utf-8", newline="\n")
    return paths


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    for path in export_fixtures(args.output_dir):
        print(path)

"""Export pinned pre-adoption compiler contracts into the native QA build tree.

The original local Git bytes are used without newline conversion. Existing
different files are never replaced; the live curriculum is never a fallback.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess

from diagnostic_fixture_source import ROOT, checked_output, existing_matches


UNIT = "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo"
FIXTURES = {
    "crt-b9e32b9": (
        "b9e32b9bff2ecc6071173abe5418fe99132b6fa6",
        "lesson_clasificar_caracteres_y_normalizar_una_copia.json",
        "d8ebd48f60f6c7ac382777e9d51c63d6854c1fee6056034d9d2351a064849116",
    ),
    "exit-585cef9": (
        "585cef9ff64dcc1bfbe37457ac6a59074650f978",
        "lesson_compartir_flujos_sin_acoplar_el_calculo.json",
        "09e6e7922ca9504bb0f485189d154c66d666e1e4c0c91fb0b7c4783fc0089488",
    ),
}


def historical_bytes(name: str) -> tuple[str, bytes]:
    commit, filename, digest = FIXTURES[name]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{UNIT}/{filename}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if hashlib.sha256(payload).hexdigest() != digest:
        raise RuntimeError(f"Pinned compiler contract differs from its local Git bytes: {name}")
    return filename, payload


def export_fixtures(output_dir: Path) -> tuple[Path, ...]:
    output = checked_output(output_dir)
    documents = {name: historical_bytes(name) for name in FIXTURES}
    for name, (filename, payload) in documents.items():
        existing_matches(output / name / filename, payload)
    for name, (filename, payload) in documents.items():
        target = output / name / filename
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        if not existing_matches(target, payload):
            try:
                with target.open("xb") as stream:
                    stream.write(payload)
            except FileExistsError:
                if not existing_matches(target, payload):
                    raise
    return tuple(output / name / filename for name, (filename, _) in documents.items())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        paths = export_fixtures(arguments.output_dir)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Compiler contract fixtures not exported: {error}\n")
    print(f"Verified {len(paths)} immutable compiler contract fixtures from local Git.")


if __name__ == "__main__":
    main()

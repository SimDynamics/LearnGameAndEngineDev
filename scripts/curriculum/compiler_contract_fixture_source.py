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
U6_UNIT = "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos"
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
    "exit-l26-e8389c8": (
        "e8389c86c3cd3b572cb77c6308103163a1e2cf7d",
        "lesson_construir_un_programa_de_carga_y_guardado.json",
        "6b6763a94875c720040322cfe259b7bf1693a743b042ffa30c1a24983e34fd67",
    ),
    "u6-diagnostics-e8389c8/declarations": (
        "e8389c86c3cd3b572cb77c6308103163a1e2cf7d", "lesson_declaraciones_y_firmas.json",
        "b5c659ef75033d6fbed9ce5bc42a640b89ce3d33a8f063aaf32292d2ea9a38b7",
    ),
    "u6-diagnostics-e8389c8/interfaces": (
        "e8389c86c3cd3b572cb77c6308103163a1e2cf7d", "lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json",
        "d250c29f303f8523ad74099086489565dce69699978ad57850a333439e31f595",
    ),
    "u6-diagnostics-e8389c8/project": (
        "e8389c86c3cd3b572cb77c6308103163a1e2cf7d", "lesson_construir_un_proyecto_desde_su_interfaz.json",
        "64b9e31d061e8d4e04e093c608b5be41a3c55367b6591028592ac82661c2bc73",
    ),
}


def historical_bytes(name: str) -> tuple[str, bytes]:
    commit, filename, digest = FIXTURES[name]
    unit = U6_UNIT if name.startswith("u6-") else UNIT
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{unit}/{filename}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if hashlib.sha256(payload).hexdigest() != digest:
        raise RuntimeError(f"Pinned compiler contract differs from its local Git bytes: {name}")
    return filename, payload


def export_fixtures(output_dir: Path) -> tuple[Path, ...]:
    output = checked_output(output_dir)
    documents = {name: historical_bytes(name) for name in FIXTURES}
    for name, (filename, payload) in documents.items():
        existing_matches(output / name.split("/", 1)[0] / filename, payload)
    for name, (filename, payload) in documents.items():
        target = output / name.split("/", 1)[0] / filename
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        if not existing_matches(target, payload):
            try:
                with target.open("xb") as stream:
                    stream.write(payload)
            except FileExistsError:
                if not existing_matches(target, payload):
                    raise
    return tuple(output / name.split("/", 1)[0] / filename for name, (filename, _) in documents.items())


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

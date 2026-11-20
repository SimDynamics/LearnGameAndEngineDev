"""Export immutable Work/main U1 lessons to disposable QA output.

Git commit and blob IDs are pinned. No branch changes, remote fallback or
modifications to the active Codex curriculum and student profiles are allowed.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
COMMIT = "befa8bf9cbf15a535bd2be7ba584dad983385dd1"
RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion"
BLOBS = {
    "lesson_primer_programa_y_salida.json": "991b47f246d77e0f959decbaf69c904746d3fa59",
    "lesson_texto_secuencia_y_formato.json": "6775b047a7b2c263a072a9cd37a46aca5488e627",
    "lesson_fuente_compilacion_y_diagnostico.json": "d0c8e9002f61ffa3692a886e4e318dd11c425c79",
    "lesson_resolver_probar_y_recuperar.json": "28ee2b6569ba7021d5e7110fcf4a842959e8a819",
}

def pinned_file(filename: str) -> bytes:
    if filename not in BLOBS:
        raise ValueError(f"Unknown U1 fixture: {filename}")
    ref = f"{COMMIT}:{RELATIVE}/{filename}"
    try:
        blob = subprocess.check_output(["git", "rev-parse", ref],
            cwd=ROOT, stderr=subprocess.PIPE, text=True, encoding="utf-8").strip()
        if blob != BLOBS[filename]:
            raise RuntimeError(f"U1 Git blob mismatch for {filename}")
        return subprocess.check_output(["git", "show", ref],
            cwd=ROOT, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise RuntimeError(f"Pinned local Git object unavailable: {ref}. "
            "Obtain complete history; never fall back to live files.") from exc

def export(directory: Path) -> None:
    # All four sources must authenticate before any output is changed.
    files = {name: pinned_file(name) for name in BLOBS}
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        target = directory / name
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    export(options.output_dir)
    print(f"Authenticated {len(BLOBS)} Work/main U1 lessons from {COMMIT}.")

if __name__ == "__main__":
    main()

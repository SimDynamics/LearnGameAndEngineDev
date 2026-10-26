"""Verified pre-retirement U1 fixtures, read only from an immutable local commit.

Migration tests must not retire today's already archived curriculum again. The
three historical documents come from the published d1933ed version, checked by
their exact Git bytes. Nothing is fetched and the live assets are never written.
The CLI materializes those same bytes only in a caller-selected test build tree.
"""
from __future__ import annotations

import argparse
from decimal import Decimal
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
BASELINE_COMMIT = "d1933edf703e19ae0569ae60849b037c48edb687"
RELATIVE_UNIT = "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion"
FIXTURE_SHA256 = {
    "lesson_primer_programa_y_salida.json": "72165efd0d74dc0374a9df2e4d020db32d1664ad89c6bd972f8452b8d6c7c71d",
    "lesson_texto_secuencia_y_formato.json": "026dd83ea21a79a6a7b44107f8be6a3b4838ea7d36f0ecc55d05ddb39d3ed53d",
    "lesson_fuente_compilacion_y_diagnostico.json": "eacd78eacf76f9ad68984c3d0eaba45b3c989e74bacf138b94292ecb9c37849d",
}


def _verify_bytes(filename: str, payload: bytes) -> bytes:
    if filename not in FIXTURE_SHA256:
        raise ValueError(f"Unknown historical retirement fixture: {filename}")
    if hashlib.sha256(payload).hexdigest() != FIXTURE_SHA256[filename]:
        raise RuntimeError(f"Historical retirement fixture SHA-256 mismatch: {filename}")
    return payload


@lru_cache(maxsize=len(FIXTURE_SHA256))
def historical_bytes(filename: str) -> bytes:
    if filename not in FIXTURE_SHA256:
        raise ValueError(f"Unknown historical retirement fixture: {filename}")
    reference = f"{BASELINE_COMMIT}:{RELATIVE_UNIT}/{filename}"
    try:
        payload = subprocess.check_output(["git", "show", reference], cwd=ROOT, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(
            f"Missing local retirement fixture {reference}; the pinned commit is required. "
            "No network retrieval or fallback to current assets is permitted."
        ) from error
    return _verify_bytes(filename, payload)


def load_historical_json(filename: str) -> dict:
    # Parse afresh: one test's mutation can never change another fixture.
    return json.loads(historical_bytes(filename), parse_float=Decimal)


def export_fixtures(output_dir: Path) -> None:
    # Verify every input before creating any output, including in a shallow
    # checkout that does not have the required historical commit locally.
    documents = {name: historical_bytes(name) for name in FIXTURE_SHA256}
    output_dir.mkdir(parents=True, exist_ok=True)
    for filename, payload in documents.items():
        target = output_dir / filename
        if not target.is_file() or target.read_bytes() != payload:
            target.write_bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    export_fixtures(args.output_dir)
    print(f"Verified {len(FIXTURE_SHA256)} U1 retirement fixtures from local {BASELINE_COMMIT}.")


if __name__ == "__main__":
    main()

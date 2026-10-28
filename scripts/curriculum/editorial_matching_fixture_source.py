"""Materialize one verified historical U9 matching lesson in a QA build tree.

The immutable local Git object is the only source. This tool never fetches,
reads student profiles or substitutes the current curricular asset.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
BASELINE_COMMIT = "cbd9a38c19ff1f02068296f7e6cb9049e2c64212"
FILENAME = "lesson_construir_y_comprobar_un_modelo_de_pedidos.json"
RELATIVE_SOURCE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "datos_propios_y_estado_valido/" + FILENAME
)
FIXTURE_SHA256 = "caf6601b9eb17bc71fa067bc334f0e20196ebe2604848fd40dc455cbeef5d836"


def verify_bytes(payload: bytes) -> bytes:
    if hashlib.sha256(payload).hexdigest() != FIXTURE_SHA256:
        raise RuntimeError("Historical U9 matching fixture SHA-256 mismatch")
    return payload


@lru_cache(maxsize=1)
def historical_bytes() -> bytes:
    reference = f"{BASELINE_COMMIT}:{RELATIVE_SOURCE}"
    try:
        payload = subprocess.check_output(
            ["git", "show", reference], cwd=ROOT, stderr=subprocess.PIPE
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(
            f"Missing local matching fixture {reference}; no fetch or current-asset fallback is permitted"
        ) from error
    return verify_bytes(payload)


def export_fixture(output_dir: Path) -> Path:
    payload = historical_bytes()  # Validate the source before creating output.
    output_dir = output_dir.resolve()
    assets = (ROOT / "app/src/main/assets").resolve()
    if output_dir == assets or assets in output_dir.parents:
        raise ValueError("Historical QA fixtures cannot overwrite live curricular assets")
    output_dir.mkdir(parents=True, exist_ok=True)
    target = output_dir / FILENAME
    if not target.is_file() or target.read_bytes() != payload:
        target.write_bytes(payload)
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(f"Verified historical matching fixture: {export_fixture(args.output_dir)}")


if __name__ == "__main__":
    main()

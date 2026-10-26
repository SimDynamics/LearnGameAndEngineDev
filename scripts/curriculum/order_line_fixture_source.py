"""Immutable old/refined order-code fixtures, materialized only in the test tree.

The live curriculum is never edited. A missing local pinned commit fails rather
than fetching history or silently using Work's later already-refined assets.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "aa230c6575210c2154e59dee60ff87cdeb8e615b"
PREFIX = "app/src/main/assets/content"
CASES = (
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", "block_0003_0002"),
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", "block_0004_0002"),
    ("objetos_y_estado/lesson_declarar_e_inicializar.json", "block_0001_0004"),
    ("objetos_y_estado/lesson_estado_asignacion_y_copia.json", "block_0003_0002"),
    ("objetos_y_estado/lesson_representar_procesar_y_recuperar.json", "block_0002_0001"),
    ("expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json", "block_0002_0004"),
    ("expresiones_y_decisiones/lesson_alternativas_y_fronteras.json", "block_0004_0001"),
    ("repeticion_y_terminacion/lesson_acumuladores_contadores_y_trazas.json", "block_0004_0001"),
    ("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", "block_0005_0004"),
    ("funciones_y_contratos/lesson_copia_y_ambito.json", "block_0004_0001"),
    ("funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json", "orden_validar_antes_de_calcular"),
    ("excepciones_y_recuperacion/lesson_recuperar_por_operacion.json", "block_0002_0003"),
    ("archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json", "block_0004_0003"),
    ("texto_interpretacion_y_flujos/lesson_una_tabla_persistente_de_nombres.json", "block_0004_0002"),
)


@lru_cache(maxsize=None)
def published_bytes(path: str, locale: str = "es") -> bytes:
    reference = f"{BASELINE}:{PREFIX}/{locale}/programming/cpp_fundamentals/{path}"
    try:
        return subprocess.check_output(["git", "show", reference], cwd=ROOT, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"Missing immutable local order fixture {reference}; no fallback is permitted") from error


def published(path: str, locale: str = "es") -> dict:
    return layout.parse_json(published_bytes(path, locale).decode("utf-8-sig"))


def fixtures() -> tuple[dict, dict[str, bytes]]:
    entries, files = [], {}
    for path, identity in CASES:
        family = {locale: published(path, locale) for locale in sorted(layout.LOCALES)}
        before = family["es"]
        prepared = layout.prepare_v2(before, "es", family, previous=before)
        refined = layout.refine_order_lines(prepared, identity)
        layout.validate_transition(before, refined)
        layout.check_localized_family([refined, family["en"], family["fr"]], ["es", "en", "fr"])
        old_block = layout._blocks(prepared)[identity]
        current = layout._blocks(refined)[identity]
        entry = {"path": path, "stableId": identity, "old": "old/es/" + path,
                 "refined": "refined/es/" + path, "oldPieces": len(old_block["fragments"]),
                 "newPieces": len(current["fragments"])}
        for locale in sorted(layout.LOCALES):
            name = f"old/{locale}/{path}"
            files[name] = published_bytes(path, locale)
            entry[locale] = name
        files[entry["refined"]] = (json.dumps(refined, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
        entry["oldSha256"] = hashlib.sha256(files[entry["old"]]).hexdigest()
        entries.append(entry)
    return {"baseline": BASELINE, "entries": entries}, files


def export_fixtures(output_dir: Path) -> None:
    manifest, files = fixtures()  # Validate every historical input before writing.
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    for relative, payload in files.items():
        target = output_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or target.read_bytes() != payload:
            target.write_bytes(payload)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    export_fixtures(parser.parse_args().output_dir)
    print(f"Verified {len(CASES)} order-line fixture pairs and their original translations from local {BASELINE}.")

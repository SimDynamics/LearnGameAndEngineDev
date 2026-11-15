"""Authenticate fixed U9/U12 predecessors for editorial regression fixtures.

These bytes belong to one real publication, not to the mutable working-tree
curriculum. Missing history or mismatching bytes fail closed; this helper never
fetches objects or substitutes today's lesson for an unavailable predecessor.
"""
from __future__ import annotations

from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content"
COMMIT = "218dc134a652e20f46a277c657109da1aae6fd24"
SOURCE_SHA256 = {
    ("es/programming/cpp_fundamentals/datos_propios_y_estado_valido/"
     "lesson_construir_y_comprobar_un_modelo_de_pedidos.json"):
        "1c18d9810ec011e774a05b73053c648a7502b6bc6c18d36c9eafd702e9d69810",
    ("es/programming/cpp_fundamentals/archivos_y_programa_completo/"
     "lesson_estado_del_formato_y_flujos_independientes.json"):
        "e829efa8dce50e4f838fa2ba08cb774d9a56906cae4b928c24f635258fcb98c5",
    ("en/programming/cpp_fundamentals/archivos_y_programa_completo/"
     "lesson_estado_del_formato_y_flujos_independientes.json"):
        "88139bcd9cf4147bdbe862f091fa4309b6ae160688cae08f23e2365d1c7b0765",
    ("fr/programming/cpp_fundamentals/archivos_y_programa_completo/"
     "lesson_estado_del_formato_y_flujos_independientes.json"):
        "3495cc29bf10a8814890cbdfba413a3eefefbe6b86fdd342d6e42b229f0b78bc",
    ("es/programming/cpp_fundamentals/archivos_y_programa_completo/"
     "lesson_leer_un_grupo_con_cierre_propio.json"):
        "d396272645b05ab122b22749479e97ffcda1c3f5d5cc108a772c3aadf92f739c",
    ("en/programming/cpp_fundamentals/archivos_y_programa_completo/"
     "lesson_leer_un_grupo_con_cierre_propio.json"):
        "cc52fb5e6fe100be2b370304a7d3e2e03c41dc83b0658dac22a6d81f21b11074",
    ("fr/programming/cpp_fundamentals/archivos_y_programa_completo/"
     "lesson_leer_un_grupo_con_cierre_propio.json"):
        "96d290650bc586bec5897307892b0031eaffc5aacfa85eab8bfed13be6c10d36",
}


def verify_bytes(relative: str, payload: bytes) -> bytes:
    expected = SOURCE_SHA256.get(relative)
    if expected is None:
        raise ValueError(f"Unknown pinned editorial fixture source: {relative}")
    if hashlib.sha256(payload).hexdigest() != expected:
        raise ValueError(f"Editorial fixture differs from its pinned publication: {relative}")
    return payload


@lru_cache(maxsize=7)
def historical_bytes(relative: str) -> bytes:
    if relative not in SOURCE_SHA256:
        raise ValueError(f"Unknown pinned editorial fixture source: {relative}")
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{COMMIT}:app/src/main/assets/content/{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    return verify_bytes(relative, payload)


def historical_document(relative: str) -> dict:
    # Cache only immutable bytes. Each caller owns its fresh object and may
    # author a QA proposal without rewriting another test's predecessor.
    return json.loads(historical_bytes(relative))


def historical_family(relative: str) -> dict[str, dict]:
    return {locale: historical_document(f"{locale}/{relative}")
            for locale in ("es", "en", "fr")}


def validate_live_descendant(before: dict, current: dict) -> None:
    """Check current provenance separately from the fixed regression scenario.

    A later publication may contain several valid editorial/addition steps.
    Do not require it to equal the fixed proposal or to replay all those steps
    as one immediate transition from this predecessor.
    """
    import stable_state_layout as layout

    layout.validate(before)
    layout.validate(current)
    if current["id"] != before["id"]:
        raise ValueError("Current editorial lesson changed its historical identity")
    old, new = before["stateLayout"], current["stateLayout"]
    for field in ("version", "locale", "origin", "family"):
        if new[field] != old[field]:
            raise ValueError(f"Current editorial lesson changed its immutable {field}")
    for field in ("additions", "editorial", "retired", "retirementHistory",
                  "orderRefinements", "choiceRevisions", "compilerRevisions",
                  "sourceFormatRevisions", "mathTextRevisions",
                  "compilerCaseOrderRevisions", "compilerCaseRevisions"):
        prefix = old.get(field, [])
        if new.get(field, [])[:len(prefix)] != prefix:
            raise ValueError(f"Current editorial lesson rewrote its historical {field}")

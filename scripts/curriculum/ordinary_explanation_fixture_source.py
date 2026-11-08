"""Authenticated source lessons for ordinary explanation revision regressions.

The five Work proposals are test candidates, never edits to active curriculum.
Sources come exclusively from the pinned publication and independent byte hashes.
No student profile, voice model, network fetch or installed application is used.
"""
from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "1c6d202f98b13fdebcb3a0b80dbfef4432ce97e5"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
PAIRED_CHOICE_LESSON = "texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json"
PAIRED_CHOICE_SOURCE_HASH = "007d41e23601fdcc4abed0c4cb90165b9e1066aae912e94febe38d3bfcc0c5b0"
CASES = {
    "empty_vector": {
        "lesson": "colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json",
        "identity": "block_0002_0002", "historical": (2, 2), "type": "output_prediction", "activity_id": None,
        "before_block_sha256": "5b3c0a7910dd6efcf08ebf960449ed0af97d734c08c67dd45c45cb5fcf326f89",
        "after": "En estas instrucciones, std::cout muestra los bool como 1 para true y 0 para false. El vector llamado ninguna no contiene elementos; el vector llamado una contiene el valor cero como un elemento. Por eso se muestran 0:1 y 1:0, respectivamente.",
        "spans": (("std::cout", "cpp", "technical"), ("bool", "cpp", "technical"),
                  ("true", "cpp", "technical"), ("false", "cpp", "technical"),
                  ("ninguna", "cpp", "technical"), ("una", "cpp", "technical", 2),
                  ("0:1", "output", "natural"), ("1:0", "output", "natural")),
        "source_hashes": {
            "es": "004abb0c8d09731d01b1591eb05ed887abdc84a34695d6ce2fd0340bbb334ed8",
            "en": "aad1b596ba361ac76dbccc7de5cdbd2b77f16fa49c93e2edacb4594906bea782",
            "fr": "a26548a2f5a3bba64c5d041543ecaedb45601c873692639ac1051659917588e8"},
    },
    "range_copy": {
        "lesson": "colecciones_y_recorridos/lesson_recorrer_valores_por_rango.json",
        "identity": "block_0007_0002", "historical": (7, 2), "type": "output_prediction", "activity_id": None,
        "before_block_sha256": "18f1ce96aa7412f805b34990b76c0daf53c928fe9f130f3e7e8588ad9d175b81",
        "after": "En for (auto valor : datos), auto deduce int y valor recibe una copia del elemento, cuyo valor es cuatro. La asignación valor = 9; cambia esa copia: dentro del bucle se muestra nueve. Después, datos.at(0) sigue proporcionando el cuatro original. Por eso la salida es 9 4.",
        "spans": (("for (auto valor : datos)", "cpp", "technical"), ("int", "cpp", "technical"),
                  ("valor = 9;", "cpp", "technical"), ("datos.at(0)", "cpp", "technical"),
                  ("9 4", "output", "natural")),
        "source_hashes": {
            "es": "dabb79e43b3a71cf122980774ae83d8944bf264eb58c1ec8908694ab32ce3fd6",
            "en": "57ba72507edbf646310460dd5344b385683f23e7d0c0b391ae3fc7fb84e7a824",
            "fr": "19a891b5e6402de4e7c2528357fdbf4e1e50b1a40eec461008a02bdd51c0c598"},
    },
    "reset_sum": {
        "lesson": "colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json",
        "identity": "block_0002_0001", "historical": (2, 1), "type": "output_prediction", "activity_id": None,
        "before_block_sha256": "963418503fea3671a9aea16a6e61b3b7a90e73bb9394619d873b4d6808eeb2c0",
        "after": "En cada vuelta, suma = 0; borra lo acumulado. Al visitar dos, el total parcial es dos; al visitar ocho vuelve a cero y queda en ocho; al visitar tres vuelve a cero y queda en tres. Por eso imprime 3, en lugar del total 13.",
        "spans": (("suma = 0;", "cpp", "technical"), ("3", "output", "natural", 1),
                  ("13", "data", "natural")),
        "source_hashes": {
            "es": "6769fab5d9aa11c9ab8d0901bcb14e56ff704e0ec9ad5eddbdc59054aee7e562",
            "en": "e18bcae655570c8d1241b27ac71d27024e132013960c24f06cd80780c64f58b0",
            "fr": "0bfac67943ef74962c41ec98e0762d93032e065e2a30cc591255693323cef71f"},
    },
    "case_sensitive_label": {
        "lesson": "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json",
        "identity": "recuperacion_empate_prediccion", "historical": (8, 1), "type": "multiple_choice",
        "activity_id": "cpp_fundamentals__colecciones_y_recorridos__contar_frecuencias_y_conservar_empates__retirar_una_observacion_y_recuperar_el_empate",
        "before_block_sha256": "e3101e6bd07e8db23fbf594360dba2bbabc9fa8cb4dadf62fe43b4d8e9971433",
        "after": "Se ha retirado una aparición de patio, no toda la etiqueta. La etiqueta patio queda con dos observaciones y sala conserva sus dos; faro tiene una. La frecuencia máxima actual es dos y ambos grupos deben figurar en Mayores: primero patio y después sala por el orden lexicográfico. N y Suma pasan a cinco, mientras Distintas sigue siendo tres. Conservar únicamente el candidato anterior perdería el empate actual.",
        "spans": (("patio", "data", "natural", 1), ("patio", "data", "natural", 2),
                  ("sala", "data", "natural", 1), ("faro", "data", "natural"),
                  ("Mayores", "output", "natural"), ("patio", "output", "natural", 3),
                  ("sala", "output", "natural", 2), ("N", "output", "natural"),
                  ("Suma", "output", "natural"), ("Distintas", "output", "natural")),
        "source_hashes": {
            "es": "0ebc3dae1d007f65e5c6102e9b0edb7793754d2f28e37a0328f714062d04dfcf",
            "en": "cc28a49e376d24fae049a528f5bf75147891c4e1ef91016d95d2f59bf4826b06",
            "fr": "63570ba4e69dd83b2904f20ca98688666109c4eff1aa4e214f67593e48b1e4d0"},
    },
    "boundary_cases": {
        "lesson": "funciones_y_contratos/lesson_contratos_y_casos_de_prueba.json",
        "identity": "block_0004_0001", "historical": (4, 1), "type": "multiple_choice", "activity_id": None,
        "before_block_sha256": "7c08b1b2fcc03f9874f044de58de6920117e6dbf1cafba1b51e7c849ff8d4a4b",
        "after": "Con capacidad seis, las cantidades menos uno y cero comprueban el límite inferior: menos uno se rechaza y cero se acepta. Las cantidades ciento veinte y ciento veintiuno comprueban el superior: ciento veinte se acepta y ciento veintiuno se rechaza. Probar sólo cantidades interiores no permite detectar que un límite se haya incluido o excluido incorrectamente.",
        "spans": (("seis", "data", "natural"), ("menos uno", "data", "natural", 1),
                  ("cero", "data", "natural", 1), ("menos uno", "data", "natural", 2),
                  ("cero", "data", "natural", 2), ("ciento veinte", "data", "natural", 1),
                  ("ciento veintiuno", "data", "natural", 1), ("ciento veinte", "data", "natural", 2),
                  ("ciento veintiuno", "data", "natural", 2)),
        "source_hashes": {
            "es": "c47161fe3403289ab3bd4201fafdefbc187f8ab936ade47afd7662457abc5645",
            "en": "8f5d6c84242f303bde7d578003298bae93813a7a449ed92e065a39af0afaaab2",
            "fr": "0f211d13fbd2444d6b228e934ba96b161878f86f41ddcceb6033f62d05f84389"},
    },
}


def verify_bytes(name: str, locale: str, payload: bytes) -> bytes:
    if hashlib.sha256(payload).hexdigest() != CASES[name]["source_hashes"][locale]:
        raise ValueError("Ordinary explanation fixture differs from its pinned publication")
    return payload


@lru_cache(maxsize=None)
def historical_bytes(name: str, locale: str = "es") -> bytes:
    relative = PREFIX.format(locale=locale) + CASES[name]["lesson"]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(name, locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def paired_choice_document() -> dict:
    relative = PREFIX.format(locale="es") + PAIRED_CHOICE_LESSON
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if hashlib.sha256(payload).hexdigest() != PAIRED_CHOICE_SOURCE_HASH:
        raise ValueError("Paired choice fixture differs from its pinned publication")
    return json.loads(payload)


def replace_explanation(document: dict, identity: str, text: str, spans: list) -> dict:
    """Author a test draft with the existing field helper; never relax validation."""
    result = deepcopy(document)
    layout._blocks(result)[identity]["explanation"] = text
    metadata = result.setdefault("textSemantics", {"version": 1, "fields": []})
    metadata["fields"] = [field for field in metadata["fields"]
                          if (field["block"], field["path"]) != (identity, "/explanation")]
    metadata["fields"].append(layout.text_semantics.make_field(result, identity, "/explanation", spans))
    return result


def candidate(name: str) -> tuple[dict, dict, dict[str, dict]]:
    case = CASES[name]
    family = {locale: json.loads(historical_bytes(name, locale)) for locale in ("es", "en", "fr")}
    before = family["es"]
    block = layout._blocks(before)[case["identity"]]
    if (layout.fingerprint(block) != case["before_block_sha256"] or block["type"] != case["type"]
            or block.get("id") != case["activity_id"] or (case["activity_id"] is None and "id" in block)):
        raise ValueError("Ordinary explanation fixture owner or original block changed")
    position = next((pn, bn) for pn, record in enumerate(before["stateLayout"]["current"])
                    for bn, identity in enumerate(record["blocks"]) if identity == case["identity"])
    if layout.validate(before)[position] != case["historical"]:
        raise ValueError("Ordinary explanation fixture storage changed")
    spans = []
    for text, role, reading, *occurrence in case["spans"]:
        spans.append(layout.text_semantics.select(case["after"], text, role, reading=reading,
                     occurrence=occurrence[0] if occurrence else None))
    draft = replace_explanation(before, case["identity"], case["after"], sorted(spans, key=lambda span: span["start"]))
    return before, layout.prepare_v2(draft, "es", family, before), family

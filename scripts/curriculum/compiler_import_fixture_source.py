"""Authenticate published U11L19-L22/U12L20 compiler import fixtures.

The complete positional source and the complete mapped destination come from
immutable Git publications. This exporter never reconstructs historical lessons,
fetches missing history, changes curriculum, or replaces existing QA evidence.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

from diagnostic_fixture_source import ROOT, checked_output, existing_matches
import stable_state_layout as layout


COMMITS = {
    "before": "617f03472d20a97f2e1e110a3233ad11fd966d01",
    "after": "5d115a0c99c8d2a17d01eee99dfe887247b82dbb",
}
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"
FIXTURES = {
    "u11l19": {
        "relative": "texto_interpretacion_y_flujos/lesson_declaraciones_aceptadas_solo_al_completar.json",
        "before": "887dbfb3f98c0f04c92567d2d1a46d82ddb1bd3ebe761ecf60c17a905e1815a0",
        "after": "bfaa88c3f4294c30bf50b58f96aff91fbf71ffdbdfd20c7471d1c71e358c1cb6",
        "owners": {"block_0004_0002": (4, 2)},
        "responses": {"block_0004_0002": (3, 3)},
    },
    "u11l20": {
        "relative": "texto_interpretacion_y_flujos/lesson_asignaciones_con_anticipacion_limitada.json",
        "before": "cde8f8a248fcc8a7eab16033f2e469932eb24929e5fff3cae3d77c688f1338f8",
        "after": "69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9",
        "owners": {"block_0005_0002": (5, 2)},
        "responses": {"block_0005_0002": (4, 1)},
    },
    "u11l21": {
        "relative": "texto_interpretacion_y_flujos/lesson_constantes_del_lenguaje_y_nombres_predefinidos.json",
        "before": "65b1eb97508bfb7f6dbbc695f25c8cfa27994333892878c44c297b7ccea463cc",
        "after": "d444222f6ce6f69436279bee30e734f6fb9823ce7c6df592f6085a07734b04db",
        "owners": {"block_0005_0002": (5, 2)},
        "responses": {"block_0005_0002": (4, 1)},
    },
    "u11l22": {
        "relative": "texto_interpretacion_y_flujos/lesson_construir_una_sesion_con_nombres_y_estado.json",
        "before": "02556412590ab4bd53c86eff1074ba2b52145fcf9a4d7d2bee771cd4af806bf7",
        "after": "1932b3c86cabf23bf02154eb2d0371c3b8e7fe48fc01e42e1d559b3220cb1696",
        "owners": {"block_0003_0001": (3, 1), "block_0006_0003": (6, 3)},
        "responses": {"block_0003_0001": (6, 1), "block_0006_0003": (6, 1)},
    },
    "u12l20": {
        "relative": "archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json",
        "before": "f2e04cdddc482d708f29ef5954d388cc68a9d2d69c69345430b0585afed04789",
        "after": "744739618d59f751567445eedbfa97fca20ce300ce61f3061d33eea610870532",
        "owners": {"block_0005_0004": (5, 4), "block_0009_0002": (9, 2)},
        "responses": {"block_0005_0004": (2, 5), "block_0009_0002": (7, 2)},
    },
}


def verify_bytes(name: str, side: str, payload: bytes) -> bytes:
    if name not in FIXTURES or side not in COMMITS:
        raise ValueError("Unknown published compiler import fixture")
    if hashlib.sha256(payload).hexdigest() != FIXTURES[name][side]:
        raise ValueError(f"Compiler import fixture differs from pinned Git bytes: {name}/{side}")
    return payload


def published_bytes(name: str, side: str) -> bytes:
    if name not in FIXTURES or side not in COMMITS:
        raise ValueError("Unknown published compiler import fixture")
    reference = f"{COMMITS[side]}:{PREFIX}{FIXTURES[name]['relative']}"
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    return verify_bytes(name, side, payload)


def execution_payload(block: dict) -> dict:
    """Remove only presentation from a compiler contract for comparison."""
    result = deepcopy(block)
    for case in result.get("outputCheck", {}).get("cases", []):
        case.pop("explanation", None)
        case.pop("reviewFeedback", None)
    return result


def validate_pair(name: str, before: dict, after: dict) -> None:
    spec = FIXTURES[name]
    if "stateLayout" in before or after.get("stateLayout", {}).get("version") != 2:
        raise ValueError("Compiler import requires the actual positional-to-v2 publication pair")
    if after.get("compilerPuzzlesRequired") is not True:
        raise ValueError("The destination must retain required compiler objectives")
    layout.validate_transition(before, after)
    positional = layout.prepare(before)
    if positional["stateLayout"]["origin"] != after["stateLayout"]["origin"]:
        raise ValueError("The mapped compiler lesson changed original storage coordinates")
    old_blocks, new_blocks = layout._blocks(positional), layout._blocks(after)
    old_editors = {identity for identity, block in old_blocks.items() if block.get("type") == "cpp_compiler"}
    new_editors = {identity for identity, block in new_blocks.items() if block.get("type") == "cpp_compiler"}
    if old_editors != set(spec["owners"]) or new_editors != old_editors:
        raise ValueError("Compiler import omitted or changed one of the seven historical owners")
    positions = layout.validate(after)
    for identity, origin in spec["owners"].items():
        old, current = execution_payload(old_blocks[identity]), execution_payload(new_blocks[identity])
        visible = next((at for at, historical in positions.items() if historical == origin), None)
        if visible is None or after["stateLayout"]["current"][visible[0]]["blocks"][visible[1]] != identity:
            raise ValueError("A compiler owner moved to another historical address")
        if name == "u12l20" and identity == "block_0005_0004":
            # The loader already authenticates this sole CRT-equivalent spelling.
            # Preserve every original control byte; never normalize source data.
            original = old["outputCheck"]["cases"][0]["outputs"]
            expanded = current["outputCheck"]["cases"][0]["outputs"]
            if len(original) != 1 or expanded != [original[0], original[0].replace("\n", "\r\n")]:
                raise ValueError("The CRT compiler must retain its exact two reviewed byte alternatives")
            current["outputCheck"]["cases"][0]["outputs"] = original
        if current != old:
            raise ValueError("Compiler import changed an executable source, input or execution objective")
        response_page, response_block = spec["responses"][identity]
        response_id = f"block_{response_page:04d}_{response_block:04d}"
        previous_response, current_response = old_blocks[response_id], new_blocks[response_id]
        if (previous_response.get("type") != "output_prediction" or not previous_response.get("answers")
                or layout.state_fingerprint(previous_response) != layout.state_fingerprint(current_response)):
            raise ValueError("Compiler import changed the independent historical response contract")


def export(output_dir: Path) -> Path:
    output = checked_output(output_dir)
    payloads = {f"{name}-{side}.json": published_bytes(name, side)
                for name in FIXTURES for side in COMMITS}
    manifest = {"kind": "published-positional-compiler-import", "coordinateBase": 0, "lessons": {}}
    for name, spec in FIXTURES.items():
        before, after = (json.loads(payloads[f"{name}-{side}.json"]) for side in COMMITS)
        validate_pair(name, before, after)
        manifest["lessons"][name] = {
            "relative": PREFIX + spec["relative"], "lessonId": before["id"],
            "owners": spec["owners"], "responses": spec["responses"],
            **{side: {"file": f"{name}-{side}.json", "commit": COMMITS[side], "sha256": spec[side]}
               for side in COMMITS},
        }
    payloads["manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    for name, payload in payloads.items():
        target = output / name
        if not existing_matches(target, payload):
            try:
                with target.open("xb") as stream:
                    stream.write(payload)
            except FileExistsError:
                if not existing_matches(target, payload):
                    raise
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    export(arguments.output_dir)
    print("Verified five authentic positional-to-v2 lesson pairs covering seven compiler owners.")

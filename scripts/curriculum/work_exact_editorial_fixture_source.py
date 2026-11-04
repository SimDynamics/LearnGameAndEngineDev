"""Authenticated before/after documents for two exact Work editorial proposals.

Fixtures come only from a pinned local Git publication. No fetch, active-asset
fallback, curricular edit, student profile or native execution is permitted.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "5cfd5514c879341216c1c34c313e34a16f60ea32"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
CASES = {
    "duplication": {
        "lesson": "objetos_y_estado/lesson_estado_asignacion_y_copia.json",
        "identity": "block_0005_0004", "historical": (5, 4), "type": "inline_fill_code", "activity_id": None,
        "field": "question",
        "before": "Completa las partes derechas para duplicar 4 y luego reducir el resultado en dos.",
        "after": "Completa las partes derechas para duplicar `4` y luego reducir el resultado en dos.",
        "text_hashes": ("499b03798d78be94d5f25f003249520c62cd0584df04d26cc36dfe63ab85c260",
                        "585e6dc1fe82669a8828ea3283d52307db4da0ae87d1473d6affb274fe4375cf"),
        "after_sha256": "e173afd79776afd69bce03d37777de30f9a6e8c076e17192730bbc6f16f39e4a",
        "source_hashes": {"es": "d0ffba352e0461a82286d282a84e14025f316467200f35007bac079cce2532a9",
                          "en": "18c650f29e42396b686b3871d6a0fbd6635d4f7db76cbfe061989a73ff8c5540",
                          "fr": "1c8fcf221df2abc5af520f188ff19ac84d94a25ce25eeed453b9276aa741c52c"},
    },
    "remainder": {
        "lesson": "texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json",
        "identity": "block_0002_0002", "historical": (2, 2), "type": "order_code",
        "activity_id": "cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete__e04",
        "field": "explanation",
        "before": "La guardia completa se ejecuta antes del operador %. El diagnóstico impide llegar al cálculo cuando el divisor es cero.",
        "after": "La guardia completa se ejecuta antes del operador %. Lanzar la excepción impide llegar al cálculo cuando el divisor es cero.",
        "text_hashes": ("c3df0179bd09fbe11d54af6e11c1fb18a140dbc9ee57894070ef1bd11eb25d9b",
                        "0d826e2c9c883119e498ec90650b531ec9fe8a1ed3ae5417dfa653e2f3c7238d"),
        "after_sha256": "25eefcb48f42908270ce0c9f03501e7ebb5eec51f6c19920e0d2099a2cade400",
        "source_hashes": {"es": "e46f006765a5fa313430b13dd5b1b0a305a6add572beea529fe84f14ab0bf739",
                          "en": "dfe05ab3d615f38091aecbfd3bd8ef49cec0b489cdd55a31c21aa729ad2ec8cf",
                          "fr": "fa5eae36ab2bc32544d37839476e63ae7f687228d80ffcfbbaa7870552a93f87"},
    },
}


def verify_bytes(name: str, locale: str, payload: bytes) -> bytes:
    if hashlib.sha256(payload).hexdigest() != CASES[name]["source_hashes"][locale]:
        raise ValueError("Exact editorial fixture differs from its independently pinned Git bytes")
    return payload


def historical_bytes(name: str, locale: str = "es") -> bytes:
    relative = PREFIX.format(locale=locale) + CASES[name]["lesson"]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(name, locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def candidate(name: str) -> tuple[dict, dict, dict[str, dict]]:
    case = CASES[name]
    family = {locale: json.loads(historical_bytes(name, locale)) for locale in ("es", "en", "fr")}
    before = family["es"]
    original = layout._blocks(before)[case["identity"]]
    if original["type"] != case["type"] or original.get("id") != case["activity_id"] or (
            case["activity_id"] is None and "id" in original):
        raise ValueError("Exact editorial fixture type/activity identity changed")
    if original[case["field"]] != case["before"] or tuple(hashlib.sha256(case[side].encode("utf-8")).hexdigest()
            for side in ("before", "after")) != case["text_hashes"]:
        raise ValueError("Exact editorial fixture source/text hash changed")
    draft = deepcopy(before); layout._blocks(draft)[case["identity"]][case["field"]] = case["after"]
    after = layout.prepare_v2(draft, "es", family, before)
    if hashlib.sha256(json_bytes(after)).hexdigest() != case["after_sha256"]:
        raise ValueError("Exact editorial fixture differs from its pinned complete candidate")
    return before, after, family


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def export(output: Path) -> Path:
    """Write only authenticated fixtures; an identical prior export is a no-op."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("Exact editorial QA output cannot follow links or reparse points")
    output = output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "out") and not output.is_relative_to(ROOT / "build"):
        raise ValueError("Exact editorial QA output cannot replace source files")
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("Exact editorial QA output cannot write another checkout")

    payloads = {}
    manifest = {"commit": COMMIT, "coordinateBase": 0, "cases": {}}
    for name, case in CASES.items():
        before, after, _ = candidate(name)
        before_name, after_name = f"{name}-before.json", f"{name}-after.json"
        payloads[before_name] = historical_bytes(name)
        payloads[after_name] = json_bytes(after)
        original, revised = (layout._blocks(document)[case["identity"]] for document in (before, after))
        manifest["cases"][name] = {
            "relative": PREFIX.format(locale="es") + case["lesson"],
            "stableId": case["identity"], "historical": list(case["historical"]),
            "type": case["type"], "activityId": case["activity_id"],
            "activityIdPresent": "id" in original, "field": case["field"],
            "sourceSha256": case["source_hashes"], "textSha256": list(case["text_hashes"]),
            "before": {"file": before_name, "sha256": hashlib.sha256(payloads[before_name]).hexdigest(),
                       "blockSha256": layout.fingerprint(original)},
            "after": {"file": after_name, "sha256": hashlib.sha256(payloads[after_name]).hexdigest(),
                      "blockSha256": layout.fingerprint(revised)},
        }
    payloads["manifest.json"] = json_bytes(manifest)
    # Verify every source and every existing destination before creating any
    # file. A bad manifest or the second bad source cannot leave a partial set.
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing exact editorial QA document")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        target = output / name
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    export(arguments.output_dir)
    print(f"Verified two exact editorial fixture pairs from {COMMIT}.")

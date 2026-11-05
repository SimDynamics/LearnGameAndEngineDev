"""Authenticated U11L20 editorial fixtures; never edits curricular assets.

The original language family and the already adopted Spanish document come
from distinct immutable Git publications. Their raw Git bytes use LF; the
working checkout may use CRLF without being a different JSON document.
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
COMMIT = "1601494bc25b07cdb6e13001989ab29092747e98"
ORIGIN_COMMIT = "de49377325565bc816ab8eaf2d528442c6754f5f"
LESSON = "texto_interpretacion_y_flujos/lesson_asignaciones_con_anticipacion_limitada.json"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
ACTIVITY_PREFIX = "cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada__"
SOURCE_HASHES = {
    "es": "dfbbac04b7a04df14f09c88b4b7b84ed59ab2e6b4b13fc1261d042d93b743fe0",
    "en": "593b2a9901b03d7a42a452d0a9c6319ad6a21db54b0f3ba60921afb8e40ef93b",
    "fr": "ee4a3655e68b38aebaaa6fb0b0dc823183d8fbab8bf3c6a1126ea1c791d97d48",
}
ORIGIN_SOURCE_HASHES = dict(SOURCE_HASHES, es="cde8f8a248fcc8a7eab16033f2e469932eb24929e5fff3cae3d77c688f1338f8")
AFTER_SHA256 = "768458f3b6b8d646dcd44fe154b5f264f31f6eb2c3851bb36c5442ebad700157"
BLOCKS = {
    "block_0002_0003": {
        "historical": (2, 3), "type": "order_code", "activity_id": ACTIVITY_PREFIX + "e03",
        "before": "75bd3371395fe205e1aa8cc4598f99dae109a053f43d7e9c3e095917ba050fe4",
        "after": "41f3edd3b7a2b7f60cf647cc00946330aed26c4fae4ef7bdbec9c22cd0aec1f8",
    },
    "block_0004_0001": {
        "historical": (4, 1), "type": "output_prediction", "activity_id": ACTIVITY_PREFIX + "e04",
        "before": "1795562bd3f78ab353f8368134ca1a8f6aa122a8148199569402b6c3c88e2595",
        "after": "db78773fab56744e367e444081e6649fd49d0c5843cbd175b81c0808f56c5d50",
    },
    "block_0004_0002": {
        "historical": (4, 2), "type": "multiple_choice", "activity_id": ACTIVITY_PREFIX + "e05",
        "before": "39cf702fee359eb05408354353e0281d5bb8693d9135cab1d5e501144b0d66ce",
        "after": "972652323f29b8b9ce5df38aa2ae0c564f916c4f1516a31cc5537532dfd72764",
    },
    "block_0007_0001": {
        "historical": (7, 1), "type": "output_prediction", "activity_id": ACTIVITY_PREFIX + "e06",
        "before": "9dcecb759eec49a264cb3e937c9338f46948e4cd498be62788c36931d7075490",
        "after": "282ebd9f9802ff3b44de06a5bbdcfcea3be89388858ec52f8a0855d2f745ead6",
    },
}
CHANGES = {
    "order_question": {
        "identity": "block_0002_0003", "field": "question",
        "before": "Ordena la rama que NO encontró =. Supón primero es el nombre y siguiente es el token que vino después.",
        "after": "Ordena la rama que no encontró `=`. Supón que `primero` es el token de nombre y `siguiente` es el token que vino después.",
        "hashes": ("c3e58a2c6549739708e6a754e133b03bb22fd298e651a4ba45a55eb10cf02b8f",
                   "1d09b90cd8682b1e179b1692dadf126e35e14600ee1ebddbb531646f1dc4056f"),
    },
    "order_explanation": {
        "identity": "block_0002_0003", "field": "explanation",
        "before": "Sólo siguiente se reserva. La consulta da la primera unidad; después se completa el término antes de la expresión.",
        "after": "Sólo se reserva `siguiente`, antes de una consulta que puede fallar. La consulta proporciona la primera unidad; después se completa el término y, con ese resultado, la expresión. Si falla la consulta, la recuperación todavía puede encontrar el terminador reservado.",
        "hashes": ("03931edf50a35510dfd8df81db571dfabc501b440b8eb23c9bb1dbd5f9b2e1f5",
                   "fd83fedfda451e39455543bad51ff91e4bb53736d7a3f06851f37c8fa7a2649d"),
    },
    "trace_explanation": {
        "identity": "block_0004_0001", "field": "explanation",
        "before": "La consulta empieza por t y completa el término: 8/3 es 2 y 2%2 es 0. La asignación usa el ocho anterior, guarda nueve y la última expresión consulta nueve.",
        "after": "La creación acepta 8 para `t`. La expresión `t/3%2+4;` completa el término de izquierda a derecha: la división entera produce 2 y el resto siguiente es 0; la suma obtiene 4. La asignación consulta el 8 anterior, acepta 9 y la última expresión obtiene 18. La tabla conserva `t=9`.",
        "hashes": ("dadcca493b61ea6162e8c5e4218f9609d706005483615184b934eac0b921d418",
                   "86db2f2a49f75d55d128f7e5a6da3c2dd03ff854fac1ca19c57218e636c4537b"),
    },
    "choice_question": {
        "identity": "block_0004_0002", "field": "question",
        "before": "¿Por qué a=b=3; se rechaza aunque a y b existan?",
        "after": "En nuestro lenguaje, ¿por qué se rechaza `a=b=3;` aunque `a` y `b` existan?",
        "hashes": ("80975de58f94834c4bb996f315dfdc7b00e9c46f4e5858e5598cca1b09ace674",
                   "7569359594104ec14a3c77a57d7a5b2e2012cdefcc02b1243b16047dc2d7e499"),
    },
    "choice_explanation": {
        "identity": "block_0004_0002", "field": "explanation",
        "before": "La gramática sólo permite asignación como sentencia superior. El = después de b impide aceptar el terminador y no publica el candidato.",
        "after": "El lado derecho sólo admite las expresiones aritméticas ya construidas. Consulta `b`, pero el `=` siguiente impide reconocer `;`. Por eso no se publica la candidata y ambas asociaciones conservan su valor anterior.",
        "hashes": ("f94ca0b92aa917edaaa626c0ed1b9ff2c45ec4eb9487e993221464dcf7e96979",
                   "298fa6db7b6c42436768017d04c5b7c1ba5eae0448fc54d6e4640a46b7f05a59"),
    },
    "recovery_explanation": {
        "identity": "block_0007_0001", "field": "explanation",
        "before": "El lado derecho obtiene el valor de b y después encuentra = en lugar de ;. Ningún cambio del candidato llega a la tabla aceptada. La recuperación llega al primer ;.",
        "after": "El lado derecho consulta el valor 2 de `b`, pero después encuentra `=` donde se exige `;`. La propuesta de asignar ese valor a `a` no llega a la tabla aceptada. La recuperación consume lo que falta hasta ese primer `;`; las consultas posteriores siguen obteniendo 6 y 2.",
        "hashes": ("ca5a1a0a51d4395638ad83c07b9434c22074c7cb148c0010823d3a07c0079225",
                   "f04c1e07e1c7deab38360970dd91b9f1aa0949debe7944bf85e7242bb10884e0"),
    },
}


def text_hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def verify_bytes(locale: str, payload: bytes, *, origin: bool = False) -> bytes:
    hashes = ORIGIN_SOURCE_HASHES if origin else SOURCE_HASHES
    if locale not in hashes or hashlib.sha256(payload).hexdigest() != hashes[locale]:
        raise ValueError("Assignment fixture differs from its independently pinned Git bytes")
    return payload


def historical_bytes(locale: str = "es", *, origin: bool = False) -> bytes:
    commit = ORIGIN_COMMIT if origin else COMMIT
    relative = PREFIX.format(locale=locale) + LESSON
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), origin=origin)


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def candidate(fields=None) -> tuple[dict, dict, dict[str, dict]]:
    """Prepare all six pairs, or an explicit subset of their named fields."""
    selected = tuple(CHANGES if fields is None else fields)
    if len(set(selected)) != len(selected) or not set(selected) <= CHANGES.keys():
        raise ValueError("Unknown or repeated closed assignment editorial field")
    family = {locale: json.loads(historical_bytes(locale, origin=True)) for locale in ("es", "en", "fr")}
    before = json.loads(historical_bytes())
    for locale in ("en", "fr"):
        if json.loads(historical_bytes(locale)) != family[locale]:
            raise ValueError("Assignment fixture cannot rewrite the original translations")
    original = layout._blocks(before)
    positions = layout.validate(before)
    for identity, spec in BLOCKS.items():
        block = original[identity]
        position = next((at for at, old_at in positions.items()
                         if old_at == spec["historical"]), None)
        if (position is None or block.get("type") != spec["type"]
                or block.get("id") != spec["activity_id"] or layout.fingerprint(block) != spec["before"]):
            raise ValueError("Assignment fixture changed a complete original owner")
    draft = deepcopy(before)
    for name, change in CHANGES.items():
        block = original[change["identity"]]
        if (block.get(change["field"]) != change["before"]
                or tuple(text_hash(change[side]) for side in ("before", "after")) != change["hashes"]):
            raise ValueError("Assignment fixture changed an exact reviewed text pair")
        if name in selected:
            layout._blocks(draft)[change["identity"]][change["field"]] = change["after"]
    after = layout.prepare_v2(draft, "es", family, before)
    layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])
    if set(selected) == CHANGES.keys():
        if hashlib.sha256(json_bytes(after)).hexdigest() != AFTER_SHA256:
            raise ValueError("Assignment fixture differs from its pinned complete candidate")
        for identity, spec in BLOCKS.items():
            if layout.fingerprint(layout._blocks(after)[identity]) != spec["after"]:
                raise ValueError("Assignment fixture differs from its complete reviewed owner")
    return before, after, family


def export(output: Path) -> Path:
    """Authenticate every input and destination before writing new QA files."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("Assignment QA output cannot follow links or reparse points")
    output = output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "out") and not output.is_relative_to(ROOT / "build"):
        raise ValueError("Assignment QA output cannot replace source files")
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("Assignment QA output cannot write another checkout")
    before, after, _ = candidate()
    payloads = {"assignment-before.json": historical_bytes(), "assignment-after.json": json_bytes(after)}
    manifest = {
        "commit": COMMIT, "originCommit": ORIGIN_COMMIT, "coordinateBase": 0,
        "relative": PREFIX.format(locale="es") + LESSON,
        "sourceSha256": SOURCE_HASHES, "originSourceSha256": ORIGIN_SOURCE_HASHES,
        "before": {"file": "assignment-before.json", "sha256": SOURCE_HASHES["es"]},
        "after": {"file": "assignment-after.json", "sha256": AFTER_SHA256},
        "blocks": {},
    }
    for identity, spec in BLOCKS.items():
        manifest["blocks"][identity] = {
            "historical": list(spec["historical"]), "type": spec["type"], "activityId": spec["activity_id"],
            "fields": {change["field"]: list(change["hashes"]) for change in CHANGES.values()
                       if change["identity"] == identity},
            "beforeSha256": spec["before"], "afterSha256": spec["after"],
            "stateSha256": layout.state_fingerprint(layout._blocks(before)[identity]),
        }
    payloads["manifest.json"] = json_bytes(manifest)
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing assignment QA document")
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
    print(f"Verified six U11L20 text pairs from {COMMIT}; origin family {ORIGIN_COMMIT}.")

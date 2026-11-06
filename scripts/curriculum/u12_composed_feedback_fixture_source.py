"""Authenticate the closed U12L4 feedback proposal without editing live lessons."""
from copy import deepcopy
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "57c0044a7484f1e6726383e9f880137cf43f6aa9"
LESSON = "archivos_y_programa_completo/lesson_componer_salida_sin_imprimirla.json"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
RELATIVE = PREFIX.format(locale="es") + LESSON
LESSON_ID = "cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"
COMPILER = "block_0002_0003"
PREDICTION = "block_0004_0001"
ACTIVITY_ID = "destino_archivo_no_es_stdout"
SOURCE_HASHES = {
    "es": "ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87",
    "en": "a932d7011d13ede1b33277fdf0e023b414f64571067b06c7e9bd1b6cc021dc6d",
    "fr": "37dbbed46bb7d292e686d3b7376eeed73d4e2869454f63a7c9dae48b3d647acb",
}
AFTER_SHA256 = "72b6f46499f7b902559a51e4a5ee998710489774c49dd42a836e783ad85cd2f1"
FAMILY_SHA256 = "f9865100c59e76353575893fd342d00b0ce5eee3ef2c9605a4b9189db4fbd887"
ORIGIN_SHA256 = "6c5a4300e032dfde02180cfb468cbb2b418931fbc65fb8f771561aa30b2dbee3"
CURRENT_SHA256 = "430aa6b167fac29d684c9c45734b6c611ce06be9bbd16a303c598d1c562a6e92"
EDITORIAL_PREFIX_SHA256 = "317364cd08c3847423c5a3d3a6b8b017e1882f91be895b738361aef13a68a956"
AFTER_EDITORIAL_SHA256 = "3ff7da20dbe7d2e0709b927606c59890b4dd3d975bfd6e67ad0f44ecb29a0c29"
PAGES_HASHES = ("5c2c07a09c4679edb36ce6c246d0dcb265f00ab856f583ee52a4ad5e1c2ef14b",
                "e9960d58dacdbb79b32b45e8c4b8d5db23fb0090eee6ab758152078754c65950")
COMPILER_HASHES = ("628f37fb8e1fb1f82180b90a6ff241b9e868482914e4ce9dd84149e453b8e00d",
                   "2a22d63af34fb093c3feeef538fdb794fa6b57074fddcc601b473162937043b7")
OUTPUT_CHECK_HASHES = ("431f4162f247c4f3343d3638463ac88e59c93a6c4b50c940e93cb27f1c438f6b",
                       "cbbe818dfd86d97195d4efc1241028a0bc5c27841d71d175136f5d8cc50c3f53")
PREDICTION_HASHES = ("cf06a21a5f5a9b85cc94cf0d4e41cebc672813a7a82c30728832291dc25bcdb8",
                     "4bddac581f21a70aa212ec0039f9ab2ab64badbcd315419e2e149a7ae7240166")
QUESTION_HASHES = ("44be22b8c5afc28f285e283cf50713a62711a14d3578c77cb3dca36fcdbfed8a",
                   "eefe8a4510dea813550508f4eb2c63305a824f465719fec486eba0e828dade82")
BEFORE_QUESTION = "Supón que se completan apertura y escritura. ¿Qué texto aparece en la salida estándar de este programa?"
AFTER_QUESTION = "Supón que la apertura, la escritura y el cierre tienen éxito. ¿Qué texto aparece en la salida estándar de este programa?"
EXPLANATION = ("La salida coincide con las tres líneas pedidas, en su orden y con un salto después de cada una. "
               "Comprueba también que tu función forma los campos desde sus parámetros, devuelve la cadena y deja "
               "la presentación a main; la coincidencia de la salida no certifica esa organización.")
REVIEW_FEEDBACK = [
    {"outputs": [""], "text": "La ejecución terminó sin mostrar texto. Completa la composición y las tres llamadas, guarda sus resultados y envíalos a la salida desde main."},
    {"outputs": ["Validos=2;Rechazados=1\nValidos=0;Rechazados=0"],
     "text": "Esta salida contiene sólo dos líneas y no tiene salto final. Revisa la tercera llamada y los saltos solicitados para la batería completa."},
]


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_bytes(locale: str, payload: bytes) -> bytes:
    if locale not in SOURCE_HASHES or hashlib.sha256(payload).hexdigest() != SOURCE_HASHES[locale]:
        raise ValueError("U12L4 feedback source differs from its complete pinned Git publication")
    return payload


def historical_bytes(locale: str = "es") -> bytes:
    if locale not in SOURCE_HASHES:
        raise ValueError("Unsupported U12L4 feedback QA locale")
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{PREFIX.format(locale=locale) + LESSON}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    return verify_bytes(locale, payload)


def verify_pair(before: dict, after: dict, family: dict[str, dict]) -> None:
    verify_bytes("es", json_bytes(before))
    if (hashlib.sha256(json_bytes(after)).hexdigest() != AFTER_SHA256 or set(family) != set(SOURCE_HASHES)
            or family["es"] != before or before["id"] != LESSON_ID or after["id"] != LESSON_ID):
        raise ValueError("U12L4 feedback proposal lost its complete source, exact destination or owner")
    for locale in SOURCE_HASHES:
        verify_bytes(locale, json_bytes(family[locale]))
    old, new = layout._blocks(before), layout._blocks(after)
    if len(old) != 25 or old.keys() != new.keys() or {key for key in old if old[key] != new[key]} != {COMPILER, PREDICTION}:
        raise ValueError("U12L4 feedback changed an independent complete block or stable identity")
    for field, digest in (("family", FAMILY_SHA256), ("origin", ORIGIN_SHA256), ("current", CURRENT_SHA256)):
        if layout.fingerprint(before["stateLayout"][field]) != digest or before["stateLayout"][field] != after["stateLayout"][field]:
            raise ValueError("U12L4 feedback changed the original language family or storage addresses")
    prefix, editorial = before["stateLayout"]["editorial"], after["stateLayout"]["editorial"]
    if (len(prefix) != 15 or len(editorial) != 17 or editorial[:15] != prefix
            or layout.fingerprint(prefix) != EDITORIAL_PREFIX_SHA256
            or layout.fingerprint(editorial) != AFTER_EDITORIAL_SHA256):
        raise ValueError("U12L4 feedback changed the complete fifteen-edge history or its two new proofs")
    for identity, hashes in ((COMPILER, COMPILER_HASHES), (PREDICTION, PREDICTION_HASHES)):
        if tuple(layout.fingerprint(document[identity]) for document in (old, new)) != hashes:
            raise ValueError("U12L4 feedback changed one of the two closed complete payload pairs")
    if (tuple(layout.fingerprint(blocks[COMPILER]["outputCheck"]) for blocks in (old, new)) != OUTPUT_CHECK_HASHES
            or tuple(hashlib.sha256(blocks[PREDICTION]["question"].encode()).hexdigest() for blocks in (old, new)) != QUESTION_HASHES
            or tuple(layout.fingerprint(document["pages"]) for document in (before, after)) != PAGES_HASHES):
        raise ValueError("U12L4 feedback changed its exact output objective, hypothesis or page scripts")
    restored = deepcopy(after)
    layout._blocks(restored)[COMPILER]["outputCheck"] = deepcopy(old[COMPILER]["outputCheck"])
    layout._blocks(restored)[PREDICTION]["question"] = BEFORE_QUESTION
    del restored["stateLayout"]["editorial"][15:]
    if restored != before:
        raise ValueError("U12L4 feedback changed fields beyond the question and exact feedback addition")
    if (sum(field in block for block in old.values() for field in ("code", "initialCode")) != 7
            or any(old[key].get(field) != new[key].get(field) for key in old for field in ("code", "initialCode"))):
        raise ValueError("U12L4 feedback changed one of the seven published source programs")
    layout.validate_transition(before, after)
    layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])


def candidate() -> tuple[dict, dict, dict[str, dict]]:
    """Return the real fifteen-edge publication and only the reviewed proposal."""
    family = {locale: json.loads(historical_bytes(locale)) for locale in SOURCE_HASHES}
    before = family["es"]
    draft = deepcopy(before)
    question = layout._blocks(draft)[PREDICTION]
    if question.get("question") != BEFORE_QUESTION or question.get("id") != ACTIVITY_ID:
        raise ValueError("U12L4 feedback source lost its original file-output prediction")
    question["question"] = AFTER_QUESTION
    case = layout._blocks(draft)[COMPILER]["outputCheck"]["cases"][0]
    case["explanation"] = EXPLANATION
    case["reviewFeedback"] = deepcopy(REVIEW_FEEDBACK)
    after = layout.prepare_v2(draft, "es", family, before)
    verify_pair(before, after, family)
    return before, after, family


def export(output: Path) -> Path:
    """Export authenticated QA copies only below this checkout's out/build."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("U12L4 feedback QA output cannot follow links or reparse points")
    output = output.resolve(); owner = ROOT.resolve()
    if not any(output.is_relative_to(owner / folder) and output != owner / folder for folder in ("out", "build")):
        raise ValueError("U12L4 feedback fixtures must stay in this checkout's out/build QA directory")
    for parent in (output, *output.parents):
        if parent == owner:
            break
        if (parent / ".git").exists():
            raise ValueError("U12L4 feedback QA cannot write another checkout")
    before, after, _ = candidate()
    old = layout._blocks(before)
    manifest = {
        "commit": COMMIT, "relative": RELATIVE, "lessonId": LESSON_ID, "afterIsProposal": True,
        "coordinateBase": 0, "identities": 25, "sources": 7, "editorialBefore": 15, "editorialAfter": 17,
        "sourceSha256": SOURCE_HASHES, "familySha256": FAMILY_SHA256, "originSha256": ORIGIN_SHA256,
        "currentSha256": CURRENT_SHA256, "editorialPrefixSha256": EDITORIAL_PREFIX_SHA256,
        "editorialSha256": AFTER_EDITORIAL_SHA256, "pagesSha256": list(PAGES_HASHES),
        "before": {"file": "before.json", "sha256": SOURCE_HASHES["es"]},
        "after": {"file": "after.json", "sha256": AFTER_SHA256},
        "blocks": {COMPILER: {"historical": [2, 3], "sha256": list(COMPILER_HASHES), "outputCheckSha256": list(OUTPUT_CHECK_HASHES)},
                   PREDICTION: {"historical": [4, 1], "sha256": list(PREDICTION_HASHES), "questionSha256": list(QUESTION_HASHES)}},
        "previousBlocks": {identity: old[identity] for identity in (COMPILER, PREDICTION)},
        "compilerPuzzlesRequired": after["compilerPuzzlesRequired"], "referenceAfter": after["referenceAfter"],
        "activityAfter": after["activityAfter"], "editorial": after["stateLayout"]["editorial"],
    }
    payloads = {"before.json": json_bytes(before), "after.json": json_bytes(after), "manifest.json": json_bytes(manifest)}
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing U12L4 feedback QA document")
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
    print(f"Verified closed U12L4 feedback proposal from {COMMIT}; no live curriculum edited.")

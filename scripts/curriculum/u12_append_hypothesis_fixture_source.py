"""Authenticate the exact U12L3 hypothesis revision without editing curriculum."""
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
COMMIT = "93923a470b98e9bd6acaf95cf836dd014b5e4a29"
LESSON = "archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
RELATIVE = PREFIX.format(locale="es") + LESSON
IDENTITY = "block_0002_0001"
ACTIVITY_ID = "segunda_apertura_reemplaza"
HISTORICAL = (2, 1)
BEFORE = ("Supón que las operaciones de archivo se completan. Esta versión omite app en la segunda apertura. "
          "Predice el diagnóstico visible.")
AFTER = ("Supón que las aperturas, escrituras y cierres tienen éxito. Esta versión omite std::ios::app en la segunda apertura. "
         "Predice el diagnóstico visible.")
TEXT_HASHES = ("cb81a97d61799530a8368b9b40e38e4beb06e9bdc02e882d9a60d03fd6703550",
               "2ab938406719084321fba3a22945ebd942e2a231674cd3cac688b917a026bc4d")
SOURCE_HASHES = {"es": "81fb6029dc88ca86fd412f9084b758a953bc14d20322a98c6dbc7fbc6987d183",
                 "en": "e1ee5510ea9a7e2451103b298f29c695a23cf6d76cdd95cef245b376988501cd",
                 "fr": "d1c22d2c6da13a48bd2310fd72235319d6a3de899bd129444e4853e2e0c426f8"}
AFTER_SHA256 = "d5cafa688c73961ff3f4e60897d23d69f0df7826a9c192e723bca7a1e34c3dcc"
BLOCK_HASHES = ("3f3f15e209f0d54ce342167a2e007062ebf83bb1784531d40873a75dafd765ef",
                "b80a16cd7af0d46d6628a11784747299e7607317f58f87b48a41019f923157b2")
FAMILY_SHA256 = "a50cf6bf3544c70d3df1b5f9e152f35d51214cf4d6999d02fb4d8b568a0e044a"
ORIGIN_SHA256 = "70274d28ca19d9c4e9c5f2d1632b135aeb5b86e4c4431dfcbd023d68c87a0ef7"
EDITORIAL_PREFIX_SHA256 = "2bbd1afca905bbafa53a49d7197e1f328bdd2f2107e305583156a3182702387c"
CURRENT_SHA256 = "553e987ff02d0c346013113e6544d8f615601e4672439fa7e1010fa6a4838e65"
# The same exact question pair was subsequently published after the first
# editor/reference preparation. Keep that real 11->12 pair separate from the
# earlier 6->7 candidate; neither fixture follows mutable HEAD or live assets.
CURRENT_BEFORE_COMMIT = "18d34c2f94f25a0796534d53fe4fd21a79aa09ce"
CURRENT_AFTER_COMMIT = "fe9dff99f7237163abd1c00a8aa5c4ddda4a1681"
CURRENT_SOURCE_HASHES = dict(SOURCE_HASHES, es="e08054fbdeb93756cb45ae6515279742ee273f55c4ddf6a1b6962b447ed50a4e")
CURRENT_AFTER_SHA256 = "9fa23a593751256a5936eabfe1ae7dc8a2388a2dd80a146af6338557c625a55b"
CURRENT_AFTER_SOURCE_HASHES = dict(SOURCE_HASHES, es=CURRENT_AFTER_SHA256)
CURRENT_EDITORIAL_PREFIX_SHA256 = "6ea8b50922d76302cd12241f8a57cc189a0e90532a6f71490e12857b361b0713"
CURRENT_EDITORIAL_SHA256 = "ec7393ec6101f1c12e30320218d163975231b416c5b87f045f145f370e84b1d4"
SUFFIX_BEFORE_COMMIT = CURRENT_BEFORE_COMMIT
SUFFIX_AFTER_COMMIT = "b2208f7da1d985216867a7ff4d4275bc3bc0ca8b"
SUFFIX_SOURCE_HASHES = dict(CURRENT_SOURCE_HASHES)
SUFFIX_AFTER_SHA256 = "d90b1ef97fe852fb6e4d6f55d841f04415cbc94a4c58459c83a5b83c5d6af526"
SUFFIX_AFTER_SOURCE_HASHES = dict(SOURCE_HASHES, es=SUFFIX_AFTER_SHA256)
SUFFIX_EDITORIAL_SHA256 = "881930065caeaf0e0ddc512d966a1cde30574b7a1e64eb7aabec2169702b68f4"


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_bytes(locale: str, payload: bytes) -> bytes:
    if locale not in SOURCE_HASHES or hashlib.sha256(payload).hexdigest() != SOURCE_HASHES[locale]:
        raise ValueError("U12L3 hypothesis fixture differs from its independently pinned Git bytes")
    return payload


def historical_bytes(locale: str = "es") -> bytes:
    relative = PREFIX.format(locale=locale) + LESSON
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def candidate() -> tuple[dict, dict, dict[str, dict]]:
    family = {locale: json.loads(historical_bytes(locale)) for locale in ("es", "en", "fr")}
    before = family["es"]
    original = layout._blocks(before)[IDENTITY]
    if (original.get("type"), original.get("id"), original.get("question")) != ("output_prediction", ACTIVITY_ID, BEFORE):
        raise ValueError("U12L3 hypothesis fixture owner, type or question changed")
    if tuple(hashlib.sha256(text.encode("utf-8")).hexdigest() for text in (BEFORE, AFTER)) != TEXT_HASHES:
        raise ValueError("U12L3 hypothesis text constants changed")
    metadata = before["stateLayout"]
    if (len(layout._blocks(before)) != 33 or len(metadata["editorial"]) != 6
            or layout.fingerprint(metadata["family"]) != FAMILY_SHA256
            or layout.fingerprint(metadata["origin"]) != ORIGIN_SHA256
            or layout.fingerprint(metadata["current"]) != CURRENT_SHA256
            or layout.fingerprint(metadata["editorial"]) != EDITORIAL_PREFIX_SHA256):
        raise ValueError("U12L3 hypothesis fixture lost its published family, addresses or provenance")
    draft = deepcopy(before)
    layout._blocks(draft)[IDENTITY]["question"] = AFTER
    after = layout.prepare_v2(draft, "es", family, before)
    if (tuple(layout.fingerprint(layout._blocks(value)[IDENTITY]) for value in (before, after)) != BLOCK_HASHES
            or hashlib.sha256(json_bytes(after)).hexdigest() != AFTER_SHA256):
        raise ValueError("U12L3 hypothesis candidate differs from its independently pinned complete document")
    return before, after, family


def verify_current_bytes(locale: str, payload: bytes, *, after: bool = False) -> bytes:
    hashes = CURRENT_AFTER_SOURCE_HASHES if after else CURRENT_SOURCE_HASHES
    if locale not in hashes or hashlib.sha256(payload).hexdigest() != hashes[locale]:
        raise ValueError("Published U12L3 hypothesis fixture differs from its independently pinned Git bytes")
    return payload


def current_bytes(locale: str = "es", *, after: bool = False) -> bytes:
    relative = PREFIX.format(locale=locale) + LESSON
    commit = CURRENT_AFTER_COMMIT if after else CURRENT_BEFORE_COMMIT
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_current_bytes(locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), after=after)


def current_candidate() -> tuple[dict, dict, dict[str, dict]]:
    """Return the complete published 11->12 pair with its unchanged family."""
    family = {locale: json.loads(current_bytes(locale)) for locale in ("es", "en", "fr")}
    before = family["es"]
    original = layout._blocks(before)[IDENTITY]
    if (original.get("type"), original.get("id"), original.get("question")) != ("output_prediction", ACTIVITY_ID, BEFORE):
        raise ValueError("Published U12L3 hypothesis fixture owner, type or question changed")
    metadata = before["stateLayout"]
    if (len(layout._blocks(before)) != 33 or len(metadata["editorial"]) != 11
            or layout.fingerprint(metadata["family"]) != FAMILY_SHA256
            or layout.fingerprint(metadata["origin"]) != ORIGIN_SHA256
            or layout.fingerprint(metadata["current"]) != CURRENT_SHA256
            or layout.fingerprint(metadata["editorial"]) != CURRENT_EDITORIAL_PREFIX_SHA256):
        raise ValueError("Published U12L3 fixture lost its actual family, addresses or eleven-edge provenance")
    draft = deepcopy(before)
    layout._blocks(draft)[IDENTITY]["question"] = AFTER
    after = layout.prepare_v2(draft, "es", family, before)
    published_after = current_bytes(after=True)
    if (json_bytes(after) != published_after
            or hashlib.sha256(published_after).hexdigest() != CURRENT_AFTER_SHA256
            or tuple(layout.fingerprint(layout._blocks(value)[IDENTITY]) for value in (before, after)) != BLOCK_HASHES
            or layout.fingerprint(after["stateLayout"]["editorial"]) != CURRENT_EDITORIAL_SHA256):
        raise ValueError("Published U12L3 candidate differs from its complete exact after publication")
    return before, after, family


def verify_suffix_bytes(locale: str, payload: bytes, *, after: bool = False) -> bytes:
    hashes = SUFFIX_AFTER_SOURCE_HASHES if after else SUFFIX_SOURCE_HASHES
    if locale not in hashes or hashlib.sha256(payload).hexdigest() != hashes[locale]:
        raise ValueError("U12L3 compatible-suffix fixture differs from its independently pinned Git bytes")
    return payload


def suffix_bytes(locale: str = "es", *, after: bool = False) -> bytes:
    relative = PREFIX.format(locale=locale) + LESSON
    commit = SUFFIX_AFTER_COMMIT if after else SUFFIX_BEFORE_COMMIT
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_suffix_bytes(locale, subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), after=after)


def suffix_candidate() -> tuple[dict, dict, dict[str, dict]]:
    """Return raw publications 11->14, keeping the two later p6 edits intact.

    This pair is not a fabricated question-only predecessor of the full later
    document. Its source is the actual eleven-edge publication; its destination
    includes the exact question edge followed by two real subsequent p6 edges.
    """
    before, question_after, family = current_candidate()
    if json.loads(suffix_bytes()) != before:
        raise ValueError("U12L3 compatible-suffix source differs from its actual published predecessor")
    payload = suffix_bytes(after=True)
    after = json.loads(payload)
    metadata = after["stateLayout"]
    if (json_bytes(after) != payload or hashlib.sha256(payload).hexdigest() != SUFFIX_AFTER_SHA256
            or len(layout._blocks(after)) != 33 or len(metadata["editorial"]) != 14
            or metadata["editorial"][:12] != question_after["stateLayout"]["editorial"]
            or layout.fingerprint(metadata["editorial"]) != SUFFIX_EDITORIAL_SHA256
            or layout.fingerprint(metadata["family"]) != FAMILY_SHA256
            or layout.fingerprint(metadata["origin"]) != ORIGIN_SHA256
            or layout.fingerprint(metadata["current"]) != CURRENT_SHA256
            or tuple(layout.fingerprint(layout._blocks(value)[IDENTITY]) for value in (before, after)) != BLOCK_HASHES):
        raise ValueError("U12L3 suffix fixture lost its complete publication, exact hypothesis or actual provenance")
    if [edge["id"] for edge in metadata["editorial"][12:]] != ["block_0005_0001", "block_0005_0002"]:
        raise ValueError("U12L3 compatible suffix changed its two later p6 owners")
    layout.validate_transition(before, after)
    layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])
    return before, after, family


def export(output: Path) -> Path:
    """Export authenticated QA copies under this checkout's out/build only."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("U12L3 hypothesis QA output cannot follow links or reparse points")
    output = output.resolve()
    if not any(output.is_relative_to(ROOT / allowed) and output != ROOT / allowed for allowed in ("out", "build")):
        raise ValueError("U12L3 hypothesis fixtures must stay in this checkout's out/build QA directory")
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("U12L3 hypothesis QA output cannot write another checkout")
    before, after, _ = candidate()
    current_before, current_after, _ = current_candidate()
    suffix_before, suffix_after, _ = suffix_candidate()
    payloads = {"before.json": historical_bytes(), "after.json": json_bytes(after)}
    manifest = {"commit": COMMIT, "relative": RELATIVE, "stableId": IDENTITY, "activityId": ACTIVITY_ID,
                "coordinateBase": 0, "historical": list(HISTORICAL), "field": "question",
                "sourceSha256": SOURCE_HASHES, "textSha256": list(TEXT_HASHES),
                "familySha256": FAMILY_SHA256, "originSha256": ORIGIN_SHA256,
                "editorialPrefixSha256": EDITORIAL_PREFIX_SHA256, "currentSha256": CURRENT_SHA256,
                "before": {"file": "before.json", "sha256": SOURCE_HASHES["es"], "blockSha256": BLOCK_HASHES[0]},
                "after": {"file": "after.json", "sha256": AFTER_SHA256, "blockSha256": BLOCK_HASHES[1]}}
    payloads["manifest.json"] = json_bytes(manifest)
    payloads["current-before.json"] = current_bytes()
    payloads["current-after.json"] = json_bytes(current_after)
    current_manifest = {"beforeCommit": CURRENT_BEFORE_COMMIT, "afterCommit": CURRENT_AFTER_COMMIT,
                        "relative": RELATIVE, "stableId": IDENTITY, "activityId": ACTIVITY_ID,
                        "coordinateBase": 0, "historical": list(HISTORICAL), "field": "question",
                        "sourceSha256": CURRENT_SOURCE_HASHES, "afterSourceSha256": CURRENT_AFTER_SOURCE_HASHES,
                        "textSha256": list(TEXT_HASHES), "familySha256": FAMILY_SHA256, "originSha256": ORIGIN_SHA256,
                        "currentSha256": CURRENT_SHA256, "editorialPrefixSha256": CURRENT_EDITORIAL_PREFIX_SHA256,
                        "editorialSha256": CURRENT_EDITORIAL_SHA256, "editorialBefore": 11, "editorialAfter": 12,
                        "before": {"file": "current-before.json", "sha256": CURRENT_SOURCE_HASHES["es"], "blockSha256": BLOCK_HASHES[0]},
                        "after": {"file": "current-after.json", "sha256": CURRENT_AFTER_SHA256, "blockSha256": BLOCK_HASHES[1]}}
    payloads["current-manifest.json"] = json_bytes(current_manifest)
    payloads["suffix-before.json"] = suffix_bytes()
    payloads["suffix-after.json"] = json_bytes(suffix_after)
    suffix_manifest = {"beforeCommit": SUFFIX_BEFORE_COMMIT, "afterCommit": SUFFIX_AFTER_COMMIT,
                       "relative": RELATIVE, "stableId": IDENTITY, "activityId": ACTIVITY_ID,
                       "coordinateBase": 0, "historical": list(HISTORICAL), "field": "question",
                       "sourceSha256": SUFFIX_SOURCE_HASHES, "afterSourceSha256": SUFFIX_AFTER_SOURCE_HASHES,
                       "textSha256": list(TEXT_HASHES), "familySha256": FAMILY_SHA256, "originSha256": ORIGIN_SHA256,
                       "currentSha256": CURRENT_SHA256, "editorialPrefixSha256": CURRENT_EDITORIAL_PREFIX_SHA256,
                       "editorialSha256": SUFFIX_EDITORIAL_SHA256, "editorialBefore": 11, "editorialAfter": 14,
                       "questionEditorialAfter": 12, "laterEditorial": suffix_after["stateLayout"]["editorial"][12:],
                       "referenceAfter": suffix_after["referenceAfter"], "activityAfter": suffix_after["activityAfter"],
                       "before": {"file": "suffix-before.json", "sha256": SUFFIX_SOURCE_HASHES["es"], "blockSha256": BLOCK_HASHES[0]},
                       "after": {"file": "suffix-after.json", "sha256": SUFFIX_AFTER_SHA256, "blockSha256": BLOCK_HASHES[1]}}
    payloads["suffix-manifest.json"] = json_bytes(suffix_manifest)
    # All input authentication and existing-target checks precede any write.
    # Identical exported evidence is a no-op; other bytes are never replaced.
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing U12L3 hypothesis QA document")
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
    print(f"Verified historical, published and compatible-suffix U12L3 hypothesis pairs through {SUFFIX_AFTER_COMMIT}.")

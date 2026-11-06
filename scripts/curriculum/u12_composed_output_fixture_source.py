"""Authenticate U12L4's real unmapped publication and first v2 adoption for QA."""
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
BEFORE_COMMIT = "4741c4241b4e83f8458c7ad4bb051e6559aa21ca"
AFTER_COMMIT = "05f14cabc192eb8851d8bd8b02ab2aecb744dbf6"
LESSON = "archivos_y_programa_completo/lesson_componer_salida_sin_imprimirla.json"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/"
RELATIVE = PREFIX.format(locale="es") + LESSON
LESSON_ID = "cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"
SOURCE_HASHES = {
    "es": "be285a02171b33a68b30900a7739ffd8b8c5ce5ccee98b6f78ec73f41ad797be",
    "en": "a932d7011d13ede1b33277fdf0e023b414f64571067b06c7e9bd1b6cc021dc6d",
    "fr": "37dbbed46bb7d292e686d3b7376eeed73d4e2869454f63a7c9dae48b3d647acb",
}
AFTER_SOURCE_HASHES = dict(SOURCE_HASHES, es="eb160976072ae3bb891ea5fab0bb1336a7d24bb0a83282756634d5189814a6f3")
FAMILY_SHA256 = "f9865100c59e76353575893fd342d00b0ce5eee3ef2c9605a4b9189db4fbd887"
ORIGIN_SHA256 = "6c5a4300e032dfde02180cfb468cbb2b418931fbc65fb8f771561aa30b2dbee3"
CURRENT_SHA256 = "430aa6b167fac29d684c9c45734b6c611ce06be9bbd16a303c598d1c562a6e92"
EDITORIAL_SHA256 = "1ad25edf3618c1f51a1e21504b52bbc65f27d70b7deba6ef5344d126912c345b"
BEFORE_PAGES_SHA256 = "ffeb1349bb0fcd7d1bc9e7002c344ce68e3fcf4a8162464e9b908606521010a8"
AFTER_PAGES_SHA256 = "b3ee4c125cf8fbc0242a4a0cbb24f34dc03677d84ad6485c7c319c8fbd878852"
SHAPE = (5, 6, 5, 6, 3)
# Zero-based coordinates and complete block fingerprints. The runtime bridge
# may reconstruct only these three authentic old prose blocks, not new history.
PARAGRAPHS = {
    "block_0000_0001": (0, 1, "c02adc2d24a66e7b0dce4f5aa5692cb2f204850bb1552792ca1bd5e2087c6b19",
                         "b3d34bd31f3e0109c48641f6c3bd16dd75eddaeda0a9788611bd595e79d73f21"),
    "block_0000_0003": (0, 3, "53da4b1c7e1185a16256c4981ac6e5efc6ae5367e6786724929ebe9ef483b2fc",
                         "d90d01bec3b7c8fedd2054b4ce07e9b3694b28f3617a894ce5ab6e88935eac82"),
    "block_0001_0002": (1, 2, "fa2bd564d660cb74e06163a7cbb9986c6fdfb9bb83a6743027e363f38b32b566",
                         "a3c73edce6e41ffa8f86f478a5667a53e2ed1801fb43430e89721228e35d5e9b"),
}
GUIDES = {
    "block_0000_0004": (0, 4, "7a041d7e9cfc4c7f5e8cc7000144fa9123dac81f9af54e4505a19640e2081e58",
                         "2834c29f0d3680bdf1fda9e6b573ba3e624ebd507e67d7083e6654d1ef4140fd"),
    "block_0001_0001": (1, 1, "042cc93bd22860e2760dabc3b06baab868eae9c22a1e89c0d738f16b9f9b264b",
                         "79de873c83c4090d903b2ac91b67fe186b053b45ba2566b33ed51bb5e909f114"),
    "block_0001_0003": (1, 3, "faf6393e200b49d4b9a31be600ebabd33fe4f7b224ff6bfbc6479a7027382480",
                         "4d21f3591bfa76c81c6fa7fdc48ffb455745414abdad0fc9cf1cb768b9d7bf0f"),
    "block_0001_0004": (1, 4, "5259a7687faffd5b6daf28dd2db9a4f73c387bc13c9f78b97affee8c1987b622",
                         "7fed11b6ada96a990c364b94264c5152690187a2b59106156cfe225daa2c31b3"),
    "block_0001_0005": (1, 5, "2b80ca083b2d0eb1b2a5b9492a15ae5ae2095f880cbcb65f36276a6069705109",
                         "8216c1ca7b4af782b539a6e72edf5b36ccd0d3fcf98103a21da4453398dbaf22"),
}
# The authentic later publication keeps the first two adopted pages intact.
# It is a separate QA pair, never a rewritten destination of the v0->8 pair.
CURRENT_AFTER_COMMIT = "57c0044a7484f1e6726383e9f880137cf43f6aa9"
CURRENT_SOURCE_HASHES = dict(SOURCE_HASHES, es="ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87")
CURRENT_EDITORIAL_SHA256 = "317364cd08c3847423c5a3d3a6b8b017e1882f91be895b738361aef13a68a956"
CURRENT_PAGES_SHA256 = "5c2c07a09c4679edb36ce6c246d0dcb265f00ab856f583ee52a4ad5e1c2ef14b"
CURRENT_SUFFIX = {
    "block_0002_0001": (2, 1, "5683b9e6da30e5658b28bbbbbc4cfa602b652c8cb562d846f32c20ab336b2fe5",
                         "8aad687163b7361a3fdbab5bebb06aa3b0e57af18d50765a8938368805660e15"),
    "block_0002_0002": (2, 2, "f3456920a23b18612356d5296cfcf87adb6e561f03ab7a7273a7ac441c59e6f6",
                         "555990b854070163a6d96061ca9fa690a0a2cd8a1c9e47b478eb0dd80b112cbe"),
    "block_0002_0004": (2, 4, "b19392377d5785801d0bbd9fe19e2c2ef50978ebf3fbbb5a510fcbdf1636bc08",
                         "139186b1b82089ff714c591ffc9c4ed37bea650945b460e7ff5e8dc0d45f9b42"),
    "block_0003_0000": (3, 0, "188c6c1eb53fc0be219b19b818186ea265781a702fc61f0d857218b0ceda24c3",
                         "087adcf253a4fe3b8c6a7ec07f9ff09d5328f159d5862f6e93533e40224e558d"),
    "block_0003_0002": (3, 2, "f277c8b2a5771dfce3e39f4aec72e1901985325daa8b26e70323bd66bcdb2ab7",
                         "0c246776640f64ba0d425dc54691b9ce686e2d4bdf9726c48b881b1c14993610"),
    "block_0003_0003": (3, 3, "9a5f1fe62ddc28a6d0149f303e7259032bdad3930f7142b1a0c09e1dfa49f402",
                         "678f48ca832205258f8e20b607727d87300dffa7e708c9ff6dae5ed7a823246e"),
    "block_0003_0004": (3, 4, "4c7882e27fbce78d45d53fd67b0e9f7eb25a26693e4ad2709ba1bb2bc6be75da",
                         "3582a8ce527b0e5a588794b19fce3238e5d7560740da2c930c3247a4ed4b55c8"),
}


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_bytes(locale: str, payload: bytes, *, after: bool = False) -> bytes:
    hashes = AFTER_SOURCE_HASHES if after else SOURCE_HASHES
    if locale not in hashes or hashlib.sha256(payload).hexdigest() != hashes[locale]:
        raise ValueError("U12L4 QA fixture differs from its complete pinned Git publication")
    return payload


def historical_bytes(locale: str = "es", *, after: bool = False) -> bytes:
    if locale not in SOURCE_HASHES:
        raise ValueError("Unsupported U12L4 QA locale")
    commit = AFTER_COMMIT if after else BEFORE_COMMIT
    relative = PREFIX.format(locale=locale) + LESSON
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    return verify_bytes(locale, payload, after=after)


def verify_pair(before: dict, after: dict, family: dict[str, dict]) -> None:
    """Check the closed published adoption; this grants no new editorial rules."""
    verify_bytes("es", json_bytes(before))
    verify_bytes("es", json_bytes(after), after=True)
    if set(family) != set(SOURCE_HASHES) or family["es"] != before:
        raise ValueError("U12L4 QA lost its original complete language family")
    for locale, document in family.items():
        verify_bytes(locale, json_bytes(document))
    if ("stateLayout" in before or before.get("id") != LESSON_ID or after.get("id") != LESSON_ID
            or set(before) != {"id", "title", "pages"} or set(after) != {*before, "stateLayout"}
            or tuple(len(page["blocks"]) for page in before["pages"]) != SHAPE
            or tuple(len(page["blocks"]) for page in after["pages"]) != SHAPE):
        raise ValueError("U12L4 adoption changed its unmapped source, owner or five-page shape")
    metadata = after["stateLayout"]
    expected = {"family": FAMILY_SHA256, "origin": ORIGIN_SHA256,
                "current": CURRENT_SHA256, "editorial": EDITORIAL_SHA256}
    if (metadata["version"] != 2 or metadata["locale"] != "es" or metadata["additions"]
            or len(layout._blocks(after)) != 25 or len(metadata["editorial"]) != 8
            or any(layout.fingerprint(metadata[field]) != digest for field, digest in expected.items())):
        raise ValueError("U12L4 adoption changed its 25 identities, family, origin or eight proofs")
    changes = {**PARAGRAPHS, **GUIDES}
    prior_blocks = layout._blocks(layout.prepare(before))
    current_blocks = layout._blocks(after)
    actual = {identity for identity in prior_blocks if prior_blocks[identity] != current_blocks[identity]}
    if actual != changes.keys():
        raise ValueError("U12L4 adoption changed an independent complete block")
    for identity, (page, block, old_hash, new_hash) in changes.items():
        old, new = prior_blocks[identity], current_blocks[identity]
        if (old != before["pages"][page]["blocks"][block] or new != after["pages"][page]["blocks"][block]
                or layout.fingerprint(old) != old_hash or layout.fingerprint(new) != new_hash):
            raise ValueError("U12L4 adoption moved or replaced an authenticated complete block")
        fields = {field for field in old.keys() | new.keys() if old.get(field) != new.get(field)}
        if (fields != ({"text", "emphasis"} if identity in PARAGRAPHS else {"reviewFeedback"})
                or (identity in GUIDES and "reviewFeedback" in old)):
            raise ValueError("U12L4 adoption changed more than its three prose passages and five guides")
    sources = [(identity, field, value[field]) for identity, value in prior_blocks.items()
               for field in ("code", "initialCode") if field in value]
    if len(sources) != 7 or any(current_blocks[identity].get(field) != source for identity, field, source in sources):
        raise ValueError("U12L4 adoption changed one of its seven literal sources")
    if (before["pages"][2:] != after["pages"][2:]
            or layout.fingerprint(before["pages"]) != BEFORE_PAGES_SHA256
            or layout.fingerprint(after["pages"]) != AFTER_PAGES_SHA256):
        raise ValueError("U12L4 adoption changed the untouched p3-p5 content")
    draft = deepcopy(after)
    del draft["stateLayout"]
    if layout.prepare_v2(draft, "es", family, before) != after:
        raise ValueError("U12L4 first adoption cannot be reproduced from its real original family")
    layout.validate_transition(before, after)
    layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])


def candidate() -> tuple[dict, dict, dict[str, dict]]:
    family = {locale: json.loads(historical_bytes(locale)) for locale in SOURCE_HASHES}
    before = family["es"]
    after = json.loads(historical_bytes(after=True))
    for locale in ("en", "fr"):
        if historical_bytes(locale, after=True) != json_bytes(family[locale]):
            raise ValueError("U12L4 adoption changed an unrevised translation")
    verify_pair(before, after, family)
    return before, after, family


def verify_current_bytes(locale: str, payload: bytes) -> bytes:
    if locale not in CURRENT_SOURCE_HASHES or hashlib.sha256(payload).hexdigest() != CURRENT_SOURCE_HASHES[locale]:
        raise ValueError("Current U12L4 fixture differs from its complete pinned fifteen-edge Git publication")
    return payload


def historical_current_bytes(locale: str = "es") -> bytes:
    if locale not in CURRENT_SOURCE_HASHES:
        raise ValueError("Unsupported current U12L4 QA locale")
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show",
         f"{CURRENT_AFTER_COMMIT}:{PREFIX.format(locale=locale) + LESSON}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    return verify_current_bytes(locale, payload)


def verify_current_pair(before: dict, current: dict, family: dict[str, dict]) -> None:
    """Keep real later p3/p4 content; only p1/p2 have old reading payloads."""
    verify_bytes("es", json_bytes(before))
    verify_current_bytes("es", json_bytes(current))
    adoption = json.loads(historical_bytes(after=True))
    verify_pair(before, adoption, family)
    metadata = current["stateLayout"]
    if (current["id"] != LESSON_ID or tuple(len(page["blocks"]) for page in current["pages"]) != SHAPE
            or len(layout._blocks(current)) != 25 or len(metadata["editorial"]) != 15
            or metadata["editorial"][:8] != adoption["stateLayout"]["editorial"]
            or layout.fingerprint(metadata["editorial"]) != CURRENT_EDITORIAL_SHA256
            or layout.fingerprint(current["pages"]) != CURRENT_PAGES_SHA256
            or current["pages"][:2] != adoption["pages"][:2]):
        raise ValueError("Current U12L4 lost its authentic first two pages, eight-edge prefix or seven-edge suffix")
    for field in adoption["stateLayout"]:
        if field != "editorial" and metadata[field] != adoption["stateLayout"][field]:
            raise ValueError("Current U12L4 changed its family, origin, stable owners or addition set")
    prior, active = layout._blocks(adoption), layout._blocks(current)
    if {identity for identity in prior if prior[identity] != active[identity]} != CURRENT_SUFFIX.keys():
        raise ValueError("Current U12L4 altered content beyond its seven complete later prose blocks")
    for index, (identity, (page, block, old_sha, new_sha)) in enumerate(CURRENT_SUFFIX.items()):
        old, new = prior[identity], active[identity]
        fields = {field for field in old.keys() | new.keys() if old.get(field) != new.get(field)}
        expected_fields = {"text", "emphasis"} if identity in ("block_0002_0001", "block_0002_0002") else {"text"}
        if (old != adoption["pages"][page]["blocks"][block] or new != current["pages"][page]["blocks"][block]
                or (layout.fingerprint(old), layout.fingerprint(new)) != (old_sha, new_sha)
                or fields != expected_fields or metadata["editorial"][8 + index]
                != {"id": identity, "beforeSha256": old_sha, "afterSha256": new_sha}):
            raise ValueError("Current U12L4 changed a complete later paragraph or its authentic ordered edge")
    references = [{"activity": "block_0002_0003", "blocks": [f"block_0003_{index:04d}" for index in range(6)]}]
    activities = [{"activity": "block_0004_0001", "requires": ["block_0002_0003"]}]
    if (current.get("compilerPuzzlesRequired") is not True or current.get("referenceAfter") != references
            or current.get("activityAfter") != activities):
        raise ValueError("Current U12L4 lost its authored required-editor, reference or dependent-activity gates")
    restored = deepcopy(current)
    for identity, (page, block, _, _) in CURRENT_SUFFIX.items():
        restored["pages"][page]["blocks"][block] = deepcopy(prior[identity])
    del restored["stateLayout"]["editorial"][8:]
    for field in ("compilerPuzzlesRequired", "referenceAfter", "activityAfter"):
        del restored[field]
    if restored != adoption:
        raise ValueError("Current U12L4 invented another predecessor or changed independent metadata")
    original = layout._blocks(layout.prepare(before))
    if (sum(field in block for block in original.values() for field in ("code", "initialCode")) != 7
            or any(original[key].get(field) != active[key].get(field)
                   for key in original for field in ("code", "initialCode"))
            or any(layout.state_fingerprint(original[key]) != layout.state_fingerprint(active[key]) for key in original)):
        raise ValueError("Current U12L4 changed one of its seven original programs or historical response contracts")
    layout.validate_transition(adoption, current)
    layout.validate_transition(before, current)
    layout.check_localized_family([current, family["en"], family["fr"]], ["es", "en", "fr"])


def current_candidate() -> tuple[dict, dict, dict[str, dict]]:
    family = {locale: json.loads(historical_bytes(locale)) for locale in SOURCE_HASHES}
    before = family["es"]
    current = json.loads(historical_current_bytes())
    for locale in ("en", "fr"):
        if historical_current_bytes(locale) != json_bytes(family[locale]):
            raise ValueError("Current U12L4 changed an unrevised original translation")
    verify_current_pair(before, current, family)
    return before, current, family


def export(output: Path) -> Path:
    """Export immutable fixture bytes only inside the caller checkout's QA area."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("U12L4 QA output cannot follow links or reparse points")
    output = output.resolve()
    owner = ROOT.resolve()
    if not any(output.is_relative_to(owner / allowed) and output != owner / allowed for allowed in ("out", "build")):
        raise ValueError("U12L4 fixtures must stay in this checkout's out/build QA directory")
    for parent in (output, *output.parents):
        if parent == owner:
            break
        if (parent / ".git").exists():
            raise ValueError("U12L4 QA output cannot write another checkout")
    before, after, _ = candidate()
    prior = layout._blocks(layout.prepare(before))
    manifest = {
        "beforeCommit": BEFORE_COMMIT, "afterCommit": AFTER_COMMIT, "relative": RELATIVE,
        "lessonId": LESSON_ID, "coordinateBase": 0, "identities": 25, "sources": 7,
        "beforeLayoutVersion": 0, "afterLayoutVersion": 2, "editorialBefore": 0, "editorialAfter": 8,
        "sourceSha256": SOURCE_HASHES, "afterSourceSha256": AFTER_SOURCE_HASHES,
        "familySha256": FAMILY_SHA256, "originSha256": ORIGIN_SHA256,
        "currentSha256": CURRENT_SHA256, "editorialSha256": EDITORIAL_SHA256,
        "beforePagesSha256": BEFORE_PAGES_SHA256, "afterPagesSha256": AFTER_PAGES_SHA256,
        "before": {"file": "before.json", "sha256": SOURCE_HASHES["es"]},
        "after": {"file": "after.json", "sha256": AFTER_SOURCE_HASHES["es"]},
        "paragraphs": {identity: {"historical": [page, block], "beforeSha256": old, "afterSha256": new}
                       for identity, (page, block, old, new) in PARAGRAPHS.items()},
        "previousBlocks": {identity: prior[identity] for identity in sorted({**PARAGRAPHS, **GUIDES})},
        "editorial": after["stateLayout"]["editorial"],
    }
    payloads = {"before.json": json_bytes(before), "after.json": json_bytes(after), "manifest.json": json_bytes(manifest)}
    # Authenticate every source and inspect all targets before creating a file.
    # Re-exporting identical evidence is a no-op; different evidence is retained.
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing U12L4 QA document")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        target = output / name
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


def export_current(output: Path) -> Path:
    """Export a separate v0->15 pair; existing v0->8 evidence is untouched."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("Current U12L4 QA output cannot follow links or reparse points")
    output = output.resolve(); owner = ROOT.resolve()
    if not any(output.is_relative_to(owner / allowed) and output != owner / allowed for allowed in ("out", "build")):
        raise ValueError("Current U12L4 fixtures must stay in this checkout's out/build QA directory")
    for parent in (output, *output.parents):
        if parent == owner:
            break
        if (parent / ".git").exists():
            raise ValueError("Current U12L4 QA output cannot write another checkout")
    before, current, _ = current_candidate()
    prior = layout._blocks(layout.prepare(before))
    manifest = {
        "beforeCommit": BEFORE_COMMIT, "afterCommit": CURRENT_AFTER_COMMIT, "adoptionCommit": AFTER_COMMIT,
        "relative": RELATIVE, "lessonId": LESSON_ID, "coordinateBase": 0, "identities": 25, "sources": 7,
        "beforeLayoutVersion": 0, "afterLayoutVersion": 2, "editorialBefore": 0, "editorialAfter": 15,
        "sourceSha256": SOURCE_HASHES, "afterSourceSha256": CURRENT_SOURCE_HASHES,
        "familySha256": FAMILY_SHA256, "originSha256": ORIGIN_SHA256, "currentSha256": CURRENT_SHA256,
        "editorialPrefixSha256": EDITORIAL_SHA256, "editorialSha256": CURRENT_EDITORIAL_SHA256,
        "beforePagesSha256": BEFORE_PAGES_SHA256, "afterPagesSha256": CURRENT_PAGES_SHA256,
        "before": {"file": "before.json", "sha256": SOURCE_HASHES["es"]},
        "after": {"file": "after.json", "sha256": CURRENT_SOURCE_HASHES["es"]},
        "previousBlocks": {identity: prior[identity] for identity in sorted({**PARAGRAPHS, **GUIDES})},
        "reconstructedPages": [0, 1], "editorial": current["stateLayout"]["editorial"],
        "laterEditorial": current["stateLayout"]["editorial"][8:],
        "compilerPuzzlesRequired": current["compilerPuzzlesRequired"],
        "referenceAfter": current["referenceAfter"], "activityAfter": current["activityAfter"],
    }
    payloads = {"before.json": json_bytes(before), "after.json": json_bytes(current), "manifest.json": json_bytes(manifest)}
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing current U12L4 QA document")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        target = output / name
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    destinations = parser.add_mutually_exclusive_group(required=True)
    destinations.add_argument("--output-dir", type=Path)
    destinations.add_argument("--current-output-dir", type=Path)
    arguments = parser.parse_args()
    if arguments.current_output_dir is not None:
        export_current(arguments.current_output_dir)
        print(f"Verified U12L4's original publication and current fifteen-edge publication through {CURRENT_AFTER_COMMIT}.")
    else:
        export(arguments.output_dir)
        print(f"Verified U12L4's original publication and first v2 adoption through {AFTER_COMMIT}.")

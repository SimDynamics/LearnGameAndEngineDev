"""Authenticate the complete published U11L16 histories used by reader QA.

The 17, 26 and 30-edge documents are immutable Git sources, not reconstructed
candidates. The latter two distinguish an explanation-only change on page 3
from a later revision of the paragraph preceding that same activity. Reading
these sources never fetches history, edits assets or accesses student state.
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
LESSON = "texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json"
RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/" + LESSON
LESSON_ID = "cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete"
COMMITS = {
    "before": "5cfd5514c879341216c1c34c313e34a16f60ea32",
    "published26": "6ed8e967b1dc747a133c11fc3804bed69a576ded",
    "published30": "e7d26cd5eeeef3c39d92a33928eeda74461dbb58",
}
SOURCE_HASHES = {
    "before": "e46f006765a5fa313430b13dd5b1b0a305a6add572beea529fe84f14ab0bf739",
    "published26": "0589de4ab11564c318ff0250ad822e01dbef10b7e2caae58f32d7ac7e20f6770",
    "published30": "be8de9afbb1b2bbb5ac07cacb8e8935b448fae295ca7e5235fc51ddb0a924b49",
}
EDITORIAL_COUNTS = {"before": 17, "published26": 26, "published30": 30}
ORIGINAL_SIGNATURE = "3ba01261399775eb24576eebab6f4e726e19a3a74ef5ac8ba175cdd9acdd1ede"
PAGE = 2
PAGE_ID = "page_0002"
PAGE_BLOCKS = [f"block_0002_{index:04d}" for index in range(4)]
TARGET = {
    "identity": "block_0002_0002", "historical": (2, 2),
    "type": "order_code", "activity_id": LESSON_ID + "__e04",
    "state_sha256": "289d858ddf0bced714db485cfcdf414d7110ae9b0773df5c51c5ef30842a87d2",
    "before_sha256": "524e9d5823d04f180a87ea73f34ac891b0d102450036493991026f93ce65ae2e",
    "after_sha256": "f1d3e2efbfd02a258da6416c5de3ad4590e2130eb4db28283f97c4a05b1270c0",
    "fragments": [
        "izquierda = resultado_en_dominio(izquierda % derecha);",
        "if (derecha == 0) {",
        '    throw std::runtime_error("Divisor cero");',
        "}",
    ],
    "correct_order": [1, 2, 3, 0],
}
EXPLANATIONS = {
    "before": "La guardia completa se ejecuta antes del operador %. El diagnóstico impide llegar al cálculo cuando el divisor es cero.",
    "after": "La guardia completa se ejecuta antes del operador %. Lanzar la excepción impide llegar al cálculo cuando el divisor es cero.",
}
EXPLANATION_HASHES = {
    "before": "c3df0179bd09fbe11d54af6e11c1fb18a140dbc9ee57894070ef1bd11eb25d9b",
    "after": "0d826e2c9c883119e498ec90650b531ec9fe8a1ed3ae5417dfa653e2f3c7238d",
}
PRECEDING = {
    "identity": "block_0002_0001", "historical": (2, 1),
    "before_sha256": "d8e19c559028ddd9747b6cd42908b2425c6951b4f964fb18904329a6925ad25d",
    "after_sha256": "90e89b257a2c692159ff77a1f7d53aa627db167753bf41fb817de3272f43b527",
    "text_before_sha256": "f60a646a4abec406f3ba21db1bb72b5fcc23bcca8baa623771d4cdd713b281ce",
    "text_after_sha256": "9bec68aebc0d8da693c99f45c629310e973f2dece9055e1327099facee17d782",
    "semantics_sha256": "c9717e3861459d6cf67db2139e0991ba603c675ae35e7c80c4c6618eeb2def72",
}


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def text_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _git(*arguments: str) -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", *arguments],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)


def verify_bytes(stage: str, payload: bytes) -> bytes:
    if stage not in COMMITS:
        raise ValueError("Unknown published remainder revision")
    if hashlib.sha256(payload).hexdigest() != SOURCE_HASHES[stage]:
        raise ValueError("Published remainder fixture differs from its independently pinned Git bytes")
    return payload


def published_bytes(stage: str) -> bytes:
    if stage not in COMMITS:
        raise ValueError("Unknown published remainder revision")
    return verify_bytes(stage, _git("show", f"{COMMITS[stage]}:{RELATIVE}"))


def validate_published_history(documents: dict[str, dict]) -> None:
    """Check the focal contracts without discarding other authentic revisions."""
    if set(documents) != set(COMMITS):
        raise ValueError("Remainder QA requires all three named publication stages")
    before, published26, published30 = (documents[stage] for stage in COMMITS)
    for stage, document in documents.items():
        positions = layout.validate(document)
        meta = document["stateLayout"]
        if (document["id"] != LESSON_ID or meta["version"] != 2 or meta["locale"] != "es"
                or meta["family"]["es"]["sha256"] != ORIGINAL_SIGNATURE
                or meta["current"][PAGE] != {"id": PAGE_ID, "blocks": PAGE_BLOCKS}
                or positions.get(TARGET["historical"]) != TARGET["historical"]
                or len(meta["editorial"]) != EDITORIAL_COUNTS[stage]):
            raise ValueError("Remainder publication changed its original signature, coordinates or stage")
        blocks = layout._blocks(document)
        target = blocks[TARGET["identity"]]
        side = "before" if stage == "before" else "after"
        if (target["type"] != TARGET["type"] or target.get("id") != TARGET["activity_id"]
                or target.get("fragments") != TARGET["fragments"]
                or target.get("correctOrder") != TARGET["correct_order"]
                or target.get("explanation") != EXPLANATIONS[side]
                or text_hash(target["explanation"]) != EXPLANATION_HASHES[side]
                or layout.fingerprint(target) != TARGET[f"{side}_sha256"]
                or layout.state_fingerprint(target) != TARGET["state_sha256"]
                or meta["origin"][PAGE]["blocks"][2] != {
                    "id": TARGET["identity"], "sha256": TARGET["state_sha256"]}):
            raise ValueError("Remainder publication changed the original order activity contract")
        preceding = blocks[PRECEDING["identity"]]
        prior_side = "after" if stage == "published30" else "before"
        if (preceding["type"] != "paragraph"
                or positions.get(PRECEDING["historical"]) != PRECEDING["historical"]
                or layout.fingerprint(preceding) != PRECEDING[f"{prior_side}_sha256"]
                or text_hash(preceding["text"]) != PRECEDING[f"text_{prior_side}_sha256"]):
            raise ValueError("Remainder publication changed the preceding paragraph revision")
        page_semantics = [field for field in document.get("textSemantics", {}).get("fields", [])
                          if field["block"] in PAGE_BLOCKS]
        if (stage != "published30" and page_semantics) or (stage == "published30" and (
                len(page_semantics) != 1 or page_semantics[0]["block"] != PRECEDING["identity"]
                or page_semantics[0]["path"] != "/text"
                or page_semantics[0]["sourceSha256"] != PRECEDING["text_after_sha256"]
                or layout.fingerprint(page_semantics) != PRECEDING["semantics_sha256"])):
            raise ValueError("Remainder publication changed the focal page's authored reading semantics")

    for previous, following in ((before, published26), (published26, published30)):
        layout.validate_transition(previous, following)
        prefix = previous["stateLayout"]["editorial"]
        if following["stateLayout"]["editorial"][:len(prefix)] != prefix:
            raise ValueError("Remainder publications must preserve the complete 17-to-26-to-30 editorial prefixes")
        for key in ("version", "locale", "origin", "current", "family", "additions"):
            if previous["stateLayout"][key] != following["stateLayout"][key]:
                raise ValueError("Remainder publication changed its existing state layout")

    target_edge = {"id": TARGET["identity"], "beforeSha256": TARGET["before_sha256"],
                   "afterSha256": TARGET["after_sha256"]}
    if [edge for edge in published26["stateLayout"]["editorial"][17:]
            if edge["id"] == TARGET["identity"]] != [target_edge]:
        raise ValueError("Remainder publication lost the exact explanation revision edge")
    prior_edge = {"id": PRECEDING["identity"], "beforeSha256": PRECEDING["before_sha256"],
                  "afterSha256": PRECEDING["after_sha256"]}
    if [edge for edge in published30["stateLayout"]["editorial"][26:]
            if edge["id"] in (TARGET["identity"], PRECEDING["identity"])] != [prior_edge]:
        raise ValueError("Remainder's thirtieth publication must revise its paragraph, not its order activity")

    page26 = deepcopy(before["pages"][PAGE])
    page26["blocks"][2]["explanation"] = EXPLANATIONS["after"]
    if page26 != published26["pages"][PAGE]:
        raise ValueError("The first remainder publication changes only the focal explanation on page 3")
    page30 = deepcopy(published26["pages"][PAGE])
    page30["blocks"][1]["text"] = published30["pages"][PAGE]["blocks"][1]["text"]
    if page30 != published30["pages"][PAGE]:
        raise ValueError("The later remainder publication changes only the preceding paragraph on page 3")
    # These complete Git sources are canonical UTF-8 JSON. Pinning all their
    # bytes also preserves compiler revisions and other pages outside this
    # reader case; accepting just a locally reconstructed page is insufficient.
    for stage, document in documents.items():
        verify_bytes(stage, json_bytes(document))


def _authenticated_payloads() -> dict[str, bytes]:
    payloads = {stage: published_bytes(stage) for stage in COMMITS}
    for earlier, later in (("before", "published26"), ("published26", "published30")):
        _git("merge-base", "--is-ancestor", COMMITS[earlier], COMMITS[later])
    validate_published_history({stage: json.loads(payload) for stage, payload in payloads.items()})
    return payloads


def published_documents() -> dict[str, dict]:
    return {stage: json.loads(payload) for stage, payload in _authenticated_payloads().items()}


def export(output: Path) -> Path:
    """Authenticate all inputs and destinations before creating isolated files."""
    output = Path(os.path.abspath(output))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError("Remainder QA output cannot follow links or reparse points")
    output = output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "out") and not output.is_relative_to(ROOT / "build"):
        raise ValueError("Remainder QA output cannot replace source files")
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if (parent / ".git").exists():
            raise ValueError("Remainder QA output cannot write another checkout")

    sources = _authenticated_payloads()
    payloads = {f"{stage}.json": payload for stage, payload in sources.items()}
    manifest = {
        "kind": "published-remainder-editorial-history", "coordinateBase": 0,
        "relative": RELATIVE, "lessonId": LESSON_ID, "originalSignature": ORIGINAL_SIGNATURE,
        "page": {"id": PAGE_ID, "index": PAGE, "blocks": PAGE_BLOCKS},
        "publications": {stage: {"file": f"{stage}.json", "commit": COMMITS[stage],
                                  "sha256": SOURCE_HASHES[stage], "editorialCount": EDITORIAL_COUNTS[stage]}
                         for stage in COMMITS},
        "target": {"stableId": TARGET["identity"], "historical": list(TARGET["historical"]),
                   "type": TARGET["type"], "activityId": TARGET["activity_id"],
                   "stateSha256": TARGET["state_sha256"], "beforeSha256": TARGET["before_sha256"],
                   "afterSha256": TARGET["after_sha256"], "field": "/explanation",
                   "textSha256": EXPLANATION_HASHES, "correctOrder": TARGET["correct_order"]},
        "preceding": {"stableId": PRECEDING["identity"], "historical": list(PRECEDING["historical"]),
                      "field": "/text", "beforeSha256": PRECEDING["before_sha256"],
                      "afterSha256": PRECEDING["after_sha256"], "changedAt": "published30",
                      "textSha256": {side: PRECEDING[f"text_{side}_sha256"] for side in ("before", "after")},
                      "semanticsSha256": PRECEDING["semantics_sha256"]},
    }
    payloads["manifest.json"] = json_bytes(manifest)
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            if (not stat.S_ISREG(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400
                    or target.read_bytes() != payload):
                raise FileExistsError("Refusing to replace an existing remainder QA document")
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
    print("Verified complete published U11L16 sources with 17-to-26-to-30 editorial edges.")

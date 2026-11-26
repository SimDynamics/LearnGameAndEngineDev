"""Export authored lesson revisions for the real native reader lifecycle QA.

All sources are synthetic. Stable-state authoring APIs authenticate additions,
reordering and prose changes, while the fixture exporter writes only build/QA
destinations and refuses to replace different existing files.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess

import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches


LESSON_ID = "cpp_fundamentals__qa__reader_revision_order"
PAGE_ZERO = "page_0000"
PAGE_ONE = "page_0001"
OWNERS = {
    "intro": "block_0000_0000",
    "a": "block_0000_0001",
    "between": "block_0000_0002",
    "b": "block_0000_0003",
    "tail": "block_0000_0004",
    "other": "block_0001_0000",
    "x": "added_x",
    "new": "added_prose",
    "y": "added_y",
}
STORAGE = {
    "intro": (0, 0), "a": (0, 1), "between": (0, 2),
    "b": (0, 3), "tail": (0, 4), "other": (1, 0),
    "x": (2, 0), "new": (2, 1), "y": (2, 2),
}
EXPECTED_ORDER = {
    "baseline": (("intro", "a", "between", "b", "tail"), ("other",)),
    # Deliberately opposite to the new owners' storage order. Old A feedback
    # has already been heard; the persisted bookmark is at P_BETWEEN.
    "insert": (("intro", "a", "new", "x", "between", "b", "tail"), ("other",)),
    "retire-insert": (("a", "new", "x", "between", "b", "tail"), ("other",)),
    "reorder": (("intro", "b", "between", "a", "tail"), ("other",)),
    "prose": (("intro", "a", "between", "b", "tail"), ("other",)),
    "other-page": (("intro", "a", "between", "b", "tail"), ("other", "y")),
}


def paragraph(text: str) -> dict:
    return {"type": "paragraph", "text": text}


def exercise(label: str) -> dict:
    return {"type": "fill_code", "question": label + "_QUESTION",
            "code": "___", "answers": ["ok"],
            "explanation": label + "_FEEDBACK"}


def expected_block(role: str, variant: str) -> dict:
    if role in {"a", "b", "x", "y"}:
        return exercise(role.upper())
    text = {"intro": "P_INTRO", "between": "P_BETWEEN", "tail": "P_TAIL",
            "other": "P_OTHER", "new": "P_NEW"}[role]
    if variant == "prose" and role == "between":
        text = "P_BETWEEN_REVISED"
    return paragraph(text)


def plan(variant: str) -> list[dict]:
    return [{"id": page_id, "blocks": [OWNERS[role] for role in roles]}
            for page_id, roles in zip((PAGE_ZERO, PAGE_ONE), EXPECTED_ORDER[variant])]


def addition(role: str, page_id: str) -> dict:
    page, block = STORAGE[role]
    return {"id": OWNERS[role], "storage": {"page": page, "block": block},
            "progressPage": page_id, "block": expected_block(role, "baseline")}


def revisions() -> dict[str, dict]:
    source = {
        "id": LESSON_ID,
        "title": "READER_REVISION_TITLE",
        "pages": [{"blocks": [expected_block(role, "baseline") for role in roles]}
                  for roles in EXPECTED_ORDER["baseline"]],
    }
    baseline = state.prepare_v2(source, "es", {"es": source})
    inserted = state.reorder(baseline, plan("insert"),
                             [addition("x", PAGE_ZERO), addition("new", PAGE_ZERO)])
    retired = state.retire(baseline, [OWNERS["intro"]])
    retired_inserted = state.reorder(retired, plan("retire-insert"),
                                     [addition("x", PAGE_ZERO), addition("new", PAGE_ZERO)])
    reordered = state.reorder(baseline, plan("reorder"))
    prose_source = deepcopy(baseline)
    prose_source["pages"][0]["blocks"][2]["text"] = "P_BETWEEN_REVISED"
    prose = state.prepare_v2(prose_source, "es", {"es": baseline}, baseline)
    other_page = state.reorder(baseline, plan("other-page"), [addition("y", PAGE_ONE)])
    documents = {"baseline": baseline, "insert": inserted, "retire-insert": retired_inserted,
                 "reorder": reordered, "prose": prose, "other-page": other_page}
    verify_revisions(documents)
    return documents


def order_sha256(record: dict) -> str:
    # Same per-page input as LessonView::page_state. A prose change preserves
    # the order identity; a change on page two must not invalidate page one.
    order = record["id"] + "".join("," + owner for owner in record["blocks"])
    return hashlib.sha256(order.encode("utf-8")).hexdigest()


def verify_revisions(documents: dict[str, dict]) -> None:
    baseline = documents["baseline"]
    for variant, document in documents.items():
        storage_map = state.validate(document)
        state.validate_transition(baseline, document)
        metadata = document["stateLayout"]
        if metadata["current"] != plan(variant):
            raise ValueError(f"The {variant} fixture lost its independent visual-order oracle")
        for field in ("origin", "family", "locale", "version"):
            if metadata[field] != baseline["stateLayout"][field]:
                raise ValueError(f"The {variant} fixture changed its immutable {field}")
        for page_index, roles in enumerate(EXPECTED_ORDER[variant]):
            blocks = document["pages"][page_index]["blocks"]
            if blocks != [expected_block(role, variant) for role in roles]:
                raise ValueError(f"Unexpected authored content in {variant}, page {page_index}")
            for block_index, role in enumerate(roles):
                if storage_map[page_index, block_index] != STORAGE[role]:
                    raise ValueError(f"The {variant} fixture changed the storage of {role}")
    original_order = [order_sha256(record) for record in baseline["stateLayout"]["current"]]
    expected_changes = {"baseline": (False, False), "insert": (True, False),
                        "retire-insert": (True, False),
                        "reorder": (True, False), "prose": (False, False),
                        "other-page": (False, True)}
    for variant, document in documents.items():
        changed = tuple(order_sha256(record) != original_order[index]
                        for index, record in enumerate(document["stateLayout"]["current"]))
        if changed != expected_changes[variant]:
            raise ValueError(f"Unexpected page identity change for {variant}")
    prose_proofs = documents["prose"]["stateLayout"]["editorial"]
    if len(prose_proofs) != 1 or prose_proofs[0]["id"] != OWNERS["between"]:
        raise ValueError("The prose fixture requires one authenticated editorial revision")
    snapshots = documents["retire-insert"]["stateLayout"].get("retirementHistory", [])
    if (len(snapshots) != 1 or snapshots[0]["pages"] != baseline["pages"]
            or snapshots[0]["current"] != baseline["stateLayout"]["current"]):
        raise ValueError("The retirement fixture must preserve the exact baseline reading archive")


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def fixture_record(variant: str, document: dict, payload: bytes) -> dict:
    pages = []
    for page_index, (record, roles) in enumerate(zip(
            document["stateLayout"]["current"], EXPECTED_ORDER[variant])):
        expected = []
        for role in roles:
            authored = expected_block(role, variant)
            page, block = STORAGE[role]
            item = {"role": role, "owner": OWNERS[role], "type": authored["type"],
                    "storage": {"page": page, "block": block}}
            if authored["type"] == "paragraph":
                item["text"] = authored["text"]
            else:
                item.update(question=authored["question"], feedback=authored["explanation"],
                            answer="ok")
            expected.append(item)
        pages.append({"index": page_index, "id": record["id"],
                      "order": record["blocks"], "orderSha256": order_sha256(record),
                      "expected": expected})
    return {"name": variant, "file": variant + ".json",
            "sha256": hashlib.sha256(payload).hexdigest(), "pages": pages}


def recovery_publications() -> dict[str, bytes]:
    root = Path(__file__).resolve().parents[2]
    relative = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
                "proyecto_assetcheck/lesson_disenar_probar_y_presentar_variante_propia.json")
    result = {}
    for name, commit, blob in (
        ("before", "70e71e5e1876f3245633d92c414dda7f358a75ba",
         "2b58163adeade4bb27da4ca45f9407397aa72e02"),
        ("after", "439471bf832decef9caaad7aedd5becfcdaf0bbc",
         "7019f0a1bfa4de85ef8589e6eba1580d55e0c012"),
    ):
        payload = subprocess.check_output(["git", "show", f"{commit}:{relative}"], cwd=root)
        frame = b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload
        if hashlib.sha1(frame).hexdigest() != blob:
            raise ValueError("The AssetCheck recovery publication differs from its Git pin")
        result["assetcheck-recovery-" + name + ".json"] = payload
    before, after = result.values()
    if before == after or json.loads(before) != json.loads(after):
        raise ValueError("Expected the actual whitespace-only repair of AssetCheck")
    envelope = json.dumps({"lessonJson": before.decode("utf-8")}, ensure_ascii=False)
    if len(json.loads(before)["pages"]) != 18 or len(envelope.encode("utf-16-le")) // 2 <= 2_000_000:
        raise ValueError("The original AssetCheck transport regression is not reproduced")
    return result


def export(output_dir: Path, include_recovery_asset: bool = False) -> Path:
    output = checked_output(output_dir)
    documents = revisions()
    payloads = {variant + ".json": json_bytes(document)
                for variant, document in documents.items()}
    if include_recovery_asset:
        payloads.update(recovery_publications())
    payloads["manifest.json"] = json_bytes({
        "version": 1, "lessonId": LESSON_ID, "locale": "es", "baseline": "baseline.json",
        "owners": OWNERS,
        "resumeScenario": {"completed": [OWNERS["a"]], "heardFeedback": ["A_FEEDBACK"],
                           "bookmarkText": "P_BETWEEN", "newExercise": OWNERS["x"],
                           "newProse": "P_NEW", "pendingExercise": OWNERS["b"]},
        "fixtures": [fixture_record(variant, document, payloads[variant + ".json"])
                     for variant, document in documents.items()],
    })
    # Check every destination before creating any file. Existing differing QA
    # outputs are never truncated, rewritten or silently accepted.
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    for name, payload in payloads.items():
        target = output / name
        if existing_matches(target, payload):
            continue
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
    parser.add_argument("--include-recovery-asset", action="store_true")
    arguments = parser.parse_args()
    try:
        manifest = export(arguments.output_dir, arguments.include_recovery_asset)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Reader revision fixtures not exported: {error}\n")
    print(manifest)

"""Isolated first-hint QA from the authentic pre-edit U3L5 Git object."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
COMMIT = "c08196c6203986d6509f0e7256dc3e46ede630d4"
BLOB = "369ce68def029a35ae27b4d7bc8d28b1101e542d"
ASSET = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
         "edition_es_1__expresiones_y_decisiones/lesson_combinar_condiciones.json")


COURSE = str(Path(ASSET).parent.parent / "course_cpp_fundamentals.json").replace("\\", "/")
UNIT = str(Path(ASSET).parent / "unit_edition_es_1__expresiones_y_decisiones.json").replace("\\", "/")
CONTENT_PREFIX = "app/src/main/assets/content/"
INDEX_BLOBS = {
    COURSE: "a26324ca155efcbece1cf731348b0ff7b2367cab",
    UNIT: "bf7eb7157995ffe259005340735c89e5db29c357",
}


def pinned_bytes(path, blob):
    raw = subprocess.check_output(["git", "show", f"{COMMIT}:{path}"], cwd=ROOT)
    if hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw).hexdigest() != blob:
        raise ValueError(f"U3L5 original differs from the pinned Git blob: {path}")
    return raw


def build():
    raw = pinned_bytes(ASSET, BLOB)
    before = json.loads(raw)
    layout.validate(before)
    draft = deepcopy(before)
    owners = []
    for identity, block in layout._blocks(draft).items():
        if block.get("type") not in layout.ORDINARY_EDITORIAL_TYPES:
            continue
        if "reviewFeedback" in block:
            raise ValueError("Expected the authentic predecessor without guidance")
        owners.append(identity)
        block["reviewFeedback"] = (
            [f"Pista QA inicial {i}." for i in range(len(block["options"]))]
            if block["type"] == "multiple_choice" else "Pista QA inicial.")
    if len(owners) != 11 or len(before["pages"]) != 6:
        raise ValueError("U3L5 predecessor inventory changed")
    middle = layout.prepare_v2(draft, "es", {"es": before}, before)
    final = middle
    for identity in owners:
        block = layout._blocks(final)[identity]
        question = block["question"] + " Comprueba esta revisión QA."
        marks = next((field["spans"] for field in final.get("textSemantics", {}).get("fields", [])
                      if field["block"] == identity and field["path"] == "/question"), [])
        def spec(text):
            return {"text": text, "spans": [layout.text_semantics.select(text, "QA", "data")]}
        review = ({"text": "Pista QA vigente.", "spans": []}
                  if type(block["reviewFeedback"]) is str else
                  {str(i): spec(f"Pista QA vigente {i}.") for i in range(len(block["options"]))})
        final = layout.revise_ordinary_editorial(final, identity, {
            "question": {"text": question, "spans": deepcopy(marks)}, "reviewFeedback": review})
    layout.validate_publication_transition(before, final)
    layout.validate_transition(before, middle)
    layout.validate_publication_transition(middle, final)
    return before, middle, final, owners


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    before, middle, final, _ = build()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    wrong_index = deepcopy(final)
    edge = next(proof for proof in wrong_index["stateLayout"]["editorial"]
                if proof.get("reviewRevision") and len(proof["reviewPaths"]) > 1)
    edge["reviewPaths"] = [edge["reviewPaths"][1]]
    wrong_source = deepcopy(final)
    edge = next(proof for proof in wrong_source["stateLayout"]["editorial"]
                if proof.get("reviewRevision"))
    edge["reviewRevision"]["previousBlock"]["question"] += " QA falsificado."
    indexes = {path: pinned_bytes(path, blob) for path, blob in INDEX_BLOBS.items()}
    for name, source in (("before", before), ("middle", middle), ("after", final),
                         ("wrong-index", wrong_index), ("wrong-source", wrong_source)):
        # Each QA edition has its own physical curriculum route. A loose
        # JSON filename would fail the course-edition path guard before the
        # native loader ever checked the actual archived guidance.
        root = args.output_dir / name
        for path, raw in indexes.items():
            destination = root / path.removeprefix(CONTENT_PREFIX)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(raw)
        destination = root / ASSET.removeprefix(CONTENT_PREFIX)
        if name == "before":
            destination.write_bytes(pinned_bytes(ASSET, BLOB))
        else:
            destination.write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n",
                                   encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

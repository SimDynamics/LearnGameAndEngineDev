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


def build():
    raw = subprocess.check_output(["git", "show", f"{COMMIT}:{ASSET}"], cwd=ROOT)
    if hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw).hexdigest() != BLOB:
        raise ValueError("U3L5 original differs from the pinned Git blob")
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
    for name, source in (("before.json", before), ("middle.json", middle), ("after.json", final)):
        (args.output_dir / name).write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

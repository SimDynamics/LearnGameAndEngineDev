"""Isolated QA for three option edits and a later correction of the same U3L4 owner."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract
from first_hint_revision_fixture import CONTENT_PREFIX, INDEX_BLOBS

ROOT = Path(__file__).resolve().parents[2]
COMMIT = "fb73e5b2a114074568611c546f5747ac10620767"
ASSET = (CONTENT_PREFIX + "es/programming/cpp_fundamentals/"
         "edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json")
BLOB = "9a31ce0a890a33d683c171fa4c4da83ee7eeac62"
OWNER = "block_0004_0002"
# Independent QA labels follow the exact three predictions in the authored
# provisional question. They are NOT the unpublished text in Work's local file.
ORIGINAL = ["Cantidad 5, que debe producir el rechazo.",
            "Cantidad 4, que debe producir Permitido.",
            "Cantidad 3, que debe producir Permitido."]
OPTIONS = ["Pedido de 5 elementos. A: Excede el limite. B: Permitido.",
           "Pedido de 4 elementos. A: Permitido. B: Excede el limite.",
           "Pedido de 3 elementos. A: Permitido. B: Excede el limite."]


def pinned_bytes(path, digest):
    raw = subprocess.check_output(["git", "show", f"{COMMIT}:{path}"], cwd=ROOT)
    actual = hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw).hexdigest()
    if actual != digest:
        raise ValueError("U3L4 fixture differs from its pinned Git object: " + path)
    return raw


def build():
    before = json.loads(pinned_bytes(ASSET, BLOB))
    mapping = layout.validate(before)
    original = layout._blocks(before)[OWNER]
    if original["correctIndex"] != 1 or original["options"] != ORIGINAL:
        raise ValueError("The authentic U3L4 prediction inventory changed")
    if layout._general_choice_records(before, OWNER):
        raise ValueError("The isolated QA source already has a revised option owner")
    stages = {"before": before}
    current = before
    for index, replacement in enumerate(OPTIONS):
        current = layout.revise_choice_editorial(current, OWNER, index, replacement,
            spans=[layout.text_semantics.select(replacement, str(5 - index), "data")])
        if index == 0:
            stages["first"] = current
    stages["three"] = current
    replacement = OPTIONS[0] + " Predicción QA posterior."
    stages["later"] = layout.revise_choice_editorial(current, OWNER, 0, replacement,
        spans=[layout.text_semantics.select(replacement, "5", "data")])
    for old, new in (("before", "first"), ("first", "three"),
                     ("three", "later"), ("before", "later"), ("first", "later")):
        layout.validate_publication_transition(stages[old], stages[new])
        if layout.validate(stages[new]) != mapping:
            raise ValueError("The three option labels changed the original state addresses")
        old_block, new_block = (layout._blocks(stages[name])[OWNER] for name in (old, new))
        position = next((p, b) for p, page in enumerate(before["stateLayout"]["current"])
                        for b, owner in enumerate(page["blocks"]) if owner == OWNER)
        pn, bn = mapping[position]
        if not contract.preserves_previous_exercise(
                ASSET, pn, bn, old_block, new_block,
                previous_document=stages[old], current_document=stages[new]):
            raise ValueError("The curricular comparison rejected a fully authenticated option chain")
    return stages


class MultiChoicePublishedFixtureTest(unittest.TestCase):
    def test_real_u3l4_three_options_and_later_same_index(self):
        stages = build()
        first, last = stages["first"], stages["later"]
        records = layout._general_choice_records(last, OWNER)
        self.assertEqual([item["optionIndex"] for item in records], [0, 1, 2, 0])
        self.assertEqual(records[0], layout._general_choice_records(first, OWNER)[0])
        self.assertEqual(layout._blocks(last)[OWNER]["correctIndex"], 1)
        self.assertEqual(layout.original_projection(last), layout.original_projection(stages["before"]))
        for old in ("before", "first", "three"):
            aligned = layout.align_for_previous_validation(stages[old], last)
            self.assertEqual(layout._blocks(aligned)[OWNER]["options"],
                             layout._blocks(stages[old])[OWNER]["options"])
            layout.text_semantics.validate(aligned)


def export(directory):
    stages = build()
    indexes = {path: pinned_bytes(path, digest) for path, digest in INDEX_BLOBS.items()}
    for name, source in stages.items():
        root = directory / name
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    export(parser.parse_args().output_dir)

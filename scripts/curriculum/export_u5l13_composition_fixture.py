"""Export both signed U5L13 editorial orders as disposable Windows QA JSON.

The files are test projections, not curricular publications. No Work asset,
student profile, draft, saved WAV, or receipt is modified.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import identifier_rename_archive_preview as rename_preview
import math_matching_text_compat as matching
import stable_state_layout as state
from test_identifier_rename_archive_preview import FormattedOwnerRenamePreview, OWNERS
from test_math_matching_text_compat import RealFrequencyMatchingEditorial, OWNER, PATH

CONTENT = (Path(__file__).resolve().parents[2] /
           "app/src/main/assets/content").resolve()


def export(directory: Path) -> dict[str, Path]:
    directory = directory.resolve()
    if directory == CONTENT or CONTENT in directory.parents:
        raise ValueError("A QA fixture cannot overwrite curricular assets")
    if directory.exists() and any(directory.iterdir()):
        raise ValueError("The isolated fixture directory must initially be empty")

    RealFrequencyMatchingEditorial.setUpClass()
    author = RealFrequencyMatchingEditorial()
    published = author.lesson
    plans = {}
    for identity, destination in OWNERS:
        current = state._blocks(published)[identity]
        slot, _, declarations, counts = FormattedOwnerRenamePreview.candidate(
            current, destination)
        if not all(n == 5 for n in counts[slot].values()):
            raise AssertionError("The authentic frequency source changed")
        plans[identity] = declarations

    changes, spans = author.revisions()
    named_first = rename_preview.prepare_declared_identifier_rename_draft(
        published, plans)
    names_then_cards, _ = matching.preview_math_matching_text_review(
        named_first, OWNER, changes, spans)
    cards_first, _ = matching.preview_math_matching_text_review(
        published, OWNER, changes, spans)
    cards_then_names = rename_preview.prepare_declared_identifier_rename_draft(
        cards_first, plans)

    for before, after in (
        (published, named_first),
        (named_first, names_then_cards),
        (published, cards_first),
        (cards_first, cards_then_names),
        (published, names_then_cards),
        (published, cards_then_names),
    ):
        state.validate(before)
        state.validate(after)
        state.validate_transition(before, after)
    if (names_then_cards["pages"] != cards_then_names["pages"]
            or names_then_cards.get("textSemantics")
            != cards_then_names.get("textSemantics")
            or state.original_projection(names_then_cards)
            != state.original_projection(published)
            or state.original_projection(cards_then_names)
            != state.original_projection(published)):
        raise AssertionError("Two publication orders changed historical grading")
    if PATH.read_bytes() != author.original_bytes:
        raise AssertionError("The original Work asset was modified")

    documents = {
        "baseline.json": published,
        "renamed-first.json": named_first,
        "matching-first.json": cards_first,
        "names-then-cards.json": names_then_cards,
        "cards-then-names.json": cards_then_names,
    }
    directory.mkdir(parents=True, exist_ok=True)
    output = {}
    for name, document in documents.items():
        path = directory / name
        path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
        output[name] = path
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = export(args.output_dir)
    print(f"PASS U5L13 two verified orders: {len(output)} isolated QA documents")


if __name__ == "__main__":
    main()

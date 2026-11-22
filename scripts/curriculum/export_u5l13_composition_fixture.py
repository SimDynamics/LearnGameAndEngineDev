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

P2_ASSETCHECK = (CONTENT /
    "es/programming/programming_practice_tools/proyecto_assetcheck/"
    "lesson_leer_manifiesto_y_publicar_informe.json")
P2_OWNER = "block_0002_0005"
P2_OLD_WORDING = "Las barras inclinadas representan caracteres de escape, no texto literal del manifiesto."
P2_NEW_WORDING = ("En las etiquetas, las secuencias con barra inversa "
    "representan LF o CR; no son una barra y una letra escritas en el manifiesto.")


def assetcheck_prompt_editions() -> tuple[dict, dict]:
    """Authenticate a real P2 matching prompt without changing the curriculum."""
    original_bytes = P2_ASSETCHECK.read_bytes()
    published = json.loads(original_bytes)
    state.validate(published)
    block = state._blocks(published)[P2_OWNER]
    if (block["type"] != "math_puzzle" or
            block["responses"][0]["type"] != "math_matching_drag" or
            block["prompt"].count(P2_OLD_WORDING) != 1):
        raise AssertionError("The authentic P2 matching prompt changed")
    replacement = block["prompt"].replace(P2_OLD_WORDING, P2_NEW_WORDING)
    candidate, proof = matching.preview_math_matching_text_review(
        published, P2_OWNER, {"/prompt": replacement}, {"/prompt": []})
    if not matching.verify_matching_preview(published, candidate, proof):
        raise AssertionError("The P2 mathematical prompt lost its authored proof")
    state.validate(candidate)
    state.validate_transition(published, candidate)
    aligned = state.align_for_previous_validation(published, candidate)
    if (state._blocks(aligned) != state._blocks(published) or
            state.original_projection(candidate) != state.original_projection(published) or
            P2_ASSETCHECK.read_bytes() != original_bytes):
        raise AssertionError("The P2 prompt revision changed the historical answer")
    return published, candidate



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
    p2_original, p2_revised = assetcheck_prompt_editions()
    documents["assetcheck-p2-original.json"] = p2_original
    documents["assetcheck-p2-revised.json"] = p2_revised
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
    print(f"PASS U5L13 two verified orders and P2 prompt: {len(output)} isolated QA documents")


if __name__ == "__main__":
    main()

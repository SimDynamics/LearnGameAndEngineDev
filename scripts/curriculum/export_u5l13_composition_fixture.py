"""Export both signed U5L13 editorial orders as disposable Windows QA JSON.

The files are test projections, not curricular publications. No Work asset,
student profile, draft, saved WAV, or receipt is modified.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

import identifier_rename_archive_preview as rename_preview
import math_matching_text_compat as matching
import stable_state_layout as state
from test_identifier_rename_archive_preview import FormattedOwnerRenamePreview, OWNERS
from test_math_matching_text_compat import RealFrequencyMatchingEditorial, OWNER, PATH

ROOT = Path(__file__).resolve().parents[2]
CONTENT = (ROOT / "app/src/main/assets/content").resolve()

P2_ASSETCHECK = (CONTENT /
    "es/programming/programming_practice_tools/proyecto_assetcheck/"
    "lesson_leer_manifiesto_y_publicar_informe.json")
P2_OWNER = "block_0002_0005"
P2_OLD_WORDING = "Las barras inclinadas representan caracteres de escape, no texto literal del manifiesto."
P2_NEW_WORDING = ("En las etiquetas, las secuencias con barra inversa "
    "representan LF o CR; no son una barra y una letra escritas en el manifiesto.")


# Work's genuine one-field publication edge. The two complete Git blobs
# are independently pinned, not reconstructed from the current journal.
P2_LEGACY_PUBLICATIONS = (
    ("3deb1e829593d5e0afc6bf720a74321bd8bb96c6",
     "ae92d8459db7503e5684ae5d905d188f19a10f2d"),
    ("08887bd1495ac745ec77b361b9dd9ac4670c23c5",
     "1670444d9d84075cfc9408796eabab6a10865a48"),
)


def assetcheck_legacy_publications() -> tuple[dict, dict]:
    relative = P2_ASSETCHECK.relative_to(ROOT).as_posix()
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    documents = []
    for commit, expected in P2_LEGACY_PUBLICATIONS:
        raw = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
             "show", commit + ":" + relative, "--"],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
        framed = b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw
        if hashlib.sha1(framed).hexdigest() != expected:
            raise ValueError("The authentic AssetCheck prompt publication differs from its Git pin")
        documents.append(state.parse_json(raw.decode("utf-8-sig")))
    before, after = documents
    state.validate_transition(before, after)
    old, current = state._blocks(before), state._blocks(after)
    archived = [item for item in after["stateLayout"].get("mathTextRevisions", [])
                if item["id"] == P2_OWNER and item["field"] == "/prompt"]
    if (len(archived) != 1 or
            state.parse_json(archived[0]["rawJson"]) != old[P2_OWNER] or
            current[P2_OWNER]["responses"] != old[P2_OWNER]["responses"] or
            current[P2_OWNER]["prompt"] == old[P2_OWNER]["prompt"] or
            {k: v for k, v in old.items() if k != P2_OWNER} !=
            {k: v for k, v in current.items() if k != P2_OWNER} or
            state.validate(before) != state.validate(after)):
        raise AssertionError("The genuine AssetCheck prompt-only transition changed grading or storage")
    return before, after


def assetcheck_prompt_editions() -> tuple[dict | None, dict]:
    """Authenticate a real P2 matching prompt without changing the curriculum."""
    original_bytes = P2_ASSETCHECK.read_bytes()
    published = json.loads(original_bytes)
    state.validate(published)
    block = state._blocks(published)[P2_OWNER]
    if (block["type"] != "math_puzzle" or
            block["responses"][0]["type"] != "math_matching_drag"):
        raise AssertionError("The authentic P2 matching owner changed")
    legacy = [item for item in published["stateLayout"].get("mathTextRevisions", [])
              if item.get("id") == P2_OWNER and item.get("field") == "/prompt"]
    if legacy:
        # The active lesson uses the general math text journal. Its actual
        # wording is authored by Work; prove the old evaluator, not a literal
        # proposed revision from the later matching-specific helper.
        if len(legacy) != 1:
            raise AssertionError("The published P2 prompt has duplicate archives")
        previous = state.parse_json(legacy[0]["rawJson"])
        if (previous["prompt"].count(P2_OLD_WORDING) != 1 or
                previous["prompt"] == block["prompt"] or
                previous["responses"] != block["responses"] or
                block["responses"][0]["validator"]["expected"] !=
                {"a": "cero", "b": "uno", "c": "dos_vacia", "d": "dos"}):
            raise AssertionError("The published P2 response or original prompt changed")
        if P2_ASSETCHECK.read_bytes() != original_bytes:
            raise AssertionError("The active P2 source was mutated")
        return None, published
    if block["prompt"].count(P2_NEW_WORDING) == 1:
        # Work has integrated the reviewed correction. Keep a native loader
        # fixture for its REAL signed publication; never forge a second
        # editorial transition or demand that the old wording remain active.
        records = [entry for entry in published["stateLayout"].get(
            "mathMatchingTextRevisions", []) if entry.get("id") == P2_OWNER]
        if len(records) != 1:
            raise AssertionError("The published P2 prompt lacks signed history")
        previous = state.parse_json(records[0]["rawJson"])
        if (previous["prompt"].count(P2_OLD_WORDING) != 1
                or previous["responses"] != block["responses"]
                or records[0]["paths"] != ["/prompt"]):
            raise AssertionError("The published P2 response or archive changed")
        if P2_ASSETCHECK.read_bytes() != original_bytes:
            raise AssertionError("The active P2 source was mutated")
        return None, published
    if block["prompt"].count(P2_OLD_WORDING) != 1:
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
    if p2_original is None:
        documents["assetcheck-p2-published.json"] = p2_revised
        if any(item["id"] == P2_OWNER and item["field"] == "/prompt"
               for item in p2_revised["stateLayout"].get("mathTextRevisions", [])):
            # Export the actual adjacent pre-adoption/review publications as
            # well as the independently validated, currently active lesson.
            before, after = assetcheck_legacy_publications()
            documents["assetcheck-p2-original.json"] = before
            documents["assetcheck-p2-revised.json"] = after
    else:
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
    print(f"PASS U5L13 and P2 matching prompt: {len(output)} isolated QA documents")


if __name__ == "__main__":
    main()

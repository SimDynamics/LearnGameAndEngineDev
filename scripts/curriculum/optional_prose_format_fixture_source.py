"""Isolated Windows QA for an added explanation and compatible C++ spacing.

Never edit the shipped curriculum, personal progress, or a published Git SHA.
All JSON examples are generated into the caller's build/test directory.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path

import stable_state_layout as layout


OWNER = "block_0000_0000"
CODE = 'std::cout << "A";'
EDITED = 'std::cout  << "A";'
EXPLANATION = "La salida A procede del literal; los espacios no cambian la evaluación."
DRAG_OWNER = "block_0000_0001"
DRAG_TEMPLATE = '#include <iostream>\nint []() {\n    std::cout [] "Revision lista"[]\n    return 0;\n}'
DRAG_EXPLANATION = "La ficha main identifica la función, << inserta el texto y ; termina la instrucción."


def fixtures() -> dict[str, dict]:
    raw = {
        "id": "cpp_fundamentals__qa__optional_prose_format",
        "title": "Formato y explicación opcional de QA",
        "pages": [{"blocks": [
            {"type": "output_prediction", "id": "qa_primer_resultado",
             "question": "¿Qué escribe el programa?", "language": "cpp",
             "code": CODE, "answers": ["A"]},
            {"type": "drag_fill_code", "id": "qa_piezas_estructura",
             "question": "Completa las tres posiciones con las fichas correctas.",
             "template": DRAG_TEMPLATE, "choices": ["main", "<<", ";", "Main", "<"],
             "correctOrder": [0, 1, 2]},
        ]}],
    }
    before = layout.prepare_v2(raw, "es", {"es": raw})
    layout.validate(before)
    proposed = deepcopy(before)
    owner = layout._blocks(proposed)[OWNER]
    assert owner["type"] == "output_prediction" and "explanation" not in owner
    owner["code"] = EDITED
    owner["explanation"] = EXPLANATION
    drag = layout._blocks(proposed)[DRAG_OWNER]
    assert drag["type"] == "drag_fill_code" and "explanation" not in drag
    drag["explanation"] = DRAG_EXPLANATION
    proposed["textSemantics"] = {
        "version": 1,
        "fields": [
            layout.text_semantics.make_field(proposed, OWNER, "/explanation", []),
            layout.text_semantics.make_field(proposed, DRAG_OWNER, "/explanation", []),
        ],
    }
    after = layout.prepare_v2(proposed, "es", {"es": before}, before)
    layout.validate_transition(before, after)
    assert len(after["stateLayout"]["sourceFormatRevisions"]) == 1
    assert len(after["stateLayout"]["editorial"]) == 2
    assert layout._blocks(after)[DRAG_OWNER]["correctOrder"] == [0, 1, 2]
    assert layout._blocks(after)[DRAG_OWNER]["choices"] == ["main", "<<", ";", "Main", "<"]
    assert after["stateLayout"]["sourceFormatRevisions"][0]["block"] == (
        layout._blocks(before)[OWNER])
    assert layout.state_fingerprint(
        layout.code_format.source_projection(layout._blocks(after)[OWNER],
                                             layout._blocks(before)[OWNER])
    ) == layout.state_fingerprint(layout._blocks(before)[OWNER])

    def repin_modified_answer(candidate: dict, identity: str) -> None:
        # Simulate a forged current hash, not an authenticated editorial edge.
        # The native loaders must still reject the changed evaluated contract.
        for edge in candidate["stateLayout"]["editorial"]:
            if edge["id"] == identity:
                edge["afterSha256"] = layout.fingerprint(layout._blocks(candidate)[identity])
                return
        raise AssertionError("Missing fixture editorial owner: " + identity)

    missing = deepcopy(after)
    missing["textSemantics"]["fields"].clear()
    stale = deepcopy(after)
    stale["textSemantics"]["fields"][0]["sourceSha256"] = "0" * 64
    missing_drag = deepcopy(after)
    missing_drag["textSemantics"]["fields"].pop(1)
    stale_drag = deepcopy(after)
    stale_drag["textSemantics"]["fields"][1]["sourceSha256"] = "0" * 64

    changed_answer = deepcopy(after)
    changed_answer["pages"][0]["blocks"][0]["answers"] = ["B"]
    repin_modified_answer(changed_answer, OWNER)
    changed_source = deepcopy(after)
    changed_source["pages"][0]["blocks"][0]["code"] = 'std::cout << "B";'
    repin_modified_answer(changed_source, OWNER)

    changed_drag_answer = deepcopy(after)
    changed_drag_answer["pages"][0]["blocks"][1]["correctOrder"] = [1, 0, 2]
    repin_modified_answer(changed_drag_answer, DRAG_OWNER)
    changed_drag_template = deepcopy(after)
    changed_drag_template["pages"][0]["blocks"][1]["template"] = (
        DRAG_TEMPLATE.replace("std::cout", "std::cerr"))
    repin_modified_answer(changed_drag_template, DRAG_OWNER)

    # Python rejects NEL (U+0085) as whitespace. Pin each forged active
    # descriptor and editorial after-hash so the native loader cannot reject
    # for an unrelated stale digest instead of the actual blank prose.
    blank_nel_explanation = deepcopy(after)
    layout._blocks(blank_nel_explanation)[OWNER]["explanation"] = "\u0085 \u2003"
    blank_nel_explanation["textSemantics"]["fields"][0] = (
        layout.text_semantics.make_field(
            blank_nel_explanation, OWNER, "/explanation", []))
    repin_modified_answer(blank_nel_explanation, OWNER)

    blank_nel_question = deepcopy(after)
    layout._blocks(blank_nel_question)[OWNER]["question"] = "\u0085 \u2003"
    blank_nel_question["textSemantics"]["fields"].append(
        layout.text_semantics.make_field(
            blank_nel_question, OWNER, "/question", []))
    repin_modified_answer(blank_nel_question, OWNER)

    examples = {
        "optional-prose.before": before,
        "optional-prose.after": after,
        "optional-prose.missing-mark": missing,
        "optional-prose.stale-mark": stale,
        "optional-prose.changed-answer": changed_answer,
        "optional-prose.changed-code": changed_source,
        "optional-prose.missing-drag-mark": missing_drag,
        "optional-prose.stale-drag-mark": stale_drag,
        "optional-prose.changed-drag-answer": changed_drag_answer,
        "optional-prose.changed-drag-template": changed_drag_template,
        "optional-prose.blank-nel-explanation": blank_nel_explanation,
        "optional-prose.blank-nel-question": blank_nel_question,
    }
    for name, candidate in examples.items():
        if name.endswith((".before", ".after")):
            continue
        try:
            layout.validate_transition(before, candidate)
        except ValueError:
            pass
        else:
            raise AssertionError("The fixture was not rejected: " + name)
    return examples


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for label, document in fixtures().items():
        (args.output_dir / (label + ".json")).write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    print("Prepared twelve isolated optional-prose/source-format QA fixtures")


if __name__ == "__main__":
    main()

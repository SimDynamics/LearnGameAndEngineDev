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


def fixtures() -> dict[str, dict]:
    raw = {
        "id": "cpp_fundamentals__qa__optional_prose_format",
        "title": "Formato y explicación opcional de QA",
        "pages": [{"blocks": [
            {"type": "output_prediction", "id": "qa_primer_resultado",
             "question": "¿Qué escribe el programa?", "language": "cpp",
             "code": CODE, "answers": ["A"]},
        ]}],
    }
    before = layout.prepare_v2(raw, "es", {"es": raw})
    layout.validate(before)
    proposed = deepcopy(before)
    owner = layout._blocks(proposed)[OWNER]
    assert owner["type"] == "output_prediction" and "explanation" not in owner
    owner["code"] = EDITED
    owner["explanation"] = EXPLANATION
    proposed["textSemantics"] = {
        "version": 1,
        "fields": [layout.text_semantics.make_field(
            proposed, OWNER, "/explanation", [])],
    }
    after = layout.prepare_v2(proposed, "es", {"es": before}, before)
    layout.validate_transition(before, after)
    assert len(after["stateLayout"]["sourceFormatRevisions"]) == 1
    assert after["stateLayout"]["sourceFormatRevisions"][0]["block"] == (
        layout._blocks(before)[OWNER])
    assert layout.state_fingerprint(
        layout.code_format.source_projection(layout._blocks(after)[OWNER],
                                             layout._blocks(before)[OWNER])
    ) == layout.state_fingerprint(layout._blocks(before)[OWNER])

    missing = deepcopy(after)
    missing["textSemantics"]["fields"].clear()
    stale = deepcopy(after)
    stale["textSemantics"]["fields"][0]["sourceSha256"] = "0" * 64
    changed_answer = deepcopy(after)
    changed_answer["pages"][0]["blocks"][0]["answers"] = ["B"]
    changed_answer["stateLayout"]["editorial"][-1]["afterSha256"] = (
        layout.fingerprint(changed_answer["pages"][0]["blocks"][0]))
    changed_source = deepcopy(after)
    changed_source["pages"][0]["blocks"][0]["code"] = 'std::cout << "B";'
    changed_source["stateLayout"]["editorial"][-1]["afterSha256"] = (
        layout.fingerprint(changed_source["pages"][0]["blocks"][0]))
    examples = {
        "optional-prose.before": before,
        "optional-prose.after": after,
        "optional-prose.missing-mark": missing,
        "optional-prose.stale-mark": stale,
        "optional-prose.changed-answer": changed_answer,
        "optional-prose.changed-code": changed_source,
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
    print("Prepared six isolated optional-prose/source-format QA fixtures")


if __name__ == "__main__":
    main()

"""Write authenticated three-locale before/after source-format QA fixtures.

These are synthetic inputs for the *real* native Windows content loader, never
student assets, published lesson replacements or persisted student accounts.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path

import stable_state_layout as layout
from test_code_format_compat import fixture, edited


def fixtures() -> dict[str, dict]:
    original: dict[str, dict] = {}
    for locale, title, description in (
        ("es", "Formato español", "Código previo"),
        ("en", "English source formatting", "Earlier code"),
        ("fr", "Mise en forme du code", "Code antérieur"),
    ):
        document = fixture()
        document["title"] = title
        document["pages"][0]["blocks"].append({"type": "paragraph", "text": description})
        original[locale] = document

    before = {
        locale: layout.prepare_v2(document, locale, original)
        for locale, document in original.items()
    }
    outputs: dict[str, dict] = {}
    for locale, previous in before.items():
        modified = edited(previous)
        after = layout.prepare_v2(modified, locale, before, previous)
        layout.validate_transition(previous, after)
        assert layout.validate(previous) == layout.validate(after)
        assert len(after["stateLayout"]["sourceFormatRevisions"]) == 5
        outputs[f"{locale}.before"] = previous
        outputs[f"{locale}.after"] = after

    # The native loader must reject both a changed result and a forged archived
    # original, even when the new visible source remains lexically equivalent.
    invalid_answer = deepcopy(outputs["es.after"])
    invalid_answer["pages"][0]["blocks"][1]["answers"] = ["other result"]
    outputs["es.changed-answer"] = invalid_answer
    invalid_archive = deepcopy(outputs["es.after"])
    invalid_archive["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] = "int other=99;"
    outputs["es.forged-archive"] = invalid_archive
    return outputs


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    files = fixtures()
    options.output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in files.items():
        destination = options.output_dir / f"{name}.json"
        destination.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Generated {len(files)} isolated multilingual source-format fixtures")


if __name__ == "__main__":
    main()

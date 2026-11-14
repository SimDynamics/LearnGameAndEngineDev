"""Write authenticated three-locale before/after source-format QA fixtures.

These are synthetic inputs for the *real* native Windows content loader, never
student assets, published lesson replacements or persisted student accounts.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
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

def real_question_spacing_fixtures() -> dict[str, dict]:
    """Actual published U7L3 source; CI-only copies, not curricular edits."""
    root = Path(__file__).resolve().parents[2] / "app/src/main/assets/content"
    relative = (Path("programming/cpp_fundamentals/errores_y_construccion_gradual")
                / "lesson_validar_antes_y_elegir_reporte.json")
    family = {
        locale: json.loads((root / locale / relative).read_text(encoding="utf-8"))
        for locale in ("es", "en", "fr")
    }
    before = family["es"]
    layout.validate(before)
    identity = "block_0000_0003"
    assert before["stateLayout"]["current"][0]["blocks"][3] == identity
    original = before["pages"][0]["blocks"][3]
    assert original["type"] == "output_prediction"
    assert not any(entry["id"] == identity for entry in
                   before["stateLayout"].get("sourceFormatRevisions", []))

    modified = deepcopy(before)
    target = modified["pages"][0]["blocks"][3]
    source = "    int piezas{10};\n    if (piezas < 0 || piezas > 10)"
    replacement = "    int piezas{10};\n\n    if (piezas < 0 || piezas > 10)"
    assert target["code"].count(source) == 1
    target["code"] = target["code"].replace(source, replacement, 1)
    target["question"] += " Examina si el extremo superior está incluido."
    target["explanation"] += " El límite superior también forma parte del intervalo."
    for field in ("/question", "/explanation"):
        marker = next(item for item in modified["textSemantics"]["fields"]
                      if item["block"] == identity and item["path"] == field)
        marker["sourceSha256"] = hashlib.sha256(
            target[field[1:]].encode("utf-8")).hexdigest()
    after = layout.prepare_v2(modified, "es", family, before)
    layout.validate_transition(before, after)
    assert original["answers"] == target["answers"]
    assert before["stateLayout"]["origin"] == after["stateLayout"]["origin"]
    assert before["stateLayout"]["current"] == after["stateLayout"]["current"]
    assert before["stateLayout"]["family"] == after["stateLayout"]["family"]

    wrong_answer = deepcopy(after)
    wrong_answer["pages"][0]["blocks"][3]["answers"] = ["falsa"]
    stale_marker = deepcopy(after)
    old_marker = next(item for item in before["textSemantics"]["fields"]
                      if item["block"] == identity and item["path"] == "/question")
    new_marker = next(item for item in stale_marker["textSemantics"]["fields"]
                      if item["block"] == identity and item["path"] == "/question")
    new_marker["sourceSha256"] = old_marker["sourceSha256"]
    forged_source = deepcopy(after)
    forged_source["pages"][0]["blocks"][3]["code"] = (
        forged_source["pages"][0]["blocks"][3]["code"].replace(
            "piezas < 0", "piezas <= 0", 1))
    return {
        "u7-question.before": before,
        "u7-question.after": after,
        "u7-question.changed-answer": wrong_answer,
        "u7-question.stale-question": stale_marker,
        "u7-question.forged-source": forged_source,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    files = fixtures()
    files.update(real_question_spacing_fixtures())
    options.output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in files.items():
        destination = options.output_dir / f"{name}.json"
        destination.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Generated {len(files)} isolated multilingual source-format fixtures")


if __name__ == "__main__":
    main()

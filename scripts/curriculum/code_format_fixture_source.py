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

import format_batch_history as history
import stable_state_layout as layout
from test_code_format_compat import fixture, edited


ROOT = Path(__file__).resolve().parents[2]
QUESTION_RELATIVE = ("programming/cpp_fundamentals/errores_y_construccion_gradual/"
                     "lesson_validar_antes_y_elegir_reporte.json")
QUESTION_IDENTITY = "block_0000_0003"
QUESTION_PUBLICATIONS = {
    "u7-question": (
        "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
        {"es": "d65de29e472e1f1518559c7e6e785d68943bf1378ff80f0b55278786bbd9f788",
         "en": "0e0eab9f7fda4ea638596f1f81e5e523ef0fbd9fc59ef42646ed1e765168c4d0",
         "fr": "9ae89d6b6d74e9b65fc5b70efec82d3b80f9fd84bc384d07b97f222844a25288"}),
    "u7-question-history": (
        "83e2d4e6d21027c6ca072dd2e85d4700a21d611d",
        {"es": "84ae6c2c5f4bd7da82782def62d1b6adf9f6a78e4fad440c6e5a6c1206273c80",
         "en": "0e0eab9f7fda4ea638596f1f81e5e523ef0fbd9fc59ef42646ed1e765168c4d0",
         "fr": "9ae89d6b6d74e9b65fc5b70efec82d3b80f9fd84bc384d07b97f222844a25288"}),
}


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
    """Both complete U7L3 publications, before and after a real format batch.

    A later checkout may already contain this owner's canonical archive and
    unrelated archives. Never reconstruct an earlier lesson by replacing its
    visible block with that partial snapshot, or discard its existing history.
    """
    publications = {}
    for prefix, (commit, digests) in QUESTION_PUBLICATIONS.items():
        family = {}
        for locale, digest in digests.items():
            publication = history.Publication(commit, digest)
            relative = f"app/src/main/assets/content/{locale}/{QUESTION_RELATIVE}"
            raw = history.publication_bytes(ROOT, relative, publication)
            family[locale] = json.loads(history.verify_publication_bytes(raw, publication))
        publications[prefix] = family

    identity = QUESTION_IDENTITY
    earliest = publications["u7-question"]["es"]
    canonical = history.active_owners(earliest)[0][identity]
    layout.validate_transition(earliest, publications["u7-question-history"]["es"])
    outputs = {}
    for prefix, family in publications.items():
        before = family["es"]
        layout.validate(before)
        owners, _ = history.active_owners(before)
        original = owners[identity]
        assert original["type"] == "output_prediction"
        entries = before["stateLayout"].get("sourceFormatRevisions", [])
        archives = {entry["id"]: entry["block"] for entry in entries}
        assert len(archives) == len(entries)
        if prefix == "u7-question":
            assert identity not in archives
        else:
            assert archives[identity] == canonical
            assert original["code"] != canonical["code"]

        modified = deepcopy(before)
        target = history.active_owners(modified)[0][identity]
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
        assert layout.validate(before) == layout.validate(after)
        assert original["answers"] == target["answers"]
        for field in ("origin", "current", "family", "additions"):
            assert before["stateLayout"][field] == after["stateLayout"][field]
        revised = history.active_owners(after)[0]
        assert {key: value for key, value in owners.items() if key != identity} == {
            key: value for key, value in revised.items() if key != identity}
        new_entries = after["stateLayout"]["sourceFormatRevisions"]
        new_archives = {entry["id"]: entry["block"] for entry in new_entries}
        assert len(new_archives) == len(new_entries)
        assert new_entries[:len(entries)] == entries
        assert set(new_archives) == set(archives) | {identity}
        assert new_archives[identity] == canonical
        prior_edges = before["stateLayout"]["editorial"]
        new_edges = after["stateLayout"]["editorial"]
        assert new_edges[:len(prior_edges)] == prior_edges
        assert new_edges[len(prior_edges):] == [{
            "id": identity, "beforeSha256": layout.fingerprint(original),
            "afterSha256": layout.fingerprint(target)}]

        wrong_answer = deepcopy(after)
        history.active_owners(wrong_answer)[0][identity]["answers"] = ["falsa"]
        stale_marker = deepcopy(after)
        old_marker = next(item for item in before["textSemantics"]["fields"]
                          if item["block"] == identity and item["path"] == "/question")
        new_marker = next(item for item in stale_marker["textSemantics"]["fields"]
                          if item["block"] == identity and item["path"] == "/question")
        new_marker["sourceSha256"] = old_marker["sourceSha256"]
        unmarked_question = deepcopy(after)
        unmarked_question["textSemantics"]["fields"] = [
            entry for entry in unmarked_question["textSemantics"]["fields"]
            if not (entry["block"] == identity and entry["path"] == "/question")
        ]
        forged_source = deepcopy(after)
        forged_owner = history.active_owners(forged_source)[0][identity]
        assert forged_owner["code"].count("piezas < 0") == 1
        forged_owner["code"] = forged_owner["code"].replace("piezas < 0", "piezas <= 0", 1)
        outputs.update({
            prefix + ".before": before,
            prefix + ".after": after,
            prefix + ".changed-answer": wrong_answer,
            prefix + ".stale-question": stale_marker,
            prefix + ".unmarked-question": unmarked_question,
            prefix + ".forged-source": forged_source,
        })
    return outputs



def real_u12_editorial_fixtures() -> dict[str, dict]:
    """Generate CI-only U12L6 return and U12L22 question copies."""
    from test_published_u12_question_editorials import CASES, proposal

    exports = {}
    for kind in ("formatting", "group"):
        before, after, family = proposal(kind)
        identity = CASES[kind]["identity"]
        assert family["es"] == before
        assert layout.validate(before) == layout.validate(after)
        layout.validate_transition(before, after)
        prefix = "u12-" + kind
        exports[prefix + ".before"] = before
        exports[prefix + ".after"] = after

        altered = deepcopy(after)
        owner = layout._blocks(altered)[identity]
        if kind == "formatting":
            assert owner["correctIndex"] == 2
            owner["correctIndex"] = 0
        else:
            assert owner["answers"] == ["1:}:fin"]
            owner["answers"] = ["FALSO"]
        exports[prefix + ".changed-answer"] = altered

        altered = deepcopy(after)
        marker = next(entry for entry in altered["textSemantics"]["fields"]
                      if entry["block"] == identity and entry["path"] == "/question")
        marker["sourceSha256"] = "0" * 64
        exports[prefix + ".stale-semantic"] = altered

        altered = deepcopy(after)
        edge = altered["stateLayout"]["editorial"][-1]
        assert edge["id"] == identity
        if kind == "formatting":
            assert edge["returnTo"]["revision"] == 3
            edge["returnTo"]["revision"] = 0
        else:
            edge["afterSha256"] = "0" * 64
        exports[prefix + ".forged-proof"] = altered
    return exports


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    options = parser.parse_args()
    files = fixtures()
    files.update(real_question_spacing_fixtures())
    files.update(real_u12_editorial_fixtures())
    options.output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in files.items():
        destination = options.output_dir / f"{name}.json"
        destination.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Generated {len(files)} isolated multilingual source-format fixtures")


if __name__ == "__main__":
    main()

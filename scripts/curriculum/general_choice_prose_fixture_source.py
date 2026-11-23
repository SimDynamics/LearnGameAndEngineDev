"""Exact U2L10 option publication followed by isolated authored prose revisions.

These QA proposals never modify a lesson asset. Each new passage is authored
through the existing general helper against its immediate predecessor; the
published option, response contract and archived reading sources stay exact.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches


ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "2ca32b2337178d1effa3a2c81759e2493cd746d0"
PUBLISHED_RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "objetos_y_estado/lesson_representar_procesar_y_recuperar.json"
)
PUBLISHED_SHA256 = "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0"
OWNER = "block_0006_0002"
QUESTION = (
    "La ejecución coincide con la salida de un solo caso. "
    "¿Qué conviene hacer a continuación para entender y comprobar la solución?"
)
EXPLANATION = (
    "Un acierto es evidencia de ese caso; explicación, variación y construcción "
    "independiente aportan evidencias distintas. Por ejemplo, con ocho mesas "
    "iniciales y dos nuevas, el estado inicial es ocho y el estado posterior es diez."
)


def verify_published_bytes(payload):
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("Choice prose QA differs from the exact published main U2L10 bytes")
    return payload


def published_bytes():
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{PUBLISHED_COMMIT}:{PUBLISHED_RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def owner(document):
    return state._blocks(document)[OWNER]


def owner_edges(document):
    return [edge for edge in document["stateLayout"]["editorial"]
            if edge["id"] == OWNER]


def publication_chain():
    published = state.parse_json(published_bytes().decode("utf-8"))
    state.validate(published)
    revisions = [record for record in published["stateLayout"].get("choiceRevisions", [])
                 if record["id"] == OWNER]
    if len(revisions) != 1 or revisions[0]["optionIndex"] != 0:
        raise ValueError("The authentic U2L10 option correction is missing")
    original = revisions[0]["block"]
    corrected = deepcopy(original)
    corrected["options"][0] = revisions[0]["replacement"]
    edges = owner_edges(published)
    if owner(published) != corrected or edges != [{
            "id": OWNER, "beforeSha256": state.fingerprint(original),
            "afterSha256": state.fingerprint(corrected)}]:
        raise ValueError("The published first choice edge differs from its complete option-only pair")
    question = state.revise_ordinary_editorial(published, OWNER, {
        "question": {"text": QUESTION, "spans": [state.text_semantics.select(
            QUESTION, "a continuación", "prose", reading="natural")]},
    })
    explanation = state.revise_ordinary_editorial(question, OWNER, {
        "explanation": {"text": EXPLANATION, "spans": [
            state.text_semantics.select(EXPLANATION, "ocho mesas iniciales", "prose", reading="natural"),
            state.text_semantics.select(EXPLANATION, "dos nuevas", "prose", reading="natural"),
        ]},
    })
    for before, after in ((published, question), (question, explanation),
                          (published, explanation)):
        state.validate_transition(before, after)
    return published, question, explanation


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def marker(document, path):
    return next(field for field in document["textSemantics"]["fields"]
                if field["block"] == OWNER and field["path"] == path)


def update_current_head(document):
    owner_edges(document)[-1]["afterSha256"] = state.fingerprint(owner(document))


def update_marker(document, path):
    entry = marker(document, path)
    entry.clear()
    entry.update(state.text_semantics.make_field(document, OWNER, path, []))


def rejected_documents(published, final):
    """Invalid archives and protected fields, including recomputed head hashes.

    Some malformed histories are meaningful only relative to the published
    predecessor. Every candidate must fail that real transition as well as the
    native full/progress loaders; the native test also checks the diagnostic
    for missing or falsified first edges so another rejection cannot mask it.
    """
    cases = []

    def add(name, mutate, diagnostic=""):
        candidate = deepcopy(final)
        mutate(candidate)
        try:
            state.validate_transition(published, candidate)
        except ValueError:
            cases.append((name, candidate, diagnostic))
            return
        raise ValueError(f"An invalid choice prose transition was accepted: {name}")

    def change_field(document, field, value, path=None):
        owner(document)[field] = value
        update_current_head(document)
        if path:
            update_marker(document, path)

    add("answer-index", lambda d: change_field(d, "correctIndex", 1))

    def another_option(document):
        owner(document)["options"][1] += " Alteración no autorizada."
        update_current_head(document)
        update_marker(document, "/options/1")
    add("another-option", another_option)

    def original_option(document):
        revision = next(r for r in document["stateLayout"]["choiceRevisions"] if r["id"] == OWNER)
        owner(document)["options"][0] = revision["block"]["options"][0]
        update_current_head(document)
        update_marker(document, "/options/0")
    add("unreviewed-option", original_option)
    add("unowned-guide", lambda d: change_field(d, "reviewFeedback", "Guía añadida sin contrato."))

    def missing_first(document):
        document["stateLayout"]["editorial"].remove(owner_edges(document)[0])
    add("missing-option-edge", missing_first, "exact archived editorial edge")

    def shortcut(document):
        edges = owner_edges(document)
        first = edges[0]
        first["afterSha256"] = edges[-1]["afterSha256"]
        document["stateLayout"]["editorial"] = [edge for edge in document["stateLayout"]["editorial"]
                                                    if edge["id"] != OWNER or edge is first]
    add("forged-shortcut", shortcut, "exact archived editorial edge")

    def archive_prose(document):
        revision = next(r for r in document["stateLayout"]["choiceRevisions"] if r["id"] == OWNER)
        revision["block"]["question"] += " Pasado inventado."
        owner_edges(document)[0]["beforeSha256"] = state.fingerprint(revision["block"])
    add("forged-archive-prose", archive_prose, "exact archived editorial edge")

    add("broken-prose-chain", lambda d: owner_edges(d)[-1].update(beforeSha256="0" * 64),
        "Broken editorial chain")

    def missing_marker(document):
        document["textSemantics"]["fields"].remove(marker(document, "/question"))
    add("missing-question-marker", missing_marker)
    add("stale-explanation-marker", lambda d: marker(d, "/explanation").update(sourceSha256="0" * 64))
    add("incompatible-question-spans", lambda d: marker(d, "/question")["spans"][0].update(end=100000))
    add("blank-explanation", lambda d: change_field(d, "explanation", "\u00a0", "/explanation"))

    def stale_head(document):
        owner(document)["explanation"] += " Prosa sin su nueva arista."
        update_marker(document, "/explanation")
    add("stale-current-head", stale_head)
    return cases


def export(output_dir):
    output = checked_output(output_dir)
    published, question, explanation = publication_chain()
    payloads = {"published.json": published_bytes(),
                "question.json": json_bytes(question),
                "explanation.json": json_bytes(explanation)}
    rejected = []
    for name, document, diagnostic in rejected_documents(published, explanation):
        filename = f"rejected-{name}.json"
        payloads[filename] = json_bytes(document)
        rejected.append({"file": filename, "reason": name, "diagnostic": diagnostic})
    payloads["manifest.json"] = json_bytes({
        "publishedCommit": PUBLISHED_COMMIT, "publishedPath": PUBLISHED_RELATIVE,
        "publishedSha256": PUBLISHED_SHA256, "identity": OWNER,
        "stages": ["published.json", "question.json", "explanation.json"],
        "rejected": rejected,
        "sha256": {name: hashlib.sha256(payload).hexdigest() for name, payload in payloads.items()},
    })
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        if not existing_matches(output / name, payload):
            with (output / name).open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    print(export(arguments.output_dir))

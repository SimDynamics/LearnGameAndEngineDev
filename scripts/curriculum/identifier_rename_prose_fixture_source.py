"""Authenticated U2L6 rename followed by independently authored prose.

All descendants are isolated QA inputs. Published source is read from its
exact Git object; the original curriculum and learner state stay untouched.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import identifier_rename_archive_preview as rename
import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "55e60dcd4c9da2ddf71aad6eddd1c5937e312337"
PUBLISHED_RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                      "objetos_y_estado/lesson_conversiones_y_deduccion.json")
PUBLISHED_SHA256 = "68799c4d572ae9914ee36ce437029035bfb7cb0d6ed6865418a106260c8c3340"
OWNER = "block_0003_0004"
OTHER_OWNER = "block_0001_0002"
PLANS = {
    OTHER_OWNER: {"code": {"a": "positivo_sin_fraccion", "b": "negativo_sin_fraccion"}},
    OWNER: {"code": {"tarde": "cociente_convertido", "antes": "cociente_con_operando_convertido"}},
}
STAGES = ("published.json", "renamed.json", "question.json", "interleaved.json",
          "explanation.json", "returned.json")
NEGATIVES = ("missing_snapshot", "wrong_prefix", "wrong_predecessor", "removed_edge",
             "reordered_edges", "stale_endpoint", "altered_contract", "altered_source",
             "source_whitespace", "wrong_origin", "stale_semantics", "unrelated_semantics",
             "forged_return", "second_rename", "intermediate_contract", "borrowed_owner")


def verify_published_bytes(payload):
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("Rename/prose QA needs the exact published U2L6 bytes")
    return payload


def published_bytes():
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1",
                       GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show",
         f"{PUBLISHED_COMMIT}:{PUBLISHED_RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def owner(document, identity=OWNER):
    return state._blocks(document)[identity]


def marker(document, path, identity=OWNER):
    return next(field for field in document["textSemantics"]["fields"]
                if field["block"] == identity and field["path"] == path)


def prose_edges(document, identity=OWNER):
    return [edge for edge in document["stateLayout"]["editorial"]
            if edge["id"] == identity and "identifierRenameProse" in edge]


def publication_chain():
    published = state.parse_json(published_bytes().decode("utf-8"))
    state.validate(published)
    renamed = rename.prepare_declared_identifier_rename_draft(published, PLANS)
    question_text = ("Compara cociente_convertido y cociente_con_operando_convertido. "
                     "¿Qué números escribe std::cout en ese orden?")
    question = state.revise_ordinary_editorial(renamed, OWNER, {
        "question": {"text": question_text, "spans": [
            state.text_semantics.select(question_text, name, "cpp", reading="technical")
            for name in ("cociente_convertido", "cociente_con_operando_convertido", "std::cout")]},
    })
    other_text = ("Tras convertir a int, ¿qué valores muestran positivo_sin_fraccion "
                  "y negativo_sin_fraccion, separados por un espacio?")
    interleaved = state.revise_ordinary_editorial(question, OTHER_OWNER, {
        "question": {"text": other_text, "spans": [
            state.text_semantics.select(other_text, name, "cpp", reading="technical")
            for name in ("int", "positivo_sin_fraccion", "negativo_sin_fraccion")]},
    })
    explanation_text = ("cociente_convertido convierte el cociente entero tres; "
                        "cociente_con_operando_convertido convierte el operando antes de dividir. "
                        "La salida es 3 3.25.")
    guide_text = "Sigue los paréntesis de static_cast antes de calcular cada división."
    explanation = state.revise_ordinary_editorial(interleaved, OWNER, {
        "explanation": {"text": explanation_text, "spans": [
            state.text_semantics.select(explanation_text, "cociente_convertido", "cpp", reading="technical"),
            state.text_semantics.select(explanation_text, "cociente_con_operando_convertido", "cpp", reading="technical"),
            state.text_semantics.select(explanation_text, "3 3.25", "output", reading="natural")]},
        "reviewFeedback": {"text": guide_text, "spans": [
            state.text_semantics.select(guide_text, "static_cast", "cpp", reading="technical")]},
    })
    history = [edge for edge in explanation["stateLayout"]["editorial"] if edge["id"] == OWNER]
    nodes = [history[0]["beforeSha256"]] + [edge["afterSha256"] for edge in history]
    restore = {field: {"text": owner(renamed)[field],
                       "spans": deepcopy(marker(renamed, "/" + field)["spans"])}
               for field in ("question", "explanation", "reviewFeedback")}
    restore["returnTo"] = nodes.index(state.fingerprint(owner(renamed)))
    returned = state.revise_ordinary_editorial(explanation, OWNER, restore)
    documents = [published, renamed, question, interleaved, explanation, returned]
    for index, after in enumerate(documents[1:], 1):
        state.validate_transition(documents[index - 1], after)
        state.validate_transition(published, after)
    return documents


def rejected_documents(documents):
    published, _, _, _, final, returned = documents
    rejected = {}
    for name in NEGATIVES:
        document = deepcopy(returned if name == "forged_return" else final)
        edges = prose_edges(document)
        layout = document["stateLayout"]
        if name == "missing_snapshot":
            del edges[0]["identifierRenameProse"]
        elif name == "wrong_prefix":
            edges[-1]["identifierRenameProse"]["historySha256"] = "0" * 64
        elif name == "wrong_predecessor":
            edges[-1]["identifierRenameProse"]["previousBlock"]["question"] += " "
        elif name == "removed_edge":
            layout["editorial"].remove(edges[0])
        elif name == "reordered_edges":
            first = layout["editorial"].index(edges[0])
            layout["editorial"][first], layout["editorial"][first + 1] = (
                layout["editorial"][first + 1], layout["editorial"][first])
        elif name == "stale_endpoint":
            next(item for item in layout["identifierRenameRevisions"] if item["id"] == OWNER)[
                "afterSha256"] = state.fingerprint(owner(document))
        elif name == "altered_contract":
            owner(document)["answers"] = ["99"]
        elif name == "altered_source":
            owner(document)["code"] = owner(document)["code"].replace("total = 13", "total = 17")
        elif name == "source_whitespace":
            owner(document)["code"] += "\n"
        elif name == "wrong_origin":
            layout["family"]["es"]["sha256"] = "0" * 64
        elif name == "stale_semantics":
            marker(document, "/explanation")["sourceSha256"] = "0" * 64
        elif name == "unrelated_semantics":
            marker(document, "/question")["spans"][0]["role"] = "prose"
            descriptor = next(field for field in edges[-1]["identifierRenameProse"][
                "currentTextSemantics"] if field["path"] == "/question")
            descriptor["spans"][0]["role"] = "prose"
        elif name == "forged_return":
            del edges[-1]["returnTo"]
        elif name == "second_rename":
            owner(document)["code"] = owner(document)["code"].replace(
                "cociente_convertido", "cociente_final")
        elif name == "intermediate_contract":
            edges[0]["identifierRenameProse"]["currentBlock"]["answers"] = ["99"]
            edges[-1]["identifierRenameProse"]["previousBlock"]["answers"] = ["99"]
            for ordinal, proof in enumerate(layout["editorial"]):
                if "identifierRenameProse" not in proof:
                    continue
                archive = proof["identifierRenameProse"]
                proof["beforeSha256"] = state.fingerprint(archive["previousBlock"])
                proof["afterSha256"] = state.fingerprint(archive["currentBlock"])
                archive["historySha256"] = state.fingerprint({"lessonId": document["id"],
                    "locale": "es", "editorial": layout["editorial"][:ordinal]})
        elif name == "borrowed_owner":
            edges[0]["identifierRenameProse"]["previousTextSemantics"][0]["block"] = OTHER_OWNER
        for operation in (lambda: state.validate(document),
                          lambda: state.validate_transition(published, document)):
            try:
                operation()
            except (ValueError, KeyError, TypeError):
                pass
            else:
                raise AssertionError("An unsafe rename/prose fixture was accepted: " + name)
        rejected[name] = document
    return rejected


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def fixture_payloads():
    documents = publication_chain()
    payloads = {name: json_bytes(document) for name, document in zip(STAGES, documents)}
    payloads["published.json"] = published_bytes().decode("utf-8")
    for name, document in rejected_documents(documents).items():
        payloads["negative/" + name + ".json"] = json_bytes(document)
    manifest = {
        "publishedCommit": PUBLISHED_COMMIT, "publishedPath": PUBLISHED_RELATIVE,
        "publishedSha256": PUBLISHED_SHA256, "owner": OWNER, "otherOwner": OTHER_OWNER,
        "stages": list(STAGES), "negative": ["negative/" + name + ".json" for name in NEGATIVES],
        "sha256": {name: hashlib.sha256(text.encode("utf-8")).hexdigest()
                   for name, text in payloads.items()},
    }
    payloads["manifest.json"] = json_bytes(manifest)
    return payloads


def export(output_dir):
    directory = Path(output_dir)
    for name, payload in fixture_payloads().items():
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload.encode("utf-8"))


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    export(parser.parse_args().output_dir)

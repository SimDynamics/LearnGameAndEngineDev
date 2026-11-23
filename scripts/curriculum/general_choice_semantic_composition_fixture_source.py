"""Authenticated U2L9 publications composed with one QA-only option revision.

No lesson asset is edited. The proposed option is an isolated technical fixture,
not the inaccessible editorial draft on the user's Windows computer.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "objetos_y_estado/lesson_leer_una_entrada_conocida.json")
PUBLICATIONS = {
    "before": ("c00798548b9ebebf60c6dac19ec296dc43d3c33d",
               "37b473d7e9b66ab8232812e4e2c9a25e0aaa207bcfc92059fcd6fc675401d94f"),
    "published": ("a658efea1d75f0fb017d484ae3936b8b4001cb12",
                  "7a5eaa190463976dc686e288abe5f61eed0574849ca51e33a0bc5a762c569208"),
}
OWNER = "block_0005_0003"
OPTION_INDEX = 1
REPLACEMENT = ("Comprobar si la lectura fue correcta después de std::cin >> cajas; "
               "y antes de cajas += 3;.")
PARAGRAPHS = (
    "block_0000_0004", "block_0002_0004", "block_0005_0001", "block_0006_0001",
    "block_0007_0002", "block_0008_0002", "block_0009_0004", "block_0010_0002",
)
CHANGED_FIELDS = tuple((owner, "/text") for owner in PARAGRAPHS) + (
    (OWNER, "/question"), (OWNER, "/explanation"),
)


def verify_publication_bytes(name, payload):
    if hashlib.sha256(payload).hexdigest() != PUBLICATIONS[name][1]:
        raise ValueError("Semantic composition QA differs from its exact complete publication")
    return payload


@lru_cache(maxsize=None)
def publication_bytes(name):
    commit, _digest = PUBLICATIONS[name]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{commit}:{RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    return verify_publication_bytes(name, payload)


def blocks(document):
    return state._blocks(document)


def markers(document):
    return {(field["block"], field["path"]): field
            for field in document.get("textSemantics", {}).get("fields", [])}


def marker(document, owner, path):
    return markers(document)[owner, path]


def option_revision(document):
    return next(item for item in document["stateLayout"]["choiceRevisions"]
                if item["id"] == OWNER)


def review_option(document):
    spans = [state.text_semantics.select(REPLACEMENT, source, "cpp", reading="technical")
             for source in ("std::cin >> cajas;", "cajas += 3;")]
    return state.revise_choice_editorial(document, OWNER, OPTION_INDEX, REPLACEMENT, spans=spans)


def publication_chain():
    before, published = (json.loads(publication_bytes(name)) for name in ("before", "published"))
    state.validate_transition(before, published)
    old, current = markers(before), markers(published)
    changed = {key for key in old.keys() | current.keys() if old.get(key) != current.get(key)}
    if changed != set(CHANGED_FIELDS):
        raise ValueError("The independently reviewed U2L9 semantic surfaces changed")
    reviewed = review_option(published)
    state.validate_transition(before, reviewed)
    return before, published, reviewed


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def update_head(document, owner=OWNER):
    edges = [item for item in document["stateLayout"]["editorial"] if item["id"] == owner]
    edges[-1]["afterSha256"] = state.fingerprint(blocks(document)[owner])


def refresh_marker(document, owner, path):
    replacement = state.text_semantics.make_field(document, owner, path, [])
    existing = markers(document).get((owner, path))
    if existing is None:
        document["textSemantics"]["fields"].append(replacement)
    else:
        existing.clear()
        existing.update(replacement)


def rejected_documents(before, published, reviewed):
    """Only standalone malformed documents are sent to both native loaders."""
    cases = []

    def add(name, mutate):
        candidate = deepcopy(reviewed)
        mutate(candidate)
        try:
            state.validate(candidate)
        except ValueError:
            cases.append((name, candidate, ""))
            return
        raise ValueError("Native negative is valid without a transition witness: " + name)

    def answer(document):
        blocks(document)[OWNER]["correctIndex"] = 0
        update_head(document)
    add("answer-index", answer)

    def another_option(document):
        blocks(document)[OWNER]["options"][0] += " Alteración QA."
        refresh_marker(document, OWNER, "/options/0")
        update_head(document)
    add("another-option", another_option)

    def stale_replacement(document):
        blocks(document)[OWNER]["options"][OPTION_INDEX] = "Reemplazo ajeno al archivo."
        refresh_marker(document, OWNER, "/options/1")
        update_head(document)
    add("unarchived-replacement", stale_replacement)

    add("wrong-archive-owner", lambda d: option_revision(d).update(id=PARAGRAPHS[0]))
    add("old-option-source", lambda d: option_revision(d)["semanticBefore"].update(sourceSha256="0" * 64))
    add("new-option-source", lambda d: marker(d, OWNER, "/options/1").update(sourceSha256="0" * 64))
    add("option-range", lambda d: marker(d, OWNER, "/options/1")["spans"][0].update(end=100000))
    add("paragraph-source", lambda d: marker(d, PARAGRAPHS[0], "/text").update(sourceSha256="0" * 64))

    def missing_option_edge(document):
        edges = document["stateLayout"]["editorial"]
        edges.remove(next(item for item in reversed(edges) if item["id"] == OWNER))
    add("missing-option-edge", missing_option_edge)

    def broken_paragraph_chain(document):
        edges = [item for item in document["stateLayout"]["editorial"] if item["id"] == PARAGRAPHS[0]]
        edges[-1]["beforeSha256"] = "0" * 64
    add("paragraph-chain", broken_paragraph_chain)
    return cases


def transition_rejections(before, published, reviewed):
    """Valid overlays cannot repaint sources unchanged by this transition."""
    cases = []

    def add(name, baseline, mutate):
        candidate = deepcopy(reviewed)
        mutate(candidate)
        try:
            state.validate_transition(baseline, candidate)
        except ValueError:
            cases.append((name, baseline, candidate))
            return
        raise ValueError("An unauthorized composed transition was accepted: " + name)

    unchanged = next(key for key, field in markers(published).items()
                     if key[1] == "/text" and key not in CHANGED_FIELDS
                     and field["spans"] and blocks(published)[key[0]]["type"] == "paragraph")
    add("unchanged-prose-marks", before,
        lambda d: marker(d, *unchanged).update(spans=[]))
    add("removed-marked-prose", before,
        lambda d: marker(d, PARAGRAPHS[0], "/text").update(spans=[]))
    add("missing-prose-descriptor", before,
        lambda d: d["textSemantics"]["fields"].remove(marker(d, PARAGRAPHS[0], "/text")))
    add("repaint-published-prose", published,
        lambda d: marker(d, PARAGRAPHS[0], "/text")["spans"][0].update(role="prose", reading="natural"))
    add("removed-unrelated-marker", before,
        lambda d: d["textSemantics"]["fields"].remove(marker(d, *unchanged)))
    add("reordered-markers", before,
        lambda d: d["textSemantics"]["fields"].reverse())
    add("removed-root-markers", before,
        lambda d: d["textSemantics"].pop("rootFields"))
    return cases


def export(output_dir):
    output = checked_output(output_dir)
    before, published, reviewed = publication_chain()
    payloads = {"before.json": publication_bytes("before"),
                "published.json": publication_bytes("published"),
                "reviewed.json": json_bytes(reviewed)}
    rejected = []
    for name, document, diagnostic in rejected_documents(before, published, reviewed):
        filename = "rejected-" + name + ".json"
        payloads[filename] = json_bytes(document)
        rejected.append({"file": filename, "reason": name, "diagnostic": diagnostic})
    payloads["manifest.json"] = json_bytes({
        "identity": OWNER, "optionIndex": OPTION_INDEX,
        "stages": ["before.json", "published.json", "reviewed.json"],
        "publications": {name: {"commit": commit, "sha256": digest, "path": RELATIVE}
                         for name, (commit, digest) in PUBLICATIONS.items()},
        "changedFields": [{"block": owner, "path": path} for owner, path in CHANGED_FIELDS],
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
    print(export(parser.parse_args().output_dir))

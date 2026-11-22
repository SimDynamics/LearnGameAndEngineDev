"""Authenticate option revisions after an older published reading snapshot.

Only QA files are exported. Published curriculum and retirement snapshots remain
unchanged; the historical input is read from one immutable Git publication.
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
PUBLISHED_COMMIT = "08b6afbdd6e747e12255bb45d9a587481d4ee26b"
PUBLISHED_RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "objetos_y_estado/lesson_conversiones_y_deduccion.json"
)
PUBLISHED_SHA256 = "cf602c1bada1ccbbf1e9ba075cdf8a5a0c913b2dc6fac6dd15167d267e25052c"
PUBLISHED_OWNER = "block_0007_0004"
PUBLISHED_REPLACEMENT = (
    "Revisar si para la tarea basta con contar los metros completos "
    "o si también se necesita el medio metro."
)


def synthetic_pair():
    source = {
        "id": "cpp_fundamentals__qa__choice_retirement",
        "title": "Historical choice reading QA",
        "pages": [{"blocks": [
            {"type": "multiple_choice", "question": "Pregunta anterior.",
             "options": ["Primera.", "Segunda.", "Tercera."], "correctIndex": 2,
             "explanation": "Explicación anterior.",
             "reviewFeedback": ["Pista A.", "Pista B.", "Pista C."]},
            {"type": "paragraph", "text": "Presentación que se retirará."},
        ]}],
    }
    original = state.prepare_v2(source, "es", {"es": source})
    retired = state.retire(original, ["block_0000_0001"])
    owner = "block_0000_0000"
    before = state.revise_ordinary_editorial(retired, owner, {
        "question": {"text": "Pregunta vigente.", "spans": []},
        "explanation": {"text": "Explicación vigente.", "spans": []},
        "reviewFeedback": {
            "0": {"text": "Pista vigente A.", "spans": []},
            "1": {"text": "Pista vigente B.", "spans": []},
            "2": {"text": "Pista vigente C.", "spans": []},
        },
    })
    after = state.revise_choice_editorial(
        before, owner, 2, "Tercera opción revisada.", spans=[])
    return before, after, owner


def verify_published_bytes(payload):
    if hashlib.sha256(payload).hexdigest() != PUBLISHED_SHA256:
        raise ValueError("U2L6 choice fixture differs from its exact published Git bytes")
    return payload


def published_bytes():
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_published_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{PUBLISHED_COMMIT}:{PUBLISHED_RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def published_pair():
    before = state.parse_json(published_bytes().decode("utf-8"))
    after = state.revise_choice_editorial(
        before, PUBLISHED_OWNER, 2, PUBLISHED_REPLACEMENT, spans=[])
    return before, after, PUBLISHED_OWNER


def verify_pair(before, after, owner):
    state.validate_transition(before, after)
    old, new = state._blocks(before)[owner], state._blocks(after)[owner]
    revisions = [
        item for item in after["stateLayout"].get("choiceRevisions", [])
        if item["id"] == owner
    ]
    if len(revisions) != 1:
        raise ValueError("The fixture requires one complete option archive")
    revision = revisions[0]
    expected = deepcopy(old)
    expected["options"][revision["optionIndex"]] = revision["replacement"]
    if new != expected or revision["block"] != old:
        raise ValueError("A choice fixture changed more than its selected option")
    for field in ("origin", "current", "family", "additions", "retired", "retirementHistory"):
        if before["stateLayout"].get(field) != after["stateLayout"].get(field):
            raise ValueError(f"A choice fixture changed its existing {field}")
    if state.state_fingerprint(old) != state.state_fingerprint(
            state._historical_choice_contract(after, owner, new)):
        raise ValueError("A choice fixture changed the stored response contract")


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def snapshot_source(document, owner):
    for snapshot in document["stateLayout"].get("retirementHistory", []):
        for page, record in zip(snapshot["pages"], snapshot["current"]):
            for block, identity in zip(page["blocks"], record["blocks"]):
                if identity == owner:
                    return snapshot, block
    raise ValueError("No archived reading source for the fixture owner")


def rejected_documents():
    _, after, owner = synthetic_pair()
    result = []
    mutations = (
        ("unknown-option", "options", ["Primera.", "Segunda.", "Texto ajeno."]),
        ("another-option", "options", ["Otra respuesta.", "Segunda.", "Tercera."]),
        ("correct-index", "correctIndex", 0),
        ("option-count", "options", ["Primera.", "Tercera."]),
        ("option-type", "options", "Primera."),
    )
    for name, field, replacement in mutations:
        forged = deepcopy(after)
        snapshot, old = snapshot_source(forged, owner)
        original_hash = state.fingerprint(old)
        old[field] = deepcopy(replacement)
        # Recalculate the full source node so a forged digest alone cannot
        # satisfy the separately protected historical response contract.
        first = next(edge for edge in forged["stateLayout"]["editorial"]
                     if edge["id"] == owner)
        if first["beforeSha256"] != original_hash:
            raise ValueError("The synthetic fixture lost its first source node")
        first["beforeSha256"] = state.fingerprint(old)
        snapshot.pop("editorialPrefix", None)
        result.append((name, forged))
    forged = deepcopy(after)
    snapshot, old = snapshot_source(forged, owner)
    snapshot.pop("editorialPrefix", None)
    old["question"] = "Prosa no publicada con la misma respuesta."
    result.append(("unpublished-prose", forged))
    for name, document in result:
        try:
            state.validate(document)
        except ValueError:
            continue
        raise ValueError(f"A forged choice fixture was accepted: {name}")
    return result


def export(output_dir):
    output = checked_output(output_dir)
    payloads = {}
    records = []
    for name, factory in (("synthetic", synthetic_pair), ("published", published_pair)):
        before, after, owner = factory()
        verify_pair(before, after, owner)
        record = {"name": name, "identity": owner}
        for side, document in (("before", before), ("after", after)):
            filename = f"{name}-{side}.json"
            payload = published_bytes() if name == "published" and side == "before" else json_bytes(document)
            payloads[filename] = payload
            record[side] = {"file": filename, "sha256": hashlib.sha256(payload).hexdigest()}
        records.append(record)
    rejected = []
    for name, document in rejected_documents():
        filename = f"rejected-{name}.json"
        payloads[filename] = json_bytes(document)
        rejected.append({"file": filename, "reason": name})
    payloads["manifest.json"] = json_bytes({
        "publishedCommit": PUBLISHED_COMMIT, "publishedPath": PUBLISHED_RELATIVE,
        "fixtures": records, "rejected": rejected,
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


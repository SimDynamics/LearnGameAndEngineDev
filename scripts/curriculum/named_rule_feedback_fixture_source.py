"""Export complete authenticated named-rule publications for Win32 feedback QA.

The original UI scenario remains tied to its actual Git publication. The current
input is the complete working-tree publication, authenticated against that
origin and its canonical owners. A second byte-pinned Git publication reproduces
the failure. No publication is reconstructed from archived blocks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import format_batch_history as history
import stable_state_layout as state
from code_format_compat import require_source_lexical, source_projection


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
            "comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json")
LESSON_ID = "programming_practice_tools__comprobaciones_repetibles__nombrar_reglas_y_separar_acuerdos"
FOCAL_OWNER = "block_0008_0007"
PREREQUISITES = ("block_0002_0004", "block_0005_0006", "block_0006_0009", "block_0007_0004")
REFERENCES = ("block_0003_0002", "block_0006_0001", "block_0006_0003", "block_0008_0001")
PUBLICATIONS = {
    "original": history.Publication(
        "5ec6d37a657ec31128e899b4b834fedf518fa84c",
        "09fbf7be9c9e236f706a943c694e6644688888b4d7d58e0717c1411dcda4ffc3"),
    "current": history.Publication(
        "50b479912dd0e598ad0b717332a15bf7d8606e69",
        "13b529b6e65afc634b485792661b6d7d7df1c48bbe8b2099d943e9ae3b5fec18"),
}
PUBLICATION_CHOICES = ("working-tree", "50b4799")


def asset_path() -> Path:
    return ROOT / RELATIVE


def publication_payloads(publication: str = "working-tree") -> dict[str, bytes]:
    if publication not in PUBLICATION_CHOICES:
        raise ValueError("Unknown named-rule publication selector")
    result = {}
    for name, pin in PUBLICATIONS.items():
        payload = history.publication_bytes(ROOT, RELATIVE, pin)
        result[name] = history.verify_publication_bytes(payload, pin)
    result["baseline"] = result["current"]
    if publication == "working-tree":
        result["current"] = asset_path().read_bytes()
    return result


def verify_publications(payloads: dict[str, bytes], publication: str = "working-tree") -> dict[str, dict]:
    if publication not in PUBLICATION_CHOICES:
        raise ValueError("Unknown named-rule publication selector")
    history.verify_publication_bytes(payloads["original"], PUBLICATIONS["original"])
    history.verify_publication_bytes(payloads["baseline"], PUBLICATIONS["current"])
    if publication == "50b4799":
        history.verify_publication_bytes(payloads["current"], PUBLICATIONS["current"])
    documents = {name: json.loads(payloads[name]) for name in ("original", "baseline", "current")}
    original, baseline, current = (documents[name] for name in ("original", "baseline", "current"))
    for document in documents.values():
        if document["id"] != LESSON_ID or document["stateLayout"]["locale"] != "es":
            raise ValueError("The named-rule publication belongs to another lesson or locale")
        # Work can publish named-rule objectives after the original UI pilot.
        # The complete live publication, including any such declarations,
        # must pass the real authoring validator. Do not strip the declarations
        # or substitute the older, prospective fixture for the current asset.
        state.validate(document)
    if any("compilerNamedRuleChecks" in document for document in (original, baseline)):
        raise ValueError("The pinned pre-adoption named-rule publications changed")
    state.validate_transition(original, baseline)
    state.validate_transition(baseline, current)
    state.validate_transition(original, current)
    old_storage, new_storage = state.validate(original), state.validate(current)
    old, old_positions = history.active_owners(original)
    new, new_positions = history.active_owners(current)
    old_meta, new_meta = original["stateLayout"], current["stateLayout"]
    for field in ("origin", "family"):
        if old_meta.get(field) != new_meta.get(field):
            raise ValueError("The named-rule format review changed stable historical metadata")
    if old_meta.get("sourceFormatRevisions", []):
        raise ValueError("The original named-rule publication unexpectedly has format archives")
    entries = new_meta.get("sourceFormatRevisions", [])
    archives = {entry["id"]: entry["block"] for entry in entries}
    if len(archives) != len(entries):
        raise ValueError("The current named-rule publication duplicates a source archive")
    previous = old_meta["editorial"]
    if new_meta["editorial"][:len(previous)] != previous:
        raise ValueError("The named-rule format review lost its exact editorial predecessor")
    baseline_meta = baseline["stateLayout"]
    baseline_edges = baseline_meta["editorial"]
    baseline_archives = baseline_meta["sourceFormatRevisions"]
    if (new_meta["editorial"][:len(baseline_edges)] != baseline_edges
            or entries[:len(baseline_archives)] != baseline_archives):
        raise ValueError("The current named-rule publication lost already published format history")
    for owner in (FOCAL_OWNER, *PREREQUISITES, *REFERENCES):
        if owner not in new or new[owner]["type"] != old[owner]["type"]:
            raise ValueError("The current named-rule publication lost a required active owner")
        if old_storage[old_positions[owner]] != new_storage[new_positions[owner]]:
            raise ValueError("A named-rule owner changed its historical storage address")
        if owner in archives and archives[owner] != old[owner]:
            raise ValueError("A named-rule source review replaced the complete canonical block")
        require_source_lexical(old[owner], new[owner])
        if state.state_fingerprint(source_projection(new[owner], old[owner])) != state.state_fingerprint(old[owner]):
            raise ValueError("The source review changed a protected named-rule block contract")
    for owner in (FOCAL_OWNER, *PREREQUISITES):
        if old[owner]["type"] != "cpp_compiler":
            raise ValueError("The focal editor or a real prerequisite lost its authenticated source")
        for field in ("outputCheck", "sourceFiles", "initialStandardInput", "cmake"):
            if old[owner].get(field) != new[owner].get(field):
                raise ValueError("The named-rule editor changed its first-case or build contract")
    focal = old[FOCAL_OWNER]
    if (len(focal["outputCheck"]["cases"]) != 1
            or focal["outputCheck"]["cases"][0].get("standardInput", "")
            or focal.get("initialStandardInput")
            or focal.get("projectFiles") or focal.get("sourceFiles") or focal.get("cmake")):
        raise ValueError("The named-rule feedback editor lost its single no-input public case")
    return documents


def write_fixtures(output: Path, publication: str = "working-tree") -> dict:
    payloads = publication_payloads(publication)
    documents = verify_publications(payloads, publication)
    manifest = {
        "version": 1, "lessonId": LESSON_ID, "owner": FOCAL_OWNER,
        "prerequisites": list(PREREQUISITES), "references": list(REFERENCES),
        "publication": publication,
        "baseline": {"file": "baseline.json", "commit": PUBLICATIONS["current"].commit,
                     "sha256": PUBLICATIONS["current"].sha256},
        "scenarios": [{"name": name, "file": name + ".json",
                       "commit": pin.commit if name == "original" or publication == "50b4799" else "",
                       "sha256": hashlib.sha256(payloads[name]).hexdigest(),
                       "sourceFormatCount": len(documents[name]["stateLayout"].get("sourceFormatRevisions", [])),
                       "editorialCount": len(documents[name]["stateLayout"]["editorial"])}
                      for name, pin in PUBLICATIONS.items()],
    }
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        (output / (name + ".json")).write_bytes(payload)
    (output / "manifest.json").write_bytes(
        (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--publication", choices=PUBLICATION_CHOICES, default="working-tree")
    args = parser.parse_args()
    write_fixtures(args.output_dir, args.publication)
    print("PASS named-rule UI publications: complete authenticated origin and current lesson, "
          "canonical source history, focal owner and four genuine prerequisites")


if __name__ == "__main__":
    main()

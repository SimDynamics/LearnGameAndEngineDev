"""Pinned QA proposals for ten bibliography-only explanations; no live edits.

The expected text is an explicit independent oracle, not the production parser's
output. The ordinary v2 authoring API adds and validates its real archive proofs.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "06d31ba73be4b0c7672c7113df2340c9edbb2c04"
PREFIX = "app/src/main/assets/content/{locale}/programming/cpp_fundamentals/objetos_y_estado/"
# Exact SHA-256 of each complete Git blob; translations are read only to
# authenticate the existing family, and are never rewritten or exported.
SOURCE_SHA256 = {
    "lesson_caracteres_y_valores_logicos.json": {
        "es": "6f936473855d63cdd9d56977ebce138aba7afba0628d486a077a08c2274a1d59",
        "en": "e50fc82dabaee48694cae74367ff918407d639b4e63363d2ea0fd1d4115c29dc",
        "fr": "ddd82ac06325d07a1fcfa1848eaf32a02f20e921f3b971403d222636dd682542"},
    "lesson_conversiones_y_deduccion.json": {
        "es": "e1ff3492e91933efd6810093f5aaac01ed9a5b15b7432a9ec992c8c3c3715f9d",
        "en": "301c9bdc18ecdcba439e9d17425580322fb26dd9bf493875a8ae7c4c8cd2a1e1",
        "fr": "249bd634c327032411e283466fc7e6a8aff7fe1f14609e18aea010b747f5e7db"},
    "lesson_enteros_y_aproximaciones.json": {
        "es": "839fe75126f0bf9c09f5a4be375b860935cadf3f202fb9341eafb91e2600d4db",
        "en": "b16ed16551631b95035c40ab9586a2096310e0b3f46e6bc8c622dce910a3d5e7",
        "fr": "28f9bcb52672ea1172ef24a3152b6a7f2586852aca7a4d82aef9deedb3a5e9c0"}}
SOURCE_BLOBS = {
    "lesson_caracteres_y_valores_logicos.json": {
        "es": "638e18296eece0605fe22c80f56c0c0d72602ce8",
        "en": "a1b30fc9fcc7a21b588cb33804b90ce381c5020f",
        "fr": "7d95a55005ac1419d07d369576550b7fbda754b2"},
    "lesson_conversiones_y_deduccion.json": {
        "es": "0bc2c0dd8db5fdaf6c08425b62073c5d4f0de90d",
        "en": "4a9871f64af7eb387d45f4a7bbbd1e8f0de578b8",
        "fr": "cc5500100cd1694962bb99caf37f68ad5a3da990"},
    "lesson_enteros_y_aproximaciones.json": {
        "es": "330b982474262a02c5a4f9d9720ff424815c8f39",
        "en": "8a6fbb6b0a474af4f7ba12d950e8abc6f4b5460a",
        "fr": "b864ad9696554266bb2d46c7006643f83119d804"}}

SHORT = "(Köppe et al., 2023)"
SHORT_MIXED = "(Köppe et al., 2023; Stroustrup, 2024)"
BOOL_CITATION = ("(Köppe et al., 2023, sección 30.4.3.3.3, párr. 5; "
                 "sección 7.3.7, párr. 6; Stroustrup, 2024)")
# Whole-parenthesis QA goldens make unchanged coauthors/separators explicit.
EDITS = {
    "lesson_caracteres_y_valores_logicos.json": {
        "block_0002_0002": [("(Köppe et al., 2023, sección 5.13.3, párr. 4 y tabla 10; Stroustrup, 2024)", SHORT_MIXED)],
        "block_0002_0003": [("(Köppe et al., 2023, sección 5.13.3, párrs. 1–3 y tabla 9)", SHORT)],
        "block_0003_0002": [("(Köppe et al., 2023, sección 23.4.3.7.1, párr. 5; sección 23.4.3.7.2, párr. 11)", SHORT)],
        "block_0005_0001": [(BOOL_CITATION, SHORT_MIXED)],
        "block_0005_0002": [(BOOL_CITATION, SHORT_MIXED)],
        "block_0005_0003": [("(Köppe et al., 2023, sección 5.13.6, párr. 1; Stroustrup, 2024)", SHORT_MIXED)],
        "block_0005_0004": [(BOOL_CITATION, SHORT_MIXED)],
        "block_0007_0004": [(BOOL_CITATION, SHORT_MIXED)]},
    "lesson_conversiones_y_deduccion.json": {
        "block_0003_0005": [
            ("(Köppe et al., 2023, sección 7.6.1.9, párr. 1; Stroustrup, 2024)", SHORT_MIXED),
            ("(Köppe et al., 2023, sección 6.8.2, párr. 12; sección 9.2.9.3, párr. 4 y tabla 17)", SHORT)]},
    "lesson_enteros_y_aproximaciones.json": {
        "block_0003_0003": [("(Köppe et al., 2023, sección 9.2.9.3, párr. 4 y tabla 17)", SHORT)]}}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _repository(repository=None):
    repository = ROOT if repository is None else Path(repository).resolve()
    _require(repository == ROOT.resolve(), "Bibliography fixtures must read this existing Codex checkout")
    return repository


def source_bytes(name, locale="es", repository=None):
    """Read only the pinned complete local blob; never fetch or honor replacements."""
    _require(name in SOURCE_SHA256 and locale in SOURCE_SHA256[name], "Unknown bibliography QA source")
    repository = _repository(repository)
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-replace-objects", "--no-optional-locks", "-c", f"safe.directory={repository}",
         "cat-file", "blob", f"{COMMIT}:{PREFIX.format(locale=locale) + name}"],
        cwd=repository, env=environment, stderr=subprocess.PIPE)
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    _require(hashlib.sha256(payload).hexdigest() == SOURCE_SHA256[name][locale]
             and blob == SOURCE_BLOBS[name][locale], "Bibliography source differs from its exact pinned Git blob")
    return payload


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def _units(text):
    return len(text.encode("utf-16-le")) // 2


def _replace_explanation(document, identity, replacements):
    block = layout._blocks(document)[identity]
    old = block["explanation"]
    edits = []
    for before, after in replacements:
        _require(old.count(before) == 1, "A bibliography golden must occur once in its exact owned explanation")
        index = old.index(before)
        edits.append((_units(old[:index]), _units(old[:index + len(before)]), after))
    edits.sort()
    _require(all(a[1] <= b[0] for a, b in zip(edits, edits[1:])), "Bibliography QA edits cannot overlap")
    raw, pieces, at = old.encode("utf-16-le"), [], 0
    for start, end, replacement in edits:
        pieces.extend((raw[at * 2:start * 2], replacement.encode("utf-16-le")))
        at = end
    pieces.append(raw[at * 2:])
    current = b"".join(pieces).decode("utf-16-le")
    block["explanation"] = current

    def shifted(position):
        _require(not any(start < position < end for start, end, _ in edits),
                 "A QA citation edit cannot split an independently authored semantic span")
        return position + sum(_units(replacement) - (end - start) for start, end, replacement in edits if end <= position)

    fields = document.get("textSemantics", {}).get("fields", [])
    for field in fields:
        if field["block"] != identity or field["path"] != "/explanation":
            continue
        _require(field["sourceSha256"] == hashlib.sha256(old.encode("utf-8")).hexdigest(),
                 "Bibliography source semantics do not own the exact old explanation")
        for span in field["spans"]:
            start, end = shifted(span["start"]), shifted(span["end"])
            span.update(start=start, end=end, text=current.encode("utf-16-le")[start * 2:end * 2].decode("utf-16-le"))
        field["sourceSha256"] = hashlib.sha256(current.encode("utf-8")).hexdigest()


def verify_pair(name, before, after):
    """Verify the independent expected texts and the exact preserved remainder."""
    _require(name in EDITS and before["id"] == after["id"], "Bibliography QA changed its lesson owner")
    previous, current = layout._blocks(before), layout._blocks(after)
    _require(previous.keys() == current.keys(), "Bibliography QA changed block identities")
    _require({key for key in previous if previous[key] != current[key]} == set(EDITS[name]),
             "Bibliography QA changed unrelated blocks")
    restored = deepcopy(after)
    for identity, replacements in EDITS[name].items():
        expected = previous[identity]["explanation"]
        for old, new in replacements:
            expected = expected.replace(old, new)
        _require(current[identity] == dict(previous[identity], explanation=expected),
                 "Bibliography QA changed text or an exercise contract outside its golden citation edits")
        layout._blocks(restored)[identity]["explanation"] = previous[identity]["explanation"]
    old_fields = {(field["block"], field["path"]): field for field in before.get("textSemantics", {}).get("fields", [])}
    new_fields = {(field["block"], field["path"]): field for field in after.get("textSemantics", {}).get("fields", [])}
    _require(old_fields.keys() == new_fields.keys(), "Bibliography QA cannot add or remove authored semantic fields")
    for identity in EDITS[name]:
        key = identity, "/explanation"
        if key not in old_fields:
            continue
        old_spans, new_spans = old_fields[key]["spans"], new_fields[key]["spans"]
        _require(len(old_spans) == len(new_spans) and all(
            {key: value for key, value in old_span.items() if key not in {"start", "end", "text"}}
            == {key: value for key, value in new_span.items() if key not in {"start", "end", "text"}}
            for old_span, new_span in zip(old_spans, new_spans)),
            "Bibliography QA changed semantic roles, reading intentions or span count")
    for index, field in enumerate(restored.get("textSemantics", {}).get("fields", [])):
        if field["block"] in EDITS[name] and field["path"] == "/explanation":
            restored["textSemantics"]["fields"][index] = deepcopy(old_fields[(field["block"], field["path"])])
    prefix, proofs = before["stateLayout"]["editorial"], after["stateLayout"]["editorial"]
    _require(proofs[:len(prefix)] == prefix and len(proofs) == len(prefix) + len(EDITS[name])
             and {proof["id"] for proof in proofs[len(prefix):]} == set(EDITS[name])
             and all("bibliography" in proof for proof in proofs[len(prefix):]),
             "Bibliography QA lost its exact previous proof prefix or complete new bibliography archives")
    restored["stateLayout"] = deepcopy(before["stateLayout"])
    _require(restored == before, "Bibliography QA changed fields beyond the ten explanations, their descriptors and proofs")
    layout.validate_transition(before, after)


def _load(repository=None):
    result, originals = {}, {}
    for name in EDITS:
        payloads = {locale: source_bytes(name, locale, repository) for locale in SOURCE_SHA256[name]}
        family = {locale: layout.parse_json(payload.decode("utf-8-sig")) for locale, payload in payloads.items()}
        before = family["es"]
        layout.validate(before)
        draft = deepcopy(before)
        for identity, replacements in EDITS[name].items():
            _replace_explanation(draft, identity, replacements)
        after = layout.prepare_v2(draft, "es", family, before)
        verify_pair(name, before, after)
        layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])
        result[name], originals[name] = (before, after), payloads["es"]
    return result, originals


def load_pairs(repository=None):
    """Return three (published before, independent proposed after) document pairs."""
    return list(_load(repository)[0].values())


def load_named_pairs(repository=None):
    """Return the same independent pairs indexed by their exact lesson filename."""
    return _load(repository)[0]


def export_fixture(output_dir=None, repository=None):
    """Write six QA documents, explicit parser oracles and their manifest."""
    repository = _repository(repository)
    output = Path(os.path.abspath(output_dir or repository / "out/build/test-fixtures/bibliography"))
    for entry in (output, *output.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        _require(not stat.S_ISLNK(attributes.st_mode) and not getattr(attributes, "st_file_attributes", 0) & 0x400,
                 "Bibliography QA output cannot follow links or reparse points")
    output = output.resolve()
    allowed = repository / "out/build"
    _require(output != allowed and output.is_relative_to(allowed), "Bibliography fixtures must stay below this checkout's out/build")
    for parent in (output, *output.parents):
        if parent == repository:
            break
        _require(not (parent / ".git").exists(), "Bibliography QA cannot write another checkout")
    pairs, originals = _load(repository)
    payloads, lessons = {}, []
    for name, (before, after) in pairs.items():
        old_name, new_name = "before-" + name, "after-" + name
        payloads[old_name], payloads[new_name] = originals[name], json_bytes(after)
        old, new = layout._blocks(before), layout._blocks(after)
        lessons.append({"file": name, "lessonId": before["id"],
            "before": {"file": old_name, "sha256": hashlib.sha256(payloads[old_name]).hexdigest()},
            "after": {"file": new_name, "sha256": hashlib.sha256(payloads[new_name]).hexdigest()},
            "owners": [{"id": identity, "path": "/explanation", "citations": len(edits),
                        "beforeSha256": layout.fingerprint(old[identity]), "afterSha256": layout.fingerprint(new[identity])}
                       for identity, edits in EDITS[name].items()]})
    # Test-only explicit before/after pairs are shared with the native mirror.
    # Import only during export: load_pairs has no dependency on a test module.
    from importlib import util
    test_spec = util.spec_from_file_location("bibliography_shared_oracles", Path(__file__).with_name("test_bibliography_revision.py"))
    test_module = util.module_from_spec(test_spec)
    test_spec.loader.exec_module(test_module)
    cases = test_module.bibliography_revision_cases()
    _require(type(cases) is list and bool(cases) and all(
        type(case) is dict and set(case) == {"before", "after", "expected"}
        and type(case["before"]) is str and type(case["after"]) is str and type(case["expected"]) is bool
        for case in cases), "Bibliography parser QA corpus changed its explicit shared contract")
    payloads["predicate-cases.json"] = json_bytes(cases)
    payloads["manifest.json"] = json_bytes({"commit": COMMIT, "afterIsProposal": True,
        "sourceSha256": SOURCE_SHA256, "sourceBlobs": SOURCE_BLOBS,
        "explanations": 10, "citations": 11, "lessons": lessons,
        "predicateCases": {"file": "predicate-cases.json", "cases": len(cases),
                           "sha256": hashlib.sha256(payloads["predicate-cases.json"]).hexdigest()}})
    for name, payload in payloads.items():
        target = output / name
        if target.exists() or target.is_symlink():
            attributes = target.lstat()
            _require(stat.S_ISREG(attributes.st_mode) and not getattr(attributes, "st_file_attributes", 0) & 0x400
                     and target.read_bytes() == payload, "Refusing to replace an existing bibliography QA file")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        target = output / name
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/build/test-fixtures/bibliography")
    arguments = parser.parse_args()
    print(export_fixture(arguments.output_dir))

"""Byte-pinned case revisions for Python, physical-state and native compiler QA.

The proposed cases are isolated authored fixtures, not edits of curricular
assets. Historical aggregates are complete executions; no fixture claims that
one aggregate receipt proves its individual members.
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
from unittest.mock import patch

import stable_state_layout as layout
import compiler_case_order_fixture_source as order_fixtures
import verify_assetcheck_editor_references_cpp23 as verifier


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "2a470e0731c72054c384c3dcc8943f0fd8605613"
PREFIX = "app/src/main/assets/content/es/programming/programming_practice_tools/proyecto_assetcheck/"
SOURCES = {
    "u4l4": {"file": "lesson_leer_y_validar_un_registro.json", "id": "block_0005_0002",
              "reference": "block_0006_0002", "function": "tamano_valido", "end": "\nint main()",
              "sha256": "25358f6748add1cba92d891690d9dab1a53c036f88f6b5d13dce55eeb960edb8",
              "referenceSha256": "5a0e2c7ec860e9c6419f2d42578387c6e21ac842c2d46c1b05186882013c1a13"},
    "u4l6": {"file": "lesson_leer_manifiesto_y_publicar_informe.json", "id": "block_0006_0002",
              "reference": "block_0007_0002", "function": "contar_lineas", "end": "\nstd::string datos(",
              "sha256": "5379136f740d584cc715a575ef446da366f5ba9788681d0b87f3372029c367e6",
              "referenceSha256": "2ab36bda4d0a1279f13dc68ab4d37e0f3076ddea588171b62f3847bcaccbdac5"},
    "u4l7": {"file": "lesson_disenar_probar_y_presentar_variante_propia.json", "id": "block_0006_0003",
              "reference": "block_0007_0002", "function": "contar_tipos", "end": "\nint main()",
              "sha256": "c2f47d0b21244feaa54c335849369dd0029ef4f152448798d9ff4698616cf671",
              "referenceSha256": "9ea517e0ca1eebed13d5d06a25cbc06a328c71ac9fc93e6bf8f957748dd8756f"},
}
OWNER = order_fixtures.SYNTHETIC_OWNER
NOTE = order_fixtures.SYNTHETIC_NOTE


@lru_cache(maxsize=None)
def historical_bytes(name: str) -> bytes:
    spec = SOURCES[name]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1",
                       GIT_TERMINAL_PROMPT="0")
    raw = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{COMMIT}:{PREFIX}{spec['file']}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if hashlib.sha256(raw).hexdigest() != spec["sha256"]:
        raise ValueError("Case revision fixture differs from its complete pinned publication")
    return raw


def published(name: str) -> dict:
    document = json.loads(historical_bytes(name))
    layout.validate(document)
    owner = layout._blocks(document)[SOURCES[name]["id"]]
    if len(owner["outputCheck"]["cases"]) != 4:
        raise ValueError("The pinned case revision predecessor must preserve four historical cases")
    return document


class _PinnedContent:
    """Give the existing reference assembler its exact pinned input, without a checkout copy."""
    def __init__(self, filename: str, source: str):
        self.filename, self.source = filename, source
    def __truediv__(self, filename):
        if filename != self.filename:
            raise ValueError("Reference assembler requested a different publication")
        return self
    def read_text(self, *, encoding):
        if encoding != "utf-8":
            raise ValueError("Reference assembler changed its source encoding")
        return self.source


def reference_source(name: str) -> str:
    spec = SOURCES[name]
    source = historical_bytes(name).decode("utf-8")
    with patch.object(verifier, "CONTENT", _PinnedContent(spec["file"], source)):
        reference, initial, cases = verifier.lesson_reference(spec["file"], spec["function"], spec["end"])
    owner = layout._blocks(json.loads(source))[spec["id"]]
    if initial != owner["initialCode"] or cases != owner["outputCheck"]["cases"]:
        raise ValueError("Reference assembly changed its original compiler or historical cases")
    if hashlib.sha256(reference.encode("utf-8")).hexdigest() != spec["referenceSha256"]:
        raise ValueError("Case revision reference differs from its independently checked complete source")
    return reference


def authored_fields(document: dict, identity: str, cases: list[dict]) -> list[dict]:
    """Fixture authors explicitly preserve retained guides and mark each new output guide."""
    old = layout._blocks(document)[identity]["outputCheck"]["cases"]
    prior_fields = layout._compiler_case_order_fields(document, identity)
    view = deepcopy(document)
    layout._blocks(view)[identity]["outputCheck"]["cases"] = deepcopy(cases)
    result = []
    for index, case in enumerate(cases):
        prior = next((i for i, original in enumerate(old) if original == case), None)
        if prior is not None:
            prefix = f"/outputCheck/cases/{prior}/"
            for field in prior_fields:
                if field["path"].startswith(prefix):
                    preserved = deepcopy(field)
                    preserved["path"] = f"/outputCheck/cases/{index}/" + field["path"][len(prefix):]
                    result.append(preserved)
        elif "explanation" in case:
            source = case["explanation"]
            selection = case["outputs"][0].rstrip("\n") if source.startswith("La salida esperada es ") else "Valor"
            spans = [layout.text_semantics.select(source, selection, "output", reading="natural")]
            result.append(layout.text_semantics.make_field(view, identity, f"/outputCheck/cases/{index}/explanation", spans))
    return result


def individual_cases(name: str, document: dict) -> list[dict]:
    """The three QA proposals are checked independently; this is not an evidence migration rule."""
    old = layout._blocks(document)[SOURCES[name]["id"]]["outputCheck"]["cases"]
    individual = []
    for group in old:
        outputs = group["outputs"][0].splitlines(keepends=True)
        if name != "u4l7":
            inputs = [token + "\n" for token in group["standardInput"].split()]
        else:
            rows = group["standardInput"].splitlines(keepends=True)
            if int(rows[0]) != len(outputs):
                raise ValueError("Pinned collection count changed")
            inputs, cursor = [], 1
            for _ in outputs:
                end = cursor + int(rows[cursor]) + 1
                inputs.append("1\n" + "".join(rows[cursor:end]))
                cursor = end
            if cursor != len(rows):
                raise ValueError("Pinned collection framing changed")
        if len(inputs) != len(outputs):
            raise ValueError("Pinned inputs and independent output rows changed")
        for stdin, stdout in zip(inputs, outputs, strict=True):
            if len(inputs) == 1:
                individual.append(deepcopy(group))
            else:
                individual.append({"standardInput": stdin, "outputs": [stdout],
                    "explanation": "La salida esperada es " + stdout.rstrip("\n") + "."})
    if name in {"u4l6", "u4l7"}:
        individual.insert(0, individual.pop(1))
    if name == "u4l7":
        # Old cases 2/3 already are individual collections. Retain each only
        # once; duplicating their inputs would make the evaluator ambiguous.
        individual.extend(deepcopy(old[:2]))
    if len(individual) != {"u4l4": 16, "u4l6": 9, "u4l7": 8}[name]:
        raise ValueError("The authored independent case count changed")
    return individual


def revised(document: dict, identity: str, cases: list[dict], *, return_to=None) -> dict:
    return layout.revise_compiler_cases(document, identity, cases,
        case_semantics=authored_fields(document, identity, cases), return_to=return_to)


def synthetic_cases(document: dict) -> list[dict]:
    old = layout._blocks(document)[OWNER]["outputCheck"]["cases"]
    return [deepcopy(old[2]), {"standardInput": "4\n", "outputs": ["4\n"],
            "standardErrorExact": "", "exitCode": 0, "explanation": "Valor observado en una entrada nueva."},
            deepcopy(old[0]), deepcopy(old[1])]


def build_fixtures() -> tuple[dict, dict[str, str]]:
    manifest, payloads = {"version": 1, "cases": [], "invalid": []}, {}
    def put(filename, document, *, raw=None):
        payloads[filename] = raw.decode("utf-8") if raw is not None else json.dumps(document, ensure_ascii=False, indent=2) + "\n"
        return filename
    def add(name, identity, before, stages, *, real=False, prefix=None, reference=None, reference_block=None):
        prefix = name if prefix is None else prefix
        before_file = put(prefix + ".before.json", before, raw=historical_bytes(prefix) if real else None)
        history = []
        previous = before
        for index, stage in enumerate(stages):
            layout.validate_transition(previous, stage)
            filename = prefix + ".after.json" if index == len(stages) - 1 else prefix + f".stage{index+1}.json"
            history.append({"source": put(filename, stage)})
            previous = stage
        entry = {"name": name, "id": identity, "before": before_file, "after": history[-1]["source"],
                 "history": history, "real": real}
        if reference is not None:
            filename = prefix + ".reference.cpp"
            payloads[filename] = reference
            entry.update(reference=filename, referenceBlock=reference_block,
                         referenceSha256=hashlib.sha256(reference.encode("utf-8")).hexdigest())
        manifest["cases"].append(entry)
    for name, spec in SOURCES.items():
        before = published(name)
        after = revised(before, spec["id"], individual_cases(name, before))
        add("real-" + name, spec["id"], before, [after], real=True, prefix=name,
            reference=reference_source(name), reference_block=spec["reference"])
    # These are complete old publications, not a reconstructed expansion.
    # Authenticate both sources and the closed archive; only the new 21 -> 20
    # case revision is authored and transition-checked by this fixture.
    original = order_fixtures.published(order_fixtures.ORIGINAL_COMMIT)
    expanded = order_fixtures.published()
    expanded_owner = order_fixtures.EXPANDED_OWNER
    archived = next(item["block"] for item in expanded["stateLayout"]["compilerRevisions"]
                    if item["id"] == expanded_owner)
    original_block = layout._blocks(original)[expanded_owner]
    expanded_cases = layout._blocks(expanded)[expanded_owner]["outputCheck"]["cases"]
    if archived != original_block or len(archived["outputCheck"]["cases"]) != 19 or len(expanded_cases) != 21:
        raise ValueError("The complete published 19 -> 21 compiler archive changed")
    expanded_after = revised(expanded, expanded_owner,
                             [deepcopy(expanded_cases[20]), *deepcopy(expanded_cases[:19])])
    layout.validate_transition(expanded, expanded_after)
    prefix = "u11l7-closed-cases"
    first = put(prefix + ".before.json", original,
                raw=order_fixtures.historical_bytes(order_fixtures.ORIGINAL_COMMIT))
    middle_file = put(prefix + ".expanded.json", expanded,
                      raw=order_fixtures.historical_bytes(order_fixtures.CURRENT_COMMIT))
    last = put(prefix + ".after.json", expanded_after)
    manifest["cases"].append({"name": "real-u11-closed-expansion", "id": expanded_owner,
                             "before": first, "after": last, "real": True,
                             "history": [{"source": middle_file}, {"source": last}]})
    before = order_fixtures.synthetic_publication()
    cases = synthetic_cases(before)
    after = revised(before, OWNER, cases)
    add("synthetic-optional-becomes-first", OWNER, before, [after])
    first_order = layout.reorder_compiler_cases(before, OWNER, [1, 2, 0])
    middle = revised(first_order, OWNER, synthetic_cases(first_order))
    last_order = layout.reorder_compiler_cases(middle, OWNER, [1, 0, 2, 3])
    add("synthetic-order-cases-order", OWNER, before, [first_order, middle, last_order])
    next_cases = deepcopy(cases) + [{"standardInput": "5\n", "outputs": ["5\n"],
                                  "explanation": "Valor observado en otra entrada nueva."}]
    twice = revised(after, OWNER, next_cases)
    add("synthetic-successive", OWNER, before, [after, twice])
    original_cases = layout._blocks(before)[OWNER]["outputCheck"]["cases"]
    absent_cases = [deepcopy(cases[1]), *deepcopy(original_cases[1:])]
    absent = revised(before, OWNER, absent_cases)
    recovered_cases = [deepcopy(original_cases[0]), *deepcopy(absent_cases)]
    recovered = layout.revise_compiler_cases(absent, OWNER, recovered_cases,
        case_semantics=authored_fields(before, OWNER, recovered_cases))
    add("synthetic-case-absent-then-recovered", OWNER, before, [absent, recovered])
    restored = layout.revise_compiler_cases(after, OWNER, original_cases,
        case_semantics=layout._compiler_case_order_fields(before, OWNER), return_to=0)
    add("synthetic-return", OWNER, before, [after, restored])
    formatted = order_fixtures.formatted(before, OWNER)
    add("synthetic-format-before", OWNER, formatted, [revised(formatted, OWNER, synthetic_cases(formatted))])
    add("synthetic-format-after", OWNER, before, [after, order_fixtures.formatted(after, OWNER)])
    retired = layout.retire(before, [NOTE])
    add("synthetic-retire-before", OWNER, retired, [revised(retired, OWNER, synthetic_cases(retired))])
    add("synthetic-retire-after", OWNER, before, [after, layout.retire(after, [NOTE])])
    add("synthetic-later-guidance", OWNER, after, [order_fixtures.revised_guidance(after, OWNER)])
    repainted = deepcopy(after)
    field = next(field for field in repainted["textSemantics"]["fields"]
                 if field["block"] == OWNER and field["path"] == "/outputCheck/cases/0/explanation")
    field["spans"][0]["role"], field["spans"][0]["reading"] = "data", "characters"
    add("synthetic-later-marks", OWNER, after, [repainted])
    project = order_fixtures.synthetic_publication(project=True)
    channels = deepcopy(layout._blocks(project)[OWNER]["outputCheck"]["cases"])
    channels[0].update(standardErrorExact="Required diagnostic\n", exitCode=2, target="beta")
    add("synthetic-new-runtime-requirements", OWNER, project, [revised(project, OWNER, channels)])

    def invalid(name, source, mutate):
        bad = deepcopy(source)
        mutate(bad)
        try:
            layout.validate(bad)
        except (ValueError, TypeError, KeyError, IndexError):
            manifest["invalid"].append({"name": name, "file": put("invalid-" + name + ".json", bad)})
        else:
            raise AssertionError("Invalid case revision was accepted: " + name)
    record = lambda document: document["stateLayout"]["compilerCaseRevisions"][0]
    block = lambda document: layout._blocks(document)[OWNER]
    invalid("missing-journal", after, lambda d: d["stateLayout"].pop("compilerCaseRevisions"))
    invalid("unknown-owner", after, lambda d: record(d).update(id="missing_owner"))
    invalid("foreign-owner", after, lambda d: record(d).update(id=NOTE))
    invalid("missing-before", after, lambda d: record(d).pop("block"))
    invalid("unknown-field", after, lambda d: record(d).update(extra=True))
    invalid("floating-edge", after, lambda d: record(d).update(editorialIndex=float(record(d)["editorialIndex"])))
    invalid("boolean-edge", after, lambda d: record(d).update(editorialIndex=False))
    invalid("wrong-prefix", after, lambda d: record(d).update(historySha256="0"*64))
    invalid("duplicate-record", after, lambda d: d["stateLayout"]["compilerCaseRevisions"].append(deepcopy(record(d))))
    invalid("empty-cases", after, lambda d: record(d).update(cases=[]))
    invalid("non-list-cases", after, lambda d: record(d).update(cases={}))
    invalid("duplicate-input", after, lambda d: record(d)["cases"].append(deepcopy(record(d)["cases"][0])))
    invalid("floating-exit-code", after, lambda d: record(d)["cases"][0].update(exitCode=0.0))
    invalid("boolean-exit-code", after, lambda d: record(d)["cases"][0].update(exitCode=False))
    invalid("unsupported-case-field", after, lambda d: record(d)["cases"][0].update(answer=True))
    invalid("changed-before-program", after, lambda d: record(d)["block"].update(initialCode="int main(){return 7;}\n"))
    invalid("changed-after-program", after, lambda d: block(d).update(initialCode="int main(){return 7;}\n"))
    invalid("changed-comparison", after, lambda d: block(d)["outputCheck"].update(inputComparison="tokens"))
    invalid("changed-cmake", after, lambda d: block(d).update(cmake={"target":"foreign"}))
    invalid("missing-semantic-after", after, lambda d: record(d).pop("semanticAfter"))
    invalid("semantic-before-foreign", after, lambda d: record(d)["semanticBefore"][0].update(block=NOTE))
    invalid("semantic-after-stale", after, lambda d: record(d)["semanticAfter"][0].update(sourceSha256="0"*64))
    invalid("semantic-after-hidden", after, lambda d: record(d)["semanticAfter"][0].update(path="/outputCheck/cases/0/outputs/0"))
    invalid("semantic-after-floating-offset", after, lambda d: record(d)["semanticAfter"][0]["spans"][0].update(start=0.0))
    invalid("foreign-editorial-edge", last_order, lambda d: record(d).update(editorialIndex=0))
    invalid("overlapping-order-edge", last_order,
            lambda d: d["stateLayout"]["compilerCaseOrderRevisions"][0].update(editorialIndex=record(d)["editorialIndex"]))
    invalid("lost-old-order-journal", last_order, lambda d: d["stateLayout"].pop("compilerCaseOrderRevisions"))
    for field, value in (("outputs", ["Changed\n"]), ("standardErrorExact", "Changed\n"), ("exitCode", 1)):
        def later(document, field=field, value=value):
            old_hash = layout.fingerprint(block(document))
            block(document)["outputCheck"]["cases"][0][field] = value
            document["stateLayout"]["editorial"].append({"id": OWNER, "beforeSha256": old_hash,
                                                        "afterSha256": layout.fingerprint(block(document))})
        invalid("later-unarchived-" + field, after, later)
    return manifest, payloads


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    manifest, payloads = build_fixtures()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for filename, source in payloads.items():
        (args.output_dir / filename).write_text(source, encoding="utf-8", newline="\n")
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Prepared {len(manifest['cases'])} case revision fixtures and {len(manifest['invalid'])} rejected variants")


if __name__ == "__main__":
    main()

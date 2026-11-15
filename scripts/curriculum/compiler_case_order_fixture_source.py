"""Authenticated compiler case-order fixtures for Python and Windows import QA.

Real publications are pinned as complete Git bytes. Derived fixtures use the
public authoring API; published assets and student data are never rewritten.
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

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
CURRENT_COMMIT = "f2f86fc62c8d589a1f3c7727c09fb49a729e50e2"
ORIGINAL_COMMIT = "f410040c7a2b36468f36b15f6d04be7a28b347a7"
ASSET = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
         "texto_interpretacion_y_flujos/lesson_construir_un_lector_de_entrada_preparada.json")
SOURCE_DIGESTS = {
    CURRENT_COMMIT: "5f0b750fed94e1200c9a7c9be7f1247b2396ac86556685dab9abed4c3a7bed88",
    ORIGINAL_COMMIT: "9e569e76b0450625bfa15a4a8a8c6ba9ab2fbb006047e1e6ab651636f0382ea8",
}
OWNER = "u11_movimientos_compensados_en_los_extremos_comprobacion"
REFERENCE = "u11_movimientos_compensados_en_los_extremos_referencia_codigo"
EXPANDED_OWNER = "block_0003_0005"
SYNTHETIC_OWNER = "block_0000_0001"
SYNTHETIC_NOTE = "block_0000_0004"
SOURCE = ('#include <iostream>\n\nint main() {\n'
          '    int value{};\n    if (std::cin >> value) std::cout << value << "\\n";\n'
          '    return 0;\n}\n')


@lru_cache(maxsize=None)
def historical_bytes(commit: str) -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1",
                       GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{commit}:{ASSET}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if hashlib.sha256(payload).hexdigest() != SOURCE_DIGESTS[commit]:
        raise ValueError("Pinned U11L7 source differs from its complete published Git bytes")
    return payload


def published(commit: str = CURRENT_COMMIT) -> dict:
    document = json.loads(historical_bytes(commit))
    layout.validate(document)
    return document


def synthetic_publication(*, marked: bool = True, project: bool = False) -> dict:
    cases = [{"standardInput": f"{value}\n", "outputs": [f"{value}\n"],
              "standardErrorExact": "", "exitCode": 0,
              "explanation": "Valor observado en esta ejecución.",
              "reviewFeedback": [{"outputs": ["0\n"], "text": "Valor diferente al esperado."}]}
             for value in (1, 2, 3)]
    compiler = {"type": "cpp_compiler", "language": "cpp", "initialCode": SOURCE,
                "initialStandardInput": "1\n", "outputCheck": {"inputComparison": "exact", "cases": cases}}
    if project:
        compiler["cmake"] = {"target": "alpha", "buildType": "Debug"}
        compiler["projectFiles"] = [
            {"path": "CMakeLists.txt", "initialCode":
             "cmake_minimum_required(VERSION 3.20)\nproject(CaseOrder LANGUAGES CXX)\n"
             "add_executable(alpha main.cpp)\nadd_executable(beta main.cpp)\n"},
            {"path": "main.cpp", "initialCode": SOURCE}]
        cases[0]["target"], cases[1]["target"], cases[2]["target"] = "alpha", "beta", "alpha"
    document = {"id": "compiler_case_order_qa_project" if project else "compiler_case_order_qa",
                "title": "Orden de comprobaciones", "pages": [{"blocks": [
                    {"type": "multiple_choice", "question": "Selecciona la primera opción.",
                     "options": ["Primera", "Segunda"], "correctIndex": 0, "explanation": "Es la primera opción."},
                    compiler,
                    {"type": "paragraph", "text": "Referencia posterior a la comprobación."},
                    {"type": "code", "language": "cpp", "code": SOURCE},
                    {"type": "note", "text": "Nota independiente que conserva su archivo histórico."}]}]}
    result = layout.prepare_v2(document, "es", {"es": document})
    result["referenceAfter"] = [{"activity": SYNTHETIC_OWNER, "blocks": ["block_0000_0002", "block_0000_0003"]}]
    if marked:
        fields = []
        for index, (role, reading) in enumerate((("output", "natural"), ("cpp", "technical"), ("data", "natural"))):
            for suffix in ("explanation", "reviewFeedback/0/text"):
                path = f"/outputCheck/cases/{index}/{suffix}"
                source = layout.text_semantics.public_text_fields(layout._blocks(result)[SYNTHETIC_OWNER])[path]
                fields.append(layout.text_semantics.make_field(result, SYNTHETIC_OWNER, path,
                    [layout.text_semantics.select(source, "Valor", role, reading=reading)]))
        result["textSemantics"] = {"version": 1, "fields": fields, "rootFields": [
            layout.text_semantics.make_root_field(result, "/title", [])]}
    layout.validate(result)
    return result


def formatted(document: dict, identity: str) -> dict:
    changed = deepcopy(document)
    owner = layout._blocks(changed)[identity]
    if "projectFiles" in owner:
        for source in owner["projectFiles"]:
            source["initialCode"] = source["initialCode"].replace("\n", "\n\n")
    owner["initialCode"] = owner["initialCode"].replace("\n", "\n\n")
    return layout.prepare_v2(changed, "es", {"es": document}, document)


def revised_guidance(document: dict, identity: str) -> dict:
    changed = deepcopy(document)
    owner = layout._blocks(changed)[identity]
    owner["outputCheck"]["cases"][0]["explanation"] = "Valor observado después de revisar la guía."
    path = "/outputCheck/cases/0/explanation"
    for field in changed.get("textSemantics", {}).get("fields", []):
        if field["block"] == identity and field["path"] == path:
            field.update(layout.text_semantics.make_field(changed, identity, path,
                [layout.text_semantics.select(owner["outputCheck"]["cases"][0]["explanation"],
                                              "Valor", "output", reading="natural")]))
    return layout.prepare_v2(changed, "es", {"es": document}, document)


def negative_documents(after: dict) -> dict[str, dict]:
    result = {}
    def altered(name, operation, *, refresh=False):
        bad = deepcopy(after)
        operation(bad, bad["stateLayout"]["compilerCaseOrderRevisions"][-1], layout._blocks(bad)[OWNER])
        if refresh:
            bad["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(layout._blocks(bad)[OWNER])
        try:
            layout.validate(bad)
        except (ValueError, KeyError, TypeError, IndexError):
            result[name] = bad
        else:
            raise AssertionError("Standalone negative was accepted: " + name)
    altered("duplicate-index", lambda d,r,b: r["order"].__setitem__(1, r["order"][0]))
    altered("missing-index", lambda d,r,b: r["order"].pop())
    altered("out-of-range", lambda d,r,b: r["order"].__setitem__(0, 22))
    altered("negative-index", lambda d,r,b: r["order"].__setitem__(0, -1))
    altered("boolean-index", lambda d,r,b: r["order"].__setitem__(1, False))
    altered("floating-index", lambda d,r,b: r["order"].__setitem__(1, 0.0))
    altered("identity-order", lambda d,r,b: r.__setitem__("order", list(range(22))))
    altered("foreign-owner", lambda d,r,b: r.__setitem__("id", EXPANDED_OWNER))
    altered("unknown-owner", lambda d,r,b: r.__setitem__("id", "missing_compiler"))
    altered("missing-history", lambda d,r,b: d["stateLayout"].pop("compilerCaseOrderRevisions"))
    altered("duplicate-history", lambda d,r,b: d["stateLayout"]["compilerCaseOrderRevisions"].append(deepcopy(r)))
    altered("floating-editorial-index", lambda d,r,b: r.__setitem__("editorialIndex", float(r["editorialIndex"])))
    altered("foreign-editorial-edge", lambda d,r,b: r.__setitem__("editorialIndex", 0))
    altered("wrong-prefix", lambda d,r,b: r.__setitem__("historySha256", "0"*64))
    altered("changed-predecessor-source", lambda d,r,b: r["block"].__setitem__("initialCode", SOURCE))
    altered("unpublished-predecessor-guide", lambda d,r,b: r["block"]["outputCheck"]["cases"][21].__setitem__("explanation", "Guía no publicada."))
    altered("changed-input", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("standardInput", "0:0"), refresh=True)
    altered("changed-output", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("outputs", ["Falso\n"]), refresh=True)
    altered("changed-exit-code", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("exitCode", 1), refresh=True)
    altered("floating-exit-code", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("exitCode", 0.0), refresh=True)
    altered("changed-stderr", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("standardErrorExact", "Nuevo\n"), refresh=True)
    altered("changed-comparison", lambda d,r,b: b["outputCheck"].__setitem__("inputComparison", "tokens"), refresh=True)
    altered("changed-target", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("target", "foreign"), refresh=True)
    altered("changed-program", lambda d,r,b: b.__setitem__("initialCode", b["initialCode"].replace("return 0;", "return 1;")), refresh=True)
    altered("changed-guidance", lambda d,r,b: b["outputCheck"]["cases"][0].__setitem__("explanation", "Guía sustituida dentro de la permutación."), refresh=True)
    altered("lost-case", lambda d,r,b: b["outputCheck"]["cases"].pop(), refresh=True)
    altered("semantic-stale-source", lambda d,r,b: r["semanticBefore"][0].__setitem__("sourceSha256", "0"*64))
    altered("semantic-foreign-owner", lambda d,r,b: r["semanticBefore"][0].__setitem__("block", EXPANDED_OWNER))
    altered("semantic-hidden-oracle", lambda d,r,b: r["semanticBefore"][0].__setitem__("path", "/outputCheck/cases/21/outputs/0"))
    altered("semantic-bad-role", lambda d,r,b: r["semanticBefore"][0]["spans"][0].__setitem__("role", "invented"))
    altered("semantic-floating-offset", lambda d,r,b: r["semanticBefore"][0]["spans"][0].__setitem__("start", 6.0))
    # Valid descriptor changes are not standalone negatives: later authoring
    # can change a mark without a case permutation. Transition tests cover loss
    # or invention DURING the permutation against its exact prior publication.
    return result


def build_fixtures() -> tuple[dict, dict[str, bytes]]:
    manifest = {"version": 1, "cases": [], "invalid": []}
    payloads = {}
    def put(name: str, document: dict, *, raw: bytes | None = None) -> str:
        filename = name + ".json"
        value = raw if raw is not None else (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
        if filename in payloads and payloads[filename] != value:
            raise ValueError("Fixture filename reused with different bytes")
        payloads[filename] = value
        return filename
    def case(name, before, after, identity, *, steps=None, order=None, real=False, reference=None,
             before_name=None, after_name=None, before_raw=None):
        layout.validate(before)
        layout.validate(after)
        first = put(before_name or name + ".before", before, raw=before_raw)
        last = put(after_name or name + ".after", after)
        histories = []
        for index, step in enumerate(steps or [after]):
            histories.append({"source": last if step == after else put(name + f".step{index+1}", step)})
        record = {"name": name, "id": identity, "before": first, "after": last,
                  "history": histories, "real": real}
        if order is not None: record["order"] = order
        if reference is not None: record["reference"] = reference
        manifest["cases"].append(record)

    current = published()
    original = published(ORIGINAL_COMMIT)
    original_owner = layout._blocks(original)[EXPANDED_OWNER]
    archive = next(item["block"] for item in current["stateLayout"]["compilerRevisions"] if item["id"] == EXPANDED_OWNER)
    assert original_owner == archive
    assert len(original_owner["outputCheck"]["cases"]) == 19
    assert len(layout._blocks(current)[EXPANDED_OWNER]["outputCheck"]["cases"]) == 21
    order = [21, *range(21)]
    after = layout.reorder_compiler_cases(current, OWNER, order)
    case("real-u11-six-movements", current, after, OWNER, order=order, real=True, reference=REFERENCE,
         before_name="u11l7.before", after_name="u11l7.after", before_raw=historical_bytes(CURRENT_COMMIT))
    second_order = [1, 2, 0, *range(3,22)]
    second = layout.reorder_compiler_cases(after, OWNER, second_order)
    case("real-u11-successive", current, second, OWNER, steps=[after,second],
         order=[order[index] for index in second_order], real=True, reference=REFERENCE)
    returned = layout.reorder_compiler_cases(after, OWNER, [*range(1,22),0], return_to=1)
    case("real-u11-return", current, returned, OWNER, steps=[after,returned],
         order=list(range(22)), real=True, reference=REFERENCE)
    expanded = layout.reorder_compiler_cases(current, EXPANDED_OWNER, [20,*range(20)])
    # The historical19 and current21 documents are both genuine publications.
    # Their old expansion/format composition is not authored by this fixture.
    # Only current21 -> reordered21 is a newly validated transition here.
    case("real-u11-expanded-order", original, expanded, EXPANDED_OWNER, steps=[current,expanded],
         real=True, reference="block_0005_0002", before_name="u11l7.original19", before_raw=historical_bytes(ORIGINAL_COMMIT))

    plain = synthetic_publication()
    reordered = layout.reorder_compiler_cases(plain, SYNTHETIC_OWNER, [2,0,1])
    case("synthetic-exact-descriptors", plain, reordered, SYNTHETIC_OWNER, order=[2,0,1])
    preformatted = formatted(plain, SYNTHETIC_OWNER)
    format_first = layout.reorder_compiler_cases(preformatted, SYNTHETIC_OWNER, [2,0,1])
    case("synthetic-format-before", preformatted, format_first, SYNTHETIC_OWNER, order=[2,0,1])
    format_after = formatted(reordered, SYNTHETIC_OWNER)
    case("synthetic-format-after", plain, format_after, SYNTHETIC_OWNER, steps=[reordered,format_after], order=[2,0,1])
    retired_first = layout.retire(plain, [SYNTHETIC_NOTE])
    order_after_retire = layout.reorder_compiler_cases(retired_first, SYNTHETIC_OWNER, [2,0,1])
    case("synthetic-retire-before", retired_first, order_after_retire, SYNTHETIC_OWNER, order=[2,0,1])
    retire_after = layout.retire(reordered, [SYNTHETIC_NOTE])
    case("synthetic-retire-after", plain, retire_after, SYNTHETIC_OWNER, steps=[reordered,retire_after], order=[2,0,1])
    guided = revised_guidance(reordered, SYNTHETIC_OWNER)
    case("synthetic-later-guidance", reordered, guided, SYNTHETIC_OWNER, order=[0,1,2])
    unmarked = synthetic_publication(marked=False)
    unmarked_order = layout.reorder_compiler_cases(unmarked, SYNTHETIC_OWNER, [1,2,0])
    later_marks = deepcopy(unmarked_order)
    later_marks["textSemantics"] = {"version":1, "fields":[layout.text_semantics.make_field(
        later_marks, SYNTHETIC_OWNER, "/outputCheck/cases/0/explanation", [])]}
    layout.validate_transition(unmarked_order, later_marks)
    case("synthetic-later-marks", unmarked_order, later_marks, SYNTHETIC_OWNER, order=[0,1,2])
    project = synthetic_publication(project=True)
    project_order = layout.reorder_compiler_cases(project, SYNTHETIC_OWNER, [1,2,0])
    case("synthetic-cmake-targets", project, project_order, SYNTHETIC_OWNER, order=[1,2,0])

    for name, bad in negative_documents(after).items():
        manifest["invalid"].append({"name": name, "file": put("invalid-" + name, bad)})
    # Keep the permutation's own edge intact. These later authored-looking
    # edges prove that the full channel/mode/target guard remains necessary.
    for name, seed, identity, operation in (
        ("later-stderr-change", after, OWNER,
         lambda block: block["outputCheck"]["cases"][0].__setitem__("standardErrorExact", "Nuevo\\n")),
        ("later-comparison-change", reordered, SYNTHETIC_OWNER,
         lambda block: block["outputCheck"].__setitem__("inputComparison", "tokens")),
        ("later-target-change", project_order, SYNTHETIC_OWNER,
         lambda block: block["outputCheck"]["cases"][0].__setitem__("target", "alpha")),
    ):
        bad = deepcopy(seed)
        owner = layout._blocks(bad)[identity]
        previous_hash = layout.fingerprint(owner)
        operation(owner)
        bad["stateLayout"]["editorial"].append({"id":identity, "beforeSha256":previous_hash,
                                               "afterSha256":layout.fingerprint(owner)})
        try:
            layout.validate(bad)
        except ValueError as error:
            assert "case, channel, comparison mode or program" in str(error), (name, error)
        else:
            raise AssertionError("A later edge rewrote the protected case contract")
        manifest["invalid"].append({"name":name, "file":put("invalid-"+name,bad)})
    # A nonidentity index list is insufficient when complete duplicate payloads
    # leave the block unchanged. This is never a valid authored revision.
    identical = deepcopy(unmarked)
    identical.pop("stateLayout")
    identical.pop("referenceAfter", None)
    duplicate_cases = identical["pages"][0]["blocks"][1]["outputCheck"]["cases"]
    duplicate_cases[1] = deepcopy(duplicate_cases[0])
    identical = layout.prepare_v2(identical, "es", {"es":identical})
    duplicate_owner = layout._blocks(identical)[SYNTHETIC_OWNER]
    signature = layout.fingerprint(duplicate_owner)
    identical["stateLayout"]["compilerCaseOrderRevisions"] = [{
        "id":SYNTHETIC_OWNER, "block":deepcopy(duplicate_owner), "order":[1,0,2],
        "editorialIndex":0, "historySha256":layout.fingerprint({"lessonId":identical["id"],
            "locale":"es", "editorial":[]}), "semanticBefore":[]}]
    identical["stateLayout"]["editorial"].append({"id":SYNTHETIC_OWNER,
        "beforeSha256":signature,"afterSha256":signature})
    try:
        layout.validate(identical)
    except ValueError:
        pass
    else:
        raise AssertionError("An unchanged duplicate-case payload acquired an editorial revision")
    manifest["invalid"].append({"name":"unchanged-duplicate-payload", "file":put("invalid-unchanged-duplicate-payload",identical)})
    return manifest, payloads


def export_fixtures(output_dir: Path) -> Path:
    manifest, payloads = build_fixtures()
    output = output_dir.resolve()
    curriculum = (ROOT / "app" / "src" / "main" / "assets").resolve()
    if output == ROOT.resolve() or output == curriculum or curriculum in output.parents:
        raise ValueError("Compiler case fixtures require an isolated QA output directory")
    payloads["manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        target = output / name
        if target.exists() and target.read_bytes() != payload:
            raise ValueError("A different existing QA fixture will not be overwritten: " + name)
    for name, payload in payloads.items():
        target = output / name
        if not target.exists():
            with target.open("xb") as stream: stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    print(export_fixtures(args.output_dir))

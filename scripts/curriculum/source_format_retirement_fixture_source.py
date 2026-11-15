"""Authenticated source-format/retirement fixtures for Python and Win32 QA.

Only isolated files under --output-dir are written. Published lesson bytes are
pinned independently; no curriculum, original fingerprint or student state is
changed. The manifest names the exact complete publication behind each history.
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
from test_code_format_compat import fixture, edited

ROOT = Path(__file__).resolve().parents[2]
COMMIT = "fce5e297b25be643a2167846e99a00b54135a1e8"
SOURCES = {
    "u7": (
        COMMIT,
        "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "errores_y_construccion_gradual/lesson_validar_antes_y_elegir_reporte.json",
        "d65de29e472e1f1518559c7e6e785d68943bf1378ff80f0b55278786bbd9f788",
    ),
    "diagnostics": (
        COMMIT,
        "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "funciones_y_contratos/lesson_declaraciones_y_firmas.json",
        "eaae140880a99040e71462b4989890c9944b42cc555cd7b729d91262ece6054e",
    ),
    "compiler": (
        "f410040c7a2b36468f36b15f6d04be7a28b347a7",
        "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json",
        "e137e259ee465676c74cbfe2cb17d4a91c2ad8d71b040dc0a1c2e9fface5000e",
    ),
}

for _base, _language, _sha256 in (
    ("diagnostics", "en", "8976a37af1ffd5359baf96e0ab4606b3d0b81c97b5d3d998a458df3ff348d716"),
    ("diagnostics", "fr", "c4397e0426ff28750c21ff4f805a3cbd7b238823958ba8b22643c6219dd2aa2d"),
    ("compiler", "en", "067b6be16cd36e5606d0c45a99ef50fa573c7f42812617df34243b7265e3f603"),
    ("compiler", "fr", "21d0dd8dd3394b10677a08994e19f2fd18a98d19ed64b29b232c81945412a6b2"),
):
    _commit, _path, _ = SOURCES[_base]
    SOURCES[_base + "-" + _language] = (
        _commit, _path.replace("/es/", "/" + _language + "/"), _sha256)

U7_ID = "cpp_fundamentals__errores_y_construccion_gradual__validar_antes_y_elegir_reporte"
RETIRED = "block_0000_0001"
QA_RETIRED = "source_format_retirement_qa_note"
COMPILER = "block_0004_0004"
REFERENCE = "block_0007_0001"


def verify_bytes(payload: bytes, name: str = "u7") -> bytes:
    if hashlib.sha256(payload).hexdigest() != SOURCES[name][2]:
        raise ValueError("Source-format retirement fixture differs from its pinned publication")
    return payload


@lru_cache(maxsize=None)
def historical_bytes(name: str = "u7") -> bytes:
    commit, relative, _ = SOURCES[name]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{commit}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), name)


def published(name: str = "u7") -> dict:
    document = json.loads(historical_bytes(name))
    layout.validate(document)
    return document


def history_block(document: dict, identity: str, index: int = 0) -> dict:
    snapshot = document["stateLayout"]["retirementHistory"][index]
    return next(block for page, record in zip(snapshot["pages"], snapshot["current"])
                for block, owner in zip(page["blocks"], record["blocks"]) if owner == identity)


def _set_source(block: dict, slot: str, source: str) -> None:
    parts = slot.split("/")
    target = block
    for part in parts[:-1]:
        target = target[int(part)] if part.isdecimal() else target[part]
    if isinstance(target, list):
        target[int(parts[-1])] = source
    else:
        target[parts[-1]] = source


def format_owners(document: dict, identities: list[str], *, family_name: str) -> dict:
    """Add physical separation only; the normal lexical guard authenticates it."""
    proposal = deepcopy(document)
    for identity in identities:
        owner = layout._blocks(proposal)[identity]
        for slot, (_, source) in layout.code_format.code_slots(owner).items():
            _set_source(owner, slot, "\n" + source)
    family = {"es": document, **{locale: published(family_name + "-" + locale)
                                for locale in ("en", "fr")}}
    return layout.prepare_v2(proposal, "es", family, document)


def with_qa_note(document: dict) -> dict:
    """Add one isolated presentation through the ordinary authenticated API.

    Retiring an already marked paragraph also requires independent semantic
    archival support. This note leaves every published block and descriptor
    intact so the format/history regression exercises only its intended scope.
    """
    meta = document["stateLayout"]
    reserved = max(len(source["origin"]) for source in meta["family"].values())
    storage_page = max([reserved] + [
        entry["storage"]["page"] + 1 for entry in meta["additions"]])
    plan = deepcopy(meta["current"])
    plan[0]["blocks"].append(QA_RETIRED)
    return layout.reorder(document, plan, [{
        "id": QA_RETIRED, "storage": {"page": storage_page, "block": 0},
        "progressPage": plan[0]["id"],
        "block": {"type": "paragraph", "text": "Presentación aislada para comprobar el archivo de lectura."},
    }])


def real_pair() -> tuple[dict, dict]:
    before = published()
    if (before["id"] != U7_ID or before["stateLayout"]["current"][0]["blocks"][1] != RETIRED
            or layout._blocks(before)[RETIRED]["type"] != "paragraph"
            or len(before["stateLayout"].get("sourceFormatRevisions", [])) != 6):
        raise ValueError("Pinned U7L3 identity or its published format archives changed")
    before = with_qa_note(before)
    after = layout.retire(before, [QA_RETIRED])
    layout.validate_transition(before, after)
    return before, after


def synthetic_publication(*, grouped: bool = False) -> tuple[dict, dict]:
    original = fixture()
    original["pages"][0]["blocks"].extend([
        {"type": "paragraph", "text": "Presentación histórica de QA."},
        {"type": "paragraph", "text": "Segunda presentación histórica de QA."},
    ])
    if grouped:
        original["pages"][0]["blocks"][2]["fragments"] = [
            "#include <iostream>\nint main(){", "return 0;\n}"]
        original["pages"][0]["blocks"][2]["correctOrder"] = [0, 1]
    before = layout.prepare_v2(original, "es", {"es": original})
    return before, {"es": deepcopy(original)}


def _include_historical_node(document: dict, identity: str, block: dict) -> None:
    """A negative can pass the chain check while its contract is still rejected.

    This deliberately corrupts a QA copy. It never represents an authorized
    publication: transition checks against the genuine predecessor reject it.
    """
    proofs = document["stateLayout"]["editorial"]
    at = next(i for i, proof in enumerate(proofs) if proof["id"] == identity)
    old = proofs[at]
    if set(old) != {"id", "beforeSha256", "afterSha256"}:
        raise ValueError("Negative fixture expects an ordinary original proof")
    value = layout.fingerprint(block)
    proofs[at:at + 1] = [
        {"id": identity, "beforeSha256": old["beforeSha256"], "afterSha256": value},
        {"id": identity, "beforeSha256": value, "afterSha256": old["afterSha256"]},
    ]


def negative_documents(document: dict) -> dict[str, tuple[dict, str | None]]:
    result = {}
    def make(name, owner, mutate, *, node=False, message=None):
        bad = deepcopy(document)
        block = history_block(bad, owner)
        mutate(block)
        if node:
            _include_historical_node(bad, owner, block)
        result[name] = bad, message

    chain_message = ("A historical formatted source needs an exact occurrence "
                     "in its owner's editorial chain")
    make("unpublished-whitespace", "block_0000_0000",
         lambda block: block.__setitem__("code", "\n" + block["code"]),
         message=chain_message)
    make("unpublished-guidance", "block_0000_0004",
         lambda block: block["outputCheck"]["cases"][0].__setitem__(
             "explanation", "Una guía que nunca se publicó."),
         message=chain_message)
    make("changed-token", "block_0000_0000",
         lambda block: block.__setitem__("code", block["code"].replace("number = 2", "number = 3")),
         node=True, message="Source formatting modified tokens, literals, comments or preprocessor lines")
    make("changed-answer", "block_0000_0001",
         lambda block: block.__setitem__("answers", ["3"]), node=True,
         message="Historical reading snapshot changed an activity contract")
    make("changed-oracle", "block_0000_0004",
         lambda block: block["outputCheck"]["cases"][0].__setitem__("outputs", ["incorrect"]),
         node=True, message="Historical reading snapshot changed an activity contract")
    make("changed-order", "block_0000_0002",
         lambda block: block.__setitem__("correctOrder", [3, 2, 1, 0]), node=True,
         message="Historical reading snapshot changed an activity contract")
    make("changed-project-path", "block_0000_0004",
         lambda block: block["projectFiles"][1].__setitem__("path", "different.cpp"),
         node=True)
    wrong_owner = deepcopy(document)
    replacement = deepcopy(history_block(wrong_owner, "block_0000_0000"))
    target = history_block(wrong_owner, "block_0000_0001")
    target.clear()
    target.update(replacement)
    result["borrowed-owner"] = wrong_owner, chain_message
    archive = deepcopy(document)
    archive["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] += "\nint forged = 1;\n"
    result["changed-original-archive"] = archive, None
    missing = deepcopy(document)
    del missing["stateLayout"]["sourceFormatRevisions"][0]
    result["missing-original-archive"] = missing, None
    broken = deepcopy(document)
    broken["stateLayout"]["editorial"][0]["beforeSha256"] = "0" * 64
    result["broken-editorial-chain"] = broken, None
    return result


def retired_owner_negative_documents(document: dict) -> dict[str, dict]:
    """Keep the new inactive-owner support bound to the same exact archive."""
    owner = "block_0000_0004"
    result = {}

    def make(name, mutate):
        bad = deepcopy(document)
        mutate(bad)
        result["retired-owner-" + name] = bad

    def retired(document):
        return next(item["block"] for item in document["stateLayout"]["retired"]
                    if item["id"] == owner)

    def forged_payload(document, mutate):
        block = retired(document)
        mutate(block)
        # Recomputing the final full-block hash cannot bless a changed token,
        # oracle or file path under the original immutable response contract.
        proofs = [item for item in document["stateLayout"]["editorial"]
                  if item["id"] == owner]
        proofs[-1]["afterSha256"] = layout.fingerprint(block)

    make("changed-token", lambda bad: forged_payload(bad, lambda block:
         block["projectFiles"][1].__setitem__("initialCode",
             block["projectFiles"][1]["initialCode"].replace("return 0", "return 1"))))
    make("changed-oracle", lambda bad: forged_payload(bad, lambda block:
         block["outputCheck"]["cases"][0].__setitem__("outputs", ["forged"])))
    make("changed-project-path", lambda bad: forged_payload(bad, lambda block:
         block["projectFiles"][1].__setitem__("path", "renamed.cpp")))
    make("missing-format-archive", lambda bad: bad["stateLayout"].__setitem__(
         "sourceFormatRevisions", [item for item in bad["stateLayout"]["sourceFormatRevisions"]
                                   if item["id"] != owner]))
    make("duplicate-format-archive", lambda bad:
         bad["stateLayout"]["sourceFormatRevisions"].append(deepcopy(next(
             item for item in bad["stateLayout"]["sourceFormatRevisions"] if item["id"] == owner))))
    make("missing-retired-owner", lambda bad: bad["stateLayout"].__setitem__("retired", []))
    make("unknown-format-owner", lambda bad: next(item for item in
         bad["stateLayout"]["sourceFormatRevisions"] if item["id"] == owner).__setitem__(
             "id", "unknown_formatted_owner"))
    make("duplicate-retired-owner", lambda bad:
         bad["stateLayout"]["retired"].append(deepcopy(bad["stateLayout"]["retired"][0])))

    def restore_active(bad):
        bad["pages"][0]["blocks"].append(deepcopy(retired(bad)))
        bad["stateLayout"]["current"][0]["blocks"].append(owner)
    make("active-and-retired", restore_active)

    def old_presentation(bad):
        original = next(item["block"] for item in bad["stateLayout"]["sourceFormatRevisions"]
                        if item["id"] == owner)
        actual = retired(bad)
        actual.clear()
        actual.update(deepcopy(original))
    make("stale-presentation", old_presentation)
    return result


def build_fixtures() -> tuple[dict[str, dict], dict]:
    documents = {}
    manifest = {"cases": [], "invalid": []}

    def put(name, document):
        filename = name + ".json"
        if filename in documents and documents[filename] != document:
            raise ValueError("A fixture name cannot replace another publication")
        documents[filename] = deepcopy(document)
        return filename

    def case(name, before, after, history, *, retired_compiler_cases=()):
        layout.validate(before)
        layout.validate(after)
        layout.validate_transition(before, after)
        metadata = after["stateLayout"]
        sources = []
        for index, source in enumerate(history):
            snapshot = metadata["retirementHistory"][index]
            assert snapshot["pages"] == source["pages"]
            assert snapshot["current"] == source["stateLayout"]["current"]
            sources.append({"index": index, "source": put(name + f"-history-{index}", source)})
        assert len(sources) == len(metadata.get("retirementHistory", []))
        manifest["cases"].append({
            "name": name, "before": put(name + "-before", before),
            "after": put(name + "-after", after),
            "retired": [entry["id"] for entry in metadata.get("retired", [])],
            "history": sources,
        })
        if retired_compiler_cases:
            manifest["cases"][-1]["retiredCompilerCases"] = [
                {"id": identity, "case": index} for identity, index in retired_compiler_cases]

    real_before, real_after = real_pair()
    case("source-format-retirement", real_before, real_after, [real_before])

    original, family = synthetic_publication()
    first = layout.prepare_v2(edited(original), "es", family, original)
    one_retirement = layout.retire(first, ["block_0000_0005"])
    case("synthetic-format-first", first, one_retirement, [first])

    owner_retired = layout.retire(first, ["block_0000_0004"])
    case("formatted-owner-retired", first, owner_retired, [first],
         retired_compiler_cases=[("block_0000_0004", 0)])
    code_retired = layout.retire(first, ["block_0000_0000"])
    case("formatted-code-owner-retired", first, code_retired, [first])
    later_proposal = deepcopy(code_retired)
    later_compiler = layout._blocks(later_proposal)["block_0000_0004"]
    later_compiler["initialCode"] = "\n" + later_compiler["initialCode"]
    later_compiler["projectFiles"][1]["initialCode"] += "\n"
    later = layout.prepare_v2(later_proposal, "es", family, code_retired)
    owners_retired = layout.retire(later, ["block_0000_0004"])
    case("formatted-owners-successive", later, owners_retired, [first, later],
         retired_compiler_cases=[("block_0000_0004", 0)])

    marked_source = fixture()
    marked_source["compilerPuzzlesRequired"] = True
    marked_source["pages"][0]["blocks"][4]["outputCheck"]["cases"][0]["explanation"] = (
        "La salida permanece vacía y el retorno es cero.")
    marked_family = {"es": deepcopy(marked_source)}
    marked_original = layout.prepare_v2(marked_source, "es", marked_family)
    marked = layout.prepare_v2(edited(marked_original), "es", marked_family, marked_original)
    marked_text = layout._blocks(marked)["block_0000_0004"]["outputCheck"]["cases"][0]["explanation"]
    marked["textSemantics"] = {"version": 1, "fields": [layout.text_semantics.make_field(
        marked, "block_0000_0004", "/outputCheck/cases/0/explanation", [
            layout.text_semantics.select(marked_text, "vacía", "output", reading="natural")])]}
    layout.validate(marked)
    marked_retired = layout.retire(marked, ["block_0000_0004"])
    case("formatted-marked-owner-retired", marked, marked_retired, [marked],
         retired_compiler_cases=[("block_0000_0004", 0)])

    retired_first = layout.retire(original, ["block_0000_0005"])
    formatted_later = layout.prepare_v2(edited(retired_first), "es", family, retired_first)
    case("synthetic-retire-first", original, formatted_later, [original])

    updated = deepcopy(one_retirement)
    updated["pages"][0]["blocks"][0]["code"] = "\n" + updated["pages"][0]["blocks"][0]["code"]
    updated["pages"][0]["blocks"][4]["outputCheck"]["cases"][0]["explanation"] = (
        "Conserva la entrada y compara la salida completa.")
    second = layout.prepare_v2(updated, "es", family, one_retirement)
    two_retirements = layout.retire(second, ["block_0000_0006"])
    case("synthetic-successive", second, two_retirements, [first, second])

    grouped, group_family = synthetic_publication(grouped=True)
    group_retired = layout.retire(grouped, ["block_0000_0005"])
    refined = layout.refine_order_lines(group_retired, "block_0000_0002")
    refined_format = layout.prepare_v2(edited(refined), "es", group_family, refined)
    refined_retired = layout.retire(refined_format, ["block_0000_0006"])
    case("refined-historical-order", refined_format, refined_retired, [grouped, refined_format])

    diagnostic_before = with_qa_note(published("diagnostics"))
    goals = ["block_0014_0002", "block_0015_0002", "block_0017_0002"]
    expected = ("u6-difference-unselected-definition", "u6-difference-conflicting-return",
                "u6-difference-missing-header")
    assert tuple(layout._blocks(diagnostic_before)[identity]["compileCheck"]["diagnostic"]
                 for identity in goals) == expected
    diagnostic_formatted = format_owners(diagnostic_before, goals, family_name="diagnostics")
    diagnostic_retired = layout.retire(diagnostic_formatted, [QA_RETIRED])
    case("reviewed-diagnostic-history", diagnostic_formatted, diagnostic_retired,
         [diagnostic_formatted])
    diagnostic_owner_retired = layout.retire(diagnostic_formatted, ["block_0017_0002"])
    case("formatted-diagnostic-owner-retired", diagnostic_formatted, diagnostic_owner_retired,
         [diagnostic_formatted])

    compiler_original = published("compiler")
    compiler_retired = layout.retire(compiler_original, [RETIRED])
    expanded_after_retirement = layout.revise_compiler_cases(compiler_retired, COMPILER)
    formatted_after_expansion = format_owners(expanded_after_retirement, [COMPILER, REFERENCE], family_name="compiler")
    case("original-before-expansion", expanded_after_retirement, formatted_after_expansion,
         [compiler_original])

    expanded = layout.revise_compiler_cases(compiler_original, COMPILER)
    expanded_retired = layout.retire(expanded, [RETIRED])
    formatted_after_retirement = format_owners(expanded_retired, [COMPILER, REFERENCE], family_name="compiler")
    case("expanded-before-formatting", expanded_retired, formatted_after_retirement, [expanded])

    expanded_formatted = format_owners(expanded, [COMPILER, REFERENCE], family_name="compiler")
    after_expanded_format = layout.retire(expanded_formatted, [RETIRED])
    case("formatted-expanded-history", expanded_formatted, after_expanded_format,
         [expanded_formatted])

    for name, bad in retired_owner_negative_documents(owner_retired).items():
        manifest["invalid"].append({"name": name, "file": put("invalid-" + name, bad)})

    for name, (bad, message) in negative_documents(one_retirement).items():
        record = {"name": name, "file": put("invalid-" + name, bad)}
        if message:
            record["message"] = message
        manifest["invalid"].append(record)

    return documents, manifest


def export_fixtures(output_dir: Path) -> Path:
    documents, manifest = build_fixtures()
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in documents.items():
        (output_dir / name).write_text(
            json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
            encoding="utf-8", newline="\n")
    destination = output_dir / "manifest.json"
    destination.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8", newline="\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    print(export_fixtures(arguments.output_dir))

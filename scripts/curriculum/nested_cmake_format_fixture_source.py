"""Export authentic nested-CMake format revisions for Windows reader QA.

The three Spanish baselines are complete, byte-pinned publications. Their
English/French siblings are read only to authenticate the existing family
through prepare_v2. Every positive edit is one leading LF in an existing
CMakeLists.txt. No publication, origin, starter, answer or old proof is rewritten.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import dataclass
import json
from pathlib import Path

import code_format_compat as source_format
from diagnostic_fixture_source import checked_output, existing_matches
from format_batch_history import (
    Publication, active_owners, publication_bytes, verify_publication_bytes,
)
import stable_state_layout as state


ROOT = Path(__file__).resolve().parents[2]
BASELINE_COMMIT = "c0d20f63c846ec7b075b70ee5c403336c78498c6"
RELATIVE_UNIT = "programming/programming_practice_tools/comprobaciones_repetibles"
PUBLICATIONS = {
    "datos_y_listas_de_objetivos": {
        "es": "8d0305f7b3649e1960ea7bb3b1dec875f52560d4f0a3b2fc9ac02c948ffacca4",
        "en": "7a809be9d0d0943497c53d90af89a66cbbf26cfbee030031bbf29db3897b3858",
        "fr": "bfe2e624abf3c7f07573db813e98ccfa1feaa891ca8666de2376558c155194a6",
    },
    "nombrar_reglas_y_separar_acuerdos": {
        "es": "13b529b6e65afc634b485792661b6d7d7df1c48bbe8b2099d943e9ae3b5fec18",
        "en": "41976c8efd968641b20fb0bc248b9b79740fcd099238c8176db9fe3c2bbab4f7",
        "fr": "cad1bde698b96fb63a7634bea91b470f075712c6edcc972afcc424dddf607a0c",
    },
    "reorganizar_la_descripcion_conservando_el_comprobador": {
        "es": "9f3280f719a7967d3e713309fdb29d9b15ac29d84dd7d385fc4e2b3e34be954e",
        "en": "fdb00f957b2417f321d9138737fda741a83b5d7e28ba0221ba3989854e63d760",
        "fr": "1044d8e42039dcba3c2553d2fa3c83030ad7521bd9722152318944fcdca208e9",
    },
}


def _edit(path: str, reference: str, reference_path: str | None = None) -> dict:
    result = {"path": path, "referenceActivity": reference}
    if reference_path is not None:
        result["referencePath"] = reference_path
    return result


def _case(lesson: str, activity: str, edits: list[dict],
          path: str = "componente/CMakeLists.txt") -> dict:
    return {"lesson": lesson, "activity": activity, "path": path,
            "executionEdits": edits}


# References bind execution to published sources, not to invented solutions.
# The unmodified before/after lessons retain their intentionally broken starters.
CASES = (
    _case("datos_y_listas_de_objetivos", "block_0003_0004", [
        _edit("CMakeLists.txt", "block_0004_0001")]),
    _case("datos_y_listas_de_objetivos", "block_0004_0007", [
        _edit("CMakeLists.txt", "block_0004_0001")]),
    _case("datos_y_listas_de_objetivos", "block_0006_0004", [
        _edit("CMakeLists.txt", "block_0007_0001")]),
    _case("datos_y_listas_de_objetivos", "block_0008_0004", [
        _edit("CMakeLists.txt", "block_0009_0001")], "codigo/lotes/CMakeLists.txt"),
    _case("nombrar_reglas_y_separar_acuerdos", "block_0002_0004", [
        _edit("componente/src/cajas.cpp", "block_0003_0002")]),
    _case("nombrar_reglas_y_separar_acuerdos", "block_0005_0006", [
        _edit("componente/src/cajas.cpp", "block_0006_0001"),
        _edit("pruebas/cajas_casos.cpp", "block_0006_0003"),
        _edit("componente/publico/envio/cajas.h", "block_0006_0009",
              "componente/publico/envio/cajas.h")]),
    _case("nombrar_reglas_y_separar_acuerdos", "block_0006_0009", [
        _edit("componente/src/cajas.cpp", "block_0006_0001")]),
    _case("reorganizar_la_descripcion_conservando_el_comprobador", "block_0004_0012", [
        _edit("componente/src/cajas.cpp", "block_0003_0004", "componente/src/cajas.cpp")]),
    _case("reorganizar_la_descripcion_conservando_el_comprobador", "block_0006_0003", [
        _edit("componente/CMakeLists.txt", "block_0007_0003")]),
    _case("reorganizar_la_descripcion_conservando_el_comprobador", "block_0010_0003", []),
)

FAKE_OWNER = "qa_nested_cmake_fake_name"
FAKE_PATH = "componente/CMakeLists.txt.bak"
NATIVE_SOURCE_ERROR = "Source formatting modified tokens, literals, comments or preprocessor lines"
NATIVE_PATH_ERROR = "Source formatting renamed a project file"
INVALID_MESSAGES = {
    "logic": NATIVE_SOURCE_ERROR,
    "path": NATIVE_PATH_ERROR,
    "literal": NATIVE_SOURCE_ERROR,
    "filename": NATIVE_SOURCE_ERROR,
}


def publication_path(name: str, locale: str = "es") -> str:
    if name not in PUBLICATIONS or locale not in PUBLICATIONS[name]:
        raise ValueError("Unknown nested-CMake publication")
    return f"app/src/main/assets/content/{locale}/{RELATIVE_UNIT}/lesson_{name}.json"


def historical_bytes(name: str, locale: str = "es") -> bytes:
    relative = publication_path(name, locale)
    pin = Publication(BASELINE_COMMIT, PUBLICATIONS[name][locale])
    # The shared loader uses git show with lazy fetching/replacements disabled.
    # Recheck here as well so callers cannot supply an unverified local fallback.
    return verify_publication_bytes(publication_bytes(ROOT, relative, pin), pin)


def project_file(block: dict, path: str) -> dict:
    files = [entry for entry in block.get("projectFiles", []) if entry["path"] == path]
    if len(files) != 1:
        raise ValueError(f"The published owner lost its unique project file: {path}")
    return files[0]


def execution_source(document: dict, edit: dict) -> str:
    owner = active_owners(document)[0][edit["referenceActivity"]]
    if "referencePath" in edit:
        if owner["type"] != "cpp_compiler":
            raise ValueError("A project reference must belong to a compiler")
        return project_file(owner, edit["referencePath"])["initialCode"]
    if owner["type"] != "code":
        raise ValueError("An execution reference must be a published code block")
    return owner["code"]


@dataclass
class LessonFixture:
    name: str
    original_bytes: bytes
    before: dict
    after: dict


def build_lessons() -> dict[str, LessonFixture]:
    result = {}
    for name in PUBLICATIONS:
        raw = historical_bytes(name)
        family = {locale: json.loads(raw if locale == "es" else historical_bytes(name, locale))
                  for locale in PUBLICATIONS[name]}
        before = family["es"]
        state.validate(before)
        modified = deepcopy(before)
        old, current = active_owners(before)[0], active_owners(modified)[0]
        for case in (item for item in CASES if item["lesson"] == name):
            identity, path = case["activity"], case["path"]
            if old[identity]["type"] != "cpp_compiler":
                raise ValueError("The nested-CMake fixture lost its published editor")
            original = project_file(old[identity], path)["initialCode"]
            if not original or original.startswith("\n"):
                raise ValueError("The pinned CMake source no longer has its reviewed leading line")
            project_file(current[identity], path)["initialCode"] = "\n" + original
            if not source_format.require_source_format(old[identity], current[identity]):
                raise ValueError("The nested-CMake source did not change its whitespace")
            for edit in case["executionEdits"]:
                project_file(old[identity], edit["path"])
                if not execution_source(before, edit):
                    raise ValueError("A published execution reference cannot be empty")
        after = state.prepare_v2(modified, "es", family, before)
        state.validate_transition(before, after)
        result[name] = LessonFixture(name, raw, before, after)
    return result


def _replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise ValueError(f"The pinned negative lost its unique source token: {before!r}")
    return source.replace(before, after, 1)


def _authenticate_final_edge(document: dict, identity: str) -> None:
    owner = active_owners(document)[0][identity]
    edge = next(item for item in reversed(document["stateLayout"]["editorial"])
                if item["id"] == identity)
    edge["afterSha256"] = state.fingerprint(owner)


def counterexamples(lessons: dict[str, LessonFixture]) -> tuple[dict, dict[str, dict]]:
    """Return full QA documents whose rejection reaches the intended guard.

    Published snapshots and old edges remain exact. Current adversarial payloads
    get matching final hashes; a stale checksum cannot masquerade as a lexical
    rejection. The fake filename uses a separate, explicitly added QA owner.
    """
    base = lessons["datos_y_listas_de_objetivos"].after
    identity, path = CASES[0]["activity"], CASES[0]["path"]
    invalid = {}
    for name in ("logic", "path", "literal"):
        forged = deepcopy(base)
        target = project_file(active_owners(forged)[0][identity], path)
        if name == "logic":
            target["initialCode"] = _replace_once(target["initialCode"], "STATIC", "INTERFACE")
        elif name == "path":
            target["path"] = "componente/otro/CMakeLists.txt"
        else:
            target["initialCode"] = _replace_once(
                target["initialCode"], '"componente"', '"componente alterado"')
        _authenticate_final_edge(forged, identity)
        invalid[name] = forged

    # This additional QA activity retains all real owners and historical
    # addresses. It provides an authenticated immutable-data control without
    # renaming a published file or rebuilding a fictitious historical origin.
    addition = deepcopy(active_owners(base)[0][identity])
    addition["projectFiles"].append({
        "path": FAKE_PATH,
        "initialCode": project_file(addition, path)["initialCode"],
    })
    plan = deepcopy(base["stateLayout"]["current"])
    plan[-1]["blocks"].append(FAKE_OWNER)
    metadata = base["stateLayout"]
    storage_page = max(
        [len(entry["origin"]) for entry in metadata["family"].values()]
        + [entry["storage"]["page"] + 1 for entry in metadata["additions"]])
    control = state.reorder(base, plan, [{
        "id": FAKE_OWNER, "storage": {"page": storage_page, "block": 0},
        "progressPage": plan[-1]["id"], "block": addition,
    }])
    state.validate_transition(base, control)
    forged = deepcopy(control)
    owner = active_owners(forged)[0][FAKE_OWNER]
    project_file(owner, FAKE_PATH)["initialCode"] = "\n" + project_file(owner, FAKE_PATH)["initialCode"]
    forged["stateLayout"].setdefault("sourceFormatRevisions", []).append({
        "id": FAKE_OWNER, "block": deepcopy(addition),
    })
    forged["stateLayout"]["editorial"].append({
        "id": FAKE_OWNER, "beforeSha256": state.fingerprint(addition),
        "afterSha256": state.fingerprint(owner),
    })
    invalid["filename"] = forged
    return control, invalid


def json_bytes(document: dict) -> bytes:
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def fixture_payloads() -> dict[str, bytes]:
    lessons = build_lessons()
    control, invalid = counterexamples(lessons)
    payloads = {}
    manifest = {"version": 1, "lessons": [], "cases": deepcopy(list(CASES)),
                "controls": [{"name": "filename", "file": "control.filename.json",
                              "lesson": "datos_y_listas_de_objetivos",
                              "activity": FAKE_OWNER, "path": FAKE_PATH}],
                "invalid": []}
    for name, fixture in lessons.items():
        before, after = f"{name}.before.json", f"{name}.after.json"
        payloads[before], payloads[after] = fixture.original_bytes, json_bytes(fixture.after)
        manifest["lessons"].append({
            "name": name, "before": before, "after": after,
            "publicationCommit": BASELINE_COMMIT,
            "publicationPath": publication_path(name),
            "publicationSha256": PUBLICATIONS[name]["es"],
        })
    payloads["control.filename.json"] = json_bytes(control)
    for name, document in invalid.items():
        filename = f"invalid.{name}.json"
        payloads[filename] = json_bytes(document)
        manifest["invalid"].append({"name": name, "file": filename,
                                    "message": INVALID_MESSAGES[name]})
    payloads["manifest.json"] = json_bytes(manifest)
    return payloads


def write_fixtures(output_dir: Path) -> Path:
    output = checked_output(output_dir)
    payloads = fixture_payloads()
    # Inspect every existing destination before creating any new output. The
    # shared guard never truncates an existing file or follows a reparse point.
    present = {name: existing_matches(output / name, payload)
               for name, payload in payloads.items()}
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    for name, payload in payloads.items():
        if not present[name]:
            with (output / name).open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(write_fixtures(args.output_dir))


if __name__ == "__main__":
    main()

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
CLIENT_COMMIT = "9871a39302b89b72f6469a73cf8cca97ca3919ca"
CLIENT_ASSET = ("app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/"
                "lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json")
CLIENT_SHA256 = "ed784ae821b11d24df9c474a3cb2eae96b9ef213382506b249417abc2a123227"
CLIENT_SOURCES = {
    "es": (285042, CLIENT_SHA256),
    "en": (35079, "d88080e6e1e2c07ffe50b9704681198a11e4532a0065458aa53dc176d495eb75"),
    "fr": (37699, "220cb1602a099bdb1c91f476aa2a5fc22d4d6db938c6e4c72792e1ae6cc02833"),
}
CLIENT_OWNER = "block_0008_0008"
CLIENT_REFERENCE = "series_cinco_referencia_cliente"
CLIENT_INPUT_OWNER = "block_0020_0002"
CLIENT_MODE_COMMIT = "51d3786537b4b8485f526875561b9a3db11b1afc"
CLIENT_MODE_SOURCES = {
    "single": {
        "name": "u1l1-single-client", "id": "block_0004_0008",
        "asset": "app/src/main/assets/content/es/programming/programming_practice_tools/"
                 "evidencia_y_diagnostico/lesson_versiones_del_fuente_y_del_ejecutable.json",
        "locales": {
            "es": (96331, "cc0f934ce40085ef095c10ac08cdc44290e75e6ecc60d6444d86f979877f18df"),
            "en": (21665, "74998e2ac1eef4336177ccbfd7d72535c6a63a1b84cd30892e811ae28a58de0b"),
            "fr": (23504, "add47016262fb54830a4e0a4101842051bb0c94e31e50113910b97a29da363a9"),
        },
    },
    "cmake": {
        "name": "u2l2-cmake-client", "id": "block_0005_0012",
        "asset": "app/src/main/assets/content/es/programming/programming_practice_tools/"
                 "construccion_reproducible/lesson_objetivos_dependencias_y_enlace.json",
        "locales": {
            "es": (193752, "3903d921c21510dfa19570683c39bb0b6431367c94872cae0a85d89e9e9b2979"),
            "en": (41868, "c9f5fe440c7f76a6027652db3e9927086a0e85dcb149ead70db8abe19217aa9d"),
            "fr": (44962, "fe7bc29fb6a2ecf8a33a6203e8db61346edc78b4d87a0edb1309a0c943ecffb1"),
        },
    },
}


@lru_cache(maxsize=None)
def client_historical_bytes(locale: str = "es") -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1",
                       GIT_TERMINAL_PROMPT="0")
    size, digest = CLIENT_SOURCES[locale]
    asset = CLIENT_ASSET.replace("/es/", "/" + locale + "/")
    raw = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", f"{CLIENT_COMMIT}:{asset}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if len(raw) != size or hashlib.sha256(raw).hexdigest() != digest:
        raise ValueError("The client fixture differs from its complete pinned U6L19 publication")
    return raw


def client_publication() -> dict:
    document = json.loads(client_historical_bytes())
    layout.validate(document)
    return document


def client_cases() -> list[dict]:
    # Independent oracles: a whole aggregate is never split into receipts.
    return [{"standardInput": stdin, "outputs": [stdout, stdout + "\n"]}
            for stdin, stdout in (("3 5", "En curso: 2"), ("0 0", "Terminada: 0"),
                                  ("5 5", "Terminada: 0"), ("0 40", "En curso: 40"),
                                  ("40 40", "Terminada: 0"))]


def client_revised(document: dict, identity: str, source: str, cases: list[dict], *, return_to=None) -> dict:
    return layout.revise_compiler_client_cases(document, identity, {"path": "main.cpp", "source": source},
        cases, case_semantics=authored_fields(document, identity, cases), return_to=return_to)


def client_formatted(document: dict, identity: str) -> dict:
    changed = deepcopy(document)
    block = layout._blocks(changed)[identity]
    source = next(entry["initialCode"] for entry in block["projectFiles"] if entry["path"] == "main.cpp")
    layout._compiler_client_replace_source(block, "main.cpp", source.replace("\n", "\n\n"))
    family = {"es": document}
    if set(document["stateLayout"]["family"]) != {"es"}:
        family.update({locale: json.loads(client_historical_bytes(locale)) for locale in ("en", "fr")})
    return layout.prepare_v2(changed, "es", family, document)


def synthetic_client_publication() -> dict:
    document = order_fixtures.synthetic_publication(marked=False)
    block = layout._blocks(document)[OWNER]
    block["projectFiles"] = [{"path": "helper.h", "initialCode": "// An unchanged auxiliary.\n"},
                             {"path": "main.cpp", "initialCode": block["initialCode"]}]
    block["sourceFiles"] = ["main.cpp"]
    document.pop("stateLayout")
    references = document.pop("referenceAfter")
    result = layout.prepare_v2(document, "es", {"es": document})
    result["referenceAfter"] = references
    layout.validate(result)
    return result


def build_client_fixtures() -> tuple[dict, dict[str, str]]:
    """Keep changed-client epochs separate from same-program case fixtures."""
    manifest, payloads = {"clientCases": [], "clientValid": [], "invalid": []}, {}
    def put(filename, document, *, raw=None):
        payloads[filename] = raw.decode("utf-8") if raw is not None else json.dumps(document, ensure_ascii=False, indent=2) + "\n"
        return filename
    before = client_publication()
    owner = layout._blocks(before)[CLIENT_OWNER]
    source = layout._blocks(before)[CLIENT_INPUT_OWNER]["initialCode"]
    cases = client_cases()
    after = client_revised(before, CLIENT_OWNER, source, cases)
    cases_only = revised(after, CLIENT_OWNER, cases[:-1])
    changed_source = source.replace(
        '    std::cout << estado_serie(hechas, total) << ": " << tareas_pendientes(hechas, total);',
        '    const int pendientes{tareas_pendientes(hechas, total)};\n'
        '    std::cout << estado_serie(hechas, total) << ": " << pendientes;')
    if changed_source == source:
        raise ValueError("The published individual client changed its independently selected expression")
    changed_client = client_revised(after, CLIENT_OWNER, changed_source, cases)
    prior = [edge for edge in changed_client["stateLayout"]["editorial"] if edge["id"] == CLIENT_OWNER]
    nodes = [prior[0]["beforeSha256"], *[edge["afterSha256"] for edge in prior]]
    returned = client_revised(changed_client, CLIENT_OWNER, source, cases,
        return_to=nodes.index(layout.fingerprint(layout._blocks(after)[CLIENT_OWNER])))
    formatted = client_formatted(after, CLIENT_OWNER)
    manifest["clientCases"].append({
        "name": "real-u6l19-client", "id": CLIENT_OWNER,
        "before": put("u6l19-client.before.json", before, raw=client_historical_bytes()),
        "after": put("u6l19-client.after.json", after),
        "casesOnly": put("u6l19-client.cases-only.json", cases_only),
        "changedClient": put("u6l19-client.changed-client.json", changed_client),
        "returnedClient": put("u6l19-client.returned-client.json", returned),
        "formattedClient": put("u6l19-client.formatted-client.json", formatted)})
    for previous, current in ((before, after), (after, cases_only), (after, changed_client),
                              (changed_client, returned), (after, formatted), (before, returned)):
        layout.validate_transition(previous, current)

    plain = synthetic_client_publication()
    old = layout._blocks(plain)[OWNER]
    newer = old["initialCode"].replace("int value{};", "int value{1};")
    changed = client_revised(plain, OWNER, newer, old["outputCheck"]["cases"])
    format_after = client_formatted(changed, OWNER)
    format_before = client_formatted(plain, OWNER)
    after_format = client_revised(format_before, OWNER, newer, old["outputCheck"]["cases"])
    final_client = client_revised(format_after, OWNER, newer.replace("value{1}", "value{2}"),
                                  old["outputCheck"]["cases"])
    retired_before = layout.retire(plain, [NOTE])
    after_retirement = client_revised(retired_before, OWNER, newer, old["outputCheck"]["cases"])
    retired_after = layout.retire(changed, [NOTE])
    for name, document in (("plain", changed), ("format-after", format_after),
                           ("format-before", after_format), ("format-then-client", final_client),
                           ("retirement-before", after_retirement), ("retirement-after", retired_after)):
        layout.validate(document)
        entry = {"name": name, "file": put("synthetic-client-" + name + ".json", document)}
        if name.startswith("retirement-"):
            predecessor = plain if name.endswith("before") else changed
            entry.update(owner=OWNER, snapshotSource=put(
                "synthetic-client-" + name + ".source.json", predecessor))
        manifest["clientValid"].append(entry)
    layout.validate_transition(plain, format_after)
    layout.validate_transition(plain, final_client)

    def invalid(name, operation, *, baseline=after):
        bad = deepcopy(baseline)
        item = bad["stateLayout"]["compilerCaseRevisions"][-1]
        block = layout._blocks(bad)[item["id"]]
        operation(bad, item, block)
        try:
            layout.validate(bad)
        except (ValueError, TypeError, KeyError, IndexError):
            manifest["invalid"].append({"name": "client-" + name,
                "file": put("invalid-client-" + name + ".json", bad)})
        else:
            raise AssertionError("Invalid client history was accepted: " + name)
    invalid("missing-declaration", lambda d,r,b: r.pop("client"))
    invalid("unknown-field", lambda d,r,b: r["client"].__setitem__("automaticDraft", True))
    invalid("missing-path", lambda d,r,b: r["client"].pop("path"))
    invalid("missing-source", lambda d,r,b: r["client"].pop("source"))
    invalid("unknown-file", lambda d,r,b: r["client"].__setitem__("path", "absent.cpp"))
    invalid("header-client", lambda d,r,b: r["client"].__setitem__("path", "serie.h"))
    invalid("wrong-source-client", lambda d,r,b: r["client"].__setitem__("path", "serie.cpp"))
    invalid("empty-source", lambda d,r,b: r["client"].__setitem__("source", ""))
    invalid("nul-source", lambda d,r,b: r["client"].__setitem__("source", r["client"]["source"] + "\0"))
    invalid("unchanged-client", lambda d,r,b: r["client"].__setitem__("source", owner["initialCode"]))
    invalid("previous-alias", lambda d,r,b: r["block"].__setitem__("initialCode", "int main() {}\n"))
    invalid("extra-client-file-field", lambda d,r,b:
            r["block"]["projectFiles"][-1].__setitem__("automaticDraft", True))
    invalid("extra-auxiliary-field", lambda d,r,b:
            r["block"]["projectFiles"][0].__setitem__("automaticDraft", True))
    def current_source(d, r, b):
        layout._compiler_client_replace_source(b, "main.cpp", b["initialCode"].replace("int hechas{0}", "int hechas{7}"))
        d["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(b)
    invalid("unrecorded-source", current_source)
    def current_auxiliary(d, r, b):
        b["projectFiles"][1]["initialCode"] += "int changed_auxiliary;\n"
        d["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(b)
    invalid("changed-auxiliary", current_auxiliary)
    invalid("lost-history-prefix", lambda d,r,b: r.__setitem__("historySha256", "0" * 64))
    invalid("stolen-editorial-edge", lambda d,r,b: r.__setitem__("editorialIndex", 0))
    def unarchived_format(d, r, b):
        previous = layout.fingerprint(b)
        layout._compiler_client_replace_source(b, "main.cpp", b["initialCode"].replace("\n", "\n\n"))
        d["stateLayout"]["editorial"].append({"id": OWNER, "beforeSha256": previous,
                                             "afterSha256": layout.fingerprint(b)})
    invalid("unarchived-format", unarchived_format, baseline=changed)
    intermediate_source = newer.replace("value{1}", "value{7}")
    intermediate = client_revised(changed, OWNER, intermediate_source, old["outputCheck"]["cases"])
    middle = client_revised(intermediate, OWNER, newer.replace("value{1}", "value{2}"),
                            old["outputCheck"]["cases"])
    cyclic = client_revised(middle, OWNER, intermediate_source, old["outputCheck"]["cases"], return_to=2)
    invalid("missing-event-before-return", lambda d,r,b:
            d["stateLayout"]["compilerCaseRevisions"].pop(1), baseline=cyclic)
    invalid("wrong-snapshot-epoch", lambda d,r,b:
            d["stateLayout"]["retirementHistory"][0].__setitem__("editorialPrefix",
                layout._retirement_editorial_prefix(d, 0)), baseline=retired_after)
    return manifest, payloads


@lru_cache(maxsize=None)
def client_mode_historical_bytes(mode: str, locale: str = "es") -> bytes:
    spec = CLIENT_MODE_SOURCES[mode]
    size, digest = spec["locales"][locale]
    asset = spec["asset"].replace("/es/", "/" + locale + "/")
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1",
                       GIT_TERMINAL_PROMPT="0")
    raw = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show",
         f"{CLIENT_MODE_COMMIT}:{asset}"], cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    if len(raw) != size or hashlib.sha256(raw).hexdigest() != digest:
        raise ValueError("The client mode differs from its complete pinned P2 publication")
    return raw


def client_mode_source(block: dict) -> str:
    if "projectFiles" not in block:
        return block["initialCode"]
    return next(entry["initialCode"] for entry in block["projectFiles"] if entry["path"] == "main.cpp")


def client_mode_set_source(block: dict, source: str) -> None:
    if "projectFiles" not in block:
        block["initialCode"] = source
    else:
        next(entry for entry in block["projectFiles"] if entry["path"] == "main.cpp")["initialCode"] = source


def client_mode_cases(mode: str) -> list[dict]:
    pairs = (("5 5 1", ""), ("4 5 1", ""), ("6 5 0", "")) if mode == "single" else (
             ("4 7", "3"), ("7 7", "0"), ("9 7", "0"))
    return [{"standardInput": value, "outputs": [output], "exitCode": 0} for value, output in pairs]


def client_mode_initial_source(mode: str, document: dict) -> str:
    owner = layout._blocks(document)[CLIENT_MODE_SOURCES[mode]["id"]]
    source = client_mode_source(owner)
    if source.count("\nint main() {") != 1:
        raise ValueError("The pinned client no longer has its one independently identified main")
    prefix = source[:source.index("\nint main() {")]
    if mode == "single":
        body = (
            "\nint main() {\n"
            "    int disponibles{0};\n"
            "    int minimo{0};\n"
            "    bool esperado{false};\n"
            "    std::cin >> disponibles >> minimo >> esperado;\n"
            "    return comprobar(disponibles, minimo, esperado) ? 0 : 1;\n"
            "}\n")
    else:
        body = (
            "\nint main() {\n"
            "    int disponibles{0};\n"
            "    int necesarias{0};\n"
            "    std::cin >> disponibles >> necesarias;\n"
            "    std::cout << faltan_unidades(disponibles, necesarias);\n"
            "    return 0;\n"
            "}\n")
    return prefix + body


def client_mode_formatted(document: dict, identity: str, mode: str) -> dict:
    changed = deepcopy(document)
    block = layout._blocks(changed)[identity]
    client_mode_set_source(block, client_mode_source(block).replace("\n", "\n\n"))
    family = {"es": document}
    if set(document["stateLayout"]["family"]) != {"es"}:
        family.update({locale: json.loads(client_mode_historical_bytes(mode, locale)) for locale in ("en", "fr")})
    return layout.prepare_v2(changed, "es", family, document)


def build_client_mode_fixtures() -> tuple[dict, dict[str, str]]:
    """Single-file and CMake clients retain their own storage and build recipes."""
    manifest, payloads = {"clientModes": [], "clientModeValid": [], "invalid": []}, {}
    def put(filename, document, *, raw=None):
        payloads[filename] = raw.decode("utf-8") if raw is not None else json.dumps(
            document, ensure_ascii=False, indent=2) + "\n"
        return filename
    for mode, spec in CLIENT_MODE_SOURCES.items():
        before = json.loads(client_mode_historical_bytes(mode))
        layout.validate(before)
        identity, prefix = spec["id"], spec["name"]
        source, cases = client_mode_initial_source(mode, before), client_mode_cases(mode)
        after = client_revised(before, identity, source, cases)
        cases_only = revised(after, identity, cases[:-1])
        read = ("    std::cin >> disponibles >> minimo >> esperado;" if mode == "single"
                else "    std::cin >> disponibles >> necesarias;")
        guarded = read.replace("    std::cin", "    if (!(std::cin").replace(";", ")) { return 1; }")
        if source.count(read) != 1:
            raise ValueError("The independent stdin client changed its one input operation")
        changed_client = client_revised(after, identity, source.replace(read, guarded), cases)
        prior = [edge for edge in changed_client["stateLayout"]["editorial"] if edge["id"] == identity]
        nodes = [prior[0]["beforeSha256"], *[edge["afterSha256"] for edge in prior]]
        returned = client_revised(changed_client, identity, source, cases,
            return_to=nodes.index(layout.fingerprint(layout._blocks(after)[identity])))
        formatted = client_mode_formatted(after, identity, mode)
        manifest["clientModes"].append({
            "name": "real-" + prefix, "mode": mode, "id": identity, "path": "main.cpp",
            "before": put(prefix + ".before.json", before, raw=client_mode_historical_bytes(mode)),
            "after": put(prefix + ".after.json", after),
            "casesOnly": put(prefix + ".cases-only.json", cases_only),
            "changedClient": put(prefix + ".changed-client.json", changed_client),
            "returnedClient": put(prefix + ".returned-client.json", returned),
            "formattedClient": put(prefix + ".formatted-client.json", formatted)})
        for previous, current in ((before, after), (after, cases_only), (after, changed_client),
                                  (changed_client, returned), (after, formatted), (before, returned)):
            layout.validate_transition(previous, current)

        # CMake's upper initialCode is deliberately independent of main.cpp.
        # Formatting all its slots checks that reversing a client never copies
        # that placeholder over its separately authenticated format history.
        plain = order_fixtures.synthetic_publication(marked=False, project=mode == "cmake")
        old = layout._blocks(plain)[OWNER]
        newer = client_mode_source(old).replace("int value{};", "int value{1};")
        changed = client_revised(plain, OWNER, newer, old["outputCheck"]["cases"])
        format_after = order_fixtures.formatted(changed, OWNER)
        format_before = order_fixtures.formatted(plain, OWNER)
        after_format = client_revised(format_before, OWNER, newer, old["outputCheck"]["cases"])
        final_client = client_revised(format_after, OWNER, newer.replace("value{1}", "value{2}"),
                                      old["outputCheck"]["cases"])
        retired_before = layout.retire(plain, [NOTE])
        after_retirement = client_revised(retired_before, OWNER, newer, old["outputCheck"]["cases"])
        retired_after = layout.retire(changed, [NOTE])
        for name, document in (("plain", changed), ("format-after", format_after),
                               ("format-before", after_format), ("format-then-client", final_client),
                               ("retirement-before", after_retirement), ("retirement-after", retired_after)):
            layout.validate(document)
            entry = {"name": name, "mode": mode, "file": put(
                "synthetic-client-" + mode + "-" + name + ".json", document)}
            if name.startswith("retirement-"):
                predecessor = plain if name.endswith("before") else changed
                entry.update(owner=OWNER, snapshotSource=put(
                    "synthetic-client-" + mode + "-" + name + ".source.json", predecessor))
            manifest["clientModeValid"].append(entry)
        layout.validate_transition(plain, format_after)
        layout.validate_transition(plain, final_client)

        def invalid(name, operation, *, baseline=after, refresh=False):
            bad = deepcopy(baseline)
            item = bad["stateLayout"]["compilerCaseRevisions"][-1]
            block = layout._blocks(bad)[item["id"]]
            operation(bad, item, block)
            if refresh:
                bad["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(block)
            try:
                layout.validate(bad)
            except (ValueError, TypeError, KeyError, IndexError):
                name = "client-" + mode + "-" + name
                manifest["invalid"].append({"name": name, "file": put("invalid-" + name + ".json", bad)})
            else:
                raise AssertionError("Invalid client mode was accepted: " + mode + "/" + name)
        invalid("wrong-path", lambda d,r,b: r["client"].__setitem__(
            "path", "Main.cpp" if mode == "single" else "CMakeLists.txt"))
        invalid("source-selection", lambda d,r,b: b.__setitem__("sourceFiles", ["main.cpp"]), refresh=True)
        invalid("unrecorded-source", lambda d,r,b: client_mode_set_source(
            b, client_mode_source(b).replace("return 0;", "return 9;") if mode == "cmake"
            else client_mode_source(b).replace("disponibles < minimo", "disponibles > minimo")), refresh=True)
        invalid("missing-client", lambda d,r,b: r.pop("client"))
        oversized = "x" * ((4 if mode == "single" else 8) * 1024 * 1024 + 1)
        invalid("source-size", lambda d,r,b: r["client"].__setitem__("source", oversized))
        invalid("wrong-snapshot-epoch", lambda d,r,b: d["stateLayout"]["retirementHistory"][0].__setitem__(
            "editorialPrefix", layout._retirement_editorial_prefix(d, 0)), baseline=retired_after)
        intermediate_source = newer.replace("value{1}", "value{7}")
        intermediate = client_revised(changed, OWNER, intermediate_source, old["outputCheck"]["cases"])
        middle = client_revised(intermediate, OWNER, newer.replace("value{1}", "value{2}"),
                                old["outputCheck"]["cases"])
        cyclic = client_revised(middle, OWNER, intermediate_source, old["outputCheck"]["cases"], return_to=2)
        invalid("missing-event-before-return", lambda d,r,b:
                d["stateLayout"]["compilerCaseRevisions"].pop(1), baseline=cyclic)
        if mode == "single":
            invalid("invented-project", lambda d,r,b: b.__setitem__("projectFiles", [
                {"path": "main.cpp", "initialCode": b["initialCode"]}]), refresh=True)
            # Build an internally authenticated but non-runnable predecessor:
            # the public revision API must now reject this empty source, so
            # construct the invalid journal explicitly instead of using it.
            empty_source = deepcopy(plain)
            layout._blocks(empty_source)[OWNER]["initialCode"] = ""
            empty_source.pop("stateLayout")
            references = empty_source.pop("referenceAfter")
            empty = layout.prepare_v2(empty_source, "es", {"es": empty_source})
            empty["referenceAfter"] = references
            layout.validate(empty)
            forged = deepcopy(empty)
            prior_block = layout._blocks(empty)[OWNER]
            forged_block = layout._blocks(forged)[OWNER]
            forged_block["initialCode"] = newer
            empty_record = {
                "id": OWNER, "block": deepcopy(prior_block),
                "client": {"path": "main.cpp", "source": newer},
                "cases": deepcopy(prior_block["outputCheck"]["cases"]),
                "editorialIndex": 0,
                "historySha256": layout.fingerprint({
                    "lessonId": empty["id"], "locale": "es", "editorial": []}),
                "semanticBefore": [], "semanticAfter": []}
            if empty["stateLayout"]["editorial"]:
                raise ValueError("The independent empty-source origin must have no editorial events")
            forged["stateLayout"]["compilerCaseRevisions"] = [empty_record]
            forged["stateLayout"]["editorial"].append({
                "id": OWNER, "beforeSha256": layout.fingerprint(prior_block),
                "afterSha256": layout.fingerprint(forged_block)})
            invalid("empty-predecessor", lambda d,r,b: None, baseline=forged)
        else:
            invalid("independent-initial-code", lambda d,r,b: b.__setitem__(
                "initialCode", "int main() { return 9; }\n"), refresh=True)
            invalid("target", lambda d,r,b: b["cmake"].__setitem__("target", "foreign"), refresh=True)
            invalid("options", lambda d,r,b: b["cmake"].__setitem__("options", {"QA_EXTRA": "ON"}), refresh=True)
            invalid("description", lambda d,r,b: b["projectFiles"][0].__setitem__(
                "initialCode", b["projectFiles"][0]["initialCode"].replace("STATIC", "SHARED")), refresh=True)
            invalid("auxiliary", lambda d,r,b: b["projectFiles"][2].__setitem__(
                "initialCode", b["projectFiles"][2]["initialCode"].replace("+ 1", "+ 2")), refresh=True)
    return manifest, payloads


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
    # Equal block bytes before and after a return are different history epochs.
    # Retirement must capture its actual prefix instead of guessing by hash.
    snapshot_before = layout.retire(before, [NOTE])
    revised_after_snapshot = revised(snapshot_before, OWNER, cases)
    returned_after_snapshot = layout.revise_compiler_cases(revised_after_snapshot, OWNER, original_cases,
        case_semantics=layout._compiler_case_order_fields(before, OWNER), return_to=0)
    name = "synthetic-snapshot-before-return"
    add(name, OWNER, before, [snapshot_before, revised_after_snapshot, returned_after_snapshot])
    manifest["cases"][-1].update(snapshotEpochOnly=True, snapshotSource=name + ".before.json",
                                  receiptSource=name + ".stage2.json")
    name = "synthetic-snapshot-after-return"
    snapshot_after = layout.retire(restored, [NOTE])
    add(name, OWNER, before, [after, restored, snapshot_after])
    manifest["cases"][-1].update(snapshotEpochOnly=True, snapshotSource=name + ".stage2.json",
                                  receiptSource=name + ".stage1.json")
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
    snapshot = lambda document: document["stateLayout"]["retirementHistory"][0]
    prefix = lambda document: snapshot(document)["editorialPrefix"]
    invalid("retirement-prefix-boolean-count", snapshot_after, lambda d: prefix(d).update(count=True))
    invalid("retirement-prefix-floating-count", snapshot_after, lambda d: prefix(d).update(count=2.0))
    invalid("retirement-prefix-out-of-range", snapshot_after,
            lambda d: prefix(d).update(count=len(d["stateLayout"]["editorial"]) + 1))
    invalid("retirement-prefix-wrong-hash", snapshot_after, lambda d: prefix(d).update(sha256="0" * 64))
    invalid("retirement-prefix-extra-field", snapshot_after, lambda d: prefix(d).update(extra=True))
    invalid("retirement-prefix-wrong-type", snapshot_after, lambda d: snapshot(d).update(editorialPrefix=[]))
    def wrong_head(document):
        # Prefix 1 is real and correctly hashed, but its compiler head is B.
        # The snapshot contains returned A, so hash validity alone cannot pass.
        prefix(document).update(count=1, sha256=layout.fingerprint({
            "lessonId": document["id"], "locale": document["stateLayout"]["locale"],
            "editorial": document["stateLayout"]["editorial"][:1]}))
    invalid("retirement-prefix-wrong-published-head", snapshot_after, wrong_head)
    client_manifest, client_payloads = build_client_fixtures()
    manifest["clientCases"] = client_manifest["clientCases"]
    manifest["clientValid"] = client_manifest["clientValid"]
    manifest["invalid"].extend(client_manifest["invalid"])
    payloads.update(client_payloads)
    mode_manifest, mode_payloads = build_client_mode_fixtures()
    manifest["clientModes"] = mode_manifest["clientModes"]
    manifest["clientModeValid"] = mode_manifest["clientModeValid"]
    manifest["invalid"].extend(mode_manifest["invalid"])
    payloads.update(mode_payloads)
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

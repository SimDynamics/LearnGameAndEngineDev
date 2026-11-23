"""Authenticate repeatable U6/U9 compileCheck source-format fixtures.

Both complete publications are available explicitly: the historical Codex
contracts and main's already-formatted contracts that exposed the build failure.
The working-tree route retains its real, append-only history. No older document
is reconstructed by removing archives or editorial edges.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import stable_state_layout as layout
from code_format_compat import require_source_format, source_projection
from format_batch_history import (
    Publication, active_owners, publication_bytes, verify_publication_bytes,
)

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = Path("programming/cpp_fundamentals/funciones_y_contratos/"
                "lesson_declaraciones_y_firmas.json")
LESSON_ID = "cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas"
OWNERS = ("block_0014_0002", "block_0015_0002", "block_0017_0002")
DIAGNOSTICS = (
    "u6-difference-unselected-definition",
    "u6-difference-conflicting-return",
    "u6-difference-missing-header",
)
BLANK = "\n    return 0;\n"
SPACED = "\n\n    return 0;\n"
OTHER_BLANK = "\n    return primero - segundo;\n"
OTHER_SPACED = "\n\n    return primero - segundo;\n"
PUBLICATION_COMMITS = {
    "921e1df": "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
    "83e2d4e6": "83e2d4e6d21027c6ca072dd2e85d4700a21d611d",
}
PUBLICATIONS = ("working-tree", *PUBLICATION_COMMITS)
# es differs between the two full publications; en/fr have identical Git blobs.
# Every digest authenticates complete file bytes, not a projected block.
PINNED_FAMILIES = {
    "programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_argumentos_con_tipos_que_expresan_su_funcion.json": {
        "921e1df": "7d8587eb12cc2a28ca97dca52e6e4929674414ad8ccb5ec098a669a074835744",
        "83e2d4e6": "adf6588bdb1dd865d894227b041189431ea261a8fe4fab0bf47983c64fe17bcd",
        "en": "1381dadab209b9f1017e97953d7c0c6134db3bb2bf73574e44190daa8ff70363",
        "fr": "682d88745c105920d4fcc5125935d37caac916632b45c2f405f69b91d6f081a1"
    },
    "programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json": {
        "921e1df": "7dfdff6e62f05a1ef01e8f713381ee809da07f07dc8bbaa945072a20ddf9a8a4",
        "83e2d4e6": "48a2fe71fa4d28d3388964bd7bef12658d83037c282ae2df6accf5205c9c8be4",
        "en": "1b9c3b9c7333436941743f44d188f06af6685570767984d327d8103538e65cf6",
        "fr": "975be6adf04a097923236454476f43ce4a6eb3891a587211e5751d861421e75b"
    },
    "programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json": {
        "921e1df": "8d12568ad8cdb4efe6d45ee42fe420267deb19cd57dd1ffe2f538a685b5d58c5",
        "83e2d4e6": "9d2e6fad4d0facc99d31c3322b82d8f2e38598df1a56b1d440e9d39cd2e9012e",
        "en": "fe3a9c94e12164163031b1dd9b7c13d559ea7c1835ce490bf5473b84e5968514",
        "fr": "bdd8902e6893491b591d1f81c5562a0123ebd62004ac4f9235aa7fe10bd48c58"
    },
    "programming/cpp_fundamentals/funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json": {
        "921e1df": "289200909e98c6953a22b8750d62668e0a3ef670dc7b62e297b4bc0c3bbff3a9",
        "83e2d4e6": "c89c2afac52787151092d364a9c8427d647ae7f00853a6c7083aa3ba4a4627eb",
        "en": "9ee7e0187f9a6b007449a84e6adab72142b4462d83852b58f9c8637c209fd82a",
        "fr": "8a919dd16fd59bbe23f4840b4664dee78fac0176d19affe3c8236cddc014e388"
    },
    "programming/cpp_fundamentals/funciones_y_contratos/lesson_declaraciones_y_firmas.json": {
        "921e1df": "eaae140880a99040e71462b4989890c9944b42cc555cd7b729d91262ece6054e",
        "83e2d4e6": "e9c67087a6c2ba34ee7cd5ea4db0700ab1e52124234011627936cc804de98f18",
        "en": "8976a37af1ffd5359baf96e0ab4606b3d0b81c97b5d3d998a458df3ff348d716",
        "fr": "c4397e0426ff28750c21ff4f805a3cbd7b238823958ba8b22643c6219dd2aa2d"
    },
    "programming/cpp_fundamentals/funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json": {
        "921e1df": "ed784ae821b11d24df9c474a3cb2eae96b9ef213382506b249417abc2a123227",
        "83e2d4e6": "d566ea60c14409b92bd9e09231a9d12a545b0b0e5c68bdd94b04b84d99366b80",
        "en": "d88080e6e1e2c07ffe50b9704681198a11e4532a0065458aa53dc176d495eb75",
        "fr": "220cb1602a099bdb1c91f476aa2a5fc22d4d6db938c6e4c72792e1ae6cc02833"
    }
}


def asset_path(locale: str, relative: Path = RELATIVE) -> Path:
    return ROOT / "app/src/main/assets/content" / locale / relative


def asset_bytes(locale: str = "es", relative: Path = RELATIVE,
                publication: str = "working-tree") -> bytes:
    if publication not in PUBLICATIONS or locale not in ("es", "en", "fr"):
        raise ValueError("Unknown compile-check publication or locale")
    if publication == "working-tree":
        return asset_path(locale, relative).read_bytes()
    relative = Path(relative)
    hashes = PINNED_FAMILIES.get(relative.as_posix())
    if hashes is None:
        raise ValueError("Unknown complete compile-check lesson publication")
    pin = Publication(PUBLICATION_COMMITS[publication],
                      hashes[publication if locale == "es" else locale])
    repository_path = "app/src/main/assets/content/" + locale + "/" + relative.as_posix()
    return verify_publication_bytes(publication_bytes(ROOT, repository_path, pin), pin)


def asset(locale: str = "es", relative: Path = RELATIVE,
          publication: str = "working-tree") -> dict:
    return json.loads(asset_bytes(locale, relative, publication))


def family(relative: Path = RELATIVE, publication: str = "working-tree") -> dict:
    return {locale: asset(locale, relative, publication) for locale in ("es", "en", "fr")}


def position(document: dict, identity: str) -> tuple[int, int]:
    return active_owners(document)[1][identity]


def owner(document: dict, identity: str) -> dict:
    return active_owners(document)[0][identity]


def project_file(block: dict, path: str) -> dict:
    matches = [entry for entry in block.get("projectFiles", []) if entry["path"] == path]
    if len(matches) != 1:
        raise ValueError("Missing or ambiguous published project file: " + path)
    return matches[0]


def archive(document: dict, identity: str) -> dict:
    matches = [entry["block"] for entry in document["stateLayout"].get("sourceFormatRevisions", [])
               if entry["id"] == identity]
    if len(matches) != 1:
        raise ValueError("Missing or ambiguous source-format archive: " + identity)
    return matches[0]


def rehash_final_edge(document: dict, identity: str) -> None:
    edge = next(entry for entry in reversed(document["stateLayout"]["editorial"])
                if entry["id"] == identity)
    edge["afterSha256"] = layout.fingerprint(owner(document, identity))


def diagnostic_groups() -> dict[str, tuple[tuple[str, str], ...]]:
    groups = {}
    for (lesson, identity), spec in layout.COMPILE_DIAGNOSTICS.items():
        groups.setdefault(lesson, []).append((identity, spec["diagnostic"]))
    return {lesson: tuple(owners) for lesson, owners in sorted(groups.items())}


def lesson_relative(lesson: str) -> Path:
    course, unit, title = lesson.split("__")
    return Path("programming") / course / unit / ("lesson_" + title + ".json")


def format_family(published: dict, goals: tuple[tuple[str, str], ...],
                  *, implementation: bool = False) -> tuple[dict, dict]:
    before = published["es"]
    layout.validate(before)
    changed = deepcopy(before)
    old, new = active_owners(before)[0], active_owners(changed)[0]
    identities = {identity for identity, _ in goals}
    if len(identities) != len(goals) or not identities:
        raise ValueError("The format review requires unique published owners")
    for identity, diagnostic in goals:
        original, updated = old[identity], new[identity]
        if (original["type"] != "cpp_compiler"
                or original["compileCheck"]["diagnostic"] != diagnostic):
            raise ValueError("The actual reviewed diagnostic owner changed")
        source = original["initialCode"]
        if source.count(BLANK) != 1:
            raise ValueError("The published source lost its unique return location")
        updated["initialCode"] = source.replace(BLANK, SPACED, 1)
        if "projectFiles" in updated:
            main = project_file(updated, "main.cpp")
            if main["initialCode"] != source:
                raise ValueError("The published main.cpp alias changed")
            main["initialCode"] = updated["initialCode"]
        if implementation:
            other = project_file(updated, "diferencia.cpp")
            if other["initialCode"].count(OTHER_BLANK) != 1:
                raise ValueError("The implementation lost its unique return location")
            other["initialCode"] = other["initialCode"].replace(OTHER_BLANK, OTHER_SPACED, 1)
        if not require_source_format(original, updated):
            raise ValueError("The diagnostic source did not gain physical spacing")
    after = layout.prepare_v2(changed, "es", published, before)
    layout.validate_transition(before, after)
    if layout.validate(before) != layout.validate(after):
        raise ValueError("Source formatting changed historical storage")
    old_meta, new_meta = before["stateLayout"], after["stateLayout"]
    old_archives = old_meta.get("sourceFormatRevisions", [])
    new_archives = new_meta.get("sourceFormatRevisions", [])
    if new_archives[:len(old_archives)] != old_archives:
        raise ValueError("A published source-format archive was replaced")
    if {entry["id"] for entry in new_archives} != {entry["id"] for entry in old_archives} | identities:
        raise ValueError("Source formatting changed another archive owner")
    previous = {entry["id"]: entry["block"] for entry in old_archives}
    for identity in identities:
        if archive(after, identity) != previous.get(identity, old[identity]):
            raise ValueError("The authentic original source snapshot changed")
    edges = old_meta["editorial"]
    suffix = new_meta["editorial"][len(edges):]
    expected = [{"id": identity, "beforeSha256": layout.fingerprint(old[identity]),
                 "afterSha256": layout.fingerprint(owner(after, identity))}
                for identity in identities]
    if (new_meta["editorial"][:len(edges)] != edges
            or sorted(suffix, key=lambda edge: edge["id"]) != sorted(expected, key=lambda edge: edge["id"])):
        raise ValueError("The format review lost its exact published editorial predecessor")
    return before, after


def formatted_pair(publication: str = "working-tree") -> tuple[dict, dict]:
    return format_family(family(publication=publication), tuple(zip(OWNERS, DIAGNOSTICS)),
                         implementation=True)


def formatted_documents(publication: str = "working-tree") -> list[tuple]:
    result = []
    for lesson, goals in diagnostic_groups().items():
        relative = lesson_relative(lesson)
        before, after = format_family(family(relative, publication), goals,
                                      implementation=lesson == LESSON_ID)
        if before["id"] != lesson:
            raise ValueError("The actual publication belongs to another lesson")
        result.append((asset_path("es", relative), before, after, goals))
    return result


class CompileCheckFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.documents = {publication: formatted_documents(publication) for publication in PUBLICATIONS}
        cls.primary = {publication: next((before, after) for _, before, after, _ in documents
                                        if before["id"] == LESSON_ID)
                       for publication, documents in cls.documents.items()}

    def assert_history(self, before, after, goals):
        old_meta, new_meta = before["stateLayout"], after["stateLayout"]
        identities = {identity for identity, _ in goals}
        old_archives = {entry["id"]: entry["block"]
                        for entry in old_meta.get("sourceFormatRevisions", [])}
        new_archives = {entry["id"]: entry["block"]
                        for entry in new_meta["sourceFormatRevisions"]}
        self.assertEqual(set(new_archives), set(old_archives) | identities)
        self.assertEqual(new_meta["sourceFormatRevisions"][:len(old_archives)],
                         old_meta.get("sourceFormatRevisions", []))
        for field in ("origin", "current", "family", "additions", "retired",
                      "retirementHistory", "orderRefinements", "choiceRevisions",
                      "compilerRevisions"):
            self.assertEqual(old_meta.get(field), new_meta.get(field), field)
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        prior = old_meta["editorial"]
        self.assertEqual(new_meta["editorial"][:len(prior)], prior)
        self.assertCountEqual(
            new_meta["editorial"][len(prior):],
            [{"id": identity, "beforeSha256": layout.fingerprint(owner(before, identity)),
              "afterSha256": layout.fingerprint(owner(after, identity))}
             for identity in identities])
        self.assertEqual(before.get("textSemantics"), after.get("textSemantics"))
        for identity, diagnostic in goals:
            with self.subTest(owner=identity):
                old, new = owner(before, identity), owner(after, identity)
                self.assertEqual(new_archives[identity], old_archives.get(identity, old))
                self.assertEqual(old["compileCheck"], new["compileCheck"])
                self.assertEqual(diagnostic, new["compileCheck"]["diagnostic"])
                for field in ("sourceFiles", "cmake", "outputCheck", "initialStandardInput"):
                    self.assertEqual(old.get(field), new.get(field), field)
                self.assertTrue(require_source_format(old, new))
                self.assertEqual(source_projection(new, old), old)
                # A visible previously formatted diagnostic is not a detached
                # initial contract. Authenticate both presentations against
                # the immutable source snapshot before comparing its payload.
                canonical = new_archives[identity]
                self.assertEqual(source_projection(old, canonical), canonical)
                self.assertEqual(source_projection(new, canonical), canonical)
                self.assertEqual(layout.state_fingerprint(canonical), layout.state_fingerprint(
                    source_projection(new, canonical)))
                self.assertNotEqual(old["initialCode"], new["initialCode"])
                if "projectFiles" in new:
                    self.assertEqual(new["initialCode"], project_file(new, "main.cpp")["initialCode"])

    def test_three_authentic_diagnostic_goals_keep_all_historical_owners(self):
        for publication, (before, after) in self.primary.items():
            with self.subTest(publication=publication):
                self.assert_history(before, after, tuple(zip(OWNERS, DIAGNOSTICS)))
                for identity in OWNERS:
                    old, new = owner(before, identity), owner(after, identity)
                    old_impl, new_impl = (project_file(block, "diferencia.cpp")["initialCode"]
                                          for block in (old, new))
                    self.assertNotEqual(old_impl, new_impl)
                    self.assertEqual(new_impl, old_impl.replace(OTHER_BLANK, OTHER_SPACED, 1))

    def test_real_publications_cover_first_and_subsequent_formatting(self):
        for publication, documents in self.documents.items():
            observed = {(before["id"], identity) for _, before, _, goals in documents
                        for identity, _ in goals}
            self.assertEqual(observed, set(layout.COMPILE_DIAGNOSTICS))
            for _, before, after, goals in documents:
                archived = {entry["id"] for entry in before["stateLayout"].get("sourceFormatRevisions", [])}
                identities = {identity for identity, _ in goals}
                if publication == "921e1df":
                    self.assertFalse(identities & archived)
                elif publication == "83e2d4e6":
                    self.assertTrue(identities <= archived)
                    self.assertEqual(before["stateLayout"]["sourceFormatRevisions"],
                                     after["stateLayout"]["sourceFormatRevisions"])

    def test_forgeries_are_rejected_even_if_editorial_digest_is_recomputed(self):
        def altered(after, mode, identity):
            document = deepcopy(after)
            candidate = owner(document, identity)
            if mode == "diagnostic":
                candidate["compileCheck"]["diagnostic"] = "explicit-assignment"
            elif mode == "source":
                project_file(candidate, "diferencia.cpp")["initialCode"] += "\nint forged = 1;"
            elif mode == "selection":
                candidate["sourceFiles"].append("unused.cpp")
            elif mode == "header":
                project_file(candidate, "diferencia.h")["path"] = "wrong.h"
            elif mode == "initial":
                candidate["initialCode"] = "int main() { return 0; }"
            elif mode == "archive":
                archive(document, identity)["initialCode"] += "// forged"
            elif mode == "answer":
                candidate["outputCheck"] = {"cases": [{"standardInput": "", "outputs": [""]}]}
            else:
                raise AssertionError(mode)
            if mode != "archive":
                rehash_final_edge(document, identity)
            return document
        for publication, (_, after) in self.primary.items():
            for identity in OWNERS:
                for mode in ("diagnostic", "source", "selection", "header", "initial", "archive", "answer"):
                    with self.subTest(publication=publication, owner=identity, mode=mode):
                        with self.assertRaises(layout.LayoutError):
                            layout.validate(altered(after, mode, identity))

    def test_all_remaining_reviewed_diagnostics_keep_lexical_format_and_history(self):
        for publication, documents in self.documents.items():
            for _, before, after, goals in documents:
                if before["id"] == LESSON_ID:
                    continue
                with self.subTest(publication=publication, lesson=before["id"]):
                    self.assert_history(before, after, goals)
                    for identity, diagnostic in goals:
                        for kind in ("logic", "diagnostic", "archive", "sourceSelection"):
                            with self.subTest(owner=identity, mutation=kind):
                                forged = deepcopy(after)
                                candidate = owner(forged, identity)
                                if kind == "logic":
                                    candidate["initialCode"] += "\nint forged = 1;"
                                elif kind == "diagnostic":
                                    candidate["compileCheck"]["diagnostic"] = (
                                        "const-member-call" if diagnostic == "explicit-assignment"
                                        else "explicit-assignment")
                                elif kind == "archive":
                                    archive(forged, identity)["initialCode"] += "\n// forged"
                                else:
                                    candidate.setdefault("sourceFiles", []).append("other.cpp")
                                if kind != "archive":
                                    rehash_final_edge(forged, identity)
                                with self.assertRaises(layout.LayoutError):
                                    layout.validate(forged)

    def test_later_editorial_and_visual_reorder_keep_original_snapshots(self):
        published = family(publication="83e2d4e6")
        _, formatted = self.primary["83e2d4e6"]
        changed = deepcopy(formatted)
        unrelated = next(identity for identity, block in active_owners(changed)[0].items()
                         if block.get("type") == "code" and block.get("code"))
        owner(changed, unrelated)["code"] = "\n" + owner(changed, unrelated)["code"]
        later_family = {**published, "es": formatted}
        later = layout.prepare_v2(changed, "es", later_family, formatted)
        self.assertEqual(later["stateLayout"]["editorial"][-1]["id"], unrelated)
        plan = deepcopy(later["stateLayout"]["current"])
        for identity in OWNERS:
            page = next(page for page in plan if identity in page["blocks"])
            page["blocks"].remove(identity)
            page["blocks"].insert(0, identity)
        moved = layout.reorder(later, plan)
        for identity in OWNERS:
            self.assertNotEqual(position(formatted, identity), position(moved, identity))
            self.assertEqual(layout.validate(formatted)[position(formatted, identity)],
                             layout.validate(moved)[position(moved, identity)])
        before, after = format_family({**published, "es": moved}, tuple(zip(OWNERS, DIAGNOSTICS)),
                                      implementation=True)
        self.assert_history(before, after, tuple(zip(OWNERS, DIAGNOSTICS)))
        self.assertEqual(before["stateLayout"]["sourceFormatRevisions"],
                         formatted["stateLayout"]["sourceFormatRevisions"])
        self.assertEqual(after["stateLayout"]["editorial"][:len(later["stateLayout"]["editorial"])],
                         later["stateLayout"]["editorial"])
        # Exercise the public exporter under the moved real publication too;
        # the named mutations must still target the selected diagnostic owner.
        real_asset = asset
        def current_asset(locale="es", relative=RELATIVE, publication="working-tree"):
            if Path(relative) == RELATIVE and locale == "es":
                return deepcopy(moved)
            return real_asset(locale, relative, publication)
        import compile_check_source_format_fixture as exporter
        with mock.patch(exporter.formatted_pair.__module__ + ".asset", side_effect=current_asset):
            exported = exporter.make("83e2d4e6")
        self.assertEqual(exported["u6.before"], moved)
        for name in ("u6.forged-goal", "u6.forged-archive", "u6.forged-source"):
            with self.assertRaises(layout.LayoutError):
                layout.validate(exported[name])

    def test_manifest_and_legacy_fixtures_keep_all_named_contracts(self):
        import compile_check_source_format_fixture as exporter
        expected_names = {f"{tag}.{kind}" for tag in ("u6", "u6more", "u9")
                          for kind in ("before", "after", "forged-source", "forged-goal", "forged-archive")}
        expected_names.add("all-diagnostics.manifest")
        expected_names.update(f"all-{i}.{stage}" for i, _ in enumerate(diagnostic_groups())
                              for stage in ("before", "after"))
        for publication in PUBLICATIONS:
            with self.subTest(publication=publication):
                exported = exporter.make(publication)
                self.assertEqual(set(exported), expected_names)
                manifest = exported["all-diagnostics.manifest"]
                self.assertEqual(manifest["version"], 1)
                observed = set()
                for record in manifest["lessons"]:
                    before = exported[record["before"].removesuffix(".json")]
                    after = exported[record["after"].removesuffix(".json")]
                    goals = tuple((entry["id"], entry["diagnostic"]) for entry in record["owners"])
                    self.assert_history(before, after, goals)
                    for identity, diagnostic in goals:
                        self.assertEqual(layout.COMPILE_DIAGNOSTICS[(before["id"], identity)]["diagnostic"],
                                         diagnostic)
                        self.assertNotIn((before["id"], identity), observed)
                        observed.add((before["id"], identity))
                    if before["id"] == LESSON_ID:
                        self.assertEqual(before, exported["u6.before"])
                        self.assertEqual(after, exported["u6.after"])
                self.assertEqual(observed, set(layout.COMPILE_DIAGNOSTICS))
                for name, document in exported.items():
                    if ".forged-" in name:
                        with self.assertRaises(layout.LayoutError):
                            layout.validate(document)

    def test_whole_publication_pins_reject_changed_bytes_and_unknown_aliases(self):
        for publication in PUBLICATION_COMMITS:
            for relative in PINNED_FAMILIES:
                for locale in ("es", "en", "fr"):
                    raw = asset_bytes(locale, Path(relative), publication)
                    with mock.patch(__name__ + ".publication_bytes", return_value=raw + b" "):
                        with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                            asset_bytes(locale, Path(relative), publication)
        with mock.patch(__name__ + ".publication_bytes") as fetch:
            with self.assertRaisesRegex(ValueError, "Unknown compile-check publication"):
                asset_bytes(publication="HEAD")
            with self.assertRaisesRegex(ValueError, "Unknown complete compile-check lesson"):
                asset_bytes(relative=Path("unpublished.json"), publication="921e1df")
            fetch.assert_not_called()

    def test_exported_before_is_the_full_selected_publication(self):
        import compile_check_source_format_fixture as exporter
        for publication in PUBLICATIONS:
            with tempfile.TemporaryDirectory(prefix="lgaed-compile-format-authoring-") as directory:
                output = Path(directory)
                exported = exporter.write_fixtures(output, publication)
                self.assertEqual(set(exported), {path.stem for path in output.iterdir()})
                for name, document in exported.items():
                    self.assertEqual(json.loads((output / (name + ".json")).read_bytes()), document)
                self.assertEqual(exported["u6.before"], asset(publication=publication))
                for record in exported["all-diagnostics.manifest"]["lessons"]:
                    document = exported[record["before"].removesuffix(".json")]
                    self.assertEqual(document, asset("es", lesson_relative(document["id"]), publication))

    def test_only_in_memory_sources_change(self):
        for publication in PUBLICATIONS:
            originals = {(locale, relative): asset_bytes(locale, Path(relative), publication)
                         for relative in PINNED_FAMILIES for locale in ("es", "en", "fr")}
            for _, before, after, _ in formatted_documents(publication):
                self.assertNotEqual(before, after)
            for (locale, relative), raw in originals.items():
                self.assertEqual(raw, asset_bytes(locale, Path(relative), publication))


if __name__ == "__main__":
    unittest.main()

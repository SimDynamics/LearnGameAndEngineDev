"""CMake File API graph authoring without rewriting original lesson blocks."""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import json
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "construccion_reproducible/lesson_objetivos_dependencias_y_enlace.json"
)


def original() -> dict:
    return json.loads(LESSON.read_text(encoding="utf-8"))


def declaration() -> list[dict]:
    return [{
        "activity": "block_0004_0004",
        "project": "Preparacion",
        "minimumCmakeVersion": "3.20",
        "targets": [
            {"name": "revisar", "type": "EXECUTABLE",
             "sources": ["main.cpp"], "exactSources": True,
             "linksTo": ["faltantes"], "explicitCxx20": True,
             "explicitCxxExtensionsOff": True},
            {"name": "faltantes", "type": "STATIC_LIBRARY",
             "sources": ["faltantes.cpp"], "exactSources": True,
             "explicitCxx20": True, "explicitCxxExtensionsOff": True},
        ],
    }, {
        "activity": "block_0008_0003",
        "project": "Pases",
        "minimumCmakeVersion": "3.20",
        "targets": [
            {"name": "portero", "type": "EXECUTABLE",
             "sources": ["acceso.cpp"], "linksTo": ["regla"],
             "exactSources": True, "explicitCxx20": True,
             "explicitCxxExtensionsOff": True},
            {"name": "regla", "type": "STATIC_LIBRARY",
             "sources": ["regla.cpp"], "exactSources": True,
             "explicitCxx20": True, "explicitCxxExtensionsOff": True},
        ],
    }]


class CmakeGraphChecksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = original()
        self.after = deepcopy(self.before)
        self.after["compilerCmakeGraphChecks"] = declaration()

    def test_append_only_check_keeps_original_payloads_and_addresses(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_owner_target_sources_and_link_relationships_are_bounded(self):
        invalid = [
            lambda d: d["compilerCmakeGraphChecks"].append(
                deepcopy(d["compilerCmakeGraphChecks"][0])),
            lambda d: d["compilerCmakeGraphChecks"][0].update(
                activity="no_such_cpp_compiler"),
            lambda d: d["compilerCmakeGraphChecks"][0].update(
                activity="block_0000_0000"),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"].clear(),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                type="STATIC_LIBRARY"),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                exactSources="true"),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                sources=["missing.cpp"]),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                sources=["main.cpp", "main.cpp"]),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                linksTo=["revisar"]),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                linksTo=["missing"]),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                linksTo=["faltantes", "faltantes"]),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                mystery="not supported"),
            lambda d: d["compilerCmakeGraphChecks"][0]["targets"].append(
                deepcopy(d["compilerCmakeGraphChecks"][0]["targets"][0])),
            lambda d: d["compilerCmakeGraphChecks"][0].update(
                unknown="not supported"),
        ]
        for mutate in invalid:
            changed = deepcopy(self.after)
            mutate(changed)
            with self.subTest(mutate=mutate), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_authored_compiler_flags_are_bounded_and_additive(self):
        reviewed = deepcopy(self.after)
        reviewed["compilerCmakeGraphChecks"][0]["targets"][1][
            "authoredCompileOptions"] = ["/W4", "/permissive-"]
        state.validate(reviewed)
        # New requirements may be published with the initial root rubric.
        state.validate_transition(self.before, reviewed)
        # They cannot be injected later into an already published rubric:
        # that would redefine the student's historical completion contract.
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, reviewed)
        self.assertEqual(reviewed["pages"], self.before["pages"])
        self.assertEqual(reviewed["stateLayout"], self.before["stateLayout"])
        for bad in ([], ["/W4", "/W4"], [""], ["W4"], [" /W4"],
                    ["SHELL:/W4"], ["/W4;message(FATAL_ERROR)"],
                    ["/W4"] * 9, [10], "not-an-array"):
            changed = deepcopy(reviewed)
            changed["compilerCmakeGraphChecks"][0]["targets"][1][
                "authoredCompileOptions"] = bad
            with self.subTest(bad=bad), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_existing_graph_cannot_be_removed_or_redefined(self):
        removed = deepcopy(self.after)
        removed.pop("compilerCmakeGraphChecks")
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, removed)
        altered = deepcopy(self.after)
        altered["compilerCmakeGraphChecks"][0]["targets"][0]["linksTo"] = []
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, altered)

    def test_second_original_owner_can_be_appended_without_changing_first(self):
        only_first = deepcopy(self.after)
        only_first["compilerCmakeGraphChecks"] = declaration()[:1]
        state.validate(only_first)
        state.validate_transition(self.before, only_first)
        state.validate_transition(only_first, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_existing_owners_cannot_be_reordered_or_moved(self):
        reordered = deepcopy(self.after)
        reordered["compilerCmakeGraphChecks"].reverse()
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, reordered)
        shortened = deepcopy(self.after)
        shortened["compilerCmakeGraphChecks"].pop()
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, shortened)

    def test_non_spanish_mapped_lesson_cannot_receive_a_graph(self):
        invalid = deepcopy(self.after)
        invalid["stateLayout"]["locale"] = "fr"
        with self.assertRaises(state.LayoutError):
            state.validate(invalid)


# U2L1 begins with an empty CMakeLists.txt. The new requirement must be
# attached to its existing owner without moving the student's saved draft.
FIRST_LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "construccion_reproducible/lesson_fuentes_objetivos_y_fases.json"
)


class FirstProjectGraphChecksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = json.loads(FIRST_LESSON.read_text(encoding="utf-8"))
        self.after = deepcopy(self.before)
        self.after["compilerCmakeGraphChecks"] = [{
            "activity": "block_0009_0003",
            "project": "InventarioLocal",
            "minimumCmakeVersion": "3.20",
            "targets": [{
                "name": "comprobar",
                "type": "EXECUTABLE",
                "sources": ["inventario.cpp"],
                "exactSources": True,
                "explicitCxx20": True,
                "explicitCxxExtensionsOff": True,
            }],
        }]

    def test_original_l1_editor_can_receive_a_root_only_graph(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_owner_and_compiled_source_must_be_original(self):
        def foreign_source(document):
            document["compilerCmakeGraphChecks"][0]["targets"][0]["sources"] = [
                "main.cpp"]

        def wrong_owner(document):
            document["compilerCmakeGraphChecks"][0]["activity"] = "block_0008_0003"

        def wrong_target_type(document):
            document["compilerCmakeGraphChecks"][0]["targets"][0]["type"] = (
                "STATIC_LIBRARY")

        for change in (foreign_source, wrong_owner, wrong_target_type):
            candidate = deepcopy(self.after)
            change(candidate)
            with self.subTest(change=change.__name__):
                with self.assertRaises(state.LayoutError):
                    state.validate(candidate)

    def test_minimum_version_has_bounded_numeric_components(self):
        for version in ("3", "3..20", "3.20-rc1", "3.20.0.0.0",
                        "4294967296.20", "3.x", ""):
            candidate = deepcopy(self.after)
            candidate["compilerCmakeGraphChecks"][0]["minimumCmakeVersion"] = (
                version)
            with self.subTest(version=version):
                with self.assertRaises(state.LayoutError):
                    state.validate(candidate)
        for invalid in (320, None, True):
            candidate = deepcopy(self.after)
            candidate["compilerCmakeGraphChecks"][0]["minimumCmakeVersion"] = invalid
            with self.subTest(invalid=invalid):
                with self.assertRaises(state.LayoutError):
                    state.validate(candidate)
        # Equivalent numeric spelling can be a new declaration, but never
        # silently mutate a requirement already authenticated and published.
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][0]["minimumCmakeVersion"] = (
            "3.20.0")
        state.validate(candidate)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, candidate)

    def test_explicit_language_rules_are_boolean_and_immutable(self):
        for field in ("explicitCxx20", "explicitCxxExtensionsOff"):
            for value in ("true", 1, None):
                candidate = deepcopy(self.after)
                candidate["compilerCmakeGraphChecks"][0]["targets"][0][field] = value
                with self.subTest(field=field, value=value):
                    with self.assertRaises(state.LayoutError):
                        state.validate(candidate)
            candidate = deepcopy(self.after)
            candidate["compilerCmakeGraphChecks"][0]["targets"][0][field] = False
            state.validate(candidate)
            with self.assertRaises(state.LayoutError):
                state.validate_transition(self.after, candidate)

    def test_published_first_graph_cannot_be_replaced_or_removed(self):
        updated = deepcopy(self.after)
        updated["compilerCmakeGraphChecks"][0]["project"] = "OtroInventario"
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, updated)
        updated = deepcopy(self.after)
        updated.pop("compilerCmakeGraphChecks")
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, updated)


# U2L3's original ON editor must also prove a separate default-OFF
# configuration. Appending the root contract must not rewrite the exercise.
OPTION_LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "construccion_reproducible/lesson_opciones_y_revision_de_la_configuracion.json"
)


class OriginalCmakeOptionSelectionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = json.loads(OPTION_LESSON.read_text(encoding="utf-8"))
        self.after = deepcopy(self.before)
        self.after["compilerCmakeGraphChecks"] = [{
            "activity": "block_0011_0003",
            "project": "Cotizaciones",
            "minimumCmakeVersion": "3.20",
            "targets": [
                {"name": "cotizar", "type": "EXECUTABLE",
                 "sources": ["muestra.cpp"], "linksTo": ["tarifas"],
                 "exactSources": True, "explicitCxx20": True,
                 "explicitCxxExtensionsOff": True},
                {"name": "tarifas", "type": "STATIC_LIBRARY",
                 "sources": ["especial.cpp"], "exactSources": True,
                 "explicitCxx20": True, "explicitCxxExtensionsOff": True},
            ],
            "optionSelection": {
                "name": "TARIFA_ESPECIAL", "default": "OFF",
                "target": "tarifas", "offSource": "normal.cpp",
                "offStdout": "0:6:15",
            },
        }]

    def test_original_option_editor_preserves_all_historical_payloads(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_invalid_option_provenance_or_source_is_rejected(self):
        changes = [
            lambda item: item["optionSelection"].update(default="ON"),
            lambda item: item["optionSelection"].update(name="OTRA_OPCION"),
            lambda item: item["optionSelection"].update(target="cotizar"),
            lambda item: item["optionSelection"].update(offSource="externo.cpp"),
            lambda item: item["optionSelection"].update(offSource="especial.cpp"),
            lambda item: item["optionSelection"].update(offStdout=42),
            lambda item: item["optionSelection"].update(unknown=True),
            lambda item: item["optionSelection"].pop("offStdout"),
        ]
        for change in changes:
            altered = deepcopy(self.after)
            change(altered["compilerCmakeGraphChecks"][0])
            with self.subTest(change=change):
                with self.assertRaises(state.LayoutError):
                    state.validate(altered)

    def test_published_option_cannot_be_mutated_or_removed(self):
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][0]["optionSelection"][
            "offStdout"] = "0:4:10"
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, changed)
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][0].pop("optionSelection")
        state.validate(changed)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, changed)


# U2L4 preserves the original clients and historical answers while
# binding explicit PUBLIC and PRIVATE roots to their existing CMake editors.
INCLUDE_LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "construccion_reproducible/lesson_cabeceras_y_requisitos_de_uso.json"
)


class OriginalCmakeIncludeScopeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = json.loads(INCLUDE_LESSON.read_text(encoding="utf-8"))
        self.after = deepcopy(self.before)
        owners = [
            ("block_0004_0002", False, False),
            ("block_0007_0008", True, False),
            ("block_0007_0011", True, False),
            ("block_0009_0004", False, True),
        ]
        self.after["compilerCmakeGraphChecks"] = [
            {
                "activity": owner,
                **({} if aforo else {"project": "Tiempos"}),
                "minimumCmakeVersion": "3.20",
                "sourceIntegrity": {
                    "preserveNonCmake": True,
                    **({"removePrefix": {
                        "path": "app/main.cpp",
                        "text": '#include "ajuste.h"\n',
                    }} if owner == "block_0007_0011" else {}),
                },
                "targets": [
                    {"name": "consultar_aforo" if aforo else "medir",
                     "type": "EXECUTABLE",
                     "sources": ["programas/consulta.cpp" if aforo else
                                 "app/main.cpp"],
                     "exactSources": True,
                     "linksTo": ["aforo" if aforo else "preparacion"],
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": "aforo" if aforo else "preparacion",
                     "type": "STATIC_LIBRARY",
                     "sources": ["motor/cupo.cpp" if aforo else
                                 "src/entrega.cpp"],
                     "exactSources": True,
                     "publicIncludeRoots": ["publico" if aforo else "include"],
                     **({"privateIncludeRoots": ["interno"]} if private else {}),
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                ],
            }
            for owner, private, aforo in owners
        ]

    def test_root_scopes_preserve_original_lessons_and_progress(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_invalid_include_roots_are_rejected(self):
        def change_root(document, value):
            document["compilerCmakeGraphChecks"][0]["targets"][1][
                "publicIncludeRoots"] = value

        for invalid in ([], ["include", "include"], ["../include"],
                        ["/include"], ["include/.."], ["externo"],
                        ["src"], ["inexistente"], "include", [1], [None]):
            candidate = deepcopy(self.after)
            change_root(candidate, invalid)
            with self.subTest(value=invalid):
                with self.assertRaises(state.LayoutError):
                    state.validate(candidate)
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][0]["targets"][0][
            "publicIncludeRoots"] = ["include"]
        with self.assertRaises(state.LayoutError):
            state.validate(candidate)
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][0]["targets"][1][
            "privateIncludeRoots"] = ["include"]
        with self.assertRaises(state.LayoutError):
            state.validate(candidate)
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][0]["targets"][0][
            "linksTo"] = []
        with self.assertRaises(state.LayoutError):
            state.validate(candidate)

    def test_non_cmake_source_integrity_is_original_and_immutable(self):
        baseline = self.after["compilerCmakeGraphChecks"]
        for value in (None, {}, {"preserveNonCmake": False},
                      {"preserveNonCmake": 1},
                      {"preserveNonCmake": True, "unknown": 1},
                      {"preserveNonCmake": True, "removePrefix": {
                          "path": "app/main.cpp", "text": "not-a-prefix"}}):
            altered = deepcopy(self.after)
            altered["compilerCmakeGraphChecks"][2]["sourceIntegrity"] = value
            with self.subTest(invalid=value):
                with self.assertRaises(state.LayoutError):
                    state.validate(altered)
        self.assertEqual(
            baseline[2]["sourceIntegrity"]["removePrefix"]["text"],
            '#include "ajuste.h"\n')
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][2]["sourceIntegrity"] = {
            "preserveNonCmake": True}
        state.validate(changed)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, changed)

    def test_published_public_private_contracts_are_immutable(self):
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][1]["targets"][1].pop(
            "privateIncludeRoots")
        state.validate(changed)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, changed)
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][0]["targets"][1][
            "publicIncludeRoots"] = ["src"]
        with self.assertRaises(state.LayoutError):
            state.validate(changed)



# U3L2 requires a real child CMakeLists rather than a root-only library
# that happens to print the same seven case results.
REORGANIZE_LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json"
)


class OriginalCmakeSubdirectoryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = json.loads(REORGANIZE_LESSON.read_text(encoding="utf-8"))
        self.after = deepcopy(self.before)
        self.after["compilerCmakeGraphChecks"] = [
            {
                "activity": "block_0006_0003",
                "project": "EnviosComprobables",
                "minimumCmakeVersion": "3.20",
                "sourceIntegrity": {"preserveNonCmake": True},
                "configureMessageOrder": [
                    {"path": "CMakeLists.txt", "text": "raiz_antes"},
                    {"path": "componente/CMakeLists.txt", "text": "componente"},
                    {"path": "CMakeLists.txt", "text": "raiz_despues"},
                ],
                "targets": [
                    {"name": "comprobar_cajas", "type": "EXECUTABLE",
                     "sources": ["pruebas/cajas_casos.cpp"],
                     "linksTo": ["envios"], "exactSources": True,
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": "consultar_cajas", "type": "EXECUTABLE",
                     "sources": ["app/consulta.cpp"], "linksTo": ["envios"],
                     "exactSources": True, "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": "envios", "type": "STATIC_LIBRARY",
                     "sources": ["componente/src/cajas.cpp"],
                     "sourceDirectory": "componente",
                     "publicIncludeRoots": ["componente/publico"],
                     "exactSources": True, "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                ],
            },
            {
                "activity": "block_0008_0003",
                "project": "LotesLocales",
                "minimumCmakeVersion": "3.20",
                "sourceIntegrity": {"preserveNonCmake": True},
                "targets": [
                    {"name": "verificar_lotes", "type": "EXECUTABLE",
                     "sources": ["pruebas/lotes_casos.cpp"],
                     "linksTo": ["lotes"], "exactSources": True,
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": "consultar_lotes", "type": "EXECUTABLE",
                     "sources": ["programa/consulta.cpp"],
                     "linksTo": ["lotes"], "exactSources": True,
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": "lotes", "type": "STATIC_LIBRARY",
                     "sources": ["codigo/lotes/motor/cantidad.cpp"],
                     "sourceDirectory": "codigo/lotes",
                     "publicIncludeRoots": ["codigo/lotes/publico"],
                     "exactSources": True, "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                ],
            },
        ]

    def test_original_subproject_owners_preserve_blocks_and_history(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_child_directory_must_be_real_original_cmake(self):
        for invalid in ("", ".", "..", "../componente", "/componente",
                        "componente/", "componente/..", "otro",
                        "componente/src", "componente//"):
            edited = deepcopy(self.after)
            edited["compilerCmakeGraphChecks"][0]["targets"][2][
                "sourceDirectory"] = invalid
            with self.subTest(directory=invalid):
                with self.assertRaises(state.LayoutError):
                    state.validate(edited)
        for invalid in (None, 1, [], True):
            edited = deepcopy(self.after)
            edited["compilerCmakeGraphChecks"][0]["targets"][2][
                "sourceDirectory"] = invalid
            with self.subTest(type_value=invalid):
                with self.assertRaises(state.LayoutError):
                    state.validate(edited)
        edited = deepcopy(self.after)
        edited["compilerCmakeGraphChecks"][0]["targets"][0][
            "sourceDirectory"] = "componente"
        with self.assertRaises(state.LayoutError):
            state.validate(edited)

    def test_status_message_order_is_bounded_and_immutable(self):
        invalid_messages = (
            None, [],
            [{"path": "CMakeLists.txt", "text": ""}],
            [{"path": "CMakeLists.txt", "text": 1}],
            [{"path": "CMakeLists.txt", "text": "a\n"}],
            [{"path": "inexistente/CMakeLists.txt", "text": "componente"}],
            [{"path": "CMakeLists.txt", "text": "raiz_antes",
              "unknown": 1}],
        )
        for value in invalid_messages:
            changed = deepcopy(self.after)
            changed["compilerCmakeGraphChecks"][0][
                "configureMessageOrder"] = value
            with self.subTest(value=value):
                with self.assertRaises(state.LayoutError):
                    state.validate(changed)
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][0][
            "configureMessageOrder"].reverse()
        state.validate(changed)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, changed)

    def test_source_directory_cannot_change_after_publication(self):
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][0]["targets"][2].pop(
            "sourceDirectory")
        state.validate(changed)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, changed)
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"][1]["targets"][2][
            "sourceDirectory"] = "componente"
        with self.assertRaises(state.LayoutError):
            state.validate(changed)


# U3L4 teaches real CMake variable/list propagation. Compiler defaults
# already set extensions OFF, so stdout and property values are insufficient.
TARGET_LIST_LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_datos_y_listas_de_objetivos.json"
)


class OriginalCmakeTargetListTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = json.loads(TARGET_LIST_LESSON.read_text(encoding="utf-8"))
        self.after = deepcopy(self.before)
        owners = [
            ("block_0003_0004", False, False),
            ("block_0004_0007", False, False),
            ("block_0006_0004", True, False),
            ("block_0008_0004", False, True),
        ]
        checks = []
        for owner, append, lots in owners:
            consumer = "consultar_lotes" if lots else "consultar_cajas"
            checker = "verificar_lotes" if lots else "comprobar_cajas"
            library = "lotes" if lots else "envios"
            checks.append({
                "activity": owner,
                "project": "LotesLocales" if lots else "EnviosComprobables",
                "minimumCmakeVersion": "3.20",
                "sourceIntegrity": {"preserveNonCmake": True},
                "targets": [
                    {"name": checker, "type": "EXECUTABLE",
                     "sources": ["pruebas/lotes_casos.cpp" if lots else
                                 "pruebas/cajas_casos.cpp"],
                     "linksTo": [library], "exactSources": True,
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": consumer, "type": "EXECUTABLE",
                     "sources": ["programa/consulta.cpp" if lots else
                                 "app/consulta.cpp"],
                     "linksTo": [library], "exactSources": True,
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                    {"name": library, "type": "STATIC_LIBRARY",
                     "sources": ["codigo/lotes/motor/cantidad.cpp" if lots else
                                 "componente/src/cajas.cpp"],
                     "sourceDirectory": "codigo/lotes" if lots else "componente",
                     "publicIncludeRoots": [
                         "codigo/lotes/publico" if lots else "componente/publico"],
                     "exactSources": True,
                     "explicitCxx20": True,
                     "explicitCxxExtensionsOff": True},
                ],
                "targetList": {
                    "targets": [consumer, checker], "requireAppend": append,
                },
            })
        self.after["compilerCmakeGraphChecks"] = checks

    def test_append_only_rubrics_preserve_all_four_original_editors(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.before["pages"], self.after["pages"])
        self.assertEqual(self.before["stateLayout"], self.after["stateLayout"])

    def test_list_contract_rejects_invalid_targets_and_flags(self):
        errors = [
            lambda c: c.update(targetList={"targets": [], "requireAppend": False}),
            lambda c: c.update(targetList={"targets": ["consultar_cajas"],
                                          "requireAppend": False}),
            lambda c: c.update(targetList={"targets": [
                "consultar_cajas", "consultar_cajas"], "requireAppend": False}),
            lambda c: c.update(targetList={"targets": [
                "consultar_cajas", "envios"], "requireAppend": False}),
            lambda c: c.update(targetList={"targets": [
                "consultar_cajas", "externo"], "requireAppend": False}),
            lambda c: c.update(targetList={"targets": [
                "consultar_cajas", "comprobar_cajas"], "requireAppend": "yes"}),
            lambda c: c.update(targetList={"targets": [
                "consultar_cajas", "comprobar_cajas"], "requireAppend": False,
                                          "unknown": True}),
            lambda c: c.update(targetList={"targets": [
                "consultar_cajas", "comprobar_cajas"], "requireAppend": None}),
            lambda c: c["targets"][1].update(explicitCxxExtensionsOff=False),
        ]
        for change in errors:
            candidate = deepcopy(self.after)
            change(candidate["compilerCmakeGraphChecks"][0])
            with self.subTest(change=change):
                with self.assertRaises(state.LayoutError):
                    state.validate(candidate)

    def test_append_requirement_cannot_be_silently_removed(self):
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][2]["targetList"][
            "requireAppend"] = False
        state.validate(candidate)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, candidate)
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][2].pop("targetList")
        state.validate(candidate)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, candidate)


EXPRESSION_LESSONS = (
    ("lesson_leer_avisos_sin_sustituir_las_pruebas.json", "block_0002_0004",
     "cantidad.cpp", "(0 <= dato) <= 20", "cantidades", "comprobar_cantidad",
     "comprobar.cpp"),
    ("lesson_integrar_un_proyecto_y_sostener_su_evidencia.json", "block_0006_0008",
     "src/ajuste.cpp", "valor - superior", "ajustes", "comprobar_ajustes",
     "pruebas/casos.cpp"),
)


def expression_declaration(case) -> dict:
    _, owner, path, expression, library, checker, driver = case
    return {
        "activity": owner,
        "sourceIntegrity": {
            "preserveNonCmake": True,
            "replaceExpression": {"path": path, "original": expression},
        },
        "targets": [
            {"name": checker, "type": "EXECUTABLE", "sources": [driver],
             "linksTo": [library], "exactSources": True},
            {"name": library, "type": "STATIC_LIBRARY", "sources": [path],
             "exactSources": True},
        ],
    }


def expression_fixture(current: dict, expected: dict) -> tuple[dict, dict]:
    """Derive two isolated QA projections, never rewrite a live publication.

    Validate the complete live lesson first. Work may already have activated
    a more complete graph; retain every validated requirement and every other
    owner, but never disguise an invalid or different expression permission.
    The QA graph is appended to a separate baseline, even when its published
    owner occurred earlier in the list. This is not a claimed transition from
    the current publication to that reordered QA projection.
    """
    state.validate(current)
    owner = expected["activity"]
    records = current.get("compilerCmakeGraphChecks", [])
    published = next((entry for entry in records if entry["activity"] == owner), None)
    declaration = deepcopy(expected if published is None else published)
    if published is not None:
        integrity = published.get("sourceIntegrity", {})
        state.require(integrity.get("preserveNonCmake") is True and
                      integrity.get("replaceExpression") ==
                      expected["sourceIntegrity"]["replaceExpression"],
                      "The live repair fixture has another source-integrity contract")
    before = deepcopy(current)
    retained = [deepcopy(entry) for entry in records if entry["activity"] != owner]
    if retained:
        before["compilerCmakeGraphChecks"] = retained
    else:
        before.pop("compilerCmakeGraphChecks", None)
    after = deepcopy(before)
    after.setdefault("compilerCmakeGraphChecks", []).append(declaration)
    state.validate(before)
    state.validate(after)
    state.validate_transition(before, after)
    return before, after


class OriginalCmakeExpressionRepairTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lessons = []
        for case in EXPRESSION_LESSONS:
            path = ROOT / (
                "app/src/main/assets/content/es/programming/programming_practice_tools/"
                "comprobaciones_repetibles") / case[0]
            current = json.loads(path.read_text(encoding="utf-8"))
            before, after = expression_fixture(current, expression_declaration(case))
            cls.lessons.append((case, current, before, after))

    def test_original_u3_repairs_add_only_root_contracts(self):
        for case, current, before, after in self.lessons:
            with self.subTest(lesson=case[0]):
                self.assertEqual(current["pages"], before["pages"])
                self.assertEqual(current["pages"], after["pages"])
                self.assertEqual(current["stateLayout"], before["stateLayout"])
                self.assertEqual(current["stateLayout"], after["stateLayout"])
                self.assertEqual(state.validate(before), state.validate(after))
                for identity, block in state._blocks(before).items():
                    self.assertEqual(state.state_fingerprint(block),
                                     state.state_fingerprint(state._blocks(after)[identity]))
                owner = state._blocks(current)[case[1]]
                source = next(f["initialCode"] for f in owner["projectFiles"]
                              if f["path"] == case[2])
                self.assertTrue(state._cpp_expression_repair_valid(case[2], source, case[3]))

    def test_absence_keeps_the_existing_preserve_all_source_contract(self):
        for case, _, before, after in self.lessons:
            with self.subTest(lesson=case[0]):
                ordinary = deepcopy(after)
                ordinary["compilerCmakeGraphChecks"][-1]["sourceIntegrity"].pop(
                    "replaceExpression")
                state.validate(ordinary)
                state.validate_transition(before, ordinary)
                self.assertEqual(ordinary["pages"], before["pages"])
                self.assertEqual(ordinary["stateLayout"], before["stateLayout"])
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(ordinary, after)

    def test_expression_permission_has_exact_bounded_fields_and_required_source(self):
        invalid = [
            lambda rule: rule.update(sourceIntegrity={"replaceExpression": {
                "path": "cantidad.cpp", "original": "(0 <= dato) <= 20"}}),
            lambda rule: rule["sourceIntegrity"].update(preserveNonCmake=False),
            lambda rule: rule["sourceIntegrity"].update(preserveNonCmake=1),
            lambda rule: rule["sourceIntegrity"].update(removePrefix={
                "path": "cantidad.cpp", "text": '#include "cantidad.hpp"\n'}),
            lambda rule: rule["sourceIntegrity"].update(replaceExpression=None),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(unknown=True),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].pop("original"),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(original=1),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(path=1),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(path="missing.cpp"),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(path="CMakeLists.txt"),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(path="cantidad.hpp"),
            lambda rule: rule["targets"][1].update(sources=[]),
        ]
        for change in invalid:
            candidate = deepcopy(self.lessons[0][3])
            change(candidate["compilerCmakeGraphChecks"][-1])
            with self.subTest(change=change), self.assertRaises(state.LayoutError):
                state.validate(candidate)

    def test_selector_must_be_a_unique_real_expression_not_a_text_occurrence(self):
        expression = "valor - superior"
        valid = (
            "int f() { return valor - superior; }",
            '// valor - superior\nint f() { return valor /* nota */ - superior; }',
            'const char* nota = "valor - superior"; int f() { return valor - superior; }',
            'const char* nota = R"tag(valor - superior)tag"; int f() { return valor - superior; }',
            '// español 😀\r\nint f() { return valor - superior; }\r\n',
        )
        for source in valid:
            with self.subTest(source=source):
                self.assertTrue(state._cpp_expression_repair_valid("src/ajuste.cpp", source, expression))
        invalid = (
            "// valor - superior\nint f() { return 0; }",
            "/* valor - superior */ int f() { return 0; }",
            'const char* nota = "valor - superior";',
            'const char* nota = R"tag(valor - superior)tag";',
            "#define DIFERENCIA valor - superior\nint f() { return DIFERENCIA; }",
            "int f() { return valor - superior; } int g() { return valor - superior; }",
            "int f() { return valor - superioridad; }",
            "int f() { return valor - \\\nsuperior; }",
            "int f() { return valor - superior; }\x00",
            "int f() { return valor - superior; } int linea = __LINE__;",
            "/* comentario abierto valor - superior",
        )
        for source in invalid:
            with self.subTest(source=source):
                self.assertFalse(state._cpp_expression_repair_valid("src/ajuste.cpp", source, expression))
        self.assertFalse(state._cpp_expression_repair_valid(
            "repair.cpp", "int f() { return 1א; }", "1"))
        self.assertFalse(state._cpp_expression_repair_valid(
            "repair.cpp", 'const char* note = R"' + "é" * 9 + "(x)" + "é" * 9 +
            '"; int f() { return valor - superior; }', expression))

    def test_selector_grammar_cannot_authorize_statements_calls_or_directives(self):
        expressions = (
            "", " ", "// valor - superior", "valor - superior; return 0",
            "valor - superior, 0", "valor = superior", "++valor", "valor--",
            "salir()", "std::exit(0)", "valor[0]", "valor.miembro", "valor->dato",
            "static_cast<int>(valor)", "throw valor", "int valor", "{}",
            "<% 0 %>", "#define valor 0", "valor ? superior", "(valor - superior",
            '"valor - superior"', 'R"(valor - superior)"', "__LINE__", "__dato",
            "valor\x00", "a" * 513, "'" + "é" * 256 + "'",
        )
        for expression in expressions:
            source = "int f() { return " + expression + "; }"
            with self.subTest(expression=expression):
                self.assertFalse(state._cpp_expression_repair_valid("repair.cpp", source, expression))

    def test_safe_expression_spellings_comments_and_utf8_lengths_match_native_contract(self):
        expressions = (
            "(0 <= dato) <= 20", "valor - superior", "0 <= dato && dato <= 20",
            "not (dato < 0 or dato > 20)", "dato < 0 ? false : dato <= 20",
            "(valor /* diferencia */ - superior)", "dato + '\\n'", "~dato bitand 7",
        )
        for expression in expressions:
            source = '#include "original.hpp"\r\nint f() { return ' + expression + "; }"
            with self.subTest(expression=expression):
                self.assertTrue(state._cpp_expression_repair_valid("src/Repair.CPP", source, expression))
        source = "int f() { return valor - superior; }"
        for path in ("", "/src/ajuste.cpp", "../ajuste.cpp", "src/../ajuste.cpp",
                     "src//ajuste.cpp", "src/ajuste.cpp/", "src\\ajuste.cpp",
                     ".hidden.cpp", "C:ajuste.cpp", "ajusté.cpp", "ajuste.hpp",
                     "src./ajuste.cpp", "AUX.cpp", "src/com1.cpp", "LPT9/ajuste.cpp",
                     "a" * 253 + ".cpp"):
            with self.subTest(path=path):
                self.assertFalse(state._cpp_expression_repair_valid(path, source, "valor - superior"))

    def test_published_permission_cannot_be_removed_mutated_or_switched(self):
        for case, _, _, after in self.lessons:
            changes = (
                lambda value: value["compilerCmakeGraphChecks"][-1]["sourceIntegrity"].pop("replaceExpression"),
                lambda value: value["compilerCmakeGraphChecks"][-1]["sourceIntegrity"]["replaceExpression"].update(
                    original=case[3].replace(" ", "")),
                lambda value: value["compilerCmakeGraphChecks"].pop(),
            )
            for change in changes:
                candidate = deepcopy(after)
                change(candidate)
                if not candidate["compilerCmakeGraphChecks"]:
                    candidate.pop("compilerCmakeGraphChecks")
                state.validate(candidate)
                with self.subTest(lesson=case[0], change=change), self.assertRaises(state.LayoutError):
                    state.validate_transition(after, candidate)

    def test_fixture_accepts_future_complete_activation_without_changing_live_data(self):
        for case, _, _, after in self.lessons:
            current = deepcopy(after)
            current["compilerCmakeGraphChecks"][-1]["targets"][0]["explicitCxx20"] = True
            snapshot = deepcopy(current)
            before, candidate = expression_fixture(current, expression_declaration(case))
            self.assertEqual(current, snapshot)
            self.assertEqual(candidate, current)
            self.assertEqual(before["pages"], current["pages"])
            self.assertEqual(before["stateLayout"], current["stateLayout"])

    def test_fixture_retains_other_owners_and_does_not_claim_a_reorder_as_publication(self):
        current = original()
        state.validate(current)
        rules = declaration()
        rules[0]["sourceIntegrity"] = {
            "preserveNonCmake": True,
            "replaceExpression": {"path": "faltantes.cpp", "original": "necesarias - disponibles"},
        }
        current = deepcopy(current)
        current["compilerCmakeGraphChecks"] = rules
        snapshot = deepcopy(current)
        before, candidate = expression_fixture(current, rules[0])
        self.assertEqual(current, snapshot)
        self.assertEqual(before["compilerCmakeGraphChecks"], rules[1:])
        self.assertEqual(candidate["compilerCmakeGraphChecks"], [rules[1], rules[0]])
        state.validate_transition(before, candidate)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(current, candidate)

    def test_fixture_never_hides_another_or_invalid_published_permission(self):
        case, _, _, after = self.lessons[0]
        for mutate in (
            lambda rule: rule["sourceIntegrity"].pop("replaceExpression"),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(original="0<=dato"),
            lambda rule: rule["sourceIntegrity"]["replaceExpression"].update(original="not_present"),
            lambda rule: rule["targets"][0].update(type="UNKNOWN"),
        ):
            candidate = deepcopy(after)
            mutate(candidate["compilerCmakeGraphChecks"][-1])
            snapshot = deepcopy(candidate)
            with self.subTest(mutate=mutate), self.assertRaises(state.LayoutError):
                expression_fixture(candidate, expression_declaration(case))
            self.assertEqual(candidate, snapshot)


if __name__ == "__main__":
    unittest.main()

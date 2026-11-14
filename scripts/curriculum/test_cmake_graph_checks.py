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


if __name__ == "__main__":
    unittest.main()

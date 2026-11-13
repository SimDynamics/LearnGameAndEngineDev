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
        "targets": [
            {"name": "revisar", "type": "EXECUTABLE",
             "sources": ["main.cpp"], "exactSources": True,
             "linksTo": ["faltantes"]},
            {"name": "faltantes", "type": "STATIC_LIBRARY",
             "sources": ["faltantes.cpp"], "exactSources": True},
        ],
    }, {
        "activity": "block_0008_0003",
        "project": "Pases",
        "targets": [
            {"name": "portero", "type": "EXECUTABLE",
             "sources": ["acceso.cpp"], "linksTo": ["regla"],
             "exactSources": True},
            {"name": "regla", "type": "STATIC_LIBRARY",
             "sources": ["regla.cpp"], "exactSources": True},
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
        candidate = deepcopy(self.after)
        candidate["compilerCmakeGraphChecks"][0]["minimumCmakeVersion"] = 320
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

    def test_published_first_graph_cannot_be_replaced_or_removed(self):
        updated = deepcopy(self.after)
        updated["compilerCmakeGraphChecks"][0]["project"] = "OtroInventario"
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, updated)
        updated = deepcopy(self.after)
        updated.pop("compilerCmakeGraphChecks")
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, updated)


if __name__ == "__main__":
    unittest.main()

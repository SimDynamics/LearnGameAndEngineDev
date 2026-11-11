"""The published U3 History owner retains its original state when declared."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "stable_state_layout", Path(__file__).with_name("stable_state_layout.py"))
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)
asset = (root / "app/src/main/assets/content/es/programming/programming_practice_tools"
         "/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
declaration = {"activity": "block_0010_0003", "kind": "cmakeSelectiveRecoveryV1"}


class CompilerWorkflowDeclarationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = json.loads(asset.read_text(encoding="utf-8"))

    def revised(self):
        result = deepcopy(self.original)
        result["compilerWorkflowChecks"] = [declaration.copy()]
        return result

    def test_addition_preserves_layout_and_complete_original_editor(self):
        before, after = self.original, self.revised()
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])

    def test_declaration_cannot_be_removed(self):
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(self.revised(), self.original)

    def test_wrong_owner_kind_duplicates_and_types_are_rejected(self):
        for candidate in (
            [{"activity": "block_0003_0004", "kind": declaration["kind"]}],
            [{"activity": declaration["activity"], "kind": "stdoutOnly"}],
            [declaration, declaration],
            [{"activity": declaration["activity"]}],
            "block_0010_0003",
        ):
            with self.subTest(checks=candidate):
                data = self.revised()
                data["compilerWorkflowChecks"] = candidate
                with self.assertRaises(layout.LayoutError):
                    layout.validate(data)

    def test_wrong_lesson_and_target_rejected(self):
        data = self.revised()
        data["id"] = "other_lesson"
        with self.assertRaises(layout.LayoutError):
            layout.validate(data)
        data = self.revised()
        data["pages"][10]["blocks"][3]["cmake"]["target"] = "consultar_cajas"
        with self.assertRaises(layout.LayoutError):
            layout.validate(data)


if __name__ == "__main__":
    unittest.main()

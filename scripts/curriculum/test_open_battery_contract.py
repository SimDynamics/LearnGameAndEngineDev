"""Author a real U3L10 CMake battery objective without touching published JSON."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / ("app/src/main/assets/content/es/programming/programming_practice_tools/"
               "comprobaciones_repetibles/lesson_integrar_un_proyecto_y_sostener_su_evidencia.json")
OWNER = "block_0006_0011"
CASES = (
    (2, 3, 7, 77), (5, 3, 7, -8), (9, 3, 7, 19),
    (4, 4, 4, -4), (-20, 20, 20, 8), (20, -20, -20, 17),
    (0, 7, 3, 77), (21, 3, 7, -8), (-21, 3, 7, 19),
    (0, -21, 7, -4), (0, 3, 21, 8),
)


def published() -> dict:
    return json.loads(PATH.read_text(encoding="utf-8"))


def authorized(before: dict) -> dict:
    result = deepcopy(before)
    result["compilerOpenBatteryChecks"] = [{
        "activity": OWNER,
        "library": "src/ajuste.cpp",
        "test": "pruebas/casos.cpp",
        "function": "intentar_ajuste",
        "comparison": "comprobar",
        "target": "comprobar_ajustes",
        "originalCases": [
            dict(value=value, lower=lower, upper=upper, initial=initial)
            for value, lower, upper, initial in CASES
        ],
    }]
    return result


class OpenBatteryRootContractTest(unittest.TestCase):
    def test_real_root_addition_preserves_all_historical_content(self):
        before = published()
        after = authorized(before)
        self.assertEqual(state.validate(before), state.validate(after))
        state.validate_transition(before, after)
        for name in ("pages", "stateLayout", "activityAfter", "textSemantics"):
            self.assertEqual(before.get(name), after.get(name))
        self.assertNotIn("compilerOpenBatteryChecks", before)
        self.assertEqual(len(after["compilerOpenBatteryChecks"][0]["originalCases"]), 11)

    def test_bad_original_calls_and_forged_semantics_are_rejected(self):
        before = published()
        for label, change in (
            ("missing", lambda r: r["originalCases"].pop()),
            ("swapped", lambda r: r["originalCases"].reverse()),
            ("duplicate", lambda r: r["originalCases"].append(deepcopy(r["originalCases"][0]))),
            ("false_value", lambda r: r["originalCases"][1].update(value=6)),
            ("new_target", lambda r: r.update(target="consultar_ajustes")),
            ("new_library", lambda r: r.update(library="pruebas/casos.cpp")),
            ("other_owner", lambda r: r.update(activity="block_0002_0003")),
            ("bad_comparison", lambda r: r.update(comparison="comprobar;exit(0)")),
            ("escape", lambda r: r.update(test="../private.cpp")),
            ("unknown_field", lambda r: r.update(required=False)),
        ):
            candidate = authorized(before)
            change(candidate["compilerOpenBatteryChecks"][0])
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)

    def test_published_declaration_is_immutable(self):
        before = published()
        accepted = authorized(before)
        removed = deepcopy(accepted)
        del removed["compilerOpenBatteryChecks"]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(accepted, removed)
        changed = deepcopy(accepted)
        changed["compilerOpenBatteryChecks"][0]["originalCases"][0]["initial"] = 0
        with self.assertRaises(state.LayoutError):
            state.validate(changed)
        repeated = deepcopy(accepted)
        repeated["compilerOpenBatteryChecks"].append(
            deepcopy(repeated["compilerOpenBatteryChecks"][0]))
        with self.assertRaises(state.LayoutError):
            state.validate(repeated)

    def test_original_asset_is_read_only(self):
        existing = PATH.read_bytes()
        state.validate(authorized(published()))
        self.assertEqual(existing, PATH.read_bytes())


if __name__ == "__main__":
    unittest.main()

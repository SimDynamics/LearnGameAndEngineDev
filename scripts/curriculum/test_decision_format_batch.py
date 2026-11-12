"""Regresion real de alternativas: fuente C++ sin cambiar respuestas ni historial."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_alternativas_y_fronteras.json"
POSITIONS = ((0,2), (1,2), (2,2), (5,2), (6,2))


class RealAlternativeFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.archives = {item["id"]: item["block"] for item in
                         self.current["stateLayout"]["sourceFormatRevisions"]}

    def test_five_original_owners_remain_protected(self):
        current = self.current
        state.validate(current)
        self.assertEqual(5, len(self.archives))
        historical = deepcopy(current)
        historical["stateLayout"].pop("sourceFormatRevisions")
        suffix = historical["stateLayout"]["editorial"][-len(POSITIONS):]
        self.assertEqual([current["stateLayout"]["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [item["id"] for item in suffix])
        historical["stateLayout"]["editorial"] = historical["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p, b in POSITIONS:
            identity = current["stateLayout"]["current"][p]["blocks"][b]
            before, after = self.archives[identity], current["pages"][p]["blocks"][b]
            historical["pages"][p]["blocks"][b] = deepcopy(before)
            self.assertTrue(require_source_format(before, after))
            self.assertEqual([line for line in before["code"].splitlines() if line.strip()],
                             [line for line in after["code"].splitlines() if line.strip()])
        state.validate(historical)
        state.validate_transition(historical, current)
        self.assertEqual(state.validate(historical), state.validate(current))
        self.assertEqual(historical["stateLayout"]["family"], current["stateLayout"]["family"])
        self.assertEqual(historical["stateLayout"]["origin"], current["stateLayout"]["origin"])

    def test_original_and_formatted_cxx23_sources_have_equal_outputs(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-alternative-format-") as directory:
            for index, (p,b) in enumerate(POSITIONS):
                identity = self.current["stateLayout"]["current"][p]["blocks"][b]
                before = self.archives[identity]["code"]
                after = self.current["pages"][p]["blocks"][b]["code"]
                outputs = []
                for label, code in (("original", before), ("formatted", after)):
                    source = Path(directory)/f"{index}-{label}.cpp"
                    exe = Path(directory)/f"{index}-{label}"
                    source.write_text(code, encoding="utf-8")
                    built = subprocess.run(["c++", "-std=c++23", "-O0", "-o", str(exe), str(source)],
                                           capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, built.stderr)
                    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
                    self.assertEqual(0, result.returncode)
                    self.assertEqual("", result.stderr)
                    outputs.append(result.stdout)
                self.assertEqual(outputs[0], outputs[1])


if __name__ == "__main__":
    unittest.main()

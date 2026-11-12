"""One source line per draggable order_code card; historical answers survive."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_for_y_limites_del_recorrido.json"
POSITION = (5, 7)
IDENTITY = "orden_while_puestos_2_3_4"


def ordered_program(block):
    return "\\n".join(block["fragments"][i] for i in block["correctOrder"])


class EveryCardExactlyOneLine(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.card = self.current["pages"][POSITION[0]]["blocks"][POSITION[1]]
        self.revision = next(x for x in self.meta["orderRefinements"]
                             if x["id"] == IDENTITY)

    def test_single_line_cards_preserve_real_old_order_answer_and_draft(self):
        state.validate(self.current)
        old = self.revision["block"]
        self.assertEqual(5, len(old["fragments"]))
        self.assertEqual(9, len(self.card["fragments"]))
        self.assertTrue(all(part and "\\n" not in part and "\\r" not in part
                            for part in self.card["fragments"]))
        self.assertEqual([5,6,7,2,8,0,1,3,4], self.card["correctOrder"])
        self.assertEqual([{"activity": IDENTITY, "count": 1}],
                         self.current["orderVisualSeparators"])
        before = deepcopy(self.current)
        before["pages"][POSITION[0]]["blocks"][POSITION[1]] = deepcopy(old)
        before["stateLayout"].pop("orderRefinements")
        self.assertEqual(IDENTITY,before["stateLayout"]["editorial"][-1]["id"])
        before["stateLayout"]["editorial"].pop()
        state.validate(before)
        state.validate_transition(before,self.current)
        self.assertEqual(state.validate(before),state.validate(self.current))
        for field in ("origin","family","current","additions","sourceFormatRevisions"):
            self.assertEqual(before["stateLayout"].get(field),self.meta.get(field))
        self.assertEqual(before["pages"][POSITION[0]]["blocks"][POSITION[1]],old)

    def test_old_and_refined_cpp23_have_identical_output(self):
        with tempfile.TemporaryDirectory(prefix="one-line-order-") as folder:
            observations = []
            for label,block in (("original",self.revision["block"]),("refined",self.card)):
                source=Path(folder)/f"{label}.cpp"
                executable=Path(folder)/label
                source.write_text(ordered_program(block)+"\\n",encoding="utf-8")
                built=subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                      capture_output=True,text=True,timeout=90)
                self.assertEqual(0,built.returncode,built.stderr)
                executed=subprocess.run([str(executable)],capture_output=True,text=True,timeout=15)
                self.assertEqual(0,executed.returncode,executed.stderr)
                self.assertEqual("",executed.stderr)
                observations.append(executed.stdout)
            self.assertEqual(observations[0],observations[1])
            self.assertEqual("Puesto 2\\nPuesto 3\\nPuesto 4\\n",observations[1])

    def test_false_saved_order_and_forged_archive_rejected(self):
        wrong=list(self.card["correctOrder"])
        wrong[0],wrong[1]=wrong[1],wrong[0]
        self.assertNotEqual([self.card["fragments"][i] for i in wrong],
                            [self.card["fragments"][i] for i in self.card["correctOrder"]])
        tampered=deepcopy(self.current)
        tampered["stateLayout"]["orderRefinements"][0]["block"]["fragments"][0] += " cambio"
        with self.assertRaises(state.LayoutError):
            state.validate(tampered)


if __name__ == "__main__":
    unittest.main()

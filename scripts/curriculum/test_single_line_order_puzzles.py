"""All Spanish programming order_code cards are exactly one physical line."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
LESSONS = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos"
CASES = json.loads(r'''[["lesson_declaraciones_y_firmas.json",4,8,"dias_completos_definicion_anterior_orden",3,9,"","6",2],["lesson_hacer_explicito_el_orden_de_las_operaciones.json",3,19,"registro_orden_propio_orden",4,19,"2 7\n","9:27",3],["lesson_nombrar_calculos_y_llamar.json",5,8,"construccion_lotes_y_sueltas_orden",4,12,"2 4\n","Fichas: 10\n",2]]''')


def program(block):
    return "\n".join(block["fragments"][i] for i in block["correctOrder"]) + "\n"


class RealFunctionOrderCards(unittest.TestCase):
    def test_all_current_spanish_programming_order_cards_are_single_line(self):
        root = ROOT / "app/src/main/assets/content/es/programming"
        violations = []
        for path in sorted(root.glob("**/lesson_*.json")):
            lesson = json.loads(path.read_text(encoding="utf-8-sig"))
            for p, page in enumerate(lesson["pages"]):
                for b, block in enumerate(page.get("blocks", [])):
                    if block.get("type") != "order_code":
                        continue
                    for index, line in enumerate(block["fragments"]):
                        if "\n" in line or "\r" in line:
                            violations.append(f"{path.relative_to(root)} p{p+1} b{b+1} tarjeta{index}")
        self.assertEqual([], violations)

    def test_three_function_puzzles_preserve_original_answers_and_state(self):
        for name, p, b, identity, old_count, new_count, stdin, expected, sep in CASES:
            with self.subTest(lesson=name):
                after = json.loads((LESSONS / name).read_text(encoding="utf-8"))
                state.validate(after)
                meta = after["stateLayout"]
                current = after["pages"][p]["blocks"][b]
                history = meta["orderRefinements"]
                self.assertEqual(1, len(history))
                self.assertEqual(identity, history[0]["id"])
                old = history[0]["block"]
                self.assertEqual(old_count, len(old["fragments"]))
                self.assertEqual(new_count, len(current["fragments"]))
                self.assertTrue(all(item.strip() and "\n" not in item and "\r" not in item
                                    for item in current["fragments"]))
                previous = deepcopy(after)
                previous["pages"][p]["blocks"][b] = deepcopy(old)
                previous["stateLayout"].pop("orderRefinements")
                previous["stateLayout"]["editorial"].pop()
                if name == "lesson_hacer_explicito_el_orden_de_las_operaciones.json":
                    self.assertEqual([{"activity": identity, "count": 3}],
                                     after["orderVisualSeparators"])
                    previous.pop("orderVisualSeparators")
                else:
                    self.assertEqual([{"activity": identity, "count": sep}],
                                     after["orderVisualSeparators"])
                state.validate(previous)
                state.validate_transition(previous, after)
                self.assertEqual(state.validate(previous), state.validate(after))
                for field in ("origin", "family", "current", "additions"):
                    self.assertEqual(previous["stateLayout"][field], meta[field])
                forged = deepcopy(after)
                forged["stateLayout"]["orderRefinements"][0]["block"]["fragments"][0] += " altered"
                with self.assertRaises(state.LayoutError):
                    state.validate(forged)

    def test_old_and_refined_programs_compile_and_run_identically(self):
        with tempfile.TemporaryDirectory(prefix="function-order-lines-") as folder:
            for i, (name, p, b, identity, old_count, new_count, stdin, expected, sep) in enumerate(CASES):
                after = json.loads((LESSONS / name).read_text(encoding="utf-8"))
                old = after["stateLayout"]["orderRefinements"][0]["block"]
                new = after["pages"][p]["blocks"][b]
                results = []
                for label, card in (("old", old), ("refined", new)):
                    path = Path(folder) / f"{i}-{label}.cpp"
                    binary = Path(folder) / f"{i}-{label}"
                    path.write_text(program(card), encoding="utf-8")
                    built = subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(path)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0, built.returncode, identity + built.stderr)
                    run = subprocess.run([str(binary)],input=stdin,capture_output=True,
                                         text=True,timeout=15)
                    self.assertEqual(0, run.returncode, identity)
                    self.assertEqual("", run.stderr, identity)
                    results.append(run.stdout)
                self.assertEqual(results[0], results[1], identity)
                self.assertEqual(expected, results[1], identity)


if __name__ == "__main__":
    unittest.main()

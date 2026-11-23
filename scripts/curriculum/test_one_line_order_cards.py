"""One source line per card, authenticated independently of later lesson edits."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from format_batch_history import (
    Publication, active_owners, assert_stable_storage, load_publication,
    publication_bytes, verify_publication_bytes,
)
from windows_cpp_ci import msvc_cpp23_command, windows_executable


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "repeticion_y_terminacion/lesson_for_y_limites_del_recorrido.json"
)
PATH = ROOT / RELATIVE
IDENTITY = "orden_while_puestos_2_3_4"
ORDER_BEFORE = Publication(
    "084b5bcfa4f7e8a942db8d26ae7a136150ea24df",
    "8c45ce7f05fa77e0aace8df46534255567f8ff34f48597bf6ecb20ebcfd6b044")
ORDER_REFINED = Publication(
    "34a1c9b0603d4ba5f4dc50199fd41399b5bf0e8c",
    "e7d437b1eedf69f4c7c6c3474ba6005f38f13dfc5051de1c023f9e1e90117cbd")


def ordered_program(block):
    return "\n".join(block["fragments"][i] for i in block["correctOrder"])


def assert_order_publication(case, before, refined, current):
    """The archive and refinement edge come from two complete publications."""
    for document in (before, refined, current):
        state.validate(document)
    original = active_owners(before)[0][IDENTITY]
    published = active_owners(refined)[0][IDENTITY]
    order = active_owners(current)[0][IDENTITY]
    for document in (refined, current):
        records = [entry for entry in document["stateLayout"]["orderRefinements"]
                   if entry["id"] == IDENTITY]
        case.assertEqual(1, len(records))
        case.assertEqual(records[0]["block"], original)
    case.assertEqual(5, len(original["fragments"]))
    case.assertEqual(9, len(order["fragments"]))
    case.assertTrue(all(part and "\n" not in part and "\r" not in part
                        for part in order["fragments"]))
    case.assertEqual([5, 6, 7, 2, 8, 0, 1, 3, 4], order["correctOrder"])
    case.assertEqual(state.state_payload(original), state.state_payload(
        state.order_contract_projection(order, original)))
    edge = {"id": IDENTITY, "beforeSha256": state.fingerprint(original),
            "afterSha256": state.fingerprint(published)}
    published_edges = [entry for entry in refined["stateLayout"]["editorial"]
                       if entry["id"] == IDENTITY]
    current_edges = [entry for entry in current["stateLayout"]["editorial"]
                     if entry["id"] == IDENTITY]
    case.assertIn(edge, published_edges)
    case.assertEqual(current_edges[:len(published_edges)], published_edges)
    declarations = [entry for entry in current["orderVisualSeparators"]
                    if entry["activity"] == IDENTITY]
    case.assertEqual([{"activity": IDENTITY, "count": 1}], declarations)
    assert_stable_storage(case, before, current, (IDENTITY,))


class EveryCardExactlyOneLine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.current = json.loads(PATH.read_bytes())
        cls.before = load_publication(ROOT, RELATIVE, ORDER_BEFORE)
        cls.refined = load_publication(ROOT, RELATIVE, ORDER_REFINED)
        cls.card = active_owners(cls.current)[0][IDENTITY]
        cls.original = active_owners(cls.before)[0][IDENTITY]

    def test_single_line_cards_preserve_real_old_order_answer_and_draft(self):
        state.validate_transition(self.before, self.refined)
        assert_order_publication(self, self.before, self.refined, self.current)
        # The displayed nine-card order projects to the original five-card
        # response contract at the same storage slot used for answers/drafts.
        projected = state.order_contract_projection(self.card, self.original)
        self.assertEqual(projected["correctOrder"], self.original["correctOrder"])
        self.assertEqual(projected["fragments"], self.original["fragments"])

    def test_old_and_refined_cpp23_have_identical_output(self):
        binaries, observations = {}, []
        with tempfile.TemporaryDirectory(prefix="one-line-order-") as folder:
            for label, block in (
                ("original", self.original),
                ("refined", active_owners(self.refined)[0][IDENTITY]),
                ("current", self.card),
            ):
                code = ordered_program(block) + "\n"
                if code not in binaries:
                    source = Path(folder) / f"{label}.cpp"
                    executable = Path(folder) / label
                    source.write_text(code, encoding="utf-8")
                    built = subprocess.run(msvc_cpp23_command(executable, source),
                        cwd=folder, capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, built.stdout + built.stderr)
                    executed = subprocess.run([str(windows_executable(executable))],
                        cwd=folder, capture_output=True, text=True, timeout=15)
                    self.assertEqual(0, executed.returncode, executed.stderr)
                    self.assertEqual("", executed.stderr)
                    binaries[code] = executed.stdout
                observations.append(binaries[code])
            self.assertEqual(observations, ["Puesto 2\nPuesto 3\nPuesto 4\n"] * 3)

    def test_false_saved_order_and_forged_archive_rejected(self):
        wrong = deepcopy(self.current)
        response = active_owners(wrong)[0][IDENTITY]["correctOrder"]
        response[0], response[1] = response[1], response[0]
        with self.assertRaises(state.LayoutError):
            state.validate(wrong)
        with self.assertRaises(state.LayoutError):
            state.order_contract_projection(active_owners(wrong)[0][IDENTITY], self.original)
        tampered = deepcopy(self.current)
        record = next(entry for entry in tampered["stateLayout"]["orderRefinements"]
                      if entry["id"] == IDENTITY)
        record["block"]["fragments"][0] += " cambio"
        with self.assertRaises(state.LayoutError):
            state.validate(tampered)
        for publication in (ORDER_BEFORE, ORDER_REFINED):
            payload = publication_bytes(ROOT, RELATIVE, publication)
            with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                verify_publication_bytes(payload + b"\n", publication)


if __name__ == "__main__":
    unittest.main()


"""Canonical Android mathematical editor/calculator through the real Windows JVM."""
from __future__ import annotations

import json
import math
import unittest

from test_worker import Worker


class WindowsMathEditorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Editing/calculating must remain available without voices or ONNX files.
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def create(self, name, latex="", **fields):
        identity = self.id().rsplit(".", 1)[-1] + "-" + name
        return identity, self.worker.ask("math_editor", editorId=identity, action="create", latex=latex, **fields)

    def edit(self, identity, action, **fields):
        return self.worker.ask("math_editor", editorId=identity, action=action, **fields)

    def test_01_structured_keys_create_original_ast_slots_not_linear_text(self):
        cases = {"Fraction": "fraction", "Power": "scripts", "Subscript": "scripts", "Root": "root",
                 "NthRoot": "root", "Parentheses": "group", "AbsoluteValue": "group", "Brackets": "group",
                 "Braces": "group", "Integral": "largeOperator", "Sum": "largeOperator", "Product": "largeOperator",
                 "Limit": "limit", "Matrix": "matrix", "Cases": "cases", "Derivative": "derivative",
                 "Function": "function"}
        for key, kind in cases.items():
            with self.subTest(key=key):
                identity, _ = self.create(key)
                result = self.edit(identity, "key", key=key, name="sin")
                self.assertTrue(result["accepted"])
                self.assertEqual(kind, result["expression"]["children"][0]["type"])
                self.assertTrue(result["hasNavigableSlots"])
                self.assertFalse(result["complete"])
                self.assertTrue(result["cursor"]["path"])
                self.edit(identity, "close")

    def test_02_numeric_leaf_cursor_edits_exact_position_and_forward_delete(self):
        identity, _ = self.create("digits", "1234")
        self.edit(identity, "cursor", cursor={"path": [], "offset": 0, "level": "INTERIOR"}, leafTextOffset=2)
        edited = self.edit(identity, "text", text="9")
        self.assertEqual("12934", edited["latex"])
        self.assertEqual("1234", self.edit(identity, "backspace")["latex"])
        self.assertEqual("124", self.edit(identity, "deleteForward")["latex"])
        state = self.edit(identity, "state")
        # Canonical replacement may split the Number into adjacent numeric
        # leaves; either representation must retain the same UTF-16 caret.
        cursor = state["cursor"]
        position = sum(len(node["value"]) for node in state["expression"]["children"][:cursor["offset"]])
        self.assertEqual(2, position + cursor.get("leafTextOffset", 0))

    def test_03_copy_preserves_selection_and_cut_uses_original_ast_range(self):
        identity, _ = self.create("clipboard", r"\frac{12}{x}+3")
        selected = self.edit(identity, "selection", path=[], start=0, end=1)
        copied = self.edit(identity, "copy")
        self.assertEqual(r"\frac{12}{x}", copied["clipboardLatex"])
        self.assertEqual(selected["draft"], copied["draft"])
        cut = self.edit(identity, "cut")
        self.assertEqual(copied["clipboardLatex"], cut["clipboardLatex"])
        self.assertEqual("+3", cut["latex"])
        self.assertEqual("+3", self.edit(identity, "copy")["clipboardLatex"])
        self.assertEqual("", self.edit(identity, "cut")["latex"])

    def test_04_draft_restores_nested_cursor_and_selection_without_answer_loss(self):
        identity, original = self.create("draft", r"\frac{\sqrt[3]{27}}{x_{12}^{2}}")
        selected = self.edit(identity, "selection", path=[{"node": 0, "slot": "denominator"}], start=0, end=1)
        self.edit(identity, "clear")
        restored = self.edit(identity, "restore", draft=selected["draft"])
        self.assertEqual(json.loads(selected["draft"]), json.loads(restored["draft"]))
        self.assertEqual(original["expression"], restored["expression"])
        failed = self.edit(identity, "restore", draft='{"version":99}')
        self.assertFalse(failed["accepted"])
        self.assertEqual(restored["draft"], failed["draft"])

    def test_05_invalid_latex_does_not_discard_expression_or_selection(self):
        identity, _ = self.create("invalid", "12+3")
        original = self.edit(identity, "selectAll")
        rejected = self.edit(identity, "insertLatex", latex=r"\notARealMathCommand{x}")
        self.assertFalse(rejected["accepted"])
        self.assertEqual(original["draft"], rejected["draft"])

    def test_06_navigation_visits_nested_parent_child_and_exact_leaf_positions(self):
        identity, original = self.create("navigation", r"\int_0^1\frac{\sqrt{x_{12}^{2}}}{\sqrt[3]{27}}+34")
        states = []
        current = original
        for _ in range(250):
            states.append(current)
            self.assertEqual(original["expression"], current["expression"])
            if not current["canPrevious"]:
                break
            current = self.edit(identity, "left")
            self.assertTrue(current["accepted"])
        else:
            self.fail("Canonical structural navigation never reached its left boundary")
        self.assertGreater(len(states), 25)
        presentations = {item["presentation"]["type"] for item in states}
        self.assertTrue({"interior", "neutral", "parent", "child"}.issubset(presentations), presentations)
        self.assertTrue(any(len(item["cursor"]["path"]) >= 3 for item in states))
        self.assertTrue(any(item["cursor"].get("leafTextOffset") == 1 for item in states))
        visited = 0
        while current["canNext"] and visited < 250:
            current = self.edit(identity, "right")
            self.assertEqual(original["expression"], current["expression"])
            visited += 1
        self.assertLess(visited, 250)
        self.assertGreater(visited, 25)
        self.assertEqual([], current["cursor"]["path"])
        self.assertEqual(len(original["expression"]["children"]), current["cursor"]["offset"])

    def test_07_terminal_integral_continuation_is_original_virtual_boundary(self):
        identity, original = self.create("terminal", r"\int_0^1")
        seen = set()
        current = original
        for _ in range(80):
            seen.add(current["presentation"]["type"])
            if not current["canPrevious"]:
                break
            current = self.edit(identity, "left")
        for _ in range(80):
            seen.add(current["presentation"]["type"])
            if not current["canNext"]:
                break
            current = self.edit(identity, "right")
        self.assertIn("continuation", seen)
        self.assertEqual(original["expression"], current["expression"])
        self.assertNotIn("body", current["expression"]["children"][0])

    def test_08_selection_wraps_fraction_and_symbol_operator_stays_atomic(self):
        identity, _ = self.create("wrap", "12")
        self.edit(identity, "selectAll")
        result = self.edit(identity, "key", key="Fraction")
        self.assertEqual("12", result["expression"]["children"][0]["numerator"]["children"][0]["value"])
        self.assertEqual("denominator", result["cursor"]["path"][-1]["slot"])
        identity, _ = self.create("combination")
        self.edit(identity, "text", text="5")
        self.edit(identity, "key", key="Symbol", name="nCr")
        result = self.edit(identity, "text", text="2")
        self.assertEqual(["number", "operator", "number"], [node["type"] for node in result["expression"]["children"]])
        self.assertEqual("nCr", result["expression"]["children"][1]["value"])
        self.assertEqual("10", self.worker.ask("math_calculate", draft=result["draft"])["latex"])

    def test_09_original_calculator_precedence_exact_integers_and_structures(self):
        cases = [("2+3*4", "14"), ("-2^2", "-4"), ("(-2)^2", "4"), ("8/4/2", "1"),
                 (r"\frac{2^3+\sqrt{16}}{\sin{\frac{\pi}{2}}+1}", "6"), (r"\sqrt[3]{-8}", "-2"),
                 ("20!", "2432902008176640000"), ("60nCr30", "118264581564861424"),
                 ("20nPr5", "1860480"), ("9007199254740993-9007199254740992", "1"), (r"200*10\%", "20")]
        for source, expected in cases:
            with self.subTest(source=source):
                result = self.worker.ask("math_calculate", latex=source)
                self.assertTrue(result["success"], result)
                self.assertEqual(expected, result["latex"])
                self.assertEqual(result["expression"], json.loads(result["draft"])["expression"])

    def test_10_angles_domains_errors_and_inputs_are_independent(self):
        identity, original = self.create("calculate", r"\sin{30}")
        degree = self.worker.ask("math_calculate", draft=original["draft"], angle="DEG")
        self.assertEqual("0.5", degree["latex"])
        radian = self.worker.ask("math_calculate", draft=original["draft"], angle="RAD")
        self.assertAlmostEqual(math.sin(30), float(radian["latex"]), places=10)
        self.assertEqual(original["draft"], self.edit(identity, "state")["draft"])
        for invalid in ["1/0", r"\sqrt{-1}", "171!", "5nCr6", r"\frac{1}{}"]:
            with self.subTest(invalid=invalid):
                result = self.worker.ask("math_calculate", latex=invalid, language="fr")
                self.assertFalse(result["success"])
                self.assertTrue(result["message"])
                self.assertNotIn("expression", result)
        self.assertEqual("14", self.worker.ask("math_calculate", latex="2+3*4")["latex"])

    def test_11_keyboard_uses_original_shortcut_selection_and_never_leaks_values(self):
        config = {"inputMode": "structured", "validator": {"type": "symbolic", "expected": r"\frac{987654}{x}+\sqrt[3]{27}"}}
        result = self.worker.ask("math_keyboard", configJson=json.dumps(config), language="es")
        self.assertTrue(result["structured"])
        self.assertEqual(["Fraction", "Symbol", "NthRoot"], [item["key"] for item in result["keys"]])
        self.assertEqual("+", result["keys"][1]["text"])
        self.assertNotIn("987654", json.dumps(result))
        self.assertNotIn("27", json.dumps(result))
        self.assertEqual("Insertar fracción", result["keys"][0]["description"])
        preview = result["keys"][0]["preview"]["children"][0]
        self.assertEqual([], preview["numerator"]["children"])
        self.assertEqual([], preview["denominator"]["children"])
        config["keyboardSymbols"] = ["ROOT", "FUNCTION:sin", "ROOT", "notAnAllowedStructure"]
        explicit = self.worker.ask("math_keyboard", config=config, language="fr")
        self.assertEqual(["Root", "Function"], [item["key"] for item in explicit["keys"]])
        self.assertEqual("sin", explicit["keys"][1]["name"])
        self.assertIn("racine", explicit["keys"][0]["description"].lower())

    def test_12_simple_keyboard_preserves_literal_minus_and_mode_defaults(self):
        literal = self.worker.ask("math_keyboard", config={"validator": {"type": "exact_text", "expected": "−2"}}, language="en-US")
        self.assertFalse(literal["structured"])
        self.assertEqual("−", literal["minusInsertion"])
        numeric = self.worker.ask("math_keyboard", config={"validator": {"type": "numeric", "expected": "-2.5"}}, language="en-GB")
        self.assertEqual("-", numeric["minusInsertion"])
        self.assertEqual(["−", "."], [item["label"] for item in numeric["keys"]])

    def test_13_same_jvm_command_lanes_keep_request_ids_and_model_sessions(self):
        identity, original = self.create("multiplex", "12")
        requests = {
            self.worker.send("math_calculate", latex="170!"): "math_calculate",
            self.worker.send("math_editor", editorId=identity, action="text", text="3"): "math_editor",
            self.worker.send("math_keyboard", config={"keyboardSymbols": ["ROOT"]}, structured=True): "math_keyboard",
            self.worker.send("plan", text="Una prueba.", language="es", voice="ef_dora"): "plan",
        }
        results = {}
        for _ in range(len(requests)):
            result = self.worker.receive()
            self.assertIn(result["id"], requests)
            self.assertEqual(requests[result["id"]], result["command"])
            self.assertNotIn("error", result)
            results[result["command"]] = result
            del requests[result["id"]]
        self.assertEqual("123", results["math_editor"]["latex"])
        self.assertEqual("Una prueba.", results["plan"]["segments"][0]["text"])
        self.assertTrue(results["math_calculate"]["success"])
        self.assertEqual("12", original["latex"])
        self.assertFalse(any("ONNX initialized" in row for row in self.worker.errors))

    def test_14_native_text_selection_reuses_bounded_ast_replacement_and_deletion(self):
        for operation in ["text", "backspace", "deleteForward", "cut"]:
            with self.subTest(operation=operation):
                identity, original = self.create(operation, "123456")
                selected = self.edit(identity, "textSelection", path=[], startNode=0, startLeaf=2, endNode=0, endLeaf=4)
                self.assertEqual({"path": [], "start": 2, "end": 4}, selected["textSelection"])
                copied = self.edit(identity, "copy")
                self.assertEqual("34", copied["clipboardLatex"])
                self.assertEqual(selected["draft"], copied["draft"])
                self.assertEqual(original["expression"], copied["expression"])
                result = self.edit(identity, operation, **({"text": "9"} if operation == "text" else {}))
                self.assertEqual("12956" if operation == "text" else "1256", result["latex"])
                self.assertNotIn("textSelection", result)
                if operation == "cut":
                    self.assertEqual("34", result["clipboardLatex"])

    def test_15_native_text_selection_cannot_consume_structures_or_survive_navigation(self):
        identity, original = self.create("structure", r"12+\frac{3}{4}+56")
        rejected = self.edit(identity, "textSelection", path=[], startNode=0, startLeaf=0, endNode=4, endLeaf=1)
        self.assertFalse(rejected["accepted"])
        self.assertEqual(original["draft"], rejected["draft"])
        selected = self.edit(identity, "textSelection", path=[], startNode=0, startLeaf=0, endNode=0, endLeaf=1)
        self.assertIn("textSelection", selected)
        moved = self.edit(identity, "right")
        self.assertNotIn("textSelection", moved)
        self.assertEqual(original["expression"], moved["expression"])

    def test_16_calculator_cancel_frames_do_not_cancel_other_rpc_or_next_calculation(self):
        # Completion may legitimately win the cancellation race. The controlled
        # Kotlin fixture separately proves running/queued interruption semantics.
        calculation = self.worker.send("math_calculate", latex="170!")
        cancel = self.worker.send("cancel", targetId=calculation)
        narration = self.worker.send("plan", text="La lectura continúa.", language="es", voice="ef_dora")
        requests = {calculation: "math_calculate", cancel: "cancel", narration: "plan"}
        for _ in range(len(requests)):
            reply = self.worker.receive()
            self.assertIn(reply["id"], requests)
            self.assertEqual(requests.pop(reply["id"]), reply["command"])
            if reply["id"] == narration:
                self.assertNotIn("error", reply)
                self.assertEqual("La lectura continúa.", reply["segments"][0]["text"])
            elif reply["id"] == calculation:
                self.assertTrue(reply.get("cancelled") or reply.get("success"), reply)
        self.assertEqual("14", self.worker.ask("math_calculate", latex="2+3*4")["latex"])

    def test_17_nested_slot_leaf_offset_is_not_reapplied_to_replacement_suffix(self):
        cases = [(r"\frac{1234}{56}+\sqrt{x}", ["numerator"]),
                 (r"\frac{56}{1234}", ["denominator"]),
                 (r"\sqrt{1234}", ["radicand"]),
                 (r"x^{1234}", ["upper"]),
                 (r"\frac{\sqrt{1234}}{56}", ["numerator", "radicand"]),
                 (r"\sin{1234}", ["argument"]),
                 (r"\begin{matrix}1234&2\\3&4\end{matrix}", ["r0c0"])]
        for index, (source, slots) in enumerate(cases):
            with self.subTest(source=source):
                identity, original = self.create(str(index), source)
                path = [{"node": 0, "slot": slot} for slot in slots]
                selected = self.edit(identity, "cursor", cursor={"path": path, "offset": 0, "level": "INTERIOR"}, leafTextOffset=2)
                self.assertEqual(path, selected["cursor"]["path"])
                self.assertEqual(2, selected["cursor"]["leafTextOffset"])
                inserted = self.edit(identity, "text", text="9")
                self.assertEqual(original["latex"].replace("1234", "12934"), inserted["latex"])
                self.assertEqual(original["latex"], self.edit(identity, "backspace")["latex"])
                self.assertEqual(original["latex"].replace("1234", "124"), self.edit(identity, "deleteForward")["latex"])

    def test_18_nested_native_text_selection_replaces_only_the_requested_digits(self):
        identity, original = self.create("selected", r"\frac{123456}{78}+\sqrt{x}")
        path = [{"node": 0, "slot": "numerator"}]
        self.edit(identity, "textSelection", path=path, startNode=0, startLeaf=2, endNode=0, endLeaf=4)
        self.assertEqual("34", self.edit(identity, "copy")["clipboardLatex"])
        changed = self.edit(identity, "text", text="9")
        self.assertEqual(original["latex"].replace("123456", "12956"), changed["latex"])


if __name__ == "__main__":
    unittest.main(verbosity=2)

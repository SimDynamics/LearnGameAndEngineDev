"""Presentation-only optional blank cards preserve all 14 original order contracts."""
import copy
import unittest

import stable_state_layout as layout
import test_order_line_refinement as published


class OptionalOrderSeparatorsTest(unittest.TestCase):
    def fixture(self):
        # Real published v2 Spanish lesson, including the historical state map.
        path, *_unused, identity, _old_count, _new_count, _before, _after = published.CASES[0]
        return published.anchored(path), identity

    def declared(self, document, identity, count=2):
        next_version = copy.deepcopy(document)
        next_version["orderVisualSeparators"] = [{"activity": identity, "count": count}]
        return next_version

    def test_one_to_four_visual_cards_leave_original_answers_and_keys_intact(self):
        original, identity = self.fixture()
        for number in (1, 2, 3, 4):
            with self.subTest(count=number):
                decorated = self.declared(original, identity, number)
                self.assertEqual(layout.validate(original), layout.validate(decorated))
                layout.validate_transition(original, decorated)
                self.assertEqual(decorated["pages"], original["pages"])
                self.assertEqual(decorated["stateLayout"], original["stateLayout"])
                self.assertEqual(layout.state_fingerprint(
                    published.current_block(original, identity)),
                    layout.state_fingerprint(published.current_block(decorated, identity)))
                self.assertEqual(original.get("referenceAfter"), decorated.get("referenceAfter"))
                self.assertEqual(original.get("activityAfter"), decorated.get("activityAfter"))

    def test_exact_original_fourteen_published_order_contracts_survive(self):
        for path, _page, _block, identity, *_rest in published.CASES:
            with self.subTest(path=path):
                source = published.anchored(path)
                refined = layout.refine_order_lines(source, identity)
                decorated = self.declared(refined, identity, 2)
                layout.validate(decorated)
                layout.validate_transition(refined, decorated)
                self.assertEqual(refined["pages"], decorated["pages"])
                self.assertEqual(refined["stateLayout"], decorated["stateLayout"])
                self.assertEqual(
                    published.line_contract_digest(published.current_block(refined, identity)),
                    published.line_contract_digest(published.current_block(decorated, identity)))

    def test_source_newlines_raw_strings_macros_and_escapes_are_untouched(self):
        source = published.synthetic([
            "#define SUMAR(x) \\\n    ((x) + 1)",
            "// comentario hasta el salto real\nint n{1};",
            'const char* texto = R"abc(A\nB)abc";',
            'std::cout << "\\n";',
            'std::cout << "\n";'
        ])
        reviewed = self.declared(source, "block_0000_0000", 3)
        layout.validate(reviewed)
        layout.validate_transition(source, reviewed)
        self.assertEqual(source["pages"], reviewed["pages"])
        self.assertEqual(source["stateLayout"]["origin"], reviewed["stateLayout"]["origin"])

    def test_declared_not_any_blank_fragment_and_no_invalid_root_extension(self):
        original, identity = self.fixture()
        cases = (
            lambda v: v["orderVisualSeparators"].append({"activity": identity, "count": 1}),
            lambda v: v["orderVisualSeparators"][0].update(count=0),
            lambda v: v["orderVisualSeparators"][0].update(count=5),
            lambda v: v["orderVisualSeparators"][0].update(count=True),
            lambda v: v["orderVisualSeparators"][0].update(count="2"),
            lambda v: v["orderVisualSeparators"][0].update(activity="unknown"),
            lambda v: v["orderVisualSeparators"][0].update(activity="block_0000_0000"),
            lambda v: v["orderVisualSeparators"][0].update(extra=True),
        )
        for bad in cases:
            with self.subTest(case=bad.__code__.co_firstlineno):
                modified = self.declared(original, identity)
                bad(modified)
                with self.assertRaises(layout.LayoutError):
                    layout.validate(modified)
        different_type = self.declared(original, identity)
        different_type["orderVisualSeparators"][0]["activity"] = original["stateLayout"]["current"][0]["blocks"][0]
        with self.assertRaises(layout.LayoutError):
            layout.validate(different_type)

    def test_published_fourteen_code_orders_activate_only_visual_separators(self):
        from pathlib import Path
        root = Path(__file__).resolve().parents[2]
        counts = [1,1,1,2,2,2,2,2,2,2,2,3,2,1]
        self.assertEqual(len(counts), len(published.CASES))
        for item, count in zip(published.CASES, counts):
            path, page, index, identity, *_ = item
            with self.subTest(path=path):
                actual = layout.load(root / (published.PREFIX + path))
                self.assertEqual(actual.get("orderVisualSeparators"),
                                 [{"activity": identity, "count": count}])
                baseline = copy.deepcopy(actual)
                del baseline["orderVisualSeparators"]
                self.assertEqual(layout.validate(baseline), layout.validate(actual))
                layout.validate_transition(baseline, actual)
                for field in ("id", "title", "pages", "stateLayout", "textSemantics",
                              "referenceAfter", "activityAfter", "compilerPuzzlesRequired"):
                    self.assertEqual(baseline.get(field), actual.get(field))
                old_block = baseline["pages"][page]["blocks"][index]
                new_block = actual["pages"][page]["blocks"][index]
                self.assertEqual(old_block["fragments"], new_block["fragments"])
                self.assertEqual(old_block["correctOrder"], new_block["correctOrder"])
                self.assertEqual(layout.state_fingerprint(old_block),
                                 layout.state_fingerprint(new_block))

    def test_two_additional_span_separators_preserve_historical_source(self):
        # Only optional presentation metadata is new. Both lessons retain the
        # exact original response, fragments, identity and source text.
        from pathlib import Path
        root = (Path(__file__).resolve().parents[2] /
                "app/src/main/assets/content/es/programming/cpp_fundamentals")
        cases = (
            ("repeticion_y_terminacion/lesson_for_y_limites_del_recorrido.json",
             5, 7, "orden_while_puestos_2_3_4", 9),
            ("texto_interpretacion_y_flujos/lesson_asignaciones_con_anticipacion_limitada.json",
             2, 3, "block_0002_0003", 4),
        )
        for name, page, index, identity, fragment_count in cases:
            with self.subTest(lesson=name):
                actual = layout.load(root / name)
                self.assertEqual(actual.get("orderVisualSeparators"),
                                 [{"activity": identity, "count": 1}])
                self.assertEqual(
                    actual["stateLayout"]["current"][page]["blocks"][index],
                    identity)
                order = actual["pages"][page]["blocks"][index]
                self.assertEqual(order["type"], "order_code")
                self.assertEqual(len(order["fragments"]), fragment_count)
                self.assertEqual(sorted(order["correctOrder"]),
                                 list(range(fragment_count)))
                if identity == "orden_while_puestos_2_3_4":
                    self.assertTrue(all("\\n" not in part and "\\r" not in part
                                        for part in order["fragments"]))
                    history = actual["stateLayout"]["orderRefinements"]
                    self.assertEqual(1, len(history))
                    self.assertEqual(identity, history[0]["id"])
                    self.assertEqual(5, len(history[0]["block"]["fragments"]))
                original = copy.deepcopy(actual)
                del original["orderVisualSeparators"]
                self.assertEqual(layout.validate(original), layout.validate(actual))
                layout.validate_transition(original, actual)
                self.assertEqual(original["pages"], actual["pages"])
                self.assertEqual(original["stateLayout"], actual["stateLayout"])
                self.assertEqual(layout.state_fingerprint(
                    original["pages"][page]["blocks"][index]),
                    layout.state_fingerprint(order))

    def test_original_drag_fill_template_and_holes_are_not_new_separators(self):
        # Real published 4-hole C++ drag editor, with a deliberate blank line.
        from pathlib import Path
        root = Path(__file__).resolve().parents[2]
        asset = (root / "app/src/main/assets/content/es/programming/cpp_fundamentals"
                 / "objetos_y_estado/lesson_leer_una_entrada_conocida.json")
        original = layout.load(asset)
        layout.validate(original)
        drag = original["pages"][8]["blocks"][5]
        self.assertEqual(drag["type"], "drag_fill_code")
        self.assertEqual(drag["template"].count("[]"), 4)
        self.assertIn("#include <string>\n\nint main()", drag["template"])
        self.assertEqual(drag["correctOrder"], [2, 3, 0, 1])
        self.assertEqual(len(drag["choices"]), 4)
        self.assertEqual(original["stateLayout"]["current"][8]["blocks"][5], "block_0008_0005")
        # Visual-only declarations cannot be attached to this original
        # answer-bearing drag_fill_code, so they cannot create a fifth choice.
        invalid = self.declared(original, "block_0008_0005")
        with self.assertRaises(layout.LayoutError):
            layout.validate(invalid)
        self.assertEqual(drag, original["pages"][8]["blocks"][5])

    def test_existing_declaration_cannot_be_deleted_or_moved_silently(self):
        original, identity = self.fixture()
        decorated = self.declared(original, identity)
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(decorated, original)
        changed = self.declared(original, identity, 3)
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(decorated, changed)


if __name__ == "__main__":
    unittest.main()

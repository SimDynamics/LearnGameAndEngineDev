"""Simulation authoring and historical preservation, with isolated synthetic lessons."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import simulation
import stable_state_layout as state
import text_semantics
import validar_json_programacion as curriculum
import validate_programming_practice_tools as courses
import verificar_secuencia_pedagogica as sequence


EXAMPLE = Path(__file__).resolve().parents[2] / "docs/examples/array_runs_simulation.json"
EMPTY_TEXT_EXAMPLE = Path(__file__).resolve().parents[2] / "docs/examples/array_runs_empty_text_simulation.json"


def block():
    return {
        "type": "simulation", "title": "Contar los grupos",
        "description": "Avanza y observa los índices y las frecuencias calculadas.",
        "simulation": {
            "version": 1, "model": "arrayRunsV1", "view": "walkthrough",
            "valueType": "integer", "values": [4, 1, 4, 1, 8],
            "sort": True, "editable": True,
            "labels": {"array": "Datos", "start": "inicio", "end": "fin",
                       "frequency": "frecuencia", "maximum": "máximo",
                       "leaders": "candidatos", "output": "Salida"},
            "explanations": {
                "initial": "Hay {size} datos y {groups} grupos.",
                "start": "Comienza {value} en {start}; el límite exclusivo es {end}.",
                "advance": "El grupo {group} tiene ahora {frequency} elementos.",
                "newMaximum": "El nuevo máximo es {maximum}: {leaders}.",
                "tie": "Empata {value}; se conservan {leaders}.",
                "belowMaximum": "{frequency} es menor que {maximum}.",
                "done": "El recorrido termina antes de leer el índice {end}.",
                "empty": "No hay elementos ni accesos al array.",
            },
            "outputTemplate": "{value}: {frequency}",
            "scenarios": [{"id": "vacio", "label": "Vacío", "values": []}],
        },
    }


def lesson(include_simulation=True):
    blocks = [{"type": "paragraph", "text": "Compara después del intento."}]
    if include_simulation:
        blocks.append(block())
    return {
        "id": "programming_practice_tools__qa__simulation", "title": "Simulación QA",
        "pages": [
            {"title": "Intento", "blocks": [
                {"type": "cpp_compiler", "language": "cpp", "initialCode": "int main() {}",
                 "outputCheck": {"inputComparison": "exact", "cases": [
                     {"standardInput": "", "outputs": [""]}]}}
            ]},
            {"title": "Comparación", "blocks": blocks},
            {"title": "Recuperación", "blocks": [
                {"type": "multiple_choice", "id": "pregunta", "question": "Elige dos.",
                 "options": ["uno", "dos"], "correctIndex": 1, "explanation": "Dos."}
            ]},
        ],
    }


class SimulationContractTest(unittest.TestCase):
    def test_native_state_layout_fixture_tracks_the_exact_documented_example(self):
        """Pin the native synthetic lesson to its actual current source.

        The test does not rewrite a published student's state or relax the
        immutable stateLayout contract. It reproduces the Win32 QA input
        verbatim, then requires every provenance digest to agree.
        """
        example = json.loads(EXAMPLE.read_text(encoding="utf-8"))
        fixture_path = (Path(__file__).resolve().parents[2] /
                        "native/presentation/windows/fixtures/simulation_state_layout.json")
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
        raw = {
            "id": "qa_simulation_lesson",
            "title": "Simulación nativa",
            "pages": [
                {"blocks": [
                    {"type": "paragraph",
                     "text": "Este párrafo conserva su propia lectura y su pausa."},
                    example]},
                {"blocks": [
                    {"type": "paragraph",
                     "text": "Página posterior sin ejercicio artificial."}]},
            ],
        }
        generated = state.prepare_v2(raw, "es", {"es": raw})
        self.assertEqual(fixture, generated["stateLayout"],
                         "Native QA provenance is stale against the current authored example")
        native_qa = deepcopy(raw)
        native_qa["stateLayout"] = deepcopy(fixture)
        state.validate(native_qa)
        self.assertEqual(fixture["family"]["es"]["origin"], fixture["origin"])
        # Regression: a future change of numeric input or explanation must
        # fail when paired with the pinned, unchanged native QA map.
        modified = deepcopy(native_qa)
        modified["pages"][0]["blocks"][1]["simulation"]["values"][0] = 8
        with self.assertRaises(state.LayoutError):
            state.validate(modified)
        self.assertEqual(json.loads(fixture_path.read_text(encoding="utf-8")),
                         fixture, "QA validation unexpectedly rewrote its source fixture")

    def test_documented_example_and_each_view_are_supported(self):
        example = json.loads(EXAMPLE.read_text(encoding="utf-8"))
        simulation.validate_contract(example)
        for view in simulation.VIEWS:
            value = block()
            value["simulation"]["view"] = view
            with self.subTest(view=view):
                simulation.validate_contract(value)

    def test_documented_text_scenarios_have_independent_empty_string_oracles(self):
        """Protect the actual example Work will adapt, not a copied fixture."""
        authored = json.loads(EMPTY_TEXT_EXAMPLE.read_text(encoding="utf-8"))
        original = deepcopy(authored)
        spec = simulation.validate_contract(authored)
        self.assertEqual((spec["model"], spec["valueType"], spec["view"]),
                         ("arrayRunsV1", "text", "walkthrough"))
        expected = {
            "initial": (["", "patio", "patio"], [("", 1), ("patio", 2)], 2, ["patio"]),
            "coleccion_vacia": ([], [], 0, []),
            "cadena_vacia": ([""], [("", 1)], 1, [""]),
            "patio_repetido": (["", "patio", "patio"], [("", 1), ("patio", 2)], 2, ["patio"]),
            "vacia_y_espacio": (["", " "], [("", 1), (" ", 1)], 1, ["", " "]),
            "empate_y_espacios": (["", " ", "casa"],
                                   [("", 1), (" ", 1), ("casa", 1)], 1, ["", " ", "casa"]),
            "desorden_y_repeticiones": (["sol", " ", "sol", ""],
                                         [("", 1), (" ", 1), ("sol", 2)], 2, ["sol"]),
        }
        authored_cases = {"initial": spec["values"]}
        authored_cases.update({item["id"]: item["values"] for item in spec["scenarios"]})
        self.assertEqual(set(authored_cases), set(expected))
        for case_id, values in authored_cases.items():
            with self.subTest(case=case_id):
                source, groups_expected, maximum_expected, leaders_expected = expected[case_id]
                self.assertEqual(values, source)
                ordered = sorted(values, key=lambda text: text.encode("utf-8"))
                groups = []
                for value in ordered:
                    if groups and groups[-1][0] == value:
                        groups[-1] = (value, groups[-1][1] + 1)
                    else:
                        groups.append((value, 1))
                maximum = max((count for _, count in groups), default=0)
                leaders = [value for value, count in groups if count == maximum]
                self.assertEqual((groups, maximum, leaders),
                                 (groups_expected, maximum_expected, leaders_expected))
                self.assertGreater(simulation._trace_text_bytes(spec, values), 0)
        self.assertEqual(authored, original)

    def test_validation_leaves_every_value_and_field_unchanged(self):
        value = block()
        value["id"] = "Identidad opcional del contenido"
        before = deepcopy(value)
        simulation.validate_contract(value)
        self.assertEqual(value, before)
        self.assertIs(simulation.validate_contract(value), value["simulation"])

    def test_exact_keys_and_required_fields_are_enforced_at_every_level(self):
        for path in ((), ("simulation",), ("simulation", "labels"),
                     ("simulation", "explanations"), ("simulation", "scenarios", 0)):
            for mutation in ("extra", "missing"):
                value = block()
                target = value
                for part in path:
                    target = target[part]
                if mutation == "extra":
                    target["invented"] = "No se admite"
                else:
                    del target[next(iter(target))]
                with self.subTest(path=path, mutation=mutation), self.assertRaises(ValueError):
                    simulation.validate_contract(value)

    def test_model_version_boolean_and_view_types_are_exact(self):
        invalid = [("model", "arrayRunsV2"), ("model", {}), ("view", "video"),
                   ("view", []), ("valueType", "float"), ("valueType", []),
                   ("version", True), ("version", "1"), ("version", 1.5),
                   ("sort", 1), ("sort", "true"), ("editable", 0)]
        for key, content in invalid:
            value = block()
            value["simulation"][key] = content
            with self.subTest(key=key, content=content), self.assertRaises(ValueError):
                simulation.validate_contract(value)

    def test_numeric_json_integral_spellings_match_winrt_without_admitting_bool(self):
        value = block()
        value["simulation"].update(version=1.0, values=[-(2 ** 31), -0.0, 1.0, 2 ** 31 - 1])
        simulation.validate_contract(value)
        for item in (True, False, "1", None, {}, [], 1.5, float("nan"),
                     float("inf"), -float("inf"), -(2 ** 31) - 1, 2 ** 31):
            value["simulation"]["values"] = [item]
            with self.subTest(item=item), self.assertRaises(ValueError):
                simulation.validate_contract(value)

    def test_array_limits_and_sorted_input_apply_to_initial_and_scenarios(self):
        value = block()
        value["simulation"].update(values=[], scenarios=[])
        simulation.validate_contract(value)
        value["simulation"].update(values=list(range(64)), sort=False)
        simulation.validate_contract(value)
        for values in (None, {}, "1", list(range(65)), [2, 1]):
            value["simulation"]["values"] = values
            with self.subTest(values=values), self.assertRaises(ValueError):
                simulation.validate_contract(value)
        value["simulation"]["values"] = [1, 1, 2]
        value["simulation"]["scenarios"] = [{"id": "reverse", "label": "Inverso", "values": [2, 1]}]
        with self.assertRaisesRegex(ValueError, "ordenado"):
            simulation.validate_contract(value)
        value["simulation"]["sort"] = True
        simulation.validate_contract(value)

    def test_text_values_preserve_spaces_and_use_utf8_binary_order(self):
        value = block()
        value["simulation"].update(valueType="text", sort=False,
                                   values=["", "", " ", "A", "Z", "a", "á", "á", "🟦"], scenarios=[])
        simulation.validate_contract(value)
        value["simulation"]["values"] = ["á", "z"]
        with self.assertRaisesRegex(ValueError, "ordenado"):
            simulation.validate_contract(value)
        value["simulation"]["sort"] = True
        simulation.validate_contract(value)
        for item in ("a\nb", "a\rb", "\0", "\ud800", "ñ" * 65, 1, False):
            value["simulation"]["values"] = [item]
            with self.subTest(item=repr(item)), self.assertRaises(ValueError):
                simulation.validate_contract(value)
        value["simulation"]["values"] = ["ñ" * 64]
        simulation.validate_contract(value)

    def test_empty_text_labels_remain_distinct_in_values_and_scenarios(self):
        samples = [[], [""], ["", "patio", "patio"], ["", " "]]
        for values in samples:
            value = block()
            value["simulation"].update(
                valueType="text", values=deepcopy(values), sort=False,
                scenarios=[{"id": f"case_{index}", "label": f"Entrada {index}",
                            "values": deepcopy(items)}
                           for index, items in enumerate(samples)])
            original = deepcopy(value)
            with self.subTest(values=values):
                self.assertIs(simulation.validate_contract(value), value["simulation"])
                self.assertEqual(value, original)
        for field in ("values", "scenario"):
            value = block()
            value["simulation"].update(valueType="text", values=[""], sort=False,
                                       scenarios=[{"id": "empty", "label": "Etiqueta vacía", "values": [""]}])
            target = value["simulation"] if field == "values" else value["simulation"]["scenarios"][0]
            target["values"] = [" ", ""]
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "ordenado"):
                simulation.validate_contract(value)

    def test_empty_labels_keep_text_limits_in_initial_and_scenario_inputs(self):
        for field in ("values", "scenario"):
            value = block()
            value["simulation"].update(valueType="text", values=[""], scenarios=[
                {"id": "empty", "label": "Etiquetas", "values": [""]}])
            target = value["simulation"] if field == "values" else value["simulation"]["scenarios"][0]
            target["values"] = [""] * 64
            simulation.validate_contract(value)
            for invalid in ([""] * 65, ["", "a\nb"], ["", "a\rb"], ["", "\0"],
                            ["", "\ud800"], ["", "ñ" * 65], ["", 1], ["", False]):
                target["values"] = invalid
                with self.subTest(field=field, invalid=repr(invalid)), self.assertRaises(ValueError):
                    simulation.validate_contract(value)

    def test_text_byte_limits_unicode_blankness_and_nul(self):
        paths = [((), "id", 128), ((), "title", 120), ((), "description", 4000),
                 (("simulation", "labels"), "array", 80),
                 (("simulation", "explanations"), "start", 2000),
                 (("simulation",), "outputTemplate", 256),
                 (("simulation", "scenarios", 0), "label", 80)]
        for path, field, limit in paths:
            value = block()
            target = value
            for item in path:
                target = target[item]
            target[field] = "ñ" * (limit // 2)
            simulation.validate_contract(value)
            for text in ("ñ" * (limit // 2) + "a", "", " \t\n", "\u2003\u00a0", "\0a", "\ud800"):
                target[field] = text
                with self.subTest(path=path, field=field, text=repr(text[:20])), self.assertRaises(ValueError):
                    simulation.validate_contract(value)

    def test_templates_allow_only_declared_names_and_do_not_evaluate_values(self):
        value = block()
        all_names = " ".join("{" + field + "}" for field in sorted(simulation.TEMPLATE_FIELDS))
        output_names = " ".join("{" + field + "}" for field in sorted(
            simulation.TEMPLATE_FIELDS - {"countedIndex", "originIndex"}))
        value["simulation"]["outputTemplate"] = output_names
        value["simulation"]["explanations"]["done"] = all_names
        simulation.validate_contract(value)
        for marker in ("{countedIndex}", "{originIndex}", "{value}:{originIndex}"):
            rejected = deepcopy(value)
            rejected["simulation"]["outputTemplate"] = marker
            with self.subTest(output_marker=marker), self.assertRaises(ValueError):
                simulation.validate_contract(rejected)
            accepted = deepcopy(value)
            accepted["simulation"]["explanations"]["start"] = marker
            accepted["simulation"]["explanations"]["advance"] = marker
            simulation.validate_contract(accepted)
        for template in ("{unknown}", "{value", "value}", "{}", "{{value}}",
                         "{ value }", "{frequency+1}", "{leaders[0]}", "{value!r}", "{value:4}"):
            for path in ("outputTemplate", "explanation"):
                altered = deepcopy(value)
                if path == "outputTemplate":
                    altered["simulation"][path] = template
                else:
                    altered["simulation"]["explanations"]["advance"] = template
                with self.subTest(template=template, path=path), self.assertRaises(ValueError):
                    simulation.validate_contract(altered)
        value["simulation"].update(valueType="text", values=["{maximum}"], scenarios=[])
        simulation.validate_contract(value)

    def test_scenarios_are_bounded_unique_and_have_matching_values(self):
        value = block()
        value["simulation"]["scenarios"] = [
            {"id": f"scenario_{i}", "label": f"Escenario {i}", "values": [i]}
            for i in range(8)]
        simulation.validate_contract(value)
        for scenarios in (None, {}, [value["simulation"]["scenarios"][0]] * 2,
                          value["simulation"]["scenarios"] + [{"id": "ninth", "label": "Noveno", "values": []}]):
            altered = deepcopy(value)
            altered["simulation"]["scenarios"] = scenarios
            with self.subTest(scenarios=scenarios), self.assertRaises(ValueError):
                simulation.validate_contract(altered)
        for identity in ("", "1bad", "con espacio", "á", "a" * 65, None, []):
            altered = deepcopy(value)
            altered["simulation"]["scenarios"][0]["id"] = identity
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                simulation.validate_contract(altered)
        value["simulation"]["scenarios"][0]["id"] = "a" * 64
        simulation.validate_contract(value)
        value["simulation"]["scenarios"][0]["values"] = ["a"]
        with self.assertRaises(ValueError):
            simulation.validate_contract(value)

    def test_counted_origin_placeholders_match_native_trace_budget(self):
        # Both indices are calculated, not authored as answers. Only start
        # and advance count a new element; completion phases must contribute
        # no invented source index. The expected byte totals are independent
        # manual oracles for the trace frame sequence.
        value = block()
        spec = value["simulation"]
        spec.update(values=[7, 3, 7, 2], scenarios=[], outputTemplate="{value}",
                    explanations={name: "x" for name in simulation.EXPLANATION_FIELDS})
        spec["explanations"]["start"] = "{countedIndex}:{originIndex}"
        spec["explanations"]["advance"] = "{countedIndex}:{originIndex}"
        original = deepcopy(value)
        simulation.validate_contract(value)
        self.assertEqual(45, simulation._trace_text_bytes(spec, spec["values"]))
        self.assertEqual(value, original)

        # In already sorted input, counted position and original position
        # coincide. A collection with no values must not fabricate an index.
        spec.update(sort=False, values=[2, 2, 7])
        simulation.validate_contract(value)
        self.assertEqual(25, simulation._trace_text_bytes(spec, spec["values"]))
        spec["values"] = []
        spec["explanations"]["empty"] = "{countedIndex}|{originIndex}"
        simulation.validate_contract(value)
        self.assertEqual(1, simulation._trace_text_bytes(spec, []))

        # Stable UTF-8 ordering repositions the empty string before "patio".
        # Its real original position is 1; repeated "patio" entries follow
        # their original indices 0 and 2.
        spec.update(sort=True, valueType="text", values=["patio", "", "patio"])
        spec["explanations"]["empty"] = "x"
        simulation.validate_contract(value)
        self.assertEqual(30, simulation._trace_text_bytes(spec, spec["values"]))
        spec["values"] = ["patio", ""]  # Invalid for sort=False
        spec["sort"] = False
        with self.assertRaises(ValueError):
            simulation.validate_contract(value)

    def test_processed_remaining_counts_use_the_real_observed_range(self):
        # Seven phases: initial, start, decision, start, advance, decision,
        # done. Each rendered count is 3 bytes (0/3 through 3/0); the
        # accumulated group outputs are 6 and then 12 bytes.
        value = block()
        spec = value["simulation"]
        spec.update(values=[5, 2, 5], scenarios=[],
                    outputTemplate="{value}:{processedCount}/{remainingCount}",
                    explanations={name: "{processedCount}/{remainingCount}"
                                  for name in simulation.EXPLANATION_FIELDS})
        published = deepcopy(value)
        self.assertIs(simulation.validate_contract(value), spec)
        self.assertEqual(3 + 3 + 9 + 9 + 9 + 15 + 15,
                         simulation._trace_text_bytes(spec, spec["values"]))
        self.assertEqual(value, published)

        # The last counted element is not also counted by the group decision.
        spec["values"] = [9, 9, 9]
        simulation.validate_contract(value)
        self.assertEqual(3 + 3 + 3 + 3 + 9 + 9,
                         simulation._trace_text_bytes(spec, spec["values"]))
        spec["values"] = []
        simulation.validate_contract(value)
        self.assertEqual(3, simulation._trace_text_bytes(spec, []))

        # For raw text output an empty string remains a real group, adding
        # ":1/2\\n" (5 bytes) before "patio:3/0\\n" (10 bytes).
        spec.update(valueType="text", values=["patio", "", "patio"])
        simulation.validate_contract(value)
        self.assertEqual(3 + 3 + 8 + 8 + 8 + 18 + 18,
                         simulation._trace_text_bytes(spec, spec["values"]))

        # The same contracts apply when sort is disabled and the input was
        # already ordered. Validation never rewrites the authored data.
        spec.update(sort=False, values=["", "patio", "patio"])
        before = deepcopy(value)
        simulation.validate_contract(value)
        self.assertEqual(66, simulation._trace_text_bytes(spec, spec["values"]))
        self.assertEqual(value, before)

    def test_previous_maximum_precedes_decision_in_explanation_and_output(self):
        lesson = block()
        spec = lesson["simulation"]
        spec.update(values=[7, 1, 7, 3, 3, 2, 5], scenarios=[],
                    outputTemplate="{value}:{frequency}/{previousMaximum}",
                    explanations={
                        "initial": "Inicio: {previousMaximum}.",
                        "start": "Contar: {previousMaximum}.",
                        "advance": "Continuar: {previousMaximum}.",
                        "newMaximum": "Antes {previousMaximum}; ahora {maximum}; frecuencia {frequency}.",
                        "tie": "Empate {frequency} con {previousMaximum}; máximo {maximum}.",
                        "belowMaximum": "Menor {frequency} que {previousMaximum}; sigue {maximum}.",
                        "done": "Fin: {previousMaximum}.",
                        "empty": "Vacío: {previousMaximum}.",
                    })
        original = deepcopy(lesson)
        self.assertIs(simulation.validate_contract(lesson), spec)
        # Independent 14-frame oracle, enumerated by observed count and
        # group decision rather than reusing the validator's loop.
        one = "1:1/0\n"
        two = one + "2:1/1\n"
        three = two + "3:2/1\n"
        four = three + "5:1/2\n"
        five = four + "7:2/2\n"
        steps = [
            ("Inicio: .", ""),
            ("Contar: .", ""),
            ("Antes 0; ahora 1; frecuencia 1.", one),
            ("Contar: .", one),
            ("Empate 1 con 1; máximo 1.", two),
            ("Contar: .", two),
            ("Continuar: .", two),
            ("Antes 1; ahora 2; frecuencia 2.", three),
            ("Contar: .", three),
            ("Menor 1 que 2; sigue 2.", four),
            ("Contar: .", four),
            ("Continuar: .", four),
            ("Empate 2 con 2; máximo 2.", five),
            ("Fin: .", five),
        ]
        expected = sum(len(explanation.encode("utf-8")) + len(output.encode("utf-8"))
                       for explanation, output in steps)
        self.assertEqual(expected, simulation._trace_text_bytes(spec, spec["values"]))
        self.assertEqual(len("Vacío: .".encode("utf-8")),
                         simulation._trace_text_bytes(spec, []))
        self.assertEqual(lesson, original)

        # The previous maximum is zero for the first (empty-string) group,
        # then one before the repeated text takes the lead.
        spec.update(valueType="text", values=["patio", "", "patio"])
        empty_group = ":1/0\n"
        whole_output = empty_group + "patio:2/1\n"
        label_steps = [
            ("Inicio: .", ""),
            ("Contar: .", ""),
            ("Antes 0; ahora 1; frecuencia 1.", empty_group),
            ("Contar: .", empty_group),
            ("Continuar: .", empty_group),
            ("Antes 1; ahora 2; frecuencia 2.", whole_output),
            ("Fin: .", whole_output),
        ]
        label_total = sum(len(explanation.encode("utf-8")) + len(output.encode("utf-8"))
                          for explanation, output in label_steps)
        self.assertEqual(label_total, simulation._trace_text_bytes(spec, spec["values"]))

    def test_trace_budget_counts_every_accumulated_frame_as_native_does(self):
        value = block()
        spec = value["simulation"]
        spec.update(values=[1], scenarios=[], outputTemplate="{value}",
                    explanations={name: "x" for name in simulation.EXPLANATION_FIELDS})
        # initial/start each have x; completion/done have x and "1\n".
        self.assertEqual(simulation._trace_text_bytes(spec, [1]), 8)
        # Two one-element groups: the first output remains during the second
        # group's start and the final complete output remains on done.
        self.assertEqual(simulation._trace_text_bytes(spec, [1, 2]), 18)
        self.assertEqual(simulation._trace_text_bytes(spec, []), 1)
        spec.update(valueType="text", values=["ñ"])
        self.assertEqual(simulation._trace_text_bytes(spec, ["ñ"]), 10)
        spec["values"] = ["{maximum}"]
        self.assertEqual(simulation._trace_text_bytes(spec, spec["values"]), 24)
        # Empty text remains a real one-element group. Its template expansion
        # is empty data, so the accumulated output still contains its newline.
        self.assertEqual(simulation._trace_text_bytes(spec, [""]), 6)
        self.assertEqual(simulation._trace_text_bytes(spec, ["", " "]), 14)

    def test_trace_budget_counts_display_labels_but_keeps_output_data_raw(self):
        spec = block()["simulation"]
        spec.update(valueType="text", values=[], scenarios=[], outputTemplate="{value}|{leaders}",
                    explanations={name: "{value}|{leaders}" for name in simulation.EXPLANATION_FIELDS})
        labels = [("", '"" (cadena vacía)'), (" ", '" " (1 espacio)'),
                  ("  ", '"  " (2 espacios)'), ("\t", '"\\t"'), ("\x1f", '"\\u001f"'),
                  ('a"b\\c', '"a\\"b\\\\c"'), ("ñ", '"ñ"'), ("{maximum}", '"{maximum}"')]
        for raw, display in labels:
            output = raw + "|" + raw + "\n"
            expected = [("|", ""), (display + "|", ""),
                        (display + "|" + display, output), ("|" + display, output)]
            with self.subTest(raw=repr(raw)):
                self.assertEqual(simulation._trace_text_bytes(spec, [raw]),
                                 sum(len((explanation + printed).encode("utf-8"))
                                     for explanation, printed in expected))
        empty, space = '"" (cadena vacía)', '" " (1 espacio)'
        # The first candidate is still the empty datum. Joining the two raw
        # candidates must retain its separator rather than dropping it.
        output = "|\n |,  \n"
        expected = [("|", ""), (empty + "|", ""), (empty + "|" + empty, "|\n"),
                    (space + "|" + empty, "|\n"),
                    (space + "|" + empty + ", " + space, output),
                    ("|" + empty + ", " + space, output)]
        self.assertEqual(simulation._trace_text_bytes(spec, ["", " "]),
                         sum(len((explanation + printed).encode("utf-8"))
                             for explanation, printed in expected))
        self.assertEqual(simulation._trace_text_bytes(spec, []), 1)

    def test_invisible_unicode_labels_have_reversible_visible_spelling(self):
        # Python's 8 MiB trace estimator must count the same escaped labels
        # that the Windows model narrates, not raw byte lengths of separators.
        block_value = block()
        spec = block_value["simulation"]
        spec.update(valueType="text", values=[], scenarios=[], outputTemplate="{value}|{leaders}",
                    explanations={name: "{value}|{leaders}"
                                  for name in simulation.EXPLANATION_FIELDS})
        cases = [
            ("\x7f", r'"\u007f"'), ("\x85", r'"\u0085"'),
            ("\u00a0", r'"\u00a0"'), ("\u00ad", r'"\u00ad"'),
            ("\u17b4", r'"\u17b4"'), ("\u17b5", r'"\u17b5"'),
            ("\u180b", r'"\u180b"'), ("\u180e", r'"\u180e"'),
            ("\u180f", r'"\u180f"'),
            ("\U0001bca0", r'"\ud82f\udca0"'),
            ("\U0001bca3", r'"\ud82f\udca3"'),
            ("\U0001d173", r'"\ud834\udd73"'),
            ("\U0001d17a", r'"\ud834\udd7a"'),
            ("\U000e0001", r'"\udb40\udc01"'),
            ("\U000e0020", r'"\udb40\udc20"'),
            ("\U000e007f", r'"\udb40\udc7f"'),
            ("\u115f", r'"\u115f"'), ("\u1160", r'"\u1160"'),
            ("\u2800", r'"\u2800"'), ("\u3164", r'"\u3164"'),
            ("\uffa0", r'"\uffa0"'),
            ("\u200b", r'"\u200b"'), ("\u2028", r'"\u2028"'),
            ("\u2029", r'"\u2029"'), ("\u2060", r'"\u2060"'),
            ("\u3000", r'"\u3000"'), ("\ufeff", r'"\ufeff"'),
            ("\ufe00", r'"\ufe00"'), ("\ufe0f", r'"\ufe0f"'),
            ("\U000e0100", r'"\udb40\udd00"'),
            ("\U000e01ef", r'"\udb40\uddef"'),
        ]
        cases.append(("A\ufe0f\U000e0100", r'"A\ufe0f\udb40\udd00"'))
        cases.append(("A\u2800B", r'"A\u2800B"'))
        cases.append(("A\u17b4B", r'"A\u17b4B"'))
        cases.append(("A\U000e0020B", r'"A\udb40\udc20B"'))
        for raw, display in cases:
            with self.subTest(codepoint=" ".join(f"U+{ord(ch):04X}" for ch in raw)):
                spec["values"] = [raw]
                before = deepcopy(block_value)
                simulation.validate_contract(block_value)
                output = raw + "|" + raw + "\n"
                steps = [("|", ""), (display + "|", ""),
                         (display + "|" + display, output), ("|" + display, output)]
                total = sum(len((explanation + stdout).encode("utf-8"))
                            for explanation, stdout in steps)
                self.assertEqual(simulation._trace_text_bytes(spec, [raw]), total)
                self.assertEqual(block_value, before)

    def test_all_353_variation_selectors_and_tags_match_json_utf16_escapes(self):
        block_value = block()
        spec = block_value["simulation"]
        spec.update(valueType="text", values=[], scenarios=[],
                    outputTemplate="{value}",
                    explanations={name: "{value}"
                                  for name in simulation.EXPLANATION_FIELDS})
        points = (tuple(range(0xfe00, 0xfe10)) + tuple(range(0xe0100, 0xe01f0))
                  + (0xe0001,) + tuple(range(0xe0020, 0xe0080)))
        self.assertEqual(len(points), 353)
        for point in points:
            raw = chr(point)
            if point <= 0xffff:
                expected = f"\\u{point:04x}"
            else:
                offset = point - 0x10000
                expected = (f"\\u{0xd800 + (offset >> 10):04x}"
                            f"\\u{0xdc00 + (offset & 0x3ff):04x}")
            with self.subTest(codepoint=f"U+{point:06X}"):
                # The renderer lives inside the trace-budget function.
                # Observe it through the calculated byte count, not through a
                # non-existent module-level value_text API.
                self.assertEqual(simulation._json_invisible_escape(point), expected)
                spec["values"] = [raw]
                before = deepcopy(block_value)
                simulation.validate_contract(block_value)
                # initial: no value, start/decision: the visible label,
                # decision/done: the accumulated raw output and newline.
                label = '"' + expected + '"'
                output = raw + "\n"
                expected_size = (2 * len(label.encode("utf-8")) +
                                 2 * len(output.encode("utf-8")))
                self.assertEqual(simulation._trace_text_bytes(spec, [raw]), expected_size)
                self.assertEqual(block_value, before)

    def test_trace_expansion_limit_covers_default_and_each_scenario(self):
        value = block()
        spec = value["simulation"]
        large_values = [f"{index:02d}" + "x" * 126 for index in range(64)]
        spec.update(valueType="text", values=large_values, scenarios=[],
                    outputTemplate="{leaders}" * 28)
        with self.assertRaisesRegex(ValueError, "8 MiB"):
            simulation.validate_contract(value)
        spec["values"] = []
        spec["scenarios"] = [{"id": "large", "label": "Grande", "values": large_values}]
        with self.assertRaisesRegex(ValueError, "8 MiB"):
            simulation.validate_contract(value)
        spec["outputTemplate"] = "{value}: {frequency}"
        simulation.validate_contract(value)


class SimulationIntegrationTest(unittest.TestCase):
    def setUp(self):
        curriculum.ERRORS.clear()

    def tearDown(self):
        curriculum.ERRORS.clear()

    def test_sequence_review_skips_simulation_and_still_finds_a_real_later_puzzle(self):
        introduction = {"type": "paragraph",
                        "text": "Antes de ejecutar el programa, resuelve el puzzle al final."}
        compiler = lesson()["pages"][0]["blocks"][0]
        puzzle = lesson()["pages"][2]["blocks"][0]
        code = "RESPUESTA_POSTERIOR_ANTES_EDITOR"
        document = {"pages": [{"blocks": [introduction, compiler, block()]}]}
        before = deepcopy(document)
        self.assertEqual([finding for finding in sequence.review_lesson(document)
                          if finding.code == code], [])
        self.assertEqual(document, before)

        # A simulation before the editor must not hide the actual puzzle
        # after it or replace that puzzle's reported page/block address.
        document["pages"][0]["blocks"] = [introduction, block(), compiler, puzzle]
        before = deepcopy(document)
        findings = [finding for finding in sequence.review_lesson(document)
                    if finding.code == code]
        self.assertEqual(len(findings), 1)
        self.assertEqual((findings[0].severity, findings[0].related_page,
                          findings[0].related_block), ("ERROR", 1, 4))
        self.assertEqual(document, before)

    def test_general_validator_accepts_simulation_without_any_answer_contract(self):
        value = block()
        curriculum.validate_simulation_contract(value, "simulation QA")
        courses._block(curriculum, value, "simulation QA", root=Path("."),
                       document=lesson(), position=(1, 1), structure_only=True)
        self.assertEqual(curriculum.ERRORS, [])
        self.assertIn("simulation", curriculum.FIELDS)
        self.assertNotIn("simulation", curriculum.EXERCISES)
        value["simulation"]["values"] = [True]
        courses._block(curriculum, value, "simulation QA", root=Path("."),
                       document=lesson(), position=(1, 1), structure_only=True)
        self.assertTrue(any("simulación curricular no válida" in text for text in curriculum.ERRORS))

    def test_state_layout_rejects_decimal_tokens_in_active_and_archived_simulations(self):
        raw = lesson()
        raw["pages"][1]["blocks"][1]["simulation"]["scenarios"].append(
            {"id": "uno", "label": "Un valor", "values": [1]})
        source = state.prepare_v2(raw, "es", {"es": raw})
        state.validate(source)
        retired = state.retire(source, ["block_0001_0001"])
        state.validate(retired)
        locations = {
            "active": (source, ("pages", 1, "blocks", 1)),
            "retired": (retired, ("stateLayout", "retired", 0, "block")),
            "history": (retired, ("stateLayout", "retirementHistory", 0,
                                  "pages", 1, "blocks", 1)),
        }
        for place, (base, path) in locations.items():
            for field in ("version", "values", "scenario"):
                for token in ("1.0", "1e0"):
                    document = deepcopy(base)
                    current = document
                    for part in path:
                        current = current[part]
                    spec = current["simulation"]
                    integral = json.loads(token)
                    if field == "version":
                        spec["version"] = integral
                    elif field == "values":
                        spec["values"][1] = integral
                    else:
                        spec["scenarios"][1]["values"][0] = integral
                    with self.subTest(place=place, field=field, token=token):
                        simulation.validate_contract(current)
                        self.assertEqual(state.fingerprint(current),
                                         state.fingerprint(raw["pages"][1]["blocks"][1]))
                        with self.assertRaisesRegex(ValueError, "sin punto ni exponente"):
                            simulation.validate_document(document)
                        with self.assertRaisesRegex(state.LayoutError, "sin punto ni exponente"):
                            state.validate(document)

        # The document guard follows the presence of stateLayout, not a new
        # restriction in the standalone schema or in historical fingerprinting.
        isolated = lesson()
        isolated["pages"][1]["blocks"][1]["simulation"].update(version=1.0, values=[1.0])
        simulation.validate_document(isolated)

    def test_only_static_title_and_description_are_public_semantic_sources(self):
        value = block()
        self.assertEqual(text_semantics.public_text_fields(value), {
            "/title": value["title"], "/description": value["description"]})
        raw = lesson()
        document = state.prepare_v2(raw, "es", {"es": raw})
        identity = "block_0001_0001"
        document["textSemantics"] = {"version": 1, "fields": [
            text_semantics.make_field(document, identity, "/title", [])]}
        state.validate(document)
        for path in ("/simulation/explanations/done", "/simulation/outputTemplate",
                     "/simulation/labels/array", "/simulation/values/0"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                text_semantics.make_field(document, identity, path, [])

    def test_reordering_preserves_every_original_storage_address(self):
        source = state.prepare(lesson())
        before = deepcopy(source)
        moved = state.reorder(source, list(reversed(source["stateLayout"]["current"])))
        self.assertEqual(source, before)
        state.validate_transition(source, moved)
        self.assertEqual(state.validate(moved)[1, 1], (1, 1))
        self.assertEqual(state.validate(moved)[0, 0], (2, 0))
        self.assertEqual(state.validate(moved)[2, 0], (0, 0))

    def test_simulation_addition_has_new_storage_without_replacing_a_puzzle(self):
        raw = lesson(False)
        before = state.prepare_v2(raw, "es", {"es": raw})
        plan = deepcopy(before["stateLayout"]["current"])
        plan[1]["blocks"].append("new_simulation")
        added = state.reorder(before, plan, [{
            "id": "new_simulation", "storage": {"page": 3, "block": 0},
            "progressPage": "page_0001", "block": block()}])
        state.validate_transition(before, added)
        self.assertEqual(state.validate(added)[1, 1], (3, 0))
        self.assertEqual(state.validate(added)[0, 0], (0, 0))
        self.assertEqual(state.validate(added)[2, 0], (2, 0))
        self.assertEqual(added["stateLayout"]["origin"], before["stateLayout"]["origin"])

    def test_documented_text_example_has_an_independent_stable_owner(self):
        """Work can add this ungraded model without adopting an old puzzle."""
        raw = lesson(False)
        before = state.prepare_v2(raw, "es", {"es": raw})
        authoring = json.loads(EMPTY_TEXT_EXAMPLE.read_text(encoding="utf-8"))
        plan = deepcopy(before["stateLayout"]["current"])
        plan[1]["blocks"].append("new_text_example")
        added = state.reorder(before, plan, [{
            "id": "new_text_example", "storage": {"page": 3, "block": 0},
            "progressPage": "page_0001", "block": authoring,
        }])
        state.validate_transition(before, added)
        self.assertEqual(before["stateLayout"]["origin"],
                         added["stateLayout"]["origin"])
        self.assertEqual(state.validate(added)[1, 1], (3, 0))
        self.assertEqual(added["pages"][1]["blocks"][1], authoring)
        self.assertEqual(before["pages"][0]["blocks"][0],
                         added["pages"][0]["blocks"][0])
        self.assertEqual(before["pages"][2]["blocks"][0],
                         added["pages"][2]["blocks"][0])
        self.assertNotIn("new_text_example",
                         {identity for page in before["stateLayout"]["current"]
                          for identity in page["blocks"]})
        self.assertIn("new_text_example",
                      {identity for page in added["stateLayout"]["current"]
                       for identity in page["blocks"]})

    def test_complete_contract_cannot_change_as_an_editorial_revision(self):
        raw = lesson()
        before = state.prepare_v2(raw, "es", {"es": raw})
        for change in (lambda b: b.update(title="Otro título"),
                       lambda b: b.update(description="Otra explicación"),
                       lambda b: b["simulation"].update(values=[1]),
                       lambda b: b["simulation"].update(view="summary"),
                       lambda b: b["simulation"]["labels"].update(start="otro índice"),
                       lambda b: b["simulation"].update(outputTemplate="Otra salida {value}")):
            proposal = deepcopy(before)
            current = proposal["pages"][1]["blocks"][1]
            change(current)
            self.assertNotEqual(state.state_fingerprint(current),
                                state.state_fingerprint(before["pages"][1]["blocks"][1]))
            with self.assertRaises(state.LayoutError):
                state.prepare_v2(proposal, "es", {"es": raw}, before)

    def test_reference_after_is_allowed_but_simulation_is_not_a_graded_activity(self):
        source = state.prepare(lesson())
        source["referenceAfter"] = [{"activity": "block_0000_0000", "blocks": ["block_0001_0001"]}]
        state.validate(source)
        source["activityAfter"] = [{"activity": "block_0001_0001", "requires": ["block_0000_0000"]}]
        with self.assertRaisesRegex(state.LayoutError, "dependent activity"):
            state.validate(source)

    def test_retirement_keeps_complete_simulation_and_reading_snapshot(self):
        raw = lesson()
        source = state.prepare_v2(raw, "es", {"es": raw})
        retired = state.retire(source, ["block_0001_0001"])
        state.validate_transition(source, retired)
        self.assertEqual(retired["stateLayout"]["retired"][0]["block"], raw["pages"][1]["blocks"][1])
        self.assertEqual(retired["stateLayout"]["retirementHistory"][0]["pages"], source["pages"])
        self.assertEqual(state.validate(retired)[0, 0], (0, 0))

    def test_empty_labels_keep_distinct_fingerprints_and_preserved_history(self):
        samples = [[], [""], [" "], ["", " "]]
        fingerprints = []
        for values in samples:
            raw = lesson()
            original = raw["pages"][1]["blocks"][1]
            original["simulation"].update(valueType="text", values=deepcopy(values), scenarios=[])
            source = state.prepare_v2(raw, "es", {"es": raw})
            before = deepcopy(source)
            with self.subTest(values=values):
                state.validate(source)
                self.assertEqual(source, before)
                fingerprints.append(state.state_fingerprint(original))
                retired = state.retire(source, ["block_0001_0001"])
                state.validate_transition(source, retired)
                self.assertEqual(retired["stateLayout"]["retired"][0]["block"], original)
                self.assertEqual(retired["stateLayout"]["retirementHistory"][0]["pages"], source["pages"])
                changed = deepcopy(source)
                changed["pages"][1]["blocks"][1]["simulation"]["values"].append("")
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(source, changed)
        self.assertEqual(len(set(fingerprints)), len(samples))

    def test_validation_checks_active_retired_and_retirement_history_sources(self):
        for place in ("active", "retired", "history"):
            bad = block()
            bad["simulation"]["outputTemplate"] = "{not_a_field}"
            document = {"pages": [], "stateLayout": {}}
            if place == "active":
                document["pages"] = [{"blocks": [bad]}]
            elif place == "retired":
                document["stateLayout"]["retired"] = [{"block": bad}]
            else:
                document["stateLayout"]["retirementHistory"] = [{"pages": [{"blocks": [bad]}]}]
            with self.subTest(place=place), self.assertRaises(ValueError):
                simulation.validate_document(document)
        invalid_lesson = lesson()
        invalid_lesson["pages"][1]["blocks"][1] = bad
        with self.assertRaisesRegex(state.LayoutError, "Invalid curricular simulation"):
            state.validate(invalid_lesson)


if __name__ == "__main__":
    unittest.main()

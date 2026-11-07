"""Compiler guidance is editorial; executable contracts and provenance stay closed."""
from copy import deepcopy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import stable_state_layout as layout
import text_semantics
import validar_json_programacion as contract

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json"


def read(path):
    return json.loads(path.read_bytes().decode("utf-8-sig"))


def guided(block):
    candidate = deepcopy(block)
    if "outputCheck" in candidate:
        for number, case in enumerate(candidate["outputCheck"]["cases"]):
            case["explanation"] = f"QA editorial: explicación del caso {number}."
            # This is an authored incorrect-output selector, never a captured run.
            wrong = f"__QA_UNOBSERVED_OUTPUT_{number}__"
            while wrong in case["outputs"]:
                wrong += "_"
            case["reviewFeedback"] = [{"outputs": [wrong], "text": "QA editorial: revisa los resultados solicitados."}]
    else:
        candidate["compileCheck"]["explanation"] = "QA editorial: explica el diagnóstico ya declarado."
    return candidate


def reauthor_changed_public_fields(before, after, identity):
    """Author the synthetic QA prose without retaining stale published ranges."""
    previous = text_semantics.public_text_fields(layout._blocks(before)[identity])
    current = text_semantics.public_text_fields(layout._blocks(after)[identity])
    changed = {path for path in previous.keys() | current.keys() if previous.get(path) != current.get(path)}
    metadata = after.setdefault("textSemantics", {"version": 1, "fields": []})
    # Retain every untouched descriptor, including the rest of this owner.
    # Removed review rules lose only their no-longer-public descriptors.
    metadata["fields"] = [field for field in metadata["fields"]
                          if field["block"] != identity or field["path"] not in changed]
    metadata["fields"].extend(text_semantics.make_field(after, identity, path, [])
                              for path in sorted(changed & current.keys()))
    return changed


class CompilerGuidanceProjectionTest(unittest.TestCase):
    def schema_errors(self, block):
        with patch.object(contract, "ERRORS", []):
            contract.validate_cpp_compiler(block, "QA in-memory compiler guidance")
            return list(contract.ERRORS)

    def preserved(self, old, new):
        return contract.preserves_previous_compiler("qa/editor.json", 0, 0, old, new, {})

    def test_current_second_l20_editor_passes_the_actual_historical_preservation_loop(self):
        path = ROOT / RELATIVE
        original_bytes = path.read_bytes()
        family = {locale: read(ROOT / RELATIVE.replace("/es/", f"/{locale}/")) for locale in ("es", "en", "fr")}
        original = family["es"]
        source = deepcopy(original)
        identity = "block_0009_0002"
        old = layout._blocks(original)[identity]
        target = layout._blocks(source)[identity]
        target.update(guided(target))
        changed_fields = reauthor_changed_public_fields(original, source, identity)
        descriptors = {(field["block"], field["path"]): field for field in source["textSemantics"]["fields"]}
        for field in original.get("textSemantics", {}).get("fields", []):
            if field["block"] != identity or field["path"] not in changed_fields:
                self.assertEqual(field, descriptors[field["block"], field["path"]])
        text_semantics.validate(source)
        stale = deepcopy(source)
        field = next(field for field in stale["textSemantics"]["fields"]
                     if field["block"] == identity and field["path"] in changed_fields)
        field["sourceSha256"] = "0" * 64
        with self.assertRaisesRegex(layout.LayoutError, "source fingerprint is stale"):
            layout.prepare_v2(stale, "es", family, original)
        prepared = layout.prepare_v2(source, "es", family, original)
        layout.validate_transition(original, prepared)
        new = layout._blocks(prepared)[identity]
        self.assertEqual(self.schema_errors(new), [])
        self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
        self.assertEqual(layout.without_compiler_guidance(old), layout.without_compiler_guidance(new))
        for key in ("origin", "current", "family", "additions"):
            self.assertEqual(original["stateLayout"][key], prepared["stateLayout"][key])
        previous_edges = original["stateLayout"]["editorial"]
        self.assertEqual(prepared["stateLayout"]["editorial"][:-1], previous_edges)
        self.assertEqual(prepared["stateLayout"]["editorial"][-1], dict(id=identity,
            beforeSha256=layout.fingerprint(old), afterSha256=layout.fingerprint(new)))
        aligned = layout.align_for_previous_validation(original, prepared)
        positions = layout.validate(original)
        checked = 0
        for pn, page in enumerate(original["pages"]):
            for bn, block in enumerate(page["blocks"]):
                if block["type"] != "cpp_compiler":
                    continue
                self.assertTrue(contract.preserves_previous_compiler(RELATIVE, *positions[pn, bn], block,
                    aligned["pages"][pn]["blocks"][bn], layout.original_projection(original)))
                checked += 1
        self.assertEqual(checked, 2)
        bad_edge = deepcopy(prepared)
        bad_edge["stateLayout"]["editorial"][-1]["beforeSha256"] = "0" * 64
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(original, bad_edge)
        bad_identity = deepcopy(prepared)
        bad_identity["stateLayout"]["current"][9]["blocks"][2] = "wrong_owner"
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(original, bad_identity)
        self.assertEqual(original_bytes, path.read_bytes())

    def test_current_compilers_accept_guidance_revisions_without_changes_to_executable_payloads(self):
        counts = {"outputCheck": 0, "compileCheck": 0}
        for path in sorted(CONTENT.glob("*/lesson_*.json")):
            raw = path.read_bytes()
            lesson = json.loads(raw.decode("utf-8-sig"))
            positions = layout.validate(lesson)
            for pn, page in enumerate(lesson["pages"]):
                for bn, old in enumerate(page["blocks"]):
                    if old.get("type") != "cpp_compiler":
                        continue
                    kind = "outputCheck" if "outputCheck" in old else "compileCheck"
                    candidate = guided(old)
                    with self.subTest(lesson=lesson["id"], position=(pn, bn)):
                        self.assertEqual(self.schema_errors(candidate), [])
                        self.assertEqual(layout.without_compiler_guidance(candidate), layout.without_compiler_guidance(old))
                        self.assertTrue(contract.preserves_previous_compiler(path.relative_to(ROOT).as_posix(),
                            *positions[pn, bn], old, candidate, layout.original_projection(lesson)))
                        self.assertEqual(old, page["blocks"][bn])
                    counts[kind] += 1
            self.assertEqual(raw, path.read_bytes())
        self.assertEqual(counts, {"outputCheck": 354, "compileCheck": 17})

    def test_all_executable_case_fields_and_their_order_stay_protected(self):
        old = {"type": "cpp_compiler", "language": "cpp", "initialCode": "int main() {}",
               "initialStandardInput": "A", "outputCheck": {"inputComparison": "exact", "cases": [
                   {"standardInput": "A", "outputs": ["A", "A\n"], "exitCode": 0, "standardError": "nonBlank"},
                   {"standardInput": "B", "outputs": ["B"], "exitCode": 1}]}}
        baseline = guided(old)
        self.assertEqual(self.schema_errors(baseline), [])
        self.assertTrue(self.preserved(old, baseline))
        for field, value in (("initialCode", "int main(){return 1;}"), ("initialStandardInput", "B"),
                             ("language", "c"), ("type", "code"), ("id", "another")):
            candidate = deepcopy(baseline)
            candidate[field] = value
            self.assertFalse(self.preserved(old, candidate), field)
        for field, value in (("standardInput", "other"), ("outputs", ["other"]), ("exitCode", 2),
                             ("standardError", "wrong"), ("target", "another"), ("unexpected", "hidden")):
            candidate = deepcopy(baseline)
            candidate["outputCheck"]["cases"][0][field] = value
            self.assertFalse(self.preserved(old, candidate), field)
        for action in (lambda cases: cases.pop(), lambda cases: cases.append(deepcopy(cases[0])), lambda cases: cases.reverse(),
                       lambda cases: cases[0]["outputs"].reverse(), lambda cases: cases[0].pop("standardError")):
            candidate = deepcopy(baseline)
            action(candidate["outputCheck"]["cases"])
            self.assertFalse(self.preserved(old, candidate))
        for field, value in (("inputComparison", "tokens"), ("unrecognized", True)):
            candidate = deepcopy(baseline)
            candidate["outputCheck"][field] = value
            self.assertFalse(self.preserved(old, candidate), field)

    def test_project_files_selection_cmake_configuration_and_targets_stay_protected(self):
        direct = {"type": "cpp_compiler", "initialCode": "", "projectFiles": [
            {"path": "main.cpp", "initialCode": "int main() {}"}, {"path": "data.cpp", "initialCode": "int data(){return 0;}"}],
            "sourceFiles": ["main.cpp", "data.cpp"], "outputCheck": {"cases": [{"standardInput": "", "outputs": ["OK"]}]}}
        cmake = deepcopy(direct)
        cmake.pop("sourceFiles")
        cmake["projectFiles"].append({"path": "CMakeLists.txt", "initialCode": "project(QA LANGUAGES CXX)"})
        cmake["cmake"] = {"target": "client", "options": {"STRICT": "ON"}, "buildType": "Release"}
        cmake["outputCheck"]["cases"][0]["target"] = "client"
        for old in (direct, cmake):
            baseline = guided(old)
            self.assertEqual(self.schema_errors(baseline), [])
            self.assertTrue(self.preserved(old, baseline))
            for action in (lambda block: block["projectFiles"][0].update(initialCode="different"),
                           lambda block: block["projectFiles"][0].update(path="other.cpp"),
                           lambda block: block["projectFiles"].reverse()):
                candidate = deepcopy(baseline)
                action(candidate)
                self.assertFalse(self.preserved(old, candidate))
        changed = guided(direct)
        changed["sourceFiles"].reverse()
        self.assertFalse(self.preserved(direct, changed))
        for field, value in (("target", "other"), ("options", {"STRICT": "OFF"}), ("buildType", "Debug")):
            changed = guided(cmake)
            changed["cmake"][field] = value
            self.assertFalse(self.preserved(cmake, changed), field)

    def test_diagnostic_guidance_never_hides_a_changed_or_removed_diagnostic(self):
        path = CONTENT / "datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json"
        lesson = read(path)
        old = next(block for page in lesson["pages"] for block in page["blocks"] if "compileCheck" in block)
        candidate = guided(old)
        self.assertEqual(self.schema_errors(candidate), [])
        self.assertTrue(self.preserved(old, candidate))
        for patch_value in ({"diagnostic": "another"}, {"unrecognized": True}):
            changed = deepcopy(candidate)
            changed["compileCheck"].update(patch_value)
            self.assertFalse(self.preserved(old, changed))
            self.assertTrue(self.schema_errors(changed))
        removed = deepcopy(candidate)
        removed.pop("compileCheck")
        self.assertFalse(self.preserved(old, removed))
        self.assertIn("compileCheck", layout.without_compiler_guidance(candidate))

    def test_diagnostic_guidance_preserves_published_history_and_explicit_returns(self):
        owners = [next(owner for owner, spec in layout.COMPILE_DIAGNOSTICS.items() if not spec.get("project")),
                  next(owner for owner, spec in layout.COMPILE_DIAGNOSTICS.items() if spec.get("project"))]
        for lesson_id, identity in owners:
            _, unit, lesson = lesson_id.split("__")
            path = CONTENT / unit / ("lesson_" + lesson + ".json")
            raw = path.read_bytes()
            before = read(path)
            relative = path.relative_to(ROOT).as_posix()
            family = {locale: read(ROOT / relative.replace("/es/", f"/{locale}/"))
                      for locale in ("es", "en", "fr")}
            first = deepcopy(before)
            layout._blocks(first)[identity]["compileCheck"]["explanation"] = "QA primera guía editorial del diagnóstico."
            first = layout.prepare_v2(first, "es", family, before)
            layout.validate_transition(before, first)
            retired = layout.retire(first, ["block_0000_0000"])
            second = deepcopy(retired)
            layout._blocks(second)[identity]["compileCheck"]["explanation"] = "QA segunda guía editorial del mismo diagnóstico."
            second = layout.prepare_v2(second, "es", family, retired)
            layout.validate_transition(retired, second)
            with self.subTest(lesson=lesson_id):
                self.assertEqual(retired["stateLayout"]["retirementHistory"], second["stateLayout"]["retirementHistory"])
                self.assertEqual(layout.state_fingerprint(layout._blocks(first)[identity]),
                                 layout.state_fingerprint(layout._blocks(second)[identity]))
                snapshot = second["stateLayout"]["retirementHistory"][-1]
                snapshot_block = next(block for page, record in zip(snapshot["pages"], snapshot["current"])
                                      for block, owner in zip(page["blocks"], record["blocks"]) if owner == identity)
                self.assertEqual(layout._blocks(first)[identity], snapshot_block)
                for patch_value in ({"explanation": "QA guía que nunca perteneció a una presentación publicada."},
                                    {"diagnostic": "unknown"}):
                    forged = deepcopy(second)
                    history = forged["stateLayout"]["retirementHistory"][-1]
                    target = next(block for page, record in zip(history["pages"], history["current"])
                                  for block, owner in zip(page["blocks"], record["blocks"]) if owner == identity)
                    target["compileCheck"].update(patch_value)
                    with self.assertRaises(layout.LayoutError):
                        layout.validate(forged)
                chain = [proof for proof in second["stateLayout"]["editorial"] if proof["id"] == identity]
                nodes = [chain[0]["beforeSha256"]] + [proof["afterSha256"] for proof in chain]
                returned = deepcopy(second)
                layout._blocks(returned)[identity]["compileCheck"] = deepcopy(layout._blocks(first)[identity]["compileCheck"])
                returned = layout.prepare_v2(returned, "es", family, second,
                    editorial_returns={identity: nodes.index(layout.fingerprint(layout._blocks(first)[identity]))})
                layout.validate_transition(second, returned)
                self.assertEqual(second["stateLayout"]["retirementHistory"], returned["stateLayout"]["retirementHistory"])
                removed = deepcopy(second)
                layout._blocks(removed)[identity].pop("compileCheck")
                with self.assertRaises(layout.LayoutError):
                    layout.prepare_v2(removed, "es", family, second, editorial_returns={identity: 0})
                for patch_value in ({"diagnostic": "unknown"}, {"unrecognized": True}):
                    changed = deepcopy(second)
                    layout._blocks(changed)[identity]["compileCheck"].update(patch_value)
                    with self.assertRaises(layout.LayoutError):
                        layout.prepare_v2(changed, "es", family, second)
                self.assertEqual(raw, path.read_bytes())

    def test_guide_shapes_still_need_the_complete_visible_schema(self):
        old = {"type": "cpp_compiler", "initialCode": "int main(){}", "outputCheck": {"cases": [{"standardInput": "", "outputs": ["OK"]}]}}
        for malformed in (dict(explanation=""), dict(explanation=17), dict(reviewFeedback="not a list"),
                          dict(reviewFeedback=[{"outputs": ["OK"], "text": "reveals success"}]),
                          dict(reviewFeedback=[{"outputs": ["wrong"], "text": "", "unexpected": True}])):
            candidate = guided(old)
            candidate["outputCheck"]["cases"][0].update(malformed)
            self.assertTrue(self.schema_errors(candidate), malformed)
            self.assertEqual(layout.without_compiler_guidance(old), layout.without_compiler_guidance(candidate))


if __name__ == "__main__":
    unittest.main()

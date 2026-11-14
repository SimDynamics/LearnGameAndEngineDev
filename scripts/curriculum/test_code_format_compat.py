"""Source-only formatting and historical state compatibility contracts."""
from __future__ import annotations

from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).parent
fmt_spec = importlib.util.spec_from_file_location("code_format_compat", ROOT / "code_format_compat.py")
fmt = importlib.util.module_from_spec(fmt_spec)
fmt_spec.loader.exec_module(fmt)
layout_spec = importlib.util.spec_from_file_location("stable_state_layout", ROOT / "stable_state_layout.py")
layout = importlib.util.module_from_spec(layout_spec)
layout_spec.loader.exec_module(layout)

def fixture():
    return {
        "id": "cpp_fundamentals__source_format_qa",
        "title": "Contrato de formato",
        "pages": [{"blocks": [
            {"type": "code", "code": "#include <iostream>\nint main(){int number=2;return number;}\n"},
            {"type": "output_prediction", "question": "¿Qué imprime?",
             "code": "int number=2;\nstd::cout << number;", "answers": ["2"]},
            {"type": "order_code", "question": "Ordena",
             "fragments": ["#include <iostream>\n", "int main(){", "return 0;", "}"],
             "correctOrder": [0, 1, 2, 3]},
            {"type": "drag_fill_code", "question": "Completa",
             "template": "int main(){\n[]\nreturn 0;\n}",
             "choices": ["int n=2;"], "correctOrder": [0]},
            {"type": "cpp_compiler", "initialCode": "int main(){return 0;}",
             "projectFiles": [
                 {"path": "CMakeLists.txt", "initialCode":
                  "cmake_minimum_required(VERSION 3.20)\nproject(QA LANGUAGES CXX)\nadd_executable(example main.cpp)\n"},
                 {"path": "main.cpp", "initialCode": "int main(){return 0;}\n"}],
             "cmake": {"target": "example", "buildType": "Debug"},
             "outputCheck": {"cases": [{"standardInput": "", "outputs": [""]}]}},
        ]}]
    }

def edited(source):
    out = deepcopy(source)
    blocks = out["pages"][0]["blocks"]
    blocks[0]["code"] = "#include <iostream>\n\nint main() {\n    int number = 2;\n\n    return number;\n}\n"
    blocks[1]["code"] = "int number = 2;\n\nstd::cout << number;"
    blocks[2]["fragments"][1] = "int main() {\n"
    blocks[2]["fragments"][2] = "    return 0;"
    blocks[3]["template"] = "int main() {\n\n[]\n\nreturn 0;\n}"
    blocks[3]["choices"][0] = "int n = 2;"
    blocks[4]["initialCode"] = "int main() {\n  return 0;\n}"
    blocks[4]["projectFiles"][0]["initialCode"] = (
        "cmake_minimum_required(VERSION 3.20)\n\n"
        "project(QA LANGUAGES CXX)\n\nadd_executable(\n  example\n  main.cpp\n)\n")
    blocks[4]["projectFiles"][1]["initialCode"] = "int main() {\n    return 0;\n}\n"
    return out

class SourceFormatTest(unittest.TestCase):
    def test_formatting_preserves_every_code_location(self):
        before = fixture()
        after = edited(before)
        for a, b in zip(before["pages"][0]["blocks"], after["pages"][0]["blocks"]):
            self.assertTrue(fmt.require_source_format(a, b))
            self.assertEqual(fmt.source_projection(b, a), a)

    def test_unsafe_semantic_changes_are_rejected(self):
        before = fixture()
        examples = [
            ("code", "int n=1; // reason\nreturn n;", "int n=1; // reason return n;"),
            ("code", 'const char* s="one\\ntwo";', 'const char* s="one\ntwo";'),
            ("code", 'const char* s=R"raw(one\ntwo)raw";', 'const char* s=R"raw(one\n\ntwo)raw";'),
            ("code", "int n=1;return n;", "int n=2;return n;"),
            ("code", "int n=x+ +y;", "int n=x++y;"),
            ("code", "int n=0;/* note */return n;", "int n=0;/* changed */return n;"),
        ]
        for kind, source, target in examples:
            with self.subTest(source=source, target=target), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format({"type": kind, "code": source},
                                          {"type": kind, "code": target})
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format({"type": "code", "code": "#define X \\\n  1\nint x=X;\n"},
                                      {"type": "code", "code": "#define X \\\n  1\n\nint x=X;\n"})
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format({"type": "cpp_compiler", "projectFiles": [
                {"path": "CMakeLists.txt", "initialCode": "message(\"a\\nb\")"}]},
                {"type": "cpp_compiler", "projectFiles": [
                    {"path": "CMakeLists.txt", "initialCode": "message(\"a\nb\")"}]})

    def test_source_line_numbers_and_udl_token_boundaries_are_protected(self):
        code_cases = [
            ('int main(){return __LINE__;}',
             'int main(){\nreturn __LINE__;\n}'),
            ('auto line = std::source_location::current().line();',
             'auto line =\nstd::source_location::current().line();'),
            ('auto size = "abc"_length;',
             'auto size = "abc" _length;'),
            ('auto size = R"tag(abc)tag"_length;',
             'auto size = R"tag(abc)tag" _length;'),
        ]
        for before, after in code_cases:
            with self.subTest(before=before, after=after), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format({"type": "code", "code": before},
                                          {"type": "code", "code": after})
        cmake_before = 'message("${CMAKE_CURRENT_LIST_LINE}")\nmessage(STATUS "ok")'
        cmake_after = '\n' + cmake_before
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format({"type": "code", "language": "cmake", "code": cmake_before},
                                      {"type": "code", "language": "cmake", "code": cmake_after})

    def test_answers_and_file_lists_are_not_formatting(self):
        before = fixture()
        after = edited(before)
        after["pages"][0]["blocks"][1]["answers"] = ["3"]
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format(before["pages"][0]["blocks"][1], after["pages"][0]["blocks"][1])
        after = edited(before)
        after["pages"][0]["blocks"][4]["projectFiles"][0]["path"] = "different.cmake"
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format(before["pages"][0]["blocks"][4], after["pages"][0]["blocks"][4])

    def test_v2_format_archive_preserves_all_old_storage_signatures(self):
        before = fixture()
        family = {"es": deepcopy(before)}
        original = layout.prepare_v2(before, "es", family)
        modified = edited(original)
        formatted = layout.prepare_v2(modified, "es", family, original)
        self.assertEqual(len(formatted["stateLayout"]["sourceFormatRevisions"]), 5)
        self.assertEqual(layout.validate(original), layout.validate(formatted))
        self.assertEqual(formatted["stateLayout"]["origin"], original["stateLayout"]["origin"])
        self.assertEqual(formatted["stateLayout"]["family"], original["stateLayout"]["family"])
        self.assertEqual(len(formatted["stateLayout"]["editorial"]), 5)
        layout.validate_transition(original, formatted)
        layout.validate_transition(before, formatted)
        self.assertEqual(fmt.source_projection(formatted["pages"][0]["blocks"][4],
                                                original["pages"][0]["blocks"][4]),
                         original["pages"][0]["blocks"][4])

    def test_retirement_reading_history_preserves_formatting_compatibility(self):
        # A retired, unrelated paragraph must not prevent the physical
        # formatting of code still visible in the earlier reading snapshot.
        before = fixture()
        before["pages"][0]["blocks"].append(
            {"type": "paragraph", "text": "Presentación anterior."})
        family = {"es": deepcopy(before)}
        initial = layout.prepare_v2(before, "es", family)
        obsolete = initial["stateLayout"]["current"][0]["blocks"][-1]
        retired = layout.retire(initial, [obsolete])
        self.assertEqual(len(retired["stateLayout"]["retirementHistory"]), 1)
        formatted = layout.prepare_v2(edited(retired), "es", family, retired)
        self.assertEqual(len(formatted["stateLayout"]["sourceFormatRevisions"]), 5)
        self.assertEqual(retired["stateLayout"]["retirementHistory"],
                         formatted["stateLayout"]["retirementHistory"])
        layout.validate(formatted)
        layout.validate_transition(retired, formatted)
        layout.validate_transition(initial, formatted)

    def test_archive_is_append_only_and_original_is_immutable(self):
        before = fixture(); family = {"es": deepcopy(before)}
        old = layout.prepare_v2(before, "es", family)
        revised = layout.prepare_v2(edited(old), "es", family, old)
        bad = deepcopy(revised)
        bad["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] += " // forged"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = deepcopy(revised)
        bad["stateLayout"].pop("sourceFormatRevisions")
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = deepcopy(revised)
        bad["pages"][0]["blocks"][1]["answers"] = ["Other"]
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = deepcopy(revised)
        bad["stateLayout"]["sourceFormatRevisions"][0]["id"] = "nonexistent"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_published_course_gate_accepts_only_own_archived_format(self):
        import validar_json_programacion as contract
        source = fixture()
        family = {"es": deepcopy(source)}
        previous = layout.prepare_v2(source, "es", family)
        published = layout.prepare_v2(edited(previous), "es", family, previous)
        path = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "source_format_qa/lesson_source_format_qa.json")
        original = previous["pages"][0]["blocks"][1]
        changed = published["pages"][0]["blocks"][1]
        self.assertTrue(contract.preserves_source_format_revision(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=published))
        self.assertFalse(contract.preserves_source_format_revision(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=published, course_id="programming_practice_tools"))
        self.assertTrue(contract.preserves_previous_exercise(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=published))
        forged = deepcopy(published)
        forged["pages"][0]["blocks"][1]["answers"] = ["different"]
        self.assertFalse(contract.preserves_source_format_revision(
            path, 0, 1, original, forged["pages"][0]["blocks"][1],
            previous_document=previous, current_document=forged))
        forgery = deepcopy(published)
        forgery["stateLayout"]["sourceFormatRevisions"][1]["block"]["code"] = "int other=1;"
        self.assertFalse(contract.preserves_source_format_revision(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=forgery))

    def test_further_formatting_can_reuse_the_same_snapshot(self):
        before = fixture(); family = {"es": deepcopy(before)}
        original = layout.prepare_v2(before, "es", family)
        first = layout.prepare_v2(edited(original), "es", family, original)
        revised = deepcopy(first)
        revised["pages"][0]["blocks"][0]["code"] += "\n\n"
        second = layout.prepare_v2(revised, "es", family, first)
        self.assertEqual(first["stateLayout"]["sourceFormatRevisions"],
                         second["stateLayout"]["sourceFormatRevisions"])
        layout.validate_transition(original, second)
        layout.validate_transition(first, second)

    def test_combined_physical_format_and_explanations_preserve_response_contracts(self):
        import validar_json_programacion as course_validator

        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Guía anterior."
        before["pages"][0]["blocks"][4]["outputCheck"]["cases"][0][
            "explanation"] = "Guía de la primera comprobación."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        edited = deepcopy(published)
        prediction = edited["pages"][0]["blocks"][1]
        prediction["code"] = "int number = 2;\n\nstd::cout << number;"
        prediction["explanation"] = "Guía aclarada con los mismos resultados."
        compiler = edited["pages"][0]["blocks"][4]
        compiler["initialCode"] = "int main() {\n    return 0;\n}"
        compiler["outputCheck"]["cases"][0]["explanation"] = (
            "Guía aclarada sin cambiar el resultado esperado.")
        combined = layout.prepare_v2(edited, "es", {"es": published}, published)
        self.assertEqual(len(combined["stateLayout"]["sourceFormatRevisions"]), 2)
        self.assertEqual(layout.validate(published), layout.validate(combined))
        layout.validate_transition(published, combined)
        self.assertEqual(published["stateLayout"]["origin"],
                         combined["stateLayout"]["origin"])
        self.assertEqual(published["stateLayout"]["current"],
                         combined["stateLayout"]["current"])
        relative = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    "source_format_qa/lesson_source_format_qa.json")
        for index in (1, 4):
            previous = published["pages"][0]["blocks"][index]
            current = combined["pages"][0]["blocks"][index]
            with self.subTest(owner=index):
                self.assertTrue(course_validator.preserves_source_format_revision(
                    relative, 0, index, previous, current,
                    previous_document=published, current_document=combined))
                self.assertTrue(course_validator.preserves_previous_exercise(
                    relative, 0, index, previous, current,
                    previous_document=published, current_document=combined))
        self.assertEqual(prediction["answers"], published["pages"][0]["blocks"][1]["answers"])
        self.assertEqual(compiler["outputCheck"]["cases"][0]["outputs"],
                         published["pages"][0]["blocks"][4]["outputCheck"]["cases"][0]["outputs"])

    def test_combined_format_cannot_hide_a_changed_answer_or_project_file(self):
        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Explicación anterior."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        candidate = deepcopy(published)
        candidate["pages"][0]["blocks"][1]["code"] = "int number = 2;\n\nstd::cout << number;"
        candidate["pages"][0]["blocks"][1]["explanation"] = "Explicación aclarada."
        wrong_answer = deepcopy(candidate)
        wrong_answer["pages"][0]["blocks"][1]["answers"] = ["3"]
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_answer, "es", {"es": published}, published)
        wrong_question = deepcopy(candidate)
        wrong_question["pages"][0]["blocks"][1]["question"] = (
            "Pregunta sustituida sin revisión independiente")
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_question, "es", {"es": published}, published)
        wrong_path = deepcopy(candidate)
        wrong_path["pages"][0]["blocks"][4]["initialCode"] = (
            "int main() {\n    return 0;\n}")
        wrong_path["pages"][0]["blocks"][4]["projectFiles"][0]["path"] = "other.cmake"
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_path, "es", {"es": published}, published)
        wrong_oracle = deepcopy(candidate)
        wrong_oracle["pages"][0]["blocks"][4]["initialCode"] = (
            "int main() {\n    return 0;\n}")
        wrong_oracle["pages"][0]["blocks"][4]["outputCheck"]["cases"][0][
            "outputs"] = ["falsified"]
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_oracle, "es", {"es": published}, published)

    def test_prose_before_spacing_uses_one_authentic_format_archive(self):
        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Descripción anterior."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        prose = deepcopy(published)
        prose["pages"][0]["blocks"][1]["explanation"] = "Descripción revisada."
        prose = layout.prepare_v2(prose, "es", {"es": published}, published)
        later = deepcopy(prose)
        later["pages"][0]["blocks"][1]["code"] = (
            "int number = 2;\n\nstd::cout << number;")
        formatted = layout.prepare_v2(later, "es", {"es": published}, prose)
        layout.validate_transition(published, formatted)
        self.assertEqual(len(formatted["stateLayout"]["sourceFormatRevisions"]), 1)
        self.assertEqual(formatted["stateLayout"]["sourceFormatRevisions"][0]["block"],
                         prose["pages"][0]["blocks"][1])


    def test_real_u10_l4_combined_format_and_authored_guide(self):
        """Authenticate a real published compiler owner without editing its JSON."""
        import hashlib
        import json
        import validar_json_programacion as contract

        root = Path(__file__).resolve().parents[2]
        relative = ("programming/cpp_fundamentals/aleatoriedad_y_reproducibilidad/"
                    "lesson_combinar_ensayos_generados_y_casos_elegidos.json")
        family = {
            locale: json.loads((root / "app/src/main/assets/content" /
                                locale / relative).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        published = family["es"]
        layout.validate(published)
        identity = "block_0004_0007"
        self.assertEqual(published["stateLayout"]["current"][4]["blocks"][7], identity)
        original = published["pages"][4]["blocks"][7]
        self.assertEqual(original["type"], "cpp_compiler")
        self.assertFalse(any(item["id"] == identity for item in
                             published["stateLayout"].get("sourceFormatRevisions", [])))
        before_code = "    int cantidad=0;\\n    for(int dato:datos)"
        after_code = "    int cantidad=0;\\n\\n    for(int dato:datos)"
        self.assertEqual(original["initialCode"].count(before_code), 1)

        updated = deepcopy(published)
        editor = updated["pages"][4]["blocks"][7]
        editor["initialCode"] = editor["initialCode"].replace(
            before_code, after_code, 1)
        case = editor["outputCheck"]["cases"][0]
        case["explanation"] += (
            "\\nRevisa que la igualdad no se cuente con la condición estricta.")
        marker = next(field for field in updated["textSemantics"]["fields"]
                      if field["block"] == identity and
                      field["path"] == "/outputCheck/cases/0/explanation")
        # Append after the existing 17 genuine semantic spans; no highlight
        # is shifted or silently given a new meaning.
        self.assertEqual(len(marker["spans"]), 17)
        marker["sourceSha256"] = hashlib.sha256(
            case["explanation"].encode("utf-8")).hexdigest()
        self.assertNotEqual(original["initialCode"], editor["initialCode"])

        reviewed = layout.prepare_v2(updated, "es", family, published)
        layout.validate_transition(published, reviewed)
        self.assertEqual(layout.validate(published), layout.validate(reviewed))
        self.assertEqual(reviewed["stateLayout"]["origin"],
                         published["stateLayout"]["origin"])
        self.assertEqual(reviewed["stateLayout"]["family"],
                         published["stateLayout"]["family"])
        self.assertEqual(reviewed["stateLayout"]["current"],
                         published["stateLayout"]["current"])
        archives = [item for item in reviewed["stateLayout"]["sourceFormatRevisions"]
                    if item["id"] == identity]
        self.assertEqual(len(archives), 1)
        self.assertEqual(archives[0]["block"], original)
        self.assertEqual(reviewed["pages"][4]["blocks"][7]["outputCheck"]
                         ["cases"][0]["outputs"], original["outputCheck"]["cases"][0]["outputs"])
        full_path = "app/src/main/assets/content/es/" + relative
        self.assertTrue(contract.preserves_source_format_revision(
            full_path, 4, 7, original, reviewed["pages"][4]["blocks"][7],
            previous_document=published, current_document=reviewed))
        self.assertTrue(contract.preserves_previous_exercise(
            full_path, 4, 7, original, reviewed["pages"][4]["blocks"][7],
            previous_document=published, current_document=reviewed))

        incorrect = deepcopy(reviewed)
        incorrect["pages"][4]["blocks"][7]["initialCode"] = (
            editor["initialCode"].replace("dato>minimo", "dato>=minimo", 1))
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, incorrect)
        incorrect = deepcopy(reviewed)
        descriptor = next(field for field in incorrect["textSemantics"]["fields"]
                          if field["block"] == identity and
                          field["path"] == "/outputCheck/cases/0/explanation")
        descriptor["sourceSha256"] = next(
            field["sourceSha256"] for field in published["textSemantics"]["fields"]
            if field["block"] == identity and
            field["path"] == "/outputCheck/cases/0/explanation")
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, incorrect)


if __name__ == "__main__":
    unittest.main()

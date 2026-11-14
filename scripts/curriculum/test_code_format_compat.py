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


    def test_real_u7_l3_question_and_source_spacing_share_published_owner(self):
        """The authored question, explanation and source each keep their own proof."""
        import hashlib
        import json
        import validar_json_programacion as contract

        root = Path(__file__).resolve().parents[2]
        relative = ("programming/cpp_fundamentals/errores_y_construccion_gradual/"
                    "lesson_validar_antes_y_elegir_reporte.json")
        family = {
            locale: json.loads((root / "app/src/main/assets/content" /
                                locale / relative).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        published = family["es"]
        layout.validate(published)
        identity, p, b = "block_0000_0003", 0, 3
        original = published["pages"][p]["blocks"][b]
        self.assertEqual(published["stateLayout"]["current"][p]["blocks"][b], identity)
        self.assertEqual(original["type"], "output_prediction")
        self.assertNotIn(identity, [entry["id"] for entry in
                                   published["stateLayout"].get("sourceFormatRevisions", [])])

        draft = deepcopy(published)
        block = draft["pages"][p]["blocks"][b]
        old_code = "    int piezas{10};\n    if (piezas < 0 || piezas > 10)"
        new_code = "    int piezas{10};\n\n    if (piezas < 0 || piezas > 10)"
        self.assertEqual(block["code"].count(old_code), 1)
        block["code"] = block["code"].replace(old_code, new_code, 1)
        block["question"] += " Examina si el extremo superior está incluido."
        block["explanation"] += " El límite superior también forma parte del intervalo."
        for field in ("/question", "/explanation"):
            marker = next(item for item in draft["textSemantics"]["fields"]
                          if item["block"] == identity and item["path"] == field)
            marker["sourceSha256"] = hashlib.sha256(
                block[field[1:]].encode("utf-8")).hexdigest()
        reviewed = layout.prepare_v2(draft, "es", family, published)
        layout.validate_transition(published, reviewed)
        self.assertEqual(layout.validate(published), layout.validate(reviewed))
        self.assertEqual(published["stateLayout"]["origin"], reviewed["stateLayout"]["origin"])
        self.assertEqual(published["stateLayout"]["current"], reviewed["stateLayout"]["current"])
        self.assertEqual(published["stateLayout"]["family"], reviewed["stateLayout"]["family"])
        self.assertEqual(len(reviewed["stateLayout"]["sourceFormatRevisions"]),
                         len(published["stateLayout"].get("sourceFormatRevisions", [])) + 1)
        archive = next(item for item in reviewed["stateLayout"]["sourceFormatRevisions"]
                       if item["id"] == identity)
        self.assertEqual(archive["block"], original)
        self.assertEqual(original["answers"], block["answers"])
        self.assertEqual(fmt.source_projection(block, original)["code"], original["code"])
        full_path = "app/src/main/assets/content/es/" + relative
        self.assertTrue(contract.preserves_previous_exercise(
            full_path, p, b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))
        self.assertTrue(contract.preserves_source_format_revision(
            full_path, p, b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))

        stale = deepcopy(draft)
        old_question_marker = next(item for item in published["textSemantics"]["fields"]
                                   if item["block"] == identity and item["path"] == "/question")
        next(item for item in stale["textSemantics"]["fields"]
             if item["block"] == identity and item["path"] == "/question")[
                "sourceSha256"] = old_question_marker["sourceSha256"]
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(stale, "es", family, published)

        forged = deepcopy(reviewed)
        forged["pages"][p]["blocks"][b]["answers"] = ["Falso"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, forged)

        changed_code = deepcopy(reviewed)
        changed_code["pages"][p]["blocks"][b]["code"] = block["code"].replace(
            "piezas > 10", "piezas >= 10")
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, changed_code)

    def test_combined_question_cannot_repaint_unchanged_explanation(self):
        import hashlib
        import json

        root = Path(__file__).resolve().parents[2]
        relative = ("programming/cpp_fundamentals/errores_y_construccion_gradual/"
                    "lesson_validar_antes_y_elegir_reporte.json")
        family = {
            locale: json.loads((root / "app/src/main/assets/content" /
                                locale / relative).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        published = family["es"]
        identity, p, b = "block_0000_0003", 0, 3
        draft = deepcopy(published)
        block = draft["pages"][p]["blocks"][b]
        block["code"] = block["code"].replace(
            "    int piezas{10};\n    if (", "    int piezas{10};\n\n    if (", 1)
        block["question"] += " Contrasta el extremo superior."
        marker = next(item for item in draft["textSemantics"]["fields"]
                      if item["block"] == identity and item["path"] == "/question")
        marker["sourceSha256"] = hashlib.sha256(block["question"].encode()).hexdigest()
        accepted = layout.prepare_v2(draft, "es", family, published)
        layout.validate_transition(published, accepted)

        forged = deepcopy(draft)
        explanation = next(item for item in forged["textSemantics"]["fields"]
                           if item["block"] == identity and item["path"] == "/explanation")
        self.assertTrue(explanation["spans"])
        explanation["spans"][0]["role"] = "prose"
        # The draft's source-format change is not yet archived, so validating
        # it before prepare_v2 would reject the missing editorial history.
        with self.assertRaisesRegex(layout.LayoutError, "repainted an unchanged public field"):
            layout.prepare_v2(forged, "es", family, published)

    def test_real_u7_combined_format_fixtures_keep_authentic_history(self):
        """The exported Win32 input is a read-only copy of the real U7L3."""
        from code_format_fixture_source import real_question_spacing_fixtures

        documents = real_question_spacing_fixtures()
        before = documents["u7-question.before"]
        after = documents["u7-question.after"]
        identity = before["stateLayout"]["current"][0]["blocks"][3]
        self.assertEqual(identity, "block_0000_0003")
        self.assertEqual(before["pages"][0]["blocks"][3]["answers"],
                         after["pages"][0]["blocks"][3]["answers"])
        self.assertNotEqual(before["pages"][0]["blocks"][3]["question"],
                            after["pages"][0]["blocks"][3]["question"])
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        for forged in ("u7-question.changed-answer",
                       "u7-question.stale-question", "u7-question.forged-source"):
            with self.subTest(forged=forged), self.assertRaises(layout.LayoutError):
                layout.validate(documents[forged])


    def test_real_u9_l3_three_source_and_explanation_revisions(self):
        """The reported enum-class slots can change spacing with authored prose.

        Nothing is written back to Work's lessons. All three published blocks
        keep their answers, owners, and authored spans in memory.
        """
        import hashlib
        import json
        import validar_json_programacion as contract

        root = Path(__file__).resolve().parents[2]
        relative = ("programming/cpp_fundamentals/datos_propios_y_estado_valido/"
                    "lesson_nombrar_alternativas_con_enum_class.json")
        family = {
            locale: json.loads((root / "app/src/main/assets/content" /
                                locale / relative).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        before = family["es"]
        layout.validate(before)
        positions = ((1, 2, "block_0001_0002"),
                     (2, 2, "block_0002_0002"),
                     (4, 3, "block_0004_0003"))
        original_archives = len(before["stateLayout"].get("sourceFormatRevisions", []))
        changed = deepcopy(before)
        original_sources = {}
        for page, index, identity in positions:
            self.assertEqual(before["stateLayout"]["current"][page]["blocks"][index], identity)
            initial = before["pages"][page]["blocks"][index]
            updated = changed["pages"][page]["blocks"][index]
            self.assertIn(initial["type"], ("fill_code", "output_prediction"))
            before_code = "    std::cout"
            self.assertEqual(initial["code"].count(before_code), 1)
            updated["code"] = initial["code"].replace(
                before_code, "\n    std::cout", 1)
            updated["explanation"] += (
                " El valor conserva el tipo Estado hasta que el ejemplo "
                "realiza la operación indicada.")
            marker = next(item for item in changed["textSemantics"]["fields"]
                          if item["block"] == identity and item["path"] == "/explanation")
            self.assertTrue(marker["spans"])
            marker["sourceSha256"] = hashlib.sha256(
                updated["explanation"].encode("utf-8")).hexdigest()
            original_sources[identity] = initial["code"]
            self.assertTrue(fmt.require_source_lexical(initial, updated))
            self.assertEqual(initial["answers"], updated["answers"])

        reviewed = layout.prepare_v2(changed, "es", family, before)
        layout.validate_transition(before, reviewed)
        self.assertEqual(layout.validate(before), layout.validate(reviewed))
        self.assertEqual(before["stateLayout"]["origin"], reviewed["stateLayout"]["origin"])
        self.assertEqual(before["stateLayout"]["family"], reviewed["stateLayout"]["family"])
        self.assertEqual(before["stateLayout"]["current"], reviewed["stateLayout"]["current"])
        new_archives = reviewed["stateLayout"]["sourceFormatRevisions"][original_archives:]
        self.assertEqual([identity for _, _, identity in positions],
                         [item["id"] for item in new_archives])
        path = "app/src/main/assets/content/es/" + relative
        for page, index, identity in positions:
            previous_block = before["pages"][page]["blocks"][index]
            revised_block = reviewed["pages"][page]["blocks"][index]
            archive = next(item for item in new_archives if item["id"] == identity)
            self.assertEqual(archive["block"], previous_block)
            self.assertEqual(previous_block["code"], original_sources[identity])
            self.assertEqual(previous_block["answers"], revised_block["answers"])
            self.assertTrue(contract.preserves_previous_exercise(
                path, page, index, previous_block, revised_block,
                previous_document=before, current_document=reviewed))
            self.assertTrue(contract.preserves_source_format_revision(
                path, page, index, previous_block, revised_block,
                previous_document=before, current_document=reviewed))
        tampered = deepcopy(reviewed)
        tampered["pages"][2]["blocks"][2]["answers"] = ["4"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, tampered)
        tampered = deepcopy(reviewed)
        tampered["pages"][4]["blocks"][3]["code"] = (
            tampered["pages"][4]["blocks"][3]["code"].replace(
                "Estado::cancelado", "Estado::preparado", 1))
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, tampered)
        for language in ("es", "en", "fr"):
            self.assertEqual(json.loads((root / "app/src/main/assets/content" /
                                         language / relative).read_text(encoding="utf-8")),
                             family[language])

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
        before_code = "    int cantidad=0;\n    for(int dato:datos)"
        after_code = "    int cantidad=0;\n\n    for(int dato:datos)"
        self.assertEqual(original["initialCode"].count(before_code), 1)

        updated = deepcopy(published)
        editor = updated["pages"][4]["blocks"][7]
        editor["initialCode"] = editor["initialCode"].replace(
            before_code, after_code, 1)
        case = editor["outputCheck"]["cases"][0]
        case["explanation"] += (
            "\nRevisa que la igualdad no se cuente con la condición estricta.")
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



    def test_real_u11_l11_combined_format_and_review_feedback(self):
        """A published prediction keeps its answer and original guide spans."""
        import hashlib
        import json
        import validar_json_programacion as contract

        root = Path(__file__).resolve().parents[2]
        relative = ("programming/cpp_fundamentals/texto_interpretacion_y_flujos/"
                    "lesson_una_gramatica_para_sumar_y_restar.json")
        family = {
            locale: json.loads((root / "app/src/main/assets/content" /
                                locale / relative).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        published = family["es"]
        layout.validate(published)
        identity = "block_0002_0001"
        self.assertEqual(published["stateLayout"]["current"][2]["blocks"][1], identity)
        original = published["pages"][2]["blocks"][1]
        self.assertEqual(original["type"], "output_prediction")
        self.assertFalse(any(item["id"] == identity for item in
                             published["stateLayout"].get("sourceFormatRevisions", [])))
        before_code = "    const int resultado = leer_expresion(lector);\n    const Token fin = lector.leer();"
        after_code = "    const int resultado = leer_expresion(lector);\n\n    const Token fin = lector.leer();"
        self.assertEqual(original["code"].count(before_code), 1)

        updated = deepcopy(published)
        puzzle = updated["pages"][2]["blocks"][1]
        puzzle["code"] = puzzle["code"].replace(before_code, after_code, 1)
        puzzle["reviewFeedback"] += (
            "\nEl terminador recuperado pertenece a la misma operación.")
        marker = next(field for field in updated["textSemantics"]["fields"]
                      if field["block"] == identity and
                      field["path"] == "/reviewFeedback")
        self.assertEqual(len(marker["spans"]), 2)
        marker["sourceSha256"] = hashlib.sha256(
            puzzle["reviewFeedback"].encode("utf-8")).hexdigest()
        reviewed = layout.prepare_v2(updated, "es", family, published)
        layout.validate_transition(published, reviewed)
        self.assertEqual(layout.validate(published), layout.validate(reviewed))
        self.assertEqual(reviewed["stateLayout"]["origin"],
                         published["stateLayout"]["origin"])
        self.assertEqual(reviewed["stateLayout"]["current"],
                         published["stateLayout"]["current"])
        self.assertEqual(reviewed["stateLayout"]["family"],
                         published["stateLayout"]["family"])
        archives = [item for item in reviewed["stateLayout"]["sourceFormatRevisions"]
                    if item["id"] == identity]
        self.assertEqual(len(archives), 1)
        self.assertEqual(archives[0]["block"], original)
        self.assertEqual(puzzle["answers"], original["answers"])
        revised_marker = next(field for field in reviewed["textSemantics"]["fields"]
                              if field["block"] == identity and
                              field["path"] == "/reviewFeedback")
        published_marker = next(field for field in published["textSemantics"]["fields"]
                                if field["block"] == identity and
                                field["path"] == "/reviewFeedback")
        self.assertEqual(revised_marker["spans"], published_marker["spans"])
        full_path = "app/src/main/assets/content/es/" + relative
        self.assertTrue(contract.preserves_source_format_revision(
            full_path, 2, 1, original, reviewed["pages"][2]["blocks"][1],
            previous_document=published, current_document=reviewed))
        self.assertTrue(contract.preserves_previous_exercise(
            full_path, 2, 1, original, reviewed["pages"][2]["blocks"][1],
            previous_document=published, current_document=reviewed))

        forged = deepcopy(reviewed)
        forged["pages"][2]["blocks"][1]["answers"] = ["Resultado falsificado"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, forged)
        stale = deepcopy(reviewed)
        marker = next(field for field in stale["textSemantics"]["fields"]
                      if field["block"] == identity and
                      field["path"] == "/reviewFeedback")
        marker["sourceSha256"] = published_marker["sourceSha256"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, stale)



    def test_real_u11_compiler_references_allow_later_physical_spacing(self):
        """Nine real expanded references, without replacing their paired compiler."""
        import json
        import validar_json_programacion as contract

        names = {
            "tokens_con_categoria_y_valor": ("block_0007_0001",),
            "numeros_de_varios_digitos_y_limites": ("block_0008_0001",),
            "una_gramatica_para_sumar_y_restar": ("block_0008_0001",),
            "terminos_prioridad_y_division_segura": ("block_0008_0001",),
            "signos_unarios_parentesis_y_progreso_recursivo": ("block_0009_0001",),
            "diagnosticar_y_recuperar_hasta_fin_de_operacion": ("block_0008_0001",),
            "construir_un_interprete_con_historial_y_pruebas":
                ("block_0009_0001", "block_0010_0001"),
            "resto_y_asociacion_en_el_interprete": ("block_0006_0001",),
        }
        root = Path(__file__).resolve().parents[2] / "app/src/main/assets/content"
        before_code = "    return 0;\n}\n"
        after_code = "\n    return 0;\n}\n"
        for name, identities in names.items():
            with self.subTest(lesson=name):
                relative = ("programming/cpp_fundamentals/texto_interpretacion_y_flujos/"
                            f"lesson_{name}.json")
                paths = {lang: root / lang / relative for lang in ("es", "en", "fr")}
                original_bytes = paths["es"].read_bytes()
                family = {lang: json.loads(path.read_text(encoding="utf-8"))
                          for lang, path in paths.items()}
                published = family["es"]
                layout.validate(published)
                old_meta = published["stateLayout"]
                historical_refs = {
                    reference["id"] for owner in old_meta.get("compilerRevisions", [])
                    for reference in owner.get("references", [])
                }
                updated = deepcopy(published)
                positions = {}
                for p, record in enumerate(old_meta["current"]):
                    for b, identity in enumerate(record["blocks"]):
                        if identity in identities:
                            positions[identity] = (p, b)
                self.assertEqual(set(positions), set(identities))
                self.assertTrue(set(identities) <= historical_refs)
                prior_archives = {item["id"] for item in
                                  old_meta.get("sourceFormatRevisions", [])}
                self.assertTrue(set(identities).isdisjoint(prior_archives))
                for identity in identities:
                    p, b = positions[identity]
                    original = published["pages"][p]["blocks"][b]
                    candidate = updated["pages"][p]["blocks"][b]
                    self.assertEqual(original["type"], "code")
                    self.assertEqual(original["code"].count(before_code), 1)
                    candidate["code"] = candidate["code"].replace(
                        before_code, after_code, 1)
                    self.assertTrue(fmt.require_source_format(original, candidate))
                    self.assertEqual(original, fmt.source_projection(candidate, original))

                revised = layout.prepare_v2(updated, "es", family, published)
                self.assertEqual(layout.validate(published), layout.validate(revised))
                layout.validate_transition(published, revised)
                for key in ("origin", "current", "family", "additions", "compilerRevisions",
                            "choiceRevisions", "retired", "retirementHistory"):
                    self.assertEqual(old_meta.get(key), revised["stateLayout"].get(key), key)
                self.assertEqual(old_meta["editorial"],
                                 revised["stateLayout"]["editorial"][:len(old_meta["editorial"])])
                self.assertEqual(paths["es"].read_bytes(), original_bytes)
                self.assertEqual(
                    [archive["id"] for archive in
                     revised["stateLayout"]["sourceFormatRevisions"][-len(identities):]],
                    list(identities))
                for identity in identities:
                    p, b = positions[identity]
                    original = published["pages"][p]["blocks"][b]
                    changed = revised["pages"][p]["blocks"][b]
                    archived = next(
                        item for item in revised["stateLayout"]["sourceFormatRevisions"]
                        if item["id"] == identity)
                    self.assertEqual(archived["block"], original)
                    self.assertEqual(changed["code"], updated["pages"][p]["blocks"][b]["code"])
                    self.assertTrue(contract.preserves_source_format_revision(
                        "app/src/main/assets/content/es/" + relative, p, b,
                        original, changed, previous_document=published,
                        current_document=revised))
                    self.assertTrue(contract.preserves_previous_exercise(
                        "app/src/main/assets/content/es/" + relative, p, b,
                        original, changed, previous_document=published,
                        current_document=revised))

                # Rehashing a different program is not a whitespace revision,
                # and rewriting the archived complete predecessor is forbidden.
                identity = identities[0]
                p, b = positions[identity]
                forged = deepcopy(revised)
                forged_block = forged["pages"][p]["blocks"][b]
                forged_block["code"] = forged_block["code"].replace(
                    "    return 0;\n}\n", "    return 1;\n}\n", 1)
                proof = next(item for item in reversed(forged["stateLayout"]["editorial"])
                             if item["id"] == identity)
                proof["afterSha256"] = layout.fingerprint(forged_block)
                with self.assertRaises(layout.LayoutError):
                    layout.validate(forged)
                altered_archive = deepcopy(revised)
                snapshot = next(item for item in
                                altered_archive["stateLayout"]["sourceFormatRevisions"]
                                if item["id"] == identity)
                snapshot["block"]["code"] += "\n// forged"
                with self.assertRaises(layout.LayoutError):
                    layout.validate(altered_archive)



    def test_real_u12_identical_references_preserve_distinct_owners_after_formatting(self):
        """Signed physical formatting is not a transfer to a matching code owner."""
        import json
        import validar_json_programacion as validator

        root = Path(__file__).resolve().parents[2]
        relative = ("programming/cpp_fundamentals/archivos_y_programa_completo/"
                    "lesson_cerrar_reemplazar_y_anadir_datos.json")
        family = {
            language: json.loads((root / "app/src/main/assets/content" /
                                  language / relative).read_text(encoding="utf-8"))
            for language in ("es", "en", "fr")
        }
        published = family["es"]
        layout.validate(published)
        original = published["stateLayout"]
        same_source_owner = "block_0004_0001"
        repeated = ("block_0006_0001", "block_0008_0001")
        self.assertEqual(original["current"][4]["blocks"][1], same_source_owner)
        self.assertEqual(original["current"][6]["blocks"][1], repeated[0])
        self.assertEqual(original["current"][8]["blocks"][1], repeated[1])
        source = published["pages"][4]["blocks"][1]["code"]
        self.assertTrue(any(item["id"] == same_source_owner
                            for item in original.get("sourceFormatRevisions", [])))
        self.assertFalse(any(item["id"] in repeated
                             for item in original.get("sourceFormatRevisions", [])))
        updated = deepcopy(published)
        for page, owner in ((6, repeated[0]), (8, repeated[1])):
            previous = published["pages"][page]["blocks"][1]
            changed = updated["pages"][page]["blocks"][1]
            self.assertEqual(previous["type"], "code")
            self.assertEqual(changed["code"].count("    return 0;\n}\n"), 1)
            self.assertNotEqual(previous["code"], source)
            changed["code"] = source
            self.assertTrue(fmt.require_source_format(previous, changed))
            self.assertEqual(previous, fmt.source_projection(changed, previous))
        revised = layout.prepare_v2(updated, "es", family, published)
        layout.validate_transition(published, revised)
        self.assertEqual(layout.validate(revised), layout.validate(published))
        for key in ("origin", "current", "family", "compilerRevisions", "additions"):
            self.assertEqual(original.get(key), revised["stateLayout"].get(key), key)
        self.assertEqual(original["editorial"],
                         revised["stateLayout"]["editorial"][:len(original["editorial"])])
        for page, owner in ((6, repeated[0]), (8, repeated[1])):
            before_block = published["pages"][page]["blocks"][1]
            after_block = revised["pages"][page]["blocks"][1]
            self.assertEqual(after_block["code"], source)
            snapshot = next(entry for entry in
                            revised["stateLayout"]["sourceFormatRevisions"]
                            if entry["id"] == owner)
            self.assertEqual(snapshot["block"], before_block)
            self.assertTrue(validator.preserves_source_format_revision(
                "app/src/main/assets/content/es/" + relative, page, 1,
                before_block, after_block, previous_document=published,
                current_document=revised))
            self.assertTrue(validator.preserves_previous_exercise(
                "app/src/main/assets/content/es/" + relative, page, 1,
                before_block, after_block, previous_document=published,
                current_document=revised))
        forged_source = deepcopy(revised)
        block = forged_source["pages"][6]["blocks"][1]
        block["code"] = block["code"].replace("    return 0;\n}\n",
                                              "    return 1;\n}\n", 1)
        latest = next(edge for edge in reversed(forged_source["stateLayout"]["editorial"])
                      if edge["id"] == repeated[0])
        latest["afterSha256"] = layout.fingerprint(block)
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged_source)
        forged_archive = deepcopy(revised)
        next(entry for entry in forged_archive["stateLayout"]["sourceFormatRevisions"]
             if entry["id"] == repeated[0])["block"]["code"] += "\n// forged"
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged_archive)
        borrowed = deepcopy(revised)
        borrowed["stateLayout"]["sourceFormatRevisions"] = [
            entry for entry in borrowed["stateLayout"]["sourceFormatRevisions"]
            if entry["id"] != repeated[0]]
        with self.assertRaises(layout.LayoutError):
            layout.validate(borrowed)



if __name__ == "__main__":
    unittest.main()

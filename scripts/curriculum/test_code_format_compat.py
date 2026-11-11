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

if __name__ == "__main__":
    unittest.main()

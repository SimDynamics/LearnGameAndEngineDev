"""Regresiones del auditor de TODAS las fuentes; este test no autoriza cambios."""
import json
from pathlib import Path
import tempfile
import unittest

import auditar_legibilidad_codigo as audit


FICHAS = '''#include <iostream>

int main() {
    const int fichas = 18;
    int capacidad = 5;
    const bool acepta_sobrantes = true;
    if ((fichas < 0) || (fichas > 30)) {
        std::cout << "Datos invalidos";
    } else {
        const int grupos = fichas / capacidad;
        const int sobrantes = fichas % capacidad;
        if (sobrantes == 0) {
            std::cout << grupos;
        }
    }
    return 0;
}
'''


class AuditCodeReadabilityTest(unittest.TestCase):
    def test_detects_semantic_boundaries_without_editing_fichas(self):
        original = FICHAS[:]
        found = audit.suggestions(FICHAS, "cpp")
        self.assertEqual(FICHAS, original)
        self.assertIn("preparacion_y_decision", [x["reason"] for x in found])
        self.assertIn("decision_y_retorno", [x["reason"] for x in found])
        self.assertEqual(len(found), len({(x["beforeLine"], x["afterLine"]) for x in found}))

    def test_preserves_escape_literals_and_existing_blank_lines(self):
        source = '#include <iostream>\n\nint main() {\n    std::cout << "\\n";\n    return 0;\n}\n'
        self.assertEqual([], audit.suggestions(source, "cpp"))
        self.assertIn('"\\n', source)
        self.assertNotEqual(source,source.replace('\\n','\n'))

    def test_cmake_only_suggests_config_and_target_groups(self):
        code = ('cmake_minimum_required(VERSION 3.20)\nproject(Prueba LANGUAGES CXX)\n'
                'add_library(faltantes STATIC faltantes.cpp)\n'
                'target_compile_features(faltantes PRIVATE cxx_std_20)\n'
                'target_include_directories(faltantes PUBLIC include)\n')
        reasons = [x["reason"] for x in audit.suggestions(code, "cmake")]
        self.assertEqual(["configuracion_y_objetivos","objetivos_y_propiedades"], reasons)

    def test_scans_three_locales_all_courses_and_project_sources(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for locale,area in (("es","programming"),("en","programming"),
                                ("fr","math_physics")):
                path = root/locale/area/"curso"/"unidad"/"lesson_a.json"
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_text(json.dumps({"id":"x","pages":[{"blocks":[
                    {"type":"code","code":FICHAS},
                    {"type":"cpp_compiler","projectFiles":[{"path":"main.cpp","initialCode":FICHAS},
                        {"path":"CMakeLists.txt","initialCode":"project(ABC)\nadd_library(a STATIC a.cpp)\ntarget_compile_features(a PRIVATE cxx_std_20)\nadd_executable(b b.cpp)\n"}]}]}],
                    "stateLayout":{"current":[{"blocks":["code-0","compiler-1"]}]}}),encoding="utf-8")
            summary = audit.audit(root)
            self.assertEqual(3, summary["counts"]["lessons"])
            self.assertEqual(9,summary["counts"]["sources"])
            self.assertEqual(9,len(summary["findings"]))
            self.assertEqual({"es","en","fr"},set(x["locale"] for x in summary["findings"]))
            self.assertTrue(all(x["owner"] for x in summary["findings"]))
            self.assertFalse(summary["changesMade"])

    def test_order_code_is_inspected_but_not_rewritten(self):
        order={"type":"order_code","fragments":["return 0;","#include <iostream>\\nint main() {",
                       "    int a{1};\\n    int b{2};\\n    if (a < b) {","        return a;\\n    }"],
               "correctOrder":[1,2,3,0]}
        original=json.dumps(order,sort_keys=True)
        data=list(audit.source_blocks(order))
        self.assertEqual(5,len(data))
        self.assertEqual(["fragments/0", "fragments/1", "fragments/2",
                          "fragments/3", "fragments/correctOrder"], [v[0] for v in data])
        self.assertEqual("composition",data[-1][2])
        self.assertEqual(original,json.dumps(order,sort_keys=True))

    def test_do_not_recommend_blank_card_between_else_if_and_else(self):
        code='int main() {\n    if (a) {\n        return 1;\n    } else if (b) {\n        return 2;\n    } else {\n        return 3;\n    }\n    return 0;\n}'
        found=audit.suggestions(code,"cpp")
        self.assertNotIn('else', ' '.join(x['after'] for x in found))


    def test_individual_drag_choices_and_order_fragments_are_inventoried(self):
        drag = {"type": "drag_fill_code", "template": "int main(){\\n[]\\nreturn 0;\\n}",
                "choices": ["int n=2;", "std::cout << n;"], "correctOrder": [0, 1]}
        actual = list(audit.source_blocks(drag))
        self.assertEqual(["template", "choices/0", "choices/1"], [item[0] for item in actual])
        self.assertTrue(all(item[2] == "source" for item in actual))
        self.assertEqual(0, len(audit.suggestions(actual[1][1], "cpp")))

    def test_safety_report_marks_line_splices_raw_strings_and_cmake_brackets(self):
        cpp = 'const char* text=R"(line\\nreturn 0;)";\\n'
        self.assertIn("raw_string_literal", audit.manual_review_reasons(cpp, "cpp"))
        self.assertIn("physical_line_splice",
                      audit.manual_review_reasons("#define X " + chr(92) + "\n 1", "cpp"))
        self.assertIn("multiline_comment", audit.manual_review_reasons("/* comment */", "cpp"))
        self.assertIn("bracket_argument_or_comment",
                      audit.manual_review_reasons("message([=[value]=])", "cmake"))
        self.assertIn("unsupported_language", audit.manual_review_reasons("print('x')", "other:python"))

    def test_unknown_language_is_counted_without_misclassifying_cpp_or_cmake(self):
        python_code = {"type": "code", "language": "python",
                       "code": "def f():\\n    if x:\\n        return 0\\n"}
        self.assertEqual("other:python", audit.code_language("code", python_code))
        self.assertEqual([], audit.suggestions(python_code["code"], "other:python"))
        self.assertEqual("cmake", audit.code_language(
            "projectFiles/0:CMakeLists.txt/initialCode", {"type": "cpp_compiler"}))
        self.assertEqual("cpp", audit.code_language(
            "projectFiles/1:source.cpp/initialCode", {"type": "cpp_compiler"}))


if __name__ == '__main__':
    unittest.main()

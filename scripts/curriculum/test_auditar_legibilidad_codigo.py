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
        self.assertEqual(1,len(data))
        self.assertEqual("composition",data[0][2])
        self.assertEqual(original,json.dumps(order,sort_keys=True))

    def test_do_not_recommend_blank_card_between_else_if_and_else(self):
        code='int main() {\n    if (a) {\n        return 1;\n    } else if (b) {\n        return 2;\n    } else {\n        return 3;\n    }\n    return 0;\n}'
        found=audit.suggestions(code,"cpp")
        self.assertNotIn('else', ' '.join(x['after'] for x in found))


if __name__ == '__main__':
    unittest.main()

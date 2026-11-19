"""Regress a declared C++ identifier rename without touching curricular assets."""
from __future__ import annotations

from copy import deepcopy
import unittest

import code_format_compat as source_format
import identifier_rename_compat as renaming


ORIGINAL = {
    "type": "cpp_compiler", "language": "cpp",
    "question": "Keep the previously published description and rubric.",
    "initialCode": (
        '#include <iostream>\n'
        'int main() {\n'
        '    int maxima = 4; // maxima stays literal in the comment\n'
        '    auto label = R"tag(maxima remains in this raw literal)tag";\n'
        '    std::cout << "Maxima:" << maxima << label;\n'
        '}\n'
    ),
    "projectFiles": [
        {"path": "src/words.cpp", "initialCode": (
            'int cuenta(int mayores) {\n'
            '    return mayores;\n'
            '}\n'
        )},
        {"path": "CMakeLists.txt", "initialCode": (
            'cmake_minimum_required(VERSION 3.25)\n'
            'project(unchanged)\n'
        )},
    ],
    "outputCheck": {"cases": [{"outputs": ["Maxima:4"], "standardInput": ""}]},
}


def proposal():
    updated = deepcopy(ORIGINAL)
    updated["initialCode"] = (
        updated["initialCode"].replace("int maxima = 4;", "int frecuencia_maxima_encontrada = 4;")
        .replace("<< maxima <<", "<< frecuencia_maxima_encontrada <<")
    )
    updated["projectFiles"][0]["initialCode"] = (
        updated["projectFiles"][0]["initialCode"].replace(
            "int mayores)", "int valores_mas_frecuentes)")
        .replace("return mayores;", "return valores_mas_frecuentes;")
    )
    return updated


RENAMES = {
    "initialCode": {"maxima": "frecuencia_maxima_encontrada"},
    "projectFiles/0/initialCode": {"mayores": "valores_mas_frecuentes"},
}
COUNTS = {
    "initialCode": {"maxima": 2},
    "projectFiles/0/initialCode": {"mayores": 2},
}


class DeclaredIdentifierRenames(unittest.TestCase):
    def verify(self, updated, names=RENAMES, counts=COUNTS):
        return renaming.require_declared_cpp_renames(
            ORIGINAL, updated, names, counts)

    def test_authored_cpp_sources_preserve_all_other_tokens_and_outputs(self):
        updated = proposal()
        self.assertEqual(self.verify(updated), COUNTS)
        self.assertEqual(updated["outputCheck"], ORIGINAL["outputCheck"])
        self.assertEqual(updated["projectFiles"][1], ORIGINAL["projectFiles"][1])
        self.assertEqual(updated["question"], ORIGINAL["question"])
        self.assertIn("// maxima stays literal", updated["initialCode"])
        self.assertIn("R\"tag(maxima remains", updated["initialCode"])
        self.assertIn('"Maxima:"', updated["initialCode"])
        self.assertEqual(ORIGINAL["initialCode"].count("\n"),
                         updated["initialCode"].count("\n"))
        with self.assertRaises(source_format.SourceFormatError):
            source_format.require_source_format(ORIGINAL, updated)

    def test_answer_or_another_project_file_is_not_part_of_a_rename(self):
        for mutation in (
            lambda d: d["outputCheck"]["cases"][0]["outputs"].append("invented"),
            lambda d: d["projectFiles"][1].update({"initialCode": "project(changed)\n"}),
            lambda d: d.update({"question": "New question without editorial proof"}),
        ):
            with self.subTest(change=mutation):
                updated = proposal()
                mutation(updated)
                with self.assertRaises(renaming.IdentifierRenameError):
                    self.verify(updated)

    def test_nonidentifier_changes_cannot_hide_inside_rename(self):
        samples = (
            ('"Maxima:"', '"Frecuencia máxima:"'),
            ("// maxima stays", "// frecuencia_maxima_encontrada stays"),
            ("int frecuencia_maxima_encontrada = 4;",
             "int frecuencia_maxima_encontrada = 5;"),
            ("<< frecuencia_maxima_encontrada <<",
             "<= frecuencia_maxima_encontrada <<"),
            ('R"tag(maxima remains', 'R"tag(new remains'),
        )
        for old, new in samples:
            with self.subTest(change=new):
                updated = proposal()
                self.assertIn(old, updated["initialCode"])
                updated["initialCode"] = updated["initialCode"].replace(old, new, 1)
                with self.assertRaises(renaming.IdentifierRenameError):
                    self.verify(updated)

    def test_partial_rename_and_misdeclared_owners_are_rejected(self):
        updated = proposal()
        for names, counts in (
            (RENAMES, {"initialCode": {"maxima": 1},
                       "projectFiles/0/initialCode": {"mayores": 2}}),
            ({"initialCode": RENAMES["initialCode"]}, COUNTS),
            ({"code": {"maxima": "frecuencia_maxima_encontrada"}}, {"code": {"maxima": 2}}),
            ({"initialCode": {"maxima": "mayores"},
              "projectFiles/0/initialCode": RENAMES["projectFiles/0/initialCode"]}, COUNTS),
        ):
            with self.subTest(names=names, counts=counts):
                with self.assertRaises(renaming.IdentifierRenameError):
                    self.verify(updated, names, counts)
        updated["initialCode"] = updated["initialCode"].replace(
            "int frecuencia_maxima_encontrada", "int maxima")
        with self.assertRaises(renaming.IdentifierRenameError):
            self.verify(updated)

    def test_preprocessor_identifier_and_physical_splices_fail_closed(self):
        before = deepcopy(ORIGINAL)
        before["initialCode"] = "#define maxima 2\nint x = maxima;\n"
        after = deepcopy(before)
        after["initialCode"] = after["initialCode"].replace("int x = maxima;",
                                                              "int x = frecuencia_maxima_encontrada;")
        with self.assertRaises(renaming.IdentifierRenameError):
            renaming.require_declared_cpp_renames(
                before, after,
                {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                {"initialCode": {"maxima": 1}})
        before["initialCode"] = "int maxima \\\n = 2;\n"
        after["initialCode"] = "int frecuencia_maxima_encontrada \\\n = 2;\n"
        with self.assertRaises(renaming.IdentifierRenameError):
            renaming.require_declared_cpp_renames(
                before, after,
                {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                {"initialCode": {"maxima": 1}})

    def test_unsafe_new_name_or_existing_name_collision_is_rejected(self):
        for bad in ("new", "__internal", "1digit", "maxima"):
            with self.subTest(replacement=bad):
                with self.assertRaises(renaming.IdentifierRenameError):
                    self.verify(
                        proposal(),
                        {"initialCode": {"maxima": bad},
                         "projectFiles/0/initialCode": RENAMES["projectFiles/0/initialCode"]},
                        COUNTS)
        with self.assertRaises(renaming.IdentifierRenameError):
            self.verify(
                proposal(), {"initialCode": {"maxima": "label"},
                             "projectFiles/0/initialCode": RENAMES["projectFiles/0/initialCode"]},
                COUNTS)


if __name__ == "__main__":
    unittest.main()

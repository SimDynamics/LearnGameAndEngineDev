"""Regress a declared C++ identifier rename without touching curricular assets."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import re
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
        for bad in ("new", "__internal", "1digit", "maxima",
                    "module", "import", "final", "override"):
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



    def test_macro_in_an_unchanged_project_file_blocks_a_rename(self):
        for defined_name in ("maxima", "frecuencia_maxima_encontrada"):
            with self.subTest(preprocessor=defined_name):
                original = deepcopy(ORIGINAL)
                original["projectFiles"][0]["initialCode"] = (
                    "#define " + defined_name + " 7\n"
                    + original["projectFiles"][0]["initialCode"]
                )
                revised = deepcopy(original)
                revised["initialCode"] = proposal()["initialCode"]
                with self.assertRaisesRegex(
                    renaming.IdentifierRenameError, "Preprocessor directive"):
                    renaming.require_declared_cpp_renames(
                        original, revised, {"initialCode": RENAMES["initialCode"]},
                        {"initialCode": COUNTS["initialCode"]})

    def test_cmake_compile_definitions_cannot_capture_old_or_new_name(self):
        for spelling in ("maxima", "frecuencia_maxima_encontrada"):
            with self.subTest(macro=spelling):
                original = deepcopy(ORIGINAL)
                original["projectFiles"][1]["initialCode"] = (
                    "target_compile_definitions(ejercicio PRIVATE " + spelling + "=7)\n")
                revised = deepcopy(original)
                revised["initialCode"] = proposal()["initialCode"]
                revised["projectFiles"][0]["initialCode"] = (
                    proposal()["projectFiles"][0]["initialCode"])
                with self.assertRaisesRegex(
                        renaming.IdentifierRenameError,
                        "CMake project arguments mention"):
                    renaming.require_declared_cpp_renames(
                        original, revised, RENAMES, COUNTS)

        # Comments are inert and do not silently define a C++ macro.
        original = deepcopy(ORIGINAL)
        original["projectFiles"][1]["initialCode"] = (
            "# maxima and frecuencia_maxima_encontrada are documented\n"
            "project(unchanged)\n")
        revised = deepcopy(original)
        revised["initialCode"] = proposal()["initialCode"]
        revised["projectFiles"][0]["initialCode"] = (
            proposal()["projectFiles"][0]["initialCode"])
        self.assertEqual(
            renaming.require_declared_cpp_renames(
                original, revised, RENAMES, COUNTS), COUNTS)

    def test_destination_collision_in_another_source_even_when_it_is_edited(self):
        for also_renamed in (False, True):
            with self.subTest(second_source_changed=also_renamed):
                original = deepcopy(ORIGINAL)
                original["projectFiles"][0]["initialCode"] = (
                    "int frecuencia_maxima_encontrada = 1;\n"
                    + original["projectFiles"][0]["initialCode"]
                )
                revised = deepcopy(original)
                revised["initialCode"] = proposal()["initialCode"]
                names = {"initialCode": RENAMES["initialCode"]}
                counts = {"initialCode": COUNTS["initialCode"]}
                if also_renamed:
                    revised["projectFiles"][0]["initialCode"] = (
                        original["projectFiles"][0]["initialCode"].replace(
                            "mayores", "valores_mas_frecuentes"))
                    names["projectFiles/0/initialCode"] = RENAMES["projectFiles/0/initialCode"]
                    counts["projectFiles/0/initialCode"] = COUNTS["projectFiles/0/initialCode"]
                with self.assertRaisesRegex(
                    renaming.IdentifierRenameError, "destination already exists"):
                    renaming.require_declared_cpp_renames(original, revised, names, counts)

    def test_malformed_names_fail_closed_before_scanning_directives(self):
        for invalid in (None, 12, [], {"maxima": 12}, {12: "variable"}):
            with self.subTest(invalid=invalid):
                names = deepcopy(RENAMES)
                names["initialCode"] = invalid
                with self.assertRaises(renaming.IdentifierRenameError):
                    self.verify(proposal(), names, COUNTS)



    def test_distinct_project_sources_cannot_merge_at_one_new_identifier(self):
        original = deepcopy(ORIGINAL)
        revised = proposal()
        merged = "identificador_fusionado"
        revised["initialCode"] = revised["initialCode"].replace(
            "frecuencia_maxima_encontrada", merged)
        revised["projectFiles"][0]["initialCode"] = (
            revised["projectFiles"][0]["initialCode"].replace(
                "valores_mas_frecuentes", merged))
        with self.assertRaisesRegex(
                renaming.IdentifierRenameError,
                "Distinct C\\+\\+ identifiers reuse a destination across slots"):
            renaming.require_declared_cpp_renames(
                original, revised,
                {"initialCode": {"maxima": merged},
                 "projectFiles/0/initialCode": {"mayores": merged}},
                COUNTS)

    def test_same_original_identifier_can_be_renamed_in_several_sources(self):
        original = deepcopy(ORIGINAL)
        original["projectFiles"][0]["initialCode"] = (
            "int cuenta(int maxima) {\n"
            "    return maxima;\n"
            "}\n"
        )
        revised = deepcopy(original)
        revised["initialCode"] = proposal()["initialCode"]
        revised["projectFiles"][0]["initialCode"] = (
            original["projectFiles"][0]["initialCode"].replace(
                "maxima", "frecuencia_maxima_encontrada"))
        names = {
            "initialCode": {"maxima": "frecuencia_maxima_encontrada"},
            "projectFiles/0/initialCode": {"maxima": "frecuencia_maxima_encontrada"},
        }
        counts = {
            "initialCode": {"maxima": 2},
            "projectFiles/0/initialCode": {"maxima": 2},
        }
        self.assertEqual(
            renaming.require_declared_cpp_renames(original, revised, names, counts),
            counts)
        self.assertEqual(original["outputCheck"], revised["outputCheck"])
        self.assertIn('"Maxima:"', revised["initialCode"])

        # Inconsistent replacements of the same old name across TUs cannot
        # claim a binding-preserving rename without a verified scope map.
        inconsistent = deepcopy(revised)
        inconsistent["projectFiles"][0]["initialCode"] = (
            inconsistent["projectFiles"][0]["initialCode"].replace(
                "frecuencia_maxima_encontrada", "otra_frecuencia"))
        invalid_names = deepcopy(names)
        invalid_names["projectFiles/0/initialCode"]["maxima"] = "otra_frecuencia"
        with self.assertRaisesRegex(
                renaming.IdentifierRenameError,
                "inconsistent cross-file replacements"):
            renaming.require_declared_cpp_renames(
                original, inconsistent, invalid_names, counts)

    def test_unreviewed_source_cannot_retain_the_old_identifier(self):
        original = deepcopy(ORIGINAL)
        original["projectFiles"][0]["initialCode"] = (
            "int observa(int maxima) {\n"
            "    return maxima;\n"
            "}\n"
        )
        revised = deepcopy(original)
        revised["initialCode"] = proposal()["initialCode"]
        # The old code remains in a different translation unit, so a
        # single-slot patch must not claim a safe cross-file renaming.
        with self.assertRaisesRegex(
                renaming.IdentifierRenameError,
                "unchanged C\+\+ source still refers"):
            renaming.require_declared_cpp_renames(
                original, revised,
                {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                {"initialCode": {"maxima": 2}})

        # Only literal text mentions the old name: it is not a code reference
        # and must not be rewritten as part of this lexical operation.
        original["projectFiles"][0]["initialCode"] = (
            'const char* letrero = "maxima";\n'
        )
        revised = deepcopy(original)
        revised["initialCode"] = proposal()["initialCode"]
        self.assertEqual(
            renaming.require_declared_cpp_renames(
                original, revised,
                {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                {"initialCode": {"maxima": 2}}),
            {"initialCode": {"maxima": 2}})

    def test_function_name_introspection_breaks_identifier_only_equivalence(self):
        for observer in ("__func__", "__FUNCTION__", "__PRETTY_FUNCTION__",
                         "__FUNCSIG__", "__FUNCDNAME__", "typeid"):
            with self.subTest(observer=observer):
                before = deepcopy(ORIGINAL)
                before["initialCode"] = (
                    "const char* maxima() {\n"
                    "    return " + observer + ";\n"
                    "}\n"
                )
                after = deepcopy(before)
                after["initialCode"] = before["initialCode"].replace(
                    "maxima()", "frecuencia_maxima_encontrada()")
                with self.assertRaisesRegex(
                        renaming.IdentifierRenameError,
                        "observe renamed C\+\+ symbol spelling"):
                    renaming.require_declared_cpp_renames(
                        before, after,
                        {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                        {"initialCode": {"maxima": 1}})

    def test_unchanged_preprocessor_stringize_and_paste_are_not_safe(self):
        for directive in ("#define STRINGIFY(x) #x",
                          "#define CONCAT(a,b) a##b"):
            with self.subTest(directive=directive):
                before = deepcopy(ORIGINAL)
                before["initialCode"] = (
                    directive + "\n"
                    "int maxima = 4;\n"
                    "int main() { return maxima; }\n"
                )
                after = deepcopy(before)
                after["initialCode"] = before["initialCode"].replace(
                    "maxima", "frecuencia_maxima_encontrada")
                with self.assertRaisesRegex(
                        renaming.IdentifierRenameError,
                        "observe renamed C\+\+ symbol spelling"):
                    renaming.require_declared_cpp_renames(
                        before, after,
                        {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                        {"initialCode": {"maxima": 2}})

    def test_inert_mentions_of_function_name_macros_remain_lexical(self):
        before = deepcopy(ORIGINAL)
        before["initialCode"] += (
            '// __func__ is only a comment\n'
            'const char* unaffected = "__PRETTY_FUNCTION__";\n'
        )
        after = deepcopy(before)
        after["initialCode"] = before["initialCode"].replace(
            "int maxima = 4;", "int frecuencia_maxima_encontrada = 4;").replace(
            "<< maxima <<", "<< frecuencia_maxima_encontrada <<")
        self.assertEqual(
            renaming.require_declared_cpp_renames(before, after,
                {"initialCode": {"maxima": "frecuencia_maxima_encontrada"}},
                {"initialCode": {"maxima": 2}}),
            {"initialCode": {"maxima": 2}})

    def test_real_frequency_lesson_covers_examples_editors_and_predictions(self):
        # Author proposals only in memory, from the real immutable published
        # owners in P1 U5L13. Work alone can edit this curricular JSON.
        path = (Path(__file__).resolve().parents[2] /
                "app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json")
        original_bytes = path.read_bytes()
        lesson = json.loads(original_bytes)
        self.assertEqual(
            lesson["id"],
            "cpp_fundamentals__colecciones_y_recorridos__contar_frecuencias_y_conservar_empates")
        cases = (
            ("block_0006_0002", "code", "etiquetas_mas_frecuentes"),
            ("recuperacion_entrada_editor", "cpp_compiler", "etiquetas_mas_frecuentes"),
            ("block_0006_0009", "cpp_compiler", "etiquetas_mas_frecuentes"),
            ("block_0007_0002", "output_prediction", "etiquetas_mas_frecuentes"),
            ("block_0007_0004", "output_prediction", "etiquetas_mas_frecuentes"),
            ("frecuencias_numericas_referencia_programa", "code", "valores_mas_frecuentes"),
        )
        owners = {
            identity: block
            for page, state_page in zip(
                    lesson["pages"], lesson["stateLayout"]["current"])
            for identity, block in zip(state_page["blocks"], page["blocks"])
        }
        for identity, kind, new_candidates in cases:
            with self.subTest(owner=identity):
                self.assertIn(identity, owners)
                before = owners[identity]
                self.assertEqual(before["type"], kind)
                slot = "initialCode" if kind == "cpp_compiler" else "code"
                original_source = before[slot]
                # These are the real historical identifiers and their complete
                # token counts, not invented sample snippets.
                self.assertEqual(
                    len(re.findall(r"\bmaxima\b", original_source)), 5)
                self.assertEqual(
                    len(re.findall(r"\bmayores\b", original_source)), 5)
                self.assertIn('"Maxima:"', original_source)
                self.assertIn('Mayores:', original_source)
                after = deepcopy(before)
                renamed_source = re.sub(
                    r"\bmaxima\b", "frecuencia_maxima_encontrada", original_source)
                renamed_source = re.sub(
                    r"\bmayores\b", new_candidates, renamed_source)
                after[slot] = renamed_source
                counts = {slot: {"maxima": 5, "mayores": 5}}
                declared = {slot: {
                    "maxima": "frecuencia_maxima_encontrada",
                    "mayores": new_candidates,
                }}
                self.assertEqual(
                    renaming.require_declared_cpp_renames(
                        before, after, declared, counts),
                    counts)
                self.assertEqual(
                    source_format.source_projection(after, before), before)
                self.assertIn('"Maxima:"', renamed_source)
                self.assertIn('Mayores:', renamed_source)
                self.assertEqual(
                    original_source.count("\n"), renamed_source.count("\n"))
                with self.assertRaises(source_format.SourceFormatError):
                    source_format.require_source_format(before, after)
        self.assertEqual(path.read_bytes(), original_bytes)


if __name__ == "__main__":
    unittest.main()

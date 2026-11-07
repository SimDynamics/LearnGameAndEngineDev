"""Bibliographic-only preservation proofs; assets are read-only fixtures."""
from copy import deepcopy
from importlib import util
import json
import os
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = Path(__file__).parent
BEFORE_AUTHORING = "06d31ba73be4b0c7672c7113df2340c9edbb2c04"


def module(name, filename):
    spec = util.spec_from_file_location(name, SCRIPTS / filename)
    result = util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


b = module("bibliography_fixture", "bibliography_revision.py")
contract = module("bibliography_contract_fixture", "validar_json_programacion.py")


def bibliography_revision_cases():
    """Shared explicit QA oracles for Python/native parser parity, never assets."""
    cases = [
        ("Una explicación (García, 2021, pp. 10–12).", "Una explicación (García, 2021).", True),
        ("Una explicación (van der Waals & Smith, 2022a, capítulo IV, sección 2.3).", "Una explicación (van der Waals & Smith, 2022a).", True),
        ("Una explicación (O’Connor et al., 1999, section 2; paragraph 3; Doe, 2000, p. 4).", "Una explicación (O’Connor et al., 1999; Doe, 2000, p. 4).", True),
        ("Una explicación (Instituto de Investigación, 2020, tabla 4 y figura 2).", "Una explicación (Instituto de Investigación, 2020).", True),
        ("Una explicación (Autor, 2024, párrs. 1–3 y tabla 9; Otro, 2020).", "Una explicación (Autor, 2024; Otro, 2020).", True),
        ("(Autor, 2024, p. 8) y después (Otra Autora, 2020, sección 4).", "(Autor, 2024) y después (Otra Autora, 2020).", True),
        ("Los literales 'X' y \"Y\" se conservan.\n\nUna explicación (Autor, 2024, p. 8).", "Los literales 'X' y \"Y\" se conservan.\n\nUna explicación (Autor, 2024).", True),
        ("😀 Una explicación (Autor, 2024, § 2.3).\r\nCódigo `x = 2;` intacto.", "😀 Una explicación (Autor, 2024).\r\nCódigo `x = 2;` intacto.", True),
    ]
    long, short = "(Autor, 2024, p. 8)", "(Autor, 2024)"
    for template in ('`%s`', '``%s``', '```cpp\n%s\n```', '~~~cpp\n%s\n~~~', '"%s"', "'%s'", '“%s”', '‘%s’', '«%s»',
                     'R"tag(%s)tag"', 'u8R"x(otra "cadena" %s)x"', '// %s\n', '/* %s */',
                     'f%s', 'f %s;', 'std::f %s;', 'Una cita «literal» %s.', 'Una cita "literal" %s.', 'Un bloque (%s) anidado'):
        cases.append((template % long, template % short, False))
    before = "Se conserva `x = 7;` y 2 datos (Autor, 2024, sección 2.3, p. 8; Otro, 2020, tabla 4)."
    valid = "Se conserva `x = 7;` y 2 datos (Autor, 2024; Otro, 2020, tabla 4)."
    cases.append((before, valid, True))
    for changed in (before.replace(", p. 8", ""), before.replace("p. 8", "p. 9"), before.replace("p. 8", "p. 8 y tabla 1"),
                    valid.replace("Autor", "Autora"), valid.replace("2024", "2023"), valid.replace("; Otro, 2020, tabla 4", ""),
                    valid.replace("; Otro", ";  Otro"), valid.replace("x = 7;", "x = 8;"), valid.replace("2 datos", "3 datos"),
                    valid.replace("tabla 4", "tabla 5"), valid.replace(").", ")!")):
        cases.append((before, changed, False))
    for ending in (" (", " `sin cierre", ' "sin cierre', ' R"x(sin cierre'):
        cases.append(("Una explicación " + long + ending, "Una explicación " + short + ending, False))
    cases.extend([
        ("Una explicación (dato, 2024, p. 8).", "Una explicación (dato, 2024).", False),
        ("Una explicación (Autor, 2024, p. 8; 2020).", "Una explicación (Autor, 2024).", False),
        ("Una explicación (Autor, 2024, pp. 8, 2020).", "Una explicación (Autor, 2024).", False),
        ("Una explicación (Autor, 2024, p. 8 y la razón es otra).", "Una explicación (Autor, 2024).", False),
        ("Una explicación " + short, "Una explicación " + short, False),
        ("Una explicación " + short, "Una explicación " + long, False),
    ])
    for prefix in ("f ", "std::f ", "obj.f ", "obj->f ", "auto x = f ", "x + f ", "return f ", "co_return f ",
                   'f("texto") + g ', "static_cast<T> ", "sizeof f ", "int f ", "auto x ", "x and f "):
        cases.append((prefix + long, prefix + short, False))
    return [{"before": first, "after": second, "expected": accepted} for first, second, accepted in cases]


class BibliographyRevisionTest(unittest.TestCase):
    def test_shared_native_parser_oracles(self):
        for index, case in enumerate(bibliography_revision_cases()):
            with self.subTest(index=index):
                self.assertEqual(b.only_parenthetical_locator_removal(case["before"], case["after"]), case["expected"])

    def test_generic_authors_locator_types_and_independent_components(self):
        cases = [
            ("Una explicación (García, 2021, pp. 10–12).", "Una explicación (García, 2021)."),
            ("Una explicación (van der Waals & Smith, 2022a, capítulo IV, sección 2.3).",
             "Una explicación (van der Waals & Smith, 2022a)."),
            ("Una explicación (Instituto de Investigación, 2020, tabla 4 y figura 2).",
             "Una explicación (Instituto de Investigación, 2020)."),
            ("Una explicación (O’Connor et al., 1999, section 2; paragraph 3; Doe, 2000, p. 4).",
             "Una explicación (O’Connor et al., 1999; Doe, 2000, p. 4)."),
            ("Una explicación (Autor, 2024, p. 8; Otro, 2020, sección 4).",
             "Una explicación (Autor, 2024; Otro, 2020)."),
            ("(Autor, 2024, p. 8) y después (Otra Autora, 2020, sección 4).",
             "(Autor, 2024) y después (Otra Autora, 2020)."),
            ("😀 Una explicación (Autor, 2024, párrs. 1–3 y tabla 9).\r\nCódigo `x = 2;` intacto.",
             "😀 Una explicación (Autor, 2024).\r\nCódigo `x = 2;` intacto."),
        ]
        for before, after in cases:
            with self.subTest(before=before):
                self.assertTrue(b.only_parenthetical_locator_removal(before, after))
                self.assertFalse(b.only_parenthetical_locator_removal(after, before))
                self.assertFalse(b.only_parenthetical_locator_removal(before, before))

    def test_quotes_code_comments_and_textual_citations_are_protected(self):
        citation, short = "(Autor, 2024, p. 8)", "(Autor, 2024)"
        for template in [
            '`%s`', '``%s``', '```cpp\n%s\n```', '~~~cpp\n%s\n~~~', '"%s"', "'%s'", '“%s”', '«%s»',
            'R"tag(%s)tag"', 'u8R"x(otra "cadena" %s)x"', '// %s\n', '/* %s */',
            'f%s', 'función%s', 'f %s;', 'std::f %s;', 'Una cita «literal» %s.', 'Una cita "literal" %s.',
            'Un bloque (%s) anidado',
        ]:
            before, after = template % citation, template % short
            with self.subTest(template=template):
                self.assertFalse(b.only_parenthetical_locator_removal(before, after))
        # Earlier character/string examples do not turn a later paraphrase
        # into a quotation. Only the protected source interval is excluded.
        before = "Los literales 'X', 'Y' y \"X\" conservan su código.\n\nUna explicación " + citation + "."
        self.assertTrue(b.only_parenthetical_locator_removal(before, before.replace(citation, short)))

    def test_no_partial_locator_edit_author_change_or_other_text_change(self):
        before = "Se conserva `x = 7;` y 2 datos (Autor, 2024, sección 2.3, p. 8; Otro, 2020, tabla 4)."
        valid = "Se conserva `x = 7;` y 2 datos (Autor, 2024; Otro, 2020, tabla 4)."
        self.assertTrue(b.only_parenthetical_locator_removal(before, valid))
        for after in [
            before.replace(", p. 8", ""), before.replace("p. 8", "p. 9"),
            before.replace("sección 2.3, p. 8", "p. 8"), before.replace("p. 8", "p. 8 y tabla 1"),
            valid.replace("Autor", "Autora"), valid.replace("2024", "2023"), valid.replace("; Otro, 2020, tabla 4", ""),
            valid.replace("; Otro", ";  Otro"), valid.replace("; Otro", ", Otro"),
            valid.replace("x = 7;", "x = 8;"), valid.replace("2 datos", "3 datos"), valid.replace("Se conserva", "Se pierde"),
            valid.replace("tabla 4", "tabla 5"), valid.replace(").", ")!"),
            "Se conserva `x = 7;` y 2 datos (Otro, 2020, tabla 4; Autor, 2024).",
        ]:
            with self.subTest(after=after):
                self.assertFalse(b.only_parenthetical_locator_removal(before, after))

    def test_ambiguous_malformed_and_unsupported_syntax_stays_protected(self):
        for before, after in [
            ("Una explicación (Autor, 2024, palabras 8).", "Una explicación (Autor, 2024)."),
            ("Una explicación (dato, 2024, p. 8).", "Una explicación (dato, 2024)."),
            ("Una explicación (Autor, 2024, p. 8 y la razón es otra).", "Una explicación (Autor, 2024)."),
            ("Una explicación (Autor, 2024, p. 8; 2020).", "Una explicación (Autor, 2024)."),
            ("Una explicación (Autor, 2024, pp. 8, 2020).", "Una explicación (Autor, 2024)."),
            ("Una explicación (Autor, 2024, p. 8). (", "Una explicación (Autor, 2024). ("),
            ("Una explicación (Autor, 2024, p. 8). `sin cierre", "Una explicación (Autor, 2024). `sin cierre"),
            ("Una explicación (Autor, 2024, p. 8). \"sin cierre", "Una explicación (Autor, 2024). \"sin cierre"),
            ("Una explicación (Autor, 2024, p. 8). R\"x(sin cierre", "Una explicación (Autor, 2024). R\"x(sin cierre"),
        ]:
            with self.subTest(before=before):
                self.assertFalse(b.only_parenthetical_locator_removal(before, after))
        for before, after in [(None, "texto"), ("texto", []), ("", ""), (" ", "texto"), ("texto", "texto")]:
            self.assertFalse(b.only_parenthetical_locator_removal(before, after))

    def test_block_proof_is_sole_explanation_and_type_independent_of_location(self):
        for kind in sorted(b.EXERCISE_TYPES):
            old = {"type": kind, "id": "qa_same_owner", "question": "Sin cambios", "explanation": "Una explicación (Autora, 2020, p. 4)."}
            current = dict(old, explanation="Una explicación (Autora, 2020).")
            self.assertTrue(b.is_bibliography_only_revision(old, current))
            for filename in ("objetos_y_estado/lesson_nuevo.json", "otra_unidad/lesson_otro.json"):
                self.assertTrue(contract.preserves_previous_exercise(str(contract.CONTENT.relative_to(ROOT) / filename), 99, 99, old, current))
            for field, value in {"id": "otro", "question": "Otra pregunta", "code": "int n=1;", "initialCode": "int n=2;",
                "options": ["A", "B"], "correctIndex": 1, "answers": ["otra"], "answerAlternatives": [["otra"]],
                "answerComparison": "cpp_decimal_literal_value", "outputComparison": "exact", "fragments": ["x;"],
                "correctOrder": [0], "reviewFeedback": "Otra guía"}.items():
                changed = dict(current, **{field: value})
                with self.subTest(kind=kind, field=field):
                    self.assertFalse(b.is_bibliography_only_revision(old, changed))
                    self.assertFalse(contract.preserves_previous_exercise(str(contract.CONTENT.relative_to(ROOT) / "otra/lesson_qa.json"), 99, 99, old, changed))
            for kind in ("cpp_compiler", "math_puzzle", "paragraph", "unknown"):
                self.assertFalse(b.is_bibliography_only_revision(dict(old, type=kind), dict(current, type=kind)))
        old = {"type": "multiple_choice", "correctIndex": 0, "explanation": "Una explicación (Autora, 2020, p. 4)."}
        current = dict(old, correctIndex=False, explanation="Una explicación (Autora, 2020).")
        self.assertFalse(b.is_bibliography_only_revision(old, current))

    def test_current_ten_blocks_eleven_citations_and_remaining_sources_are_exact(self):
        # Explicit expected authoring lives only in this QA fixture. Production
        # has no author, location, text or digest exception list.
        endings = {
            "(Köppe et al., 2023, sección 5.13.3, párr. 4 y tabla 10; Stroustrup, 2024)": "(Köppe et al., 2023; Stroustrup, 2024)",
            "(Köppe et al., 2023, sección 5.13.3, párrs. 1–3 y tabla 9)": "(Köppe et al., 2023)",
            "(Köppe et al., 2023, sección 23.4.3.7.1, párr. 5; sección 23.4.3.7.2, párr. 11)": "(Köppe et al., 2023)",
            "(Köppe et al., 2023, sección 30.4.3.3.3, párr. 5; sección 7.3.7, párr. 6; Stroustrup, 2024)": "(Köppe et al., 2023; Stroustrup, 2024)",
            "(Köppe et al., 2023, sección 5.13.6, párr. 1; Stroustrup, 2024)": "(Köppe et al., 2023; Stroustrup, 2024)",
            "(Köppe et al., 2023, sección 7.6.1.9, párr. 1; Stroustrup, 2024)": "(Köppe et al., 2023; Stroustrup, 2024)",
            "(Köppe et al., 2023, sección 6.8.2, párr. 12; sección 9.2.9.3, párr. 4 y tabla 17)": "(Köppe et al., 2023)",
            "(Köppe et al., 2023, sección 9.2.9.3, párr. 4 y tabla 17)": "(Köppe et al., 2023)",
        }
        folder = contract.CONTENT / "objetos_y_estado"
        count = citations = 0
        for name in ("lesson_caracteres_y_valores_logicos.json", "lesson_conversiones_y_deduccion.json", "lesson_enteros_y_aproximaciones.json"):
            path = folder / name
            raw = path.read_bytes()
            saved = subprocess.check_output(["git", "--no-replace-objects", "show", BEFORE_AUTHORING + ":" + path.relative_to(ROOT).as_posix()],
                cwd=ROOT, env=dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1"))
            document = json.loads(saved.decode("utf-8-sig"))
            for pn, page in enumerate(document["pages"]):
                for bn, old in enumerate(page["blocks"]):
                    explanation = old.get("explanation", "")
                    replacements = [(before, after) for before, after in endings.items() if before in explanation]
                    if not replacements:
                        continue
                    expected = explanation
                    for before, after in replacements:
                        citations += expected.count(before)
                        expected = expected.replace(before, after)
                    current = dict(deepcopy(old), explanation=expected)
                    with self.subTest(lesson=name, page=pn + 1, block=bn + 1):
                        self.assertTrue(b.is_bibliography_only_revision(old, current))
                        self.assertTrue(contract.preserves_previous_exercise(path.relative_to(ROOT).as_posix(), pn, bn, old, current))
                        self.assertEqual({k: v for k, v in old.items() if k != "explanation"},
                                         {k: v for k, v in current.items() if k != "explanation"})
                    count += 1
            self.assertEqual(path.read_bytes(), raw)
        self.assertEqual((count, citations), (10, 11))


if __name__ == "__main__":
    unittest.main()

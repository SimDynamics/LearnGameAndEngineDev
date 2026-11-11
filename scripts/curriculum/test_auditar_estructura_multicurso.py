"""Pruebas independientes para auditoría NO aprobatoria de 2 cursos."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr
from io import StringIO

import auditar_estructura_multicurso as audit
import stable_state_layout as state
import validar_json_programacion as contract


class StructureOnlyAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.lessons = {}
        self.units = {}
        self.courses = {}
        for course in audit.COURSES:
            content = self.root / "app/src/main/assets/content/es/programming" / course
            lesson = content / "unidad" / "lesson_ficticia.json"
            unit = content / "unidad" / "unit_unidad.json"
            index = content / f"course_{course}.json"
            lesson.parent.mkdir(parents=True, exist_ok=True)
            lesson_id = f"{course}__unidad__ficticia"
            unit_id = f"{course}__unidad"
            self.lessons[course] = lesson
            self.units[course] = unit
            self.courses[course] = index
            self.write(lesson, {
                "id": lesson_id,
                "title": "Lección",
                "pages": [{"blocks": [
                    {"type": "heading", "text": "Contrato"},
                    {"type": "multiple_choice", "id": "pregunta",
                     "question": "¿Qué respuesta?", "options": ["A", "B"],
                     "correctIndex": 0, "explanation": "La primera"},
                ]}],
            })
            self.write(unit, {
                "courseId": course, "unitId": unit_id, "title": "Unidad",
                "lessons": [{"number": 1, "id": lesson_id, "title": "Lección"}],
            })
            self.write(index, {
                "id": course, "title": "Curso",
                "units": [{"number": 1, "id": unit_id, "title": "Unidad"}],
            })

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")

    @staticmethod
    def read(path):
        return json.loads(path.read_text(encoding="utf-8"))

    def test_checks_both_courses_and_counts_exact_actual_json(self):
        saved_errors = contract.ERRORS
        previous = list(saved_errors)
        result = audit.inspect(self.root)
        self.assertEqual("STRUCTURE_ONLY_NOT_APPROVED", result["scope"])
        self.assertEqual(result["errors"], 0, result["issues"])
        self.assertEqual(result["lessons"], 2)
        self.assertEqual(result["blocks"], 4)
        self.assertEqual(result["files"], 6)
        self.assertEqual([row["lessons"] for row in result["courses"]], [1, 1])
        self.assertFalse(result["historicalOriginVerified"])
        self.assertFalse(result["publishedTransitionsVerified"])
        self.assertFalse(result["compilerProgramsExecuted"])
        self.assertFalse(result["changesMadeToCurriculum"])
        self.assertIs(contract.ERRORS, saved_errors)
        self.assertEqual(saved_errors, previous)

    def test_detects_real_answer_contract_error_in_programming_2(self):
        path = self.lessons["programming_practice_tools"]
        document = self.read(path)
        document["pages"][0]["blocks"][1]["correctIndex"] = 5
        self.write(path, document)
        result = audit.inspect(self.root)
        self.assertGreaterEqual(result["errors"], 1)
        self.assertTrue(any("índice de respuesta fuera de rango" in issue["message"]
                            and issue["course"] == "programming_practice_tools"
                            for issue in result["issues"]), result["issues"])

    def test_detects_wrong_unit_reference_and_cross_course_identity(self):
        course = "programming_practice_tools"
        path = self.lessons[course]
        lesson = self.read(path)
        lesson["id"] = "cpp_fundamentals__unidad__ficticia"
        self.write(path, lesson)
        result = audit.inspect(self.root)
        self.assertGreaterEqual(result["errors"], 2)
        self.assertTrue(any("ID de lección distinto" in issue["message"]
                            for issue in result["issues"]))
        self.assertTrue(any("referencia a lección inexistente" in issue["message"]
                            for issue in result["issues"]))

    def test_bad_state_map_does_not_masquerade_as_global_approval(self):
        course = "programming_practice_tools"
        path = self.lessons[course]
        document = self.read(path)
        document["stateLayout"] = {"version": 999}
        self.write(path, document)
        with self.assertRaises((ValueError, KeyError, TypeError)):
            state.validate(document)
        result = audit.inspect(self.root)
        self.assertEqual(result["errors"], 0, result["issues"])
        self.assertFalse(result["historicalOriginVerified"])
        self.assertFalse(result["publishedTransitionsVerified"])
        self.assertEqual(result["scope"], "STRUCTURE_ONLY_NOT_APPROVED")

    def test_missing_course_does_not_pass_and_no_files_are_touched(self):
        course = "programming_practice_tools"
        missing = self.root / "missing"
        result = audit.inspect(missing, (course,))
        self.assertGreater(result["errors"], 0)
        self.assertIn(course, result["issues"][0]["course"])
        before = {str(p): p.read_bytes() for p in self.lessons.values()}
        with redirect_stdout(StringIO()):
            self.assertEqual(audit.main(["--root", str(self.root)]), 0)
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.lessons.values()})

    def test_report_only_outside_assets_and_correct_exit_code(self):
        output = self.root / "out/qa/structure-audit.json"
        capture = StringIO()
        with redirect_stdout(capture):
            status = audit.main(["--root", str(self.root),
                                 "--output-json", str(output)])
        self.assertEqual(status, 0)
        self.assertTrue(output.exists())
        self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["files"], 6)
        self.assertIn("SOLO ESTRUCTURA: NO certifica", capture.getvalue())
        forbidden = self.root / "app/src/main/assets/content/es/programming/report.json"
        with redirect_stderr(StringIO()), self.assertRaises(SystemExit):
            audit.main(["--root", str(self.root), "--output-json", str(forbidden)])
        self.assertFalse(forbidden.exists())
        mutated = self.read(self.lessons["cpp_fundamentals"])
        mutated["pages"][0]["blocks"][1]["correctIndex"] = 9
        self.write(self.lessons["cpp_fundamentals"], mutated)
        with redirect_stdout(StringIO()):
            self.assertEqual(audit.main(["--root", str(self.root)]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)

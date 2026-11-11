"""Regresiones del recorrido estructural multi-curso y de autoría editorial."""
from __future__ import annotations

from contextlib import redirect_stdout
from copy import deepcopy
from io import StringIO
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import stable_state_layout as layout
import validar_json_programacion as validator


COURSE = "programming_practice_tools"
FOLDER = "comprobaciones_repetibles"
NAME = "lesson_nombrar_reglas_y_separar_acuerdos.json"


class StructuralCourseFixtures(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="multicourse-validation-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        base = self.root / "app/src/main/assets/content/es/programming"
        self.cpp = base / "cpp_fundamentals"
        self.content = base / COURSE
        self.unit_path = self.content / "fixture" / "unit_fixture.json"
        self.lesson_path = self.content / "fixture" / "lesson_fixture.json"
        self.course_path = self.content / f"course_{COURSE}.json"
        self.unit_id = f"{COURSE}__fixture"
        self.lesson_id = f"{self.unit_id}__fixture"
        self.lesson = {
            "id": self.lesson_id,
            "title": "Contrato de prueba",
            "pages": [{
                "blocks": [
                    {"type": "heading", "text": "Actividad propia"},
                    {"type": "multiple_choice", "id": "pregunta_fixture",
                     "question": "¿Qué respuesta cumple el contrato?",
                     "options": ["La primera", "La segunda"], "correctIndex": 0,
                     "explanation": "La primera corresponde al acuerdo."},
                ],
            }],
        }
        self.unit = {
            "courseId": COURSE,
            "unitId": self.unit_id,
            "title": "Unidad ficticia",
            "lessons": [{"id": self.lesson_id, "number": 1, "title": self.lesson["title"]}],
        }
        self.course = {
            "id": COURSE,
            "title": "Curso ficticio",
            "units": [{"id": self.unit_id, "number": 1, "title": self.unit["title"]}],
        }
        self.save()

    def save(self):
        for path, value in ((self.lesson_path, self.lesson),
                            (self.unit_path, self.unit),
                            (self.course_path, self.course)):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")

    def run_course(self, *, reject=False):
        output = StringIO()
        with mock.patch.object(validator, "ROOT", self.root), \
             mock.patch.object(validator, "CONTENT", self.cpp), \
             mock.patch.object(validator, "ERRORS", []), \
             mock.patch.object(validator._state_layout, "check_repository", return_value={}), \
             redirect_stdout(output):
            if reject:
                with self.assertRaises(SystemExit) as exit_code:
                    validator.main(["--course", COURSE, "--list-files"])
                self.assertEqual(exit_code.exception.code, 1)
            else:
                validator.main(["--course", COURSE, "--list-files"])
        return output.getvalue()

    def test_programming2_is_checked_with_its_actual_course_prefix_and_files(self):
        output = self.run_course()
        self.assertIn(f"Curso {COURSE}: 1 lecciones, 1 unidades, 2 bloques", output)
        self.assertIn(self.lesson_path.relative_to(self.root).as_posix(), output)
        self.assertIn(self.unit_path.relative_to(self.root).as_posix(), output)
        self.assertIn(self.course_path.relative_to(self.root).as_posix(), output)

    def test_invalid_programming2_correct_index_fails_in_usual_cli(self):
        self.lesson["pages"][0]["blocks"][1]["correctIndex"] = 3
        self.save()
        output = self.run_course(reject=True)
        self.assertIn("índice de respuesta fuera de rango", output)
        self.assertIn(COURSE, output)

    def test_programming2_wrong_lesson_id_and_wrong_unit_reference_fail(self):
        self.lesson["id"] = "cpp_fundamentals__fixture__fixture"
        self.save()
        output = self.run_course(reject=True)
        self.assertIn("ID de lección distinto de la ruta", output)
        self.assertIn("referencia a lección inexistente", output)

    def test_programming2_wrong_course_and_unit_index_fail(self):
        self.course["id"] = "cpp_fundamentals"
        self.unit["lessons"][0]["number"] = 2
        self.save()
        output = self.run_course(reject=True)
        self.assertIn("ID de curso distinto del loader", output)
        self.assertIn("numeración de lecciones discontinua", output)

    def test_missing_programming2_directory_is_not_silently_skipped(self):
        self.course_path.unlink()
        self.unit_path.unlink()
        self.lesson_path.unlink()
        self.content.rmdir() if not any(self.content.iterdir()) else None
        # A missing index must fail, not count as a valid empty course.
        with self.assertRaises(FileNotFoundError):
            self.run_course()


class PublishedOrdinaryExplanationGuard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = (validator.ROOT / "app/src/main/assets/content/es/programming" /
                    COURSE / FOLDER / NAME)
        if not (validator.ROOT / ".git").exists() or not cls.path.is_file():
            raise unittest.SkipTest("Requires the published multi-course Git checkout")
        cls.previous, cls.family = layout.published_family(cls.path, "es", "HEAD")
        cls.relative = cls.path.relative_to(validator.ROOT).as_posix()
        cls.page, cls.block = 0, 5
        cls.identity = cls.previous["stateLayout"]["current"][cls.page]["blocks"][cls.block]
        cls.old = cls.previous["pages"][cls.page]["blocks"][cls.block]
        if cls.old.get("type") != "multiple_choice" or not cls.old.get("explanation"):
            raise AssertionError("Published editorial fixture changed its activity")

    @classmethod
    def reviewed(cls):
        drafted = deepcopy(cls.previous)
        revised = drafted["pages"][cls.page]["blocks"][cls.block]
        revised["explanation"] = (
            "La primera respuesta corresponde al acuerdo del programa y conserva "
            "las otras opciones, la pregunta y su criterio de corrección."
        )
        descriptor = layout.text_semantics.make_field(drafted, cls.identity,
                                                      "/explanation", [])
        for index, entry in enumerate(drafted["textSemantics"]["fields"]):
            if entry["block"] == cls.identity and entry["path"] == "/explanation":
                drafted["textSemantics"]["fields"][index] = descriptor
                break
        else:
            raise AssertionError("Published source does not own /explanation")
        prepared = layout.prepare_v2(drafted, "es", cls.family, cls.previous)
        return prepared

    @classmethod
    def accepts(cls, current, *, before=None, after=None, relative=None,
                course=COURSE, position=None):
        original_p, original_b = layout.validate(cls.previous)[cls.page, cls.block]
        if position is not None:
            original_p, original_b = position
        return validator.preserves_ordinary_explanation_revision(
            cls.relative if relative is None else relative,
            original_p, original_b,
            cls.old if before is None else before,
            layout._blocks(current)[cls.identity] if after is None else after,
            cls.previous, current, course_id=course,
        )

    def test_authored_explanation_only_is_accepted_for_programming2(self):
        current = self.reviewed()
        layout.validate_transition(self.previous, current)
        self.assertTrue(self.accepts(current))
        self.assertTrue(validator.preserves_previous_exercise(
            self.relative, *layout.validate(self.previous)[self.page, self.block],
            self.old, layout._blocks(current)[self.identity],
            previous_document=self.previous, current_document=current,
            course_id=COURSE,
        ))

    def test_contract_changes_cannot_hide_inside_explanation_revision(self):
        current = self.reviewed()
        revised = layout._blocks(current)[self.identity]
        variants = [
            dict(revised, question="Otra pregunta"),
            dict(revised, options=["A", "B"]),
            dict(revised, correctIndex=1),
            dict(revised, reviewFeedback=["Otra pista"]),
            dict(revised, id="otra_actividad"),
        ]
        for bad in variants:
            with self.subTest(change=sorted(set(bad) - set(revised)) or
                              sorted(key for key in revised if revised[key] != bad.get(key))):
                self.assertFalse(self.accepts(current, after=bad))

    def test_wrong_owner_family_chain_or_semantics_are_rejected(self):
        current = self.reviewed()
        self.assertFalse(self.accepts(current,
            relative=self.relative.replace("lesson_", "lesson_otra_", 1)))
        self.assertFalse(self.accepts(current, course="cpp_fundamentals"))
        wrong_locale = deepcopy(current)
        wrong_locale["stateLayout"]["locale"] = "en"
        self.assertFalse(self.accepts(wrong_locale))
        wrong_chain = deepcopy(current)
        wrong_chain["stateLayout"]["editorial"].pop()
        self.assertFalse(self.accepts(wrong_chain))
        no_descriptor = deepcopy(current)
        no_descriptor["textSemantics"]["fields"] = [
            item for item in no_descriptor["textSemantics"]["fields"]
            if item["block"] != self.identity or item["path"] != "/explanation"
        ]
        self.assertFalse(self.accepts(no_descriptor))

    def test_only_programming1_has_legacy_feedback_exceptions(self):
        current = self.reviewed()
        altered = dict(self.old, reviewFeedback=["Sin prueba"])
        self.assertFalse(validator.preserves_previous_exercise(
            self.relative, *layout.validate(self.previous)[self.page, self.block],
            self.old, altered, course_id=COURSE))
        self.assertTrue(self.accepts(current))


if __name__ == "__main__":
    unittest.main(verbosity=2)

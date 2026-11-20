"""Regresiones del recorrido estructural multi-curso y de autoría editorial."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
from io import StringIO
import json
from pathlib import Path
import tempfile
import unittest
import zlib
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
        self.select_course(COURSE)

    def select_course(self, course_id):
        self.course_id = course_id
        base = self.root / "app/src/main/assets/content/es/programming"
        self.cpp = base / "cpp_fundamentals"
        self.content = base / course_id
        self.unit_path = self.content / "fixture" / "unit_fixture.json"
        self.lesson_path = self.content / "fixture" / "lesson_fixture.json"
        self.course_path = self.content / f"course_{course_id}.json"
        self.unit_id = f"{course_id}__fixture"
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
            "courseId": course_id,
            "unitId": self.unit_id,
            "title": "Unidad ficticia",
            "lessons": [{"id": self.lesson_id, "number": 1, "title": self.lesson["title"]}],
        }
        self.course = {
            "id": course_id,
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

    def run_course(self, *, reject=False, course=None, exit_code=1, state_error=None):
        output = StringIO()
        with mock.patch.object(validator, "ROOT", self.root), \
             mock.patch.object(validator, "CONTENT", self.cpp), \
             mock.patch.object(validator, "ERRORS", []), \
             mock.patch.object(validator._state_layout, "check_repository",
                               return_value={}, side_effect=state_error), \
             redirect_stdout(output), redirect_stderr(output):
            arguments = ["--course", self.course_id if course is None else course, "--list-files"]
            if reject:
                with self.assertRaises(SystemExit) as failure:
                    validator.main(arguments)
                self.assertEqual(failure.exception.code, exit_code)
            else:
                validator.main(arguments)
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

    def test_usual_programming2_cli_rejects_an_unindexed_active_lesson(self):
        orphan = self.lesson_path.parent / "lesson_omitida.json"
        orphan.write_text(json.dumps({
            "id": f"{COURSE}__fixture__omitida",
            "title": "Lección omitida en índice",
            "pages": [{"blocks": [{"type": "paragraph", "text": "Contenido"}]}],
        }, ensure_ascii=False), encoding="utf-8")
        output = self.run_course(reject=True)
        self.assertIn("lección publicada sin índice en unidad activa", output)
        self.assertIn(f"{COURSE}__fixture__omitida", output)

    def test_usual_programming1_cli_rejects_an_unindexed_active_lesson(self):
        course = "cpp_fundamentals"
        folder = self.cpp / "fixture"
        folder.mkdir(parents=True, exist_ok=True)
        def write(path, value):
            path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
        lesson = {"id": f"{course}__fixture__primera", "title": "Primera",
                  "pages": [{"blocks": [{"type": "paragraph", "text": "Texto"}]}]}
        orphan = {"id": f"{course}__fixture__omitida", "title": "Omitida",
                  "pages": [{"blocks": [{"type": "paragraph", "text": "Texto"}]}]}
        write(folder / "lesson_primera.json", lesson)
        write(folder / "lesson_omitida.json", orphan)
        write(folder / "unit_fixture.json", {
            "courseId": course, "unitId": f"{course}__fixture",
            "title": "Unidad",
            "lessons": [{"number": 1, "id": lesson["id"], "title": lesson["title"]}],
        })
        write(self.cpp / "course_cpp_fundamentals.json", {
            "id": course, "title": "Curso",
            "units": [{"number": 1, "id": f"{course}__fixture", "title": "Unidad"}],
        })
        output = StringIO()
        with mock.patch.object(validator, "ROOT", self.root), \
             mock.patch.object(validator, "CONTENT", self.cpp), \
             mock.patch.object(validator, "ERRORS", []), \
             mock.patch.object(validator._state_layout, "check_repository", return_value={}), \
             redirect_stdout(output):
            with self.assertRaises(SystemExit) as failure:
                validator.main(["--course", course])
            self.assertEqual(failure.exception.code, 1)
        self.assertIn("lección publicada sin índice en unidad activa", output.getvalue())
        self.assertIn(orphan["id"], output.getvalue())

    def test_missing_programming2_directory_is_not_silently_skipped(self):
        self.course_path.unlink()
        self.unit_path.unlink()
        self.lesson_path.unlink()
        self.content.rmdir() if not any(self.content.iterdir()) else None
        # A missing index must fail, not count as a valid empty course.
        output = self.run_course(reject=True)
        self.assertIn(COURSE, output)
        self.assertIn(self.course_path.name, output)
        self.assertNotIn("Traceback", output)



    def complete_lesson(self):
        """Small authored payloads use the production contracts, not validator stubs."""
        code = '#include <iostream>\nint main() { std::cout << "7\\n"; }\n'
        self.lesson["pages"][0]["blocks"].extend([
            {"type": "order_code", "id": "orden_fixture", "language": "cpp",
             "question": "Ordena las líneas.", "fragments": ["int n = 7;", "return n;"],
             "correctOrder": [0, 1], "explanation": "Se declara antes de usar."},
            {"type": "fill_code", "id": "relleno_fixture", "language": "cpp",
             "question": "Completa el nombre.", "code": "int [] = 7;", "answers": ["n"],
             "explanation": "n identifica el objeto."},
            {"type": "output_prediction", "id": "salida_fixture", "language": "cpp",
             "question": "Predice la salida.", "code": code, "answers": ["7"],
             "explanation": "Se escribe el valor 7."},
            {"type": "inline_fill_code", "id": "inline_fixture", "language": "cpp",
             "question": "Completa el nombre.", "template": "int [] = 7;", "answers": ["n"]},
            {"type": "drag_fill_code", "id": "ficha_fixture", "language": "cpp",
             "question": "Elige el nombre.", "template": "int [] = 7;",
             "choices": ["n", "m"], "correctOrder": [0]},
            {"type": "cpp_compiler", "language": "cpp", "initialCode": code,
             "projectFiles": [{"path": "main.cpp", "initialCode": code}],
             "sourceFiles": ["main.cpp"],
             "outputCheck": {"cases": [{"standardInput": "", "outputs": ["7"],
                 "explanation": "El programa escribe 7."}]}},
            {"type": "code", "language": "cpp", "code": code},
            {"type": "image", "imageId": "fixture_pixel", "src": "images/fixture_pixel.png",
             "alt": "Recurso mínimo para comprobar el contrato de imagen."},
        ])
        image = self.root / "app/src/main/assets/content/images/fixture_pixel.png"
        image.parent.mkdir(parents=True, exist_ok=True)
        def chunk(kind, payload):
            return (len(payload).to_bytes(4, "big") + kind + payload +
                    (zlib.crc32(kind + payload) & 0xffffffff).to_bytes(4, "big"))
        image.write_bytes(b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", b"\0\0\0\1\0\0\0\1\x08\x02\0\0\0") +
            chunk(b"IDAT", zlib.compress(b"\0\0\0\0")) + chunk(b"IEND", b""))
        self.save()

    def test_new_and_future_course_ids_use_the_same_complete_contract(self):
        for course_id in ("cpp_language_memory_oop", "future_programming_2040"):
            with self.subTest(course=course_id):
                self.select_course(course_id)
                self.complete_lesson()
                before = {p.relative_to(self.root): p.read_bytes()
                          for p in self.root.rglob("*") if p.is_file()}
                output = self.run_course()
                self.assertIn(f"Curso {course_id}: 1 lecciones, 1 unidades, 10 bloques", output)
                for path in (self.course_path, self.unit_path, self.lesson_path):
                    self.assertIn(path.relative_to(self.root).as_posix(), output)
                self.assertEqual(before, {p.relative_to(self.root): p.read_bytes()
                                         for p in self.root.rglob("*") if p.is_file()})

    def test_all_discovers_existing_new_and_future_folders_without_a_catalog(self):
        courses = ("cpp_fundamentals", COURSE, "cpp_language_memory_oop",
                   "systems_architecture_toolchain", "future_programming_2040")
        for course_id in courses:
            self.select_course(course_id)
        output = self.run_course(course="all")
        for course_id in courses:
            self.assertIn(f"Curso {course_id}: 1 lecciones, 1 unidades, 2 bloques", output)
        self.assertIn("5 lecciones, 10 bloques", output)
        self.assertEqual(output.count("  archivo: "), 3 * len(courses))
        self.assertFalse((self.root / "app/src/main/assets/content/catalog.json").exists())

    def test_explicit_course_does_not_validate_a_different_unselected_course(self):
        self.course_path.write_text("{invalid", encoding="utf-8")
        self.select_course("cpp_language_memory_oop")
        self.assertIn("Curso cpp_language_memory_oop:", self.run_course())
        self.assertIn(COURSE, self.run_course(course="all", reject=True))

    def test_all_does_not_skip_a_new_folder_without_its_real_index(self):
        self.select_course("future_programming_2040")
        self.course_path.unlink()
        output = self.run_course(course="all", reject=True)
        self.assertIn(self.course_id, output)
        self.assertIn(self.course_path.name, output)

    def test_selector_rejects_paths_reserved_forms_and_empty_ids(self):
        for course_id in ("", "../outside", "a/b", "a\\b", "course__unit",
                          "UPPER", "_leading", "trailing_", "all/../cpp_fundamentals",
                          str(self.root / "outside")):
            with self.subTest(selector=course_id):
                output = self.run_course(course=course_id, reject=True, exit_code=2)
                self.assertIn("Identificador de curso inválido", output)
                self.assertNotIn("Traceback", output)

    def test_unknown_but_well_formed_course_has_a_controlled_missing_directory_error(self):
        output = self.run_course(course="not_authored_yet", reject=True)
        self.assertIn("not_authored_yet", output)
        self.assertIn("directorio", output)

    def test_selected_course_cannot_redirect_outside_programming(self):
        target = self.root / "external-course"
        target.mkdir()
        link = self.content.parent / "redirected_course"
        try:
            link.symlink_to(target, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Directory symlinks unavailable for this QA profile: {error}")
        output = self.run_course(course=link.name, reject=True)
        self.assertIn("redirected_course", output)
        self.assertIn("sin enlaces", output)

    def test_new_course_rejects_broken_json_and_malformed_document_shapes(self):
        for name in ("course", "unit", "lesson"):
            for value in ("{invalid", "[]", "null", "{}"):
                with self.subTest(document=name, payload=value):
                    self.select_course("cpp_language_memory_oop")
                    path = getattr(self, name + "_path")
                    path.write_text(value, encoding="utf-8")
                    output = self.run_course(reject=True)
                    self.assertIn(self.course_id, output)
                    self.assertTrue(path.name in output or path.parent.as_posix() in output)
                    self.assertNotIn("Traceback", output)

    def test_new_course_rejects_wrong_indices_numbers_and_cross_folder_references(self):
        variants = (
            ("course", lambda d: d.update(id="cpp_fundamentals"), "ID de curso"),
            ("course", lambda d: d["units"][0].update(number=2), "numeración"),
            ("course", lambda d: d["units"].append(deepcopy(d["units"][0])), "repetida"),
            ("unit", lambda d: d.update(courseId=COURSE), "ID de unidad"),
            ("unit", lambda d: d["lessons"][0].update(number=True), "numeración"),
            ("unit", lambda d: d["lessons"][0].update(title="Otro título"), "título"),
            ("unit", lambda d: d["lessons"][0].update(id="cpp_language_memory_oop__otra__fixture"), "inexistente"),
            ("lesson", lambda d: d.update(id="cpp_fundamentals__fixture__fixture"), "ID de lección"),
        )
        for name, mutate, expected in variants:
            with self.subTest(document=name, expected=expected):
                self.select_course("cpp_language_memory_oop")
                mutate(getattr(self, name))
                self.save()
                self.assertIn(expected, self.run_course(reject=True))

    def test_order_cards_are_nonblank_in_programming1_and_future_courses(self):
        # A blank separator is a presentation-only orderVisualSeparators entry,
        # not a fragment the learner must place in correctOrder. The native
        # parser retains legacy tolerance, while new authoring rejects it.
        for course_id in ("cpp_fundamentals", "cpp_language_memory_oop",
                          "future_programming_2040"):
            for bad in ("", " \t", "\n"):
                with self.subTest(course=course_id, fragment=repr(bad)):
                    self.select_course(course_id)
                    if course_id == "cpp_fundamentals":
                        # Keep its fixture independent of later project
                        # contract extensions specific to this first course.
                        self.lesson["pages"][0]["blocks"].append({
                            "type": "order_code", "id": "orden_fixture",
                            "language": "cpp", "question": "Ordena el programa.",
                            "fragments": ["int n = 7;", "return n;"],
                            "correctOrder": [0, 1],
                        })
                        self.save()
                    else:
                        self.complete_lesson()
                    order = next(block for block in self.lesson["pages"][0]["blocks"]
                                 if block["type"] == "order_code")
                    self.assertIn(f"Curso {course_id}:", self.run_course())
                    order["fragments"][0] = bad
                    self.save()
                    output = self.run_course(reject=True)
                    self.assertIn("fragments", output)
                    self.assertIn("no vac", output)
                    self.assertNotIn("Traceback", output)

    def test_future_course_rejects_undeclared_index_fields(self):
        # An extra field must not become an implicit authoring extension merely
        # because all IDs, titles and sequential numbers are otherwise valid.
        for level, collection in (("course", "units"), ("unit", "lessons")):
            with self.subTest(index=level):
                self.select_course("future_programming_2040")
                document = getattr(self, level)
                document[collection][0]["unreviewedMetadata"] = "ignored before"
                self.save()
                output = self.run_course(reject=True)
                self.assertIn("campos no admitidos", output)
                self.assertIn(self.course_id, output)
                self.assertNotIn("Traceback", output)

    def test_future_course_rejects_unknown_root_index_metadata(self):
        # Index roots are native reader contracts too. Unknown fields must
        # not be silently ignored just because every entry is well formed.
        for level in ("course", "unit"):
            with self.subTest(index=level):
                self.select_course("future_programming_2040")
                getattr(self, level)["unreviewedMetadata"] = {
                    "mayGrantCredit": True
                }
                self.save()
                output = self.run_course(reject=True)
                self.assertIn("índice de " + ("curso" if level == "course" else "unidad"), output)
                self.assertIn("campos no admitidos", output)
                self.assertIn(self.course_id, output)
                self.assertNotIn("Traceback", output)

    def test_future_course_allows_documented_optional_root_fields(self):
        # The publication gate authenticates the edition descriptor itself;
        # the structural index still needs to admit its declared envelope.
        self.select_course("future_programming_2040")
        self.course["developmentContent"] = False
        edition = {"version": 1, "courseId": self.course_id,
                   "locale": "es", "id": "qa", "generation": 1}
        self.course["courseEdition"] = deepcopy(edition)
        self.unit["courseEdition"] = deepcopy(edition)
        self.save()
        output = self.run_course()
        self.assertIn(f"Curso {self.course_id}:", output)

    def test_new_course_rejects_a_valid_lesson_indexed_from_another_folder(self):
        self.select_course("cpp_language_memory_oop")
        other = deepcopy(self.lesson)
        other["id"] = self.course_id + "__otra__fixture"
        path = self.content / "otra" / "lesson_fixture.json"
        path.parent.mkdir()
        path.write_text(json.dumps(other, ensure_ascii=False), encoding="utf-8")
        self.unit["lessons"].append({"number": 2, "id": other["id"], "title": other["title"]})
        self.save()
        self.assertIn("otra carpeta", self.run_course(reject=True))

    def test_new_course_rejects_unindexed_active_content(self):
        self.select_course("cpp_language_memory_oop")
        orphan = deepcopy(self.lesson)
        orphan["id"] = self.course_id + "__fixture__omitida"
        path = self.lesson_path.with_name("lesson_omitida.json")
        path.write_text(json.dumps(orphan, ensure_ascii=False), encoding="utf-8")
        self.assertIn("lección publicada sin índice en unidad activa", self.run_course(reject=True))

    def test_future_course_rejects_invalid_answers_fields_and_block_shapes(self):
        changes = (
            lambda d: d.update(pages={}),
            lambda d: d["pages"][0].update(blocks=[]),
            lambda d: d["pages"][0]["blocks"].append(None),
            lambda d: d["pages"][0]["blocks"][1].pop("type"),
            lambda d: d["pages"][0]["blocks"][1].update(type="unknown_activity"),
            lambda d: d["pages"][0]["blocks"][1].update(unreviewed=True),
            lambda d: d["pages"][0]["blocks"][1].update(options=["same", "same"]),
            lambda d: d["pages"][0]["blocks"][1].update(options={}),
            lambda d: d["pages"][0]["blocks"][1].update(correctIndex=True),
            lambda d: d["pages"][0]["blocks"][1].update(correctIndex=2),
            lambda d: d["pages"][0]["blocks"][2].update(correctOrder=[0, 0]),
            lambda d: d["pages"][0]["blocks"][3].update(answers=[""]),
            lambda d: d["pages"][0]["blocks"][4].update(answers={}),
            lambda d: d["pages"][0]["blocks"][5].update(answers=["n", "m"]),
            lambda d: d["pages"][0]["blocks"][6].update(correctOrder=[9]),
        )
        for index, mutate in enumerate(changes):
            with self.subTest(mutation=index):
                self.select_course("future_programming_2040")
                self.complete_lesson()
                mutate(self.lesson)
                self.save()
                output = self.run_course(reject=True)
                self.assertIn(self.course_id, output)
                self.assertNotIn("Traceback", output)

    def test_new_course_reuses_compiler_project_and_oracle_guards(self):
        changes = (
            (lambda b: b.update(initialCode=3), "initialCode"),
            (lambda b: b.update(sourceFiles=["missing.cpp"]), "fuente no existe"),
            (lambda b: b["projectFiles"][0].update(path="../main.cpp"), "ruta"),
            (lambda b: b["outputCheck"].update(cases=[]), "cases"),
            (lambda b: b["outputCheck"]["cases"][0].update(outputs=[]), "outputs"),
            (lambda b: b["outputCheck"]["cases"][0].update(exitCode=True), "exitCode"),
        )
        for mutate, expected in changes:
            with self.subTest(expected=expected):
                self.select_course("cpp_language_memory_oop")
                self.complete_lesson()
                mutate(self.lesson["pages"][0]["blocks"][7])
                self.save()
                self.assertIn(expected, self.run_course(reject=True))

    def test_future_course_reuses_real_image_resource_checks(self):
        self.select_course("future_programming_2040")
        self.complete_lesson()
        image = self.root / "app/src/main/assets/content/images/fixture_pixel.png"
        image.unlink()
        output = self.run_course(reject=True)
        self.assertIn("imagen curricular no válida", output)
        self.assertIn("ausente", output)
        self.lesson["pages"][0]["blocks"][9]["src"] = "../fixture_pixel.png"
        self.save()
        self.assertIn("image.src", self.run_course(reject=True))

    def test_new_course_reuses_relationship_and_state_identity_validation(self):
        self.select_course("cpp_language_memory_oop")
        self.complete_lesson()
        self.lesson = layout.prepare(self.lesson)
        self.lesson["referenceAfter"] = [{"activity": "block_0000_0007", "blocks": ["block_0000_0008"]}]
        self.save()
        self.run_course()
        self.lesson["referenceAfter"][0]["blocks"] = ["unknown_owner"]
        self.save()
        self.assertIn("relación de presentación inválida", self.run_course(reject=True))
        self.lesson.pop("referenceAfter")
        self.lesson["stateLayout"]["origin"][0]["blocks"][1]["sha256"] = "0" * 64
        self.save()
        self.assertIn("identidad de estado", self.run_course(reject=True))

    def test_global_family_conservation_gate_runs_before_accepting_a_new_course(self):
        self.select_course("cpp_language_memory_oop")
        output = self.run_course(reject=True, state_error=layout.LayoutError("family provenance changed"))
        self.assertIn("Conservación de estado no verificada", output)
        self.assertIn("family provenance changed", output)
        self.assertNotIn("Contrato, referencias y posiciones previas correctos", output)

    def test_authenticated_editorial_revision_is_generic_and_preserves_its_family(self):
        for course_id in ("cpp_language_memory_oop", "future_programming_2040"):
            with self.subTest(course=course_id):
                self.select_course(course_id)
                previous = layout.prepare_v2(self.lesson, "es", {"es": self.lesson})
                identity = previous["stateLayout"]["current"][0]["blocks"][1]
                current = layout.revise_ordinary_editorial(previous, identity, {"explanation": {
                    "text": "El acuerdo exige la primera respuesta; la segunda no lo cumple.", "spans": []}})
                layout.validate_transition(previous, current)
                self.assertEqual(previous["stateLayout"]["family"], current["stateLayout"]["family"])
                relative = self.lesson_path.relative_to(self.root).as_posix()
                old, new = layout._blocks(previous)[identity], layout._blocks(current)[identity]
                with mock.patch.object(validator, "ROOT", self.root), mock.patch.object(validator, "CONTENT", self.cpp):
                    self.assertTrue(validator.preserves_previous_exercise(relative, 0, 1, old, new,
                        previous_document=previous, current_document=current, course_id=course_id))
                    for mutation in ("family", "owner", "descriptor", "answer"):
                        bad = deepcopy(current)
                        if mutation == "family":
                            bad["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                        elif mutation == "owner":
                            bad["stateLayout"]["editorial"][0]["id"] = "block_0000_0000"
                        elif mutation == "descriptor":
                            bad["textSemantics"]["fields"] = []
                        else:
                            layout._blocks(bad)[identity]["correctIndex"] = 1
                        self.assertFalse(validator.preserves_previous_exercise(relative, 0, 1, old,
                            layout._blocks(bad)[identity], previous_document=previous,
                            current_document=bad, course_id=course_id), mutation)

    def test_future_courses_do_not_inherit_programming1_feedback_relaxations(self):
        for course_id in (COURSE, "cpp_language_memory_oop", "future_programming_2040"):
            with self.subTest(course=course_id):
                self.select_course(course_id)
                old = self.lesson["pages"][0]["blocks"][1]
                new = dict(old, reviewFeedback=[{"option": 1, "text": "Guía sin publicación."}])
                relative = self.lesson_path.relative_to(self.root).as_posix()
                with mock.patch.object(validator, "ROOT", self.root), mock.patch.object(validator, "CONTENT", self.cpp):
                    self.assertTrue(validator.preserves_previous_exercise(relative, 0, 1, old, deepcopy(old), course_id=course_id))
                    self.assertFalse(validator.preserves_previous_exercise(relative, 0, 1, old, new, course_id=course_id))
        legacy_relative = "app/src/main/assets/content/es/programming/cpp_fundamentals/fixture/lesson_fixture.json"
        with mock.patch.object(validator, "ROOT", self.root), mock.patch.object(validator, "CONTENT", self.cpp):
            self.assertTrue(validator.preserves_previous_exercise(legacy_relative, 0, 1, old, new,
                course_id="cpp_fundamentals"))


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


class PublishedU1L4OptionalExplanationGuard(unittest.TestCase):
    """Real U1L4: add feedback to a published drag-fill without changing credit.

    The immutable Codex ancestor is the predecessor under test. Work may later
    integrate different author-written wording without changing this proof.
    No test edits the installed curriculum or an actual learner profile.
    """
    BASE = "9a7b9e8540856e4ac60e117efce2053f1183a79e"
    RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json")
    OWNER = "block_0004_0001"
    TEXT = ("main inicia el programa; << envía Revision lista a la salida y "
            "el punto y coma termina esa instrucción.")

    @classmethod
    def setUpClass(cls):
        cls.path = validator.ROOT / cls.RELATIVE
        if not (validator.ROOT / ".git").exists():
            raise unittest.SkipTest("La regresión publicada requiere el historial Git completo")
        cls.previous, _ = layout.published_family(cls.path, "es", cls.BASE)
        cls.visible = [(p, b) for p, row in enumerate(
            cls.previous["stateLayout"]["current"]) for b, owner in enumerate(
            row["blocks"]) if owner == cls.OWNER]
        if cls.visible != [(5, 8)]:
            raise AssertionError("El propietario U1L4 dejó de ocupar su ranura histórica")
        cls.position = layout.validate(cls.previous)[cls.visible[0]]
        if cls.position != (4, 1):
            raise AssertionError("El propietario U1L4 perdió su coordenada original")
        p, b = cls.visible[0]
        cls.old = cls.previous["pages"][p]["blocks"][b]
        if (cls.old.get("type") != "drag_fill_code" or
                cls.old.get("id") != "piezas_estructura" or
                "explanation" in cls.old or cls.old.get("correctOrder") != [0, 1, 2]):
            raise AssertionError("La actividad histórica U1L4 no coincide con la publicada")

    def reviewed(self, *, spans=None):
        return layout.revise_ordinary_editorial(
            self.previous, self.OWNER, {"explanation": {
                "text": self.TEXT, "spans": [] if spans is None else spans}})

    def accepts(self, revised):
        return validator.preserves_previous_exercise(
            self.RELATIVE, *self.position, self.old,
            layout._blocks(revised)[self.OWNER],
            previous_document=self.previous, current_document=revised,
            course_id="cpp_fundamentals")

    def test_new_explanation_preserves_grading_identity_and_old_question_marks(self):
        revised = self.reviewed()
        layout.validate_transition(self.previous, revised)
        self.assertTrue(self.accepts(revised))
        before = self.old
        after = layout._blocks(revised)[self.OWNER]
        self.assertEqual(set(after) - set(before), {"explanation"})
        self.assertEqual({key: after[key] for key in before}, before)
        self.assertEqual(layout.validate(self.previous), layout.validate(revised))
        self.assertEqual(self.previous["stateLayout"]["origin"],
                         revised["stateLayout"]["origin"])
        self.assertEqual(self.previous["stateLayout"]["family"],
                         revised["stateLayout"]["family"])
        self.assertEqual(self.previous["stateLayout"]["current"],
                         revised["stateLayout"]["current"])
        self.assertEqual(revised["stateLayout"]["editorial"][:-1],
                         self.previous["stateLayout"]["editorial"])
        self.assertEqual(revised["stateLayout"]["editorial"][-1]["id"], self.OWNER)
        previous_markers = {(m["block"], m["path"]): m
                            for m in self.previous["textSemantics"]["fields"]}
        current_markers = {(m["block"], m["path"]): m
                           for m in revised["textSemantics"]["fields"]}
        new_key = (self.OWNER, "/explanation")
        self.assertNotIn(new_key, previous_markers)
        self.assertEqual(current_markers[new_key]["spans"], [])
        self.assertEqual(set(current_markers) - set(previous_markers), {new_key})
        for key, marker in previous_markers.items():
            self.assertEqual(marker, current_markers[key])

    def test_authored_utf16_explanation_and_wrong_owner_or_contract_are_rejected(self):
        marked = self.reviewed(spans=[
            layout.text_semantics.select(self.TEXT, "Revision lista", "output",
                                         reading="natural")])
        self.assertTrue(self.accepts(marked))
        self.assertEqual(len(next(m for m in marked["textSemantics"]["fields"]
                                  if (m["block"], m["path"]) ==
                                     (self.OWNER, "/explanation"))["spans"]), 1)

        for kind in ("unproven-prose", "missing-descriptor", "stale-source-hash",
                     "changed-question-mark", "changed-answer", "changed-choices",
                     "changed-template", "changed-owner", "changed-question"):
            candidate = deepcopy(marked)
            item = layout._blocks(candidate)[self.OWNER]
            if kind == "unproven-prose":
                candidate["stateLayout"]["editorial"].pop()
            elif kind == "missing-descriptor":
                candidate["textSemantics"]["fields"] = [
                    m for m in candidate["textSemantics"]["fields"]
                    if (m["block"], m["path"]) != (self.OWNER, "/explanation")]
            elif kind == "stale-source-hash":
                next(m for m in candidate["textSemantics"]["fields"]
                     if (m["block"], m["path"]) ==
                        (self.OWNER, "/explanation"))["sourceSha256"] = "0" * 64
            elif kind == "changed-question-mark":
                next(m for m in candidate["textSemantics"]["fields"]
                     if (m["block"], m["path"]) ==
                        (self.OWNER, "/question"))["spans"][0]["role"] = "prose"
            elif kind == "changed-answer":
                item["correctOrder"] = [2, 1, 0]
            elif kind == "changed-choices":
                item["choices"][1] = ">";
            elif kind == "changed-template":
                item["template"] += "\n"
            elif kind == "changed-owner":
                item["id"] = "otro_propietario"
            else:
                item["question"] += " Reescrita."
            with self.subTest(case=kind):
                self.assertFalse(self.accepts(candidate),
                    "No se debe admitir un cambio encubierto bajo una explicación")

        self.assertFalse(validator.preserves_previous_exercise(
            self.RELATIVE, 4, 2, self.old,
            layout._blocks(marked)[self.OWNER],
            previous_document=self.previous, current_document=marked,
            course_id="cpp_fundamentals"))
        self.assertFalse(validator.preserves_previous_exercise(
            self.RELATIVE.replace("lesson_fuente_", "lesson_otra_fuente_"),
            *self.position, self.old,
            layout._blocks(marked)[self.OWNER],
            previous_document=self.previous, current_document=marked,
            course_id="cpp_fundamentals"))


if __name__ == "__main__":
    unittest.main(verbosity=2)

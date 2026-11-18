"""Focused Windows authoring/publication checks for explicit course editions.

No student profile, existing course asset or remote service is modified.
Temporary fixture trees are created under the caller's QA working directory.
"""
from copy import deepcopy
from importlib import util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

SCRIPT = Path(__file__).with_name("course_edition.py")
SPEC = util.spec_from_file_location("edition_authoring", SCRIPT)
edition = util.module_from_spec(SPEC)
SPEC.loader.exec_module(edition)
state = edition._state_module()
CONTENT = edition.CONTENT_DIRECTORY
COURSE = "qa_edition"


def descriptor(generation=1, *, course=COURSE, locale="es"):
    return {"version": 1, "courseId": course, "locale": locale,
            "id": f"qa-edition-{generation}", "generation": generation}


def lesson_source(value, *, answer=0):
    return {"id": edition.unit_prefix(value) + "inicio__primera", "title": "Primera lección",
            "pages": [{"blocks": [{"type": "paragraph", "text": "Una explicación nueva."}]},
                      {"blocks": [{"type": "multiple_choice", "id": "elegir",
                                   "question": "Elige la respuesta actual.", "options": ["A", "B"],
                                   "correctIndex": answer, "explanation": "La respuesta indicada."}]}]}


def current_documents(value=None):
    if value is None:
        course_id, locale, suffix = COURSE, "es", "original"
        unit_id = course_id + "__" + suffix
        lesson = {"id": unit_id + "__primera", "title": "Primera lección",
                  "pages": [{"blocks": [{"type": "multiple_choice", "id": "anterior",
                                        "question": "Objetivo anterior", "options": ["Sí", "No"],
                                        "correctIndex": 0, "explanation": "Sí."}]}]}
    else:
        course_id, locale = value["courseId"], value["locale"]
        unit_id = edition.unit_prefix(value) + "inicio"
        suffix = unit_id[len(course_id) + 2:]
        lesson = edition.prepare_edition_lesson(lesson_source(value, answer=value["generation"] % 2), value)
    course = {"id": course_id, "title": "Curso de comprobación",
              "units": [{"id": unit_id, "number": 1, "title": "Inicio"}]}
    unit = {"courseId": course_id, "unitId": unit_id, "title": "Inicio",
            "lessons": [{"id": lesson["id"], "number": 1, "title": lesson["title"]}]}
    if value is not None:
        course["courseEdition"] = deepcopy(value)
        unit["courseEdition"] = deepcopy(value)
    prefix = f"{CONTENT}/{locale}/programming/{course_id}"
    return {f"{prefix}/course_{course_id}.json": course,
            f"{prefix}/{suffix}/unit_{suffix}.json": unit,
            f"{prefix}/{suffix}/lesson_primera.json": lesson}


def encoded(documents):
    return {path: (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
            for path, document in documents.items()}


def first_delivery(document):
    relative = f'{CONTENT}/es/programming/{COURSE}/edition_es_1__inicio/lesson_primera.json'
    family = relative.removeprefix(CONTENT + "/es/")
    state._validate_family_baseline(Path.cwd(), "0" * 40, family, [(relative, document)],
                                    set(), {}, {document["id"]: {family}})


class CourseEditionAuthoringTest(unittest.TestCase):
    def check_tree(self, prior, current):
        before = encoded(prior)
        with tempfile.TemporaryDirectory(prefix="course-edition-qa-", dir=Path.cwd()) as temporary:
            root = Path(temporary)
            for relative, raw in encoded(current).items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(raw)
            with mock.patch.object(edition, "_paths_at", return_value=set(before)), \
                    mock.patch.object(edition, "_git_bytes", side_effect=lambda _, __, path: before[path]):
                result = edition.check_repository(root, "0" * 40)
            actual = {path.relative_to(root).as_posix(): path.read_bytes()
                      for path in root.rglob("*.json")}
            self.assertEqual(encoded(current), actual, "The read-only publication gate changed source files")
            return result

    def test_source_is_a_literal_new_family_without_legacy_maps(self):
        value = descriptor()
        source = lesson_source(value)
        source["pages"][1]["blocks"] = [{"type": "cpp_compiler", "initialCode": "int main() {}",
                                        "outputCheck": {"cases": [{"standardInput": "", "outputs": [""]}]}}]
        source["referenceAfter"] = [{"activity": "block_0001_0000", "blocks": ["block_0002_0000"]}]
        source["pages"].append({"blocks": [{"type": "paragraph", "text": "La comparación posterior."}]})
        original = deepcopy(source)
        prepared = edition.prepare_edition_lesson(source, value)
        self.assertEqual(original, source)
        self.assertEqual({"es"}, set(prepared["stateLayout"]["family"]))
        self.assertEqual([], prepared["stateLayout"]["additions"])
        self.assertEqual([], prepared["stateLayout"]["editorial"])
        self.assertEqual("page_0000", prepared["stateLayout"]["current"][0]["id"])
        self.assertEqual(["block_0002_0000"], prepared["stateLayout"]["current"][2]["blocks"])
        first_delivery(prepared)
        state.validate(prepared)
        with self.assertRaisesRegex(ValueError, "earlier stateLayout"):
            edition.prepare_edition_lesson(prepared, value)

    def test_first_family_rejects_a_forged_source_digest(self):
        prepared = edition.prepare_edition_lesson(lesson_source(descriptor()), descriptor())
        prepared["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "exact delivered source"):
            first_delivery(prepared)

    def test_legacy_to_first_edition_keeps_other_locales_courses_and_old_assets(self):
        old = current_documents()
        other = current_documents(descriptor(course="qa_other"))
        # A pre-existing independent edition is not part of this reset.
        translations = {}
        for locale in ("en", "fr"):
            for path, document in current_documents().items():
                translations[path.replace("/es/", f"/{locale}/")] = deepcopy(document)
        prior = {**old, **translations, **other}
        after = {**deepcopy(prior), **current_documents(descriptor())}
        result = self.check_tree(prior, after)
        self.assertEqual([f"{CONTENT}/es/programming/{COURSE}/course_{COURSE}.json"],
                         result["course_edition_boundaries"])
        self.assertEqual(4, len(result["active_edition_resources"]))
        for path, document in prior.items():
            if not path.endswith(f"/{COURSE}/course_{COURSE}.json") or "/es/" not in path:
                self.assertEqual(document, after[path])

    def test_second_edition_keeps_the_entire_first_edition_outside_active_references(self):
        first = {**current_documents(), **current_documents(descriptor())}
        second = {**deepcopy(first), **current_documents(descriptor(2))}
        result = self.check_tree(first, second)
        self.assertEqual(1, len(result["course_edition_boundaries"]))
        self.assertTrue(all("/edition_es_2__" in path for path in result["active_edition_resources"]))
        self.assertTrue(any("/edition_es_1__" in path for path in second))
        self.check_tree(second, deepcopy(second))  # Reopening is not another reset.

    def test_rollback_skips_reuse_and_removal_are_rejected(self):
        previous = next(document for path, document in current_documents(descriptor(2)).items()
                        if "/course_" in path)
        for value in (descriptor(1), descriptor(4), dict(descriptor(2), id="renamed"),
                      dict(descriptor(3), id=descriptor(2)["id"])):
            current = dict(previous, courseEdition=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                edition.validate_transition(previous, current)
        no_edition = {key: value for key, value in previous.items() if key != "courseEdition"}
        with self.assertRaisesRegex(ValueError, "cannot be removed"):
            edition.validate_transition(previous, no_edition)
        self.assertFalse(edition.validate_transition(previous, deepcopy(previous)))

    def test_index_only_activation_cannot_reuse_legacy_units(self):
        prior = current_documents()
        current = deepcopy(prior)
        course = current[f"{CONTENT}/es/programming/{COURSE}/course_{COURSE}.json"]
        course["courseEdition"] = descriptor()
        with self.assertRaisesRegex(ValueError, "new course/locale/generation namespace"):
            self.check_tree(prior, current)

    def test_mixed_descriptors_missing_maps_and_retired_references_are_rejected(self):
        prior = current_documents()
        complete = {**deepcopy(prior), **current_documents(descriptor())}
        unit_path = next(path for path in complete if "/edition_es_1__" in path and "/unit_" in path)
        lesson_path = next(path for path in complete if "/edition_es_1__" in path and "/lesson_" in path)
        mutations = []
        mixed = deepcopy(complete)
        mixed[unit_path]["courseEdition"] = descriptor(2)
        mutations.append(mixed)
        missing_map = deepcopy(complete)
        del missing_map[lesson_path]["stateLayout"]
        mutations.append(missing_map)
        missing_descriptor = deepcopy(complete)
        del missing_descriptor[lesson_path]["courseEdition"]
        mutations.append(missing_descriptor)
        old_reference = deepcopy(complete)
        old_reference[unit_path]["lessons"][0]["id"] = COURSE + "__original__primera"
        mutations.append(old_reference)
        for current in mutations:
            with self.subTest(current=current), self.assertRaises(ValueError):
                self.check_tree(prior, current)

    def test_inactive_sources_cannot_be_deleted_or_rewritten_at_the_boundary(self):
        prior = current_documents()
        after = {**deepcopy(prior), **current_documents(descriptor())}
        old_path = next(path for path in prior if "/lesson_" in path)
        for remove in (False, True):
            current = deepcopy(after)
            if remove:
                del current[old_path]
            else:
                current[old_path]["pages"][0]["blocks"][0]["correctIndex"] = 1
            with self.subTest(remove=remove), self.assertRaisesRegex(ValueError, "previous resources intact"):
                self.check_tree(prior, current)

    def test_unindexed_new_edition_resources_do_not_enter_the_publication(self):
        prior = current_documents()
        after = {**deepcopy(prior), **current_documents(descriptor())}
        unused = edition.prepare_edition_lesson(
            dict(lesson_source(descriptor()), id=edition.unit_prefix(descriptor()) + "inicio__sobrante"), descriptor())
        after[f"{CONTENT}/es/programming/{COURSE}/edition_es_1__inicio/lesson_sobrante.json"] = unused
        with self.assertRaisesRegex(ValueError, "active or an unchanged historical"):
            self.check_tree(prior, after)

    def test_generation_does_not_weaken_answer_transitions(self):
        before = edition.prepare_edition_lesson(lesson_source(descriptor()), descriptor())
        after = deepcopy(before)
        after["pages"][1]["blocks"][0]["correctIndex"] = 1
        with self.assertRaises(ValueError):
            state.validate_transition(before, after)
        # Self-consistent regeneration cannot stand in for an authenticated
        # revision of this now-published new family.
        regenerated = edition.prepare_edition_lesson(lesson_source(descriptor(), answer=1), descriptor())
        with self.assertRaises(ValueError):
            state.validate_transition(before, regenerated)

    def test_individual_validation_requires_the_complete_v2_edition_map(self):
        source = dict(lesson_source(descriptor()), courseEdition=descriptor())
        with self.assertRaisesRegex(ValueError, "complete stateLayout v2"):
            state.validate(source)
        literal = edition.literal_layout(source, pages=state._pages, state_fingerprint=state.state_fingerprint)
        source["stateLayout"] = {"version": 1, **literal}
        with self.assertRaisesRegex(ValueError, "complete stateLayout v2"):
            state.validate(source)

    def test_same_generation_cannot_retire_active_units_or_lessons(self):
        value = descriptor()
        prior = {**current_documents(), **current_documents(value)}
        course_path = f"{CONTENT}/es/programming/{COURSE}/course_{COURSE}.json"
        unit_path = next(path for path in prior if "/edition_es_1__" in path and "/unit_" in path)
        first_unit = prior[unit_path]
        second_lesson = edition.prepare_edition_lesson(
            dict(lesson_source(value), id=edition.unit_prefix(value) + "inicio__segunda"), value)
        first_unit["lessons"].append({"id": second_lesson["id"], "number": 2, "title": "Segunda"})
        prior[unit_path.rsplit("/", 1)[0] + "/lesson_segunda.json"] = second_lesson
        second_unit_id = edition.unit_prefix(value) + "continuacion"
        second_unit_folder = second_unit_id[len(COURSE) + 2:]
        second_prefix = f"{CONTENT}/es/programming/{COURSE}/{second_unit_folder}"
        second_unit_lesson = edition.prepare_edition_lesson(
            dict(lesson_source(value), id=second_unit_id + "__primera"), value)
        prior[second_prefix + "/lesson_primera.json"] = second_unit_lesson
        prior[second_prefix + f"/unit_{second_unit_folder}.json"] = {
            "courseId": COURSE, "unitId": second_unit_id, "courseEdition": deepcopy(value),
            "title": "Continuación",
            "lessons": [{"id": second_unit_lesson["id"], "number": 1, "title": "Primera"}]}
        prior[course_path]["units"].append({"id": second_unit_id, "number": 2, "title": "Continuación"})
        for remove_unit in (False, True):
            current = deepcopy(prior)
            if remove_unit:
                current[course_path]["units"].pop()
            else:
                current[unit_path]["lessons"].pop()
            with self.subTest(remove_unit=remove_unit), self.assertRaisesRegex(
                    ValueError, "cannot leave its active index"):
                self.check_tree(prior, current)

    def test_edition_does_not_unlock_closed_legacy_compiler_diagnostics(self):
        source = lesson_source(descriptor())
        diagnostic = next(iter(state.COMPILE_DIAGNOSTICS.values()))["diagnostic"]
        source["pages"][1]["blocks"] = [{"type": "cpp_compiler", "initialCode": "int main() {}",
                                        "compileCheck": {"diagnostic": diagnostic}}]
        with self.assertRaisesRegex(ValueError, "exact complete initial block"):
            edition.prepare_edition_lesson(source, descriptor())

    def test_prepare_authenticates_one_new_source_while_other_lessons_are_still_drafts(self):
        prior = encoded(current_documents())
        current = {**current_documents(), **current_documents(descriptor())}
        unit_path = next(path for path in current if "/edition_es_1__" in path and "/unit_" in path)
        lesson_path = next(path for path in current if "/edition_es_1__" in path and "/lesson_" in path)
        current[lesson_path] = lesson_source(descriptor())
        extra_path = lesson_path.replace("lesson_primera.json", "lesson_segunda.json")
        current[extra_path] = dict(lesson_source(descriptor()),
                                  id=edition.unit_prefix(descriptor()) + "inicio__segunda")
        current[unit_path]["lessons"].append({"id": current[extra_path]["id"], "number": 2,
                                             "title": current[extra_path]["title"]})
        with tempfile.TemporaryDirectory(prefix="course-edition-authoring-qa-", dir=Path.cwd()) as temporary:
            root = Path(temporary)
            for relative, raw in encoded(current).items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(raw)
            def git(command, **_):
                return SimpleNamespace(returncode=0, stderr="",
                                       stdout=str(root) + "\n" if "--show-toplevel" in command else "0" * 40 + "\n")
            with mock.patch.object(edition, "_paths_at", return_value=set(prior)), \
                    mock.patch.object(edition, "_git_bytes", side_effect=lambda _, __, path: prior[path]), \
                    mock.patch.object(edition.subprocess, "run", side_effect=git):
                prepared = edition.prepare_from_repository(root / lesson_path, "0" * 40, locale="es")
                edition.validate_lesson(prepared, require_layout=True)
                with self.assertRaises(ValueError):
                    edition.check_repository(root, "0" * 40)
            actual = {path.relative_to(root).as_posix(): path.read_bytes() for path in root.rglob("*.json")}
            self.assertEqual(encoded(current), actual)

    def test_descriptor_and_namespace_reject_ambiguous_or_cross_scope_values(self):
        for value in (dict(descriptor(), version=True), dict(descriptor(), generation=True),
                      dict(descriptor(), version=1.0), dict(descriptor(), generation=1.0),
                      dict(descriptor(), generation=0), dict(descriptor(), id="legacy"),
                      dict(descriptor(), id="../other"), dict(descriptor(), locale="de")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                edition.descriptor({"courseEdition": value})
        for identity in (COURSE + "__original__primera",
                         edition.unit_prefix(descriptor()) + "inicio___primera",
                         edition.unit_prefix(descriptor()) + "inicio__primera__extra",
                         edition.unit_prefix(descriptor(2)) + "inicio__primera"):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                edition.prepare_edition_lesson(dict(lesson_source(descriptor()), id=identity), descriptor())
        with self.assertRaisesRegex(ValueError, "namespace requires"):
            state.validate(lesson_source(descriptor()))
        foreign = edition.prepare_edition_lesson(lesson_source(descriptor()), descriptor())
        foreign["stateLayout"]["family"]["en"] = deepcopy(foreign["stateLayout"]["family"]["es"])
        with self.assertRaisesRegex(ValueError, "only its explicitly authored locale"):
            state.validate(foreign)


class CourseEditionFixtureTreeTest(unittest.TestCase):
    """The CMake/Ninja fixture generator must not mistake empty output
    directories for foreign files, or overwrite any unrecognized QA data.
    """

    def test_ninja_byproduct_directories_are_safe_on_first_generation(self):
        import course_edition_fixture_source as fixture

        with tempfile.TemporaryDirectory(prefix="edition-byproducts-qa-") as base:
            root = Path(base) / "course-edition"
            files, _ = fixture.documents()
            # Ninja precreates the parents of all declared BYPRODUCTS before
            # invoking the Python producer. This is precisely the Windows CI
            # shape that failed with a missing manifest.
            for name in files:
                (root / name).parent.mkdir(parents=True, exist_ok=True)
            self.assertTrue(any(root.iterdir()))
            self.assertFalse((root / "manifest.json").exists())
            self.assertFalse(any(entry.is_file() for entry in root.rglob("*")))

            fixture.produce(root)
            manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["fixture"], fixture.MARKER)
            self.assertEqual(set(manifest["files"]), set(files))
            before = {name: (root / name).read_bytes() for name in files}
            self.assertEqual(set(manifest["files"]), set(before))
            fixture.produce(root)
            self.assertEqual({name: (root / name).read_bytes() for name in files}, before)
            self.assertEqual(json.loads((root / "manifest.json").read_text(encoding="utf-8")),
                             manifest, "A repeated fixture build silently changed the QA manifest")

    def test_preexisting_files_and_unrecognized_empty_directories_are_rejected(self):
        import course_edition_fixture_source as fixture

        files, _ = fixture.documents()
        inside = next(name for name in files if "/" in name)
        for unwanted in ("foreign-file", "expected-path-file", "foreign-directory"):
            with self.subTest(unwanted=unwanted), tempfile.TemporaryDirectory(
                    prefix="edition-foreign-qa-") as base:
                root = Path(base) / "course-edition"
                if unwanted == "foreign-file":
                    path = root / "student-notes.txt"
                    path.parent.mkdir(parents=True)
                    path.write_text("must be preserved", encoding="utf-8")
                elif unwanted == "expected-path-file":
                    path = root / inside
                    path.parent.mkdir(parents=True)
                    path.write_text("must be preserved", encoding="utf-8")
                else:
                    path = root / "unexpected"
                    path.mkdir(parents=True)
                with self.assertRaisesRegex(
                        ValueError, "no es un árbol de pruebas de edición reconocido"):
                    fixture.produce(root)
                self.assertTrue(path.exists())
                if path.is_file():
                    self.assertEqual(path.read_text(encoding="utf-8"), "must be preserved")
                self.assertFalse((root / "manifest.json").exists())

    def test_unrecognized_existing_manifest_is_not_replaced(self):
        import course_edition_fixture_source as fixture

        with tempfile.TemporaryDirectory(prefix="edition-marker-qa-") as base:
            root = Path(base) / "course-edition"
            root.mkdir()
            marker = root / "manifest.json"
            marker.write_text('{"fixture": "unrelated"}', encoding="utf-8")
            with self.assertRaisesRegex(
                    ValueError, "no es un árbol de pruebas de edición reconocido"):
                fixture.produce(root)
            self.assertEqual(json.loads(marker.read_text(encoding="utf-8"))["fixture"],
                             "unrelated")
            self.assertEqual([p.name for p in root.iterdir()], ["manifest.json"])


if __name__ == "__main__":
    unittest.main()

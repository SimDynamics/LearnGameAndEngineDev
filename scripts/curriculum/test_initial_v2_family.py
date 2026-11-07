"""Focused first-delivery provenance tests; no repository-wide run or file writes."""
from copy import deepcopy
from importlib import util
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock

SCRIPT = Path(__file__).with_name("stable_state_layout.py")
SPEC = util.spec_from_file_location("initial_family_layout", SCRIPT)
m = util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)
ROOT = SCRIPT.parents[2]
FAMILY = "programming/cpp_fundamentals/archivos_y_programa_completo/lesson_preparar_el_proyecto_windows.json"
INITIAL_DELIVERY = "c394316d80d76eaffd9ee9a29e3bc61e4e73d30d"


def minimal(locale="es"):
    return {"id": "qa_initial_family", "title": "Primera entrega" if locale == "es" else "First delivery",
            "pages": [{"blocks": [{"type": "paragraph", "text": "Fuente literal."},
                                  {"type": "multiple_choice", "id": "qa_choice", "question": "Elige",
                                   "options": ["A", "B"], "correctIndex": 0, "explanation": "A."}]}]}


def member(document, locale="es", family=FAMILY):
    return f"{m.CONTENT_DIRECTORY}/{locale}/{family}", document


def verify(members, prior=None, family=FAMILY, identity_families=None):
    prior = {} if prior is None else prior
    if identity_families is None:
        identity_families = {document["id"]: {family} for _, document in members}
    with mock.patch.object(m, "_git_document", side_effect=AssertionError("A focused fixture must provide its exact baseline")):
        m._validate_family_baseline(ROOT, "0" * 40, family, members, set(prior), deepcopy(prior), identity_families)


class InitialV2FamilyTest(unittest.TestCase):
    def test_published_l27_first_delivery_is_literal_with_exact_annotations(self):
        path = ROOT / m.CONTENT_DIRECTORY / "es" / FAMILY
        raw = path.read_bytes()
        # Work may legitimately revise the active lesson later. Authenticate
        # its actual first delivery, without exporting or replacing that asset.
        relative = path.relative_to(ROOT).as_posix()
        saved = subprocess.check_output(["git", "--no-replace-objects", "show", INITIAL_DELIVERY + ":" + relative],
            cwd=ROOT, env=dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1"))
        document = m.parse_json(saved.decode("utf-8-sig"))
        before = deepcopy(document)
        m.validate(document)
        self.assertEqual(document["stateLayout"]["editorial"], [])
        self.assertTrue(document["textSemantics"]["fields"])
        # The old unconditional requirement failed even though no source of
        # this family had been published; now its exact initial source proves it.
        prior_path = f"{m.CONTENT_DIRECTORY}/es/programming/qa/lesson_other.json"
        verify([member(document)], {prior_path: {"id": "unrelated_published_lesson"}})
        self.assertEqual(document, before)
        self.assertEqual(path.read_bytes(), raw)

    def test_two_new_languages_each_keep_their_literal_complete_map(self):
        sources = {locale: minimal(locale) for locale in ("es", "en")}
        documents = {locale: m.prepare_v2(source, locale, sources) for locale, source in sources.items()}
        verify([member(documents[locale], locale) for locale in sources])

    def test_published_family_still_requires_its_original_provenance(self):
        source = minimal()
        prepared = m.prepare_v2(source, "es", {"es": source})
        path, _ = member(source)
        verify([member(prepared)], {path: source})
        # Once the initial v2 delivery itself is published, that exact v2
        # family remains the anchor; a fictitious older v1 is never required.
        verify([member(prepared)], {path: prepared})
        forged = deepcopy(prepared)
        forged["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(m.LayoutError, "published baseline"):
            verify([member(forged)], {path: source})
        with self.assertRaisesRegex(m.LayoutError, "published language provenance was replaced"):
            verify([member(forged)], {path: prepared})
        # A new locale/path cannot masquerade as the first delivery when any
        # other locale of that same family already existed in the baseline.
        old_en = member(minimal("en"), "en")[0]
        with self.assertRaisesRegex(m.LayoutError, "absent from the published baseline"):
            verify([member(prepared)], {old_en: minimal("en")})

    def test_new_path_cannot_reuse_a_published_lesson_identity(self):
        source = minimal()
        prepared = m.prepare_v2(source, "es", {"es": source})
        prior = f"{m.CONTENT_DIRECTORY}/fr/other_route/lesson_old_name.json"
        with self.assertRaisesRegex(m.LayoutError, "reuse a published lesson identity"):
            verify([member(prepared)], {prior: {"id": source["id"]}})

    def test_two_new_families_cannot_share_an_identity_but_locales_can(self):
        source = minimal()
        prepared = m.prepare_v2(source, "es", {"es": source})
        other_family = "programming/other_route/lesson_other.json"
        duplicated = {source["id"]: {FAMILY, other_family}}
        for family in (FAMILY, other_family):
            with self.subTest(family=family), self.assertRaisesRegex(m.LayoutError, "another current family"):
                verify([member(prepared, family=family)], family=family, identity_families=duplicated)
        # The new guard deliberately does not rewrite historical duplicate
        # namespaces when checking a family that was already published.
        path, _ = member(prepared)
        verify([member(prepared)], {path: prepared}, identity_families=duplicated)
        sources = {locale: minimal(locale) for locale in ("es", "en")}
        documents = [member(m.prepare_v2(source, locale, sources), locale) for locale, source in sources.items()]
        verify(documents, identity_families={source["id"]: {FAMILY}})

    def test_new_family_rejects_forged_digest_source_or_nonliteral_initial_ids(self):
        source = minimal()
        prepared = m.prepare_v2(source, "es", {"es": source})
        changed_digest = deepcopy(prepared)
        changed_digest["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
        changed_source = deepcopy(prepared)
        changed_source["pages"][0]["blocks"][0]["text"] = "Texto no incluido en la fuente declarada."
        renamed = deepcopy(prepared)
        renamed["stateLayout"]["origin"][0]["blocks"][0]["id"] = "invented_original_owner"
        renamed["stateLayout"]["family"]["es"]["origin"][0]["blocks"][0]["id"] = "invented_original_owner"
        renamed["stateLayout"]["current"][0]["blocks"][0] = "invented_original_owner"
        plan = deepcopy(prepared["stateLayout"]["current"])
        plan[0]["blocks"].reverse()
        reordered = m.reorder(prepared, plan)
        for candidate in (changed_digest, changed_source, renamed, reordered):
            m.validate(candidate)  # These are internally coherent, not first-source proofs.
            with self.subTest(candidate=candidate), self.assertRaises(m.LayoutError):
                verify([member(candidate)])

    def test_new_family_does_not_invent_earlier_editorial_addition_or_retirement(self):
        source = minimal()
        prepared = m.prepare_v2(source, "es", {"es": source})
        edited = deepcopy(prepared)
        edited["pages"][0]["blocks"][0]["text"] = "Una revisión posterior legítima."
        editorial = m.prepare_v2(edited, "es", {"es": prepared}, previous=prepared)
        retired = m.retire(prepared, ["block_0000_0000"])
        plan = deepcopy(prepared["stateLayout"]["current"])
        plan[0]["blocks"].append("new_activity")
        added = m.reorder(prepared, plan, [{"id": "new_activity", "storage": {"page": 1, "block": 0},
            "progressPage": "page_0000", "block": {"type": "paragraph", "text": "Un añadido posterior."}}])
        for candidate in (editorial, retired, added):
            m.validate(candidate)
            with self.subTest(kind=set(candidate["stateLayout"])), self.assertRaisesRegex(m.LayoutError, "earlier additions"):
                verify([member(candidate)])
        for field in ("orderRefinements", "choiceRevisions", "compilerRevisions"):
            candidate = deepcopy(prepared)
            candidate["stateLayout"][field] = [{"id": "invented"}]
            with self.subTest(field=field), self.assertRaises(m.LayoutError):
                verify([member(candidate)])

    def test_missing_language_or_partial_new_map_is_not_initial_provenance(self):
        sources = {locale: minimal(locale) for locale in ("es", "en")}
        prepared = m.prepare_v2(sources["es"], "es", sources)
        with self.assertRaises(m.LayoutError):
            verify([member(prepared)])
        with self.assertRaisesRegex(m.LayoutError, "complete v2 map"):
            verify([member(prepared), member(sources["en"], "en")])


if __name__ == "__main__":
    unittest.main()

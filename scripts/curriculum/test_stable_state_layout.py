from copy import deepcopy
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('stable_state_layout', Path(__file__).with_name('stable_state_layout.py'))
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


def fixture():
    return {
        'id': 'cpp_fundamentals__programas_y_ejecucion__fixture', 'title': 'Fixture',
        'pages': [
            {'title': 'A', 'blocks': [{'type': 'paragraph', 'text': 'Teoría A'},
                                    {'type': 'output_prediction', 'id': 'a', 'answers': ['Zona inicial']}]},
            {'title': 'B', 'blocks': [{'type': 'paragraph', 'text': 'Teoría B'},
                                    {'type': 'cpp_compiler', 'initialCode': 'SOURCE', 'initialStandardInput': 'Rita 4'}]},
            {'title': 'C', 'blocks': [{'type': 'math_puzzle', 'id': 'm', 'responses': [{'id':'r','validator': {'value':2}}]}]},
        ]
    }


class StateLayoutTest(unittest.TestCase):
    def test_bootstrap_preserves_source(self):
        source = fixture(); before = deepcopy(source)
        new = m.prepare(source)
        self.assertEqual(source, before)
        self.assertEqual(new['pages'], source['pages'])
        self.assertEqual(m.validate(new), m.validate(source))

    def test_bootstrap_is_idempotent(self):
        one = m.prepare(fixture())
        self.assertEqual(m.prepare(one), one)

    def test_swap_editor_prediction_without_crossing_their_state_keys(self):
        source = m.prepare(fixture())
        plan = deepcopy(source['stateLayout']['current'])
        plan[0]['blocks'][1], plan[1]['blocks'][1] = plan[1]['blocks'][1], plan[0]['blocks'][1]
        moved = m.reorder(source, plan)
        self.assertEqual(m.validate(moved)[0, 1], (1, 1))
        self.assertEqual(m.validate(moved)[1, 1], (0, 1))
        m.validate_transition(fixture(), moved)

    def test_editor_without_curricular_id_gets_stable_identity(self):
        source = m.prepare(fixture())
        self.assertNotIn('id', source['pages'][1]['blocks'][1])
        self.assertIn((1, 1), m.validate(source))

    def test_math_block_remains_byte_equivalent_as_json_payload(self):
        import json
        source = m.prepare(fixture())
        before = json.dumps(source['pages'][2]['blocks'][0])
        moved = m.reorder(source, list(reversed(source['stateLayout']['current'])))
        self.assertEqual(json.dumps(moved['pages'][0]['blocks'][0]), before)

    def test_math_key_reordering_cannot_invalidate_the_legacy_runtime_fingerprint(self):
        original = fixture()
        moved = m.reorder(m.prepare(original), list(reversed(m.prepare(original)['stateLayout']['current'])))
        block = moved['pages'][0]['blocks'][0]
        moved['pages'][0]['blocks'][0] = dict(reversed(list(block.items())))
        m.validate(moved)  # Canonical state hashes alone cannot detect this regression.
        with self.assertRaisesRegex(m.LayoutError, 'historical JSON key order'):
            m.validate_transition(original, moved)

    def test_nested_math_key_reordering_is_rejected_even_without_a_visible_move(self):
        original = m.prepare(fixture())
        revised = deepcopy(original)
        response = revised['pages'][2]['blocks'][0]['responses'][0]
        revised['pages'][2]['blocks'][0]['responses'][0] = dict(reversed(list(response.items())))
        m.validate(revised)
        with self.assertRaisesRegex(m.LayoutError, 'historical JSON key order'):
            m.validate_transition(original, revised)

    def test_skipped_intermediate_releases_and_repeated_moves_keep_original_slots(self):
        source = m.prepare(fixture())
        moved = m.reorder(source, list(reversed(source['stateLayout']['current'])))
        twice = m.reorder(moved, [moved['stateLayout']['current'][1], moved['stateLayout']['current'][0],
                                 moved['stateLayout']['current'][2]])
        m.validate_transition(fixture(), twice)
        self.assertEqual(twice['stateLayout']['origin'], source['stateLayout']['origin'])

    def test_old_validator_receives_original_positions_after_verified_reorder(self):
        source = fixture()
        prepared = m.prepare(source)
        moved = m.reorder(prepared, list(reversed(prepared['stateLayout']['current'])))
        aligned = m.align_for_previous_validation(source, moved)
        self.assertEqual(aligned['pages'], source['pages'])

    def test_old_validator_still_has_no_positional_relaxation_without_manifest(self):
        source = fixture(); moved = deepcopy(source)
        moved['pages'].reverse()
        self.assertIs(m.align_for_previous_validation(source, moved), moved)
        self.assertNotEqual(moved['pages'], source['pages'])

    def test_changed_correct_answer_is_rejected(self):
        source = m.prepare(fixture())
        source['pages'][0]['blocks'][1]['answers'] = ['other']
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_changed_editor_source_is_rejected(self):
        source = m.prepare(fixture())
        source['pages'][1]['blocks'][1]['initialCode'] = 'OTHER'
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_changed_block_under_existing_id_is_rejected(self):
        source = m.prepare(fixture())
        source['pages'][0]['blocks'][1], source['pages'][1]['blocks'][1] = source['pages'][1]['blocks'][1], source['pages'][0]['blocks'][1]
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_origin_rewrite_cannot_hide_new_answer_from_transition_check(self):
        original = m.prepare(fixture()); forged = deepcopy(original)
        forged['pages'][0]['blocks'][1]['answers'] = ['forged']
        forged['stateLayout']['origin'][0]['blocks'][1]['sha256'] = m.state_fingerprint(forged['pages'][0]['blocks'][1])
        m.validate(forged)  # Local integrity alone is not proof of a safe transition.
        with self.assertRaises(m.LayoutError): m.validate_transition(original, forged)

    def test_reinitializing_origins_after_reorder_is_rejected(self):
        source = m.prepare(fixture()); moved = m.reorder(source, list(reversed(source['stateLayout']['current'])))
        del moved['stateLayout']; forged = m.prepare(moved)
        with self.assertRaises(m.LayoutError): m.validate_transition(source, forged)

    def test_theory_only_origin_cannot_be_regenerated_after_swapping_equal_shape_pages(self):
        original = {'id':'theory', 'pages': [
            {'blocks':[{'type':'paragraph', 'text':'First explanation'}]},
            {'blocks':[{'type':'paragraph', 'text':'Second explanation'}]}]}
        source = m.prepare(original)
        swapped = deepcopy(original); swapped['pages'].reverse()
        forged = m.prepare(swapped)
        # Prose is deliberately excluded from the persistent state signature.
        self.assertEqual(source['stateLayout']['origin'], forged['stateLayout']['origin'])
        with self.assertRaises(m.LayoutError): m.validate_transition(original, forged)
        with self.assertRaises(m.LayoutError): m.validate_transition(source, forged)

    def test_first_adoption_keeps_full_editorial_content_unambiguous(self):
        previous = fixture(); current = m.prepare(previous)
        current['pages'][0]['blocks'][0]['text'] = 'Changed while adopting'
        with self.assertRaisesRegex(m.LayoutError, 'preserve complete blocks'):
            m.validate_transition(previous, current)

    def test_move_and_editorial_revision_are_checked_in_separate_transitions(self):
        previous = m.prepare(fixture())
        moved = m.reorder(previous, list(reversed(previous['stateLayout']['current'])))
        revised = deepcopy(moved)
        revised['pages'][2]['blocks'][0]['text'] = 'Clearer wording after the move'
        with self.assertRaisesRegex(m.LayoutError, 'preserve complete blocks'):
            m.validate_transition(previous, revised)
        m.validate_transition(moved, revised)

    def test_removing_manifest_is_rejected(self):
        source = m.prepare(fixture())
        with self.assertRaises(m.LayoutError): m.validate_transition(source, fixture())

    def test_duplicate_block_identity_rejected(self):
        source = m.prepare(fixture())
        source['stateLayout']['current'][0]['blocks'][1] = source['stateLayout']['current'][0]['blocks'][0]
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_duplicate_origin_page_rejected(self):
        source = m.prepare(fixture()); source['stateLayout']['origin'][1]['id'] = source['stateLayout']['origin'][0]['id']
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_unknown_block_rejected(self):
        source = m.prepare(fixture()); source['stateLayout']['current'][0]['blocks'][0] = 'unregistered'
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_removed_block_is_not_silently_forgotten(self):
        source = m.prepare(fixture()); source['pages'][0]['blocks'].pop(); source['stateLayout']['current'][0]['blocks'].pop()
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_added_block_must_not_inherit_old_slot(self):
        source = m.prepare(fixture()); source['pages'][0]['blocks'].append({'type':'paragraph','text':'NEW'})
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_invalid_or_path_like_ids_rejected(self):
        for bad in ('../x', 'a/b', '', 'a\x00b', 'é', 'x'*129, 3):
            with self.subTest(bad=bad):
                source = m.prepare(fixture()); source['stateLayout']['current'][0]['id'] = bad
                with self.assertRaises(m.LayoutError): m.validate(source)

    def test_empty_or_future_version_fails(self):
        for version in (None, True, 1.0, 2, '1'):
            with self.subTest(version=version):
                source = m.prepare(fixture()); source['stateLayout']['version'] = version
                with self.assertRaises(m.LayoutError): m.validate(source)

    def test_null_manifest_is_not_a_legacy_lesson(self):
        source = fixture()
        source['stateLayout'] = None
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_unknown_metadata_field_fails(self):
        source = m.prepare(fixture()); source['stateLayout']['ignoreMismatch'] = True
        with self.assertRaises(m.LayoutError): m.validate(source)

    def test_object_key_order_does_not_change_fingerprint(self):
        self.assertEqual(m.fingerprint({'a':1, 'b':2}), m.fingerprint({'b':2, 'a':1}))

    def test_string_spaces_and_linebreaks_are_not_discarded(self):
        values = ['Caja recibida', 'Caja  recibida', 'Caja recibida\n', 'Caja recibida ']
        self.assertEqual(len({m.fingerprint(v) for v in values}), len(values))

    def test_numerical_canonicalization_without_conflating_boolean(self):
        self.assertEqual(m.fingerprint(1), m.fingerprint(1.0))
        self.assertNotEqual(m.fingerprint(1), m.fingerprint(True))
        self.assertEqual(m.fingerprint(-0.0), m.fingerprint(0))

    def test_translations_may_use_different_order_but_same_origins(self):
        spanish = fixture(); english = deepcopy(spanish)
        english['pages'][0]['blocks'][0]['text'] = 'Theory A'
        es = m.prepare(spanish); en = m.prepare(english)
        en = m.reorder(en, list(reversed(en['stateLayout']['current'])))
        m.check_localized_family([es, en])

    def test_translation_without_manifest_blocks_adoption(self):
        with self.assertRaises(m.LayoutError): m.check_localized_family([m.prepare(fixture()), fixture()])

    def test_misplaced_translation_activity_is_rejected(self):
        es = m.prepare(fixture()); other = fixture()
        other['pages'][0]['blocks'][1], other['pages'][1]['blocks'][1] = other['pages'][1]['blocks'][1], other['pages'][0]['blocks'][1]
        with self.assertRaises(m.LayoutError): m.check_localized_family([es, m.prepare(other)])

    def test_random_permutations_do_not_lose_any_address_or_mutate_content(self):
        rng = random.Random(90431)
        source = m.prepare(fixture()); moved = source
        for _ in range(200):
            plan = deepcopy(moved['stateLayout']['current']); rng.shuffle(plan)
            flat = [x for p in plan for x in p['blocks']]; rng.shuffle(flat)
            cursor = 0
            for page in plan:
                count = len(page['blocks']); page['blocks'] = flat[cursor:cursor+count]; cursor += count
            moved = m.reorder(moved, plan)
            m.validate_transition(source, moved)
            self.assertEqual(set(m.validate(moved).values()), set(m.validate(source).values()))

    def test_editorial_text_does_not_change_saved_response_identity(self):
        original = m.prepare(fixture()); revised = deepcopy(original)
        revised['pages'][0]['blocks'][0]['text'] = 'Clearer wording of the existing explanation'
        revised['pages'][0]['blocks'][1]['question'] = 'A clearer question'
        revised['pages'][0]['blocks'][1]['explanation'] = 'Corrected feedback'
        m.validate_transition(original, revised)
        self.assertEqual(m.validate(original), m.validate(revised))

    def test_math_response_contract_still_uses_full_payload(self):
        original = m.prepare(fixture()); revised = deepcopy(original)
        revised['pages'][2]['blocks'][0]['responses'][0]['validator']['value'] = 5
        with self.assertRaises(m.LayoutError): m.validate(revised)

    def test_output_check_expected_stdout_cannot_change(self):
        original = fixture()
        original['pages'][1]['blocks'][1]['outputCheck'] = {'cases':[{'standardInput':'','outputs':['Caja recibida'], 'explanation':'Feedback'}]}
        prepared = m.prepare(original)
        prepared['pages'][1]['blocks'][1]['outputCheck']['cases'][0]['explanation'] = 'Improved feedback'
        m.validate(prepared)
        prepared['pages'][1]['blocks'][1]['outputCheck']['cases'][0]['outputs'] = ['Wrong target']
        with self.assertRaises(m.LayoutError): m.validate(prepared)

    def test_duplicate_json_keys_are_rejected(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'bad.json'; path.write_text('{"id":"x", "id":"y"}')
            with self.assertRaises(m.LayoutError): m.load(path)

    def test_cli_does_not_overwrite_target(self):
        import json, subprocess, sys, tempfile
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'source.json'; source.write_text(json.dumps(fixture()))
            target = Path(folder)/'target.json'; target.write_text('KEEP')
            proc = subprocess.run([sys.executable, str(SPEC.origin), 'prepare', str(source),
                                   '--previous', str(source), '--output', str(target)], capture_output=True)
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(target.read_text(), 'KEEP')

    def test_cli_requires_published_previous_document(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'source.json'; source.write_text(json.dumps(fixture()))
            target = Path(folder)/'target.json'
            result = subprocess.run([sys.executable, str(SPEC.origin), 'prepare', str(source),
                                     '--output', str(target)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('--previous', result.stderr)
            self.assertFalse(target.exists())

    def test_cli_cannot_reinitialize_origin_after_reorder(self):
        with tempfile.TemporaryDirectory() as folder:
            previous = Path(folder)/'published.json'; previous.write_text(json.dumps(fixture()))
            changed = fixture(); changed['pages'].reverse()
            source = Path(folder)/'source.json'; source.write_text(json.dumps(changed))
            target = Path(folder)/'target.json'
            result = subprocess.run([sys.executable, str(SPEC.origin), 'prepare', str(source),
                                     '--previous', str(previous), '--output', str(target)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('immutable origin', result.stderr)
            self.assertFalse(target.exists())


class RepositoryGateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.git('init', '--quiet', '--initial-branch=codex-chat/app')
        self.files = {locale: self.root/m.CONTENT_DIRECTORY/locale/'programming/cpp_fundamentals/unit/lesson_fixture.json'
                      for locale in ('es', 'en', 'fr')}
        for path in self.files.values(): self.write(path, fixture())
        self.base = self.commit()

    def tearDown(self):
        self.temporary.cleanup()

    def git(self, *args):
        return subprocess.check_output(['git', '-c', 'user.name=State contract fixture',
                                        '-c', 'user.email=fixture@example.invalid', *args],
                                       cwd=self.root, text=True, encoding='utf-8').strip()

    def commit(self):
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'State migration fixture')
        return self.git('rev-parse', 'HEAD')

    def write(self, path, document):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(document, ensure_ascii=False), encoding='utf-8')

    def mapped(self, reordered=True):
        source = m.prepare(fixture())
        return m.reorder(source, list(reversed(source['stateLayout']['current']))) if reordered else source

    def adopt(self):
        for path in self.files.values(): self.write(path, self.mapped())

    def test_direct_upgrade_from_legacy_tree_needs_no_intermediate_release(self):
        self.adopt()
        report = m.check_repository(self.root, self.base)
        self.assertEqual(report, {'base_commit': self.base, 'mapped_lessons': 3, 'families': 1})
        for path in self.files.values():
            self.assertEqual(m.validate(m.load(path))[0, 0], (2, 0))

    def test_tree_without_identity_metadata_keeps_legacy_validation(self):
        self.assertEqual(m.check_repository(self.root, None)['mapped_lessons'], 0)

    def test_adoption_requires_explicit_published_base(self):
        self.adopt()
        with self.assertRaisesRegex(m.LayoutError, '--base-ref'):
            m.check_repository(self.root, None)

    def test_invalid_base_fails_instead_of_skipping_transition(self):
        self.adopt()
        with self.assertRaisesRegex(m.LayoutError, 'Cannot resolve'):
            m.check_repository(self.root, 'nonexistent-publication')

    def test_all_shipped_translations_must_adopt_together(self):
        for locale in ('es', 'en'): self.write(self.files[locale], self.mapped())
        with self.assertRaisesRegex(m.LayoutError, 'Every shipped translation'):
            m.check_repository(self.root, self.base)

    def test_current_locale_orders_may_differ_without_changing_original_keys(self):
        self.adopt()
        self.write(self.files['fr'], self.mapped(reordered=False))
        self.assertEqual(m.check_repository(self.root, self.base)['families'], 1)

    def test_regenerated_metadata_cannot_hide_reorder_even_after_local_commit(self):
        self.adopt()
        forged = self.mapped(); del forged['stateLayout']; forged = m.prepare(forged)
        for path in self.files.values(): self.write(path, forged)
        self.commit()
        with self.assertRaisesRegex(m.LayoutError, 'immutable origin'):
            m.check_repository(self.root, self.base)

    def test_repeated_reorder_can_skip_all_intermediate_releases(self):
        self.adopt()
        intermediate = self.commit()
        moved = self.mapped()
        moved = m.reorder(moved, [moved['stateLayout']['current'][1], moved['stateLayout']['current'][0],
                                 moved['stateLayout']['current'][2]])
        for path in self.files.values(): self.write(path, moved)
        self.assertEqual(m.check_repository(self.root, self.base)['mapped_lessons'], 3)
        self.assertEqual(m.check_repository(self.root, intermediate)['mapped_lessons'], 3)

    def test_removing_every_manifest_is_detected_from_previous_tree(self):
        self.adopt(); published = self.commit()
        for path in self.files.values(): self.write(path, fixture())
        with self.assertRaisesRegex(m.LayoutError, 'identity manifest'):
            m.check_repository(self.root, published)

    def test_removing_every_manifest_cannot_bypass_explicit_baseline_gate(self):
        self.adopt(); self.commit()
        for path in self.files.values(): self.write(path, fixture())
        with self.assertRaisesRegex(m.LayoutError, '--base-ref'):
            m.check_repository(self.root, None)

    def test_deleting_mapped_lessons_is_rejected(self):
        self.adopt(); published = self.commit()
        for path in self.files.values(): path.unlink()
        with self.assertRaisesRegex(m.LayoutError, 'mapped lesson was removed'):
            m.check_repository(self.root, published)

    def test_new_translation_cannot_reintroduce_positional_storage(self):
        self.files['fr'].unlink()
        for locale in ('es', 'en'): self.write(self.files[locale], self.mapped())
        published = self.commit()
        self.write(self.files['fr'], fixture())
        with self.assertRaisesRegex(m.LayoutError, 'Every shipped translation'):
            m.check_repository(self.root, published)

    def test_guard_covers_mathematics_and_other_programming_courses(self):
        for route in ('mathematics/algebra', 'programming/programming_practice_tools'):
            for locale in ('es', 'en', 'fr'):
                path = self.root/m.CONTENT_DIRECTORY/locale/route/'unit/lesson_fixture.json'
                self.write(path, fixture())
        previous = self.commit()
        for route in ('mathematics/algebra', 'programming/programming_practice_tools'):
            for locale in ('es', 'en', 'fr'):
                path = self.root/m.CONTENT_DIRECTORY/locale/route/'unit/lesson_fixture.json'
                self.write(path, self.mapped())
        self.assertEqual(m.check_repository(self.root, previous)['families'], 2)

    def test_cli_check_tree_runs_family_and_published_origin_checks(self):
        self.adopt()
        result = subprocess.run([sys.executable, str(SPEC.origin), 'check-tree', str(self.root),
                                 '--base-ref', self.base], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(self.base, result.stdout)
        self.assertIn('does not verify an Android build', result.stdout)

    def test_existing_validator_entrypoint_enforces_translation_family(self):
        import validar_json_programacion as contract
        self.write(self.files['es'], self.mapped())
        output = io.StringIO()
        with mock.patch.object(contract, 'ROOT', self.root), redirect_stderr(output):
            with self.assertRaises(SystemExit) as error:
                contract.main(['--base-ref', self.base])
        self.assertEqual(error.exception.code, 1)
        self.assertIn('Every shipped translation', output.getvalue())

    def test_existing_validator_entrypoint_requires_explicit_published_base(self):
        import validar_json_programacion as contract
        self.adopt()
        output = io.StringIO()
        with mock.patch.object(contract, 'ROOT', self.root), redirect_stderr(output):
            with self.assertRaises(SystemExit) as error:
                contract.main([])
        self.assertEqual(error.exception.code, 1)
        self.assertIn('--base-ref', output.getvalue())


def divergent_family():
    family = {language: deepcopy(fixture()) for language in ('es', 'en', 'fr')}
    for language in ('en', 'fr'):
        family[language]['pages'][0]['blocks'][1] = {'type': 'note', 'text': 'Historical translated note'}
    family['es']['pages'][2]['blocks'].append({'type': 'cpp_compiler', 'initialCode': 'INDEPENDENT SOURCE'})
    return family


def new_activity(identity='new_original_execution', page=3, block=0):
    return {'id': identity, 'storage': {'page': page, 'block': block}, 'progressPage': 'page_0001',
            'block': {'type': 'cpp_compiler', 'initialCode': 'NEW ORIGINAL SOURCE'}}


class ExtendedStateLayoutTest(unittest.TestCase):
    def prepared(self, family=None):
        family = family or divergent_family()
        return m.prepare_v2(family['es'], 'es', family)

    def add(self, source, addition=None):
        addition = addition or new_activity()
        plan = deepcopy(source['stateLayout']['current'])
        plan[1]['blocks'].insert(1, addition['id'])
        return m.reorder(source, plan, [addition])

    def test_divergent_history_is_preserved_without_editing_translations(self):
        family = divergent_family(); before = deepcopy(family)
        source = self.prepared(family)
        m.check_localized_family([source, family['en'], family['fr']], ['es', 'en', 'fr'])
        self.assertEqual(family, before)
        self.assertEqual(source['stateLayout']['origin'], m.prepare(family['es'])['stateLayout']['origin'])
        self.assertNotEqual(source['stateLayout']['family']['es']['origin'], source['stateLayout']['family']['en']['origin'])

    def test_editorial_revision_and_move_are_one_anchored_transition(self):
        family = divergent_family(); edited = deepcopy(family['es'])
        edited['pages'][0]['blocks'][0]['text'] = 'Clearer original explanation'
        edited['pages'][0]['blocks'][1]['question'] = 'An explicit revised prediction'
        prepared = m.prepare_v2(edited, 'es', family)
        moved = m.reorder(prepared, list(reversed(prepared['stateLayout']['current'])))
        m.validate_transition(family['es'], moved)
        self.assertEqual(m.validate(moved)[2, 1], (0, 1))
        self.assertEqual(len(moved['stateLayout']['editorial']), 2)

    def test_same_shaped_paragraphs_cannot_exchange_their_coverage(self):
        family = divergent_family(); edited = deepcopy(family['es'])
        edited['pages'][0]['blocks'][0], edited['pages'][1]['blocks'][0] = (
            edited['pages'][1]['blocks'][0], edited['pages'][0]['blocks'][0])
        with self.assertRaisesRegex(m.LayoutError, 'another identity'):
            m.prepare_v2(edited, 'es', family)

    def test_missing_editorial_proof_cannot_hide_revision_in_a_move(self):
        family = divergent_family(); source = self.prepared(family)
        moved = m.reorder(source, list(reversed(source['stateLayout']['current'])))
        moved['pages'][2]['blocks'][0]['text'] = 'Changed without provenance'
        with self.assertRaisesRegex(m.LayoutError, 'before/after proof'):
            m.validate_transition(family['es'], moved)

    def test_false_before_or_after_proof_is_rejected(self):
        family = divergent_family(); edited = deepcopy(family['es'])
        edited['pages'][0]['blocks'][0]['text'] = 'Actual revision'
        source = m.prepare_v2(edited, 'es', family)
        for field in ('beforeSha256', 'afterSha256'):
            with self.subTest(field=field):
                bad = deepcopy(source); bad['stateLayout']['editorial'][0][field] = '0' * 64
                with self.assertRaises(m.LayoutError): m.validate_transition(family['es'], bad)

    def test_repeated_editorial_proofs_support_skipping_an_intermediate_release(self):
        family = divergent_family(); one = self.prepared(family)
        edited = deepcopy(one); edited['pages'][0]['blocks'][0]['text'] = 'Revision one'
        two = m.prepare_v2(edited, 'es', family, one)
        edited = deepcopy(two); edited['pages'][0]['blocks'][0]['text'] = 'Revision two'
        three = m.prepare_v2(edited, 'es', family, two)
        m.validate_transition(family['es'], three)
        m.validate_transition(one, three)
        m.validate_transition(two, three)
        self.assertEqual(len(three['stateLayout']['editorial']), 2)

    def test_editorial_proofs_cannot_be_removed_reordered_or_cycled(self):
        family = divergent_family(); one = self.prepared(family)
        edited = deepcopy(one); edited['pages'][0]['blocks'][0]['text'] = 'Revision one'
        two = m.prepare_v2(edited, 'es', family, one)
        edited = deepcopy(two); edited['pages'][0]['blocks'][0]['text'] = 'Revision two'
        three = m.prepare_v2(edited, 'es', family, two)
        for change in ('remove', 'reverse', 'cycle'):
            with self.subTest(change=change):
                bad = deepcopy(three)
                if change == 'remove': bad['stateLayout']['editorial'].pop(0)
                if change == 'reverse': bad['stateLayout']['editorial'].reverse()
                if change == 'cycle':
                    bad['stateLayout']['editorial'][-1]['afterSha256'] = bad['stateLayout']['editorial'][0]['beforeSha256']
                with self.assertRaises(m.LayoutError): m.validate_transition(two, bad)

    def test_response_code_and_math_key_order_remain_protected_with_editorial_proof(self):
        family = divergent_family()
        mutations = (
            lambda d: d['pages'][0]['blocks'][1].update(answers=['OTHER']),
            lambda d: d['pages'][1]['blocks'][1].update(initialCode='OTHER SOURCE'),
            lambda d: d['pages'][2]['blocks'].__setitem__(0, dict(reversed(list(d['pages'][2]['blocks'][0].items())))),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                bad = deepcopy(family['es']); mutate(bad)
                with self.assertRaises(m.LayoutError): m.prepare_v2(bad, 'es', family)

    def test_new_activity_has_an_explicit_unoccupied_storage_slot(self):
        source = self.prepared(); added = self.add(source)
        self.assertEqual(m.validate(added)[1, 1], (3, 0))
        self.assertEqual(m.validate(added)[1, 2], (1, 1))
        self.assertEqual(added['stateLayout']['origin'], source['stateLayout']['origin'])
        self.assertEqual(added['stateLayout']['additions'][0]['sha256'], m.state_fingerprint(new_activity()['block']))

    def test_reservation_considers_the_largest_historical_translation(self):
        family = divergent_family()
        family['en']['pages'].append({'title': 'Only historical English', 'blocks': []})
        source = self.prepared(family)
        with self.assertRaisesRegex(m.LayoutError, 'outside every historical'):
            self.add(source, new_activity(page=3))
        self.assertEqual(m.validate(self.add(source, new_activity(page=4)))[1, 1], (4, 0))

    def test_new_slots_identity_and_contract_are_append_only(self):
        source = self.add(self.prepared())
        for change in ('slot', 'id', 'digest', 'remove'):
            with self.subTest(change=change):
                bad = deepcopy(source)
                if change == 'slot': bad['stateLayout']['additions'][0]['storage']['page'] = 40
                if change == 'id':
                    bad['stateLayout']['additions'][0]['id'] = 'renamed'
                    bad['stateLayout']['current'][1]['blocks'][1] = 'renamed'
                if change == 'digest':
                    bad['pages'][1]['blocks'][1]['initialCode'] = 'CHANGED'
                    bad['stateLayout']['additions'][0]['sha256'] = m.state_fingerprint(bad['pages'][1]['blocks'][1])
                if change == 'remove':
                    bad['stateLayout']['additions'].clear(); bad['stateLayout']['current'][1]['blocks'].pop(1)
                    bad['pages'][1]['blocks'].pop(1)
                with self.assertRaises(m.LayoutError): m.validate_transition(source, bad)

    def test_duplicate_new_slots_and_historical_ids_are_rejected(self):
        source = self.add(self.prepared())
        with self.assertRaises(m.LayoutError): self.add(source, new_activity('another', 3, 0))
        with self.assertRaises(m.LayoutError): self.add(self.prepared(), new_activity('block_0000_0000'))

    def test_new_activity_can_be_editorially_revised_without_losing_its_storage(self):
        family = divergent_family(); source = self.add(self.prepared(family))
        # Compiler feedback is editorial; its program and expected output stay fixed.
        original = new_activity('new_theory')
        original['block'] = {'type': 'paragraph', 'text': 'A newly authored explanation'}
        source = self.add(self.prepared(family), original)
        edited = deepcopy(source); edited['pages'][1]['blocks'][1]['text'] = 'A clearer new explanation'
        revised = m.prepare_v2(edited, 'es', family, source)
        m.validate_transition(source, revised)
        self.assertEqual(m.validate(revised)[1, 1], (3, 0))

    def test_old_validator_projection_keeps_only_historical_positions_but_source_contains_additions(self):
        family = divergent_family(); source = self.add(self.prepared(family))
        aligned = m.align_for_previous_validation(family['es'], source)
        self.assertEqual(aligned['pages'], family['es']['pages'])
        self.assertEqual(sum(len(p['blocks']) for p in source['pages']),
                         1 + sum(len(p['blocks']) for p in family['es']['pages']))
        moved = m.reorder(source, list(reversed(source['stateLayout']['current'])))
        self.assertEqual(m.original_projection(moved)['pages'], family['es']['pages'])

    def test_family_is_immutable_and_own_origin_cannot_be_borrowed(self):
        source = self.prepared()
        bad = deepcopy(source); bad['stateLayout']['family']['en']['sha256'] = '0' * 64
        with self.assertRaises(m.LayoutError): m.validate_transition(source, bad)
        bad = deepcopy(source); bad['stateLayout']['origin'] = bad['stateLayout']['family']['en']['origin']
        with self.assertRaises(m.LayoutError): m.validate(bad)

    def test_unmapped_translation_must_remain_exact_and_have_its_source_locale(self):
        family = divergent_family(); source = self.prepared(family)
        with self.assertRaises(m.LayoutError): m.check_localized_family([source, family['en'], family['fr']])
        bad = deepcopy(family['en']); bad['pages'][0]['blocks'][0]['text'] = 'Unanchored translation revision'
        with self.assertRaises(m.LayoutError):
            m.check_localized_family([source, bad, family['fr']], ['es', 'en', 'fr'])

    def test_each_mapped_translation_keeps_its_own_origin(self):
        family = divergent_family()
        documents = [m.prepare_v2(family[locale], locale, family) for locale in ('es', 'en', 'fr')]
        m.check_localized_family(documents)
        self.assertNotEqual(documents[0]['stateLayout']['origin'], documents[1]['stateLayout']['origin'])

    def test_new_localized_activities_cannot_alias_different_types_or_storage(self):
        family = divergent_family(); spanish = self.add(self.prepared(family))
        english = m.prepare_v2(family['en'], 'en', family)
        for change in ('type', 'slot', 'id'):
            with self.subTest(change=change):
                addition = new_activity()
                if change == 'type': addition['block'] = {'type': 'paragraph', 'text': 'Different ownership'}
                if change == 'slot': addition['storage']['block'] = 1
                if change == 'id': addition['id'] = 'different_new_activity'
                plan = deepcopy(english['stateLayout']['current']); plan[1]['blocks'].insert(1, addition['id'])
                bad = m.reorder(english, plan, [addition])
                with self.assertRaises(m.LayoutError):
                    m.check_localized_family([spanish, bad, family['fr']], ['es', 'en', 'fr'])

    def test_downgrade_and_unknown_fields_are_rejected(self):
        source = self.prepared()
        with self.assertRaises(m.LayoutError): m.validate_transition(source, m.prepare(divergent_family()['es']))
        for location in ('root', 'family', 'addition', 'editorial'):
            with self.subTest(location=location):
                bad = deepcopy(self.add(source))
                if location == 'root': bad['stateLayout']['ignoreMismatch'] = True
                if location == 'family': bad['stateLayout']['family']['en']['ignoreMismatch'] = True
                if location == 'addition': bad['stateLayout']['additions'][0]['ignoreMismatch'] = True
                if location == 'editorial':
                    bad['stateLayout']['editorial'] = [{'id':'block_0000_0000', 'beforeSha256':'0'*64,
                                                       'afterSha256':'1'*64, 'ignoreMismatch':True}]
                with self.assertRaises(m.LayoutError): m.validate(bad)

    def test_real_u1l2_family_keeps_34_spanish_and_29_translated_historical_blocks(self):
        root = Path(__file__).resolve().parents[2]
        suffix = 'programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json'
        family = {locale: m.load(root/m.CONTENT_DIRECTORY/locale/suffix) for locale in ('es', 'en', 'fr')}
        translated_bytes = {locale: (root/m.CONTENT_DIRECTORY/locale/suffix).read_bytes() for locale in ('en', 'fr')}
        self.assertEqual([sum(len(p['blocks']) for p in family[locale]['pages']) for locale in ('es', 'en', 'fr')],
                         [34, 29, 29])
        edited = deepcopy(family['es'])
        edited['pages'][4]['blocks'][1]['text'] += ' Verificación editorial aislada.'
        source = m.prepare_v2(edited, 'es', family)
        plan = deepcopy(source['stateLayout']['current'])
        plan[4]['blocks'][2], plan[4]['blocks'][3] = plan[4]['blocks'][3], plan[4]['blocks'][2]
        addition = new_activity(page=6)
        addition['progressPage'] = 'page_0004'
        plan[4]['blocks'].insert(3, addition['id'])
        moved = m.reorder(source, plan, [addition])
        positions = m.validate(moved)
        self.assertEqual(positions[4, 2], (4, 3))  # Existing prediction, not the translated note.
        self.assertEqual(positions[4, 3], (6, 0))  # Independent original execution, without inherited state.
        self.assertEqual(positions[4, 4], (4, 2))  # Original editable lab keeps its draft.
        self.assertEqual(positions[5, 6], (5, 6))  # The separate Zona segura practice remains intact.
        m.validate_transition(family['es'], moved)
        m.check_localized_family([moved, family['en'], family['fr']], ['es', 'en', 'fr'])
        self.assertEqual(translated_bytes,
                         {locale: (root/m.CONTENT_DIRECTORY/locale/suffix).read_bytes() for locale in ('en', 'fr')})


class ExtendedRepositoryGateTest(unittest.TestCase):
    git = RepositoryGateTest.git
    commit = RepositoryGateTest.commit
    write = RepositoryGateTest.write
    tearDown = RepositoryGateTest.tearDown

    def setUp(self):
        RepositoryGateTest.setUp(self)
        self.family = divergent_family()
        for locale, document in self.family.items(): self.write(self.files[locale], document)
        self.base = self.commit()

    def adopt(self):
        result = m.prepare_v2(self.family['es'], 'es', self.family)
        self.write(self.files['es'], result)
        return result

    def test_spanish_can_adopt_while_old_translations_stay_byte_identical(self):
        before = {locale: self.files[locale].read_bytes() for locale in ('en', 'fr')}
        self.adopt()
        self.assertEqual(m.check_repository(self.root, self.base)['mapped_lessons'], 1)
        self.assertEqual(before, {locale: self.files[locale].read_bytes() for locale in ('en', 'fr')})

    def test_forged_family_digest_is_rejected_by_the_published_git_anchor(self):
        source = self.adopt(); source['stateLayout']['family']['es']['sha256'] = '0' * 64
        self.write(self.files['es'], source)
        with self.assertRaisesRegex(m.LayoutError, 'published source'):
            m.check_repository(self.root, self.base)

    def test_omitting_a_shipped_locale_or_changing_its_raw_text_is_rejected(self):
        source = self.adopt(); del source['stateLayout']['family']['fr']
        self.write(self.files['es'], source)
        with self.assertRaises(m.LayoutError): m.check_repository(self.root, self.base)
        self.adopt(); changed = deepcopy(self.family['fr']); changed['pages'][0]['blocks'][0]['text'] = 'Changed'
        self.write(self.files['fr'], changed)
        with self.assertRaises(m.LayoutError): m.check_repository(self.root, self.base)

    def test_repeat_delivery_keeps_the_original_family_instead_of_rebaselining_it(self):
        source = self.adopt(); published = self.commit()
        moved = m.reorder(source, list(reversed(source['stateLayout']['current'])))
        self.write(self.files['es'], moved)
        self.assertEqual(m.check_repository(self.root, published)['families'], 1)
        self.assertEqual(m.check_repository(self.root, self.base)['families'], 1)

    def test_later_translation_adopts_its_real_history_without_rewriting_the_family(self):
        spanish = self.adopt(); spanish = m.reorder(spanish, list(reversed(spanish['stateLayout']['current'])))
        self.write(self.files['es'], spanish); published = self.commit()
        previous, actual_family = m.published_family(self.files['en'], 'en', published)
        english = m.prepare_v2(previous, 'en', actual_family)
        self.write(self.files['en'], english)
        self.assertEqual(m.check_repository(self.root, published)['mapped_lessons'], 2)
        self.assertEqual(english['stateLayout']['family'], spanish['stateLayout']['family'])

    def test_git_cli_prepares_edits_move_and_new_activity_without_exporting_old_assets(self):
        edited = deepcopy(self.family['es']); edited['pages'][0]['blocks'][0]['text'] = 'Edited in the same delivery'
        self.write(self.files['es'], edited)
        initial = m.prepare_v2(edited, 'es', self.family)
        plan = list(reversed(deepcopy(initial['stateLayout']['current'])))
        plan[1]['blocks'].insert(0, 'new_original_execution')
        plan_path = self.root/'authoring-plan.json'; additions_path = self.root/'authoring-additions.json'
        plan_path.write_text(json.dumps(plan)); additions_path.write_text(json.dumps([new_activity()]))
        target = self.root/'prepared-lesson.json'
        before = set(self.root.rglob('*'))
        result = subprocess.run([sys.executable, str(SPEC.origin), 'prepare', str(self.files['es']),
                                 '--locale', 'es', '--base-ref', self.base, '--plan', str(plan_path),
                                 '--additions', str(additions_path), '--output', str(target)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(self.root.rglob('*')) - before, {target})
        m.validate_transition(self.family['es'], m.load(target))

    def test_git_cli_rejects_forged_locale_and_never_overwrites_an_existing_output(self):
        target = self.root/'prepared-lesson.json'; target.write_text('KEEP')
        for locale in ('es', 'fr'):
            result = subprocess.run([sys.executable, str(SPEC.origin), 'prepare', str(self.files['es']),
                                     '--locale', locale, '--base-ref', self.base, '--output', str(target)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_text(), 'KEEP')

    def valid_curriculum(self):
        source = {'id': 'cpp_fundamentals__unit__fixture', 'title': 'Fixture', 'pages': [
            {'title': 'A', 'blocks': [
                {'type': 'paragraph', 'text': 'A complete explanation'},
                {'type': 'cpp_compiler', 'initialCode': 'int main() {}'},
                {'type': 'output_prediction', 'id': 'prior_prediction', 'language': 'cpp',
                 'question': 'Which output?', 'code': 'int main() {}', 'answers': ['Empty'],
                 'explanation': 'This program sends no text.'}]}]}
        family = {locale: deepcopy(source) for locale in ('es', 'en', 'fr')}
        for locale, document in family.items(): self.write(self.files[locale], document)
        content = self.files['es'].parents[1]
        self.write(content/'unit/unit_fixture.json', {
            'courseId': 'cpp_fundamentals', 'unitId': 'cpp_fundamentals__unit', 'title': 'Unit',
            'lessons': [{'number': 1, 'id': source['id'], 'title': 'Fixture'}]})
        self.write(content/'course_cpp_fundamentals.json', {
            'units': [{'number': 1, 'id': 'cpp_fundamentals__unit', 'title': 'Unit'}]})
        published = self.commit()
        return source, family, content, published

    def test_existing_validator_checks_new_blocks_before_projecting_historical_ones(self):
        import validar_json_programacion as contract
        source, family, content, published = self.valid_curriculum()
        prepared = m.prepare_v2(source, 'es', family)
        plan = deepcopy(prepared['stateLayout']['current'])
        plan[0]['blocks'].insert(0, 'new_original_execution')
        for malformed in (False, True):
            with self.subTest(malformed=malformed):
                addition = new_activity(page=1); addition['progressPage'] = 'page_0000'
                if malformed: addition['block']['initialCode'] = 42
                added = m.reorder(prepared, plan, [addition])
                self.write(self.files['es'], added)
                output = io.StringIO()
                with mock.patch.object(contract, 'ROOT', self.root), mock.patch.object(contract, 'CONTENT', content), \
                        mock.patch.object(contract, 'ERRORS', []), redirect_stderr(output), \
                        redirect_stdout(output):
                    if malformed:
                        with self.assertRaises(SystemExit): contract.main(['--base-ref', published])
                    else:
                        contract.main(['--base-ref', published])
                if malformed:
                    self.assertIn('initialCode debe ser texto', output.getvalue())
                else:
                    self.assertIn('posiciones previas correctos', output.getvalue())

    def test_existing_editorial_authorizations_follow_original_coordinates_after_an_earlier_move(self):
        import validar_json_programacion as contract
        source, family, content, _ = self.valid_curriculum()
        prepared = m.prepare_v2(source, 'es', family)
        plan = deepcopy(prepared['stateLayout']['current'])
        plan[0]['blocks'] = [plan[0]['blocks'][2], plan[0]['blocks'][0], plan[0]['blocks'][1]]
        moved = m.reorder(prepared, plan)
        self.write(self.files['es'], moved); published = self.commit()
        edited = deepcopy(moved)
        old_question = edited['pages'][0]['blocks'][0]['question']
        edited['pages'][0]['blocks'][0]['question'] = 'An approved, clearer question'
        previous, actual_family = m.published_family(self.files['es'], 'es', published)
        revised = m.prepare_v2(edited, 'es', actual_family, previous)
        self.write(self.files['es'], revised)
        digest = lambda text: hashlib.sha256(text.encode('utf-8')).hexdigest()
        approval = {('unit/lesson_fixture.json', 1, 3, 'output_prediction', 'question'):
                    (digest(old_question), digest(edited['pages'][0]['blocks'][0]['question']))}
        with mock.patch.object(contract, 'ROOT', self.root), mock.patch.object(contract, 'CONTENT', content), \
                mock.patch.object(contract, 'ERRORS', []), \
                mock.patch.object(contract, 'APPROVED_EDITORIAL_REVISIONS', approval), \
                redirect_stdout(io.StringIO()):
            contract.main(['--base-ref', published])

if __name__ == '__main__': unittest.main(verbosity=2)

"""Exercise the ordinary validator, not only the state-layout helper.

The authentic published U6/U9 families are read without changing assets.
In-memory reviews keep their full source archive and editorial chain.
"""
from __future__ import annotations
from contextlib import redirect_stdout
from copy import deepcopy
import io
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import validar_json_programacion as contract
import validate_programming_practice_tools as shared_course

layout = contract._state_layout
ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'app/src/main/assets/content'

def position(document, identity):
    matches = [(p, b) for p, page in enumerate(document['stateLayout']['current'])
               for b, candidate in enumerate(page['blocks']) if candidate == identity]
    if len(matches) != 1:
        raise AssertionError('The published identity is missing or ambiguous: ' + identity)
    return matches[0]

def formatted_documents():
    groups = {}
    for (lesson, owner), spec in layout.COMPILE_DIAGNOSTICS.items():
        groups.setdefault(lesson, []).append((owner, spec['diagnostic']))
    result = []
    for lesson, owners in sorted(groups.items()):
        course, unit, title = lesson.split('__')
        relative = Path('programming') / course / unit / ('lesson_' + title + '.json')
        family = {locale: json.loads((ASSETS / locale / relative).read_text(encoding='utf-8'))
                  for locale in ('es', 'en', 'fr')}
        before = family['es']
        layout.validate(before)
        changed = deepcopy(before)
        for identity, diagnostic in owners:
            p, b = position(changed, identity)
            block = changed['pages'][p]['blocks'][b]
            if block['compileCheck']['diagnostic'] != diagnostic:
                raise AssertionError('The actual reviewed diagnostic changed')
            original = block['initialCode']
            anchor = '\n    return 0;\n'
            if original.count(anchor) != 1:
                raise AssertionError('The published fixture no longer has its return')
            spaced = original.replace(anchor, '\n\n    return 0;\n', 1)
            block['initialCode'] = spaced
            for file in block.get('projectFiles', []):
                if file['path'] == 'main.cpp':
                    if file['initialCode'] != original:
                        raise AssertionError('The published project main changed')
                    file['initialCode'] = spaced
        after = layout.prepare_v2(changed, 'es', family, before)
        layout.validate_transition(before, after)
        result.append((ASSETS / 'es' / relative, before, after, owners))
    return result

class ReviewedDiagnosticStructuralFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.documents = formatted_documents()
        cls.original_bytes = {path: path.read_bytes() for path, _, _, _ in cls.documents}

    def setUp(self):
        contract.ERRORS.clear()

    def tearDown(self):
        contract.ERRORS.clear()
        for path, original in self.original_bytes.items():
            self.assertEqual(path.read_bytes(), original)

    def validate_owner(self, document, identity, block=None, at=None, *, structure_only=None):
        at = position(document, identity) if at is None else at
        visible = document['pages'][at[0]]['blocks'][at[1]] if block is None else block
        if structure_only is None:
            contract.validate_cpp_compiler(visible, identity, document=document, position=at)
        else:
            shared_course._block(SimpleNamespace(**vars(contract)), visible, identity,
                root=ROOT, document=document, position=at, structure_only=structure_only)

    def format_guard(self, before, after, identity, *, block=None, old=None,
                     at=None, relative=None):
        p, b = position(before, identity)
        original = before['pages'][p]['blocks'][b] if old is None else old
        if block is None:
            q, c = position(after, identity)
            block = after['pages'][q]['blocks'][c]
        storage = layout.validate(before)[p, b] if at is None else at
        course, unit, title = before['id'].split('__')
        path = (f'app/src/main/assets/content/es/programming/{course}/'
                f'{unit}/lesson_{title}.json') if relative is None else relative
        return contract.preserves_source_format_revision(
            path, *storage, original, block,
            previous_document=before, current_document=after)

    def run_full_validator(self, label, operation):
        """Publish diagnostics before unittest can defer them past a CTest timeout."""
        captured = io.StringIO()
        started = time.perf_counter()
        try:
            with redirect_stdout(captured):
                result = operation()
                self.assertEqual(contract.ERRORS, [])
        except BaseException:
            print(f'FAIL {label} after {time.perf_counter() - started:.3f}s',
                  file=sys.stderr, flush=True)
            print('Captured validator stdout:\n' + captured.getvalue(),
                  file=sys.stderr, flush=True)
            print('Validator ERRORS:\n' + '\n'.join(map(str, contract.ERRORS)),
                  file=sys.stderr, flush=True)
            traceback.print_exc(file=sys.stderr)
            sys.stderr.flush()
            raise
        print(f'PASS {label} in {time.perf_counter() - started:.3f}s', flush=True)
        return result

    def test_all_seventeen_closed_goals_pass_with_authenticated_format(self):
        total = 0
        for _, before, after, owners in self.documents:
            self.assertEqual(layout.validate(before), layout.validate(after))
            aligned = layout.align_for_previous_validation(before, after)
            for identity, _ in owners:
                with self.subTest(owner=identity, lesson=before['id']):
                    self.validate_owner(after, identity)
                    self.assertEqual(contract.ERRORS, [])
                    p, b = position(before, identity)
                    old = before['pages'][p]['blocks'][b]
                    q, c = position(after, identity)
                    new = after['pages'][q]['blocks'][c]
                    self.assertEqual(old['compileCheck'], new['compileCheck'])
                    self.assertEqual(old.get('sourceFiles'), new.get('sourceFiles'))
                    self.assertNotEqual(old['initialCode'], new['initialCode'])
                    # The ordinary historical loop passes this aligned block.
                    # It must still be the actual visible owner, even when the
                    # canonical diagnostic archive predates both sources.
                    self.assertEqual(aligned['pages'][p]['blocks'][b], new)
                    self.assertTrue(self.format_guard(before, after, identity,
                                                     block=aligned['pages'][p]['blocks'][b]))
                    self.assertFalse(self.format_guard(before, before, identity))
                    total += 1
        self.assertEqual(total, 17)

    def test_both_audit_modes_accept_published_and_followup_diagnostic_formats(self):
        total = 0
        for _, before, after, owners in self.documents:
            for identity, _ in owners:
                for name, document in (('published', before), ('followup', after)):
                    for structure_only in (False, True):
                        with self.subTest(lesson=before['id'], owner=identity,
                                          epoch=name, structure_only=structure_only):
                            contract.ERRORS.clear()
                            self.validate_owner(document, identity,
                                                structure_only=structure_only)
                            self.assertEqual(contract.ERRORS, [])
                total += 1
        self.assertEqual(total, 17)

    def test_standalone_blocks_and_unarchived_spacing_stay_strict(self):
        for path, before, after, owners in self.documents:
            identity = owners[0][0]
            p, b = position(after, identity)
            block = after['pages'][p]['blocks'][b]
            contract.ERRORS.clear()
            contract.validate_cpp_compiler(block, identity)
            self.assertTrue(contract.ERRORS)
            with self.assertRaises(layout.LayoutError):
                layout.validate_compile_check_contract(block)
            unproved = deepcopy(after)
            unproved['stateLayout']['sourceFormatRevisions'] = [
                entry for entry in unproved['stateLayout']['sourceFormatRevisions']
                if entry['id'] != identity]
            contract.ERRORS.clear()
            self.validate_owner(unproved, identity)
            self.assertTrue(contract.ERRORS)
            self.assertFalse(self.format_guard(before, unproved, identity))

            relative = path.relative_to(ASSETS / 'es')
            family = {locale: json.loads((ASSETS / locale / relative).read_text(encoding='utf-8'))
                      for locale in ('es', 'en', 'fr')}
            canonical = next(entry['block'] for entry in
                             before['stateLayout']['sourceFormatRevisions']
                             if entry['id'] == identity)
            prose = deepcopy(before)
            p, b = position(prose, identity)
            explanation = prose['pages'][p]['blocks'][b]['compileCheck']
            explanation['explanation'] += ' Esta revisión sólo aclara la explicación.'
            marker = next(field for field in prose['textSemantics']['fields']
                          if field['block'] == identity and
                          field['path'] == '/compileCheck/explanation')
            marker['sourceSha256'] = hashlib.sha256(
                explanation['explanation'].encode('utf-8')).hexdigest()
            prose = layout.prepare_v2(prose, 'es', family, before)
            layout.validate_transition(before, prose)
            self.assertEqual(before['pages'][p]['blocks'][b]['initialCode'],
                             prose['pages'][p]['blocks'][b]['initialCode'])
            self.assertFalse(self.format_guard(before, prose, identity))

            restored = deepcopy(prose)
            restored['pages'][p]['blocks'][b] = layout.code_format.source_projection(
                restored['pages'][p]['blocks'][b], canonical)
            restored = layout.prepare_v2(restored, 'es', {**family, 'es': prose}, prose)
            # Canonical whitespace plus the separately authored new guide is
            # a new complete payload, not an undeclared editorial cycle.
            # The physical change stays true when projected sources coincide.
            self.assertNotEqual(prose['pages'][p]['blocks'][b]['initialCode'],
                                restored['pages'][p]['blocks'][b]['initialCode'])
            self.assertTrue(self.format_guard(prose, restored, identity))

    def test_forged_source_archive_diagnostic_and_proof_fail_closed(self):
        for _, before, after, owners in self.documents:
            identity = owners[0][0]
            for kind in ('token', 'diagnostic', 'archive', 'proof', 'lesson', 'owner'):
                with self.subTest(owner=identity, kind=kind):
                    forged = deepcopy(after)
                    p, b = position(forged, identity)
                    candidate = forged['pages'][p]['blocks'][b]
                    if kind == 'token':
                        candidate['initialCode'] += '\nint forged = 1;\n'
                    elif kind == 'diagnostic':
                        candidate['compileCheck']['diagnostic'] = 'not-authored'
                    elif kind == 'archive':
                        snapshot = next(entry for entry in forged['stateLayout']['sourceFormatRevisions']
                                        if entry['id'] == identity)
                        snapshot['block']['initialCode'] += '\n// altered history\n'
                    elif kind == 'proof':
                        editorial = forged['stateLayout']['editorial']
                        last = max(i for i, entry in enumerate(editorial) if entry['id'] == identity)
                        editorial.pop(last)
                    elif kind == 'lesson':
                        forged['id'] = 'cpp_fundamentals__other__unrelated_owner'
                    else:
                        forged['stateLayout']['current'][p]['blocks'][b] = 'qa_unrelated_diagnostic_owner'
                    self.assertFalse(self.format_guard(before, forged, identity, block=candidate))
                    for structure_only in (None, False, True):
                        with self.subTest(structure_only=structure_only):
                            contract.ERRORS.clear()
                            self.validate_owner(forged, identity, at=(p, b),
                                                structure_only=structure_only)
                            self.assertTrue(contract.ERRORS)

    def test_valid_document_never_authorizes_a_detached_or_mislocated_block(self):
        _, before, after, owners = self.documents[0]
        identity = owners[0][0]
        p, b = position(after, identity)
        block = deepcopy(after['pages'][p]['blocks'][b])
        block['initialCode'] += '\nint not_in_document = 1;\n'
        self.validate_owner(after, identity, block=block)
        self.assertTrue(contract.ERRORS)
        self.assertFalse(self.format_guard(before, after, identity, block=block))
        for invalid in ((-1, b), (True, b), (100000, b), None):
            contract.ERRORS.clear()
            contract.validate_cpp_compiler(after['pages'][p]['blocks'][b], identity,
                                           document=after, position=invalid)
            self.assertTrue(contract.ERRORS)
        canonical = next(entry['block'] for entry in after['stateLayout']['sourceFormatRevisions']
                         if entry['id'] == identity)
        self.assertNotEqual(canonical, after['pages'][p]['blocks'][b])
        self.assertFalse(self.format_guard(before, after, identity, block=canonical))
        self.assertFalse(self.format_guard(before, after, identity, old=canonical))
        for invalid in ((-1, b), (True, b), (100000, b), (None, b), (p, b + 1)):
            with self.subTest(storage=invalid):
                self.assertFalse(self.format_guard(before, after, identity, at=invalid))
        self.assertFalse(self.format_guard(before, after, identity,
            relative='app/src/main/assets/content/es/programming/cpp_fundamentals/other/lesson_other.json'))

    def test_full_ordinary_validator_consumes_the_authored_reviews(self):
        # Supply exactly the reviewed in-memory documents to the real
        # load boundary. Git baselines and every other validator stay real.
        overlays = {path: after for path, _, after, _ in self.documents}
        real_load = contract.load
        def reviewed_load(path):
            return deepcopy(overlays[Path(path)]) if Path(path) in overlays else real_load(path)
        with patch.object(contract, 'load', side_effect=reviewed_load):
            self.run_full_validator('ordinary full course validator',
                lambda: contract.main(['--course', 'cpp_fundamentals', '--base-ref', 'HEAD']))
        self.assertEqual(contract.ERRORS, [])

    def test_shared_course_validator_passes_context_without_promoting_structure_audit(self):
        # Both routes authenticate the specific closed diagnostic before
        # projecting its source. The audit still grants no global approval.
        real_load = contract.load
        overlays = {path: after for path, _, after, _ in self.documents}
        def reviewed_load(path):
            return deepcopy(overlays[Path(path)]) if Path(path) in overlays else real_load(path)
        namespace = SimpleNamespace(**vars(contract))
        namespace.load = reviewed_load
        self.run_full_validator('shared full course validator',
            lambda: shared_course.validate(namespace, ROOT, 'HEAD', course_id='cpp_fundamentals'))
        self.assertEqual(contract.ERRORS, [])
        contract.ERRORS.clear()
        summary = self.run_full_validator('shared structural audit',
            lambda: shared_course.validate(namespace, ROOT, None,
                course_id='cpp_fundamentals', structure_only=True))
        self.assertEqual(summary['scope'], 'STRUCTURE_ONLY_NOT_APPROVED')
        self.assertFalse(summary['stateLayoutValidated'])
        self.assertFalse(summary['historicalProtectionChecked'])
        self.assertEqual(summary['historical'], 0)
        self.assertEqual(contract.ERRORS, [])

if __name__ == '__main__':
    unittest.main()

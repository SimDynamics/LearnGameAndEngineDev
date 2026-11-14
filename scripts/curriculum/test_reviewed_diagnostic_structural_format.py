"""Exercise the ordinary validator, not only the state-layout helper.

The authentic published U6/U9 families are read without changing assets.
In-memory reviews keep their full source archive and editorial chain.
"""
from __future__ import annotations
from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
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

    def validate_owner(self, document, identity, block=None, at=None):
        at = position(document, identity) if at is None else at
        visible = document['pages'][at[0]]['blocks'][at[1]] if block is None else block
        contract.validate_cpp_compiler(visible, identity, document=document, position=at)

    def test_all_seventeen_closed_goals_pass_with_authenticated_format(self):
        total = 0
        for _, before, after, owners in self.documents:
            self.assertEqual(layout.validate(before), layout.validate(after))
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
                    total += 1
        self.assertEqual(total, 17)

    def test_standalone_blocks_and_unarchived_spacing_stay_strict(self):
        for _, before, after, owners in self.documents:
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

    def test_forged_source_archive_diagnostic_and_proof_fail_closed(self):
        for _, _, after, owners in self.documents:
            identity = owners[0][0]
            for kind in ('token', 'diagnostic', 'archive', 'proof', 'lesson'):
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
                    else:
                        forged['id'] = 'cpp_fundamentals__other__unrelated_owner'
                    contract.ERRORS.clear()
                    self.validate_owner(forged, identity)
                    self.assertTrue(contract.ERRORS)

    def test_valid_document_never_authorizes_a_detached_or_mislocated_block(self):
        _, _, after, owners = self.documents[0]
        identity = owners[0][0]
        p, b = position(after, identity)
        block = deepcopy(after['pages'][p]['blocks'][b])
        block['initialCode'] += '\nint not_in_document = 1;\n'
        self.validate_owner(after, identity, block=block)
        self.assertTrue(contract.ERRORS)
        for invalid in ((-1, b), (True, b), (100000, b), None):
            contract.ERRORS.clear()
            contract.validate_cpp_compiler(after['pages'][p]['blocks'][b], identity,
                                           document=after, position=invalid)
            self.assertTrue(contract.ERRORS)

    def test_full_ordinary_validator_consumes_the_authored_reviews(self):
        # Supply exactly the reviewed in-memory documents to the real
        # load boundary. Git baselines and every other validator stay real.
        overlays = {path: after for path, _, after, _ in self.documents}
        real_load = contract.load
        def reviewed_load(path):
            return deepcopy(overlays[Path(path)]) if Path(path) in overlays else real_load(path)
        with patch.object(contract, 'load', side_effect=reviewed_load), redirect_stdout(io.StringIO()):
            contract.main(['--course', 'cpp_fundamentals', '--base-ref', 'HEAD'])
        self.assertEqual(contract.ERRORS, [])

    def test_shared_course_validator_passes_context_but_structure_audit_does_not(self):
        # Both routes reuse this same block validator; the bounded audit
        # must not quietly turn itself into historical authentication.
        from types import SimpleNamespace
        real_load = contract.load
        overlays = {path: after for path, _, after, _ in self.documents}
        def reviewed_load(path):
            return deepcopy(overlays[Path(path)]) if Path(path) in overlays else real_load(path)
        namespace = SimpleNamespace(**vars(contract))
        namespace.load = reviewed_load
        shared_course.validate(namespace, ROOT, 'HEAD', course_id='cpp_fundamentals')
        self.assertEqual(contract.ERRORS, [])
        contract.ERRORS.clear()
        summary = shared_course.validate(namespace, ROOT, None,
            course_id='cpp_fundamentals', structure_only=True)
        self.assertEqual(summary['scope'], 'STRUCTURE_ONLY_NOT_APPROVED')
        self.assertTrue(any('exact complete initial block' in error for error in contract.ERRORS))

if __name__ == '__main__':
    unittest.main()

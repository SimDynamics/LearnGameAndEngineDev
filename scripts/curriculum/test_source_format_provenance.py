"""Bind formatting permission to the actual published lesson and both owners.

All reviewed documents are generated in memory from the real U6 assets.
This does not change curricular JSON, original hashes or student state.
"""
from copy import deepcopy
import unittest
import validar_json_programacion as contract
import test_compile_check_source_format as fixture


class SourceFormatProvenanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.after = fixture.formatted_pair()
        cls.relative = 'app/src/main/assets/content/es/' + fixture.RELATIVE.as_posix()
        cls.asset_path = fixture.ROOT / cls.relative
        cls.asset_bytes = cls.asset_path.read_bytes()

    def setUp(self):
        self.previous = deepcopy(self.before)
        self.current_doc = deepcopy(self.after)
        self.page, self.index = fixture.POSITIONS[0]
        self.old = self.previous['pages'][self.page]['blocks'][self.index]
        self.current = self.current_doc['pages'][self.page]['blocks'][self.index]

    def tearDown(self):
        self.assertEqual(self.asset_bytes, self.asset_path.read_bytes())

    def accepts(self, **overrides):
        args = dict(relative=self.relative, pn=self.page, bn=self.index,
                    old=self.old, current=self.current,
                    previous_document=self.previous, current_document=self.current_doc)
        args.update(overrides)
        return contract.preserves_source_format_revision(**args)

    def test_three_authentic_owners_preserve_their_archived_format(self):
        for p, b in fixture.POSITIONS:
            with self.subTest(owner=(p, b)):
                self.assertTrue(self.accepts(pn=p, bn=b,
                    old=self.previous['pages'][p]['blocks'][b],
                    current=self.current_doc['pages'][p]['blocks'][b]))

    def test_equivalent_windows_path_spelling_stays_valid(self):
        self.assertTrue(self.accepts(relative=self.relative.replace('/', '\\')))

    def test_another_lesson_filename_cannot_borrow_the_proof(self):
        self.assertFalse(self.accepts(relative=self.relative.replace(
            'lesson_declaraciones_y_firmas.json', 'lesson_other.json')))

    def test_another_unit_cannot_borrow_the_proof(self):
        self.assertFalse(self.accepts(relative=self.relative.replace(
            '/funciones_y_contratos/', '/another_unit/')))

    def test_noncanonical_parent_path_and_wrong_suffix_are_rejected(self):
        for path in (self.relative.replace('/funciones_y_contratos/',
                      '/funciones_y_contratos/../funciones_y_contratos/'),
                     self.relative.removesuffix('.json') + '.txt',
                     self.relative + '/extra.json'):
            with self.subTest(path=path):
                self.assertFalse(self.accepts(relative=path))

    def test_lexically_equivalent_but_detached_old_source_is_rejected(self):
        detached = deepcopy(self.old)
        detached['initialCode'] = '\n' + detached['initialCode']
        for item in detached.get('projectFiles', []):
            if item['path'] == 'main.cpp':
                item['initialCode'] = detached['initialCode']
        self.assertFalse(self.accepts(old=detached))

    def test_detached_previous_guidance_is_not_historical_evidence(self):
        detached = deepcopy(self.old)
        detached['compileCheck']['explanation'] = detached['compileCheck'].get('explanation', '') + ' Texto no publicado.'
        self.assertFalse(self.accepts(old=detached))

    def test_detached_current_block_is_rejected(self):
        detached = deepcopy(self.current)
        detached['initialCode'] = '\n' + detached['initialCode']
        self.assertFalse(self.accepts(current=detached))

    def test_coordinates_are_nonnegative_integers_not_equal_numeric_aliases(self):
        for pn, bn in ((float(self.page), self.index),
                       (self.page, float(self.index)),
                       (True, self.index), (self.page, False),
                       (-1, self.index), (self.page, -1),
                       (100000, self.index)):
            with self.subTest(position=(pn, bn)):
                self.assertFalse(self.accepts(pn=pn, bn=bn))

    def test_missing_archive_and_duplicate_identity_stay_rejected(self):
        identity = self.current_doc['stateLayout']['current'][self.page]['blocks'][self.index]
        for mode in ('missing', 'duplicate'):
            forged = deepcopy(self.current_doc)
            archives = forged['stateLayout']['sourceFormatRevisions']
            if mode == 'missing':
                forged['stateLayout']['sourceFormatRevisions'] = [
                    entry for entry in archives if entry['id'] != identity]
            else:
                archives.append(deepcopy(next(entry for entry in archives if entry['id'] == identity)))
            with self.subTest(mode=mode):
                self.assertFalse(self.accepts(current_document=forged))

    def test_changed_tokens_do_not_become_a_format_revision(self):
        forged = deepcopy(self.current_doc)
        changed = forged['pages'][self.page]['blocks'][self.index]
        changed['initialCode'] += '\nint unapproved = 1;\n'
        self.assertFalse(self.accepts(current_document=forged, current=changed))

    def test_wrong_course_locale_and_absent_documents_stay_rejected(self):
        self.assertFalse(self.accepts(course_id='programming_practice_tools'))
        self.assertFalse(self.accepts(relative=self.relative.replace('/es/', '/en/')))
        self.assertFalse(self.accepts(previous_document=None))
        self.assertFalse(self.accepts(current_document=None))


if __name__ == '__main__':
    unittest.main()

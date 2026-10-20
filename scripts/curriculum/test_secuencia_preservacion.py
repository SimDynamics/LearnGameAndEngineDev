"""Las correcciones de navegación no autorizan cambios de respuesta ni fuente."""

import copy
import json
import subprocess
import unittest

import validar_json_programacion as contract


BASELINE = 'dd6787d190fcb707a8e935abeadda64584d773d9'


def old_lesson(lesson):
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    return json.loads(subprocess.check_output(
        ['git', 'show', BASELINE + ':' + relative], cwd=contract.ROOT))


class SequencePreservationTest(unittest.TestCase):
    def cases(self):
        for lesson, page, block, kind, field in contract.SEQUENCE_EDITORIAL_REVISIONS:
            before = old_lesson(lesson)['pages'][page - 1]['blocks'][block - 1]
            after = contract.load(contract.CONTENT / lesson)['pages'][page - 1]['blocks'][block - 1]
            self.assertEqual(kind, before['type'])
            yield lesson, page, block, field, before, after

    def preserved(self, lesson, page, block, before, after):
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
        return contract.preserves_previous_exercise(relative, page - 1, block - 1, before, after)

    def test_seven_exact_reviewed_texts_preserve_all_other_fields(self):
        cases = list(self.cases())
        self.assertEqual(7, len(cases))
        for lesson, page, block, field, before, after in cases:
            with self.subTest(lesson=lesson, page=page, block=block):
                self.assertEqual({field}, {k for k in before.keys() | after.keys()
                                          if before.get(k) != after.get(k)})
                self.assertTrue(self.preserved(lesson, page, block, before, after))

    def test_approval_does_not_accept_other_text_answers_sources_or_positions(self):
        for lesson, page, block, field, before, after in self.cases():
            for key in (field, 'answers', 'code', 'id', 'outputComparison'):
                candidate = copy.deepcopy(after)
                candidate[key] = 'UNAPPROVED'
                with self.subTest(lesson=lesson, key=key):
                    self.assertFalse(self.preserved(lesson, page, block, before, candidate))
            self.assertFalse(self.preserved(lesson, page + 1, block, before, after))

    def compiler_case(self):
        lesson = 'programas_y_ejecucion/lesson_primer_programa_y_salida.json'
        before_document = old_lesson(lesson)
        before = before_document['pages'][4]['blocks'][2]
        after = contract.load(contract.CONTENT / lesson)['pages'][4]['blocks'][2]
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
        return relative, before_document, before, after

    def test_output_guide_changes_only_the_obsolete_navigation(self):
        relative, document, before, after = self.compiler_case()
        expected = copy.deepcopy(before)
        expected['outputCheck']['cases'][0]['reviewFeedback'][0]['text'] = (
            'Revisa el mensaje solicitado en esta actividad y qué caracteres contiene el literal que acabas de ejecutar.')
        self.assertEqual(expected, after)
        self.assertTrue(contract.preserves_previous_compiler(relative, 4, 2, before, after, document))

    def test_output_guide_approval_cannot_change_compiler_goal_source_or_slot(self):
        relative, document, before, after = self.compiler_case()
        for key, value in [('outputs', ['Wrong']), ('standardInput', 'Wrong')]:
            candidate = copy.deepcopy(after)
            candidate['outputCheck']['cases'][0][key] = value
            self.assertFalse(contract.preserves_previous_compiler(relative, 4, 2, before, candidate, document))
        candidate = dict(after, initialCode='int main() {}')
        self.assertFalse(contract.preserves_previous_compiler(relative, 4, 2, before, candidate, document))
        self.assertFalse(contract.preserves_previous_compiler(relative, 5, 2, before, after, document))


if __name__ == '__main__':
    unittest.main()

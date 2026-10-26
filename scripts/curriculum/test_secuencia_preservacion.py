"""Las correcciones de navegación no autorizan cambios de respuesta ni fuente."""

import copy
from functools import lru_cache
import json
import subprocess
import unittest

import validar_json_programacion as contract
import stable_state_layout as state_layout
import retirement_fixture_source as retirement_fixtures


BASELINE = 'dd6787d190fcb707a8e935abeadda64584d773d9'
LAYOUT_BASELINE = '25cfe2965b4fb03740795ef1d10d6fd7a0dcb50d'


def old_lesson(lesson):
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    return json.loads(subprocess.check_output(
        ['git', 'show', BASELINE + ':' + relative], cwd=contract.ROOT))


@lru_cache(maxsize=32)
def published_source(lesson):
    """The reviewed sequence before retirement, never today's live asset.

    The three U1 files also have independent exact-byte SHA-256 pins. Other
    routes are addressed by path in that same full immutable local commit.
    No network lookup or fallback to a different presentation is permitted.
    """
    prefix = 'programas_y_ejecucion/'
    if lesson.startswith(prefix) and lesson[len(prefix):] in retirement_fixtures.FIXTURE_SHA256:
        return retirement_fixtures.historical_bytes(lesson[len(prefix):])
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    return subprocess.check_output(
        ['git', 'show', retirement_fixtures.BASELINE_COMMIT + ':' + relative], cwd=contract.ROOT)


def published_lesson(lesson):
    return json.loads(published_source(lesson))


class SequencePreservationTest(unittest.TestCase):
    def cases(self):
        for lesson, page, block, kind, field in contract.SEQUENCE_EDITORIAL_REVISIONS:
            before = old_lesson(lesson)['pages'][page - 1]['blocks'][block - 1]
            current = published_lesson(lesson)
            after = state_layout.original_projection(current)['pages'][page - 1]['blocks'][block - 1]
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
        current = published_lesson(lesson)
        after = state_layout.original_projection(current)['pages'][4]['blocks'][2]
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

    def test_reordered_prediction_and_editor_keep_their_real_historical_contracts(self):
        lesson = 'programas_y_ejecucion/lesson_primer_programa_y_salida.json'
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
        previous = json.loads(subprocess.check_output(
            ['git', 'show', LAYOUT_BASELINE + ':' + relative], cwd=contract.ROOT))
        # Reordering and retirement are two real published transitions, not
        # one invented archive claiming that the much older layout was its
        # immediate predecessor.
        current = published_lesson(lesson)
        self.assertEqual(2, current['stateLayout']['version'])
        state_layout.validate_transition(previous, current)
        positions = state_layout.validate(current)
        inverse = {stored: displayed for displayed, stored in positions.items()}
        projected = state_layout.original_projection(current)
        self.assertEqual(34, sum(len(page['blocks']) for page in projected['pages']))

        for stored in ((4, 3), (4, 2), (4, 4), (5, 6)):
            with self.subTest(historical_slot=stored):
                page, block = inverse[stored]
                before = previous['pages'][stored[0]]['blocks'][stored[1]]
                after = current['pages'][page]['blocks'][block]
                self.assertEqual(state_layout.state_payload(before), state_layout.state_payload(after))
                self.assertEqual(before.get('id'), after.get('id'))
                self.assertEqual(projected['pages'][stored[0]]['blocks'][stored[1]], after)
                identity = current['stateLayout']['current'][page]['blocks'][block]
                self.assertEqual(current['stateLayout']['origin'][stored[0]]['blocks'][stored[1]]['id'],
                                 identity)

        self.assertLess(inverse[4, 3], inverse[4, 2])  # Prediction precedes the editable construction.
        self.assertEqual('prediccion_zona_inicial',
                         projected['pages'][4]['blocks'][3]['id'])
        self.assertEqual(['Zona inicial'], projected['pages'][4]['blocks'][3]['answers'])

        original_id = 'ejecucion_original_editor'
        original_slots = [(page, block) for page, record in enumerate(current['stateLayout']['current'])
                          for block, identity in enumerate(record['blocks']) if identity == original_id]
        self.assertEqual(1, len(original_slots))
        original_slot = original_slots[0]
        original = current['pages'][original_slot[0]]['blocks'][original_slot[1]]
        construction = projected['pages'][4]['blocks'][2]
        addition = next(item for item in current['stateLayout']['additions'] if item['id'] == original_id)
        self.assertEqual('cpp_compiler', original['type'])
        self.assertEqual((addition['storage']['page'], addition['storage']['block']), positions[original_slot])
        self.assertNotIn(positions[original_slot],
                         {(page, block) for page, item in enumerate(previous['pages'])
                          for block in range(len(item['blocks']))})
        self.assertLess(inverse[4, 3], original_slot)
        self.assertLess(original_slot, inverse[4, 2])
        self.assertEqual(construction['initialCode'], original['initialCode'])
        self.assertEqual(['Zona inicial'], original['outputCheck']['cases'][0]['outputs'])
        self.assertEqual(['Caja recibida'], construction['outputCheck']['cases'][0]['outputs'])
        self.assertNotEqual(positions[original_slot], positions[inverse[4, 2]])
        integrated = contract.load(contract.CONTENT / lesson)
        state_layout.validate_transition(current, integrated)
        self.assertEqual(current['pages'], integrated['stateLayout']['retirementHistory'][0]['pages'])
        self.assertEqual(current['stateLayout']['current'],
                         integrated['stateLayout']['retirementHistory'][0]['current'])
        self.assertNotIn(original_id, self.visible_state_blocks(integrated))
        archived = next(item for item in integrated['stateLayout']['retired'] if item['id'] == original_id)
        self.assertEqual(original, archived['block'])

    def reviewed_routes(self):
        """Historical identities survive prose edits and changes of visible position."""
        return (
            ('programas_y_ejecucion/lesson_primer_programa_y_salida.json',
             'block_0004_0003', 'prediccion_zona_inicial', 'ejecucion_original_editor'),
            ('programas_y_ejecucion/lesson_texto_secuencia_y_formato.json',
             'block_0006_0003', 'prediccion_informe_inicial',
             'ejecucion_original_texto_secuencia_y_formato_editor'),
            ('programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json',
             'block_0003_0005', 'prediccion_revision_inicial',
             'ejecucion_original_fuente_compilacion_y_diagnostico_editor'),
            ('objetos_y_estado/lesson_leer_una_entrada_conocida.json',
             'block_0006_0004',
             'cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__etapas_lectura_y_actualizacion',
             'block_0006_0002'),
            ('objetos_y_estado/lesson_representar_procesar_y_recuperar.json',
             'block_0004_0004',
             'cpp_fundamentals__objetos_y_estado__representar_procesar_y_recuperar__registrar_datos_y_suma_del_informe',
             'block_0004_0002'),
            ('objetos_y_estado/lesson_representar_procesar_y_recuperar.json',
             'block_0007_0004',
             'cpp_fundamentals__objetos_y_estado__representar_procesar_y_recuperar__registrar_estados_de_copia_y_devolucion',
             'block_0007_0002'),
            ('expresiones_y_decisiones/lesson_alternativas_y_fronteras.json',
             'block_0003_0003', None, 'block_0003_0002'),
            ('funciones_y_contratos/lesson_recursion_finita_y_comparacion.json',
             'block_0009_0006',
             'cpp_fundamentals__funciones_y_contratos__recursion_finita_y_comparacion__casos_y_traza_de_grupos',
             'block_0009_0003'),
            ('datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json',
             'block_0001_0004',
             'cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos__traza_de_solicitudes',
             'block_0001_0002'),
            ('archivos_y_programa_completo/lesson_componer_informes_sin_cambiar_los_datos.json',
             'block_0001_0004',
             'cpp_fundamentals__archivos_y_programa_completo__componer_informes_sin_cambiar_los_datos__prediccion_del_informe',
             'block_0001_0002'),
        )

    def visible_state_blocks(self, document):
        self.assertEqual(2, document['stateLayout']['version'])
        self.assertEqual('es', document['stateLayout']['locale'])
        state_layout.validate(document)
        result = {}
        for page, record in enumerate(document['stateLayout']['current']):
            for block, identity in enumerate(record['blocks']):
                self.assertNotIn(identity, result)
                result[identity] = (page, block, document['pages'][page]['blocks'][block])
        return result

    def test_all_ten_pre_execution_puzzles_precede_the_first_editor_of_their_activity(self):
        routes = self.reviewed_routes()
        self.assertEqual(10, len(routes))
        self.assertEqual(9, len({lesson for lesson, *_ in routes}))
        for lesson, prediction_identity, prediction_id, editor_identity in routes:
            with self.subTest(lesson=lesson, prediction=prediction_identity):
                document = published_lesson(lesson)
                blocks = self.visible_state_blocks(document)
                prediction_page, prediction_position, prediction = blocks[prediction_identity]
                editor_page, editor_position, editor = blocks[editor_identity]
                self.assertIn(prediction['type'], ('output_prediction', 'multiple_choice', 'math_puzzle'))
                self.assertEqual(prediction_id, prediction.get('id'))
                self.assertEqual('cpp_compiler', editor['type'])
                self.assertEqual(prediction_page, editor_page,
                                 'The preparation and its editor must share one forward reading path.')
                first_editor = next(index for index, block in
                                    enumerate(document['pages'][editor_page]['blocks'])
                                    if block['type'] == 'cpp_compiler')
                self.assertEqual(editor_position, first_editor,
                                 'An earlier executable editor would already reveal the predicted result.')
                self.assertLess(prediction_position, first_editor,
                                'Predict before execution; do not send the student back from an editor.')

    def test_first_unit_predictions_original_runs_and_modifications_have_separate_goals(self):
        modification_goals = (
            ('block_0004_0002', {'Caja recibida'}),
            ('block_0006_0002', {'Informe\nZona Este\nFin', 'Informe\nZona Este\nFin\n'}),
            ('block_0003_0003', {'Revision completa'}),
        )
        for route, (modification_identity, expected_outputs) in zip(self.reviewed_routes()[:3],
                                                                  modification_goals):
            lesson, prediction_identity, _, original_identity = route
            with self.subTest(lesson=lesson):
                document = published_lesson(lesson)
                blocks = self.visible_state_blocks(document)
                prediction_page, prediction_position, prediction = blocks[prediction_identity]
                original_page, original_position, original = blocks[original_identity]
                modification_page, modification_position, modification = blocks[modification_identity]
                self.assertEqual(prediction_page, original_page)
                self.assertEqual(original_page, modification_page)
                self.assertLess(prediction_position, original_position)
                self.assertLess(original_position, modification_position)
                self.assertEqual('cpp_compiler', original['type'])
                self.assertEqual('cpp_compiler', modification['type'])
                self.assertEqual(prediction['code'], original['initialCode'])
                self.assertEqual(original['initialCode'], modification['initialCode'])
                self.assertEqual(1, len(original['outputCheck']['cases']))
                self.assertEqual(1, len(modification['outputCheck']['cases']))
                original_case = original['outputCheck']['cases'][0]
                modification_case = modification['outputCheck']['cases'][0]
                self.assertEqual(prediction['answers'], original_case['outputs'])
                self.assertEqual(expected_outputs, set(modification_case['outputs']))
                self.assertTrue(set(original_case['outputs']).isdisjoint(modification_case['outputs']),
                                'The modification must not also accept the unchanged program.')
                self.assertEqual(original_case['standardInput'], modification_case['standardInput'])
                positions = state_layout.validate(document)
                self.assertNotEqual(positions[original_page, original_position],
                                    positions[modification_page, modification_position],
                                    'The original run must not reuse the historical editing draft.')
                self.assertIn(original_identity,
                              {addition['id'] for addition in document['stateLayout']['additions']})

    def test_current_routes_preserve_predictions_before_useful_editors_and_archive_redundant_runs(self):
        useful_editors = {
            'ejecucion_original_editor': 'block_0004_0002',
            'ejecucion_original_texto_secuencia_y_formato_editor': 'block_0006_0002',
            'ejecucion_original_fuente_compilacion_y_diagnostico_editor': 'block_0003_0003',
        }
        routes = self.reviewed_routes()
        self.assertEqual(10, len(routes))
        for lesson, prediction_identity, prediction_id, old_editor_identity in routes:
            with self.subTest(lesson=lesson, prediction=prediction_identity):
                document = contract.load(contract.CONTENT / lesson)
                previous = published_lesson(lesson)
                state_layout.validate_transition(previous, document)
                blocks = self.visible_state_blocks(document)
                old_blocks = self.visible_state_blocks(previous)
                editor_identity = useful_editors.get(old_editor_identity, old_editor_identity)
                prediction_page, prediction_position, prediction = blocks[prediction_identity]
                editor_page, editor_position, editor = blocks[editor_identity]
                self.assertEqual(prediction_id, prediction.get('id'))
                self.assertEqual('cpp_compiler', editor['type'])
                self.assertEqual(prediction_page, editor_page)
                self.assertLess(prediction_position, editor_position)
                self.assertEqual(editor_position, next(index for index, block in
                    enumerate(document['pages'][editor_page]['blocks']) if block['type'] == 'cpp_compiler'))
                for identity in (prediction_identity, editor_identity):
                    self.assertEqual(state_layout.state_payload(old_blocks[identity][2]),
                                     state_layout.state_payload(blocks[identity][2]))
                self.assertEqual(previous['stateLayout']['origin'], document['stateLayout']['origin'])
                self.assertEqual(previous['stateLayout']['family'], document['stateLayout']['family'])
                if old_editor_identity in useful_editors:
                    self.assertTrue(document['compilerPuzzlesRequired'])
                    self.assertNotIn(old_editor_identity, blocks)
                    archived = next(item for item in document['stateLayout']['retired']
                                    if item['id'] == old_editor_identity)
                    self.assertEqual(old_blocks[old_editor_identity][2], archived['block'])
                    self.assertEqual(previous['pages'], document['stateLayout']['retirementHistory'][0]['pages'])
                    self.assertEqual(previous['stateLayout']['current'],
                                     document['stateLayout']['retirementHistory'][0]['current'])
                    self.assertTrue(set(prediction['answers']).isdisjoint(editor['outputCheck']['cases'][0]['outputs']),
                                    'The useful editor must retain its distinct construction goal.')


if __name__ == '__main__':
    unittest.main()

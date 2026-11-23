"""Las correcciones de navegación no autorizan cambios de respuesta ni fuente."""

import copy
from functools import lru_cache
import hashlib
import json
import os
import re
import subprocess
import unittest

import validar_json_programacion as contract
import stable_state_layout as state_layout
import retirement_fixture_source as retirement_fixtures
import code_format_compat as source_format
from format_batch_history import (
    Publication, active_owners, all_owners,
    assert_stable_storage, load_publication, verify_publication_bytes,
)


BASELINE = 'dd6787d190fcb707a8e935abeadda64584d773d9'
LAYOUT_BASELINE = '25cfe2965b4fb03740795ef1d10d6fd7a0dcb50d'
BEFORE_REFERENCE_RETIREMENT_COMMIT = '52d057d1aad844b133d0d26ff195cbfcbe18b2e1'
BEFORE_REFERENCE_RETIREMENT_SHA256 = {
    'lesson_primer_programa_y_salida.json': '449b62604d3738cc10b54ac0a0b8835f020d36f64cb120e9c9ac28ff3a62118f',
    'lesson_texto_secuencia_y_formato.json': '9b5bce191c5100cacfb7b2c32d213c1c3a71a9362b6af84454c57cd118fd9786',
    'lesson_fuente_compilacion_y_diagnostico.json': '6509a4ab4b7e00d48b10387e45f6a232ffbf6c7e4edf7dd087918de3f7f24904',
}
ALTERNATIVES_LESSON = 'expresiones_y_decisiones/lesson_alternativas_y_fronteras.json'
ALTERNATIVES_RETIREMENT_PARENT = Publication(
    '2ca32b2337178d1effa3a2c81759e2493cd746d0',
    '8fb1dcd0f381205dc20a1a8138edf1ea11fd5091b0d9a7a9349cac1cf48095b6')
ALTERNATIVES_RETIREMENT = Publication(
    '286a2c7d9e383d4a377ba5c379dc8d6e9f95e766',
    '3766c8922c794668d8f6090235ffad9e17d73fcdde613c8038da24fae3d63f59')


def alternatives_retirement_publications():
    relative = (contract.CONTENT / ALTERNATIVES_LESSON).relative_to(contract.ROOT).as_posix()
    return tuple(load_publication(contract.ROOT, relative, publication) for publication in (
        ALTERNATIVES_RETIREMENT_PARENT, ALTERNATIVES_RETIREMENT))


# Immutable complete publications anchor the existing routes. Later published
# versions are discovered from the actual file ancestry, never from timestamps.
SEQUENCE_SOURCE_SHA256 = {
    "programas_y_ejecucion/lesson_primer_programa_y_salida.json": "72165efd0d74dc0374a9df2e4d020db32d1664ad89c6bd972f8452b8d6c7c71d",
    "programas_y_ejecucion/lesson_texto_secuencia_y_formato.json": "026dd83ea21a79a6a7b44107f8be6a3b4838ea7d36f0ecc55d05ddb39d3ed53d",
    "programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json": "eacd78eacf76f9ad68984c3d0eaba45b3c989e74bacf138b94292ecb9c37849d",
    "objetos_y_estado/lesson_leer_una_entrada_conocida.json": "2a6a6cb3e8aa61f46c9ebeacf121aeeac284a19213a48dbe4e15bbb0c7d0e811",
    "objetos_y_estado/lesson_representar_procesar_y_recuperar.json": "fa463b1ac1e9699a04a95775148917f4239e767dec95185f9614c43d6d7d6e6f",
    "expresiones_y_decisiones/lesson_alternativas_y_fronteras.json": "5fe6feb4afcd29f47f17dd06bc188c7fc05b82f1d6ecb2cb21b7d4765270a5e0",
    "funciones_y_contratos/lesson_recursion_finita_y_comparacion.json": "2aef603be78f04c0888f33f7486b8071d0a13e95a86876fa99f574d93f1d64ae",
    "datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json": "7bb304395fb3d39dcc64ae0eb5526447e8499e260b46ef1bc45a85cc28b30aa7",
    "archivos_y_programa_completo/lesson_componer_informes_sin_cambiar_los_datos.json": "7ca010e74ce193fd73b68538e8f248c7317d598ef5f64c123cf4e45afff55f0b",
}
REVIEWED_PUBLICATIONS = {
    "programas_y_ejecucion/lesson_primer_programa_y_salida.json": Publication(
        "befa8bf9cbf15a535bd2be7ba584dad983385dd1", "6f53c1a7af5fb82eba2b405e1eb655a9c4d6658b0326fcdac832ff32b70841e0"),
    "programas_y_ejecucion/lesson_texto_secuencia_y_formato.json": Publication(
        "befa8bf9cbf15a535bd2be7ba584dad983385dd1", "80b21077fa749639d349f17144f764fd66b25cf33e4a81d4f01be920ce7ac3d2"),
    "programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json": Publication(
        "befa8bf9cbf15a535bd2be7ba584dad983385dd1", "66750cbe31cf58231fba48b1ca51cf4064b2775e36386ffd7d5a70d8fa2b382d"),
    "objetos_y_estado/lesson_leer_una_entrada_conocida.json": Publication(
        "b28ff090a8b16ebf7c9571ecfda7f94cf6796023", "37b473d7e9b66ab8232812e4e2c9a25e0aaa207bcfc92059fcd6fc675401d94f"),
    "objetos_y_estado/lesson_representar_procesar_y_recuperar.json": Publication(
        "cdc468168a1d9ed5c1f810ab836cdb47ffcadad1", "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0"),
    "expresiones_y_decisiones/lesson_alternativas_y_fronteras.json": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766", "3766c8922c794668d8f6090235ffad9e17d73fcdde613c8038da24fae3d63f59"),
    "funciones_y_contratos/lesson_recursion_finita_y_comparacion.json": Publication(
        "aa945b8ecbcbddccc0a3adf222b87cab748772bc", "5c5c69ac0f24266311f02e412404e0de02edffccd6656453bf5efb4e024a776e"),
    "datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json": Publication(
        "7fd638c2daeed87a473757c4053a33f6551ab15f", "1c18d9810ec011e774a05b73053c648a7502b6bc6c18d36c9eafd702e9d69810"),
    "archivos_y_programa_completo/lesson_componer_informes_sin_cambiar_los_datos.json": Publication(
        "50efea65f92ad5b3049689ceb1624643cfb7508b", "b5411e9cec3b1e50662c6e71d162b35c4e93913c46678f743059fcbfc157fb99"),
}
# These five original publications predate editorialPrefix. Their six-field
# snapshots remain historical data; the current authoring rule stays strict.
LEGACY_RETIREMENT_PUBLICATIONS = {
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", "93fa96675d370bccd420616ddd079bbf25dc0f7b"): (
        Publication("d1933edf703e19ae0569ae60849b037c48edb687", "72165efd0d74dc0374a9df2e4d020db32d1664ad89c6bd972f8452b8d6c7c71d"),
        Publication("93fa96675d370bccd420616ddd079bbf25dc0f7b", "3b98f6b9ee6ace411c4120e2d2efb2a204acbc3947d1abd86215609f2d2ad07c")),
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", "284cfbf2da5e9deab526e3fe7751c9484ea038d1"): (
        Publication("52d057d1aad844b133d0d26ff195cbfcbe18b2e1", "449b62604d3738cc10b54ac0a0b8835f020d36f64cb120e9c9ac28ff3a62118f"),
        Publication("284cfbf2da5e9deab526e3fe7751c9484ea038d1", "5bfe589ed54c896eaee2d77c113877d31684d2579131dc7e528d25a5d1988b3d")),
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", "93fa96675d370bccd420616ddd079bbf25dc0f7b"): (
        Publication("d1933edf703e19ae0569ae60849b037c48edb687", "026dd83ea21a79a6a7b44107f8be6a3b4838ea7d36f0ecc55d05ddb39d3ed53d"),
        Publication("93fa96675d370bccd420616ddd079bbf25dc0f7b", "6eeca0158eb48c676cebb1fb249a59d36a7c45ae11e0c3c8bb6677032cc48e63")),
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", "284cfbf2da5e9deab526e3fe7751c9484ea038d1"): (
        Publication("52d057d1aad844b133d0d26ff195cbfcbe18b2e1", "9b5bce191c5100cacfb7b2c32d213c1c3a71a9362b6af84454c57cd118fd9786"),
        Publication("284cfbf2da5e9deab526e3fe7751c9484ea038d1", "9763a9f8caab258d1175517d54fe77375fc79205f0379cd34d8629ce95e1a654")),
    ("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", "93fa96675d370bccd420616ddd079bbf25dc0f7b"): (
        Publication("d1933edf703e19ae0569ae60849b037c48edb687", "eacd78eacf76f9ad68984c3d0eaba45b3c989e74bacf138b94292ecb9c37849d"),
        Publication("93fa96675d370bccd420616ddd079bbf25dc0f7b", "6509a4ab4b7e00d48b10387e45f6a232ffbf6c7e4edf7dd087918de3f7f24904")),
}


@lru_cache(maxsize=None)
def git_read(*arguments):
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={contract.ROOT}",
         *arguments], cwd=contract.ROOT, env=environment, stderr=subprocess.PIPE)


@lru_cache(maxsize=None)
def published_versions(lesson):
    """Read complete, connected file publications through this checkout's HEAD.

    A merge with identical file bytes is not a new curricular transition. Each
    changed publication must have a real Git parent whose complete file equals
    the preceding version. Divergent or missing history is an error.
    """
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    baseline = retirement_fixtures.BASELINE_COMMIT
    head = git_read("rev-parse", "--verify", "HEAD").decode("ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{40}", head):
        raise ValueError("Invalid checkout commit")
    git_read("merge-base", "--is-ancestor", baseline, head)
    payload = verify_publication_bytes(
        git_read("show", f"{baseline}:{relative}"),
        Publication(baseline, SEQUENCE_SOURCE_SHA256[lesson]))
    versions = [(baseline, "", payload, b"")]
    commits = git_read("log", "--ancestry-path", "--topo-order", "--reverse",
                      "--format=%H", f"{baseline}..{head}", "--", relative).decode("ascii").splitlines()
    for commit in commits:
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise ValueError("Invalid publication commit in local file history")
        current = git_read("show", f"{commit}:{relative}")
        if current == payload:
            continue
        ancestry = git_read("rev-list", "--parents", "-n", "1", commit).decode("ascii").split()
        if not ancestry or ancestry[0] != commit:
            raise ValueError("Invalid publication parent metadata")
        parent = next((candidate for candidate in ancestry[1:]
                       if git_read("show", f"{candidate}:{relative}") == payload), None)
        if parent is None:
            raise ValueError("Published file history is not connected to its actual parent: " + commit)
        versions.append((commit, parent, current, payload))
        payload = current
    if payload != git_read("show", f"{head}:{relative}"):
        raise ValueError("The complete file publication chain did not reach checkout HEAD")
    reviewed = REVIEWED_PUBLICATIONS[lesson]
    witness = next((raw for commit, _parent, raw, _old in versions
                    if commit == reviewed.commit), None)
    if witness is None:
        raise ValueError("The reviewed publication is missing from this file ancestry")
    verify_publication_bytes(witness, reviewed)
    return tuple(versions)


def historical_contract_blocks(document):
    # Public projection restores authenticated option/case/identifier revisions.
    # It is a lookup of old owners, never a manufactured publication for a
    # transition check. Added owners retain their own active/retired contracts.
    blocks = all_owners(document)
    projected = state_layout.original_projection(document)
    for page, record in zip(projected["pages"], document["stateLayout"]["origin"]):
        for block, owner in zip(page["blocks"], record["blocks"]):
            blocks[owner["id"]] = block
    return blocks


def assert_owner_contract(case, earlier, later, identity):
    before = historical_contract_blocks(earlier)[identity]
    after = historical_contract_blocks(later)[identity]
    if source_format.code_slots(before):
        source_format.require_source_lexical(before, after)
        after = source_format.source_projection(after, before)
    case.assertEqual(state_layout.state_payload(before),
                     state_layout.state_payload(after), identity)


def assert_legacy_retirement(case, lesson, version):
    commit, parent, payload, parent_payload = version
    before_pin, after_pin = LEGACY_RETIREMENT_PUBLICATIONS[lesson, commit]
    case.assertEqual(parent, before_pin.commit)
    verify_publication_bytes(parent_payload, before_pin)
    verify_publication_bytes(payload, after_pin)
    before, after = json.loads(parent_payload), json.loads(payload)
    state_layout.validate(before)
    state_layout.validate(after)
    old_meta, meta = before["stateLayout"], after["stateLayout"]
    old_retired, retired = old_meta.get("retired", []), meta["retired"]
    old_history, history = old_meta.get("retirementHistory", []), meta["retirementHistory"]
    case.assertEqual(retired[:len(old_retired)], old_retired)
    case.assertEqual(history[:len(old_history)], old_history)
    case.assertEqual(len(history), len(old_history) + 1)
    appended = retired[len(old_retired):]
    identities = {item["id"] for item in appended}
    case.assertTrue(identities)
    expected = {
        "retired": [item["id"] for item in appended],
        "current": old_meta["current"], "pages": before["pages"],
        "compilerPuzzlesRequired": before.get("compilerPuzzlesRequired", False),
        "referenceAfter": before.get("referenceAfter", []),
        "activityAfter": before.get("activityAfter", []),
    }
    case.assertEqual(history[-1], expected)
    old_blocks, old_positions = active_owners(before)
    active = active_owners(after)[0]
    case.assertEqual(old_blocks.keys() - active.keys(), identities)
    for item in appended:
        identity = item["id"]
        page, index = old_positions[identity]
        siblings = old_meta["current"][page]["blocks"]
        successor = next((owner for owner in siblings[index + 1:]
                          if owner not in identities), "")
        case.assertEqual(item, {
            "id": identity, "page": old_meta["current"][page]["id"],
            "next": successor, "block": old_blocks[identity]})
    for identity in old_blocks:
        assert_owner_contract(case, before, after, identity)
    case.assertEqual(meta["editorial"][:len(old_meta["editorial"])], old_meta["editorial"])
    case.assertEqual(meta["additions"][:len(old_meta["additions"])], old_meta["additions"])
    assert_stable_storage(case, before, after, old_blocks)


def assert_publication_chain(case, lesson, current):
    versions = published_versions(lesson)
    documents = [json.loads(raw) for _commit, _parent, raw, _old in versions]
    state_layout.validate(documents[0])
    retirements = []
    for previous, document, version in zip(documents, documents[1:], versions[1:]):
        if (lesson, version[0]) in LEGACY_RETIREMENT_PUBLICATIONS:
            assert_legacy_retirement(case, lesson, version)
        else:
            state_layout.validate_transition(previous, document)
        old_retired = previous["stateLayout"].get("retired", [])
        added = document["stateLayout"].get("retired", [])[len(old_retired):]
        if added:
            retirements.append((previous, document, added))
            snapshots = document["stateLayout"]["retirementHistory"]
            case.assertEqual(current["stateLayout"]["retirementHistory"][:len(snapshots)], snapshots)
            current_retired = {item["id"]: item for item in current["stateLayout"]["retired"]}
            for item in added:
                case.assertEqual(current_retired[item["id"]], item)
    state_layout.validate_transition(documents[-1], current)
    assert_stable_storage(case, documents[0], current, all_owners(documents[0]))
    return documents, retirements


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


@lru_cache(maxsize=3)
def first_retired_source(lesson):
    """Freeze the first retirement after its reviewed editorial refinements.

    These are the actual published pages immediately before the later removal
    of the two static references. The live asset cannot stand in for this
    intermediate reading layout when it already contains both retirements.
    """
    prefix = 'programas_y_ejecucion/'
    filename = lesson.removeprefix(prefix)
    if not lesson.startswith(prefix) or filename not in BEFORE_REFERENCE_RETIREMENT_SHA256:
        raise ValueError('Unknown first-retirement lesson: ' + lesson)
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    payload = subprocess.check_output(
        ['git', 'show', BEFORE_REFERENCE_RETIREMENT_COMMIT + ':' + relative], cwd=contract.ROOT)
    if hashlib.sha256(payload).hexdigest() != BEFORE_REFERENCE_RETIREMENT_SHA256[filename]:
        raise RuntimeError('First-retirement fixture SHA-256 mismatch: ' + lesson)
    return payload


def first_retired_lesson(lesson):
    return json.loads(first_retired_source(lesson))


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

    def test_editorial_output_guides_cannot_change_compiler_goal_or_source(self):
        relative, document, before, after = self.compiler_case()
        for key, value in [('outputs', ['Wrong']), ('standardInput', 'Wrong')]:
            candidate = copy.deepcopy(after)
            candidate['outputCheck']['cases'][0][key] = value
            self.assertFalse(contract.preserves_previous_compiler(relative, 4, 2, before, candidate, document))
        candidate = dict(after, initialCode='int main() {}')
        self.assertFalse(contract.preserves_previous_compiler(relative, 4, 2, before, candidate, document))
        # Slot/family/edge validation remains the whole lesson's responsibility.
        self.assertTrue(contract.preserves_previous_compiler(relative, 5, 2, before, after, document))

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
        latest = contract.load(contract.CONTENT / lesson)
        _documents, retirements = assert_publication_chain(self, lesson, latest)
        parent, integrated, _retired = retirements[0]
        self.assertEqual(parent, current)
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
        replacements = {
            'programas_y_ejecucion/lesson_primer_programa_y_salida.json': 'p1u1l2_construir_salida',
            'programas_y_ejecucion/lesson_texto_secuencia_y_formato.json': 'p1u1l3_construir_informe',
            'programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json': 'p1u1l4_reparar_orden_editor',
            'objetos_y_estado/lesson_leer_una_entrada_conocida.json': 'p1u2l9_reparar_lectura_de_cajas',
            ALTERNATIVES_LESSON: 'p1u3l4_reparar_alerta',
        }
        original_modifications = {
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
                documents, retirements = assert_publication_chain(self, lesson, document)
                self.assertEqual(documents[0], previous)
                blocks = self.visible_state_blocks(document)
                old_blocks = self.visible_state_blocks(previous)
                editor_identity = replacements.get(lesson, old_editor_identity)
                prediction_page, prediction_position, prediction = blocks[prediction_identity]
                editor_page, editor_position, editor = blocks[editor_identity]
                self.assertEqual(prediction_id, prediction.get('id'))
                self.assertIn(prediction['type'], ('output_prediction', 'multiple_choice', 'math_puzzle'))
                self.assertEqual('cpp_compiler', editor['type'])
                preparation = (prediction_page, prediction_position)
                destination = (editor_page, editor_position)
                self.assertLess(preparation, destination)
                # Legitimate edits may insert a page or an earlier independent
                # diagnostic. This preparation must still precede its first
                # following editor without a backward step or intervening run.
                following = sorted((page, index, identity) for identity, (page, index, block)
                                   in blocks.items()
                                   if block['type'] == 'cpp_compiler' and (page, index) > preparation)
                self.assertTrue(following)
                self.assertEqual(following[0], (*destination, editor_identity))
                preserved = [prediction_identity, old_editor_identity]
                if old_editor_identity in original_modifications:
                    preserved.append(original_modifications[old_editor_identity])
                for identity in preserved:
                    assert_owner_contract(self, previous, document, identity)
                assert_stable_storage(self, previous, document, preserved)
                if editor_identity != old_editor_identity:
                    self.assertTrue(document['compilerPuzzlesRequired'])
                    self.assertNotIn(old_editor_identity, blocks)
                    witnesses = [(parent, child, item)
                                 for parent, child, additions in retirements
                                 for item in additions if item['id'] == old_editor_identity]
                    self.assertEqual(1, len(witnesses))
                    parent, _child, archived = witnesses[0]
                    self.assertEqual(active_owners(parent)[0][old_editor_identity], archived['block'])
                    self.assertIn(editor_identity,
                                  {item['id'] for item in document['stateLayout']['additions']})
                    stored = state_layout.validate(document)[destination]
                    for identity in preserved[1:]:
                        old_slot = state_layout.validate(previous)[old_blocks[identity][:2]]
                        self.assertNotEqual(old_slot, stored,
                            'A new objective must not overwrite a historical editor answer or draft.')
                    if old_editor_identity in original_modifications:
                        self.assertTrue(set(prediction['answers']).isdisjoint(editor['outputCheck']['cases'][0]['outputs']),
                            'The useful editor must retain its distinct construction or repair goal.')

    def test_alternatives_retirement_rejects_skipping_its_actual_parent(self):
        previous = published_lesson(ALTERNATIVES_LESSON)
        parent, retired = alternatives_retirement_publications()
        state_layout.validate_transition(previous, parent)
        state_layout.validate_transition(parent, retired)
        with self.assertRaisesRegex(state_layout.LayoutError,
                                    'Retirement reading snapshot does not match the complete previous published presentation'):
            state_layout.validate_transition(previous, retired)

    def test_second_retirement_preserves_its_actual_reading_snapshot_and_rejects_skipping_it(self):
        for lesson, *_ in self.reviewed_routes()[:2]:
            with self.subTest(lesson=lesson):
                document = contract.load(contract.CONTENT / lesson)
                _publications, retirements = assert_publication_chain(self, lesson, document)
                self.assertGreaterEqual(len(retirements), 2)
                first_parent, first, first_items = retirements[0]
                second_parent, second, second_items = retirements[1]
                self.assertEqual(second_parent, first_retired_lesson(lesson))
                self.assertEqual(2, len(first_items))
                self.assertEqual(2, len(second_items))
                self.assertEqual(2, len(first['stateLayout']['retired']))
                self.assertEqual(4, len(second['stateLayout']['retired']))
                self.assertEqual(1, len(first['stateLayout']['retirementHistory']))
                self.assertEqual(2, len(second['stateLayout']['retirementHistory']))
                self.assertEqual(second_parent['pages'],
                                 second['stateLayout']['retirementHistory'][1]['pages'])
                self.assertEqual(second_parent['stateLayout']['current'],
                                 second['stateLayout']['retirementHistory'][1]['current'])
                self.assertEqual(first['stateLayout']['retirementHistory'],
                                 second['stateLayout']['retirementHistory'][:1])
                self.assertEqual(second['stateLayout']['retirementHistory'],
                                 document['stateLayout']['retirementHistory'][:2])
                self.assertEqual(second['stateLayout']['retired'],
                                 document['stateLayout']['retired'][:4])
                with self.assertRaisesRegex(state_layout.LayoutError,
                                            'One transition can archive only its actual previous reading layout'):
                    state_layout.validate_transition(first_parent, second)

    def test_legacy_publication_pins_do_not_allow_new_unsealed_retirements(self):
        for lesson, commit in LEGACY_RETIREMENT_PUBLICATIONS:
            version = next(item for item in published_versions(lesson) if item[0] == commit)
            before_pin, after_pin = LEGACY_RETIREMENT_PUBLICATIONS[lesson, commit]
            for payload, pin in ((version[3], before_pin), (version[2], after_pin)):
                with self.subTest(lesson=lesson, publication=pin.commit):
                    with self.assertRaisesRegex(ValueError, 'pinned complete publication'):
                        verify_publication_bytes(payload + b'\n', pin)
        parent, retired = alternatives_retirement_publications()
        forged = copy.deepcopy(retired)
        del forged['stateLayout']['retirementHistory'][-1]['editorialPrefix']
        # Historical six-field records stay readable; a NEW retirement may
        # never claim that legacy contract to bypass its actual source prefix.
        with self.assertRaisesRegex(state_layout.LayoutError,
                                    'Retirement reading snapshot does not match the complete previous published presentation'):
            state_layout.validate_transition(parent, forged)


if __name__ == '__main__':
    unittest.main()


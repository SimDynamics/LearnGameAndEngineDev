"""Published grouped order puzzles can gain movable lines without new history."""
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "aa230c6575210c2154e59dee60ff87cdeb8e615b"
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"
# Published positions are used only to retrieve the immutable fixture. Runtime
# addresses always come from its stable manifest, including added activities.
CASES = (
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 3, 2, "block_0003_0002", 4, 5,
     "19f6a9fbadf97f7da14cfd5508b89d448bdf1d026cf18cb15ed2d9f41844db42", "49b4ac34094d6f9ae7448708c3846c6767f69bc79003aab51d52d8e07cb256db"),
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 4, 2, "block_0004_0002", 4, 6,
     "faeabba361aa86513646db4a8a6fdd23a3b9748cdaf2dd45735cd8b815d38137", "7af1633c846ffb4c47f74c01160757405561000f8dd328aebaf7e50bbf3350f0"),
    ("objetos_y_estado/lesson_declarar_e_inicializar.json", 1, 4, "block_0001_0004", 5, 6,
     "9a3c90b656f78291d7a7ee0eff7b683bd3df56427151c831d47f2be79cbed943", "b10143f422245c051f3fc3fd62e35e7ad9f83152f983ca0001c7e3935469fd6e"),
    ("objetos_y_estado/lesson_estado_asignacion_y_copia.json", 3, 2, "block_0003_0002", 6, 8,
     "a9c7737ad6f2fc8e08dcba55e79f4f8b7f2920bf54ce4d9e03cb2a735e8e5a7a", "764654869128bcfdd09afb3130d8580214ed4d0c8a39ee8d35524a6bb8a0f69b"),
    ("objetos_y_estado/lesson_representar_procesar_y_recuperar.json", 2, 1, "block_0002_0001", 6, 9,
     "2a0834ee64dd3ee521a20d10ac797f7a67a030f9be46f0935b7a69338033e2c7", "26e52720b1d86d0df8e65cf5b7ebb61f625d42cfd32d519b21fb94c15a584cbf"),
    ("expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json", 2, 4, "block_0002_0004", 5, 8,
     "df87aa3c735ad81a3478fa143a954e67ccde33639ddfb1bb2675c332ce060440", "1544430c2e47814fe5b89ae72b5d79c0cdb774e7b5fdb04896a17c415250a2f6"),
    ("expresiones_y_decisiones/lesson_alternativas_y_fronteras.json", 4, 1, "block_0004_0001", 4, 10,
     "1bd409662cbb4c3548814261b4fcd20a70fff45d670ef6ba15df8deae467f0ff", "cbf8c40bf3bec0c6c65899e73d9c81cdb62688616839e4349f33e93da62ef616"),
    ("repeticion_y_terminacion/lesson_acumuladores_contadores_y_trazas.json", 4, 1, "block_0004_0001", 4, 9,
     "5edd38b0d9b09326fd239fad11def223bd0ffe88dd8d5462f2551a05a2744a09", "3f115de3d507f91068046678e00f3c81ed30c38316bfa2062642f40d05633a42"),
    ("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", 5, 4, "block_0005_0004", 7, 11,
     "91fbdf981cc2c89fab55a072d7af464c0f32b85d5558d343c849964c91d7a62b", "b3a10a573631656f423973592492d3abb8598fada98615836c70257c919475ce"),
    ("funciones_y_contratos/lesson_copia_y_ambito.json", 4, 1, "block_0004_0001", 4, 10,
     "a6e77e7630ac8d93d17865a7f46bf3579df69f6bce30a6d330ec940f081344fe", "b4f3773e9abad9dfe1b64ff169988329aa3bf050097531271270de050053a8e0"),
    ("funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json", 2, 5, "orden_validar_antes_de_calcular", 3, 8,
     "61de96bab1ace975d724380200efeed597253bb0d26ee2ad0af2a3eadc0835e1", "826a578461d8716e93c589ceaaedf77dc368a3867838db16adb410f7e55c3e17"),
    ("excepciones_y_recuperacion/lesson_recuperar_por_operacion.json", 2, 3, "block_0002_0003", 5, 19,
     "8b5d9d8e5a76d5cf5de9c36e239df96e8255ac25e0d5f38993b28c3aad8cd313", "b6dea54b808b50183d76e68e35089b57b7c3c7589aeff3e6ce35607ed51168f8"),
    ("archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json", 4, 3, "block_0004_0003", 5, 10,
     "6f79c7acb31e061155fee99058248f4b1bf795f39a647179165073fcc0e3ed90", "6a5c05746b25314bb16b9520bdb1213eca65067ac5b28ea371da7dc98d134003"),
    ("texto_interpretacion_y_flujos/lesson_una_tabla_persistente_de_nombres.json", 4, 2, "block_0004_0002", 3, 5,
     "26afbee5a8a819a627c8aec7e8027b59b6723fb41b8530b87a85f6404d6ddc25", "395588ec7633e33d24e0bf3b41e87e898e94e540eb7c186f634c204f96a26ce3"),
)


@lru_cache(maxsize=None)
def published(path, locale="es"):
    relative = PREFIX.replace("/es/", "/" + locale + "/") + path
    return json.loads(subprocess.check_output(
        ["git", "show", BASELINE + ":" + relative], cwd=contract.ROOT))


def anchored(path):
    source = deepcopy(published(path))
    if source.get("stateLayout", {}).get("version") == 2:
        return source
    family = {locale: published(path, locale) for locale in ("es", "en", "fr")}
    return layout.prepare_v2(source, "es", family)


def current_block(document, identity):
    for page, record in zip(document["pages"], document["stateLayout"]["current"]):
        for block, candidate in zip(page["blocks"], record["blocks"]):
            if candidate == identity:
                return block
    raise AssertionError("Fixture identity missing: " + identity)


def line_contract_digest(block):
    value = {key: block[key] for key in ("fragments", "correctOrder")}
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()


def synthetic(fragments, order=None):
    block = {"type": "order_code", "id": "qa_order", "question": "Ordena las líneas.", "language": "cpp",
             "fragments": fragments, "correctOrder": list(range(len(fragments))) if order is None else order,
             "explanation": "La secuencia conserva el programa."}
    source = {"id": "qa_order_lines", "title": "Ordenación", "pages": [{"blocks": [block]}]}
    return layout.prepare_v2(source, "es", {"es": source})


class OrderLineRefinementTest(unittest.TestCase):
    def assert_semantic_line_boundaries_rejected(self, fragments, order=None):
        source = synthetic(fragments, order)
        before = deepcopy(source)
        with self.assertRaises(layout.LayoutError):
            layout.refine_order_lines(source, "block_0000_0000")
        self.assertEqual(before, source)
        # Reproduce a forged asset produced by the earlier naive splitter. The
        # loader/transition gate must reject it even with self-consistent hashes
        # and a complete snapshot, rather than trusting its authoring helper.
        forged = deepcopy(source)
        old = source["pages"][0]["blocks"][0]
        current = forged["pages"][0]["blocks"][0]
        pieces, groups = [], []
        for fragment in old["fragments"]:
            lines = [line for line in fragment.replace("\r\n", "\n").split("\n") if line.strip(" \t")]
            groups.append(list(range(len(pieces), len(pieces) + len(lines))))
            pieces.extend(lines)
        current["fragments"] = pieces
        current["correctOrder"] = [index for group in old["correctOrder"] for index in groups[group]]
        forged["stateLayout"]["orderRefinements"] = [{"id": "block_0000_0000", "block": deepcopy(old)}]
        forged["stateLayout"]["editorial"].append({"id": "block_0000_0000",
                                                   "beforeSha256": layout.fingerprint(old),
                                                   "afterSha256": layout.fingerprint(current)})
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged)
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(source, forged)

    def test_all_fourteen_published_puzzles_have_exact_independent_line_contracts(self):
        old_total = new_total = blank_total = 0
        for path, pn, bn, identity, old_count, new_count, old_hash, new_hash in CASES:
            with self.subTest(path=path):
                original = deepcopy(published(path))
                source = anchored(path)
                untouched = deepcopy(source)
                before = current_block(source, identity)
                self.assertEqual(published(path)["pages"][pn]["blocks"][bn], before)
                self.assertEqual(old_hash, layout.state_fingerprint(before))
                refined = layout.refine_order_lines(source, identity)
                block = current_block(refined, identity)
                self.assertEqual(untouched, source, "Authoring modified the published source in place")
                self.assertEqual((old_count, new_count), (len(before["fragments"]), len(block["fragments"])))
                self.assertEqual(new_hash, line_contract_digest(block))
                self.assertTrue(all(fragment.strip() and "\n" not in fragment and "\r" not in fragment
                                    for fragment in block["fragments"]))
                self.assertEqual(before, dict(block, fragments=before["fragments"], correctOrder=before["correctOrder"]))
                self.assertEqual(source["stateLayout"].get("orderRefinements", []) + [{"id": identity, "block": before}],
                                 refined["stateLayout"]["orderRefinements"])
                for field in ("origin", "current", "family", "additions", "retired", "retirementHistory"):
                    self.assertEqual(source["stateLayout"].get(field), refined["stateLayout"].get(field))
                self.assertEqual(source["stateLayout"]["editorial"],
                                 refined["stateLayout"]["editorial"][:-1])
                self.assertEqual({"id": identity, "beforeSha256": layout.fingerprint(before),
                                  "afterSha256": layout.fingerprint(block)}, refined["stateLayout"]["editorial"][-1])
                self.assertEqual(layout.validate(source), layout.validate(refined))
                layout.validate_transition(original, refined)
                old_total += old_count
                new_total += new_count
                blank_total += sum(not line.strip() for fragment in before["fragments"] for line in fragment.splitlines())
        self.assertEqual((65, 124, 8), (old_total, new_total, blank_total))

    def test_curricular_guard_accepts_only_a_validated_projection_to_the_published_representation(self):
        for path, pn, bn, identity, *_ in CASES:
            with self.subTest(path=path):
                previous = deepcopy(published(path))
                refined = layout.refine_order_lines(anchored(path), identity)
                block = current_block(refined, identity)
                old_block = previous["pages"][pn]["blocks"][bn]
                self.assertFalse(contract.preserves_previous_exercise(PREFIX + path, pn, bn, old_block, block),
                                 "A detached new block licensed a source/answer change without its provenance")
                aligned = layout.align_for_previous_validation(previous, refined)
                self.assertTrue(contract.preserves_previous_exercise(
                    PREFIX + path, pn, bn, old_block, aligned["pages"][pn]["blocks"][bn]))

    def test_three_unmapped_published_lessons_adopt_the_real_family_before_refinement(self):
        names = [case for case in CASES if "stateLayout" not in published(case[0])]
        self.assertEqual(3, len(names))
        for path, _, _, identity, *_ in names:
            with self.subTest(path=path):
                with self.assertRaises(layout.LayoutError):
                    layout.refine_order_lines(deepcopy(published(path)), identity)
                prepared = anchored(path)
                refined = layout.refine_order_lines(prepared, identity)
                self.assertEqual({"es", "en", "fr"}, set(refined["stateLayout"]["family"]))
                layout.validate_transition(published(path), refined)

    def test_literal_backslash_n_and_indentation_do_not_become_extra_cards(self):
        source = synthetic(['#include <iostream>\r\n\r\nint main() {',
                            '    std::cout << "Sala\\nNorte";', '    return 0;\r\n}'])
        revised = layout.refine_order_lines(source, "block_0000_0000")
        self.assertEqual(['#include <iostream>', 'int main() {', '    std::cout << "Sala\\nNorte";',
                          '    return 0;', '}'], revised["pages"][0]["blocks"][0]["fragments"])
        self.assertEqual(source["pages"][0]["blocks"][0], revised["stateLayout"]["orderRefinements"][0]["block"])
        layout.validate_transition(source, revised)

    def test_compact_table_case_preserves_every_character_and_only_its_exact_form_is_split(self):
        path, _, _, identity, *_ = CASES[-1]
        source = anchored(path)
        revised = layout.refine_order_lines(source, identity)
        lines = current_block(revised, identity)["fragments"]
        self.assertEqual(['if (validar(candidata)) {', ' tabla = candidata;', ' }'], lines[:3])
        self.assertEqual(current_block(source, identity)["fragments"][0], "".join(lines[:3]))
        similar = synthetic(['if (validar(otra)) { tabla = candidata; }', 'uno();\ndos();'])
        other = layout.refine_order_lines(similar, "block_0000_0000")
        self.assertEqual(['if (validar(otra)) { tabla = candidata; }', 'uno();', 'dos();'],
                         other["pages"][0]["blocks"][0]["fragments"])

    def test_source_order_and_identity_mutations_cannot_hide_inside_a_refinement(self):
        path, _, _, identity, *_ = CASES[0]
        source = anchored(path)
        revised = layout.refine_order_lines(source, identity)
        for change in ("character", "indentation", "correctOrder", "missing", "duplicate", "id", "type", "extra"):
            with self.subTest(change=change):
                bad = deepcopy(revised)
                block = current_block(bad, identity)
                if change == "character":
                    block["fragments"][0] += ";"
                elif change == "indentation":
                    block["fragments"][0] = block["fragments"][0].lstrip()
                elif change == "correctOrder":
                    block["correctOrder"][0], block["correctOrder"][1] = block["correctOrder"][1], block["correctOrder"][0]
                elif change == "missing":
                    block["fragments"].pop()
                elif change == "duplicate":
                    block["fragments"].append(block["fragments"][0])
                elif change == "id":
                    block["id"] += "_different"
                elif change == "type":
                    block["type"] = "code"
                else:
                    block["undocumentedAnswerField"] = True
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(source, bad)

    def test_snapshot_identity_and_proof_are_verified_independently(self):
        path, _, _, identity, *_ = CASES[0]
        source = anchored(path)
        revised = layout.refine_order_lines(source, identity)
        for change in ("unknown", "duplicate", "snapshot", "snapshotType", "extra", "proof", "proofBefore", "proofAfter"):
            with self.subTest(change=change):
                bad = deepcopy(revised)
                records = bad["stateLayout"]["orderRefinements"]
                if change == "unknown":
                    records[0]["id"] = "block_9999_9999"
                elif change == "duplicate":
                    records.append(deepcopy(records[0]))
                elif change == "snapshot":
                    records[0]["block"]["fragments"][0] += " changed"
                elif change == "snapshotType":
                    records[0]["block"]["type"] = "cpp_compiler"
                elif change == "extra":
                    records[0]["allowChangedAnswers"] = True
                elif change == "proof":
                    bad["stateLayout"]["editorial"].pop()
                else:
                    key = "beforeSha256" if change == "proofBefore" else "afterSha256"
                    bad["stateLayout"]["editorial"][-1][key] = "0" * 64
                with self.assertRaises(layout.LayoutError):
                    layout.validate(bad)

    def test_origin_family_and_refinement_history_cannot_be_replaced(self):
        path, _, _, identity, *_ = CASES[0]
        source = anchored(path)
        revised = layout.refine_order_lines(source, identity)
        for change in ("origin", "family", "remove", "replace", "revert"):
            with self.subTest(change=change):
                bad = deepcopy(revised)
                if change == "origin":
                    bad["stateLayout"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
                elif change == "family":
                    bad["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                elif change == "remove":
                    del bad["stateLayout"]["orderRefinements"]
                elif change == "replace":
                    bad["stateLayout"]["orderRefinements"][0]["block"]["question"] += " changed"
                else:
                    before = current_block(source, identity)
                    current_block(bad, identity).update(deepcopy(before))
                    del bad["stateLayout"]["orderRefinements"]
                    bad["stateLayout"]["editorial"].pop()
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(revised, bad)

    def test_unknown_identity_non_order_block_and_non_expanding_shape_are_rejected(self):
        path = CASES[0][0]
        source = anchored(path)
        with self.assertRaises(layout.LayoutError):
            layout.refine_order_lines(source, "block_9999_9999")
        with self.assertRaises(layout.LayoutError):
            layout.refine_order_lines(source, source["stateLayout"]["current"][0]["blocks"][0])
        with self.assertRaises(layout.LayoutError):
            layout.refine_order_lines(synthetic(["uno();", "dos();"]), "block_0000_0000")

    def test_reopen_reorder_and_translations_keep_the_same_historical_addresses(self):
        for path, _, _, identity, *_ in CASES:
            with self.subTest(path=path):
                source = anchored(path)
                revised = layout.refine_order_lines(source, identity)
                reopened = json.loads(json.dumps(revised, ensure_ascii=False))
                layout.validate_transition(source, reopened)
                plan = deepcopy(reopened["stateLayout"]["current"])
                page = next(record for record in plan if identity in record["blocks"])
                page["blocks"].reverse()
                moved = layout.reorder(reopened, plan)
                layout.validate_transition(published(path), moved)
                self.assertEqual(line_contract_digest(current_block(revised, identity)),
                                 line_contract_digest(current_block(moved, identity)))
                before_addresses = {candidate: layout.validate(source)[p, b]
                                    for p, record in enumerate(source["stateLayout"]["current"])
                                    for b, candidate in enumerate(record["blocks"])}
                after_addresses = {candidate: layout.validate(moved)[p, b]
                                   for p, record in enumerate(moved["stateLayout"]["current"])
                                   for b, candidate in enumerate(record["blocks"])}
                self.assertEqual(before_addresses, after_addresses)
                translations = [deepcopy(published(path, locale)) for locale in ("en", "fr")]
                frozen = deepcopy(translations)
                layout.check_localized_family([moved, *translations], ["es", "en", "fr"])
                self.assertEqual(frozen, translations)

    def test_refinement_is_idempotent_and_two_independent_snapshots_accumulate(self):
        source = synthetic(["uno();\ndos();", "tres();"])
        original = deepcopy(source)
        first = layout.refine_order_lines(source, "block_0000_0000")
        self.assertEqual(first, layout.refine_order_lines(first, "block_0000_0000"))
        # Two activities intentionally share their shape but retain distinct
        # curricular IDs and historical storage addresses.
        second_block = deepcopy(source["pages"][0]["blocks"][0])
        second_block["id"] = "qa_order_second"
        raw = {"id": "qa_order_two", "title": "Dos actividades", "pages": [{"blocks": [
            deepcopy(source["pages"][0]["blocks"][0]), second_block]}]}
        prepared = layout.prepare_v2(raw, "es", {"es": raw})
        one = layout.refine_order_lines(prepared, "block_0000_0000")
        two = layout.refine_order_lines(one, "block_0000_0001")
        self.assertEqual(one["stateLayout"]["orderRefinements"], two["stateLayout"]["orderRefinements"][:1])
        self.assertEqual(2, len(two["stateLayout"]["orderRefinements"]))
        self.assertEqual(2, len(two["stateLayout"]["editorial"]))
        layout.validate_transition(prepared, two)
        self.assertEqual(original, source)

    def test_real_identical_closures_have_equivalent_text_but_other_cards_do_not(self):
        for case_index in (9, 11):
            path, _, _, identity, *_ = CASES[case_index]
            with self.subTest(path=path):
                revised = layout.refine_order_lines(anchored(path), identity)
                block = current_block(revised, identity)
                pieces, expected = block["fragments"], block["correctOrder"]
                closures = [i for i, text in enumerate(pieces) if text == "}"]
                self.assertEqual(2, len(closures), "Fixture no longer exercises interchangeable exact closures")
                swapped = list(expected)
                a, b = (swapped.index(index) for index in closures)
                swapped[a], swapped[b] = swapped[b], swapped[a]
                self.assertNotEqual(expected, swapped)
                self.assertEqual([pieces[index] for index in expected], [pieces[index] for index in swapped])
                wrong = list(expected)
                wrong[0], wrong[1] = wrong[1], wrong[0]
                self.assertNotEqual([pieces[index] for index in expected], [pieces[index] for index in wrong])
                changed = list(pieces)
                changed[closures[0]] = " }"
                self.assertNotEqual([changed[index] for index in expected], [changed[index] for index in swapped])

    def test_boolean_indices_bare_carriage_returns_and_blank_original_cards_are_rejected(self):
        for fragments, order in ((["uno();\ndos();", "tres();"], [False, 1]),
                                 (["uno();\rdos();", "tres();"], [0, 1]),
                                 (["uno();\ndos();", " \t\n"], [0, 1]),
                                 (["uno();\ndos();", "tres();"], [0, 0])):
            with self.subTest(fragments=fragments, order=order):
                source = synthetic(fragments, order)
                with self.assertRaises(layout.LayoutError):
                    layout.refine_order_lines(source, "block_0000_0000")

    def test_multiline_and_incomplete_raw_strings_reject_authoring_and_forged_proofs(self):
        # Empty lines inside a raw literal are program data, not decorative
        # spacing. Multiline raw literals currently require another contract.
        examples = (
            (['std::cout << R"(uno\n\ndos)";\nfinish();'], None),
            (['std::cout << u8R"tag(uno\ndos)tag";\nfinish();'], None),
            (['dos)tag";\nfinish();', 'std::cout << R"tag(uno'], [1, 0]),
            (['std::cout << R"(sin cierre\nfinish();'], None),
        )
        for fragments, order in examples:
            with self.subTest(fragments=fragments, order=order):
                self.assert_semantic_line_boundaries_rejected(fragments, order)

    def test_single_line_raw_strings_preserve_written_backslash_n_and_delimiters(self):
        for raw in (r'    std::cout << R"(uno\n\ndos)";',
                    r'    std::cout << u8R"tag(uno\ndos)tag";',
                    r'    std::cout << LR"1234567890123456(uno\ndos)1234567890123456";'):
            with self.subTest(raw=raw):
                source = synthetic(['header();\nbody();', raw])
                refined = layout.refine_order_lines(source, "block_0000_0000")
                self.assertEqual(['header();', 'body();', raw], refined["pages"][0]["blocks"][0]["fragments"])
                layout.validate_transition(source, refined)

    def test_semantic_backslash_continuations_reject_lf_crlf_and_group_boundaries(self):
        examples = (
            (['#define PAYLOAD \\\n\nint main() {\n    return 0;\n}'], None),
            (['#define PAYLOAD \\\r\n\r\nint main() {\r\n    return 0;\r\n}'], None),
            (['#define PAYLOAD \\', '\nint main() {\n    return 0;\n}'], None),
            (['\nint main() {\n    return 0;\n}', '#define PAYLOAD \\\r'], [1, 0]),
        )
        for fragments, order in examples:
            with self.subTest(fragments=fragments, order=order):
                self.assert_semantic_line_boundaries_rejected(fragments, order)


if __name__ == "__main__":
    unittest.main()

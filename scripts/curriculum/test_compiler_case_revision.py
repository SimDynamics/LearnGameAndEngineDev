"""Closed compiler expansions keep immutable originals and reviewed output contracts."""
from copy import deepcopy
from functools import lru_cache
from itertools import permutations
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "f410040c7a2b36468f36b15f6d04be7a28b347a7"
PROPOSAL_BASELINE = "c570ceb3643592d16f46c25969cf82202313efe0"
PROPOSAL_LESSONS = frozenset({"terminos_prioridad_y_division_segura",
                             "signos_unarios_parentesis_y_progreso_recursivo",
                             "diagnosticar_y_recuperar_hasta_fin_de_operacion"})
NEW_PROPOSAL_BASELINE = "5cfd5514c879341216c1c34c313e34a16f60ea32"
# These independent pins describe the exact Work payloads, not a second
# authoring allowlist. U11L16's published hashes use sorted compact JSON;
# stateLayout/Windows use the independently checked typed canonical framing.
NEW_COMPILER_PROPOSALS = {
    "construir_un_interprete_con_historial_y_pruebas": {
        "identity": "block_0002_0002", "reference": "block_0009_0001",
        "source_hashes": {"es": "0d126de46acf582f4ff389c38ba48323e126b07d2595bfc333ea92bf5280d14a",
                          "en": "b821159b28e8165c10e4bf6b23361b59d8eba0e1213dde4c3f617952616d86b6",
                          "fr": "853f786bf08cb1ea0434df6ac45026b5c95de6cee09c26097feda5dae92de83b"},
        "compiler_hashes": ("ebf1067d19fbf87bce3ddef2bae69442972df180055d5572faef4473eb76c34e",
                            "7b30da24db7834b2b8cc9ca2014b05de9dfeea69637764e8a9834dbe90b69f5b"),
        "reference_hashes": ("778473599281a1b2165b4dabb9acedde00904b55d9b907c8dedeb9831de20618",
                             "96bcd1d3821c7e80908dd178e855705206d366ead6fbe1831fc891a85969087b"),
        "code_hashes": ("f6b8e87eaf46d2ac7a375e2d2ac7ed5bf0795509fbc3e0d13dba1f7f65460e7c",
                        "0ceebfc2c973d5082e5b84277e78b59fb6f1966ac06f91ca96bbc47ebf09d743"),
        "output_hashes": ("9051c1a5d5b074e8d5309863ae4cf254ca887b1cb6de761ff3521319d5c93e69",
                          "c2d33675495ab7828e098c5a2cc35c18b218692bab843a797b23f13d9ffe47d4"),
        "sizes": (1122, 1518), "lines": (92, 123), "reports": (14, 19),
        "suffix": "Caso 15\nError: Resultado fuera de dominio\nAceptadas: 0\nRechazadas: 1\nSuma: 0\nHistorial:\n"
                  "Caso 16\nError: Anidamiento excesivo\nAceptadas: 0\nRechazadas: 1\nSuma: 0\nHistorial:\n"
                  "Caso 17\nError: Caracter no admitido\nValor: 4\nAceptadas: 1\nRechazadas: 1\nSuma: 4\nHistorial: 4\n"
                  "Caso 18\nValor: 14\nAceptadas: 1\nRechazadas: 0\nSuma: 14\nHistorial: 14\n"
                  "Caso 19\nValor: 3\nAceptadas: 1\nRechazadas: 0\nSuma: 3\nHistorial: 3\n",
        "insert": '    std::cout << "Caso 15\\n";\n    probar("11*10/2;");\n'
                  '    std::cout << "Caso 16\\n";\n    probar("(((((((((1)))))))));");\n'
                  '    std::cout << "Caso 17\\n";\n    probar("?z;4;");\n'
                  '    std::cout << "Caso 18\\n";\n    probar("2+3*4;");\n'
                  '    std::cout << "Caso 19\\n";\n    probar("9-4-2;");\n',
    },
    "resto_y_asociacion_en_el_interprete": {
        "identity": "block_0003_0004", "reference": "block_0006_0001",
        "source_hashes": {"es": "e46f006765a5fa313430b13dd5b1b0a305a6add572beea529fe84f14ab0bf739",
                          "en": "dfe05ab3d615f38091aecbfd3bd8ef49cec0b489cdd55a31c21aa729ad2ec8cf",
                          "fr": "fa5eae36ab2bc32544d37839476e63ae7f687228d80ffcfbbaa7870552a93f87"},
        "compiler_hashes": ("c03fef34aa19969bac10860c7229545551733638be93dcc84104a56e61227b22",
                            "247c41d8494fe203bcc83908dacb2c4e563ae6959e81438452991a30c31c561b"),
        "reference_hashes": ("14fa456ea18c3478fb9fb2476d70f337bdab5ee3cfad5d6b3224b07bc36db0b2",
                             "1e16cb31b5885fa0ae2f3faa6ffd2687391d088f304a035a7cd124278316d2e2"),
        "compact_compiler_hashes": ("b0e9fa06113010a10e7d3066ddfa450c84c9a956f1e405bf5bff951a1963c14b",
                                    "ff4dd34e621e038ddf992c37c8633f1d1b83d15c9896a37e03744bd0e2df2ec3"),
        "compact_reference_hashes": ("e3caf556c2836d4cf662279de660ccc69ec2881409bbf086e3d9875f46c62db3",
                                     "2ecae7604cac2a55e92cf5e5a6c734272f805abcad264c14fcf098e743eddf07"),
        "code_hashes": ("12088f6781f120c6909d5efc0777a22a6d7779778094488bb1f74c30ca36a491",
                        "99f355fda3472200893c645bf689dbaadc3ac72fdeb43bb0de3ec5ce0a27496e"),
        "output_hashes": ("97c94b4d91fd89a68c296eb6bb1f2f5034acd7c31bc74593e4e3a98ba2becbce",
                          "325337278a73b54441d04ecacecda6550cc5515db5c1356f2019061a0f956219"),
        "sizes": (677, 895), "lines": (58, 76), "reports": (9, 12),
        "suffix": "Caso 10\nValor: -2\nAceptadas: 1\nRechazadas: 0\nSuma: -2\nHistorial: -2\n"
                  "Caso 11\nValor: 14\nAceptadas: 1\nRechazadas: 0\nSuma: 14\nHistorial: 14\n"
                  "Caso 12\nError: Anidamiento excesivo\nAceptadas: 0\nRechazadas: 1\nSuma: 0\nHistorial:\n",
        "insert": '    std::cout << "Caso 10\\n";\n    probar("-14/5;");\n'
                  '    std::cout << "Caso 11\\n";\n    probar("2+3*4;");\n'
                  '    std::cout << "Caso 12\\n";\n    probar("(((((((((1)))))))));");\n',
    },
}
STARTER_PROPOSAL = {
    "lesson": layout._CHOICE_PREFIX + "construir_un_interprete_con_historial_y_pruebas",
    "identity": "block_0006_0003", "reference": "block_0010_0001",
    "compiler_hashes": ("b7e77211fde9c243eee599d2711c8cf0cb2f11fbb47de347de298147188a2e47",
                        "7b9c4229709dc9caf07a9c0bd56bf496e62b0148195f69797290cd8ac63be83f"),
    "reference_hashes": ("9f88cd2b376b8b869b2fa3dd6fb569fce823ce3deae64c138a0f9a2c643fcccc",
                         "80ee77eccf110d4438d2643a1dd2c2cb9be696d5f50e57ef285b162e37a0a8a1"),
    "initial_hashes": ("6377abdeb131d6cbed90bc1993da42604854f60669321a3ed236c835faeec2c6",
                       "36a5c835a62bcb62134cbec7f5d0af63424b713f2d58b7f3ac8fccc21b1c6382"),
    "reference_code_hashes": ("4d0fbd8bab1953d8b276dd70b93e00b147256c0c3d892da10af9607d23115941",
                              "7b8ebe1d4dd178ca96c70efd07283f90c57670a0813fa880b30db2b4e91a244b"),
    "output_hashes": ("7be794d71d6246419c02736c73b816b490fed431b4b963634fa254938e8e2f27",
                      "0d08e0407a81f78ae17fb06e038fe3f03443667ecdacbc8b49cc21ec3f4e7d6e"),
    "discarded_output_only": "24819112ab0cd2468968824326b0b7eb4c445bc801dd1e64af7be08d6b3c7794",
    "insert": '    std::cout << "Caso 5\\n";\n    probar("2;2;");\n',
    "suffix": "Caso 5\nValor: 2\nValor: 2\nAceptadas: 2\nRechazadas: 0\nSuma: 4\nHistorial: 2 2\nPositivos: 2\nNegativos: 0\n",
}
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"


def asset_path(lesson):
    return "texto_interpretacion_y_flujos/lesson_" + lesson.split("__")[-1] + ".json"


def baseline(lesson):
    # The new three are authenticated proposals from the current publication;
    # the older four retain their independent pre-integration source commits.
    name = lesson.split("__")[-1]
    if name in NEW_COMPILER_PROPOSALS:
        return NEW_PROPOSAL_BASELINE
    return PROPOSAL_BASELINE if name in PROPOSAL_LESSONS else BASELINE


@lru_cache(maxsize=None)
def published_bytes(lesson, locale="es"):
    payload = subprocess.check_output(["git", "show", baseline(lesson) + ":" + PREFIX.replace("/es/", "/" + locale + "/")
                                      + asset_path(lesson)], cwd=contract.ROOT)
    spec = NEW_COMPILER_PROPOSALS.get(lesson.split("__")[-1])
    if spec is not None and hashlib.sha256(payload).hexdigest() != spec["source_hashes"][locale]:
        raise ValueError("Compiler proposal source differs from its independently pinned complete Git bytes")
    return payload


def published(lesson, locale="es"):
    return json.loads(published_bytes(lesson, locale))


def block(document, identity):
    return layout._blocks(document)[identity]


def revised(lesson, identity):
    return layout.revise_compiler_cases(published(lesson), identity)


def replace_terminal_proof(document, identity):
    proofs = [proof for proof in document["stateLayout"]["editorial"] if proof["id"] == identity]
    proofs[-1]["afterSha256"] = layout.fingerprint(block(document, identity))


def history_block(document, identity):
    snapshot = document["stateLayout"]["retirementHistory"][0]
    return next(candidate for page, record in zip(snapshot["pages"], snapshot["current"])
                for candidate, owner in zip(page["blocks"], record["blocks"]) if owner == identity)


class CompilerCaseRevisionTest(unittest.TestCase):
    def test_second_u11l15_exact_starter_and_expected_and_reference_revision(self):
        plan = STARTER_PROPOSAL
        before = published(plan["lesson"])
        old, old_ref = block(before, plan["identity"]), block(before, plan["reference"])
        self.assertEqual(plan["compiler_hashes"][0], layout.fingerprint(old))
        self.assertEqual(plan["reference_hashes"][0], layout.fingerprint(old_ref))
        after = revised(plan["lesson"], plan["identity"])
        new, new_ref = block(after, plan["identity"]), block(after, plan["reference"])
        self.assertEqual(plan["compiler_hashes"], tuple(layout.fingerprint(item) for item in (old, new)))
        self.assertEqual(plan["reference_hashes"], tuple(layout.fingerprint(item) for item in (old_ref, new_ref)))
        self.assertEqual(49, len(plan["insert"].encode("utf-8")))
        self.assertEqual(101, len(plan["suffix"].encode("utf-8")))
        tail = "    return 0;\n}\n"
        for field, items, hashes in (("initialCode", (old, new), plan["initial_hashes"]),
                                     ("code", (old_ref, new_ref), plan["reference_code_hashes"])):
            self.assertEqual(hashes, tuple(hashlib.sha256(item[field].encode("utf-8")).hexdigest() for item in items))
            self.assertTrue(items[0][field].endswith(tail))
            self.assertEqual(items[0][field][:-len(tail)] + plan["insert"] + tail, items[1][field])
        self.assertEqual((7771, 7820), tuple(len(item["initialCode"].encode("utf-8")) for item in (old, new)))
        self.assertEqual((264, 266), tuple(item["initialCode"].count("\n") for item in (old, new)))
        outputs = [item["outputCheck"]["cases"][0]["outputs"][0] for item in (old, new)]
        self.assertEqual(plan["output_hashes"], tuple(hashlib.sha256(value.encode("utf-8")).hexdigest() for value in outputs))
        self.assertEqual((420, 521), tuple(len(value.encode("utf-8")) for value in outputs))
        self.assertEqual((35, 44), tuple(value.count("\n") for value in outputs))
        self.assertEqual(outputs[0] + plan["suffix"], outputs[1])
        self.assertTrue(all(len(item["outputCheck"]["cases"]) == 1 for item in (old, new)))
        self.assertTrue(all(item["outputCheck"]["cases"][0]["standardInput"] == "" for item in (old, new)))

    def test_two_new_batteries_append_the_exact_authenticated_reports_and_reference_calls(self):
        for name, plan in NEW_COMPILER_PROPOSALS.items():
            with self.subTest(lesson=name):
                lesson, identity = layout._CHOICE_PREFIX + name, plan["identity"]
                before = published(lesson)
                self.assertEqual(plan["compiler_hashes"][0], layout.fingerprint(block(before, identity)))
                self.assertEqual(plan["reference_hashes"][0], layout.fingerprint(block(before, plan["reference"])))
                after = revised(lesson, identity)
                old, new = block(before, identity), block(after, identity)
                old_ref, new_ref = (block(document, plan["reference"]) for document in (before, after))
                self.assertEqual(plan["compiler_hashes"], tuple(layout.fingerprint(item) for item in (old, new)))
                self.assertEqual(plan["reference_hashes"], tuple(layout.fingerprint(item) for item in (old_ref, new_ref)))
                self.assertEqual(plan["code_hashes"], tuple(hashlib.sha256(item["code"].encode("utf-8")).hexdigest()
                                                          for item in (old_ref, new_ref)))
                if "compact_compiler_hashes" in plan:
                    compact = lambda item: hashlib.sha256(json.dumps(item, ensure_ascii=False, sort_keys=True,
                        separators=(",", ":")).encode("utf-8")).hexdigest()
                    self.assertEqual(plan["compact_compiler_hashes"], tuple(compact(item) for item in (old, new)))
                    self.assertEqual(plan["compact_reference_hashes"], tuple(compact(item) for item in (old_ref, new_ref)))
                self.assertEqual(old["initialCode"], new["initialCode"])
                outputs = [item["outputCheck"]["cases"][0]["outputs"][0] for item in (old, new)]
                self.assertTrue(all(len(item["outputCheck"]["cases"]) == 1 for item in (old, new)))
                self.assertTrue(all(item["outputCheck"]["cases"][0]["standardInput"] == "" for item in (old, new)))
                self.assertEqual(plan["sizes"], tuple(len(output.encode("utf-8")) for output in outputs))
                self.assertEqual(plan["lines"], tuple(output.count("\n") for output in outputs))
                self.assertEqual(plan["reports"], tuple(sum(line.startswith("Caso ") for line in output.splitlines()) for output in outputs))
                self.assertEqual(plan["output_hashes"], tuple(hashlib.sha256(output.encode("utf-8")).hexdigest() for output in outputs))
                self.assertEqual(outputs[0] + plan["suffix"], outputs[1])
                tail = "    return 0;\n}\n"
                self.assertTrue(old_ref["code"].endswith(tail))
                self.assertEqual(old_ref["code"][:-len(tail)] + plan["insert"] + tail, new_ref["code"])

    def test_new_expansions_compose_with_exact_math_and_prose_reviews_in_both_orders(self):
        for name, plan in NEW_COMPILER_PROPOSALS.items():
            with self.subTest(lesson=name):
                lesson, identity = layout._CHOICE_PREFIX + name, plan["identity"]
                before = published(lesson)
                family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
                paths = [contract.ROOT / (PREFIX + asset_path(lesson)).replace("/es/", f"/{locale}/")
                         for locale in ("es", "en", "fr")]
                untouched = {path: path.read_bytes() for path in paths}
                mathematical = name == "construir_un_interprete_con_historial_y_pruebas"
                other_identity = "block_0008_0002" if mathematical else "block_0002_0002"
                def editorial(document):
                    draft = deepcopy(document)
                    target = block(draft, other_identity)
                    if mathematical:
                        self.assertEqual("100+1;", target["responses"][0]["sources"][5]["label"])
                        target["responses"][0]["sources"][5]["label"] = "99+2;"
                    else:
                        self.assertEqual("La guardia completa se ejecuta antes del operador %. El diagnóstico impide llegar al cálculo cuando el divisor es cero.", target["explanation"])
                        target["explanation"] = "La guardia completa se ejecuta antes del operador %. Lanzar la excepción impide llegar al cálculo cuando el divisor es cero."
                    return layout.prepare_v2(draft, "es", family, document)
                compiler_first = layout.revise_compiler_cases(before, identity)
                compiler_then_editorial = editorial(compiler_first)
                editorial_first = editorial(before)
                editorial_then_compiler = layout.revise_compiler_cases(editorial_first, identity)
                self.assertEqual(compiler_then_editorial["pages"], editorial_then_compiler["pages"])
                self.assertEqual(compiler_then_editorial["stateLayout"]["compilerRevisions"],
                                 editorial_then_compiler["stateLayout"]["compilerRevisions"])
                for previous, current in ((compiler_first, compiler_then_editorial), (editorial_first, editorial_then_compiler)):
                    layout.validate_transition(previous, current)
                    self.assertEqual(previous["stateLayout"]["editorial"],
                                     current["stateLayout"]["editorial"][:len(previous["stateLayout"]["editorial"])])
                for current in (compiler_then_editorial, editorial_then_compiler):
                    layout.validate_transition(before, current)
                    layout.check_localized_family([current, family["en"], family["fr"]], ["es", "en", "fr"])
                    self.assertEqual(current, layout.revise_compiler_cases(current, identity))
                    for field in before["stateLayout"]:
                        if field != "editorial": self.assertEqual(before["stateLayout"][field], current["stateLayout"][field])
                    for other, original in layout._blocks(before).items():
                        if other not in {identity, plan["reference"], other_identity}: self.assertEqual(original, block(current, other))
                    aligned = layout.align_for_previous_validation(before, current)
                    positions = layout.validate(before)
                    for pn, page in enumerate(before["pages"]):
                        for bn, original in enumerate(page["blocks"]):
                            actual = aligned["pages"][pn]["blocks"][bn]
                            origin_p, origin_b = positions[pn, bn]
                            if original["type"] == "cpp_compiler":
                                self.assertTrue(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), origin_p,
                                    origin_b, original, actual, layout.original_projection(before)))
                            elif original["type"] in contract.EXERCISES | {"code"}:
                                self.assertTrue(contract.preserves_previous_exercise(PREFIX + asset_path(lesson), origin_p,
                                    origin_b, original, actual))
                self.assertEqual(untouched, {path: path.read_bytes() for path in paths})
                # The first compiler and these editorial reviews must not
                # silently adopt the separate second compiler's new starter.
                if mathematical:
                    self.assertEqual(block(before, STARTER_PROPOSAL["identity"]),
                                     block(compiler_then_editorial, STARTER_PROPOSAL["identity"]))

    def test_second_u11l15_rejects_output_only_and_mixed_or_unreviewed_initials(self):
        plan = STARTER_PROPOSAL
        before, good = published(plan["lesson"]), revised(plan["lesson"], plan["identity"])
        self.assertEqual([plan["identity"]], [identity for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items()
            if "initial_insert" in spec and lesson == plan["lesson"]])
        self.assertEqual(1, sum("initial_insert" in spec for spec in layout.COMPILER_CASE_REVISIONS.values()))
        for change in ("output_only", "initial_only", "different_initial", "duplicated_insert", "wrong_calls", "wrong_owner"):
            with self.subTest(change=change):
                bad = deepcopy(good); actual = block(bad, plan["identity"])
                if change == "output_only":
                    actual["initialCode"] = block(before, plan["identity"])["initialCode"]
                    self.assertEqual(plan["discarded_output_only"], layout.fingerprint(actual))
                elif change == "initial_only": actual["outputCheck"] = deepcopy(block(before, plan["identity"])["outputCheck"])
                elif change == "different_initial": actual["initialCode"] += "\n// Otra implementación\n"
                elif change == "duplicated_insert": actual["initialCode"] = actual["initialCode"].replace(plan["insert"], plan["insert"] * 2)
                elif change == "wrong_calls": actual["initialCode"] = actual["initialCode"].replace('probar("2;2;")', 'probar("2;3;")')
                else:
                    actual = block(bad, "block_0002_0002")
                    actual["initialCode"] = actual["initialCode"].replace("    return 0;\n", plan["insert"] + "    return 0;\n")
                    bad["stateLayout"]["editorial"].append({"id": "block_0002_0002",
                        "beforeSha256": layout.fingerprint(block(before, "block_0002_0002")), "afterSha256": layout.fingerprint(actual)})
                if change != "wrong_owner": replace_terminal_proof(bad, plan["identity"])
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
        # A forged old starter is not a published original, even if metadata
        # still quotes its old hash and the current pair is otherwise exact.
        bad = deepcopy(good)
        bad["stateLayout"]["compilerRevisions"][0]["block"]["initialCode"] += "\n"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_both_u11l15_compilers_and_exact_math_review_compose_in_all_six_orders(self):
        plan = STARTER_PROPOSAL; lesson = plan["lesson"]
        before = published(lesson)
        family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
        paths = [contract.ROOT / (PREFIX + asset_path(lesson)).replace("/es/", f"/{locale}/") for locale in ("es", "en", "fr")]
        original_bytes = {path: path.read_bytes() for path in paths}; expected_pages = None
        owners = {"block_0002_0002", plan["identity"]}
        for sequence in permutations(("block_0002_0002", plan["identity"], "math")):
            with self.subTest(order=sequence):
                current = before
                for action in sequence:
                    previous = current
                    if action == "math":
                        draft = deepcopy(previous)
                        block(draft, "block_0008_0002")["responses"][0]["sources"][5]["label"] = "99+2;"
                        current = layout.prepare_v2(draft, "es", family, previous)
                    else: current = layout.revise_compiler_cases(previous, action)
                    layout.validate_transition(previous, current)
                    self.assertEqual(previous["stateLayout"]["editorial"],
                                     current["stateLayout"]["editorial"][:len(previous["stateLayout"]["editorial"])])
                layout.validate_transition(before, current)
                layout.check_localized_family([current, family["en"], family["fr"]], ["es", "en", "fr"])
                self.assertEqual(owners, {entry["id"] for entry in current["stateLayout"]["compilerRevisions"]})
                for entry in current["stateLayout"]["compilerRevisions"]:
                    self.assertEqual(block(before, entry["id"]), entry["block"])
                    reference_id = layout.COMPILER_REFERENCE_REVISIONS[lesson, entry["id"]]["id"]
                    self.assertEqual([{"id": reference_id, "block": block(before, reference_id)}], entry["references"])
                for field in before["stateLayout"]:
                    if field != "editorial": self.assertEqual(before["stateLayout"][field], current["stateLayout"][field])
                for owner in owners: self.assertEqual(current, layout.revise_compiler_cases(current, owner))
                aligned = layout.align_for_previous_validation(before, current)
                for pn, page in enumerate(before["pages"]):
                    for bn, old in enumerate(page["blocks"]):
                        if old["type"] == "cpp_compiler":
                            actual = aligned["pages"][pn]["blocks"][bn]
                            self.assertEqual(old, actual)
                            self.assertTrue(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), pn, bn,
                                old, actual, layout.original_projection(before)))
                if expected_pages is None: expected_pages = current["pages"]
                else: self.assertEqual(expected_pages, current["pages"])
        self.assertEqual(original_bytes, {path: path.read_bytes() for path in paths})

    def test_three_pending_batteries_append_only_the_independently_pinned_reports_and_calls(self):
        # Pins are independent of the authoring allowlist. These remain QA
        # proposals; a report count is not a count of execution receipts.
        plans = [
            ("terminos_prioridad_y_division_segura", "block_0004_0003", "block_0008_0001", 199, 233,
             "a34d52d3096f1ea54d998807701c6d02b943f1c637aca0e2b929a8c76715ae22",
             "5bd75ba1888258cd6d02ceb255724439cbcdfbdee7f65590784caf5bd9b50915",
             '    probar("11*10/2;");\n'),
            ("signos_unarios_parentesis_y_progreso_recursivo", "block_0004_0004", "block_0009_0001", 248, 310,
             "a161812776b989acf7ff05023a94228b5dfd39f0fa6590fbb389f3b33e02e32d",
             "81607dd072f0f3c6867a4b567753b0b131b42476464a74255863dccdc9fd5d43",
             '    probar("11*10/2;");\n    probar("(((((((((1)))))))));");\n'),
            ("diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0004_0004", "block_0008_0001", 357, 406,
             "a4a5cb2f5ff65a572e8803577ac0705409598f65895167e7aa92e5de8c82ba99",
             "9e1ab256574d1c00240942829ea735907caac6af57258c972b49a1c31bc4b30e",
             '    std::cout << "Caso 11\\n";\n    probar("?z;4;");\n'),
        ]
        for name, identity, reference_id, before_size, after_size, before_sha, after_sha, calls in plans:
            with self.subTest(lesson=name):
                lesson = layout._CHOICE_PREFIX + name
                before, after = published(lesson), revised(lesson, identity)
                old, new = block(before, identity), block(after, identity)
                self.assertEqual(1, len(old["outputCheck"]["cases"]))
                self.assertEqual(1, len(new["outputCheck"]["cases"]))
                self.assertEqual("", old["outputCheck"]["cases"][0]["standardInput"])
                self.assertEqual(old.get("initialStandardInput"), new.get("initialStandardInput"))
                old_output, new_output = (item["outputCheck"]["cases"][0]["outputs"][0].encode("utf-8")
                                          for item in (old, new))
                self.assertEqual((before_size, after_size), (len(old_output), len(new_output)))
                self.assertEqual(before_sha, hashlib.sha256(old_output).hexdigest())
                self.assertEqual(after_sha, hashlib.sha256(new_output).hexdigest())
                self.assertTrue(new_output.startswith(old_output))
                tail = "    return 0;\n}\n"
                self.assertEqual(block(before, reference_id)["code"][:-len(tail)] + calls + tail,
                                 block(after, reference_id)["code"])

    def test_ten_exact_expansions_preserve_the_original_program_map_and_translations(self):
        self.assertEqual(10, len(layout.COMPILER_CASE_REVISIONS))
        for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items():
            with self.subTest(lesson=lesson):
                before = published(lesson); untouched = deepcopy(before)
                current = layout.revise_compiler_cases(before, identity)
                old, new = block(before, identity), block(current, identity)
                self.assertEqual(spec["before"], layout.fingerprint(old))
                self.assertEqual(spec["after"], layout.fingerprint(new))
                self.assertEqual(spec["before"], layout.state_fingerprint(new))
                allowed = {"outputCheck"}
                if "initial_insert" in spec:
                    self.assertEqual((STARTER_PROPOSAL["lesson"], STARTER_PROPOSAL["identity"]), (lesson, identity))
                    self.assertEqual(STARTER_PROPOSAL["insert"], spec["initial_insert"])
                    tail = "    return 0;\n}\n"
                    self.assertTrue(old["initialCode"].endswith(tail))
                    self.assertEqual(old["initialCode"][:-len(tail)] + STARTER_PROPOSAL["insert"] + tail, new["initialCode"])
                    allowed.add("initialCode")
                self.assertEqual({key: value for key, value in old.items() if key not in allowed},
                                 {key: value for key, value in new.items() if key not in allowed})
                cases, original = new["outputCheck"]["cases"], old["outputCheck"]["cases"]
                if "append" in spec:
                    self.assertEqual(19, len(original)); self.assertEqual(21, len(cases))
                    self.assertEqual(original, cases[:19]); self.assertEqual(spec["append"], cases[19:])
                    self.assertTrue(all(not case["standardInput"].endswith("\n") for case in cases[19:]))
                else:
                    expected = deepcopy(old["outputCheck"])
                    expected["cases"][0]["outputs"][0] += spec["suffix"]
                    self.assertEqual(expected, new["outputCheck"])
                self.assertEqual(before, untouched)
                self.assertEqual(current, layout.revise_compiler_cases(current, identity))
                archive = {"id": identity, "block": old}
                reference = layout.COMPILER_REFERENCE_REVISIONS.get((lesson, identity))
                if reference is not None:
                    old_reference = block(before, reference["id"])
                    new_reference = block(current, reference["id"])
                    self.assertEqual(reference["before"], layout.fingerprint(old_reference))
                    self.assertEqual(reference["after"], layout.fingerprint(new_reference))
                    archive["references"] = [{"id": reference["id"], "block": old_reference}]
                    self.assertEqual(old_reference, layout.historical_compiler_reference_payload(new_reference))
                self.assertEqual([archive], current["stateLayout"]["compilerRevisions"])
                changed_ids = {identity} | ({reference["id"]} if reference else set())
                for other, previous in layout._blocks(before).items():
                    if other not in changed_ids:
                        self.assertEqual(previous, block(current, other))
                for field in ("origin", "current", "family", "additions", "retired", "retirementHistory", "orderRefinements", "choiceRevisions"):
                    self.assertEqual(before["stateLayout"].get(field), current["stateLayout"].get(field))
                self.assertEqual(layout.validate(before), layout.validate(current))
                layout.validate_transition(before, current)
                layout.check_localized_family([current, published(lesson, "en"), published(lesson, "fr")], ["es", "en", "fr"])

    def test_curricular_guard_only_accepts_the_validated_document_projection(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before, current = published(lesson), revised(lesson, identity)
                _, page, position = identity.split("_"); page, position = int(page), int(position)
                self.assertFalse(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
                    block(before, identity), block(current, identity), before))
                aligned = layout.align_for_previous_validation(before, current)
                self.assertTrue(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
                    block(before, identity), block(aligned, identity), before))
                self.assertEqual(layout.original_projection(before)["pages"],
                                 layout.original_projection(current)["pages"])

    def test_v2_spanish_owner_archive_and_exact_first_editorial_edge_are_required(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            good = revised(lesson, identity)
            def no_map(doc): doc.pop("stateLayout")
            def no_archive(doc): doc["stateLayout"].pop("compilerRevisions")
            def wrong_locale(doc): doc["stateLayout"]["locale"] = "en"
            def wrong_lesson(doc): doc["id"] += "_other"
            def no_proof(doc): doc["stateLayout"]["editorial"] = [p for p in doc["stateLayout"]["editorial"] if p["id"] != identity]
            def wrong_identity(doc): doc["stateLayout"]["compilerRevisions"][0]["id"] = "block_0000_0000"
            def downgrade_v1(doc): doc["stateLayout"] = {key: value for key, value in doc["stateLayout"].items() if key in {"origin", "current"}} | {"version": 1}
            for mutate in (no_map, no_archive, wrong_locale, wrong_lesson, no_proof, wrong_identity, downgrade_v1):
                with self.subTest(lesson=lesson, mutation=mutate.__name__):
                    bad = deepcopy(good); mutate(bad)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
            bad = deepcopy(good); proof = next(p for p in bad["stateLayout"]["editorial"] if p["id"] == identity)
            bad["stateLayout"]["editorial"].remove(proof); middle = "0" * 64
            bad["stateLayout"]["editorial"].extend([{**proof, "afterSha256": middle}, {**proof, "beforeSha256": middle}])
            with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_archive_schema_original_bytes_and_unique_identity_are_protected(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        for value in (None, {}, [{}], [{"id": identity, "block": []}],
                      [{"id": identity, "block": block(published(lesson), identity), "other": True}]):
            with self.subTest(value=value):
                bad = revised(lesson, identity); bad["stateLayout"]["compilerRevisions"] = value
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity); bad["stateLayout"]["compilerRevisions"] *= 2
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity); bad["stateLayout"]["compilerRevisions"][0]["block"]["initialCode"] += "\n"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_sources_inputs_outputs_comparison_and_existing_case_order_stay_protected(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            for change in ("source", "input", "expected", "mode", "case", "id", "type"):
                with self.subTest(lesson=lesson, change=change):
                    bad = revised(lesson, identity); actual = block(bad, identity)
                    cases = actual["outputCheck"]["cases"]
                    if change == "source": actual["initialCode"] += "\n// a different source"
                    elif change == "input": cases[0]["standardInput"] += " "
                    elif change == "expected": cases[0]["outputs"][0] += " "
                    elif change == "mode": actual["outputCheck"]["comparison"] = "tokens"
                    elif change == "case": cases.append(deepcopy(cases[0]))
                    elif change == "id": actual["id"] = "another_compiler"
                    else: actual["type"] = "code"
                    replace_terminal_proof(bad, identity)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        bad = revised(lesson, identity); cases = block(bad, identity)["outputCheck"]["cases"]; cases[0], cases[1] = cases[1], cases[0]
        replace_terminal_proof(bad, identity)
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_metadata_and_expansion_cannot_be_removed_on_later_publication(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            good = revised(lesson, identity)
            with self.subTest(lesson=lesson):
                with self.assertRaises(layout.LayoutError): layout.validate_transition(good, published(lesson))
                bad = deepcopy(good)
                actual = block(bad, identity); actual.clear(); actual.update(deepcopy(block(published(lesson), identity)))
                with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_retirement_snapshots_preserve_the_real_version_before_and_after_expansion(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before = published(lesson)
                archived = layout.retire(before, ["block_0000_0001"])
                current = layout.revise_compiler_cases(archived, identity)
                layout.validate_transition(archived, current)
                self.assertEqual(block(before, identity), history_block(current, identity))
                expanded = revised(lesson, identity); current = layout.retire(expanded, ["block_0000_0001"])
                layout.validate_transition(expanded, current)
                self.assertEqual(block(expanded, identity), history_block(current, identity))

    def test_historical_expansion_cannot_be_fabricated_or_borrow_another_owner(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                forged = layout.retire(published(lesson), ["block_0000_0001"])
                target = history_block(forged, identity); target.clear(); target.update(deepcopy(block(revised(lesson, identity), identity)))
                with self.assertRaisesRegex(layout.LayoutError, "historical revised compiler"): layout.validate(forged)
                current = layout.retire(revised(lesson, identity), ["block_0000_0001"])
                snapshot = current["stateLayout"]["retirementHistory"][0]
                page, position = next((pn, record["blocks"].index(identity))
                                      for pn, record in enumerate(snapshot["current"]) if identity in record["blocks"])
                blocks = snapshot["pages"][page]["blocks"]
                blocks[position - 1], blocks[position] = blocks[position], blocks[position - 1]
                with self.assertRaises(layout.LayoutError): layout.validate(current)

    def test_future_guidance_appends_provenance_but_cannot_weaken_runtime_stderr(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before = revised(lesson, identity); draft = deepcopy(before)
                block(draft, identity)["outputCheck"]["cases"][0]["explanation"] = "Conserva el orden de comprobación."
                block(draft, identity)["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
                family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
                current = layout.prepare_v2(draft, "es", family, previous=before)
                self.assertEqual(before["stateLayout"]["compilerRevisions"], current["stateLayout"]["compilerRevisions"])
                self.assertEqual(before["stateLayout"]["editorial"], current["stateLayout"]["editorial"][:-1])
                rollback = deepcopy(current); block(rollback, identity)["outputCheck"]["cases"][0].pop("standardError")
                rollback["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": layout.fingerprint(block(current, identity)),
                    "afterSha256": layout.fingerprint(block(rollback, identity))})
                layout.validate(rollback)
                with self.assertRaisesRegex(layout.LayoutError, "stderr"): layout.validate_transition(current, rollback)

    def test_reorder_keeps_history_and_refuses_reattachment(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        current = revised(lesson, identity); plan = deepcopy(current["stateLayout"]["current"])
        _, page, position = identity.split("_"); page, position = int(page), int(position)
        plan[page]["blocks"][position - 1], plan[page]["blocks"][position] = plan[page]["blocks"][position], plan[page]["blocks"][position - 1]
        moved = layout.reorder(current, plan); layout.validate_transition(published(lesson), moved)
        self.assertEqual(current["stateLayout"]["compilerRevisions"], moved["stateLayout"]["compilerRevisions"])
        bad = deepcopy(moved); bad["stateLayout"]["compilerRevisions"][0]["id"] = "block_0003_0004"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_helper_requires_the_exact_unmodified_pre_revision_block(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        before = published(lesson)
        with self.assertRaises(layout.LayoutError): layout.revise_compiler_cases(before, "block_0000_0000")
        changed = deepcopy(before)
        block(changed, identity)["outputCheck"]["cases"][0]["explanation"] = "Otra guía publicada."
        changed["stateLayout"]["editorial"].append({"id": identity,
            "beforeSha256": layout.fingerprint(block(before, identity)),
            "afterSha256": layout.fingerprint(block(changed, identity))})
        layout.validate(changed); unchanged = deepcopy(changed)
        with self.assertRaises(layout.LayoutError): layout.revise_compiler_cases(changed, identity)
        self.assertEqual(unchanged, changed)

    def test_compiler_and_choice_revisions_keep_their_independent_archives(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        choice_id = "block_0000_0004"
        compiler_first = revised(lesson, identity)
        both = layout.revise_choice_option(compiler_first, choice_id)
        layout.validate_transition(published(lesson), both)
        self.assertEqual(compiler_first["stateLayout"]["compilerRevisions"], both["stateLayout"]["compilerRevisions"])
        choice_first = layout.revise_choice_option(published(lesson), choice_id)
        alternate = layout.revise_compiler_cases(choice_first, identity)
        layout.validate_transition(choice_first, alternate)
        self.assertEqual(choice_first["stateLayout"]["choiceRevisions"], alternate["stateLayout"]["choiceRevisions"])
        self.assertEqual(both["pages"], alternate["pages"])

    def test_retired_contract_cannot_smuggle_in_an_unarchived_expansion(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before = published(lesson)
                references = [link for link in before.get("referenceAfter", []) if link["activity"] != identity]
                activities = []
                for link in before.get("activityAfter", []):
                    remaining = [value for value in link["requires"] if value != identity]
                    if link["activity"] != identity and remaining:
                        activities.append({**link, "requires": remaining})
                current = layout.retire(before, [identity], reference_after=references, activity_after=activities)
                target = next(item["block"] for item in current["stateLayout"]["retired"] if item["id"] == identity)
                target.clear(); target.update(deepcopy(block(revised(lesson, identity), identity)))
                with self.assertRaisesRegex(layout.LayoutError, "active revision archive"): layout.validate(current)

    def test_new_guidance_is_not_hidden_from_the_ordinary_curricular_guard(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        before = revised(lesson, identity); draft = deepcopy(before)
        block(draft, identity)["outputCheck"]["cases"][0]["explanation"] = "Una explicación nueva."
        family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
        current = layout.prepare_v2(draft, "es", family, previous=before)
        aligned = layout.align_for_previous_validation(published(lesson), current)
        _, page, position = identity.split("_"); page, position = int(page), int(position)
        self.assertEqual("Una explicación nueva.", block(aligned, identity)["outputCheck"]["cases"][0]["explanation"])
        self.assertFalse(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
            block(published(lesson), identity), block(aligned, identity), published(lesson)))

    def test_nine_reference_expansions_require_their_matching_compiler_and_immutable_archive(self):
        self.assertEqual(9, len(layout.COMPILER_REFERENCE_REVISIONS))
        for (lesson, identity), reference in layout.COMPILER_REFERENCE_REVISIONS.items():
            before, good = published(lesson), revised(lesson, identity)
            reference_id = reference["id"]
            def old_reference(doc):
                target = block(doc, reference_id); target.clear(); target.update(deepcopy(block(before, reference_id)))
            def old_compiler(doc):
                target = block(doc, identity); target.clear(); target.update(deepcopy(block(before, identity)))
            def no_reference_archive(doc): doc["stateLayout"]["compilerRevisions"][0].pop("references")
            def wrong_reference_owner(doc): doc["stateLayout"]["compilerRevisions"][0]["references"][0]["id"] = "block_0000_0000"
            def duplicate_reference(doc): doc["stateLayout"]["compilerRevisions"][0]["references"] *= 2
            def forged_original(doc): doc["stateLayout"]["compilerRevisions"][0]["references"][0]["block"]["code"] += "\n"
            def no_reference_proof(doc): doc["stateLayout"]["editorial"] = [p for p in doc["stateLayout"]["editorial"] if p["id"] != reference_id]
            for mutate in (old_reference, old_compiler, no_reference_archive, wrong_reference_owner,
                           duplicate_reference, forged_original, no_reference_proof):
                with self.subTest(lesson=lesson, mutation=mutate.__name__):
                    bad = deepcopy(good); mutate(bad)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
            bad = deepcopy(good); block(bad, reference_id)["code"] += "// unrelated source change\n"
            replace_terminal_proof(bad, reference_id)
            with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_reviewed_reference_alone_cannot_enter_unmapped_or_archived_presentations(self):
        for (lesson, identity), reference in layout.COMPILER_REFERENCE_REVISIONS.items():
            reference_id = reference["id"]; good = revised(lesson, identity)
            with self.subTest(lesson=lesson):
                bad = deepcopy(good); bad.pop("stateLayout")
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
                old = layout.retire(published(lesson), ["block_0000_0001"])
                target = history_block(old, reference_id); target.clear(); target.update(deepcopy(block(good, reference_id)))
                with self.assertRaisesRegex(layout.LayoutError, "historical revised compiler reference"): layout.validate(old)
                current = layout.retire(good, ["block_0000_0001"])
                self.assertEqual(block(good, reference_id), history_block(current, reference_id))
                layout.validate_transition(good, current)

    def test_historical_compiler_and_reference_cannot_mix_before_and_after_versions(self):
        for (lesson, identity), reference in layout.COMPILER_REFERENCE_REVISIONS.items():
            for old_identity in (identity, reference["id"]):
                with self.subTest(lesson=lesson, old_identity=old_identity):
                    current = layout.retire(revised(lesson, identity), ["block_0000_0001"])
                    target = history_block(current, old_identity)
                    target.clear(); target.update(deepcopy(block(published(lesson), old_identity)))
                    with self.assertRaisesRegex(layout.LayoutError, "same published version"):
                        layout.validate(current)

    def test_cli_never_overwrites_source_or_an_existing_proposal(self):
        for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items():
            with self.subTest(lesson=lesson, source="current asset, integrated or pending proposal"):
                source = contract.ROOT / (PREFIX + asset_path(lesson)); source_bytes = source.read_bytes()
                current = json.loads(source_bytes); layout.validate(current)
                archives = [entry for entry in current["stateLayout"].get("compilerRevisions", []) if entry["id"] == identity]
                if archives:
                    self.assertEqual(1, len(archives))
                    self.assertEqual(spec["before"], layout.fingerprint(archives[0]["block"]))
                    self.assertIn({"id": identity, "beforeSha256": spec["before"], "afterSha256": spec["after"]},
                                  current["stateLayout"]["editorial"])
                    expected = current
                else:
                    self.assertIn(lesson.split("__")[-1], PROPOSAL_LESSONS | NEW_COMPILER_PROPOSALS.keys())
                    self.assertEqual(spec["before"], layout.fingerprint(block(current, identity)))
                    expected = layout.revise_compiler_cases(current, identity)
                    self.assertNotEqual(current, expected)
                # Retain every subsequent editorial/choice revision from the
                # current source. Integrated revisions are no-ops; pending
                # proposals must actually expand their pinned original.
                with tempfile.TemporaryDirectory() as directory:
                    output = Path(directory) / "proposal.json"
                    command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-compiler-cases", str(source),
                               "--locale", "es", "--base-ref", baseline(lesson), "--identity", identity, "--output", str(output)]
                    result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                    proposal = output.read_bytes(); self.assertEqual(expected, json.loads(proposal))
                    again = subprocess.run(command, cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, again.returncode); self.assertEqual(proposal, output.read_bytes())
                    overwrite_source = subprocess.run(command[:-1] + [str(source)], cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, overwrite_source.returncode)
                    self.assertEqual(proposal, output.read_bytes())
                self.assertEqual(source_bytes, source.read_bytes())

    def test_cli_really_expands_archived_sources_without_overwriting_their_files(self):
        for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items():
            with self.subTest(lesson=lesson, source="verified historical snapshot"):
                original_bytes = published_bytes(lesson); original = json.loads(original_bytes)
                layout.validate(original)
                self.assertEqual(spec["before"], layout.fingerprint(block(original, identity)))
                expected = revised(lesson, identity)
                self.assertNotEqual(original, expected)  # A no-op cannot prove adoption.
                self.assertEqual(spec["after"], layout.fingerprint(block(expected, identity)))
                layout.validate_transition(original, expected)
                with tempfile.TemporaryDirectory() as directory:
                    source = Path(directory) / "historical-source.json"
                    previous = Path(directory) / "published-reference.json"
                    output = Path(directory) / "proposal.json"
                    # New, temporary QA files only; never roll back the asset.
                    source.write_bytes(original_bytes); previous.write_bytes(original_bytes)
                    command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-compiler-cases", str(source),
                               "--previous", str(previous), "--identity", identity, "--output", str(output)]
                    result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                    proposal = output.read_bytes(); self.assertEqual(expected, json.loads(proposal))
                    again = subprocess.run(command, cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, again.returncode); self.assertEqual(proposal, output.read_bytes())
                    for protected in (source, previous):
                        overwrite = subprocess.run(command[:-1] + [str(protected)], cwd=contract.ROOT, capture_output=True)
                        self.assertNotEqual(0, overwrite.returncode)
                        self.assertEqual(original_bytes, protected.read_bytes())
                    self.assertEqual(original_bytes, source.read_bytes()); self.assertEqual(original_bytes, previous.read_bytes())
                    self.assertEqual(proposal, output.read_bytes())


if __name__ == "__main__":
    unittest.main()

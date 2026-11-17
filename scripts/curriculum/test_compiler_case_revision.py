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

    def test_new_guidance_remains_visible_and_editorial_after_an_archived_case_revision(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        before = revised(lesson, identity); draft = deepcopy(before)
        block(draft, identity)["outputCheck"]["cases"][0]["explanation"] = "Una explicación nueva."
        family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
        current = layout.prepare_v2(draft, "es", family, previous=before)
        layout.validate_transition(before, current)
        aligned = layout.align_for_previous_validation(published(lesson), current)
        _, page, position = identity.split("_"); page, position = int(page), int(position)
        self.assertEqual("Una explicación nueva.", block(aligned, identity)["outputCheck"]["cases"][0]["explanation"])
        self.assertTrue(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
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
                    with self.assertRaisesRegex(layout.LayoutError, "exact editorial prefix"):
                        layout.validate(current)
                    # Legacy captures still reject the mismatched compiler/reference
                    # pair through their original guard when no exact epoch exists.
                    current["stateLayout"]["retirementHistory"][0].pop("editorialPrefix")
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
                # proposals must actually expand their pinned original. The
                # operation uses the current Git publication as its baseline;
                # the separate historical-source test covers old adoption.
                with tempfile.TemporaryDirectory() as directory:
                    output = Path(directory) / "proposal.json"
                    command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-compiler-cases", str(source),
                               "--locale", "es", "--base-ref", "HEAD", "--identity", identity, "--output", str(output)]
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




# General revisions coexist with every closed expansion tested above.
import compiler_case_revision_fixture_source as general_fixtures


class GeneralCompilerCaseRevisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.payloads = general_fixtures.build_fixtures()
        cls.entries = {entry['name']: entry for entry in cls.manifest['cases']}

    def pair(self, name):
        entry = self.entries[name]
        return (json.loads(self.payloads[entry['before']]), json.loads(self.payloads[entry['after']]))

    def reject(self, operation):
        with self.assertRaises((ValueError, KeyError, TypeError, IndexError)):
            operation()

    def test_real_publications_are_byte_pinned_and_references_use_the_existing_assembler(self):
        for name, spec in general_fixtures.SOURCES.items():
            with self.subTest(name=name):
                raw = general_fixtures.historical_bytes(name)
                self.assertEqual(hashlib.sha256(raw).hexdigest(), spec['sha256'])
                self.assertEqual(self.payloads[name + '.before.json'].encode('utf-8'), raw)
                reference = self.payloads[name + '.reference.cpp']
                self.assertEqual(hashlib.sha256(reference.encode('utf-8')).hexdigest(), spec['referenceSha256'])
                self.assertEqual(reference, general_fixtures.reference_source(name))

    def test_real_revisions_change_only_cases_under_the_same_owners(self):
        for name, count in (('u4l4', 16), ('u4l6', 9), ('u4l7', 8)):
            with self.subTest(name=name):
                before, after = self.pair('real-' + name)
                identity = general_fixtures.SOURCES[name]['id']
                old, new = layout._blocks(before)[identity], layout._blocks(after)[identity]
                self.assertEqual(len(new['outputCheck']['cases']), count)
                restored = deepcopy(new)
                restored['outputCheck']['cases'] = deepcopy(old['outputCheck']['cases'])
                self.assertEqual(old, restored)
                for key in ('origin', 'family', 'current', 'additions'):
                    self.assertEqual(before['stateLayout'][key], after['stateLayout'][key])
                for key in ('referenceAfter', 'activityAfter', 'compilerPuzzlesRequired',
                            'compilerWorkflowChecks', 'compilerRuntimeChecks', 'compilerNamedRuleChecks'):
                    self.assertEqual(before.get(key), after.get(key))
                for other, original in layout._blocks(before).items():
                    if other != identity:
                        self.assertEqual(original, layout._blocks(after)[other])
                self.assertEqual(layout.validate(before), layout.validate(after))

    def test_real_first_cases_are_the_requested_discriminants(self):
        expected = {
            'u4l4': ('1\n', '1:1:1\n'),
            'u4l6': ('LF\n', 'LF:2:0:0:0\n'),
            'u4l7': ('1\n4\ntextura 5\nmalla 240\nsonido 8\nsonido 2\n',
                     'textura=1 malla=1 sonido=2 bytes=255\n'),
        }
        for name, (stdin, stdout) in expected.items():
            _, after = self.pair('real-' + name)
            case = layout._blocks(after)[general_fixtures.SOURCES[name]['id']]['outputCheck']['cases'][0]
            self.assertEqual(case['standardInput'], stdin)
            self.assertEqual(case['outputs'], [stdout])
            self.assertEqual(case.get('exitCode', 0), 0)

    def test_complete_original_aggregates_and_editorial_prefix_are_archived(self):
        for name, spec in general_fixtures.SOURCES.items():
            before, after = self.pair('real-' + name)
            item = after['stateLayout']['compilerCaseRevisions'][0]
            self.assertEqual(set(item), {'id', 'block', 'cases', 'editorialIndex', 'historySha256',
                                         'semanticBefore', 'semanticAfter'})
            self.assertEqual(item['block'], layout._blocks(before)[spec['id']])
            self.assertEqual(len(item['block']['outputCheck']['cases']), 4)
            self.assertEqual(item['cases'], layout._blocks(after)[spec['id']]['outputCheck']['cases'])
            self.assertEqual(item['editorialIndex'], len(before['stateLayout']['editorial']))
            self.assertEqual(item['historySha256'], layout.fingerprint({
                'lessonId': before['id'], 'locale': 'es', 'editorial': before['stateLayout']['editorial']}))
            self.assertEqual(after['stateLayout']['editorial'][:-1], before['stateLayout']['editorial'])

    def test_u4l7_retains_all_four_historical_cases_without_duplicate_active_inputs(self):
        before, after = self.pair('real-u4l7')
        identity = general_fixtures.SOURCES['u4l7']['id']
        old = layout._blocks(before)[identity]['outputCheck']['cases']
        current = layout._blocks(after)[identity]['outputCheck']['cases']
        for case in old:
            self.assertEqual(sum(candidate == case for candidate in current), 1)
        self.assertEqual(current[6:], old[:2])
        self.assertEqual(len({case['standardInput'] for case in current}), 8)
        self.assertEqual(after['stateLayout']['sourceFormatRevisions'], before['stateLayout']['sourceFormatRevisions'])

    def test_case_marks_are_archived_and_replaced_explicitly_without_touching_other_fields(self):
        for name, spec in general_fixtures.SOURCES.items():
            before, after = self.pair('real-' + name)
            item = after['stateLayout']['compilerCaseRevisions'][0]
            self.assertEqual(item['semanticBefore'], layout._compiler_case_order_fields(before, spec['id']))
            self.assertEqual(item['semanticAfter'], layout._compiler_case_order_fields(after, spec['id']))
            self.assertTrue(item['semanticAfter'])
            def unrelated(document):
                return [field for field in document.get('textSemantics', {}).get('fields', [])
                        if field['block'] != spec['id'] or not field['path'].startswith('/outputCheck/cases/')]
            self.assertEqual(unrelated(before), unrelated(after))
            self.assertEqual(before.get('textSemantics', {}).get('rootFields'), after.get('textSemantics', {}).get('rootFields'))
            layout.text_semantics.validate(after)

    def test_new_api_requires_explicit_cases_and_semantics_and_rejects_noop(self):
        before, after = self.pair('synthetic-optional-becomes-first')
        owner = general_fixtures.OWNER
        cases = layout._blocks(after)[owner]['outputCheck']['cases']
        self.reject(lambda: layout.revise_compiler_cases(before, owner, cases))
        self.reject(lambda: layout.revise_compiler_cases(before, owner, cases, case_semantics=None))
        self.reject(lambda: layout.revise_compiler_cases(before, owner, None, case_semantics=[]))
        self.reject(lambda: layout.revise_compiler_cases(before, owner,
            layout._blocks(before)[owner]['outputCheck']['cases'],
            case_semantics=layout._compiler_case_order_fields(before, owner)))

    def test_lost_or_invented_authorship_is_rejected_against_the_actual_predecessor(self):
        before, after = self.pair('synthetic-optional-becomes-first')
        bad = deepcopy(after)
        bad['stateLayout']['compilerCaseRevisions'][0]['semanticBefore'] = []
        layout.validate(bad)  # Structural validity cannot certify a prior publication's marks.
        self.reject(lambda: layout.validate_transition(before, bad))
        bad = deepcopy(after)
        field = bad['textSemantics']['fields'][0]
        field['spans'][0]['role'] = 'data' if field['spans'][0]['role'] != 'data' else 'cpp'
        layout.validate(bad)
        self.reject(lambda: layout.validate_transition(before, bad))

    def test_both_journals_are_append_only_even_when_a_rewrite_is_structurally_valid(self):
        entry = self.entries['synthetic-successive']
        intermediate = json.loads(self.payloads[entry['history'][0]['source']])
        _, after = self.pair('synthetic-successive')
        bad = deepcopy(after)
        bad['stateLayout']['compilerCaseRevisions'][0]['semanticAfter'][0]['spans'][0]['reading'] = 'characters'
        layout.validate(bad)
        self.reject(lambda: layout.validate_transition(intermediate, bad))
        before, mixed = self.pair('synthetic-order-cases-order')
        self.assertEqual(len(mixed['stateLayout']['compilerCaseOrderRevisions']), 2)
        self.assertEqual(len(mixed['stateLayout']['compilerCaseRevisions']), 1)
        self.assertNotIn('compilerCaseOrderRevisions', before['stateLayout'])

    def test_mixed_timeline_reverses_each_event_at_its_own_cardinality(self):
        before, after = self.pair('synthetic-order-cases-order')
        owner = general_fixtures.OWNER
        events = layout._compiler_case_events(after, owner)
        self.assertEqual([kind for kind, _ in events], ['order', 'cases', 'order'])
        self.assertEqual([len(item['block']['outputCheck']['cases']) for _, item in events], [3, 3, 4])
        projected = layout._compiler_case_order_projection(after, owner, layout._blocks(after)[owner])
        self.assertEqual(projected, layout._blocks(before)[owner])
        layout.validate_transition(before, after)
        entry = self.entries['synthetic-order-cases-order']
        for stage in entry['history']:
            previous = json.loads(self.payloads[stage['source']])
            layout.validate_transition(previous, after)

    def test_authentic_return_retains_intermediate_cases_and_needs_explicit_return_to(self):
        before, after = self.pair('synthetic-return')
        owner = general_fixtures.OWNER
        entry = self.entries['synthetic-return']
        middle = json.loads(self.payloads[entry['history'][0]['source']])
        self.assertEqual(layout._blocks(before)[owner], layout._blocks(after)[owner])
        self.assertEqual(len(after['stateLayout']['compilerCaseRevisions']), 2)
        self.assertEqual(len(middle['stateLayout']['compilerCaseRevisions'][0]['cases']), 4)
        self.assertIn('returnTo', after['stateLayout']['editorial'][-1])
        old_cases = layout._blocks(before)[owner]['outputCheck']['cases']
        fields = layout._compiler_case_order_fields(before, owner)
        self.reject(lambda: layout.revise_compiler_cases(middle, owner, old_cases, case_semantics=fields))
        self.reject(lambda: layout.revise_compiler_cases(middle, owner, old_cases, case_semantics=fields, return_to=1))
        layout.validate_transition(before, after)

    def test_format_on_either_side_preserves_its_authentic_source_archive(self):
        owner = general_fixtures.OWNER
        for name in ('synthetic-format-before', 'synthetic-format-after'):
            before, after = self.pair(name)
            self.assertEqual(len(after['stateLayout']['sourceFormatRevisions']), 1)
            layout.validate_transition(before, after)
            self.assertNotEqual(layout._blocks(after)[owner]['initialCode'],
                                after['stateLayout']['sourceFormatRevisions'][0]['block']['initialCode'])
        # As with the established order API, a batched authoring check cannot
        # invent an intermediate publication as its first archived predecessor.
        plain = general_fixtures.order_fixtures.synthetic_publication()
        _, formatted_after = self.pair('synthetic-format-before')
        self.reject(lambda: layout.validate_transition(plain, formatted_after))

    def test_other_retirements_keep_the_full_case_list_and_marks_of_each_snapshot(self):
        for name in ('synthetic-retire-before', 'synthetic-retire-after'):
            before, after = self.pair(name)
            snapshot = after['stateLayout']['retirementHistory'][0]
            view = {'pages': snapshot['pages'], 'stateLayout': {'current': snapshot['current']}}
            cases = layout._blocks(view)[general_fixtures.OWNER]['outputCheck']['cases']
            self.assertEqual(len(cases), 3 if name.endswith('before') else 4)
            self.assertIn('textSemantics', snapshot)
            if name.endswith('after'):
                entry = self.entries[name]
                preceding = json.loads(self.payloads[entry['history'][-2]['source']])
                layout.validate_transition(preceding, after)
                self.reject(lambda: layout.validate_transition(before, after))
            else:
                layout.validate_transition(before, after)
        _, after = self.pair('synthetic-optional-becomes-first')
        self.reject(lambda: layout.retire(after, [general_fixtures.OWNER], reference_after=[]))

    def test_later_guides_and_marks_remain_valid_without_rewriting_archives(self):
        for name in ('synthetic-later-guidance', 'synthetic-later-marks'):
            before, after = self.pair(name)
            self.assertEqual(before['stateLayout']['compilerCaseRevisions'], after['stateLayout']['compilerCaseRevisions'])
            layout.validate_transition(before, after)

    def test_intermediate_authorship_changes_require_their_actual_stage_as_baseline(self):
        before, first = self.pair('synthetic-optional-becomes-first')
        revised_guide = general_fixtures.order_fixtures.revised_guidance(first, general_fixtures.OWNER)
        cases = deepcopy(layout._blocks(revised_guide)[general_fixtures.OWNER]['outputCheck']['cases'])
        cases.append({'standardInput': '6\n', 'outputs': ['6\n'], 'explanation': 'Valor de otra comprobación.'})
        second = general_fixtures.revised(revised_guide, general_fixtures.OWNER, cases)
        layout.validate_transition(first, revised_guide)
        layout.validate_transition(revised_guide, second)
        self.reject(lambda: layout.validate_transition(before, second))

    def test_optional_exact_case_can_become_first_but_aggregates_do_not_match_members(self):
        before, after = self.pair('synthetic-optional-becomes-first')
        old = layout._blocks(before)[general_fixtures.OWNER]['outputCheck']['cases']
        new = layout._blocks(after)[general_fixtures.OWNER]['outputCheck']['cases']
        self.assertEqual(new[0], old[2])
        self.assertNotEqual(new[0], old[0])
        for name in ('u4l4', 'u4l6'):
            before, after = self.pair('real-' + name)
            owner = general_fixtures.SOURCES[name]['id']
            old = layout._blocks(before)[owner]['outputCheck']['cases']
            first = layout._blocks(after)[owner]['outputCheck']['cases'][0]
            self.assertTrue(all(case['standardInput'] != first['standardInput'] for case in old))

    def test_same_stdout_does_not_make_different_runtime_requirements_identical(self):
        before, after = self.pair('synthetic-new-runtime-requirements')
        old = layout._blocks(before)[general_fixtures.OWNER]
        new = layout._blocks(after)[general_fixtures.OWNER]
        self.assertEqual(old['initialCode'], new['initialCode'])
        self.assertEqual(old['projectFiles'], new['projectFiles'])
        self.assertEqual(old['cmake'], new['cmake'])
        self.assertEqual(old['outputCheck']['cases'][0]['outputs'], new['outputCheck']['cases'][0]['outputs'])
        for field in ('exitCode', 'standardErrorExact', 'target'):
            self.assertNotEqual(old['outputCheck']['cases'][0][field], new['outputCheck']['cases'][0][field])

    def test_legacy_two_argument_reviewed_expansion_keeps_its_existing_behavior(self):
        fixture = general_fixtures.order_fixtures
        before = fixture.published(fixture.ORIGINAL_COMMIT)
        after = layout.revise_compiler_cases(before, fixture.EXPANDED_OWNER)
        self.assertEqual(len(layout._blocks(before)[fixture.EXPANDED_OWNER]['outputCheck']['cases']), 19)
        self.assertEqual(len(layout._blocks(after)[fixture.EXPANDED_OWNER]['outputCheck']['cases']), 21)
        self.assertIn('compilerRevisions', after['stateLayout'])
        self.assertNotIn('compilerCaseRevisions', after['stateLayout'])

    def test_general_revision_composes_with_the_same_owners_published_closed_expansion(self):
        fixture = general_fixtures.order_fixtures
        entry = self.entries['real-u11-closed-expansion']
        before, after = self.pair(entry['name'])
        expanded = json.loads(self.payloads[entry['history'][0]['source']])
        self.assertEqual(self.payloads[entry['before']].encode('utf-8'),
                         fixture.historical_bytes(fixture.ORIGINAL_COMMIT))
        self.assertEqual(self.payloads[entry['history'][0]['source']].encode('utf-8'),
                         fixture.historical_bytes(fixture.CURRENT_COMMIT))
        owner = fixture.EXPANDED_OWNER
        archived = next(item['block'] for item in after['stateLayout']['compilerRevisions']
                        if item['id'] == owner)
        self.assertEqual(archived, layout._blocks(before)[owner])
        self.assertEqual([len(layout._blocks(document)[owner]['outputCheck']['cases'])
                          for document in (before, expanded, after)], [19, 21, 20])
        self.assertEqual(after['stateLayout']['compilerRevisions'], expanded['stateLayout']['compilerRevisions'])
        self.assertEqual(after['stateLayout']['sourceFormatRevisions'], expanded['stateLayout']['sourceFormatRevisions'])
        self.assertEqual(after['stateLayout']['compilerCaseRevisions'][0]['block'], layout._blocks(expanded)[owner])
        self.assertEqual(layout._compiler_case_order_projection(after, owner, archived), archived)
        aligned = layout.align_for_previous_validation(expanded, after)
        self.assertEqual(layout._blocks(aligned)[owner], layout._blocks(expanded)[owner])
        layout.validate_transition(expanded, after)

    def test_recovered_case_is_absent_from_the_immediate_predecessor_but_archived_exactly(self):
        entry = self.entries['synthetic-case-absent-then-recovered']
        before, after = self.pair(entry['name'])
        middle = json.loads(self.payloads[entry['history'][0]['source']])
        owner = general_fixtures.OWNER
        first = layout._blocks(before)[owner]['outputCheck']['cases'][0]
        current = layout._blocks(after)[owner]['outputCheck']['cases']
        self.assertNotIn(first, layout._blocks(middle)[owner]['outputCheck']['cases'])
        self.assertEqual(current[0], first)
        self.assertEqual(current[1:], layout._blocks(middle)[owner]['outputCheck']['cases'])
        records = after['stateLayout']['compilerCaseRevisions']
        self.assertEqual(records[0]['block']['outputCheck']['cases'][0], first)
        self.assertNotIn(first, records[1]['block']['outputCheck']['cases'])
        for document in (middle, after):
            self.assertEqual(layout._blocks(document)[owner]['initialCode'], layout._blocks(before)[owner]['initialCode'])
        layout.validate_transition(before, after)

    def test_curricular_alignment_restores_the_previous_publication_not_the_latest_epoch(self):
        before, after = self.pair('synthetic-order-cases-order')
        aligned = layout.align_for_previous_validation(before, after)
        self.assertEqual(layout._blocks(aligned)[general_fixtures.OWNER], layout._blocks(before)[general_fixtures.OWNER])
        entry = self.entries['synthetic-order-cases-order']
        intermediate = json.loads(self.payloads[entry['history'][1]['source']])
        aligned = layout.align_for_previous_validation(intermediate, after)
        self.assertEqual(layout._blocks(aligned)[general_fixtures.OWNER], layout._blocks(intermediate)[general_fixtures.OWNER])

    def test_all_shared_invalid_documents_fail_before_projection_can_hide_the_change(self):
        for item in self.manifest['invalid']:
            with self.subTest(name=item['name']):
                self.reject(lambda: layout.validate(json.loads(self.payloads[item['file']])))

    def test_fixture_histories_are_complete_and_each_stage_validates_against_its_actual_source(self):
        self.assertEqual(len(self.manifest['cases']), 18)
        self.assertEqual(len([item for item in self.manifest['invalid']
                              if not item['name'].startswith('client-')]), 37)
        for entry in self.manifest['cases']:
            self.assertEqual(entry['history'][-1]['source'], entry['after'])
            previous = json.loads(self.payloads[entry['before']])
            for index, stage in enumerate(entry['history']):
                current = json.loads(self.payloads[stage['source']])
                if entry['name'] == 'real-u11-closed-expansion' and index == 0:
                    # Both old publications are independently pinned and the
                    # original owner is the exact closed archive. This fixture
                    # does not recreate that historical authoring operation.
                    fixture = general_fixtures.order_fixtures
                    self.assertEqual(current, fixture.published())
                    self.assertEqual(previous, fixture.published(fixture.ORIGINAL_COMMIT))
                    archived = next(item['block'] for item in current['stateLayout']['compilerRevisions']
                                    if item['id'] == entry['id'])
                    self.assertEqual(archived, layout._blocks(previous)[entry['id']])
                else:
                    layout.validate_transition(previous, current)
                previous = current

    def test_cli_authors_a_new_file_with_explicit_cases_and_semantics_without_overwrite(self):
        from contextlib import redirect_stderr, redirect_stdout
        import io
        from unittest.mock import patch
        before, after = self.pair('synthetic-optional-becomes-first')
        item = after['stateLayout']['compilerCaseRevisions'][0]
        with tempfile.TemporaryDirectory(prefix='case-revision-cli-') as directory:
            root = Path(directory)
            source, cases, semantics, output = (root / name for name in ('source.json', 'cases.json', 'semantics.json', 'new.json'))
            source.write_text(json.dumps(before), encoding='utf-8')
            cases.write_text(json.dumps(item['cases']), encoding='utf-8')
            semantics.write_text(json.dumps(item['semanticAfter']), encoding='utf-8')
            original = source.read_bytes()
            argv = ['stable_state_layout.py', 'revise-compiler-cases', str(source), '--previous', str(source),
                    '--identity', general_fixtures.OWNER, '--cases', str(cases), '--case-semantics', str(semantics), '--output', str(output)]
            with patch.object(sys, 'argv', argv), redirect_stdout(io.StringIO()):
                layout.main()
            self.assertEqual(json.loads(output.read_text()), after)
            saved = output.read_bytes()
            with patch.object(sys, 'argv', argv), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                layout.main()
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(output.read_bytes(), saved)


class CompilerClientCaseRevisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.payloads = general_fixtures.build_client_fixtures()
        cls.entry = cls.manifest['clientCases'][0]
        cls.owner = general_fixtures.CLIENT_OWNER

    def stage(self, key):
        return json.loads(self.payloads[self.entry[key]])

    def reject(self, operation):
        with self.assertRaises((ValueError, KeyError, TypeError, IndexError)):
            operation()

    def test_real_source_and_shipped_translations_are_independently_byte_pinned(self):
        for locale, (size, digest) in general_fixtures.CLIENT_SOURCES.items():
            with self.subTest(locale=locale):
                raw = general_fixtures.client_historical_bytes(locale)
                self.assertEqual(len(raw), size)
                self.assertEqual(hashlib.sha256(raw).hexdigest(), digest)
        before = self.stage('before')
        self.assertEqual(self.payloads[self.entry['before']].encode('utf-8'),
                         general_fixtures.client_historical_bytes())
        source = layout._blocks(self.stage('after'))[self.owner]['initialCode']
        self.assertEqual(source, layout._blocks(before)[general_fixtures.CLIENT_INPUT_OWNER]['initialCode'])
        self.assertEqual(hashlib.sha256(source.encode('utf-8')).hexdigest(),
                         'e9f48221b7862810d460cdc7b77078c4b93eb0d8d80791826b8cd27da023fc27')

    def test_transition_changes_only_one_client_alias_and_explicit_individual_cases(self):
        before, after = self.stage('before'), self.stage('after')
        old, new = layout._blocks(before)[self.owner], layout._blocks(after)[self.owner]
        restored = deepcopy(new)
        restored['initialCode'] = old['initialCode']
        restored['projectFiles'][-1]['initialCode'] = old['projectFiles'][-1]['initialCode']
        restored['outputCheck']['cases'] = deepcopy(old['outputCheck']['cases'])
        self.assertEqual(restored, old)
        self.assertEqual(new['sourceFiles'], ['serie.cpp', 'main.cpp'])
        self.assertNotIn('initialStandardInput', new)
        self.assertEqual(new['outputCheck']['inputComparison'], 'tokens')
        self.assertEqual([case['standardInput'] for case in new['outputCheck']['cases']],
                         ['3 5', '0 0', '5 5', '0 40', '40 40'])
        self.assertEqual([case['outputs'] for case in new['outputCheck']['cases']],
            [[value, value + '\n'] for value in ['En curso: 2', 'Terminada: 0', 'Terminada: 0',
                                               'En curso: 40', 'Terminada: 0']])
        for identity, block in layout._blocks(before).items():
            if identity != self.owner:
                self.assertEqual(block, layout._blocks(after)[identity])
        for key in ('origin', 'family', 'current', 'additions', 'sourceFormatRevisions'):
            self.assertEqual(before['stateLayout'][key], after['stateLayout'][key])
        for key in ('referenceAfter', 'activityAfter', 'compilerPuzzlesRequired', 'textSemantics'):
            self.assertEqual(before.get(key), after.get(key))

    def test_complete_predecessor_and_shared_editorial_prefix_authenticate_the_new_client(self):
        before, after = self.stage('before'), self.stage('after')
        item = after['stateLayout']['compilerCaseRevisions'][-1]
        self.assertEqual(set(item), {'id', 'block', 'client', 'cases', 'editorialIndex',
                                    'historySha256', 'semanticBefore', 'semanticAfter'})
        self.assertEqual(item['block'], layout._blocks(before)[self.owner])
        self.assertEqual(item['client'], {'path': 'main.cpp',
                                         'source': layout._blocks(after)[self.owner]['initialCode']})
        self.assertEqual(item['editorialIndex'], len(before['stateLayout']['editorial']))
        self.assertEqual(item['historySha256'], layout.fingerprint({
            'lessonId': before['id'], 'locale': 'es', 'editorial': before['stateLayout']['editorial']}))
        self.assertEqual(after['stateLayout']['editorial'][:-1], before['stateLayout']['editorial'])
        self.assertEqual(layout.validate(before), layout.validate(after))

    def test_old_api_stays_cases_only_and_an_unrecorded_client_cannot_hide_in_its_projection(self):
        before, after = self.stage('before'), self.stage('after')
        client = after['stateLayout']['compilerCaseRevisions'][-1]['client']
        cases = layout._blocks(after)[self.owner]['outputCheck']['cases']
        self.reject(lambda: layout.revise_compiler_cases(before, self.owner, cases,
                    case_semantics=[], client=client))
        cases_only = layout.revise_compiler_cases(before, self.owner, cases, case_semantics=[])
        self.assertEqual(layout._blocks(cases_only)[self.owner]['projectFiles'],
                         layout._blocks(before)[self.owner]['projectFiles'])
        self.assertNotIn('client', cases_only['stateLayout']['compilerCaseRevisions'][-1])
        bad = deepcopy(cases_only)
        layout._compiler_client_replace_source(layout._blocks(bad)[self.owner], 'main.cpp', client['source'])
        bad['stateLayout']['editorial'][-1]['afterSha256'] = layout.fingerprint(layout._blocks(bad)[self.owner])
        self.reject(lambda: layout.validate(bad))

    def test_explicit_cases_semantics_real_client_change_and_exact_alias_are_required(self):
        before, after = self.stage('before'), self.stage('after')
        item = after['stateLayout']['compilerCaseRevisions'][-1]
        self.reject(lambda: layout.revise_compiler_client_cases(before, self.owner, item['client'], item['cases']))
        self.reject(lambda: layout.revise_compiler_client_cases(before, self.owner, item['client'], [], case_semantics=[]))
        self.reject(lambda: layout.revise_compiler_client_cases(after, self.owner, item['client'], item['cases'], case_semantics=[]))
        for client in (None, {'path': 'serie.h', 'source': item['client']['source']},
                       {'path': 'serie.cpp', 'source': item['client']['source']},
                       {'path': 'main.cpp', 'source': ''}, {'path': 'main.cpp', 'source': 'x\0y'},
                       {'path': 'main.cpp', 'source': item['client']['source'], 'automaticDraft': True}):
            with self.subTest(client=client):
                self.reject(lambda: layout.revise_compiler_client_cases(before, self.owner, client,
                            item['cases'], case_semantics=[]))

    def test_project_selection_can_be_implicit_but_paths_and_auxiliaries_are_strict(self):
        original = general_fixtures.synthetic_client_publication()
        def publication(edit):
            raw = deepcopy(original)
            block = layout._blocks(raw)[general_fixtures.OWNER]
            edit(block)
            raw.pop('stateLayout')
            raw.pop('referenceAfter')
            return layout.prepare_v2(raw, 'es', {'es': raw})
        before = publication(lambda block: block.pop('sourceFiles'))
        old = layout._blocks(before)[general_fixtures.OWNER]
        newer = old['initialCode'].replace('int value{};', 'int value{2};')
        changed = general_fixtures.client_revised(before, general_fixtures.OWNER, newer,
                                                 old['outputCheck']['cases'])
        self.assertNotIn('sourceFiles', layout._blocks(changed)[general_fixtures.OWNER])
        invalid = [
            lambda b: b['projectFiles'].append({'path': 'MAIN.cpp', 'initialCode': newer}),
            lambda b: b['projectFiles'].append({'path': 'con.txt', 'initialCode': ''}),
            lambda b: b['projectFiles'].append({'path': 'main.cpp/extra.h', 'initialCode': ''}),
            lambda b: b['projectFiles'][0].__setitem__('path', '../helper.h'),
            lambda b: b['projectFiles'][0].__setitem__('automaticDraft', True),
            lambda b: b['projectFiles'][-1].__setitem__('automaticDraft', True),
            lambda b: b['sourceFiles'].append('main.cpp'),
            lambda b: b.__setitem__('initialCode', 'int main() {}\n'),
        ]
        for edit in invalid:
            broken = publication(edit)
            self.reject(lambda: general_fixtures.client_revised(broken, general_fixtures.OWNER, newer,
                        layout._blocks(broken)[general_fixtures.OWNER]['outputCheck']['cases']))

    def test_diagnostic_objectives_do_not_enter_the_client_api(self):
        before = general_fixtures.synthetic_client_publication()
        bad = deepcopy(before)
        layout._blocks(bad)[general_fixtures.OWNER]['compileCheck'] = {'expected': 'failure'}
        self.reject(lambda: general_fixtures.client_revised(bad, general_fixtures.OWNER, 'int main() {}', []))

    def test_identical_cases_and_returned_source_retain_distinct_authenticated_client_records(self):
        first, changed, returned = (self.stage(key) for key in ('after', 'changedClient', 'returnedClient'))
        for document in (changed, returned):
            self.assertEqual(layout._blocks(document)[self.owner]['outputCheck'],
                             layout._blocks(first)[self.owner]['outputCheck'])
        self.assertEqual(layout._blocks(returned)[self.owner], layout._blocks(first)[self.owner])
        records = returned['stateLayout']['compilerCaseRevisions']
        self.assertEqual(len({layout.fingerprint(item) for item in records}), 3)
        self.assertEqual(records[:2], changed['stateLayout']['compilerCaseRevisions'])
        self.assertIn('returnTo', returned['stateLayout']['editorial'][-1])
        layout.validate_transition(first, returned)

    def test_return_requires_its_actual_earlier_revision_and_never_erases_the_intermediate_client(self):
        changed, returned = self.stage('changedClient'), self.stage('returnedClient')
        source = layout._blocks(returned)[self.owner]['initialCode']
        cases = layout._blocks(returned)[self.owner]['outputCheck']['cases']
        self.reject(lambda: general_fixtures.client_revised(changed, self.owner, source, cases))
        self.reject(lambda: general_fixtures.client_revised(changed, self.owner, source, cases, return_to=0))
        bad = deepcopy(returned)
        bad['stateLayout']['compilerCaseRevisions'].pop(1)
        self.reject(lambda: layout.validate_transition(changed, bad))

    def test_later_cases_only_and_formatting_preserve_the_same_client_epoch_data(self):
        after, cases, formatted = (self.stage(key) for key in ('after', 'casesOnly', 'formattedClient'))
        first_record = after['stateLayout']['compilerCaseRevisions'][0]
        self.assertEqual(cases['stateLayout']['compilerCaseRevisions'][0], first_record)
        self.assertNotIn('client', cases['stateLayout']['compilerCaseRevisions'][-1])
        self.assertEqual(formatted['stateLayout']['compilerCaseRevisions'], [first_record])
        self.assertEqual(len(layout._blocks(cases)[self.owner]['outputCheck']['cases']), 4)
        self.assertEqual(layout._blocks(cases)[self.owner]['outputCheck']['cases'][0],
                         layout._blocks(after)[self.owner]['outputCheck']['cases'][0])
        self.assertEqual(formatted['stateLayout']['sourceFormatRevisions'], after['stateLayout']['sourceFormatRevisions'])
        layout.validate_transition(after, cases)
        layout.validate_transition(after, formatted)

    def test_format_archives_created_in_either_client_epoch_stay_raw_and_authenticated(self):
        documents = {entry['name']: json.loads(self.payloads[entry['file']])
                     for entry in self.manifest['clientValid']}
        for name in ('format-before', 'format-after', 'format-then-client'):
            layout.validate(documents[name])
        before = general_fixtures.synthetic_client_publication()
        after = documents['plain']
        archive = documents['format-after']['stateLayout']['sourceFormatRevisions'][0]['block']
        self.assertEqual(archive, layout._blocks(after)[general_fixtures.OWNER])
        self.assertNotEqual(archive['initialCode'], layout._blocks(before)[general_fixtures.OWNER]['initialCode'])
        self.assertEqual(documents['format-then-client']['stateLayout']['sourceFormatRevisions'][0]['block'], archive)
        layout.validate_transition(before, documents['format-after'])
        layout.validate_transition(before, documents['format-then-client'])

    def test_other_retirements_preserve_the_actual_historical_client_sources(self):
        for entry in self.manifest['clientValid']:
            if not entry['name'].startswith('retirement-'):
                continue
            current = json.loads(self.payloads[entry['file']])
            layout.validate(current)
            snapshot = current['stateLayout']['retirementHistory'][0]
            view = {'pages': snapshot['pages'], 'stateLayout': {'current': snapshot['current']}}
            source = layout._blocks(view)[general_fixtures.OWNER]['initialCode']
            self.assertIn('value{}' if entry['name'].endswith('before') else 'value{1}', source)
        self.reject(lambda: layout.retire(self.stage('after'), [self.owner], reference_after=[]))

    def test_cases_orders_and_client_events_share_one_authenticated_timeline(self):
        after = self.stage('after')
        ordered = layout.reorder_compiler_cases(after, self.owner, [1, 0, 2, 3, 4])
        client = layout._blocks(self.stage('changedClient'))[self.owner]['initialCode']
        current = general_fixtures.client_revised(ordered, self.owner, client,
            layout._blocks(ordered)[self.owner]['outputCheck']['cases'])
        self.assertEqual([kind for kind, _ in layout._compiler_case_events(current, self.owner)],
                         ['cases', 'order', 'cases'])
        layout.validate_transition(after, current)
        self.assertEqual(layout._blocks(layout.align_for_previous_validation(after, current))[self.owner],
                         layout._blocks(after)[self.owner])

    def test_curricular_alignment_and_origin_projection_restore_each_actual_publication(self):
        before, after = self.stage('before'), self.stage('after')
        current = self.stage('returnedClient')
        for previous in (before, after, self.stage('changedClient')):
            aligned = layout.align_for_previous_validation(previous, current)
            self.assertEqual(layout._blocks(aligned)[self.owner], layout._blocks(previous)[self.owner])
        self.assertEqual(layout.original_projection(before)['pages'], layout.original_projection(current)['pages'])
        contract.ERRORS.clear()
        contract.validate_lesson_state_contract(after, 'client-fixture')
        self.assertEqual(contract.ERRORS, [])

    def test_all_shared_client_negatives_fail_before_source_projection(self):
        self.assertEqual(len(self.manifest['invalid']), 20)
        for item in self.manifest['invalid']:
            with self.subTest(name=item['name']):
                self.reject(lambda: layout.validate(json.loads(self.payloads[item['file']])))

    def test_an_earlier_client_event_cannot_disappear_behind_identical_returned_bytes(self):
        before = general_fixtures.synthetic_client_publication()
        owner = general_fixtures.OWNER
        block = layout._blocks(before)[owner]
        cases = block['outputCheck']['cases']
        source = block['initialCode']
        first = general_fixtures.client_revised(before, owner, source.replace('value{}', 'value{1}'), cases)
        earlier = general_fixtures.client_revised(first, owner, source.replace('value{}', 'value{7}'), cases)
        middle = general_fixtures.client_revised(earlier, owner, source.replace('value{}', 'value{2}'), cases)
        returned = general_fixtures.client_revised(middle, owner, source.replace('value{}', 'value{7}'), cases,
                                                  return_to=2)
        layout.validate_transition(before, returned)
        self.assertEqual(layout._blocks(earlier)[owner], layout._blocks(returned)[owner])
        bad = deepcopy(returned)
        bad['stateLayout']['compilerCaseRevisions'].pop(1)
        self.reject(lambda: layout.validate(bad))

    def test_cli_separates_client_authoring_from_cases_only_and_never_overwrites(self):
        from contextlib import redirect_stderr, redirect_stdout
        import io
        from unittest.mock import patch
        before, after = self.stage('before'), self.stage('after')
        item = after['stateLayout']['compilerCaseRevisions'][0]
        with tempfile.TemporaryDirectory(prefix='client-case-revision-cli-') as directory:
            root = Path(directory)
            source, client, cases, semantics, output = (root / name for name in
                ('source.json', 'client.json', 'cases.json', 'semantics.json', 'new.json'))
            for path, value in ((source, before), (client, item['client']),
                                (cases, item['cases']), (semantics, item['semanticAfter'])):
                path.write_text(json.dumps(value), encoding='utf-8')
            original = source.read_bytes()
            argv = ['stable_state_layout.py', 'revise-compiler-client-cases', str(source),
                    '--previous', str(source), '--identity', self.owner, '--client', str(client),
                    '--cases', str(cases), '--case-semantics', str(semantics), '--output', str(output)]
            with patch.object(sys, 'argv', argv), redirect_stdout(io.StringIO()):
                layout.main()
            self.assertEqual(json.loads(output.read_text()), after)
            saved = output.read_bytes()
            for action in ('revise-compiler-client-cases', 'revise-compiler-cases'):
                rejected = list(argv)
                rejected[1] = action
                with patch.object(sys, 'argv', rejected), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    layout.main()
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(output.read_bytes(), saved)


class CompilerClientModeRevisionTests(unittest.TestCase):
    owner = general_fixtures.OWNER

    def publication(self, mode, edit=None):
        document = general_fixtures.order_fixtures.synthetic_publication(marked=False, project=mode == 'cmake')
        references = document.pop('referenceAfter')
        document.pop('stateLayout')
        if edit is not None:
            edit(document['pages'][0]['blocks'][1])
        result = layout.prepare_v2(document, 'es', {'es': document})
        result['referenceAfter'] = references
        layout.validate(result)
        return result

    def source(self, document):
        block = layout._blocks(document)[self.owner]
        if 'projectFiles' not in block:
            return block['initialCode']
        return next(file['initialCode'] for file in block['projectFiles'] if file['path'] == 'main.cpp')

    def revise(self, document, *, source=None, cases=None, path='main.cpp', return_to=None):
        block = layout._blocks(document)[self.owner]
        if source is None:
            source = self.source(document).replace('value{}', 'value{1}')
        return layout.revise_compiler_client_cases(document, self.owner, {'path': path, 'source': source},
            block['outputCheck']['cases'] if cases is None else cases, case_semantics=[], return_to=return_to)

    def reject(self, operation):
        with self.assertRaises((ValueError, KeyError, TypeError, IndexError)):
            operation()

    def test_single_source_changes_only_initial_code_and_explicit_cases(self):
        before = self.publication('single')
        cases = [{'standardInput': '7', 'outputs': ['7\n'], 'exitCode': 0},
                 {'standardInput': '9', 'outputs': ['9\n'], 'exitCode': 0}]
        after = self.revise(before, cases=cases)
        old, new = layout._blocks(before)[self.owner], layout._blocks(after)[self.owner]
        restored = deepcopy(new)
        restored['initialCode'] = old['initialCode']
        restored['outputCheck']['cases'] = old['outputCheck']['cases']
        self.assertEqual(restored, old)
        self.assertFalse({'projectFiles', 'sourceFiles', 'cmake'} & new.keys())
        self.assertEqual(new['initialStandardInput'], old['initialStandardInput'])
        self.assertEqual(after['stateLayout']['compilerCaseRevisions'][0]['block'], old)
        self.assertEqual(layout.original_projection(after)['pages'], layout.original_projection(before)['pages'])
        self.assertEqual(layout._blocks(layout.align_for_previous_validation(before, after))[self.owner], old)

    def test_cmake_preserves_independent_top_level_source_recipe_and_configuration(self):
        for top_source in ('', 'int independent{7};\n', general_fixtures.order_fixtures.SOURCE):
            with self.subTest(top_source=top_source):
                before = self.publication('cmake', lambda b: b.update(initialCode=top_source,
                    cmake={'target': 'alpha', 'buildType': 'Release', 'options': {'MODE': 'ON'}}))
                after = self.revise(before)
                old, new = layout._blocks(before)[self.owner], layout._blocks(after)[self.owner]
                self.assertEqual(new['initialCode'], top_source)
                self.assertEqual(new['cmake'], old['cmake'])
                self.assertEqual(new['projectFiles'][0], old['projectFiles'][0])
                self.assertNotIn('sourceFiles', new)
                restored = deepcopy(new)
                restored['projectFiles'][1] = old['projectFiles'][1]
                self.assertEqual(restored, old)
                self.assertEqual(layout.original_projection(after)['pages'], layout.original_projection(before)['pages'])
                self.assertEqual(layout._blocks(layout.align_for_previous_validation(before, after))[self.owner], old)

    def test_single_source_requires_virtual_path_and_keeps_project_modes_distinct(self):
        before = self.publication('single')
        for path in ('', 'Main.cpp', './main.cpp', 'src/main.cpp', 'main.cc', None):
            with self.subTest(path=path):
                self.reject(lambda: self.revise(before, path=path))
        for edit in (lambda b: b.update(sourceFiles=[]), lambda b: b.update(projectFiles=[]),
                     lambda b: b.update(cmake={'target': 'alpha'})):
            self.reject(lambda: self.revise(self.publication('single', edit), source='int main() {}\n'))

    def test_single_source_limit_counts_utf8_bytes_before_and_after_the_revision(self):
        before = self.publication('single')
        limit = 4 * 1024 * 1024
        boundary = '// ' + 'é' * ((limit - 4) // 2) + '\n'
        self.assertEqual(len(boundary.encode('utf-8')), limit)
        after = self.revise(before, source=boundary)
        self.assertEqual(self.source(after), boundary)
        self.reject(lambda: self.revise(before, source=boundary + ' '))
        self.reject(lambda: self.revise(self.publication('single',
                    lambda b: b.update(initialCode=boundary + ' ')), source='int main() {}\n'))

    def test_single_source_predecessor_must_be_nonempty_but_is_not_trimmed(self):
        empty = self.publication('single', lambda b: b.update(initialCode=''))
        layout.validate(empty)
        self.reject(lambda: self.revise(empty, source='int main() { return 0; }\n'))
        spaces = self.publication('single', lambda b: b.update(initialCode=' \t\n'))
        after = self.revise(spaces, source='int main() { return 0; }\n')
        self.assertEqual(after['stateLayout']['compilerCaseRevisions'][0]['block']['initialCode'], ' \t\n')
        layout.validate_transition(spaces, after)

    def test_both_new_modes_require_changed_nonempty_source_and_explicit_cases(self):
        for mode in ('single', 'cmake'):
            before = self.publication(mode)
            with self.subTest(mode=mode):
                for source in (self.source(before), '', 'int main() {\0}\n', None):
                    client = {'path': 'main.cpp', 'source': source}
                    self.reject(lambda: layout.revise_compiler_client_cases(before, self.owner, client,
                        layout._blocks(before)[self.owner]['outputCheck']['cases'], case_semantics=[]))
                self.reject(lambda: self.revise(before, cases=[]))
                self.reject(lambda: layout.revise_compiler_client_cases(before, self.owner,
                    {'path': 'main.cpp', 'source': 'int main() {}\n'},
                    layout._blocks(before)[self.owner]['outputCheck']['cases']))
                self.reject(lambda: self.revise(self.publication(mode,
                    lambda b: b.update(compileCheck={'expected': 'failure'}))))

    def test_cmake_selects_an_existing_cpp_path_without_reinterpreting_the_recipe(self):
        for path in ('src/client.CPP', 'driver.cc', 'driver.cxx', 'driver.c++'):
            def edit(block):
                block['initialCode'] = ''
                block['projectFiles'][1]['path'] = path
                block['projectFiles'][0]['initialCode'] = '# The learner must complete this recipe.\n'
            before = self.publication('cmake', edit)
            after = self.revise(before, source='int main() { return 0; }\n', path=path)
            self.assertEqual(layout._blocks(after)[self.owner]['projectFiles'][0],
                             layout._blocks(before)[self.owner]['projectFiles'][0])
        before = self.publication('cmake')
        for path in ('CMakeLists.txt', 'main.h', 'MAIN.cpp', '../main.cpp', 'missing.cpp'):
            self.reject(lambda: self.revise(before, path=path))
        self.reject(lambda: self.revise(self.publication('cmake', lambda b: b.update(sourceFiles=['main.cpp']))))
        self.reject(lambda: self.revise(self.publication('cmake',
                    lambda b: b['projectFiles'][0].update(extra='not an authored file field'))))

    def test_cmake_configuration_uses_native_limits_without_changing_legacy_publications(self):
        valid = {'target': 'alpha', 'buildType': 'RelWithDebInfo',
                 'options': {f'Option{i}': 'é' * 2048 if i == 0 else 'ON' for i in range(64)}}
        before = self.publication('cmake', lambda b: b.update(cmake=valid))
        after = self.revise(before)
        self.assertEqual(layout._blocks(after)[self.owner]['cmake'], valid)
        invalid = [None, {}, {'target': 'two words'}, {'target': 'alpha', 'extra': True},
                   {'target': 'alpha', 'buildType': 'debug'}, {'target': 'alpha', 'options': []},
                   {'target': 'alpha', 'options': {f'Option{i}': 'ON' for i in range(65)}},
                   {'target': 'alpha', 'options': {'Mode': 'é' * 2049}}]
        invalid += [{'target': 'alpha', 'options': {name: value}} for name, value in
                    (('cmake_hidden', 'ON'), ('lGp_private', 'ON'), ('0Bad', 'ON'),
                     ('Mode', 1), ('Mode', 'line\nline'), ('Mode', '\x7f'))]
        for config in invalid:
            with self.subTest(config=config if not isinstance(config, dict) else list(config)):
                self.reject(lambda: self.revise(self.publication('cmake', lambda b: b.update(cmake=config))))

    def test_cmake_file_count_and_total_bytes_follow_its_smaller_native_budget(self):
        def counted(count):
            return self.publication('cmake', lambda b: b['projectFiles'].extend(
                {'path': f'notes/{index}.txt', 'initialCode': ''} for index in range(count - 2)))
        self.assertEqual(len(layout._blocks(self.revise(counted(128)))[self.owner]['projectFiles']), 128)
        self.reject(lambda: self.revise(counted(129)))
        limit = 8 * 1024 * 1024
        def sized(block):
            used = sum(len(file['initialCode'].encode('utf-8')) for file in block['projectFiles'])
            block['projectFiles'].append({'path': 'padding.txt', 'initialCode': 'x' * (limit - used)})
        before = self.publication('cmake', sized)
        old = self.source(before)
        after = self.revise(before, source=old.replace('return 0', 'return 1'))
        self.assertEqual(sum(len(file['initialCode'].encode('utf-8')) for file in
                             layout._blocks(after)[self.owner]['projectFiles']), limit)
        self.reject(lambda: self.revise(before, source=old + '\n'))

    def test_client_epochs_cases_only_and_returns_preserve_both_storage_modes(self):
        for mode in ('single', 'cmake'):
            with self.subTest(mode=mode):
                before = self.publication(mode)
                first = self.revise(before)
                middle = self.revise(first, source=self.source(first).replace('value{1}', 'value{2}'))
                self.reject(lambda: self.revise(middle, source=self.source(first)))
                returned = self.revise(middle, source=self.source(first), return_to=1)
                records = returned['stateLayout']['compilerCaseRevisions']
                self.assertEqual(len({layout.fingerprint(item) for item in records}), 3)
                self.assertEqual(layout._blocks(first)[self.owner], layout._blocks(returned)[self.owner])
                layout.validate_transition(before, returned)
                cases = layout._blocks(first)[self.owner]['outputCheck']['cases'][:2]
                cases_only = layout.revise_compiler_cases(first, self.owner, cases, case_semantics=[])
                self.assertEqual(cases_only['stateLayout']['compilerCaseRevisions'][0], records[0])
                self.assertNotIn('client', cases_only['stateLayout']['compilerCaseRevisions'][-1])
                self.assertEqual(self.source(cases_only), self.source(first))
                bad = deepcopy(returned)
                bad['stateLayout']['compilerCaseRevisions'].pop(1)
                self.reject(lambda: layout.validate(bad))

    def test_format_before_and_after_clients_keeps_every_source_in_its_authored_epoch(self):
        for mode in ('single', 'cmake'):
            with self.subTest(mode=mode):
                before = self.publication(mode)
                preformatted = general_fixtures.order_fixtures.formatted(before, self.owner)
                after_format = self.revise(preformatted)
                self.assertEqual(after_format['stateLayout']['sourceFormatRevisions'],
                                 preformatted['stateLayout']['sourceFormatRevisions'])
                first = self.revise(before)
                formatted = general_fixtures.order_fixtures.formatted(first, self.owner)
                second = self.revise(formatted, source=self.source(formatted).replace('value{1}', 'value{2}'))
                archive = formatted['stateLayout']['sourceFormatRevisions'][0]['block']
                self.assertEqual(archive, layout._blocks(first)[self.owner])
                self.assertEqual(second['stateLayout']['sourceFormatRevisions'][0]['block'], archive)
                for current in (after_format, formatted, second):
                    layout.validate(current)
                    self.assertEqual(layout._compiler_case_order_contract(current, self.owner,
                        layout._blocks(current)[self.owner]), layout._blocks(before)[self.owner])
                layout.validate_transition(before, second)

    def test_cmake_top_level_format_is_independent_of_the_revised_file(self):
        before = self.publication('cmake', lambda b: b.update(initialCode='int independent{7};\n'))
        for format_first in (True, False):
            with self.subTest(format_first=format_first):
                first = before if format_first else self.revise(before)
                changed = deepcopy(first)
                layout._blocks(changed)[self.owner]['initialCode'] = 'int  independent{7};\n\n'
                formatted = layout.prepare_v2(changed, 'es', {'es': first}, first)
                current = self.revise(formatted) if format_first else self.revise(formatted,
                    source=self.source(formatted).replace('value{1}', 'value{2}'))
                self.assertEqual(layout._blocks(current)[self.owner]['initialCode'], 'int  independent{7};\n\n')
                self.assertEqual(layout._compiler_case_order_contract(current, self.owner,
                    layout._blocks(current)[self.owner]), layout._blocks(before)[self.owner])
                layout.validate_transition(formatted, current)

    def test_cmake_client_proof_cannot_hide_changes_to_other_sources_or_recipe(self):
        before = self.publication('cmake', lambda b: b.update(initialCode='int independent{7};\n'))
        first = self.revise(before)
        formatted = general_fixtures.order_fixtures.formatted(first, self.owner)
        edits = (lambda b: b.update(initialCode='int independent{8};\n'),
                 lambda b: b['projectFiles'][0].update(initialCode='add_executable(other main.cpp)\n'),
                 lambda b: b['cmake'].update(target='other'),
                 lambda b: b['cmake'].update(options={'MODE': 'ON'}))
        for document in (first, formatted):
            for edit in edits:
                bad = deepcopy(document)
                edit(layout._blocks(bad)[self.owner])
                bad['stateLayout']['editorial'][-1]['afterSha256'] = layout.fingerprint(layout._blocks(bad)[self.owner])
                self.reject(lambda: layout.validate(bad))

    def test_other_retirement_snapshots_keep_the_original_source_mode_and_epoch(self):
        for mode in ('single', 'cmake'):
            before = self.publication(mode)
            first = self.revise(before)
            for source in (before, first):
                with self.subTest(mode=mode, revised=source is first):
                    retired = layout.retire(source, [general_fixtures.NOTE])
                    current = self.revise(retired, source=self.source(source).replace(
                        'value{1}' if source is first else 'value{}', 'value{2}'))
                    layout.validate_transition(source, current)
                    snapshot = current['stateLayout']['retirementHistory'][0]
                    view = {'pages': snapshot['pages'], 'stateLayout': {'current': snapshot['current']}}
                    self.assertEqual(layout._blocks(view)[self.owner], layout._blocks(source)[self.owner])
                    self.assertEqual(snapshot['editorialPrefix']['count'], len(source['stateLayout']['editorial']))


class CompilerClientModeFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.payloads = general_fixtures.build_client_mode_fixtures()
        cls.entries = {entry['mode']: entry for entry in cls.manifest['clientModes']}

    def stage(self, mode, key):
        return json.loads(self.payloads[self.entries[mode][key]])

    def test_real_mode_publications_and_translations_are_exactly_pinned(self):
        expected = {
            'single': {'es': (96331, 'cc0f934ce40085ef095c10ac08cdc44290e75e6ecc60d6444d86f979877f18df'),
                       'en': (21665, '74998e2ac1eef4336177ccbfd7d72535c6a63a1b84cd30892e811ae28a58de0b'),
                       'fr': (23504, 'add47016262fb54830a4e0a4101842051bb0c94e31e50113910b97a29da363a9')},
            'cmake': {'es': (193752, '3903d921c21510dfa19570683c39bb0b6431367c94872cae0a85d89e9e9b2979'),
                      'en': (41868, 'c9f5fe440c7f76a6027652db3e9927086a0e85dcb149ead70db8abe19217aa9d'),
                      'fr': (44962, 'fe7bc29fb6a2ecf8a33a6203e8db61346edc78b4d87a0edb1309a0c943ecffb1')}}
        self.assertEqual(set(self.entries), set(expected))
        for mode, locales in expected.items():
            self.assertEqual(general_fixtures.CLIENT_MODE_SOURCES[mode]['locales'], locales)
            for locale, (size, digest) in locales.items():
                with self.subTest(mode=mode, locale=locale):
                    raw = general_fixtures.client_mode_historical_bytes(mode, locale)
                    self.assertEqual((len(raw), hashlib.sha256(raw).hexdigest()), (size, digest))
            self.assertEqual(self.payloads[self.entries[mode]['before']].encode('utf-8'),
                             general_fixtures.client_mode_historical_bytes(mode))

    def test_real_transitions_preserve_faults_auxiliaries_recipes_and_only_separate_cases(self):
        expected = {'single': [('5 5 1', ''), ('4 5 1', ''), ('6 5 0', '')],
                    'cmake': [('4 7', '3'), ('7 7', '0'), ('9 7', '0')]}
        for mode in expected:
            with self.subTest(mode=mode):
                owner = self.entries[mode]['id']
                before, after = self.stage(mode, 'before'), self.stage(mode, 'after')
                old, new = layout._blocks(before)[owner], layout._blocks(after)[owner]
                self.assertEqual(new['outputCheck']['cases'], [
                    {'standardInput': stdin, 'outputs': [stdout], 'exitCode': 0} for stdin, stdout in expected[mode]])
                restored = deepcopy(new)
                if mode == 'single':
                    restored['initialCode'] = old['initialCode']
                    self.assertIn('return disponibles < minimo;', new['initialCode'])
                    self.assertIn('std::cin >> disponibles >> minimo >> esperado;', new['initialCode'])
                    self.assertFalse({'projectFiles', 'sourceFiles', 'cmake'} & new.keys())
                    self.assertEqual(old['initialCode'].split('\nint main() {')[0],
                                     new['initialCode'].split('\nint main() {')[0])
                else:
                    self.assertEqual(new['initialCode'], '')
                    self.assertEqual(new['cmake'], old['cmake'])
                    for index, file in enumerate(old['projectFiles']):
                        if file['path'] == 'main.cpp':
                            restored['projectFiles'][index] = file
                        else:
                            self.assertEqual(new['projectFiles'][index], file)
                    self.assertIn('STATIC', old['projectFiles'][0]['initialCode'])
                    self.assertTrue(any('+ 1' in file['initialCode'] for file in old['projectFiles']))
                restored['outputCheck']['cases'] = old['outputCheck']['cases']
                self.assertEqual(restored, old)
                for identity, block in layout._blocks(before).items():
                    if identity != owner:
                        self.assertEqual(layout._blocks(after)[identity], block)
                for field in ('referenceAfter', 'activityAfter', 'compilerPuzzlesRequired',
                              'compilerWorkflowChecks', 'compilerCmakeGraphChecks', 'compilerNamedRuleChecks'):
                    self.assertEqual(before.get(field), after.get(field))

    def test_six_real_publications_keep_client_epochs_and_cases_only_authoring_separate(self):
        for mode, entry in self.entries.items():
            with self.subTest(mode=mode):
                owner = entry['id']
                before, first, cases, changed, returned, formatted = [self.stage(mode, key) for key in
                    ('before', 'after', 'casesOnly', 'changedClient', 'returnedClient', 'formattedClient')]
                self.assertEqual(layout._blocks(cases)[owner]['outputCheck']['cases'],
                                 layout._blocks(first)[owner]['outputCheck']['cases'][:2])
                self.assertNotIn('client', cases['stateLayout']['compilerCaseRevisions'][-1])
                self.assertEqual(layout._blocks(returned)[owner], layout._blocks(first)[owner])
                self.assertEqual(layout._blocks(changed)[owner]['outputCheck'], layout._blocks(first)[owner]['outputCheck'])
                self.assertIn('returnTo', returned['stateLayout']['editorial'][-1])
                self.assertEqual(len({layout.fingerprint(item) for item in
                    returned['stateLayout']['compilerCaseRevisions']}), 3)
                self.assertEqual(formatted['stateLayout']['compilerCaseRevisions'], first['stateLayout']['compilerCaseRevisions'])
                for previous, current in ((before, first), (first, cases), (first, changed),
                                          (changed, returned), (first, formatted), (before, returned)):
                    layout.validate_transition(previous, current)
                    self.assertEqual(layout._blocks(layout.align_for_previous_validation(previous, current))[owner],
                                     layout._blocks(previous)[owner])
                for document in (first, formatted, returned):
                    contract.ERRORS.clear()
                    contract.validate_lesson_state_contract(document, 'client-mode-fixture')
                    self.assertEqual(contract.ERRORS, [])

    def test_shared_mode_format_and_retirement_snapshots_authenticate_their_actual_publication(self):
        self.assertEqual(len(self.manifest['clientModeValid']), 12)
        for entry in self.manifest['clientModeValid']:
            with self.subTest(mode=entry['mode'], name=entry['name']):
                current = json.loads(self.payloads[entry['file']])
                layout.validate(current)
                if not entry['name'].startswith('retirement-'):
                    continue
                source = json.loads(self.payloads[entry['snapshotSource']])
                snapshot = current['stateLayout']['retirementHistory'][0]
                view = {'pages': snapshot['pages'], 'stateLayout': {'current': snapshot['current']}}
                self.assertEqual(layout._blocks(view)[entry['owner']], layout._blocks(source)[entry['owner']])
                self.assertEqual(snapshot['editorialPrefix'], layout._retirement_editorial_prefix(
                    source, len(source['stateLayout']['editorial'])))

    def test_shared_mode_negatives_reject_changes_outside_the_authenticated_client(self):
        self.assertEqual(len(self.manifest['invalid']), 21)
        self.assertIn('client-single-empty-predecessor', {item['name'] for item in self.manifest['invalid']})
        for item in self.manifest['invalid']:
            with self.subTest(name=item['name']):
                with self.assertRaises((ValueError, KeyError, TypeError, IndexError)):
                    layout.validate(json.loads(self.payloads[item['file']]))


class RetirementEditorialPrefixTests(unittest.TestCase):
    def chain(self, retire_first=False):
        fixture = general_fixtures
        original = fixture.order_fixtures.synthetic_publication()
        first = layout.retire(original, [fixture.NOTE]) if retire_first else original
        changed = fixture.revised(first, fixture.OWNER, fixture.synthetic_cases(first))
        returned = layout.revise_compiler_cases(changed, fixture.OWNER,
            layout._blocks(original)[fixture.OWNER]['outputCheck']['cases'],
            case_semantics=layout._compiler_case_order_fields(original, fixture.OWNER), return_to=0)
        final = returned if retire_first else layout.retire(returned, [fixture.NOTE])
        return original, changed, returned, final

    def test_identical_returned_payloads_keep_their_actual_retirement_epoch(self):
        old = self.chain(True)[-1]
        new = self.chain(False)[-1]
        old_snapshot = old['stateLayout']['retirementHistory'][0]
        new_snapshot = new['stateLayout']['retirementHistory'][0]
        self.assertEqual(old_snapshot['pages'], new_snapshot['pages'])
        self.assertEqual(old_snapshot['editorialPrefix']['count'], 0)
        self.assertEqual(new_snapshot['editorialPrefix']['count'], 2)
        for document, count in ((old, 0), (new, 2)):
            snapshot = document['stateLayout']['retirementHistory'][0]
            self.assertEqual(snapshot['editorialPrefix'], layout._retirement_editorial_prefix(document, count))
            owner = general_fixtures.OWNER
            block = layout._blocks({'pages': snapshot['pages'], 'stateLayout': {'current': snapshot['current']}})[owner]
            self.assertEqual(layout._compiler_case_epoch(document, owner, block,
                layout._compiler_case_events(document, owner), editorial_prefix=count), count)
            layout.validate(document)

    def test_prefix_count_and_hash_require_the_exact_typed_shape(self):
        document = self.chain(False)[-1]
        variants = [{'count': value, 'sha256': '0' * 64} for value in (False, 0.0, -1, 3)]
        variants += [{'count': 2, 'sha256': '0' * 64}, {'count': 2},
                     dict(document['stateLayout']['retirementHistory'][0]['editorialPrefix'], extra=True)]
        for prefix in variants:
            with self.subTest(prefix=prefix), self.assertRaises(layout.LayoutError):
                bad = deepcopy(document)
                bad['stateLayout']['retirementHistory'][0]['editorialPrefix'] = prefix
                layout.validate(bad)

    def test_valid_prefix_cannot_borrow_a_payload_from_another_position(self):
        document = self.chain(False)[-1]
        bad = deepcopy(document)
        # The middle prefix is genuine, but its owner is B, not the returned A.
        bad['stateLayout']['retirementHistory'][0]['editorialPrefix'] = layout._retirement_editorial_prefix(bad, 1)
        with self.assertRaisesRegex(layout.LayoutError, 'exact editorial prefix'):
            layout.validate(bad)

    def test_new_retirement_requires_its_actual_predecessor_prefix(self):
        _, _, previous, document = self.chain(False)
        for prefix in (None, layout._retirement_editorial_prefix(document, 0)):
            bad = deepcopy(document)
            if prefix is None:
                del bad['stateLayout']['retirementHistory'][0]['editorialPrefix']
            else:
                bad['stateLayout']['retirementHistory'][0]['editorialPrefix'] = prefix
            layout.validate(bad)  # Legacy/earlier A can only be disproved with the real predecessor.
            with self.assertRaisesRegex(layout.LayoutError, 'complete previous published presentation'):
                layout.validate_transition(previous, bad)

    def test_published_prefix_is_append_only_and_legacy_remains_unchanged(self):
        document = self.chain(False)[-1]
        legacy = deepcopy(document)
        del legacy['stateLayout']['retirementHistory'][0]['editorialPrefix']
        layout.validate(legacy)
        layout.validate_transition(legacy, deepcopy(legacy))
        self.assertNotIn('editorialPrefix', legacy['stateLayout']['retirementHistory'][0])
        with self.assertRaisesRegex(layout.LayoutError, 'snapshots are append-only'):
            layout.validate_transition(document, legacy)
        changed = deepcopy(document)
        changed['stateLayout']['retirementHistory'][0]['editorialPrefix'] = layout._retirement_editorial_prefix(changed, 0)
        with self.assertRaisesRegex(layout.LayoutError, 'snapshots are append-only'):
            layout.validate_transition(document, changed)




class CompilerCmakeRecipeRevisionTests(unittest.TestCase):
    owner = general_fixtures.OWNER

    def publication(self, *, marked=True, extra=None):
        document = general_fixtures.order_fixtures.synthetic_publication(marked=marked, project=True)
        if extra is None:
            return document
        references = document.pop('referenceAfter')
        document.pop('stateLayout')
        document.pop('textSemantics', None)
        extra(document['pages'][0]['blocks'][1])
        result = layout.prepare_v2(document, 'es', {'es': document})
        if 'outputCheck' in layout._blocks(result)[self.owner]:
            result['referenceAfter'] = references
        layout.validate(result)
        return result

    def source(self, document, path='CMakeLists.txt'):
        return layout._compiler_client_source(layout._blocks(document)[self.owner], path)

    def revise(self, document, source=None, *, path='CMakeLists.txt', return_to=None):
        if source is None:
            source = self.source(document, path) + '\ntarget_compile_options(alpha PRIVATE /W4)\n'
        return layout.revise_compiler_cmake_recipe(document, self.owner,
            {'path': path, 'source': source}, return_to=return_to)

    def reject(self, operation):
        with self.assertRaises((ValueError, KeyError, TypeError, IndexError)):
            operation()

    def open_battery_publication(self):
        def edit(block):
            block.pop('outputCheck')
            block['initialCode'] = ''
            block['projectFiles'] = [
                {'path': 'CMakeLists.txt', 'initialCode':
                 'cmake_minimum_required(VERSION 3.20)\nproject(Recipe LANGUAGES CXX)\n'
                 'add_library(ajustes STATIC ajuste.cpp)\nadd_executable(alpha main.cpp)\n'
                 'target_link_libraries(alpha PRIVATE ajustes)\n'},
                {'path': 'ajuste.cpp', 'initialCode':
                 'bool ajustar(int valor, int inferior, int superior, int& destino) {\n'
                 '    if (valor < -20 || valor > 20 || inferior < -20 || inferior > 20 ||\n'
                 '        superior < -20 || superior > 20 || inferior > superior) return false;\n'
                 '    destino = valor < inferior ? inferior - valor : valor > superior ? superior - valor : 0;\n'
                 '    return true;\n}\n'},
                {'path': 'main.cpp', 'initialCode':
                 'bool ajustar(int, int, int, int&);\n'
                 'bool comparar(int numero, int valor, int inferior, int superior, int destino, bool acepta, int esperado) {\n'
                 '    const bool obtenido = ajustar(valor, inferior, superior, destino);\n'
                 '    return obtenido == acepta && destino == esperado;\n}\n'
                 'int main() { return comparar(1, 2, 3, 7, 77, true, 1) ? 0 : 1; }\n'}]
        document = self.publication(marked=False, extra=edit)
        document['compilerOpenBatteryChecks'] = [{
            'activity': self.owner, 'library': 'ajuste.cpp', 'test': 'main.cpp',
            'function': 'ajustar', 'comparison': 'comparar', 'target': 'alpha',
            'originalCases': [{'value': 2, 'lower': 3, 'upper': 7, 'initial': 77}]}]
        layout.validate(document)
        return document

    def test_recipe_preserves_every_case_marker_other_file_and_original_identity(self):
        before = self.publication()
        before['textSemantics']['fields'].insert(1, layout.text_semantics.make_field(
            before, general_fixtures.NOTE, '/text', []))
        layout.validate(before)
        untouched = deepcopy(before)
        after = self.revise(before)
        record = after['stateLayout']['compilerCaseRevisions'][-1]
        self.assertEqual(set(record), {'id', 'block', 'recipe', 'cases', 'editorialIndex',
                                      'historySha256', 'semanticBefore', 'semanticAfter'})
        old, new = layout._blocks(before)[self.owner], layout._blocks(after)[self.owner]
        self.assertEqual(record['block'], old)
        self.assertEqual(record['cases'], old['outputCheck']['cases'])
        self.assertEqual(record['semanticBefore'], record['semanticAfter'])
        self.assertEqual(after['textSemantics'], before['textSemantics'])
        restored = deepcopy(new)
        restored['projectFiles'][0] = old['projectFiles'][0]
        self.assertEqual(restored, old)
        self.assertEqual(before, untouched)
        self.assertEqual(layout.original_projection(after)['pages'], layout.original_projection(before)['pages'])
        self.assertEqual(layout._blocks(layout.align_for_previous_validation(before, after))[self.owner], old)
        for key in ('origin', 'family', 'current', 'additions'):
            self.assertEqual(after['stateLayout'][key], before['stateLayout'][key])
        contract.ERRORS.clear()
        contract.validate_lesson_state_contract(after, 'cmake-recipe-fixture')
        self.assertEqual(contract.ERRORS, [])

    def test_existing_child_recipes_are_supported_without_creating_paths(self):
        for path in ('component/CMakeLists.txt', 'cmake/warnings.cmake'):
            with self.subTest(path=path):
                before = self.publication(extra=lambda b: b['projectFiles'].append(
                    {'path': path, 'initialCode': '# Avisos del proyecto.\n'}))
                after = self.revise(before, 'target_compile_options(alpha PRIVATE /W3)\n', path=path)
                self.assertEqual(layout._blocks(before)[self.owner]['projectFiles'][:-1],
                                 layout._blocks(after)[self.owner]['projectFiles'][:-1])
                layout.validate_transition(before, after)

    def test_recipe_rejects_unsupported_paths_modes_noops_and_metadata(self):
        before = self.publication()
        for path in ('main.cpp', 'CMakelists.txt', 'missing.cmake', '../CMakeLists.txt', '', None):
            self.reject(lambda: self.revise(before, 'project(Other)\n', path=path))
        for source in (self.source(before), '', 'project(Bad)\0\n', None):
            self.reject(lambda: self.revise(before, source, path='CMakeLists.txt') if source is not None else
                layout.revise_compiler_cmake_recipe(before, self.owner, {'path': 'CMakeLists.txt', 'source': None}))
        single = general_fixtures.order_fixtures.synthetic_publication(marked=False)
        self.reject(lambda: layout.revise_compiler_cmake_recipe(single, self.owner,
            {'path': 'main.cpp', 'source': 'int main() {}\n'}))
        for edit in (lambda b: b.update(sourceFiles=['main.cpp']),
                     lambda b: b.update(compileCheck={'expected': 'failure'}),
                     lambda b: b['cmake'].update(target='two targets'),
                     lambda b: b['projectFiles'][0].update(path='root.cmake')):
            self.reject(lambda: self.revise(self.publication(marked=False, extra=edit)))
        self.reject(lambda: layout.revise_compiler_cmake_recipe(before, self.owner,
            {'path': 'CMakeLists.txt', 'source': 'project(Other)\n', 'draft': True}))

    def test_recipe_cannot_change_cases_semantics_or_mix_client_mode(self):
        before = self.publication()
        original_case = layout._blocks(before)[self.owner]['outputCheck']['cases'][0]
        self.assertIs(type(original_case['exitCode']), int)
        self.assertEqual(original_case['exitCode'], 0)
        after = self.revise(before)
        for edit in (
            lambda r: r.update(client={'path': 'main.cpp', 'source': 'int main() {}\n'}),
            lambda r: r['cases'][0].update(standardInput='foreign\n'),
            lambda r: r['cases'][0].update(exitCode=False),
            lambda r: r['cases'][0].update(exitCode=0.0),
            lambda r: r.update(semanticAfter=[]),
            lambda r: r.update(cases=None)):
            bad = deepcopy(after)
            edit(bad['stateLayout']['compilerCaseRevisions'][-1])
            self.reject(lambda: layout.validate(bad))
        cases = layout._blocks(before)[self.owner]['outputCheck']['cases']
        self.reject(lambda: layout.revise_compiler_cases(before, self.owner, cases,
            case_semantics=[], recipe=after['stateLayout']['compilerCaseRevisions'][0]['recipe']))
        self.reject(lambda: layout.revise_compiler_client_cases(before, self.owner,
            after['stateLayout']['compilerCaseRevisions'][0]['recipe'], cases, case_semantics=[]))

    def test_root_generated_battery_keeps_absent_output_check_and_its_complete_goal(self):
        before = self.open_battery_publication()
        after = self.revise(before)
        record = after['stateLayout']['compilerCaseRevisions'][-1]
        self.assertIsNone(record['cases'])
        self.assertEqual(record['semanticBefore'], [])
        self.assertEqual(record['semanticAfter'], [])
        for document in (before, after, layout.align_for_previous_validation(before, after)):
            self.assertNotIn('outputCheck', layout._blocks(document)[self.owner])
        self.assertEqual(before['compilerOpenBatteryChecks'], after['compilerOpenBatteryChecks'])
        layout.validate_transition(before, after)
        without_goal = deepcopy(before)
        without_goal.pop('compilerOpenBatteryChecks')
        self.reject(lambda: self.revise(without_goal))
        invalid = deepcopy(after)
        invalid.pop('compilerOpenBatteryChecks')
        self.reject(lambda: layout.validate(invalid))
        invalid = deepcopy(after)
        invalid['stateLayout']['compilerCaseRevisions'][0]['cases'] = []
        self.reject(lambda: layout.validate(invalid))
        self.reject(lambda: layout.revise_compiler_cases(before, self.owner, None, case_semantics=[]))

    def test_explicit_return_keeps_recipe_epochs_and_does_not_hide_removed_history(self):
        for before in (self.publication(), self.open_battery_publication()):
            first = self.revise(before)
            second = self.revise(first, self.source(before) + '\ntarget_compile_options(alpha PRIVATE /W3)\n')
            self.reject(lambda: self.revise(second, self.source(before)))
            self.reject(lambda: self.revise(second, self.source(before), return_to=1))
            returned = self.revise(second, self.source(before), return_to=0)
            self.assertEqual(layout._blocks(returned)[self.owner], layout._blocks(before)[self.owner])
            self.assertEqual(returned['stateLayout']['compilerCaseRevisions'][:2],
                             second['stateLayout']['compilerCaseRevisions'])
            layout.validate_transition(before, returned)
            for previous in (before, first, second):
                self.assertEqual(layout._blocks(layout.align_for_previous_validation(previous, returned))[self.owner],
                                 layout._blocks(previous)[self.owner])
            bad = deepcopy(returned)
            bad['stateLayout']['compilerCaseRevisions'].pop(1)
            self.reject(lambda: layout.validate(bad))

    def test_recipe_client_cases_orders_and_formatting_share_their_real_timeline(self):
        for format_first in (False, True):
            with self.subTest(format_first=format_first):
                before = self.publication(marked=False)
                start = general_fixtures.order_fixtures.formatted(before, self.owner) if format_first else before
                recipe = self.revise(start)
                prior = layout._blocks(recipe)[self.owner]
                client = layout.revise_compiler_client_cases(recipe, self.owner,
                    {'path': 'main.cpp', 'source': self.source(recipe, 'main.cpp').replace('value{}', 'value{1}')},
                    prior['outputCheck']['cases'], case_semantics=[])
                cases = layout.revise_compiler_cases(client, self.owner,
                    [{'standardInput': '4\n', 'outputs': ['4\n']},
                     {'standardInput': '5\n', 'outputs': ['5\n']}], case_semantics=[])
                ordered = layout.reorder_compiler_cases(cases, self.owner, [1, 0])
                formatted = ordered if format_first else general_fixtures.order_fixtures.formatted(ordered, self.owner)
                final = self.revise(formatted, self.source(formatted).replace('/W4', '/W3'))
                stages = (before, start, recipe, client, cases, ordered, formatted, final)
                for previous, current in zip(stages, stages[1:]):
                    layout.validate_transition(previous, current)
                    aligned = layout._blocks(layout.align_for_previous_validation(previous, current))[self.owner]
                    original = layout._blocks(previous)[self.owner]
                    layout.code_format.require_source_lexical(original, aligned)
                    self.assertEqual(layout.code_format.source_projection(aligned, original), original)
                self.assertEqual(layout._compiler_case_order_contract(final, self.owner,
                    layout._blocks(final)[self.owner]), layout._blocks(before)[self.owner])

    def test_recipe_proof_does_not_authorize_collateral_changes_or_a_forged_prefix(self):
        before = self.publication()
        after = self.revise(before)
        edits = (
            lambda b: b['projectFiles'][1].update(initialCode='int main() { return 9; }\n'),
            lambda b: b['cmake'].update(target='beta'),
            lambda b: b.update(initialCode='int unrelated;\n'),
            lambda b: b['outputCheck']['cases'][0].update(exitCode=1),
            lambda b: b['outputCheck'].update(inputComparison='tokens'))
        for edit in edits:
            bad = deepcopy(after)
            edit(layout._blocks(bad)[self.owner])
            bad['stateLayout']['editorial'][-1]['afterSha256'] = layout.fingerprint(layout._blocks(bad)[self.owner])
            self.reject(lambda: layout.validate(bad))
        bad = deepcopy(after)
        bad['stateLayout']['compilerCaseRevisions'][0]['historySha256'] = '0' * 64
        self.reject(lambda: layout.validate(bad))
        bad = deepcopy(after)
        bad['stateLayout']['compilerCaseRevisions'][0]['block']['initialStandardInput'] = 'changed\n'
        self.reject(lambda: layout.validate(bad))
        self.reject(lambda: layout.validate_transition(after, before))

    def test_recipe_retirement_snapshots_keep_their_own_source_epoch(self):
        before = self.publication(marked=False)
        first = self.revise(before)
        retired = layout.retire(first, [general_fixtures.NOTE])
        after = self.revise(retired, self.source(first).replace('/W4', '/W3'))
        layout.validate_transition(first, after)
        snapshot = after['stateLayout']['retirementHistory'][0]
        view = {'pages': snapshot['pages'], 'stateLayout': {'current': snapshot['current']}}
        self.assertEqual(layout._blocks(view)[self.owner], layout._blocks(first)[self.owner])

    def test_recipe_cli_preserves_cases_and_refuses_overwrite_or_other_source_options(self):
        from contextlib import redirect_stderr, redirect_stdout
        import io
        from unittest.mock import patch
        before = self.publication()
        expected = self.revise(before)
        with tempfile.TemporaryDirectory(prefix='cmake-recipe-cli-') as directory:
            root = Path(directory)
            source, recipe, output = (root / name for name in ('source.json', 'recipe.json', 'new.json'))
            source.write_text(json.dumps(before), encoding='utf-8')
            recipe.write_text(json.dumps(expected['stateLayout']['compilerCaseRevisions'][0]['recipe']), encoding='utf-8')
            original = source.read_bytes()
            argv = ['stable_state_layout.py', 'revise-compiler-cmake-recipe', str(source),
                    '--previous', str(source), '--identity', self.owner, '--recipe', str(recipe), '--output', str(output)]
            with patch.object(sys, 'argv', argv), redirect_stdout(io.StringIO()):
                layout.main()
            self.assertEqual(json.loads(output.read_text()), expected)
            saved = output.read_bytes()
            for extra in ([], ['--client', str(recipe)], ['--cases', str(recipe)], ['--case-semantics', str(recipe)]):
                with patch.object(sys, 'argv', argv + extra), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    layout.main()
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()



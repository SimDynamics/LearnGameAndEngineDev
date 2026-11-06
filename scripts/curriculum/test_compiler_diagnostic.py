"""Closed diagnostic goals preserve original source, state and ownership."""
from copy import deepcopy
from functools import lru_cache
import json
from pathlib import Path
import subprocess
import unittest
from unittest import mock
import stable_state_layout as layout
import validar_json_programacion as contract

BASELINE = "7862f1064487a961d5cfc9668d4316da6d1b3ef2"
# This immutable fixture predates the five original single-source goals only.
SINGLE_SOURCE_DIAGNOSTICS = {owner: spec for owner, spec in layout.COMPILE_DIAGNOSTICS.items()
                             if not spec.get("project", False)}
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"

def asset_path(lesson):
    return "datos_propios_y_estado_valido/lesson_" + lesson.split("__")[-1] + ".json"

@lru_cache(maxsize=None)
def published_bytes(lesson, locale="es"):
    return subprocess.check_output(["git", "show", BASELINE + ":" + PREFIX.replace("/es/", "/" + locale + "/") + asset_path(lesson)], cwd=contract.ROOT)

def published(lesson, locale="es"):
    return json.loads(published_bytes(lesson, locale))

def block(document, identity):
    return layout._blocks(document)[identity]

def revise_edge(document, identity):
    proof = next(item for item in reversed(document["stateLayout"]["editorial"]) if item["id"] == identity)
    proof["afterSha256"] = layout.fingerprint(block(document, identity))

class CompilerDiagnosticTest(unittest.TestCase):
    def test_five_goals_preserve_complete_sources_maps_addresses_and_translations(self):
        self.assertEqual(5, len(SINGLE_SOURCE_DIAGNOSTICS))
        self.assertEqual(5, len({s["diagnostic"] for s in SINGLE_SOURCE_DIAGNOSTICS.values()}))
        for (lesson, identity), spec in SINGLE_SOURCE_DIAGNOSTICS.items():
            with self.subTest(identity=identity):
                previous = published(lesson); original = deepcopy(previous)
                current = layout.add_compile_check(previous, identity)
                self.assertEqual(original, previous)
                self.assertEqual(spec["before"], layout.fingerprint(block(previous, identity)))
                self.assertEqual(dict(block(previous, identity), compileCheck={"diagnostic": spec["diagnostic"]}), block(current, identity))
                self.assertEqual(layout.validate(previous), layout.validate(current))
                self.assertEqual(layout.state_fingerprint(block(previous, identity)), layout.state_fingerprint(block(current, identity)))
                for field in ("origin", "family", "current", "additions", "retired", "retirementHistory"):
                    self.assertEqual(previous["stateLayout"].get(field), current["stateLayout"].get(field))
                for field in ("referenceAfter", "activityAfter", "compilerPuzzlesRequired"):
                    self.assertEqual(previous.get(field), current.get(field))
                self.assertEqual(current, layout.add_compile_check(current, identity))
                self.assertEqual(previous["stateLayout"]["editorial"], current["stateLayout"]["editorial"][:-1])
                self.assertEqual({"id": identity, "beforeSha256": spec["before"], "afterSha256": layout.fingerprint(block(current, identity))}, current["stateLayout"]["editorial"][-1])
                layout.validate_transition(previous, current)
                layout.check_localized_family([current, published(lesson, "en"), published(lesson, "fr")], ["es", "en", "fr"])

    def test_explanation_is_authored_without_changing_initial_code(self):
        lesson, identity = next(iter(SINGLE_SOURCE_DIAGNOSTICS)); previous = published(lesson)
        current = layout.add_compile_check(previous, identity, "El programa no llegó a ejecutarse.")
        self.assertEqual("El programa no llegó a ejecutarse.", block(current, identity)["compileCheck"]["explanation"])
        self.assertEqual(current, layout.add_compile_check(current, identity))
        layout.validate_transition(previous, current)
        for bad in ("", " \t\n", False, 3, {}, []):
            with self.subTest(explanation=bad):
                with self.assertRaises(layout.LayoutError): layout.add_compile_check(previous, identity, bad)
        with self.assertRaises(layout.LayoutError): layout.add_compile_check(current, identity, "Otra explicación")

    def test_goals_require_v2_spanish_owner_and_actual_editorial_proof(self):
        for lesson, identity in SINGLE_SOURCE_DIAGNOSTICS:
            good = layout.add_compile_check(published(lesson), identity)
            for mode in ("unmapped", "v1", "locale", "lesson", "identity", "edge"):
                with self.subTest(identity=identity, mode=mode):
                    bad = deepcopy(good)
                    if mode == "unmapped": bad.pop("stateLayout")
                    if mode == "v1":
                        bad["stateLayout"] = {k: v for k, v in bad["stateLayout"].items() if k in {"origin", "current"}}
                        bad["stateLayout"]["version"] = 1
                    if mode == "locale": bad["stateLayout"]["locale"] = "fr"
                    if mode == "lesson": bad["id"] += "_other"
                    if mode == "identity":
                        ids = next(r["blocks"] for r in bad["stateLayout"]["current"] if identity in r["blocks"])
                        index = ids.index(identity); other = (index + 1) % len(ids)
                        ids[index], ids[other] = ids[other], ids[index]
                    if mode == "edge": bad["stateLayout"]["editorial"].pop()
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_unknown_or_malformed_contract_is_not_a_state_payload_alias(self):
        for (lesson, identity), spec in SINGLE_SOURCE_DIAGNOSTICS.items():
            previous = published(lesson); goal = spec["diagnostic"]
            for config in (None, False, [], goal, {}, {"diagnostic": []}, {"diagnostic": "anything"},
                           {"diagnostic": goal, "explanation": " "}, {"diagnostic": goal, "explanation": 2},
                           {"diagnostic": goal, "outputs": [""]}, {"diagnostic": goal, "errorCode": "C2662"}):
                with self.subTest(identity=identity, config=config):
                    candidate = dict(block(previous, identity), compileCheck=config)
                    with self.assertRaises(layout.LayoutError): layout.state_fingerprint(candidate)
                    with mock.patch.object(contract, "ERRORS", []):
                        contract.validate_cpp_compiler(candidate, "diagnostic QA")
                        self.assertTrue(contract.ERRORS)

    def test_source_interface_and_other_fields_cannot_be_rewritten_under_known_goal(self):
        for lesson, identity in SINGLE_SOURCE_DIAGNOSTICS:
            good = layout.add_compile_check(published(lesson), identity); source = block(good, identity)["initialCode"]
            mutations = (source + "\n", "int main(){}", source.replace("explicit ", ""), source.replace("const ", ""))
            for changed in set(mutations) - {source}:
                bad = deepcopy(good); block(bad, identity)["initialCode"] = changed; revise_edge(bad, identity)
                with self.subTest(identity=identity, source=changed[:30]):
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
            for key, value in (("id", "other"), ("language", "cpp")):
                bad = deepcopy(good); block(bad, identity)[key] = value; revise_edge(bad, identity)
                with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_output_stdin_and_projects_are_incompatible_with_compile_check(self):
        for lesson, identity in SINGLE_SOURCE_DIAGNOSTICS:
            good = layout.add_compile_check(published(lesson), identity)
            for key, value in (("outputCheck", {"cases": [{"standardInput": "", "outputs": [""]}]}),
                               ("initialStandardInput", ""), ("projectFiles", []), ("sourceFiles", []), ("cmake", {})):
                bad = deepcopy(good); block(bad, identity)[key] = value; revise_edge(bad, identity)
                with self.subTest(identity=identity, key=key):
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_published_goal_cannot_be_removed_replaced_or_downgraded(self):
        for (lesson, identity), spec in SINGLE_SOURCE_DIAGNOSTICS.items():
            current = layout.add_compile_check(published(lesson), identity)
            with self.assertRaises(layout.LayoutError): layout.validate_transition(current, published(lesson))
            for mode in ("remove", "replace"):
                bad = deepcopy(current)
                if mode == "remove": block(bad, identity).pop("compileCheck")
                else: block(bad, identity)["compileCheck"]["diagnostic"] = "const-member-call" if spec["diagnostic"] != "const-member-call" else "explicit-assignment"
                bad["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": layout.fingerprint(block(current, identity)), "afterSha256": layout.fingerprint(block(bad, identity))})
                with self.subTest(identity=identity, mode=mode):
                    with self.assertRaises(layout.LayoutError): layout.validate_transition(current, bad)

    def test_prepare_v2_adopts_goal_and_explanation_in_one_transition(self):
        for (lesson, identity), spec in SINGLE_SOURCE_DIAGNOSTICS.items():
            previous = published(lesson); source = deepcopy(previous)
            block(source, identity)["compileCheck"] = {"diagnostic": spec["diagnostic"], "explanation": "Explicación curricular posterior."}
            family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
            current = layout.prepare_v2(source, "es", family, previous)
            layout.validate_transition(previous, current)
            self.assertEqual(source["pages"], current["pages"])
            self.assertEqual(current, layout.add_compile_check(previous, identity, "Explicación curricular posterior."))

    def test_curricular_guard_accepts_new_goal_and_preserves_existing_goal(self):
        for lesson, identity in SINGLE_SOURCE_DIAGNOSTICS:
            previous = published(lesson); current = layout.add_compile_check(previous, identity)
            _, pn, bn = identity.split("_")
            with self.subTest(identity=identity):
                with mock.patch.object(contract, "ERRORS", []):
                    contract.validate_lesson_state_contract(current, "diagnostic QA")
                    contract.validate_cpp_compiler(block(current, identity), "diagnostic QA")
                    self.assertFalse(contract.ERRORS, contract.ERRORS)
                self.assertTrue(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), int(pn), int(bn), block(previous, identity), block(current, identity), previous))
                self.assertFalse(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), int(pn), int(bn), block(current, identity), block(previous, identity), current))

    def test_diagnostic_goal_can_guard_later_reference_and_activity(self):
        lesson, identity = next(iter(SINGLE_SOURCE_DIAGNOSTICS)); previous = published(lesson)
        current = layout.add_compile_check(previous, identity)
        later = [(owner, candidate) for page, record in zip(current["pages"][7:], current["stateLayout"]["current"][7:]) for candidate, owner in zip(page["blocks"], record["blocks"])]
        target = next(i for i, b in later if b["type"] == "paragraph")
        dependent = next(i for i, b in later if b["type"] == "multiple_choice")
        current["referenceAfter"] = [{"activity": identity, "blocks": [target]}]
        current.setdefault("activityAfter", []).append({"activity": dependent, "requires": [identity]})
        layout.validate(current); layout.validate_transition(previous, current)
        block(current, identity).pop("compileCheck")
        with self.assertRaises(layout.LayoutError): layout.validate(current)

    def test_retirement_preserves_goal_and_its_original_presentation(self):
        for lesson, identity in SINGLE_SOURCE_DIAGNOSTICS:
            previous = layout.add_compile_check(published(lesson), identity)
            current = layout.retire(previous, [identity], activity_after=[r for r in previous.get("activityAfter", []) if r["activity"] != identity])
            layout.validate_transition(previous, current)
            archived = next(i["block"] for i in current["stateLayout"]["retired"] if i["id"] == identity)
            self.assertEqual(block(previous, identity), archived)

    def test_prior_history_is_preserved_and_new_history_cannot_forge_a_goal(self):
        for lesson, identity in SINGLE_SOURCE_DIAGNOSTICS:
            previous = layout.retire(published(lesson), ["block_0000_0001"])
            current = layout.add_compile_check(previous, identity)
            self.assertEqual(previous["stateLayout"]["retirementHistory"], current["stateLayout"]["retirementHistory"])
            layout.validate_transition(previous, current)
            newer = layout.retire(current, ["block_0000_0000"]); layout.validate_transition(current, newer)
            snapshot = newer["stateLayout"]["retirementHistory"][-1]
            historic = next(b for page, r in zip(snapshot["pages"], snapshot["current"]) for b, i in zip(page["blocks"], r["blocks"]) if i == identity)
            historic["compileCheck"]["diagnostic"] = "unknown"
            with self.assertRaises(layout.LayoutError): layout.validate(newer)

    def test_helper_never_writes_assets_and_returns_independent_proposals(self):
        for (lesson, identity), spec in SINGLE_SOURCE_DIAGNOSTICS.items():
            path = contract.ROOT / (PREFIX + asset_path(lesson)); asset_bytes = path.read_bytes()
            historical = published(lesson); layout.validate(historical)
            self.assertNotIn("compileCheck", block(historical, identity))
            self.assertEqual(spec["before"], layout.fingerprint(block(historical, identity)))
            current = json.loads(asset_bytes); layout.validate(current)
            self.assertEqual(spec["diagnostic"], block(current, identity)["compileCheck"]["diagnostic"])
            for label, source in (("verified historical snapshot", historical), ("current integrated asset", current)):
                with self.subTest(identity=identity, source=label):
                    untouched = deepcopy(source); proposed = layout.add_compile_check(source, identity)
                    layout.validate_transition(source, proposed)
                    if label == "verified historical snapshot":
                        self.assertNotEqual(source, proposed)  # The adoption must actually add the goal.
                        self.assertEqual(dict(block(source, identity), compileCheck={"diagnostic": spec["diagnostic"]}),
                                         block(proposed, identity))
                    else:
                        # Already authored goals are retained, including their
                        # explanation. This check is independence/idempotence,
                        # not evidence of a historical-to-current transition.
                        self.assertEqual(source, proposed)
                    block(proposed, identity)["initialCode"] = "Caller changed its own proposal"
                    self.assertEqual(untouched, source)
                    self.assertEqual(asset_bytes, path.read_bytes())

if __name__ == "__main__": unittest.main()

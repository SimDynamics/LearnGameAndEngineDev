"""Explicit compiler retirement preserves published contracts and state slots."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content"
UNIT = "programming/cpp_fundamentals/programas_y_ejecucion"
PLANS = (
    ("lesson_primer_programa_y_salida.json", ("ejecucion_original_consigna", "ejecucion_original_editor")),
    ("lesson_texto_secuencia_y_formato.json", ("ejecucion_original_texto_secuencia_y_formato_consigna",
                                             "ejecucion_original_texto_secuencia_y_formato_editor")),
    ("lesson_fuente_compilacion_y_diagnostico.json", ("ejecucion_original_fuente_compilacion_y_diagnostico_consigna",
                                                  "ejecucion_original_fuente_compilacion_y_diagnostico_editor")),
)


def source(filename):
    return layout.load(CONTENT / "es" / UNIT / filename)


def retired(source, identities):
    # The fixture expresses exactly Work's U1 decision. Production retire()
    # never invents or removes dependencies on the author's behalf.
    removing = set(identities)
    activities = []
    for relation in source.get("activityAfter", []):
        if relation["activity"] not in removing:
            remaining = [item for item in relation["requires"] if item not in removing]
            if remaining:
                activities.append({"activity": relation["activity"], "requires": remaining})
    references = []
    for relation in source.get("referenceAfter", []):
        if relation["activity"] not in removing:
            remaining = [item for item in relation["blocks"] if item not in removing]
            if remaining:
                references.append({"activity": relation["activity"], "blocks": remaining})
    return layout.retire(source, list(identities), activity_after=activities,
                         reference_after=references, compiler_puzzles_required=True)


class RetiredStateLayoutTest(unittest.TestCase):
    def test_three_real_retirements_preserve_every_contract_and_historical_slot(self):
        useful_compilers = 0
        for filename, identities in PLANS:
            before = source(filename)
            original = deepcopy(before)
            after = retired(before, identities)
            layout.validate_transition(before, after)
            self.assertEqual(original, before)
            for field in ("origin", "family", "additions", "editorial"):
                self.assertEqual(before["stateLayout"][field], after["stateLayout"][field])
            self.assertTrue(after["compilerPuzzlesRequired"])
            self.assertEqual(layout._all_blocks(before), layout._all_blocks(after))
            self.assertTrue(set(identities).isdisjoint(layout._blocks(after)))
            old_mapping, new_mapping = layout.validate(before), layout.validate(after)
            old_slots = {identity: old_mapping[p, b]
                         for p, row in enumerate(before["stateLayout"]["current"])
                         for b, identity in enumerate(row["blocks"])}
            for p, row in enumerate(after["stateLayout"]["current"]):
                for b, identity in enumerate(row["blocks"]):
                    self.assertEqual(old_slots[identity], new_mapping[p, b])
            useful_compilers += sum(block.get("type") == "cpp_compiler"
                                    for block in layout._blocks(after).values())
            # The regular curriculum guard sees the untouched retired block at
            # its real previous coordinate, so retirement is no edit bypass.
            aligned = layout.align_for_previous_validation(before, after)
            self.assertEqual(before["pages"], aligned["pages"])
            languages = ["es"]
            members = [after]
            for language in before["stateLayout"]["family"]:
                if language != "es":
                    languages.append(language)
                    members.append(layout.load(CONTENT / language / UNIT / filename))
            layout.check_localized_family(members, languages)
        # These three lessons retain six useful editors. The final two are in
        # U1L5, which is intentionally outside this retirement plan.
        self.assertEqual(6, useful_compilers)
        other_compilers = sum(block.get("type") == "cpp_compiler"
                              for path in (CONTENT / "es" / UNIT).glob("lesson_*.json")
                              if path.name not in {plan[0] for plan in PLANS}
                              for page in layout.load(path)["pages"] for block in page["blocks"])
        self.assertEqual(8, useful_compilers + other_compilers)

    def test_archives_are_append_only_and_cannot_be_rewritten_or_resurrected(self):
        filename, identities = PLANS[0]
        before = source(filename)
        after = retired(before, identities)
        for key, value in (("page", "page_0000"), ("next", ""), ("block", {"type": "paragraph", "text": "replacement"})):
            changed = deepcopy(after)
            changed["stateLayout"]["retired"][0][key] = value
            with self.subTest(key=key), self.assertRaises(layout.LayoutError):
                layout.validate_transition(before, changed)
        for changed in (before, dict(after, stateLayout={**after["stateLayout"], "retired": []})):
            with self.assertRaises(layout.LayoutError):
                layout.validate_transition(after, changed)
        changed = deepcopy(after)
        changed["stateLayout"]["retired"][1]["block"]["initialCode"] += "// changed"
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)
        changed = deepcopy(after)
        changed["stateLayout"]["retired"][0]["block"]["text"] += " changed"
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, changed)

    def test_stderr_objectives_preserve_active_addresses_and_immutable_retirement_snapshots(self):
        filename, identities = PLANS[0]
        before = source(filename)
        required = deepcopy(before)
        editor = layout._blocks(required)[identities[1]]
        previous_editor = deepcopy(editor)
        editor["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        required["stateLayout"]["editorial"].append({"id": identities[1],
            "beforeSha256": layout.fingerprint(previous_editor), "afterSha256": layout.fingerprint(editor)})
        layout.validate_transition(before, required)
        after = retired(required, identities)
        layout.validate_transition(required, after)
        archived = after["stateLayout"]["retired"][1]["block"]
        self.assertEqual("nonBlank", archived["outputCheck"]["cases"][0]["standardError"])
        history = deepcopy(after["stateLayout"]["retirementHistory"])
        # A later active objective can be extended while old reading payloads
        # retain precisely the requirements published when they were archived.
        revised = deepcopy(after)
        active_id = "block_0004_0002"
        active = layout._blocks(revised)[active_id]
        old_active = deepcopy(active)
        active["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        revised["stateLayout"]["editorial"].append({"id": active_id,
            "beforeSha256": layout.fingerprint(old_active), "afterSha256": layout.fingerprint(active)})
        layout.validate_transition(after, revised)
        self.assertEqual(layout.validate(after), layout.validate(revised))
        self.assertEqual(history, revised["stateLayout"]["retirementHistory"])
        for part in ("retired", "retirementHistory"):
            changed = deepcopy(after)
            if part == "retired":
                case = changed["stateLayout"][part][1]["block"]["outputCheck"]["cases"][0]
            else:
                old_pages = changed["stateLayout"][part][0]["pages"]
                old_editor = next(block for page in old_pages for block in page["blocks"]
                                  if block.get("type") == "cpp_compiler" and
                                  block.get("outputCheck", {}).get("cases", [{}])[0].get("standardError"))
                case = old_editor["outputCheck"]["cases"][0]
            del case["standardError"]
            with self.subTest(part=part), self.assertRaises(layout.LayoutError):
                layout.validate_transition(after, changed)

    def test_unknown_duplicate_foreign_and_cyclic_retirements_are_rejected(self):
        filename, identities = PLANS[0]
        before = source(filename)
        for plan in ([], [identities[0], identities[0]], ["not_an_activity"], ["block_0004_0003"], [{}], [None]):
            with self.subTest(plan=plan), self.assertRaises(layout.LayoutError):
                layout.retire(before, plan)
        after = retired(before, identities)
        for change in ("active", "cycle", "foreign", "locale"):
            changed = deepcopy(after)
            if change == "active":
                changed["stateLayout"]["retired"][0]["id"] = changed["stateLayout"]["current"][0]["blocks"][0]
            elif change == "cycle":
                first, second = changed["stateLayout"]["retired"]
                first["next"], second["next"] = second["id"], first["id"]
            elif change == "foreign":
                changed["stateLayout"]["retired"][0]["next"] = "unrelated"
            else:
                changed["stateLayout"]["locale"] = "en"
            with self.subTest(change=change), self.assertRaises(layout.LayoutError):
                layout.validate(changed)

    def test_relationship_updates_are_explicit_and_useful_dependencies_survive(self):
        before = source(PLANS[0][0])
        with self.assertRaisesRegex(layout.LayoutError, "does not exist"):
            layout.retire(before, list(PLANS[0][1]))
        after = retired(before, PLANS[0][1])
        self.assertTrue(all(set(link["requires"]).isdisjoint(PLANS[0][1])
                            for link in after.get("activityAfter", [])))
        before = source(PLANS[1][0])
        after = retired(before, PLANS[1][1])
        for relation in before["activityAfter"]:
            if relation["activity"] == "block_0006_0006":
                self.assertIn(relation, after["activityAfter"])

    def test_mandatory_flow_flag_is_typed_and_cannot_be_disabled_after_publication(self):
        before = source(PLANS[0][0])
        enabled = dict(deepcopy(before), compilerPuzzlesRequired=True)
        layout.validate_transition(before, enabled)
        for value in (None, "true", 1, [], {}):
            with self.subTest(value=value), self.assertRaises(layout.LayoutError):
                layout.validate(dict(before, compilerPuzzlesRequired=value))
        for current in (before, dict(enabled, compilerPuzzlesRequired=False)):
            with self.assertRaises(layout.LayoutError):
                layout.validate_transition(enabled, current)

    def test_reading_archive_keeps_exact_old_payloads_layout_and_visibility_during_active_revision(self):
        before = source(PLANS[0][0])
        after = retired(before, PLANS[0][1])
        snapshot = after["stateLayout"]["retirementHistory"][0]
        self.assertEqual(before["pages"], snapshot["pages"])
        self.assertEqual(before["stateLayout"]["current"], snapshot["current"])
        self.assertEqual(before.get("activityAfter", []), snapshot["activityAfter"])
        self.assertEqual(before.get("referenceAfter", []), snapshot["referenceAfter"])
        self.assertFalse(snapshot["compilerPuzzlesRequired"])
        identity = "block_0004_0003"
        active = layout._blocks(after)[identity]
        old = deepcopy(active)
        active["question"] += " Consigna activa revisada sin ejecutar el original."
        after["stateLayout"]["editorial"].append({"id": identity,
            "beforeSha256": layout.fingerprint(old), "afterSha256": layout.fingerprint(active)})
        layout.validate_transition(before, after)
        old_blocks = {stable: block for page, record in zip(snapshot["pages"], snapshot["current"])
                      for stable, block in zip(record["blocks"], page["blocks"])}
        self.assertEqual(old, old_blocks[identity])
        self.assertNotEqual(old_blocks[identity]["question"], active["question"])
        # A second retirement records the actual intervening presentation,
        # without reintroducing identities already absent from that version.
        another = next(stable for stable, block in layout._blocks(after).items()
                       if block.get("type") == "paragraph")
        twice = layout.retire(after, [another])
        self.assertEqual(snapshot, twice["stateLayout"]["retirementHistory"][0])
        self.assertEqual(after["pages"], twice["stateLayout"]["retirementHistory"][1]["pages"])
        layout.validate_transition(after, twice)

    def test_reading_archive_cannot_invent_source_text_graph_order_or_flow(self):
        before = source(PLANS[0][0])
        after = retired(before, PLANS[0][1])
        for change in ("text", "graph", "flow", "order", "removed", "unknown"):
            changed = deepcopy(after)
            history = changed["stateLayout"]["retirementHistory"]
            if change == "text":
                history[0]["pages"][0]["blocks"][0]["text"] += " historia inventada"
            elif change == "graph":
                history[0]["activityAfter"] = []
            elif change == "flow":
                history[0]["compilerPuzzlesRequired"] = True
            elif change == "order":
                history[0]["pages"][0]["blocks"].reverse()
                history[0]["current"][0]["blocks"].reverse()
            elif change == "removed":
                changed["stateLayout"].pop("retirementHistory")
                layout.validate(changed)  # Older archives remain loadable.
            else:
                history[0]["activityAfter"][0]["requires"] = ["unrelated_activity"]
            with self.subTest(change=change), self.assertRaises(layout.LayoutError):
                layout.validate_transition(before, changed)
            with self.subTest(update=change), self.assertRaises(layout.LayoutError):
                layout.validate_transition(after, changed)

    def test_later_additions_preserve_old_reading_history_across_another_retirement(self):
        before = source(PLANS[0][0])
        after = retired(before, PLANS[0][1])
        snapshot = deepcopy(after["stateLayout"]["retirementHistory"])
        plan = deepcopy(after["stateLayout"]["current"])
        identity = "qa_addition_after_retirement"
        plan[0]["blocks"].append(identity)
        reserved = max([len(row["origin"]) for row in after["stateLayout"]["family"].values()] +
                       [row["storage"]["page"] + 1 for row in after["stateLayout"]["additions"]])
        added = layout.reorder(after, plan, [{"id": identity, "storage": {"page": reserved, "block": 0},
            "progressPage": plan[0]["id"], "block": {"type": "paragraph", "text": "Alta compatible posterior."}}])
        layout.validate_transition(after, added)
        self.assertEqual(snapshot, added["stateLayout"]["retirementHistory"])
        self.assertNotIn(identity, {item for page in snapshot[0]["current"] for item in page["blocks"]})
        self.assertIn(identity, layout._blocks(added))
        self.assertEqual((reserved, 0), layout.validate(added)[0, len(plan[0]["blocks"]) - 1])
        again = layout.retire(added, [identity])
        layout.validate_transition(added, again)
        self.assertEqual(snapshot[0], again["stateLayout"]["retirementHistory"][0])
        self.assertEqual(added["pages"], again["stateLayout"]["retirementHistory"][1]["pages"])
        # Original blocks cannot be mistaken for a later addition.
        damaged = deepcopy(added)
        old = damaged["stateLayout"]["retirementHistory"][0]
        old["current"][0]["blocks"].pop(0)
        old["pages"][0]["blocks"].pop(0)
        with self.assertRaises(layout.LayoutError):
            layout.validate(damaged)


if __name__ == "__main__":
    unittest.main()

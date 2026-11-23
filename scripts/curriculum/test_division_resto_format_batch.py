"""U3L2 source formatting survives authentic reordering and retirement.

The nine owners are resolved by identity. Complete Git publications provide
the original format transition and the later retirement; no prior document is
reconstructed by deleting the current lesson's history or restoring old indices.
"""
from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import code_format_compat as source_format
import stable_state_layout as state
from windows_cpp_ci import msvc_cpp23_command, windows_executable


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_division_y_resto.json"
)
PATH = ROOT / RELATIVE
LESSON_ID = "cpp_fundamentals__expresiones_y_decisiones__division_y_resto"
PUBLICATIONS = {
    "before": (
        "6a1f851c45895dea43f92448f0c3b5ea591764a5",
        "dc652430276b3e9fb31c1d3bdbae08330c81eacf2b4831fc5a4842bbb778da4a",
    ),
    "formatted": (
        "b1177700366ddbd05ebc3d8acf8b0cd9ea3c42f1",
        "ee2b0bb66fa0f20e54e7ffaca62dd41de6367d27856d1342d99faf670cd0b9f9",
    ),
    "retirement_parent": (
        "f64db0fc17a1bc6e76bb2828084ea567997c6fc8",
        "ee2b0bb66fa0f20e54e7ffaca62dd41de6367d27856d1342d99faf670cd0b9f9",
    ),
    "retirement": (
        "cdc468168a1d9ed5c1f810ab836cdb47ffcadad1",
        "0f0fccd697fc9bd24ac1087bb735cdb89ecf362b9b8b35964790b94a5c48b596",
    ),
}
TRANSLATIONS = {
    "en": (
        "b1177700366ddbd05ebc3d8acf8b0cd9ea3c42f1",
        "859fac3d149d59ccffee8ad6e4d37fef97a0e1f26241ff1b928323aab06f6090",
    ),
    "fr": (
        "b1177700366ddbd05ebc3d8acf8b0cd9ea3c42f1",
        "56b7b52d72ba3ccf483d30de47e2cb8899b9ee5a3e9305789e546e7170caeedf",
    ),
}
OWNERS = (
    "block_0000_0002", "block_0000_0004", "block_0001_0003",
    "block_0002_0001", "block_0002_0003", "block_0003_0003",
    "block_0003_0008", "block_0004_0003", "block_0005_0002",
)
RETIRED_OWNERS = {"block_0003_0003", "block_0003_0008", "block_0005_0002"}


def verify_bytes(payload: bytes, publication: str) -> bytes:
    sources = TRANSLATIONS if publication in TRANSLATIONS else PUBLICATIONS
    if hashlib.sha256(payload).hexdigest() != sources[publication][1]:
        raise ValueError("U3L2 source differs from its pinned complete publication")
    return payload


@lru_cache(maxsize=None)
def historical_bytes(publication: str) -> bytes:
    sources = TRANSLATIONS if publication in TRANSLATIONS else PUBLICATIONS
    commit, _ = sources[publication]
    relative = RELATIVE.replace("/es/", f"/{publication}/") if publication in TRANSLATIONS else RELATIVE
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{commit}:{relative}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), publication)


def source_field(block: dict) -> str:
    return "initialCode" if block["type"] == "cpp_compiler" else (
        "template" if block["type"] == "inline_fill_code" else "code")


def compilable_code(block: dict) -> str:
    code = block[source_field(block)]
    if block["type"] == "inline_fill_code":
        if code.count("[]") != len(block["answers"]):
            raise ValueError("Inline source and authored answer slots differ")
        for answer in block["answers"]:
            code = code.replace("[]", answer, 1)
    return code


class DivisionRestoRealFormat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.publications = {name: json.loads(historical_bytes(name))
                            for name in PUBLICATIONS}
        cls.before = cls.publications["before"]
        cls.formatted = cls.publications["formatted"]
        cls.retirement_parent = cls.publications["retirement_parent"]
        cls.retirement = cls.publications["retirement"]
        cls.current = json.loads(PATH.read_bytes())
        cls.translations = {locale: json.loads(historical_bytes(locale))
                            for locale in TRANSLATIONS}

    def publish(self, previous, proposal):
        # Existing translations are read-only family inputs, never fabricated
        # from Spanish or rewritten to make the proposal pass validation.
        before = deepcopy(self.translations)
        family = {"es": previous, **self.translations}
        revised = state.prepare_v2(proposal, "es", family, previous)
        self.assertEqual(self.translations, before)
        return revised

    def active_owners(self, document):
        pages, layout = document["pages"], document["stateLayout"]["current"]
        self.assertEqual(len(pages), len(layout))
        blocks, positions, page_ids = {}, {}, set()
        for p, (page, record) in enumerate(zip(pages, layout)):
            self.assertNotIn(record["id"], page_ids)
            page_ids.add(record["id"])
            self.assertEqual(len(page["blocks"]), len(record["blocks"]))
            for b, (block, owner) in enumerate(zip(page["blocks"], record["blocks"])):
                self.assertNotIn(owner, blocks)
                blocks[owner], positions[owner] = block, (p, b)
        return blocks, positions

    def all_owners(self, document):
        blocks, _ = self.active_owners(document)
        for entry in document["stateLayout"].get("retired", []):
            self.assertNotIn(entry["id"], blocks)
            blocks[entry["id"]] = entry["block"]
        return blocks

    def assert_sources_and_history(self, document):
        state.validate(document)
        before = self.all_owners(self.before)
        formatted = self.all_owners(self.formatted)
        resolved = self.all_owners(document)
        entries = document["stateLayout"]["sourceFormatRevisions"]
        archives = {entry["id"]: entry["block"] for entry in entries}
        self.assertEqual(len(entries), len(archives))
        self.assertTrue(set(OWNERS) <= archives.keys())
        for owner in OWNERS:
            with self.subTest(owner=owner):
                self.assertEqual(archives[owner], before[owner])
                self.assertTrue(source_format.require_source_format(before[owner], formatted[owner]))
                source_format.require_source_lexical(before[owner], resolved[owner])
                protected = source_format.source_projection(resolved[owner], before[owner])
                self.assertEqual(state.state_fingerprint(protected),
                                 state.state_fingerprint(before[owner]))
                edge = {"id": owner,
                        "beforeSha256": state.fingerprint(before[owner]),
                        "afterSha256": state.fingerprint(formatted[owner])}
                published = [proof for proof in self.formatted["stateLayout"]["editorial"]
                             if proof["id"] == owner]
                current = [proof for proof in document["stateLayout"]["editorial"]
                           if proof["id"] == owner]
                self.assertIn(edge, published)
                self.assertEqual(current[:len(published)], published)

    def test_complete_publications_and_their_actual_parent_transitions(self):
        for document in (*self.publications.values(), self.current):
            self.assertEqual(document["id"], LESSON_ID)
            state.validate(document)
        state.validate_transition(self.before, self.formatted)
        state.validate_transition(self.retirement_parent, self.retirement)
        self.assertEqual(state.validate(self.before), state.validate(self.formatted))
        snapshots = self.retirement["stateLayout"]["retirementHistory"]
        self.assertEqual(len(snapshots), 1)
        snapshot = snapshots[0]
        self.assertEqual(snapshot["pages"], self.retirement_parent["pages"])
        self.assertEqual(snapshot["current"], self.retirement_parent["stateLayout"]["current"])
        for key, default in (("compilerPuzzlesRequired", False), ("referenceAfter", []),
                             ("activityAfter", []), ("textSemantics", [])):
            self.assertEqual(snapshot[key], self.retirement_parent.get(key, default))
        self.assertEqual(snapshot["editorialPrefix"]["count"],
                         len(self.retirement_parent["stateLayout"]["editorial"]))

    def test_proveniencia_y_estado_historico_por_bloque(self):
        for document in (self.formatted, self.retirement, self.current):
            self.assert_sources_and_history(document)
            _, old_positions = self.active_owners(self.formatted)
            active, positions = self.active_owners(document)
            old_storage, storage = state.validate(self.formatted), state.validate(document)
            for owner in set(OWNERS) & active.keys():
                self.assertEqual(storage[positions[owner]], old_storage[old_positions[owner]])
            for field in ("origin", "family"):
                self.assertEqual(document["stateLayout"][field],
                                 self.before["stateLayout"][field])

    def test_reordered_lesson_resolves_retired_editors_and_reference(self):
        active, positions = self.active_owners(self.retirement)
        _, previous_positions = self.active_owners(self.formatted)
        self.assertNotEqual(positions["block_0004_0003"],
                            previous_positions["block_0004_0003"])
        self.assertEqual(set(OWNERS) - active.keys(), RETIRED_OWNERS)
        archived = self.all_owners(self.retirement)
        prior = self.all_owners(self.retirement_parent)
        snapshot = self.retirement["stateLayout"]["retirementHistory"][0]
        snapshot_blocks = self.active_owners(
            {"pages": snapshot["pages"], "stateLayout": {"current": snapshot["current"]}})[0]
        for owner in RETIRED_OWNERS:
            self.assertEqual(archived[owner], prior[owner])
            self.assertEqual(snapshot_blocks[owner], prior[owner])
        self.assertEqual(len(archived["block_0003_0003"]["outputCheck"]["cases"]), 1)
        self.assertEqual(len(archived["block_0003_0008"]["outputCheck"]["cases"]), 4)
        # The new objectives have their own identities; an old archived editor
        # must never be silently replaced by the activity occupying its position.
        self.assertIn("p1u3l2_reparar_conversion_minutos", active)
        self.assertIn("p1u3l2_construir_informe_bandejas", active)

    def test_byte_pins_reject_modified_or_substituted_publications(self):
        for publication in (*PUBLICATIONS, *TRANSLATIONS):
            payload = historical_bytes(publication)
            with self.subTest(publication=publication):
                self.assertEqual(verify_bytes(payload, publication), payload)
                with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                    verify_bytes(payload + b"\n", publication)
                other = "formatted" if publication == "before" else "before"
                with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                    verify_bytes(historical_bytes(other), publication)

    def test_equivalent_reflow_does_not_require_identical_nonempty_lines(self):
        proposal = deepcopy(self.formatted)
        blocks = self.active_owners(proposal)[0]
        for owner in OWNERS:
            field = source_field(blocks[owner])
            code = blocks[owner][field]
            self.assertIn("int main() {", code)
            blocks[owner][field] = code.replace("int main() {", "int   main( )\n{").replace("\n\n", "\n")
            self.assertTrue(source_format.require_source_format(
                self.all_owners(self.formatted)[owner], blocks[owner]))
        revised = self.publish(self.formatted, proposal)
        state.validate_transition(self.formatted, revised)
        self.assert_sources_and_history(revised)
        self.assertEqual(state.validate(revised), state.validate(self.formatted))

    def test_later_spacing_preserves_reordered_prose_and_retirement_history(self):
        proposal = deepcopy(self.retirement)
        blocks = self.active_owners(proposal)[0]
        for owner in set(OWNERS) & blocks.keys():
            field = source_field(blocks[owner])
            code = blocks[owner][field]
            self.assertIn("\n\n", code)
            blocks[owner][field] = code.replace("\n\n", "\n", 1)
        revised = self.publish(self.retirement, proposal)
        state.validate_transition(self.retirement, revised)
        self.assert_sources_and_history(revised)
        self.assertEqual(state.validate(revised), state.validate(self.retirement))
        for field in ("origin", "family", "current", "retired", "retirementHistory", "sourceFormatRevisions"):
            self.assertEqual(revised["stateLayout"][field], self.retirement["stateLayout"][field])

    def test_logic_answers_oracles_and_history_cannot_change_as_format(self):
        mutations = []
        logical = deepcopy(self.retirement)
        owner = self.active_owners(logical)[0]["block_0000_0004"]
        owner["code"] = owner["code"].replace("distancia / tramos", "distancia % tramos", 1)
        with self.assertRaises(source_format.SourceFormatError):
            source_format.require_source_lexical(self.all_owners(self.before)["block_0000_0004"], owner)
        mutations.append(logical)
        answer = deepcopy(self.retirement)
        self.active_owners(answer)[0]["block_0000_0004"]["answers"] = ["0 0"]
        mutations.append(answer)
        oracle = deepcopy(self.retirement)
        self.all_owners(oracle)["block_0003_0008"]["outputCheck"]["cases"][0]["outputs"] = ["incorrecto"]
        mutations.append(oracle)
        archive = deepcopy(self.retirement)
        archive["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] += "// sin procedencia\n"
        mutations.append(archive)
        snapshot = deepcopy(self.retirement)
        snapshot["stateLayout"]["retirementHistory"][0]["editorialPrefix"]["count"] -= 1
        mutations.append(snapshot)
        identities = deepcopy(self.retirement)
        records = identities["stateLayout"]["sourceFormatRevisions"]
        records[0]["id"], records[1]["id"] = records[1]["id"], records[0]["id"]
        mutations.append(identities)
        for index, document in enumerate(mutations):
            with self.subTest(mutation=index), self.assertRaises(state.LayoutError):
                state.validate(document)

    def test_cpp23_mismas_salidas_y_oraculos_publicados(self):
        binaries, executions = {}, {}
        original = self.all_owners(self.before)
        with tempfile.TemporaryDirectory(prefix="u3l2-formato-") as folder:
            def observe(block, inputs, owner):
                code = compilable_code(block)
                if code not in binaries:
                    number = len(binaries)
                    src, exe = Path(folder) / f"source-{number}.cpp", Path(folder) / f"source-{number}"
                    src.write_text(code, encoding="utf-8")
                    built = subprocess.run(msvc_cpp23_command(exe, src),
                        cwd=folder, capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, owner + built.stdout + built.stderr)
                    binaries[code] = windows_executable(exe)
                results = []
                for stdin in inputs:
                    key = (code, stdin)
                    if key not in executions:
                        run = subprocess.run([str(binaries[code])], input=stdin,
                            cwd=folder, capture_output=True, text=True, timeout=15)
                        self.assertEqual(0, run.returncode, owner + run.stderr)
                        self.assertEqual("", run.stderr, owner)
                        executions[key] = (run.returncode, run.stdout, run.stderr)
                    results.append(executions[key])
                return results

            for name, document in (("formatted", self.formatted),
                                   ("reordered", self.retirement), ("checkout", self.current)):
                self.assert_sources_and_history(document)
                resolved = self.all_owners(document)
                for owner in OWNERS:
                    with self.subTest(publication=name, owner=owner):
                        old, new = original[owner], resolved[owner]
                        cases = new.get("outputCheck", {}).get("cases", [])
                        inputs = [case["standardInput"] for case in cases] or [new.get("initialStandardInput", "")]
                        before, after = observe(old, inputs, owner), observe(new, inputs, owner)
                        self.assertEqual(before, after, owner)
                        for result, case in zip(after, cases):
                            self.assertIn(result[1], case["outputs"], owner)
                        if new["type"] == "output_prediction":
                            self.assertIn(after[0][1], new["answers"], owner)
            print(f"PASS U3L2 MSVC: {len(binaries)} compiled sources, "
                  f"{len(executions)} executed input cases, {len(OWNERS)} authenticated owners")


if __name__ == "__main__":
    unittest.main()

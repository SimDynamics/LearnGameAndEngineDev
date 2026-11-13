"""Pilot a real, published lesson without changing any curricular JSON.

The two historical 'code' blocks are real copies from Programación 1. Their
extra physical blank lines separate declarations, validation, calculation,
decision, and return without changing operators, literals or answers.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import stable_state_layout as state
from code_format_compat import require_source_format, source_projection, SourceFormatError

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = Path("programming/cpp_fundamentals/expresiones_y_decisiones/"
                "lesson_construir_decisiones_desde_un_problema.json")
POSITIONS = {
    (3, 2): "block_0003_0002",  # page 4 / block 3
    (5, 2): "block_0005_0002",  # page 6 / block 3
}
EXPECTED_STDOUT = "Grupos: 3\nSobrantes: 3"


def source_path(locale: str) -> Path:
    return ROOT / "app/src/main/assets/content" / locale / RELATIVE


def documents() -> dict[str, dict]:
    return {locale: json.loads(source_path(locale).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")}


def format_real_code(source: str) -> str:
    changes = (
        ("    const bool acepta_sobrantes = true;\n    if (",
         "    const bool acepta_sobrantes = true;\n\n    if ("),
        ("        const int sobrantes = fichas % capacidad;\n        if (",
         "        const int sobrantes = fichas % capacidad;\n\n        if ("),
        ("    }\n    return 0;\n}\n", "    }\n\n    return 0;\n}\n"),
    )
    output = source
    for before, after in changes:
        if output.count(before) != 1:
            raise AssertionError(f"The published lesson lost its exact review location: {before!r}")
        output = output.replace(before, after, 1)
    return output


def pilot() -> tuple[dict, dict]:
    family = documents()
    original = family["es"]
    assert original["stateLayout"]["version"] == 2
    assert original["stateLayout"]["locale"] == "es"
    assert set(original["stateLayout"]["family"]) == {"es", "en", "fr"}
    state.validate(original)
    # Once the real lesson has been published, restore only its authenticated
    # source snapshots in memory so the same pilot still tests old versus new.
    # Never reset the authored origin, student progress, or other editorial work.
    archived = original["stateLayout"].get("sourceFormatRevisions", [])
    if archived:
        assert len(archived) == len(POSITIONS)
        assert [item["id"] for item in archived] == list(POSITIONS.values())
        historical = deepcopy(original)
        editorial = historical["stateLayout"]["editorial"]
        # An editorial update can be published after a safe source-format
        # revision. Only a terminal format suffix can be removed to rebuild a
        # genuine previous publication. Otherwise the archived blocks are an
        # in-memory comparison view, NOT a forged valid historical state.
        terminal = [entry["id"] for entry in editorial[-len(POSITIONS):]] == list(POSITIONS.values())
        if terminal:
            historical["stateLayout"]["editorial"] = editorial[:-len(POSITIONS)]
            historical["stateLayout"].pop("sourceFormatRevisions")
        for (page, block), identity in POSITIONS.items():
            saved = next(item for item in archived if item["id"] == identity)
            current = original["pages"][page]["blocks"][block]
            historical["pages"][page]["blocks"][block] = deepcopy(saved["block"])
            assert current["code"] == format_real_code(saved["block"]["code"])
            assert require_source_format(saved["block"], current)
            assert source_projection(current, saved["block"]) == saved["block"]
        if terminal:
            state.validate(historical)
            state.validate_transition(historical, original)
            assert state.prepare_v2(original, "es", family, historical) == original
        else:
            # state.validate(original) above authenticates every editorial
            # edge and its source snapshot, including later unrelated edits.
            # Never discard an append-only post-format editorial proof.
            assert historical["stateLayout"] == original["stateLayout"]
        return historical, original
    changed = deepcopy(original)
    for (page, block), identity in POSITIONS.items():
        assert original["stateLayout"]["current"][page]["blocks"][block] == identity
        before = original["pages"][page]["blocks"][block]
        assert before["type"] == "code"
        after = changed["pages"][page]["blocks"][block]
        after["code"] = format_real_code(before["code"])
        assert require_source_format(before, after)
        assert source_projection(after, before) == before
    formatted = state.prepare_v2(changed, "es", family, original)
    state.validate_transition(original, formatted)
    assert state.validate(original) == state.validate(formatted)
    assert original["stateLayout"]["origin"] == formatted["stateLayout"]["origin"]
    assert original["stateLayout"]["family"] == formatted["stateLayout"]["family"]
    assert original["stateLayout"]["additions"] == formatted["stateLayout"]["additions"]
    assert len(formatted["stateLayout"]["sourceFormatRevisions"]) == 2
    assert set(entry["id"] for entry in formatted["stateLayout"]["sourceFormatRevisions"]) == set(POSITIONS.values())
    assert (formatted["stateLayout"]["editorial"][:len(original["stateLayout"]["editorial"])]
            == original["stateLayout"]["editorial"])
    assert len(formatted["stateLayout"]["editorial"]) == len(original["stateLayout"]["editorial"]) + 2
    for entry in formatted["stateLayout"]["sourceFormatRevisions"]:
        pos = next(pos for pos, identity in POSITIONS.items() if identity == entry["id"])
        assert entry["block"] == original["pages"][pos[0]]["blocks"][pos[1]]
    return original, formatted


def run_cpp(source: str, directory: Path, name: str) -> str:
    src = directory / f"{name}.cpp"
    artifact = directory / name
    src.write_text(source, encoding="utf-8")
    compile_run = subprocess.run(["c++", "-std=c++23", "-O0", "-o", str(artifact), str(src)],
                                 capture_output=True, text=True, timeout=90, check=False)
    if compile_run.returncode:
        raise AssertionError(f"C++ compilation failed: {compile_run.stderr}")
    execution = subprocess.run([str(artifact)], capture_output=True, text=True,
                               timeout=15, check=False)
    if execution.returncode or execution.stderr:
        raise AssertionError(f"Program failed: {execution.returncode}, {execution.stderr}")
    return execution.stdout


class RealPublishedFormatPilot(unittest.TestCase):
    def test_real_immutable_source_identity_and_safe_revision(self):
        before_bytes = source_path("es").read_bytes()
        old, revised = pilot()
        self.assertEqual(before_bytes, source_path("es").read_bytes())
        self.assertEqual(old["pages"][3]["blocks"][3]["answers"],
                         revised["pages"][3]["blocks"][3]["answers"])
        self.assertEqual(old["pages"][3]["blocks"][6],
                         revised["pages"][3]["blocks"][6])
        self.assertEqual(old["pages"][5]["blocks"][6],
                         revised["pages"][5]["blocks"][6])
        self.assertNotEqual(old["pages"][3]["blocks"][2]["code"],
                            revised["pages"][3]["blocks"][2]["code"])

    def test_real_code_compiles_and_outputs_identically(self):
        original, formatted = pilot()
        with tempfile.TemporaryDirectory(prefix="lgaed-real-format-") as path:
            directory = Path(path)
            for index, (page, block) in enumerate(POSITIONS):
                old = original["pages"][page]["blocks"][block]["code"]
                new = formatted["pages"][page]["blocks"][block]["code"]
                with self.subTest(page=page+1):
                    actual_before = run_cpp(old, directory, f"baseline_{index}")
                    actual_after = run_cpp(new, directory, f"formatted_{index}")
                    self.assertEqual(actual_before, EXPECTED_STDOUT)
                    self.assertEqual(actual_after, actual_before)

    def test_real_answer_and_archived_source_forgeries_rejected(self):
        original, formatted = pilot()
        changed_answer = deepcopy(formatted)
        changed_answer["pages"][3]["blocks"][3]["answers"] = ["forged"]
        with self.assertRaises(state.LayoutError):
            state.validate(changed_answer)
        changed_code = deepcopy(formatted)
        changed_code["pages"][3]["blocks"][2]["code"] = changed_code["pages"][3]["blocks"][2]["code"].replace(
            "const int fichas = 18;", "const int fichas = 19;", 1)
        with self.assertRaises((state.LayoutError, SourceFormatError)):
            state.validate(changed_code)
        forged_archive = deepcopy(formatted)
        forged_archive["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] += " // forged"
        with self.assertRaises(state.LayoutError):
            state.validate(forged_archive)
        if "sourceFormatRevisions" not in original["stateLayout"]:
            self.assertEqual(state.validate(original), state.validate(formatted))
        else:
            # A comparison view after later editorial changes is intentionally
            # not a valid prior publication; its immutable metadata is exact.
            self.assertEqual(original["stateLayout"], formatted["stateLayout"])
            state.validate(formatted)

    def test_later_editorial_update_preserves_format_proofs(self):
        # Reproduce a later, independent paragraph correction (as in main)
        # without modifying any on-disk curriculum or historical snapshots.
        family = documents()
        published = family["es"]
        changed = deepcopy(published)
        changed["pages"][0]["blocks"][3]["text"] += " Comprueba los datos antes de calcular."
        # The published lesson declares text semantics for every paragraph.
        # Keep its explicit source fingerprint consistent in the simulated
        # editorial revision, exactly as a real curriculum publication does.
        descriptor = next(field for field in changed["textSemantics"]["fields"]
                          if field["block"] == "block_0000_0003" and field["path"] == "/text")
        self.assertEqual(descriptor["spans"], [])
        descriptor["sourceSha256"] = hashlib.sha256(
            changed["pages"][0]["blocks"][3]["text"].encode("utf-8")).hexdigest()
        revised = state.prepare_v2(changed, "es", family, published)
        self.assertEqual(revised["stateLayout"]["editorial"][-1]["id"], "block_0000_0003")
        self.assertEqual(revised["stateLayout"]["sourceFormatRevisions"],
                         published["stateLayout"]["sourceFormatRevisions"])
        with mock.patch.dict(pilot.__globals__, {"documents": lambda: {**family, "es": revised}}):
            before, after = pilot()
        self.assertEqual(after, revised)
        self.assertEqual(before["stateLayout"], revised["stateLayout"])
        for page, block in POSITIONS:
            self.assertNotEqual(before["pages"][page]["blocks"][block]["code"],
                                after["pages"][page]["blocks"][block]["code"])
        forged = deepcopy(after)
        forged["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
        with self.assertRaises(state.LayoutError):
            state.validate(forged)


def write_fixtures(directory: Path) -> None:
    original, formatted = pilot()
    directory.mkdir(parents=True, exist_ok=True)
    for name, document in (("real.before.json", original), ("real.after.json", formatted)):
        (directory / name).write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                                      encoding="utf-8")
    print("PASS real U4 decision lesson: two authenticated code blocks, original assets untouched")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    write_fixtures(args.output_dir)


if __name__ == "__main__":
    main()

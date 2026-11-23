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
from format_batch_history import Publication, active_owners, all_owners, publication_bytes, verify_publication_bytes

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = Path("programming/cpp_fundamentals/expresiones_y_decisiones/"
                "lesson_construir_decisiones_desde_un_problema.json")
POSITIONS = {
    (3, 2): "block_0003_0002",  # page 4 / block 3
    (5, 2): "block_0005_0002",  # page 6 / block 3
}
EXPECTED_STDOUT = "Grupos: 3\nSobrantes: 3"
OWNERS = tuple(POSITIONS.values())
PUBLICATIONS = {
    "921e1df": {
        "es": Publication("921e1df9171992ab7a9f1b3803b963b02fd45bbc", "6b387aa196ecd5ee8f739e82f57f3275bd86817e4ddc3dfc4a6df1a960879094"),
        "en": Publication("921e1df9171992ab7a9f1b3803b963b02fd45bbc", "3911b9f5e0a64c46c1b636d4234315c4e31a52593ea6eb2b0f86ce9bc0017793"),
        "fr": Publication("921e1df9171992ab7a9f1b3803b963b02fd45bbc", "842f47d9660793d49ead0aace70721d096d73f1f1676a7981cce06ef16fefcb4"),
    },
    "83e2d4e6": {
        "es": Publication("83e2d4e6d21027c6ca072dd2e85d4700a21d611d", "12a18382e05a4552d67b561d5490beb455d44b421ee4f8dbb1b71d3f5f15149d"),
        "en": Publication("83e2d4e6d21027c6ca072dd2e85d4700a21d611d", "3911b9f5e0a64c46c1b636d4234315c4e31a52593ea6eb2b0f86ce9bc0017793"),
        "fr": Publication("83e2d4e6d21027c6ca072dd2e85d4700a21d611d", "842f47d9660793d49ead0aace70721d096d73f1f1676a7981cce06ef16fefcb4"),
    },
}


def source_path(locale: str) -> Path:
    return ROOT / "app/src/main/assets/content" / locale / RELATIVE


def documents(publication: str = "working-tree") -> dict[str, dict]:
    if publication == "working-tree":
        return {locale: json.loads(source_path(locale).read_bytes())
                for locale in ("es", "en", "fr")}
    result = {}
    for locale, expected in PUBLICATIONS[publication].items():
        relative = (Path("app/src/main/assets/content") / locale / RELATIVE).as_posix()
        payload = publication_bytes(ROOT, relative, expected)
        result[locale] = json.loads(verify_publication_bytes(payload, expected))
    return result


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


def pilot(publication: str = "working-tree") -> tuple[dict, dict]:
    family = documents(publication)
    original = family["es"]
    assert original["stateLayout"]["version"] == 2
    assert original["stateLayout"]["locale"] == "es"
    assert set(original["stateLayout"]["family"]) == {"es", "en", "fr"}
    state.validate(original)
    archived = original["stateLayout"].get("sourceFormatRevisions", [])
    by_id = {entry["id"]: entry["block"] for entry in archived}
    original_blocks, _ = active_owners(original)
    changed = deepcopy(original)
    changed_blocks, _ = active_owners(changed)
    additions = []
    for identity in OWNERS:
        before = original_blocks[identity]
        assert before["type"] == "code"
        if identity not in by_id:
            after = changed_blocks[identity]
            after["code"] = format_real_code(before["code"])
            assert require_source_format(before, after)
            assert source_projection(after, before) == before
            additions.append(identity)
    formatted = (state.prepare_v2(changed, "es", family, original)
                 if additions else original)
    state.validate_transition(original, formatted)
    assert state.validate(original) == state.validate(formatted)
    assert original["stateLayout"]["origin"] == formatted["stateLayout"]["origin"]
    assert original["stateLayout"]["family"] == formatted["stateLayout"]["family"]
    assert original["stateLayout"]["additions"] == formatted["stateLayout"]["additions"]
    records = formatted["stateLayout"]["sourceFormatRevisions"]
    assert records[:len(archived)] == archived
    appended = records[len(archived):]
    assert len(appended) == len(additions)
    assert {entry["id"] for entry in appended} == set(additions)
    assert (formatted["stateLayout"]["editorial"][:len(original["stateLayout"]["editorial"])]
            == original["stateLayout"]["editorial"])
    assert len(formatted["stateLayout"]["editorial"]) == len(original["stateLayout"]["editorial"]) + len(additions)
    snapshots = {entry["id"]: entry["block"] for entry in records}
    current_blocks, positions = active_owners(formatted)
    comparison = deepcopy(formatted)
    for identity in OWNERS:
        saved, current = snapshots[identity], current_blocks[identity]
        # Keep the original pilot's exact three-spacing revision as evidence,
        # while permitting later authenticated formatting of the same owner.
        first_format = deepcopy(saved)
        first_format["code"] = format_real_code(saved["code"])
        edge = {"id": identity, "beforeSha256": state.fingerprint(saved),
                "afterSha256": state.fingerprint(first_format)}
        assert edge in formatted["stateLayout"]["editorial"]
        assert require_source_format(saved, current)
        assert source_projection(current, saved) == saved
        page, block = positions[identity]
        comparison["pages"][page]["blocks"][block] = deepcopy(saved)
        if identity in additions:
            assert saved == original_blocks[identity]
    # This view compares canonical source bytes only. Never strip editorial
    # edges or other owners' archives to fabricate a historical publication.
    return (original if len(additions) == len(OWNERS) else comparison), formatted


def run_cpp(source: str, directory: Path, name: str) -> str:
    src = directory / f"{name}.cpp"
    artifact = directory / (name + ".exe")
    src.write_text(source, encoding="utf-8")
    # Windows is the only supported runtime: build with the same MSVC family
    # as the student's program, keeping compiler objects in disposable QA.
    compile_run = subprocess.run(
        ["cl", "/nologo", "/std:c++latest", "/EHsc", "/W4",
         "/permissive-", "/Zc:__cplusplus", "/utf-8",
         str(src), "/Fe:" + str(artifact)],
        cwd=directory, capture_output=True, text=True, timeout=90, check=False)
    if compile_run.returncode:
        raise AssertionError(f"C++ compilation failed: {compile_run.stderr}")
    execution = subprocess.run([str(artifact)], capture_output=True, text=True,
                               timeout=15, check=False)
    if execution.returncode or execution.stderr:
        raise AssertionError(f"Program failed: {execution.returncode}, {execution.stderr}")
    return execution.stdout


def append_independent_editorial(published: dict, family: dict[str, dict]) -> dict:
    # Simulate another real editorial publication after formatting, including
    # the explicit semantic fingerprint required by the Windows reader.
    changed = deepcopy(published)
    paragraph = active_owners(changed)[0]["block_0000_0003"]
    paragraph["text"] += " Comprueba los datos antes de calcular."
    descriptor = next(field for field in changed["textSemantics"]["fields"]
                      if field["block"] == "block_0000_0003" and field["path"] == "/text")
    assert descriptor["spans"] == []
    descriptor["sourceSha256"] = hashlib.sha256(
        paragraph["text"].encode("utf-8")).hexdigest()
    return state.prepare_v2(changed, "es", family, published)


class RealPublishedFormatPilot(unittest.TestCase):
    def test_real_immutable_source_identity_and_safe_revision(self):
        before_bytes = source_path("es").read_bytes()
        old, revised = pilot()
        self.assertEqual(before_bytes, source_path("es").read_bytes())
        old_blocks, new_blocks = all_owners(old), all_owners(revised)
        for identity in old_blocks.keys() - set(OWNERS):
            self.assertEqual(old_blocks[identity], new_blocks[identity], identity)
        for identity in OWNERS:
            self.assertNotEqual(old_blocks[identity]["code"], new_blocks[identity]["code"])

    def test_real_code_compiles_and_outputs_identically(self):
        original, formatted = pilot()
        with tempfile.TemporaryDirectory(prefix="lgaed-real-format-") as path:
            directory = Path(path)
            old_blocks, new_blocks = all_owners(original), all_owners(formatted)
            for index, identity in enumerate(OWNERS):
                old, new = old_blocks[identity]["code"], new_blocks[identity]["code"]
                with self.subTest(owner=identity):
                    actual_before = run_cpp(old, directory, f"baseline_{index}")
                    actual_after = run_cpp(new, directory, f"formatted_{index}")
                    self.assertEqual(actual_before, EXPECTED_STDOUT)
                    self.assertEqual(actual_after, actual_before)

    def test_real_answer_and_archived_source_forgeries_rejected(self):
        original, formatted = pilot()
        changed_answer = deepcopy(formatted)
        all_owners(changed_answer)["block_0003_0003"]["answers"] = ["forged"]
        with self.assertRaises(state.LayoutError):
            state.validate(changed_answer)
        changed_code = deepcopy(formatted)
        code = all_owners(changed_code)[OWNERS[0]]
        code["code"] = code["code"].replace(
            "const int fichas = 18;", "const int fichas = 19;", 1)
        with self.assertRaises((state.LayoutError, SourceFormatError)):
            state.validate(changed_code)
        forged_archive = deepcopy(formatted)
        next(entry["block"] for entry in forged_archive["stateLayout"]["sourceFormatRevisions"]
             if entry["id"] == OWNERS[0])["code"] += " // forged"
        with self.assertRaises(state.LayoutError):
            state.validate(forged_archive)
        if original["stateLayout"] != formatted["stateLayout"]:
            self.assertEqual(state.validate(original), state.validate(formatted))
        else:
            # A comparison view after later editorial changes is intentionally
            # not a valid prior publication; its immutable metadata is exact.
            self.assertEqual(original["stateLayout"], formatted["stateLayout"])
            state.validate(formatted)

    def test_windows_fixtures_remain_valid_after_later_editorial(self):
        family = documents()
        revised = append_independent_editorial(family["es"], family)
        with tempfile.TemporaryDirectory(prefix="lgaed-real-later-fixtures-") as path:
            with mock.patch.dict(pilot.__globals__,
                                 {"documents": lambda publication="working-tree": {**family, "es": revised}}):
                write_fixtures(Path(path))
            previous = json.loads((Path(path) / "real.before.json").read_text(encoding="utf-8"))
            current = json.loads((Path(path) / "real.after.json").read_text(encoding="utf-8"))
            later = json.loads((Path(path) / "after-editorial" / "real.after.json")
                               .read_text(encoding="utf-8"))
        state.validate(previous)
        state.validate(current)
        state.validate(later)
        self.assertEqual(previous, current)
        self.assertEqual(current, revised)
        current_blocks = all_owners(current)
        for identity in OWNERS:
            archived = next(entry["block"] for entry in current["stateLayout"]["sourceFormatRevisions"]
                            if entry["id"] == identity)
            self.assertNotEqual(archived["code"], current_blocks[identity]["code"])

    def test_later_editorial_update_preserves_format_proofs(self):
        # Reproduce a later, independent paragraph correction (as in main)
        # without modifying any on-disk curriculum or historical snapshots.
        family = documents()
        published = family["es"]
        revised = append_independent_editorial(published, family)
        self.assertEqual(revised["stateLayout"]["editorial"][-1]["id"], "block_0000_0003")
        self.assertEqual(revised["stateLayout"]["sourceFormatRevisions"],
                         published["stateLayout"]["sourceFormatRevisions"])
        with mock.patch.dict(pilot.__globals__,
                             {"documents": lambda publication="working-tree": {**family, "es": revised}}):
            before, after = pilot()
        self.assertEqual(after, revised)
        self.assertEqual(before["stateLayout"], revised["stateLayout"])
        before_blocks, after_blocks = all_owners(before), all_owners(after)
        for identity in OWNERS:
            self.assertNotEqual(before_blocks[identity]["code"], after_blocks[identity]["code"])
        forged = deepcopy(after)
        forged["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
        with self.assertRaises(state.LayoutError):
            state.validate(forged)

    def test_complete_publications_keep_coexisting_archives_and_later_format(self):
        for publication in PUBLICATIONS:
            with self.subTest(publication=publication):
                family = documents(publication)
                published = family["es"]
                untouched = deepcopy(published)
                comparison, current = pilot(publication)
                self.assertEqual(published, untouched)
                self.assertEqual(current, published)
                self.assertEqual(comparison["stateLayout"], published["stateLayout"])
                # These are intentionally complete publications: 83 contains
                # four additional real archives and retired compiler owners.
                expected = 2 if publication == "921e1df" else 6
                self.assertEqual(len(current["stateLayout"]["sourceFormatRevisions"]), expected)
                changed = deepcopy(current)
                all_owners(changed)[OWNERS[0]]["code"] = "\n" + all_owners(changed)[OWNERS[0]]["code"]
                later = state.prepare_v2(changed, "es", family, current)
                with mock.patch.dict(pilot.__globals__,
                                     {"documents": lambda publication="working-tree": {**family, "es": later}}):
                    earlier_source, resolved = pilot()
                    with tempfile.TemporaryDirectory(prefix="lgaed-format-coexisting-") as folder:
                        write_fixtures(Path(folder))
                        exported = json.loads((Path(folder) / "real.before.json").read_bytes())
                self.assertEqual(resolved, later)
                self.assertEqual(exported, later)
                self.assertEqual(earlier_source["stateLayout"], later["stateLayout"])
                self.assertEqual(later["stateLayout"]["sourceFormatRevisions"],
                                 current["stateLayout"]["sourceFormatRevisions"])
                self.assertEqual(later["stateLayout"]["editorial"][:-1],
                                 current["stateLayout"]["editorial"])

    def test_pinned_publications_reject_changed_bytes_and_missing_history(self):
        for name, locales in PUBLICATIONS.items():
            for locale, publication in locales.items():
                relative = (Path("app/src/main/assets/content") / locale / RELATIVE).as_posix()
                payload = publication_bytes(ROOT, relative, publication)
                with self.subTest(publication=name, locale=locale):
                    self.assertEqual(verify_publication_bytes(payload, publication), payload)
                    with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                        verify_publication_bytes(payload + b"\n", publication)
            with mock.patch.dict(pilot.__globals__,
                                 {"publication_bytes": mock.Mock(side_effect=FileNotFoundError("history"))}):
                with self.assertRaises(FileNotFoundError):
                    documents(name)


def write_fixtures(directory: Path, publication: str = "working-tree") -> None:
    original, formatted = pilot(publication)
    # A source-only comparison view after a later editorial revision is not a
    # valid loadable lesson: its block bytes precede its current editorial SHA.
    # Use the authentic current document for both physical fixtures then.
    # Windows retrieves the older source directly from the authenticated
    # sourceFormatRevisions, rather than loading a forged historical lesson.
    before = (formatted if original["stateLayout"] == formatted["stateLayout"]
              else original)
    later = append_independent_editorial(formatted, documents(publication))
    directory.mkdir(parents=True, exist_ok=True)
    pairs = (
        ("real.before.json", before), ("real.after.json", formatted),
        ("after-editorial/real.before.json", later),
        ("after-editorial/real.after.json", later),
    )
    for name, document in pairs:
        state.validate(document)
        destination = directory / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8")
    print("PASS real U4 decision lesson: authenticated current/archived sources, original assets untouched")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--publication", choices=("working-tree", *PUBLICATIONS), default="working-tree")
    args = parser.parse_args()
    write_fixtures(args.output_dir, args.publication)


if __name__ == "__main__":
    main()

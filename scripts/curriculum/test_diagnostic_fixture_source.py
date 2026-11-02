"""Pinned-source, destination and preservation checks for diagnostic QA inputs."""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import diagnostic_fixture_source as fixture


class DiagnosticFixtureSourceTest(unittest.TestCase):
    def setUp(self):
        fixture.historical_bytes.cache_clear()

    def tearDown(self):
        fixture.historical_bytes.cache_clear()

    @contextmanager
    def own_output(self):
        qa = (fixture.ROOT / "out/qa").resolve()
        qa.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="diagnostic-fixture-test-", dir=qa)
        output = Path(temporary.name).resolve()
        self.assertTrue(output.is_relative_to(qa)); self.assertNotEqual(output, qa)
        try:
            yield output
        finally:
            # Only this exclusively created, resolved QA child is removed.
            temporary.cleanup()

    def test_real_local_bytes_have_the_three_exact_pins_and_no_added_diagnostic(self):
        self.assertEqual("7862f1064487a961d5cfc9668d4316da6d1b3ef2", fixture.BASELINE_COMMIT)
        self.assertEqual(3, len(fixture.FIXTURE_SHA256))
        targets = {
            "lesson_consultas_const_y_objetos_constantes.json": [(6, 6)],
            "lesson_valores_por_defecto_con_significado.json": [(12, 8), (12, 11), (14, 11)],
            "lesson_argumentos_con_tipos_que_expresan_su_funcion.json": [(6, 6)],
        }
        self.assertEqual(set(targets), set(fixture.FIXTURE_SHA256))
        for filename, positions in targets.items():
            with self.subTest(filename=filename):
                payload = fixture.historical_bytes(filename)
                self.assertEqual(fixture.FIXTURE_SHA256[filename], hashlib.sha256(payload).hexdigest())
                document = json.loads(payload)
                for page, index in positions:
                    block = document["pages"][page]["blocks"][index]
                    self.assertEqual("cpp_compiler", block["type"])
                    self.assertNotIn("compileCheck", block)
                    self.assertTrue(block["initialCode"])

    def test_corrupt_or_unknown_input_is_rejected_without_creating_output(self):
        filenames = list(fixture.FIXTURE_SHA256)
        original = [fixture.historical_bytes(name) for name in filenames]
        with self.assertRaisesRegex(ValueError, "Unknown"):
            fixture.historical_bytes("../lesson.json")
        for filename, payload in zip(filenames, original):
            with self.subTest(filename=filename), self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                fixture.verify_bytes(filename, payload[:-1] + bytes([payload[-1] ^ 1]))
        fixture.historical_bytes.cache_clear()
        with self.own_output() as parent, patch.object(fixture.subprocess, "check_output",
                side_effect=[original[0], original[1] + b"changed"]) as show:
            output = parent / "not-created"
            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                fixture.export_fixtures(output)
            self.assertEqual(2, show.call_count); self.assertFalse(output.exists())

    def test_missing_git_object_is_local_only_and_never_reads_current_assets(self):
        filename = next(iter(fixture.FIXTURE_SHA256))
        with patch.object(fixture.subprocess, "check_output", side_effect=subprocess.CalledProcessError(128, "git show")) as show, \
                patch.object(fixture.subprocess, "run", side_effect=AssertionError("No fallback command")) as other, \
                patch.object(Path, "read_bytes", side_effect=AssertionError("No current-asset fallback")):
            with self.assertRaisesRegex(RuntimeError, "Missing local diagnostic fixture"):
                fixture.historical_bytes(filename)
        show.assert_called_once(); other.assert_not_called()
        arguments, keywords = show.call_args
        self.assertEqual(["git", "--no-optional-locks", "-c", f"safe.directory={fixture.ROOT}", "show",
            f"{fixture.BASELINE_COMMIT}:{fixture.RELATIVE_UNIT}/{filename}"], arguments[0])
        self.assertEqual(fixture.ROOT, keywords["cwd"])
        self.assertEqual("1", keywords["env"]["GIT_NO_LAZY_FETCH"])
        self.assertEqual("1", keywords["env"]["GIT_NO_REPLACE_OBJECTS"])

    def test_export_is_exact_and_identical_files_keep_their_mtimes(self):
        with self.own_output() as parent:
            output = parent / "diagnostics-7862f10"
            targets = fixture.export_fixtures(output)
            self.assertEqual({output / name for name in fixture.FIXTURE_SHA256}, set(targets))
            for index, target in enumerate(targets):
                self.assertEqual(fixture.historical_bytes(target.name), target.read_bytes())
                stamp = 1_600_000_000_000_000_000 + index * 10_000_000
                os.utime(target, ns=(stamp, stamp))
            before = {target: (target.read_bytes(), target.stat().st_mtime_ns) for target in targets}
            self.assertEqual(targets, fixture.export_fixtures(output))
            self.assertEqual(before, {target: (target.read_bytes(), target.stat().st_mtime_ns) for target in targets})
            self.assertEqual(set(targets), set(output.iterdir()))

    def test_conflict_or_directory_is_rejected_before_any_partial_export(self):
        filenames = list(fixture.FIXTURE_SHA256)
        for directory in (False, True):
            with self.subTest(directory=directory), self.own_output() as output:
                conflict = output / filenames[1]
                if directory:
                    conflict.mkdir()
                else:
                    conflict.write_bytes(b"existing unrelated bytes")
                before = conflict.stat().st_mtime_ns
                with self.assertRaises(FileExistsError):
                    fixture.export_fixtures(output)
                self.assertFalse((output / filenames[0]).exists()); self.assertFalse((output / filenames[2]).exists())
                self.assertEqual(before, conflict.stat().st_mtime_ns)
                if not directory:
                    self.assertEqual(b"existing unrelated bytes", conflict.read_bytes())

    def test_authored_and_source_destinations_are_rejected_before_mkdir(self):
        for relative in (".", "app", "app/src/main/assets/diagnostics-forbidden", "native", "scripts", "cmake", "docs", "toolchains", ".git"):
            with self.subTest(relative=relative), patch.object(Path, "mkdir", side_effect=AssertionError("No source writes")) as mkdir:
                with self.assertRaisesRegex(ValueError, "authored/source trees"):
                    fixture.export_fixtures(fixture.ROOT / relative)
                mkdir.assert_not_called()
        with self.own_output() as parent:
            with self.assertRaisesRegex(ValueError, "authored/source trees"):
                fixture.export_fixtures(parent / "app/src/main/assets")
            self.assertFalse((parent / "app").exists())
            checkout = parent / "another-checkout"; (checkout / ".git").mkdir(parents=True)
            with self.assertRaisesRegex(ValueError, "another source checkout"):
                fixture.export_fixtures(checkout / "out")
            self.assertFalse((checkout / "out").exists())

    def test_reparse_ancestor_is_rejected_without_following_it(self):
        # Exercise the Windows junction/reparse attribute without requiring
        # elevation/Developer Mode or creating a link to authored files.
        with self.own_output() as parent:
            alias = parent / "alias"; alias.mkdir(); original_lstat = Path.lstat
            class Reparse:
                st_mode = 0o040755
                st_file_attributes = 0x400
            def attributes(path, *args, **kwargs):
                return Reparse() if path == alias else original_lstat(path, *args, **kwargs)
            with patch.object(Path, "lstat", attributes):
                with self.assertRaisesRegex(ValueError, "reparse points"):
                    fixture.export_fixtures(alias / "not-created")
            self.assertEqual([], list(alias.iterdir()))

    def test_cli_exports_only_the_three_pinned_files(self):
        with self.own_output() as parent:
            output = parent / "cli"
            command = [sys.executable, "-B", str(Path(fixture.__file__)), "--output-dir", str(output)]
            result = subprocess.run(command, cwd=fixture.ROOT, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(set(fixture.FIXTURE_SHA256), {path.name for path in output.iterdir()})
            target = output / next(iter(fixture.FIXTURE_SHA256)); target.write_bytes(b"preserve conflict")
            result = subprocess.run(command, cwd=fixture.ROOT, capture_output=True, text=True, encoding="utf-8")
            self.assertNotEqual(0, result.returncode); self.assertIn("Refusing to overwrite", result.stderr)
            self.assertEqual(b"preserve conflict", target.read_bytes())


if __name__ == "__main__":
    unittest.main(verbosity=2)

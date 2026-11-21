"""Verifica la cadena MSVC C++23 sin compiladores adicionales.

Los tres ejercicios U3L9 conservan sus identidades históricas, pero el
ejecutor y los recibos deben ser nativos Windows.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"


class WindowsCompilerPolicy(unittest.TestCase):
    def test_migrated_curriculum_audits_are_native_windows_python(self):
        for name in (
                "programming-multicourse-contract.yml",
                "codex-readability-audit.yml",
                "p2-u2-client-runtime.yml",
                "codex-u3-diagnostic-facts.yml",
                "simulation-core-codex.yml",
                "codex-cpp-expression-repair.yml",
                "codex-assetcheck-u4l7-format.yml",
                "codex-general-choice-editorial.yml",
                "codex-math-matching-text.yml",
                "codex-open-battery-contract.yml",
                "codex-p3-ownership-authoring.yml"):
            with self.subTest(workflow=name):
                source = (WORKFLOWS / name).read_text(encoding="utf-8")
                self.assertNotIn("ubuntu-", source)
                self.assertNotIn("python3 ", source)
                self.assertIn("actions/setup-python@v5", source)
                self.assertIn("runs-on: windows-2022", source)
                self.assertIn("python -B", source)

    def test_no_obsolete_android_build_workflows(self):
        for name in ("android-code-format-compat.yml", "build-clang23-android.yml"):
            self.assertFalse((WORKFLOWS / name).exists(), name)

    def test_active_workflows_do_not_invoke_gcc_or_desktop_clang(self):
        # Match executable/runner tokens, not words like the curricular
        # explanation of a compiler or the "Windows" product description.
        unused = re.compile(r"(?<![A-Za-z0-9_])(?:g\+\+|clang\+\+|gcc)(?![A-Za-z0-9_])",
                            re.IGNORECASE)
        workflows = sorted(WORKFLOWS.glob("*.yml"))
        self.assertTrue(workflows)
        for path in workflows:
            with self.subTest(workflow=path.name):
                self.assertNotRegex(path.read_text(encoding="utf-8"), unused)

    def test_native_pins_and_removed_wasi_dependencies(self):
        manifest = json.loads((ROOT / "toolchains/windows.versions.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["cpp"]["standard"], 23)
        self.assertTrue(manifest["visualStudio"]["msvcToolset"])
        self.assertTrue(manifest["visualStudio"]["windowsSdk"])
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        self.assertIn("windows-x64-debug", {p["name"] for p in presets["configurePresets"]})
        self.assertFalse((WORKFLOWS / "windows-wasi-execution.yml").exists())
        self.assertFalse((ROOT / "scripts/windows/Install-WasiTools.ps1").exists())
        runner = (ROOT / "native/platform/windows/cpp_runner.cpp").read_text(encoding="utf-8")
        self.assertNotIn("run_wasi_single", runner)
        self.assertNotIn("Backend::wasi", runner)
        source = (ROOT / "native/core/assertion_runtime_check.hpp").read_text(encoding="utf-8")
        self.assertIn("assertion_native_abort", source)
        self.assertNotIn("assertion_runtime_trap", source)
        qa = (WORKFLOWS / "codex-native-assertion-migration.yml").read_text(encoding="utf-8")
        self.assertIn("lgaed_native_assertion_runner_tests", qa)
        self.assertIn("test_native_assertions_msvc.py", qa)

    def test_windows_source_execution_probes_do_not_use_gcc(self):
        for relative in (
                "scripts/curriculum/test_real_code_format_pilot.py",
                "scripts/curriculum/test_current_batched_client_split.py"):
            with self.subTest(source=relative):
                source = (ROOT / relative).read_text(encoding="utf-8")
                self.assertIn('"cl"', source)
                self.assertNotRegex(source, r'["\'](?:g\+\+|clang\+\+|c\+\+)["\']')

    def test_legibility_suite_runs_all_originals_on_windows_msvc(self):
        workflow = (WORKFLOWS / "legibilidad-codigo.yml").read_text(encoding="utf-8")
        self.assertIn("runs-on: windows-2022", workflow)
        self.assertIn("ilammy/msvc-dev-cmd@v1", workflow)
        self.assertIn("test_windows_cpp_ci.py", workflow)
        files = sorted(set(re.findall(r"-p (test_[a-z0-9_]+\.py)", workflow)))
        self.assertGreaterEqual(len(files), 45)
        validated = 0
        for name in files:
            source = (ROOT / "scripts" / "curriculum" / name).read_text(encoding="utf-8")
            with self.subTest(original_test=name):
                for compiler in ("g++", "clang++", "c++"):
                    self.assertNotIn(f'"{compiler}"', source)
                    self.assertNotIn(f"'{compiler}'", source)
                if "msvc_cpp23_command(" in source:
                    self.assertIn("from windows_cpp_ci import msvc_cpp23_command", source)
                    validated += 1
        self.assertGreaterEqual(validated, 40, "Lost real native MSVC coverage")

    def test_real_rename_runtime_uses_msvc_not_unneeded_compilers(self):
        source = (ROOT / "scripts/curriculum/test_identifier_rename_execution.py").read_text(
            encoding="utf-8")
        self.assertIn('shutil.which("cl.exe")', source)
        self.assertIn('"/std:c++latest"', source)
        self.assertNotIn('shutil.which("g++")', source)
        self.assertNotIn('shutil.which("clang++")', source)


if __name__ == "__main__":
    unittest.main()

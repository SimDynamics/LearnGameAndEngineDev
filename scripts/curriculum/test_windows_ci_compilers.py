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
                "codex-p3-ownership-authoring.yml",
                "assetcheck-curricular-oracles.yml",
                "named-rule-core-codex.yml",
                "general-choice-editorial-codex.yml",
                "codex-local-int-dependency.yml",
                "codex-compiler-guidance-source.yml"):
            with self.subTest(workflow=name):
                source = (WORKFLOWS / name).read_text(encoding="utf-8")
                self.assertNotIn("ubuntu-", source)
                self.assertNotIn("python3 ", source)
                self.assertIn("actions/setup-python@v5", source)
                self.assertIn("runs-on: windows-2022", source)
                self.assertIn("python -B", source)

    def test_order_equivalence_uses_windows_python_without_bash(self):
        source = (WORKFLOWS / "codex-order-equivalence-authoring.yml").read_text(encoding="utf-8")
        self.assertIn("runs-on: windows-2022", source)
        self.assertNotIn("matrix.os", source)
        self.assertIn("actions/setup-python@v5", source)
        self.assertIn("shell: pwsh", source)
        self.assertNotIn("shell: bash", source)
        self.assertIn("python -B", source)

    def test_single_windows_runner_jobs_do_not_need_a_matrix(self):
        # Preserve all native and Python checks while avoiding redundant
        # one-entry OS matrices and accidentally skipped Windows steps.
        for name in (
                "codex-batched-client-split.yml",
                "codex-order-equivalence-authoring.yml",
                "codex-course-edition-portable.yml",
                "codex-compiler-case-evaluator.yml"):
            with self.subTest(workflow=name):
                source = (WORKFLOWS / name).read_text(encoding="utf-8")
                self.assertIn("runs-on: windows-2022", source)
                self.assertNotIn("matrix.os", source)
                self.assertNotIn("os: [windows-2022]", source)
                self.assertNotIn("if: runner.os == 'Windows'", source)
        guarded = (WORKFLOWS / "codex-batched-client-split.yml").read_text(
            encoding="utf-8")
        first = guarded.index("test_windows_ci_compilers.py -v")
        second = guarded.index("test_windows_cpp_ci.py -v", first)
        self.assertIn("if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }",
                      guarded[first:second])
        self.assertIn("if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }",
                      guarded[second:])

    def test_assetcheck_reference_build_uses_one_explicit_windows_generator(self):
        # The four authentic reference executables were already built on
        # Windows VS 2026. A one-item matrix added no coverage and could
        # hide a future generator/report mismatch.
        source = (WORKFLOWS / "assetcheck-curricular-oracles.yml").read_text(
            encoding="utf-8")
        self.assertIn("runs-on: windows-2025-vs2026", source)
        self.assertNotIn("matrix.", source)
        self.assertNotIn("matrix:", source)
        self.assertIn('--generator "Visual Studio 18 2026"', source)
        self.assertIn('--report "out/qa/assetcheck-curriculum/msvc-x64.json"',
                      source)
        self.assertIn("name: assetcheck-curriculum-msvc-x64-${{ github.sha }}",
                      source)
        self.assertIn("path: out/qa/assetcheck-curriculum/msvc-x64.json",
                      source)
        for job in ("check-contracts:", "compile-curricular-references:",
                    "check-cmake-preparation:", "capture-u4l3-real-console:"):
            self.assertIn(job, source)

    def test_no_obsolete_android_build_workflows(self):
        for name in ("android-code-format-compat.yml", "build-clang23-android.yml"):
            self.assertFalse((WORKFLOWS / name).exists(), name)

    @staticmethod
    def _uses_non_windows_runner(source):
        # A runner may be a scalar, an inline list or one item of a matrix.
        # Checking only 'runs-on: ubuntu' misses 'os: [windows, linux]'.
        unsupported = re.compile(r"(?i)\b(?:ubuntu|linux|macos)(?:[-.][\w.]+)?\b")
        runner_key = re.compile(r"(?i)^\s*(?:-\s*)?(?:runs-on|os)\s*:\s*(.*)$")
        matrix_item = re.compile(r"(?i)^\s*-\s*([a-z][a-z0-9_.-]*)\s*$")
        for line in source.splitlines():
            line = line.partition("#")[0]  # Comments are not runnable labels.
            key = runner_key.match(line)
            if key and unsupported.search(key.group(1)):
                return True
            item = matrix_item.match(line)
            if item and unsupported.fullmatch(item.group(1)):
                return True
        return False

    def test_all_workflow_runners_and_python_commands_remain_windows_only(self):
        workflows = sorted((*WORKFLOWS.glob("*.yml"), *WORKFLOWS.glob("*.yaml")))
        self.assertTrue(workflows)
        for path in workflows:
            with self.subTest(workflow=path.name):
                source = path.read_text(encoding="utf-8")
                self.assertFalse(
                    self._uses_non_windows_runner(source),
                    "A workflow or inline matrix restored a non-Windows runner")
                self.assertNotRegex(source, r"(?i)\bpython3(?:[.]exe)?\b")

    def test_linux_runner_hidden_in_inline_matrix_is_detected(self):
        bad = (
            "runs-on: ubuntu-24.04",
            "runs-on: [self-hosted, linux, x64]",
            "strategy:\n  matrix:\n    os: [windows-2022, linux]",
            "strategy:\n  matrix:\n    include:\n      - os: macos-15",
            "strategy:\n  matrix:\n    os:\n      - windows-2022\n      - ubuntu-24.04",
        )
        for source in bad:
            with self.subTest(source=source):
                self.assertTrue(self._uses_non_windows_runner(source))
        for source in (
            "runs-on: windows-2022",
            "runs-on: ${{ matrix.os }}\nstrategy:\n  matrix:\n    os: [windows-2022, windows-2025-vs2026]",
            "runs-on: windows-2022 # linux was retired",
            "name: Linux was removed\nruns-on: windows-2022",
        ):
            with self.subTest(source=source):
                self.assertFalse(self._uses_non_windows_runner(source))

    def test_unused_android_only_launcher_scripts_are_absent(self):
        # These three standalone launchers had no Windows CMake, native,
        # workflow, Gradle or documented call sites. The Git history retains
        # their exact former sources; the independent Android app is untouched.
        for relative in (
                "scripts/build_debug_dual_clang.ps1",
                "tools/build-clang23-android-local.ps1",
                "tools/import-clang23-arm64.ps1"):
            with self.subTest(file=relative):
                self.assertFalse((ROOT / relative).exists())

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

    def test_compiler_policy_uses_focused_ci_without_unrelated_native_rebuilds(self):
        policy = "scripts/curriculum/test_windows_ci_compilers.py"
        fast = (WORKFLOWS / "codex-batched-client-split.yml").read_text(encoding="utf-8")
        self.assertIn("- .github/workflows/**", fast)
        self.assertIn("- " + policy, fast)
        self.assertIn("test_windows_ci_compilers.py -v", fast)
        multicourse = (WORKFLOWS / "programming-multicourse-contract.yml").read_text(
            encoding="utf-8")
        self.assertNotIn(policy, multicourse)
        self.assertNotIn("unittest -v test_windows_ci_compilers", multicourse)
        self.assertIn("test_programming_multicourse_validation.py", multicourse)
        state_import = (WORKFLOWS / "windows-state-import.yml").read_text(
            encoding="utf-8")
        self.assertLess(
            state_import.index("      - scripts/curriculum/**"),
            state_import.index("      - '!scripts/curriculum/test_windows_ci_compilers.py'"))
        for targeted in ("test_windows_ci_compilers.py",
                         "test_local_int_live_compilation.py",
                         "test_local_string_live_compilation.py"):
            with self.subTest(excluded_from_state_import=targeted):
                self.assertLess(
                    state_import.index("      - scripts/curriculum/**"),
                    state_import.index(
                        "      - '!scripts/curriculum/" + targeted + "'"))
                self.assertIn(
                    "      - '!scripts/curriculum/" + targeted + "'",
                    state_import)
        self.assertIn("      - native/core/**", state_import)
        self.assertIn("Test-StateImportCI.ps1", state_import)

    def test_historical_state_import_runs_python_before_pinned_msvc_preflight(self):
        workflow = (WORKFLOWS / "windows-state-import.yml").read_text(encoding="utf-8")
        python = workflow.index("Select the declared Python version")
        exact_stderr = workflow.index("Test exact stderr authoring")
        assertions = workflow.index("Verify U3L9 assertion declarations")
        case_history = workflow.index("Verify authenticated case revisions")
        pinned = workflow.index("Fail early if hosted Windows lacks the exact declared MSVC")
        native = workflow.index("Verify MSVC and execute history")
        self.assertLess(python, exact_stderr)
        self.assertLess(exact_stderr, assertions)
        self.assertLess(assertions, case_history)
        self.assertLess(case_history, pinned)
        self.assertLess(pinned, native)
        self.assertIn("-Suite StateImport -MsvcPreflightOnly", workflow)

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

    def test_u2_local_variable_probes_use_only_windows_msvc(self):
        # Preserve the 55 integer and 30 string source executions while
        # retiring inactive GCC/Clang branches and Linux binary formats.
        for name in ("test_local_int_live_compilation.py",
                     "test_local_string_live_compilation.py"):
            with self.subTest(script=name):
                source = (ROOT / "scripts/curriculum" / name).read_text(
                    encoding="utf-8")
                self.assertIn(
                    "from windows_cpp_ci import msvc_cpp23_command", source)
                self.assertIn('choices=("MSVC",)', source)
                self.assertIn("return msvc_cpp23_command(binary, source)", source)
                self.assertNotIn('"g++"', source)
                self.assertNotIn('"clang++"', source)
                self.assertNotIn('".bin"', source)
        workflow = (WORKFLOWS / "codex-local-int-dependency.yml").read_text(
            encoding="utf-8")
        for script in ("test_local_int_live_compilation.py",
                       "test_local_string_live_compilation.py"):
            self.assertIn(script + " --emitter", workflow)
        self.assertGreaterEqual(workflow.count("--compiler MSVC"), 2)

    def test_real_rename_runtime_uses_msvc_not_unneeded_compilers(self):
        source = (ROOT / "scripts/curriculum/test_identifier_rename_execution.py").read_text(
            encoding="utf-8")
        self.assertIn('shutil.which("cl.exe")', source)
        self.assertIn('"/std:c++latest"', source)
        self.assertNotIn('shutil.which("g++")', source)
        self.assertNotIn('shutil.which("clang++")', source)


if __name__ == "__main__":
    unittest.main()

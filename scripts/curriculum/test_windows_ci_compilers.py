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

    def test_exact_workflow_watch_paths_exist(self):
        # Retirement of Android-only tools must not leave dead path filters
        # behind. Check literal paths; globs and exclusions are intentional
        # patterns and are not required to resolve to an existing file.
        workflows = sorted((*WORKFLOWS.glob("*.yml"),
                            *WORKFLOWS.glob("*.yaml")))
        self.assertTrue(workflows)
        checked = 0
        for workflow in workflows:
            in_paths = False
            for line in workflow.read_text(encoding="utf-8").splitlines():
                if re.fullmatch(r" {4}paths:\s*(?:#.*)?", line):
                    in_paths = True
                    continue
                if not in_paths:
                    continue
                entry = re.fullmatch(r" {6}-\s+(.+?)\s*", line)
                if entry:
                    path = entry.group(1).strip()
                    if len(path) >= 2 and path[0] in ("'", '"') and path[-1] == path[0]:
                        path = path[1:-1]
                    if path.startswith("!") or re.search(r"[*?\[\]{}]", path):
                        continue
                    checked += 1
                    with self.subTest(workflow=workflow.name, watched=path):
                        self.assertTrue((ROOT / path).exists(),
                                        "A workflow watches a retired repository path")
                elif line.strip() and not line.lstrip().startswith("#"):
                    in_paths = False
        self.assertGreater(checked, 0, "No literal workflow paths were checked")

    def test_workflow_source_invocations_and_unittest_patterns_exist(self):
        # In addition to path filters, verify source paths named in actual
        # workflow commands. Artifact outputs under RUNNER_TEMP are excluded.
        direct = re.compile(
            r"(?<![A-Za-z0-9_.+-])"
            r"((?:scripts|tools|native|cmake)[/\\]+"
            r"(?:[A-Za-z0-9_.+-]+[/\\]+)*"
            r"[A-Za-z0-9_.+-]+\.(?:py|ps1|cpp))"
            r"(?=$|[^A-Za-z0-9_.+-])")
        discovery = re.compile(
            r"-s\s+(scripts[/\\][A-Za-z0-9_-]+)\s+"
            r"-p\s+(test_[A-Za-z0-9_]+\.py)\b")
        # Check that the scanner recognizes a hypothetical retired command,
        # including Windows separators, without executing anything.
        self.assertEqual(
            re.sub(r"[/\\]+", "/", direct.findall(
                r"run: python -B scripts\windows\retired.py")[0]),
            "scripts/windows/retired.py")
        self.assertEqual(
            discovery.findall("-s scripts/curriculum -p test_retired.py"),
            [("scripts/curriculum", "test_retired.py")])

        checked = 0
        for workflow in sorted((*WORKFLOWS.glob("*.yml"),
                                *WORKFLOWS.glob("*.yaml"))):
            source = workflow.read_text(encoding="utf-8")
            for line in source.splitlines():
                if line.lstrip().startswith("#"):
                    continue
                for candidate in direct.findall(line):
                    relative = re.sub(r"[/\\]+", "/", candidate)
                    checked += 1
                    with self.subTest(workflow=workflow.name, source=relative):
                        self.assertTrue((ROOT / relative).is_file(),
                                        "A workflow references a retired source")
            for directory, filename in discovery.findall(source):
                relative = re.sub(r"[/\\]+", "/", directory) + "/" + filename
                checked += 1
                with self.subTest(workflow=workflow.name, unittest=relative):
                    self.assertTrue((ROOT / relative).is_file(),
                                    "An unittest discovery pattern names a missing test")
        self.assertGreater(checked, 0, "No executable source paths were checked")

    def test_editorial_windows_suite_follows_the_live_u1_u2_owners(self):
        workflow = (WORKFLOWS / "codex-general-choice-editorial.yml").read_text(
            encoding="utf-8")
        root = "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        for lesson in (
                "programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json",
                "objetos_y_estado/lesson_conversiones_y_deduccion.json",
                "objetos_y_estado/lesson_cadenas_y_texto.json"):
            with self.subTest(lesson=lesson):
                self.assertIn("      - " + root + lesson, workflow)
        self.assertIn("runs-on: windows-2022", workflow)
        self.assertIn("-p test_review_feedback_prose_revision.py -v", workflow)

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
                "codex-compiler-case-evaluator.yml",
                "codex-reviewed-diagnostic-format.yml"):
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

    def test_windows_tool_paths_match_the_active_manifest_not_android_history(self):
        versions = json.loads(
            (ROOT / "toolchains/windows.versions.json").read_text(encoding="utf-8"))
        guide = (ROOT / "docs/WINDOWS_NATIVE.md").read_text(encoding="utf-8")
        for section in ("cmake", "ffmpeg", "vulkan"):
            with self.subTest(tool=section):
                self.assertIn("`" + versions[section]["path"] + "`", guide)
        self.assertNotIn("C:\\LGP-Tools", guide)
        self.assertIn("14.51.36231", guide)
        legacy = (ROOT / "docs/ADVANCED_CPP_RUNTIME.md").read_text(
            encoding="utf-8")
        self.assertIn("Documento histórico de Android/WASI", legacy)
        self.assertIn("toolchains/versions.properties", legacy)
        self.assertIn("toolchains/windows.versions.json", legacy)
        # The historical toolchain record is still referenced by Android history.
        self.assertTrue((ROOT / "toolchains/versions.properties").is_file())

    def test_android_cmake_recipe_has_live_gradle_consumer_but_no_windows_dependency(self):
        # Android assets are historical, but Gradle still declares a real task
        # that consumes the recipe's generated tools. Preserve that owner.
        gradle = (ROOT / "app/build.gradle.kts").read_text(encoding="utf-8")
        self.assertIn("val prepareCmakeRuntime by tasks.registering", gradle)
        self.assertIn('dir(".gradle/cmake-android-arm64")', gradle)
        self.assertIn("toolchains/cmake/build-android-local.ps1", gradle)
        root = ROOT / "toolchains/cmake"
        script = (root / "build-android-local.ps1").read_text(encoding="utf-8")
        self.assertIn(".gradle/cmake-android-arm64", script)
        self.assertIn("extract-cmake-source.py", script)
        for expected in (
                "provenance.properties", "extract-cmake-source.py",
                "cmake-android-resources.patch", "cmake-android-libuv.patch",
                "cmake-android-pthread-source.patch", "cmake-android-affinity.patch",
                "cmake-android-archive-h.patch", "cmake-android-archive_entry-h.patch",
                "ninja-android-shell.patch", "llvm-ar/CMakeLists.txt"):
            with self.subTest(historical_recipe=expected):
                self.assertTrue((root / expected).is_file())
        historical = (ROOT / "docs/CPP_CMAKE_PROJECTS.md").read_text(
            encoding="utf-8")
        archived_qa = (ROOT / "docs/curriculum/STABLE_LESSON_STATE_VERIFICATION.md").read_text(
            encoding="utf-8")
        self.assertIn("Referencia histórica del laboratorio Android", historical)
        self.assertIn("Evidencia histórica del APK", archived_qa)
        self.assertIn("toolchains/windows.versions.json", historical)
        self.assertIn("docs/WINDOWS_NATIVE.md", historical)
        # The active CMake/MSVC builder must never demand Android's NDK or
        # packaged ARM64 CMake/Ninja, even if old Gradle recipes are retained.
        for source in (
                "CMakeLists.txt", "CMakePresets.json",
                "cmake/WindowsToolchain.cmake",
                "scripts/windows/Build-Native.ps1"):
            with self.subTest(native_source=source):
                content = (ROOT / source).read_text(encoding="utf-8")
                self.assertNotIn("toolchains/cmake/build-android-local.ps1", content)
                self.assertNotIn(".gradle/cmake-android-arm64", content)

    def test_no_obsolete_android_build_workflows(self):
        for name in ("android-code-format-compat.yml", "build-clang23-android.yml"):
            self.assertFalse((WORKFLOWS / name).exists(), name)

    def test_adb_maintenance_duplicates_retired_without_breaking_windows(self):
        # Byte-identical copies reside in SimDynamics/LearnGraphicsProgramming.
        retired = ("backup-android-data.ps1", "restore-kokoro-resources.ps1")
        for name in retired:
            with self.subTest(retired=name):
                self.assertFalse((ROOT / "tools" / name).exists())
        # Native builds, CI, and Windows scripts must not call these old entrypoints.
        sources = [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json",
                   ROOT / "app/build.gradle.kts"]
        for folder in (WORKFLOWS, ROOT / "scripts/windows", ROOT / "cmake"):
            sources.extend(p for p in folder.rglob("*") if p.is_file()
                           and p.suffix in (".yml", ".yaml", ".py", ".ps1", ".cmake", ".txt"))
        for path in sources:
            source = path.read_text(encoding="utf-8")
            for old in retired:
                with self.subTest(source=str(path.relative_to(ROOT)), retired=old):
                    self.assertNotIn(old, source)
        # This historical Gradle task remains a real consumer of its recipe.
        gradle = (ROOT / "app/build.gradle.kts").read_text(encoding="utf-8")
        self.assertIn("scripts/runtime/prepare_clang21_installation.py", gradle)
        self.assertTrue((ROOT / "scripts/runtime/prepare_clang21_installation.py").is_file())

    @staticmethod
    def _uses_non_windows_runner(source):
        # A runner may be a scalar, an inline list or one item of a matrix.
        # Checking only 'runs-on: ubuntu' misses 'os: [windows, linux]'.
        unsupported = re.compile(r"(?i)\b(?:ubuntu|linux|macos)(?:[-.][\w.]+)?\b")
        runner_key = re.compile(r"(?i)^\s*(?:-\s*)?(?:runs-on|os)\s*:\s*(.*)$")
        matrix_item = re.compile(r"(?i)^\s*-\s*([a-z][a-z0-9_.-]*)\s*$")
        matrix_key = re.compile(r"(?i)^\s*matrix\s*:\s*(.*)$")
        matrix_indent = None
        for raw in source.splitlines():
            line = raw.partition("#")[0]  # Comments are not runnable labels.
            if not line.strip():
                continue
            indent = len(line) - len(line.lstrip(" "))
            # Job authors may name their matrix dimension host, image,
            # runner or anything else. The expression ${{ matrix.host }}
            # still selects an OS even when no key is literally named "os".
            if matrix_indent is not None:
                if indent <= matrix_indent:
                    matrix_indent = None
                else:
                    value = line.strip()
                    if value.startswith("- "):
                        value = value[2:].strip()
                    if ":" in value:
                        value = value.split(":", 1)[1].strip()
                    value = value.strip("'\"")
                    if ((value.startswith(("[", "{")) and unsupported.search(value))
                            or unsupported.fullmatch(value)):
                        return True
            matrix = matrix_key.match(line)
            if matrix:
                matrix_indent = indent
                if unsupported.search(matrix.group(1)):
                    return True
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
            "runs-on: ${{ matrix.host }}\nstrategy:\n  matrix:\n    host: [windows-2022, ubuntu-24.04]",
            "runs-on: ${{ matrix.runner }}\nstrategy:\n  matrix:\n    include:\n      - runner: macos-15",
            "strategy:\n  matrix: {image: [windows-2022, linux]}",
        )
        for source in bad:
            with self.subTest(source=source):
                self.assertTrue(self._uses_non_windows_runner(source))
        for source in (
            "runs-on: windows-2022",
            "runs-on: ${{ matrix.os }}\nstrategy:\n  matrix:\n    os: [windows-2022, windows-2025-vs2026]",
            "runs-on: windows-2022 # linux was retired",
            "name: Linux was removed\nruns-on: windows-2022",
            "strategy:\n  matrix:\n    include:\n      - host: windows-2022\n        name: Linux was removed",
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

    @staticmethod
    def _strip_shell_comment(raw):
        # A hash inside a quoted shell argument is data, not a comment.
        # Retain commands chained after that argument; ignore actual trailing
        # comments mentioning compilers that are no longer installed.
        quote = None
        escaped = False
        for index, char in enumerate(raw):
            if escaped:
                escaped = False
                continue
            if char in ("\\", chr(96)):
                escaped = True
            elif char in ("'", '"'):
                if quote is None:
                    quote = char
                elif quote == char:
                    quote = None
            elif char == "#" and quote is None:
                return raw[:index]
        return raw

    @staticmethod
    def _following_command_after_report(tail):
        # A reporting command may contain multiple quoted arguments or
        # PowerShell $(...) expressions. A subsequent executable starts only
        # after a separator outside both scopes. Doubled apostrophes and
        # backtick-escaped quotes are part of a literal, not its boundary.
        quote = None
        escaped = False
        expression_depth = 0
        index = 0
        while index < len(tail):
            char = tail[index]
            if escaped:
                escaped = False
                index += 1
                continue
            if char in ("\\", chr(96)) and quote != "'":
                escaped = True
                index += 1
                continue
            if char == "'" and quote != '"':
                if quote == "'" and tail[index:index + 2] == "''":
                    index += 2
                    continue
                quote = None if quote == "'" else "'"
                index += 1
                continue
            if char == '"' and quote != "'":
                if quote == '"' and tail[index:index + 2] == '""':
                    index += 2
                    continue
                quote = None if quote == '"' else '"'
                index += 1
                continue
            if tail.startswith("$(", index) and quote != "'":
                expression_depth += 1
                index += 2
                continue
            if expression_depth and char == "(":
                expression_depth += 1
            elif expression_depth and char == ")":
                expression_depth -= 1
            elif not expression_depth and quote is None and char in ";&|":
                return tail[index:]
            index += 1
        return ""

    @staticmethod
    def _uses_unneeded_compiler(source):
        # Detect compiler executable tokens, but distinguish log messages
        # and their harmless PowerShell formatting from actual subprocesses.
        unused = re.compile(
            r"(?i)(?<![A-Za-z0-9_.+-])(?:g\+\+|gcc|clang\+\+|clang-cl|clang)"
            r"(?:-[0-9]+)?(?:\.exe)?(?![A-Za-z0-9_.+-])")
        subcommand = re.compile(
            r"(?i)(?:^|[;&|{}()])\s*&?\s*(?:g\+\+|gcc|clang\+\+|clang-cl|clang)"
            r"(?:-[0-9]+)?(?:\.exe)?(?=\s|$|[;&|])")
        report = re.compile(
            r"(?i)^(?:(?:-\s*)?run:\s*)?(?:echo|write-host)\s+")

        def visible_subcommand(expression):
            # Split only on shell operators OUTSIDE quoted PowerShell strings.
            # A format such as "clang; gcc retired" is not a command after
            # its literal semicolon. Keep real chained commands inspectable.
            result = []
            quote = None
            escaped = False
            index = 0
            while index < len(expression):
                char = expression[index]
                if escaped:
                    escaped = False
                    result.append(" ")
                    index += 1
                    continue
                if char in ("\\", chr(96)) and quote != "'":
                    escaped = True
                    result.append(" ")
                    index += 1
                    continue
                if char == "'" and quote != '"':
                    if quote == "'" and expression[index:index + 2] == "''":
                        result.extend("  ")
                        index += 2
                        continue
                    quote = None if quote == "'" else "'"
                    result.append(" ")
                elif char == '"' and quote != "'":
                    if quote == '"' and expression[index:index + 2] == '""':
                        result.extend("  ")
                        index += 2
                        continue
                    quote = None if quote == '"' else '"'
                    result.append(" ")
                else:
                    result.append(" " if quote else char)
                index += 1
            return "".join(result)

        def balanced_substitution_body(tail, opening):
            # PowerShell substitutions may nest $(...), group expressions,
            # and contain quoted parentheses. The first ')' is not their end.
            depth = 1
            quote = None
            escaped = False
            index = opening + 2
            while index < len(tail):
                char = tail[index]
                if escaped:
                    escaped = False
                    index += 1
                    continue
                if char in ("\\", chr(96)) and quote != "'":
                    escaped = True
                    index += 1
                    continue
                if char == "'" and quote != '"':
                    if quote == "'" and tail[index:index + 2] == "''":
                        index += 2
                        continue
                    quote = None if quote == "'" else "'"
                elif char == '"' and quote != "'":
                    if quote == '"' and tail[index:index + 2] == '""':
                        index += 2
                        continue
                    quote = None if quote == '"' else '"'
                elif quote is None:
                    if char == "(":
                        depth += 1
                    elif char == ")":
                        depth -= 1
                        if depth == 0:
                            return tail[opening + 2:index]
                index += 1
            return None

        def invokes_compiler_substitution(tail):
            # Inspect real commands inside $(...), but not quoted report text.
            # Nested substitutions and if (...) groups must not hide compilers.
            quote = None
            escaped = False
            for position, character in enumerate(tail):
                if escaped:
                    escaped = False
                    continue
                if character in ("\\", chr(96)) and quote != "'":
                    escaped = True
                    continue
                if character in ("'", '"'):
                    if quote is None:
                        quote = character
                    elif quote == character:
                        quote = None
                    continue
                if tail.startswith("$(", position) and quote != "'":
                    body = balanced_substitution_body(tail, position)
                    if body is not None and subcommand.search(
                            visible_subcommand(body)):
                        return True
            return False

        for raw in source.splitlines():
            line = WindowsCompilerPolicy._strip_shell_comment(raw).strip()
            if not line or line.startswith(("name:", "- name:")):
                continue
            # Continue through reporting-only commands. A keyword inside
            # another echo/Write-Host is not an executed compiler, even when
            # several log statements are chained on the same workflow line.
            while True:
                command = line.lstrip(" \t;&|")
                match = report.match(command)
                if not match:
                    break
                tail = command[match.end():].lstrip()
                if invokes_compiler_substitution(tail):
                    return True
                line = WindowsCompilerPolicy._following_command_after_report(tail)
            if unused.search(line):
                return True
        return False

    def test_active_workflows_do_not_invoke_gcc_or_desktop_clang(self):
        # Check both supported workflow extensions, not only *.yml.
        workflows = sorted((*WORKFLOWS.glob("*.yml"),
                            *WORKFLOWS.glob("*.yaml")))
        self.assertTrue(workflows)
        for path in workflows:
            with self.subTest(workflow=path.name):
                self.assertFalse(self._uses_unneeded_compiler(
                    path.read_text(encoding="utf-8")),
                    "An active Windows workflow reintroduced GCC or Clang")

    def test_compiler_audit_detects_direct_versioned_and_clang_cl_commands(self):
        for source in (
                "run: clang -c example.cpp",
                "run: clang-cl.exe /c example.cpp",
                "run: clang++-19 example.cpp",
                "run: gcc-14 example.c",
                "run: g++ -std=c++23 example.cpp",
                "run: & clang-cl /std:c++latest example.cpp",
                "run: echo 'gcc was removed' && gcc -c example.c",
                "run: Write-Host 'Clang retired'; & clang-cl.exe /c example.cpp",
                "run: echo 'clang removed' | clang -c example.c",
                "run: echo $(clang --version)",
                "run: echo gcc; gcc -c example.c",
                "run: echo '# this is a note' && gcc-14 example.c",
                "run: Write-Host '# compiled on MSVC' ; & clang-cl.exe /c example.cpp",
                "run: echo '# heading' && clang++ -std=c++23 example.cpp",
                "run: echo 'Clang # retired' && gcc -c example.c",
                'run: Write-Host "# info"; & clang -c example.cpp',
                "run: echo 'gcc retired' $(gcc --version)",
                'run: echo "$(clang --version)"',
                "run: echo 'gcc retired' $(Get-Date); clang-cl.exe /c example.cpp",
                "run: Write-Host 'Don''t use gcc' ; clang --version",
                'run: echo "gcc" && Write-Host "clang retired" && gcc -c x.c',
                'run: echo "gcc" | Write-Host "clang" | clang-cl /c x.cpp',
                'run: Write-Host "gcc" $(Get-Date -Format "clang; g++"); clang-cl /c x.cpp',
                'run: echo "GCC" && Write-Host "$(clang --version)"',
                'run: echo "$(Write-Host \'hello\'; clang --version)"',
                "run: echo $(Get-Date; clang-cl.exe /c example.cpp)",
                "run: echo $(Write-Host 'hello' | & clang -c test.cpp)",
                "run: echo $(Write-Host 'gcc retired'; & clang++-19 -c test.cpp)"):
            with self.subTest(command=source):
                self.assertTrue(self._uses_unneeded_compiler(source))
        for source in (
                "name: Clang compiler was retired",
                "- name: GCC is no longer needed",
                "run: cl /nologo /std:c++latest example.cpp",
                "- scripts/build-clang23-android.ps1",
                "run: python -B -m unittest test_windows_ci_compilers.py",
                "run: cl /c example.cpp # clang was retired",
                "run: Write-Host 'Clang is retired'",
                "run: echo 'gcc was removed'",
                "run: Write-Host 'Clang; gcc are not used anymore' | Out-File logs.txt",
                "run: echo gcc was removed",
                "run: echo 'gcc' && cl /c example.cpp",
                "run: echo '# no compiler here' && cl /c example.cpp",
                "run: echo '# it was clang-cl.exe' # clang retired",
                "run: Write-Host '# clang retired' ; cl /c example.cpp",
                "run: echo 'Build is # MSVC only'",
                "run: cl /c sample.cpp # clang++-19 is retired",
                'run: echo "GCC is retired" $(Get-Date)',
                'run: Write-Host "Clang is retired" $(Get-Date)',
                'run: Write-Host "Clang is retired" ' + chr(96) + 'n',
                "run: echo 'Clang was retired' $(Write-Host 'gcc')",
                "run: echo '$(clang --version)'",
                'run: echo "$(Get-Date) Clang retired"',
                "run: echo gcc retired $(Get-Date)",
                "run: echo $(Get-Date -Format 'gcc')",
                "run: Write-Host 'Don''t use gcc'",
                'run: Write-Host "Clang ' + chr(96) + '"retired' + chr(96) + '" gcc"',
                'run: Write-Host "gcc" -NoNewline "clang retired"',
                'run: echo "clang is retired" "gcc retired"',
                'run: echo "gcc" && Write-Host "clang retired"',
                'run: echo "gcc" | Write-Host "clang" | cl /c x.cpp',
                'run: Write-Host "gcc" $(Get-Date -Format "clang; g++")',
                "run: echo $(Get-Date -Format 'foo; gcc --version')",
                'run: echo "GCC" $(Get-Date -Format "clang; gcc is retired")',
                'run: Write-Host "GCC retired" $(Get-Date -Format "clang; clang-cl reported")',
                'run: echo "$(Write-Host \'gcc; clang is retired\')"',
                'run: echo "$(Write-Host \'hello; clang --version\')"',
                'run: Write-Host "$(Get-Date -Format \'foo; gcc --version\')"'):
            with self.subTest(allowed=source):
                self.assertFalse(self._uses_unneeded_compiler(source))

    def test_nested_powershell_substitutions_distinguish_execution_from_text(self):
        # Compiler calls inside nested PowerShell expressions are executable.
        # Parentheses and compiler names inside quoted reports are not.
        executed = (
            'run: Write-Host "$(Write-Host $(Get-Date); & clang-cl.exe /c example.cpp)"',
            "run: echo $(if (Test-Path sample.cpp) { & clang++-19 -c sample.cpp })",
            'run: echo "$(Write-Host \'gcc retired (report)\'; & clang-cl /c x.cpp)"',
            'run: echo "$(Write-Host $(Get-Date -Format \'gcc; clang\'); gcc -c x.c)"',
        )
        reported = (
            'run: echo "$(Write-Host \'old (gcc; clang --version) message\')"',
            'run: Write-Host "$(Get-Date -Format \'foo (gcc; clang)\')"',
            'run: echo "$(Write-Host $(Get-Date -Format \'gcc; clang\'))"',
            'run: echo "$(Write-Host \'gcc (x)\'; Write-Host \'clang; retired\')"',
        )
        for command in executed:
            with self.subTest(executes=command):
                self.assertTrue(self._uses_unneeded_compiler(command))
        for command in reported:
            with self.subTest(reports=command):
                self.assertFalse(self._uses_unneeded_compiler(command))

    def test_isolated_composition_qa_avoids_unrelated_pinned_windows_builds(self):
        isolated = "native/platform/windows/matching_rename_composition_state_tests.cpp"
        focused = (WORKFLOWS / "codex-compiler-guidance-source.yml").read_text(
            encoding="utf-8")
        self.assertIn("      - " + isolated, focused)
        self.assertIn("LGAED_U5L13_COMPOSITION_FIXTURE_DIR", focused)
        self.assertIn("Execute saved progress reopen and authenticated guidance checks",
                      focused)
        for name in ("windows-state-import.yml", "windows-reader-build.yml",
                     "windows-narration-worker.yml"):
            source = (WORKFLOWS / name).read_text(encoding="utf-8")
            with self.subTest(workflow=name):
                self.assertIn("      - native/platform/windows/**", source)
                self.assertIn("      - '!" + isolated + "'", source)
                self.assertLess(source.index("      - native/platform/windows/**"),
                                source.index("      - '!" + isolated + "'"))
        state_import = (WORKFLOWS / "windows-state-import.yml").read_text(
            encoding="utf-8")
        for script in ("export_u5l13_composition_fixture.py",
                       "test_math_matching_text_compat.py"):
            with self.subTest(script=script):
                excluded = "      - '!scripts/curriculum/" + script + "'"
                self.assertIn(excluded, state_import)
                self.assertLess(state_import.index("      - scripts/curriculum/**"),
                                state_import.index(excluded))
                self.assertIn("      - scripts/curriculum/" + script, focused)

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
                         "test_local_string_live_compilation.py",
                         "test_general_choice_revision_live.py",
                         "test_review_feedback_prose_revision.py"):
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

    def test_u3_diagnostic_authoring_checks_fail_independently(self):
        # The second U3 editorial audit must not mask an earlier rejected
        # contract just because it completes successfully afterward.
        workflow = (WORKFLOWS / "codex-u3-diagnostic-facts.yml").read_text(
            encoding="utf-8")
        first = workflow.index("test_u3_rejection_runtime_contract.py -v")
        second = workflow.index("test_u3_observation_runtime_contract.py -v", first)
        guard = "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }"
        self.assertIn("shell: pwsh", workflow[:first])
        self.assertIn(guard, workflow[first:second])
        self.assertIn(guard, workflow[second:])
        self.assertIn("runs-on: windows-2022", workflow)
        self.assertIn("native/core/rejection_diagnostic_facts_tests.cpp", workflow)

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

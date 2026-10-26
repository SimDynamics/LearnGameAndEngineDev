"""Exercise the MSVC/Ninja header repair using isolated real compiler objects."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == "nt", "MSVC/Ninja regression requires Windows")
class NativeDependenciesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        vs = Path(subprocess.check_output([
            str(vswhere), "-latest", "-version", "[18.0,19.0)", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
        ], text=True).strip())
        cls.cmake = vs / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        cls.ninja = vs / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
        cls.compiler = vs / "VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
        cls.vcvars = vs / "VC/Auxiliary/Build/vcvarsall.bat"
        cls.version = subprocess.check_output([str(cls.cmake), "--version"], text=True).splitlines()[0].split()[-1]
        (REPOSITORY / "out/qa").mkdir(parents=True, exist_ok=True)

    def run_console(self, codepage, arguments, *, success=True, tool_environment=False):
        # A private hidden console reproduces the build button's actual console
        # code page. It never changes the user's console or Windows preferences.
        command = f"chcp {codepage} >nul && " + subprocess.list2cmdline([str(item) for item in arguments])
        if tool_environment:
            command = f'chcp {codepage} >nul && call "{self.vcvars}" x64 10.0.28000.0 -vcvars_ver=14.51.36231 >nul && ' + subprocess.list2cmdline([str(item) for item in arguments])
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        result = subprocess.run(f'"{os.environ["ComSpec"]}" /d /v:off /s /c "{command}"',
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout.decode("utf-8", errors="replace"))
        return result

    def test_repairs_legacy_objects_and_tracks_later_header_changes(self):
        for codepage in (65001, 850):
            with self.subTest(codepage=codepage), tempfile.TemporaryDirectory(
                    prefix="native-dependencies-", dir=REPOSITORY / "out/qa") as temporary:
                root = Path(temporary)
                source = root / "native"
                binary = root / "build"
                objects = binary / "CMakeFiles/fixture.dir"
                source.mkdir()
                objects.mkdir(parents=True)
                header = source / "local.hpp"
                header.write_text("#define FIXTURE_VALUE 7\n", encoding="utf-8")
                (source / "main.cpp").write_text('#include "local.hpp"\nint value = FIXTURE_VALUE;\n', encoding="utf-8")
                (source / "no_headers.cpp").write_text("int standalone;\n", encoding="utf-8")
                (binary / "CMakeCache.txt").write_text(
                    f"CMAKE_HOME_DIRECTORY:INTERNAL={root.as_posix()}\n"
                    f"CMAKE_CACHEFILE_DIR:INTERNAL={binary.as_posix()}\n", encoding="utf-8")
                compiler_info = binary / f"CMakeFiles/{self.version}/CMakeCXXCompiler.cmake"
                compiler_info.parent.mkdir()
                newline = b"\r\n" if codepage == 65001 else b"\n"
                preserved = b"# preserved \x80 \xff" + newline
                compiler_info.write_bytes(b'set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "BAD")' + newline + preserved)
                rules = f'''msvc_deps_prefix = BAD
rule cc
  command = "{self.compiler.as_posix()}" /nologo /utf-8 /showIncludes /c "$in" /Fo"$out"
  deps = msvc
build CMakeFiles/fixture.dir/main.cpp.obj.ddi: cc {source.as_posix().replace(':', '$:')}/main.cpp
build CMakeFiles/fixture.dir/main.cpp.obj: cc {source.as_posix().replace(':', '$:')}/main.cpp
build CMakeFiles/fixture.dir/no_headers.cpp.obj: cc {source.as_posix().replace(':', '$:')}/no_headers.cpp
'''
                (binary / "build.ninja").write_text(rules, encoding="utf-8")
                self.run_console(codepage, [self.ninja, "-C", binary])
                object_file = objects / "main.cpp.obj"
                scan = objects / "main.cpp.obj.ddi"
                plain = objects / "no_headers.cpp.obj"
                original_plain = plain.stat().st_mtime_ns
                original_header = header.stat().st_mtime_ns
                original_source = (source / "main.cpp").read_bytes()
                deps = self.run_console(codepage, [self.ninja, "-C", binary, "-t", "deps"]).stdout
                self.assertIn(b"main.cpp.obj: #deps 0,", deps)
                standalone = [self.cmake,
                              f"-DLGAED_DEPENDENCY_SOURCE_ROOT={root}",
                              f"-DLGAED_DEPENDENCY_BINARY_ROOT={binary}",
                              f"-DLGAED_DEPENDENCY_CXX_COMPILER={self.compiler}",
                              f"-DLGAED_DEPENDENCY_NINJA={self.ninja}",
                              "-P", REPOSITORY / "cmake/VerifyNativeDependencies.cmake"]
                if codepage == 850:
                    old_metadata = compiler_info.read_bytes()
                    old_object = object_file.stat().st_mtime_ns
                    rejected = self.run_console(codepage, standalone, success=False)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertIn(b"non-UTF-8 dependency prefix", rejected.stdout)
                    self.assertEqual(compiler_info.read_bytes(), old_metadata)
                    self.assertEqual(object_file.stat().st_mtime_ns, old_object)
                self.run_console(65001, standalone)
                configure = root / "generate.cmake"
                configure.write_text(f'''
set(CMAKE_BINARY_DIR "{binary.as_posix()}")
# Reopen the migrated cache as an unchanged checkout's configure does.
include("{compiler_info.as_posix()}")
file(READ "${{CMAKE_BINARY_DIR}}/build.ninja" rules)
string(REPLACE "msvc_deps_prefix = BAD" "msvc_deps_prefix = ${{CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}}" rules "${{rules}}")
file(WRITE "${{CMAKE_BINARY_DIR}}/build.ninja" "${{rules}}")
''', encoding="utf-8")
                self.run_console(65001, [self.cmake, "-P", configure])
                self.assertEqual(object_file.stat().st_mtime, 0)
                self.assertEqual(scan.stat().st_mtime, 0)
                self.assertEqual(plain.stat().st_mtime_ns, original_plain)
                self.assertEqual(header.stat().st_mtime_ns, original_header)
                self.assertEqual((source / "main.cpp").read_bytes(), original_source)
                self.assertEqual(compiler_info.read_bytes().split(b")", 1)[1], newline + preserved)
                self.run_console(65001, [self.ninja, "-C", binary])
                deps = self.run_console(65001, [self.ninja, "-C", binary, "-t", "deps"]).stdout
                self.assertIn(b"main.cpp.obj: #deps 1,", deps)
                self.assertIn(b"main.cpp.obj.ddi: #deps 1,", deps)
                self.assertIn(b"local.hpp", deps)
                verify = [Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe",
                          "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", REPOSITORY / "scripts/windows/Repair-NativeDependencies.ps1",
                          "-SourceRoot", root, "-BinaryRoot", binary, "-Ninja", self.ninja, "-VerifyOnly"]
                self.run_console(65001, verify)
                old_object_time = object_file.stat().st_mtime_ns
                os.utime(header, ns=(original_header, old_object_time + 2_000_000_000))
                pending = self.run_console(65001, [self.ninja, "-C", binary, "-n"]).stdout
                self.assertIn(b"main.cpp.obj", pending)
                self.assertIn(b"main.cpp.obj.ddi", pending)

    def test_rejects_mismatched_cache_before_writing_probe(self):
        with tempfile.TemporaryDirectory(prefix="native-dependencies-", dir=REPOSITORY / "out/qa") as temporary:
            root = Path(temporary)
            binary = root / "build"
            binary.mkdir()
            (binary / "CMakeCache.txt").write_text(
                f"CMAKE_HOME_DIRECTORY:INTERNAL={root.as_posix()}/another-source\n"
                f"CMAKE_CACHEFILE_DIR:INTERNAL={binary.as_posix()}\n", encoding="utf-8")
            result = self.run_console(65001, [self.cmake,
                f"-DLGAED_DEPENDENCY_SOURCE_ROOT={root}",
                f"-DLGAED_DEPENDENCY_BINARY_ROOT={binary}",
                f"-DLGAED_DEPENDENCY_CXX_COMPILER={self.compiler}",
                f"-DLGAED_DEPENDENCY_NINJA={self.ninja}",
                "-P", REPOSITORY / "cmake/VerifyNativeDependencies.cmake"], success=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((binary / "CMakeFiles").exists())

    def test_real_cmake_generator_reopens_repaired_compiler_metadata(self):
        for codepage in (65001, 850):
            with self.subTest(codepage=codepage), tempfile.TemporaryDirectory(
                    prefix="native-dependencies-project-", dir=REPOSITORY / "out/qa") as temporary:
                root = Path(temporary)
                source = root / "native"
                binary = root / "build"
                source.mkdir()
                (source / "local.hpp").write_text("#define FIXTURE_VALUE 7\n", encoding="utf-8")
                (source / "main.cpp").write_text('#include "local.hpp"\nint value=FIXTURE_VALUE;\n', encoding="utf-8")
                (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 4.3.1)
project(NativeDependencyFixture LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 20)
add_library(fixture STATIC native/main.cpp)
target_compile_options(fixture PRIVATE /utf-8)
if(FIXTURE_GUARD)
include("''' + (REPOSITORY / "cmake/VerifyNativeDependencies.cmake").as_posix() + '''")
lgaed_verify_native_dependencies()
endif()
''', encoding="utf-8")
                configure = [self.cmake, "-S", root, "-B", binary, "-G", "Ninja",
                             f"-DCMAKE_CXX_COMPILER={self.compiler}", f"-DCMAKE_MAKE_PROGRAM={self.ninja}"]
                self.run_console(codepage, configure, tool_environment=True)
                metadata = binary / f"CMakeFiles/{self.version}/CMakeCXXCompiler.cmake"
                import re
                metadata.write_bytes(re.sub(rb'set\(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "[^\r\n]*"\)',
                                            b'set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "BAD")', metadata.read_bytes()))
                self.run_console(codepage, configure, tool_environment=True)
                self.assertIn(b"msvc_deps_prefix = BAD", (binary / "CMakeFiles/rules.ninja").read_bytes())
                self.run_console(codepage, [self.cmake, "--build", binary], tool_environment=True)
                deps = self.run_console(codepage, [self.ninja, "-C", binary, "-t", "deps"]).stdout
                self.assertIn(b"main.cpp.obj: #deps 0,", deps)
                repair = [self.cmake,
                    f"-DLGAED_DEPENDENCY_SOURCE_ROOT={root}", f"-DLGAED_DEPENDENCY_BINARY_ROOT={binary}",
                    f"-DLGAED_DEPENDENCY_CXX_COMPILER={self.compiler}", f"-DLGAED_DEPENDENCY_NINJA={self.ninja}",
                    "-P", REPOSITORY / "cmake/VerifyNativeDependencies.cmake"]
                if codepage == 850:
                    old_metadata = metadata.read_bytes()
                    rejected = self.run_console(codepage, repair, success=False, tool_environment=True)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertIn(b"non-UTF-8 dependency prefix", rejected.stdout)
                    self.assertEqual(metadata.read_bytes(), old_metadata)
                    rejected = self.run_console(codepage, configure + ["-DFIXTURE_GUARD=ON"], success=False, tool_environment=True)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertIn(b"non-UTF-8 dependency prefix", rejected.stdout)
                self.run_console(65001, repair, tool_environment=True)
                self.run_console(65001, configure + ["-DFIXTURE_GUARD=ON"], tool_environment=True)
                rules = (binary / "CMakeFiles/rules.ninja").read_bytes()
                self.assertNotIn(b"msvc_deps_prefix = BAD", rules)
                self.run_console(65001, [self.cmake, "--build", binary], tool_environment=True)
                deps = self.run_console(65001, [self.ninja, "-C", binary, "-t", "deps"]).stdout
                self.assertIn(b"main.cpp.obj: #deps 1,", deps)


if __name__ == "__main__":
    unittest.main()

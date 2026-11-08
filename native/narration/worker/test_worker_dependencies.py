"""Isolated Windows Maven/JDK contract tests; never start Java or touch models."""
from __future__ import annotations

import hashlib
import io
import inspect
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import build_worker as build


def dependency(artifact, version="1", *, group="test", scope="", extra=""):
    return (f"<dependency><groupId>{group}</groupId><artifactId>{artifact}</artifactId>"
            f"<version>{version}</version><scope>{scope}</scope>{extra}</dependency>")


def pom(*dependencies, properties="", parent="", managed="", extra=""):
    return (f"<project>{parent}<properties>{properties}</properties>"
            f"<dependencyManagement><dependencies>{managed}</dependencies></dependencyManagement>"
            f"<dependencies>{''.join(dependencies)}</dependencies>{extra}</project>")


class Graph(build.MavenResolver):
    def __init__(self, folder, documents):
        super().__init__(folder, {})
        self.documents = documents

    def fetch(self, coordinate, extension="pom", classifier=""):
        path = self.cache / self.relative(coordinate, extension, classifier)
        path.parent.mkdir(parents=True, exist_ok=True)
        content = self.documents[tuple(coordinate)] if extension == "pom" else ":".join(coordinate)
        path.write_text(content, encoding="utf-8")
        return path


class WorkerDependenciesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lgaed-windows-deps-")
        self.folder = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def graph(self, documents, roots):
        return Graph(self.folder, documents).closure(roots)

    def test_nearest_definition_wins_before_deep_first_branch(self):
        documents = {
            ("test", "root", "1"): pom(dependency("deep"), dependency("shared", "2")),
            ("test", "deep", "1"): pom(dependency("middle")),
            ("test", "middle", "1"): pom(dependency("shared", "1")),
            ("test", "shared", "1"): pom(), ("test", "shared", "2"): pom(),
        }
        names = [path.name for path in self.graph(documents, [("test", "root", "1")])]
        self.assertIn("shared-2.jar", names)
        self.assertNotIn("shared-1.jar", names)

    def test_explicit_roots_align_graph_without_duplicate_versions(self):
        documents = {
            ("test", "compiler", "1"): pom(dependency("reflect", "1"), dependency("runtime", "1")),
            ("test", "reflect", "2"): pom(dependency("runtime", "2")),
            ("test", "runtime", "3"): pom(),
        }
        paths = self.graph(documents, [("test", "compiler", "1"), ("test", "reflect", "2"), ("test", "runtime", "3")])
        self.assertEqual([path.name for path in paths], ["compiler-1.jar", "reflect-2.jar", "runtime-3.jar"])

    def test_parent_properties_and_inherited_dependencies_resolve_in_child(self):
        parent = "<parent><groupId>test</groupId><artifactId>parent</artifactId><version>1</version></parent>"
        documents = {
            ("test", "parent", "1"): pom(dependency("shared", "${library.version}"),
                                              properties="<library.version>1</library.version>"),
            ("test", "child", "1"): pom(parent=parent, properties="<library.version>2</library.version>"),
            ("test", "shared", "2"): pom(),
        }
        self.assertEqual([path.name for path in self.graph(documents, [("test", "child", "1")])],
                         ["child-1.jar", "shared-2.jar"])

    def test_bom_management_and_explicit_scope_do_not_import_test_libraries(self):
        documents = {
            ("test", "root", "1"): pom(dependency("runtime", ""), dependency("skip", "", scope="test"),
                managed=dependency("bom", extra="<type>pom</type>").replace("<scope></scope>", "<scope>import</scope>")),
            ("test", "bom", "1"): pom(managed=dependency("runtime", "2", scope="runtime") + dependency("skip", "2")),
            ("test", "runtime", "2"): pom(),
        }
        self.assertEqual([path.name for path in self.graph(documents, [("test", "root", "1")])],
                         ["root-1.jar", "runtime-2.jar"])

    def test_branch_exclusions_do_not_hide_a_separate_runtime_path(self):
        exclusion = "<exclusions><exclusion><groupId>test</groupId><artifactId>leaf</artifactId></exclusion></exclusions>"
        documents = {
            ("test", "root", "1"): pom(dependency("left", extra=exclusion), dependency("right"),
                dependency("optional", extra="<optional>true</optional>"), dependency("provided", scope="provided")),
            ("test", "left", "1"): pom(dependency("common")),
            ("test", "right", "1"): pom(dependency("common")),
            ("test", "common", "1"): pom(dependency("leaf")),
            ("test", "leaf", "1"): pom(),
        }
        names = [path.name for path in self.graph(documents, [("test", "root", "1")])]
        self.assertEqual(names.count("common-1.jar"), 1)
        self.assertIn("leaf-1.jar", names)
        self.assertNotIn("optional-1.jar", names)
        self.assertNotIn("provided-1.jar", names)

    def test_unknown_runtime_metadata_fails_instead_of_guessing(self):
        for extra in ("<distributionManagement><relocation><artifactId>other</artifactId></relocation></distributionManagement>",
                      "<profiles><profile><activation><activeByDefault>true</activeByDefault></activation><dependencies>" + dependency("conditional") + "</dependencies></profile></profiles>"):
            with self.subTest(extra=extra), self.assertRaisesRegex(RuntimeError, "explicit review"):
                self.graph({("test", "root", "1"): pom(extra=extra)}, [("test", "root", "1")])
        with self.assertRaisesRegex(ValueError, "Unsupported Maven"):
            build.MavenResolver.relative(("test", "root", "[1,2)"))
        with self.assertRaisesRegex(RuntimeError, "Unresolved Maven"):
            self.graph({("test", "root", "1"): pom(dependency("unknown", "${absent}"))}, [("test", "root", "1")])

    def test_locked_bytes_download_once_and_corrupt_cache_is_preserved(self):
        relative = build.MavenResolver.relative(("test", "runtime", "1"), "jar")
        payload = b"verified-jar-content"
        resolver = build.MavenResolver(self.folder, {relative: hashlib.sha256(payload).hexdigest()})
        with patch.object(build.urllib.request, "urlopen", return_value=io.BytesIO(payload)) as network:
            path = resolver.fetch(("test", "runtime", "1"), "jar")
            self.assertEqual(network.call_args.args[0], build.MAVEN_CENTRAL + relative)
        with patch.object(build.urllib.request, "urlopen", side_effect=AssertionError("offline")):
            self.assertEqual(resolver.fetch(("test", "runtime", "1"), "jar"), path)
            path.write_bytes(b"damaged-do-not-delete")
            with self.assertRaisesRegex(ValueError, "left untouched"):
                resolver.fetch(("test", "runtime", "1"), "jar")
        self.assertEqual(path.read_bytes(), b"damaged-do-not-delete")

    def test_unlocked_or_bad_download_is_not_installed(self):
        with patch.object(build.urllib.request, "urlopen", side_effect=AssertionError("must fail before network")):
            with self.assertRaisesRegex(RuntimeError, "absent from"):
                build.MavenResolver(self.folder, {}).fetch(("test", "unlocked", "1"))
        resolver = build.MavenResolver(self.folder, None)
        with patch.object(resolver, "published_checksum", return_value=("sha256", "0" * 64)), \
             patch.object(build.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
            with self.assertRaisesRegex(ValueError, "published sha256"):
                resolver.fetch(("test", "bad", "1"))
        self.assertEqual(list(self.folder.rglob("*.pom")), [])
        self.assertEqual(list(self.folder.rglob("*.part")), [])

    def complete_jdk(self):
        home = self.folder / "jdk"
        for name in ("bin/java.exe", "bin/javac.exe", "bin/jar.exe", "include/jni.h",
                     "include/win32/jni_md.h", "bin/server/jvm.dll"):
            path = home / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        (home / "release").write_text('JAVA_VERSION="27.0.1"\nOS_ARCH="amd64"\n', encoding="utf-8")
        return home

    def test_explicit_java_home_requires_complete_x64_jdk_with_supported_api(self):
        home = self.complete_jdk()
        self.assertEqual(build.resolve_java_home(home), home.resolve())
        (home / "include/jni.h").unlink()
        with self.assertRaisesRegex(RuntimeError, "Download JDK"):
            build.resolve_java_home(home)
        home = self.complete_jdk()
        (home / "release").write_text('JAVA_VERSION="21"\nOS_ARCH="amd64"\n', encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "targets JVM"):
            build.resolve_java_home(home)
        (home / "release").write_text('JAVA_VERSION="27"\nOS_ARCH="aarch64"\n', encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "x64 JDK"):
            build.resolve_java_home(home)

    def test_replacing_jdk_or_compiler_in_same_location_invalidates_fingerprint(self):
        home = self.complete_jdk()
        compiler = self.folder / "compiler.jar"
        compiler.write_bytes(b"compiler-release-a")
        before = build.toolchain_fingerprint(home, [compiler])
        compiler.write_bytes(b"compiler-release-b")
        after = build.toolchain_fingerprint(home, [compiler])
        self.assertNotEqual(before, after)
        (home / "bin/server/jvm.dll").write_bytes(b"updated-jvm-same-home")
        self.assertNotEqual(after, build.toolchain_fingerprint(home, [compiler]))

    def native_fixture(self):
        root = self.folder / "checkout"
        manifest = root / "toolchains/windows.versions.json"
        manifest.parent.mkdir(parents=True)
        versions = json.loads((build.ROOT / "toolchains/windows.versions.json").read_text(encoding="utf-8"))
        manifest.write_text(json.dumps(versions), encoding="utf-8")
        native = versions["visualStudio"]
        vs = self.folder / "Visual Studio"
        sdk = self.folder / "Windows SDK"
        batch = vs / "VC/Auxiliary/Build/vcvarsall.bat"
        compiler = vs / f"VC/Tools/MSVC/{native['msvcToolset']}/bin/Hostx64/x64/cl.exe"
        rc = sdk / f"bin/{native['windowsSdk']}/x64/rc.exe"
        source = root / "native/narration/worker_launcher.cpp"
        for path in (batch, compiler, rc, source):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(path.name.encode())
        home = self.complete_jdk()
        ffmpeg = self.folder / "FFmpeg/bin/ffmpeg.exe"
        ffmpeg.parent.mkdir(parents=True)
        ffmpeg.write_bytes(b"fixture-only-not-an-executable")
        payload = {"tools": {"Scope": "Application", "Manifest": str(manifest), "MSVCToolset": native["msvcToolset"],
            "WindowsSdk": native["windowsSdk"], "VsRoot": str(vs), "VcVars": str(batch), "WindowsSdkRoot": str(sdk),
            "JavaHome": str(home), "FFmpeg": str(ffmpeg)},
            "environment": {"Path": "fresh-helper-path", "LGAED_JAVA_HOME": "selected-complete-jdk"}}
        with patch.object(build, "ROOT", root), patch.object(build.subprocess, "check_output", return_value=json.dumps(payload)):
            tools = build.launcher_native_tools()
        return root, payload, tools

    def test_launcher_shares_manifest_selector_without_legacy_vs_paths(self):
        root, payload, expected = self.native_fixture()
        with patch.object(build, "ROOT", root), \
             patch.object(build.subprocess, "check_output", return_value=json.dumps(payload)) as selector:
            tools = build.launcher_native_tools()
            command = selector.call_args.args[0]
            self.assertEqual(command[0], "powershell.exe")
            self.assertIn(str(root / "scripts/windows/Resolve-NativeTools.ps1"), command[-1])
            self.assertIn("-Scope 'Application'", command[-1])
            self.assertEqual(tools["compiler"], expected["compiler"])
            self.assertEqual(tools["cpp"], {"standard": 23, "msvcStandardOption": "/std:c++23preview"})
            self.assertEqual(tools["environment"]["PATH"], "fresh-helper-path")
        payload["tools"]["MSVCToolset"] = "0.0.0"
        with patch.object(build, "ROOT", root), \
             patch.object(build.subprocess, "check_output", return_value=json.dumps(payload)):
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                build.launcher_native_tools()
        source = inspect.getsource(build.launcher)
        self.assertNotIn("vcvars64", source)
        self.assertNotIn("vswhere", source)

    def test_launcher_explicit_locations_use_the_real_selector_and_must_be_retained(self):
        root, payload, _ = self.native_fixture()
        locations = {"cmake": self.folder / "shared ' tools/$cmake.exe",
                     "python": self.folder / "shared Python/python.exe",
                     "git": self.folder / "shared Git/git.exe",
                     "java_home": Path(payload["tools"]["JavaHome"]),
                     "ffmpeg": Path(payload["tools"]["FFmpeg"])}
        fields = {"cmake": ("CMakePath", "CMake"), "python": ("PythonPath", "Python"),
                  "git": ("GitPath", "Git"), "java_home": ("JavaHome", "JavaHome"),
                  "ffmpeg": ("FFmpegPath", "FFmpeg")}
        payload["tools"]["Scope"] = "NarrationHost"
        for name, (_, field) in fields.items():
            payload["tools"][field] = str(locations[name])
        with patch.object(build, "ROOT", root), \
             patch.object(build.subprocess, "check_output", return_value=json.dumps(payload)) as selector:
            tools = build.launcher_native_tools(scope="NarrationHost", **locations)
            script = selector.call_args.args[0][-1]
            self.assertIn("-Scope 'NarrationHost'", script)
            for name, (option, _) in fields.items():
                self.assertIn("-" + option + " '" + str(locations[name]).replace("'", "''") + "'", script)
            self.assertEqual(tools["java_home"], locations["java_home"])
            self.assertEqual(tools["ffmpeg"], locations["ffmpeg"])
        for field in ("CMake", "Python", "Git", "JavaHome", "FFmpeg"):
            changed = {**payload, "tools": {**payload["tools"], field: str(self.folder / "another installation")}}
            with self.subTest(field=field), patch.object(build, "ROOT", root), \
                 patch.object(build.subprocess, "check_output", return_value=json.dumps(changed)):
                with self.assertRaisesRegex(RuntimeError, "did not retain"):
                    build.launcher_native_tools(scope="NarrationHost", **locations)
        with patch.object(build, "ROOT", root), \
             patch.object(build.subprocess, "check_output", return_value=json.dumps(payload)):
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                build.launcher_native_tools()
        with patch.object(build.subprocess, "check_output", side_effect=AssertionError("unknown scope must fail first")):
            with self.assertRaisesRegex(ValueError, "scope"):
                build.launcher_native_tools(scope="SkipValidation")

    def test_build_selects_explicit_tools_before_dependency_downloads_or_compilation(self):
        options = {"cmake": "shared/cmake.exe", "python": "shared/python.exe", "git": "shared/git.exe",
                   "java-home": "shared/jdk", "ffmpeg": "shared/ffmpeg.exe"}
        arguments = ["build_worker.py", "--native-tools-scope", "NarrationHost"]
        for name, value in options.items():
            arguments += ["--" + name, value]
        with patch.object(build.sys, "argv", arguments), patch.object(build, "verify_staged_assets"), \
             patch.object(build, "launcher_native_tools", side_effect=RuntimeError("selector rejected an installation")) as selector, \
             patch.object(build, "worker_dependencies", side_effect=AssertionError("must validate before downloads")), \
             patch.object(build.subprocess, "run", side_effect=AssertionError("must validate before compiling")):
            with self.assertRaisesRegex(RuntimeError, "selector rejected"):
                build.build()
            selector.assert_called_once_with(scope="NarrationHost", **{
                name.replace("-", "_"): Path(value) for name, value in options.items()})

    def test_launcher_reuses_selection_and_rejects_a_different_jdk_for_jni(self):
        root, _, tools = self.native_fixture()
        output = self.folder / "launcher-output"
        output.mkdir()
        with patch.object(build, "ROOT", root), patch.object(build, "OUT", output), \
             patch.object(build, "launcher_native_tools", side_effect=AssertionError("do not select again")), \
             patch.object(build, "launcher_native_environment", return_value=tools["environment"]), \
             patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as compiler:
            build.launcher(tools["java_home"] / "bin/java.exe", tools)
            self.assertEqual(compiler.call_args.kwargs["env"], tools["environment"])
            self.assertIn("/I" + str(tools["java_home"] / "include"), compiler.call_args.args[0])
            with self.assertRaisesRegex(RuntimeError, "JDK validated"):
                build.launcher(tools["java_home"] / "bin/java.exe", {**tools, "java_home": self.folder / "other-jdk"})
            self.assertEqual(compiler.call_count, 1)

    def test_launcher_rejects_missing_or_ambiguous_cpp_mode_before_tool_selection(self):
        root, _, tools = self.native_fixture()
        versions = json.loads(tools["manifest"].read_text(encoding="utf-8"))
        for profile in (None, [], {}, {"standard": 20, "msvcStandardOption": "/std:c++20"},
                        {"standard": 23.0, "msvcStandardOption": "/std:c++23preview"},
                        {"standard": 23, "msvcStandardOption": "/std:c++23"},
                        {"standard": 23, "msvcStandardOption": "/std:c++latest"},
                        {"standard": 23, "msvcStandardOption": "/std:c++23preview /Od"}):
            with self.subTest(profile=profile):
                invalid = dict(versions)
                if profile is None:
                    invalid.pop("cpp", None)
                else:
                    invalid["cpp"] = profile
                tools["manifest"].write_text(json.dumps(invalid), encoding="utf-8")
                with patch.object(build, "ROOT", root), \
                     patch.object(build.subprocess, "check_output", side_effect=AssertionError("invalid mode must fail before tool selection")):
                    with self.assertRaisesRegex(RuntimeError, "manifest C\\+\\+23 profile"):
                        build.launcher_native_tools()

    def test_launcher_fingerprint_separates_cpp_modes_without_changing_source_or_manifest(self):
        _, _, tools = self.native_fixture()
        home = self.complete_jdk()
        source = tools["manifest"].parents[1] / "native/narration/worker_launcher.cpp"
        first = build.launcher_fingerprint(source, home, tools)
        for profile in ({"standard": 20, "msvcStandardOption": "/std:c++23preview"},
                        {"standard": 23, "msvcStandardOption": "/std:c++latest"}):
            with self.subTest(profile=profile):
                changed = {**tools, "cpp": profile}
                self.assertNotEqual(first, build.launcher_fingerprint(source, home, changed))
        self.assertEqual(first, build.launcher_fingerprint(source, home, tools))

    def native_setup_output(self, tools, **overrides):
        values = {"PATH": "pinned-msvc-and-sdk-path", "VCToolsInstallDir": str(tools["compiler"].parents[3]),
                  "WindowsSdkDir": str(tools["sdk_root"]), "WindowsSDKVersion": tools["versions"]["windowsSdk"] + "\\"}
        values.update(overrides)
        return "\n".join(f"{key}={value}" for key, value in values.items())

    def test_launcher_vcvarsall_activates_exact_manifest_sdk_and_toolset(self):
        _, _, tools = self.native_fixture()
        output = self.native_setup_output(tools)
        with patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output)) as setup, \
             patch.object(build.shutil, "which", side_effect=lambda name, **kw: str(tools["compiler"] if name == "cl.exe" else tools["rc"])):
            environment = build.launcher_native_environment(tools)
            command = setup.call_args.args[0]
            self.assertIn('vcvarsall.bat" x64 ' + tools["versions"]["windowsSdk"], command)
            self.assertIn("-vcvars_ver=" + tools["versions"]["msvcToolset"], command)
            self.assertEqual(setup.call_args.kwargs["env"], tools["environment"])
            self.assertEqual(setup.call_args.kwargs["encoding"], "utf-16-le")
            self.assertEqual(environment["PATH"], "pinned-msvc-and-sdk-path")
            self.assertEqual(environment["LGAED_JAVA_HOME"], "selected-complete-jdk")

    def test_launcher_rejects_different_sdk_or_compiler_from_vcvars(self):
        _, _, tools = self.native_fixture()
        for overrides in ({"VCToolsInstallDir": str(self.folder / "old-msvc")}, {"WindowsSDKVersion": "0.0.0\\"},
                          {"WindowsSdkDir": str(self.folder / "old-sdk")}):
            with self.subTest(overrides=overrides), \
                 patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, self.native_setup_output(tools, **overrides))), \
                 patch.object(build.shutil, "which", side_effect=lambda name, **kw: str(tools["compiler"] if name == "cl.exe" else tools["rc"])):
                with self.assertRaisesRegex(RuntimeError, "exact manifest"):
                    build.launcher_native_environment(tools)

    def test_launcher_rebuilds_for_sdk_servicing_and_skips_identical_inputs(self):
        root, _, tools = self.native_fixture()
        home = self.complete_jdk()
        output = self.folder / "launcher-output"
        output.mkdir()
        binary = output / "lgaed_narration_host.exe"

        def fake_compile(*args, **kwargs):
            binary.write_bytes(b"fixture-only-not-an-executable")
            return subprocess.CompletedProcess(args, 0)

        with patch.object(build, "ROOT", root), patch.object(build, "OUT", output), \
             patch.object(build, "launcher_native_tools", return_value=tools), \
             patch.object(build, "launcher_native_environment", return_value={}), \
             patch.object(build.subprocess, "run", side_effect=fake_compile) as compiler:
            self.assertEqual(build.launcher(home / "bin/java.exe"), binary)
            command = compiler.call_args.args[0]
            self.assertEqual([argument for argument in command if argument.startswith("/std:")], ["/std:c++23preview"])
            self.assertIn("/Zc:__cplusplus", command)
            first = (output / "launcher-build.sha256").read_text()
            build.launcher(home / "bin/java.exe")
            self.assertEqual(compiler.call_count, 1)
            tools["rc"].write_bytes(b"new-sdk-servicing-same-directory")
            build.launcher(home / "bin/java.exe")
            self.assertEqual(compiler.call_count, 2)
            second = (output / "launcher-build.sha256").read_text()
            self.assertNotEqual(first, second)
            versions = json.loads(tools["manifest"].read_text())
            versions["visualStudio"]["windowsSdkServicing"] = "next-servicing"
            tools["manifest"].write_text(json.dumps(versions), encoding="utf-8")
            build.launcher(home / "bin/java.exe")
            self.assertEqual(compiler.call_count, 3)
            self.assertNotEqual(second, (output / "launcher-build.sha256").read_text())

    def test_lock_covers_current_roots_and_only_fixed_sha256_entries(self):
        lock = json.loads(build.DEPENDENCY_LOCK.read_text(encoding="utf-8"))
        self.assertEqual(lock["repository"], build.MAVEN_CENTRAL)
        self.assertEqual(lock["roots"], {"runtime": [list(item) for item in build.RUNTIME_ROOTS],
                                         "compiler": [list(item) for item in build.COMPILER_ROOTS]})
        for roots in lock["roots"].values():
            for coordinate in roots:
                for extension in ("pom", "jar"):
                    self.assertIn(build.MavenResolver.relative(coordinate, extension), lock["sha256"])
        for relative, checksum in lock["sha256"].items():
            self.assertNotIn("SNAPSHOT", relative)
            self.assertRegex(checksum, r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()

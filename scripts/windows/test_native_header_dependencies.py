#!/usr/bin/env python3
"""Verify native header dependencies using the configured Windows tools.

All mutations belong to a new, exclusive fixture below --qa-root.
--configured-build only supplies the actual CMake/Ninja/MSVC configuration.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


class ProbeFailure(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProbeFailure(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(paths: list[Path]) -> dict[str, tuple[str, int, int]]:
    return {str(path): (digest(path), path.stat().st_size, path.stat().st_mtime_ns)
            for path in paths}


def cache_entries(path: Path) -> dict[str, tuple[str, str]]:
    result = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^([^#/:][^:]*):([^=]*)=(.*)$", line)
        if match:
            result[match[1]] = (match[2], match[3])
    return result


def cache_values(path: Path) -> dict[str, str]:
    return {key: value for key, (_, value) in cache_entries(path).items()}


def executable(value: str, label: str) -> Path:
    candidate = Path(value)
    if not candidate.is_absolute():
        found = shutil.which(value)
        require(found is not None, f"Cannot resolve configured {label}: {value}")
        candidate = Path(found)
    candidate = candidate.resolve()
    require(candidate.is_file(), f"Configured {label} is not a file: {candidate}")
    return candidate


def same_path(one: Path, two: Path) -> bool:
    return os.path.normcase(str(one.resolve())) == os.path.normcase(str(two.resolve()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--configured-build", required=True, type=Path)
    parser.add_argument("--qa-root", required=True, type=Path)
    args = parser.parse_args()
    require(os.name == "nt", "This probe requires real Windows, MSVC and Ninja; no portable substitute.")

    configured_build = args.configured_build.resolve()
    parent_cache_path = configured_build / "CMakeCache.txt"
    require(parent_cache_path.is_file(), "The supplied native build has not been configured.")
    parent_cache_bytes = parent_cache_path.read_bytes()
    parent = cache_values(parent_cache_path)
    require(parent.get("CMAKE_GENERATOR") == "Ninja", "The configured native build must use Ninja.")
    for key in ("CMAKE_HOME_DIRECTORY", "CMAKE_MAKE_PROGRAM", "CMAKE_CXX_COMPILER", "CMAKE_COMMAND"):
        require(bool(parent.get(key)), f"The configured native cache has no {key}.")
    cmake = executable(args.cmake, "CMake")
    require(same_path(cmake, executable(parent["CMAKE_COMMAND"], "cached CMake")),
            "The requested CMake differs from the already configured native build.")
    ninja = executable(parent["CMAKE_MAKE_PROGRAM"], "Ninja")
    compiler = executable(parent["CMAKE_CXX_COMPILER"], "C++ compiler")
    source_root = Path(parent["CMAKE_HOME_DIRECTORY"]).resolve()
    standard = source_root / "cmake" / "WindowsCxxStandard.cmake"
    policy = source_root / "cmake" / "NativeHeaderDependencies.cmake"
    versions = source_root / "toolchains" / "windows.versions.json"
    require(standard.is_file() and policy.is_file() and versions.is_file(),
            "The configured source tree lacks the production Windows standard/header policy.")
    profile = json.loads(versions.read_text(encoding="utf-8-sig"))
    expected_standard = profile["cpp"]["msvcStandardOption"]
    require(profile["cpp"]["standard"] == 23 and
            re.fullmatch(r"/std:c\+\+23(?:preview)?", expected_standard) is not None,
            "The real Windows language profile must select C++23 explicitly.")
    production_hashes = {str(path): digest(path) for path in (standard, policy, versions)}

    qa_root = args.qa_root.resolve()
    qa_root.mkdir(parents=True, exist_ok=True)
    owned = Path(tempfile.mkdtemp(prefix="native-headers-", dir=qa_root)).resolve()
    source, build, logs, evidence = (owned / name for name in ("src", "build", "logs", "evidence"))
    for directory in (source, logs, evidence):
        directory.mkdir()
    marker = owned / "fixture-owner.txt"
    marker.write_text(str(uuid.uuid4()) + "\n", encoding="utf-8")
    sentinel = marker.read_text(encoding="utf-8").strip()
    print(f"NATIVE_HEADER_QA fixture={owned}", flush=True)

    commands = []
    def run(label: str, command: list[str | Path], *, echo: bool = True,
            timeout: int = 180) -> str:
        argv = [str(value) for value in command]
        print(f"NATIVE_HEADER_QA {label}", flush=True)
        started = time.monotonic()
        try:
            result = subprocess.run(argv, cwd=owned, text=True, encoding="utf-8",
                                    errors="replace", stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            partial = error.stdout or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            (logs / f"{label}.log").write_text(partial, encoding="utf-8")
            raise ProbeFailure(f"{label} exceeded {timeout}s; see {logs / (label + '.log')}") from error
        output = result.stdout
        (logs / f"{label}.log").write_text(output, encoding="utf-8")
        commands.append({"phase": label, "argv": argv, "exit_code": result.returncode,
                         "seconds": round(time.monotonic() - started, 3)})
        if echo or result.returncode:
            print(output, end="" if output.endswith("\n") else "\n", flush=True)
        require(result.returncode == 0, f"{label} failed with exit code {result.returncode}.")
        return output

    try:
        run("cmake-version", [cmake, "--version"])
        run("ninja-version", [ninja, "--version"])
        cmake_source = r"""
cmake_minimum_required(VERSION 4.4.5)
include("${LGAED_PRODUCTION_SOURCE}/cmake/WindowsCxxStandard.cmake")
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX "${LGAED_PRODUCTION_SOURCE}/cmake/WindowsCxxStandard.cmake")
project(NativeHeaderDependencyProbe LANGUAGES CXX)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "This regression must use the actual MSVC compiler.")
endif()
# Source-side phase selection keeps configure arguments and cache unchanged.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/policy-mode.txt" _phase)
string(STRIP "${_phase}" _phase)
if(_phase STREQUAL "ON")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
elseif(_phase STREQUAL "OFF")
    include("${LGAED_PRODUCTION_SOURCE}/cmake/NativeHeaderDependencies.cmake")
else()
    message(FATAL_ERROR "Unknown header dependency QA phase.")
endif()
add_executable(native_header_probe main.cpp unrelated.cpp)
target_compile_options(native_header_probe PRIVATE /Zc:__cplusplus)
message(STATUS "Header QA cache sentinel: ${LGAED_NATIVE_HEADER_QA_SENTINEL}")
"""
        (source / "CMakeLists.txt").write_text(cmake_source.lstrip(), encoding="utf-8")
        mode = source / "policy-mode.txt"
        mode.write_text("ON\n", encoding="utf-8")
        (source / "main.cpp").write_text(
            '#include <expected>\n#include <iostream>\n#include "bridge.hpp"\n'
            'static_assert(__cplusplus == 202302L && _MSVC_LANG == 202302L);\n'
            'int unrelated_value();\n'
            'int main() {\n'
            '    std::expected<int, int> value{visible_value()};\n'
            '    if (unrelated_value() != 23) return 3;\n'
            "    std::cout << value.value() << '\\n';\n"
            '    return 0;\n}\n', encoding="utf-8")
        (source / "unrelated.cpp").write_text(
            'int unrelated_value() { return 23; }\n', encoding="utf-8")
        (source / "bridge.hpp").write_text(
            '#pragma once\n#include "value.hpp"\n'
            'constexpr int visible_value() { return header_value; }\n', encoding="utf-8")
        header = source / "value.hpp"
        header.write_text('#pragma once\ninline constexpr int header_value = 1;\n', encoding="utf-8")
        configure = [cmake, "-S", source, "-B", build, "-G", "Ninja",
                     f"-DCMAKE_MAKE_PROGRAM:FILEPATH={ninja.as_posix()}",
                     f"-DCMAKE_CXX_COMPILER:FILEPATH={compiler.as_posix()}",
                     "-DCMAKE_BUILD_TYPE:STRING=Debug",
                     "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON",
                     f"-DLGAED_PRODUCTION_SOURCE:PATH={source_root.as_posix()}",
                     f"-DLGAED_NATIVE_HEADER_QA_SENTINEL:STRING={sentinel}"]

        def graph() -> str:
            return "\n".join(path.read_text(encoding="utf-8-sig")
                             for path in (build / "build.ninja", build / "CMakeFiles" / "rules.ninja"))

        def objects() -> dict[str, Path]:
            entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8-sig"))
            found = {}
            for entry in entries:
                name = Path(entry["file"]).name
                if name not in ("main.cpp", "unrelated.cpp"):
                    continue
                standards = re.findall(r"/std:([^\s\"']+)", entry["command"])
                require(standards == [expected_standard.removeprefix("/std:")],
                        f"{name} lost the production C++23 option: {standards}")
                output = Path(entry["output"])
                if not output.is_absolute():
                    output = Path(entry["directory"]) / output
                output = output.resolve()
                require(output.is_relative_to(build.resolve()) and output.suffix.lower() == ".obj",
                        f"Unexpected compiler output outside the owned fixture: {output}")
                require(name not in found, f"Duplicate compilation entry for {name}.")
                found[name] = output
            require(set(found) == {"main.cpp", "unrelated.cpp"}, "The real compilation database lost a probe source.")
            return found

        def build_probe(label: str) -> None:
            run(label, [ninja, "-C", build, "-v", "native_header_probe"])

        program = build / "native_header_probe.exe"
        def result(label: str, expected: int) -> None:
            actual = run(label, [program], echo=False).strip()
            require(actual == str(expected), f"{label}: expected program result {expected}, got {actual!r}.")
            print(f"PASS {label}: executable result={actual}", flush=True)

        def no_op(label: str, obj: dict[str, Path]) -> None:
            before = snapshot([*obj.values(), program])
            output = run(label, [ninja, "-C", build, "-v", "native_header_probe"])
            require("no work to do" in output.lower(), f"{label} scheduled work instead of remaining incremental.")
            require(snapshot([*obj.values(), program]) == before, f"{label} changed an object or executable.")

        def object_dependencies(label: str, obj: Path) -> dict[str, object]:
            output = run(label, [ninja, "-C", build, "-t", "deps", obj.relative_to(build)], echo=False)
            entries = re.findall(r"(?m)^\S.*: #deps (\d+), deps mtime (\d+) \(VALID\)$", output)
            require(len(entries) == 1, f"{label} lost its valid object dependency entry: {output.strip()}")
            recorded = set()
            for line in output.splitlines():
                if not line.startswith("    "):
                    continue
                dependency = Path(line.strip())
                if not dependency.is_absolute():
                    dependency = build / dependency
                recorded.add(os.path.normcase(str(dependency.resolve())))
            count, mtime = entries[0]
            require(len(recorded) == int(count), f"{label} could not account for every recorded dependency.")
            return {"mtime": int(mtime), "dependencies": sorted(recorded)}

        def verify_header_deps(label: str, obj: Path) -> dict[str, object]:
            entry = object_dependencies(label, obj)
            for included in (source / "bridge.hpp", header):
                require(os.path.normcase(str(included.resolve())) in entry["dependencies"],
                        f"{label} did not record the real include dependency {included.name}.")
            return entry

        run("configure-scan-on", configure)
        on_graph = graph()
        require(re.search(r"(?m)^\s*dyndep\s*=", on_graph) is not None and
                "cmake_ninja_dyndep" in on_graph, "The seed phase did not really exercise module scanning.")
        (evidence / "scan-on-graph.txt").write_text(on_graph, encoding="utf-8")
        build_probe("build-scan-on")
        result("run-scan-on", 1)
        obj = objects()
        before_dependencies = {
            "main.cpp": verify_header_deps("deps-scan-on", obj["main.cpp"]),
            "unrelated.cpp": object_dependencies("deps-scan-on-unrelated", obj["unrelated.cpp"]),
        }
        cache = build / "CMakeCache.txt"
        deps = build / ".ninja_deps"
        require(deps.is_file(), "The seed build did not create Ninja's real dependency log.")
        preserved = [*obj.values(), program]
        tracked = [cache, *preserved, deps]
        before_reconfigure = snapshot(tracked)
        before_cache = cache_values(cache)
        before_types = {key: kind for key, (kind, _) in cache_entries(cache).items()}
        for phase in ("scan-on", "scan-off"):
            (evidence / phase).mkdir()
        shutil.copyfile(cache, evidence / "scan-on" / "CMakeCache.txt")
        regeneration = {"before": before_reconfigure, "before_dependencies": before_dependencies}
        regeneration_path = evidence / "regeneration.json"
        regeneration_path.write_text(json.dumps(regeneration, indent=2) + "\n", encoding="utf-8")
        mode.write_text("OFF\n", encoding="utf-8")
        run("configure-production-policy", configure)
        after_reconfigure = {str(path): snapshot([path])[str(path)] if path.is_file() else None
                             for path in tracked}
        after_cache = cache_values(cache) if cache.is_file() else {}
        after_types = {key: kind for key, (kind, _) in cache_entries(cache).items()} if cache.is_file() else {}
        if cache.is_file():
            shutil.copyfile(cache, evidence / "scan-off" / "CMakeCache.txt")
        off_graph = graph()
        (evidence / "scan-off-graph.txt").write_text(off_graph, encoding="utf-8")
        changes = {name: {"before": before_reconfigure[name], "after": after_reconfigure[name]}
                   for name in before_reconfigure if before_reconfigure[name] != after_reconfigure[name]}
        cache_changes = {key: {"before": before_cache.get(key), "after": after_cache.get(key)}
                         for key in before_cache.keys() | after_cache.keys()
                         if before_cache.get(key) != after_cache.get(key)}
        type_changes = {key: {"before": before_types.get(key), "after": after_types.get(key)}
                        for key in before_types.keys() | after_types.keys()
                        if before_types.get(key) != after_types.get(key)}
        regeneration.update(after=after_reconfigure, file_changes=changes,
                            cache_value_changes=cache_changes, cache_type_changes=type_changes)
        regeneration_path.write_text(json.dumps(regeneration, indent=2) + "\n", encoding="utf-8")
        print("NATIVE_HEADER_QA regeneration changes: " + json.dumps(changes, sort_keys=True), flush=True)
        altered = [str(path) for path in preserved
                   if after_reconfigure[str(path)] is None or
                   after_reconfigure[str(path)][0] != before_reconfigure[str(path)][0]]
        require(not altered, "Regeneration discarded or altered object/executable bytes: " +
                "; ".join(altered) + f"; hashes and cache deltas: {regeneration_path}")
        require(not cache_changes, f"Regeneration changed cache values: {cache_changes}; see {regeneration_path}")
        # Initial compiler detection stores CACHE STRING; the same explicit
        # -D...:FILEPATH argument may normalize that type on regeneration.
        # Accept only this observed metadata change, with every value intact.
        require(not type_changes or type_changes == {
                    "CMAKE_CXX_COMPILER": {"before": "STRING", "after": "FILEPATH"}},
                f"Regeneration changed an unexpected cache type: {type_changes}; see {regeneration_path}")
        require(cache_values(cache).get("LGAED_NATIVE_HEADER_QA_SENTINEL") == sentinel,
                "Regeneration replaced the configured cache.")
        require(objects() == obj, "The policy transition replaced the configured object paths.")
        # CMake runs `ninja -t recompact` after explicit generation. Retired
        # .ddi entries may disappear from .ninja_deps; live object entries must
        # retain their complete dependency sets and recorded output mtimes.
        require(deps.is_file(), "Regeneration removed the real dependency log.")
        after_dependencies = {
            "main.cpp": verify_header_deps("deps-regenerated", obj["main.cpp"]),
            "unrelated.cpp": object_dependencies("deps-regenerated-unrelated", obj["unrelated.cpp"]),
        }
        regeneration["after_dependencies"] = after_dependencies
        regeneration_path.write_text(json.dumps(regeneration, indent=2) + "\n", encoding="utf-8")
        require(after_dependencies == before_dependencies,
                f"Regeneration changed a live object's dependencies or recorded mtime; see {regeneration_path}")
        # CMake may retain unused SCAN/DYNDEP rule definitions. Check active
        # build edges and the commands reachable from our actual target.
        off_manifest = (build / "build.ninja").read_text(encoding="utf-8-sig")
        require(re.search(r"(?m)^\s*dyndep\s*=", off_manifest) is None and
                re.search(r"(?m)^build .*: CXX_(?:SCAN|DYNDEP)__", off_manifest) is None,
                "The production native policy still has active module scanning/dyndep edges.")
        active_commands = run("commands-production-policy",
                              [ninja, "-C", build, "-t", "commands", "native_header_probe"], echo=False)
        require(re.search(r"(?i)scanDependencies|cmake_ninja_dyndep|\.modmap\b", active_commands) is None,
                "The production target still invokes module scanning or dyndep commands.")
        print("PASS ON-to-OFF regeneration: all cache values retained; cache types unchanged except compiler normalization; "
              "object/executable bytes and live object dependencies retained; "
              "C++23 retained; no active dyndep.", flush=True)

        # Removing @modmap legitimately changes compile commands. Objects may
        # rebuild once here; only the subsequent invocation must be a no-op.
        build_probe("build-production-policy")
        result("run-production-policy", 1)
        verify_header_deps("deps-production-policy", obj["main.cpp"])
        no_op("no-op-production-policy", obj)

        before_header = snapshot(list(obj.values()))
        # Separate writes even on file systems with coarse timestamp resolution.
        time.sleep(1.1)
        header.write_text('#pragma once\ninline constexpr int header_value = 2;\n', encoding="utf-8")
        build_probe("build-header-change")
        require(snapshot([obj["unrelated.cpp"]])[str(obj["unrelated.cpp"])] ==
                before_header[str(obj["unrelated.cpp"])],
                "Changing a transitively included header rebuilt an unrelated source.")
        require(digest(obj["main.cpp"]) != before_header[str(obj["main.cpp"])][0],
                "Changing a transitively included header did not update its consumer object.")
        result("run-header-change", 2)
        verify_header_deps("deps-header-change", obj["main.cpp"])
        no_op("no-op-header-change", obj)
        print("PASS transitive header dependency: result 1->2; consumer rebuilt; unrelated object untouched.", flush=True)

        # Exercise missing dependency entries only AFTER proving scan OFF.
        # The real configured build and its .ninja_deps are never modified.
        require(deps.resolve().is_relative_to(owned) and marker.is_file(),
                "Refusing dependency-log loss outside the exclusive QA fixture.")
        before_loss = snapshot(list(obj.values()))
        deps.rename(evidence / "ninja-deps-before-loss.bin")
        build_probe("build-missing-deps")
        require(deps.is_file(), "Ninja did not recreate its missing QA dependency log.")
        for name, path in obj.items():
            require(path.stat().st_mtime_ns != before_loss[str(path)][2],
                    f"Ninja skipped {name} despite its missing dependency entry.")
        result("run-missing-deps", 2)
        verify_header_deps("deps-recovered", obj["main.cpp"])
        no_op("no-op-recovered", obj)
        require(parent_cache_path.read_bytes() == parent_cache_bytes,
                "The probe changed the real native build's CMakeCache.")
        require(all(digest(Path(path)) == sha for path, sha in production_hashes.items()),
                "The probe changed a production policy or toolchain manifest.")
        report = {"result": "PASS", "platform": sys.platform, "fixture": str(owned),
                  "configured_build": str(configured_build), "cmake": str(cmake),
                  "ninja": str(ninja), "compiler": str(compiler),
                  "standard_option": expected_standard, "production_sha256": production_hashes,
                  "checks": ["scan ON seed", "same-tree OFF regeneration preserves all cache values and object/executable bytes",
                             "cache types unchanged except CMAKE_CXX_COMPILER STRING-to-FILEPATH normalization",
                             "live object dependencies preserved across metadata compaction",
                             "no active module dyndep; real MSVC include deps", "C++23 real compilation",
                             "executable/no-op", "transitive header update; unrelated unchanged",
                             "QA-only missing deps recovery/executable/no-op"],
                  "commands": commands}
        (owned / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("PASS native header dependency policy: real Windows CMake/MSVC/Ninja, incremental C++23 and missing-deps recovery.", flush=True)
        return 0
    except Exception as error:
        (owned / "result.json").write_text(json.dumps(
            {"result": "FAIL", "fixture": str(owned), "error": str(error), "commands": commands},
            indent=2) + "\n", encoding="utf-8")
        print(f"FAIL native header dependency policy: {error}; fixture={owned}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ProbeFailure as error:
        print(f"FAIL native header dependency setup: {error}", file=sys.stderr, flush=True)
        raise SystemExit(1)

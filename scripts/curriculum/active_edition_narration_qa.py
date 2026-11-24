"""Run one real three-voice narration regression on disposable Windows CI.

The production sources, dependency lock and JNI launcher are used unchanged.
Hosted MSVC is an explicitly separate QA compiler; no product toolchain pin,
installed runtime, source asset or learner state is rewritten. Only a new QA
directory receives resources, the worker, WAV files and their evidence.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
from importlib import util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import traceback
import unittest

ROOT = Path(__file__).resolve().parents[2]
MAIN_CLASS = "lgaed.narration.windows.WindowsNarrationWorkerKt"
TEST_NAME = ("SpanishProseBoundariesTest."
             "test_05_current_output_prompt_colon_and_conjunction_pcm_context_and_cache")
VOICES = ("ef_dora", "em_alex", "em_santa")


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as target:
        json.dump(value, target, ensure_ascii=False, indent=2)
        target.write("\n")


def load_module(name: str, path: Path):
    if name in sys.modules:
        raise RuntimeError(f"A QA module is already loaded: {name}")
    spec = util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load the original QA module: {path}")
    module = util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def physical(path: Path) -> Path:
    path = path.absolute()
    for component in (path, *path.parents):
        if component.is_symlink() or getattr(component, "is_junction", lambda: False)():
            raise ValueError(f"QA paths cannot contain links or junctions: {component}")
    return path.resolve()


def prepare_destination(destination: Path) -> Path:
    if (sys.platform != "win32" or os.environ.get("GITHUB_ACTIONS") != "true"
            or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted"):
        raise RuntimeError("Run this probe only on disposable GitHub-hosted Windows.")
    runner_temp = os.environ.get("RUNNER_TEMP")
    if not runner_temp:
        raise RuntimeError("The hosted runner has no declared temporary directory.")
    destination, temporary = physical(destination), physical(Path(runner_temp))
    if destination == temporary or not destination.is_relative_to(temporary):
        raise ValueError("Create this isolated narration probe below RUNNER_TEMP.")
    if destination.exists():
        raise FileExistsError("Use a new narration QA directory; existing files are preserved.")
    destination.mkdir(parents=True, exist_ok=False)
    return destination


def compile_original_launcher(destination: Path, java_home: Path, ffmpeg: Path) -> Path:
    compiler_name = shutil.which("cl.exe")
    compiler_root = os.environ.get("VCToolsInstallDir")
    if not compiler_name or not compiler_root:
        raise RuntimeError("Activate the hosted MSVC x64 environment before this probe.")
    compiler = Path(compiler_name).resolve()
    expected = Path(compiler_root) / "bin/Hostx64/x64/cl.exe"
    if os.path.normcase(str(compiler)) != os.path.normcase(str(expected.resolve())):
        raise RuntimeError("The active cl.exe does not belong to the activated x64 toolset.")
    probe = subprocess.run([str(compiler), "/Bv"], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=30)
    banner = probe.stdout + probe.stderr
    version = re.search(r"\bVersion\s+(19\.52\.\d+(?:\.\d+)?)\s+for\s+x64\b", banner)
    if not version:
        raise RuntimeError("This isolated probe requires hosted MSVC 19.52 x64: " + banner)
    sdk = os.environ.get("WindowsSDKVersion", "").rstrip("\\/")
    sdk_root = os.environ.get("WindowsSdkDir", "")
    if (not re.fullmatch(r"\d+(?:\.\d+){3}", sdk) or not sdk_root
            or not (Path(sdk_root) / "Include" / sdk / "um/Windows.h").is_file()):
        raise RuntimeError("The activated hosted Windows SDK is incomplete.")
    release = (java_home / "release").read_text(encoding="utf-8")
    if not re.search(r'^JAVA_VERSION="27(?:\.|\+|\")', release, re.MULTILINE):
        raise RuntimeError("Use the authenticated OpenJDK 27 prepared by Prepare-NarrationCITools.ps1.")
    ffmpeg_probe = subprocess.run([str(ffmpeg), "-version"], capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", timeout=30)
    if ffmpeg_probe.returncode or not re.search(r"(?m)^ffmpeg version 9\.0\.2(?:-|\s)", ffmpeg_probe.stdout):
        raise RuntimeError("Use the authenticated FFmpeg 9.0.2 archive for this probe.")
    native = destination / "native"
    native.mkdir()
    launcher = native / "lgaed_narration_host.exe"
    source = ROOT / "native/narration/worker_launcher.cpp"
    command = [str(compiler), "/nologo", "/std:c++23preview", "/Zc:__cplusplus",
               "/EHsc", "/W4", "/utf-8", "/MD", f"/I{java_home / 'include'}",
               f"/I{java_home / 'include/win32'}", str(source),
               f"/Fo{native / 'worker_launcher.obj'}", f"/Fe{launcher}"]
    compiled = subprocess.run(command, cwd=native, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=120)
    with (destination / "compiler.log").open("x", encoding="utf-8") as log:
        log.write(banner + "\n" + subprocess.list2cmdline(command) + "\n")
        log.write(compiled.stdout + compiled.stderr)
    print(compiled.stdout + compiled.stderr, end="", flush=True)
    write_json(destination / "compiler-evidence.json", {
        "kind": "hosted-msvc-original-jni-launcher", "compiler": str(compiler),
        "compilerVersion": version[1], "compilerSha256": digest(compiler),
        "windowsSdk": sdk, "windowsSdkRoot": sdk_root,
        "command": command, "returncode": compiled.returncode,
        "source": str(source), "sourceSha256": digest(source),
        "launcherSha256": digest(launcher) if launcher.is_file() else None,
        "javaRelease": release, "javaReleaseSha256": digest(java_home / "release"),
        "ffmpeg": str(ffmpeg), "ffmpegSha256": digest(ffmpeg),
        "ffmpegVersion": ffmpeg_probe.stdout.splitlines()[0],
        "productToolchainSha256": digest(ROOT / "toolchains/windows.versions.json"),
    })
    compiled.check_returncode()
    if not launcher.is_file():
        raise RuntimeError("MSVC did not produce the original JNI launcher.")
    return launcher


def prepare_resources(destination: Path):
    helper = load_module("lgaed_active_narration_resources",
                         ROOT / "scripts/windows/prepare_narration_ci_resources.py")
    resources, english = helper.destinations(destination / "resources/kokoro",
                                             destination / "resources/english")
    artifacts = helper.spanish_contract()
    dora = next(artifact for artifact in artifacts if artifact.name == "ef_dora.bin")
    catalog_path = ROOT / "app/src/main/java/com/learngraphicsprogramming/app/narration/KokoroVoiceCatalog.kt"
    catalog = catalog_path.read_text(encoding="utf-8-sig")
    for voice in VOICES[1:]:
        hashes = re.findall(r'\bKokoroVoice\("' + re.escape(voice)
                            + r'",\s*NarrationLanguage\.SPANISH,\s*"([0-9a-f]{64})"\)', catalog)
        if len(hashes) != 1:
            raise ValueError(f"The original Spanish voice contract changed: {voice}")
        artifact = helper.Artifact(voice + ".bin", dora.size, hashes[0],
                                   dora.url.rsplit("/", 1)[0] + "/" + voice + ".bin")
        helper._pinned_url(artifact.url)
        artifacts.append(artifact)
    helper.stage_english(english)
    # The shared function validates the supplied inventory. Its CLI main()
    # fixes the six-file Dora pack, so this probe deliberately uses the same
    # authenticated primitives with the complete eight-file Spanish pack.
    helper.prepare_spanish(resources, artifacts)
    if helper._existing_files(resources) != {artifact.name for artifact in artifacts}:
        raise ValueError("The isolated Spanish pack has unexpected or missing files.")
    for artifact in artifacts:
        helper.english_pack.verify_file(resources / artifact.name, artifact)
    pack = helper.english_pack.verify_staged_assets(english)
    evidence = {
        "kind": "authenticated-three-spanish-voice-resources", "voices": list(VOICES),
        "catalogSha256": digest(catalog_path),
        "spanish": [{"name": a.name, "bytes": a.size, "sha256": a.sha256, "url": a.url}
                    for a in artifacts],
        "spanishBytes": sum(a.size for a in artifacts),
        "englishArtifactCount": pack.artifact_count,
        "englishArtifactBytes": pack.total_artifact_bytes,
        "englishMetadataFileCount": pack.metadata_file_count,
    }
    write_json(destination / "resources.json", evidence)
    return resources, english


def prepare_runtime(destination: Path, java_home: Path, ffmpeg: Path, launcher: Path) -> Path:
    builder = load_module("lgaed_active_narration_worker_builder",
                          ROOT / "scripts/curriculum/edition_source_worker_runtime.py")
    generated = builder.prepare(java_home, destination / "worker")
    original = json.loads(generated.read_text(encoding="utf-8"))
    arguments = original["arguments"]
    if arguments[:1] != ["-cp"] or arguments[2] != MAIN_CLASS:
        raise ValueError("The original worker builder changed its launch contract.")
    resources, english = prepare_resources(destination)
    classpath, jvm = arguments[1], java_home / "bin/server/jvm.dll"
    assets = ROOT / "app/src/main/assets"
    original_wavs = destination / "historical-wavs-not-installed"
    profile = "Work" if ROOT.name.endswith("-Work") else "Codex"
    config = {
        "java": str(launcher), "javaExecutable": original["java"], "jvm": str(jvm),
        "classpath": classpath, "mainClass": MAIN_CLASS,
        "arguments": [str(jvm), classpath, MAIN_CLASS,
                      "--resources", str(resources), "--english-resources", str(english),
                      "--assets", str(assets), "--original-wavs", str(original_wavs),
                      "--profile", profile, "--ffmpeg", str(ffmpeg)],
        "assets": str(assets), "resources": str(resources), "englishResources": str(english),
        "ffmpeg": str(ffmpeg), "originalWavs": str(original_wavs),
    }
    # The worker consumes these exact launch arguments, not the optional UI
    # voice catalogue in a product runtime. run_one_test verifies all three
    # original Spanish voices and verify_runtime does not require that field.
    runtime = destination / "runtime.json"
    write_json(runtime, config)
    # builder.prepare imports build_worker, which adds scripts/narration to
    # sys.path. Reuse its unchanged verifier: English provenance, retained Opus
    # and the actual runtime paths must still satisfy their full contract.
    from verify_windows_distribution import verify_runtime
    verify_runtime(runtime, repo_root=ROOT)
    return runtime


class Tee:
    def __init__(self, *streams):
        self.streams = streams

    def write(self, text):
        for stream in self.streams:
            stream.write(text)
            stream.flush()
        return len(text)

    def flush(self):
        for stream in self.streams:
            stream.flush()


def run_one_test(destination: Path, runtime: Path) -> bool:
    worker = load_module("test_worker", ROOT / "native/narration/worker/test_worker.py")
    worker.RUNTIME = runtime
    worker.CACHE = destination / "cache"
    test = load_module("lgaed_active_narration_test",
                       ROOT / "native/narration/worker/test_spanish_prose_boundaries.py")
    test.OUT = destination / "samples"
    if tuple(test.VOICES) != VOICES:
        raise ValueError("This probe must exercise all three original Spanish voices.")
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromName(TEST_NAME, test)
    if loader.errors or suite.countTestCases() != 1:
        raise ValueError("The exact focal narration test is unavailable: " + str(loader.errors))
    started = time.monotonic()
    with (destination / "test.log").open("x", encoding="utf-8", newline="\n") as log:
        output = Tee(sys.stdout, log)
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
    final_worker = getattr(test.SpanishProseBoundariesTest, "worker", None)
    with (destination / "worker-final-stderr.log").open("x", encoding="utf-8") as log:
        log.write("Final worker instance, after the test's real process reopen.\n")
        if final_worker is not None:
            log.write("\n".join(final_worker.errors) + "\n")
    success = (result.wasSuccessful() and result.testsRun == 1 and not result.skipped
               and not result.expectedFailures and not result.unexpectedSuccesses)
    recordings = sorted((destination / "samples").rglob("recordings.json"))
    if success and len(recordings) != 1:
        success = False
    write_json(destination / "test-results.json", {
        "test": TEST_NAME, "testsRun": result.testsRun, "success": success,
        "elapsedSeconds": time.monotonic() - started,
        "failures": [{"test": str(case), "traceback": trace} for case, trace in result.failures],
        "errors": [{"test": str(case), "traceback": trace} for case, trace in result.errors],
        "skipped": [{"test": str(case), "reason": reason} for case, reason in result.skipped],
        "recordings": [{"path": str(path.relative_to(destination)), "sha256": digest(path)}
                       for path in recordings],
        "claim": "Real PCM, timings, contexts and cache/reopen; human listening is separate.",
    })
    return success


def run(java_home: Path, ffmpeg: Path, output_dir: Path) -> int:
    destination = prepare_destination(output_dir)
    protected = [ROOT / name for name in (
        "toolchains/windows.versions.json", "native/narration/worker_launcher.cpp",
        "native/narration/worker/build_worker.py", "native/narration/worker/dependencies.lock.json",
        "native/narration/worker/test_worker.py", "native/narration/worker/test_spanish_prose_boundaries.py",
        "out/narration/runtime.json")]
    before = {str(path): digest(path) if path.exists() else None for path in protected}
    success = False
    runtime = None
    try:
        java_home, ffmpeg = physical(java_home), physical(ffmpeg)
        launcher = compile_original_launcher(destination, java_home, ffmpeg)
        runtime = prepare_runtime(destination, java_home, ffmpeg, launcher)
        success = run_one_test(destination, runtime)
    except Exception:
        failure = traceback.format_exc()
        write_json(destination / "failure.json", {"traceback": failure})
        print(failure, file=sys.stderr)
    finally:
        after = {str(path): digest(path) if path.exists() else None for path in protected}
        preserved = before == after
        success = success and preserved
        write_json(destination / "narration-qa-evidence.json", {
            "kind": "isolated-active-edition-three-voice-narration", "test": TEST_NAME,
            "success": success, "protectedFilesPreserved": preserved,
            "protectedBefore": before, "protectedAfter": after,
            "runtime": str(runtime) if runtime is not None else None,
            "runtimeSha256": digest(runtime) if runtime is not None else None,
            "workerBuildEvidence": "worker/build-evidence.json",
            "sampleDirectory": "samples", "voices": list(VOICES),
            "productToolchainAndRuntimeUnchanged": preserved,
        })
    return 0 if success else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    return run(args.java_home, args.ffmpeg, args.output_dir)


if __name__ == "__main__":
    raise SystemExit(main())

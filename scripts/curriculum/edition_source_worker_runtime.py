"""Build the real Windows math worker for isolated edition-copy QA.

This test runtime uses the production Kotlin sources, adaptations and dependency
lock through build_worker. It starts the real worker with java.exe; it neither
builds the application's JNI launcher nor installs speech models. The native
NarrationWorker still owns its ordinary pipe protocol and math evaluation.
"""
from __future__ import annotations

import argparse
from importlib import util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def prepare(java_home: Path, destination: Path) -> Path:
    if sys.platform != "win32":
        raise RuntimeError("The edition-source math runtime requires native Windows.")
    for component in (destination, *destination.parents):
        junction = getattr(component, "is_junction", None)
        if component.is_symlink() or (junction is not None and junction()):
            raise ValueError("A QA runtime destination cannot use an alias.")
    destination = destination.resolve()
    if destination.exists():
        raise FileExistsError("Use a new isolated QA worker directory; existing data is preserved.")
    module_path = ROOT / "native/narration/worker/build_worker.py"
    spec = util.spec_from_file_location("lgaed_edition_source_worker_build", module_path)
    worker = util.module_from_spec(spec)
    spec.loader.exec_module(worker)
    home = worker.resolve_java_home(java_home)
    java = home / "bin/java.exe"
    destination.mkdir(parents=True)
    # Reuse the exact Windows generator, with only its output directory scoped
    # to this test. The source tree and shipped dependency lock stay unchanged.
    worker.OUT = destination / "generated-worker"
    worker.DEPS = destination / "dependencies"
    with worker.build_lock():
        dependencies = worker.worker_dependencies()
        runtime, compiler = dependencies["runtime"], dependencies["compiler"]
        sources = worker.host_sources()
        java_sources = sorted((ROOT / "native/narration/worker/androidjson").glob("*.java"))
        if not sources or not java_sources:
            raise RuntimeError("The real worker source inventory is empty.")
        classes = destination / "android-json-classes"
        classes.mkdir()
        subprocess.run([str(home / "bin/javac.exe"), "-encoding", "UTF-8",
                        "--release", worker.JVM_TARGET, "-d", str(classes),
                        *map(str, java_sources)], check=True, cwd=ROOT, timeout=120)
        jar = destination / "edition-source-worker.jar"
        subprocess.run([str(java), "-cp", os.pathsep.join(map(str, compiler)),
                        "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                        "-Xjdk-release=" + worker.JVM_TARGET,
                        "-classpath", os.pathsep.join(map(str, [*runtime, classes])),
                        "-d", str(jar), *map(str, sources)],
                       check=True, cwd=ROOT, timeout=300)
        subprocess.run([str(home / "bin/jar.exe"), "uf", str(jar), "-C", str(classes), "."],
                       check=True, cwd=ROOT, timeout=120)
        classpath = os.pathsep.join(map(str, [jar, *runtime]))
        missing_resources = destination / "speech-models-not-installed"
        config = {
            "java": str(java),
            "arguments": ["-cp", classpath,
                          "lgaed.narration.windows.WindowsNarrationWorkerKt",
                          "--resources", str(missing_resources),
                          "--english-resources", str(missing_resources),
                          "--assets", str(ROOT / "app/src/main/assets"),
                          "--profile", "Codex"],
        }
        output = destination / "runtime.json"
        output.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8", newline="\n")
        evidence = {
            "kind": "edition-source-real-math-worker", "jvmTarget": worker.JVM_TARGET,
            "javaRelease": (home / "release").read_text(encoding="utf-8"),
            "workerJarSha256": worker.digest(jar),
            "dependencyLockSha256": worker.digest(worker.DEPENDENCY_LOCK),
            "sources": {str(path): worker.digest(path) for path in [*sources, *java_sources]},
            "dependencies": {str(path): worker.digest(path) for path in [*runtime, *compiler]},
        }
        (destination / "build-evidence.json").write_text(
            json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Prepared the real Windows math worker without speech models: " + str(output))
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.java_home, args.output_dir)


if __name__ == "__main__":
    main()

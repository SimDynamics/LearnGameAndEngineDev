"""Compile/run the controlled calculator Future lifecycle fixture against production code."""
from __future__ import annotations

import json
import os
import subprocess

from build_worker import ROOT, OUT, JVM_TARGET, build_lock, worker_dependencies, generated


def main():
    runtime = json.loads((OUT / "runtime.json").read_text(encoding="utf-8"))
    java_home = (OUT / "jdk-root.txt").read_text(encoding="utf-8")
    from pathlib import Path
    java = Path(java_home) / "bin/java.exe"
    compiler = worker_dependencies()["compiler"]
    source = generated("WindowsCalculatorTaskTest.kt", (ROOT / "native/narration/worker/test_calculator_task.kt.in").read_text(encoding="utf-8"))
    jar = OUT / "calculator-task-tests.jar"
    subprocess.run([str(java), "-cp", os.pathsep.join(map(str, compiler)), "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler",
                    "-no-stdlib", "-no-reflect", f"-Xjdk-release={JVM_TARGET}", "-classpath", runtime["arguments"][1],
                    "-Xfriend-paths=" + str(OUT / "kokoro-worker.jar"), "-d", str(jar), str(source)], check=True, cwd=ROOT)
    subprocess.run([str(java), "-cp", str(jar) + os.pathsep + runtime["arguments"][1],
                    "lgaed.narration.windows.WindowsCalculatorTaskTestKt"], check=True, cwd=ROOT, timeout=20)


if __name__ == "__main__":
    with build_lock():
        main()

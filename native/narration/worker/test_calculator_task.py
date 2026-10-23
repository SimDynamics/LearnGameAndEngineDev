"""Compile/run the controlled calculator Future lifecycle fixture against production code."""
from __future__ import annotations

import json
import os
import subprocess

from build_worker import ROOT, OUT, build_lock, cached_jar, generated


def main():
    runtime = json.loads((OUT / "runtime.json").read_text(encoding="utf-8"))
    java_home = (OUT / "jdk-root.txt").read_text(encoding="utf-8")
    from pathlib import Path
    java = Path(java_home) / "bin/java.exe"
    compiler = [cached_jar("org.jetbrains.kotlin", "kotlin-compiler-embeddable", "2.2.10"),
                cached_jar("org.jetbrains.kotlin", "kotlin-stdlib", "2.2.10"),
                cached_jar("org.jetbrains.kotlin", "kotlin-script-runtime", "2.2.10"),
                cached_jar("org.jetbrains.kotlin", "kotlin-reflect", "1.6.10"),
                cached_jar("org.jetbrains.kotlinx", "kotlinx-coroutines-core-jvm", "1.8.0"),
                cached_jar("org.jetbrains", "annotations", "23.0.0")]
    source = generated("WindowsCalculatorTaskTest.kt", (ROOT / "native/narration/worker/test_calculator_task.kt.in").read_text(encoding="utf-8"))
    jar = OUT / "calculator-task-tests.jar"
    subprocess.run([str(java), "-cp", os.pathsep.join(map(str, compiler)), "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler",
                    "-no-stdlib", "-no-reflect", "-jvm-target", "17", "-classpath", runtime["arguments"][1],
                    "-Xfriend-paths=" + str(OUT / "kokoro-worker.jar"), "-d", str(jar), str(source)], check=True, cwd=ROOT)
    subprocess.run([str(java), "-cp", str(jar) + os.pathsep + runtime["arguments"][1],
                    "lgaed.narration.windows.WindowsCalculatorTaskTestKt"], check=True, cwd=ROOT, timeout=20)


if __name__ == "__main__":
    with build_lock():
        main()

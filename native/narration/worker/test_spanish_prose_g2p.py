"""Compile the real Windows Spanish frontend through G2P and phone ownership.

The default uses the project-selected JDK and JVM target. An explicit Java/target
override can run this text-only regression on another host; it does not verify
Windows playback, Kokoro PCM, model durations, or human pronunciation.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "native/narration/worker"


def historical_cases() -> list[dict]:
    """Read the same authenticated Git publications as the native reader tests."""
    sys.path.insert(0, str(ROOT / "scripts/curriculum"))
    import compiler_import_fixture_source as compiler
    import u11_remainder_editorial_fixture_source as remainder

    cases = []
    for stage in compiler.COMMITS:
        lesson = json.loads(compiler.published_bytes("u12l20", stage))
        block = lesson["pages"][5]["blocks"][2]
        if block.get("type") != "paragraph":
            raise AssertionError("The authentic U12L20 p6b3 source changed")
        cases.append(dict(label=f"u12l20/{stage}/p6b3", source=block["text"], mode="auto"))
    for stage in remainder.COMMITS:
        lesson = json.loads(remainder.published_bytes(stage))
        block = lesson["pages"][2]["blocks"][2]
        if block.get("type") != "order_code" or "resultado_en_dominio" not in block["question"]:
            raise AssertionError("The authentic U11L16 p3b3 question changed")
        cases.append(dict(label=f"u11l16/{stage}/p3b3/question", source=block["question"], mode="cpp"))
    lesson = json.loads(compiler.published_bytes("u11l19", "before"))
    block = lesson["pages"][4]["blocks"][4]
    if block.get("type") != "paragraph" or "duplicados/ausencia/mala" not in block["text"]:
        raise AssertionError("The authentic U11L19 p5b5 criterion changed")
    cases.append(dict(label="u11l19/before/p5b5", source=block["text"], mode="auto",
                      phoneSentence=1,
                      # Captured from the unchanged production frontend before
                      # the Windows trace fix, using the same empty lexicon.
                      phoneSha256="2e3b3ffe8b5e2d698cc7c5b072faf1fd7dfb588d772b67df101281f9d9e965d2"))
    return cases


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java", help="Explicit Java executable; otherwise use the project-selected full JDK")
    parser.add_argument("--jvm-target", help="Text-only test override; defaults to the production worker target")
    parser.add_argument("--dependency-cache", type=Path, help="Reuse authenticated compiler JARs from this directory")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("spanish_prose_build", WORKER / "build_worker.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if args.java:
        java = args.java
    else:
        tools = module.launcher_native_tools()
        java = str(module.resolve_java_home(Path(tools["environment"]["JAVA_HOME"])) / "bin/java.exe")
    target = args.jvm_target or module.JVM_TARGET
    # This frontend has no ONNX/model dependency. Reuse the production compiler,
    # versions and lock, while resolving only its actual pure-JVM dependencies.
    lock = json.loads(module.DEPENDENCY_LOCK.read_text(encoding="utf-8"))
    resolver = module.MavenResolver(args.dependency_cache or module.DEPS, lock["sha256"])
    compiler = resolver.closure(module.COMPILER_ROOTS)
    roots = [coordinate for coordinate in module.RUNTIME_ROOTS
             if coordinate[:2] in {("org.jetbrains.kotlin", "kotlin-stdlib"),
                                   ("org.json", "json"), ("org.jetbrains", "annotations")}]
    if len(roots) != 3:
        raise RuntimeError("The original frontend runtime dependencies changed")
    runtime = os.pathsep.join(str(resolver.fetch(coordinate, "jar")) for coordinate in roots)
    cases = historical_cases()
    with tempfile.TemporaryDirectory(prefix="lgaed-spanish-prose-g2p-") as temporary:
        folder = Path(temporary)
        module.OUT = folder / "host"
        names = {
            "SpanishTechnicalSpeech.kt", "SpanishMathSpeech.kt", "SpanishFluidG2P.kt",
            "ManualEnglishTerms.kt", "ManualEnglishSpeech.kt", "ManualEnglishPronunciations.kt",
            "MixedSpanishG2P.kt", "NarrationSpokenWords.kt", "NarrationSegments.kt",
            "EnglishTechnicalSpeech.kt", "FrenchSpeechSymbols.kt", "MultilingualMathSpeech.kt",
            "NarrationLanguage.kt", "ReadingRanges.kt", "WindowsNarrationSpeechRules.kt",
            "MathExpression.kt", "MathReadingAtoms.kt", "MathTextSemantics.kt",
            "MathLatexSerializer.kt", "MathLatexParser.kt", "MathUiMessage.kt",
        }
        sources = [path for path in module.host_sources() if path.name in names]
        if {path.name for path in sources} != names:
            raise RuntimeError("Missing original Spanish frontend dependency")
        # This complete original object shares a file with unrelated presentation
        # code; preserve its implementation exactly, as the existing symbol test does.
        original = (module.APP / "narration/ProgrammingTechnicalRanges.kt").read_text(encoding="utf-8-sig")
        boundary = "/** Presentation-only C++ ranges over the original text, shared by all programming lessons. */"
        if original.count(boundary) != 1:
            raise RuntimeError("The original semicolon helper boundary changed")
        semicolon = folder / "TechnicalSemicolonContext.kt"
        semicolon.write_text(original.split(boundary)[0], encoding="utf-8")
        fixture = folder / "SpanishProseG2PTest.kt"
        fixture.write_text((WORKER / "test_spanish_prose_g2p.kt.in").read_text(encoding="utf-8-sig"), encoding="utf-8")
        inputs = folder / "published-sources.json"
        inputs.write_text(json.dumps(cases, ensure_ascii=False), encoding="utf-8")
        jar = folder / "spanish-prose-g2p.jar"
        command = [java, "-cp", os.pathsep.join(map(str, compiler)),
                   "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                   f"-Xjdk-release={target}", "-classpath", runtime, "-d", str(jar),
                   *map(str, sources), str(semicolon), str(fixture)]
        subprocess.run(command, check=True, cwd=ROOT, timeout=120)
        print(f"Spanish prose frontend only: Kotlin {module.KOTLIN_VERSION}, JVM target {target}", flush=True)
        subprocess.run([java, "-cp", str(jar) + os.pathsep + runtime,
                        "com.learngraphicsprogramming.app.narration.SpanishProseG2PTestKt", str(inputs)],
                       check=True, cwd=ROOT, timeout=60)


if __name__ == "__main__":
    main()

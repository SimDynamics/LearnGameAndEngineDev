"""Compile the real Windows Spanish frontend through G2P and phone ownership.

The default uses the project-selected JDK and JVM target. An explicit Java/target
override can run this text-only regression on another host; it does not verify
Windows playback, Kokoro PCM, model durations, or human pronunciation.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_semantic_source_envelopes import text_sources


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
        cases.append(dict(publication=f"u12l20/{stage}", label=f"u12l20/{stage}/p6b3",
                          source=block["text"], mode="auto"))
        if stage == "after":
            explanation = lesson["pages"][9]["blocks"][2]["outputCheck"]["cases"][0]["explanation"]
            fields = [field for field in lesson["textSemantics"]["fields"]
                      if field["block"] == "block_0009_0002"
                      and field["path"] == "/outputCheck/cases/0/explanation"]
            if len(fields) != 1 or hashlib.sha256(explanation.encode("utf-8")).hexdigest() != fields[0]["sourceSha256"]:
                raise AssertionError("The authentic U12L20 output/natural feedback changed")
            prefix = "✓ Correcto\n\n"
            shift = len(prefix.encode("utf-16-le")) // 2
            cases.append(dict(publication="u12l20/after", label="u12l20/after/p10b3/explanation",
                source=prefix + explanation, modes=["auto", "cpp"],
                semantics=dict(spans=[dict(span, start=span["start"] + shift, end=span["end"] + shift)
                                      for span in fields[0]["spans"]]),
                # Actual current semantic planner before the contextual-trace fix:
                # unlike plain preparation, output/natural retains literal --7.
                preparedSha256=[
                    "8d569fec25177f21f8e7e040dc94d14ccf20f34038830a28aa851c34b452b3df",
                    "5c0fcb2cece1d5f1ed90ff47e73e40b71404b0a2643e505b6635351ec2b0ebf0",
                    "b07937d7fba1233ad23cb1aba6f7849d449501c007fd21383727feece5f10cb7",
                    "b656f6c1385f1517b2550138a194bdcffc12c9fea1954f7a9b47fc77fd8f105b",
                    "23e0e7374545793274b84ac63e11d74e1ae72fcca08fd642f10d3349a138386f",
                    "62434f09d122e98322c91de613ff2ef3152ef4509b2d22acc2b53cc0a482829c",
                    "775af9536dd8cd8f9522aa5cece81f19b3f26ae66e4362017faf4cb1aa15f0ec",
                    "1160e2c97c97733ddc93160eb8fe372f1bcd7ea3e7acf726435ac4855d01a372",
                ],
                sentencePhoneSha256=[
                    "4e5bbc77877025ddcc04d4bdf336fd39b7dcf8cdd90136bae429b720e796b369",
                    "6a8dbad84ce6d4e0fa9038ed5ba0cffce3b502ae47dd4110fb5e02d9c69be3fa",
                    "ea4bcb9021d3ac831605b81611687e4c50be9c9e506032017d254a6b7b65704f",
                    "06faa076519dc7d07df622615a4f1550ae10b0aef1bced33c2029d24e8510ebe",
                    "1c6d62e28dfccd3303b26713f7b839f0a5e60a82cd65a1ddf9b7463abec5a46f",
                    "24088d3bffbea58c1fd60187c0cf90954f224881e8cc6a19d8007ec822b65a8b",
                    "9b09718fc159fef4dd11c7d6dba544c887497f7d1da79cb39095f10af90f95a5",
                    "a136ca1478ae61feebd81e3f23883c929c1908f78da74a1556fd27159a7d87ba",
                ],
                recordingKeys=[
                    "566be3a773e625cef836ba6bdbd39ba53dbd4e6d6a5dd8b620d496cfadc16454",
                    "b666cf074842b38f4ae6df309c77d426388af74714f1ac9afb8bccb4b523857a",
                    "2f6a9fcaf790554080f67eb2077efc9f287f0a37d0aafd48f582bb2651332bcc",
                    "aaad6ddb3b7e684b0489f5fcf0c1baf567f3b253b7dc7c9691f1ffe461c493eb",
                    "0ff3b94ac7aa1a8f1aa003e6b617624bbb756b5a851d3978af52bb15ac85dc82",
                    "8e236f6b2f399ecc2f68893afe42414c944a981ea8060789b5abea3dfea9c66c",
                    "ef3fdeece308e30b8f1842f41a7bc39acdcec5ebab19862155e7346486023dac",
                    "43e80ee4bd29f5814b9e710e484836d8653b0130dfd960080daa93309f9d2af4",
                ]))
    for stage in remainder.COMMITS:
        lesson = json.loads(remainder.published_bytes(stage))
        block = lesson["pages"][2]["blocks"][2]
        if block.get("type") != "order_code" or "resultado_en_dominio" not in block["question"]:
            raise AssertionError("The authentic U11L16 p3b3 question changed")
        cases.append(dict(publication=f"u11l16/{stage}", label=f"u11l16/{stage}/p3b3/question",
                          source=block["question"], mode="cpp"))
        side = "before" if stage == "before" else "after"
        if block.get("explanation") != remainder.EXPLANATIONS[side]:
            raise AssertionError("The authentic U11L16 p3b3 explanation changed")
        cases.append(dict(publication=f"u11l16/{stage}", label=f"u11l16/{stage}/p3b3/explanation",
                          source=block["explanation"], modes=["auto", "cpp"],
                          # Exact empty-lexicon production phones from 8259a709,
                          # before observing residual percent expansion.
                          sentencePhoneSha256=[
                              "e0668db8e9a2cb6e528746a66a063ded3db525a5ee2fec49217abc7ee3464abc",
                              "ba69dd9af28d3001384b2f07ad7937b1caf2494c43f966cafd438d4506caf3b1"
                              if side == "before" else
                              "93f85e5d1b5bbf90b0ea323a0169d9fdcf0ccfe70afb7b80b0d4864b7c846e28",
                          ]))
    lesson = json.loads(compiler.published_bytes("u11l19", "before"))
    block = lesson["pages"][4]["blocks"][4]
    if block.get("type") != "paragraph" or "duplicados/ausencia/mala" not in block["text"]:
        raise AssertionError("The authentic U11L19 p5b5 criterion changed")
    cases.append(dict(publication="u11l19/before", label="u11l19/before/p5b5", source=block["text"], mode="auto",
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
        # Reuse the complete production planner extraction so authored natural
        # output reaches G2P without an extra unmarked preparation hiding a bug.
        sources = text_sources(module, folder)
        fixture = folder / "SpanishProseG2PTest.kt"
        fixture.write_text((WORKER / "test_spanish_prose_g2p.kt.in").read_text(encoding="utf-8-sig"), encoding="utf-8")
        inputs = folder / "published-sources.json"
        inputs.write_text(json.dumps(cases, ensure_ascii=False), encoding="utf-8")
        jar = folder / "spanish-prose-g2p.jar"
        command = [java, "-cp", os.pathsep.join(map(str, compiler)),
                   "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                   f"-Xjdk-release={target}", "-classpath", runtime, "-d", str(jar),
                   *map(str, sources), str(fixture)]
        subprocess.run(command, check=True, cwd=ROOT, timeout=120)
        print(f"Spanish prose frontend only: Kotlin {module.KOTLIN_VERSION}, JVM target {target}", flush=True)
        subprocess.run([java, "-cp", str(jar) + os.pathsep + runtime,
                        "com.learngraphicsprogramming.app.narration.SpanishProseG2PTestKt", str(inputs)],
                       check=True, cwd=ROOT, timeout=60)


if __name__ == "__main__":
    main()

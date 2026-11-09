"""Compile the real Windows source planner and verify its semantic envelopes.

This text-only regression loads authenticated curricular publications and uses the
production planning, projection, segmentation, word mapping and audio-key code.
It does not synthesize PCM or certify Windows playback or human pronunciation.
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


ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "native/narration/worker"

# Measured from the production planner at 6bb669d6, before the semantic-envelope fix.
# speech includes prepared/spoken text, absolute word ranges and recording keys;
# original/unmarked include every public plan field, including source envelopes.
BASELINES = {
    "adjacent_wrappers": {
        "original": "702f417e2be9fbb051edafc63d7468e65547266d776f45ff623e17e695e24be8",
        "speech": "5f8b8696b521469ab6c222191aa967a0076d2095f62434ab1ab6f47555441a41",
        "unmarked": "30738b81c0dda91b559f0ec21c22c5754d71db12848a86e8aad7c023db80aa00"
    },
    "correct_icon": {
        "original": "b777b9f90164ffe8f04b5abdc85cbe8b3256b9107a7b035b336d4dc0a88213b2",
        "speech": "5cae59866e13ebfe44d309899a68dae4f9e867c7f383e29d67575516910c673c",
        "unmarked": "af02ffb37e3502c3fd59210e71bbdacee1c337efc8971c5c0b10dcd8355fb295"
    },
    "explicit_roles": {
        "original": "010e54d77db2edb1c5f21ef2475a7195657731c091fe61f825c290ec3d81eea1",
        "speech": "e1e765bb7bfead12a07571acb93d0082895bf7de9fd26c43462a7081a5d9c2f9",
        "unmarked": "183ff9b29a01cefff171e305f788e8363d93a85835ea5bc09c6842c97eafddc8"
    },
    "incorrect_icon": {
        "original": "02aea4aca9eb5f3dc27dbb26457129aadf9fe886f312dd0041b72dfd5ebe502c",
        "speech": "06e49d62d362da2465e7cc6febcfd298af030f3a86b843a03db64cd8f275b50c",
        "unmarked": "58cb6f24e2c64cbc95f7a34d5c762092583e9cc253a6dd418a8b6f6b6d8e5a15"
    },
    "punctuation": {
        "original": "9e9638e3a7c5b861fcaa8301ee828ecdfb6db87660c4b5b36af0e2ef33c3334e",
        "speech": "c9270ffa22de3bfe6306a63f8356c3a1e3fd1b6c1680d687617a9593bd269a24",
        "unmarked": "72ec35faf26776ea9af4be413d194d1140ef0e1c47b1712dd846e0c086da3f2e"
    },
    "u11l16/published30/block_0002_0001/text": {
        "original": "82aeb2a3922fead1b7594f577437de3edbb8542ecda5b09e1c159b5b635d9bc0",
        "speech": "5ca227a65a025c1187435b3e86003a0fcde324023d728c002e5ed96faaa1b39a",
        "unmarked": "2b90ae711e6eebe6136fb15b3e932b3fae0ceae6470ec35053b007ce5c52576a"
    },
    "u12l20/after/block_0005_0001/text": {
        "original": "93878216e9181313eb95915f2681359034479a0dca2a318f1c31b36e3e0cc757",
        "speech": "0f567e8c850dc2b821f7a09b90a84357796781e8db25e32a2e3a980f59872e56",
        "unmarked": "99d28744d63df9273c6b858bd0dd562982eb3b7e8e2b7269ae10fb00c52d298b"
    },
    "u12l20/after/block_0005_0002/text": {
        "original": "e312ea1922c422b73482d19b17845e5ef8d7ab34fe573ebbaf2f3496e2bba2eb",
        "speech": "ca04e6856585c87dec0385963c11892ec4e569ca9c1e603aa534cc418c42d286",
        "unmarked": "496c466ddf69e56187c0745137676e8c46a679ed0fb2dfa1cc3328be28ddc5fd"
    },
    "u12l20/after/block_0005_0003/text": {
        "original": "d2cb575dcfed2198b1ac6eb28396e909efa4f00a94991e0c6bedd6cd5ecdcf3a",
        "speech": "5bbf83467206abfb6102af506bc37be640f2cd76c400a20b30ed5d1c0072247b",
        "unmarked": "a8b5627ed418124b6ccb00c3df409c06ce15dc2ca0348c45726485fc9821597a"
    },
    "u12l20/after/block_0005_0004/outputCheck/cases/0/explanation": {
        "original": "a37869fab4413710f67700a5bafd52562421a6442c04ee9719e3f2f044df7708",
        "speech": "5c7b6a96fe4208cccffcecfc540a67160df958700cbf032e44fb8c2507ca04da",
        "unmarked": "bdfb26bddb7806a1759648eb9fafbdf553d38fc4a9b60797871c9d075fe321ce"
    },
    "u12l20/after/block_0005_0006/text": {
        "original": "a2435e6d2dfdc1c9b406f7ed06e1fc6fa31134b0ea5e2376979300282df0c64b",
        "speech": "8f7725ae63b1a5c97c3be5cb830b41cc6e62d6f19d688c1038eafff13c98270a",
        "unmarked": "4e09251a03fc6b63944391b1f7cf25de23d076ba1d3be5fcf7c009691ffb59ed"
    },
    "utf16": {
        "original": "456252ae0ed938d6fc010a273007142906c3e80b6f3b52cfec0335c6609e3631",
        "speech": "1552b7676a28cc5af63aa53034cbf48504e356c5d53719da50493b2380255f9a",
        "unmarked": "0dcfbb06e1a947d2c03abdf75b4f823ff061bc9a39f345e7840c97ebdabf5cd6"
    },
    "wrappers": {
        "original": "311e57914943d450b6ee9a462701f26f331065ab68145545e270135a44d25957",
        "speech": "f0790ca6363733323b0fdf8ad7c50c12def80df75662a8939cffb8351a3f00e6",
        "unmarked": "c9090e96fc3a46ec2e0cf2da04d3b8e48ea1cbbce8fbeb8d62b0d02a1f4e769a"
    }
}


def units(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def cases() -> list[dict]:
    sys.path.insert(0, str(ROOT / "scripts/curriculum"))
    import compiler_import_fixture_source as compiler
    import u11_remainder_editorial_fixture_source as remainder

    result = []
    fields = [(f"block_0005_000{index}", "/text") for index in (1, 2, 3, 6)]
    fields.append(("block_0005_0004", "/outputCheck/cases/0/explanation"))
    publications = [
        ("u12l20/after", json.loads(compiler.published_bytes("u12l20", "after")), fields),
        ("u11l16/published30", json.loads(remainder.published_bytes("published30")), [("block_0002_0001", "/text")]),
    ]
    for publication, lesson, selected_fields in publications:
        for owner, pointer in selected_fields:
            declarations = [field for field in lesson["textSemantics"]["fields"]
                            if field["block"] == owner and field["path"] == pointer]
            if len(declarations) != 1:
                raise AssertionError("The authenticated semantic owner changed")
            declaration = declarations[0]
            positions = [(page, block) for page, layout in enumerate(lesson["stateLayout"]["current"])
                         for block, identity in enumerate(layout["blocks"]) if identity == owner]
            if len(positions) != 1:
                raise AssertionError("The authenticated stable layout changed")
            page, block = positions[0]
            source = lesson["pages"][page]["blocks"][block]
            for part in pointer.split("/")[1:]:
                source = source[int(part)] if isinstance(source, list) else source[part]
            if hashlib.sha256(source.encode("utf-8")).hexdigest() != declaration["sourceSha256"]:
                raise AssertionError("The authenticated semantic field is stale")
            spans = [dict(span) for span in declaration["spans"]]
            if pointer != "/text":
                # The native successful compiler feedback uses this actual heading;
                # its icon stays outside the authored field and shifts all spans.
                prefix = "✓ Correcto\n\n"
                source = prefix + source
                spans = [dict(span, start=span["start"] + units(prefix), end=span["end"] + units(prefix))
                         for span in spans]
            result.append(dict(label=publication + "/" + owner + pointer, source=source,
                               semantics=dict(spans=spans), authenticated=True))
    for label, source in (
        ("punctuation", "¡Primer aviso! ¿Segundo paso? Cierre; sigue, termina: listo."),
        ("wrappers", "`Uno.` `Dos.` ```Tres.```"),
        ("adjacent_wrappers", "`Uno.`` Dos.`"),
        ("correct_icon", "✓ Correcto\n\n`Lista`. Fin; continúa."),
        ("incorrect_icon", "✗ Incorrecto\n\n`Dato`. Revisa."),
        ("utf16", "😀 Inicio. `Árbol`. Final 😀."),
    ):
        result.append(dict(label=label, source=source, semantics=dict(spans=[])))
    source = "El valor `y` conserva 7; termina."
    first = units(source[:source.index("y")])
    number = units(source[:source.index("7")])
    result.append(dict(label="explicit_roles", source=source, semantics=dict(spans=[
        dict(start=first, end=first + 1, text="y", role="cpp", reading="technical"),
        dict(start=number, end=number + 1, text="7", role="data", reading="natural"),
    ])))
    return result


def between(source: str, first: str, last: str) -> str:
    if source.count(first) != 1 or source.count(last) != 1:
        raise RuntimeError("A production source-planning extraction boundary changed")
    return source[source.index(first):source.index(last)]


def text_sources(module, folder: Path) -> list[Path]:
    module.OUT = folder / "host"
    names = {
        "SpanishTechnicalSpeech.kt", "SpanishMathSpeech.kt", "SpanishFluidG2P.kt",
        "ManualEnglishTerms.kt", "ManualEnglishSpeech.kt", "ManualEnglishPronunciations.kt",
        "MixedSpanishG2P.kt", "NarrationSpokenWords.kt", "NarrationSegments.kt",
        "EnglishTechnicalSpeech.kt", "FrenchSpeechSymbols.kt", "MultilingualMathSpeech.kt",
        "NarrationLanguage.kt", "ReadingRanges.kt", "WindowsNarrationSpeechRules.kt",
        "MathExpression.kt", "MathReadingAtoms.kt", "MathTextSemantics.kt",
        "MathLatexSerializer.kt", "MathLatexParser.kt", "MathUiMessage.kt",
        "FrenchTechnicalSpeech.kt", "NarrationSourceProjection.kt", "NarrationWordMappings.kt",
        "IncorrectFeedbackNarration.kt", "WindowsLegacyTextSemantics.kt", "WindowsCharacterSpeech.kt",
        "KokoroNarrationCache.kt", "KokoroModelManifest.kt", "KokoroVoiceCatalog.kt", "EnglishAuxResources.kt",
    }
    sources = [path for path in module.host_sources() if path.name in names]
    if {path.name for path in sources} != names:
        raise RuntimeError("Missing original source-planning dependency")
    # Keep complete original methods. Their files also contain unrelated native
    # resource collectors and ONNX synthesis, which this text-only test omits.
    original = (module.APP / "narration/ProgrammingTechnicalRanges.kt").read_text(encoding="utf-8-sig")
    boundary = "/** Presentation-only C++ ranges over the original text, shared by all programming lessons. */"
    if original.count(boundary) != 1:
        raise RuntimeError("The original semicolon helper boundary changed")
    semicolon = folder / "TechnicalSemicolonContext.kt"
    semicolon.write_text(original.split(boundary)[0], encoding="utf-8")
    metadata = (module.APP / "narration/phonemes/EnglishMisakiG2P.kt").read_text(encoding="utf-8-sig")
    english = folder / "EnglishMisakiResources.kt"
    english.write_text("package com.learngraphicsprogramming.app.narration.phonemes\n" +
        between(metadata, "object EnglishMisakiResources {", "internal object MisakiStress {"), encoding="utf-8")
    french = (module.APP / "narration/MixedFrenchG2P.kt").read_text(encoding="utf-8-sig")
    cache_helpers = folder / "MixedFrenchG2P.kt"
    cache_helpers.write_text("package com.learngraphicsprogramming.app.narration\n" +
        between(french, "internal object MixedFrenchG2P {", "    fun phonemizePrepared(") + "}\n", encoding="utf-8")
    semantics = (WORKER / "WindowsTextSemantics.kt").read_text(encoding="utf-8-sig")
    pure_semantics = folder / "WindowsTextSemantics.kt"
    pure_semantics.write_text(between(semantics, "package lgaed.narration.windows", "    fun lookup(") +
        between(semantics, "    private fun hiddenPresentation(", "    /** Anticipation only:") + "}\n", encoding="utf-8")
    worker = (WORKER / "WindowsNarrationWorker.kt").read_text(encoding="utf-8-sig")
    backend = folder / "SemanticPlanBackend.kt"
    backend.write_text("""package lgaed.narration.windows
import com.learngraphicsprogramming.app.narration.*
import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest
""" + between(worker, "private fun sha256(bytes:", "private fun sha256(file:") +
        between(worker, "private fun language(request:", "// Mirrors Android NarrationStorageBudget") +
        between(worker, "private data class Segment(", "private class Request(") +
        "internal class SemanticPlanBackend {\n" +
        between(worker, "    private val plans = object", "    private var spanish:") +
        between(worker, "    private fun planKey(", "    fun identity(request:") + "}\n", encoding="utf-8")
    return sources + [semicolon, english, cache_helpers, pure_semantics, backend]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java", help="Explicit Java executable; otherwise use the project-selected full JDK")
    parser.add_argument("--jvm-target", help="Text-only test override; defaults to the production worker target")
    parser.add_argument("--dependency-cache", type=Path, help="Reuse authenticated compiler JARs from this directory")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("semantic_envelopes_build", WORKER / "build_worker.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if args.java:
        java = args.java
    else:
        tools = module.launcher_native_tools()
        java = str(module.resolve_java_home(Path(tools["environment"]["JAVA_HOME"])) / "bin/java.exe")
    target = args.jvm_target or module.JVM_TARGET
    lock = json.loads(module.DEPENDENCY_LOCK.read_text(encoding="utf-8"))
    resolver = module.MavenResolver(args.dependency_cache or module.DEPS, lock["sha256"])
    compiler = resolver.closure(module.COMPILER_ROOTS)
    roots = [coordinate for coordinate in module.RUNTIME_ROOTS
             if coordinate[:2] in {("org.jetbrains.kotlin", "kotlin-stdlib"),
                                   ("org.json", "json"), ("org.jetbrains", "annotations")}]
    if len(roots) != 3:
        raise RuntimeError("The original source-planning runtime dependencies changed")
    runtime = os.pathsep.join(str(resolver.fetch(coordinate, "jar")) for coordinate in roots)
    inputs = cases()
    if {case["label"] for case in inputs} != set(BASELINES):
        raise AssertionError("The measured original planning contracts are incomplete")
    for case in inputs:
        case["baseline"] = BASELINES[case["label"]]
    with tempfile.TemporaryDirectory(prefix="lgaed-semantic-envelopes-") as temporary:
        folder = Path(temporary)
        sources = text_sources(module, folder)
        fixture = folder / "SemanticSourceEnvelopesTest.kt"
        fixture.write_text((WORKER / "test_semantic_source_envelopes.kt.in").read_text(encoding="utf-8-sig"), encoding="utf-8")
        input_file = folder / "published-sources.json"
        input_file.write_text(json.dumps(inputs, ensure_ascii=False), encoding="utf-8")
        jar = folder / "semantic-source-envelopes.jar"
        subprocess.run([java, "-cp", os.pathsep.join(map(str, compiler)),
                        "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                        f"-Xjdk-release={target}", "-classpath", runtime, "-d", str(jar),
                        *map(str, sources), str(fixture)], check=True, cwd=ROOT, timeout=120)
        print(f"Semantic source planner only: Kotlin {module.KOTLIN_VERSION}, JVM target {target}", flush=True)
        subprocess.run([java, "-cp", str(jar) + os.pathsep + runtime,
                        "lgaed.narration.windows.SemanticSourceEnvelopesTestKt", str(input_file)],
                       check=True, cwd=ROOT, timeout=60)


if __name__ == "__main__":
    main()

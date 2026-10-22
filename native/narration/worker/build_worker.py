"""Build the Windows host for the existing Kotlin Kokoro frontend.

The canonical phonemizers/dictionary remain in app/src/main/java. Generated
sources under out/narration replace only Android platform glue, never speech
rules, cache identities, model weights or curricular data.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "app/src/main/java/com/learngraphicsprogramming/app"
OUT = ROOT / "out/narration"
DEPS = ROOT / "out/deps"
ORT_SHA256 = "57d8cedf6f0c8fd6d50ed65c17bf9939f55a4512f4a1f3e732023d258f262ac6"
PURE_NARRATION = [
    "ManualEnglishTerms", "ManualEnglishPronunciations", "ManualEnglishSpeech",
    "SpanishTechnicalSpeech", "SpanishMathSpeech", "EnglishTechnicalSpeech",
    "FrenchTechnicalSpeech", "FrenchSpeechSymbols", "MultilingualMathSpeech",
    "ProgrammingTechnicalRanges", "NarrationSpokenWords", "NarrationSegments",
    "NarrationWordAlignment", "NarrationWordMappings", "NarrationWavWriter",
    "KokoroModelManifest", "KokoroVoiceCatalog", "EnglishAuxResources",
    "MixedSpanishG2P", "MixedFrenchG2P",
]
PHONEMES = ["SpanishFluidG2P", "FrenchFluidG2P", "FrenchFluidResources",
    "FrenchRuleBasedG2P", "FrenchNumberNormalizer", "EnglishMisakiG2P",
    "EnglishMisakiBartG2P", "EnglishMisakiSpacyPos", "MisakiLexicon",
    "MisakiEnglishPipeline", "MisakiEnglishNumbers", "SpacyPosFrontend"]


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


@contextmanager
def build_lock():
    """Serialize presets of this checkout; Work owns a different output tree."""
    import msvcrt
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "build.lock").open("a+b") as lock:
        lock.seek(0, os.SEEK_END)
        if lock.tell() == 0:
            lock.write(b"\0")
            lock.flush()
        acquired = False
        try:
            while not acquired:
                lock.seek(0)
                try:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                    acquired = True
                except OSError:
                    time.sleep(0.1)
            yield
        finally:
            if acquired:
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def download_ort() -> Path:
    path = DEPS / "onnxruntime-1.30.0.jar"
    if path.is_file():
        if digest(path) != ORT_SHA256:
            raise ValueError("Existing ONNX Runtime dependency failed SHA-256; left untouched")
        return path
    DEPS.mkdir(parents=True, exist_ok=True)
    partial = path.with_suffix(".jar.part")
    try:
        url = "https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime/1.30.0/onnxruntime-1.30.0.jar"
        with urllib.request.urlopen(url, timeout=90) as source, partial.open("xb") as output:
            shutil.copyfileobj(source, output, 1024 * 1024)
        if digest(partial) != ORT_SHA256:
            raise ValueError("Official desktop ONNX Runtime failed SHA-256")
        partial.rename(path)
    finally:
        partial.unlink(missing_ok=True)
    return path


def cached_jar(group: str, artifact: str, version: str) -> Path:
    base = Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".gradle/caches/modules-2/files-2.1"
    files = sorted((base / group / artifact / version).glob(f"*/{artifact}-{version}.jar"))
    if len(files) != 1:
        raise RuntimeError(f"Missing local build dependency {group}:{artifact}:{version}")
    return files[0]


def exact(source: str, old: str, new: str, count: int = 1) -> str:
    if source.count(old) != count:
        raise ValueError(f"Platform adaptation anchor changed: {old[:90]}")
    return source.replace(old, new)


def generated(name: str, source: str) -> Path:
    if re.search(r"^import android\.|android\.content\.|android\.os\.", source, re.MULTILINE):
        raise ValueError(f"Android platform dependency remained in {name}")
    path = OUT / "generated" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    content = "// Generated from canonical app sources; Windows platform adaptation only.\n" + source
    if not path.is_file() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8", newline="\n")
    return path


def math_host_sources() -> list[Path]:
    """Compile original evaluators; only resource resolution is platform-specific."""
    names = ("MathPuzzleSpec", "MathValidators", "MathAnswer", "MathShortcutResolver",
             "MathExpressionSemantics", "ParametricRationalExpression", "RigidAngleCopy",
             "EuclideanDiagram", "MathPuzzleFeedback", "MathInputMode")
    sources = []
    for name in names:
        path = APP / "math" / f"{name}.kt"
        code = path.read_text(encoding="utf-8-sig")
        if "import com.learngraphicsprogramming.app.R\n" in code:
            code = exact(code, "import com.learngraphicsprogramming.app.R\n", "")
        code = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', code)
        if name == "MathValidators":
            code = exact(code, "    fun resolveMessage(context: android.content.Context): String = uiMessage?.resolve(context) ?: message\n", "")
        sources.append(generated(f"{name}.kt", code))
    code = (APP / "math/MathUiMessage.kt").read_text(encoding="utf-8-sig")
    for old in ("import android.content.Context\n", "import androidx.annotation.StringRes\n",
                "import com.learngraphicsprogramming.app.R\n"):
        code = exact(code, old, "")
    code = exact(code, "@StringRes val resourceId: Int", "val resourceId: String")
    start = code.index("    fun resolve(context: Context)")
    end = code.index("    // Stable diagnostic", start)
    code = code[:start] + code[end:]
    code = code[:code.index("/** Technical/parser exceptions")]
    code = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', code)
    sources.append(generated("MathUiMessage.kt", code))

    # Resource names replace Android's generated integers without a fake R class.
    # Learner-facing strings remain the original, translated Android resources.
    def kotlin(value: str) -> str:
        return json.dumps(value, ensure_ascii=False).replace("$", "\\$")
    def resources(folder: str) -> dict[str, str]:
        result = {}
        for path in sorted((ROOT / "app/src/main/res" / folder).glob("*.xml")):
            tree = ET.parse(path)
            for element in tree.findall("string"):
                name = element.attrib["name"]
                if not name.startswith("math_"):
                    continue
                text = re.sub(r"[\t\r\n ]+", " ", "".join(element.itertext())).strip()
                text = re.sub(r"\\([nt'\"\\])", lambda m: {"n": "\n", "t": "\t"}.get(m[1], m[1]), text)
                result[name] = text
        return result
    maps = {"es": resources("values"), "en": resources("values-en"), "fr": resources("values-fr")}
    body = "package lgaed.narration.windows\n\ninternal object MathLocaleResources {\n"
    body += "    private val languages = mapOf(\n" + ",\n".join(
        f"        {kotlin(locale)} to mapOf(\n" + ",\n".join(
            f"            {kotlin(key)} to {kotlin(value)}" for key, value in sorted(values.items())) + "\n        )"
        for locale, values in maps.items()) + "\n    )\n"
    body += '''    fun resolve(code: String, arguments: List<Any> = emptyList(), language: String): String {
        val locale = language.substringBefore('-')
        val pattern = languages[locale]?.get(code) ?: languages.getValue("es")[code]
            ?: error("Missing original mathematical UI resource: $code")
        val values = arguments.map {
            if (it is com.learngraphicsprogramming.app.math.MathUiMessage)
                resolve(it.resourceId,it.arguments,language) else it
        }.toTypedArray()
        return if (values.isEmpty()) pattern else String.format(java.util.Locale.forLanguageTag(language),pattern,*values)
    }
}
'''
    sources.append(generated("MathLocaleResources.kt", body))
    return sources


def host_sources() -> list[Path]:
    sources = [APP / "narration" / f"{name}.kt" for name in PURE_NARRATION]
    sources += [APP / "math" / f"{name}.kt" for name in ("MathExpression", "MathReadingAtoms", "MathTextSemantics", "MathLatexSerializer")]
    for name in PHONEMES + ["KokoroOnnxEngine", "KokoroDurationModel"]:
        path = APP / "narration" / ("phonemes" if name in PHONEMES else "") / f"{name}.kt"
        code = path.read_text(encoding="utf-8-sig")
        if "import android.os.Looper" in code:
            code = exact(code, "import android.os.Looper", "import lgaed.narration.windows.WindowsWorkerThread")
            code = code.replace("Looper.myLooper() != Looper.getMainLooper()", "WindowsWorkerThread.isWorker()")
            code = code.replace("Looper.myLooper() == Looper.getMainLooper()", "!WindowsWorkerThread.isWorker()")
        if name == "FrenchFluidG2P":
            code = exact(code, "import android.util.JsonReader", "import com.google.gson.stream.JsonReader")
        if name == "KokoroOnnxEngine":
            code = exact(code, "import android.os.Build", "import lgaed.narration.windows.WindowsHost")
            code = exact(code, "import android.util.Log", "import lgaed.narration.windows.WindowsLog")
            code = code.replace("Log.w(", "WindowsLog.w(").replace("Log.i(", "WindowsLog.i(")
            code = code.replace("android.os.SystemClock.elapsedRealtime()", "WindowsHost.elapsedRealtime()")
            code = exact(code, "NarrationCpuBudget.foregroundThreadCount()", "Runtime.getRuntime().availableProcessors()")
            code = exact(code, "Build.VERSION.SDK_INT >= 29", "WindowsHost.nnapiAvailable")
            code = code.replace("($cpuCores cores)", "($cpuCores ORT threads)")
        sources.append(generated(f"{name}.kt", code))

    language = (APP / "narration/NarrationLanguage.kt").read_text(encoding="utf-8-sig")
    language = exact(language, "import android.content.Context\n", "")
    language = exact(language, "import com.learngraphicsprogramming.app.R\n", "")
    language = exact(language, ", val labelRes: Int", "")
    language = re.sub(r", R\.string\.narration_language_[a-z_]+,", ",", language)
    language = exact(language, "    fun label(context: Context): String = context.getString(labelRes)\n", "")
    sources.append(generated("NarrationLanguage.kt", language))

    # The parser's diagnostic resource handles are a UI concern, not its AST or
    # parsing rules. Keep all parser logic and use host-readable resource names.
    parser = (APP / "math/MathLatexParser.kt").read_text(encoding="utf-8-sig")
    parser = exact(parser, "import com.learngraphicsprogramming.app.R\n", "")
    parser = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', parser)
    parser = re.sub(r"^\s*fun resolveMessage\(context: android\.content\.Context\): String = uiMessage\.resolve\(context\)\n", "\n", parser, flags=re.MULTILINE)
    sources.append(generated("MathLatexParser.kt", parser))
    sources += math_host_sources()

    projection = (APP / "narration/NarrationSourceProjection.kt").read_text(encoding="utf-8-sig")
    projection = exact(projection, "import com.learngraphicsprogramming.app.math.MathTextRenderer\n", "")
    # Native Windows supplies original strings, never Android Spanned values.
    # The original MathTextRenderer returns empty ranges for those same strings.
    projection = exact(projection,
        "else MathTextRenderer.formulaRanges(source)\n            .sortedBy { it.first }.map { ReadingTextRange(it.first, it.last + 1) }",
        "else emptyList()")
    sources.append(generated("NarrationSourceProjection.kt", projection))

    prefix = "package com.learngraphicsprogramming.app.narration\n"
    cache = (APP / "narration/KokoroNarrationCache.kt").read_text(encoding="utf-8-sig")
    cache = cache[cache.index("internal object KokoroNarrationCache {"):cache.index("    private fun root(context:")]
    sources.append(generated("KokoroNarrationCache.kt", prefix + "import java.security.MessageDigest\n" + cache + "}\n"))
    history = (APP / "narration/NarrationResumeHistory.kt").read_text(encoding="utf-8-sig")
    identity = history[history.index("    fun key("):history.index("    fun restore(")]
    sources.append(generated("NarrationResumeIdentity.kt", prefix + "import java.security.MessageDigest\n"
        + "enum class NarrationEngine { ANDROID, KOKORO }\ninternal object NarrationResumePolicy {\n" + identity + "}\n"))
    reading = (APP / "narration/ReadingTextView.kt").read_text(encoding="utf-8-sig")
    text_range = re.search(r"data class ReadingTextRange\([^\n]+", reading).group()
    mapping = (APP / "narration/LessonWordReadingProgress.kt").read_text(encoding="utf-8-sig")
    mapping = mapping[mapping.index("data class NarrationWordMapping("):mapping.index("/** Position-bound")]
    sources.append(generated("ReadingRanges.kt", prefix + text_range + "\n" + mapping))
    sources += sorted((ROOT / "native/narration/worker").glob("*.kt"))
    return sources


def launcher(java: Path) -> Path:
    home = java.parent.parent
    if not (home / "include/jni.h").is_file():
        raise RuntimeError("The configured JDK must include the official JNI headers (JRE-only runtimes cannot build the native host)")
    source = ROOT / "native/narration/worker_launcher.cpp"
    output = OUT / "lgaed_narration_host.exe"
    signature = digest(source) + str(home)
    stamp = OUT / "launcher-build.sha256"
    if not output.is_file() or not stamp.is_file() or stamp.read_text() != signature:
        vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
        installation = subprocess.check_output([str(vswhere),"-latest","-products","*",
            "-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
        batch = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
        if re.search(r'["%!?&|<>^\r\n]', str(batch)):
            raise ValueError("Unexpected shell metacharacter in installed MSVC path")
        setup = subprocess.run(f'cmd.exe /d /v:off /s /c "call "{batch}" >nul && set"',
            check=True,capture_output=True,text=True)
        environment = {key.upper():value for key,value in os.environ.items()}
        for line in setup.stdout.splitlines():
            if "=" in line and not line.startswith("="):
                key,value = line.split("=",1); environment[key.upper()] = value
        compiler_path = shutil.which("cl.exe",path=environment["PATH"])
        if not compiler_path:
            raise RuntimeError("MSVC x64 compiler is missing from its configured environment")
        command = [compiler_path,"/nologo","/std:c++20","/EHsc","/W4","/utf-8","/MD",
            f"/I{home / 'include'}",f"/I{home / 'include/win32'}",str(source),
            f"/Fo{OUT / 'worker_launcher.obj'}",f"/Fe{output}"]
        subprocess.run(command,env=environment,check=True,cwd=ROOT)
        stamp.write_text(signature,encoding="utf-8")
    (OUT / "jdk-root.txt").write_text(str(home),encoding="utf-8")
    return output


def build() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path)
    parser.add_argument("--english-resources", type=Path)
    args = parser.parse_args()
    java = Path(os.environ.get("JAVA_HOME", r"C:\Program Files\Android\Android Studio\jbr")) / "bin/java.exe"
    if not java.is_file():
        raise RuntimeError("A local x64 JDK/JBR is required to compile and run the shared Kotlin frontend")
    ort = download_ort()
    runtime = [cached_jar("org.jetbrains.kotlin", "kotlin-stdlib", "2.2.10"),
               cached_jar("org.json", "json", "20240303"), cached_jar("com.google.code.gson", "gson", "2.11.0"), ort]
    compiler = [cached_jar("org.jetbrains.kotlin", "kotlin-compiler-embeddable", "2.2.10"),
                runtime[0], cached_jar("org.jetbrains.kotlin", "kotlin-script-runtime", "2.2.10"),
                cached_jar("org.jetbrains.kotlin", "kotlin-reflect", "1.6.10"),
                cached_jar("org.jetbrains.kotlinx", "kotlinx-coroutines-core-jvm", "1.8.0"),
                cached_jar("org.jetbrains", "annotations", "23.0.0")]
    sources = host_sources()
    jar = OUT / "kokoro-worker.jar"
    signature = hashlib.sha256(("\n".join(str(p) + ":" + digest(p) for p in sources + runtime)).encode()).hexdigest()
    stamp = OUT / "worker-build.sha256"
    if not jar.is_file() or not stamp.is_file() or stamp.read_text() != signature:
        command = [str(java), "-cp", os.pathsep.join(map(str, compiler)),
            "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect", "-jvm-target", "17",
            "-classpath", os.pathsep.join(map(str, runtime)), "-d", str(jar), *map(str, sources)]
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode:
            raise SystemExit(result.returncode)
        stamp.write_text(signature, encoding="ascii")
    origin = ROOT.parent / "LearnGraphicsProgramming/build/kokoro"
    resources = (args.resources or origin / "downloads/1939ad2a8e416c0acfeecc08a694d14ef25f2231").resolve()
    english = (args.english_resources or origin / "english-install").resolve()
    ffmpeg = shutil.which("ffmpeg")
    classpath = os.pathsep.join(map(str,[jar,*runtime]))
    original_wavs = (origin / "../first-unit-speech-export").resolve()
    native_host = launcher(java)
    jvm = java.parent / "server/jvm.dll"
    arguments = [str(jvm),classpath,"lgaed.narration.windows.WindowsNarrationWorkerKt",
        "--resources",str(resources),"--english-resources",str(english),
        "--assets",str(ROOT / "app/src/main/assets"),"--original-wavs",str(original_wavs),
        "--profile","Work" if ROOT.name.endswith("-Work") else "Codex"]
    if ffmpeg:
        arguments += ["--ffmpeg",ffmpeg]
    config = {"java": str(native_host), "javaExecutable": str(java), "jvm": str(jvm),
        "classpath": classpath, "arguments": arguments,
        "mainClass": "lgaed.narration.windows.WindowsNarrationWorkerKt",
        "assets": str(ROOT / "app/src/main/assets"), "resources": str(resources),
        "englishResources": str(english), "ffmpeg": ffmpeg or "",
        "originalWavs": str(original_wavs)}
    config_path = OUT / "runtime.json"
    content = json.dumps(config, ensure_ascii=False, indent=2)
    if not config_path.is_file() or config_path.read_text(encoding="utf-8") != content:
        pending_config = config_path.with_suffix(".json.part")
        try:
            pending_config.write_text(content, encoding="utf-8")
            pending_config.replace(config_path)
        finally:
            pending_config.unlink(missing_ok=True)
    print(config_path)


if __name__ == "__main__":
    with build_lock():
        build()

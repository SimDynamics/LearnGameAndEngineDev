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
import zipfile

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


def cached_dependency_closure(group: str, artifact: str, version: str) -> list[Path]:
    """Read the original Maven runtime graph from Gradle's existing cache only.

    No repository-wide jar scan, downloads, dependency upgrades or Gradle build
    is needed. Parent/BOM management and exclusions retain the original graph.
    """
    base = Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".gradle/caches/modules-2/files-2.1"
    models: dict[tuple[str, str, str], tuple[dict, dict, list, str]] = {}

    def model(coordinate):
        if coordinate in models:
            return models[coordinate]
        g, a, v = coordinate
        files = sorted((base / g / a / v).glob(f"*/{a}-{v}.pom"))
        if len(files) != 1:
            # Gradle retains Guava's intentionally empty conflict-marker jar
            # without a POM. Verify it contains no classes before treating that
            # resource-only artifact as a leaf; never guess for real code jars.
            jar_files = sorted((base / g / a / v).glob(f"*/{a}-{v}.jar"))
            # Official versioned Maven Central POMs confirm these two jars have
            # no runtime dependencies (JavaPoet lists test dependencies only).
            # Gradle already holds the jars but not their POMs. Keep builds
            # offline rather than downloading duplicate metadata each time.
            # https://repo.maven.apache.org/maven2/org/jetbrains/annotations/23.0.0/annotations-23.0.0.pom
            # https://repo.maven.apache.org/maven2/com/squareup/javapoet/1.13.0/javapoet-1.13.0.pom
            leaves={("org.jetbrains","annotations","23.0.0"),("com.squareup","javapoet","1.13.0")}
            if not files and (g,a,v) in leaves and len(jar_files)==1:
                result=({}, {}, [], "jar");models[coordinate]=result;return result
            if not files and len(jar_files)==1:
                with zipfile.ZipFile(jar_files[0]) as archive:
                    if not any(name.endswith(".class") for name in archive.namelist()):
                        result=({}, {}, [], "jar");models[coordinate]=result;return result
            raise RuntimeError(f"Missing original cached calculator metadata {g}:{a}:{v}")
        tree = ET.parse(files[0]).getroot()
        for element in tree.iter():
            element.tag = element.tag.rsplit("}", 1)[-1]
        properties, managed = {}, {}
        parent = tree.find("parent")
        if parent is not None:
            inherited = model(tuple(parent.findtext(key, "") for key in ("groupId", "artifactId", "version")))
            properties.update(inherited[0]); managed.update(inherited[1])
            properties.update({f"project.parent.{key}":parent.findtext(key, "") for key in ("groupId","artifactId","version")})
        properties.update({"project.groupId": g, "project.artifactId": a, "project.version": v,
                           "pom.groupId": g, "pom.artifactId": a, "pom.version": v})
        properties.update({child.tag: child.text or "" for child in tree.findall("properties/*")})

        def expand(value, strict=True):
            for _ in range(20):
                changed = re.sub(r"\$\{([^}]+)\}", lambda found: properties.get(found[1], found[0]), value)
                if changed == value:
                    break
                value = changed
            if strict and "${" in value:
                raise RuntimeError(f"Unresolved original calculator dependency version: {value}")
            return value

        for key, value in list(properties.items()):
            properties[key] = expand(value, strict=False)

        def dependency(element):
            value = {key: expand(element.findtext(key, fallback)) for key, fallback in
                     (("groupId", ""), ("artifactId", ""), ("version", ""), ("scope", "compile"),
                      ("type", "jar"), ("optional", "false"))}
            value["exclusions"] = {(expand(item.findtext("groupId", "")), expand(item.findtext("artifactId", "")))
                                   for item in element.findall("exclusions/exclusion")}
            value["scope_declared"] = element.find("scope") is not None
            return value

        for element in tree.findall("dependencyManagement/dependencies/dependency"):
            item = dependency(element)
            if item["type"] == "pom" and item["scope"] == "import":
                managed.update(model((item["groupId"], item["artifactId"], item["version"]))[1])
            else:
                managed[(item["groupId"], item["artifactId"])] = item
        result = properties, managed, [dependency(item) for item in tree.findall("dependencies/dependency")], tree.findtext("packaging", "jar")
        models[coordinate] = result
        return result

    initial = (group, artifact, version)
    root_managed = model(initial)[1]
    selected: dict[tuple[str, str], Path] = {}

    def runtime_jar(g,a,v):
        try:
            return cached_jar(g,a,v)
        except RuntimeError:
            # Gradle module metadata may select an alternate runtime artifact
            # under the same coordinate (Guava's portable Android variant).
            # Accept only the exact locally cached file explicitly declared by
            # that metadata, not an arbitrary different/newer dependency.
            candidates=set()
            for metadata in (base/g/a/v).glob(f"*/{a}-{v}.module"):
                for variant in json.loads(metadata.read_text(encoding="utf-8")).get("variants",[]):
                    if variant.get("attributes",{}).get("org.gradle.usage")!="java-runtime":continue
                    for file in variant.get("files",[]):
                        name=file.get("name","")
                        if Path(name).name==name and name.endswith(".jar"):
                            candidates.update((base/g/a/v).glob(f"*/{name}"))
            if len(candidates)==1:return candidates.pop()
            raise

    def visit(coordinate, excluded=frozenset()):
        g, a, v = coordinate
        if (g, a) in excluded or (g, "*") in excluded or ("*", a) in excluded or (g, a) in selected:
            return
        _, managed, dependencies, packaging = model(coordinate)
        if packaging in ("jar","bundle"):
            selected[(g, a)] = runtime_jar(g, a, v)
        for item in dependencies:
            key = item["groupId"], item["artifactId"]
            selected_management = root_managed.get(key) or managed.get(key) or {}
            scope=item["scope"] if item["scope_declared"] else selected_management.get("scope",item["scope"])
            if scope not in ("compile", "runtime") or item["optional"] == "true" or item["type"] not in ("jar", "pom"):
                continue
            child_version = selected_management.get("version") or item["version"]
            if not child_version:
                raise RuntimeError(f"Missing original calculator dependency version {key}")
            try:
                visit((*key, child_version), excluded | item["exclusions"] | selected_management.get("exclusions",set()))
            except RuntimeError as failure:
                raise RuntimeError(f"{failure} (required by {g}:{a}:{v})") from failure
    visit(initial)
    return list(selected.values())


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


def remove_platform_method(source: str, marker: str) -> str:
    """Remove an Android-only method while retaining its canonical class data."""
    while marker in source:
        start = source.index(marker)
        # These canonical glue signatures contain no function-valued parameters.
        end = source.index(")", start) + 1
        while source[end].isspace():
            end += 1
        if source[end] == "=":
            end = source.index("\n", end)
        elif source[end] == "{":
            depth, quoted, escaped = 1, False, False
            end += 1
            while depth:
                char = source[end]
                if quoted:
                    if escaped:
                        escaped = False
                    elif char == "\\":
                        escaped = True
                    elif char == '"':
                        quoted = False
                elif char == '"':
                    quoted = True
                elif char == "{":
                    depth += 1
                elif char == "}":
                    depth -= 1
                end += 1
        else:
            raise ValueError(f"Unknown canonical Android method boundary: {marker}")
        source = source[:start] + source[end:]
    return source


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
    model = (APP / "math/MathExpressionModel.kt").read_text(encoding="utf-8-sig")
    model = exact(model, "import com.learngraphicsprogramming.app.R\n", "")
    model = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', model)
    sources.append(generated("MathExpressionModel.kt", model))

    # Reuse the actual structured-key dispatcher from the canonical Android
    # editor. Only its Android action objects become named RPC actions; tree
    # editing, wrapping, slot selection and rollback remain the original code.
    editor = (APP / "math/MathExpressionEditor.kt").read_text(encoding="utf-8-sig")
    first = editor.index("        val previousDraft = model.saveDraft()")
    last = editor.index("        refresh()", first)
    actions = editor[first:last]
    actions = exact(actions, "        when (action) {", "        when (action) {")
    actions = re.sub(r"is MathKeyboardAction\.([A-Za-z]+)", r'"\1"', actions)
    actions = re.sub(r"MathKeyboardAction\.([A-Za-z]+)", r'"\1"', actions)
    actions = actions.replace("action.name", "name").replace("action.label", "label")
    actions = actions.replace("        if (!accepted) model.restoreDraft(previousDraft)",
        "        if (!accepted) model.restoreDraft(previousDraft)\n        return accepted")
    action_source = '''package lgaed.narration.windows
import com.learngraphicsprogramming.app.math.*
internal object WindowsMathActions {
    fun apply(model: MathExpressionModel, action: String, name: String, label: String,
              expectedTemplate: MathExpression.Sequence?): Boolean {
        fun template(predicate: (MathExpression) -> Boolean): MathExpression? {
            fun find(node: MathExpression): MathExpression? {
                if (predicate(node)) return node
                val children = if (node is MathExpression.Sequence) node.children else node.childSlots().map { it.second }
                return children.firstNotNullOfOrNull(::find)
            }
            return expectedTemplate?.let(::find)
        }
'''+actions+'''    }
}
'''
    sources.append(generated("WindowsMathActions.kt", action_source))
    # Keyboard identity, labels, localized descriptions, shortcut mapping and
    # shape-only previews are shared unchanged. Only EditText application belongs
    # to Android; the canonical model dispatcher above performs Windows edits.
    keyboard = (APP / "math/MathKeyboardAction.kt").read_text(encoding="utf-8-sig")
    keyboard = exact(keyboard, "import android.widget.EditText\n", "")
    keyboard = exact(keyboard, "import com.learngraphicsprogramming.app.R\n", "")
    keyboard = exact(keyboard, "    fun localizedDescription(context: android.content.Context): String = description?.resolve(context) ?: contentDescription\n", "")
    keyboard = re.sub(r"    abstract fun apply\([\s\S]*?\n    \)\n", "", keyboard, count=1)
    keyboard = remove_platform_method(keyboard, "override fun apply(")
    keyboard = remove_platform_method(keyboard, "private fun replaceSelection(")
    keyboard = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', keyboard)
    sources.append(generated("MathKeyboardAction.kt", keyboard))
    sources.append(APP / "math/MathKeyboardActionPreviews.kt")
    for name in ("CalculatorExpressionCompiler", "MathToolService", "MathCalculator", "CalculatorKeyOrder"):
        code = (APP / "calculator" / f"{name}.kt").read_text(encoding="utf-8-sig")
        if "import com.learngraphicsprogramming.app.R\n" in code:
            code = exact(code, "import com.learngraphicsprogramming.app.R\n", "")
        code = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', code)
        if name == "MathCalculator":
            code = exact(code, "        fun resolveMessage(context: android.content.Context): String = uiMessage?.resolve(context) ?: message\n", "")
        if name == "MathToolService":
            # Symja's per-engine diagnostics belong to stderr. stdout is the
            # concurrent JSONL protocol, and redirecting System.out globally
            # would corrupt unrelated narration/editor responses.
            code = exact(code, "        val engine = evaluator.evalEngine\n", "        val engine = evaluator.evalEngine\n        engine.setOutPrintStream(System.err)\n        engine.setErrorPrintStream(System.err)\n")
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

    state = (APP / "math/MathPuzzleState.kt").read_text(encoding="utf-8-sig")
    state = exact(state,"import org.json.JSONObject","import lgaed.narration.androidjson.JSONObject")
    state = state[:state.index("    fun isCurrentCorrect(")] + "}\n"
    sources.append(generated("MathPuzzleState.kt",state))

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

    # Android's native verdict mark is presentation, not a spoken symbol. Reuse
    # its exact prefix rule; lesson extraction itself belongs to the native host.
    feedback = (APP / "narration/IncorrectFeedbackNarration.kt").read_text(encoding="utf-8-sig")
    # prepare() alone depends on Android lesson extraction. The two pure prefix
    # methods precede it in this canonical object; retain their source verbatim.
    feedback = feedback[:feedback.index("    fun prepare(")] + "}\n"
    sources.append(generated("IncorrectFeedbackNarration.kt", feedback))

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
    # Preserve existing speech dependency precedence. The calculator reuses the
    # already-installed, original Symja3.2 runtime graph without replacing the
    # tested speech jars or fetching unrelated cached libraries.
    runtime += [path for path in cached_dependency_closure("org.matheclipse", "matheclipse-core", "3.2.0") if path not in runtime]
    compiler = [cached_jar("org.jetbrains.kotlin", "kotlin-compiler-embeddable", "2.2.10"),
                runtime[0], cached_jar("org.jetbrains.kotlin", "kotlin-script-runtime", "2.2.10"),
                cached_jar("org.jetbrains.kotlin", "kotlin-reflect", "1.6.10"),
                cached_jar("org.jetbrains.kotlinx", "kotlinx-coroutines-core-jvm", "1.8.0"),
                cached_jar("org.jetbrains", "annotations", "23.0.0")]
    sources = host_sources()
    java_sources = sorted((ROOT / "native/narration/worker/androidjson").glob("*.java"))
    java_classes = OUT / "android-json-classes"
    java_stamp = OUT / "android-json-build.sha256"
    java_signature = "".join(digest(source) for source in java_sources)
    if not java_stamp.is_file() or java_stamp.read_text() != java_signature:
        java_classes.mkdir(parents=True,exist_ok=True)
        subprocess.run([str(java.parent / "javac.exe"),"-encoding","UTF-8","--release","17","-d",str(java_classes),
                        *map(str,java_sources)],check=True,cwd=ROOT)
        java_stamp.write_text(java_signature,encoding="ascii")
    jar = OUT / "kokoro-worker.jar"
    signature = hashlib.sha256(("\n".join(str(p) + ":" + digest(p) for p in sources + runtime)+java_signature).encode()).hexdigest()
    stamp = OUT / "worker-build.sha256"
    if not jar.is_file() or not stamp.is_file() or stamp.read_text() != signature:
        command = [str(java), "-cp", os.pathsep.join(map(str, compiler)),
            "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect", "-jvm-target", "17",
            "-classpath", os.pathsep.join(map(str, [*runtime,java_classes])), "-d", str(jar), *map(str, sources)]
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode:
            raise SystemExit(result.returncode)
        subprocess.run([str(java.parent / "jar.exe"),"uf",str(jar),"-C",str(java_classes),"."],check=True,cwd=ROOT)
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
    voice_catalog = (ROOT / "app/src/main/java/com/learngraphicsprogramming/app/narration/KokoroVoiceCatalog.kt").read_text(encoding="utf-8")
    languages = {"SPANISH": "es", "FRENCH": "fr", "AMERICAN_ENGLISH": "en-US", "BRITISH_ENGLISH": "en-GB"}
    voices = [{"id": name, "language": languages[language], "sha256": sha}
              for name, language, sha in re.findall(r'KokoroVoice\("([a-z_]+)",\s*NarrationLanguage\.([A-Z_]+),\s*"([0-9a-f]{64})"\)', voice_catalog)
              if language in languages]
    if len(voices) != 32:
        raise RuntimeError("The original Kokoro catalogue changed; check its explicit Windows language coverage")
    config = {"java": str(native_host), "javaExecutable": str(java), "jvm": str(jvm),
        "classpath": classpath, "arguments": arguments,
        "mainClass": "lgaed.narration.windows.WindowsNarrationWorkerKt",
        "assets": str(ROOT / "app/src/main/assets"), "resources": str(resources),
        "englishResources": str(english), "ffmpeg": ffmpeg or "",
        "originalWavs": str(original_wavs), "voices": voices}
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

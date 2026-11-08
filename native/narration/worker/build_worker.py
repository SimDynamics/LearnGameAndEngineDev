"""Build the Windows host for the existing Kotlin Kokoro frontend.

The canonical phonemizers/dictionary remain in app/src/main/java. Generated
sources under out/narration adapt Android platform glue and the Windows reading
rules below, without editing Android, model weights or curricular data.
"""
from __future__ import annotations

import argparse
from collections import deque
from contextlib import contextmanager
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
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/narration"))
from prepare_english_ai_pack import verify_staged_assets
from verify_windows_distribution import verify_runtime

APP = ROOT / "app/src/main/java/com/learngraphicsprogramming/app"
OUT = ROOT / "out/narration"
DEPS = ROOT / "out/deps"
MAVEN_CENTRAL = "https://repo.maven.apache.org/maven2/"
DEPENDENCY_LOCK = Path(__file__).with_name("dependencies.lock.json")
KOTLIN_VERSION = "2.4.21"
# Kotlin 2.4.21's highest supported bytecode/API target is 26, including when
# the complete build/runtime JDK is 27. Keep Java helpers on the same target.
# https://kotlinlang.org/docs/compiler-reference.html#jvm-target-version
JVM_TARGET = "26"
RUNTIME_ROOTS = [
    ("org.jetbrains.kotlin", "kotlin-stdlib", KOTLIN_VERSION),
    ("org.json", "json", "20260814"),
    ("com.google.code.gson", "gson", "2.15.0"),
    ("com.microsoft.onnxruntime", "onnxruntime", "1.31.1"),
    ("org.matheclipse", "matheclipse-core", "3.2.0"),
    ("org.jetbrains", "annotations", "26.1.0"),
    # Windows is independent of Android's graph. Align the maintained stable
    # releases consumed by Symja; resolve their own official POMs as well.
    ("com.google.errorprone", "error_prone_annotations", "2.51.0"),
    ("com.google.guava", "guava", "33.7.2-jre"),
    ("org.apache.commons", "commons-lang3", "3.21.0"),
    ("commons-codec", "commons-codec", "1.23.0"),
    ("org.logicng", "logicng", "2.6.2"),
    ("it.unimi.dsi", "fastutil-core", "8.5.19"),
    ("org.choco-solver", "choco-solver", "6.0.2"),
    ("com.esotericsoftware", "kryo", "5.7.1"),
    ("com.github.freva", "ascii-table", "1.13.0"),
    ("org.apache.logging.log4j", "log4j-api", "2.26.1"),
    ("com.fasterxml.jackson.core", "jackson-databind", "2.22.4"),
    ("com.fasterxml.jackson.core", "jackson-core", "2.22.4"),
    ("com.fasterxml.jackson.core", "jackson-annotations", "2.22"),
    ("io.pebbletemplates", "pebble", "4.1.2"),
    ("org.jspecify", "jspecify", "1.0.1"),
    ("args4j", "args4j", "2.37"),
    ("org.ehcache", "sizeof", "0.4.4"),
    ("org.objenesis", "objenesis", "3.6"),
    ("org.slf4j", "slf4j-api", "2.0.20"),
    ("org.jheaps", "jheaps", "0.16"),
]
COMPILER_ROOTS = [
    ("org.jetbrains.kotlin", "kotlin-compiler-embeddable", KOTLIN_VERSION),
    ("org.jetbrains.kotlin", "kotlin-stdlib", KOTLIN_VERSION),
    ("org.jetbrains.kotlin", "kotlin-reflect", KOTLIN_VERSION),
    ("org.jetbrains.kotlinx", "kotlinx-coroutines-core-jvm", "1.11.0"),
    ("org.jetbrains", "annotations", "26.1.0"),
]
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


class MavenResolver:
    """Resolve this worker's fixed Maven graph, independently of Android/Gradle.

    Normal builds accept only the checked-in SHA-256 lock. An explicit refresh
    validates Central's published checksums before producing the replacement
    lock. Breadth-first mediation follows Maven's nearest/first declaration;
    explicit roots align shared libraries across the complete classpath.
    """

    def __init__(self, cache: Path, hashes: dict[str, str] | None):
        self.cache = cache
        self.hashes = hashes
        self.observed: dict[str, str] = {}
        self.models: dict[tuple, dict] = {}
        self.loading: set[tuple] = set()

    @staticmethod
    def relative(coordinate, extension="pom", classifier=""):
        group, artifact, version = coordinate
        for value in (*coordinate, extension, classifier):
            if value and (not re.fullmatch(r"[A-Za-z0-9_.-]+", value) or value in (".", "..")):
                raise ValueError(f"Unsupported Maven coordinate component: {value!r}")
        if not version or "SNAPSHOT" in version.upper() or version.upper() in ("LATEST", "RELEASE"):
            raise ValueError(f"A fixed released Maven version is required: {coordinate}")
        suffix = "-" + classifier if classifier else ""
        return f"{group.replace('.', '/')}/{artifact}/{version}/{artifact}-{version}{suffix}.{extension}"

    @staticmethod
    def published_checksum(relative):
        # Central requires SHA-1; newer publishers also supply SHA-256/512.
        # The resulting lock always stores SHA-256 of the verified bytes.
        for algorithm in ("sha256", "sha512", "sha1"):
            try:
                with urllib.request.urlopen(MAVEN_CENTRAL + relative + "." + algorithm, timeout=90) as source:
                    value = source.read().decode("ascii").strip().split()[0].lower()
            except urllib.error.HTTPError as failure:
                if failure.code == 404:
                    continue
                raise
            if not re.fullmatch(r"[0-9a-f]{%d}" % (hashlib.new(algorithm).digest_size * 2), value):
                raise ValueError(f"Invalid published {algorithm} for {relative}")
            return algorithm, value
        raise RuntimeError(f"Central has no published checksum for {relative}")

    def fetch(self, coordinate, extension="pom", classifier="") -> Path:
        relative = self.relative(coordinate, extension, classifier)
        path = self.cache / relative
        expected = None if self.hashes is None else self.hashes.get(relative)
        if self.hashes is not None and not expected:
            raise RuntimeError(f"Dependency is absent from the Windows SHA-256 lock: {relative}")
        published = self.published_checksum(relative) if self.hashes is None else None

        def verify(candidate):
            sha256 = digest(candidate)
            if expected and sha256 != expected:
                raise ValueError(f"Windows dependency failed locked SHA-256; left untouched: {candidate}")
            if published:
                algorithm, checksum = published
                with candidate.open("rb") as stream:
                    actual = hashlib.file_digest(stream, algorithm).hexdigest()
                if actual != checksum:
                    raise ValueError(f"Windows dependency failed published {algorithm}; left untouched: {candidate}")
            self.observed[relative] = sha256

        if path.is_file():
            verify(path)
            return path
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, suffix=".part", delete=False) as output:
            partial = Path(output.name)
            try:
                with urllib.request.urlopen(MAVEN_CENTRAL + relative, timeout=90) as source:
                    shutil.copyfileobj(source, output, 1024 * 1024)
            except BaseException:
                output.close()
                partial.unlink(missing_ok=True)
                raise
        try:
            verify(partial)
            # The checkout build lock serializes normal builds; never replace an
            # existing cache entry if a separate dependency refresh won a race.
            if path.exists():
                verify(path)
            else:
                partial.rename(path)
        finally:
            partial.unlink(missing_ok=True)
        return path

    def model(self, coordinate):
        if coordinate in self.models:
            return self.models[coordinate]
        if coordinate in self.loading:
            raise RuntimeError(f"Cyclic Maven parent/BOM: {coordinate}")
        self.loading.add(coordinate)
        try:
            tree = ET.parse(self.fetch(coordinate)).getroot()
            for element in tree.iter():
                element.tag = element.tag.rsplit("}", 1)[-1]
            if tree.find("distributionManagement/relocation") is not None:
                raise RuntimeError(f"Maven relocation requires explicit review: {coordinate}")
            for profile in tree.findall("profiles/profile"):
                conditional = profile.findall("dependencies/dependency") + profile.findall("dependencyManagement/dependencies/dependency")
                if profile.find("activation") is not None and any(
                    item.findtext("scope", "compile") not in ("test", "provided", "system") and
                    item.findtext("optional", "false") != "true" for item in conditional
                ):
                    raise RuntimeError(f"Conditional Maven dependencies require explicit review: {coordinate}")
            inherited = {"raw_properties": {}, "raw_managed": [], "raw_dependencies": []}
            parent = tree.find("parent")
            if parent is not None:
                inherited = self.model(tuple(parent.findtext(key, "") for key in ("groupId", "artifactId", "version")))
            group, artifact, version = coordinate
            raw_properties = {**inherited["raw_properties"],
                **{child.tag: child.text or "" for child in tree.findall("properties/*")}}
            properties = {**raw_properties, "project.groupId": group, "project.artifactId": artifact,
                "project.version": version, "pom.groupId": group, "pom.artifactId": artifact, "pom.version": version}
            if parent is not None:
                properties.update({f"project.parent.{key}": parent.findtext(key, "")
                                   for key in ("groupId", "artifactId", "version")})

            def expand(value):
                for _ in range(40):
                    changed = re.sub(r"\$\{([^}]+)\}", lambda found: properties.get(found[1], found[0]), value)
                    if changed == value:
                        break
                    value = changed
                if "${" in value:
                    raise RuntimeError(f"Unresolved Maven value {value!r} in {coordinate}")
                return value

            def dependency(element):
                result = {key: expand(element.findtext(key, "")) for key in
                          ("groupId", "artifactId", "version", "scope", "type", "classifier", "optional")}
                result["exclusions"] = frozenset((expand(item.findtext("groupId", "")),
                                                  expand(item.findtext("artifactId", "")))
                                                 for item in element.findall("exclusions/exclusion"))
                result["key"] = (result["groupId"], result["artifactId"], result["type"] or "jar", result["classifier"])
                return result

            raw_managed = inherited["raw_managed"] + tree.findall("dependencyManagement/dependencies/dependency")
            managed, imports = {}, []
            for element in raw_managed:
                item = dependency(element)
                if item["type"] == "pom" and item["scope"] == "import":
                    imports.append(item)
                else:
                    managed[item["key"]] = item
            for item in imports:
                for key, value in self.model((item["groupId"], item["artifactId"], item["version"]))["managed"].items():
                    managed.setdefault(key, value)
            raw_dependencies = inherited["raw_dependencies"] + tree.findall("dependencies/dependency")
            dependencies = {}
            for element in raw_dependencies:
                # Test-only versions can depend on a publisher's test profiles;
                # they are outside the worker's runtime graph and need no JAR.
                if expand(element.findtext("scope", "")) in ("test", "provided", "system"):
                    continue
                item = dependency(element)
                dependencies[item["key"]] = item
            result = {"raw_properties": raw_properties, "raw_managed": raw_managed,
                "raw_dependencies": raw_dependencies, "managed": managed,
                "dependencies": list(dependencies.values()), "packaging": tree.findtext("packaging", "jar")}
            self.models[coordinate] = result
            return result
        finally:
            self.loading.remove(coordinate)

    def closure(self, roots) -> list[Path]:
        roots = [tuple(item) for item in roots]
        versions = {(group, artifact): version for group, artifact, version in roots}
        if len(versions) != len(roots):
            raise ValueError("Duplicate Windows Maven roots")
        queue = deque((item, frozenset(), self.model(item)["managed"]) for item in roots)
        selected, visited, jars = {}, set(), []
        while queue:
            coordinate, excluded, root_management = queue.popleft()
            group, artifact, version = coordinate
            key = group, artifact
            if any((g in (group, "*") and a in (artifact, "*")) for g, a in excluded):
                continue
            version = versions.get(key, version)
            coordinate = group, artifact, version
            if key in selected and selected[key] != version:
                continue
            visit = coordinate, excluded, id(root_management)
            if visit in visited:
                continue
            visited.add(visit)
            model = self.model(coordinate)
            if key not in selected:
                selected[key] = version
                if model["packaging"] in ("jar", "bundle"):
                    jars.append(self.fetch(coordinate, "jar"))
                elif model["packaging"] != "pom":
                    raise RuntimeError(f"Unsupported runtime packaging {model['packaging']}: {coordinate}")
            for item in model["dependencies"]:
                management = root_management.get(item["key"], model["managed"].get(item["key"], {}))
                scope = item["scope"] or management.get("scope") or "compile"
                optional = item["optional"] or management.get("optional") or "false"
                if scope not in ("compile", "runtime") or optional == "true":
                    continue
                if item["classifier"] or (item["type"] and item["type"] not in ("jar", "pom")):
                    raise RuntimeError(f"Unsupported runtime Maven artifact: {item['key']}")
                child_key = item["groupId"], item["artifactId"]
                child_version = versions.get(child_key) or management.get("version") or item["version"]
                if not child_version:
                    raise RuntimeError(f"Missing runtime Maven version {child_key} required by {coordinate}")
                queue.append(((*child_key, child_version), excluded | item["exclusions"] |
                              management.get("exclusions", frozenset()), root_management))
        return jars


def worker_dependencies(*, refresh=False):
    roots = {"runtime": RUNTIME_ROOTS, "compiler": COMPILER_ROOTS}
    lock = None if refresh else json.loads(DEPENDENCY_LOCK.read_text(encoding="utf-8"))
    if lock is not None and (lock.get("repository") != MAVEN_CENTRAL or lock.get("roots") !=
                             {key: [list(item) for item in value] for key, value in roots.items()}):
        raise ValueError("Windows Maven roots changed; explicitly refresh the dependency lock")
    resolver = MavenResolver(DEPS / "windows-maven", None if refresh else lock["sha256"])
    resolved = {key: resolver.closure(value) for key, value in roots.items()}
    if refresh:
        content = {"repository": MAVEN_CENTRAL, "roots": roots, "sha256": dict(sorted(resolver.observed.items()))}
        pending = DEPENDENCY_LOCK.with_suffix(".json.part")
        try:
            pending.write_text(json.dumps(content, indent=2) + "\n", encoding="utf-8", newline="\n")
            pending.replace(DEPENDENCY_LOCK)
        finally:
            pending.unlink(missing_ok=True)
    return resolved


def cached_jar(group: str, artifact: str, version: str) -> Path:
    """Compatibility accessor for fixtures, using only the Windows hash lock."""
    lock = json.loads(DEPENDENCY_LOCK.read_text(encoding="utf-8"))
    return MavenResolver(DEPS / "windows-maven", lock["sha256"]).fetch((group, artifact, version), "jar")


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


def windows_segment_contract(name: str, source: str) -> str:
    """Opt-in host extension; canonical Android sources keep their original types.

    The new construction uses the existing typed Graph answer, codec and stable
    state. Exact anchors preserve all existing parser/validator branches.
    """
    if name == "MathPuzzleSpec":
        source = exact(source, '"math_geometry_builder", "math_rigid_angle_copy"',
                       '"math_geometry_builder", "math_rigid_angle_copy", "math_segment_transport"')
        source = exact(source, '        "rigid_angle_copy" -> "rigid_angle"',
                       '        "rigid_angle_copy" -> "rigid_angle"\n        "segment_transport" -> "segment_transport"')
        source = exact(source, '            "math_rigid_angle_copy" -> "rigid_angle"',
                       '            "math_rigid_angle_copy" -> "rigid_angle"\n            "math_segment_transport" -> "segment_transport"')
        source = exact(source, '            "rigid_angle" -> {', '''            "segment_transport" -> {
                require(validatorType == "segment_transport") { "A segment construction requires its own validator." }
                lgaed.narration.windows.WindowsSegmentTransportSpec.parse(validator)
            }
            "rigid_angle" -> {''')
    elif name == "MathValidators":
        source = exact(source, '        register("rigid_angle_copy", ::rigidAngle)',
                       '        register("rigid_angle_copy", ::rigidAngle)\n        register("segment_transport", lgaed.narration.windows.WindowsSegmentTransport::validate)')
        source = exact(source, '            "rigid_angle_copy" -> RigidAngleCopySpec.parse(spec)',
                       '            "rigid_angle_copy" -> RigidAngleCopySpec.parse(spec)\n            "segment_transport" -> lgaed.narration.windows.WindowsSegmentTransportSpec.parse(spec)')
        source = exact(source, '"coordinates", "geometry", "rigid_angle_copy")',
                       '"coordinates", "geometry", "rigid_angle_copy", "segment_transport")')
    return source


def math_host_sources() -> list[Path]:
    """Compile original evaluators; only resource resolution is platform-specific."""
    names = ("MathPuzzleSpec", "MathValidators", "MathAnswer", "MathShortcutResolver",
             "MathExpressionSemantics", "ParametricRationalExpression", "RigidAngleCopy",
             "EuclideanDiagram", "RigidAngleLabelLayout", "MathPuzzleFeedback", "MathInputMode")
    sources = []
    for name in names:
        path = APP / "math" / f"{name}.kt"
        code = path.read_text(encoding="utf-8-sig")
        if "import com.learngraphicsprogramming.app.R\n" in code:
            code = exact(code, "import com.learngraphicsprogramming.app.R\n", "")
        code = re.sub(r"R\.string\.([a-z_]+)", r'"\1"', code)
        if name == "MathValidators":
            code = exact(code, "    fun resolveMessage(context: android.content.Context): String = uiMessage?.resolve(context) ?: message\n", "")
        code = windows_segment_contract(name, code)
        sources.append(generated(f"{name}.kt", code))
    # Reuse Android's exact implicit-formula detector and parser. Only the
    # Spannable drawing sink becomes UTF-16 ranges plus the original AST;
    # neither detection expressions nor their priority are reimplemented.
    typesetter = (APP / "math/MathTextView.kt").read_text(encoding="utf-8-sig")
    typesetter = typesetter[typesetter.index("    private val numericFraction"):typesetter.index("\nprivate class MathFormulaSpan")]
    typesetter = remove_platform_method(typesetter, "    fun apply(")
    typesetter = exact(typesetter, 'fun render(value: String, mode: String = "auto"): CharSequence {',
        'fun render(value: String, mode: String = "auto"): WindowsMathTextOutput {')
    typesetter = exact(typesetter, 'if (mode == "text" || value.isBlank()) return value',
        'if (mode == "text" || value.isBlank()) return WindowsMathTextOutput()')
    typesetter = exact(typesetter, "val output = SpannableStringBuilder()", "val output = WindowsMathTextOutput()")
    typesetter = exact(typesetter, "fun appendMath(source: String, content: Boolean = false) {",
        "fun appendMath(source: String, content: Boolean = false, sourceStart: Int = 0, sourceEnd: Int = sourceStart + source.length) {")
    typesetter = exact(typesetter, "            val start = output.length\n", "")
    typesetter = exact(typesetter,
        "            output.setSpan(MathFormulaSpan(expression, MathReadingAtoms.flat(source, expression)), start, output.length,\n                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)",
        "            val literal = expression.children.singleOrNull().let { it is MathExpression.Symbol && it.value == source }\n            output.fragments += WindowsMathFragment(sourceStart, sourceEnd, if (literal) source else normalizeExpression(source), literal)")
    typesetter = exact(typesetter, "fun appendContent(source: String) {", "fun appendContent(source: String, sourceStart: Int) {\n            var lineStart = sourceStart")
    typesetter = exact(typesetter, "appendMath(line, content = true)", "appendMath(line, content = true, sourceStart = lineStart)\n                lineStart += line.length + 1")
    typesetter = exact(typesetter, "appendContent(value.substring(contentStart, index))", "appendContent(value.substring(contentStart, index), contentStart)")
    typesetter = exact(typesetter, "appendContent(value.substring(contentStart))", "appendContent(value.substring(contentStart), contentStart)")
    typesetter = exact(typesetter, "appendMath(value.substring(index + delimiters.first.length, end).let { if (display) it.trim() else it })",
        "val source = value.substring(index + delimiters.first.length, end)\n                    appendMath(if (display) source.trim() else source, sourceStart = index, sourceEnd = end + delimiters.second.length)", count=2)
    typesetter = exact(typesetter, "appendMath(candidate.value)", "appendMath(candidate.value, sourceStart = index)")
    typesetter = exact(typesetter, "appendMath(value[index].toString())", "appendMath(value[index].toString(), sourceStart = index)")
    # Keep the original normalization independently of the serializer: serializing
    # the AST would turn visible '×' into '\\cdot', and Unicode minus into '-'.
    # The native projection must retain the same original displayed operators.
    method = typesetter.index("    internal fun parseExpression(")
    normalization_start = typesetter.index("        // A coordinate separator", method)
    normalization_end = typesetter.index("        return when (val result =", normalization_start)
    normalization = typesetter[normalization_start:normalization_end]
    typesetter = typesetter[:normalization_start] + "        val normalized = normalizeExpression(source)\n" + typesetter[normalization_end:]
    typesetter = typesetter[:method] + "    internal fun normalizeExpression(source: String): String {\n" + normalization + "        return normalized\n    }\n\n" + typesetter[method:]
    typesetter_source = '''package com.learngraphicsprogramming.app.math
internal data class WindowsMathFragment(val start: Int, val end: Int, val formula: String, val literal: Boolean)
internal class WindowsMathTextOutput : CharSequence {
    private val display = StringBuilder()
    val fragments = mutableListOf<WindowsMathFragment>()
    override val length get() = display.length
    override fun get(index: Int) = display[index]
    override fun subSequence(startIndex: Int, endIndex: Int) = display.subSequence(startIndex, endIndex)
    fun append(value: String) { display.append(value) }
    fun append(value: Char) { display.append(value) }
    fun append(value: String, start: Int, end: Int) { display.append(value, start, end) }
}
internal object WindowsMathTypesetting {
'''+typesetter
    sources.append(generated("WindowsMathTypesetting.kt", typesetter_source))
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
                if not name.startswith(("math_", "exercise_")) and name != "lesson_note":
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
        WindowsSegmentTransport.message(code,language)?.let { return it }
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


def windows_reading_source(name: str, code: str) -> str:
    """Use the canonical parser and provenance in both foreground/background plans.

    Only generated Windows sources change. Exact anchors fail closed if Android's
    implementation moves, rather than silently leaving one preparation path stale.
    """
    if name == "ManualEnglishTerms":
        code = exact(code, 'object ManualEnglishTerms {', '''object ManualEnglishTerms {
    // Keep the historical dictionary/parser exact, but require actual code
    // syntax or closed authored delimiters before applying its English voice.
    // Nominal prose cues and a spelling's PROSE permission are not authorship.
    private fun currentSelection(source: String, selected: List<Match>, mathMode: String): List<Match> {
        if (WindowsNarrationSpeechRules.keepPreviousProseInference() || selected.isEmpty()) return selected
        val code = SpanishTechnicalSpeech.syntacticCodeRanges(source, mathMode)
        return selected.filter { match ->
            code.any { match.start >= it.first && match.endExclusive <= it.last + 1 } ||
                templateContext(match.entry, source.substring((match.start - 100).coerceAtLeast(0), match.start),
                    source.substring(match.endExclusive, (match.endExclusive + 100).coerceAtMost(source.length)))
        }
    }
    fun select(source: String, mathMode: String = "auto"): List<Match> =
        currentSelection(source, selectPrevious(source, mathMode), mathMode)
    fun selectFrench(source: String, mathMode: String = "auto"): List<Match> =
        currentSelection(source, selectFrenchPrevious(source, mathMode), mathMode)
''')
        code = exact(code, '    fun select(source: String, mathMode: String = "auto"): List<Match> {',
                     '    private fun selectPrevious(source: String, mathMode: String = "auto"): List<Match> {')
        code = exact(code, '    fun selectFrench(source: String, mathMode: String = "auto"): List<Match> {',
                     '    private fun selectFrenchPrevious(source: String, mathMode: String = "auto"): List<Match> {')
    elif name == "SpanishTechnicalSpeech":
        code = exact(code, 'object SpanishTechnicalSpeech {', '''object SpanishTechnicalSpeech {
    // Reuse the canonical syntax parser for the current lexical voice filter.
    // Prose-only names, known-word lists and open backticks supply no code range.
    internal fun syntacticCodeRanges(source: String, mathMode: String): List<IntRange> {
        val ranges = mutableListOf<IntRange>()
        var index = 0
        while (index < source.length) {
            if (source[index] == '`') {
                val delimiter = if (source.startsWith("```", index)) "```" else "`"
                val end = source.indexOf(delimiter, index + delimiter.length)
                if (end >= 0) {
                    ranges += index + delimiter.length until end
                    index = end + delimiter.length
                    continue
                }
            }
            val math = mathEnd(source, index)
            if (math != null) { index = math; continue }
            val address = url.matchAt(source, index)
            if (address != null) { index = address.range.last + 1; continue }
            if (source[index] == '\\'' || source[index] == '"') {
                val end = quotedEnd(source, index)
                if (end != null) { index = end; continue }
            }
            // A data record's braces alone do not make every word inside it
            // source code. Declarations/calls inside a real body still match.
            if (source[index] == '{' || source[index] == '[') { index++; continue }
            val end = codeEnd(source, index, mathMode) ?: WindowsNarrationSpeechRules.keywordStatementEnd(source, index)
            if (end != null && end > index) { ranges += index until end; index = end }
            else index++
        }
        return ranges
    }
    internal fun semanticCode(source: String, onPiece: (Int, Int, String) -> Unit): String {
        val selected = ManualEnglishTerms.select("`" + source + "`", "cpp")
            .map { it.copy(start = it.start - 1, endExclusive = it.endExclusive - 1) }.associateBy { it.start }
        onPiece(0, 0, ManualEnglishSpeech.PREPARED.toString())
        return ManualEnglishSpeech.PREPARED + readCode(source, 0, selected, onPiece)
    }
''')
        code = exact(code, '        val identifier = technicalIdentifier.matchAt(text, index)',
                     '        val identifier = if (WindowsNarrationSpeechRules.keepPreviousProseInference())\n'
                     '            technicalIdentifier.matchAt(text, index) else null')
        code = exact(code, '                \'_\' -> "guion bajo"',
                     '                \'_\' -> if (WindowsNarrationSpeechRules.keepPreviousProseInference()) "guion bajo" else null')
        # Bound only implicit source detection. Explicit code, identifier speech
        # and onPiece source ownership continue through the canonical reader.
        declaration = '    private val declaration = Regex("(?:(?:const|unsigned|signed|long|short)\\\\s+)*(?:int|double|float|bool|char|auto|void|long|short)(?:\\\\s*[*&]+(?:\\\\s*[A-Za-z_][A-Za-z_0-9]*)?|\\\\s+[A-Za-z_][A-Za-z_0-9]*(?=\\\\s*[({=;]))")'
        code = exact(code, declaration, declaration.replace('[A-Za-z_]', '[\\\\p{L}_]').replace('[A-Za-z_0-9]', '[\\\\p{L}\\\\p{M}\\\\p{N}_]'))
        code = exact(code,
                     '    private val statementAssignment = Regex("[A-Za-z_][A-Za-z_0-9]*',
                     '    private val statementAssignment = Regex("[\\\\p{L}_][\\\\p{L}\\\\p{M}\\\\p{N}_]*')
        code = exact(code,
                     "        statementAssignment.matchAt(text, index)?.let { return it.range.last + 1 }",
                     "        statementAssignment.matchAt(text, index)?.let {\n"
                     "            WindowsNarrationSpeechRules.implicitStatementAssignmentEnd(text, index)?.let { end -> return end }\n"
                     "        }")
        code = exact(code, '''    private fun expressionEnd(text: String, index: Int): Int {
        var end = index
        while (end < text.length) {
            val char = text[end]
            if (char == ';') return end + 1
            if (char in "\\n:!?" || char == '.' && text.getOrNull(end + 1)?.isDigit() != true) break
            if (char == '\\'' || char == '"') {
                val quoteEnd = quotedEnd(text, end) ?: break
                end = quoteEnd
                continue
            }
            // Stop at a prose conjunction after the operands instead of reading
            // the remainder of the explanatory sentence as source code.
            if (end > index && proseJoiner.matchAt(text, end) != null) break
            end++
        }
        return end
    }''', '''    private fun expressionEnd(text: String, index: Int): Int =
        WindowsNarrationSpeechRules.implicitExpressionEnd(text, index)''')
        # Display styling does not make an expected output label source code.
        code = exact(code, '                    val code = text.substring(index + delimiter.length, end)', '''                    val code = text.substring(index + delimiter.length, end)
                    if (delimiter.length == 1 && WindowsNarrationSpeechRules.isDisplayedOutputRecord(text, index, code)) {
                        output.append(' ')
                        onPiece?.invoke(index, index + delimiter.length, " ")
                        output.append(readCode(code, index + delimiter.length, selected, onPiece, numericExpression = true))
                        output.append(' ')
                        onPiece?.invoke(end, end + delimiter.length, " ")
                        observedChildren = onPiece != null
                        index = end + delimiter.length
                        changed = true
                        continue
                    }
                    if (delimiter.length == 1 && (WindowsNarrationSpeechRules.isDisplayedOutputValue(text, index, code) ||
                            WindowsNarrationSpeechRules.isDisplayedOutputPhrase(text, index, code))) {
                        // Visible labels retain their established pause and keys.
                        output.append(' ')
                        onPiece?.invoke(index, index + delimiter.length, " ")
                        code.forEachIndexed { offset, character ->
                            val spoken = if (character == ':') ", " else character.toString()
                            output.append(spoken)
                            onPiece?.invoke(index + delimiter.length + offset, index + delimiter.length + offset + 1, spoken)
                        }
                        output.append(' ')
                        onPiece?.invoke(end, end + delimiter.length, " ")
                        observedChildren = onPiece != null
                        index = end + delimiter.length
                        changed = true
                        continue
                    }''')
        # Recognize author/year citations before local technical detection.
        # The same production pass serves ordinary speech and word ownership.
        code = exact(code, "            val technicalEnd = codeEnd(text, index)", '            val citationEnd = WindowsNarrationSpeechRules.proseCitationEnd(text, index)\n            if (citationEnd != null) {\n                // Spoken punctuation only: a short opening pause, no bracket\n                // names. Keep the author\'s words/year and their source owners.\n                val content = text.substring(index + 1, citationEnd - 1)\n                output.append(", ").append(content)\n                if (onPiece != null) {\n                    onPiece(index, index + 1, ", ")\n                    onPiece(index + 1, citationEnd - 1, content)\n                    onPiece(citationEnd - 1, citationEnd, "")\n                    observedChildren = true\n                }\n                index = citationEnd\n                changed = true\n                continue\n            }\n            val technicalEnd = codeEnd(text, index)')
        code = exact(code, "            if (text.getOrNull(next) in listOf('(', '{', '[')) {",
                     "            if (text.getOrNull(next) in listOf('(', '{', '[') &&\n"
                     "                WindowsNarrationSpeechRules.proseCitationEnd(text, next) == null) {")
        # Windows-only labels; retain canonical parsing and source ownership.
        code = exact(code, '                    result += "abre $name"', '                    result += if (char == \'"\') name else "abre $name"')
        code = exact(code, '                    result += "cierra $name"', '                    result += if (char == \'"\') name else "cierra $name"')
        code = exact(code, '    private val codeOperators = listOf(', '    private val codeOperators = listOf(\n        "//" to "doble diagonal",')
        code = exact(code, 'text[index] == \'"\' && (inside.contains(\'\\\\\')', 'text[index] == \'"\' && (inside.isEmpty() || inside == "//" || inside.contains(\'\\\\\')')
        code = exact(code, '    private fun readLiteral(source: String): String = buildList {\n        var index = 0\n        while (index < source.length) {\n            val char = source[index]', '    private fun readLiteral(source: String): String = buildList {\n        var index = 0\n        while (index < source.length) {\n            val char = source[index]\n            if (source.startsWith("//", index)) {\n                add("doble diagonal"); index += 2; continue\n            }')
        code = exact(code, "            selected[index]?.let { match ->", """            if (WindowsNarrationSpeechRules.isIsolatedSymbolEscape(text, index)) {
                output.append(if (text[index + 1] == '"') "barra inversa comilla" else "barra inversa barra inversa")
                index += 2
                changed = true
                continue
            }
            if (WindowsNarrationSpeechRules.isNamedLetter(text, index)) {
                val letter = text[index].toString()
                val name = readIdentifier(letter)
                output.append(name)
                if (name != letter) changed = true
                index++
                continue
            }
            selected[index]?.let { match ->""")
        code = exact(code, "                    add(codeSymbols[next] ?: readIdentifier(next.toString()))",
                     """                    add(if (next == '"') "comilla" else codeSymbols[next] ?: readIdentifier(next.toString()))""")
        # Only an explicitly typed Windows C++ puzzle owns these bare numeric
        # expressions. Other prose and explicit mathematical modes keep their
        # existing detectors, source owners and recording identities.
        code = exact(code, 'val pureMath = !forcedMath &&', 'val pureMath = mathMode !in listOf("cpp", "cpp_previous_parentheses") && !forcedMath &&')
        code = exact(code, 'private fun codeEnd(text: String, index: Int): Int? {',
            'private fun codeEnd(text: String, index: Int, mathMode: String): Int? {\n'
            '        if (mathMode in listOf("cpp", "cpp_previous_parentheses")) WindowsNarrationSpeechRules.cppNumericExpressionEnd(text, index)?.let { return it }')
        code = exact(code, 'val technicalEnd = codeEnd(text, index)', 'val technicalEnd = codeEnd(text, index, mathMode)')
        code = exact(code, 'readCode(text.substring(index, technicalEnd), index, selected, onPiece)',
            'readCode(text.substring(index, technicalEnd), index, selected, onPiece,\n'
            '                    numericExpression = mathMode in listOf("cpp", "cpp_previous_parentheses") && WindowsNarrationSpeechRules.cppNumericExpressionEnd(text, index) == technicalEnd)')
        code = exact(code, '''        selected: Map<Int, ManualEnglishTerms.Match> = emptyMap(),
        onPiece: ((Int, Int, String) -> Unit)? = null
    ): String {''', '''        selected: Map<Int, ManualEnglishTerms.Match> = emptyMap(),
        onPiece: ((Int, Int, String) -> Unit)? = null,
        numericExpression: Boolean = false
    ): String {''')
        code = exact(code, '            val name = when (char) {',
            '            val name = when (char) {\n                \'=\' -> if (numericExpression) "igual a" else codeSymbols[char]\n'
            '                \':\' -> if (numericExpression && WindowsNarrationSpeechRules.isOutputLabelColon(source, index)) "," else codeSymbols[char]')
        code = exact(code, '            val symbol = when (text[index]) {',
            '            val symbol = when (text[index]) {\n'
            '                \'(\', \')\' -> if (mathMode == "cpp" &&\n'
            '                    WindowsNarrationSpeechRules.isIsolatedCppParenthesisMention(text, index)) codeSymbols[text[index]] else null')
    elif name == "NarrationWordMappings":
        code = exact(code, 'listOf("auto", "text", "inline", "block")',
                     'listOf("auto", "text", "inline", "block", "cpp", "cpp_previous_parentheses")')
    elif name == "LessonNarrationText":
        code = exact(code, '            "heading", "paragraph" -> append(output, block.string("text"), block.mode())',
                     '            "image" -> if (WindowsNarrationSpeechRules.keepWholeReadingRegions()) {\n'
                     '                append(output,block.string("alt"))\n'
                     '                append(output,block.string("caption"))\n'
                     '            }\n'
                     '            "heading", "paragraph" -> append(output, block.string("text"), block.mode())')
        # Only current Windows anticipation selects the exact sources shown by
        # native controls. Historical export keeps its preceding extractor.
        code = exact(code, '''        when {
            segments != null -> {''', '''        if (WindowsNarrationSpeechRules.keepWholeReadingRegions()) {
            lgaed.narration.windows.WindowsTextSemantics.nativeFieldSources(response).forEach { append(output,it) }
        } else when {
            segments != null -> {''')
        code = exact(code, '        if (completeVisibleLabels && (segments != null || table != null)) {',
                     '        if (!WindowsNarrationSpeechRules.keepWholeReadingRegions() && completeVisibleLabels && (segments != null || table != null)) {')
        code = exact(code, '''        if (values == null) return
        val spoken = (0 until minOf(values.length(), limit)).map { scalar(values.opt(it)) }''', '''        if (values == null) return
        if (WindowsNarrationSpeechRules.keepWholeReadingRegions()) {
            for (index in 0 until minOf(values.length(),limit)) append(output,scalar(values.opt(index)))
            return
        }
        val spoken = (0 until minOf(values.length(), limit)).map { scalar(values.opt(it)) }''')
        code = exact(code, '''            val label = if (completeVisibleLabels)
                item.optString("label", item.optString("text", item.optString("id")))''', '''            val label = if (WindowsNarrationSpeechRules.keepWholeReadingRegions())
                lgaed.narration.windows.WindowsSemanticFields.visibleCardSource(item)
                else if (completeVisibleLabels)
                item.optString("label", item.optString("text", item.optString("id")))''')
        code = exact(code, '''            "text" -> append(output, if (completeVisibleLabels) config.optString("text", config.optString("value"))''', '''            "text" -> append(output, if (WindowsNarrationSpeechRules.keepWholeReadingRegions())
                lgaed.narration.windows.WindowsSemanticFields.visibleRepresentationSource(config)
                else if (completeVisibleLabels) config.optString("text", config.optString("value"))''')
        code = exact(code, '''        if (!completeVisibleLabels) append(output, response.string("unit"))''', '''        if (!completeVisibleLabels) append(output, response.string("unit"))
        if (WindowsNarrationSpeechRules.keepWholeReadingRegions())
            append(output, response.string("instruction"), response.mode())''')
        code = exact(code, '                    if (it.optBoolean("interactive", false)) {',
            '                    if (it.optBoolean("interactive", false) &&\n'
            '                        (!WindowsNarrationSpeechRules.keepWholeReadingRegions() ||\n'
            '                            it.optString("type", "text") !in setOf("text", "math_formula", "math_table", "rich_answer_option"))) {')
        code = exact(code, '        // Recognize quoted/embedded C++ before math can reinterpret its operators.', '''        val explicit = lgaed.narration.windows.WindowsTextSemantics.lookup(source)
        if (explicit != null) {
            val prepared = lgaed.narration.windows.WindowsTextSemantics.prepared(explicit, speechLanguage)
            if (prepared.isNotBlank()) output.add(prepared)
            return
        }
        // Recognize quoted/embedded C++ before math can reinterpret its operators.''')
        code = exact(code, '}.joinToString(" ")', '}.let { lgaed.narration.windows.WindowsTextSemantics.compose(it, " ") }')
        code = exact(code, '.filter(String::isNotBlank).joinToString(" ")',
                     '.filter(String::isNotBlank).let { lgaed.narration.windows.WindowsTextSemantics.compose(it, " ") }')
        code = exact(code, '.filter(String::isNotBlank).joinToString("; ")',
                     '.filter(String::isNotBlank).let { lgaed.narration.windows.WindowsTextSemantics.compose(it, "; ") }')
        code = exact(code, 'content.joinToString("; ")', 'lgaed.narration.windows.WindowsTextSemantics.compose(content, "; ")')
        # Windows anticipation owns original reading regions. Its caller uses
        # the same NarrationSegments sentence splitter as the visible reader;
        # a second 700-unit split would manufacture different recording keys.
        # Historical pack export does not enter this request-local scope.
        code = exact(code, 'return output.flatMap(::segments)',
                     'return if (WindowsNarrationSpeechRules.keepWholeReadingRegions()) output else output.flatMap(::segments)')
        code = exact(code, 'append(output, block.string("question"))',
                     'append(output, block.string("question"), block.mode())', count=2)
        code = exact(code, 'options(output, block.optJSONArray("options"))',
                     'options(output, block.optJSONArray("options"), block.mode())')
        code = exact(code, 'private fun options(output: MutableList<String>, options: JSONArray?)',
                     'private fun options(output: MutableList<String>, options: JSONArray?, mathMode: String = "auto")')
        code = exact(code, 'else append(output, scalar(options.opt(index)))',
                     'else append(output, scalar(options.opt(index)), mathMode)')
    elif name == "NarrationSegments":
        code = exact(code, "ManualEnglishSpeech.segment(paragraph, ::plainSentences)",
                     "ManualEnglishSpeech.segment(paragraph) { plainSentences(it, prosePauses = true) }")
        code = exact(code, "private fun plainSentences(paragraph: String): List<String>",
                     "private fun plainSentences(paragraph: String, prosePauses: Boolean = false): List<String>")
        code = exact(code, '                if (symbol !in ".!?;" ||', """                val prosePause = prosePauses && WindowsNarrationSpeechRules.isProsePause(source, index) &&
                    source.substring(start, index).any(Char::isLetterOrDigit)
                if ((!prosePause && symbol !in ".!?;") ||""")
        code = exact(code, "                    (index + 1 < source.length && !source[index + 1].isWhitespace())",
                     "                    (!prosePause && index + 1 < source.length && !source[index + 1].isWhitespace())")
    return code


def windows_math_ui_texts(source: str | None = None) -> dict[str, tuple[str, str, str]]:
    """Bind anticipation to the same static help/legend exposed by Win32.

    These are UI reading regions, not curriculum or validator data. Extracting
    their localized literals avoids a second translation table and makes their
    generated Kotlin bytes participate in the ordinary runtime fingerprint.
    Fail closed if a native region moves instead of silently preparing mobile
    instructions or omitting a newly integrated Windows control.
    """
    if source is None:
        source = (ROOT / "native/presentation/windows/math_puzzle_view.cpp").read_text(encoding="utf-8-sig")
    literal = r'"(?:[^"\\]|\\.)*"'
    instruction = re.compile(
        rf'add_text\(tr\(\s*({literal})\s*,\s*({literal})\s*,\s*({literal})\s*\),'
        rf'\s*id\s*\+\s*"/(help|legend)"\s*\)')
    result = {}
    for response_type in ("math_segment_transport", "math_rigid_angle_copy"):
        anchor = f'else if(type=="{response_type}")'
        if source.count(anchor) != 1:
            raise ValueError(f"Native Windows response anchor changed: {response_type}")
        start = source.index(anchor)
        end = source.find("canvas(config,", start)
        if end < 0:
            raise ValueError(f"Native Windows canvas anchor changed: {response_type}")
        rows = instruction.findall(source[start:end])
        if len(rows) != 2 or [row[3] for row in rows] != ["help", "legend"]:
            raise ValueError(f"Native Windows help/legend reading regions changed: {response_type}")
        for spanish, english, french, role in rows:
            # JSON and these C++ UI literals share their supported escapes.
            # Unsupported C++ spellings are deliberately an adaptation error.
            values = tuple(json.loads(value) for value in (spanish, english, french))
            if any(not value.strip() for value in values):
                raise ValueError(f"Empty native Windows reading region: {response_type}/{role}")
            result[f"{response_type}:{role}"] = values
    return result


def kotlin_literal(value: str) -> str:
    # Kotlin strings interpolate '$'; native UI text must remain literal.
    # Escape individual characters so a literal backslash followed by 'f' is
    # never mistaken for JSON's form-feed escape (unsupported by Kotlin).
    return '"' + ''.join(r"\$" if character == "$" else r"\u000c" if character == "\f"
        else json.dumps(character, ensure_ascii=True)[1:-1] for character in value) + '"'


def windows_math_ui_fingerprint(source: str | None = None) -> str:
    # The visible Windows literals are explicit build inputs as well as the
    # generated Kotlin source: changing a translation must invalidate the JAR.
    payload = json.dumps(windows_math_ui_texts(source), ensure_ascii=False, sort_keys=True)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def host_sources() -> list[Path]:
    sources = []
    for name in PURE_NARRATION:
        path = APP / "narration" / f"{name}.kt"
        if name in {"ManualEnglishTerms", "SpanishTechnicalSpeech", "NarrationSegments", "NarrationWordMappings"}:
            sources.append(generated(f"{name}.kt", windows_reading_source(name, path.read_text(encoding="utf-8-sig"))))
        else:
            sources.append(path)
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
    sources.append(APP / "CodeExerciseLanguage.kt")
    sources.append(generated("LessonNarrationText.kt", windows_reading_source("LessonNarrationText",
        (APP / "narration/LessonNarrationText.kt").read_text(encoding="utf-8-sig"))))
    math_ui = (APP / "math/MathPuzzleUiInstructions.kt").read_text(encoding="utf-8-sig")
    math_ui = exact(math_ui,
        '        if (type !in textResponseTypes || config.has("fields")) return ""',
        '        if (type !in textResponseTypes || if (com.learngraphicsprogramming.app.narration.WindowsNarrationSpeechRules.keepWholeReadingRegions())\n'
        '            (config.optJSONArray("fields")?.length() ?: 0) > 0 else config.has("fields")) return ""')
    math_ui = exact(math_ui,
        '        return if (template.isBlank()) unit else String.format(Locale.ROOT, template, unit)',
        '        return if (template.isBlank()) unit else if (com.learngraphicsprogramming.app.narration.WindowsNarrationSpeechRules.keepWholeReadingRegions())\n'
        '            lgaed.narration.windows.WindowsTextSemantics.unitCaption(template,unit)\n'
        '        else String.format(Locale.ROOT, template, unit)')
    math_ui = exact(math_ui,
        '        "math_rigid_angle_copy" -> listOf(resolve(RIGID_ANGLE_HELP), resolve(RIGID_ANGLE_LEGEND))',
        '        "math_rigid_angle_copy" -> listOf(resolve(RIGID_ANGLE_HELP), resolve(RIGID_ANGLE_LEGEND))\n'
        '        "math_segment_transport" -> listOf(resolve("math_segment_transport:help"), resolve("math_segment_transport:legend"))')
    sources.append(generated("MathPuzzleUiInstructions.kt", math_ui))
    script = (APP / "narration/LessonNarrationScript.kt").read_text(encoding="utf-8-sig")
    script = exact(script, '''                if (!isExercise(type)) continue
                val start = texts.size''', '''                if (!isExercise(type)) continue
                if (WindowsNarrationSpeechRules.keepWholeReadingRegions()) {
                    val start = texts.size
                    append(paragraph(lgaed.narration.windows.WindowsTextSemantics.nativeCorrectFeedbackSpeech(block),
                        block.optString("mathMode","auto")),index,feedback=true)
                    if (texts.size > start) gates += NarrationTextGate(index,start,texts.size)
                    continue
                }
                val start = texts.size''')
    sources.append(generated("LessonNarrationScript.kt", script[:script.index("internal object NarrationScriptSegments")]))
    instructions = (APP / "narration/NarrationUiInstructions.kt").read_text(encoding="utf-8-sig")
    mapping = dict(re.findall(r'"([^"]+)" -> R.string.([a-z_]+)', instructions))
    for key, value in re.findall(r'MathPuzzleUiInstructions\.([A-Z_]+) -> R.string.([a-z_]+)', instructions):
        constant = re.search(rf'const val {key} = "([^"]+)"',
            (APP / "math/MathPuzzleUiInstructions.kt").read_text(encoding="utf-8-sig"))
        mapping[constant[1]] = value
    body = 'package lgaed.narration.windows\ninternal object WindowsNarrationInstructions {\n'
    body += '    fun resolve(type: String, language: String): String {\n'
    body += '        val native = when(type) {\n'
    for key, (spanish, english, french) in windows_math_ui_texts().items():
        body += (f'            {kotlin_literal(key)} -> when {{\n'
                 f'                language.startsWith("fr") -> {kotlin_literal(french)}\n'
                 f'                language.startsWith("en") -> {kotlin_literal(english)}\n'
                 f'                else -> {kotlin_literal(spanish)}\n'
                 '            }\n')
    body += '            else -> null\n        }\n        if(native != null) return native\n'
    body += '        if(type == "note") return MathLocaleResources.resolve("lesson_note",listOf(""),language)\n'
    body += '        val resource = when(type) {\n' + ''.join(
        f'            "{key}" -> "{value}"\n' for key,value in mapping.items())
    body += '            else -> return ""\n        }\n        return MathLocaleResources.resolve(resource,language=language)\n    }\n}\n'
    sources.append(generated("WindowsNarrationInstructions.kt", body))

    projection = (APP / "narration/NarrationSourceProjection.kt").read_text(encoding="utf-8-sig")
    projection = exact(projection, "import com.learngraphicsprogramming.app.math.MathTextRenderer\n", "")
    # Native Windows supplies original strings, never Android Spanned values.
    # The original MathTextRenderer returns empty ranges for those same strings.
    projection = exact(projection,
        "else MathTextRenderer.formulaRanges(source)\n            .sortedBy { it.first }.map { ReadingTextRange(it.first, it.last + 1) }",
        "else emptyList()")
    projection = exact(projection, 'internal object NarrationSourceProjection {', '''internal object NarrationSourceProjection {
    fun semanticCode(source: String, language: NarrationLanguage): Projection {
        if (language == NarrationLanguage.SPANISH) {
            val initial = Projection(source, source.indices.map { ReadingTextRange(it, it + 1) })
            return stage(initial) { observe -> SpanishTechnicalSpeech.semanticCode(source, observe) }
                ?: error("Unverifiable semantic C++ source projection")
        }
        val value = forLanguage("`" + source + "`", "text", language)
            ?: error("Unverifiable multilingual semantic C++ projection")
        return Projection(value.text, value.owners.map {
            ReadingTextRange((it.start - 1).coerceIn(0, source.length), (it.endExclusive - 1).coerceIn(0, source.length))
        })
    }
''')
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


def launcher_cpp_profile(versions: dict) -> dict:
    """Select the exact C++23 mode verified for the pinned stable MSVC."""
    profile = versions.get("cpp")
    if (not isinstance(profile, dict) or type(profile.get("standard")) is not int or
        profile["standard"] != 23 or profile.get("msvcStandardOption") != "/std:c++23preview"):
        # The pinned compiler ignores /std:c++23 and c++latest enables a later
        # draft. Neither is a substitute for the requested C++23 profile.
        raise RuntimeError("Windows launcher requires the manifest C++23 profile /std:c++23preview")
    return {"standard": profile["standard"], "msvcStandardOption": profile["msvcStandardOption"]}


def launcher_native_tools(*, scope: str = "Application", cmake: Path | None = None,
                          python: Path | None = None, git: Path | None = None,
                          java_home: Path | None = None, ffmpeg: Path | None = None) -> dict:
    """Validate the shared installations, including explicitly located CI tools."""
    if scope not in ("Application", "NarrationHost"):
        raise ValueError("Unsupported Windows native tool scope")
    manifest = ROOT / "toolchains/windows.versions.json"
    versions = json.loads(manifest.read_text(encoding="utf-8"))
    if versions.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported Windows toolchain manifest")
    cpp = launcher_cpp_profile(versions)
    native = versions["visualStudio"]
    for key in ("msvcToolset", "windowsSdk"):
        if not re.fullmatch(r"\d+(?:\.\d+)+", native[key]):
            raise ValueError(f"Invalid pinned Windows toolchain version: {key}")
    helper = ROOT / "scripts/windows/Resolve-NativeTools.ps1"
    # The helper validates stable VS within versionRange, exact cl.exe version,
    # SDK files and the shared runtime tools. Capture its child-only environment
    # instead of trusting a potentially stale PATH inherited by direct Python.
    def quoted(value) -> str:
        return "'" + str(value).replace("'", "''") + "'"

    locations = {"CMakePath": cmake, "PythonPath": python, "GitPath": git,
                 "JavaHome": java_home, "FFmpegPath": ffmpeg}
    selection = " ".join(["-Scope", quoted(scope), *[
        f"-{name} {quoted(path)}" for name, path in locations.items() if path is not None]])
    script = ('$ErrorActionPreference = "Stop"; '
              '[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); '
              f"$tools = & {quoted(helper)} {selection}; "
              '@{tools=$tools; environment=[Environment]::GetEnvironmentVariables("Process")} '
              '| ConvertTo-Json -Depth 4 -Compress')
    resolved = json.loads(subprocess.check_output(["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive",
        "-ExecutionPolicy", "Bypass", "-Command", script], text=True, encoding="utf-8"))
    tools = resolved["tools"]
    if (tools["Scope"] != scope or Path(tools["Manifest"]).resolve() != manifest.resolve() or
        tools["MSVCToolset"] != native["msvcToolset"] or tools["WindowsSdk"] != native["windowsSdk"]):
        raise RuntimeError("Windows tool selection does not match this checkout's manifest")
    for field, requested in (("CMake", cmake), ("Python", python), ("Git", git),
                             ("JavaHome", java_home), ("FFmpeg", ffmpeg)):
        if requested is not None and Path(tools[field]).resolve() != requested.resolve():
            raise RuntimeError(f"Windows tool selector did not retain the requested {field} installation")
    vs_root = Path(tools["VsRoot"])
    batch = vs_root / "VC/Auxiliary/Build/vcvarsall.bat"
    if Path(tools["VcVars"]).resolve() != batch.resolve():
        raise RuntimeError("Windows tool selector returned an unexpected vcvarsall path")
    compiler = vs_root / f"VC/Tools/MSVC/{native['msvcToolset']}/bin/Hostx64/x64/cl.exe"
    sdk_root = Path(tools["WindowsSdkRoot"])
    rc = sdk_root / f"bin/{native['windowsSdk']}/x64/rc.exe"
    for path in (batch, compiler, rc):
        if not path.is_file():
            raise RuntimeError(f"Missing pinned Windows launcher tool: {path}")
    return {"manifest": manifest, "versions": native, "cpp": cpp, "batch": batch, "compiler": compiler,
            "sdk_root": sdk_root, "rc": rc, "java_home": Path(tools["JavaHome"]),
            "ffmpeg": Path(tools["FFmpeg"]),
            "environment": {key.upper(): value for key, value in resolved["environment"].items()}}


def launcher_native_environment(tools: dict) -> dict:
    batch = tools["batch"]
    if re.search(r'["%!?&|<>^\r\n]', str(batch)):
        raise ValueError("Unexpected shell metacharacter in installed MSVC path")
    native = tools["versions"]
    setup = subprocess.run(f'cmd.exe /d /u /v:off /s /c "call "{batch}" x64 '
        f'{native["windowsSdk"]} -vcvars_ver={native["msvcToolset"]} >nul && set"',
        env=tools["environment"], check=True, capture_output=True, text=True, encoding="utf-16-le")
    environment = dict(tools["environment"])
    for line in setup.stdout.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            environment[key.upper()] = value

    def same_path(first, second):
        return os.path.normcase(str(Path(first).resolve())) == os.path.normcase(str(Path(second).resolve()))

    compiler = shutil.which("cl.exe", path=environment.get("PATH", ""))
    rc = shutil.which("rc.exe", path=environment.get("PATH", ""))
    toolset_root = tools["compiler"].parents[3]
    if (not compiler or not same_path(compiler, tools["compiler"]) or
        not same_path(environment.get("VCTOOLSINSTALLDIR", ""), toolset_root) or
        not rc or not same_path(rc, tools["rc"]) or
        not same_path(environment.get("WINDOWSSDKDIR", ""), tools["sdk_root"]) or
        environment.get("WINDOWSSDKVERSION", "").rstrip("\\/") != native["windowsSdk"]):
        raise RuntimeError("vcvarsall did not activate the exact manifest MSVC x64 toolset and Windows SDK")
    return environment


def launcher_fingerprint(source: Path, home: Path, tools: dict) -> str:
    # SDK servicing keeps the same 10.0.x.0 directory. Both its declared
    # servicing version and the actual rc.exe bytes must invalidate the wrapper.
    inputs = [source, tools["manifest"], tools["compiler"], tools["rc"],
              *[home / name for name in ("release", "include/jni.h", "include/win32/jni_md.h")]]
    profile = tools["cpp"]
    mode = (f"native-launcher-v2\ncpp-standard={profile['standard']}\n"
            f"msvc-standard-option={profile['msvcStandardOption']}\n/Zc:__cplusplus\n")
    return hashlib.sha256((mode + str(home) + "\n".join(str(path) + ":" + digest(path) for path in inputs)).encode()).hexdigest()


def launcher(java: Path, tools: dict | None = None) -> Path:
    home = java.parent.parent
    if not (home / "include/jni.h").is_file():
        raise RuntimeError("The configured JDK must include the official JNI headers (JRE-only runtimes cannot build the native host)")
    source = ROOT / "native/narration/worker_launcher.cpp"
    output = OUT / "lgaed_narration_host.exe"
    if tools is None:
        tools = launcher_native_tools(java_home=home)
    if home.resolve() != tools["java_home"].resolve():
        raise RuntimeError("JNI launcher must use the JDK validated by the Windows tool selector")
    signature = launcher_fingerprint(source, home, tools)
    stamp = OUT / "launcher-build.sha256"
    if not output.is_file() or not stamp.is_file() or stamp.read_text() != signature:
        environment = launcher_native_environment(tools)
        command = [str(tools["compiler"]),"/nologo",tools["cpp"]["msvcStandardOption"],"/Zc:__cplusplus","/EHsc","/W4","/utf-8","/MD",
            f"/I{home / 'include'}",f"/I{home / 'include/win32'}",str(source),
            f"/Fo{OUT / 'worker_launcher.obj'}",f"/Fe{output}"]
        subprocess.run(command,env=environment,check=True,cwd=ROOT)
        stamp.write_text(signature,encoding="utf-8")
    (OUT / "jdk-root.txt").write_text(str(home),encoding="utf-8")
    return output


def resolve_java_home(explicit: Path | None = None) -> Path:
    home = (explicit or Path(os.environ.get("JAVA_HOME", r"C:\Program Files\Android\Android Studio\jbr"))).resolve()
    required = ("bin/java.exe", "bin/javac.exe", "bin/jar.exe", "include/jni.h",
                "include/win32/jni_md.h", "bin/server/jvm.dll", "release")
    missing = [name for name in required if not (home / name).is_file()]
    if missing:
        raise RuntimeError(f"A complete x64 JDK is required at {home}; missing: {', '.join(missing)}. "
                           "Use Android Studio's Download JDK and pass --java-home; its bundled JRE/JBR may lack JNI headers.")
    release = (home / "release").read_text(encoding="utf-8")
    version = re.search(r'^JAVA_VERSION="(\d+)', release, re.MULTILINE)
    if not version or int(version[1]) < int(JVM_TARGET):
        raise RuntimeError(f"The Windows worker targets JVM {JVM_TARGET}; Download JDK {JVM_TARGET} or newer "
                           "and pass its complete directory using --java-home")
    architecture = re.search(r'^OS_ARCH="([^"]+)"', release, re.MULTILINE)
    if not architecture or architecture[1] not in ("amd64", "x86_64", "x64"):
        raise RuntimeError(f"The Windows native worker requires an x64 JDK, not {architecture[1] if architecture else 'an unknown architecture'}")
    return home


def toolchain_fingerprint(java_home: Path, compiler: list[Path]) -> str:
    return "\n".join([str(java_home), f"jvm-target={JVM_TARGET}",
        *[digest(java_home / name) for name in ("release", "bin/java.exe", "bin/javac.exe", "bin/server/jvm.dll")],
        *[str(path) + ":" + digest(path) for path in compiler]])


def build() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path)
    parser.add_argument("--english-resources", type=Path)
    parser.add_argument("--java-home", type=Path)
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--native-tools-scope", choices=("Application", "NarrationHost"), default="Application")
    parser.add_argument("--cmake", type=Path)
    parser.add_argument("--python", type=Path)
    parser.add_argument("--git", type=Path)
    parser.add_argument("--refresh-dependencies", action="store_true",
                        help="Verify the fixed Maven Central graph and refresh its SHA-256 lock, without running Java")
    args = parser.parse_args()
    if args.refresh_dependencies:
        dependencies = worker_dependencies(refresh=True)
        print(f"Verified {len(dependencies['runtime'])} runtime and {len(dependencies['compiler'])} compiler jars: {DEPENDENCY_LOCK}")
        return
    # Use the preserved, attributed pack from this checkout. Validate before any
    # build/download so damaged resources never become a successful runtime.
    english = (args.english_resources or ROOT / "english_misaki/src/main/assets").resolve()
    verify_staged_assets(english)
    native_tools = launcher_native_tools(scope=args.native_tools_scope, cmake=args.cmake,
        python=args.python, git=args.git, java_home=args.java_home, ffmpeg=args.ffmpeg)
    java_home = resolve_java_home(native_tools["java_home"])
    java = java_home / "bin/java.exe"
    dependencies = worker_dependencies()
    runtime, compiler = dependencies["runtime"], dependencies["compiler"]
    toolchain_signature = toolchain_fingerprint(java_home, compiler)
    sources = host_sources()
    java_sources = sorted((ROOT / "native/narration/worker/androidjson").glob("*.java"))
    java_classes = OUT / "android-json-classes"
    java_stamp = OUT / "android-json-build.sha256"
    java_signature = hashlib.sha256((toolchain_signature + "".join(digest(source) for source in java_sources)).encode()).hexdigest()
    if not java_stamp.is_file() or java_stamp.read_text() != java_signature:
        java_classes.mkdir(parents=True,exist_ok=True)
        subprocess.run([str(java.parent / "javac.exe"),"-encoding","UTF-8","--release",JVM_TARGET,"-d",str(java_classes),
                        *map(str,java_sources)],check=True,cwd=ROOT,env=native_tools["environment"])
        java_stamp.write_text(java_signature,encoding="ascii")
    jar = OUT / "kokoro-worker.jar"
    signature = hashlib.sha256(("\n".join(str(p) + ":" + digest(p) for p in sources + runtime)
        + windows_math_ui_fingerprint() + java_signature + toolchain_signature).encode()).hexdigest()
    stamp = OUT / "worker-build.sha256"
    if not jar.is_file() or not stamp.is_file() or stamp.read_text() != signature:
        command = [str(java), "-cp", os.pathsep.join(map(str, compiler)),
            "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect", f"-Xjdk-release={JVM_TARGET}",
            "-classpath", os.pathsep.join(map(str, [*runtime,java_classes])), "-d", str(jar), *map(str, sources)]
        result = subprocess.run(command, cwd=ROOT, env=native_tools["environment"])
        if result.returncode:
            raise SystemExit(result.returncode)
        subprocess.run([str(java.parent / "jar.exe"),"uf",str(jar),"-C",str(java_classes),"."],
                       check=True,cwd=ROOT,env=native_tools["environment"])
        stamp.write_text(signature, encoding="ascii")
    origin = ROOT.parent / "LearnGraphicsProgramming/build/kokoro"
    resources = (args.resources or origin / "downloads/1939ad2a8e416c0acfeecc08a694d14ef25f2231").resolve()
    ffmpeg = str(native_tools["ffmpeg"])
    classpath = os.pathsep.join(map(str,[jar,*runtime]))
    original_wavs = (origin / "../first-unit-speech-export").resolve()
    native_host = launcher(java, native_tools)
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
            verify_runtime(pending_config, repo_root=ROOT)
            pending_config.replace(config_path)
        finally:
            pending_config.unlink(missing_ok=True)
    verify_runtime(config_path, repo_root=ROOT)
    print(config_path)


if __name__ == "__main__":
    with build_lock():
        build()


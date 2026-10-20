package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.graphics.Typeface
import android.os.SystemClock
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.ScrollView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.math.MathGeometryQaSupport
import com.learngraphicsprogramming.app.narration.LessonReadingTextColors
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.narration.ProgrammingTechnicalRanges
import com.learngraphicsprogramming.app.narration.ReadingBlockState
import com.learngraphicsprogramming.app.narration.ReadingTextRange
import com.learngraphicsprogramming.app.narration.ReadingTextView
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import kotlin.math.ceil
import kotlin.math.floor

/** Current authored content, rather than invented formatting fixtures. This only verifies
 * native rendering and reversible word masks; its screenshots do not claim audio alignment.
 * The thirteen paragraphs already accepted by Work are deliberately not replayed. */
@RunWith(AndroidJUnit4::class)
class CurrentParagraphEmphasisDeviceTest : MathGeometryQaSupport("com.learngraphicsprogramming.app.realdownloadsqa") {
    private val session = UUID.randomUUID().toString()
    private data class Location(val relative: String, val page: Int, val block: Int) {
        val key get() = "$relative:p$page:b$block"
    }

    @Test fun remainingTwentyThreeParagraphsKeepTheirActualBoldSyntaxAndWordMasks() = verify(remainingCore())

    @Test fun elevenInputLessonParagraphsKeepTheirActualBoldSyntaxAndWordMasks() = verify(inputLocations())

    private fun verify(locations: List<Location>) {
        // These new settings only touch the isolated QA package and are restored by
        // the inherited fixture, including any preexisting per-course scale choice.
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "cpp_fundamentals", true)
        val inventory = inventory()
        assertEquals("The updated curricular inventory is explicit, not a sample", 47, inventory.size)
        assertEquals("Every remaining location and every already accepted location is accounted for",
            inventory.toSet(), (remainingCore() + inputLocations() + accepted()).toSet())
        assertEquals(34, (remainingCore() + inputLocations()).size)
        assertTrue(locations.none { it in accepted() })
        val records = JSONArray()
        locations.groupBy { it.relative }.forEach { (relative, paragraphs) ->
            val path = ROOT + relative
            val bytes = context.assets.open(path).use { it.readBytes() }
            val original = JSONObject(String(bytes, Charsets.UTF_8))
            val originalId = original.getString("id")
            val qaId = "${originalId}__emphasis_native_qa_${session.replace("-", "")}"
            val qaLesson = JSONObject(original.toString()).put("id", qaId)
            val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
                .putExtra("courseId", "cpp_fundamentals")
                .putExtra("unitId", "cpp_fundamentals__${relative.substringBefore('/')}")
                .putExtra("lessonId", originalId).putExtra("lessonNumber", 1).putExtra("lessonTotal", 14)
            ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
                // A real asset load may finish after Activity.onCreate. Let it settle before
                // installing an isolated identity with the exact original pages and blocks.
                var loadedSince = 0L
                await("The original lesson finishes loading before isolated rendering") { ready ->
                    scenario.onActivity { activity ->
                        val loaded = (field(activity, "lessonJson") as? JSONObject)?.optString("id") == originalId
                        if (!loaded) loadedSince = 0L else if (loadedSince == 0L) loadedSince = SystemClock.uptimeMillis()
                        ready(loaded && SystemClock.uptimeMillis() - loadedSince >= 500L)
                    }
                }
                paragraphs.forEach { location ->
                    val block = original.getJSONArray("pages").getJSONObject(location.page - 1)
                        .getJSONArray("blocks").getJSONObject(location.block - 1)
                    val source = block.getString("text")
                    val record = JSONObject().put("sourceAsset", path).put("sourceSha256", sha256(bytes))
                        .put("sourceLessonId", originalId).put("page", location.page).put("block", location.block)
                        .put("qaLessonId", qaId).put("sourceText", source).put("authoredEmphasis", block.getJSONArray("emphasis"))
                    scenario.onActivity { activity ->
                        setField(activity, "currentLessonId", qaId)
                        setField(activity, "lessonJson", qaLesson)
                        setField(activity, "totalPages", qaLesson.getJSONArray("pages").length())
                        setField(activity, "currentPage", location.page - 1)
                        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { name ->
                            (field(activity, name) as MutableMap<*, *>).clear()
                        }
                        LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }.invoke(activity)
                        activity.currentFocus?.clearFocus()
                        (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                            .hideSoftInputFromWindow(activity.window.decorView.windowToken, 0)
                    }
                    await("The original paragraph has its real native layout: ${location.key}") { ready ->
                        scenario.onActivity { activity ->
                            val view = paragraph(activity, source)
                            ready(view.width > 0 && view.height > 0 && view.layout != null && !view.isLayoutRequested)
                        }
                    }
                    val stem = relative.substringAfter("lesson_").removeSuffix(".json") + "-p${location.page}-b${location.block}"
                    scenario.onActivity { activity ->
                        val view = paragraph(activity, source)
                        assertEquals(source, view.text.toString())
                        assertTrue("The original paragraph remains eligible for word masking", view.safeSupportsWordReading)
                        val spanned = view.text as Spanned
                        val expected = LessonParagraphEmphasis.ranges(source, block.getJSONArray("emphasis"))
                        assertEquals("Every authored item resolves to its exact original range", block.getJSONArray("emphasis").length(), expected.size)
                        val bold = spanned.getSpans(0, spanned.length, StyleSpan::class.java).filter { it.style == Typeface.BOLD }
                            .map { LessonParagraphEmphasis.Range(spanned.getSpanStart(it), spanned.getSpanEnd(it)) }.sortedBy { it.start }
                        assertEquals("The renderer applies exactly the authored emphasis", expected, bold)
                        val syntax = ProgrammingTechnicalRanges.analyze(source, NarrationLanguage.SPANISH)
                        if (syntax.fragments.isNotEmpty()) assertTrue("Original technical colors are retained",
                            spanned.getSpans(0, spanned.length, ForegroundColorSpan::class.java).isNotEmpty())
                        val textObject = view.text
                        val originalLayout = view.layout
                        val originalWidth = view.width
                        val originalHeight = view.height
                        val originalSpans = spanSnapshot(spanned)
                        val full = draw(view)
                        val colors = LessonReadingTextColors().apply { register(location.block - 1, view) }
                        fun pending(from: Int) = colors.show(mapOf(location.block - 1 to ReadingBlockState.CURRENT), true,
                            unreadWords = mapOf(location.block - 1 to listOf(ReadingTextRange(from, source.length))))
                        // A real lexical boundary, including cases where the highlighted
                        // term is code. These are controlled draw oracles, not audio times.
                        val boundary = Regex("\\S+").findAll(source).drop(2).firstOrNull()?.range?.first
                            ?: Regex("\\S+").findAll(source).last().range.first
                        assertTrue(boundary in 1 until source.length)
                        pending(0)
                        val unread = draw(view)
                        assertFalse("Pending glyphs differ from their original ink", full.sameAs(unread))
                        pending(boundary)
                        val partial = draw(view)
                        assertFalse("Read and pending words coexist", unread.sameAs(partial))
                        assertFalse("Later words remain pending", full.sameAs(partial))
                        val ink = view.readingInk
                        fun inkField(name: String) = ink.javaClass.getDeclaredField(name).apply { isAccessible = true }.get(ink)
                        assertEquals(listOf(ReadingTextRange(boundary, source.length)), inkField("unreadRanges"))
                        val grays = setOf(inkField("unreadColor") as Int, inkField("semanticUnreadColor") as Int)
                        val pixels = IntArray(partial.width * partial.height)
                        partial.getPixels(pixels, 0, partial.width, 0, 0, partial.width, partial.height)
                        val grayPixels = pixels.count { it in grays }
                        assertTrue("The atomic native-view Canvas contains real pending gray glyphs", grayPixels > 20)
                        val fullCanvasFile = "$stem-canvas-complete.png"
                        val progressiveCanvasFile = "$stem-canvas-progressive.png"
                        File(evidence(), fullCanvasFile).outputStream().use { assertTrue(full.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                        File(evidence(), progressiveCanvasFile).outputStream().use { assertTrue(partial.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                        val completeSha = sha256(File(evidence(), fullCanvasFile).readBytes())
                        val progressiveSha = sha256(File(evidence(), progressiveCanvasFile).readBytes())
                        assertNotEquals(completeSha, progressiveSha)
                        record.put("controlledWordMaskCanvas", JSONObject()
                            .put("kind", "native-view-canvas")
                            .put("description", "Atomic Canvas draw of the SAME native ReadingTextView on the UI thread; not a window screenshot or audio timing")
                            .put("completeFile", fullCanvasFile).put("progressiveFile", progressiveCanvasFile)
                            .put("completeSha256", completeSha).put("progressiveSha256", progressiveSha)
                            .put("width", view.width).put("height", view.height)
                            .put("pendingRange", JSONArray(listOf(boundary, source.length)))
                            .put("pendingGrayColors", JSONArray(grays.toList())).put("pendingGlyphPixels", grayPixels))
                        pending(0)
                        val rewind = draw(view)
                        assertTrue("Rewinding restores exactly the pending mask", unread.sameAs(rewind))
                        pending(boundary)
                        val resumed = draw(view)
                        assertTrue("Restoring the same cursor restores identical ink", partial.sameAs(resumed))
                        colors.show(mapOf(location.block - 1 to ReadingBlockState.READ), true)
                        val completed = draw(view)
                        assertTrue("Completion restores every original styled glyph", full.sameAs(completed))
                        assertSame(textObject, view.text)
                        assertSame(originalLayout, view.layout)
                        assertEquals(originalWidth, view.width)
                        assertEquals(originalHeight, view.height)
                        assertEquals(originalSpans, spanSnapshot(view.text as Spanned))
                        listOf(full, unread, partial, rewind, resumed, completed).forEach(Bitmap::recycle)
                        // Do not force a mask into a window whose narrator is disabled:
                        // its production callbacks correctly restore ordinary ink.
                        colors.clear()
                        record.put("boldRanges", JSONArray(expected.map { JSONArray(listOf(it.start, it.endExclusive)) }))
                            .put("syntaxSpanCount", spanned.getSpans(0, spanned.length, ForegroundColorSpan::class.java).size)
                            .put("allOriginalSpansPreserved", true).put("textAndLayoutIdentityPreserved", true)
                            .put("wordMaskBoundary", boundary).put("wordMasksReversible", true)
                    }
                    record.put("completePresentation", captureParagraph(scenario, source, "$stem-complete"))
                    scenario.onActivity { activity ->
                        assertEquals(source, paragraph(activity, source).text.toString())
                        assertEquals("Only the isolated identity differs in the in-memory QA lesson",
                            original.toString(), JSONObject(qaLesson.toString()).put("id", originalId).toString())
                    }
                    records.put(record)
                    writeManifest(locations, records)
                }
            }
            assertArrayEquals("The source asset remains byte-for-byte unchanged", bytes, context.assets.open(path).use { it.readBytes() })
        }
        assertEquals(locations.size, records.length())
    }

    private fun paragraph(activity: LessonActivity, source: String): ReadingTextView =
        descendants(activity.findViewById(android.R.id.content)).filterIsInstance<ReadingTextView>()
            .single { it.text.toString() == source }

    private fun spanSnapshot(text: Spanned) = text.getSpans(0, text.length, Any::class.java).map {
        listOf(it, text.getSpanStart(it), text.getSpanEnd(it), text.getSpanFlags(it))
    }

    /** The shared native-capture method requires a whole view. Long paragraphs instead
     * need the existing inline-feedback pattern: legal lesson scrolling and coverage of
     * every complete native line above the unchanged floating reader. */
    private fun captureParagraph(scenario: ActivityScenario<LessonActivity>, source: String, label: String): JSONObject {
        var required = emptySet<Int>()
        scenario.onActivity { activity ->
            val view = paragraph(activity, source)
            val layout = requireNotNull(view.layout)
            required = (0 until layout.lineCount).filter { line ->
                source.subSequence(layout.getLineStart(line), layout.getLineEnd(line)).any { !it.isWhitespace() }
            }.toSet()
        }
        val covered = mutableSetOf<Int>()
        val frames = JSONArray()
        while (!covered.containsAll(required)) {
            val requested = required.first { it !in covered }
            var last: Bitmap? = null
            var previousCrop: Bitmap? = null
            var stable = 0
            var sampled = 0
            var visible = emptySet<Int>()
            var metadata = JSONObject()
            val deadline = SystemClock.uptimeMillis() + 8_000L
            try {
                while (SystemClock.uptimeMillis() < deadline && stable < 4) {
                    instrumentation.waitForIdleSync()
                    var viewport = Rect()
                    var ready = false
                    var glyphBounds = emptyList<Rect>()
                    scenario.onActivity { activity ->
                        val view = paragraph(activity, source)
                        val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
                        viewport = visibleBounds(scroll)
                        val reader = activity.findViewById<View>(R.id.lessonNarrationControls)
                            ?.takeIf { it.alpha > 0f }?.let(::visibleBounds) ?: Rect()
                        if (!reader.isEmpty && Rect.intersects(viewport, reader)) viewport.bottom =
                            (reader.top - (8 * activity.resources.displayMetrics.density).toInt()).coerceAtLeast(viewport.top)
                        assertTrue(viewport.width() > 0 && viewport.height() > 0)
                        val before = IntArray(2).also(view::getLocationOnScreen)
                        val scrollPosition = IntArray(2).also(scroll::getLocationOnScreen)
                        val child = requireNotNull(scroll.getChildAt(0))
                        val margins = (child.layoutParams as? ViewGroup.MarginLayoutParams)?.let { it.topMargin + it.bottomMargin } ?: 0
                        val limit = (child.height + margins + scroll.paddingTop + scroll.paddingBottom - scroll.height).coerceAtLeast(0)
                        val layout = requireNotNull(view.layout)
                        val contentTop = before[1] - scrollPosition[1] + scroll.scrollY
                        val desired = (contentTop + view.totalPaddingTop + layout.getLineTop(requested) - view.scrollY -
                            (viewport.top - scrollPosition[1]) - (8 * activity.resources.displayMetrics.density).toInt()).coerceIn(0, limit)
                        if (scroll.scrollY != desired) scroll.scrollTo(0, desired)
                        ready = activity.hasWindowFocus() && !scroll.isLayoutRequested && !view.isLayoutRequested && scroll.scrollY == desired
                        val position = IntArray(2).also(view::getLocationOnScreen)
                        val lines = JSONArray()
                        val actualGlyphBounds = mutableListOf<Rect>()
                        visible = required.filter { line ->
                            val left = floor(position[0] + view.totalPaddingLeft + layout.getLineLeft(line) - view.scrollX).toInt()
                            val right = ceil(position[0] + view.totalPaddingLeft + layout.getLineRight(line) - view.scrollX).toInt()
                            val bounds = Rect(left, position[1] + view.totalPaddingTop + layout.getLineTop(line) - view.scrollY,
                                right.coerceAtLeast(left + 1), position[1] + view.totalPaddingTop + layout.getLineBottom(line) - view.scrollY)
                            val fullyVisible = viewport.contains(bounds)
                            lines.put(JSONObject().put("line", line).put("start", layout.getLineStart(line))
                                .put("endExclusive", layout.getLineEnd(line)).put("bounds", rect(bounds)).put("fullyVisible", fullyVisible))
                            if (fullyVisible) actualGlyphBounds += bounds
                            fullyVisible
                        }.toSet()
                        glyphBounds = actualGlyphBounds
                        metadata = JSONObject().put("scrollY", scroll.scrollY).put("maxLegalScrollY", limit)
                            .put("safeViewport", rect(viewport)).put("lines", lines).put("fullyVisibleLines", JSONArray(visible.sorted()))
                            .put("kind", "native-window-screenshot")
                            .put("narrationEnabled", false)
                    }
                    val screenshot = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                    assertTrue(viewport.left >= 0 && viewport.top >= 0 && viewport.right <= screenshot.width && viewport.bottom <= screenshot.height)
                    val crop = Bitmap.createBitmap(screenshot, viewport.left, viewport.top, viewport.width(), viewport.height())
                    val digest = MessageDigest.getInstance("SHA-256")
                    glyphBounds.forEach { bounds ->
                        val pixels = IntArray(bounds.width() * bounds.height())
                        screenshot.getPixels(pixels, 0, bounds.width(), bounds.left, bounds.top, bounds.width(), bounds.height())
                        val raw = java.nio.ByteBuffer.allocate(pixels.size * 4)
                        raw.asIntBuffer().put(pixels)
                        digest.update(raw.array())
                    }
                    metadata.put("glyphRegionSha256", digest.digest().joinToString("") { "%02x".format(it) })
                    stable = if (ready && requested in visible && previousCrop?.sameAs(crop) == true) stable + 1
                        else if (ready && requested in visible) 1 else 0
                    previousCrop?.recycle()
                    previousCrop = crop
                    last?.recycle()
                    last = screenshot
                    sampled++
                    if (stable < 4) SystemClock.sleep(100L)
                }
                assertTrue("Requested original text line is fully visible in four stable frames: $label/$requested", stable >= 4)
                assertTrue(visible.any { it !in covered })
                val name = "$label-frame-${frames.length() + 1}.png"
                File(evidence(), name).outputStream().use { assertTrue(last!!.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                metadata.put("file", name).put("stableConsecutiveFrames", stable).put("sampledFrames", sampled)
                    .put("samplingIntervalMillis", 100).put("boundsCoordinateSpace", "displayPixels")
                frames.put(metadata)
                covered += visible
                assertTrue(frames.length() <= required.size)
            } finally { previousCrop?.recycle(); last?.recycle() }
        }
        assertEquals(required, covered)
        return JSONObject().put("frames", frames).put("requiredLines", JSONArray(required.sorted()))
            .put("coveredLines", JSONArray(covered.sorted())).put("allNonblankLinesCovered", true)
            .put("floatingReaderHiddenOrMoved", false)
    }

    private fun visibleBounds(view: View): Rect {
        val rect = Rect()
        if (!view.isShown || !view.getGlobalVisibleRect(rect)) return Rect()
        val origin = IntArray(2).also(view.rootView::getLocationOnScreen)
        rect.offset(origin[0], origin[1])
        return rect
    }
    private fun rect(rect: Rect) = JSONArray(listOf(rect.left, rect.top, rect.right, rect.bottom))
    private fun await(reason: String, condition: ((Boolean) -> Unit) -> Unit) {
        val deadline = SystemClock.uptimeMillis() + 12_000L
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            condition { ready = it }
            if (ready) return
            SystemClock.sleep(40L)
        }
        fail(reason)
    }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "paragraph-emphasis-current-qa/$session").apply { mkdirs() }
    private fun writeManifest(requested: List<Location>, records: JSONArray) {
        File(evidence(), "manifest-${requested.size}.json").writeText(JSONObject()
            .put("package", context.packageName).put("session", session)
            .put("authoredParagraphCount", 47).put("authoredFragmentCount", 93)
            .put("evidenceKind", "Original native LessonActivity windows; controlled masks are draw checks, not audio timing")
            .put("requestedLocations", JSONArray(requested.map { it.key })).put("alreadyAcceptedLocationsNotRepeated", JSONArray(accepted().map { it.key }))
            .put("completedParagraphs", records.length()).put("requestedParagraphs", requested.size).put("records", records).toString(2))
    }
    private fun inventory(): List<Location> = context.assets.list(ROOT.removeSuffix("/"))!!.flatMap { unit ->
        context.assets.list(ROOT + unit).orEmpty().filter { it.endsWith(".json") }.flatMap { file ->
            val lesson = JSONObject(context.assets.open("$ROOT$unit/$file").bufferedReader().use { it.readText() })
            val pages = lesson.optJSONArray("pages") ?: JSONArray()
            (0 until pages.length()).flatMap { page ->
                val blocks = pages.getJSONObject(page).getJSONArray("blocks")
                (0 until blocks.length()).filter { blocks.getJSONObject(it).has("emphasis") }
                    .map { Location("$unit/$file", page + 1, it + 1) }
            }
        }
    }
    private fun sha256(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun accepted(): List<Location> = listOf(
        Location("programas_y_ejecucion/lesson_problemas_instrucciones_y_resultados.json", 1, 2),
        Location("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 1, 4),
        Location("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 2, 2),
        Location("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 1, 2),
        Location("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 1, 2)
    ) + listOf("objetos_valores_y_tipos", "declarar_e_inicializar", "estado_asignacion_y_copia", "nombres_con_intencion",
        "enteros_y_aproximaciones", "conversiones_y_deduccion", "cadenas_y_texto", "caracteres_y_valores_logicos")
        .map { Location("objetos_y_estado/lesson_$it.json", 1, 2) }
    private fun remainingCore(): List<Location> = listOf(
        Location("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 4, 3),
        Location("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 4, 2),
        Location("objetos_y_estado/lesson_estado_asignacion_y_copia.json", 3, 2),
        Location("objetos_y_estado/lesson_nombres_con_intencion.json", 5, 2)
    ) + listOf(1 to 3, 3 to 2, 4 to 2, 7 to 2).map { Location("objetos_y_estado/lesson_enteros_y_aproximaciones.json", it.first, it.second) } +
        listOf(2 to 2, 3 to 2, 4 to 2, 5 to 4, 7 to 2).map { Location("objetos_y_estado/lesson_conversiones_y_deduccion.json", it.first, it.second) } +
        listOf(2 to 2, 3 to 2, 4 to 2, 6 to 2, 7 to 2).map { Location("objetos_y_estado/lesson_cadenas_y_texto.json", it.first, it.second) } +
        listOf(1 to 4, 2 to 2, 5 to 2, 5 to 4, 7 to 2).map { Location("objetos_y_estado/lesson_caracteres_y_valores_logicos.json", it.first, it.second) }
    private fun inputLocations(): List<Location> = listOf(1 to 2, 2 to 2, 3 to 2, 4 to 2, 5 to 2, 6 to 2, 7 to 2, 9 to 5, 10 to 3, 12 to 4, 13 to 2)
        .map { Location("objetos_y_estado/lesson_leer_una_entrada_conocida.json", it.first, it.second) }
    companion object { private const val ROOT = "content/es/programming/cpp_fundamentals/" }
}

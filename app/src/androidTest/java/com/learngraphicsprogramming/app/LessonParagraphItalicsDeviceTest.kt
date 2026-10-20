package com.learngraphicsprogramming.app

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Typeface
import android.os.SystemClock
import android.text.SpannableString
import android.text.Spanned
import android.text.TextPaint
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.text.style.UnderlineSpan
import android.text.style.URLSpan
import android.view.View
import android.view.ViewGroup
import androidx.lifecycle.Lifecycle
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.math.MathGeometryQaSupport
import com.learngraphicsprogramming.app.narration.LessonReadingColor
import com.learngraphicsprogramming.app.narration.LessonReadingTextColors
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
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

/** Optional styles are proposed only in isolated in-memory fixtures. Work retains authorship
 * of APA title selection. Canvas masks are drawing evidence, never fabricated audio timing. */
@RunWith(AndroidJUnit4::class)
class LessonParagraphItalicsDeviceTest : MathGeometryQaSupport("com.learngraphicsprogramming.app.realdownloadsqa") {
    private val session = UUID.randomUUID().toString()
    private data class Target(val relative: String, val page: Int, val block: Int, val title: String) {
        val key get() = "$relative:p$page:b$block"
    }

    @Test fun nativeItalicBoldOverlapKeepsExistingSpansAndReversibleWordMasks() = instrumentation.runOnMainSync {
        for (language in NarrationLanguage.entries) for (surface in listOf(Color.rgb(16, 16, 16), Color.WHITE)) {
            val source = "Consulta std::cout y conserva su título."
            val underline = UnderlineSpan()
            val link = URLSpan("https://example.invalid/original")
            val externalStyle = StyleSpan(Typeface.ITALIC)
            val original = SpannableString(source).apply {
                setSpan(underline, 0, 8, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                setSpan(link, source.indexOf("título"), source.length - 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                setSpan(externalStyle, source.indexOf("conserva"), source.indexOf("su título"), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            val view = ReadingTextView(context).apply {
                // A hosted TextView always has LayoutParams. This standalone Canvas
                // fixture also needs them before replacing text after its first layout.
                layoutParams = ViewGroup.LayoutParams(1080, ViewGroup.LayoutParams.WRAP_CONTENT)
                text = original; textSize = 18f; setTextColor(if (surface == Color.WHITE) Color.BLACK else Color.WHITE)
                setBackgroundColor(surface)
            }
            val config = JSONArray().put(entry("std::cout", "italic")).put(entry("cout", "bold"))
                .put(entry("Consulta", "bold_italic"))
            repeat(3) { LessonParagraphEmphasis.apply(view, config) }
            LessonTechnicalText.apply(view, false, language)
            measure(view)
            val text = view.text as Spanned
            assertEquals(source, text.toString())
            for (span in listOf(underline, link, externalStyle)) assertTrue(text.getSpans(0, text.length, Any::class.java).any { it === span })
            assertEquals(4, text.getSpans(0, text.length, StyleSpan::class.java).size)
            val paint = TextPaint().apply { typeface = Typeface.DEFAULT }
            text.getSpans(source.indexOf("cout"), source.indexOf("cout") + 4, StyleSpan::class.java)
                .forEach { it.updateDrawState(paint) }
            assertEquals("Native overlapping bold and italic combine", Typeface.BOLD_ITALIC, paint.typeface.style)
            assertTrue("The original console syntax palette remains", text.getSpans(0, text.length, ForegroundColorSpan::class.java).isNotEmpty())
            checkMasks(view, source, "overlay-${language.tag}-${if (surface == Color.WHITE) "light" else "dark"}", surface)
            val before = snapshot(view.text as Spanned).filter { it.first() === underline || it.first() === link || it.first() === externalStyle }
            LessonParagraphEmphasis.apply(view, null)
            assertEquals(before, snapshot(view.text as Spanned).filter { it.first() === underline || it.first() === link || it.first() === externalStyle })
            assertEquals(1, (view.text as Spanned).getSpans(0, source.length, StyleSpan::class.java).size)
        }
    }

    @Test fun registeredCitationAndReferenceTitlesRenderInBothOrientationsWithExactOriginalText() {
        NarrationSettingsRepository.saveLanguage(context, NarrationLanguage.SPANISH)
        val targets = targets()
        assertEquals(27, targets.size)
        val originals = targets.map { it.relative }.distinct().associateWith { relative ->
            context.assets.open(ROOT + relative).use { it.readBytes() }
        }
        val originalBlocks = targets.map { target -> JSONObject(String(originals.getValue(target.relative), Charsets.UTF_8))
            .getJSONArray("pages").getJSONObject(target.page - 1).getJSONArray("blocks").getJSONObject(target.block - 1) }
        val proposed = originalBlocks.mapIndexed { index, original ->
            val source = original.getString("text")
            val title = targets[index].title
            val matches = Regex(Regex.escape(title)).findAll(source).toList()
            assertTrue("The registered exact source title is present: ${targets[index].key}", matches.isNotEmpty())
            JSONObject(original.toString()).apply {
                val emphasis = JSONArray(original.optJSONArray("emphasis")?.toString() ?: "[]")
                matches.forEachIndexed { occurrence, _ -> emphasis.put(entry(title, "italic").apply {
                    if (matches.size > 1) put("occurrence", occurrence + 1)
                }) }
                put("emphasis", emphasis)
            }
        }
        assertEquals("The twenty-two cited fields include both titles in the repeated field", 23,
            proposed.drop(5).sumOf { block -> LessonParagraphEmphasis.styledRanges(block.getString("text"), block.opt("emphasis"))
                .count { it.style == LessonParagraphEmphasis.Style.ITALIC } })
        val id = "cpp_fundamentals__archivos_y_programa_completo__italic_native_qa_${session.replace("-", "")}"
        val lesson = JSONObject().put("id", id).put("title", "Comprobación técnica de cursivas")
            .put("pages", JSONArray(proposed.map { JSONObject().put("blocks", JSONArray().put(it)) }))
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "cpp_fundamentals__archivos_y_programa_completo")
            .putExtra("lessonId", "cpp_fundamentals__archivos_y_programa_completo__referencias_del_curso")
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        val records = JSONArray()
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            for (orientation in listOf(Configuration.ORIENTATION_PORTRAIT, Configuration.ORIENTATION_LANDSCAPE)) {
                scenario.onActivity { it.requestedOrientation = if (orientation == Configuration.ORIENTATION_PORTRAIT)
                    ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
                for ((index, target) in targets.withIndex()) {
                    awaitPage(scenario, lesson, id, index, orientation)
                    scenario.onActivity { activity ->
                        val originalBlock = originalBlocks[index]
                        val body = originalBlock.getString("text")
                        val source = presentedText(activity, originalBlock)
                        val view = paragraph(activity, source)
                        val text = view.text as Spanned
                        assertEquals(source, text.toString())
                        assertTrue("A localized note prefix never changes its original body", source.endsWith(body))
                        val expected = LessonParagraphEmphasis.styledRanges(source, proposed[index].opt("emphasis"),
                            LessonParagraphEmphasis.Range(source.length - body.length, source.length))
                        val actual = text.getSpans(0, text.length, StyleSpan::class.java).map { span ->
                            Triple(text.getSpanStart(span), text.getSpanEnd(span), span.style)
                        }.toSet()
                        assertEquals(expected.map { Triple(it.start, it.endExclusive, it.style.typefaceStyle) }.toSet(), actual)
                        assertEquals("References, URLs, punctuation and accessible source remain exact", source, view.text.toString())
                        val record = checkMasks(view, source, "field-${index + 1}-${if (orientation == Configuration.ORIENTATION_PORTRAIT) "portrait" else "landscape"}",
                            activity.getColor(R.color.lesson_canvas))
                        record.put("source", target.key).put("sourceAssetSha256", sha256(originals.getValue(target.relative)))
                            .put("selectedTitle", target.title).put("italicRanges", JSONArray(expected.filter { it.style == LessonParagraphEmphasis.Style.ITALIC }
                                .map { JSONArray(listOf(it.start, it.endExclusive)) }))
                            .put("orientation", orientation).put("blockType", originalBlock.getString("type"))
                            .put("originalText", body).put("visibleText", source).put("sourceBodyStart", source.length - body.length)
                        records.put(record)
                        assertEquals("Only presentation metadata is proposed, never a second source", originalBlocks[index].toString(),
                            JSONObject(proposed[index].toString()).apply {
                                if (originalBlocks[index].has("emphasis")) put("emphasis", originalBlocks[index].get("emphasis")) else remove("emphasis")
                            }.toString())
                    }
                    if (index == 0) {
                        var targetView: View? = null
                        scenario.onActivity { activity -> targetView = paragraph(activity, originalBlocks[index].getString("text")); scrollVisible(activity, targetView!!) }
                        // Scroll and rotation may request another native layout. Obtain
                        // the view again only after the current Activity/window is ready.
                        awaitPage(scenario, lesson, id, index, orientation)
                        scenario.onActivity { targetView = paragraph(it, originalBlocks[index].getString("text")) }
                        capture("italic-reference-$session-$orientation", { requireNotNull(targetView) }, JSONObject()
                            .put("source", target.key).put("kind", "native-window-screenshot")
                            .put("fixtureOnly", true).put("audioTimingEvidence", false))
                    }
                }
            }
        }
        originals.forEach { (relative, bytes) -> assertArrayEquals("Every packaged curricular asset remains untouched", bytes,
            context.assets.open(ROOT + relative).use { it.readBytes() }) }
        File(evidence(), "registered-title-fixtures.json").writeText(JSONObject().put("records", records)
            .put("fieldCount", targets.size).put("citationOccurrences", 23).put("referenceTitles", 5)
            .put("source", "Optional metadata injected only into an isolated in-memory QA lesson")
            .put("audioTimingEvidence", false).toString(2))
    }

    @Test fun allExistingAuthoredBoldEntriesKeepTheirDefaultStyleWithoutMigration() {
        var paragraphs = 0
        var fragments = 0
        fun walk(path: String) {
            context.assets.list(path).orEmpty().forEach { name ->
                val full = "$path/$name"
                if (!name.endsWith(".json")) walk(full) else {
                    val lesson = JSONObject(context.assets.open(full).bufferedReader().use { it.readText() })
                    val pages = lesson.optJSONArray("pages") ?: return@forEach
                    for (p in 0 until pages.length()) {
                        val blocks = pages.getJSONObject(p).optJSONArray("blocks") ?: continue
                        for (b in 0 until blocks.length()) {
                            val block = blocks.getJSONObject(b)
                            if (block.optString("type") != "paragraph" || !block.has("emphasis")) continue
                            paragraphs++
                            val entries = block.getJSONArray("emphasis")
                            val ranges = LessonParagraphEmphasis.styledRanges(block.getString("text"), entries)
                            assertEquals(entries.length(), ranges.size)
                            assertTrue(ranges.all { it.style == LessonParagraphEmphasis.Style.BOLD })
                            fragments += ranges.size
                        }
                    }
                }
            }
        }
        walk(ROOT.removeSuffix("/"))
        assertEquals("Current authored inventory, separate from the historical 47-paragraph fixture", 152, paragraphs)
        assertEquals(278, fragments)
    }

    private fun awaitPage(scenario: ActivityScenario<LessonActivity>, lesson: JSONObject, id: String, page: Int, orientation: Int) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        var lastObserved = "No native Activity observation"
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            val observedState = scenario.state
            var ready = false
            scenario.onActivity { activity ->
                if (activity.resources.configuration.orientation != orientation) return@onActivity
                val source = presentedText(activity, lesson.getJSONArray("pages").getJSONObject(page).getJSONArray("blocks").getJSONObject(0))
                if (field(activity, "currentLessonId") != id || field(activity, "currentPage") != page ||
                    descendants(content(activity)).filterIsInstance<ReadingTextView>().none { it.text.toString() == source }) {
                    setField(activity, "currentLessonId", id)
                    setField(activity, "lessonJson", JSONObject(lesson.toString()))
                    setField(activity, "totalPages", lesson.getJSONArray("pages").length())
                    setField(activity, "currentPage", page)
                    listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                        (field(activity, it) as MutableMap<*, *>).clear()
                    }
                    LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }.invoke(activity)
                }
                val view = descendants(content(activity)).filterIsInstance<ReadingTextView>().singleOrNull { it.text.toString() == source }
                val resumed = observedState == Lifecycle.State.RESUMED
                val focused = activity.hasWindowFocus() && view?.hasWindowFocus() == true
                val attached = view?.isAttachedToWindow == true && view.rootView === activity.window.decorView
                val laidOut = view?.layout != null && view.width > 0 && view.height > 0 &&
                    !view.isLayoutRequested && !activity.window.decorView.isLayoutRequested
                val decor = activity.window.decorView
                val windowOrientationMatches = if (orientation == Configuration.ORIENTATION_PORTRAIT)
                    decor.width < decor.height else decor.width > decor.height
                ready = resumed && focused && attached && laidOut && windowOrientationMatches
                lastObserved = "state=$observedState, activity=${System.identityHashCode(activity)}, focused=$focused, " +
                    "attached=$attached, laidOut=$laidOut, decor=${decor.width}x${decor.height}, requestedOrientation=$orientation"
            }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The real paragraph did not finish native layout/focus for page $page and orientation $orientation: $lastObserved")
    }

    private fun paragraph(activity: LessonActivity, source: String) = descendants(content(activity))
        .filterIsInstance<ReadingTextView>().single { it.text.toString() == source }

    private fun presentedText(activity: LessonActivity, block: JSONObject): String =
        if (block.getString("type") == "note") activity.getString(R.string.lesson_note, block.getString("text"))
        else block.getString("text")

    private fun checkMasks(view: ReadingTextView, source: String, stem: String, surface: Int): JSONObject {
        assertTrue(view.safeSupportsWordReading)
        val content = view.text
        val layout = view.layout
        val dimensions = view.width to view.height
        val spans = snapshot(content as Spanned)
        val full = draw(view)
        val boundary = Regex("\\S+").findAll(source).drop(2).first().range.first
        val colors = LessonReadingTextColors().apply { register(0, view) }
        fun pending(from: Int) = colors.show(mapOf(0 to ReadingBlockState.CURRENT), true,
            unreadWords = mapOf(0 to listOf(ReadingTextRange(from, source.length))))
        pending(0)
        val unread = draw(view)
        pending(boundary)
        val progressive = draw(view)
        assertFalse(full.sameAs(progressive))
        assertFalse(unread.sameAs(progressive))
        pending(0)
        val rewind = draw(view)
        assertTrue("Rewind retains the exact native styles and restores pending words", unread.sameAs(rewind))
        pending(boundary)
        val resumed = draw(view)
        assertTrue(progressive.sameAs(resumed))
        colors.show(mapOf(0 to ReadingBlockState.READ), true)
        val completed = draw(view)
        assertTrue(full.sameAs(completed))
        assertSame(content, view.text)
        assertSame(layout, view.layout)
        assertEquals(dimensions, view.width to view.height)
        assertEquals(spans, snapshot(view.text as Spanned))
        assertTrue(LessonReadingColor.contrast(LessonReadingColor.unread(surface), surface) >= LessonReadingColor.MIN_CONTRAST)
        val fullFile = File(evidence(), "$stem-canvas-complete.png")
        val partialFile = File(evidence(), "$stem-canvas-progressive.png")
        fullFile.outputStream().use { assertTrue(full.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        partialFile.outputStream().use { assertTrue(progressive.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        colors.clear()
        listOf(full, unread, progressive, rewind, resumed, completed).forEach(Bitmap::recycle)
        return JSONObject().put("kind", "native-view-canvas").put("audioTimingEvidence", false)
            .put("completeFile", fullFile.name).put("progressiveFile", partialFile.name)
            .put("completeSha256", sha256(fullFile.readBytes())).put("progressiveSha256", sha256(partialFile.readBytes()))
            .put("wordMasksReversible", true).put("textSpansAndLayoutIdentityPreserved", true)
    }

    private fun measure(view: View) {
        view.measure(View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }
    private fun snapshot(text: Spanned) = text.getSpans(0, text.length, Any::class.java).map {
        listOf(it, text.getSpanStart(it), text.getSpanEnd(it), text.getSpanFlags(it))
    }
    private fun evidence() = File(requireNotNull(context.getExternalFilesDir(null)), "paragraph-italics-qa/$session").apply { mkdirs() }
    private fun entry(text: String, style: String) = JSONObject().put("text", text).put("style", style)
    private fun sha256(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun targets(): List<Target> {
        val references = "archivos_y_programa_completo/lesson_referencias_del_curso.json"
        val title = "Working Draft: Programming Languages — C++"
        return listOf(
            Target(references, 1, 2, "Dictionary of algorithms and data structures"),
            Target(references, 1, 3, "Programming languages — C"),
            Target(references, 1, 4, "Working draft, standard for programming language C++"),
            Target(references, 1, 5, "Programming: Principles and practice using C++"),
            Target(references, 1, 6, "Working draft: Programming languages — C++"),
            Target("aleatoriedad_y_reproducibilidad/lesson_producir_enteros_en_un_intervalo.json", 7, 4, title),
            Target("aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json", 8, 7, title),
            Target("aleatoriedad_y_reproducibilidad/lesson_semillas_continuacion_y_reinicio.json", 7, 4, title),
            Target("aleatoriedad_y_reproducibilidad/lesson_validar_la_generacion_y_compartir_el_motor.json", 7, 4, title),
            Target("archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json", 8, 6, title),
            Target("archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json", 7, 6, title),
            Target("colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json", 8, 7, title),
            Target("datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json", 9, 2, title),
            Target("datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json", 14, 7, title),
            Target("datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json", 15, 4, title),
            Target("datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json", 16, 4, title),
            Target("funciones_y_contratos/lesson_conversion_comprobada_sin_perdida.json", 7, 7, title),
            Target("funciones_y_contratos/lesson_espacios_de_nombres_y_nombres_calificados.json", 11, 5, title),
            Target("funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json", 12, 4, title),
            Target("funciones_y_contratos/lesson_raiz_cuadrada_dominio_y_resultado_aproximado.json", 8, 6, title),
            Target("funciones_y_contratos/lesson_redondeo_deliberado_y_conversion_segura.json", 8, 6, title),
            Target("funciones_y_contratos/lesson_tipo_de_retorno_explicito_y_deducido.json", 11, 4, title),
            Target("repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json", 5, 6, title),
            Target("repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json", 5, 2, title),
            Target("repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json", 7, 3, title),
            Target("repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json", 10, 7, title),
            Target("texto_interpretacion_y_flujos/lesson_construir_un_lector_de_entrada_preparada.json", 8, 4, title))
    }

    companion object { private const val ROOT = "content/es/programming/cpp_fundamentals/" }
}

package com.learngraphicsprogramming.app

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Rect
import android.os.SystemClock
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.math.MathGeometryQaSupport
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor

/** Native pixels and baselines, using only uniquely identified in-memory QA lessons.
 * The inherited guard rejects the normal package and restores every QA preference. */
@RunWith(AndroidJUnit4::class)
class CppLineNumbersDeviceTest : MathGeometryQaSupport() {
    @Test fun authoredCodeAndPredictionsKeepCompleteNumbersAndTheirExistingAnswers() {
        for ((lesson, page, index) in listOf(
            Triple(VERSIONS, 2, 2), Triple(VERSIONS, 5, 3), Triple(HYPOTHESES, 3, 3)
        )) {
            val source = LessonRepository.loadLessonJson(context, "programming", lesson)
            val block = JSONObject(source.getJSONArray("pages").getJSONObject(page)
                .getJSONArray("blocks").getJSONObject(index).toString())
            val untouched = block.toString()
            withCodeFixture(block) { scenario, identity ->
                for ((orientation, scale) in listOf(
                    Configuration.ORIENTATION_PORTRAIT to 100,
                    Configuration.ORIENTATION_LANDSCAPE to 150
                )) {
                    orientCode(scenario, identity, block, orientation, scale)
                    lateinit var gutter: TextView
                    lateinit var code: TextView
                    scenario.onActivity { activity ->
                        gutter = descendants(content(activity)).filterIsInstance<CodeLineNumbersView>().single()
                        code = shownCode(activity, block.getString("code"))
                        assertLogicalLabels(gutter, code.layout.lineCount)
                        val line = minOf(10, code.layout.lineCount)
                        scrollToCodeRow(activity, code, line - 1)
                    }
                    val tag = "authored-${lesson.substringAfterLast("__")}-p${page + 1}-b${index + 1}-$orientation-$scale"
                    captureNumber(tag, gutter, minOf(10, code.layout.lineCount) - 1, code,
                        JSONObject().put("originalLessonId", lesson).put("page", page + 1).put("block", index + 1))
                    scenario.onActivity { activity ->
                        descendants(content(activity)).filterIsInstance<HorizontalScrollView>()
                            .firstOrNull { it.getChildAt(0) === code }?.let { scroll ->
                                val gutterX = gutter.left
                                scroll.scrollTo((code.width - scroll.width).coerceAtLeast(0), 0)
                                assertEquals("Horizontal source movement never moves the gutter", gutterX, gutter.left)
                            }
                    }
                }
                if (block.getString("type") == "output_prediction") {
                    scenario.onActivity { activity ->
                        val input = descendants(content(activity)).filterIsInstance<EditText>().single()
                        input.setText("__respuesta_qa_incorrecta__")
                        answerButton(activity).performClick()
                        assertFalse(requireNotNull(ProgressRepository.getExerciseState(activity, identity, 0, 0))
                            .getBoolean("correct"))
                        input.setText(block.getJSONArray("answers").getString(0))
                        answerButton(activity).performClick()
                        assertTrue(requireNotNull(ProgressRepository.getExerciseState(activity, identity, 0, 0))
                            .getBoolean("correct"))
                    }
                    scenario.recreate()
                    awaitLesson(scenario, identity, block)
                    scenario.onActivity { activity ->
                        assertEquals(block.getJSONArray("answers").getString(0),
                            descendants(content(activity)).filterIsInstance<EditText>().single().text.toString())
                        assertTrue(requireNotNull(ProgressRepository.getExerciseState(activity, identity, 0, 0))
                            .getBoolean("correct"))
                    }
                }
            }
            assertEquals("No authored source, IDs, answers or properties are rewritten", untouched, block.toString())
        }
    }

    @Test fun staticAndInteractiveGuttersFitOneToFourDigitsAfterResizingAndScaling() {
        val source = (1..1005).joinToString("\n") { "// linea $it" }
        val block = JSONObject().put("type", "code").put("language", "cpp").put("code", source)
        withCodeFixture(block) { scenario, identity ->
            for ((orientation, scale) in listOf(
                Configuration.ORIENTATION_PORTRAIT to 100,
                Configuration.ORIENTATION_LANDSCAPE to 150
            )) {
                orientCode(scenario, identity, block, orientation, scale)
                lateinit var gutter: TextView
                lateinit var code: TextView
                scenario.onActivity { activity ->
                    gutter = descendants(content(activity)).filterIsInstance<CodeLineNumbersView>().single()
                    code = shownCode(activity, source)
                    assertEquals(source, code.text.toString())
                    assertLogicalLabels(gutter, 1005)
                }
                for (number in listOf(1, 10, 100, 1000)) {
                    scenario.onActivity { scrollToCodeRow(it, code, number - 1) }
                    captureNumber("static-$number-$orientation-$scale", gutter, number - 1, code)
                }
            }
            // The same native shared column used by order/fill/inline-fill rows, with
            // independent TextViews, retains a common right edge as labels grow.
            lateinit var column: CodeLineNumberColumn
            scenario.onActivity { activity ->
                content(activity).removeAllViews()
                column = CodeLineNumberColumn(activity).apply {
                    orientation = LinearLayout.VERTICAL
                    setBackgroundColor(Color.rgb(24, 26, 31))
                    layoutParams = LinearLayout.LayoutParams(18, ViewGroup.LayoutParams.WRAP_CONTENT)
                }
                for (number in listOf(1, 10, 100, 1000)) column.addView(TextView(activity).apply {
                    text = number.toString()
                    typeface = android.graphics.Typeface.MONOSPACE
                    textSize = 27f
                    includeFontPadding = false
                    gravity = android.view.Gravity.END
                    setTextColor(Color.rgb(111, 119, 130))
                    setPadding(0, 0, 12, 0)
                    layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT)
                })
                content(activity).addView(column)
                activity.findViewById<ScrollView>(R.id.lessonScroll).scrollTo(0, 0)
            }
            instrumentation.waitForIdleSync()
            for (index in 0 until 4) {
                val label = column.getChildAt(index) as TextView
                assertEquals(1, label.layout.lineCount)
                assertEquals(column.width, label.width)
                captureNumber("interactive-column-${label.text}", label, 0)
            }
        }
    }

    @Test fun editorNumbersRemainWholeAlignedAndScrollableWithBlankWrappedContinuations() {
        val sourceText = (1..1005).joinToString("\n") {
            "int value_$it = $it; // " + "long_expression_token ".repeat(6)
        }
        ActivityScenario.launch(CppPrototypeActivity::class.java).use { scenario ->
            for ((orientation, factor) in listOf(Configuration.ORIENTATION_PORTRAIT to 1f,
                Configuration.ORIENTATION_LANDSCAPE to 1.5f)) {
                scenario.onActivity { activity ->
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    activity.requestedOrientation = requested(orientation)
                }
                awaitEditor(scenario) { it.resources.configuration.orientation == orientation && it.hasWindowFocus() }
                scenario.onActivity { activity ->
                    val input = activity.findViewById<FreeScrollEditText>(R.id.sourceCode)
                    val gutter = activity.findViewById<TextView>(R.id.lineNumbers)
                    input.showSoftInputOnFocus = false
                    input.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18 * factor)
                    gutter.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18 * factor)
                    input.setText(sourceText)
                    input.setSelection(0)
                    input.clearFocus()
                }
                awaitEditor(scenario) { activity ->
                    val input = activity.findViewById<FreeScrollEditText>(R.id.sourceCode)
                    val gutter = activity.findViewById<TextView>(R.id.lineNumbers)
                    input.layout?.lineCount == 1005 && gutter.layout?.lineCount == 1005 &&
                        activity.findViewById<SeekBar>(R.id.editorVerticalBar).max > 0
                }
                for (number in listOf(1, 10, 100, 1000)) {
                    lateinit var gutter: TextView
                    lateinit var input: FreeScrollEditText
                    scenario.onActivity { activity ->
                        input = activity.findViewById(R.id.sourceCode)
                        gutter = activity.findViewById(R.id.lineNumbers)
                        input.suppressCursorAutoScroll = true
                        input.scrollTo(0, input.layout.getLineTop(number - 1))
                        assertLogicalLabels(gutter, 1005)
                        assertEquals(sourceText, input.text.toString())
                        assertEquals(0, input.selectionStart)
                        val outer = activity.findViewById<ScrollView>(R.id.compilerScroll)
                        val body = activity.findViewById<View>(R.id.codeEditorBody)
                        val rect = Rect()
                        body.getDrawingRect(rect)
                        outer.offsetDescendantRectToMyCoords(body, rect)
                        outer.scrollTo(0, rect.top)
                    }
                    captureNumber("editor-$number-$orientation-${factor * 100}", gutter, number - 1, input)
                }
                scenario.onActivity { activity ->
                    activity.findViewById<View>(R.id.alignCodeButton).performClick()
                }
                awaitEditor(scenario) { activity ->
                    val input = activity.findViewById<FreeScrollEditText>(R.id.sourceCode)
                    val gutter = activity.findViewById<TextView>(R.id.lineNumbers)
                    val rows = gutter.text.toString().split('\n')
                    input.layout.lineCount > 1005 && gutter.layout?.lineCount == input.layout.lineCount &&
                        rows.filter(String::isNotEmpty) == (1..1005).map(Int::toString) &&
                        rows.any(String::isEmpty) && activity.findViewById<SeekBar>(R.id.editorHorizontalBar).max == 0
                }
                // Restore the ordinary unwrapped state before changing orientation.
                scenario.onActivity { it.findViewById<View>(R.id.alignCodeButton).performClick() }
                awaitEditor(scenario) { it.findViewById<FreeScrollEditText>(R.id.sourceCode).layout.lineCount == 1005 }
            }
        }
    }

    private fun assertLogicalLabels(gutter: TextView, count: Int) {
        assertEquals((1..count).map(Int::toString), gutter.text.toString().split('\n'))
        assertEquals("One native visual row per complete logical number", count, gutter.layout.lineCount)
        assertTrue(gutter.width - gutter.compoundPaddingLeft - gutter.compoundPaddingRight >=
            ceil(gutter.paint.measureText(count.toString()).toDouble()).toInt())
    }

    /** Checks every digit's physical-screen glyph pixels, plus whole-number row/baseline geometry. */
    private fun captureNumber(name: String, label: TextView, row: Int, source: TextView? = null,
                              extra: JSONObject = JSONObject()) {
        instrumentation.waitForIdleSync()
        lateinit var glyphRects: List<Rect>
        instrumentation.runOnMainSync {
            assertTrue("Visible evidence belongs to the focused real Android window", label.hasWindowFocus())
            val layout = requireNotNull(label.layout)
            val start = layout.getLineStart(row)
            val end = layout.getLineEnd(row)
            val token = label.text.subSequence(start, end).toString().trimEnd('\n')
            assertTrue(token.matches(Regex("[0-9]+")))
            assertEquals("Every digit belongs to the same visual row", row, layout.getLineForOffset(start + token.lastIndex))
            val location = IntArray(2).also(label::getLocationOnScreen)
            val baseY = location[1] + label.totalPaddingTop - label.scrollY
            val baseline = baseY + layout.getLineBaseline(row)
            if (source != null) {
                val sourceLocation = IntArray(2).also(source::getLocationOnScreen)
                val sourceBaseline = sourceLocation[1] + source.totalPaddingTop - source.scrollY +
                    source.layout.getLineBaseline(row)
                assertTrue("Each whole number stays aligned with its source row: $baseline / $sourceBaseline",
                    abs(baseline - sourceBaseline) <= 2)
            }
            val visible = Rect()
            assertTrue(label.getGlobalVisibleRect(visible))
            val rootOrigin = IntArray(2).also(label.rootView::getLocationOnScreen)
            visible.offset(rootOrigin[0], rootOrigin[1])
            glyphRects = token.indices.map { digit ->
                val x = location[0] + label.totalPaddingLeft - label.scrollX
                Rect(floor(x + layout.getPrimaryHorizontal(start + digit)).toInt(),
                    baseY + layout.getLineTop(row),
                    ceil(x + layout.getPrimaryHorizontal(start + digit + 1)).toInt(),
                    baseY + layout.getLineBottom(row)).also {
                    assertTrue("Complete digit $digit of $token is visible on the physical display: $it / $visible",
                        visible.contains(it))
                }
            }
            extra.put("number", token).put("row", row).put("baselineDisplayPx", baseline)
                .put("gutterWidthPx", label.width).put("fontPixels", label.textSize)
                .put("digitBounds", JSONArray(glyphRects.map { JSONArray(listOf(it.left, it.top, it.right, it.bottom)) }))
        }
        var previous: Bitmap? = null
        var stable = 0
        var finalScreen: Bitmap? = null
        try {
            repeat(24) {
                if (stable >= 4) return@repeat
                val screen = requireNotNull(instrumentation.uiAutomation.takeScreenshot())
                val region = glyphRects.reduce { a, b -> Rect(a).apply { union(b) } }
                val crop = Bitmap.createBitmap(screen, region.left, region.top, region.width(), region.height())
                stable = if (previous?.sameAs(crop) == true) stable + 1 else 1
                previous?.recycle()
                previous = crop
                finalScreen?.recycle()
                finalScreen = screen
                if (stable < 4) SystemClock.sleep(200)
            }
            assertTrue("Wait for stable physical glyph pixels after rotation/scroll", stable >= 4)
            val screen = requireNotNull(finalScreen)
            val foreground = label.currentTextColor
            val counts = glyphRects.map { bounds ->
                var count = 0
                for (y in bounds.top until bounds.bottom) for (x in bounds.left until bounds.right) {
                    val pixel = screen.getPixel(x, y)
                    if (abs(Color.red(pixel) - Color.red(foreground)) < 36 &&
                        abs(Color.green(pixel) - Color.green(foreground)) < 36 &&
                        abs(Color.blue(pixel) - Color.blue(foreground)) < 36) count++
                }
                assertTrue("Every full digit is actually painted in its own horizontal slot: $bounds", count > 2)
                count
            }
            extra.put("glyphPixelCounts", JSONArray(counts)).put("stableFrames", stable)
                .put("capture", "complete unmodified native display screenshot")
            val directory = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-line-numbers-qa").apply { mkdirs() }
            File(directory, "$name.png").outputStream().use {
                assertTrue(screen.compress(Bitmap.CompressFormat.PNG, 100, it))
            }
            File(directory, "$name.json").writeText(extra.toString(2))
        } finally { previous?.recycle(); finalScreen?.recycle() }
    }

    private fun withCodeFixture(block: JSONObject, test: (ActivityScenario<LessonActivity>, String) -> Unit) {
        val identity = "cpp_fundamentals__programas_y_ejecucion__gutter_qa_${UUID.randomUUID().toString().replace("-", "")}"
        val sourceLesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
        ProgressRepository.saveCurrentPage(context, "programming", sourceLesson, 0)
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, COURSE, true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 100)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "programas_y_ejecucion")
            .putExtra("lessonId", sourceLesson).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity { injectCode(it, identity, block) }
            awaitLesson(scenario, identity, block)
            test(scenario, identity)
        }
    }

    private fun injectCode(activity: LessonActivity, identity: String, block: JSONObject) {
        setField(activity, "currentCourseId", COURSE)
        setField(activity, "currentLessonId", identity)
        setField(activity, "lessonJson", JSONObject().put("id", identity).put("title", "Verificación técnica aislada")
            .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(JSONObject(block.toString()))))))
        setField(activity, "totalPages", 1)
        setField(activity, "currentPage", 0)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
            @Suppress("UNCHECKED_CAST")
            (field(activity, it) as MutableMap<Int, Any>).clear()
        }
        invoke(activity, "renderCurrentPage")
    }

    private fun orientCode(scenario: ActivityScenario<LessonActivity>, identity: String, block: JSONObject,
                           orientation: Int, scale: Int) {
        scenario.onActivity { it.requestedOrientation = requested(orientation) }
        awaitLesson(scenario, identity, block, orientation)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, scale)
        scenario.onActivity { invoke(it, "refreshLessonContentTextScale") }
        instrumentation.waitForIdleSync()
    }

    private fun awaitLesson(scenario: ActivityScenario<LessonActivity>, identity: String, block: JSONObject,
                            orientation: Int? = null) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { activity ->
                if (orientation == null || activity.resources.configuration.orientation == orientation) {
                    if (field(activity, "currentLessonId") != identity) injectCode(activity, identity, block)
                    val gutter = descendants(content(activity)).filterIsInstance<CodeLineNumbersView>().singleOrNull()
                    ready = activity.hasWindowFocus() && gutter?.layout != null && gutter.width > 0
                }
            }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The real C++ lesson gutter did not finish native layout")
    }

    private fun awaitEditor(scenario: ActivityScenario<CppPrototypeActivity>, condition: (CppPrototypeActivity) -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { ready = condition(it) }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The real editor geometry did not finish native layout")
    }

    private fun shownCode(activity: LessonActivity, original: String) = descendants(content(activity))
        .filterIsInstance<TextView>().single {
            it !is CodeLineNumbersView && it !is EditText &&
                it.text.toString().count { c -> c == '\n' } == original.count { c -> c == '\n' } &&
                it.text.toString().startsWith(original.lineSequence().first())
        }

    private fun scrollToCodeRow(activity: LessonActivity, code: TextView, row: Int) {
        val scroll = activity.findViewById<ScrollView>(R.id.lessonScroll)
        val bounds = Rect(0, code.paddingTop + code.layout.getLineTop(row), code.width,
            code.paddingTop + code.layout.getLineBottom(row))
        scroll.offsetDescendantRectToMyCoords(code, bounds)
        scroll.scrollTo(0, (bounds.top - 16 * activity.resources.displayMetrics.density).toInt().coerceAtLeast(0))
    }

    private fun answerButton(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Button>()
        .single { it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct)) }
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)
    private fun requested(orientation: Int) = if (orientation == Configuration.ORIENTATION_PORTRAIT)
        ActivityInfo.SCREEN_ORIENTATION_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

    companion object {
        private const val COURSE = "programming_practice_tools"
        private const val VERSIONS = "programming_practice_tools__evidencia_y_diagnostico__versiones_del_fuente_y_del_ejecutable"
        private const val HYPOTHESES = "programming_practice_tools__evidencia_y_diagnostico__reducir_un_caso_y_contrastar_hipotesis"
    }
}

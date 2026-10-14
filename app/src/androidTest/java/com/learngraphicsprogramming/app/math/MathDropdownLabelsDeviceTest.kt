package com.learngraphicsprogramming.app.math

import android.content.res.Configuration
import android.graphics.Color
import android.os.SystemClock
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.widget.ListView
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.widget.AppCompatSpinner
import androidx.appcompat.widget.ListPopupWindow
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Uses the unchanged p2/b5 published block, actual Spinner popup windows, Check listeners
 * and LessonActivity recreation. Native layouts/pixels, rather than text extraction, prove
 * that every authored option and selected answer can be read in full. */
@RunWith(AndroidJUnit4::class)
class MathDropdownLabelsDeviceTest : MathGeometryQaSupport() {
    @Test fun sameInlineSpinnerReflowsWhenItsViewportNarrowsAndLessonScaleChanges() {
        val published = publishedFixture()
        val authoredField = published.block.getJSONArray("responses").getJSONObject(0)
            .getJSONArray("fields").getJSONObject(2)
        val response = JSONObject().put("id", "qa_inline__answer").put("type", "math_multi_input")
            .put("fields", JSONArray().put(JSONObject(authoredField.toString())))
            .put("segments", JSONArray().put(JSONObject().put("field", "pertenencia")))
            .put("validator", JSONObject().put("type", "fields").put("fields", JSONObject()
                .put("pertenencia", JSONObject().put("type", "exact_text").put("expected", "pertenece"))))
        val fixture = fixture(JSONObject().put("type", "math_puzzle").put("schemaVersion", 1)
            .put("id", "qa_inline_dropdown_resize").put("prompt", "Verificación técnica de reflujo de una selección conservada.")
            .put("representations", JSONArray()).put("tools", JSONArray()).put("responses", JSONArray().put(response))
            .put("feedback", JSONObject().put("correct", "Dato conservado.").put("incorrect", "Revisa la pertenencia indicada.")))
        val completeLabel = authoredField.getJSONArray("options").getJSONObject(2).getString("label")
        withFixture(fixture) { scenario ->
            orient(scenario, fixture, Configuration.ORIENTATION_PORTRAIT)
            var originalSpinner: Spinner? = null
            var viewport: HorizontalScrollView? = null
            val density = context.resources.displayMetrics.density
            val wideWidth = (300 * density).toInt()
            val narrowWidth = (160 * density).toInt()
            scenario.onActivity { activity ->
                originalSpinner = spinners(activity).single()
                viewport = inlineViewport(requireNotNull(originalSpinner))
                requireNotNull(viewport).layoutParams = LinearLayout.LayoutParams(wideWidth, ViewGroup.LayoutParams.WRAP_CONTENT)
                requireNotNull(originalSpinner).setSelection(3)
            }
            awaitInlineLayout(scenario, requireNotNull(originalSpinner), requireNotNull(viewport), wideWidth)
            var wideLineCount = 0
            scenario.onActivity {
                wideLineCount = assertCompleteNativeText(requireNotNull(originalSpinner).selectedView as TextView, completeLabel)
                    .getInt("lineCount")
                requireNotNull(viewport).layoutParams = LinearLayout.LayoutParams(narrowWidth, ViewGroup.LayoutParams.WRAP_CONTENT)
            }
            awaitInlineLayout(scenario, requireNotNull(originalSpinner), requireNotNull(viewport), narrowWidth)
            scenario.onActivity { activity ->
                assertSame("Width changes reuse the same selected control without recreation or reselection",
                    originalSpinner, spinners(activity).single())
                val label = requireNotNull(originalSpinner).selectedView as TextView
                assertCompleteNativeText(label, completeLabel)
                assertTrue("The entire inline selection fits its newly narrowed viewport",
                    requireNotNull(originalSpinner).width <= requireNotNull(viewport).width)
                assertTrue("A narrower control reflows the existing selected label into more native lines", label.layout.lineCount > wideLineCount)
                assertEquals(3, requireNotNull(originalSpinner).selectedItemPosition)
                // Invoke the existing application action that rescales cached mathematical
                // content. This uses both MathTextScaleAware and its ordinary TextViews.
                LessonTextSizeSettingsRepository.saveGlobalScale(activity, 150)
                LessonActivity::class.java.getDeclaredMethod("refreshLessonContentTextScale")
                    .apply { isAccessible = true }.invoke(activity)
            }
            awaitInlineLayout(scenario, requireNotNull(originalSpinner), requireNotNull(viewport), narrowWidth)
            var finalLabel: TextView? = null
            var metadata = JSONObject()
            scenario.onActivity { activity ->
                assertSame(originalSpinner, spinners(activity).single())
                finalLabel = requireNotNull(originalSpinner).selectedView as TextView
                metadata = assertCompleteNativeText(requireNotNull(finalLabel), completeLabel)
                    .put("sameSpinnerAcrossResize", true).put("wideViewportWidth", wideWidth).put("narrowViewportWidth", narrowWidth)
                    .put("wideLineCount", wideLineCount).put("lessonScale", 150).put("selectedOptionId", "cerca")
                assertEquals(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 27f, requireNotNull(finalLabel).resources.displayMetrics),
                    requireNotNull(finalLabel).textSize, 1.1f)
                assertTrue(requireNotNull(originalSpinner).width <= requireNotNull(viewport).width)
                scrollVisible(activity, requireNotNull(originalSpinner))
                check(activity).performClick()
                assertState(activity, fixture, false, MathAnswer.Fields(mapOf("pertenencia" to "cerca")))
            }
            capture("inline-same-spinner-narrow-large", { requireNotNull(finalLabel) }, metadata)
            scenario.recreate()
            scenario.onActivity { activity ->
                inject(activity, fixture)
                assertState(activity, fixture, false, MathAnswer.Fields(mapOf("pertenencia" to "cerca")))
                val recreatedViewport = inlineViewport(spinners(activity).single())
                recreatedViewport.layoutParams = LinearLayout.LayoutParams(narrowWidth, ViewGroup.LayoutParams.WRAP_CONTENT)
                viewport = recreatedViewport
                originalSpinner = spinners(activity).single()
            }
            awaitInlineLayout(scenario, requireNotNull(originalSpinner), requireNotNull(viewport), narrowWidth)
            scenario.onActivity { activity ->
                assertCompleteNativeText(requireNotNull(originalSpinner).selectedView as TextView, completeLabel)
                assertEquals(3, requireNotNull(originalSpinner).selectedItemPosition)
                requireNotNull(originalSpinner).setSelection(1)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertCompleteNativeText(requireNotNull(originalSpinner).selectedView as TextView,
                    authoredField.getJSONArray("options").getJSONObject(0).getString("label"))
                check(activity).performClick()
                assertState(activity, fixture, true, MathAnswer.Fields(mapOf("pertenencia" to "pertenece")))
            }
        }
    }

    private fun awaitInlineLayout(scenario: ActivityScenario<LessonActivity>, spinner: Spinner,
                                  viewport: HorizontalScrollView, expectedWidth: Int) {
        val deadline = SystemClock.uptimeMillis() + 4_000
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var ready = false
            scenario.onActivity { ready = viewport.width == expectedWidth && !viewport.isLayoutRequested &&
                !spinner.isLayoutRequested && (spinner.selectedView as? TextView)?.layout != null }
            if (ready) return
            SystemClock.sleep(50)
        }
        fail("The same inline control did not finish remeasuring at the new viewport width")
    }

    @Test fun publishedMenusAndSelectedAnswersAreCompleteInBothOrientationsAndAtLargeLessonScale() {
        val fixture = publishedFixture()
        withFixture(fixture) { scenario ->
            for ((orientation, scale, name) in listOf(
                Triple(Configuration.ORIENTATION_PORTRAIT, 100, "portrait"),
                Triple(Configuration.ORIENTATION_LANDSCAPE, 150, "landscape-large"))) {
                scenario.onActivity { activity ->
                    LessonTextSizeSettingsRepository.saveGlobalScale(activity, scale)
                    ProgressRepository.resetExerciseState(activity, ROUTE, fixture.id, fixture.page, fixture.index)
                    inject(activity, fixture)
                }
                orient(scenario, fixture, orientation)
                for (fieldIndex in 0..2) inspectActualPopup(scenario, fixture, fieldIndex, "$name-menu-${FIELD_IDS[fieldIndex]}", scale)
                // A complete incorrect answer remains editable, and every selected distractor
                // is inspected and captured before a later correct answer locks the controls.
                setAnswers(scenario, fixture, WRONG)
                scenario.onActivity { activity ->
                    check(activity).performClick()
                    assertState(activity, fixture, false, MathAnswer.Fields(WRONG))
                }
                captureSelections(scenario, fixture, WRONG, "$name-incorrect")
                scenario.recreate()
                scenario.onActivity { inject(it, fixture) }
                awaitLayout(scenario, fixture, orientation)
                scenario.onActivity { assertState(it, fixture, false, MathAnswer.Fields(WRONG)) }
                inspectSelections(scenario, WRONG)
                setAnswers(scenario, fixture, CORRECT)
                scenario.onActivity { activity ->
                    check(activity).performClick()
                    assertState(activity, fixture, true, MathAnswer.Fields(CORRECT))
                }
                captureSelections(scenario, fixture, CORRECT, "$name-correct")
                scenario.recreate()
                scenario.onActivity { inject(it, fixture) }
                awaitLayout(scenario, fixture, orientation)
                scenario.onActivity { activity ->
                    assertState(activity, fixture, true, MathAnswer.Fields(CORRECT))
                    assertTrue(spinners(activity).all { !it.isEnabled })
                }
                inspectSelections(scenario, CORRECT)
            }
        }
    }

    @Test fun preCorrectionFingerprintAndCorrectChoiceIdsRemainRecognizedWithoutContentMigration() {
        val fixture = publishedFixture()
        val response = fixture.block.getJSONArray("responses").getJSONObject(0).getString("id")
        val previousState = JSONObject().put("schemaVersion", 1).put("contentFingerprint", ORIGINAL_FINGERPRINT)
            .put("correct", true).put("responses", JSONObject().put(response, MathAnswerJson.encode(MathAnswer.Fields(CORRECT))))
            .put("tools", JSONObject())
        assertTrue(MathPuzzleState.isCurrentCorrect(fixture.block, previousState))
        ProgressRepository.saveExerciseState(context, ROUTE, fixture.id, fixture.page, fixture.index, previousState)
        withFixture(fixture) { scenario ->
            scenario.onActivity { assertState(it, fixture, true, MathAnswer.Fields(CORRECT)) }
            inspectSelections(scenario, CORRECT)
            scenario.recreate()
            scenario.onActivity { inject(it, fixture) }
            awaitLayout(scenario, fixture)
            scenario.onActivity { assertState(it, fixture, true, MathAnswer.Fields(CORRECT)) }
            inspectSelections(scenario, CORRECT)
        }
    }

    private fun publishedFixture(): Fixture {
        val original = context.assets.open(SOURCE_ASSET).bufferedReader().use { JSONObject(it.readText()) }
        assertEquals(SOURCE_ID, original.getString("id"))
        val block = original.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").getJSONObject(4)
        assertEquals("conservar_el_significado_de_nombres_y_datos", block.getString("id"))
        assertEquals(ORIGINAL_FINGERPRINT, MathPuzzleState.fingerprint(block))
        val fields = block.getJSONArray("responses").getJSONObject(0).getJSONArray("fields")
        assertEquals(FIELD_IDS, (0..2).map { fields.getJSONObject(it).getString("id") })
        assertEquals(listOf("un_punto", "dos_puntos", "sin_decidir"), optionIds(fields.getJSONObject(0)))
        assertEquals(listOf("un_punto", "dos_puntos", "sin_decidir"), optionIds(fields.getJSONObject(1)))
        assertEquals(listOf("pertenece", "fuera", "cerca"), optionIds(fields.getJSONObject(2)))
        return fixture(block, page = 1, index = 4)
    }

    private fun inspectActualPopup(scenario: ActivityScenario<LessonActivity>, fixture: Fixture,
                                   fieldIndex: Int, name: String, scale: Int) {
        var spinner: Spinner? = null
        var popup: ListPopupWindow? = null
        scenario.onActivity { activity ->
            spinner = spinners(activity)[fieldIndex]
            scrollVisible(activity, requireNotNull(spinner))
        }
        instrumentation.waitForIdleSync()
        scenario.onActivity {
            assertTrue("Open the real attached AppCompat popup", requireNotNull(spinner).performClick())
            popup = AppCompatSpinner::class.java.getDeclaredField("mPopup").apply { isAccessible = true }
                .get(spinner) as? ListPopupWindow
            assertNotNull("The real dropdown window is present, not an adapter-only drawing", popup)
            assertTrue(requireNotNull(popup).isShowing)
        }
        val deadline = SystemClock.uptimeMillis() + 3_000
        while (requireNotNull(popup).listView?.width == 0 && SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            SystemClock.sleep(50)
        }
        val list = requireNotNull(requireNotNull(popup).listView)
        val fields = fixture.block.getJSONArray("responses").getJSONObject(0).getJSONArray("fields")
        val options = fields.getJSONObject(fieldIndex).getJSONArray("options")
        val labels = listOf(context.getString(com.learngraphicsprogramming.app.R.string.math_ui_select_placeholder)) +
            (0 until options.length()).map { options.getJSONObject(it).getString("label") }
        val rows = JSONArray()
        assertEquals(4, list.count)
        // The native popup may scroll at large scale in landscape. Bring each row into the
        // real ListView viewport and check all its native lines, including the final glyphs.
        for (position in labels.indices) {
            instrumentation.runOnMainSync { list.setSelection(position) }
            instrumentation.waitForIdleSync()
            SystemClock.sleep(50)
            var row: TextView? = null
            instrumentation.runOnMainSync {
                row = visibleRow(list, position)
                rows.put(assertCompleteNativeText(requireNotNull(row), labels[position]).put("position", position))
                assertEquals("Popup rows inherit the configured lesson scale", TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP,
                    18f * scale / 100f, requireNotNull(row).resources.displayMetrics),
                    requireNotNull(row).textSize, 1.1f)
            }
            // An actual screenshot of every authored row is retained if scrolling is needed;
            // labels can never be credited merely because their model string is complete.
            if (position > 0) capture("$name-option-$position", { requireNotNull(row) }, JSONObject()
                .put("fieldId", FIELD_IDS[fieldIndex]).put("optionId", options.getJSONObject(position - 1).getString("id"))
                .put("lessonScale", scale).put("layout", rows.getJSONObject(rows.length() - 1)))
        }
        saveMetadata("$name-summary", JSONObject().put("fieldId", FIELD_IDS[fieldIndex]).put("rows", rows)
            .put("popupWidth", list.width).put("spinnerWidth", requireNotNull(spinner).width)
            .put("originalFingerprint", fixture.fingerprint).put("originalPage", fixture.page + 1).put("originalBlock", fixture.index + 1))
        // Exercise selection through the popup's real item listener before closing it.
        instrumentation.runOnMainSync {
            val position = optionIds(fields.getJSONObject(fieldIndex)).indexOf(WRONG.getValue(FIELD_IDS[fieldIndex])) + 1
            list.setSelection(position)
        }
        instrumentation.waitForIdleSync()
        instrumentation.runOnMainSync {
            val position = optionIds(fields.getJSONObject(fieldIndex)).indexOf(WRONG.getValue(FIELD_IDS[fieldIndex])) + 1
            assertTrue(list.performItemClick(visibleRow(list, position), position, list.adapter.getItemId(position)))
            assertFalse("Selecting an option dismisses the real popup", requireNotNull(popup).isShowing)
            assertEquals(position, requireNotNull(spinner).selectedItemPosition)
        }
    }

    private fun visibleRow(list: ListView, position: Int): TextView {
        val child = list.getChildAt(position - list.firstVisiblePosition)
        assertNotNull("The authored row is in the actual popup viewport", child)
        return (child as? TextView) ?: descendants(requireNotNull(child)).filterIsInstance<TextView>().single()
    }

    private fun setAnswers(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, values: Map<String, String>) {
        scenario.onActivity { activity ->
            val fields = fixture.block.getJSONArray("responses").getJSONObject(0).getJSONArray("fields")
            spinners(activity).forEachIndexed { index, spinner ->
                assertTrue(spinner.isEnabled)
                spinner.setSelection(optionIds(fields.getJSONObject(index)).indexOf(values.getValue(FIELD_IDS[index])) + 1)
            }
        }
        instrumentation.waitForIdleSync()
    }

    private fun inspectSelections(scenario: ActivityScenario<LessonActivity>, values: Map<String, String>) {
        instrumentation.waitForIdleSync()
        scenario.onActivity { activity ->
            val fields = publishedFixture().block.getJSONArray("responses").getJSONObject(0).getJSONArray("fields")
            spinners(activity).forEachIndexed { index, spinner ->
                val options = fields.getJSONObject(index).getJSONArray("options")
                val position = optionIds(fields.getJSONObject(index)).indexOf(values.getValue(FIELD_IDS[index]))
                assertEquals(position + 1, spinner.selectedItemPosition)
                assertCompleteNativeText(spinner.selectedView as TextView, options.getJSONObject(position).getString("label"))
            }
        }
    }

    private fun captureSelections(scenario: ActivityScenario<LessonActivity>, fixture: Fixture,
                                  values: Map<String, String>, name: String) {
        for (index in 0..2) {
            var label: TextView? = null
            var metadata = JSONObject()
            scenario.onActivity { activity ->
                val spinner = spinners(activity)[index]
                label = spinner.selectedView as TextView
                scrollVisible(activity, spinner)
                val field = fixture.block.getJSONArray("responses").getJSONObject(0).getJSONArray("fields").getJSONObject(index)
                val optionIndex = optionIds(field).indexOf(values.getValue(FIELD_IDS[index]))
                val option = field.getJSONArray("options").getJSONObject(optionIndex)
                metadata = assertCompleteNativeText(requireNotNull(label), option.getString("label"))
                    .put("fieldId", FIELD_IDS[index]).put("optionId", option.getString("id"))
                    .put("originalFingerprint", fixture.fingerprint).put("stateCorrect", state(activity, fixture).getBoolean("correct"))
            }
            capture("$name-${FIELD_IDS[index]}", { requireNotNull(label) }, metadata)
        }
    }

    private fun assertCompleteNativeText(view: TextView, expected: String): JSONObject {
        assertEquals(expected, view.text.toString())
        assertNull("Native selected and popup labels have no ellipsis policy", view.ellipsize)
        val layout = requireNotNull(view.layout)
        assertTrue("The authored label has a measured native layout", layout.lineCount > 0 && view.width > 0)
        assertEquals("The final authored character is part of the layout", expected.length, layout.getLineEnd(layout.lineCount - 1))
        assertTrue("All native lines fit vertically", layout.height <= view.height - view.compoundPaddingTop - view.compoundPaddingBottom)
        val lines = JSONArray()
        for (line in 0 until layout.lineCount) {
            assertEquals("No native line substitutes an ellipsis", 0, layout.getEllipsisCount(line))
            assertTrue("No native line extends beyond its available width", layout.getLineRight(line) <= layout.width + 1f)
            assertTrue(layout.getLineLeft(line) >= -1f)
            lines.put(JSONObject().put("start", layout.getLineStart(line)).put("end", layout.getLineEnd(line))
                .put("left", layout.getLineLeft(line)).put("right", layout.getLineRight(line)).put("ellipsisCount", layout.getEllipsisCount(line)))
        }
        val image = draw(view)
        try {
            val last = layout.lineCount - 1
            val top = (view.compoundPaddingTop + layout.getLineTop(last)).coerceIn(0, image.height - 1)
            val bottom = (view.compoundPaddingTop + layout.getLineBottom(last)).coerceIn(top + 1, image.height)
            val foreground = view.currentTextColor
            val textPixels = (top until bottom).sumOf { y -> (view.compoundPaddingLeft until image.width - view.compoundPaddingRight).count { x ->
                val pixel = image.getPixel(x, y)
                kotlin.math.abs(Color.red(pixel) - Color.red(foreground)) < 30 &&
                    kotlin.math.abs(Color.green(pixel) - Color.green(foreground)) < 30 &&
                    kotlin.math.abs(Color.blue(pixel) - Color.blue(foreground)) < 30
            } }
            assertTrue("The final native text line actually paints visible foreground glyphs", textPixels > 10)
            return JSONObject().put("text", expected).put("lineCount", layout.lineCount).put("lines", lines)
                .put("width", view.width).put("height", view.height).put("layoutHeight", layout.height).put("lastLineForegroundPixels", textPixels)
                .put("textSizePx", view.textSize)
        } finally { image.recycle() }
    }

    private fun spinners(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<Spinner>()
    private fun inlineViewport(spinner: Spinner): HorizontalScrollView {
        var ancestor = spinner.parent
        while (ancestor != null) {
            if (ancestor is HorizontalScrollView) return ancestor
            ancestor = ancestor.parent
        }
        error("The real inline dropdown must belong to a HorizontalScrollView")
    }
    private fun optionIds(field: JSONObject) = field.getJSONArray("options").let { options ->
        (0 until options.length()).map { options.getJSONObject(it).getString("id") }
    }

    companion object {
        private const val SOURCE_ASSET = "content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_del_trazo_a_la_figura_geometrica.json"
        private const val SOURCE_ID = "math_geometria_plana__fundamentos_geometricos__del_trazo_a_la_figura_geometrica"
        private const val ORIGINAL_FINGERPRINT = "0225073d50328f34e28ee6c3f25f3bf6645b1d83bd8fe6d2b2f7281fc86eaa7f"
        private val FIELD_IDS = listOf("mismo", "distintos", "pertenencia")
        private val CORRECT = linkedMapOf("mismo" to "un_punto", "distintos" to "dos_puntos", "pertenencia" to "pertenece")
        private val WRONG = linkedMapOf("mismo" to "dos_puntos", "distintos" to "sin_decidir", "pertenencia" to "cerca")
    }
}

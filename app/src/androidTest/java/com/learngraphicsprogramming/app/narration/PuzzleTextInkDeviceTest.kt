package com.learngraphicsprogramming.app.narration

import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.view.ContextThemeWrapper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonTechnicalText
import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/** Native label drawing only: no Activity, preference, curricular file or audio is changed. */
@RunWith(AndroidJUnit4::class)
class PuzzleTextInkDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = ContextThemeWrapper(instrumentation.targetContext, R.style.Theme_LearnGraphicsProgramming)
    private val surface = Color.rgb(16, 17, 20)
    private val green = Color.rgb(180, 220, 190)
    private val red = Color.rgb(240, 150, 150)
    private val source = "Correct.\nUse `std::cout` and `#include` in the program."
    private val language = NarrationLanguage.AMERICAN_ENGLISH

    @Test fun narratedGreenFeedbackRevealsNormalGreenAndOriginalSyntaxWithoutChangingItsLayout() = onMain {
        val view = feedback(source, green)
        val baseline = render(view)
        val text = view.text
        val layout = view.layout
        val geometry = geometry(view)
        val reading = LessonReadingTextColors().apply { register(0, view, feedback = true) }
        val mapping = mappings(reading, feedback = true)
        assertTrue(mapping.isNotEmpty())
        val target = mapping.values.flatMap { requireNotNull(it.targets) }.distinct().single()
        val ranges = mapping.values.flatMap { it.displayed }
        assertTrue("Real script provenance must map the complete successful feedback", ranges.isNotEmpty())
        assertInk(baseline, view, "Correct", green)
        assertInk(baseline, view, "std", Color.rgb(171, 141, 227))
        assertInk(baseline, view, "#include", Color.rgb(92, 156, 231))

        reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true, unreadWords = mapOf(target to ranges))
        val pending = render(view)
        assertPending(pending, view, "Correct")
        assertPending(pending, view, "std")
        assertPending(pending, view, "#include")
        assertEquals(0, countColor(pending, bounds(view, source.indexOf("Correct"), 7), green))

        val tail = source.indexOf("in the program")
        reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true,
            unreadWords = mapOf(target to ranges.filter { it.endExclusive > tail }))
        val partial = render(view)
        assertSamePixels(baseline, partial, bounds(view, source.indexOf("Correct"), 7))
        assertSamePixels(baseline, partial, bounds(view, source.indexOf("std::cout"), 9))
        assertSamePixels(baseline, partial, bounds(view, source.indexOf("#include"), 8))
        assertPending(partial, view, "program")

        reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true,
            unreadWords = mapOf(target to emptyList()), completedSegments = mapping.keys)
        assertTrue("Read feedback restores every original color, including its green heading", baseline.sameAs(render(view)))
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(geometry, geometry(view))
        reading.clear()
    }

    @Test fun replacingGreenFeedbackWithIncorrectRedKeepsSyntaxAndCannotRetainPendingInkOrOldGreen() = onMain {
        val view = feedback(source, green)
        val reading = LessonReadingTextColors().apply { register(0, view, feedback = true) }
        val mapping = mappings(reading, feedback = true)
        assertTrue(mapping.isNotEmpty())
        val oldTarget = mapping.values.flatMap { requireNotNull(it.targets) }.distinct().single()
        val oldRanges = mapping.values.flatMap { it.displayed }
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = mapOf(oldTarget to oldRanges))
        assertPending(render(view), view, "Correct")

        reading.unregister(view)
        view.text = "Review `std::cout` and `#include` before trying again."
        view.setTextColor(red)
        LessonTechnicalText.apply(view, mathematics = false, language = language)
        measure(view)
        val expected = render(view)
        val content = view.text
        val geometry = geometry(view)
        assertInk(expected, view, "Review", red)
        assertInk(expected, view, "std", Color.rgb(171, 141, 227))
        assertInk(expected, view, "#include", Color.rgb(92, 156, 231))

        for (state in listOf(ReadingBlockState.UNREAD, ReadingBlockState.CURRENT, ReadingBlockState.READ)) {
            reading.show(mapOf(0 to state), enabled = true, unreadWords = mapOf(oldTarget to oldRanges))
            assertTrue("Incorrect feedback is visual only; old successful-feedback progress cannot shade it", expected.sameAs(render(view)))
            assertEquals(red, view.currentTextColor)
        }
        reading.restore()
        assertTrue(expected.sameAs(render(view)))
        assertSame(content, view.text)
        assertEquals(geometry, geometry(view))
        reading.clear()
    }

    @Test fun nativeRadioIndicatorAndClicksStayIndependentFromPendingAndRevealedOptionGlyphs() = onMain {
        val accent = Color.rgb(67, 154, 238)
        val label = "Use `std::cout` to display the result."
        val parent = LinearLayout(context).apply { setBackgroundColor(surface) }
        val view = ReadingRadioButton(context).apply {
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            text = label
            textSize = 25f
            setTextColor(Color.WHITE)
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setPadding(13, 15, 13, 19)
            buttonTintList = ColorStateList.valueOf(accent)
        }
        parent.addView(view)
        LessonTechnicalText.apply(view, mathematics = false, language = language)
        measure(view)
        assertTrue(view.safeSupportsWordReading)
        val text = view.text
        val layout = view.layout
        val geometry = geometry(view)
        val baseline = render(view)
        val indicator = intArrayOf(0, 0, view.compoundPaddingLeft, view.height)
        assertTrue("The indicator comparison must contain native radio ink", countColor(baseline, indicator, accent) > 3)
        val reading = LessonReadingTextColors().apply { register(0, view) }
        val mapping = mappings(reading, feedback = false, option = label)
        assertTrue("The option matches its own real source segments", mapping.isNotEmpty())
        val ranges = mapping.values.flatMap { it.displayed }
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = mapOf(0 to ranges))
        val pending = render(view)
        assertPending(pending, view, "Use")
        assertPending(pending, view, "std")
        assertSamePixels(baseline, pending, indicator)
        assertEquals(geometry, geometry(view))

        reading.show(mapOf(0 to ReadingBlockState.READ), enabled = true, unreadWords = mapOf(0 to emptyList()))
        assertTrue("Revealed option glyphs and the native indicator recover exactly", baseline.sameAs(render(view)))
        var clicks = 0
        view.setOnClickListener { clicks++ }
        assertFalse(view.isChecked)
        assertTrue(view.performClick())
        assertTrue(view.isChecked)
        assertEquals(1, clicks)
        val checked = render(view)
        reading.show(mapOf(0 to ReadingBlockState.CURRENT), enabled = true, unreadWords = mapOf(0 to ranges))
        assertSamePixels(checked, render(view), indicator)
        assertTrue(view.performClick())
        assertTrue("A selected native radio remains selected when clicked again", view.isChecked)
        assertEquals(2, clicks)
        assertSame(text, view.text)
        assertSame(layout, view.layout)
        assertEquals(geometry, geometry(view))
        reading.clear()
    }

    private fun feedback(value: String, color: Int) = ReadingTextView(context).apply {
        layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        text = value
        textSize = 25f
        setTextColor(color)
        setBackgroundColor(surface)
        setPadding(19, 17, 13, 23)
        LessonTechnicalText.apply(this, mathematics = false, language = language)
        measure(this)
        assertTrue(safeSupportsWordReading)
    }

    private fun mappings(reading: LessonReadingTextColors, feedback: Boolean,
                         option: String = "A visible alternative."): Map<Int, NarrationWordMapping> {
        val block = JSONObject().put("type", "multiple_choice").put("question", "Choose a statement.")
            .put("options", JSONArray().put(option)).put("correctIndex", 0)
            .put("explanation", source.substringAfter('\n'))
        val lesson = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
        val script = LessonNarrationScript.page(lesson, 0, language, forKokoro = true)
        val plan = requireNotNull(script.readingPlan(NarrationEngine.KOKORO))
        val parts = NarrationSegments.sentences(script.texts)
        val mapping = reading.wordMappings(parts, plan.origins, language)
        assertTrue(mapping.keys.all { plan.origins[it].feedback == feedback })
        return mapping
    }

    private fun measure(view: TextView) {
        view.measure(View.MeasureSpec.makeMeasureSpec(1400, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        view.layout(0, 0, view.measuredWidth, view.measuredHeight)
    }

    private fun render(view: TextView) = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888).also {
        it.eraseColor(surface)
        view.draw(Canvas(it))
    }

    private fun geometry(view: TextView): List<Any> = listOf(view.width, view.height, view.lineCount,
        view.paddingLeft, view.paddingTop, view.paddingRight, view.paddingBottom, view.compoundPaddingLeft,
        view.textSize, (0 until view.lineCount).map { view.layout.getLineBaseline(it) })

    private fun bounds(view: TextView, start: Int, length: Int): IntArray {
        val line = view.layout.getLineForOffset(start)
        assertEquals(line, view.layout.getLineForOffset(start + length - 1))
        val first = view.layout.getPrimaryHorizontal(start)
        val last = view.layout.getPrimaryHorizontal(start + length)
        return intArrayOf(floor(min(first, last) + view.compoundPaddingLeft).toInt(),
            view.layout.getLineTop(line) + view.totalPaddingTop,
            ceil(max(first, last) + view.compoundPaddingLeft).toInt(), view.layout.getLineBottom(line) + view.totalPaddingTop)
    }

    private fun countColor(bitmap: Bitmap, bounds: IntArray, color: Int): Int {
        var count = 0
        for (y in bounds[1] until bounds[3]) for (x in bounds[0] until bounds[2]) if (bitmap.getPixel(x, y) == color) count++
        return count
    }

    private fun assertInk(bitmap: Bitmap, view: TextView, word: String, color: Int) {
        assertTrue("Expected original ${Integer.toHexString(color)} ink for $word",
            countColor(bitmap, bounds(view, view.text.toString().indexOf(word), word.length), color) > 3)
    }

    private fun assertPending(bitmap: Bitmap, view: TextView, word: String) {
        val box = bounds(view, view.text.toString().indexOf(word), word.length)
        val colors = mutableMapOf<Int, Int>()
        for (y in box[1] until box[3]) for (x in box[0] until box[2]) {
            val color = bitmap.getPixel(x, y)
            if (Color.red(color) == Color.green(color) && Color.red(color) == Color.blue(color) &&
                Color.red(color) in 1..220 &&
                LessonReadingColor.contrast(color, surface) >= LessonReadingColor.MIN_CONTRAST)
                colors[color] = (colors[color] ?: 0) + 1
        }
        assertTrue("Pending $word needs visible neutral gray glyph ink", colors.values.any { it > 3 })
    }

    private fun assertSamePixels(first: Bitmap, second: Bitmap, bounds: IntArray) {
        for (y in bounds[1] until bounds[3]) for (x in bounds[0] until bounds[2])
            assertEquals("Pixel ($x,$y)", first.getPixel(x, y), second.getPixel(x, y))
    }

    private fun onMain(action: () -> Unit) = instrumentation.runOnMainSync(action)
}

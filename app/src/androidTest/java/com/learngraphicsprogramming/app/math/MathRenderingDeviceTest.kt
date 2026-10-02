package com.learngraphicsprogramming.app.math

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.text.Spanned
import android.text.style.ReplacementSpan
import android.view.View
import android.view.ViewGroup
import android.widget.RadioButton
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Checks actual Android span measurements/drawing; this does not claim visual phone QA. */
@RunWith(AndroidJUnit4::class)
class MathRenderingDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()

    @Test fun fractionHasStackedMetricsDrawnBarAndProportionalScaling() {
        val rendered = MathTextRenderer.render("\\frac{1}{2}", "block") as Spanned
        val span = rendered.getSpans(0, rendered.length, ReplacementSpan::class.java).single()
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 32f; color = Color.WHITE }
        val normal = paint.fontMetricsInt
        val metrics = paint.fontMetricsInt
        val width = span.getSize(paint, rendered, 0, rendered.length, metrics)
        assertTrue("A fraction reserves separate numerator/denominator space", metrics.descent - metrics.ascent > normal.descent - normal.ascent)
        paint.textSize = 48f
        val larger = paint.fontMetricsInt
        val largerWidth = span.getSize(paint, rendered, 0, rendered.length, larger)
        assertEquals(width * 1.5f, largerWidth.toFloat(), 2f)
        assertEquals((metrics.descent - metrics.ascent) * 1.5f, (larger.descent - larger.ascent).toFloat(), 3f)

        paint.textSize = 32f
        val image = Bitmap.createBitmap(120, 120, Bitmap.Config.ARGB_8888)
        span.draw(Canvas(image), rendered, 0, rendered.length, 8f, 0, 70, 120, paint)
        val barY = (70 - 32 * .29f).toInt()
        val widestInk = (barY - 1..barY + 2).maxOf { y ->
            (8 until 8 + width).count { x -> Color.alpha(image.getPixel(x, y)) > 30 }
        }
        assertTrue("The renderer draws a horizontal fraction bar, rather than a slash string", widestInk >= width * .7f)
        image.recycle()
    }

    @Test fun proseIsPreservedAndInlineExpressionsAreLocalSpans() {
        val prose = "Observa la figura antes de responder."
        val ordinary = MathTextRenderer.render(prose) as Spanned
        assertEquals(prose, ordinary.toString())
        assertEquals(0, ordinary.getSpans(0, ordinary.length, ReplacementSpan::class.java).size)
        val source = "Usa 1/2, compara −2 < 1 y sitúa (2, 3)."
        val mixed = MathTextRenderer.render(source) as Spanned
        assertEquals(source, mixed.toString())
        assertEquals(3, mixed.getSpans(0, mixed.length, ReplacementSpan::class.java).size)
        assertEquals(source, MathTextRenderer.render(source, "text"))

        val explicit = MathTextRenderer.render("La regla es \\(y = 3x + 2\\).") as Spanned
        assertEquals("La regla es y = 3x + 2.", explicit.toString())
        assertEquals(1, explicit.getSpans(0, explicit.length, ReplacementSpan::class.java).size)
    }

    @Test fun scriptsExpandLineMetricsAndRichChoicesExposeOneRepresentation() {
        val rendered = MathTextRenderer.render("x^{2} + a_{1}", "inline") as Spanned
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 24f }
        val normal = paint.fontMetricsInt
        val metrics = paint.fontMetricsInt
        rendered.getSpans(0, rendered.length, ReplacementSpan::class.java).single()
            .getSize(paint, rendered, 0, rendered.length, metrics)
        assertTrue(metrics.ascent < normal.ascent)
        assertTrue(metrics.descent > normal.descent)

        instrumentation.runOnMainSync {
            val config = JSONObject("""{"id":"choice","type":"math_multiple_choice_single","options":[{"id":"half","label":"2/4","content":[{"type":"math_formula","text":"2/4"}]}],"validator":{"type":"choice","expected":["half"]}}""")
            val response = MathInputControls.create(instrumentation.targetContext,
                MathResponseSpec("choice", "math_multiple_choice_single", config, config.getJSONObject("validator")), Color.GREEN, {})!!
            val textViews = descendants(response.view).filterIsInstance<TextView>()
            assertEquals(1, textViews.count { it.text.toString() == "2/4" })
            val indicator = textViews.filterIsInstance<RadioButton>().single()
            assertEquals("", indicator.text.toString())
            assertEquals("2/4", indicator.contentDescription.toString())
        }
    }

    private fun descendants(root: View): List<View> = listOf(root) + if (root is ViewGroup)
        (0 until root.childCount).flatMap { descendants(root.getChildAt(it)) } else emptyList()
}

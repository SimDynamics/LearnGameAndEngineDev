package com.learngraphicsprogramming.app.math

import android.graphics.Color
import android.graphics.Paint
import android.text.Spanned
import android.text.style.ReplacementSpan
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MathTextLayoutDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun paint() = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 32f; color = Color.WHITE }
    private fun tree(source: String) = (MathLatexParser.parse(source) as MathLatexParser.Result.Success).expression

    @Test fun fullContentEngineRetainsMeasuredWordSpacing() {
        for (mode in listOf("inline", "block")) {
            val source = "3/4 de una unidad"
            val rendered = MathTextRenderer.render(source, mode) as Spanned
            val span = rendered.getSpans(0, rendered.length, ReplacementSpan::class.java).single()
            val measured = span.getSize(paint(), rendered, 0, rendered.length, null)
            val spaced = MathExpressionLayout.create(MathTextRenderer.parseExpression(source, content = true), paint(), 32f)
            val compact = MathExpressionLayout.create(MathTextRenderer.parseExpression("3/4deunaunidad", content = true), paint(), 32f)
            assertEquals(spaced.width, measured.toFloat(), 1f)
            assertTrue("$mode: full content keeps all three word spaces", spaced.width - compact.width > paint().measureText("  "))
            val prose = MathTextRenderer.parseExpression(source, content = true).children.drop(1)
                .joinToString("") { (it as MathExpression.Symbol).value }
            assertEquals(" de una unidad", prose)
        }
    }

    @Test fun commonRepresentationRendererTypesetsInlineAndDisplayOutsideInputs() {
        instrumentation.runOnMainSync {
            val paragraph = "Texto inline con \\(x^2+1\\), y display: \\[\\frac{x+1}{2}\\]. Continúa."
            val view = MathRepresentationRenderer.render(instrumentation.targetContext,
                JSONObject().put("type", "text").put("text", paragraph), Color.CYAN) as TextView
            val rendered = view.text as Spanned
            assertEquals(2, rendered.getSpans(0, rendered.length, ReplacementSpan::class.java).size)
            assertFalse(rendered.toString().contains("\\["))
            assertTrue(rendered.toString().contains("\n\\frac{x+1}{2}\n"))
            val formula = MathRepresentationRenderer.render(instrumentation.targetContext,
                JSONObject().put("type", "math_formula").put("latex", "\\frac{x+1}{2}"), Color.CYAN) as ViewGroup
            val formulaText = formula.getChildAt(0) as TextView
            val math = formulaText.text as Spanned
            assertEquals(1, math.getSpans(0, math.length, ReplacementSpan::class.java).size)
        }
        val full = MathTextRenderer.render("Una unidad \\(x^2\\) y \\[\\frac{1}{2}\\] al final.", "block") as Spanned
        assertTrue("Prose still goes through the common engine in full-content mode",
            full.getSpans(0, full.length, ReplacementSpan::class.java).size >= 5)
        assertFalse(full.toString().contains("\\("))
        assertTrue(full.toString().contains("\n\\frac{1}{2}\n"))
    }

    @Test fun upperAndLowerScriptsShareOneHorizontalAnchorAndRemainSeparatelyTouchable() {
        for (source in listOf("x^{22}_{1}", "x^{2}_{111}", "{x+y}_{1}^{2}")) {
            val expression = tree(source)
            val layout = MathExpressionLayout.create(expression, paint(), 32f, true)
            val upperCursor = MathCursor(listOf(MathCursorStep(0, "upper")), 0)
            val lowerCursor = MathCursor(listOf(MathCursorStep(0, "lower")), 0)
            val upper = layout.caretBounds(upperCursor)!!
            val lower = layout.caretBounds(lowerCursor)!!
            assertEquals("$source uses a shared script anchor", upper.left, lower.left, .01f)
            assertTrue(upper.centerY() < lower.centerY())
            assertEquals(upperCursor, layout.hitTest(upper.left, upper.centerY()))
            assertEquals(lowerCursor, layout.hitTest(lower.left, lower.centerY()))
            val scripts = expression.children.single() as MathExpression.Scripts
            val base = MathExpressionLayout.create(scripts.base, paint(), 32f, true)
            assertEquals(base.width + 32f * .025f, upper.left, .01f)
            val serialized = MathLatexSerializer.serialize(expression)
            assertEquals("Layout geometry never changes the mathematical tree", expression, tree(serialized))
            assertFalse("Layout spacing never becomes answer content", serialized.contains(' '))
        }
    }
}

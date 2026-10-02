package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ReplacementSpan
import android.widget.TextView
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min

/** Local math typesetting. Span measurements use the TextView's current paint, including the
 * lesson text scale: fractions and scripts grow with the surrounding words and share a baseline.
 * This is deliberately a documented TeX subset, rather than a remote WebView or a full TeX engine.
 */
class MathTextView(context: Context) : TextView(context) {
    fun setMathContent(value: String, mode: String = "auto") = MathTextRenderer.apply(this, value, mode)
}

object MathTextRenderer {
    private val numericFraction = Regex("[−-]?\\d+(?:[.,]\\d+)?/\\d+(?:[.,]\\d+)?")
    private val coordinates = Regex("\\([−-]?\\d+(?:[.,]\\d+)?,\\s*[−-]?\\d+(?:[.,]\\d+)?\\)")
    private val relation = Regex("[−-]?(?:\\d+(?:[.,]\\d+)?|[xyab])\\s*[≤≥<>≠=]\\s*[−-]?(?:\\d+(?:[.,]\\d+)?|[xyab])")
    private val equation = Regex("[xyab]\\s*[=≤≥<>≠]\\s*[−-]?(?:\\d+(?:[.,]\\d+)?[xyabπ]?|[xyabπ])(?:\\s*[+−-]\\s*(?:\\d+(?:[.,]\\d+)?[xyabπ]?|[xyabπ]))*")
    private val arithmetic = Regex("[−-]?\\d+(?:[.,]\\d+)?(?:\\s*[×÷+−]\\s*[−-]?\\d+(?:[.,]\\d+)?)+")
    private val power = Regex("[a-zA-Z0-9π]+(?:\\^[{]?[−-]?\\d+[}]?|[²³⁴⁵⁶⁷⁸⁹⁰¹]+)")
    private val wholeMath = Regex("[\\d\\s.,;:xyabnmπθ=<>≤≥≠+−*/×÷()\\[\\]{}|∪∩?²³⁴⁵⁶⁷⁸⁹⁰¹^_%-]+")

    /** mode: text preserves prose; inline/block typeset the complete expression; auto recognizes
     * only explicit delimiters, unambiguous mathematical fragments and short whole expressions.
     * Use \\( ... \\), \\[ ... \\] or $ ... $ to author mathematics inside ordinary prose.
     */
    fun apply(view: TextView, value: String, mode: String = "auto") {
        view.text = render(value, mode)
        view.contentDescription = value.replace("\\(", "").replace("\\)", "")
            .replace("\\[", "").replace("\\]", "")
    }

    fun render(value: String, mode: String = "auto"): CharSequence {
        if (mode == "text" || value.isBlank()) return value
        val output = SpannableStringBuilder()
        fun appendMath(source: String) {
            if (source.isBlank()) { output.append(source); return }
            val start = output.length
            output.append(source)
            output.setSpan(MathFormulaSpan(MathFormulaParser(source).parse()), start, output.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        if (mode == "inline" || mode == "block" || (value.length <= 100 && wholeMath.matches(value.trim()))) {
            appendMath(value.removeSurrounding("\\(", "\\)").removeSurrounding("\\[", "\\]"))
            return output
        }
        var index = 0
        while (index < value.length) {
            val delimiters = when {
                value.startsWith("\\(", index) -> "\\(" to "\\)"
                value.startsWith("\\[", index) -> "\\[" to "\\]"
                value[index] == '$' -> "$" to "$"
                else -> null
            }
            if (delimiters != null) {
                val end = value.indexOf(delimiters.second, index + delimiters.first.length)
                if (end > index + delimiters.first.length) {
                    appendMath(value.substring(index + delimiters.first.length, end))
                    index = end + delimiters.second.length
                    continue
                }
            }
            val candidate = listOf(numericFraction, coordinates, equation, relation, arithmetic, power).mapNotNull {
                it.matchAt(value, index)
            }.maxByOrNull { it.value.length }
            if (candidate != null && (index == 0 || !value[index - 1].isLetterOrDigit())) {
                appendMath(candidate.value)
                index = candidate.range.last + 1
            } else if (value[index] in "π≤≥≠×÷") {
                appendMath(value[index].toString())
                index++
            } else {
                output.append(value[index])
                index++
            }
        }
        return output
    }
}

private data class MathMetrics(val width: Float, val ascent: Float, val descent: Float)

private sealed interface MathBox {
    fun measure(paint: Paint, size: Float): MathMetrics
    fun draw(canvas: Canvas, paint: Paint, size: Float, x: Float, baseline: Float)
}

private class MathLiteral(private val value: String) : MathBox {
    private fun configure(paint: Paint, size: Float) { paint.textSize = size; paint.typeface = Typeface.create("serif", paint.typeface?.style ?: Typeface.NORMAL) }
    override fun measure(paint: Paint, size: Float): MathMetrics {
        configure(paint, size)
        val metrics = paint.fontMetrics
        return MathMetrics(paint.measureText(value), metrics.ascent, metrics.descent)
    }
    override fun draw(canvas: Canvas, paint: Paint, size: Float, x: Float, baseline: Float) {
        configure(paint, size)
        canvas.drawText(value, x, baseline, paint)
    }
}

private class MathRow(private val children: List<MathBox>) : MathBox {
    override fun measure(paint: Paint, size: Float): MathMetrics {
        val metrics = children.map { it.measure(paint, size) }
        return MathMetrics(metrics.sumOf { it.width.toDouble() }.toFloat(), metrics.minOfOrNull { it.ascent } ?: 0f, metrics.maxOfOrNull { it.descent } ?: 0f)
    }
    override fun draw(canvas: Canvas, paint: Paint, size: Float, x: Float, baseline: Float) {
        var currentX = x
        children.forEach { child ->
            val width = child.measure(paint, size).width
            child.draw(canvas, paint, size, currentX, baseline)
            currentX += width
        }
    }
}

private class MathFraction(private val numerator: MathBox, private val denominator: MathBox) : MathBox {
    private fun positions(paint: Paint, size: Float): Triple<MathMetrics, Float, Float> {
        val top = numerator.measure(paint, size * .82f)
        val bottom = denominator.measure(paint, size * .82f)
        val axis = -size * .29f
        val topBaseline = axis - size * .11f - top.descent
        val bottomBaseline = axis + size * .11f - bottom.ascent
        return Triple(MathMetrics(max(top.width, bottom.width) + size * .25f, topBaseline + top.ascent, bottomBaseline + bottom.descent), topBaseline, bottomBaseline)
    }
    override fun measure(paint: Paint, size: Float): MathMetrics = positions(paint, size).first
    override fun draw(canvas: Canvas, paint: Paint, size: Float, x: Float, baseline: Float) {
        val (metrics, top, bottom) = positions(paint, size)
        val numeratorWidth = numerator.measure(paint, size * .82f).width
        val denominatorWidth = denominator.measure(paint, size * .82f).width
        numerator.draw(canvas, paint, size * .82f, x + (metrics.width - numeratorWidth) / 2, baseline + top)
        denominator.draw(canvas, paint, size * .82f, x + (metrics.width - denominatorWidth) / 2, baseline + bottom)
        paint.strokeWidth = max(1f, size * .045f)
        canvas.drawLine(x + size * .025f, baseline - size * .29f, x + metrics.width - size * .025f, baseline - size * .29f, paint)
    }
}

private class MathScript(private val base: MathBox, private val script: MathBox, private val superscript: Boolean) : MathBox {
    override fun measure(paint: Paint, size: Float): MathMetrics {
        val first = base.measure(paint, size)
        val second = script.measure(paint, size * .68f)
        val offset = if (superscript) -size * .48f else size * .28f
        return MathMetrics(first.width + second.width, min(first.ascent, second.ascent + offset), max(first.descent, second.descent + offset))
    }
    override fun draw(canvas: Canvas, paint: Paint, size: Float, x: Float, baseline: Float) {
        val width = base.measure(paint, size).width
        base.draw(canvas, paint, size, x, baseline)
        script.draw(canvas, paint, size * .68f, x + width, baseline + if (superscript) -size * .48f else size * .28f)
    }
}

private class MathRoot(private val body: MathBox) : MathBox {
    override fun measure(paint: Paint, size: Float): MathMetrics {
        val metrics = body.measure(paint, size)
        return MathMetrics(metrics.width + size * .8f, min(metrics.ascent - size * .09f, -size), metrics.descent)
    }
    override fun draw(canvas: Canvas, paint: Paint, size: Float, x: Float, baseline: Float) {
        val metrics = body.measure(paint, size)
        MathLiteral("√").draw(canvas, paint, size, x, baseline)
        body.draw(canvas, paint, size, x + size * .7f, baseline)
        paint.strokeWidth = max(1f, size * .035f)
        canvas.drawLine(x + size * .65f, baseline + metrics.ascent - size * .04f, x + size * .8f + metrics.width, baseline + metrics.ascent - size * .04f, paint)
    }
}

private class MathFormulaSpan(private val box: MathBox) : ReplacementSpan() {
    override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
        val size = paint.textSize
        val metrics = box.measure(Paint(paint), size)
        fm?.let {
            it.ascent = min(it.ascent, kotlin.math.floor(metrics.ascent).toInt())
            it.top = min(it.top, it.ascent)
            it.descent = max(it.descent, ceil(metrics.descent).toInt())
            it.bottom = max(it.bottom, it.descent)
        }
        return ceil(metrics.width).toInt()
    }
    override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float, top: Int, y: Int, bottom: Int, paint: Paint) {
        box.draw(canvas, Paint(paint), paint.textSize, x, y.toFloat())
    }
}

/** A bounded parser for display only; it never changes the answer/validator syntax. */
private class MathFormulaParser(private val source: String, private val depth: Int = 0) {
    private var index = 0
    fun parse(): MathBox {
        if (depth > 12 || source.length > 2000) return MathLiteral(source)
        val boxes = mutableListOf<MathBox>()
        while (index < source.length) {
            val char = source[index]
            if (char == '^' || char == '_') {
                index++
                val script = argument()
                val base = if (boxes.isEmpty()) MathLiteral("") else boxes.removeAt(boxes.lastIndex)
                boxes += MathScript(base, script, char == '^')
                continue
            }
            if (char in "⁰¹²³⁴⁵⁶⁷⁸⁹") {
                val digits = "⁰¹²³⁴⁵⁶⁷⁸⁹"
                val value = buildString { while (index < source.length && source[index] in digits) append(digits.indexOf(source[index++])) }
                val base = if (boxes.isEmpty()) MathLiteral("") else boxes.removeAt(boxes.lastIndex)
                boxes += MathScript(base, MathLiteral(value), true)
                continue
            }
            val fraction = NUMERIC_FRACTION.matchAt(source, index)
            if (fraction != null) {
                val (top, bottom) = fraction.value.split('/', limit = 2)
                boxes += MathFraction(MathLiteral(top.replace('-', '−')), MathLiteral(bottom))
                index = fraction.range.last + 1
                continue
            }
            if (char == '\\') { boxes += command(); continue }
            if (char == '{') { boxes += argument(); continue }
            boxes += MathLiteral(if (char == '-') "−" else char.toString())
            index++
        }
        return MathRow(boxes)
    }

    private fun argument(): MathBox {
        while (index < source.length && source[index].isWhitespace()) index++
        if (index >= source.length) return MathLiteral("")
        if (source[index] != '{') return if (source[index] == '\\') command() else MathLiteral(source[index++].toString())
        val start = ++index
        var balance = 1
        while (index < source.length && balance > 0) {
            if (source[index] == '{') balance++
            if (source[index] == '}') balance--
            index++
        }
        val end = if (balance == 0) index - 1 else index
        return MathFormulaParser(source.substring(start, end), depth + 1).parse()
    }

    private fun command(): MathBox {
        val start = index++
        while (index < source.length && source[index].isLetter()) index++
        val command = source.substring(start + 1, index)
        return when (command) {
            "frac", "dfrac", "tfrac" -> MathFraction(argument(), argument())
            "sqrt" -> MathRoot(argument())
            "text", "mathrm", "operatorname" -> argument()
            "left", "right" -> MathLiteral("")
            "pi" -> MathLiteral("π")
            "theta" -> MathLiteral("θ")
            "times" -> MathLiteral(" × ")
            "cdot" -> MathLiteral(" · ")
            "div" -> MathLiteral(" ÷ ")
            "le", "leq" -> MathLiteral(" ≤ ")
            "ge", "geq" -> MathLiteral(" ≥ ")
            "ne", "neq" -> MathLiteral(" ≠ ")
            "infty" -> MathLiteral("∞")
            "cup" -> MathLiteral(" ∪ ")
            "cap" -> MathLiteral(" ∩ ")
            "in" -> MathLiteral(" ∈ ")
            else -> MathLiteral(source.substring(start, index))
        }
    }

    companion object {
        private val NUMERIC_FRACTION = Regex("[−-]?\\d+(?:[.,]\\d+)?/\\d+(?:[.,]\\d+)?")
    }
}

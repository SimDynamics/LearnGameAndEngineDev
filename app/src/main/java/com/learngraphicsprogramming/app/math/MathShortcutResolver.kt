package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.math.MathContext

/** The shortest intended notation, not the union of infinitely many equivalent expressions.
 * Android supplies letters/digits. This resolver has no View or page-ID dependency.
 */
object MathShortcutResolver {
    private val allowed = setOf(".", ",", "+", "−", "-", "*", "×", "·", "÷", "π", "%", "a/b", "/",
        "(", ")", "()", "=", "<", ">", "≤", "≥", "≠", "^", "√", "∞")
    private val numeric = setOf("numeric", "tolerance", "exact_fraction")
    private val symbolic = setOf("equivalent_expression", "symbolic")
    private val literal = setOf("text", "exact_text")

    fun resolve(validator: JSONObject?, mode: String, keyboardSymbols: JSONArray? = null): List<String> {
        if (keyboardSymbols != null) return (0 until keyboardSymbols.length())
            .mapNotNull { (keyboardSymbols.opt(it) as? String)?.takeIf(allowed::contains) }.distinct()
        val type = validator?.optString("type").orEmpty()
        if (type in numeric) {
            var expected = validator!!.opt("expected")?.toString().orEmpty()
            // A NUMERIC tolerance exercise asks for a decimal approximation, e.g. 1/3 → .333….
            if (type == "tolerance" && mode.uppercase() == "NUMERIC" && '/' in expected) {
                val pieces = expected.split('/')
                expected = runCatching {
                    BigDecimal(pieces[0].trim()).divide(BigDecimal(pieces[1].trim()), MathContext.DECIMAL64)
                        .stripTrailingZeros().toPlainString()
                }.getOrDefault(expected)
            }
            return notationSymbols(expected, false).filter { it in setOf("−", ".", ",", "/", "%", "+") }
        }
        if (type in symbolic || type in literal) return notationSymbols(validator!!.optString("expected"), type in symbolic)
        // Only legacy callers/custom validators without a known expected notation use mode hints.
        return when (mode.uppercase()) {
            "FRACTION" -> listOf("/")
            "ALGEBRA", "CALCULUS" -> listOf("+", "−", "×", "/", "^", "=", "()")
            "INEQUALITY" -> listOf("<", ">", "≤", "≥", "=", "≠")
            "TRIG" -> listOf("π", "+", "−", "×", "/", "^", "()")
            else -> listOf("−", ".")
        }
    }

    private fun notationSymbols(value: String, normalize: Boolean): List<String> {
        // The polynomial parser recognizes adjacent pi atoms (4pix, pipi) as multiplication.
        val text = if (normalize) value.replace("pi", "π", ignoreCase = true) else value
        val result = linkedSetOf<String>()
        text.forEach { char ->
            val symbol = when (char) {
                '-', '−' -> "−"
                '*' -> if (normalize) "×" else "*"
                '·' -> if (normalize) "×" else "·"
                '(', ')' -> "()"
                else -> char.toString()
            }
            if (symbol in allowed) result.add(symbol)
        }
        return result.toList()
    }

    /** Visual editors parse Double, clamp to bounds and snap. They cannot accept fractions/π.
     * Bounds and all interchangeable points matter; expected[index] is not a field constraint.
     */
    fun visualSymbols(config: JSONObject, validator: JSONObject, axis: String? = null): List<String> {
        val bounds = config.optJSONObject("bounds") ?: JSONObject()
        val min = if (axis == null) config.optDouble("min", -10.0) else bounds.optDouble("${axis}Min", -5.0)
        val max = if (axis == null) config.optDouble("max", 10.0) else bounds.optDouble("${axis}Max", 5.0)
        val step = if (axis == null) config.optDouble("step", 1.0).let { if (it > 0) it else 1.0 }
            else config.optDouble("snap", 1.0).coerceAtLeast(0.0)
        val values = mutableListOf(min, max)
        if (axis == null) {
            values += config.optDouble("value", 0.0)
            values += validator.optDouble("value", validator.optDouble("expected", 0.0))
        } else {
            for (array in listOf(config.optJSONArray("points"), validator.optJSONArray("expected"))) {
                if (array != null) for (index in 0 until array.length()) {
                    array.optJSONObject(index)?.optDouble(axis)?.takeIf(Double::isFinite)?.let(values::add)
                }
            }
        }
        return buildList {
            if (values.any { it < 0 }) add("−")
            if (step == 0.0 || step % 1.0 != 0.0 || values.any { it.isFinite() && it % 1.0 != 0.0 }) add(".")
        }
    }

    private fun equivalence(symbol: String): Set<String> = when (symbol) {
        "-", "−" -> setOf("−")
        "a/b", "/", "÷" -> setOf("/")
        "*", "×", "·" -> setOf("×")
        ".", "," -> setOf(".")
        "()" -> setOf("(", ")")
        else -> setOf(symbol)
    }

    /** Explicit overrides are checked at content parse time, before an unusable field renders. */
    fun validateConfiguration(validator: JSONObject?, mode: String, keyboardSymbols: JSONArray?) {
        if (keyboardSymbols == null) return
        val declared = (0 until keyboardSymbols.length()).map { index ->
            val symbol = keyboardSymbols.opt(index)
            require(symbol is String && symbol in allowed) { "keyboardSymbols[$index] debe ser un símbolo matemático; sin letras ni dígitos." }
            symbol
        }
        require(declared.distinct().size == declared.size) { "keyboardSymbols contiene símbolos duplicados." }
        val type = validator?.optString("type").orEmpty()
        if (type !in numeric + symbolic + literal) return // Registered extension: author declares its syntax.
        if (type in numeric) require(declared.all { it in setOf("−", "-", ".", ",", "/", "a/b", "%", "+") }) {
            "keyboardSymbols incluye operaciones que el validador numérico no acepta."
        }
        val required = resolve(validator, mode).flatMap(::equivalence).toSet()
        val available = declared.flatMap(::equivalence).toSet()
        require(available.containsAll(required)) { "keyboardSymbols omite símbolos requeridos: ${required - available}" }
        require(required.containsAll(available)) { "keyboardSymbols incluye símbolos innecesarios: ${available - required}" }
        if (type in literal) {
            // Literal validators do not normalize ×, ÷, decimal separators or Unicode relations.
            require(declared.all { symbol -> symbol == "()" || symbol == "−" || symbol in validator!!.optString("expected") }) {
                "keyboardSymbols usa un alias incompatible con el validador literal."
            }
        }
    }

    fun validateResponseConfiguration(config: JSONObject, validator: JSONObject, responseType: String) {
        fun override(owner: JSONObject): JSONArray? {
            require(!owner.has("keyboardSymbols") || owner.optJSONArray("keyboardSymbols") != null) {
                "keyboardSymbols debe ser un arreglo de símbolos."
            }
            return owner.optJSONArray("keyboardSymbols")
        }
        val parent = override(config)
        val fields = config.optJSONArray("fields")
        if (fields != null) {
            for (index in 0 until fields.length()) {
                val field = fields.getJSONObject(index)
                val fieldOverride = override(field)
                if (field.optString("type", "numeric") !in setOf("numeric", "expression")) continue
                val mode = field.optString("keyboard", if (field.optString("type") == "expression") "ALGEBRA" else "NUMERIC")
                validateConfiguration(validator.optJSONObject("fields")?.optJSONObject(field.getString("id")), mode, fieldOverride ?: parent)
            }
        } else if (responseType in setOf("math_numeric_input", "math_expression_input")) {
            validateConfiguration(validator, config.optString("keyboard", if (responseType == "math_expression_input") "ALGEBRA" else "NUMERIC"), parent)
        } else if (parent != null) {
            require(false) { "keyboardSymbols solo corresponde a respuestas de texto o sus campos." }
        }
    }
}

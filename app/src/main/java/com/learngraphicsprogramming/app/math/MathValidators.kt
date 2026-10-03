package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.math.BigInteger
import java.math.MathContext
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max

data class MathValidationResult(val correct: Boolean, val message: String)

interface MathAnswerValidator {
    fun validate(spec: JSONObject, answer: MathAnswer): MathValidationResult
}

/**
 * Validators operate on typed answers. Register another validator without changing a renderer.
 * Numeric answers are rationally exact unless a tolerance is declared. Tolerance is absolute.
 * Expressions use exact bounded polynomials, not a CAS:
 * +, -, *, / by rational constants, integer powers 0..8, implicit multiplication and pi/π.
 * Expressions have at most one variable; equations have at most two and are compared after
 * moving both sides to the left, up to a nonzero rational factor. Functions, variable
 * denominators and general symbolic equivalence deliberately return an unsupported message.
 */
object MathValidators {
    private val registry = mutableMapOf<String, MathAnswerValidator>()

    init {
        register("numeric", ::numeric)
        register("tolerance", ::numeric)
        register("exact_fraction", ::fraction)
        register("equivalent_expression", ::expression)
        register("symbolic", ::expression)
        register("text", ::literal)
        register("exact_text", ::literal)
        register("latex", ::latex)
        register("choice", ::choice)
        register("fields", ::fields)
        register("order", ::order)
        register("matching", ::matching)
        register("number_line", ::numberLine)
        register("sliders", ::sliders)
        register("coordinates", ::coordinates)
        register("geometry", ::geometry)
    }

    fun register(type: String, validator: MathAnswerValidator) {
        require(type.isNotBlank()) { "El tipo de validador no puede estar vacío." }
        registry[type] = validator
    }

    private fun register(type: String, callback: (JSONObject, MathAnswer) -> MathValidationResult) {
        register(type, object : MathAnswerValidator {
            override fun validate(spec: JSONObject, answer: MathAnswer) = callback(spec, answer)
        })
    }

    fun validate(spec: JSONObject, answer: MathAnswer): MathValidationResult = try {
        val type = spec.optString("type")
        val validator = registry[type]
        if (validator == null) fail("Validador no soportado: $type") else validator.validate(spec, answer)
    } catch (exception: IllegalArgumentException) {
        fail(exception.message ?: "No se pudo validar esta respuesta.")
    } catch (_: Exception) {
        fail("La configuración de validación no es válida.")
    }

    /** Fail malformed lesson content early, before an answer is requested. */
    fun validateSpec(spec: JSONObject) {
        val type = spec.opt("type")
        require(type is String && type.isNotBlank()) { "validator requiere type como texto." }
        require(type in registry) { "Tipo de validador desconocido: $type" }
        tolerance(spec)
        when (type) {
            "numeric", "tolerance", "exact_fraction" -> number(spec, "expected")
            "equivalent_expression", "symbolic" -> {
                require(spec.opt("expected") is String) { "$type requiere expected como texto." }
                PolynomialExpression.parse(MathExpressionSemantics.polynomialSource(spec.getString("expected")))
            }
            "text", "exact_text" -> require(spec.opt("expected") is String) { "$type requiere expected como texto." }
            "latex" -> {
                require(spec.opt("expected") is String) { "latex requiere expected como texto." }
                MathExpressionSemantics.canonical(spec.getString("expected"))
            }
            "choice", "order" -> stringList(requiredArray(spec, "expected"))
            "matching" -> stringMap(requiredObject(spec, "expected"))
            "fields", "sliders" -> {
                val values = requiredObject(spec, if (type == "fields") "fields" else "values")
                require(values.length() > 0) { "$type requiere al menos un valor esperado." }
                values.keys().forEach { key ->
                    require(key.isNotBlank()) { "Los IDs de $type no pueden estar vacíos." }
                    val validator = values.optJSONObject(key)
                        ?: throw IllegalArgumentException("$type.$key requiere un objeto validador.")
                    validateSpec(validator)
                    val childType = validator.getString("type")
                    val nonTextBuiltins = setOf("choice", "fields", "order", "matching", "number_line", "sliders", "coordinates", "geometry")
                    require(childType !in nonTextBuiltins) { "$type.$key requiere un validador de entrada textual/numeral." }
                }
            }
            "number_line" -> {
                number(spec, if (spec.has("value")) "value" else "expected")
                require(!spec.has("closed") || spec.opt("closed") is Boolean) {
                    "number_line.closed debe ser booleano."
                }
                require(!spec.has("direction") || spec.optString("direction") in setOf("left", "right", "none")) {
                    "number_line.direction debe ser left, right o none."
                }
            }
            "coordinates" -> {
                val mode = spec.optString("mode", "point")
                require(mode in setOf("point", "line", "polygon", "scatter", "parabola")) {
                    "Modo de coordenadas no soportado: $mode"
                }
                val points = points(requiredArray(spec, "expected"))
                require(points.isNotEmpty()) { "coordinates.expected no puede estar vacío." }
                require(when (mode) {
                    "point" -> points.size == 1
                    "line", "parabola" -> points.size == 2
                    "polygon" -> points.size >= 3
                    else -> true
                }) { "Cantidad de puntos incompatible con el modo $mode." }
                if (mode == "line") require(distance(points[0], points[1]) > 1e-12) {
                    "Una recta esperada requiere dos puntos distintos."
                }
                if (mode == "parabola") parabolaCoefficient(points, 1e-12)
                if (mode == "polygon") require(MathGeometry.isSimplePolygon(points)) {
                    "El polígono esperado debe ser simple y no degenerado."
                }
            }
            "geometry" -> {
                require(number(spec, if (spec.has("expected")) "expected" else "area") > 0.0) {
                    "El área esperada debe ser positiva."
                }
                require(!spec.has("shape") || spec.optString("shape") in setOf("polygon", "rectangle")) {
                    "Forma geométrica no soportada."
                }
            }
        }
    }

    private fun numeric(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val text = (answer as? MathAnswer.Text)?.value ?: return wrongKind()
        val value = Rational.parse(text) ?: return fail("Escribe un número o una fracción válidos.")
        if (!value.toDouble().isFinite()) return fail("Escribe un número finito.")
        val expected = Rational.parse(spec.get("expected").toString())
            ?: return fail("El número esperado no es válido.")
        val tolerance = tolerance(spec, if (spec.optString("type") == "tolerance") 1e-6 else 0.0)
        return result(rationalNear(value, expected, tolerance))
    }

    private fun fraction(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val text = (answer as? MathAnswer.Text)?.value ?: return wrongKind()
        val value = Rational.parse(text) ?: return fail("Escribe una fracción válida; el denominador debe ser distinto de cero.")
        val expected = Rational.parse(spec.get("expected").toString())
            ?: return fail("La fracción esperada no es válida.")
        val tolerance = tolerance(spec, 0.0)
        return result(rationalNear(value, expected, tolerance))
    }

    private fun expression(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val text = (answer as? MathAnswer.Text)?.value ?: return wrongKind()
        if (text.isBlank()) return fail("Escribe una expresión.")
        val actual = PolynomialExpression.parse(MathExpressionSemantics.polynomialSource(text))
        val expected = PolynomialExpression.parse(MathExpressionSemantics.polynomialSource(spec.getString("expected")))
        return result(actual == expected)
    }

    private fun literal(spec: JSONObject, answer: MathAnswer): MathValidationResult =
        result((answer as? MathAnswer.Text)?.value == spec.getString("expected"))

    private fun latex(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val text = (answer as? MathAnswer.Text)?.value ?: return wrongKind()
        return result(MathExpressionSemantics.canonical(text) == MathExpressionSemantics.canonical(spec.getString("expected")))
    }

    private fun choice(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val selected = (answer as? MathAnswer.Choice)?.selected ?: return wrongKind()
        return result(selected == stringList(spec.getJSONArray("expected")).toSet())
    }

    private fun fields(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val values = (answer as? MathAnswer.Fields)?.values ?: return wrongKind()
        val expected = spec.getJSONObject("fields")
        val keys = expected.keys().asSequence().toSet()
        if (values.keys != keys) return fail("Completa todos los campos de respuesta.")
        for (key in keys) {
            val checked = validate(expected.getJSONObject(key), MathAnswer.Text(values.getValue(key)))
            if (!checked.correct) return fail("$key: ${checked.message}")
        }
        return result(true)
    }

    private fun order(spec: JSONObject, answer: MathAnswer): MathValidationResult =
        result((answer as? MathAnswer.Order)?.ids == stringList(spec.getJSONArray("expected")))

    private fun matching(spec: JSONObject, answer: MathAnswer): MathValidationResult =
        result((answer as? MathAnswer.Matching)?.pairs == stringMap(spec.getJSONObject("expected")))

    private fun numberLine(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val value = answer as? MathAnswer.NumberLine ?: return wrongKind()
        val expected = number(spec, if (spec.has("value")) "value" else "expected")
        return result(
            value.value.isFinite() && near(value.value, expected, tolerance(spec)) &&
                (!spec.has("closed") || value.closed == spec.getBoolean("closed")) &&
                (!spec.has("direction") || value.direction == spec.getString("direction"))
        )
    }

    private fun sliders(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val values = (answer as? MathAnswer.Sliders)?.values ?: return wrongKind()
        val expected = spec.getJSONObject("values")
        val keys = expected.keys().asSequence().toSet()
        if (values.keys != keys || values.values.any { !it.isFinite() }) return fail("Revisa todos los controles.")
        for (key in keys) {
            val itemValidator = JSONObject(expected.getJSONObject(key).toString())
            // Sliders compute Double values, so their internal roundoff is expected. Text
            // numeric inputs remain exact; a slider author can explicitly choose tolerance: 0.
            if (itemValidator.optString("type") == "numeric" && !itemValidator.has("tolerance")) itemValidator.put("tolerance", 1e-6)
            val checked = validate(itemValidator, MathAnswer.Text(values.getValue(key).toString()))
            if (!checked.correct) return fail("$key: ${checked.message}")
        }
        return result(true)
    }

    private fun coordinates(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val actual = (answer as? MathAnswer.Graph)?.points ?: return wrongKind()
        if (actual.any { !it.x.isFinite() || !it.y.isFinite() }) return fail("Las coordenadas deben ser finitas.")
        val expected = points(spec.getJSONArray("expected"))
        val tolerance = tolerance(spec)
        val mode = spec.optString("mode", "point")
        val correct = when (mode) {
            "point" -> actual.size == 1 && expected.size == 1 && closePoint(actual[0], expected[0], tolerance)
            "line" -> {
                if (actual.size != 2 || expected.size != 2 || distance(actual[0], actual[1]) <= 1e-12) false
                else {
                    val span = distance(expected[0], expected[1])
                    span > 1e-12 && actual.all { abs(cross(expected[0], expected[1], it)) / span <= tolerance }
                }
            }
            "scatter" -> unorderedPoints(actual, expected, tolerance)
            "polygon" -> cyclicPoints(actual, expected, tolerance)
            "parabola" -> {
                if (actual.size != 2 || expected.size != 2 || !closePoint(actual[0], expected[0], tolerance)) false
                else {
                    val expectedA = parabolaCoefficient(expected, 1e-12)
                    val actualA = try { parabolaCoefficient(actual, 1e-12) } catch (_: IllegalArgumentException) { null }
                    actualA != null && near(actualA, expectedA, tolerance)
                }
            }
            else -> return fail("Modo de gráfica no soportado: $mode")
        }
        return result(correct)
    }

    private fun geometry(spec: JSONObject, answer: MathAnswer): MathValidationResult {
        val points = (answer as? MathAnswer.Graph)?.points ?: return wrongKind()
        if (!MathGeometry.isSimplePolygon(points)) return fail("Construye un polígono sin cruces ni lados de longitud cero.")
        val expected = number(spec, if (spec.has("expected")) "expected" else "area")
        val tolerance = tolerance(spec)
        if (spec.optString("shape", "polygon") == "rectangle" && !MathGeometry.isRectangle(points, tolerance)) {
            return fail("La figura debe ser un rectángulo.")
        }
        return result(near(MathGeometry.area(points), expected, tolerance))
    }

    private fun unorderedPoints(actual: List<MathPoint>, expected: List<MathPoint>, tolerance: Double): Boolean {
        if (actual.size != expected.size) return false
        // Bipartite matching avoids a greedy ambiguity when two tolerance regions overlap.
        val matched = IntArray(expected.size) { -1 }
        fun match(index: Int, visited: BooleanArray): Boolean {
            for (target in expected.indices) {
                if (!visited[target] && closePoint(actual[index], expected[target], tolerance)) {
                    visited[target] = true
                    if (matched[target] == -1 || match(matched[target], visited)) {
                        matched[target] = index
                        return true
                    }
                }
            }
            return false
        }
        return actual.indices.all { match(it, BooleanArray(expected.size)) }
    }

    private fun cyclicPoints(actual: List<MathPoint>, expected: List<MathPoint>, tolerance: Double): Boolean {
        if (actual.size != expected.size || actual.size < 3 || !MathGeometry.isSimplePolygon(actual)) return false
        return expected.indices.any { start ->
            actual.indices.all { closePoint(actual[it], expected[(start + it) % expected.size], tolerance) } ||
                actual.indices.all { closePoint(actual[it], expected[(start - it + expected.size) % expected.size], tolerance) }
        }
    }

    private fun parabolaCoefficient(points: List<MathPoint>, epsilon: Double): Double {
        require(points.size == 2) { "La parábola requiere vértice y otro punto." }
        val dx = points[1].x - points[0].x
        require(abs(dx) > epsilon) { "El segundo punto de la parábola debe tener otra coordenada x." }
        val a = (points[1].y - points[0].y) / (dx * dx)
        require(a.isFinite() && abs(a) > epsilon) { "Los puntos deben definir una parábola no degenerada." }
        return a
    }

    private fun tolerance(spec: JSONObject, default: Double = 1e-6): Double {
        val value = if (spec.has("tolerance")) spec.optDouble("tolerance", Double.NaN) else default
        require(value.isFinite() && value >= 0.0) { "La tolerancia debe ser finita y no negativa." }
        return value
    }

    private fun number(spec: JSONObject, key: String): Double {
        val value = Rational.parse(spec.opt(key)?.toString() ?: "")?.toDouble()
        require(value != null && value.isFinite()) { "validator.$key requiere un número finito." }
        return value
    }

    private fun requiredArray(spec: JSONObject, key: String): JSONArray = spec.optJSONArray(key)
        ?: throw IllegalArgumentException("validator.$key requiere un arreglo.")

    private fun requiredObject(spec: JSONObject, key: String): JSONObject = spec.optJSONObject(key)
        ?: throw IllegalArgumentException("validator.$key requiere un objeto.")

    private fun stringList(values: JSONArray): List<String> = (0 until values.length()).map {
        val value = values.get(it)
        require(value is String && value.isNotBlank()) { "Los IDs esperados deben ser textos no vacíos." }
        value
    }.also { require(it.distinct().size == it.size) { "Los IDs esperados no pueden repetirse." } }

    private fun stringMap(values: JSONObject): Map<String, String> = values.keys().asSequence().associateWith {
        val value = values.get(it)
        require(value is String && it.isNotBlank() && value.isNotBlank()) { "Los pares esperados requieren IDs de texto." }
        value
    }

    private fun points(values: JSONArray): List<MathPoint> {
        require(values.length() <= 100) { "Máximo 100 puntos por validador." }
        return (0 until values.length()).map {
            val point = values.optJSONObject(it) ?: throw IllegalArgumentException("Cada punto requiere x e y.")
            MathPoint(number(point, "x"), number(point, "y"))
        }
    }

    private fun near(a: Double, b: Double, tolerance: Double) = a.isFinite() && b.isFinite() && abs(a - b) <= tolerance
    private fun rationalNear(a: Rational, b: Rational, tolerance: Double): Boolean {
        if (tolerance == 0.0) return a == b
        val exactTolerance = Rational.parse(tolerance.toString()) ?: return false
        return (a - b).absolute() <= exactTolerance
    }
    private fun closePoint(a: MathPoint, b: MathPoint, tolerance: Double) = near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance)
    private fun result(correct: Boolean) = MathValidationResult(correct, if (correct) "¡Correcto!" else "Revisa tu respuesta e inténtalo de nuevo.")
    private fun fail(message: String) = MathValidationResult(false, message)
    private fun wrongKind() = fail("El mecanismo de respuesta no coincide con el validador.")
}

/** Pure polygon helpers; crossings and degeneracy are rejected before area can be accepted. */
object MathGeometry {
    fun area(points: List<MathPoint>): Double {
        if (points.size < 3) return 0.0
        // Subtract a local origin before the shoelace sum. Translating a small figure far
        // from (0,0) otherwise cancels large products and can lose its whole area.
        val origin = points.first()
        return abs((1 until points.lastIndex).sumOf { cross(origin, points[it], points[it + 1]) }) / 2.0
    }

    fun isSimplePolygon(points: List<MathPoint>): Boolean {
        if (points.size < 3 || points.size > 100 || points.any { !it.x.isFinite() || !it.y.isFinite() }) return false
        if (points.indices.any { distance(points[it], points[(it + 1) % points.size]) <= 1e-10 }) return false
        if (!area(points).isFinite() || area(points) <= 1e-10) return false
        for (i in points.indices) for (j in i + 1 until points.size) {
            if (j == i + 1 || (i == 0 && j == points.lastIndex)) continue
            if (intersects(points[i], points[(i + 1) % points.size], points[j], points[(j + 1) % points.size])) return false
        }
        // A reversal at adjacent collinear edges overlaps even though the edges share an endpoint.
        for (i in points.indices) {
            val before = points[(i - 1 + points.size) % points.size]
            val here = points[i]
            val after = points[(i + 1) % points.size]
            if (abs(cross(before, here, after)) <= 1e-10 &&
                (before.x - here.x) * (after.x - here.x) + (before.y - here.y) * (after.y - here.y) > 0) return false
        }
        return true
    }

    fun isRectangle(points: List<MathPoint>, tolerance: Double = 1e-6): Boolean {
        if (points.size != 4 || !isSimplePolygon(points)) return false
        return points.indices.all {
            val previous = points[(it + 3) % 4]
            val next = points[(it + 1) % 4]
            val here = points[it]
            val dot = (previous.x - here.x) * (next.x - here.x) + (previous.y - here.y) * (next.y - here.y)
            abs(dot) / (distance(previous, here) * distance(next, here)) <= max(tolerance, 1e-10)
        }
    }

    private fun intersects(a: MathPoint, b: MathPoint, c: MathPoint, d: MathPoint): Boolean {
        val first = cross(a, b, c)
        val second = cross(a, b, d)
        val third = cross(c, d, a)
        val fourth = cross(c, d, b)
        if (((first > 0 && second < 0) || (first < 0 && second > 0)) &&
            ((third > 0 && fourth < 0) || (third < 0 && fourth > 0))) return true
        fun onSegment(p: MathPoint, q: MathPoint, r: MathPoint, cross: Double): Boolean =
            abs(cross) <= 1e-10 && r.x >= minOf(p.x, q.x) - 1e-10 && r.x <= maxOf(p.x, q.x) + 1e-10 &&
                r.y >= minOf(p.y, q.y) - 1e-10 && r.y <= maxOf(p.y, q.y) + 1e-10
        return onSegment(a, b, c, first) || onSegment(a, b, d, second) ||
            onSegment(c, d, a, third) || onSegment(c, d, b, fourth)
    }
}

private fun cross(a: MathPoint, b: MathPoint, c: MathPoint): Double =
    (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)

private fun distance(a: MathPoint, b: MathPoint) = hypot(a.x - b.x, a.y - b.y)

private data class Rational private constructor(val numerator: BigInteger, val denominator: BigInteger) {
    operator fun plus(other: Rational) = of(numerator * other.denominator + other.numerator * denominator, denominator * other.denominator)
    operator fun unaryMinus() = of(-numerator, denominator)
    operator fun minus(other: Rational) = this + -other
    operator fun times(other: Rational) = of(numerator * other.numerator, denominator * other.denominator)
    operator fun div(other: Rational) = of(numerator * other.denominator, denominator * other.numerator)
    fun toDouble() = BigDecimal(numerator).divide(BigDecimal(denominator), MathContext.DECIMAL64).toDouble()
    fun isZero() = numerator == BigInteger.ZERO
    fun absolute() = of(numerator.abs(), denominator)
    operator fun compareTo(other: Rational): Int = (numerator * other.denominator).compareTo(other.numerator * denominator)

    companion object {
        val ZERO = of(BigInteger.ZERO, BigInteger.ONE)
        val ONE = of(BigInteger.ONE, BigInteger.ONE)
        fun of(numerator: BigInteger, denominator: BigInteger): Rational {
            require(denominator != BigInteger.ZERO) { "El denominador debe ser distinto de cero." }
            val sign = if (denominator.signum() < 0) BigInteger.valueOf(-1) else BigInteger.ONE
            val gcd = numerator.gcd(denominator)
            return Rational(numerator / gcd * sign, denominator / gcd * sign)
        }

        fun parse(raw: String): Rational? {
            return try {
            var text = raw.trim().replace('−', '-').replace(',', '.')
            if (text.length > 256 || text.isEmpty()) return null
            val percent = text.endsWith('%')
            if (percent) text = text.dropLast(1).trim()
            val parts = text.split('/')
            if (parts.size !in 1..2) return null
            fun decimal(value: String): Rational {
                val decimal = BigDecimal(value.trim())
                require(abs(decimal.scale()) <= 1000 && decimal.precision() <= 256)
                return if (decimal.scale() >= 0) of(decimal.unscaledValue(), BigInteger.TEN.pow(decimal.scale()))
                    else of(decimal.unscaledValue() * BigInteger.TEN.pow(-decimal.scale()), BigInteger.ONE)
            }
            var value = decimal(parts[0])
            if (parts.size == 2) value /= decimal(parts[1])
            if (percent) value /= of(BigInteger.valueOf(100), BigInteger.ONE)
            value
            } catch (_: Exception) { null }
        }
    }
}

private data class Monomial(val powers: Map<String, Int>) {
    fun times(other: Monomial): Monomial {
        val merged = powers.toMutableMap()
        other.powers.forEach { (name, power) -> merged[name] = (merged[name] ?: 0) + power }
        require(merged.values.sum() <= 16) { UNSUPPORTED_EXPRESSION }
        return Monomial(merged)
    }
    fun sortKey() = powers.toSortedMap().entries.joinToString(";") { "${it.key}:${it.value}" }
}

private data class Polynomial(val terms: Map<Monomial, Rational>) {
    operator fun plus(other: Polynomial): Polynomial {
        val sum = terms.toMutableMap()
        other.terms.forEach { (key, value) -> sum[key] = (sum[key] ?: Rational.ZERO) + value }
        return normalized(sum)
    }
    operator fun unaryMinus() = normalized(terms.mapValues { -it.value })
    operator fun minus(other: Polynomial) = this + -other
    operator fun times(other: Polynomial): Polynomial {
        var result = constant(Rational.ZERO)
        for ((a, av) in terms) for ((b, bv) in other.terms) result += normalized(mapOf(a.times(b) to av * bv))
        return result
    }
    fun divide(other: Polynomial): Polynomial {
        val divisor = other.terms[Monomial(emptyMap())]
        require(other.terms.size == 1 && divisor != null && !divisor.isZero()) { UNSUPPORTED_EXPRESSION }
        return normalized(terms.mapValues { it.value / divisor })
    }
    fun pow(exponent: Int): Polynomial {
        require(exponent in 0..8) { UNSUPPORTED_EXPRESSION }
        var value = constant(Rational.ONE)
        repeat(exponent) { value *= this }
        return value
    }
    fun equationNormalized(): Polynomial {
        val pivot = terms.entries.minByOrNull { it.key.sortKey() }?.value ?: return this
        return normalized(terms.mapValues { it.value / pivot })
    }
    fun variables(): Set<String> = terms.keys.flatMap { it.powers.keys }.filter { it != "pi" }.toSet()

    companion object {
        fun constant(value: Rational) = normalized(mapOf(Monomial(emptyMap()) to value))
        fun variable(name: String) = Polynomial(mapOf(Monomial(mapOf(name to 1)) to Rational.ONE))
        private fun normalized(terms: Map<Monomial, Rational>): Polynomial {
            val cleaned = terms.filterValues { !it.isZero() }
            require(cleaned.size <= 128) { UNSUPPORTED_EXPRESSION }
            return Polynomial(cleaned)
        }
    }
}

private const val UNSUPPORTED_EXPRESSION = "Expresión no soportada todavía. Usa polinomios, fracciones constantes y π; sin funciones ni división por variables."

private data class PolynomialExpression(val polynomial: Polynomial, val equation: Boolean) {
    companion object {
        fun parse(raw: String): PolynomialExpression {
            require(raw.isNotBlank() && raw.length <= 256) { UNSUPPORTED_EXPRESSION }
            val parts = raw.split('=')
            require(parts.size in 1..2 && parts.all { it.isNotBlank() }) { UNSUPPORTED_EXPRESSION }
            val left = ExpressionParser(parts[0]).parse()
            if (parts.size == 1) {
                require(left.variables().size <= 1) { UNSUPPORTED_EXPRESSION }
                return PolynomialExpression(left, false)
            }
            val difference = left - ExpressionParser(parts[1]).parse()
            require(difference.variables().size <= 2) { UNSUPPORTED_EXPRESSION }
            return PolynomialExpression(difference.equationNormalized(), true)
        }
    }
}

/** Small recursive descent parser. No eval, reflection, scripting engine or numerical sampling. */
private class ExpressionParser(raw: String) {
    private val source = raw.replace('−', '-').replace('×', '*').replace('·', '*').replace('÷', '/').replace(',', '.')
    private var position = 0
    private var operations = 0

    fun parse(): Polynomial {
        val value = sum()
        skipSpaces()
        require(position == source.length) { UNSUPPORTED_EXPRESSION }
        return value
    }

    private fun sum(): Polynomial {
        var value = product()
        while (true) {
            value = when {
                consume('+') -> value + product()
                consume('-') -> value - product()
                else -> return value
            }
        }
    }

    private fun product(): Polynomial {
        var value = unary()
        while (true) {
            value = when {
                consume('*') -> value * unary()
                consume('/') -> value.divide(unary())
                beginsAtom() -> value * unary()
                else -> return value
            }
            require(++operations <= 256) { UNSUPPORTED_EXPRESSION }
        }
    }

    private fun unary(): Polynomial = when {
        consume('+') -> unary()
        consume('-') -> -unary()
        else -> power()
    }

    private fun power(): Polynomial {
        val value = atom()
        if (!consume('^')) return value
        skipSpaces()
        val start = position
        while (position < source.length && source[position].isDigit()) position++
        require(position > start) { UNSUPPORTED_EXPRESSION }
        val exponent = source.substring(start, position).toIntOrNull()
        require(exponent != null && exponent in 0..8) { UNSUPPORTED_EXPRESSION }
        return value.pow(exponent)
    }

    private fun atom(): Polynomial {
        skipSpaces()
        require(position < source.length) { UNSUPPORTED_EXPRESSION }
        if (consume('(')) {
            val value = sum()
            require(consume(')')) { UNSUPPORTED_EXPRESSION }
            return value
        }
        if (source[position].isDigit() || source[position] == '.') {
            val start = position
            while (position < source.length && (source[position].isDigit() || source[position] == '.')) position++
            val rational = Rational.parse(source.substring(start, position))
            require(rational != null) { UNSUPPORTED_EXPRESSION }
            return Polynomial.constant(rational)
        }
        if (source[position] == 'π') { position++; return Polynomial.variable("pi") }
        if (source.startsWith("pi", position)) { position += 2; return Polynomial.variable("pi") }
        val letter = source[position]
        require(letter in 'a'..'z' && letter !in setOf('e', 'i')) { UNSUPPORTED_EXPRESSION }
        position++
        // Allow xy as multiplication, but reject function-like identifiers such as sin/cos.
        require(!source.substring(position - 1).startsWith("sin") &&
            !source.substring(position - 1).startsWith("cos") &&
            !source.substring(position - 1).startsWith("tan") &&
            !source.substring(position - 1).startsWith("sqrt") &&
            !source.substring(position - 1).startsWith("log")) { UNSUPPORTED_EXPRESSION }
        return Polynomial.variable(letter.toString())
    }

    private fun beginsAtom(): Boolean {
        skipSpaces()
        if (position >= source.length) return false
        val char = source[position]
        return char == '(' || char == '.' || char.isDigit() || char in 'a'..'z' || char == 'π'
    }

    private fun consume(char: Char): Boolean {
        skipSpaces()
        return if (position < source.length && source[position] == char) { position++; true } else false
    }

    private fun skipSpaces() { while (position < source.length && source[position].isWhitespace()) position++ }
}

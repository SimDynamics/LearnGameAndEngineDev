package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.math.MathDelimiter
import com.learngraphicsprogramming.app.math.MathExpression
import java.math.BigDecimal

enum class CalculatorAngleMode { RAD, DEG }

/** A small arithmetic language; engine commands and editor/navigation state cannot enter it. */
internal sealed interface ScalarExpression {
    // Decimal leaves remain exact until the engine explicitly requests a numerical result.
    data class Number(val value: BigDecimal) : ScalarExpression {
        constructor(value: Int) : this(BigDecimal.valueOf(value.toLong()))
    }
    enum class Constant : ScalarExpression { PI, E }
    data class Binary(val operation: Operation, val left: ScalarExpression, val right: ScalarExpression) : ScalarExpression
    data class Function(val function: FunctionKind, val argument: ScalarExpression) : ScalarExpression
    data class Root(val radicand: ScalarExpression, val index: ScalarExpression) : ScalarExpression
    enum class Operation { ADD, SUBTRACT, MULTIPLY, DIVIDE, POWER, COMBINATION, PERMUTATION }
    enum class FunctionKind { SIN, COS, TAN, ARCSIN, ARCCOS, ARCTAN, LOG, ABS, FACTORIAL }
}

/** Converts semantic regions, preserving nesting and arithmetic precedence without LaTeX rewrites. */
internal class CalculatorExpressionCompiler(private val angleMode: CalculatorAngleMode) {
    private var nodeCount = 0

    fun compile(expression: MathExpression.Sequence): ScalarExpression = sequence(expression, 0)

    private fun visit(depth: Int) {
        require(depth <= 32 && ++nodeCount <= 512) { "La expresión es demasiado grande para calcularla." }
    }

    private fun sequence(sequence: MathExpression.Sequence, depth: Int): ScalarExpression {
        visit(depth)
        require(sequence.children.isNotEmpty()) { "Completa los espacios vacíos de la expresión." }
        val tokens = mutableListOf<Token>()
        var index = 0
        while (index < sequence.children.size) {
            val child = sequence.children[index++]
            if (child is MathExpression.Number) {
                // Precise editor selections may split a numeric leaf into adjacent leaves.
                val value = StringBuilder(child.value)
                visit(depth + 1)
                while (index < sequence.children.size && sequence.children[index] is MathExpression.Number) {
                    visit(depth + 1)
                    value.append((sequence.children[index++] as MathExpression.Number).value)
                }
                val text = value.toString()
                require(text.length <= 64 && NUMBER.matches(text)) { "Hay un número incompleto o inválido." }
                val number = text.toBigDecimalOrNull()
                require(number != null && number.toDouble().isFinite()) { "El número está fuera del rango admitido." }
                tokens += Token.Value(ScalarExpression.Number(number))
            } else if (child is MathExpression.Operator || child is MathExpression.Symbol && child.value in setOf("%", "!")) {
                visit(depth + 1)
                val value = if (child is MathExpression.Operator) child.value else (child as MathExpression.Symbol).value
                val operator = when (value) {
                    "+" -> "+"
                    "-", "−" -> "-"
                    "*", "×", "·" -> "*"
                    "/", "÷" -> "/"
                    "^", "%", "!", "nCr", "nPr" -> value
                    else -> error("Esta operación todavía no está disponible en la calculadora.")
                }
                tokens += Token.Operator(operator)
            } else if (child is MathExpression.Scripts && child.lower == null && child.upper != null &&
                child.base.children.singleOrNull().let {
                    it is MathExpression.Operator && it.value in setOf("%", "!") ||
                        it is MathExpression.Symbol && it.value in setOf("%", "!")
                }) {
                // LaTeX's !^2 places the superscript on the punctuation leaf. Its
                // arithmetic meaning is the power of the preceding postfix result.
                visit(depth + 1)
                visit(depth + 2) // Base region.
                visit(depth + 3) // Postfix leaf.
                val postfix = child.base.children.single()
                tokens += Token.Operator(if (postfix is MathExpression.Operator) postfix.value else (postfix as MathExpression.Symbol).value)
                tokens += Token.Operator("^")
                tokens += Token.Value(sequence(child.upper, depth + 2))
            } else {
                tokens += Token.Value(atom(child, depth + 1))
            }
        }
        return ArithmeticParser(tokens).parse()
    }

    private fun atom(expression: MathExpression, depth: Int): ScalarExpression {
        visit(depth)
        fun region(value: MathExpression.Sequence) = sequence(value, depth + 1)
        return when (expression) {
            is MathExpression.Sequence -> region(expression)
            is MathExpression.Symbol -> constant(expression.value)
            is MathExpression.Variable -> constant(expression.name)
            is MathExpression.Fraction -> binary(ScalarExpression.Operation.DIVIDE, region(expression.numerator), region(expression.denominator))
            is MathExpression.Scripts -> {
                require(expression.lower == null) { "Los subíndices no tienen un valor numérico en esta versión." }
                val base = region(expression.base)
                expression.upper?.let { binary(ScalarExpression.Operation.POWER, base, region(it)) } ?: base
            }
            is MathExpression.Root -> ScalarExpression.Root(region(expression.radicand),
                expression.index?.let(::region) ?: ScalarExpression.Number(2))
            is MathExpression.Group -> region(expression.content).let {
                if (expression.delimiter == MathDelimiter.ABSOLUTE) ScalarExpression.Function(ScalarExpression.FunctionKind.ABS, it) else it
            }
            is MathExpression.Function -> {
                val argument = region(expression.argument)
                when (expression.name) {
                    "sin", "cos", "tan" -> {
                        require(expression.base == null) { "Esta función no admite una base." }
                        val radians = if (angleMode == CalculatorAngleMode.DEG) binary(ScalarExpression.Operation.DIVIDE,
                            binary(ScalarExpression.Operation.MULTIPLY, argument, ScalarExpression.Constant.PI), ScalarExpression.Number(180)) else argument
                        ScalarExpression.Function(when (expression.name) {
                            "sin" -> ScalarExpression.FunctionKind.SIN
                            "cos" -> ScalarExpression.FunctionKind.COS
                            else -> ScalarExpression.FunctionKind.TAN
                        }, radians)
                    }
                    "arcsin", "arccos", "arctan" -> {
                        require(expression.base == null) { "Esta función no admite una base." }
                        val radians = ScalarExpression.Function(when (expression.name) {
                            "arcsin" -> ScalarExpression.FunctionKind.ARCSIN
                            "arccos" -> ScalarExpression.FunctionKind.ARCCOS
                            else -> ScalarExpression.FunctionKind.ARCTAN
                        }, argument)
                        if (angleMode == CalculatorAngleMode.DEG) binary(ScalarExpression.Operation.DIVIDE,
                            binary(ScalarExpression.Operation.MULTIPLY, radians, ScalarExpression.Number(180)), ScalarExpression.Constant.PI)
                        else radians
                    }
                    "ln", "log" -> {
                        require(expression.name != "ln" || expression.base == null) { "ln no admite una base." }
                        val natural = ScalarExpression.Function(ScalarExpression.FunctionKind.LOG, argument)
                        if (expression.name == "ln") natural else binary(ScalarExpression.Operation.DIVIDE, natural,
                            ScalarExpression.Function(ScalarExpression.FunctionKind.LOG,
                                expression.base?.let(::region) ?: ScalarExpression.Number(10)))
                    }
                    else -> error("La función ${expression.name} todavía no está disponible.")
                }
            }
            else -> error("Esta estructura todavía no se puede evaluar en la calculadora.")
        }
    }

    private fun constant(value: String): ScalarExpression = when (value) {
        "π" -> ScalarExpression.Constant.PI
        "e" -> ScalarExpression.Constant.E
        else -> error("La calculadora necesita valores numéricos; $value no está definido.")
    }

    private sealed interface Token {
        data class Value(val expression: ScalarExpression) : Token
        data class Operator(val symbol: String) : Token
    }

    private class ArithmeticParser(private val tokens: List<Token>) {
        private var position = 0
        fun parse(): ScalarExpression {
            val result = sum()
            require(position == tokens.size) { "Revisa los operadores de la expresión." }
            return result
        }
        private fun take(operator: String): Boolean =
            if ((tokens.getOrNull(position) as? Token.Operator)?.symbol == operator) { position++; true } else false

        private fun sum(): ScalarExpression {
            var value = product()
            while (true) value = when {
                take("+") -> binary(ScalarExpression.Operation.ADD, value, product())
                take("-") -> binary(ScalarExpression.Operation.SUBTRACT, value, product())
                else -> return value
            }
        }
        private fun product(): ScalarExpression {
            var value = combinatorial()
            while (true) value = when {
                take("*") -> binary(ScalarExpression.Operation.MULTIPLY, value, combinatorial())
                take("/") -> binary(ScalarExpression.Operation.DIVIDE, value, combinatorial())
                tokens.getOrNull(position) is Token.Value -> binary(ScalarExpression.Operation.MULTIPLY, value, combinatorial())
                else -> return value
            }
        }
        private fun combinatorial(): ScalarExpression {
            var value = unary()
            while (true) value = when {
                take("nCr") -> binary(ScalarExpression.Operation.COMBINATION, value, unary())
                take("nPr") -> binary(ScalarExpression.Operation.PERMUTATION, value, unary())
                else -> return value
            }
        }
        private fun unary(): ScalarExpression = when {
            take("+") -> unary()
            take("-") -> binary(ScalarExpression.Operation.SUBTRACT, ScalarExpression.Number(0), unary())
            else -> power()
        }
        private fun power(): ScalarExpression {
            val value = postfix()
            return if (take("^")) binary(ScalarExpression.Operation.POWER, value, unary()) else value
        }
        private fun postfix(): ScalarExpression {
            var value = (tokens.getOrNull(position++) as? Token.Value)?.expression
                ?: error("Falta un valor antes o después de un operador.")
            while (true) value = when {
                take("%") -> binary(ScalarExpression.Operation.DIVIDE, value, ScalarExpression.Number(100))
                take("!") -> ScalarExpression.Function(ScalarExpression.FunctionKind.FACTORIAL, value)
                else -> return value
            }
        }
    }

    companion object {
        private val NUMBER = Regex("(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)")
        private fun binary(operation: ScalarExpression.Operation, left: ScalarExpression, right: ScalarExpression) = ScalarExpression.Binary(operation, left, right)
    }
}

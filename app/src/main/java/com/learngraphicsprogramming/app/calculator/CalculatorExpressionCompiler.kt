package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.math.MathDelimiter
import com.learngraphicsprogramming.app.math.MathExpression

enum class CalculatorAngleMode { RAD, DEG }

/** A small arithmetic language; engine commands and editor/navigation state cannot enter it. */
internal sealed interface ScalarExpression {
    data class Number(val value: Double) : ScalarExpression
    enum class Constant : ScalarExpression { PI, E }
    data class Binary(val operation: Operation, val left: ScalarExpression, val right: ScalarExpression) : ScalarExpression
    data class Function(val function: FunctionKind, val argument: ScalarExpression) : ScalarExpression
    enum class Operation { ADD, SUBTRACT, MULTIPLY, DIVIDE, POWER }
    enum class FunctionKind { SIN, COS, TAN, LOG, ABS }
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
                val number = text.toDoubleOrNull()
                require(number != null && number.isFinite()) { "El número está fuera del rango admitido." }
                tokens += Token.Value(ScalarExpression.Number(number))
            } else if (child is MathExpression.Operator) {
                visit(depth + 1)
                val operator = when (child.value) {
                    "+" -> '+'
                    "-", "−" -> '-'
                    "*", "×", "·" -> '*'
                    "/", "÷" -> '/'
                    "^" -> '^'
                    else -> error("Esta operación todavía no está disponible en la calculadora.")
                }
                tokens += Token.Operator(operator)
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
            is MathExpression.Root -> binary(ScalarExpression.Operation.POWER, region(expression.radicand),
                expression.index?.let { binary(ScalarExpression.Operation.DIVIDE, ScalarExpression.Number(1.0), region(it)) }
                    ?: ScalarExpression.Number(0.5))
            is MathExpression.Group -> region(expression.content).let {
                if (expression.delimiter == MathDelimiter.ABSOLUTE) ScalarExpression.Function(ScalarExpression.FunctionKind.ABS, it) else it
            }
            is MathExpression.Function -> {
                val argument = region(expression.argument)
                when (expression.name) {
                    "sin", "cos", "tan" -> {
                        require(expression.base == null) { "Esta función no admite una base." }
                        val radians = if (angleMode == CalculatorAngleMode.DEG) binary(ScalarExpression.Operation.DIVIDE,
                            binary(ScalarExpression.Operation.MULTIPLY, argument, ScalarExpression.Constant.PI), ScalarExpression.Number(180.0)) else argument
                        ScalarExpression.Function(when (expression.name) {
                            "sin" -> ScalarExpression.FunctionKind.SIN
                            "cos" -> ScalarExpression.FunctionKind.COS
                            else -> ScalarExpression.FunctionKind.TAN
                        }, radians)
                    }
                    "ln", "log" -> {
                        require(expression.name != "ln" || expression.base == null) { "ln no admite una base." }
                        val natural = ScalarExpression.Function(ScalarExpression.FunctionKind.LOG, argument)
                        if (expression.name == "ln") natural else binary(ScalarExpression.Operation.DIVIDE, natural,
                            ScalarExpression.Function(ScalarExpression.FunctionKind.LOG,
                                expression.base?.let(::region) ?: ScalarExpression.Number(10.0)))
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
        data class Operator(val symbol: Char) : Token
    }

    private class ArithmeticParser(private val tokens: List<Token>) {
        private var position = 0
        fun parse(): ScalarExpression {
            val result = sum()
            require(position == tokens.size) { "Revisa los operadores de la expresión." }
            return result
        }
        private fun take(operator: Char): Boolean =
            if ((tokens.getOrNull(position) as? Token.Operator)?.symbol == operator) { position++; true } else false

        private fun sum(): ScalarExpression {
            var value = product()
            while (true) value = when {
                take('+') -> binary(ScalarExpression.Operation.ADD, value, product())
                take('-') -> binary(ScalarExpression.Operation.SUBTRACT, value, product())
                else -> return value
            }
        }
        private fun product(): ScalarExpression {
            var value = unary()
            while (true) value = when {
                take('*') -> binary(ScalarExpression.Operation.MULTIPLY, value, unary())
                take('/') -> binary(ScalarExpression.Operation.DIVIDE, value, unary())
                tokens.getOrNull(position) is Token.Value -> binary(ScalarExpression.Operation.MULTIPLY, value, unary())
                else -> return value
            }
        }
        private fun unary(): ScalarExpression = when {
            take('+') -> unary()
            take('-') -> binary(ScalarExpression.Operation.SUBTRACT, ScalarExpression.Number(0.0), unary())
            else -> power()
        }
        private fun power(): ScalarExpression {
            val value = (tokens.getOrNull(position++) as? Token.Value)?.expression
                ?: error("Falta un valor antes o después de un operador.")
            return if (take('^')) binary(ScalarExpression.Operation.POWER, value, unary()) else value
        }
    }

    companion object {
        private val NUMBER = Regex("(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)")
        private fun binary(operation: ScalarExpression.Operation, left: ScalarExpression, right: ScalarExpression) = ScalarExpression.Binary(operation, left, right)
    }
}

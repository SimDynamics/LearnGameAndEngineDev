package com.learngraphicsprogramming.app.math

import com.learngraphicsprogramming.app.R
import android.widget.EditText

/**
 * Acción semántica ejecutada por la barra matemática.
 *
 * La barra ya no necesita saber qué texto representa cada operación.
 *
 * Hoy las acciones se aplican sobre un EditText simple.
 * En el futuro MathExpressionEditor podrá interpretar las mismas acciones
 * estructuralmente, por ejemplo:
 *
 * FRACTION -> frac{□}{□}
 * POWER    -> □^□
 * ROOT     -> sqrt{□}
 */
sealed class MathKeyboardAction {

    abstract val label: String
    abstract val description: MathUiMessage?
    open val contentDescription: String get() = description?.diagnostic ?: label
    fun localizedDescription(context: android.content.Context): String = description?.resolve(context) ?: contentDescription

    abstract fun apply(
        target: EditText,
        minusInsertion: String
    )

    data class Symbol(
        private val symbol: String
    ) : MathKeyboardAction() {

        override val label: String = symbol
        override val description: MathUiMessage? = null
        override val contentDescription: String = symbol

        override fun apply(
            target: EditText,
            minusInsertion: String
        ) {
            replaceSelection(
                target,
                if (symbol == "−") minusInsertion else symbol
            )
        }
    }

    data object Fraction : MathKeyboardAction() {

        // Por ahora mostramos un símbolo de fracción limpio.
        // Después podremos sustituir solamente su representación visual
        // por una plantilla apilada sin cambiar la acción.
        override val label: String = "□\n―\n□"

        override val description: MathUiMessage =
            MathUiMessage(R.string.math_ui_insert_fraction)

        override fun apply(
            target: EditText,
            minusInsertion: String
        ) {
            // Input SIMPLE:
            // seguimos escribiendo / exactamente como antes.
            //
            // FUTURO input STRUCTURED:
            // MathExpressionEditor.insertFraction()
            replaceSelection(target, "/")
        }
    }

    data object Parentheses : MathKeyboardAction() {

        override val label: String = "()"

        override val description: MathUiMessage =
            MathUiMessage(R.string.math_ui_insert_parentheses)

        override fun apply(
            target: EditText,
            minusInsertion: String
        ) {
            val start = target.selectionStart.coerceAtLeast(0)
            val end = target.selectionEnd.coerceAtLeast(start)

            val selected =
                target.text.substring(start, end)

            target.text.replace(
                start,
                end,
                "($selected)"
            )

            target.setSelection(start + 1)
        }
    }

    data object Backspace : MathKeyboardAction() {

        override val label: String = "⌫"

        override val description: MathUiMessage =
            MathUiMessage(R.string.math_ui_delete)

        override fun apply(
            target: EditText,
            minusInsertion: String
        ) {
            val start = target.selectionStart.coerceAtLeast(0)
            val end = target.selectionEnd.coerceAtLeast(start)

            when {
                start != end ->
                    target.text.delete(start, end)

                start > 0 ->
                    target.text.delete(start - 1, start)
            }
        }
    }

    /** One action has linear insertion for SIMPLE and AST insertion for STRUCTURED. */
    sealed class Structure(override val label: String, override val description: MathUiMessage,
        private val linear: String) : MathKeyboardAction() {
        override fun apply(target: EditText, minusInsertion: String) = replaceSelection(target, linear)
    }
    data object Power : Structure("xⁿ", MathUiMessage(R.string.math_ui_insert_power), "^")
    data object Subscript : Structure("xₙ", MathUiMessage(R.string.math_ui_insert_subscript), "_")
    data object Root : Structure("√", MathUiMessage(R.string.math_ui_insert_root), "√")
    data object NthRoot : Structure("ⁿ√", MathUiMessage(R.string.math_ui_insert_nth_root), "√")
    data object AbsoluteValue : Structure("|□|", MathUiMessage(R.string.math_ui_insert_absolute), "||")
    data object Brackets : Structure("[□]", MathUiMessage(R.string.math_ui_insert_brackets), "[]")
    data object Braces : Structure("{□}", MathUiMessage(R.string.math_ui_insert_braces), "{}")
    data object Integral : Structure("∫", MathUiMessage(R.string.math_ui_insert_integral), "∫")
    data object Sum : Structure("∑", MathUiMessage(R.string.math_ui_insert_sum), "∑")
    data object Product : Structure("∏", MathUiMessage(R.string.math_ui_insert_product), "∏")
    data object Limit : Structure("lim", MathUiMessage(R.string.math_ui_insert_limit), "lim")
    data object Matrix : Structure("▦", MathUiMessage(R.string.math_ui_insert_matrix), "")
    data object Cases : Structure("{…", MathUiMessage(R.string.math_ui_insert_cases), "")
    data object Derivative : Structure("d/dx", MathUiMessage(R.string.math_ui_insert_derivative), "d/dx")
    data class Function(val name: String) : Structure("$name()", MathUiMessage(R.string.math_ui_insert_function, listOf(name)), "$name()")
    data object MoveLeft : Structure("←", MathUiMessage(R.string.math_ui_cursor_back), "")
    data object MoveRight : Structure("→", MathUiMessage(R.string.math_ui_cursor_forward), "")
    data object PreviousSlot : MathKeyboardAction() {
        override val label = ""
        override val description = MathUiMessage(R.string.math_ui_previous_slot)
        override fun apply(target: EditText, minusInsertion: String) = Unit
    }
    data object NextSlot : MathKeyboardAction() {
        override val label = ""
        override val description = MathUiMessage(R.string.math_ui_next_slot)
        override fun apply(target: EditText, minusInsertion: String) = Unit
    }

    companion object {

        /**
         * Convierte los atajos actuales en acciones semánticas.
         *
         * Conservamos "a/b" como alias temporal para no romper contenido
         * existente, pero deja de ser la identidad interna de la fracción.
         */
        fun fromShortcut(shortcut: String): MathKeyboardAction =
            when (shortcut) {
                "a/b" ->
                    Fraction

                "/" ->
                    Symbol("/")

                "()" ->
                    Parentheses

                "⌫" ->
                    Backspace

                "FRACTION" -> Fraction
                "POWER" -> Power
                "SUBSCRIPT" -> Subscript
                "ROOT" -> Root
                "NTH_ROOT" -> NthRoot
                "PARENTHESES" -> Parentheses
                "ABSOLUTE_VALUE" -> AbsoluteValue
                "BRACKETS" -> Brackets
                "BRACES" -> Braces
                "INTEGRAL" -> Integral
                "SUM" -> Sum
                "PRODUCT" -> Product
                "LIMIT" -> Limit
                "MATRIX" -> Matrix
                "CASES" -> Cases
                "DERIVATIVE" -> Derivative
                "MOVE_LEFT" -> MoveLeft
                "MOVE_RIGHT" -> MoveRight
                "PREVIOUS_SLOT" -> PreviousSlot
                "NEXT_SLOT" -> NextSlot

                else ->
                    if (shortcut.startsWith("FUNCTION:")) {
                        Function(shortcut.substringAfter(':'))
                    } else {
                        Symbol(shortcut)
                    }
            }

        private fun replaceSelection(
            target: EditText,
            replacement: String
        ) {
            val start =
                target.selectionStart.coerceAtLeast(0)

            val end =
                target.selectionEnd.coerceAtLeast(start)

            target.text.replace(
                start,
                end,
                replacement
            )
        }
    }
}

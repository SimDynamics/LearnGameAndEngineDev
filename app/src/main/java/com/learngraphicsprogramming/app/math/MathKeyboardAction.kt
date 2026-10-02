package com.learngraphicsprogramming.app.math

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
internal sealed class MathKeyboardAction {

    abstract val label: String
    abstract val contentDescription: String

    abstract fun apply(
        target: EditText,
        minusInsertion: String
    )

    data class Symbol(
        private val symbol: String
    ) : MathKeyboardAction() {

        override val label: String = symbol
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

        override val contentDescription: String =
            "Insertar fracción"

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

        override val contentDescription: String =
            "Insertar paréntesis"

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

        override val contentDescription: String =
            "Borrar"

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

    companion object {

        /**
         * Convierte los atajos actuales en acciones semánticas.
         *
         * Conservamos "a/b" como alias temporal para no romper contenido
         * existente, pero deja de ser la identidad interna de la fracción.
         */
        fun fromShortcut(shortcut: String): MathKeyboardAction =
            when (shortcut) {
                "a/b",
                "/" ->
                    Fraction

                "()" ->
                    Parentheses

                "⌫" ->
                    Backspace

                else ->
                    Symbol(shortcut)
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
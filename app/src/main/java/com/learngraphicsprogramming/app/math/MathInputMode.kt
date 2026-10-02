package com.learngraphicsprogramming.app.math

/**
 * Superficie usada para introducir una respuesta matemática.
 *
 * SIMPLE:
 * EditText lineal actual.
 *
 * STRUCTURED:
 * reservado para MathExpressionEditor.
 */
internal enum class MathInputMode {
    SIMPLE,
    STRUCTURED;

    companion object {
        fun parse(
            value: String?,
            fieldType: String
        ): MathInputMode {

            val mode = when (
                value.orEmpty()
                    .ifBlank { "simple" }
                    .lowercase()
            ) {
                "simple" -> SIMPLE
                "structured" -> STRUCTURED

                else -> throw IllegalArgumentException(
                    "inputMode desconocido: $value"
                )
            }

            require(
                mode != STRUCTURED ||
                        fieldType == "expression"
            ) {
                "inputMode=structured solo corresponde a expresiones matemáticas."
            }

            return mode
        }
    }
}
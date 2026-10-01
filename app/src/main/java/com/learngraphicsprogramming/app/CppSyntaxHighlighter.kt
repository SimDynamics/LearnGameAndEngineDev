package com.learngraphicsprogramming.app

import android.graphics.Color
import android.text.Editable
import android.text.Spannable
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan

object CppSyntaxHighlighter {

    /*
     * Span exclusivo de nuestro resaltador.
     *
     * Así podemos quitar y volver a aplicar
     * los colores sin modificar otros spans
     * internos del EditText.
     */
    private class SyntaxColorSpan(
        color: Int
    ) : ForegroundColorSpan(color)

    /*
     * PALETA C++ DE CHATGPT
     *
     * Colores obtenidos de la referencia
     * visual actual de ChatGPT.
     */

    private val identifierColor =
        Color.rgb(
            171,
            141,
            227
        ) // #AB8DE3

    private val keywordColor =
        Color.rgb(
            231,
            155,
            186
        ) // #E79BBA

    private val stringColor =
        Color.rgb(
            122,
            195,
            141
        ) // #7AC38D

    private val literalColor =
        Color.rgb(
            224,
            151,
            109
        ) // #E0976D

    private val preprocessorColor =
        Color.rgb(
            92,
            156,
            231
        ) // #5C9CE7

    private val commentColor =
        Color.rgb(
            172,
            172,
            172
        ) // #ACACAC

    /*
     * Palabras que ChatGPT muestra en rosa.
     *
     * Los tipos como int, bool, double, etc.
     * NO están aquí porque ChatGPT los muestra
     * en morado.
     */
    private val keywordRegex =
        Regex(
            """\b(?:alignas|alignof|asm|break|case|catch|class|concept|const|consteval|constexpr|constinit|continue|co_await|co_return|co_yield|default|delete|do|else|enum|explicit|export|extern|for|friend|goto|if|inline|mutable|namespace|new|noexcept|operator|private|protected|public|register|requires|return|sizeof|static|static_assert|struct|switch|template|throw|try|typedef|typename|union|using|virtual|volatile|while|import|module)\b"""
        )

    /*
     * Literales que ChatGPT muestra
     * igual que los números.
     */
    private val literalRegex =
        Regex(
            """\b(?:true|false|nullptr)\b"""
        )

    private val numberRegex =
        Regex(
            """(?<![A-Za-z_])(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[fFuUlL]*)(?![A-Za-z_])"""
        )

    /*
     * #include, #define, #ifdef...
     */
    private val preprocessorRegex =
        Regex(
            """(?m)^\s*#\s*[A-Za-z_][A-Za-z0-9_]*"""
        )

    /*
     * <iostream>
     * <string>
     * <vector>
     *
     * ChatGPT los muestra verdes.
     */
    private val includeHeaderRegex =
        Regex(
            """(?m)#\s*include\s*(<[^>\n]+>)"""
        )

    /*
     * Operadores que en la referencia
     * aparecen en rosa.
     *
     * No incluimos '=' porque ChatGPT
     * lo muestra con el color base.
     */
    private val operatorRegex =
        Regex(
            """(?:<<|>>|->|::|\+\+|--|&&|\|\||==|!=|<=|>=|\+|-|\*|/|%|<|>|!|&|\||\^|~)"""
        )

    /*
     * Identificadores.
     *
     * Esto cubre:
     *
     * int
     * string
     * cout
     * std
     * main
     * sumar
     * nombre
     * edad
     *
     * porque ChatGPT los muestra
     * con el mismo morado.
     */
    private val identifierRegex =
        Regex(
            """\b[A-Za-z_][A-Za-z0-9_]*\b"""
        )

    /*
     * Para bloques de código de solo lectura.
     *
     * Lo utilizaremos después también
     * en puzzles y bloques fake.
     */
    fun highlight(
        code: String
    ): SpannableString {

        val result =
            SpannableString(
                code
            )

        applyColors(
            result,
            code
        )

        return result
    }

    /*
     * Para el editor editable.
     */
    fun applyTo(
        editable: Editable
    ) {

        val oldSpans =
            editable.getSpans(
                0,
                editable.length,
                SyntaxColorSpan::class.java
            )

        for (
        span in oldSpans
        ) {

            editable.removeSpan(
                span
            )
        }

        applyColors(
            editable,
            editable.toString()
        )
    }

    private fun applyColors(
        target: Spannable,
        code: String
    ) {

        if (
            code.isEmpty()
        ) {
            return
        }

        /*
         * Permite evitar que, por ejemplo:
         *
         * "int"
         *
         * termine interpretándose como
         * una palabra de C++ dentro
         * de una cadena.
         */
        val occupied =
            BooleanArray(
                code.length
            )

        fun applySpan(
            start: Int,
            end: Int,
            color: Int
        ) {

            if (
                start < 0 ||
                end > code.length ||
                start >= end
            ) {
                return
            }

            target.setSpan(
                SyntaxColorSpan(
                    color
                ),
                start,
                end,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )

            for (
            index in start until end
            ) {

                occupied[index] =
                    true
            }
        }

        fun rangeAvailable(
            start: Int,
            end: Int
        ): Boolean {

            for (
            index in start until end
            ) {

                if (
                    occupied[index]
                ) {
                    return false
                }
            }

            return true
        }

        /*
         * PRIMERA PASADA.
         *
         * Comentarios y cadenas tienen
         * prioridad sobre cualquier otra cosa.
         */
        var index =
            0

        while (
            index < code.length
        ) {

            when {

                /*
                 * Comentario //
                 */
                code.startsWith(
                    "//",
                    index
                ) -> {

                    val newline =
                        code.indexOf(
                            '\n',
                            index + 2
                        )

                    val end =
                        if (
                            newline == -1
                        ) {
                            code.length
                        } else {
                            newline
                        }

                    applySpan(
                        index,
                        end,
                        commentColor
                    )

                    index =
                        end
                }

                /*
                 * Comentario multilínea de C++.
                 */
                code.startsWith(
                    "/*",
                    index
                ) -> {

    val closing =
        code.indexOf(
            "*/",
            index + 2
        )

    val end =
        if (
            closing == -1
        ) {
            code.length
        } else {
            closing + 2
        }

    applySpan(
        index,
        end,
        commentColor
    )

    index =
        end
}

/*
 * Strings y caracteres.
 */
code[index] == '"' ||
        code[index] == '\'' -> {

    val quote =
        code[index]

    var end =
        index + 1

    while (
        end < code.length
    ) {

        /*
         * Escape:
         *
         * \"
         * \'
         * \n
         * etc.
         */
        if (
            code[end] == '\\'
        ) {

            end =
                minOf(
                    end + 2,
                    code.length
                )

            continue
        }

        if (
            code[end] ==
            quote
        ) {

            end++

            break
        }

        end++
    }

    applySpan(
        index,
        end,
        stringColor
    )

    index =
        end
}

else -> {

    index++
}
}
}

fun applyRegex(
regex: Regex,
color: Int
) {

for (
match in regex.findAll(
code
)
) {

val start =
    match.range.first

val end =
    match.range.last + 1

if (
    rangeAvailable(
        start,
        end
    )
) {

    applySpan(
        start,
        end,
        color
    )
}
}
}

/*
* PREPROCESADOR.
*/
applyRegex(
preprocessorRegex,
preprocessorColor
)

/*
* Headers:
*
* <iostream>
* <string>
*/
for (
match in includeHeaderRegex
.findAll(
code
)
) {

val header =
match.groups[1]
    ?: continue

val start =
header.range.first

val end =
header.range.last + 1

if (
rangeAvailable(
    start,
    end
)
) {

applySpan(
    start,
    end,
    stringColor
)
}
}

/*
* PALABRAS RESERVADAS ROSAS.
*/
applyRegex(
keywordRegex,
keywordColor
)

/*
* true, false, nullptr.
*/
applyRegex(
literalRegex,
literalColor
)

/*
* 25
* 19.95
* 0
*/
applyRegex(
numberRegex,
literalColor
)

/*
* +, <<, >>, etc.
*/
applyRegex(
operatorRegex,
keywordColor
)

/*
* Todo identificador restante queda
* morado al estilo de la referencia:
*
* int
* main
* cout
* nombre
* string
* etc.
*/
applyRegex(
identifierRegex,
identifierColor
)
}
}
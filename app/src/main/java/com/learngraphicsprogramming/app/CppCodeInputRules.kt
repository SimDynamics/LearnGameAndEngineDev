package com.learngraphicsprogramming.app

import android.text.Editable
import android.text.TextWatcher
import android.widget.EditText

/**
 * Reglas originales de escritura de C++ de LessonActivity, extraídas sin
 * cambiar sus casos especiales. Compartidas con el compilador independiente.
 * No es un TextWatcher simplificado: también mantiene el comportamiento
 * de borrado, sustituciones del IME, saltos sobre cierres e indentación.
 */
internal object CppCodeInputRules {
    fun install(
        input: EditText
    ) {

        // All native programming fields stay in the lesson in landscape. The IME's
        // extract editor would replace this surface and consume its usable viewport.
        input.imeOptions = input.imeOptions or
            android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI or
            android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN

        /*
         * Desactiva autocorrección, sugerencias y
         * espaciado inteligente del teclado.
         *
         * Esto es especialmente importante para
         * símbolos de programación.
         */
        val isMultiline =
            input.maxLines != 1

        input.inputType =
            android.text.InputType.TYPE_CLASS_TEXT or
                    android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
                    android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or
                    if (isMultiline) {
                        android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    } else {
                        0
                    }

        var internalChange =
            false

        var changeStart =
            0

        var removedCount =
            0

        var addedCount =
            0

        var previousText =
            ""

        /*
         * Algunos teclados insertan automáticamente
         * un espacio justo después de escribir
         * determinados signos.
         *
         * Esta protección vive solamente durante
         * el mismo ciclo de entrada del teclado.
         */
        var suppressAutoSpaceAfterSkip =
            false

        var suppressAutoSpacePosition =
            -1

        input.addTextChangedListener(
            object : TextWatcher {

                override fun beforeTextChanged(
                    text: CharSequence?,
                    start: Int,
                    count: Int,
                    after: Int
                ) {

                    if (internalChange) {
                        return
                    }

                    previousText =
                        text?.toString()
                            .orEmpty()

                    changeStart =
                        start

                    removedCount =
                        count

                    addedCount =
                        after
                }

                override fun onTextChanged(
                    text: CharSequence?,
                    start: Int,
                    before: Int,
                    count: Int
                ) {
                }

                override fun afterTextChanged(
                    editable: Editable?
                ) {

                    if (
                        internalChange ||
                        editable == null
                    ) {
                        return
                    }

                    /*
 * El teclado puede hacer esto:
 *
 * 1. escribir ")"
 * 2. nosotros saltamos sobre ")"
 * 3. el teclado inserta " " automáticamente
 *
 * Si ese espacio pertenece todavía al mismo
 * ciclo de entrada, lo eliminamos.
 */
                    if (
                        suppressAutoSpaceAfterSkip &&
                        removedCount == 0 &&
                        addedCount > 0 &&
                        changeStart ==
                        suppressAutoSpacePosition &&
                        changeStart + addedCount <=
                        editable.length
                    ) {

                        val insertedText =
                            editable.subSequence(
                                changeStart,
                                changeStart + addedCount
                            )
                                .toString()

                        val onlyWhitespace =
                            insertedText.all {
                                it == ' ' ||
                                        it == '\t'
                            }

                        if (onlyWhitespace) {

                            internalChange =
                                true

                            editable.delete(
                                changeStart,
                                changeStart + addedCount
                            )

                            input.setSelection(
                                changeStart.coerceAtMost(
                                    editable.length
                                )
                            )

                            internalChange =
                                false

                            suppressAutoSpaceAfterSkip =
                                false

                            suppressAutoSpacePosition =
                                -1

                            return
                        }
                    }

                    /*
                     * BORRADO DEL PAR VACÍO
                     *
                     * (|)
                     *
                     * borrar "(" elimina también ")".
                     */
                    if (
                        removedCount == 1 &&
                        addedCount == 0 &&
                        changeStart in previousText.indices
                    ) {

                        val deletedCharacter =
                            previousText[
                                changeStart
                            ]

                        val expectedClosing =
                            when (
                                deletedCharacter
                            ) {

                                '(' -> ')'
                                '{' -> '}'
                                '[' -> ']'
                                '"' -> '"'
                                '\'' -> '\''

                                else -> null
                            }

                        if (
                            expectedClosing != null &&
                            changeStart <
                            editable.length &&
                            editable[
                                changeStart
                            ] == expectedClosing
                        ) {

                            internalChange =
                                true

                            editable.delete(
                                changeStart,
                                changeStart + 1
                            )

                            input.setSelection(
                                changeStart.coerceAtMost(
                                    editable.length
                                )
                            )

                            internalChange =
                                false
                        }

                        return
                    }

                    /*
 * Algunos teclados Android pueden insertar
 * un cierre junto con un espacio:
 *
 * ") "
 *
 * Si delante ya existe ese mismo cierre,
 * eliminamos TODO lo que acaba de insertar
 * el teclado y simplemente avanzamos sobre
 * el cierre existente.
 */
                    if (
                        removedCount == 0 &&
                        addedCount >= 1 &&
                        changeStart >= 0 &&
                        changeStart + addedCount <= editable.length
                    ) {

                        val insertedText =
                            editable.subSequence(
                                changeStart,
                                changeStart + addedCount
                            )
                                .toString()

                        val firstInsertedCharacter =
                            insertedText
                                .firstOrNull()

                        val isClosingCharacter =
                            firstInsertedCharacter == ')' ||
                                    firstInsertedCharacter == '}' ||
                                    firstInsertedCharacter == ']' ||
                                    firstInsertedCharacter == '"' ||
                                    firstInsertedCharacter == '\''

                        val remainingInsertedTextIsWhitespace =
                            insertedText
                                .drop(1)
                                .all {
                                    it.isWhitespace()
                                }

                        val existingClosingPosition =
                            changeStart +
                                    addedCount

                        if (
                            isClosingCharacter &&
                            remainingInsertedTextIsWhitespace &&
                            existingClosingPosition <
                            editable.length &&
                            editable[
                                existingClosingPosition
                            ] == firstInsertedCharacter
                        ) {

                            internalChange =
                                true

                            android.view.inputmethod
                                .BaseInputConnection
                                .removeComposingSpans(
                                    editable
                                )

                            /*
                             * Eliminamos tanto el cierre recién
                             * escrito como cualquier espacio
                             * añadido por el teclado.
                             */
                            editable.delete(
                                changeStart,
                                changeStart + addedCount
                            )

                            /*
                             * El cierre original ahora vuelve
                             * a estar en changeStart.
                             */
                            val cursorAfterClosing =
                                (
                                        changeStart + 1
                                        ).coerceAtMost(
                                        editable.length
                                    )

                            input.setSelection(
                                cursorAfterClosing
                            )

                            /*
                             * Si el teclado solamente introdujo
                             * el cierre, existe la posibilidad de
                             * que inmediatamente después intente
                             * agregar un espacio automático.
                             *
                             * Lo bloqueamos únicamente durante
                             * este mismo ciclo del Looper.
                             *
                             * Un espacio que el usuario pulse
                             * posteriormente sí será permitido.
                             */
                            if (
                                addedCount == 1
                            ) {

                                suppressAutoSpaceAfterSkip =
                                    true

                                suppressAutoSpacePosition =
                                    cursorAfterClosing

                                input.post {

                                    suppressAutoSpaceAfterSkip =
                                        false

                                    suppressAutoSpacePosition =
                                        -1
                                }

                            } else {

                                /*
                                 * Si ") " llegó completo en la misma
                                 * modificación, ya fue eliminado arriba.
                                 */
                                suppressAutoSpaceAfterSkip =
                                    false

                                suppressAutoSpacePosition =
                                    -1
                            }

                            internalChange =
                                false

                            return
                        }
                    }

                    /*
                     * Desde aquí, el resto de la lógica
                     * corresponde a una sola tecla escrita.
                     */
                    if (
                        removedCount != 0 ||
                        addedCount != 1 ||
                        changeStart !in editable.indices
                    ) {

                        return
                    }

                    val typedCharacter =
                        editable[
                            changeStart
                        ]

                    /*
                     * ENTER:
                     *
                     * conserva la sangría actual.
                     *
                     * Si la línea termina en "{",
                     * agrega cuatro espacios.
                     */
                    if (
                        typedCharacter == '\n' &&
                        isMultiline
                    ) {

                        val previousLineStart =
                            previousText
                                .lastIndexOf(
                                    '\n',
                                    (
                                            changeStart - 1
                                            ).coerceAtLeast(
                                            0
                                        )
                                ) + 1

                        val previousLine =
                            previousText.substring(
                                previousLineStart,
                                changeStart.coerceAtMost(
                                    previousText.length
                                )
                            )

                        val baseIndent =
                            previousLine
                                .takeWhile {
                                    it == ' ' ||
                                            it == '\t'
                                }

                        val trimmedPreviousLine =
                            previousLine
                                .trimEnd()

                        val opensBlock =
                            trimmedPreviousLine
                                .endsWith(
                                    "{"
                                )

                        val childIndent =
                            if (opensBlock) {

                                baseIndent +
                                        "    "

                            } else {

                                baseIndent
                            }

                        /*
                         * Caso especial:
                         *
                         * {
                         *     |
                         * }
                         *
                         * Si Enter se pulsa entre un par
                         * {}, dejamos automáticamente la
                         * llave de cierre en su propia línea.
                         */
                        val closingBraceImmediatelyAfter =
                            changeStart + 1 <
                                    editable.length &&
                                    editable[
                                        changeStart + 1
                                    ] == '}'

                        internalChange =
                            true

                        if (
                            opensBlock &&
                            closingBraceImmediatelyAfter
                        ) {

                            editable.insert(
                                changeStart + 1,
                                childIndent +
                                        "\n" +
                                        baseIndent
                            )

                            val newCursorPosition =
                                changeStart +
                                        1 +
                                        childIndent.length

                            input.setSelection(
                                newCursorPosition
                            )

                            /*
                             * SwiftKey/Android puede reajustar la selección
                             * durante el mismo ciclo del IME.
                             *
                             * La reafirmamos después de que termine
                             * esa actualización para que el cursor quede
                             * realmente en la nueva línea.
                             */
                            input.post {

                                val safePosition =
                                    newCursorPosition.coerceIn(
                                        0,
                                        input.text?.length ?: 0
                                    )

                                input.setSelection(
                                    safePosition
                                )

                                input.bringPointIntoView(
                                    safePosition
                                )
                            }

                        } else {

                            editable.insert(
                                changeStart + 1,
                                childIndent
                            )

                            val newCursorPosition =
                                changeStart +
                                        1 +
                                        childIndent.length

                            input.setSelection(
                                newCursorPosition
                            )

                            input.post {

                                val safePosition =
                                    newCursorPosition.coerceIn(
                                        0,
                                        input.text?.length ?: 0
                                    )

                                input.setSelection(
                                    safePosition
                                )

                                input.bringPointIntoView(
                                    safePosition
                                )
                            }
                        }

                        internalChange =
                            false

                        return
                    }

                    /*
                     * Una comilla escapada:
                     *
                     * \"
                     *
                     * no genera un cierre automático.
                     */
                    if (
                        (
                                typedCharacter == '"' ||
                                        typedCharacter == '\''
                                ) &&
                        changeStart > 0 &&
                        editable[
                            changeStart - 1
                        ] == '\\'
                    ) {

                        return
                    }

                    /*
                     * Si escribimos } al principio lógico
                     * de una línea indentada, reducimos
                     * automáticamente un nivel.
                     *
                     * Ejemplo:
                     *
                     *     |
                     *
                     * escribir "}" produce:
                     *
                     * }
                     */
                    if (
                        typedCharacter == '}' &&
                        isMultiline
                    ) {

                        val lineStart =
                            editable
                                .lastIndexOf(
                                    '\n',
                                    (
                                            changeStart - 1
                                            ).coerceAtLeast(
                                            0
                                        )
                                ) + 1

                        val textBeforeBrace =
                            editable.substring(
                                lineStart,
                                changeStart
                            )

                        val onlyWhitespaceBefore =
                            textBeforeBrace.all {
                                it == ' ' ||
                                        it == '\t'
                            }

                        if (
                            onlyWhitespaceBefore &&
                            textBeforeBrace.isNotEmpty()
                        ) {

                            var charactersToRemove =
                                0

                            var position =
                                changeStart - 1

                            while (
                                position >= lineStart &&
                                charactersToRemove < 4 &&
                                editable[
                                    position
                                ] == ' '
                            ) {

                                charactersToRemove++
                                position--
                            }

                            if (
                                charactersToRemove > 0
                            ) {

                                internalChange =
                                    true

                                editable.delete(
                                    changeStart -
                                            charactersToRemove,
                                    changeStart
                                )

                                val newBracePosition =
                                    changeStart -
                                            charactersToRemove

                                input.setSelection(
                                    (
                                            newBracePosition + 1
                                            ).coerceAtMost(
                                            editable.length
                                        )
                                )

                                internalChange =
                                    false

                                return
                            }
                        }
                    }

                    /*
                     * AUTOCIERRE
                     */
                    val closingCharacter =
                        when (
                            typedCharacter
                        ) {

                            '(' -> ')'
                            '{' -> '}'
                            '[' -> ']'
                            '"' -> '"'
                            '\'' -> '\''

                            else -> null
                        }

                    if (
                        closingCharacter ==
                        null
                    ) {

                        return
                    }

                    internalChange =
                        true

                    editable.insert(
                        changeStart + 1,
                        closingCharacter
                            .toString()
                    )

                    input.setSelection(
                        changeStart + 1
                    )

                    internalChange =
                        false
                }
            }
        )
    }
}

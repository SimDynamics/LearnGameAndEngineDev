package com.learngraphicsprogramming.app.math

import android.content.Context
import androidx.annotation.StringRes
import com.learngraphicsprogramming.app.R
import kotlin.contracts.ExperimentalContracts
import kotlin.contracts.contract

/** A locale-independent description. Only the UI resolves it using its current resources. */
data class MathUiMessage(@StringRes val resourceId: Int, val arguments: List<Any> = emptyList()) {
    fun resolve(context: Context): String = context.getString(resourceId, *arguments.map {
        if (it is MathUiMessage) it.resolve(context) else it
    }.toTypedArray())

    // Stable diagnostic information for pure consumers, tests and logs; never displayed as UI.
    val diagnostic: String get() =
        // Existing pure consumers recognize this unsupported-expression diagnostic.
        (if (resourceId == R.string.math_ui_unsupported_expression) "no soportada:" else "resource:") +
            resourceId + arguments.joinToString(prefix = "(", postfix = ")")
}

class MathUiException(val uiMessage: MathUiMessage) : IllegalArgumentException(uiMessage.diagnostic)

@OptIn(ExperimentalContracts::class)
inline fun requireMath(value: Boolean, lazyMessage: () -> Any = { MathUiMessage(R.string.math_invalid_configuration) }) {
    contract { returns() implies value }
    if (!value) {
        val message = lazyMessage()
        if (message is MathUiMessage) throw MathUiException(message)
        // Schema diagnostics are useful to authors and tests, but are never rendered.
        throw IllegalArgumentException(message.toString())
    }
}

fun mathError(message: MathUiMessage): Nothing = throw MathUiException(message)

/** Technical/parser exceptions receive a controlled translation; engine diagnostics remain in logs. */
fun Context.mathErrorMessage(error: Throwable): String =
    (error as? MathUiException)?.uiMessage?.resolve(this) ?: getString(R.string.math_invalid_configuration)

package com.learngraphicsprogramming.app.calculator

import com.learngraphicsprogramming.app.math.MathKeyboardAction

/** Calculator keys map to the existing structured editor's semantic actions. */
sealed interface CalculatorEditorAction {
    data class Edit(val action: MathKeyboardAction) : CalculatorEditorAction
    data object Clear : CalculatorEditorAction
    data object Evaluate : CalculatorEditorAction
}

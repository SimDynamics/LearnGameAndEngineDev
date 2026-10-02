package com.learngraphicsprogramming.app.math

import android.view.View

/** An interaction is independent of its subject, validator and lesson navigation. */
interface MathResponseControl {
    val view: View
    fun answer(): MathAnswer
    fun restore(answer: MathAnswer)
    fun setEditable(enabled: Boolean)
}

/** Canvas labels participate in the same lesson text-size setting as Android widgets. */
interface MathTextScaleAware {
    fun scaleMathText(factor: Float)
}

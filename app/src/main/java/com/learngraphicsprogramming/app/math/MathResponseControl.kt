package com.learngraphicsprogramming.app.math

import android.view.View
import org.json.JSONObject

/** An interaction is independent of its subject, validator and lesson navigation. */
interface MathResponseControl {
    val view: View
    fun answer(): MathAnswer
    fun restore(answer: MathAnswer)
    fun setEditable(enabled: Boolean)
    /** Editors on a visual may contain incomplete text while its last valid model is unchanged. */
    fun hasDraftChanges(): Boolean = false
    fun draftError(): String? = null
    fun saveDraft(): JSONObject? = null
    fun restoreDraft(state: JSONObject) = Unit
}

/** Canvas labels participate in the same lesson text-size setting as Android widgets. */
interface MathTextScaleAware {
    fun scaleMathText(factor: Float)
}

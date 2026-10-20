package com.learngraphicsprogramming.app

import android.graphics.Color
import android.text.Editable
import android.text.TextWatcher
import android.widget.EditText
import android.widget.LinearLayout
import android.view.View
import com.learngraphicsprogramming.app.narration.ReadingTextView

/** An optional verdict over a fresh successful run. Never evaluates or persists an exercise. */
internal class CppOutputCheckController(
    private val activity: LessonActivity,
    specification: CppOutputCheck.Specification,
    parent: LinearLayout,
    private val source: EditText,
    private val input: EditText,
    private val snapshot: () -> CppOutputCheck.Snapshot?,
    private val onShown: (ReadingTextView) -> Unit,
    private val onCleared: (ReadingTextView) -> Unit,
    private val inputValue: () -> String = { input.text.toString() }
) {
    private val session = CppOutputCheck.Session(specification)
    val result = ReadingTextView(activity).apply {
        tag = "cpp_output_check_result"
        textSize = 18f
        visibility = View.GONE
        setPadding(0, dp(8), 0, dp(12))
    }
    init {
        parent.addView(result, LinearLayout.LayoutParams(-1, -2))
        result.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(view: View) = Unit
            override fun onViewDetachedFromWindow(view: View) {
                // Lesson pages cache their Views. Returning to one must not revive
                // a verdict or a worker ticket from the previous visit.
                invalidate()
            }
        })
        val watcher = object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) { draftChanged() }
        }
        source.addTextChangedListener(watcher)
        input.addTextChangedListener(watcher)
        draftChanged()
    }

    fun draftChanged() {
        if (session.observe(snapshot(), inputValue())) clearPresentation()
    }

    fun invalidate() {
        session.invalidate()
        clearPresentation()
    }

    fun begin(): CppOutputCheck.Session.Ticket? {
        val ticket = session.begin(snapshot(), inputValue())
        clearPresentation()
        return ticket
    }

    fun complete(ticket: CppOutputCheck.Session.Ticket?, stdout: String?) {
        if (!source.isAttachedToWindow || !result.isAttachedToWindow) {
            invalidate()
            return
        }
        val current = snapshot()
        val currentInput = inputValue()
        if (session.observe(current, currentInput)) clearPresentation()
        // Only this successful completion can publish a verdict. Never reuse the
        // console, a restored CMake result, or another run's prior session.check().
        if (!session.complete(ticket, current, currentInput, stdout)) return
        val verdict = session.check() ?: return
        result.setTextColor(if (verdict.correct) Color.rgb(180, 220, 190) else Color.rgb(240, 150, 150))
        result.text = buildString {
            append(activity.getString(if (verdict.correct) R.string.cpp_output_check_correct else R.string.cpp_output_check_incorrect))
            verdict.explanation?.let { append('\n'); append(it) }
        }
        result.visibility = View.VISIBLE
        onShown(result)
    }

    private fun clearPresentation() {
        onCleared(result)
        result.text = ""
        result.visibility = View.GONE
    }

    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density + .5f).toInt()
}

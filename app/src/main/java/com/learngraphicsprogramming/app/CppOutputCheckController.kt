package com.learngraphicsprogramming.app

import android.graphics.Color
import android.text.Editable
import android.text.TextWatcher
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.view.View
import com.google.android.material.button.MaterialButton
import com.learngraphicsprogramming.app.narration.ReadingTextView

/** One optional check over the actual last run. Does not evaluate or persist an exercise. */
internal class CppOutputCheckController(
    private val activity: LessonActivity,
    specification: CppOutputCheck.Specification,
    parent: LinearLayout,
    private val source: EditText,
    private val input: EditText,
    private val snapshot: () -> CppOutputCheck.Snapshot?,
    private val onShown: (ReadingTextView) -> Unit,
    private val onCleared: (ReadingTextView) -> Unit
) {
    private val session = CppOutputCheck.Session(specification)
    val result = ReadingTextView(activity).apply {
        tag = "cpp_output_check_result"
        textSize = 18f
        visibility = View.GONE
        setPadding(0, dp(8), 0, dp(12))
    }
    val button: Button = MaterialButton(activity).apply {
        tag = "cpp_check_output"
        text = activity.getString(R.string.cpp_check_output)
        isAllCaps = false
        textSize = 18f
        isEnabled = false
    }

    init {
        parent.addView(button, LinearLayout.LayoutParams(-1, -2))
        parent.addView(result, LinearLayout.LayoutParams(-1, -2))
        val watcher = object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) { draftChanged() }
        }
        source.addTextChangedListener(watcher)
        input.addTextChangedListener(watcher)
        draftChanged()
        button.setOnClickListener {
            draftChanged()
            val verdict = session.check() ?: return@setOnClickListener
            result.setTextColor(if (verdict.correct) Color.rgb(180, 220, 190) else Color.rgb(240, 150, 150))
            result.text = buildString {
                append(activity.getString(if (verdict.correct) R.string.cpp_output_check_correct else R.string.cpp_output_check_incorrect))
                verdict.explanation?.let { append('\n'); append(it) }
            }
            result.visibility = View.VISIBLE
            onShown(result)
        }
    }

    fun draftChanged() {
        if (session.observe(snapshot(), input.text.toString())) clearPresentation()
        refresh()
    }

    fun invalidate() {
        session.invalidate()
        clearPresentation()
        refresh()
    }

    fun begin(): CppOutputCheck.Session.Ticket? {
        val ticket = session.begin(snapshot(), input.text.toString())
        clearPresentation()
        refresh()
        return ticket
    }

    fun complete(ticket: CppOutputCheck.Session.Ticket?, stdout: String?) {
        if (session.observe(snapshot(), input.text.toString())) clearPresentation()
        session.complete(ticket, snapshot(), input.text.toString(), stdout)
        refresh()
    }

    private fun clearPresentation() {
        onCleared(result)
        result.text = ""
        result.visibility = View.GONE
    }

    private fun refresh() {
        button.isEnabled = session.canCheck
        button.contentDescription = activity.getString(if (session.canCheck) R.string.cpp_check_output
            else R.string.cpp_output_check_run_required)
    }

    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density + .5f).toInt()
}

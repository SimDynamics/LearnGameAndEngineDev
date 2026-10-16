package com.learngraphicsprogramming.app

import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import com.learngraphicsprogramming.app.runtime.CppProjectHistory
import java.text.DateFormat
import java.util.Date
import java.util.concurrent.Executors

/** An explicit local revision is separate from the editor's continuously saved draft. */
internal class CppProjectHistoryDialog(
    private val context: Context,
    private val history: CppProjectHistory,
    private val draft: () -> Draft,
    private val restore: (CppProjectHistory.Revision, String) -> Unit
) {
    data class Draft(val files: Map<String, String>, val sources: List<String>,
                     val indentation: Map<String, String>)

    private val worker = Executors.newSingleThreadExecutor()
    private val children = mutableListOf<AlertDialog>()
    private val body = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
    private val root = AlertDialog.Builder(context)
        .setTitle(R.string.cpp_history_title)
        .setView(ScrollView(context).apply { addView(body) })
        .setPositiveButton(R.string.cpp_history_record, null)
        .setNegativeButton(R.string.cpp_history_close, null)
        .create()

    fun show() {
        root.setOnDismissListener {
            children.toList().forEach { it.dismiss() }
            worker.shutdown()
        }
        root.setOnShowListener {
            root.window?.decorView?.tag = "cpp_history_dialog"
            root.getButton(AlertDialog.BUTTON_POSITIVE).apply {
                tag = "cpp_history_register"
                setOnClickListener { record() }
            }
            refresh()
        }
        root.show()
    }

    fun dismiss() = root.dismiss()

    private fun active(): Boolean = root.isShowing && (context !is Activity || !context.isFinishing && !context.isDestroyed)

    private fun <T> work(operation: () -> T, result: (T) -> Unit) {
        if (!active()) return
        root.getButton(AlertDialog.BUTTON_POSITIVE).isEnabled = false
        worker.execute {
            val value = runCatching(operation)
            body.post {
                if (!active()) return@post
                root.getButton(AlertDialog.BUTTON_POSITIVE).isEnabled = true
                value.fold(result) {
                    children.forEach { child -> child.getButton(AlertDialog.BUTTON_POSITIVE)?.isEnabled = true }
                    error()
                }
            }
        }
    }

    private fun error() {
        if (active()) child(AlertDialog.Builder(context)
            .setTitle(R.string.cpp_history_title)
            .setMessage(R.string.cpp_history_error)
            .setPositiveButton(android.R.string.ok, null).create())
    }

    private fun refresh() = work(history::summaries) { revisions ->
        body.removeAllViews()
        body.addView(TextView(context).apply {
            text = context.getString(if (revisions.isEmpty()) R.string.cpp_history_empty else R.string.cpp_history_explanation)
            setPadding(dp(20), dp(12), dp(20), dp(12))
        })
        revisions.forEach { revision ->
            body.addView(Button(context).apply {
                tag = "cpp_history_revision_${revision.id}"
                text = context.getString(R.string.cpp_history_revision_row, revision.label, revision.id.take(12),
                    DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(revision.createdAt)))
                isAllCaps = false
                setOnClickListener { openRevision(revision.id) }
            })
        }
    }

    private fun record() {
        val label = EditText(context).apply {
            tag = "cpp_history_label"
            hint = context.getString(R.string.cpp_history_label_hint)
            maxLines = 2
        }
        val dialog = AlertDialog.Builder(context).setTitle(R.string.cpp_history_record)
            .setView(label).setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.cpp_history_record, null).create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val name = label.text.toString().trim()
                if (name.isBlank() || name.length > 160) {
                    label.error = context.getString(R.string.cpp_history_label_required)
                    return@setOnClickListener
                }
                val current = draft()
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).isEnabled = false
                work({ history.register(current.files, current.sources, name, current.indentation) }) {
                    dialog.dismiss()
                    refresh()
                }
            }
        }
        child(dialog)
    }

    private fun openRevision(id: String) {
        val current = draft()
        work({ history.load(id).let { it to history.statuses(current.files, it) } }) { (revision, statuses) ->
            val files = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
            files.addView(TextView(context).apply {
                text = context.getString(R.string.cpp_history_comparison_heading, revision.label, revision.id.take(12))
                setPadding(dp(20), dp(12), dp(20), dp(12))
            })
            statuses.forEach { status ->
                files.addView(Button(context).apply {
                    tag = "cpp_history_file_${status.path}"
                    text = context.getString(R.string.cpp_history_file_row, status.path, changeLabel(status.change))
                    isAllCaps = false
                    setOnClickListener { compare(revision, status.path) }
                })
            }
            child(AlertDialog.Builder(context).setTitle(R.string.cpp_history_files)
                .setView(ScrollView(context).apply { addView(files) })
                .setNegativeButton(R.string.cpp_history_close, null).create())
        }
    }

    private fun compare(revision: CppProjectHistory.Revision, path: String) {
        val current = draft()
        work({ history.diff(path, current.files, revision) }) { comparison ->
            val source = TextView(context).apply {
                tag = "cpp_history_diff"
                typeface = Typeface.MONOSPACE
                textSize = 14f
                setTextColor(Color.rgb(225, 225, 235))
                setBackgroundColor(Color.rgb(31, 35, 48))
                setPadding(dp(14), dp(12), dp(14), dp(12))
                setTextIsSelectable(true)
                text = buildString {
                    if (comparison.change == CppProjectHistory.Change.ADDED) {
                        append(context.getString(R.string.cpp_history_absent_in_revision)).append('\n')
                    }
                    append(context.getString(R.string.cpp_history_diff_legend)).append('\n')
                    comparison.lines.forEach { line ->
                        append(when (line.type) {
                            CppProjectHistory.LineType.CONTEXT -> "  "
                            CppProjectHistory.LineType.ADDED -> "+ "
                            CppProjectHistory.LineType.REMOVED -> "− "
                        })
                        append(line.text)
                        if (line.type != CppProjectHistory.LineType.CONTEXT && line.lineEnding in listOf("\r", "\r\n"))
                            append(if (line.lineEnding == "\r\n") " ⏎ CRLF" else " ⏎ CR")
                        append('\n')
                        if (line.lineEnding.isEmpty()) append(context.getString(R.string.cpp_history_no_final_newline)).append('\n')
                    }
                }
            }
            val dialog = AlertDialog.Builder(context).setTitle(path)
                .setView(ScrollView(context).apply { addView(HorizontalScrollView(context).apply { addView(source) }) })
                .setNegativeButton(R.string.cpp_history_close, null)
                .setPositiveButton(if (comparison.change == CppProjectHistory.Change.ADDED)
                    R.string.cpp_history_remove_file else R.string.cpp_history_restore_file, null).create()
            dialog.setOnShowListener {
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).apply {
                    tag = "cpp_history_restore"
                    isEnabled = comparison.change != CppProjectHistory.Change.UNCHANGED
                    setOnClickListener {
                        // Revalidate against the latest draft; only this path is recovered.
                        runCatching { restore(revision, path) }.fold({
                            children.toList().forEach { it.dismiss() }
                            refresh()
                        }, { error() })
                    }
                }
            }
            child(dialog)
        }
    }

    private fun changeLabel(change: CppProjectHistory.Change): String = context.getString(when (change) {
        CppProjectHistory.Change.UNCHANGED -> R.string.cpp_history_unchanged
        CppProjectHistory.Change.ADDED -> R.string.cpp_history_added
        CppProjectHistory.Change.MODIFIED -> R.string.cpp_history_modified
        CppProjectHistory.Change.REMOVED -> R.string.cpp_history_removed
    })

    private fun child(dialog: AlertDialog) {
        children += dialog
        dialog.setOnDismissListener { children.remove(dialog) }
        dialog.show()
    }

    private fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()
}

package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.SharedPreferences
import android.os.Handler
import android.os.Looper
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.math.MathPuzzleState
import com.learngraphicsprogramming.app.math.MathPuzzleTypes
import org.json.JSONObject

/** Observe existing evaluation results; reading audio never evaluates or completes an exercise. */
internal class LessonNarrationExerciseState(context: Context,
    private val changed: (String, Set<Int>) -> Unit) : AutoCloseable {
    private val context = context.applicationContext
    private val preferences = this.context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE)
    private val main = Handler(Looper.getMainLooper())
    private var pageIdentity = ""
    private var lessonId = ""
    private var page = 0
    private var blocks: List<Pair<Int,JSONObject>> = emptyList()
    private var last: Set<Int>? = null
    private var closed = false
    private val update = Runnable {
        if (!closed) {
            val correct = current()
            if (correct != last) { last = correct; changed(pageIdentity,correct) }
        }
    }
    private val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        // A visible page may now own exercises stored in several original page slots.
        if (!closed && (key == null || key.startsWith("exercise_${lessonId}_"))) {
            main.removeCallbacks(update)
            main.post(update)
        }
    }
    init { preferences.registerOnSharedPreferenceChangeListener(listener) }
    fun watch(identity: String, id: String, lesson: JSONObject, pageIndex: Int): Set<Int> {
        if (identity != pageIdentity) {
            pageIdentity = identity; lessonId = id; page = pageIndex; last = null
        }
        val array = lesson.optJSONArray("pages")?.optJSONObject(pageIndex)?.optJSONArray("blocks")
        blocks = if (array == null) emptyList() else (0 until array.length()).mapNotNull { index ->
            array.optJSONObject(index)?.takeIf { LessonNarrationScript.isExercise(it.optString("type")) }
                ?.let { index to it }
        }
        return current().also { last = it }
    }
    private fun current(): Set<Int> = blocks.mapNotNull { (index, block) ->
        val state = ProgressRepository.getExerciseState(context,lessonId,page,index)
        val correct = if (MathPuzzleTypes.isExercise(block.optString("type")))
            MathPuzzleState.isCurrentCorrect(block,state)
        else state?.optBoolean("correct",false) == true
        index.takeIf { correct }
    }.toSet()
    override fun close() {
        if (closed) return
        closed = true
        preferences.unregisterOnSharedPreferenceChangeListener(listener)
        main.removeCallbacksAndMessages(null)
        blocks = emptyList()
    }
}

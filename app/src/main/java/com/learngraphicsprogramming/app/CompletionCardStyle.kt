package com.learngraphicsprogramming.app

import android.graphics.Color
import android.view.View
import android.widget.TextView

/**
 * Completion colors shared by course, unit and lesson selectors.
 * Always restore the unfinished colors when RecyclerView reuses a row.
 */
object CompletionCardStyle {
    private val completedBackground = Color.rgb(29, 78, 52)
    private val completedNumber = Color.rgb(105, 220, 145)
    private val defaultBackground = Color.rgb(32, 33, 38)

    fun apply(
        container: View,
        number: TextView,
        progress: ProgressSnapshot,
        accentTextColor: Int
    ) {
        val completed = progress.total > 0 && progress.completed == progress.total
        container.setBackgroundColor(
            if (completed) completedBackground else defaultBackground
        )
        number.setTextColor(
            if (completed) completedNumber else accentTextColor
        )
    }
}

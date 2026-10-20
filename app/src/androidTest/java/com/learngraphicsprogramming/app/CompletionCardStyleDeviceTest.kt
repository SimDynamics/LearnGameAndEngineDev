package com.learngraphicsprogramming.app

import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.widget.LinearLayout
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** The same completed-row styling is used by course, unit and lesson pickers. */
@RunWith(AndroidJUnit4::class)
class CompletionCardStyleDeviceTest {

    @Test
    fun onlyNonEmptyAndFullyCompletedProgressUsesTheExistingGreen() {
        val app = InstrumentationRegistry.getInstrumentation()
        app.runOnMainSync {
            val card = LinearLayout(app.targetContext)
            val label = TextView(app.targetContext)
            val accent = Color.rgb(85, 230, 242)

            for ((progress, finished) in listOf(
                ProgressSnapshot(0, 0) to false,
                ProgressSnapshot(0, 3) to false,
                ProgressSnapshot(2, 3) to false,
                ProgressSnapshot(3, 3) to true
            )) {
                CompletionCardStyle.apply(card, label, progress, accent)
                assertEquals(
                    if (finished) Color.rgb(29, 78, 52) else Color.rgb(32, 33, 38),
                    (card.background as ColorDrawable).color
                )
                assertEquals(
                    if (finished) Color.rgb(105, 220, 145) else accent,
                    label.currentTextColor
                )
            }
        }
    }

    @Test
    fun rebindingARecycledCompletedRowRestoresTheUnfinishedRouteAccent() {
        val app = InstrumentationRegistry.getInstrumentation()
        app.runOnMainSync {
            val card = LinearLayout(app.targetContext)
            val label = TextView(app.targetContext)
            CompletionCardStyle.apply(card, label, ProgressSnapshot(5, 5), Color.MAGENTA)
            CompletionCardStyle.apply(card, label, ProgressSnapshot(4, 5), Color.CYAN)

            assertEquals(Color.rgb(32, 33, 38), (card.background as ColorDrawable).color)
            assertEquals(Color.CYAN, label.currentTextColor)
        }
    }
}

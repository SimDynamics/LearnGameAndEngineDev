package com.learngraphicsprogramming.app.narration

import android.app.AlertDialog
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.graphics.Color
import android.view.ContextThemeWrapper
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.OptionsActivity
import com.learngraphicsprogramming.app.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Verifies the rendered Android dialog buttons, not just their resource strings. */
@RunWith(AndroidJUnit4::class)
class NarrationDialogActionColorsDeviceTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test fun voiceDialogActionsUseNeutralTextInBothLightAndDarkDialogs() {
        ActivityScenario.launch<OptionsActivity>(Intent(context, OptionsActivity::class.java)).use { scenario ->
            scenario.onActivity { activity ->
                for (theme in listOf(
                    android.R.style.Theme_Material_Light_Dialog_Alert,
                    android.R.style.Theme_Material_Dialog_Alert
                )) {
                    val themed = ContextThemeWrapper(activity, theme)
                    var downloaded = false
                    val dialog = AlertDialog.Builder(themed)
                        .setTitle(activity.getString(R.string.narration_voice_name_id, "Heart", "af_heart"))
                        .setMessage("Not installed. Uses the shared model.")
                        .setNeutralButton(R.string.narration_remove, null)
                        .setNegativeButton(R.string.narration_close, null)
                        .setPositiveButton(R.string.narration_download) { _, _ -> downloaded = true }
                        .showWithNeutralActionText()
                    try {
                        val attrs = dialog.context.obtainStyledAttributes(
                            intArrayOf(android.R.attr.textColorPrimary))
                        val expected = try {
                            requireNotNull(attrs.getColorStateList(0))
                        } finally {
                            attrs.recycle()
                        }
                        for (which in intArrayOf(
                            DialogInterface.BUTTON_NEUTRAL,
                            DialogInterface.BUTTON_NEGATIVE,
                            DialogInterface.BUTTON_POSITIVE
                        )) {
                            val button = requireNotNull(dialog.getButton(which))
                            val neutral = expected.getColorForState(
                                button.drawableState, expected.defaultColor)
                            assertEquals("Action buttons must use the dialog's own text color",
                                neutral, button.currentTextColor)
                            assertNotEquals("Cyan accent must not paint dialog actions",
                                Color.rgb(89, 240, 252), button.currentTextColor)
                            assertNotEquals("Yellow accent must not paint dialog actions",
                                Color.YELLOW, button.currentTextColor)
                        }
                        dialog.getButton(DialogInterface.BUTTON_NEUTRAL).isEnabled = false
                        val disabled = dialog.getButton(DialogInterface.BUTTON_NEUTRAL)
                        assertEquals("Disabled actions retain the theme's text state list",
                            expected.getColorForState(disabled.drawableState, expected.defaultColor),
                            disabled.currentTextColor)
                        assertTrue(dialog.isShowing)
                        dialog.getButton(DialogInterface.BUTTON_POSITIVE).performClick()
                        assertTrue("The original action must remain clickable", downloaded)
                    } finally {
                        dialog.dismiss()
                    }
                }
            }
        }
    }
}

package com.learngraphicsprogramming.app

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.util.AttributeSet
import android.widget.FrameLayout
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.widget.AppCompatImageButton

/** Shortcut to the existing GLOBAL app-language setting, not a translation service. */
class LessonLanguageButton @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = androidx.appcompat.R.attr.imageButtonStyle
) : AppCompatImageButton(context, attrs, defStyleAttr) {
    private var languageDialog: AlertDialog? = null

    init {
        setOnClickListener { showLanguagePicker() }
    }

    private fun showLanguagePicker() {
        if (!isAttachedToWindow || languageDialog?.isShowing == true) return
        var owner: Context = context
        while (owner is ContextWrapper && owner !is Activity) {
            val base = owner.baseContext
            if (base === owner) break
            owner = base
        }
        if (owner is Activity && (owner.isFinishing || owner.isDestroyed)) return

        // Use exactly the tags, translated labels and persistence used by global settings.
        val tags = listOf("") + UiLanguageSettings.supportedTags(context)
        val labels = listOf(context.getString(R.string.settings_language_auto)) +
            resources.getStringArray(R.array.ui_language_names).toList()
        val current = UiLanguageSettings.selectedTag(context)
        val selected = LessonLanguageSelection.indexOf(tags, current)
        val dialog = AlertDialog.Builder(context)
            .setTitle(R.string.lesson_header_language_title)
            .setSingleChoiceItems(labels.toTypedArray(), selected) { chooser, index ->
                chooser.dismiss()
                val tag = tags[index]
                if (tag != UiLanguageSettings.selectedTag(context)) {
                    // LocaleManager/AppCompat recreates the Activity as needed. Do not
                    // relaunch it, reset a lesson, or change the independent voice setting.
                    UiLanguageSettings.select(context, tag)
                }
            }
            .setNegativeButton(R.string.settings_cancel, null)
            .create()
        dialog.setOnDismissListener {
            if (languageDialog === dialog) languageDialog = null
        }
        languageDialog = dialog
        dialog.show()
    }

    override fun onDetachedFromWindow() {
        languageDialog?.dismiss()
        languageDialog = null
        super.onDetachedFromWindow()
    }
}

/** Keep the full 48dp touch height while the existing lives graphic is scaled to 80%. */
class LessonAttemptTouchTarget @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : FrameLayout(context, attrs) {
    override fun onFinishInflate() {
        super.onFinishInflate()
        setOnClickListener { findViewById<android.view.View>(R.id.attemptIndicator).performClick() }
        // Accessibility continues to use the original indicator, whose details are dynamic.
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }
}

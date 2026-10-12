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
    private val realign = Runnable { alignRemainingCounter() }

    override fun onFinishInflate() {
        super.onFinishInflate()
        setOnClickListener { findViewById<android.view.View>(R.id.attemptIndicator).performClick() }
        // Accessibility continues to use the original indicator, whose details are dynamic.
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO

        // A fixed-height TextView might not trigger another parent layout as its
        // content changes from "7h 55m" to "1m". Recheck its real glyph geometry.
        findViewById<android.widget.TextView>(R.id.textAttemptRemaining)
            ?.addTextChangedListener(object : android.text.TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
                override fun afterTextChanged(s: android.text.Editable?) {
                    removeCallbacks(realign)
                    post(realign)
                }
            })
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        removeCallbacks(realign)
        post(realign)
    }

    private fun glyphBottomOnScreen(view: android.widget.TextView): Float? {
        if (view.visibility != android.view.View.VISIBLE || view.text.isNullOrBlank() || view.height <= 0)
            return null
        val rectangle = android.graphics.Rect()
        if (!view.getGlobalVisibleRect(rectangle)) return null
        val painted = android.graphics.Rect()
        val text = view.text.toString()
        view.paint.getTextBounds(text, 0, text.length, painted)
        val scale = rectangle.height().toFloat() / view.height.toFloat()
        return rectangle.top + (view.baseline + painted.bottom) * scale
    }

    private fun alignRemainingCounter() {
        if (!isAttachedToWindow || width <= 0 || height <= 0) return
        val icon = findViewById<android.widget.ImageView>(R.id.imageAttemptRoute) ?: return
        val label = findViewById<android.widget.TextView>(R.id.textAttemptRemaining) ?: return
        if (label.text.isNullOrBlank() || label.width <= 0 || label.height <= 0) return
        val bounds = android.graphics.Rect()
        val iconBounds = android.graphics.Rect()
        if (!getGlobalVisibleRect(bounds) || !icon.getGlobalVisibleRect(iconBounds)) return

        val upper = listOfNotNull(
            glyphBottomOnScreen(findViewById(R.id.textAttemptCount)),
            glyphBottomOnScreen(findViewById(R.id.textAttemptPercent))
        ).maxOrNull() ?: iconBounds.top.toFloat()

        val actualText = label.text.toString()
        val glyph = android.graphics.Rect()
        label.paint.getTextBounds(actualText, 0, actualText.length, glyph)
        val position = AttemptCountdownPlacement.offsets(
            iconLeft = iconBounds.left.toFloat(),
            iconTop = iconBounds.top.toFloat(),
            iconRight = iconBounds.right.toFloat(),
            iconBottom = iconBounds.bottom.toFloat(),
            upperGlyphBottom = upper,
            labelLeft = bounds.left.toFloat() + label.left,
            labelTop = bounds.top.toFloat() + label.top,
            labelWidth = label.width.toFloat(),
            labelHeight = label.height.toFloat(),
            textWidth = label.paint.measureText(actualText),
            glyphTopInsideLabel = label.baseline + glyph.top.toFloat(),
            slotBottom = bounds.bottom.toFloat(),
            minimumGap = resources.displayMetrics.density // 1dp, only if upper text overlaps.
        )
        if (kotlin.math.abs(label.translationX - position.x) > 0.1f)
            label.translationX = position.x
        if (kotlin.math.abs(label.translationY - position.y) > 0.1f)
            label.translationY = position.y
    }

    override fun onDetachedFromWindow() {
        removeCallbacks(realign)
        super.onDetachedFromWindow()
    }
}

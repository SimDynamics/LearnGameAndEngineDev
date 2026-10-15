package com.learngraphicsprogramming.app.narration

import android.content.res.ColorStateList
import android.graphics.Color
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.ProgressBar
import android.widget.SeekBar
import android.widget.TextView
import java.util.IdentityHashMap

/** Presentation only. Keeps each control's normal state for a later re-enable. */
internal class NarrationControlAvailability {
    private data class NormalState(
        val enabled: Boolean,
        val alpha: Float,
        val textColors: ColorStateList?,
        val imageTint: ColorStateList?,
        val progressTint: ColorStateList?,
        val thumbTint: ColorStateList?
    )

    private val normal = IdentityHashMap<View, NormalState>()
    private val disabledColor = ColorStateList.valueOf(Color.rgb(170, 174, 183))

    fun apply(root: View, enabled: Boolean, except: View? = null) {
        if (root === except) return
        if (enabled) {
            normal.remove(root)?.let { previous ->
                root.isEnabled = previous.enabled
                root.alpha = previous.alpha
                previous.textColors?.let { (root as? TextView)?.setTextColor(it) }
                (root as? ImageView)?.imageTintList = previous.imageTint
                (root as? ProgressBar)?.progressTintList = previous.progressTint
                (root as? SeekBar)?.thumbTintList = previous.thumbTint
            }
        } else {
            normal.putIfAbsent(root, NormalState(root.isEnabled, root.alpha,
                (root as? TextView)?.textColors, (root as? ImageView)?.imageTintList,
                (root as? ProgressBar)?.progressTintList, (root as? SeekBar)?.thumbTintList))
            // A theme can be applied while disabled (for example onResume).
            // Remember its new normal colors instead of restoring an old theme.
            val previous = normal.getValue(root)
            normal[root] = previous.copy(
                textColors = (root as? TextView)?.textColors?.takeUnless { it == disabledColor }
                    ?: previous.textColors,
                imageTint = (root as? ImageView)?.imageTintList?.takeUnless { it == disabledColor }
                    ?: previous.imageTint,
                progressTint = (root as? ProgressBar)?.progressTintList?.takeUnless { it == disabledColor }
                    ?: previous.progressTint,
                thumbTint = (root as? SeekBar)?.thumbTintList?.takeUnless { it == disabledColor }
                    ?: previous.thumbTint
            )
            root.isEnabled = false
            // Fade leaves once; nested groups must not compound the attenuation.
            if (root !is ViewGroup) root.alpha = normal.getValue(root).alpha * 0.75f
            (root as? TextView)?.setTextColor(disabledColor)
            (root as? ImageView)?.imageTintList = disabledColor
            (root as? ProgressBar)?.progressTintList = disabledColor
            (root as? SeekBar)?.thumbTintList = disabledColor
        }
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) apply(root.getChildAt(index), enabled, except)
        }
    }
}

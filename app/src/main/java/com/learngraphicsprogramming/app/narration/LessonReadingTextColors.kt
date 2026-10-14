package com.learngraphicsprogramming.app.narration

import android.content.res.ColorStateList
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import java.util.IdentityHashMap
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

enum class ReadingBlockState { UNREAD, CURRENT, READ }

/** A page owns these existing text views. No text, spans, layout or interaction is replaced. */
class LessonReadingTextColors {
    private data class Entry(val block: Int, val view: TextView, val original: ColorStateList)
    private val entries = IdentityHashMap<TextView, Entry>()
    private val unreadColors = mutableMapOf<Int, Int>()

    fun register(blockIndex: Int, view: TextView) {
        // A caller identifies prose, but a misplaced registration must never recolor controls
        // or syntax spans. In particular, icons in this app are ordinary clickable TextViews.
        if (view is EditText || view is Button || view.isClickable || view.isLongClickable ||
            view.text.isNullOrBlank() || hasForegroundSpans(view)) return
        if (!entries.containsKey(view)) entries[view] = Entry(blockIndex, view, view.textColors)
    }

    fun show(states: Map<Int, ReadingBlockState>, enabled: Boolean) {
        entries.values.forEach { entry ->
            val view = entry.view
            val unread = enabled && states[entry.block] == ReadingBlockState.UNREAD
            val background = if (unread) backgroundColor(view) else null
            val color = background?.let { unreadColors.getOrPut(it) { LessonReadingColor.unread(it) } }
            if (color == null || view.isClickable || view.isLongClickable || hasForegroundSpans(view)) {
                if (view.textColors !== entry.original) view.setTextColor(entry.original)
            } else if (!view.textColors.isStateful && view.textColors.defaultColor == color) {
                // No invalidation is needed when a player checkpoint repeats the same state.
            } else view.setTextColor(color)
        }
    }

    fun restore() {
        entries.values.forEach { entry ->
            if (entry.view.textColors !== entry.original) entry.view.setTextColor(entry.original)
        }
    }

    fun clear() {
        restore()
        entries.clear()
        unreadColors.clear()
    }

    private fun hasForegroundSpans(view: TextView): Boolean {
        val text = view.text as? Spanned ?: return false
        return text.getSpans(0, text.length, ForegroundColorSpan::class.java).isNotEmpty()
    }

    private fun backgroundColor(view: View): Int? {
        var surface = 0
        var ancestor: View? = view
        while (ancestor != null) {
            val drawable = ancestor.background
            if (drawable != null) {
                val color = drawableColor(drawable, ancestor.drawableState) ?: return null
                surface = LessonReadingColor.composite(surface, color)
                if (surface ushr 24 == 255) return surface
            }
            ancestor = ancestor.parent as? View
        }
        val value = TypedValue()
        if (!view.context.theme.resolveAttribute(android.R.attr.colorBackground, value, true)) return null
        val fallback = when {
            value.type in TypedValue.TYPE_FIRST_COLOR_INT..TypedValue.TYPE_LAST_COLOR_INT -> value.data
            value.resourceId != 0 -> runCatching {
                view.context.getColorStateList(value.resourceId).getColorForState(view.drawableState, 0)
            }.getOrNull()
            else -> null
        } ?: return null
        return LessonReadingColor.composite(surface, fallback).takeIf { it ushr 24 == 255 }
    }

    private fun drawableColor(drawable: Drawable, state: IntArray): Int? = when (drawable) {
        is ColorDrawable -> drawable.color
        is GradientDrawable -> {
            // Gradients, images and unrecognized backgrounds cannot provide reliable contrast.
            if (drawable.colors != null) null
            else drawable.color?.let {
                val fill = it.getColorForState(state, it.defaultColor)
                val alpha = (fill ushr 24) * drawable.alpha / 255
                (fill and 0xffffff) or (alpha shl 24)
            }
        }
        else -> null
    }
}

/** Pure color arithmetic also exercises both light and dark surfaces in JVM tests. */
internal object LessonReadingColor {
    const val MIN_CONTRAST = 4.5

    fun unread(background: Int): Int {
        // Keep an opaque, neutral gray. On intermediate surfaces select the closest gray
        // meeting ordinary text contrast instead of fading the view or its formula strokes.
        val preferred = if (luminance(background) < .35) 170 else 100
        if (contrast(gray(preferred), background) >= MIN_CONTRAST) return gray(preferred)
        val channel = (0..255).asSequence()
            .filter { contrast(gray(it), background) >= MIN_CONTRAST }
            .minBy { kotlin.math.abs(it - preferred) }
        return gray(channel)
    }

    fun contrast(first: Int, second: Int): Double {
        val a = luminance(first)
        val b = luminance(second)
        return (max(a, b) + .05) / (min(a, b) + .05)
    }

    fun composite(foreground: Int, background: Int): Int {
        val frontAlpha = foreground ushr 24
        val backAlpha = background ushr 24
        val outputAlpha = 255 - (255 - backAlpha) * (255 - frontAlpha) / 255
        if (outputAlpha == 0) return 0
        fun channel(shift: Int): Int {
            val front = foreground ushr shift and 255
            val back = background ushr shift and 255
            return ((front * frontAlpha * 255L + back * backAlpha * (255 - frontAlpha)) /
                (outputAlpha * 255L)).toInt()
        }
        return outputAlpha shl 24 or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }

    private fun gray(channel: Int): Int = -0x1000000 or (channel shl 16) or (channel shl 8) or channel

    private fun luminance(color: Int): Double {
        fun linear(shift: Int): Double {
            val value = (color ushr shift and 255) / 255.0
            return if (value <= .04045) value / 12.92 else ((value + .055) / 1.055).pow(2.4)
        }
        return .2126 * linear(16) + .7152 * linear(8) + .0722 * linear(0)
    }
}

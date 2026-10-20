package com.learngraphicsprogramming.app.narration

import android.content.res.ColorStateList
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import com.learngraphicsprogramming.app.math.MathPuzzleUiInstructions
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

enum class ReadingBlockState { UNREAD, CURRENT, READ }

/** A page owns these existing text views. No text, spans, layout or interaction is replaced. */
class LessonReadingTextColors {
    private data class Entry(val block: Int, val view: TextView, val original: ColorStateList,
                             val feedback: Boolean, val incorrect: Boolean, val target: Int)
    private val entries = linkedMapOf<TextView, Entry>()
    private data class SpokenAnchor(val block: Int, val position: View, val label: String, val target: Int)
    private data class MappingSource(val target: Int, val position: View, val text: CharSequence)
    private val spokenAnchors = linkedMapOf<View, SpokenAnchor>()
    private var nextTarget = Int.MIN_VALUE
    private val sourceSegments = mutableMapOf<Int, Set<Int>>()
    private val unreadColors = mutableMapOf<Int, Int>()
    private val unreadSurfaceColors = mutableMapOf<Pair<Int, Int>, Pair<Int, Int>>()
    private val puzzleMappings = NarrationPuzzleWordMappings.Cache()

    fun register(blockIndex: Int, view: TextView, feedback: Boolean = false, incorrect: Boolean = false) {
        if (view is EditText || (view !is ReadingTextHost &&
            (view is Button || view.isClickable || view.isLongClickable)) || view.text.isNullOrBlank() ||
            (hasForegroundSpans(view) && view !is ReadingTextHost)) return
        if (!entries.containsKey(view)) {
            val target = if (!feedback && entries.values.none { it.block == blockIndex && !it.feedback }) blockIndex else nextTarget++
            entries[view] = Entry(blockIndex, view, view.textColors, feedback, incorrect, target)
        }
    }

    /** An already announced hole has no paintable view. Its native position establishes
     * exact speech order while the surrounding real glyphs keep the full word alignment. */
    fun registerSpokenAnchor(blockIndex: Int, position: View, label: String) {
        val target = spokenAnchors[position]?.target ?: nextTarget++
        spokenAnchors[position] = SpokenAnchor(blockIndex, position, label, target)
    }

    /** Feedback changes role and ink in the same native view; never restore stale success colors. */
    fun unregister(view: TextView): Boolean {
        (view as? ReadingTextHost)?.setUnreadWordRanges(null, 0)
        val removed = entries.remove(view)
        removed?.let { sourceSegments.remove(it.target) }
        val anchor = spokenAnchors.remove(view)
        return removed != null || anchor != null
    }

    fun show(states: Map<Int, ReadingBlockState>, enabled: Boolean,
             unreadWords: Map<Int, List<ReadingTextRange>> = emptyMap(),
             completedSegments: Set<Int> = emptySet(),
             incorrectCompletedSegments: Set<Int> = emptySet()) {
        entries.values.forEach { entry ->
            val view = entry.view
            val pendingFeedback = entry.feedback && sourceSegments[entry.target].orEmpty().let {
                it.isEmpty() || !(if (entry.incorrect) incorrectCompletedSegments else completedSegments).containsAll(it)
            }
            if (view is ReadingTextHost) {
                val wordRanges = unreadWords[entry.target].takeIf { enabled && view.safeSupportsWordReading }
                val surface = if (wordRanges != null) backgroundColor(view) else null
                if (surface != null) {
                    if (view.textColors !== entry.original) view.setTextColor(entry.original)
                    setPendingInk(view, wordRanges, surface)
                    return@forEach
                }
                if (wordRanges == null && enabled && view.safeSupportsWordReading &&
                    (pendingFeedback || (!entry.feedback && states[entry.block] in setOf(ReadingBlockState.UNREAD, ReadingBlockState.CURRENT)))) {
                    backgroundColor(view)?.let { background ->
                        if (view.textColors !== entry.original) view.setTextColor(entry.original)
                        setPendingInk(view, listOf(ReadingTextRange(0, view.text.length)), background)
                        return@forEach
                    }
                }
                view.setUnreadWordRanges(null, 0)
            }
            // Missing safe word provenance is never permission to whiten an entire current block.
            val unread = enabled && (pendingFeedback || (!entry.feedback && states[entry.block] in setOf(ReadingBlockState.UNREAD, ReadingBlockState.CURRENT)))
            val background = if (unread) backgroundColor(view)?.let { color ->
                (view as? ReadingTextHost)?.readingInk?.effectiveReadingSurface(color) ?: color
            } else null
            val color = background?.let { unreadColors.getOrPut(it) { LessonReadingColor.unread(it) } }
            if (color == null || view.isClickable || view.isLongClickable || hasForegroundSpans(view)) {
                if (view.textColors !== entry.original) view.setTextColor(entry.original)
            } else if (!view.textColors.isStateful && view.textColors.defaultColor == color) {
                // No invalidation is needed when a player checkpoint repeats the same state.
            } else view.setTextColor(color)
        }
    }

    private fun setPendingInk(view: ReadingTextHost, ranges: List<ReadingTextRange>?, surface: Int) {
        val semanticSurface = view.readingInk.effectiveReadingSurface(surface)
        val grays = unreadSurfaceColors.getOrPut(surface to semanticSurface) {
            val semanticGray = unreadColors.getOrPut(semanticSurface) { LessonReadingColor.unread(semanticSurface) }
            val common = if (LessonReadingColor.contrast(semanticGray, surface) >= LessonReadingColor.MIN_CONTRAST) semanticGray
                else (0..255).asSequence().map { channel -> 0xff000000.toInt() or (channel shl 16) or (channel shl 8) or channel }
                    .filter { LessonReadingColor.contrast(it, surface) >= LessonReadingColor.MIN_CONTRAST &&
                        LessonReadingColor.contrast(it, semanticSurface) >= LessonReadingColor.MIN_CONTRAST }
                    .minByOrNull { kotlin.math.abs((it and 255) - (semanticGray and 255)) }
            (common ?: unreadColors.getOrPut(surface) { LessonReadingColor.unread(surface) }) to (common ?: semanticGray)
        }
        view.setUnreadWordRanges(ranges, grays.first, grays.second)
    }

    fun restore() {
        entries.values.forEach { entry ->
            (entry.view as? ReadingTextHost)?.setUnreadWordRanges(null, 0)
            if (entry.view.textColors !== entry.original) entry.view.setTextColor(entry.original)
        }
    }

    fun clear() {
        restore()
        entries.clear()
        spokenAnchors.clear()
        sourceSegments.clear()
        nextTarget = Int.MIN_VALUE
        unreadColors.clear()
        unreadSurfaceColors.clear()
        puzzleMappings.clear()
    }

    /** Require complete monotonic lexical identity; repeated words are never found by indexOf. */
    fun wordMappings(parts: List<String>, origins: List<NarrationTextOrigin>,
                     language: NarrationLanguage = NarrationLanguage.SPANISH,
                     incorrectFeedback: Boolean = false): Map<Int, NarrationWordMapping> {
        if (parts.size != origins.size) return emptyMap()
        val result = mutableMapOf<Int, NarrationWordMapping>()
        entries.values.filter { it.incorrect == incorrectFeedback }
            .groupBy { it.block to it.feedback }.forEach { (origin, candidates) ->
            val (block, feedback) = origin
            val views = candidates.filter { (it.view as? ReadingTextHost)?.safeSupportsWordReading == true }
            val anchors = spokenAnchors.values.filter { it.block == block && !feedback && !incorrectFeedback }
            val sources = (views.map { MappingSource(it.target, it.view, it.view.text) } +
                anchors.map { MappingSource(it.target, it.position, it.label.ifBlank { MathPuzzleUiInstructions.spokenBlank(language) }) })
                .sortedWith { left, right -> compareVisualOrder(left.position, right.position) }
            val segments = origins.indices.filter { origins[it].blockIndex == block && origins[it].feedback == feedback }
                .associateWith { parts[it] }
            candidates.forEach { sourceSegments[it.target] = segments.keys }
            if (views.size == 1 && views.single().target == block && anchors.isEmpty()) {
                result += NarrationWordMappings.forView(block, views.single().view.text, segments, language)
            } else {
                result += puzzleMappings.forViews(block, sources.map { it.target to it.text }, segments, language, feedback = feedback)
            }
        }
        return result
    }

    /** Native child order also works before layout and after a card is moved or restored.
     * Registration order is only a fallback for unrelated windows. */
    private fun compareVisualOrder(left: View, right: View): Int {
        if (left.rootView !== right.rootView) return 0
        fun path(view: View): List<Int> {
            val result = mutableListOf<Int>()
            var child = view
            while (true) {
                val parent = child.parent as? ViewGroup ?: break
                result += parent.indexOfChild(child)
                child = parent
            }
            return result.asReversed()
        }
        val a = path(left)
        val b = path(right)
        for (index in 0 until minOf(a.size, b.size)) {
            val compared = a[index].compareTo(b[index])
            if (compared != 0) return compared
        }
        return a.size.compareTo(b.size)
    }

    private fun hasForegroundSpans(view: TextView): Boolean {
        val text = view.text as? Spanned ?: return false
        return text.getSpans(0, text.length, ForegroundColorSpan::class.java).isNotEmpty()
    }

    private fun backgroundColor(view: View): Int? {
        var surface = 0
        var ancestor: View? = view
        while (ancestor != null) {
            // A native radio ripple is transient control feedback, outside label ink.
            val drawable = if (ancestor === view && view is ReadingRadioButton) null else ancestor.background
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
        // Darker than the original pending gray, with the same per-surface contrast floor.
        // This changes glyph ink only; the existing spans and native layout stay intact.
        val preferred = if (luminance(background) < .35) 122 else 80
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

package com.learngraphicsprogramming.app

import android.text.Editable

/** Complements Android's text history with indentation provenance, never with a second text undo.
 * Only fingerprints and unit offsets are retained. Android still owns grouping, text, cursor,
 * selection and redo invalidation. Repeated identical text can have different origins, so states
 * are ordered and traversed in the native action's direction rather than indexed by text alone.
 */
internal class CppIndentationHistory {
    /** Ordinary typing shifts a suffix of the units. Share that layout instead of retaining
     * a full offsets array for every keystroke (and share unchanged layouts as well). */
    private class Units(val base: List<Int>, val split: Int = base.size, val shift: Int = 0) {
        val size get() = base.size
        fun at(index: Int) = base[index] + if (index >= split) shift else 0
        fun matches(starts: List<Int>) = size == starts.size && starts.indices.all { at(it) == starts[it] }
        fun materialize() = if (shift == 0) base else base.indices.map(::at)
        fun next(starts: List<Int>): Units {
            if (size == starts.size) {
                val first = starts.indices.firstOrNull { at(it) != starts[it] } ?: return this
                val delta = starts[first] - at(first)
                if ((first until size).all { starts[it] - at(it) == delta }) {
                    return if (shift == 0 || split == first) Units(base, first, shift + delta)
                    else Units(materialize(), first, delta)
                }
            }
            return Units(starts)
        }
    }
    private data class Recorded(val sha256: String, val units: Units)
    private val states = mutableListOf<Recorded>()
    private var position = -1
    private var editable: Editable? = null
    private var editDepth = 0
    private var batchDepth = 0
    private var replayChanged = false
    private var edited = false
    var replaying = false
        private set

    fun beforeEdit(text: CharSequence?, count: Int, after: Int) {
        if (replaying) {
            if (count != 0 || after != 0) replayChanged = true
            return
        }
        if (editDepth == 0 && batchDepth == 0) record(text as? Editable)
        if (count != 0 || after != 0) edited = true
        editDepth++
    }

    fun afterEdit(text: Editable?) {
        if (replaying) return
        editDepth--
        if (editDepth == 0 && batchDepth == 0) {
            record(text, force = edited)
            edited = false
        }
    }

    fun beginBatch(text: Editable?) {
        if (replaying) return
        if (batchDepth == 0 && editDepth == 0) {
            record(text)
            edited = false
        }
        batchDepth++
    }

    fun endBatch(text: Editable?) {
        if (replaying || batchDepth == 0) return
        batchDepth--
        if (batchDepth == 0 && editDepth == 0) {
            record(text, force = edited)
            edited = false
        }
    }

    fun replay(text: Editable?, redo: Boolean, action: () -> Boolean, restored: () -> Unit): Boolean {
        if (text == null || replaying) return action()
        record(text)
        replayChanged = false
        replaying = true
        val result = try { action() } finally { replaying = false }
        if (replayChanged) {
            val target = CppIndentationUnits.capture(text).sha256
            val candidates = if (redo) (position + 1)..states.lastIndex else (position - 1) downTo 0
            val match = candidates.firstOrNull { states[it].sha256 == target }
            if (match != null) {
                position = match
                val state = states[match]
                CppIndentationUnits.restore(text, CppIndentationUnits.State(state.sha256, state.units.materialize()))
                // The ordinary text observers have already saved their drafts during super.
                // Persist the restored origin too, without another text edit or undo operation.
                restored()
            } else {
                // An external/native history predating this editor's metadata is not evidence
                // that any visually identical spaces were authored indentation.
                states.clear()
                position = -1
                record(text)
            }
        }
        return result
    }

    private fun record(text: Editable?, force: Boolean = false) {
        if (text == null) return
        if (editable !== text) {
            // setText replaces Android's Editable/history (also when changing project files).
            editable = text
            states.clear()
            position = -1
        }
        val state = CppIndentationUnits.capture(text)
        val previous = states.getOrNull(position)
        // An edit ending with identical text still exists in Android's history (e.g. skipping
        // an existing closing delimiter), and invalidates redo. Preserve that transition too.
        if (!force && previous?.sha256 == state.sha256 && previous.units.matches(state.starts)) return
        if (position < states.lastIndex) states.subList(position + 1, states.size).clear()
        states.add(Recorded(state.sha256, previous?.units?.next(state.starts) ?: Units(state.starts)))
        position = states.lastIndex
    }
}

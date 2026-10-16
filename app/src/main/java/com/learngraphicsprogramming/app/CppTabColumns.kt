package com.learngraphicsprogramming.app

import kotlin.math.roundToInt

/** Logical columns for native TAB layout. Offsets always refer to the original UTF-16 source. */
internal object CppTabColumns {
    const val WIDTH = 4

    data class Tab(val offset: Int, val paragraphStart: Int, val column: Int, val target: Int)

    /** Only an explicit TAB can complete the spaces in its current four-column cell.
     * Complete manual cells and source characters before that cell stay untouched. */
    fun completedUnitStart(source: CharSequence, offset: Int): Int {
        require(offset in 0..source.length)
        var lineStart = offset
        while (lineStart > 0 && source[lineStart - 1] != '\n') lineStart--
        var column = 0
        var cursor = lineStart
        while (cursor < offset) {
            when (source[cursor]) {
                '\t' -> column = (column / WIDTH + 1) * WIDTH
                '\r' -> Unit
                else -> {
                    column++
                    if (Character.isHighSurrogate(source[cursor]) && cursor + 1 < offset &&
                        Character.isLowSurrogate(source[cursor + 1])) cursor++
                }
            }
            cursor++
        }
        var remaining = column % WIDTH
        var start = offset
        while (remaining > 0 && start > lineStart && source[start - 1] == ' ') {
            start--
            remaining--
        }
        return start
    }

    fun tabs(source: CharSequence): List<Tab> = buildList {
        var start = 0
        var column = 0
        var offset = 0
        while (offset < source.length) {
            when (source[offset]) {
                '\t' -> {
                    val target = (column / WIDTH + 1) * WIDTH
                    add(Tab(offset, start, column, target))
                    column = target
                }
                '\n' -> {
                    start = offset + 1
                    column = 0
                }
                '\r' -> Unit
                else -> {
                    column++
                    if (Character.isHighSurrogate(source[offset]) && offset + 1 < source.length &&
                        Character.isLowSurrogate(source[offset + 1])) offset++
                }
            }
            offset++
        }
    }

    /** Correct previous pixel rounding so repeated TABs never accumulate alignment drift. */
    fun pixelWidths(tabs: List<Tab>, cellWidth: Float): List<Int> {
        var paragraph = -1
        var column = 0
        var pixels = 0f
        return tabs.map { tab ->
            if (paragraph != tab.paragraphStart) {
                paragraph = tab.paragraphStart
                column = 0
                pixels = 0f
            }
            pixels += (tab.column - column) * cellWidth
            val width = (tab.target * cellWidth - pixels).roundToInt().coerceAtLeast(1)
            pixels += width
            column = tab.target
            width
        }
    }
}

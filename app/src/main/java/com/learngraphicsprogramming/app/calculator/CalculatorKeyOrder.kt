package com.learngraphicsprogramming.app.calculator

/** Stable names survive resource-ID changes; null cells keep the six-column grid uniform. */
internal class CalculatorKeyOrder(private val defaults: List<String?>) {
    private val knownKeys = defaults.filterNotNull().toSet()
    var slots: List<String?> = defaults.toList()
        private set

    init {
        require(defaults.size % COLUMNS == 0 && knownKeys.size == defaults.count { it != null })
    }

    fun restore(saved: List<String?>?) {
        if (saved.isNullOrEmpty() || saved.size > MAX_SLOTS) {
            slots = defaults.toList()
            return
        }
        val seen = mutableSetOf<String>()
        val restored = saved.map { key -> key?.takeIf { it in knownKeys && seen.add(it) } }.toMutableList()
        while (restored.size < defaults.size) restored.add(null)
        // New tools fill empty cells without removing or duplicating existing keys.
        for (missing in defaults.filterNotNull().filterNot { it in seen }) {
            val blank = restored.indexOf(null)
            if (blank >= 0) restored[blank] = missing else restored.add(missing)
        }
        while (restored.size % COLUMNS != 0) restored.add(null)
        slots = restored
    }

    fun swap(first: String, second: String): Boolean {
        val left = slots.indexOf(first)
        val right = slots.indexOf(second)
        if (left < 0 || right < 0 || left == right) return false
        slots = slots.toMutableList().apply {
            this[left] = second
            this[right] = first
        }
        return true
    }

    companion object {
        const val COLUMNS = 6
        private const val MAX_SLOTS = 256
    }
}

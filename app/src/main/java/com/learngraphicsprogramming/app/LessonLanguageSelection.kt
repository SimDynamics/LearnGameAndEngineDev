package com.learngraphicsprogramming.app

/** Match regional Android language preferences to the application's supported choices. */
internal object LessonLanguageSelection {
    fun indexOf(tags: List<String>, selected: String): Int {
        val exact = tags.indexOf(selected)
        if (exact >= 0) return exact
        val primary = selected.substringBefore(',')
        val regional = tags.indexOf(primary)
        if (regional >= 0) return regional
        return tags.indexOf(primary.substringBefore('-')).coerceAtLeast(0)
    }
}

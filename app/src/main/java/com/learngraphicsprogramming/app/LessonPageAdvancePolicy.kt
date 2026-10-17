package com.learngraphicsprogramming.app

/** The bottom button and Enter share the exact same completion decision. */
internal object LessonPageAdvancePolicy {
    fun canAdvance(required: Int, completed: Int): Boolean =
        required >= 0 && completed >= required
}

/** Enter is committed on key-up only for the page that received key-down. */
internal class LessonPageAdvanceEnterGate {
    private var armedPage: String? = null
    fun down(page: String, repeatCount: Int) {
        if (repeatCount == 0) armedPage = page
    }
    fun up(page: String, canceled: Boolean): Boolean {
        val advance = !canceled && armedPage == page
        armedPage = null
        return advance
    }
    fun cancel() { armedPage = null }
}

package com.learngraphicsprogramming.app

/** The Continue and Finish buttons share the same exercise gate as keyboard navigation. */
internal object LessonPageAdvancePolicy {
    fun canAdvance(required: Int, completed: Int): Boolean =
        required >= 0 && completed >= required
}

/** A physical Enter owns exactly one action. Trigger on the first DOWN, not on
 * the IME-dependent UP that can be consumed during focus transitions.
 *
 * Matching key-up and repeated DOWN are consumed; a genuinely new DOWN may act
 * immediately even when an emulator reports an unchanged key downTime.
 */
internal class LessonPageAdvanceEnterGate {
    enum class Route { NAVIGATE, PUZZLE, EDITOR, CONSUME }
    private data class Press(val page: String, val keyCode: Int, val route: Route)
    private var pending: Press? = null
    private var lastDownIdentity: Triple<Int, Long, Long>? = null

    fun down(
        page: String,
        keyCode: Int,
        downTime: Long,
        eventTime: Long,
        repeatCount: Int,
        canNavigate: Boolean,
        editorOwnsEnter: Boolean
    ): Route {
        if (repeatCount > 0) {
            return if (pending?.route == Route.EDITOR && pending?.keyCode == keyCode) Route.EDITOR
                else Route.CONSUME
        }
        val identity = Triple(keyCode, downTime, eventTime)
        // A repeated dispatch of the SAME native event must never trigger twice.
        // Distinct presses with the same downTime are allowed, a real LDPlayer case.
        if (eventTime > 0 && lastDownIdentity == identity) return Route.CONSUME
        lastDownIdentity = identity
        val route = when {
            editorOwnsEnter -> Route.EDITOR
            canNavigate -> Route.NAVIGATE
            else -> Route.PUZZLE
        }
        pending = Press(page, keyCode, route)
        return route
    }

    @Suppress("UNUSED_PARAMETER")
    fun up(page: String, keyCode: Int, downTime: Long, eventTime: Long, canceled: Boolean): Route {
        val press = pending ?: return Route.CONSUME
        if (press.keyCode != keyCode) return Route.CONSUME
        pending = null
        // The action already ran on DOWN. Never execute another action on UP,
        // even if the puzzle was just solved or the Activity opened the next page.
        return if (!canceled && press.page == page && press.route == Route.EDITOR)
            Route.EDITOR else Route.CONSUME
    }
}

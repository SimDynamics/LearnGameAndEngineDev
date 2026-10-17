package com.learngraphicsprogramming.app

/** The Continue and Finish buttons share the same exercise gate as keyboard navigation. */
internal object LessonPageAdvancePolicy {
    fun canAdvance(required: Int, completed: Int): Boolean =
        required >= 0 && completed >= required
}

/** A physical Enter owns one action from its first non-repeated DOWN.
 * Its UP cannot trigger anything else, even when an IME or LDPlayer drops it.
 * Never equate two physical presses by timestamp: emulators can reuse both
 * downTime and eventTime, especially after a mapped touch changes focus.
 */
internal class LessonPageAdvanceEnterGate {
    enum class Route { NAVIGATE, PUZZLE, EDITOR, CONSUME }
    private data class Press(val page: String, val keyCode: Int, val route: Route)
    private var pending: Press? = null

    @Suppress("UNUSED_PARAMETER") // Native timing is diagnostic, never a key identity.
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
        // A repeatCount == 0 is a new native key press, even if LDPlayer has
        // consumed the preceding UP or reused identical downTime/eventTime.
        // Never infer "still held" from the virtual keyboard's clock values.
        // Android supplies repeatCount > 0 for actual held-key repeats.
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
        // Virtual IMEs can return a different downTime in their release event.
        // The action already happened on DOWN, so any UP for this key safely
        // closes the held press without running a second action.
        if (press.keyCode != keyCode) return Route.CONSUME
        pending = null
        // The action already ran on DOWN. Never execute another action on UP,
        // even if the puzzle was just solved or the Activity opened the next page.
        return if (!canceled && press.page == page && press.route == Route.EDITOR)
            Route.EDITOR else Route.CONSUME
    }
}

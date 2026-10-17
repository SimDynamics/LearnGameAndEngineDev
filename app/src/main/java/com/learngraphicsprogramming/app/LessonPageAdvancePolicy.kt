package com.learngraphicsprogramming.app

/** The bottom button and Enter share the exact same completion decision. */
internal object LessonPageAdvancePolicy {
    fun canAdvance(required: Int, completed: Int): Boolean =
        required >= 0 && completed >= required
}

/**
 * A press has exactly one owner from DOWN through UP.
 *
 * Never reconsider the action on UP: checking the last puzzle on DOWN can
 * enable Continue before UP arrives. Likewise, a release from an earlier page
 * cannot navigate the newly rendered page. The short transition guard prevents
 * a rapid double press from skipping a newly displayed page.
 */
internal class LessonPageAdvanceEnterGate {
    enum class Route { NAVIGATE, PUZZLE, EDITOR, CONSUME }

    private data class Press(
        val page: String,
        val keyCode: Int,
        val downTime: Long,
        val route: Route
    )

    private var pending: Press? = null
    private var lastNavigationTime: Long? = null
    private var lastNavigationKey: Pair<Int, Long>? = null

    fun down(
        page: String,
        keyCode: Int,
        downTime: Long,
        eventTime: Long,
        repeatCount: Int,
        canNavigate: Boolean,
        editorOwnsEnter: Boolean
    ): Route {
        val held = pending
        if (repeatCount > 0) {
            return if (held?.keyCode == keyCode && held.downTime == downTime) {
                if (held.route == Route.NAVIGATE) Route.CONSUME else held.route
            } else if (editorOwnsEnter) Route.EDITOR else Route.CONSUME
        }
        if (held != null && held.keyCode == keyCode && held.downTime == downTime)
            return if (held.route == Route.NAVIGATE) Route.CONSUME else held.route

        val tooSoon = lastNavigationTime?.let {
            eventTime >= it && eventTime - it < PAGE_TRANSITION_GUARD_MS
        } == true || lastNavigationKey == (keyCode to downTime)

        val route = when {
            editorOwnsEnter -> Route.EDITOR
            canNavigate -> if (tooSoon) Route.CONSUME else Route.NAVIGATE
            else -> Route.PUZZLE
        }
        pending = Press(page, keyCode, downTime, route)
        return if (route == Route.NAVIGATE) Route.CONSUME else route
    }

    fun up(page: String, keyCode: Int, downTime: Long, eventTime: Long, canceled: Boolean): Route {
        val press = pending ?: return Route.CONSUME
        if (press.keyCode != keyCode || press.downTime != downTime) return Route.CONSUME
        pending = null
        if (press.page != page || canceled) return Route.CONSUME
        if (press.route == Route.NAVIGATE) {
            lastNavigationTime = eventTime
            lastNavigationKey = keyCode to downTime
        }
        return press.route
    }

    companion object {
        const val PAGE_TRANSITION_GUARD_MS = 650L
    }
}

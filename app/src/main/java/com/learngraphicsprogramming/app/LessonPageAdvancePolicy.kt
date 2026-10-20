package com.learngraphicsprogramming.app

/** The Continue and Finish buttons share the same exercise gate as keyboard navigation. */
internal object LessonPageAdvancePolicy {
    fun canAdvance(required: Int, completed: Int): Boolean =
        required >= 0 && completed >= required
}

/** An ordinary Enter acts on its first DOWN. LDPlayer sometimes delivers
 * only UP for a physical press. Recover a valid, previously unseen release
 * without allowing a later duplicate to trigger a second action.
 *
 * Release identity is only used to reject duplicate orphan UP events.
 * Independent DOWN presses remain valid even if timestamps are reused.
 */
internal class LessonPageAdvanceEnterGate {
    enum class Route { NAVIGATE, PUZZLE, EDITOR, CONSUME }
    private data class Press(val page: String, val keyCode: Int, val route: Route, val downTime: Long)
    private data class ReleasedKey(val keyCode: Int, val downTime: Long)
    private var pending: Press? = null
    private var lastFinishedRelease: ReleasedKey? = null
    private data class EditorSubmission(val at: Long, val keyCode: Int?)
    private var editorSubmission: EditorSubmission? = null

    /** IME actions can check without delivering DOWN to the Activity. A release
     * whose press already began before that action belongs to that same check.
     * This is native event ordering, not a delay or a debounce interval. */
    fun editorSubmitted(at: Long, keyCode: Int? = null) {
        editorSubmission = EditorSubmission(at, keyCode)
    }

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
        editorSubmission = null // A new DOWN is always an independent press.
        // A repeatCount == 0 is a new native key press, even if LDPlayer has
        // consumed the preceding UP or reused identical downTime/eventTime.
        // Never infer "still held" from the virtual keyboard's clock values.
        // Android supplies repeatCount > 0 for actual held-key repeats.
        val route = when {
            editorOwnsEnter -> Route.EDITOR
            canNavigate -> Route.NAVIGATE
            else -> Route.PUZZLE
        }
        pending = Press(page, keyCode, route, downTime)
        return route
    }

    fun up(
        page: String,
        keyCode: Int,
        downTime: Long,
        eventTime: Long,
        canceled: Boolean,
        canNavigate: Boolean = false,
        editorOwnsEnter: Boolean = false
    ): Route {
        val press = pending
        if (press != null) {
            if (press.keyCode != keyCode) return Route.CONSUME
            pending = null
            editorSubmission = null
            // The DOWN already checked or navigated. Never execute twice.
            val releaseDownTime = downTime.takeIf { it > 0L } ?: press.downTime
            if (releaseDownTime > 0L) {
                lastFinishedRelease = ReleasedKey(keyCode, releaseDownTime)
            }
            return if (!canceled && press.page == page && press.route == Route.EDITOR)
                Route.EDITOR else Route.CONSUME
        }

        // Some physical Enter presses from LDPlayer reach us as UP only.
        // Only a new, valid key release without a pending DOWN can be recovered.
        if (downTime <= 0L || eventTime < downTime) return Route.CONSUME
        val released = ReleasedKey(keyCode, downTime)
        if (lastFinishedRelease == released) return Route.CONSUME
        lastFinishedRelease = released
        if (canceled) return Route.CONSUME

        val submitted = editorSubmission
        editorSubmission = null
        if (submitted != null && (submitted.keyCode == null || submitted.keyCode == keyCode) &&
            downTime <= submitted.at) return Route.CONSUME

        // Preserve the native C++ editor; never synthesize a newline here.
        return when {
            editorOwnsEnter -> Route.EDITOR
            canNavigate -> Route.NAVIGATE
            else -> Route.PUZZLE
        }
    }
}

package com.learngraphicsprogramming.app

/** One owner for the whole pointer stream, including its final UP or CANCEL. */
internal class FloatingGestureOwnership {
    var readerOwnsGesture = false
        private set
    var consumeAnchorGesture = false
        private set
    fun begin(onReader: Boolean, onOpenPopupAnchor: Boolean) {
        readerOwnsGesture = onReader
        consumeAnchorGesture = !onReader && onOpenPopupAnchor
    }
    fun end() { readerOwnsGesture = false; consumeAnchorGesture = false }
}

/** Tokens prevent an old timeout from dismissing a newly reopened popup. */
internal class PopupLifetime {
    companion object { const val TIMEOUT_MS = 5_000L }
    private var generation = 0L
    private var deadline: Long? = null
    fun opened(now: Long): Long { generation++; deadline = now + TIMEOUT_MS; return generation }
    fun dismissed() { generation++; deadline = null }
    fun expired(token: Long, now: Long): Boolean = token == generation && deadline?.let { now >= it } == true
}

internal data class FloatingPoint(val x: Float, val y: Float)
internal object FloatingReaderPlacement {
    fun visible(point: FloatingPoint, left: Float, top: Float, right: Float, bottom: Float) =
        FloatingPoint((if(point.x.isFinite()) point.x else right).coerceIn(left, maxOf(left,right)),
            (if(point.y.isFinite()) point.y else top).coerceIn(top,maxOf(top,bottom)))
    fun outside(point: FloatingPoint, left: Float, top: Float, right: Float, bottom: Float): Boolean =
        !point.x.isFinite() || !point.y.isFinite() || point.x < left || point.y < top ||
            point.x > right || point.y > bottom
}

package com.learngraphicsprogramming.app.math

import kotlin.math.max

/** View-independent zoom geometry. Neither releasing pointers nor panning changes the scale. */
internal class MathViewportTransform {
    var scale: Float = 1f
        private set
    var panX: Float = 0f
        private set
    var panY: Float = 0f
        private set
    private var viewportWidth = 0f
    private var viewportHeight = 0f
    private var contentWidth = 0f
    private var contentHeight = 0f

    fun resize(viewportWidth: Float, viewportHeight: Float, contentWidth: Float, contentHeight: Float) {
        this.viewportWidth = viewportWidth.coerceAtLeast(0f)
        this.viewportHeight = viewportHeight.coerceAtLeast(0f)
        this.contentWidth = contentWidth.coerceAtLeast(0f)
        this.contentHeight = contentHeight.coerceAtLeast(0f)
        clampPan()
    }

    fun zoomBy(factor: Float, focusX: Float, focusY: Float) {
        if (!factor.isFinite() || factor <= 0 || !focusX.isFinite() || !focusY.isFinite()) return
        val next = (scale * factor).coerceIn(1f, 5f)
        val ratio = next / scale
        panX = focusX - viewportWidth / 2 - (focusX - viewportWidth / 2 - panX) * ratio
        panY = focusY - viewportHeight / 2 - (focusY - viewportHeight / 2 - panY) * ratio
        scale = if (next < 1.001f) 1f else next
        clampPan()
    }

    fun panBy(dx: Float, dy: Float) {
        if (!dx.isFinite() || !dy.isFinite() || scale == 1f) return
        panX += dx
        panY += dy
        clampPan()
    }

    fun reset() {
        scale = 1f
        panX = 0f
        panY = 0f
    }

    private fun clampPan() {
        if (scale == 1f) {
            panX = 0f
            panY = 0f
            return
        }
        val maxX = max(0f, (contentWidth * scale - viewportWidth) / 2)
        val maxY = max(0f, (contentHeight * scale - viewportHeight) / 2)
        panX = panX.coerceIn(-maxX, maxX)
        panY = panY.coerceIn(-maxY, maxY)
    }
}

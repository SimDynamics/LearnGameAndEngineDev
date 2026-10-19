package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Region
import android.os.Build
import android.os.Parcel
import android.os.Parcelable
import android.view.MotionEvent
import android.view.View
import com.learngraphicsprogramming.app.R
import kotlin.math.*

/** A one-finger drag changes the copy only in an explicitly selected editing mode.
 * Two fingers and View mode retain the existing enlarged viewport's pan/zoom ownership. */
class MathRigidAngleCopyView(context: Context, val spec: RigidAngleCopySpec, private val accent: Int) :
    View(context), MathTextScaleAware, MathFullscreenGestureCapability {
    enum class Mode { MOVE_COPY, ROTATE_COPY, VIEW }
    override val allowEdgeSwipeToDismiss = false
    var mode = Mode.MOVE_COPY
    var editable = true
    var onPlacementChanged: (() -> Unit)? = null
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val density = resources.displayMetrics.density.toDouble()
    private var textScale = 1.0
    private val copyColor = Color.rgb(255, 190, 95)
    private val background = context.getColor(R.color.lesson_canvas)
    private val initialPoints = spec.reference.diagram.points.map { it.position } + spec.copy.moved(spec.initial, "").points.map { it.position }
    private val radius = max(spec.reference.radius, spec.copy.radius).coerceAtLeast(1.0)
    // Fixed for the whole attempt: moving the copy must never refit or translate the reference.
    val worldBounds: EuclideanRect = run {
        val centerX = (initialPoints.minOf { it.x } + initialPoints.maxOf { it.x }) / 2
        val centerY = (initialPoints.minOf { it.y } + initialPoints.maxOf { it.y }) / 2
        val halfWidth = max(radius, (initialPoints.maxOf { it.x } - initialPoints.minOf { it.x }) / 2 + radius*.25)
        val halfHeight = max(radius, (initialPoints.maxOf { it.y } - initialPoints.minOf { it.y }) / 2 + radius*.25)
        EuclideanRect(centerX-halfWidth, centerY-halfHeight, centerX+halfWidth, centerY+halfHeight)
    }
    var placement = spec.initial
        private set
    private var previous = EuclideanPosition(0.0, 0.0)
    private var gesture = false

    init {
        id = View.generateViewId()
        isSaveEnabled = true
        setBackgroundColor(background)
        contentDescription = context.getString(R.string.math_rigid_surface)
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
    }

    fun restorePlacement(value: RigidAnglePlacement) { placement = value; invalidate() }

    /** Keep a direct drag recoverable, without snapping to the reference or changing shape.
     * Stored placements remain untouched when restored; this constraint applies only to gestures. */
    private fun reachable(value: RigidAnglePlacement): RigidAnglePlacement {
        val margin = gestureMargin()
        val bottomLeft = unproject(EuclideanPosition(margin, height-margin))
        val topRight = unproject(EuclideanPosition(width-margin, margin))
        val points = spec.copy.moved(value, "").points.map { it.position }
        val lowX = bottomLeft.x-points.minOf { it.x }
        val highX = topRight.x-points.maxOf { it.x }
        val lowY = bottomLeft.y-points.minOf { it.y }
        val highY = topRight.y-points.maxOf { it.y }
        fun correction(low: Double, high: Double) = if (low <= high) 0.0.coerceIn(low, high) else (low+high)/2
        return value.copy(x = value.x+correction(lowX, highX), y = value.y+correction(lowY, highY))
    }

    private fun marksVisible(value: RigidAnglePlacement): Boolean {
        val margin = gestureMargin()
        return spec.copy.moved(value, "").points.all { point ->
            val pixel = project(point.position)
            pixel.x >= margin && pixel.y >= margin && pixel.x <= width-margin && pixel.y <= height-margin
        }
    }

    private fun gestureMargin(): Double {
        val padding = min(max(30*density, textSize()*1.8), min(width, height)*.24)
        // A named mark is not a ray endpoint. Leave room beyond it for the complete
        // arrowhead instead of clamping the marker onto the ray's clipping boundary.
        return min(padding+16*density, min(width, height)*.4)
    }

    private fun factor(): Double {
        val padding = min(max(30 * density, textSize() * 1.8), min(width, height) * .24)
        return min((width - 2 * padding) / (worldBounds.width.coerceAtLeast(1.0) * 1.8),
            (height - 2 * padding) / (worldBounds.height.coerceAtLeast(1.0) * 1.8)).coerceAtLeast(1e-9)
    }

    fun project(point: EuclideanPosition) = EuclideanPosition(width / 2.0 + (point.x - (worldBounds.left + worldBounds.right) / 2) * factor(),
        height / 2.0 - (point.y - (worldBounds.top + worldBounds.bottom) / 2) * factor())

    private fun unproject(point: EuclideanPosition) = EuclideanPosition((point.x - width / 2.0) / factor() + (worldBounds.left + worldBounds.right) / 2,
        -(point.y - height / 2.0) / factor() + (worldBounds.top + worldBounds.bottom) / 2)

    private fun textSize() = 15 * resources.displayMetrics.scaledDensity * textScale

    /** The same reusable Euclidean ray clipping, arrowheads and local name placement as passive diagrams. */
    fun drawingSnapshot(): EuclideanDrawingSnapshot {
        val fixed = spec.reference.moved(spec.referencePlacement, "reference:")
        val moving = spec.copy.moved(placement, "copy:")
        // Coincident vertices are a single geometric location with both original names.
        val grouped = (fixed.points + moving.points).groupBy { it.position }
        val owners = grouped.values.flatMap { group -> group.map { it.id to group.first().id } }.toMap()
        val points = grouped.values.map { group -> group.first().copy(labels = group.flatMap { it.labels }.distinct()) }
        val objects = (fixed.objects + moving.objects).map { obj -> obj.copy(
            definingPointIds = obj.definingPointIds.map { owners.getValue(it) }, contains = obj.contains.map { owners.getValue(it) }.toSet()) }
        paint.textSize = textSize().toFloat()
        val drawing = EuclideanDiagramLayout.layoutInBounds(EuclideanDiagram(points, objects), max(1, width).toDouble(), max(1, height).toDouble(), density,
            paint.textSize.toDouble(), worldBounds) { text, maximum ->
            val lines = mutableListOf<String>()
            var start = 0
            while (start < text.length) {
                var end = start + Character.charCount(Character.codePointAt(text, start))
                while (end < text.length && paint.measureText(text, start, end + Character.charCount(Character.codePointAt(text, end))) <= maximum)
                    end += Character.charCount(Character.codePointAt(text, end))
                lines += text.substring(start, end)
                start = end
            }
            val metrics = paint.fontMetrics
            EuclideanLabelMetrics(lines, lines.maxOf { paint.measureText(it).toDouble() }, (metrics.descent - metrics.ascent + 2 * density), -metrics.ascent.toDouble())
        }
        return RigidAngleLabelLayout.clear(drawing,density)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val drawing = drawingSnapshot()
        fun region(figure: RigidAngleFigure, position: RigidAnglePlacement, color: Int) {
            val center = project(EuclideanPosition(position.x, position.y))
            val first = position.rotate(figure.sides.first().direction)
            val start = -Math.toDegrees(atan2(first.y, first.x))
            val sweep = Math.toDegrees(figure.sweep) * if (figure.counterclockwise) -1 else 1
            val r = (radius * .28 * factor()).coerceAtLeast(14 * density)
            paint.style = Paint.Style.FILL
            paint.color = (color and 0x00ffffff) or 0x35000000
            canvas.drawArc(RectF((center.x-r).toFloat(), (center.y-r).toFloat(), (center.x+r).toFloat(), (center.y+r).toFloat()), start.toFloat(), sweep.toFloat(), true, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = (1.5 * density).toFloat()
            paint.color = color
            canvas.drawArc(RectF((center.x-r).toFloat(), (center.y-r).toFloat(), (center.x+r).toFloat(), (center.y+r).toFloat()), start.toFloat(), sweep.toFloat(), false, paint)
        }
        region(spec.reference, spec.referencePlacement, accent)
        region(spec.copy, placement, copyColor)
        fun stroke(first: EuclideanPosition, second: EuclideanPosition) = canvas.drawLine(first.x.toFloat(), first.y.toFloat(), second.x.toFloat(), second.y.toFloat(), paint)
        paint.strokeCap = Paint.Cap.ROUND
        paint.strokeWidth = (2.2 * density).toFloat()
        drawing.paths.forEach { path ->
            paint.color = if (path.id.startsWith("copy:")) copyColor else accent
            stroke(path.start, path.end)
            drawing.arrows.filter { it.objectId == path.id }.forEach { stroke(it.tip, it.wingA); stroke(it.tip, it.wingB) }
        }
        // Short leaders also identify names relocated to clear a neighboring marker.
        // Their endpoints are the original points; no snapping or displaced marker is used.
        paint.strokeWidth = density.toFloat()
        drawing.labels.forEach { label ->
            val owner = drawing.points.singleOrNull { it.id == label.ownerId } ?: return@forEach
            paint.color = if (label.ownerId.startsWith("copy:")) copyColor else Color.WHITE
            val start = EuclideanPosition(owner.center.x.coerceIn(label.bounds.left, label.bounds.right),
                owner.center.y.coerceIn(label.bounds.top, label.bounds.bottom))
            stroke(start, owner.center)
        }
        paint.style = Paint.Style.FILL
        drawing.points.forEach { point ->
            paint.color = background
            canvas.drawCircle(point.center.x.toFloat(), point.center.y.toFloat(), (5.6 * density).toFloat(), paint)
            paint.color = if (point.id.startsWith("copy:")) copyColor else Color.WHITE
            canvas.drawCircle(point.center.x.toFloat(), point.center.y.toFloat(), (4 * density).toFloat(), paint)
        }
        val protectedInk = Path().apply {
            drawing.points.forEach { addCircle(it.center.x.toFloat(),it.center.y.toFloat(),(6.6*density).toFloat(),Path.Direction.CW) }
            val arrowStroke = Paint(paint).apply { style = Paint.Style.STROKE; strokeWidth = (3.6*density).toFloat() }
            drawing.arrows.forEach { arrow ->
                for (wing in listOf(arrow.wingA,arrow.wingB)) {
                    val line = Path().apply { moveTo(arrow.tip.x.toFloat(),arrow.tip.y.toFloat()); lineTo(wing.x.toFloat(),wing.y.toFloat()) }
                    val outline = Path()
                    arrowStroke.getFillPath(line,outline)
                    addPath(outline)
                }
            }
        }
        drawing.labels.forEach { label ->
            val bounds = label.bounds
            paint.color = background
            val save = canvas.save()
            if (Build.VERSION.SDK_INT >= 26) canvas.clipOutPath(protectedInk)
            else {
                @Suppress("DEPRECATION")
                canvas.clipPath(protectedInk,Region.Op.DIFFERENCE)
            }
            canvas.drawRoundRect(RectF(bounds.left.toFloat(), bounds.top.toFloat(), bounds.right.toFloat(), bounds.bottom.toFloat()), (2*density).toFloat(), (2*density).toFloat(), paint)
            canvas.restoreToCount(save)
            paint.color = if (label.ownerId.startsWith("copy:")) copyColor else Color.WHITE
            label.lines.forEachIndexed { index, text -> canvas.drawText(text, (bounds.left+2*density).toFloat(),
                (bounds.top+density+label.ascent+index*label.lineHeight).toFloat(), paint) }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!editable || mode == Mode.VIEW || event.pointerCount != 1) {
            gesture = false
            parent?.requestDisallowInterceptTouchEvent(false)
            return false
        }
        val current = unproject(EuclideanPosition(event.x.toDouble(), event.y.toDouble()))
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                previous = current
                gesture = true
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> if (gesture) {
                val next = if (mode == Mode.MOVE_COPY) RigidAnglePlacement(placement.x+current.x-previous.x, placement.y+current.y-previous.y, placement.rotation)
                else {
                    val center = EuclideanPosition(placement.x, placement.y)
                    val before = previous-center
                    val after = current-center
                    if (hypot(before.x, before.y) * factor() < 20*density || hypot(after.x, after.y) * factor() < 20*density) placement
                    else placement.copy(rotation = placement.rotation + signedAngle(before, after))
                }
                // Rotation keeps its vertex fixed. The explicit reset is always available;
                // direct translation is bounded so a long drag cannot lose the entire copy.
                placement = if (mode == Mode.MOVE_COPY) reachable(next) else if (marksVisible(next)) next else placement
                previous = current
                invalidate()
                onPlacementChanged?.invoke()
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val handled = gesture
                gesture = false
                parent?.requestDisallowInterceptTouchEvent(false)
                if (event.actionMasked == MotionEvent.ACTION_UP && handled) performClick()
                return handled
            }
        }
        return gesture
    }

    override fun performClick(): Boolean { super.performClick(); return true }
    override fun scaleMathText(factor: Float) { if (factor.isFinite() && factor > 0) { textScale *= factor; invalidate() } }

    // The renderer persists the answer to the existing progress store. This additionally
    // protects the same native view when Android restores its ordinary hierarchy state.
    override fun onSaveInstanceState(): Parcelable = SavedState(super.onSaveInstanceState()).also { it.value = placement }
    override fun onRestoreInstanceState(state: Parcelable?) {
        if (state is SavedState) { super.onRestoreInstanceState(state.superState); restorePlacement(state.value) }
        else super.onRestoreInstanceState(state)
    }
    private class SavedState : BaseSavedState {
        var value = RigidAnglePlacement(0.0, 0.0, 0.0)
        constructor(superState: Parcelable?) : super(superState)
        private constructor(parcel: Parcel) : super(parcel) { value = RigidAnglePlacement(parcel.readDouble(), parcel.readDouble(), parcel.readDouble()) }
        override fun writeToParcel(out: Parcel, flags: Int) { super.writeToParcel(out, flags); out.writeDouble(value.x); out.writeDouble(value.y); out.writeDouble(value.rotation) }
        companion object { @JvmField val CREATOR = object : Parcelable.Creator<SavedState> {
            override fun createFromParcel(source: Parcel) = SavedState(source)
            override fun newArray(size: Int): Array<SavedState?> = arrayOfNulls(size)
        } }
    }
}

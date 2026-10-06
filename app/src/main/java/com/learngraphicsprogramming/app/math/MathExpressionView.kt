package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.util.TypedValue
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min

/** An editing touch may identify a UTF-16 boundary inside one AST leaf. */
internal data class MathExpressionTouchTarget(
    val cursor: MathCursor,
    val leafTextOffset: Int? = null,
    val sequenceInsertion: Boolean = false,
    val prefixOffset: Int? = null
)

/** The same layout is used by editable expressions and formula spans in lesson prose. */
class MathExpressionLayout private constructor(private val box: ExpressionBox, private val textSize: Float) {
    val width: Float get() = box.width
    val ascent: Float get() = box.ascent
    val descent: Float get() = box.descent
    val height: Float get() = descent - ascent

    fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) = box.draw(canvas, paint, x, baseline)

    /** Coordinates are relative to the expression's origin and baseline. */
    fun caretBounds(cursor: MathCursor): RectF? = box.carets.firstOrNull {
        sameLocation(
            it.cursor,
            cursor
        ) &&
                it.leafTextOffset == null &&
                it.prefixOffset == null
    }?.let {

        RectF(
            it.x,
            it.top,
            it.x,
            it.bottom
        )
    }

    /**
     * Visualiza el estado lógico del modelo.
     *
     * La geometría nunca decide la navegación.
     */
    fun presentationCaretBounds(
        cursor: MathCursor,
        presentation: MathCursorPresentation
    ): RectF? =
        when (presentation) {

            /*
             * EDICIÓN.
             */
            is MathCursorPresentation.Interior -> {

                /*
                 * Si existe una posición interna
                 * de texto, utilizamos el caret
                 * que el layout ya calcula para
                 * cada frontera UTF-16 del nodo.
                 *
                 * Así Number("22") puede mostrar:
                 *
                 * |22
                 * 2|2
                 * 22|
                 *
                 * sin modificar el AST.
                 */
                val textOffset =
                    presentation.leafTextOffset

                if (textOffset != null) {

                    textCaretBounds(
                        cursor,
                        textOffset
                    )

                } else {

                    val caret =
                        caretBounds(
                            cursor
                        )

                    val atom =
                        presentation.scope
                                as? MathContentScope.Atom

                    val content =
                        atom?.let {

                            contentBounds(
                                cursor.path,
                                it.start,
                                it.end
                            )
                        }

                    if (
                        caret == null ||
                        content == null
                    ) {
                        caret
                    } else {
                        RectF(
                            caret.left,
                            content.top,
                            caret.left,
                            content.bottom
                        )
                    }
                }
            }

            /*
 * Cursor dentro del prefijo
 * virtual ^ / ^^.
 */
            is MathCursorPresentation.Prefix -> {

                prefixCaretBounds(
                    presentation.slotPath,
                    presentation.offset
                )
            }

            /*
             * FIN REAL.
             *
             * FIN pertenece a la expresión completa,
             * por lo tanto su cursor debe ser GRANDE.
             *
             * Si existe el PARENT virtual después
             * de LargeOperator:
             *
             * ∫ □ |
             *
             * X está después del cuadro,
             * pero la altura corresponde a TODA
             * la Sequence.
             */
            MathCursorPresentation.Neutral -> {

                val caret =
                    caretBounds(
                        cursor
                    )

                val continuation =
                    continuationBounds(
                        cursor
                    )

                val sequence =
                    sequenceBounds(
                        cursor.path
                    )

                if (
                    continuation != null &&
                    sequence != null
                ) {

                    val gap =
                        max(
                            1f,
                            textSize * .08f
                        )

                    val x =
                        continuation.right +
                                gap

                    RectF(
                        x,
                        sequence.top -
                                textSize * .06f,
                        x,
                        sequence.bottom +
                                textSize * .06f
                    )

                } else if (
                    caret != null &&
                    sequence != null
                ) {

                    /*
                     * FIN normal sin continuación.
                     *
                     * También es grande.
                     */
                    RectF(
                        caret.left,
                        sequence.top -
                                textSize * .06f,
                        caret.left,
                        sequence.bottom +
                                textSize * .06f
                    )

                } else {

                    caret
                }
            }

            /*
             * BORDE DERECHO DEL PARENT VIRTUAL:
             *
             * □ ]
             *
             * Este sí conserva solamente
             * la altura del cuadro.
             */
            MathCursorPresentation.ContinuationBoundary -> {

                val continuation =
                    continuationBounds(
                        cursor
                    )

                if (
                    continuation != null
                ) {

                    val gap =
                        max(
                            1f,
                            textSize * .05f
                        )

                    val x =
                        continuation.right +
                                gap

                    RectF(
                        x,
                        continuation.top,
                        x,
                        continuation.bottom
                    )

                } else {

                    caretBounds(
                        cursor
                    )
                }
            }

            /*
             * Borde derecho de una parte interna.
             */
            is MathCursorPresentation.ChildBoundary -> {

                sequenceBounds(
                    presentation.slotPath
                )?.let { scope ->

                    val gap =
                        max(
                            1f,
                            textSize * .05f
                        )

                    val x =
                        scope.right +
                                gap

                    RectF(
                        x,
                        scope.top,
                        x,
                        scope.bottom
                    )

                } ?: caretBounds(
                    cursor
                )
            }

            /*
 * Único borde del grupo
 * upper + lower.
 */
            is MathCursorPresentation.LimitsBoundary -> {

                val upper =
                    sequenceBounds(
                        presentation.path +
                                MathCursorStep(
                                    presentation.nodeIndex,
                                    "upper"
                                )
                    )

                val lower =
                    sequenceBounds(
                        presentation.path +
                                MathCursorStep(
                                    presentation.nodeIndex,
                                    "lower"
                                )
                    )

                val bounds =
                    listOfNotNull(
                        upper,
                        lower
                    )

                if (
                    bounds.isEmpty()
                ) {

                    caretBounds(
                        cursor
                    )

                } else {

                    val gap =
                        max(
                            1f,
                            textSize * .05f
                        )

                    val x =
                        bounds.maxOf {
                            it.right
                        } +
                                gap

                    RectF(
                        x,
                        bounds.minOf {
                            it.top
                        },
                        x,
                        bounds.maxOf {
                            it.bottom
                        }
                    )
                }
            }

            /*
             * Borde derecho de un PARENT
             * o STRUCTURE real.
             */
            is MathCursorPresentation.ParentBoundary -> {

                childBounds(
                    presentation.path,
                    presentation.nodeIndex
                )?.let { parent ->

                    val gap =
                        max(
                            1f,
                            textSize * .05f
                        )

                    val x =
                        parent.right +
                                gap

                    RectF(
                        x,
                        parent.top,
                        x,
                        parent.bottom
                    )

                } ?: caretBounds(
                    cursor
                )
            }
        }

    /** Bounds of an actual Sequence, including its empty placeholder when shown. */
    internal fun sequenceBounds(path: List<MathCursorStep>): RectF? = box.sequences.firstOrNull { it.path == path }?.let {
        RectF(it.left, it.top, it.right, it.bottom)
    }

    /** Bounds of one direct group include all of its nested children and decorations. */
    internal fun childBounds(path: List<MathCursorStep>, nodeIndex: Int): RectF? = box.childrenBounds.firstOrNull {
        it.path == path && it.index == nodeIndex
    }?.let { RectF(it.left, it.top, it.right, it.bottom) }

    /** An atom may span adjacent text leaves after a precise edit splits its AST node. */
    private fun contentBounds(path: List<MathCursorStep>, start: Int, end: Int): RectF? {
        val children = box.childrenBounds.filter { it.path == path && it.index in start until end }
        if (children.isEmpty()) return null
        return RectF(children.minOf { it.left }, children.minOf { it.top }, children.maxOf { it.right }, children.maxOf { it.bottom })
    }

    fun hitTest(x: Float, y: Float): MathCursor {
        continuationTouchTarget(x, y)?.let { return it.cursor }
        rootExteriorTouchCursor(x)?.let { return it }
        return box.carets.filter {
            it.leafTextOffset == null &&
                    it.prefixOffset == null
        }.minByOrNull {
            distance(it, x, y)
        }?.cursor ?: MathCursor()
    }

    /** Interior glyph boundaries are resolved only for an actual editing touch, never during draw. */
    internal fun hitTestForEditing(x: Float, y: Float): MathExpressionTouchTarget {
        continuationTouchTarget(x, y)?.let { return it }
        rootExteriorTouchCursor(x)?.let { return MathExpressionTouchTarget(it) }
        return box.carets
            .filter {

                it.prefixOffset != null ||
                        it.leafTextOffset == null ||
                        (
                                x in
                                        requireNotNull(
                                            it.leafLeft
                                        )..
                                        requireNotNull(
                                            it.leafRight
                                        ) &&
                                        y in
                                        it.top..
                                        it.bottom
                                )
            }
            .minWithOrNull(
                compareBy<ExpressionCaret> {
                    distance(
                        it,
                        x,
                        y
                    )
                }
                    .thenBy {
                        if (
                            it.prefixOffset != null
                        ) {
                            0
                        } else if (
                            it.leafTextOffset != null
                        ) {
                            1
                        } else {
                            2
                        }
                    }
            )
            ?.let {

                MathExpressionTouchTarget(
                    cursor =
                        it.cursor,

                    leafTextOffset =
                        it.leafTextOffset,

                    prefixOffset =
                        it.prefixOffset
                )
            }
            ?: MathExpressionTouchTarget(
                MathCursor()
            )
    }

    internal fun continuationBounds(cursor: MathCursor): RectF? = box.carets.firstOrNull {
        sameLocation(it.cursor, cursor) && it.continuationBounds != null
    }?.continuationBounds?.let(::RectF)

    private fun continuationTouchTarget(x: Float, y: Float): MathExpressionTouchTarget? = box.carets
        .filter { it.continuationBounds?.contains(x, y) == true }
        .maxByOrNull { it.cursor.path.size }
        ?.let { MathExpressionTouchTarget(it.cursor, sequenceInsertion = true) }

    /** Empty fields have HOME; space beyond all written groups has the last parent edge, at any height. */
    private fun rootExteriorTouchCursor(x: Float): MathCursor? {
        val parents = box.childrenBounds.filter { it.path.isEmpty() }
        if (parents.isEmpty()) return MathCursor()
        if (x > parents.maxOf { it.right }) return MathCursor(offset = parents.maxOf { it.index } + 1)
        return null
    }

    internal fun textCaretBounds(
        cursor: MathCursor,
        leafTextOffset: Int
    ): RectF? =

        box.carets
            .firstOrNull {

                sameLocation(
                    it.cursor,
                    cursor
                ) &&
                        it.leafTextOffset ==
                        leafTextOffset
            }
            ?.let {

                RectF(
                    it.x,
                    it.top,
                    it.x,
                    it.bottom
                )
            }

    internal fun prefixCaretBounds(
        slotPath: List<MathCursorStep>,
        prefixOffset: Int
    ): RectF? =

        box.carets
            .firstOrNull {

                it.cursor.path ==
                        slotPath &&
                        it.prefixOffset ==
                        prefixOffset
            }
            ?.let {

                RectF(
                    it.x,
                    it.top,
                    it.x,
                    it.bottom
                )
            }

    private fun sameLocation(
        first: MathCursor,
        second: MathCursor
    ): Boolean =
        first.path ==
                second.path &&
                first.offset ==
                second.offset

    private fun distance(caret: ExpressionCaret, x: Float, y: Float): Float {
        val dx = abs(x - caret.x)
        val dy = when { y < caret.top -> caret.top - y; y > caret.bottom -> y - caret.bottom; else -> 0f }
        return dx * dx + dy * dy * 4f - caret.cursor.path.size * .01f
    }

    companion object {
        fun create(expression: MathExpression.Sequence, paint: Paint, size: Float, showPlaceholders: Boolean = false): MathExpressionLayout =
            MathExpressionLayout(
                ExpressionLayoutBuilder(
                    Paint(paint),
                    showPlaceholders,
                    size
                ).sequence(
                    expression,
                    size,
                    emptyList()
                ),
                size
            )
    }
}

/** Native two-dimensional mathematical surface. Editors add input/state behavior to this view. */
open class MathExpressionView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    View(context, attrs), MathTextScaleAware, MathFullscreenGestureCapability {
    override val allowEdgeSwipeToDismiss: Boolean get() = onCursorRequested == null
    protected val expressionPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private var cachedLayout: MathExpressionLayout? = null

    var expression: MathExpression.Sequence = MathExpression.Sequence(emptyList())
        set(value) {
            field = value
            contentDescription = MathLatexSerializer.serialize(value)
            resetExpressionLayout()
        }
    var cursor: MathCursor? = null
        set(value) { field = value; invalidate() }
    var cursorPresentation: MathCursorPresentation = MathCursorPresentation.Interior()
        set(value) { field = value; invalidate() }
    var onCursorRequested: ((MathCursor) -> Unit)? = null
    var showPlaceholders: Boolean = false
        set(value) { field = value; resetExpressionLayout() }
    var textSizePx: Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 18f, resources.displayMetrics)
        set(value) {
            if (!value.isFinite() || value <= 0f) return
            field = value
            resetExpressionLayout()
        }
    var textColor: Int = Color.WHITE
        set(value) { field = value; expressionPaint.color = value; invalidate() }
    var caretColor: Int = Color.rgb(133, 193, 255)
        set(value) { field = value; invalidate() }

    /** Temporary display budget; the source text size and mathematical state stay unchanged. */
    internal var viewportHeightLimit: Int? = null
        set(value) {
            if (field == value) return
            field = value
            resetExpressionLayout()
        }

    val expressionLayout: MathExpressionLayout
        get() = cachedLayout ?: run {
            val normal = MathExpressionLayout.create(expression, expressionPaint, textSizePx, showPlaceholders)
            val inkHeight = viewportHeightLimit?.let { (it - paddingTop - paddingBottom - 1).coerceAtLeast(1) }
            if (inkHeight != null && normal.height > inkHeight) {
                // Ordinary fractions/scripts fit as a whole. Taller matrices retain readable
                // text and the host reveals the active slot once, instead of shrinking forever.
                val readableSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 12f, resources.displayMetrics)
                val size = min(textSizePx, max(readableSize, textSizePx * inkHeight / normal.height))
                MathExpressionLayout.create(expression, expressionPaint, size, showPlaceholders)
            } else normal
        }.also { cachedLayout = it }
    val expressionOriginX: Float get() = paddingLeft.toFloat()
    val expressionBaseline: Float get() = paddingTop - expressionLayout.ascent

    init { setWillNotDraw(false) }

    protected fun resetExpressionLayout() {
        cachedLayout = null
        requestLayout()
        invalidate()
    }

    override fun scaleMathText(factor: Float) {
        if (factor.isFinite() && factor > 0f) textSizePx *= factor
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val layout = expressionLayout
        val desiredWidth = max(suggestedMinimumWidth, ceil(layout.width).toInt() + paddingLeft + paddingRight)
        val minimum = viewportHeightLimit?.let { min(suggestedMinimumHeight, it) } ?: suggestedMinimumHeight
        val desiredHeight = max(minimum, ceil(layout.height).toInt() + paddingTop + paddingBottom)
        setMeasuredDimension(resolveSize(desiredWidth, widthMeasureSpec), resolveSize(desiredHeight, heightMeasureSpec))
    }

    override fun onDraw(
        canvas: Canvas
    ) {

        super.onDraw(
            canvas
        )

        expressionPaint.color =
            textColor

        expressionLayout.draw(
            canvas,
            expressionPaint,
            expressionOriginX,
            expressionBaseline
        )

        cursor?.let { selected ->

            val presentation =
                cursorPresentation

            expressionLayout
                .presentationCaretBounds(
                    selected,
                    presentation
                )
                ?.let { bounds ->

                    expressionPaint.color =
                        caretColor

                    expressionPaint.style =
                        Paint.Style.STROKE

                    val structuralBoundary =
                        presentation is
                                MathCursorPresentation.ParentBoundary ||
                                presentation is
                                        MathCursorPresentation.ChildBoundary ||
                                presentation is
                                        MathCursorPresentation.LimitsBoundary

                    expressionPaint.strokeWidth =
                        max(
                            resources.displayMetrics.density,
                            textSizePx * .045f
                        ) *
                                if (
                                    presentation ==
                                    MathCursorPresentation.Neutral ||
                                    structuralBoundary
                                ) {
                                    1.35f
                                } else {
                                    1f
                                }

                    canvas.drawLine(
                        expressionOriginX +
                                bounds.left,
                        expressionBaseline +
                                bounds.top,
                        expressionOriginX +
                                bounds.left,
                        expressionBaseline +
                                bounds.bottom,
                        expressionPaint
                    )

                    /*
                     * Únicamente los checkpoints ]
                     * reciben las dos patitas.
                     */
                    if (
                        structuralBoundary
                    ) {

                        val inside =
                            bounds.left -
                                    textSizePx * .07f

                        canvas.drawLine(
                            expressionOriginX +
                                    bounds.left,
                            expressionBaseline +
                                    bounds.top,
                            expressionOriginX +
                                    inside,
                            expressionBaseline +
                                    bounds.top,
                            expressionPaint
                        )

                        canvas.drawLine(
                            expressionOriginX +
                                    bounds.left,
                            expressionBaseline +
                                    bounds.bottom,
                            expressionOriginX +
                                    inside,
                            expressionBaseline +
                                    bounds.bottom,
                            expressionPaint
                        )
                    }

                    expressionPaint.style =
                        Paint.Style.FILL
                }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val callback = onCursorRequested ?: return super.onTouchEvent(event)
        if (!isEnabled) return false
        if (event.actionMasked == MotionEvent.ACTION_DOWN) return true
        if (event.actionMasked == MotionEvent.ACTION_UP) {
            callback(expressionLayout.hitTest(event.x - expressionOriginX, event.y - expressionBaseline))
            performClick()
            return true
        }
        return event.actionMasked == MotionEvent.ACTION_MOVE
    }

    override fun performClick(): Boolean { super.performClick(); return true }
}

private data class ExpressionCaret(
    val cursor: MathCursor,
    val x: Float,
    val top: Float,
    val bottom: Float,
    val leafTextOffset: Int? = null,
    val leafLeft: Float? = null,
    val leafRight: Float? = null,
    val continuationBounds: RectF? = null,
    val prefixOffset: Int? = null
) {
    fun translated(dx: Float, dy: Float) = copy(x = x + dx, top = top + dy, bottom = bottom + dy,
        leafLeft = leafLeft?.plus(dx), leafRight = leafRight?.plus(dx),
        continuationBounds = continuationBounds?.let { RectF(it).apply { offset(dx, dy) } })
}

private data class ExpressionSequenceBounds(val path: List<MathCursorStep>, val left: Float, val top: Float,
    val right: Float, val bottom: Float) {
    fun translated(dx: Float, dy: Float) = copy(left = left + dx, top = top + dy, right = right + dx, bottom = bottom + dy)
}

private data class ExpressionChildBounds(val path: List<MathCursorStep>, val index: Int, val left: Float,
    val top: Float, val right: Float, val bottom: Float) {
    fun translated(dx: Float, dy: Float) = copy(left = left + dx, top = top + dy, right = right + dx, bottom = bottom + dy)
}

private interface ExpressionInk { fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) }

private class ExpressionText(private val value: String, private val size: Float, private val italic: Boolean = false) : ExpressionInk {
    override fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) {
        paint.textSize = size
        paint.typeface = Typeface.create("serif", if (italic) Typeface.ITALIC else Typeface.NORMAL)
        paint.style = Paint.Style.FILL
        canvas.drawText(value, x, baseline, paint)
    }
}

private class ExpressionStroke(private val path: Path, private val size: Float, private val muted: Boolean = false) : ExpressionInk {
    override fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) {
        val save = canvas.save()
        val oldAlpha = paint.alpha
        canvas.translate(x, baseline)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = max(1f, size * .035f)
        paint.strokeCap = Paint.Cap.ROUND
        if (muted) paint.alpha = min(oldAlpha, 120)
        canvas.drawPath(path, paint)
        paint.alpha = oldAlpha
        paint.style = Paint.Style.FILL
        canvas.restoreToCount(save)
    }
}

private class ExpressionPlaced(private val child: ExpressionBox, private val dx: Float, private val dy: Float) : ExpressionInk {
    override fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) = child.draw(canvas, paint, x + dx, baseline + dy)
}

private data class ExpressionBox(val width: Float, val ascent: Float, val descent: Float,
    val ink: List<ExpressionInk>, val carets: List<ExpressionCaret> = emptyList(),
    val sequences: List<ExpressionSequenceBounds> = emptyList(),
    val childrenBounds: List<ExpressionChildBounds> = emptyList()) {
    fun draw(canvas: Canvas, paint: Paint, x: Float, baseline: Float) { ink.forEach { it.draw(canvas, paint, x, baseline) } }
}

private class ExpressionLayoutBuilder(
    private val paint: Paint,
    private val placeholders: Boolean,
    private val rootSize: Float
) {
    fun sequence(
        sequence: MathExpression.Sequence,
        size: Float,
        path: List<MathCursorStep>
    ): ExpressionBox {

        if (
            sequence.children.isEmpty()
        ) {

            /*
             * La raíz vacía del editor se comporta
             * como un campo de texto vacío normal.
             *
             * Los cuadrados aparecen únicamente dentro
             * de estructuras creadas por el usuario.
             */
            val internalSlot =
                placeholders &&
                        path.isNotEmpty()

            if (!internalSlot) {

                return ExpressionBox(
                    0f,
                    -size * .8f,
                    size * .2f,
                    emptyList(),
                    listOf(
                        ExpressionCaret(
                            MathCursor(
                                path,
                                0
                            ),
                            0f,
                            -size * .8f,
                            size * .2f
                        )
                    ),
                    listOf(
                        ExpressionSequenceBounds(
                            path,
                            0f,
                            -size * .8f,
                            0f,
                            size * .2f
                        )
                    )
                )
            }

            /*
             * Un slot vacío necesita ser cómodo para touch.
             */
            val slotSize =
                max(
                    size,
                    rootSize * 1.05f
                )

            val width =
                slotSize * 1.05f

            val top =
                -slotSize * .88f

            val bottom =
                slotSize * .28f

            val horizontalInset =
                slotSize * .08f

            val corner =
                slotSize * .10f

            val ink =
                listOf(
                    ExpressionStroke(
                        Path().apply {

                            addRoundRect(
                                RectF(
                                    horizontalInset,
                                    top,
                                    width -
                                            horizontalInset,
                                    bottom
                                ),
                                corner,
                                corner,
                                Path.Direction.CW
                            )
                        },
                        slotSize,
                        true
                    )
                )

            return ExpressionBox(
                width,
                top,
                bottom,
                ink,
                listOf(
                    ExpressionCaret(
                        MathCursor(
                            path,
                            0
                        ),
                        width / 2f,
                        top,
                        bottom
                    )
                ),
                listOf(
                    ExpressionSequenceBounds(
                        path,
                        0f,
                        top,
                        width,
                        bottom
                    )
                )
            )
        }

        val boxes =
            sequence.children.mapIndexed { index, child ->
                node(
                    child,
                    size,
                    path,
                    index
                )
            }

        /*
         * Si el último padre es un operador grande,
         * mostramos una posición de continuación.
         *
         * IMPORTANTE:
         *
         * Este cuadro NO pertenece al LargeOperator.
         * No es "body".
         * No existe dentro del AST.
         *
         * Representa solamente la posición normal:
         *
         * MathCursor(path, sequence.children.size)
         *
         * Es decir, el lugar donde aparecerá el siguiente
         * hermano de la Sequence.
         */
        val showLargeOperatorContinuation =
            placeholders &&
                    sequence.hasContinuationInsertion()

        val continuationSlotSize =
            max(
                size,
                rootSize * 1.05f
            )

        val continuationWidth =
            continuationSlotSize * 1.05f

        val continuationTop =
            -continuationSlotSize * .88f

        val continuationBottom =
            continuationSlotSize * .28f

        val normalAscent =
            boxes.minOf {
                it.ascent
            }

        val normalDescent =
            boxes.maxOf {
                it.descent
            }

        val ascent =
            if (showLargeOperatorContinuation) {
                min(
                    normalAscent,
                    continuationTop
                )
            } else {
                normalAscent
            }

        val descent =
            if (showLargeOperatorContinuation) {
                max(
                    normalDescent,
                    continuationBottom
                )
            } else {
                normalDescent
            }

        val ink =
            mutableListOf<ExpressionInk>()

        val carets =
            mutableListOf<ExpressionCaret>()

        val sequences =
            mutableListOf<ExpressionSequenceBounds>()

        val childrenBounds =
            mutableListOf<ExpressionChildBounds>()

        var x =
            0f

        boxes.forEachIndexed { index, child ->

            carets +=
                ExpressionCaret(
                    MathCursor(
                        path,
                        index
                    ),
                    x,
                    ascent,
                    descent
                )

            ink +=
                ExpressionPlaced(
                    child,
                    x,
                    0f
                )

            carets +=
                child.carets.map {
                    it.translated(
                        x,
                        0f
                    )
                }

            sequences +=
                child.sequences.map {
                    it.translated(
                        x,
                        0f
                    )
                }

            childrenBounds +=
                ExpressionChildBounds(
                    path,
                    index,
                    x,
                    child.ascent,
                    x + child.width,
                    child.descent
                )

            childrenBounds +=
                child.childrenBounds.map {
                    it.translated(
                        x,
                        0f
                    )
                }

            x +=
                child.width
        }

        /*
         * Éste sigue siendo el cursor REAL de la Sequence.
         *
         * Si hay un LargeOperator al final, queda justo
         * al comienzo del cuadro visual de continuación.
         */
        carets +=
            ExpressionCaret(
                MathCursor(
                    path,
                    boxes.size
                ),
                x,
                if (showLargeOperatorContinuation) continuationTop else ascent,
                if (showLargeOperatorContinuation) continuationBottom else descent,
                continuationBounds = if (showLargeOperatorContinuation) RectF(
                    x + continuationSlotSize * .08f, continuationTop,
                    x + continuationWidth - continuationSlotSize * .08f, continuationBottom
                ) else null
            )

        if (showLargeOperatorContinuation) {

            val horizontalInset =
                continuationSlotSize * .08f

            val corner =
                continuationSlotSize * .10f

            val placeholder =
                ExpressionBox(
                    continuationWidth,
                    continuationTop,
                    continuationBottom,
                    listOf(
                        ExpressionStroke(
                            Path().apply {

                                addRoundRect(
                                    RectF(
                                        horizontalInset,
                                        continuationTop,
                                        continuationWidth -
                                                horizontalInset,
                                        continuationBottom
                                    ),
                                    corner,
                                    corner,
                                    Path.Direction.CW
                                )
                            },
                            continuationSlotSize,
                            true
                        )
                    )
                )

            /*
             * Sólo presentación.
             *
             * NO añadimos:
             *
             * - childrenBounds
             * - Sequence
             * - MathExpression
             * - slot
             *
             * Por tanto el AST continúa intacto.
             */
            ink +=
                ExpressionPlaced(
                    placeholder,
                    x,
                    0f
                )

            x +=
                continuationWidth
        }

        sequences +=
            ExpressionSequenceBounds(
                path,
                0f,
                ascent,
                x,
                descent
            )

        return ExpressionBox(
            x,
            ascent,
            descent,
            ink,
            carets,
            sequences,
            childrenBounds
        )
    }

    private fun text(value: String, size: Float, italic: Boolean = false, gap: Float = 0f): ExpressionBox {
        paint.textSize = size
        paint.typeface = Typeface.create("serif", if (italic) Typeface.ITALIC else Typeface.NORMAL)
        val fm = paint.fontMetrics
        val literal = ExpressionBox(paint.measureText(value), fm.ascent, fm.descent, listOf(ExpressionText(value, size, italic)))
        return if (gap == 0f) literal else compose(listOf(Triple(literal, gap, 0f)), literal.width + 2f * gap)
    }

    private fun node(node: MathExpression, size: Float, path: List<MathCursorStep>, index: Int): ExpressionBox {
        fun child(value: MathExpression.Sequence, slot: String, scale: Float = 1f) = sequence(value, size * scale, path + MathCursorStep(index, slot))
        fun leaf(value: String, rendered: String = value, italic: Boolean = false, gap: Float = 0f): ExpressionBox {
            val box = text(rendered, size, italic, gap)
            val widths = FloatArray(rendered.length)
            paint.getTextWidths(rendered, widths)
            val carets = mutableListOf<ExpressionCaret>()
            var advance = gap
            for (offset in 0..value.length) {
                if (offset > 0) advance += widths[offset - 1]
                if (offset > 0 && offset < value.length && Character.isHighSurrogate(value[offset - 1]) && Character.isLowSurrogate(value[offset])) continue
                carets += ExpressionCaret(MathCursor(path, index), advance,
                    box.ascent, box.descent, leafTextOffset = offset, leafLeft = gap, leafRight = box.width - gap)
            }
            return box.copy(carets = carets)
        }
        return when (node) {
            is MathExpression.Sequence -> sequence(node, size, path + MathCursorStep(index, "content"))
            is MathExpression.Number -> leaf(node.value, node.value.replace('-', '−'))
            is MathExpression.Variable -> leaf(node.name, italic = true)
            is MathExpression.Symbol -> leaf(node.value)
            is MathExpression.Operator -> leaf(node.value, node.value.replace('-', '−'), gap = size * .1f)
            is MathExpression.Relation -> leaf(node.value, gap = size * .14f)
            is MathExpression.Fraction -> fraction(child(node.numerator, "numerator", .82f), child(node.denominator, "denominator", .82f), size)
            is MathExpression.Scripts -> scripts(child(node.base, "base"), node.upper?.let { child(it, "upper", .68f) }, node.lower?.let { child(it, "lower", .68f) }, size)
            is MathExpression.Root -> root(child(node.radicand, "radicand"), node.index?.let { child(it, "index", .56f) }, size)
            is MathExpression.Group -> delimit(child(node.content, "content"), node.delimiter, size)
            is MathExpression.Function -> {
                val label = text(node.name, size)
                val function = if (node.base == null) label else scripts(label, null, child(node.base!!, "base", .68f), size)
                val argument = delimit(child(node.argument, "argument"), MathDelimiter.PARENTHESES, size)
                compose(listOf(Triple(function, 0f, 0f), Triple(argument, function.width + size * .08f, 0f)))
            }
            is MathExpression.LargeOperator ->
                largeOperator(
                    node.kind,

                    node.lower?.let {

                        largeOperatorBound(
                            content =
                                child(
                                    it,
                                    "lower",
                                    .62f
                                ),

                            prefix =
                                "^^",

                            path =
                                path +
                                        MathCursorStep(
                                            index,
                                            "lower"
                                        ),

                            size =
                                size * .62f
                        )
                    },

                    node.upper?.let {

                        largeOperatorBound(
                            content =
                                child(
                                    it,
                                    "upper",
                                    .62f
                                ),

                            prefix =
                                "^",

                            path =
                                path +
                                        MathCursorStep(
                                            index,
                                            "upper"
                                        ),

                            size =
                                size * .62f
                        )
                    },

                    size
                )
            is MathExpression.Limit -> {
                val label = text("lim", size)
                val condition = child(node.condition, "condition", .62f)
                val lower = label.descent + size * .1f - condition.ascent
                val width = max(label.width, condition.width)
                val head = compose(listOf(Triple(label, (width - label.width) / 2f, 0f), Triple(condition, (width - condition.width) / 2f, lower)), width)
                val body = child(node.body, "body")
                compose(listOf(Triple(head, 0f, 0f), Triple(body, width + size * .2f, 0f)))
            }
            is MathExpression.Derivative -> {
                val d = text("d", size * .82f, true)
                val orderTop = node.order?.let { child(it, "order", .56f) }
                val topD = if (orderTop == null) d else scripts(d, orderTop, null, size * .82f)
                val body = child(node.body, "body", .82f)
                val numerator = compose(listOf(Triple(topD, 0f, 0f), Triple(body, topD.width + size * .04f, 0f)))
                val variable = child(node.variable, "variable", .82f)
                val variablePower = if (orderTop == null) variable else scripts(variable, orderTop.copy(carets = emptyList(),
                    sequences = emptyList(), childrenBounds = emptyList()), null, size * .82f)
                val denominator = compose(listOf(Triple(d, 0f, 0f), Triple(variablePower, d.width, 0f)))
                fraction(numerator, denominator, size)
            }
            is MathExpression.Matrix -> grid(node.rows.mapIndexed { row, cells -> cells.mapIndexed { column, value -> child(value, "r${row}c$column") } }, size, false)
            is MathExpression.Cases -> grid(node.rows.mapIndexed { row, value -> listOf(child(value.expression, "expression$row"), child(value.condition, "condition$row")) }, size, true)
        }
    }

    private fun compose(children: List<Triple<ExpressionBox, Float, Float>>, width: Float? = null, ink: List<ExpressionInk> = emptyList()): ExpressionBox =
        ExpressionBox(width ?: (children.maxOfOrNull { it.second + it.first.width } ?: 0f),
            children.minOfOrNull { it.third + it.first.ascent } ?: 0f,
            children.maxOfOrNull { it.third + it.first.descent } ?: 0f,
            children.map { ExpressionPlaced(it.first, it.second, it.third) } + ink,
            children.flatMap { item -> item.first.carets.map { it.translated(item.second, item.third) } },
            children.flatMap { item -> item.first.sequences.map { it.translated(item.second, item.third) } },
            children.flatMap { item -> item.first.childrenBounds.map { it.translated(item.second, item.third) } })

    private fun fraction(top: ExpressionBox, bottom: ExpressionBox, size: Float): ExpressionBox {
        val axis = -size * .29f
        val width = max(top.width, bottom.width) + size * .25f
        val topY = axis - size * .11f - top.descent
        val bottomY = axis + size * .11f - bottom.ascent
        val line = ExpressionStroke(Path().apply { moveTo(size * .025f, axis); lineTo(width - size * .025f, axis) }, size)
        return compose(listOf(Triple(top, (width - top.width) / 2f, topY), Triple(bottom, (width - bottom.width) / 2f, bottomY)), width, listOf(line))
    }

    private fun scripts(base: ExpressionBox, upper: ExpressionBox?, lower: ExpressionBox?, size: Float): ExpressionBox {
        val items = mutableListOf(Triple(base, 0f, 0f))
        // Scripts share the base anchor, irrespective of either script's own width.
        val scriptX = base.width + size * .025f
        upper?.let { items += Triple(it, scriptX, min(-size * .48f, base.ascent - it.descent + size * .55f)) }
        lower?.let { items += Triple(it, scriptX, max(size * .28f, base.descent - it.ascent - size * .55f)) }
        return compose(items)
    }

    private fun root(body: ExpressionBox, index: ExpressionBox?, size: Float): ExpressionBox {
        val indexWidth = index?.width ?: 0f
        val lead = max(size * .68f, indexWidth + size * .22f)
        val top = min(body.ascent - size * .1f, -size * .9f)
        val bottom = max(body.descent, size * .1f)
        val rootStroke = Path().apply {
            moveTo(lead - size * .65f, -size * .15f)
            lineTo(lead - size * .51f, -size * .24f)
            lineTo(lead - size * .31f, bottom)
            lineTo(lead - size * .11f, top)
            lineTo(lead + body.width + size * .08f, top)
        }
        val children = mutableListOf(Triple(body, lead, 0f))
        index?.let { children += Triple(it, 0f, top + size * .24f - it.descent) }
        val box = compose(children, lead + body.width + size * .1f, listOf(ExpressionStroke(rootStroke, size)))
        return box.copy(ascent = min(box.ascent, top), descent = max(box.descent, bottom))
    }

    private fun delimit(body: ExpressionBox, delimiter: MathDelimiter, size: Float): ExpressionBox {
        val top = min(body.ascent, -size * .8f) - size * .07f
        val bottom = max(body.descent, size * .2f) + size * .07f
        val side = size * (if (delimiter == MathDelimiter.ABSOLUTE) .18f else .34f)
        val width = body.width + 2f * side + size * .08f
        val left = delimiterPath(delimiter, side, top, bottom, false)
        val right = delimiterPath(delimiter, side, top, bottom, true).apply { offset(width - side, 0f) }
        return compose(listOf(Triple(body, side + size * .04f, 0f)), width,
            listOf(ExpressionStroke(left, size), ExpressionStroke(right, size))).copy(ascent = top, descent = bottom)
    }

    private fun delimiterPath(delimiter: MathDelimiter, width: Float, top: Float, bottom: Float, right: Boolean): Path {
        fun x(value: Float) = if (right) width - value else value
        val middle = (top + bottom) / 2f
        return Path().apply {
            when (delimiter) {
                MathDelimiter.ABSOLUTE -> { moveTo(width / 2f, top); lineTo(width / 2f, bottom) }
                MathDelimiter.BRACKETS -> { moveTo(x(width * .88f), top); lineTo(x(width * .2f), top); lineTo(x(width * .2f), bottom); lineTo(x(width * .88f), bottom) }
                MathDelimiter.BRACES -> {
                    moveTo(x(width * .95f), top)
                    cubicTo(x(width * .25f), top, x(width * .5f), middle - (bottom - top) * .08f, x(width * .05f), middle)
                    cubicTo(x(width * .5f), middle + (bottom - top) * .08f, x(width * .25f), bottom, x(width * .95f), bottom)
                }
                MathDelimiter.PARENTHESES -> {
                    moveTo(x(width * .9f), top)
                    cubicTo(x(-width * .1f), top + (bottom - top) * .2f, x(-width * .1f), bottom - (bottom - top) * .2f, x(width * .9f), bottom)
                }
            }
        }
    }

    private fun largeOperatorBound(
        content: ExpressionBox,
        prefix: String,
        path: List<MathCursorStep>,
        size: Float
    ): ExpressionBox {

        val prefixBox =
            text(
                prefix,
                size
            )

        val result =
            compose(
                listOf(
                    Triple(
                        prefixBox,
                        0f,
                        0f
                    ),

                    Triple(
                        content,
                        prefixBox.width,
                        0f
                    )
                )
            )

        paint.textSize =
            size

        paint.typeface =
            Typeface.create(
                "serif",
                Typeface.NORMAL
            )

        val widths =
            FloatArray(
                prefix.length
            )

        paint.getTextWidths(
            prefix,
            widths
        )

        val prefixCarets =
            mutableListOf<ExpressionCaret>()

        var x =
            0f

        for (
        offset in prefix.indices
        ) {

            prefixCarets +=
                ExpressionCaret(
                    cursor =
                        MathCursor(
                            path = path,
                            offset = 0,
                            level =
                                MathCursorLevel.INTERIOR,
                            scope =
                                MathContentScope.Slot
                        ),

                    x =
                        x,

                    top =
                        result.ascent,

                    bottom =
                        result.descent,

                    prefixOffset =
                        offset
                )

            x +=
                widths[offset]
        }

        return result.copy(
            carets =
                prefixCarets +
                        result.carets
        )
    }

    private fun largeOperator(
        kind: MathLargeOperator,
        lower: ExpressionBox?,
        upper: ExpressionBox?,
        size: Float
    ): ExpressionBox {

        val symbol =
            text(
                when (
                    kind
                ) {

                    MathLargeOperator.INTEGRAL ->
                        "∫"

                    MathLargeOperator.SUM ->
                        "∑"

                    MathLargeOperator.PRODUCT ->
                        "∏"
                },

                size * 1.5f
            )
        val symbolY = -size * .29f - (symbol.ascent + symbol.descent) / 2f
        val items = mutableListOf<Triple<ExpressionBox, Float, Float>>()
        val headWidth: Float
        if (kind == MathLargeOperator.INTEGRAL) {
            headWidth = symbol.width + max(lower?.width ?: 0f, upper?.width ?: 0f) + size * .06f
            items += Triple(symbol, 0f, symbolY)
            upper?.let { items += Triple(it, symbol.width * .9f, symbolY + symbol.ascent + it.descent) }
            lower?.let { items += Triple(it, symbol.width * .65f, symbolY + symbol.descent - it.ascent * .6f) }
        } else {
            headWidth = max(symbol.width, max(lower?.width ?: 0f, upper?.width ?: 0f))
            items += Triple(symbol, (headWidth - symbol.width) / 2f, symbolY)
            upper?.let { items += Triple(it, (headWidth - it.width) / 2f, symbolY + symbol.ascent - size * .08f - it.descent) }
            lower?.let { items += Triple(it, (headWidth - it.width) / 2f, symbolY + symbol.descent + size * .08f - it.ascent) }
        }
        val head =
            compose(items)

        /*
         * El contenido matemático situado a la derecha
         * ya no forma parte del LargeOperator.
         *
         * Dejamos solamente una pequeña separación visual
         * para que el siguiente padre de la Sequence
         * aparezca junto al operador.
         */
        return head.copy(
            width =
                head.width +
                        size * .2f
        )
    }

    private fun grid(rows: List<List<ExpressionBox>>, size: Float, cases: Boolean): ExpressionBox {
        if (rows.isEmpty()) return delimit(text("", size), if (cases) MathDelimiter.BRACES else MathDelimiter.BRACKETS, size)
        val columns = rows.maxOf { it.size }
        val widths = (0 until columns).map { column -> max(size * .5f, rows.maxOf { it.getOrNull(column)?.width ?: 0f }) }
        val rowAscent = rows.map { row -> min(-size * .8f, row.minOfOrNull { it.ascent } ?: 0f) }
        val rowDescent = rows.map { row -> max(size * .2f, row.maxOfOrNull { it.descent } ?: 0f) }
        val gap = size * .5f
        val height = rowAscent.indices.sumOf { (rowDescent[it] - rowAscent[it]).toDouble() }.toFloat() + size * .3f * (rows.size - 1)
        val top = -height / 2f - size * .29f
        val side = size * .45f
        val contentWidth = widths.sum() + gap * max(0, columns - 1)
        var y = top
        val children = mutableListOf<Triple<ExpressionBox, Float, Float>>()
        rows.forEachIndexed { row, cells ->
            val baseline = y - rowAscent[row]
            var x = side
            cells.forEachIndexed { column, cell ->
                children += Triple(cell, x + if (cases) 0f else (widths[column] - cell.width) / 2f, baseline)
                x += widths[column] + gap
            }
            y = baseline + rowDescent[row] + size * .3f
        }
        val width = contentWidth + side * if (cases) 1.2f else 2f
        val bottom = top + height
        val left = delimiterPath(if (cases) MathDelimiter.BRACES else MathDelimiter.BRACKETS, side * .8f, top - size * .07f, bottom + size * .07f, false)
        val ink = mutableListOf<ExpressionInk>(ExpressionStroke(left, size))
        if (!cases) ink += ExpressionStroke(delimiterPath(MathDelimiter.BRACKETS, side * .8f, top - size * .07f, bottom + size * .07f, true).apply { offset(width - side * .8f, 0f) }, size)
        return compose(children, width, ink).copy(ascent = top - size * .07f, descent = bottom + size * .07f)
    }
}

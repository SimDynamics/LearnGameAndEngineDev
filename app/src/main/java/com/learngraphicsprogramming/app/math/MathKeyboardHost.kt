package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.drawable.ColorDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.EditText
import android.widget.PopupWindow
import android.widget.ScrollView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsAnimationCompat
import androidx.core.view.WindowInsetsCompat
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.R
import java.lang.ref.WeakReference
import java.util.WeakHashMap

/**
 * A single non-focusable toolbar follows the resized window
 * immediately above the real IME.
 *
 * Measures its own height and asks the existing ScrollView
 * to reveal the input plus that space.
 */
class MathKeyboardHost private constructor(
    private val activity: Activity
) {

    private val root =
        activity.findViewById<View>(
            android.R.id.content
        )

    private val insetsView = activity.window.decorView

    private var keyboard:
            MathKeyboardView? = null

    private var activeInput:
            View? = null

    /*
     * Guarda el último campo al que ya se le hizo
     * el ajuste especial para mostrar también Comprobar.
     *
     * Mientras conserve el foco y el viewport, no volvemos a perseguirlo.
     */
    private var lastSpeciallyRevealedInput:
            View? = null

    private data class RevealViewport(
        val windowWidth: Int,
        val windowHeight: Int,
        val scrollWidth: Int?,
        val scrollHeight: Int?,
        val toolbarHeight: Int
    )

    private var lastRevealedViewport: RevealViewport? = null

    private val runningImeAnimations =
        mutableSetOf<WindowInsetsAnimationCompat>()

    private var popup:
            PopupWindow? = null

    private var originalParent:
            ViewGroup? = null

    private var originalIndex =
        0

    private var originalParams:
            ViewGroup.LayoutParams? = null

    private var previousGeometry =
        ""

    private val lessonHeader =
        activity.findViewById<View>(R.id.lessonHeader)

    private var normalHeaderHeight = 0

    private var imeVisible = false

    /** Share the existing IME/toolbar observation with lesson overlays. */
    val isKeyboardVisible: Boolean
        get() = imeVisible || runningImeAnimations.isNotEmpty() || popup?.isShowing == true

    private var paddedScroll:
            ScrollView? = null

    private var originalScrollBottomPadding =
        0

    private var compactEditor: MathExpressionEditor? = null
    private var originalEditorPadding: Pair<Int, Int>? = null

    private fun setEditorViewport(editor: MathExpressionEditor?, height: Int?) {
        if (compactEditor !== editor || height == null) {
            compactEditor?.let { previous ->
                originalEditorPadding?.let { (top, bottom) ->
                    previous.setPadding(previous.paddingLeft, top, previous.paddingRight, bottom)
                }
                previous.viewportHeightLimit = null
            }
            compactEditor = null
            originalEditorPadding = null
        }
        if (editor != null && height != null) {
            if (compactEditor == null) {
                compactEditor = editor
                originalEditorPadding = editor.paddingTop to editor.paddingBottom
            }
            if (editor.paddingTop != 0 || editor.paddingBottom != 0) {
                editor.setPadding(editor.paddingLeft, 0, editor.paddingRight, 0)
            }
            editor.viewportHeightLimit = height
        }
    }

    internal fun refreshPosition() {

        root.post {
            update()
        }
    }

    internal fun onConfigurationChanged() {
        // Configuration arrives before the resized window's first layout. Use the
        // last real IME state so the header cannot collapse lessonScroll to zero.
        updateLessonHeader()
        refreshPosition()
    }

    private fun observeImeInsets(insets: WindowInsetsCompat?) {
        if (insets != null) {
            imeVisible = insets.isVisible(WindowInsetsCompat.Type.ime())
        }
        updateLessonHeader()
    }

    private fun updateLessonHeader() {
        // Final IME insets are dispatched before the animation/layout finishes.
        // Keep the header out of the resize during both opening and closing.
        val imeOccupiesViewport = imeVisible || runningImeAnimations.isNotEmpty()
        val header = lessonHeader ?: return
        if ((header.visibility == View.VISIBLE || normalHeaderHeight == 0) && header.measuredHeight > 0) {
            normalHeaderHeight = header.measuredHeight
        }
        val frame = Rect().also(activity.window.decorView::getWindowVisibleDisplayFrame)
        val content = header.parent as? View ?: root
        val focusedInput = root.findFocus()?.takeIf {
            (it is EditText || it is MathExpressionEditor) && it.hasFocus() &&
                it.tag == MathInputFocusOrder.GESTURE_OWNER_TAG
        }
        // A portrait configuration can precede the content's taller layout. Keep
        // the existing focused ancestor nonzero until the IME animation and native
        // layout have finished, rather than restoring into an intermediate resize.
        val waitForHeaderSpace = header.visibility == View.GONE && focusedInput != null &&
            (runningImeAnimations.isNotEmpty() || root.isLayoutRequested || content.isLayoutRequested ||
                content.width != frame.width() || minOf(frame.height(), content.height) <= normalHeaderHeight)
        val visibility = if (
            (activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
                imeOccupiesViewport) || waitForHeaderSpace
        ) View.GONE else View.VISIBLE
        if (header.visibility != visibility) {
            header.visibility = visibility
        }
    }

    private fun restoreScrollPadding() {

        paddedScroll?.let { scroll ->

            scroll.setPadding(
                scroll.paddingLeft,
                scroll.paddingTop,
                scroll.paddingRight,
                originalScrollBottomPadding
            )
        }

        paddedScroll =
            null
    }

    private fun reserveToolbarSpace(
        scroll: ScrollView?,
        height: Int
    ) {

        if (
            paddedScroll !== scroll
        ) {

            restoreScrollPadding()

            if (
                scroll != null
            ) {

                paddedScroll =
                    scroll

                originalScrollBottomPadding =
                    scroll.paddingBottom
            }
        }

        scroll?.let {

            val bottom =
                originalScrollBottomPadding +
                        height

            if (
                it.paddingBottom !=
                bottom
            ) {

                it.setPadding(
                    it.paddingLeft,
                    it.paddingTop,
                    it.paddingRight,
                    bottom
                )
            }
        }
    }

    init {

        root.viewTreeObserver
            .addOnGlobalLayoutListener {

                update()
            }

        /*
         * Insets may arrive after the resize/layout
         * event on real IMEs.
         */
        ViewCompat.setOnApplyWindowInsetsListener(
            insetsView
        ) { view, insets ->

            // Apply before measure/layout, not in the posted popup geometry pass.
            // A zero-height focused ancestor causes Android to clear editor focus.
            observeImeInsets(insets)

            root.post {
                update()
            }

            // Decor applies legacy fit-system-windows sizing before dispatching to
            // AppCompat's content. Observe first, then preserve that default sizing.
            ViewCompat.onApplyWindowInsets(view, insets)
        }

        ViewCompat.setWindowInsetsAnimationCallback(
            insetsView,
            object :
                WindowInsetsAnimationCompat.Callback(
                    DISPATCH_MODE_CONTINUE_ON_SUBTREE
                ) {

                override fun onPrepare(animation: WindowInsetsAnimationCompat) {
                    super.onPrepare(animation)
                    if (animation.typeMask and WindowInsetsCompat.Type.ime() != 0) {
                        runningImeAnimations.add(animation)
                        updateLessonHeader()
                    }
                }

                override fun onProgress(
                    insets: WindowInsetsCompat,
                    runningAnimations:
                    MutableList<WindowInsetsAnimationCompat>
                ): WindowInsetsCompat {

                    observeImeInsets(insets)

                    root.post {
                        update()
                    }

                    return insets
                }

                override fun onEnd(
                    animation:
                    WindowInsetsAnimationCompat
                ) {

                    runningImeAnimations.remove(animation)

                    observeImeInsets(ViewCompat.getRootWindowInsets(root))

                    super.onEnd(
                        animation
                    )

                    /*
                     * Android puede reajustar automáticamente
                     * el ScrollView mientras abre el IME.
                     *
                     * Cuando termina la animación hacemos
                     * una pasada final.
                     */
                    root.post {

                        previousGeometry =
                            ""

                        update()
                    }
                }
            }
        )

        ViewCompat.requestApplyInsets(
            insetsView
        )

        root.viewTreeObserver
            .addOnGlobalFocusChangeListener { _, _ ->

                root.post {

                    /*
                     * El foco pudo volver a cambiar antes
                     * de que este post se ejecute.
                     *
                     * Consultamos siempre el foco real actual.
                     */
                    val input =
                        root.findFocus()
                            ?.takeIf {

                                (
                                        it is EditText ||
                                                it is MathExpressionEditor
                                        ) &&
                                        it.isEnabled &&
                                        it.tag ==
                                        MathInputFocusOrder
                                            .GESTURE_OWNER_TAG &&
                                        it.hasFocus()
                            }

                    if (
                        input == null
                    ) {

                        lastSpeciallyRevealedInput =
                            null

                        dismissCurrent()

                    } else if (
                        activeInput !==
                        input
                    ) {

                        /*
                         * Cambiamos de campo.
                         *
                         * Si el nuevo campo es el último
                         * del puzzle, permitimos nuevamente
                         * un único ajuste especial.
                         */
                        lastSpeciallyRevealedInput =
                            null

                        val bar =
                            bindings[input]
                                ?.get()
                                ?: MathKeyboardView(
                                    activity,
                                    AppearanceSettingsRepository
                                        .getRouteAccentColor(
                                            activity,
                                            AppearanceSettingsRepository
                                                .ROUTE_MATH_PHYSICS
                                        )
                                )

                        if (
                            input is EditText
                        ) {

                            bar.bind(
                                input,
                                "NUMERIC"
                            )

                        } else if (
                            input is MathExpressionEditor
                        ) {

                            bar.bind(
                                input,
                                input.keyboardShortcuts
                            )
                        }

                    } else {

                        update()
                    }
                }
            }
    }

    internal fun show(
        bar: MathKeyboardView,
        input: View
    ) {

        remember(
            input,
            bar
        )

        if (
            keyboard !== bar
        ) {

            dismissCurrent()

            keyboard =
                bar

            originalParent =
                bar.parent as? ViewGroup

            originalIndex =
                originalParent
                    ?.indexOfChild(
                        bar
                    )
                    ?: 0

            originalParams =
                bar.layoutParams

            originalParent
                ?.removeView(
                    bar
                )

            popup =
                PopupWindow(
                    bar,
                    1,
                    1,
                    false
                ).apply {

                    isOutsideTouchable =
                        false

                    inputMethodMode =
                        PopupWindow
                            .INPUT_METHOD_NOT_NEEDED

                    softInputMode =
                        WindowManager
                            .LayoutParams
                            .SOFT_INPUT_ADJUST_NOTHING

                    setBackgroundDrawable(
                        ColorDrawable(
                            Color.rgb(
                                24,
                                26,
                                31
                            )
                        )
                    )

                    elevation =
                        MathWidgetUi
                            .dp(
                                activity,
                                8
                            )
                            .toFloat()
                }
        }

        activeInput =
            input

        previousGeometry =
            ""

        root.post {
            update()
        }
    }

    internal fun hide(
        bar: MathKeyboardView
    ) {

        if (
            keyboard === bar
        ) {

            dismissCurrent()
        }
    }

    private fun findPuzzleRoot(
        input: View
    ): ViewGroup? {

        var current:
                View? = input

        while (
            current != null
        ) {

            if (
                current.tag ==
                MathPuzzleRenderer
                    .INTERACTION_TAG
            ) {

                return current as? ViewGroup
            }

            current =
                current.parent as? View
        }

        return null
    }

    private fun findPuzzleCheckButton(
        input: View
    ): View? =

        findPuzzleRoot(
            input
        )
            ?.findViewWithTag(
                MathPuzzleRenderer
                    .CHECK_ACTION_TAG
            )

    /*
     * Solo devolvemos true cuando el input pertenece
     * realmente a un puzzle y además es su último
     * campo editable.
     *
     * Si no pertenece a un puzzle dejamos que Android
     * conserve su comportamiento normal.
     */
    private fun isLastEditableInputInPuzzle(
        input: View
    ): Boolean {

        val puzzle =
            findPuzzleRoot(
                input
            )
                ?: return false

        val inputs =
            mutableListOf<View>()

        fun collect(
            view: View
        ) {

            if (
                view.visibility !=
                View.VISIBLE
            ) {

                return
            }

            if (
                view.isEnabled &&
                (
                        view is EditText ||
                                view is MathExpressionEditor
                        )
            ) {

                inputs +=
                    view
            }

            if (
                view is ViewGroup
            ) {

                for (
                index in
                0 until view.childCount
                ) {

                    collect(
                        view.getChildAt(
                            index
                        )
                    )
                }
            }
        }

        collect(
            puzzle
        )

        return inputs
            .lastOrNull() ===
                input
    }

    private fun update() {

        observeImeInsets(ViewCompat.getRootWindowInsets(root))

        val bar =
            keyboard
                ?: return

        val input =
            activeInput
                ?: return

        if (
            activity.isFinishing ||
            activity.isDestroyed ||
            !input.isAttachedToWindow ||
            !input.isEnabled ||
            !input.hasFocus()
        ) {

            dismissCurrent()

            return
        }

        val frame =
            Rect()

        root.getWindowVisibleDisplayFrame(
            frame
        )

        if (
            !imeVisible ||
            !activity.hasWindowFocus() ||
            frame.width() <= 0
        ) {

            bar.setCompactHeight(null)
            setEditorViewport(null, null)

            popup?.dismiss()

            restoreScrollPadding()

            previousGeometry =
                ""

            return
        }

        // ============================================================
        // MEDIR BARRA MATEMÁTICA
        // ============================================================

        bar.visibility =
            View.VISIBLE

        val compact =
            activity.resources.configuration.orientation ==
                    Configuration.ORIENTATION_LANDSCAPE

        val toolbarHeightLimit =
            if (compact) {
                (frame.height() / 2)
                    .coerceAtLeast(1)
            } else {
                frame.height()
            }

        val compactKeyHeight =
            if (compact) {
                bar.responsiveCompactHeight(
                    toolbarHeightLimit
                )
            } else {
                null
            }

        bar.setCompactHeight(
            compactKeyHeight
        )

        val measuredBarHeight =
            if (compact) {
                compactKeyHeight
                    ?: toolbarHeightLimit
            } else {
                toolbarHeightLimit
            }

        bar.measure(
            View.MeasureSpec
                .makeMeasureSpec(
                    frame.width(),
                    View.MeasureSpec.EXACTLY
                ),
            View.MeasureSpec
                .makeMeasureSpec(
                    measuredBarHeight,
                    if (compact) {
                        View.MeasureSpec.EXACTLY
                    } else {
                        View.MeasureSpec.AT_MOST
                    }
                )
        )

        val barHeight =
            if (compact) {
                measuredBarHeight
            } else {
                bar.measuredHeight
                    .coerceAtLeast(
                        MathWidgetUi.dp(
                            activity,
                            48
                        )
                    )
            }

        setEditorViewport(input as? MathExpressionEditor, (frame.height() - barHeight).takeIf { compact })

        val margin = if (compact) 0 else MathWidgetUi.dp(
                activity,
                8
            )

        // ============================================================
        // ENCONTRAR SCROLLVIEW
        // ============================================================

        val scroll =
            generateSequence(
                input.parent
            ) {
                it.parent
            }
                .filterIsInstance<ScrollView>()
                .firstOrNull()

        reserveToolbarSpace(
            scroll.takeIf {
                input is MathExpressionEditor
            },
            barHeight + margin
        )

        // ============================================================
        // POSICIÓN DEL INPUT
        // ============================================================

        val inputLocation =
            IntArray(2)

        input.getLocationOnScreen(
            inputLocation
        )

        val inputTop =
            inputLocation[1]

        val inputBottom =
            inputTop +
                    input.height

        // ============================================================
        // BOTÓN COMPROBAR DEL MISMO PUZZLE
        // ============================================================

        val lastInput =
            isLastEditableInputInPuzzle(
                input
            )

        val checkButton =
            if (
                lastInput
            ) {

                findPuzzleCheckButton(
                    input
                )
                    ?.takeIf {

                        it.visibility ==
                                View.VISIBLE &&
                                it.isAttachedToWindow
                    }

            } else {

                null
            }

        val checkLocation =
            IntArray(2)

        val checkBottom =
            if (
                checkButton != null
            ) {

                checkButton
                    .getLocationOnScreen(
                        checkLocation
                    )

                checkLocation[1] +
                        checkButton.height

            } else {

                inputBottom
            }

        val contentBottom =
            maxOf(
                inputBottom,
                checkBottom
            )

        // ============================================================
        // POSICIÓN DE LA BARRA MATEMÁTICA
        // ============================================================

        val barY =
            (
                    frame.bottom -
                            barHeight
                    )
                .coerceAtLeast(
                    frame.top
                )

        val geometry =
            "${frame.flattenToString()}:" +
                    "$barHeight:" +
                    "$barY"

        popup?.let { window ->

            if (
                !window.isShowing
            ) {

                window.width =
                    frame.width()

                window.height =
                    barHeight

                window.showAtLocation(
                    root,
                    Gravity.TOP or
                            Gravity.START,
                    frame.left,
                    barY
                )

                previousGeometry =
                    geometry

            } else if (
                geometry !=
                previousGeometry
            ) {

                window.update(
                    frame.left,
                    barY,
                    frame.width(),
                    barHeight
                )

                previousGeometry =
                    geometry
            }
        }

        // Normal geometry reveals only the final puzzle input and its check action.
        // The compact viewport must also reveal whichever math input retains focus.
        if (
            !lastInput && !compact
        ) {

            return
        }

        /*
         * Mientras el dedo está interactuando con
         * un slot estructural todavía no movemos
         * automáticamente la página.
         *
         * ACTION_UP llamará nuevamente a update().
         */
        if (
            (
                    input as?
                            MathExpressionEditor
                    )
                ?.isSelectingSlot ==
            true
        ) {

            return
        }

        /*
         * El campo recibe el ajuste especial
         * solamente una vez por foco y tamaño del viewport.
         *
         * Después de esto:
         *
         * - escribir no mueve la página
         * - mover el cursor no mueve la página
         * - hacer scroll manual no vuelve a llevarnos
         *   automáticamente al campo
         */
        // IME visibility becomes true before its opening animation and resize finish.
        // Keep positioning the popup above, but consume the single reveal only after
        // the final viewport and pending toolbar-padding/input layouts have settled.
        if (runningImeAnimations.isNotEmpty() || root.isLayoutRequested ||
            scroll?.isLayoutRequested == true || input.isLayoutRequested) {
            return
        }

        // Rotation and real IME resizes need a new placement. Text, cursor changes
        // and manual scrolling do not change these viewport dimensions.
        val viewport = RevealViewport(
            frame.width(), frame.height(), scroll?.width, scroll?.height, barHeight
        )
        if (lastSpeciallyRevealedInput === input && lastRevealedViewport == viewport) {
            return
        }

        // ============================================================
        // SCROLL ESPECIAL DEL ÚLTIMO CAMPO
        // ============================================================

        if (
            scroll != null
        ) {

            val scrollBounds =
                Rect()

            if (
                !scroll.getGlobalVisibleRect(
                    scrollBounds
                )
            ) {

                return
            }

            val visibleBottom =
                barY -
                        margin

            val visibleTop =
                scrollBounds.top +
                        margin

            val availableHeight =
                visibleBottom -
                        visibleTop

            val contentHeight =
                contentBottom -
                        inputTop

            val caret = when (input) {
                is MathExpressionEditor -> input.let { editor ->

                        editor
                            .expressionLayout
                            .caretBounds(
                                editor.model.cursor
                            )
                            ?.apply {

                                offset(
                                    0f,
                                    inputTop +
                                            editor
                                                .expressionBaseline
                                )
                            }
                }
                is EditText -> if (compact) input.layout?.let { layout ->
                    val line = layout.getLineForOffset(input.selectionEnd.coerceIn(0, input.text.length))
                    RectF(0f, layout.getLineTop(line).toFloat(), 0f, layout.getLineBottom(line).toFloat()).apply {
                        offset(0f, (inputTop + input.compoundPaddingTop - input.scrollY).toFloat())
                    }
                } else null
                else -> null
            }

            val delta =
                when {

                    /*
                     * Una matriz o fracción alta puede
                     * exceder el viewport.
                     *
                     * En ese caso seguimos el slot activo
                     * durante este único ajuste inicial.
                     */
                    input.height >
                            availableHeight &&
                            caret != null ->

                        when {

                            caret.height() >=
                                    availableHeight ->

                                (
                                        caret.top -
                                                visibleTop
                                        )
                                    .toInt()

                            caret.bottom >
                                    visibleBottom ->

                                kotlin.math
                                    .ceil(
                                        caret.bottom -
                                                visibleBottom
                                    )
                                    .toInt()

                            caret.top <
                                    visibleTop ->

                                kotlin.math
                                    .floor(
                                        caret.top -
                                                visibleTop
                                    )
                                    .toInt()

                            else ->
                                0
                        }

                    /*
                     * Un editor completo es más alto que
                     * todo el área disponible.
                     */
                    input.height >
                            availableHeight ->

                        inputTop -
                                visibleTop

                    /*
                     * Si campo + Comprobar caben juntos,
                     * desplazamos hasta mostrar Comprobar.
                     */
                    contentHeight <=
                            availableHeight &&
                            contentBottom >
                            visibleBottom ->

                        contentBottom -
                                visibleBottom

                    /*
                     * El campo quedó demasiado arriba.
                     */
                    inputTop <
                            visibleTop ->

                        inputTop -
                                visibleTop

                    /*
                     * Campo + Comprobar no caben juntos.
                     *
                     * En ese caso priorizamos el campo.
                     */
                    contentHeight >
                            availableHeight &&
                            inputBottom >
                            visibleBottom ->

                        inputBottom -
                                visibleBottom

                    else ->
                        0
                }

            /*
             * A partir de aquí este campo ya recibió
             * su única colocación automática.
             *
             * Incluso delta == 0 significa que ya estaba
             * correctamente visible.
             */
            lastSpeciallyRevealedInput =
                input

            lastRevealedViewport = viewport

            if (
                delta != 0
            ) {

                val previousScrollY =
                    scroll.scrollY

                // Android's focus/resize reveal may still own a pending OverScroller target.
                // scrollBy only jumps the View, which that animation then overwrites. The
                // native smooth-scroll operation replaces or aborts the pending scroll with
                // this single target, including Comprobar, without revealing again on navigation.
                scroll.smoothScrollBy(
                    0,
                    delta
                )

                /*
                 * Recalculamos la geometría del popup,
                 * pero update() ya NO volverá a hacer
                 * scroll porque el campo quedó marcado.
                 */
                if (
                    scroll.scrollY !=
                    previousScrollY
                ) {

                    root.post {
                        update()
                    }
                }
            }

        } else {

            /*
             * Caso raro sin ScrollView.
             *
             * También se ejecuta una sola vez
             * por enfoque.
             */
            input.requestRectangleOnScreen(
                Rect(
                    0,
                    -margin,
                    input.width,
                    input.height +
                            barHeight +
                            margin
                ),
                true
            )

            lastSpeciallyRevealedInput =
                input

            lastRevealedViewport = viewport
        }
    }

    private fun dismissCurrent() {

        setEditorViewport(null, null)

        restoreScrollPadding()

        popup?.dismiss()

        val bar =
            keyboard

        if (
            bar != null
        ) {

            bar.setCompactHeight(null)

            (
                    bar.parent as?
                            ViewGroup
                    )
                ?.removeView(
                    bar
                )

            bar.visibility =
                View.GONE

            originalParent
                ?.let { parent ->

                    val index =
                        originalIndex
                            .coerceAtMost(
                                parent.childCount
                            )

                    val params =
                        originalParams

                    if (
                        params != null
                    ) {

                        parent.addView(
                            bar,
                            index,
                            params
                        )

                    } else {

                        parent.addView(
                            bar,
                            index
                        )
                    }
                }
        }

        keyboard =
            null

        activeInput =
            null

        lastSpeciallyRevealedInput =
            null

        lastRevealedViewport = null

        popup =
            null

        originalParent =
            null

        originalParams =
            null

        previousGeometry =
            ""
    }

    fun close() {

        dismissCurrent()

        lessonHeader?.visibility = View.VISIBLE

        hosts.remove(
            activity
        )
    }

    companion object {

        private val hosts =
            WeakHashMap<
                    Activity,
                    MathKeyboardHost
                    >()

        private val bindings =
            WeakHashMap<
                    View,
                    WeakReference<MathKeyboardView>
                    >()

        internal fun remember(
            input: View,
            bar: MathKeyboardView
        ) {

            bindings[input] =
                WeakReference(
                    bar
                )
        }

        fun install(
            activity: Activity
        ): MathKeyboardHost =

            hosts.getOrPut(
                activity
            ) {
                MathKeyboardHost(
                    activity
                )
            }

        internal fun current(
            activity: Activity
        ): MathKeyboardHost? =

            hosts[
                activity
            ]
    }
}

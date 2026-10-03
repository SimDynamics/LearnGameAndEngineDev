package com.learngraphicsprogramming.app.math

import android.app.Activity
import android.graphics.Color
import android.graphics.Rect
import android.graphics.drawable.ColorDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.EditText
import android.widget.PopupWindow
import android.widget.ScrollView
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.R
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsAnimationCompat
import java.util.WeakHashMap
import java.lang.ref.WeakReference

/** A single non-focusable toolbar follows the resized window immediately above the real IME.
 * Measures its own height and asks the existing ScrollView to reveal the input plus that space.
 */
class MathKeyboardHost private constructor(private val activity: Activity) {
    private val root = activity.findViewById<View>(android.R.id.content)
    private var keyboard: MathKeyboardView? = null
    private var activeInput: View? = null
    private var popup: PopupWindow? = null
    private var originalParent: ViewGroup? = null
    private var originalIndex = 0
    private var originalParams: ViewGroup.LayoutParams? = null
    private var previousGeometry = ""
    private var compactHeader: View? = null
    private var originalHeaderVisibility = View.VISIBLE
    private var paddedScroll: ScrollView? = null
    private var originalScrollBottomPadding = 0

    internal fun refreshPosition() { root.post { update() } }

    private fun restoreScrollPadding() {
        paddedScroll?.let { scroll ->
            scroll.setPadding(scroll.paddingLeft, scroll.paddingTop, scroll.paddingRight, originalScrollBottomPadding)
        }
        paddedScroll = null
    }

    private fun reserveToolbarSpace(scroll: ScrollView?, height: Int) {
        if (paddedScroll !== scroll) {
            restoreScrollPadding()
            if (scroll != null) { paddedScroll = scroll; originalScrollBottomPadding = scroll.paddingBottom }
        }
        scroll?.let {
            val bottom = originalScrollBottomPadding + height
            if (it.paddingBottom != bottom) it.setPadding(it.paddingLeft, it.paddingTop, it.paddingRight, bottom)
        }
    }

    init {
        root.viewTreeObserver.addOnGlobalLayoutListener { update() }
        // Insets may arrive after the resize/layout event on real IMEs. Listen to both sources.
        ViewCompat.setOnApplyWindowInsetsListener(root) { _, insets ->
            root.post { update() }
            insets
        }
        ViewCompat.setWindowInsetsAnimationCallback(
            root,
            object : WindowInsetsAnimationCompat.Callback(
                DISPATCH_MODE_CONTINUE_ON_SUBTREE
            ) {
                override fun onProgress(
                    insets: WindowInsetsCompat,
                    runningAnimations: MutableList<WindowInsetsAnimationCompat>
                ): WindowInsetsCompat {
                    root.post { update() }
                    return insets
                }

                override fun onEnd(animation: WindowInsetsAnimationCompat) {
                    super.onEnd(animation)

                    // Android puede reajustar automáticamente el ScrollView
                    // mientras abre el IME. Cuando termina la animación,
                    // hacemos una pasada definitiva.
                    root.post {
                        previousGeometry = ""
                        update()
                    }
                }
            }
        )
        ViewCompat.requestApplyInsets(root)
        root.viewTreeObserver.addOnGlobalFocusChangeListener { _, _ ->
            root.post {
                // Focus can move again before this callback runs; never dismiss the latest binding
                // because an earlier input has already lost focus.
                val input = root.findFocus()?.takeIf {
                    (it is EditText || it is MathExpressionEditor) && it.isEnabled && it.tag == MathInputFocusOrder.GESTURE_OWNER_TAG && it.hasFocus()
                }
                if (input == null) dismissCurrent()
                else if (activeInput !== input) {
                    // Graph and number-line coordinate editors use the same toolbar infrastructure.
                    val bar = bindings[input]?.get() ?: MathKeyboardView(activity, AppearanceSettingsRepository.getRouteAccentColor(activity,
                        AppearanceSettingsRepository.ROUTE_MATH_PHYSICS))
                    if (input is EditText) bar.bind(input, "NUMERIC")
                    else if (input is MathExpressionEditor) bar.bind(input, input.keyboardShortcuts)
                } else update()
            }
        }
    }

    internal fun show(bar: MathKeyboardView, input: View) {
        remember(input, bar)
        if (keyboard !== bar) {
            dismissCurrent()
            keyboard = bar
            originalParent = bar.parent as? ViewGroup
            originalIndex = originalParent?.indexOfChild(bar) ?: 0
            originalParams = bar.layoutParams
            originalParent?.removeView(bar)
            popup = PopupWindow(bar, 1, 1, false).apply {
                isOutsideTouchable = false
                inputMethodMode = PopupWindow.INPUT_METHOD_NOT_NEEDED
                softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                setBackgroundDrawable(ColorDrawable(Color.rgb(24, 26, 31)))
                elevation = MathWidgetUi.dp(activity, 8).toFloat()
            }
        }
        activeInput = input
        previousGeometry = ""
        root.post { update() }
    }

    internal fun hide(bar: MathKeyboardView) {
        if (keyboard === bar) dismissCurrent()
    }

    private fun findPuzzleCheckButton(input: View): View? {
        var current: View? = input

        while (current != null) {
            if (current.tag == MathPuzzleRenderer.INTERACTION_TAG) {
                return current.findViewWithTag(MathPuzzleRenderer.CHECK_ACTION_TAG)
            }

            current = current.parent as? View
        }

        return null
    }

    private fun update() {
        val bar = keyboard ?: return
        val input = activeInput ?: return

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

        val insets = ViewCompat.getRootWindowInsets(root)

        val imeVisible =
            insets?.isVisible(WindowInsetsCompat.Type.ime()) == true

        val frame = Rect()
        root.getWindowVisibleDisplayFrame(frame)

        if (
            !imeVisible ||
            !activity.hasWindowFocus() ||
            frame.width() <= 0
        ) {
            popup?.dismiss()
            restoreHeader()
            restoreScrollPadding()
            previousGeometry = ""
            return
        }

        // ============================================================
        // MEDIR BARRA MATEMÁTICA
        // ============================================================

        bar.visibility = View.VISIBLE

        bar.measure(
            View.MeasureSpec.makeMeasureSpec(
                frame.width(),
                View.MeasureSpec.EXACTLY
            ),
            View.MeasureSpec.makeMeasureSpec(
                frame.height(),
                View.MeasureSpec.AT_MOST
            )
        )

        val barHeight =
            bar.measuredHeight.coerceAtLeast(
                MathWidgetUi.dp(activity, 48)
            )

        val margin = MathWidgetUi.dp(activity, 8)

        // ============================================================
        // ENCONTRAR SCROLLVIEW
        // ============================================================

        val scroll =
            generateSequence(input.parent) { it.parent }
                .filterIsInstance<ScrollView>()
                .firstOrNull()
        reserveToolbarSpace(scroll.takeIf { input is MathExpressionEditor }, barHeight + margin)

        // ============================================================
        // LANDSCAPE / ESPACIO MUY PEQUEÑO
        // ============================================================

        if (
            scroll != null &&
            compactHeader == null &&
            scroll.height <
            input.height + barHeight + margin * 2
        ) {
            (
                    activity
                        .findViewById<View>(R.id.textTopLesson)
                        ?.parent
                        ?.parent as? View
                    )?.let { header ->

                    if (header.visibility == View.VISIBLE) {
                        compactHeader = header
                        originalHeaderVisibility = header.visibility
                        header.visibility = View.GONE

                        root.post { update() }
                        return
                    }
                }
        }

        // ============================================================
        // POSICIÓN DEL INPUT
        // ============================================================

        val inputLocation = IntArray(2)
        input.getLocationOnScreen(inputLocation)

        val inputTop = inputLocation[1]
        val inputBottom = inputTop + input.height

        // ============================================================
        // BOTÓN COMPROBAR DEL MISMO PUZZLE
        // ============================================================

        val checkButton =
            findPuzzleCheckButton(input)
                ?.takeIf {
                    it.visibility == View.VISIBLE &&
                            it.isAttachedToWindow
                }

        val checkLocation = IntArray(2)

        val checkBottom =
            if (checkButton != null) {
                checkButton.getLocationOnScreen(checkLocation)

                checkLocation[1] + checkButton.height
            } else {
                inputBottom
            }

        // Queremos que TODO desde el input hasta Comprobar
        // quede visible.
        val contentBottom =
            maxOf(
                inputBottom,
                checkBottom
            )

        // ============================================================
        // POSICIÓN DE LA BARRA MATEMÁTICA
        // ============================================================

        val barY =
            (frame.bottom - barHeight)
                .coerceAtLeast(frame.top)

        // La geometría solo se usa para evitar actualizar
        // innecesariamente el PopupWindow.
        //
        // IMPORTANTE:
        // YA NO usamos esto para saltarnos el cálculo de scroll.
        val geometry =
            "${frame.flattenToString()}:" +
                    "$barHeight:" +
                    "$barY"

        popup?.let { window ->

            if (!window.isShowing) {
                window.width = frame.width()
                window.height = barHeight

                window.showAtLocation(
                    root,
                    Gravity.TOP or Gravity.START,
                    frame.left,
                    barY
                )

                previousGeometry = geometry

            } else if (geometry != previousGeometry) {

                window.update(
                    frame.left,
                    barY,
                    frame.width(),
                    barHeight
                )

                previousGeometry = geometry
            }
        }

        // ============================================================
        // SCROLL REAL
        // ============================================================

        if (scroll != null) {

            // A touch picks a slot in the geometry at DOWN. Reposition only after
            // the gesture ends so lower controls cannot move under the finger.
            if ((input as? MathExpressionEditor)?.isSelectingSlot == true) return

            val scrollBounds = Rect()

            if (!scroll.getGlobalVisibleRect(scrollBounds)) {
                return
            }

            // Todo debe quedar por encima de nuestra barra flotante.
            val visibleBottom =
                barY - margin

            val visibleTop =
                scrollBounds.top + margin

            val availableHeight =
                visibleBottom - visibleTop

            val contentHeight =
                contentBottom - inputTop

            val caret = (input as? MathExpressionEditor)?.let { editor ->
                editor.expressionLayout.caretBounds(editor.model.cursor)?.apply {
                    offset(0f, inputTop + editor.expressionBaseline)
                }
            }

            val delta = when {

                // Una matriz o fracción alta puede exceder el viewport. En ese
                // caso seguimos el slot activo para poder editar sus filas inferiores.
                input.height >= availableHeight && caret != null -> when {
                    caret.height() >= availableHeight -> (caret.top - visibleTop).toInt()
                    caret.bottom > visibleBottom -> kotlin.math.ceil(caret.bottom - visibleBottom).toInt()
                    caret.top < visibleTop -> kotlin.math.floor(caret.top - visibleTop).toInt()
                    else -> 0
                }

                // Un editor más alto que el área disponible no puede mostrar
                // ambos extremos. Alinearlo arriba evita alternar entre ellos.
                input.height >= availableHeight ->

                    inputTop - visibleTop

                // Si input + Comprobar caben juntos,
                // desplazamos hasta hacer visible Comprobar.
                contentHeight <= availableHeight &&
                        contentBottom > visibleBottom ->

                    contentBottom - visibleBottom

                // El input quedó demasiado arriba.
                inputTop < visibleTop ->

                    inputTop - visibleTop

                // En una pantalla extremadamente pequeña no caben
                // input + Comprobar simultáneamente.
                // En ese caso priorizamos el input.
                contentHeight > availableHeight &&
                        inputBottom > visibleBottom ->

                    inputBottom - visibleBottom

                else ->
                    0
            }

            if (delta != 0) {
                val previousScrollY = scroll.scrollY
                scroll.scrollBy(0, delta)

                // Después del movimiento obtenemos nuevamente
                // las coordenadas reales. En el extremo del contenido,
                // scrollBy puede quedar limitado sin mover nada.
                if (scroll.scrollY != previousScrollY) {
                    root.post {
                        update()
                    }
                }
            }

        } else {

            input.requestRectangleOnScreen(
                Rect(
                    0,
                    -margin,
                    input.width,
                    input.height + barHeight + margin
                ),
                true
            )
        }
    }

    private fun dismissCurrent() {
        restoreHeader()
        restoreScrollPadding()
        popup?.dismiss()
        val bar = keyboard
        if (bar != null) {
            (bar.parent as? ViewGroup)?.removeView(bar)
            bar.visibility = View.GONE
            originalParent?.let { parent ->
                parent.addView(bar, originalIndex.coerceAtMost(parent.childCount), originalParams)
            }
        }
        keyboard = null
        activeInput = null
        popup = null
        originalParent = null
        originalParams = null
        previousGeometry = ""
    }

    private fun restoreHeader() {
        compactHeader?.visibility = originalHeaderVisibility
        compactHeader = null
    }

    fun close() {
        dismissCurrent()
        hosts.remove(activity)
    }

    companion object {
        private val hosts = WeakHashMap<Activity, MathKeyboardHost>()
        private val bindings = WeakHashMap<View, WeakReference<MathKeyboardView>>()
        internal fun remember(input: View, bar: MathKeyboardView) { bindings[input] = WeakReference(bar) }
        fun install(activity: Activity): MathKeyboardHost = hosts.getOrPut(activity) { MathKeyboardHost(activity) }
        internal fun current(activity: Activity): MathKeyboardHost? = hosts[activity]
    }
}

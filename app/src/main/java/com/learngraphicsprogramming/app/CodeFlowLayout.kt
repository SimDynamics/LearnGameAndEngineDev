package com.learngraphicsprogramming.app

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.view.ViewGroup
import kotlin.math.max

class CodeFlowLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : ViewGroup(
    context,
    attrs
) {

    var wrapEnabled: Boolean =
        false
        set(value) {

            if (
                field == value
            ) {
                return
            }

            field =
                value

            requestLayout()
        }

    var wrapWidthPx: Int =
        0
        set(value) {

            field =
                value

            requestLayout()
        }

    override fun generateDefaultLayoutParams():
            LayoutParams {

        return MarginLayoutParams(
            LayoutParams.WRAP_CONTENT,
            LayoutParams.WRAP_CONTENT
        )
    }

    override fun generateLayoutParams(
        attrs: AttributeSet?
    ): LayoutParams {

        return MarginLayoutParams(
            context,
            attrs
        )
    }

    override fun generateLayoutParams(
        params: LayoutParams?
    ): LayoutParams {

        return if (
            params is MarginLayoutParams
        ) {

            MarginLayoutParams(
                params
            )

        } else {

            MarginLayoutParams(
                params
            )
        }
    }

    override fun checkLayoutParams(
        params: LayoutParams?
    ): Boolean {

        return params is MarginLayoutParams
    }

    override fun onMeasure(
        widthMeasureSpec: Int,
        heightMeasureSpec: Int
    ) {

        val widthMode =
            MeasureSpec.getMode(
                widthMeasureSpec
            )

        val widthSize =
            MeasureSpec.getSize(
                widthMeasureSpec
            )

        /*
         * Primero medimos todos los elementos.
         */
        for (
        i in 0 until childCount
        ) {

            val child =
                getChildAt(i)

            if (
                child.visibility ==
                View.GONE
            ) {
                continue
            }

            measureChildWithMargins(
                child,
                widthMeasureSpec,
                0,
                heightMeasureSpec,
                0
            )
        }

        /*
         * SIN ajuste de línea:
         *
         * todos los elementos permanecen
         * en una sola fila.
         */
        if (
            !wrapEnabled
        ) {

            var contentWidth =
                0

            var contentHeight =
                0

            for (
            i in 0 until childCount
            ) {

                val child =
                    getChildAt(i)

                if (
                    child.visibility ==
                    View.GONE
                ) {
                    continue
                }

                val params =
                    child.layoutParams
                            as MarginLayoutParams

                contentWidth +=
                    child.measuredWidth +
                            params.leftMargin +
                            params.rightMargin

                contentHeight =
                    max(
                        contentHeight,
                        child.measuredHeight +
                                params.topMargin +
                                params.bottomMargin
                    )
            }

            val desiredWidth =
                paddingLeft +
                        contentWidth +
                        paddingRight

            val desiredHeight =
                paddingTop +
                        contentHeight +
                        paddingBottom

            val measuredWidth =
                when (
                    widthMode
                ) {

                    MeasureSpec.EXACTLY ->
                        widthSize

                    else ->
                        desiredWidth
                }

            setMeasuredDimension(
                measuredWidth.coerceAtLeast(
                    suggestedMinimumWidth
                ),
                resolveSize(
                    desiredHeight.coerceAtLeast(
                        suggestedMinimumHeight
                    ),
                    heightMeasureSpec
                )
            )

            return
        }

        /*
         * CON ajuste de línea.
         */
        val availableWidth =
            if (
                wrapWidthPx > 0
            ) {

                wrapWidthPx

            } else {

                when (
                    widthMode
                ) {

                    MeasureSpec.UNSPECIFIED -> {
                        Int.MAX_VALUE / 4
                    }

                    else -> {
                        (
                                widthSize -
                                        paddingLeft -
                                        paddingRight
                                ).coerceAtLeast(
                                0
                            )
                    }
                }
            }

        var currentLineWidth =
            0

        var currentLineHeight =
            0

        var maximumLineWidth =
            0

        var totalContentHeight =
            0

        for (
        i in 0 until childCount
        ) {

            val child =
                getChildAt(i)

            if (
                child.visibility ==
                View.GONE
            ) {
                continue
            }

            val params =
                child.layoutParams
                        as MarginLayoutParams

            val childWidth =
                child.measuredWidth +
                        params.leftMargin +
                        params.rightMargin

            val childHeight =
                child.measuredHeight +
                        params.topMargin +
                        params.bottomMargin

            /*
             * Si el siguiente elemento ya no cabe,
             * empezamos una nueva línea.
             */
            if (
                currentLineWidth > 0 &&
                currentLineWidth +
                childWidth >
                availableWidth
            ) {

                maximumLineWidth =
                    max(
                        maximumLineWidth,
                        currentLineWidth
                    )

                totalContentHeight +=
                    currentLineHeight

                currentLineWidth =
                    0

                currentLineHeight =
                    0
            }

            currentLineWidth +=
                childWidth

            currentLineHeight =
                max(
                    currentLineHeight,
                    childHeight
                )
        }

        maximumLineWidth =
            max(
                maximumLineWidth,
                currentLineWidth
            )

        totalContentHeight +=
            currentLineHeight

        val desiredWidth =
            paddingLeft +
                    maximumLineWidth +
                    paddingRight

        val desiredHeight =
            paddingTop +
                    totalContentHeight +
                    paddingBottom

        setMeasuredDimension(
            resolveSize(
                desiredWidth.coerceAtLeast(
                    suggestedMinimumWidth
                ),
                widthMeasureSpec
            ),
            resolveSize(
                desiredHeight.coerceAtLeast(
                    suggestedMinimumHeight
                ),
                heightMeasureSpec
            )
        )
    }

    override fun onLayout(
        changed: Boolean,
        left: Int,
        top: Int,
        right: Int,
        bottom: Int
    ) {

        /*
         * SIN ajuste de línea.
         */
        if (
            !wrapEnabled
        ) {

            var currentX =
                paddingLeft

            val lineTop =
                paddingTop

            var lineHeight =
                0

            for (
            i in 0 until childCount
            ) {

                val child =
                    getChildAt(i)

                if (
                    child.visibility ==
                    View.GONE
                ) {
                    continue
                }

                val params =
                    child.layoutParams
                            as MarginLayoutParams

                lineHeight =
                    max(
                        lineHeight,
                        child.measuredHeight +
                                params.topMargin +
                                params.bottomMargin
                    )
            }

            for (
            i in 0 until childCount
            ) {

                val child =
                    getChildAt(i)

                if (
                    child.visibility ==
                    View.GONE
                ) {
                    continue
                }

                val params =
                    child.layoutParams
                            as MarginLayoutParams

                val occupiedHeight =
                    child.measuredHeight +
                            params.topMargin +
                            params.bottomMargin

                val verticalOffset =
                    (
                            lineHeight -
                                    occupiedHeight
                            ) / 2

                val childLeft =
                    currentX +
                            params.leftMargin

                val childTop =
                    lineTop +
                            verticalOffset +
                            params.topMargin

                child.layout(
                    childLeft,
                    childTop,
                    childLeft +
                            child.measuredWidth,
                    childTop +
                            child.measuredHeight
                )

                currentX +=
                    params.leftMargin +
                            child.measuredWidth +
                            params.rightMargin
            }

            return
        }

        /*
         * CON ajuste de línea.
         */
        val availableWidth =
            if (
                wrapWidthPx > 0
            ) {

                wrapWidthPx

            } else {

                (
                        width -
                                paddingLeft -
                                paddingRight
                        ).coerceAtLeast(
                        0
                    )
            }

        var lineStart =
            0

        var currentY =
            paddingTop

        while (
            lineStart <
            childCount
        ) {

            var lineEnd =
                lineStart

            var lineWidth =
                0

            var lineHeight =
                0

            /*
             * Determinar qué elementos caben
             * en esta línea.
             */
            while (
                lineEnd <
                childCount
            ) {

                val child =
                    getChildAt(
                        lineEnd
                    )

                if (
                    child.visibility ==
                    View.GONE
                ) {

                    lineEnd++
                    continue
                }

                val params =
                    child.layoutParams
                            as MarginLayoutParams

                val childWidth =
                    child.measuredWidth +
                            params.leftMargin +
                            params.rightMargin

                val childHeight =
                    child.measuredHeight +
                            params.topMargin +
                            params.bottomMargin

                if (
                    lineWidth > 0 &&
                    lineWidth +
                    childWidth >
                    availableWidth
                ) {
                    break
                }

                lineWidth +=
                    childWidth

                lineHeight =
                    max(
                        lineHeight,
                        childHeight
                    )

                lineEnd++
            }

            /*
             * Colocar todos los elementos
             * de esta línea.
             */
            var currentX =
                paddingLeft

            for (
            i in lineStart until lineEnd
            ) {

                val child =
                    getChildAt(i)

                if (
                    child.visibility ==
                    View.GONE
                ) {
                    continue
                }

                val params =
                    child.layoutParams
                            as MarginLayoutParams

                val occupiedHeight =
                    child.measuredHeight +
                            params.topMargin +
                            params.bottomMargin

                val verticalOffset =
                    (
                            lineHeight -
                                    occupiedHeight
                            ) / 2

                val childLeft =
                    currentX +
                            params.leftMargin

                val childTop =
                    currentY +
                            verticalOffset +
                            params.topMargin

                child.layout(
                    childLeft,
                    childTop,
                    childLeft +
                            child.measuredWidth,
                    childTop +
                            child.measuredHeight
                )

                currentX +=
                    params.leftMargin +
                            child.measuredWidth +
                            params.rightMargin
            }

            currentY +=
                lineHeight

            /*
             * Seguridad para evitar un bucle
             * si existiera un elemento extraño.
             */
            if (
                lineEnd ==
                lineStart
            ) {

                lineStart++

            } else {

                lineStart =
                    lineEnd
            }
        }
    }
}
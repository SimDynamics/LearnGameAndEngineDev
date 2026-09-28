package com.learngraphicsprogramming.app

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.SeekBar
import android.widget.Switch

object AppearanceUi {

    fun apply(
        activity: Activity
    ) {

        val accentColor =
            AppearanceSettingsRepository
                .getAccentColor(
                    activity
                )

        /*
         * Barra superior del sistema.
         */
        val statusBarColor =
            createStatusBarColor(
                accentColor
            )

        activity.window.statusBarColor =
            statusBarColor

        /*
         * Elegimos automáticamente iconos
         * claros u oscuros para la barra.
         */
        val decorView =
            activity.window.decorView

        decorView.systemUiVisibility =
            if (
                isLightColor(
                    statusBarColor
                )
            ) {

                decorView.systemUiVisibility or
                        View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR

            } else {

                decorView.systemUiVisibility and
                        View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR.inv()
            }

        val root =
            activity.findViewById<View>(
                android.R.id.content
            )

        applyToViewTree(
            activity,
            root
        )
    }

    fun applyToViewTree(
        context: Context,
        view: View
    ) {

        val accentColor =
            AppearanceSettingsRepository
                .getAccentColor(
                    context
                )

        when (
            view
        ) {

            /*
             * Switch antes de Button porque
             * Switch pertenece a la familia
             * de controles derivados de Button.
             */
            is Switch -> {

                applySwitch(
                    view,
                    accentColor
                )
            }

            is RadioButton -> {

                applyRadioButton(
                    view,
                    accentColor
                )
            }

            is SeekBar -> {

                applySeekBar(
                    view,
                    accentColor
                )
            }

            is Button -> {

                applyButton(
                    view,
                    accentColor
                )
            }

            is ProgressBar -> {

                view.progressTintList =
                    ColorStateList.valueOf(
                        accentColor
                    )
            }
        }

        /*
         * Recorremos también todos
         * los controles hijos.
         */
        if (
            view is ViewGroup
        ) {

            for (
            index in 0 until
                    view.childCount
            ) {

                applyToViewTree(
                    context,
                    view.getChildAt(
                        index
                    )
                )
            }
        }
    }

    private fun applyButton(
        button: Button,
        accentColor: Int
    ) {

        val disabledColor =
            Color.rgb(
                72,
                74,
                82
            )

        button.backgroundTintList =
            ColorStateList(
                arrayOf(
                    intArrayOf(
                        android.R.attr.state_enabled
                    ),
                    intArrayOf()
                ),
                intArrayOf(
                    accentColor,
                    disabledColor
                )
            )

        val enabledTextColor =
            if (
                isLightColor(
                    accentColor
                )
            ) {

                Color.BLACK

            } else {

                Color.WHITE
            }

        button.setTextColor(
            ColorStateList(
                arrayOf(
                    intArrayOf(
                        android.R.attr.state_enabled
                    ),
                    intArrayOf()
                ),
                intArrayOf(
                    enabledTextColor,
                    Color.rgb(
                        170,
                        174,
                        183
                    )
                )
            )
        )
    }

    private fun applySeekBar(
        seekBar: SeekBar,
        accentColor: Int
    ) {

        seekBar.progressTintList =
            ColorStateList.valueOf(
                accentColor
            )

        seekBar.thumbTintList =
            ColorStateList.valueOf(
                accentColor
            )
    }

    private fun applyRadioButton(
        radioButton: RadioButton,
        accentColor: Int
    ) {

        radioButton.buttonTintList =
            ColorStateList(
                arrayOf(
                    intArrayOf(
                        android.R.attr.state_checked
                    ),
                    intArrayOf()
                ),
                intArrayOf(
                    accentColor,
                    Color.rgb(
                        120,
                        124,
                        134
                    )
                )
            )
    }

    private fun applySwitch(
        switch: Switch,
        accentColor: Int
    ) {

        switch.thumbTintList =
            ColorStateList(
                arrayOf(
                    intArrayOf(
                        android.R.attr.state_checked
                    ),
                    intArrayOf()
                ),
                intArrayOf(
                    accentColor,
                    Color.rgb(
                        145,
                        148,
                        158
                    )
                )
            )

        switch.trackTintList =
            ColorStateList(
                arrayOf(
                    intArrayOf(
                        android.R.attr.state_checked
                    ),
                    intArrayOf()
                ),
                intArrayOf(
                    withAlpha(
                        accentColor,
                        135
                    ),
                    Color.argb(
                        90,
                        120,
                        124,
                        134
                    )
                )
            )
    }

    private fun createStatusBarColor(
        accentColor: Int
    ): Int {

        val hsv =
            FloatArray(
                3
            )

        Color.colorToHSV(
            accentColor,
            hsv
        )

        hsv[2] =
            (
                    hsv[2] *
                            0.55f
                    )
                .coerceIn(
                    0f,
                    1f
                )

        return Color.HSVToColor(
            hsv
        )
    }

    private fun isLightColor(
        color: Int
    ): Boolean {

        val red =
            Color.red(
                color
            )

        val green =
            Color.green(
                color
            )

        val blue =
            Color.blue(
                color
            )

        val brightness =
            (
                    red * 299 +
                            green * 587 +
                            blue * 114
                    ) / 1000

        return brightness >=
                160
    }

    private fun withAlpha(
        color: Int,
        alpha: Int
    ): Int {

        return Color.argb(
            alpha,
            Color.red(
                color
            ),
            Color.green(
                color
            ),
            Color.blue(
                color
            )
        )
    }
}
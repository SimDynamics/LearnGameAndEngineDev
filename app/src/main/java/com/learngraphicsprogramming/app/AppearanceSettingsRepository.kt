package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Color

data class AccentRgb(

    val red: Int,

    val green: Int,

    val blue: Int
)

object AppearanceSettingsRepository {

    private const val PREFS_NAME =
        "appearance_settings"

    /*
 * Color de acento predeterminado:
 *
 * #59F0FC
 *
 * RGB:
 * 89, 240, 252
 */
    private const val DEFAULT_RED =
        89

    private const val DEFAULT_GREEN =
        240

    private const val DEFAULT_BLUE =
        252

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

    fun getAccentRgb(
        context: Context
    ): AccentRgb {

        val prefs =
            preferences(
                context
            )

        return AccentRgb(

            red =
                prefs.getInt(
                    "accent_red",
                    DEFAULT_RED
                ),

            green =
                prefs.getInt(
                    "accent_green",
                    DEFAULT_GREEN
                ),

            blue =
                prefs.getInt(
                    "accent_blue",
                    DEFAULT_BLUE
                )
        )
    }

    fun saveAccentColor(
        context: Context,
        red: Int,
        green: Int,
        blue: Int
    ) {

        preferences(
            context
        )
            .edit()
            .putInt(
                "accent_red",
                red.coerceIn(
                    0,
                    255
                )
            )
            .putInt(
                "accent_green",
                green.coerceIn(
                    0,
                    255
                )
            )
            .putInt(
                "accent_blue",
                blue.coerceIn(
                    0,
                    255
                )
            )
            .apply()
    }

    /*
     * COLOR PRINCIPAL.
     *
     * Este será utilizado para botones,
     * barras y otros elementos destacados.
     */
    fun getAccentColor(
        context: Context
    ): Int {

        val rgb =
            getAccentRgb(
                context
            )

        return Color.rgb(
            rgb.red,
            rgb.green,
            rgb.blue
        )
    }

    /*
     * COLOR SECUNDARIO.
     *
     * Conserva exactamente el mismo tono
     * y saturación, pero reduce ligeramente
     * el brillo.
     *
     * Así el usuario solamente escoge
     * UN color.
     */
    fun getAccentTextColor(
        context: Context
    ): Int {

        return createAccentTextColor(
            getAccentColor(
                context
            )
        )
    }

    /*
     * También podrá utilizarse para
     * la vista previa antes de guardar.
     */
    fun createAccentTextColor(
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

        /*
         * Oscurecemos aproximadamente
         * un 4 % sin modificar el tono.
         */
        hsv[2] =
            (
                    hsv[2] *
                            0.96f
                    )
                .coerceIn(
                    0f,
                    1f
                )

        return Color.HSVToColor(
            hsv
        )
    }
}
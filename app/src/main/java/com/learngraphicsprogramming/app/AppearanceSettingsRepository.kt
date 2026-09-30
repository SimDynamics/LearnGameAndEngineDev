package com.learngraphicsprogramming.app

import android.content.Context
import android.graphics.Color

data class AccentRgb(

    val red: Int,

    val green: Int,

    val blue: Int
)

object AppearanceSettingsRepository {

    const val ROUTE_PROGRAMMING =
        "programming"

    const val ROUTE_MATH_PHYSICS =
        "math_physics"

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

    /*
 * COLORES PREDETERMINADOS POR RUTA.
 */
    private const val DEFAULT_PROGRAMMING_RED =
        228

    private const val DEFAULT_PROGRAMMING_GREEN =
        50

    private const val DEFAULT_PROGRAMMING_BLUE =
        72

    private const val DEFAULT_MATH_PHYSICS_RED =
        108

    private const val DEFAULT_MATH_PHYSICS_GREEN =
        209

    private const val DEFAULT_MATH_PHYSICS_BLUE =
        92

    private const val PREFIX_ROUTE_RED =
        "route_accent_red_"

    private const val PREFIX_ROUTE_GREEN =
        "route_accent_green_"

    private const val PREFIX_ROUTE_BLUE =
        "route_accent_blue_"

    private const val PREFIX_ALLOW_GLOBAL_OVERWRITE =
        "route_allow_global_accent_overwrite_"

    /*
 * PREDETERMINADOS EDITABLES.
 */
    private const val KEY_DEFAULT_GLOBAL_RED =
        "default_global_accent_red"

    private const val KEY_DEFAULT_GLOBAL_GREEN =
        "default_global_accent_green"

    private const val KEY_DEFAULT_GLOBAL_BLUE =
        "default_global_accent_blue"

    private const val PREFIX_DEFAULT_ROUTE_RED =
        "default_route_accent_red_"

    private const val PREFIX_DEFAULT_ROUTE_GREEN =
        "default_route_accent_green_"

    private const val PREFIX_DEFAULT_ROUTE_BLUE =
        "default_route_accent_blue_"

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

    fun getDefaultAccentRgb(
        context: Context
    ): AccentRgb {

        val prefs =
            preferences(
                context
            )

        return AccentRgb(
            red =
                prefs.getInt(
                    KEY_DEFAULT_GLOBAL_RED,
                    DEFAULT_RED
                ),

            green =
                prefs.getInt(
                    KEY_DEFAULT_GLOBAL_GREEN,
                    DEFAULT_GREEN
                ),

            blue =
                prefs.getInt(
                    KEY_DEFAULT_GLOBAL_BLUE,
                    DEFAULT_BLUE
                )
        )
    }

    fun saveDefaultAccentColor(
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
                KEY_DEFAULT_GLOBAL_RED,
                red.coerceIn(0, 255)
            )
            .putInt(
                KEY_DEFAULT_GLOBAL_GREEN,
                green.coerceIn(0, 255)
            )
            .putInt(
                KEY_DEFAULT_GLOBAL_BLUE,
                blue.coerceIn(0, 255)
            )
            .apply()
    }

    fun getRouteDefaultAccentRgb(
        context: Context,
        routeId: String
    ): AccentRgb {

        val factoryDefault =
            when (
                routeId
            ) {

                ROUTE_PROGRAMMING ->
                    AccentRgb(
                        DEFAULT_PROGRAMMING_RED,
                        DEFAULT_PROGRAMMING_GREEN,
                        DEFAULT_PROGRAMMING_BLUE
                    )

                ROUTE_MATH_PHYSICS ->
                    AccentRgb(
                        DEFAULT_MATH_PHYSICS_RED,
                        DEFAULT_MATH_PHYSICS_GREEN,
                        DEFAULT_MATH_PHYSICS_BLUE
                    )

                else ->
                    getDefaultAccentRgb(
                        context
                    )
            }

        val prefs =
            preferences(
                context
            )

        return AccentRgb(
            red =
                prefs.getInt(
                    PREFIX_DEFAULT_ROUTE_RED +
                            routeId,
                    factoryDefault.red
                ),

            green =
                prefs.getInt(
                    PREFIX_DEFAULT_ROUTE_GREEN +
                            routeId,
                    factoryDefault.green
                ),

            blue =
                prefs.getInt(
                    PREFIX_DEFAULT_ROUTE_BLUE +
                            routeId,
                    factoryDefault.blue
                )
        )
    }

    fun saveRouteDefaultAccentColor(
        context: Context,
        routeId: String,
        red: Int,
        green: Int,
        blue: Int
    ) {

        preferences(
            context
        )
            .edit()
            .putInt(
                PREFIX_DEFAULT_ROUTE_RED +
                        routeId,
                red.coerceIn(0, 255)
            )
            .putInt(
                PREFIX_DEFAULT_ROUTE_GREEN +
                        routeId,
                green.coerceIn(0, 255)
            )
            .putInt(
                PREFIX_DEFAULT_ROUTE_BLUE +
                        routeId,
                blue.coerceIn(0, 255)
            )
            .apply()
    }

    /*
     * COLOR GLOBAL.
     *
     * Este color pertenece a la aplicación
     * general y siempre será utilizado por
     * la pantalla principal de rutas.
     */
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
     * COLOR PROPIO DE UNA RUTA.
     *
     * Si todavía nunca se guardó un color
     * particular para esa ruta, comienza
     * utilizando el color global actual.
     */
    fun getRouteAccentRgb(
        context: Context,
        routeId: String
    ): AccentRgb {

        val prefs =
            preferences(
                context
            )

        val global =
            getAccentRgb(
                context
            )

        val defaultRouteAccent =
            when (
                routeId
            ) {

                ROUTE_PROGRAMMING ->
                    AccentRgb(
                        DEFAULT_PROGRAMMING_RED,
                        DEFAULT_PROGRAMMING_GREEN,
                        DEFAULT_PROGRAMMING_BLUE
                    )

                ROUTE_MATH_PHYSICS ->
                    AccentRgb(
                        DEFAULT_MATH_PHYSICS_RED,
                        DEFAULT_MATH_PHYSICS_GREEN,
                        DEFAULT_MATH_PHYSICS_BLUE
                    )

                else ->
                    global
            }

        return AccentRgb(

            red =
                prefs.getInt(
                    PREFIX_ROUTE_RED +
                            routeId,
                    defaultRouteAccent.red
                ),

            green =
                prefs.getInt(
                    PREFIX_ROUTE_GREEN +
                            routeId,
                    defaultRouteAccent.green
                ),

            blue =
                prefs.getInt(
                    PREFIX_ROUTE_BLUE +
                            routeId,
                    defaultRouteAccent.blue
                )
        )
    }

    fun saveRouteAccentColor(
        context: Context,
        routeId: String,
        red: Int,
        green: Int,
        blue: Int
    ) {

        preferences(
            context
        )
            .edit()
            .putInt(
                PREFIX_ROUTE_RED +
                        routeId,
                red.coerceIn(
                    0,
                    255
                )
            )
            .putInt(
                PREFIX_ROUTE_GREEN +
                        routeId,
                green.coerceIn(
                    0,
                    255
                )
            )
            .putInt(
                PREFIX_ROUTE_BLUE +
                        routeId,
                blue.coerceIn(
                    0,
                    255
                )
            )
            .apply()
    }

    /*
     * PERMITIR QUE UN FUTURO CAMBIO
     * DEL COLOR GLOBAL SOBRESCRIBA
     * EL COLOR DE ESTA RUTA.
     *
     * Activado por defecto.
     */
    fun allowsGlobalAccentOverwrite(
        context: Context,
        routeId: String
    ): Boolean {

        return preferences(
            context
        )
            .getBoolean(
                PREFIX_ALLOW_GLOBAL_OVERWRITE +
                        routeId,
                true
            )
    }

    fun saveAllowsGlobalAccentOverwrite(
        context: Context,
        routeId: String,
        enabled: Boolean
    ) {

        preferences(
            context
        )
            .edit()
            .putBoolean(
                PREFIX_ALLOW_GLOBAL_OVERWRITE +
                        routeId,
                enabled
            )
            .apply()
    }

    /*
     * GUARDA EL COLOR GLOBAL Y LO APLICA
     * A LAS RUTAS QUE TODAVÍA PERMITEN
     * SER SOBRESCRITAS.
     */
    fun saveGlobalAccentAndApplyToRoutes(
        context: Context,
        red: Int,
        green: Int,
        blue: Int,
        routeIds: List<String>
    ) {

        saveAccentColor(
            context,
            red,
            green,
            blue
        )

        for (
        routeId in routeIds
        ) {

            if (
                allowsGlobalAccentOverwrite(
                    context,
                    routeId
                )
            ) {

                saveRouteAccentColor(
                    context,
                    routeId,
                    red,
                    green,
                    blue
                )
            }
        }
    }

    /*
     * COLOR GLOBAL COMO INT.
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
     * COLOR DE UNA RUTA COMO INT.
     */
    fun getRouteAccentColor(
        context: Context,
        routeId: String
    ): Int {

        val rgb =
            getRouteAccentRgb(
                context,
                routeId
            )

        return Color.rgb(
            rgb.red,
            rgb.green,
            rgb.blue
        )
    }

    /*
     * COLOR SECUNDARIO GLOBAL.
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
     * COLOR SECUNDARIO DE UNA RUTA.
     */
    fun getRouteAccentTextColor(
        context: Context,
        routeId: String
    ): Int {

        return createAccentTextColor(
            getRouteAccentColor(
                context,
                routeId
            )
        )
    }

    /*
     * Conserva exactamente el mismo tono
     * y saturación, pero reduce ligeramente
     * el brillo.
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
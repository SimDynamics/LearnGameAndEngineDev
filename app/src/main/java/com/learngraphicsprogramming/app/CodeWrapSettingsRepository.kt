package com.learngraphicsprogramming.app

import android.content.Context

object CodeWrapSettingsRepository {

    private const val PREFERENCES_NAME =
        "code_wrap_settings"

    private const val KEY_GLOBAL_WRAP =
        "global_wrap"

    private const val PREFIX_COURSE_WRAP =
        "course_wrap_"

    private const val PREFIX_ALLOW_GLOBAL_OVERWRITE =
        "course_allow_global_overwrite_"

    const val DEFAULT_WRAP =
        false

    const val DEFAULT_ALLOW_GLOBAL_OVERWRITE =
        true

    /*
     * AJUSTE GLOBAL.
     *
     * Representa el valor predeterminado que
     * puede aplicarse masivamente a los cursos
     * que permiten sobrescritura global.
     */
    fun getGlobalWrap(
        context: Context
    ): Boolean {

        return context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .getBoolean(
                KEY_GLOBAL_WRAP,
                DEFAULT_WRAP
            )
    }

    fun saveGlobalWrap(
        context: Context,
        enabled: Boolean
    ) {

        context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .edit()
            .putBoolean(
                KEY_GLOBAL_WRAP,
                enabled
            )
            .apply()
    }

    /*
     * AJUSTE REAL DEL CURSO.
     *
     * Todas las consolas fake y el compilador
     * de un mismo curso utilizan este valor.
     *
     * Si el curso todavía nunca ha guardado
     * uno propio, parte del valor global actual.
     */
    fun getCourseWrap(
        context: Context,
        courseId: String
    ): Boolean {

        return context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .getBoolean(
                PREFIX_COURSE_WRAP +
                        courseId,
                getGlobalWrap(
                    context
                )
            )
    }

    fun saveCourseWrap(
        context: Context,
        courseId: String,
        enabled: Boolean
    ) {

        context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .edit()
            .putBoolean(
                PREFIX_COURSE_WRAP +
                        courseId,
                enabled
            )
            .apply()
    }

    /*
     * PROTECCIÓN DEL CURSO.
     *
     * true:
     * los cambios realizados desde Opciones
     * globales pueden sobrescribir este curso.
     *
     * false:
     * el curso conserva su ajuste individual
     * aunque posteriormente cambie el global.
     *
     * Esta protección NO afecta al botón de
     * ajuste de línea dentro de las consolas.
     */
    fun allowsGlobalOverwrite(
        context: Context,
        courseId: String
    ): Boolean {

        return context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .getBoolean(
                PREFIX_ALLOW_GLOBAL_OVERWRITE +
                        courseId,
                DEFAULT_ALLOW_GLOBAL_OVERWRITE
            )
    }

    fun saveAllowsGlobalOverwrite(
        context: Context,
        courseId: String,
        allowed: Boolean
    ) {

        context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .edit()
            .putBoolean(
                PREFIX_ALLOW_GLOBAL_OVERWRITE +
                        courseId,
                allowed
            )
            .apply()
    }

    /*
     * CAMBIO GLOBAL.
     *
     * Guarda el nuevo valor global y lo copia
     * únicamente en los cursos que permiten
     * ser sobrescritos.
     */
    fun saveGlobalWrapAndApplyToCourses(
        context: Context,
        enabled: Boolean,
        courseIds: List<String>
    ) {

        val preferences =
            context.getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )

        val editor =
            preferences.edit()

        editor.putBoolean(
            KEY_GLOBAL_WRAP,
            enabled
        )

        for (
        courseId in courseIds
        ) {

            val allowsOverwrite =
                preferences.getBoolean(
                    PREFIX_ALLOW_GLOBAL_OVERWRITE +
                            courseId,
                    DEFAULT_ALLOW_GLOBAL_OVERWRITE
                )

            if (
                allowsOverwrite
            ) {

                editor.putBoolean(
                    PREFIX_COURSE_WRAP +
                            courseId,
                    enabled
                )
            }
        }

        editor.apply()
    }

    /*
     * RESTABLECER EXCEPCIONES DE UNA RUTA.
     *
     * Todos sus cursos vuelven a:
     *
     * - permitir sobrescritura global
     * - utilizar el valor global actual
     */
    fun resetRouteExceptions(
        context: Context,
        courseIds: List<String>
    ) {

        val preferences =
            context.getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )

        val globalWrap =
            preferences.getBoolean(
                KEY_GLOBAL_WRAP,
                DEFAULT_WRAP
            )

        val editor =
            preferences.edit()

        for (
        courseId in courseIds
        ) {

            editor.putBoolean(
                PREFIX_ALLOW_GLOBAL_OVERWRITE +
                        courseId,
                true
            )

            editor.putBoolean(
                PREFIX_COURSE_WRAP +
                        courseId,
                globalWrap
            )
        }

        editor.apply()
    }
}
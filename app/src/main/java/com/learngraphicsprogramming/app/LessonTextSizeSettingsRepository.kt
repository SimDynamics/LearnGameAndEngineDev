package com.learngraphicsprogramming.app

import android.content.Context

object LessonTextSizeSettingsRepository {

    private const val PREFERENCES_NAME =
        "lesson_text_size_settings"

    private const val KEY_GLOBAL_SCALE =
        "global_scale"

    private const val PREFIX_USE_GLOBAL =
        "course_use_global_"

    private const val PREFIX_COURSE_SCALE =
        "course_scale_"

    const val MIN_SCALE =
        75

    const val MAX_SCALE =
        150

    const val DEFAULT_SCALE =
        100

    fun getGlobalScale(
        context: Context
    ): Int {

        return context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .getInt(
                KEY_GLOBAL_SCALE,
                DEFAULT_SCALE
            )
            .coerceIn(
                MIN_SCALE,
                MAX_SCALE
            )
    }

    fun saveGlobalScale(
        context: Context,
        scale: Int
    ) {

        context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .edit()
            .putInt(
                KEY_GLOBAL_SCALE,
                scale.coerceIn(
                    MIN_SCALE,
                    MAX_SCALE
                )
            )
            .apply()
    }

    /*
     * Lo normal es que cada curso herede
     * el tamaño global.
     *
     * false representa una excepción
     * específica para ese curso.
     */
    fun usesGlobalScale(
        context: Context,
        courseId: String
    ): Boolean {

        return context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .getBoolean(
                PREFIX_USE_GLOBAL +
                        courseId,
                true
            )
    }

    fun saveUsesGlobalScale(
        context: Context,
        courseId: String,
        useGlobal: Boolean
    ) {

        context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .edit()
            .putBoolean(
                PREFIX_USE_GLOBAL +
                        courseId,
                useGlobal
            )
            .apply()
    }

    fun getCourseScale(
        context: Context,
        courseId: String
    ): Int {

        return context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .getInt(
                PREFIX_COURSE_SCALE +
                        courseId,
                getGlobalScale(
                    context
                )
            )
            .coerceIn(
                MIN_SCALE,
                MAX_SCALE
            )
    }

    fun saveCourseScale(
        context: Context,
        courseId: String,
        scale: Int
    ) {

        context
            .getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )
            .edit()
            .putInt(
                PREFIX_COURSE_SCALE +
                        courseId,
                scale.coerceIn(
                    MIN_SCALE,
                    MAX_SCALE
                )
            )
            .apply()
    }

    fun getEffectiveScale(
        context: Context,
        courseId: String
    ): Int {

        return if (
            usesGlobalScale(
                context,
                courseId
            )
        ) {

            getGlobalScale(
                context
            )

        } else {

            getCourseScale(
                context,
                courseId
            )
        }
    }

    fun scaleTextSize(
        baseSp: Float,
        scale: Int
    ): Float {

        return baseSp *
                scale /
                100f
    }
}
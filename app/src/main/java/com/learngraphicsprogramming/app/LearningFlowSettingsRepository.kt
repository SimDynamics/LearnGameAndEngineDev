package com.learngraphicsprogramming.app

import android.content.Context

data class LearningFlowSettings(

    val automaticBetweenLessons: Boolean,

    val automaticBetweenUnits: Boolean,

    val automaticBetweenCourses: Boolean,

    val swipeCanFinishLesson: Boolean
)

object LearningFlowSettingsRepository {

    private const val PREFERENCES_NAME =
        "learning_flow_settings"

    /*
     * CONFIGURACIÓN GLOBAL
     */

    private const val KEY_GLOBAL_BETWEEN_LESSONS =
        "global_between_lessons"

    private const val KEY_GLOBAL_BETWEEN_UNITS =
        "global_between_units"

    private const val KEY_GLOBAL_BETWEEN_COURSES =
        "global_between_courses"

    private const val KEY_GLOBAL_SWIPE_FINISH =
        "global_swipe_finish"

    /*
     * PREFIJO DE CONFIGURACIONES
     * ESPECÍFICAS POR CURSO
     */

    private const val COURSE_PREFIX =
        "course_override_"

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE
        )

    /*
     * VALORES GLOBALES
     *
     * Predeterminados:
     *
     * Entre lecciones: ON
     * Entre unidades: OFF
     * Entre cursos: OFF
     * Swipe para terminar: OFF
     */

    fun getGlobalSettings(
        context: Context
    ): LearningFlowSettings {

        val prefs =
            preferences(
                context
            )

        return LearningFlowSettings(

            automaticBetweenLessons =
                prefs.getBoolean(
                    KEY_GLOBAL_BETWEEN_LESSONS,
                    true
                ),

            automaticBetweenUnits =
                prefs.getBoolean(
                    KEY_GLOBAL_BETWEEN_UNITS,
                    false
                ),

            automaticBetweenCourses =
                prefs.getBoolean(
                    KEY_GLOBAL_BETWEEN_COURSES,
                    false
                ),

            swipeCanFinishLesson =
                prefs.getBoolean(
                    KEY_GLOBAL_SWIPE_FINISH,
                    false
                )
        )
    }

    /*
     * GUARDAR CONFIGURACIÓN GLOBAL
     *
     * Una modificación global elimina
     * todas las excepciones por curso.
     *
     * Así los 44 cursos vuelven a heredar
     * exactamente la misma configuración.
     */

    fun saveGlobalSettings(
        context: Context,
        settings: LearningFlowSettings
    ) {

        val prefs =
            preferences(
                context
            )

        val editor =
            prefs.edit()

        editor.putBoolean(
            KEY_GLOBAL_BETWEEN_LESSONS,
            settings.automaticBetweenLessons
        )

        editor.putBoolean(
            KEY_GLOBAL_BETWEEN_UNITS,
            settings.automaticBetweenUnits
        )

        editor.putBoolean(
            KEY_GLOBAL_BETWEEN_COURSES,
            settings.automaticBetweenCourses
        )

        editor.putBoolean(
            KEY_GLOBAL_SWIPE_FINISH,
            settings.swipeCanFinishLesson
        )

        /*
         * Borramos todas las excepciones
         * específicas de cursos.
         */
        for (
        key in prefs.all.keys
        ) {

            if (
                key.startsWith(
                    COURSE_PREFIX
                )
            ) {

                editor.remove(
                    key
                )
            }
        }

        editor.apply()
    }

    /*
     * CONFIGURACIÓN EFECTIVA
     * DE UN CURSO
     *
     * Si ese curso tiene una excepción,
     * usamos la excepción.
     *
     * Si no, heredamos la global.
     */

    fun getSettingsForCourse(
        context: Context,
        courseId: String
    ): LearningFlowSettings {

        if (
            !hasCourseOverride(
                context,
                courseId
            )
        ) {

            return getGlobalSettings(
                context
            )
        }

        val prefs =
            preferences(
                context
            )

        val global =
            getGlobalSettings(
                context
            )

        return LearningFlowSettings(

            automaticBetweenLessons =
                prefs.getBoolean(
                    courseKey(
                        courseId,
                        "between_lessons"
                    ),
                    global.automaticBetweenLessons
                ),

            automaticBetweenUnits =
                prefs.getBoolean(
                    courseKey(
                        courseId,
                        "between_units"
                    ),
                    global.automaticBetweenUnits
                ),

            automaticBetweenCourses =
                prefs.getBoolean(
                    courseKey(
                        courseId,
                        "between_courses"
                    ),
                    global.automaticBetweenCourses
                ),

            swipeCanFinishLesson =
                prefs.getBoolean(
                    courseKey(
                        courseId,
                        "swipe_finish"
                    ),
                    global.swipeCanFinishLesson
                )
        )
    }

    /*
     * GUARDAR EXCEPCIÓN
     * PARA UN CURSO
     */

    fun saveCourseSettings(
        context: Context,
        courseId: String,
        settings: LearningFlowSettings
    ) {

        preferences(context)
            .edit()
            .putBoolean(
                courseKey(
                    courseId,
                    "enabled"
                ),
                true
            )
            .putBoolean(
                courseKey(
                    courseId,
                    "between_lessons"
                ),
                settings.automaticBetweenLessons
            )
            .putBoolean(
                courseKey(
                    courseId,
                    "between_units"
                ),
                settings.automaticBetweenUnits
            )
            .putBoolean(
                courseKey(
                    courseId,
                    "between_courses"
                ),
                settings.automaticBetweenCourses
            )
            .putBoolean(
                courseKey(
                    courseId,
                    "swipe_finish"
                ),
                settings.swipeCanFinishLesson
            )
            .apply()
    }

    /*
     * ¿ESTE CURSO TIENE
     * CONFIGURACIÓN PROPIA?
     */

    fun hasCourseOverride(
        context: Context,
        courseId: String
    ): Boolean {

        return preferences(context)
            .getBoolean(
                courseKey(
                    courseId,
                    "enabled"
                ),
                false
            )
    }

    /*
     * ELIMINAR LA EXCEPCIÓN
     * DE UN CURSO
     *
     * Después volverá a heredar
     * la configuración global.
     */

    fun clearCourseOverride(
        context: Context,
        courseId: String
    ) {

        val prefs =
            preferences(
                context
            )

        val editor =
            prefs.edit()

        val prefix =
            "${COURSE_PREFIX}${courseId}_"

        for (
        key in prefs.all.keys
        ) {

            if (
                key.startsWith(
                    prefix
                )
            ) {

                editor.remove(
                    key
                )
            }
        }

        editor.apply()
    }

    private fun courseKey(
        courseId: String,
        setting: String
    ): String {

        return (
                COURSE_PREFIX +
                        courseId +
                        "_" +
                        setting
                )
    }
}
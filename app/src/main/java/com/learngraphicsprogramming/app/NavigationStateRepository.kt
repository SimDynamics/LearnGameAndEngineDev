package com.learngraphicsprogramming.app

import android.content.Context

data class NavigationState(

    val screen: String,

    val courseId: String?,

    val unitId: String?,

    val lessonId: String?
)

object NavigationStateRepository {

    const val SCREEN_ROUTES =
        "routes"

    const val SCREEN_COURSES =
        "courses"

    const val SCREEN_COURSE =
        "course"

    const val SCREEN_UNIT =
        "unit"

    const val SCREEN_LESSON =
        "lesson"

    private const val PREFERENCES_NAME =
        "navigation_state"

    private const val KEY_SCREEN =
        "last_screen"

    private const val KEY_COURSE_ID =
        "last_course_id"

    private const val KEY_UNIT_ID =
        "last_unit_id"

    private const val KEY_LESSON_ID =
        "last_lesson_id"

    fun saveRoutes(
        context: Context
    ) {

        preferences(context)
            .edit()
            .putString(
                KEY_SCREEN,
                SCREEN_ROUTES
            )
            .remove(
                KEY_COURSE_ID
            )
            .remove(
                KEY_UNIT_ID
            )
            .remove(
                KEY_LESSON_ID
            )
            .apply()
    }

    fun saveCourses(
        context: Context
    ) {

        preferences(context)
            .edit()
            .putString(
                KEY_SCREEN,
                SCREEN_COURSES
            )
            .remove(
                KEY_COURSE_ID
            )
            .remove(
                KEY_UNIT_ID
            )
            .remove(
                KEY_LESSON_ID
            )
            .apply()
    }

    fun saveCourse(
        context: Context,
        courseId: String
    ) {

        preferences(context)
            .edit()
            .putString(
                KEY_SCREEN,
                SCREEN_COURSE
            )
            .putString(
                KEY_COURSE_ID,
                courseId
            )
            .remove(
                KEY_UNIT_ID
            )
            .remove(
                KEY_LESSON_ID
            )
            .apply()
    }

    fun saveUnit(
        context: Context,
        courseId: String,
        unitId: String
    ) {

        preferences(context)
            .edit()
            .putString(
                KEY_SCREEN,
                SCREEN_UNIT
            )
            .putString(
                KEY_COURSE_ID,
                courseId
            )
            .putString(
                KEY_UNIT_ID,
                unitId
            )
            .remove(
                KEY_LESSON_ID
            )
            .apply()
    }

    fun saveLesson(
        context: Context,
        lessonId: String
    ) {

        val parts =
            lessonId.split(
                "__",
                limit = 3
            )

        val editor =
            preferences(context)
                .edit()
                .putString(
                    KEY_SCREEN,
                    SCREEN_LESSON
                )
                .putString(
                    KEY_LESSON_ID,
                    lessonId
                )

        /*
         * Nuestros IDs de lección tienen:
         *
         * curso__unidad__leccion
         *
         * Por tanto podemos reconstruir también
         * automáticamente sus padres.
         */
        if (
            parts.size == 3
        ) {

            val courseId =
                parts[0]

            val unitId =
                "${parts[0]}__${parts[1]}"

            editor.putString(
                KEY_COURSE_ID,
                courseId
            )

            editor.putString(
                KEY_UNIT_ID,
                unitId
            )
        }

        editor.apply()
    }

    fun getState(
        context: Context
    ): NavigationState {

        val preferences =
            preferences(
                context
            )

        return NavigationState(

            screen =
                preferences.getString(
                    KEY_SCREEN,
                    SCREEN_ROUTES
                ) ?: SCREEN_ROUTES,

            courseId =
                preferences.getString(
                    KEY_COURSE_ID,
                    null
                ),

            unitId =
                preferences.getString(
                    KEY_UNIT_ID,
                    null
                ),

            lessonId =
                preferences.getString(
                    KEY_LESSON_ID,
                    null
                )
        )
    }

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE
        )
}
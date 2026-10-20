package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONObject

object LessonRepository {

    /*
     * COMPATIBILIDAD CON PROGRAMACIÓN.
     *
     * Las llamadas antiguas continúan
     * utilizando automáticamente la ruta
     * de Programación.
     */
    fun loadLessons(
        context: Context,
        courseId: String,
        unitId: String
    ): List<Lesson> {

        return loadLessons(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            courseId,
            unitId
        )
    }

    /*
     * CARGAR LECCIONES SEGÚN LA RUTA.
     */
    fun loadLessons(
        context: Context,
        routeId: String,
        courseId: String,
        unitId: String
    ): List<Lesson> {

        val root =
            UnitRepository.loadUnitJson(
                context,
                routeId,
                courseId,
                unitId
            )

        val array =
            root.getJSONArray(
                "lessons"
            )

        val lessons =
            mutableListOf<Lesson>()

        for (
        i in 0 until array.length()
        ) {

            val item =
                array.getJSONObject(
                    i
                )

            lessons.add(
                Lesson(
                    id =
                        item.getString(
                            "id"
                        ),

                    number =
                        item.getInt(
                            "number"
                        ),

                    title =
                        item.getString(
                            "title"
                        )
                )
            )
        }

        return lessons
    }

    /*
     * COMPATIBILIDAD CON PROGRAMACIÓN.
     */
    fun loadLessonJson(
        context: Context,
        lessonId: String
    ): JSONObject {

        return loadLessonJson(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            lessonId
        )
    }

    /*
     * CARGAR EL JSON DE UNA LECCIÓN
     * SEGÚN LA RUTA.
     */
    fun loadLessonJson(
        context: Context,
        routeId: String,
        lessonId: String
    ): JSONObject {

        val path =
            getLessonAssetPath(
                routeId,
                lessonId
            )

        val json = CurriculumAssetLocale.read(context, path)

        return JSONObject(
            json
        ).also { LessonStateCoordinates.register(context, lessonId, it) }
    }

    /*
     * COMPATIBILIDAD CON PROGRAMACIÓN.
     */
    fun lessonExists(
        context: Context,
        lessonId: String
    ): Boolean {

        return lessonExists(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            lessonId
        )
    }

    /*
     * COMPROBAR SI EXISTE UNA LECCIÓN
     * SEGÚN LA RUTA.
     */
    fun lessonExists(
        context: Context,
        routeId: String,
        lessonId: String
    ): Boolean {

        return try {

            CurriculumAssetLocale.open(
                context,
                getLessonAssetPath(routeId, lessonId)
            ).close()

            true

        } catch (
            exception: Exception
        ) {

            false
        }
    }

    /*
     * CONSTRUIR LA RUTA DEL ARCHIVO
     * DE UNA LECCIÓN.
     */
    private fun getLessonAssetPath(
        routeId: String,
        lessonId: String
    ): String {

        val parts =
            lessonId.split(
                "__",
                limit = 3
            )

        require(
            parts.size == 3
        )

        val courseId =
            parts[0]

        val unitSlug =
            parts[1]

        val lessonSlug =
            parts[2]

        return "$routeId/" +
                "$courseId/" +
                "$unitSlug/" +
                "lesson_$lessonSlug.json"
    }
}

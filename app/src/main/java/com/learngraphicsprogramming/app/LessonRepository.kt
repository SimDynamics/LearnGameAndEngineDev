package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONObject

object LessonRepository {

    private const val BASE_PATH =
        "content/es/programming"

    fun loadLessons(
        context: Context,
        courseId: String,
        unitId: String
    ): List<Lesson> {

        val root =
            UnitRepository.loadUnitJson(
                context,
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
                array.getJSONObject(i)

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

    fun loadLessonJson(
        context: Context,
        lessonId: String
    ): JSONObject {

        val path =
            getLessonAssetPath(
                lessonId
            )

        val json =
            context.assets
                .open(path)
                .bufferedReader()
                .use {
                    it.readText()
                }

        return JSONObject(
            json
        )
    }

    fun lessonExists(
        context: Context,
        lessonId: String
    ): Boolean {

        return try {

            context.assets
                .open(
                    getLessonAssetPath(
                        lessonId
                    )
                )
                .close()

            true

        } catch (
            exception: Exception
        ) {

            false
        }
    }

    private fun getLessonAssetPath(
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

        return "$BASE_PATH/" +
                "$courseId/" +
                "$unitSlug/" +
                "lesson_$lessonSlug.json"
    }
}
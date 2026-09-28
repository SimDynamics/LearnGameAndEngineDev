package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONObject

object UnitRepository {

    private const val BASE_PATH =
        "content/es/programming"

    fun loadUnits(
        context: Context,
        courseId: String
    ): List<LearningUnit> {

        val root =
            loadCourseJson(
                context,
                courseId
            )

        val array =
            root.getJSONArray(
                "units"
            )

        val units =
            mutableListOf<LearningUnit>()

        for (
        i in 0 until array.length()
        ) {

            val item =
                array.getJSONObject(i)

            units.add(
                LearningUnit(
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

        return units
    }

    fun loadCourseJson(
        context: Context,
        courseId: String
    ): JSONObject {

        val path =
            "$BASE_PATH/" +
                    "$courseId/" +
                    "course_$courseId.json"

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

    fun loadUnitJson(
        context: Context,
        courseId: String,
        unitId: String
    ): JSONObject {

        val prefix =
            "${courseId}__"

        val unitSlug =
            unitId.removePrefix(
                prefix
            )

        val path =
            "$BASE_PATH/" +
                    "$courseId/" +
                    "$unitSlug/" +
                    "unit_$unitSlug.json"

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
}
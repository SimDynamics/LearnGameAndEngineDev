package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONArray

object CourseRepository {

    fun loadCourses(
        context: Context,
        routeId: String
    ): List<Course> {

        val json = CurriculumAssetLocale.read(context, "$routeId/courses.json")

        val array = JSONArray(json)

        val courses = mutableListOf<Course>()

        for (i in 0 until array.length()) {

            val item = array.getJSONObject(i)

            courses.add(
                Course(
                    id = item.getString("id"),
                    number = item.getInt("number"),
                    title = item.getString("title")
                )
            )
        }

        return courses
    }
}

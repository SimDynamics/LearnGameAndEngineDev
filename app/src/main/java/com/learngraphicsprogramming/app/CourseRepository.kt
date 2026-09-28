package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONArray

object CourseRepository {

    fun loadProgrammingCourses(context: Context): List<Course> {

        val json = context.assets
            .open("content/es/programming/courses.json")
            .bufferedReader()
            .use { it.readText() }

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
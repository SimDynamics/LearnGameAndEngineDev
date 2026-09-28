package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class NavigationLesson(

    val courseId: String,
    val courseNumber: Int,
    val courseTitle: String,

    val unitId: String,
    val unitNumber: Int,
    val unitTitle: String,

    val lessonId: String,
    val lessonNumber: Int,
    val lessonTitle: String
)

object NavigationRepository {

    private const val BASE_PATH =
        "content/es/programming"

    fun getNextLesson(
        context: Context,
        currentLessonId: String
    ): NavigationLesson? {

        val parts =
            currentLessonId.split(
                "__",
                limit = 3
            )

        if (
            parts.size != 3
        ) {

            return null
        }

        val currentCourseId =
            parts[0]

        val currentUnitId =
            "${parts[0]}__${parts[1]}"

        val courses =
            loadCourses(
                context
            ) ?: return null

        var currentCourseIndex =
            -1

        for (
        i in 0 until courses.length()
        ) {

            if (
                courses
                    .getJSONObject(i)
                    .getString("id") ==
                currentCourseId
            ) {

                currentCourseIndex =
                    i

                break
            }
        }

        if (
            currentCourseIndex == -1
        ) {

            return null
        }

        val currentCourse =
            courses.getJSONObject(
                currentCourseIndex
            )

        val insideCurrentCourse =
            findNextInsideCourse(
                context,
                currentCourse,
                currentUnitId,
                currentLessonId
            )

        if (
            insideCurrentCourse != null
        ) {

            return insideCurrentCourse
        }

        for (
        courseIndex in
        currentCourseIndex + 1 until
                courses.length()
        ) {

            val course =
                courses.getJSONObject(
                    courseIndex
                )

            val firstLesson =
                findFirstLessonInCourse(
                    context,
                    course
                )

            if (
                firstLesson != null
            ) {

                return firstLesson
            }
        }

        return null
    }

    private fun findNextInsideCourse(
        context: Context,
        course: JSONObject,
        currentUnitId: String,
        currentLessonId: String
    ): NavigationLesson? {

        val courseId =
            course.getString(
                "id"
            )

        val courseRoot =
            try {

                UnitRepository.loadCourseJson(
                    context,
                    courseId
                )

            } catch (
                exception: Exception
            ) {

                return null
            }

        val units =
            courseRoot.getJSONArray(
                "units"
            )

        var currentUnitIndex =
            -1

        for (
        i in 0 until units.length()
        ) {

            if (
                units
                    .getJSONObject(i)
                    .getString("id") ==
                currentUnitId
            ) {

                currentUnitIndex =
                    i

                break
            }
        }

        if (
            currentUnitIndex == -1
        ) {

            return null
        }

        val currentUnit =
            units.getJSONObject(
                currentUnitIndex
            )

        val unitRoot =
            try {

                UnitRepository.loadUnitJson(
                    context,
                    courseId,
                    currentUnitId
                )

            } catch (
                exception: Exception
            ) {

                return null
            }

        val lessons =
            unitRoot.getJSONArray(
                "lessons"
            )

        var currentLessonIndex =
            -1

        for (
        i in 0 until lessons.length()
        ) {

            if (
                lessons
                    .getJSONObject(i)
                    .getString("id") ==
                currentLessonId
            ) {

                currentLessonIndex =
                    i

                break
            }
        }

        if (
            currentLessonIndex != -1
        ) {

            for (
            lessonIndex in
            currentLessonIndex + 1 until
                    lessons.length()
            ) {

                val lesson =
                    lessons.getJSONObject(
                        lessonIndex
                    )

                val result =
                    createNavigationLesson(
                        context,
                        course,
                        currentUnit,
                        lesson
                    )

                if (
                    result != null
                ) {

                    return result
                }
            }
        }

        for (
        unitIndex in
        currentUnitIndex + 1 until
                units.length()
        ) {

            val unit =
                units.getJSONObject(
                    unitIndex
                )

            val result =
                findFirstLessonInUnit(
                    context,
                    course,
                    unit
                )

            if (
                result != null
            ) {

                return result
            }
        }

        return null
    }

    private fun findFirstLessonInCourse(
        context: Context,
        course: JSONObject
    ): NavigationLesson? {

        val courseId =
            course.getString(
                "id"
            )

        val courseRoot =
            try {

                UnitRepository.loadCourseJson(
                    context,
                    courseId
                )

            } catch (
                exception: Exception
            ) {

                return null
            }

        val units =
            courseRoot.getJSONArray(
                "units"
            )

        for (
        unitIndex in
        0 until units.length()
        ) {

            val unit =
                units.getJSONObject(
                    unitIndex
                )

            val result =
                findFirstLessonInUnit(
                    context,
                    course,
                    unit
                )

            if (
                result != null
            ) {

                return result
            }
        }

        return null
    }

    private fun findFirstLessonInUnit(
        context: Context,
        course: JSONObject,
        unit: JSONObject
    ): NavigationLesson? {

        val courseId =
            course.getString(
                "id"
            )

        val unitId =
            unit.getString(
                "id"
            )

        val root =
            try {

                UnitRepository.loadUnitJson(
                    context,
                    courseId,
                    unitId
                )

            } catch (
                exception: Exception
            ) {

                return null
            }

        val lessons =
            root.getJSONArray(
                "lessons"
            )

        for (
        lessonIndex in
        0 until lessons.length()
        ) {

            val lesson =
                lessons.getJSONObject(
                    lessonIndex
                )

            val result =
                createNavigationLesson(
                    context,
                    course,
                    unit,
                    lesson
                )

            if (
                result != null
            ) {

                return result
            }
        }

        return null
    }

    private fun createNavigationLesson(
        context: Context,
        course: JSONObject,
        unit: JSONObject,
        lesson: JSONObject
    ): NavigationLesson? {

        val lessonId =
            lesson.getString(
                "id"
            )

        if (
            !LessonRepository.lessonExists(
                context,
                lessonId
            )
        ) {

            return null
        }

        return NavigationLesson(

            courseId =
                course.getString(
                    "id"
                ),

            courseNumber =
                course.getInt(
                    "number"
                ),

            courseTitle =
                course.getString(
                    "title"
                ),

            unitId =
                unit.getString(
                    "id"
                ),

            unitNumber =
                unit.getInt(
                    "number"
                ),

            unitTitle =
                unit.getString(
                    "title"
                ),

            lessonId =
                lessonId,

            lessonNumber =
                lesson.getInt(
                    "number"
                ),

            lessonTitle =
                lesson.getString(
                    "title"
                )
        )
    }

    private fun loadCourses(
        context: Context
    ): JSONArray? {

        return try {

            val json =
                context.assets
                    .open(
                        "$BASE_PATH/courses.json"
                    )
                    .bufferedReader()
                    .use {
                        it.readText()
                    }

            JSONArray(
                json
            )

        } catch (
            exception: Exception
        ) {

            null
        }
    }
}
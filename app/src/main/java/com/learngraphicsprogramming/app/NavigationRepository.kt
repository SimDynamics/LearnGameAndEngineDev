package com.learngraphicsprogramming.app

import android.content.Context

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

    /*
     * Compatibilidad con todo el código
     * anterior de Programación.
     */
    fun getNextLesson(
        context: Context,
        currentLessonId: String
    ): NavigationLesson? {

        return getNextLesson(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            currentLessonId
        )
    }

    /*
     * Busca la siguiente lección dentro
     * de la ruta indicada.
     *
     * Puede recorrer:
     *
     * lección -> lección
     * unidad  -> unidad
     * curso   -> curso
     */
    fun getNextLesson(
        context: Context,
        routeId: String,
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
            try {

                CourseRepository.loadCourses(
                    context,
                    routeId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        val currentCourseIndex =
            courses.indexOfFirst {
                it.id ==
                        currentCourseId
            }

        if (
            currentCourseIndex < 0
        ) {

            return null
        }

        val currentCourse =
            courses[
                currentCourseIndex
            ]

        /*
         * Primero intentamos continuar
         * dentro del mismo curso.
         */
        val units =
            try {

                UnitRepository.loadUnits(
                    context,
                    routeId,
                    currentCourse.id
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        val currentUnitIndex =
            units.indexOfFirst {
                it.id ==
                        currentUnitId
            }

        if (
            currentUnitIndex >= 0
        ) {

            val currentUnit =
                units[
                    currentUnitIndex
                ]

            val lessons =
                try {

                    LessonRepository.loadLessons(
                        context,
                        routeId,
                        currentCourse.id,
                        currentUnit.id
                    )

                } catch (
                    exception: Exception
                ) {

                    emptyList()
                }

            val currentLessonIndex =
                lessons.indexOfFirst {
                    it.id ==
                            currentLessonId
                }

            /*
             * Siguiente lección dentro
             * de la misma unidad.
             */
            if (
                currentLessonIndex >= 0
            ) {

                for (
                lessonIndex in
                currentLessonIndex + 1 until
                        lessons.size
                ) {

                    val lesson =
                        lessons[
                            lessonIndex
                        ]

                    val result =
                        createNavigationLesson(
                            context,
                            routeId,
                            currentCourse,
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

            /*
             * Si la unidad terminó,
             * buscamos la primera lección
             * disponible de las unidades
             * posteriores.
             */
            for (
            unitIndex in
            currentUnitIndex + 1 until
                    units.size
            ) {

                val result =
                    findFirstLessonInUnit(
                        context,
                        routeId,
                        currentCourse,
                        units[unitIndex]
                    )

                if (
                    result != null
                ) {

                    return result
                }
            }
        }

        /*
         * Si terminó el curso,
         * buscamos el primer contenido
         * disponible de los cursos siguientes.
         */
        for (
        courseIndex in
        currentCourseIndex + 1 until
                courses.size
        ) {

            val result =
                findFirstLessonInCourse(
                    context,
                    routeId,
                    courses[courseIndex]
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
        routeId: String,
        course: Course
    ): NavigationLesson? {

        val units =
            try {

                UnitRepository.loadUnits(
                    context,
                    routeId,
                    course.id
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        for (
        unit in units
        ) {

            val result =
                findFirstLessonInUnit(
                    context,
                    routeId,
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
        routeId: String,
        course: Course,
        unit: LearningUnit
    ): NavigationLesson? {

        val lessons =
            try {

                LessonRepository.loadLessons(
                    context,
                    routeId,
                    course.id,
                    unit.id
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        for (
        lesson in lessons
        ) {

            val result =
                createNavigationLesson(
                    context,
                    routeId,
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
        routeId: String,
        course: Course,
        unit: LearningUnit,
        lesson: Lesson
    ): NavigationLesson? {

        if (
            !LessonRepository.lessonExists(
                context,
                routeId,
                lesson.id
            )
        ) {

            return null
        }

        return NavigationLesson(

            courseId =
                course.id,

            courseNumber =
                course.number,

            courseTitle =
                course.title,

            unitId =
                unit.id,

            unitNumber =
                unit.number,

            unitTitle =
                unit.title,

            lessonId =
                lesson.id,

            lessonNumber =
                lesson.number,

            lessonTitle =
                lesson.title
        )
    }
}
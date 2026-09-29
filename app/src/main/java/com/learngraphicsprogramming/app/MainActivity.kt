package com.learngraphicsprogramming.app

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.learngraphicsprogramming.app.databinding.ActivityMainBinding
import org.json.JSONArray
import org.json.JSONObject

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        binding =
            ActivityMainBinding.inflate(
                layoutInflater
            )

        setContentView(
            binding.root
        )

        binding.buttonProgramming.setOnClickListener {

            NavigationStateRepository.saveCourses(
                this
            )

            openCourses()
        }

        binding.buttonOptions.setOnClickListener {

            val intent =
                Intent(
                    this,
                    OptionsActivity::class.java
                )

            startActivity(
                intent
            )
        }

        binding.buttonCppPrototype.setOnClickListener {

            val intent =
                Intent(
                    this,
                    CppPrototypeActivity::class.java
                )

            startActivity(
                intent
            )
        }

        /*
         * Solo restauramos la navegación cuando
         * MainActivity acaba de crearse.
         *
         * Así evitamos reconstruir pantallas
         * durante una rotación u otra recreación.
         */
        if (
            savedInstanceState == null
        ) {

            restoreNavigationState()
        }
    }

    private fun restoreNavigationState() {

        val state =
            NavigationStateRepository.getState(
                this
            )

        when (
            state.screen
        ) {

            NavigationStateRepository.SCREEN_ROUTES -> {

                /*
                 * Ya estamos en la pantalla correcta.
                 */
            }

            NavigationStateRepository.SCREEN_COURSES -> {

                openCourses()
            }

            NavigationStateRepository.SCREEN_COURSE -> {

                val courseId =
                    state.courseId
                        ?: return resetToRoutes()

                restoreCourse(
                    courseId
                )
            }

            NavigationStateRepository.SCREEN_UNIT -> {

                val courseId =
                    state.courseId
                        ?: return resetToRoutes()

                val unitId =
                    state.unitId
                        ?: return resetToRoutes()

                restoreUnit(
                    courseId,
                    unitId
                )
            }

            NavigationStateRepository.SCREEN_LESSON -> {

                val courseId =
                    state.courseId
                        ?: return resetToRoutes()

                val unitId =
                    state.unitId
                        ?: return resetToRoutes()

                val lessonId =
                    state.lessonId
                        ?: return resetToRoutes()

                restoreLesson(
                    courseId,
                    unitId,
                    lessonId
                )
            }

            else -> {

                resetToRoutes()
            }
        }
    }

    private fun restoreCourse(
        courseId: String
    ) {

        val course =
            findCourse(
                courseId
            ) ?: return resetToRoutes()

        /*
         * Reconstruimos:
         *
         * Rutas
         * → Cursos
         * → Curso
         */

        openCourses()

        openCourse(
            course
        )
    }

    private fun restoreUnit(
        courseId: String,
        unitId: String
    ) {

        val course =
            findCourse(
                courseId
            ) ?: return resetToRoutes()

        val unit =
            findUnit(
                courseId,
                unitId
            ) ?: return resetToRoutes()

        /*
         * Reconstruimos:
         *
         * Rutas
         * → Cursos
         * → Curso
         * → Unidad
         */

        openCourses()

        openCourse(
            course
        )

        openUnit(
            courseId,
            unit
        )
    }

    private fun restoreLesson(
        courseId: String,
        unitId: String,
        lessonId: String
    ) {

        val course =
            findCourse(
                courseId
            ) ?: return resetToRoutes()

        val unit =
            findUnit(
                courseId,
                unitId
            ) ?: return resetToRoutes()

        val lessonInfo =
            findLesson(
                courseId,
                unitId,
                lessonId
            ) ?: return resetToRoutes()

        /*
         * Reconstruimos la pila completa:
         *
         * Rutas
         * → Cursos
         * → Curso
         * → Unidad
         * → Lección
         *
         * Por eso, después de reabrir la aplicación,
         * Atrás seguirá recorriendo cada nivel.
         */

        openCourses()

        openCourse(
            course
        )

        openUnit(
            courseId,
            unit
        )

        openLesson(
            courseId,
            lessonInfo.lesson,
            lessonInfo.totalLessons
        )
    }

    private fun openCourses() {

        val intent =
            Intent(
                this,
                CoursesActivity::class.java
            )

        startActivity(
            intent
        )
    }

    private fun openCourse(
        course: JSONObject
    ) {

        val intent =
            Intent(
                this,
                CourseActivity::class.java
            )

        intent.putExtra(
            "courseId",
            course.getString(
                "id"
            )
        )

        intent.putExtra(
            "courseNumber",
            course.getInt(
                "number"
            )
        )

        intent.putExtra(
            "courseTitle",
            course.getString(
                "title"
            )
        )

        startActivity(
            intent
        )
    }

    private fun openUnit(
        courseId: String,
        unit: JSONObject
    ) {

        val intent =
            Intent(
                this,
                UnitActivity::class.java
            )

        intent.putExtra(
            "courseId",
            courseId
        )

        intent.putExtra(
            "unitId",
            unit.getString(
                "id"
            )
        )

        intent.putExtra(
            "unitNumber",
            unit.getInt(
                "number"
            )
        )

        intent.putExtra(
            "unitTitle",
            unit.getString(
                "title"
            )
        )

        startActivity(
            intent
        )
    }

    private fun openLesson(
        courseId: String,
        lesson: JSONObject,
        totalLessons: Int
    ) {

        val intent =
            Intent(
                this,
                LessonActivity::class.java
            )

        intent.putExtra(
            "courseId",
            courseId
        )

        intent.putExtra(
            "lessonId",
            lesson.getString(
                "id"
            )
        )

        intent.putExtra(
            "lessonNumber",
            lesson.getInt(
                "number"
            )
        )

        intent.putExtra(
            "lessonTotal",
            totalLessons
        )

        startActivity(
            intent
        )
    }

    private fun findCourse(
        courseId: String
    ): JSONObject? {

        return try {

            val json =
                assets
                    .open(
                        "content/es/programming/courses.json"
                    )
                    .bufferedReader()
                    .use {
                        it.readText()
                    }

            val courses =
                JSONArray(
                    json
                )

            for (
            i in 0 until courses.length()
            ) {

                val course =
                    courses.getJSONObject(i)

                if (
                    course.getString(
                        "id"
                    ) ==
                    courseId
                ) {

                    return course
                }
            }

            null

        } catch (
            exception: Exception
        ) {

            null
        }
    }

    private fun findUnit(
        courseId: String,
        unitId: String
    ): JSONObject? {

        return try {

            val course =
                UnitRepository.loadCourseJson(
                    this,
                    courseId
                )

            val units =
                course.getJSONArray(
                    "units"
                )

            for (
            i in 0 until units.length()
            ) {

                val unit =
                    units.getJSONObject(i)

                if (
                    unit.getString(
                        "id"
                    ) ==
                    unitId
                ) {

                    return unit
                }
            }

            null

        } catch (
            exception: Exception
        ) {

            null
        }
    }

    private fun findLesson(
        courseId: String,
        unitId: String,
        lessonId: String
    ): LessonRestoreInfo? {

        return try {

            val unit =
                UnitRepository.loadUnitJson(
                    this,
                    courseId,
                    unitId
                )

            val lessons =
                unit.getJSONArray(
                    "lessons"
                )

            for (
            i in 0 until lessons.length()
            ) {

                val lesson =
                    lessons.getJSONObject(i)

                if (
                    lesson.getString(
                        "id"
                    ) ==
                    lessonId
                ) {

                    return LessonRestoreInfo(
                        lesson =
                            lesson,

                        totalLessons =
                            lessons.length()
                    )
                }
            }

            null

        } catch (
            exception: Exception
        ) {

            null
        }
    }

    private fun resetToRoutes() {

        NavigationStateRepository.saveRoutes(
            this
        )
    }

    private data class LessonRestoreInfo(

        val lesson: JSONObject,

        val totalLessons: Int
    )

    override fun onResume() {

        super.onResume()

        AppearanceUi.apply(
            this
        )

        NavigationStateRepository.saveRoutes(
            this
        )
    }
}

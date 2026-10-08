package com.learngraphicsprogramming.app

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import com.learngraphicsprogramming.app.databinding.ActivityCoursesBinding

class CoursesActivity : AppCompatActivity() {

    private lateinit var binding: ActivityCoursesBinding

    private var routeId =
        AppearanceSettingsRepository.ROUTE_PROGRAMMING

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        binding =
            ActivityCoursesBinding.inflate(
                layoutInflater
            )

        setContentView(
            binding.root
        )

        routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        /*
         * ENCABEZADO SEGÚN LA RUTA.
         *
         * Los títulos y descripciones se obtienen
         * de los recursos del idioma seleccionado.
         */
        if (
            routeId ==
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
        ) {

            binding.textCoursesTitle.text =
                getString(
                    R.string.math_physics
                )

            binding.textCoursesDescription.text =
                getString(
                    R.string.nav_math_route_description
                )

        } else {

            binding.textCoursesTitle.text =
                getString(
                    R.string.programming
                )

            binding.textCoursesDescription.text =
                getString(
                    R.string.nav_programming_route_description
                )
        }

        binding.buttonBack.setOnClickListener {

            finish()
        }

        /*
         * CARGAMOS LOS CURSOS DE LA RUTA ACTUAL.
         */
        val courses =
            CourseRepository.loadCourses(
                this,
                routeId
            )

        binding.recyclerCourses.layoutManager =
            LinearLayoutManager(
                this
            )

        binding.recyclerCourses.adapter =
            CourseAdapter(
                courses,
                routeId
            ) { course ->

                val available =
                    if (
                        routeId ==
                        AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
                    ) {

                        // Availability follows actual local lesson assets, including provisional
                        // QA courses. A listed future course with no content remains unavailable.
                        runCatching {
                            UnitRepository.loadUnits(this, routeId, course.id).any { unit ->
                                LessonRepository.loadLessons(this, routeId, course.id, unit.id).any { lesson ->
                                    LessonRepository.lessonExists(this, routeId, lesson.id)
                                }
                            }
                        }.getOrDefault(false)

                    } else {

                        /*
                         * Cursos actualmente disponibles
                         * de Programación.
                         */
                        course.id in
                                setOf(
                                    "cpp_fundamentals",
                                    "programming_practice_tools",
                                    "systems_architecture_toolchain"
                                )
                    }

                if (
                    available
                ) {

                    val intent =
                        Intent(
                            this,
                            CourseActivity::class.java
                        )

                    intent.putExtra(
                        "routeId",
                        routeId
                    )

                    intent.putExtra(
                        "courseId",
                        course.id
                    )

                    intent.putExtra(
                        "courseNumber",
                        course.number
                    )

                    intent.putExtra(
                        "courseTitle",
                        course.title
                    )

                    startActivity(
                        intent
                    )

                } else {

                    Toast.makeText(
                        this,
                        getString(R.string.nav_coming_soon),
                        Toast.LENGTH_SHORT
                    ).show()
                }
            }
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.applyRoute(
            this,
            routeId
        )

        NavigationStateRepository.saveCourses(
            this,
            routeId
        )

        binding.recyclerCourses.adapter
            ?.notifyDataSetChanged()
    }
}

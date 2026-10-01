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
         */
        if (
            routeId ==
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
        ) {

            binding.textCoursesTitle.text =
                "Matemáticas y Física"

            binding.textCoursesDescription.text =
                "Ruta progresiva de matemáticas y física."

        } else {

            binding.textCoursesTitle.text =
                "Programación Avanzada y Arquitectura de Motores"

            binding.textCoursesDescription.text =
                "Ruta de programación, sistemas, gráficos y motores."
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

                        /*
                         * Por ahora solamente existe
                         * contenido real para el curso 1
                         * de Matemáticas.
                         */
                        course.id ==
                                "math_course_01"

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
                        "En desarrollo · Próximamente disponible",
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
            this
        )

        binding.recyclerCourses.adapter
            ?.notifyDataSetChanged()
    }
}
package com.learngraphicsprogramming.app

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import com.learngraphicsprogramming.app.databinding.ActivityCoursesBinding

class CoursesActivity : AppCompatActivity() {

    private lateinit var binding: ActivityCoursesBinding

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

        binding.buttonBack.setOnClickListener {

            finish()
        }

        val courses =
            CourseRepository
                .loadProgrammingCourses(
                    this
                )

        binding.recyclerCourses.layoutManager =
            LinearLayoutManager(
                this
            )

        binding.recyclerCourses.adapter =
            CourseAdapter(
                courses
            ) { course ->

                val availableCourses =
                    setOf(
                        "cpp_fundamentals",
                        "programming_practice_tools",
                        "systems_architecture_toolchain"
                    )

                if (
                    course.id in
                    availableCourses
                ) {

                    val intent =
                        Intent(
                            this,
                            CourseActivity::class.java
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
            AppearanceSettingsRepository.ROUTE_PROGRAMMING
        )

        NavigationStateRepository.saveCourses(
            this
        )

        binding.recyclerCourses.adapter
            ?.notifyDataSetChanged()
    }
}
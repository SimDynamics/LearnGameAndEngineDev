package com.learngraphicsprogramming.app

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.ImageButton
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView

class CourseActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_course
        )

        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
            )

        val textCourseNumber =
            findViewById<TextView>(
                R.id.textCourseNumber
            )

        val textCourseTitle =
            findViewById<TextView>(
                R.id.textCourseTitle
            )

        val recyclerUnits =
            findViewById<RecyclerView>(
                R.id.recyclerUnits
            )

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        val courseNumber =
            intent.getIntExtra(
                "courseNumber",
                1
            )

        val courseTitle =
            intent.getStringExtra(
                "courseTitle"
            ) ?: ""

        val totalCourses =
            if (
                routeId ==
                AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
            ) {

                72

            } else {

                44
            }

        textCourseNumber.text =
            "Curso $courseNumber de $totalCourses"

        textCourseNumber.setTextColor(
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    this,
                    routeId
                )
        )

        textCourseTitle.text =
            courseTitle

        buttonBack.setOnClickListener {

            finish()
        }

        val buttonCourseSettings =
            findViewById<ImageButton>(
                R.id.buttonCourseSettings
            )

        buttonCourseSettings.setOnClickListener {

            val intent =
                Intent(
                    this,
                    CourseOptionsActivity::class.java
                )

            intent.putExtra(
                "routeId",
                routeId
            )

            intent.putExtra(
                "courseId",
                courseId
            )

            intent.putExtra(
                "courseNumber",
                courseNumber
            )

            intent.putExtra(
                "courseTitle",
                courseTitle
            )

            startActivity(
                intent
            )
        }

        val units =
            UnitRepository.loadUnits(
                this,
                routeId,
                courseId
            )

        recyclerUnits.layoutManager =
            LinearLayoutManager(
                this
            )

        recyclerUnits.adapter =
            UnitAdapter(
                courseId,
                units,
                routeId
            ) { unit ->

                val intent =
                    Intent(
                        this,
                        UnitActivity::class.java
                    )

                intent.putExtra(
                    "routeId",
                    routeId
                )

                intent.putExtra(
                    "courseId",
                    courseId
                )

                intent.putExtra(
                    "unitId",
                    unit.id
                )

                intent.putExtra(
                    "unitNumber",
                    unit.number
                )

                intent.putExtra(
                    "unitTitle",
                    unit.title
                )

                startActivity(
                    intent
                )
            }
    }

    override fun onResume() {

        super.onResume()

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        AppearanceUi.applyRoute(
            this,
            routeId
        )

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        NavigationStateRepository.saveCourse(
            this,
            courseId,
            routeId
        )

        val recyclerUnits =
            findViewById<RecyclerView>(
                R.id.recyclerUnits
            )

        recyclerUnits.adapter
            ?.notifyDataSetChanged()
    }
}

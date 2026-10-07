package com.learngraphicsprogramming.app

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.ImageButton
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView

class UnitActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_unit
        )

        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
            )

        val buttonHome =
            findViewById<ImageButton>(
                R.id.buttonHome
            )

        val buttonUnitSettings =
            findViewById<ImageButton>(
                R.id.buttonUnitSettings
            )

        val textUnitNumber =
            findViewById<TextView>(
                R.id.textUnitNumber
            )

        val textUnitTitle =
            findViewById<TextView>(
                R.id.textUnitTitle
            )

        val recyclerLessons =
            findViewById<RecyclerView>(
                R.id.recyclerLessons
            )

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        val unitId =
            intent.getStringExtra(
                "unitId"
            ) ?: return

        val unitNumber =
            intent.getIntExtra(
                "unitNumber",
                1
            )

        val unitTitle =
            intent.getStringExtra(
                "unitTitle"
            ) ?: ""

        textUnitNumber.text =
            "Unidad $unitNumber"

        textUnitNumber.setTextColor(
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    this,
                    routeId
                )
        )

        textUnitTitle.text =
            unitTitle

        buttonBack.setOnClickListener {

            finish()
        }

        buttonHome.setOnClickListener {

            NavigationStateRepository.saveRoutes(
                this
            )

            val intent =
                Intent(
                    this,
                    MainActivity::class.java
                )

            intent.flags =
                Intent.FLAG_ACTIVITY_NEW_TASK or
                        Intent.FLAG_ACTIVITY_CLEAR_TASK

            startActivity(
                intent
            )
        }

        buttonUnitSettings.setOnClickListener {

            val intent =
                Intent(
                    this,
                    UnitOptionsActivity::class.java
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
                unitId
            )

            intent.putExtra(
                "unitNumber",
                unitNumber
            )

            intent.putExtra(
                "unitTitle",
                unitTitle
            )

            startActivity(
                intent
            )
        }

        val lessons =
            LessonRepository.loadLessons(
                this,
                routeId,
                courseId,
                unitId
            )

        recyclerLessons.layoutManager =
            LinearLayoutManager(
                this
            )

        recyclerLessons.adapter =
            LessonAdapter(
                lessons,
                routeId
            ) { lesson ->

                val intent =
                    Intent(
                        this,
                        LessonActivity::class.java
                    )

                intent.putExtra(
                    "routeId",
                    routeId
                )

                intent.putExtra(
                    "courseId",
                    courseId
                )

                intent.putExtra("unitId", unitId)

                intent.putExtra(
                    "lessonId",
                    lesson.id
                )

                intent.putExtra(
                    "lessonNumber",
                    lesson.number
                )

                intent.putExtra(
                    "lessonTotal",
                    lessons.size
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

        val unitId =
            intent.getStringExtra(
                "unitId"
            ) ?: return

        NavigationStateRepository.saveUnit(
            this,
            courseId,
            unitId,
            routeId
        )

        val recyclerLessons =
            findViewById<RecyclerView>(
                R.id.recyclerLessons
            )

        recyclerLessons.adapter
            ?.notifyDataSetChanged()
    }
}

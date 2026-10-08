package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.os.Bundle
import android.widget.Button
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class CourseOptionsActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_course_options
        )

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        val courseNumber =
            intent.getIntExtra(
                "courseNumber",
                1
            )

        val courseTitle =
            intent.getStringExtra(
                "courseTitle"
            ) ?: ""

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

        val switchUseGlobal =
            findViewById<Switch>(
                R.id.switchUseGlobal
            )

        val switchBetweenLessons =
            findViewById<Switch>(
                R.id.switchBetweenLessons
            )

        val switchBetweenUnits =
            findViewById<Switch>(
                R.id.switchBetweenUnits
            )

        val switchBetweenCourses =
            findViewById<Switch>(
                R.id.switchBetweenCourses
            )

        val switchSwipeFinish =
            findViewById<Switch>(
                R.id.switchSwipeFinish
            )

        val buttonSave =
            findViewById<Button>(
                R.id.buttonSave
            )

        val buttonResetCourse =
            findViewById<Button>(
                R.id.buttonResetCourse
            )

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
            getString(R.string.nav_course_number, courseNumber, totalCourses)

        textCourseNumber.setTextColor(
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    this,
                    routeId
                )
        )

        textCourseTitle.text =
            courseTitle

        val hasOverride =
            LearningFlowSettingsRepository
                .hasCourseOverride(
                    this,
                    courseId
                )

        switchUseGlobal.isChecked =
            !hasOverride

        val settings =
            LearningFlowSettingsRepository
                .getSettingsForCourse(
                    this,
                    courseId
                )

        val originalUseGlobal =
            !hasOverride

        val originalBetweenLessons =
            settings.automaticBetweenLessons

        val originalBetweenUnits =
            settings.automaticBetweenUnits

        val originalBetweenCourses =
            settings.automaticBetweenCourses

        val originalSwipeFinish =
            settings.swipeCanFinishLesson

        switchBetweenLessons.isChecked =
            settings.automaticBetweenLessons

        switchBetweenUnits.isChecked =
            settings.automaticBetweenUnits

        switchBetweenCourses.isChecked =
            settings.automaticBetweenCourses

        switchSwipeFinish.isChecked =
            settings.swipeCanFinishLesson

        fun updateEnabledState() {

            val enabled =
                !switchUseGlobal.isChecked

            switchBetweenLessons.isEnabled =
                enabled

            switchBetweenUnits.isEnabled =
                enabled

            switchBetweenCourses.isEnabled =
                enabled

            switchSwipeFinish.isEnabled =
                enabled

            val alpha =
                if (
                    enabled
                ) {

                    1f

                } else {

                    0.45f
                }

            switchBetweenLessons.alpha =
                alpha

            switchBetweenUnits.alpha =
                alpha

            switchBetweenCourses.alpha =
                alpha

            switchSwipeFinish.alpha =
                alpha
        }

        fun updateSaveButtonState() {

            val useGlobalHasChanges =
                switchUseGlobal.isChecked !=
                        originalUseGlobal

            val courseSettingsHaveChanges =
                !switchUseGlobal.isChecked &&
                        (
                                switchBetweenLessons.isChecked !=
                                        originalBetweenLessons ||

                                        switchBetweenUnits.isChecked !=
                                        originalBetweenUnits ||

                                        switchBetweenCourses.isChecked !=
                                        originalBetweenCourses ||

                                        switchSwipeFinish.isChecked !=
                                        originalSwipeFinish
                                )

            val hasChanges =
                useGlobalHasChanges ||
                        courseSettingsHaveChanges

            buttonSave.isEnabled =
                hasChanges

            buttonSave.alpha =
                if (
                    hasChanges
                ) {

                    1f

                } else {

                    0.35f
                }
        }

        updateEnabledState()
        updateSaveButtonState()

        switchUseGlobal
            .setOnCheckedChangeListener {
                    _,
                    useGlobal ->

                if (
                    useGlobal
                ) {

                    val global =
                        LearningFlowSettingsRepository
                            .getGlobalSettings(
                                this
                            )

                    switchBetweenLessons.isChecked =
                        global.automaticBetweenLessons

                    switchBetweenUnits.isChecked =
                        global.automaticBetweenUnits

                    switchBetweenCourses.isChecked =
                        global.automaticBetweenCourses

                    switchSwipeFinish.isChecked =
                        global.swipeCanFinishLesson
                }

                updateEnabledState()
                updateSaveButtonState()
            }

        switchBetweenLessons
            .setOnCheckedChangeListener {
                    _,
                    _ ->

                updateSaveButtonState()
            }

        switchBetweenUnits
            .setOnCheckedChangeListener {
                    _,
                    _ ->

                updateSaveButtonState()
            }

        switchBetweenCourses
            .setOnCheckedChangeListener {
                    _,
                    _ ->

                updateSaveButtonState()
            }

        switchSwipeFinish
            .setOnCheckedChangeListener {
                    _,
                    _ ->

                updateSaveButtonState()
            }

        buttonBack.setOnClickListener {

            finish()
        }

        buttonResetCourse.setOnClickListener {

            AlertDialog.Builder(
                this
            )
                .setTitle(
                    getString(R.string.course_settings_reset)
                )
                .setMessage(
                    getString(R.string.course_settings_reset_confirmation)
                )
                .setNegativeButton(
                    getString(R.string.settings_cancel),
                    null
                )
                .setPositiveButton(
                    getString(R.string.settings_reset)
                ) { _, _ ->

                    ProgressRepository.resetCourse(
                        this,
                        routeId,
                        courseId
                    )

                    Toast.makeText(
                        this,
                        getString(R.string.course_settings_reset_success),
                        Toast.LENGTH_SHORT
                    ).show()

                    finish()
                }
                .show()
        }

        buttonSave.setOnClickListener {

            if (
                !buttonSave.isEnabled
            ) {

                return@setOnClickListener
            }

            if (
                switchUseGlobal.isChecked
            ) {

                LearningFlowSettingsRepository
                    .clearCourseOverride(
                        this,
                        courseId
                    )

            } else {

                val courseSettings =
                    LearningFlowSettings(

                        automaticBetweenLessons =
                            switchBetweenLessons.isChecked,

                        automaticBetweenUnits =
                            switchBetweenUnits.isChecked,

                        automaticBetweenCourses =
                            switchBetweenCourses.isChecked,

                        swipeCanFinishLesson =
                            switchSwipeFinish.isChecked
                    )

                LearningFlowSettingsRepository
                    .saveCourseSettings(
                        this,
                        courseId,
                        courseSettings
                    )
            }

            Toast.makeText(
                this,
                getString(R.string.course_settings_saved),
                Toast.LENGTH_SHORT
            ).show()

            finish()
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
    }
}
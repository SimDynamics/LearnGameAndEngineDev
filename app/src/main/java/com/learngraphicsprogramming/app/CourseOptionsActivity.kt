package com.learngraphicsprogramming.app

import android.os.Bundle
import android.widget.Button
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import android.app.AlertDialog

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

        textCourseNumber.text =
            "Curso $courseNumber de 44"

        textCourseNumber.setTextColor(
            AppearanceSettingsRepository
                .getAccentTextColor(
                    this
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
                if (enabled) {
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
                if (hasChanges) {
                    1f
                } else {
                    0.35f
                }
        }

        updateEnabledState()
        updateSaveButtonState()

        switchUseGlobal.setOnCheckedChangeListener { _, useGlobal ->

            if (useGlobal) {

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
            .setOnCheckedChangeListener { _, _ ->

                updateSaveButtonState()
            }

        switchBetweenUnits
            .setOnCheckedChangeListener { _, _ ->

                updateSaveButtonState()
            }

        switchBetweenCourses
            .setOnCheckedChangeListener { _, _ ->

                updateSaveButtonState()
            }

        switchSwipeFinish
            .setOnCheckedChangeListener { _, _ ->

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
                    "Reiniciar curso"
                )
                .setMessage(
                    "Se eliminarán el progreso y las respuestas " +
                            "de todas las unidades y lecciones " +
                            "de este curso.\n\n" +
                            "Esta acción no se puede deshacer."
                )
                .setNegativeButton(
                    "Cancelar",
                    null
                )
                .setPositiveButton(
                    "Reiniciar"
                ) { _, _ ->

                    ProgressRepository.resetCourse(
                        this,
                        courseId
                    )

                    Toast.makeText(
                        this,
                        "Curso reiniciado",
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
                "Ajustes del curso guardados",
                Toast.LENGTH_SHORT
            ).show()

            finish()
        }
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.apply(
            this
        )
    }
}
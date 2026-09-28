package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class LessonOptionsActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_lesson_options
        )

        val lessonId =
            intent.getStringExtra(
                "lessonId"
            ) ?: return

        val lessonNumber =
            intent.getIntExtra(
                "lessonNumber",
                1
            )

        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
            )

        val textLessonNumber =
            findViewById<TextView>(
                R.id.textLessonNumber
            )

        val buttonResetLesson =
            findViewById<Button>(
                R.id.buttonResetLesson
            )

        textLessonNumber.text =
            "Lección $lessonNumber"

        textLessonNumber.setTextColor(
            AppearanceSettingsRepository
                .getAccentTextColor(
                    this
                )
        )

        buttonBack.setOnClickListener {

            finish()
        }

        buttonResetLesson.setOnClickListener {

            AlertDialog.Builder(
                this
            )
                .setTitle(
                    "Reiniciar lección"
                )
                .setMessage(
                    "Se eliminarán todas las respuestas, " +
                            "los ejercicios completados, " +
                            "la página guardada y el progreso " +
                            "de esta lección.\n\n" +
                            "Esta acción no se puede deshacer."
                )
                .setNegativeButton(
                    "Cancelar",
                    null
                )
                .setPositiveButton(
                    "Reiniciar"
                ) { _, _ ->

                    ProgressRepository.resetLesson(
                        this,
                        lessonId
                    )

                    Toast.makeText(
                        this,
                        "Lección reiniciada",
                        Toast.LENGTH_SHORT
                    ).show()

                    /*
                     * Avisamos a LessonActivity
                     * para que también elimine
                     * su estado mantenido en memoria.
                     */
                    setResult(
                        RESULT_OK
                    )

                    finish()
                }
                .show()
        }
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.apply(
            this
        )
    }
}
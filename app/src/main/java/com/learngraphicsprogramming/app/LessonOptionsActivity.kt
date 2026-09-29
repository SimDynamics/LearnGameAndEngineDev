package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.graphics.Color
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
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

        val buttonEditProgrammingShortcuts =
            findViewById<Button>(
                R.id.buttonEditProgrammingShortcuts
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

        buttonEditProgrammingShortcuts
            .setOnClickListener {

                showProgrammingShortcutEditor()
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

                    setResult(
                        RESULT_OK
                    )

                    finish()
                }
                .show()
        }
    }

    private fun showProgrammingShortcutEditor() {

        /*
         * Tenemos 20 posiciones configurables.
         *
         * TAB no forma parte de ellas:
         * siempre permanece fijo.
         */
        val currentShortcuts =
            KeyboardShortcutSettingsRepository
                .getShortcuts(
                    this
                )
                .toMutableList()

        while (
            currentShortcuts.size <
            SHORTCUT_SLOT_COUNT
        ) {

            currentShortcuts.add(
                ""
            )
        }

        val inputs =
            mutableListOf<EditText>()

        val content =
            LinearLayout(this)

        content.orientation =
            LinearLayout.VERTICAL

        content.setPadding(
            dp(20),
            dp(8),
            dp(20),
            dp(20)
        )

        /*
         * Explicación.
         */
        val description =
            TextView(this)

        description.text =
            "TAB permanece fijo y equivale a 4 espacios.\n\n" +
                    "Puedes modificar las demás teclas. " +
                    "Deja una posición vacía para ocultarla."

        description.setTextColor(
            Color.rgb(
                174,
                180,
                190
            )
        )

        description.textSize =
            15f

        description.setPadding(
            0,
            0,
            0,
            dp(16)
        )

        content.addView(
            description
        )

        /*
         * Restaurar valores predeterminados.
         */
        val buttonDefaults =
            Button(this)

        buttonDefaults.text =
            "Restaurar predeterminados"

        buttonDefaults.isAllCaps =
            false

        content.addView(
            buttonDefaults
        )

        /*
         * 20 posiciones configurables.
         */
        for (
        index in 0 until
                SHORTCUT_SLOT_COUNT
        ) {

            val input =
                EditText(this)

            input.setSingleLine(
                true
            )

            input.hint =
                "Atajo ${index + 1}"

            input.setText(
                currentShortcuts[
                    index
                ]
            )

            input.inputType =
                InputType.TYPE_CLASS_TEXT or
                        InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS

            input.setTextColor(
                Color.WHITE
            )

            input.setHintTextColor(
                Color.rgb(
                    120,
                    125,
                    135
                )
            )

            input.gravity =
                Gravity.CENTER_VERTICAL

            val params =
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    dp(52)
                )

            params.setMargins(
                0,
                dp(8),
                0,
                0
            )

            input.layoutParams =
                params

            inputs.add(
                input
            )

            content.addView(
                input
            )
        }

        buttonDefaults.setOnClickListener {

            val defaults =
                KeyboardShortcutSettingsRepository
                    .getDefaultShortcuts()

            for (
            index in inputs.indices
            ) {

                inputs[index].setText(
                    defaults.getOrNull(
                        index
                    ) ?: ""
                )
            }
        }

        val scroll =
            ScrollView(this)

        scroll.addView(
            content
        )

        val dialog =
            AlertDialog.Builder(
                this
            )
                .setTitle(
                    "Atajos de programación"
                )
                .setView(
                    scroll
                )
                .setNegativeButton(
                    "Cancelar",
                    null
                )
                .setPositiveButton(
                    "Guardar",
                    null
                )
                .create()

        /*
         * Sobrescribimos Guardar para poder
         * procesar las 20 posiciones antes
         * de cerrar el diálogo.
         */
        dialog.setOnShowListener {

            dialog.getButton(
                AlertDialog.BUTTON_POSITIVE
            )
                .setOnClickListener {

                    val shortcuts =
                        inputs
                            .map {
                                it.text
                                    .toString()
                            }
                            .filter {
                                it.isNotEmpty()
                            }

                    KeyboardShortcutSettingsRepository
                        .saveShortcuts(
                            this,
                            shortcuts
                        )

                    Toast.makeText(
                        this,
                        "Atajos guardados",
                        Toast.LENGTH_SHORT
                    ).show()

                    dialog.dismiss()
                }
        }

        dialog.show()
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.apply(
            this
        )
    }

    private fun dp(
        value: Int
    ): Int {

        return (
                value *
                        resources
                            .displayMetrics
                            .density
                ).toInt()
    }

    private companion object {

        const val SHORTCUT_SLOT_COUNT =
            20
    }
}
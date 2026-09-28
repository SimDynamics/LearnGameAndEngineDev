package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.os.Bundle
import android.widget.Button
import android.widget.Switch
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView

class OptionsActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_options
        )

        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
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

        val buttonResetProgrammingRoute =
            findViewById<Button>(
                R.id.buttonResetProgrammingRoute
            )

        val textAccentRed =
            findViewById<TextView>(
                R.id.textAccentRed
            )

        val textAccentGreen =
            findViewById<TextView>(
                R.id.textAccentGreen
            )

        val textAccentBlue =
            findViewById<TextView>(
                R.id.textAccentBlue
            )

        val textAccentRgb =
            findViewById<TextView>(
                R.id.textAccentRgb
            )

        val seekAccentRed =
            findViewById<SeekBar>(
                R.id.seekAccentRed
            )

        val seekAccentGreen =
            findViewById<SeekBar>(
                R.id.seekAccentGreen
            )

        val seekAccentBlue =
            findViewById<SeekBar>(
                R.id.seekAccentBlue
            )

        val accentPreview =
            findViewById<LinearLayout>(
                R.id.accentPreview
            )

        val textAccentPreviewSecondary =
            findViewById<TextView>(
                R.id.textAccentPreviewSecondary
            )

        /*
         * Recuperamos la configuración
         * global actualmente guardada.
         */
        val settings =
            LearningFlowSettingsRepository
                .getGlobalSettings(
                    this
                )

        val originalAccent =
            AppearanceSettingsRepository
                .getAccentRgb(
                    this
                )

        val originalAccentRed =
            originalAccent.red

        val originalAccentGreen =
            originalAccent.green

        val originalAccentBlue =
            originalAccent.blue

        /*
         * Guardamos los valores originales.
         *
         * Estos serán nuestra referencia para
         * saber si realmente existe algo
         * pendiente por guardar.
         */
        val originalBetweenLessons =
            settings.automaticBetweenLessons

        val originalBetweenUnits =
            settings.automaticBetweenUnits

        val originalBetweenCourses =
            settings.automaticBetweenCourses

        val originalSwipeFinish =
            settings.swipeCanFinishLesson

        /*
         * Mostramos los valores actuales.
         */
        switchBetweenLessons.isChecked =
            originalBetweenLessons

        switchBetweenUnits.isChecked =
            originalBetweenUnits

        switchBetweenCourses.isChecked =
            originalBetweenCourses

        switchSwipeFinish.isChecked =
            originalSwipeFinish

        seekAccentRed.progress =
            originalAccentRed

        seekAccentGreen.progress =
            originalAccentGreen

        seekAccentBlue.progress =
            originalAccentBlue


        /*
         * ACTUALIZAR ESTADO DEL BOTÓN GUARDAR.
         *
         * Solo estará disponible cuando
         * al menos una opción sea diferente
         * a la configuración originalmente cargada.
         */
        fun updateSaveButtonState() {

            val flowHasChanges =
                switchBetweenLessons.isChecked !=
                        originalBetweenLessons ||

                        switchBetweenUnits.isChecked !=
                        originalBetweenUnits ||

                        switchBetweenCourses.isChecked !=
                        originalBetweenCourses ||

                        switchSwipeFinish.isChecked !=
                        originalSwipeFinish

            val appearanceHasChanges =
                seekAccentRed.progress !=
                        originalAccentRed ||

                        seekAccentGreen.progress !=
                        originalAccentGreen ||

                        seekAccentBlue.progress !=
                        originalAccentBlue

            val hasChanges =
                flowHasChanges ||
                        appearanceHasChanges

            buttonSave.isEnabled =
                hasChanges

            buttonSave.alpha =
                if (hasChanges) {
                    1f
                } else {
                    0.35f
                }
        }

        fun updateAccentPreview() {

            val red =
                seekAccentRed.progress

            val green =
                seekAccentGreen.progress

            val blue =
                seekAccentBlue.progress

            textAccentRed.text =
                "Rojo · $red"

            textAccentGreen.text =
                "Verde · $green"

            textAccentBlue.text =
                "Azul · $blue"

            textAccentRgb.text =
                "RGB($red, $green, $blue)"

            val primaryColor =
                Color.rgb(
                    red,
                    green,
                    blue
                )

            val secondaryColor =
                AppearanceSettingsRepository
                    .createAccentTextColor(
                        primaryColor
                    )

            val background =
                GradientDrawable()

            background.setColor(
                primaryColor
            )

            background.cornerRadius =
                14f * resources
                    .displayMetrics
                    .density

            accentPreview.background =
                background

            textAccentPreviewSecondary
                .setTextColor(
                    secondaryColor
                )
        }

        /*
         * Al entrar no existe nada pendiente.
         */
        updateAccentPreview()
        updateSaveButtonState()

        /*
         * Cada cambio vuelve a comparar
         * TODOS los ajustes con sus
         * valores originales.
         *
         * Si el usuario cambia algo y luego
         * lo devuelve manualmente a su valor
         * original, el botón vuelve a apagarse.
         */
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

        val accentSeekListener =
            object :
                SeekBar.OnSeekBarChangeListener {

                override fun onProgressChanged(
                    seekBar: SeekBar?,
                    progress: Int,
                    fromUser: Boolean
                ) {

                    updateAccentPreview()
                    updateSaveButtonState()
                }

                override fun onStartTrackingTouch(
                    seekBar: SeekBar?
                ) {
                }

                override fun onStopTrackingTouch(
                    seekBar: SeekBar?
                ) {
                }
            }

        seekAccentRed
            .setOnSeekBarChangeListener(
                accentSeekListener
            )

        seekAccentGreen
            .setOnSeekBarChangeListener(
                accentSeekListener
            )

        seekAccentBlue
            .setOnSeekBarChangeListener(
                accentSeekListener
            )

        buttonBack.setOnClickListener {

            finish()
        }

        /*
         * REINICIO DE PROGRESO.
         *
         * Es una acción inmediata.
         * No necesita utilizar Guardar cambios.
         */
        buttonResetProgrammingRoute
            .setOnClickListener {

                AlertDialog.Builder(
                    this
                )
                    .setTitle(
                        "Reiniciar progreso de Programación"
                    )
                    .setMessage(
                        "Se eliminarán el progreso, las respuestas " +
                                "y las páginas guardadas de todos los cursos " +
                                "de la ruta de Programación.\n\n" +
                                "Los ajustes generales y las configuraciones " +
                                "particulares de los cursos se conservarán.\n\n" +
                                "Esta acción no se puede deshacer."
                    )
                    .setNegativeButton(
                        "Cancelar",
                        null
                    )
                    .setPositiveButton(
                        "Reiniciar"
                    ) { _, _ ->

                        ProgressRepository
                            .resetProgrammingRoute(
                                this
                            )

                        Toast.makeText(
                            this,
                            "Progreso de Programación reiniciado",
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                    .show()
            }

        /*
         * GUARDAR ÚNICAMENTE LOS AJUSTES
         * QUE REQUIEREN CONFIRMACIÓN MANUAL.
         */
        buttonSave.setOnClickListener {

            if (
                !buttonSave.isEnabled
            ) {

                return@setOnClickListener
            }

            val newSettings =
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
                .saveGlobalSettings(
                    this,
                    newSettings
                )

            AppearanceSettingsRepository
                .saveAccentColor(
                    this,
                    seekAccentRed.progress,
                    seekAccentGreen.progress,
                    seekAccentBlue.progress
                )

            Toast.makeText(
                this,
                "Configuración guardada",
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
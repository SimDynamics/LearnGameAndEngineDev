package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.graphics.Color
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import android.content.Intent

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

        val routeId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        val lessonId =
            intent.getStringExtra(
                "lessonId"
            ) ?: return

        val lessonNumber =
            intent.getIntExtra(
                "lessonNumber",
                1
            )

        /*
         * VISTAS GENERALES.
         */
        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
            )

        val textLessonNumber =
            findViewById<TextView>(
                R.id.textLessonNumber
            )

        /*
         * TAMAÑO DEL CURSO.
         */
        val switchUseGlobalTextSize =
            findViewById<Switch>(
                R.id.switchUseGlobalTextSize
            )

        val textLessonTextSize =
            findViewById<TextView>(
                R.id.textLessonTextSize
            )

        val seekLessonTextSize =
            findViewById<SeekBar>(
                R.id.seekLessonTextSize
            )

        val textLessonTextSizePreview =
            findViewById<TextView>(
                R.id.textLessonTextSizePreview
            )

        val buttonSaveCourseSettings =
            findViewById<Button>(
                R.id.buttonSaveCourseSettings
            )

        val switchAllowGlobalWrapOverwrite =
            findViewById<Switch>(
                R.id.switchAllowGlobalWrapOverwrite
            )

        /*
         * PROGRAMACIÓN.
         */
        val buttonEditProgrammingShortcuts =
            findViewById<Button>(
                R.id.buttonEditProgrammingShortcuts
            )

        /*
         * PROGRESO.
         */
        val buttonResetLesson =
            findViewById<Button>(
                R.id.buttonResetLesson
            )

        val isProgramming = routeId == AppearanceSettingsRepository.ROUTE_PROGRAMMING
        val programmingVisibility = if (isProgramming) View.VISIBLE else View.GONE
        listOf(R.id.textCodeWrapHeading, R.id.codeWrapSettings,
            R.id.textProgrammingHeading, R.id.buttonEditProgrammingShortcuts,
            R.id.textProgrammingShortcutsDescription).forEach { id ->
            findViewById<View>(id).visibility = programmingVisibility
        }
        findViewById<TextView>(R.id.textLessonOptionsDescription).text =
            if (isProgramming) getString(R.string.lesson_settings_programming_description)
            else getString(R.string.lesson_settings_math_description)

        /*
         * CABECERA.
         */
        textLessonNumber.text =
            getString(R.string.nav_lesson_number, lessonNumber)

        textLessonNumber.setTextColor(
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    this,
                    routeId
                )
        )

        /*
         * VALORES GUARDADOS.
         */
        val globalTextScale =
            LessonTextSizeSettingsRepository
                .getGlobalScale(
                    this
                )

        val savedUseGlobal =
            LessonTextSizeSettingsRepository
                .usesGlobalScale(
                    this,
                    courseId
                )

        val savedCourseScale =
            LessonTextSizeSettingsRepository
                .getCourseScale(
                    this,
                    courseId
                )

        val savedAllowGlobalWrapOverwrite =
            CodeWrapSettingsRepository
                .allowsGlobalOverwrite(
                    this,
                    courseId
                )

        /*
         * VALORES TEMPORALES.
         *
         * Estos cambian mientras el usuario
         * utiliza la pantalla, pero todavía
         * NO modifican el curso.
         */
        var pendingUseGlobal =
            savedUseGlobal

        var pendingCourseScale =
            savedCourseScale

        var pendingAllowGlobalWrapOverwrite =
            savedAllowGlobalWrapOverwrite

        fun updateSaveButtonState() {

            val textSizeHasChanges =
                pendingUseGlobal !=
                        savedUseGlobal ||
                        (
                                !pendingUseGlobal &&
                                        pendingCourseScale !=
                                        savedCourseScale
                                )

            val wrapProtectionHasChanges = isProgramming &&
                pendingAllowGlobalWrapOverwrite != savedAllowGlobalWrapOverwrite

            val hasChanges =
                textSizeHasChanges ||
                        wrapProtectionHasChanges

            buttonSaveCourseSettings.isEnabled =
                hasChanges

            buttonSaveCourseSettings.alpha =
                if (
                    hasChanges
                ) {
                    1f
                } else {
                    0.35f
                }
        }

        /*
         * VISTA PREVIA.
         */
        fun updateCourseTextSizePreview() {

            val visibleScale =
                if (
                    pendingUseGlobal
                ) {

                    globalTextScale

                } else {

                    pendingCourseScale
                }

            textLessonTextSize.text =
                if (
                    pendingUseGlobal
                ) {

                    getString(R.string.lesson_settings_course_size_global, visibleScale)

                } else {

                    getString(R.string.lesson_settings_course_size, visibleScale)
                }

            textLessonTextSizePreview.textSize =
                LessonTextSizeSettingsRepository
                    .scaleTextSize(
                        17f,
                        visibleScale
                    )
        }

        /*
         * ESTADO VISUAL DEL SLIDER.
         */
        fun updateCourseTextSizeControls() {

            seekLessonTextSize.isEnabled =
                !pendingUseGlobal

            seekLessonTextSize.alpha =
                if (
                    pendingUseGlobal
                ) {
                    0.35f
                } else {
                    1f
                }

            textLessonTextSize.alpha =
                if (
                    pendingUseGlobal
                ) {
                    0.7f
                } else {
                    1f
                }

            updateCourseTextSizePreview()
        }

        /*
 * ESTADO INICIAL.
 */
        switchUseGlobalTextSize.isChecked =
            savedUseGlobal

        switchAllowGlobalWrapOverwrite.isChecked =
            savedAllowGlobalWrapOverwrite

        seekLessonTextSize.progress =
            (
                    if (
                        savedUseGlobal
                    ) {
                        globalTextScale
                    } else {
                        savedCourseScale
                    }
                    ) -
                    LessonTextSizeSettingsRepository.MIN_SCALE

        updateCourseTextSizeControls()
        updateSaveButtonState()

        /*
         * USAR TAMAÑO GLOBAL.
         *
         * Aquí SOLO modificamos el valor
         * temporal.
         */
        switchUseGlobalTextSize
            .setOnCheckedChangeListener {
                    _,
                    isChecked ->

                pendingUseGlobal =
                    isChecked

                seekLessonTextSize.progress =
                    (
                            if (
                                pendingUseGlobal
                            ) {
                                globalTextScale
                            } else {
                                pendingCourseScale
                            }
                            ) -
                            LessonTextSizeSettingsRepository.MIN_SCALE

                updateCourseTextSizeControls()
                updateSaveButtonState()
            }

        /*
         * TAMAÑO PERSONALIZADO DEL CURSO.
         *
         * Mientras se arrastra, únicamente
         * cambia la vista previa.
         */
        seekLessonTextSize
            .setOnSeekBarChangeListener(
                object :
                    SeekBar.OnSeekBarChangeListener {

                    override fun onProgressChanged(
                        seekBar: SeekBar?,
                        progress: Int,
                        fromUser: Boolean
                    ) {

                        if (
                            fromUser &&
                            !pendingUseGlobal
                        ) {

                            pendingCourseScale =
                                progress +
                                        LessonTextSizeSettingsRepository.MIN_SCALE
                        }

                        updateCourseTextSizePreview()
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
            )

        switchAllowGlobalWrapOverwrite
            .setOnCheckedChangeListener {
                    _,
                    isChecked ->

                pendingAllowGlobalWrapOverwrite =
                    isChecked

                updateSaveButtonState()
            }

        /*
         * GUARDAR AJUSTES DEL CURSO.
         *
         * Solo aquí escribimos realmente
         * los valores persistentes.
         */
        buttonSaveCourseSettings
            .setOnClickListener {

                LessonTextSizeSettingsRepository
                    .saveUsesGlobalScale(
                        this,
                        courseId,
                        pendingUseGlobal
                    )

                if (
                    !pendingUseGlobal
                ) {

                    LessonTextSizeSettingsRepository
                        .saveCourseScale(
                            this,
                            courseId,
                            pendingCourseScale
                        )
                }

                if (isProgramming) {
                    CodeWrapSettingsRepository.saveAllowsGlobalOverwrite(
                        this, courseId, pendingAllowGlobalWrapOverwrite
                    )
                }

                Toast.makeText(
                    this,
                    getString(R.string.course_settings_saved),
                    Toast.LENGTH_SHORT
                ).show()

                val resultIntent =
                    Intent()

                resultIntent.putExtra(
                    "courseSettingsChanged",
                    true
                )

                setResult(
                    RESULT_OK,
                    resultIntent
                )

                finish()
            }

        /*
         * ATRÁS.
         *
         * Si hubo modificaciones pero no
         * se pulsó Guardar, se descartan.
         */
        buttonBack.setOnClickListener {

            finish()
        }

        /*
         * ATAJOS DE PROGRAMACIÓN.
         */
        buttonEditProgrammingShortcuts
            .setOnClickListener {

                showProgrammingShortcutEditor()
            }

        /*
         * REINICIAR SOLO ESTA LECCIÓN.
         */
        buttonResetLesson.setOnClickListener {

            AlertDialog.Builder(
                this
            )
                .setTitle(
                    getString(R.string.lesson_settings_reset)
                )
                .setMessage(
                    getString(R.string.lesson_settings_reset_confirmation)
                )
                .setNegativeButton(
                    getString(R.string.settings_cancel),
                    null
                )
                .setPositiveButton(
                    getString(R.string.settings_reset)
                ) { _, _ ->

                    ProgressRepository.resetLesson(
                        this,
                        routeId,
                        lessonId
                    )

                    Toast.makeText(
                        this,
                        getString(R.string.lesson_settings_reset_success),
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
            getString(R.string.lesson_settings_shortcuts_help)

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
            getString(R.string.lesson_settings_shortcuts_defaults)

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
                getString(R.string.lesson_shortcut_hint, index + 1)

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
                    getString(R.string.lesson_settings_shortcuts_title)
                )
                .setView(
                    scroll
                )
                .setNegativeButton(
                    getString(R.string.settings_cancel),
                    null
                )
                .setPositiveButton(
                    getString(R.string.lesson_settings_shortcuts_save),
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
                        getString(R.string.lesson_settings_shortcuts_saved),
                        Toast.LENGTH_SHORT
                    ).show()

                    dialog.dismiss()
                }
        }

        dialog.show()
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
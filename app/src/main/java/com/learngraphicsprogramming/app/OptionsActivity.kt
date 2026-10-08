package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.content.SharedPreferences
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.Switch
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.text.Editable
import android.text.TextWatcher
import android.view.inputmethod.EditorInfo
import android.widget.EditText


class OptionsActivity : AppCompatActivity() {

    /*
     * Actualiza la interfaz cuando Google Play
     * confirma un cambio en el estado Premium.
     */
    private val premiumStateListener =
        SharedPreferences.OnSharedPreferenceChangeListener {
                _,
                key ->

            if (key == "state") {

                runOnUiThread {
                    updatePremiumUi()
                }
            }
        }

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

        val buttonPremiumPurchase =
            findViewById<Button>(
                R.id.buttonPremiumPurchase
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

        val switchGlobalCodeWrap =
            findViewById<Switch>(
                R.id.switchGlobalCodeWrap
            )

        val buttonSave =
            findViewById<Button>(
                R.id.buttonSave
            )

        val buttonResetProgrammingRoute =
            findViewById<Button>(
                R.id.buttonResetProgrammingRoute
            )

        val buttonResetProgrammingWrapExceptions =
            findViewById<Button>(
                R.id.buttonResetProgrammingWrapExceptions
            )

        val buttonResetMathPhysicsWrapExceptions =
            findViewById<Button>(
                R.id.buttonResetMathPhysicsWrapExceptions
            )

        val textGlobalTextSize =
            findViewById<TextView>(
                R.id.textGlobalTextSize
            )

        val seekGlobalTextSize =
            findViewById<SeekBar>(
                R.id.seekGlobalTextSize
            )

        val textGlobalTextSizePreview =
            findViewById<TextView>(
                R.id.textGlobalTextSizePreview
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
 * COLOR DE PROGRAMACIÓN.
 */
        val switchProgrammingAllowGlobalAccentOverwrite =
            findViewById<Switch>(
                R.id.switchProgrammingAllowGlobalAccentOverwrite
            )

        val textProgrammingAccentRed =
            findViewById<TextView>(
                R.id.textProgrammingAccentRed
            )

        val textProgrammingAccentGreen =
            findViewById<TextView>(
                R.id.textProgrammingAccentGreen
            )

        val textProgrammingAccentBlue =
            findViewById<TextView>(
                R.id.textProgrammingAccentBlue
            )



        val seekProgrammingAccentRed =
            findViewById<SeekBar>(
                R.id.seekProgrammingAccentRed
            )

        val seekProgrammingAccentGreen =
            findViewById<SeekBar>(
                R.id.seekProgrammingAccentGreen
            )

        val seekProgrammingAccentBlue =
            findViewById<SeekBar>(
                R.id.seekProgrammingAccentBlue
            )

        val programmingAccentPreview =
            findViewById<LinearLayout>(
                R.id.programmingAccentPreview
            )

        val textProgrammingAccentPreviewSecondary =
            findViewById<TextView>(
                R.id.textProgrammingAccentPreviewSecondary
            )

        /*
         * COLOR DE MATEMÁTICAS Y FÍSICA.
         */
        val switchMathPhysicsAllowGlobalAccentOverwrite =
            findViewById<Switch>(
                R.id.switchMathPhysicsAllowGlobalAccentOverwrite
            )

        val textMathPhysicsAccentRed =
            findViewById<TextView>(
                R.id.textMathPhysicsAccentRed
            )

        val textMathPhysicsAccentGreen =
            findViewById<TextView>(
                R.id.textMathPhysicsAccentGreen
            )

        val textMathPhysicsAccentBlue =
            findViewById<TextView>(
                R.id.textMathPhysicsAccentBlue
            )



        val seekMathPhysicsAccentRed =
            findViewById<SeekBar>(
                R.id.seekMathPhysicsAccentRed
            )

        val seekMathPhysicsAccentGreen =
            findViewById<SeekBar>(
                R.id.seekMathPhysicsAccentGreen
            )

        val seekMathPhysicsAccentBlue =
            findViewById<SeekBar>(
                R.id.seekMathPhysicsAccentBlue
            )

        val mathPhysicsAccentPreview =
            findViewById<LinearLayout>(
                R.id.mathPhysicsAccentPreview
            )

        val textMathPhysicsAccentPreviewSecondary =
            findViewById<TextView>(
                R.id.textMathPhysicsAccentPreviewSecondary
            )

        /*
 * ENTRADA MANUAL DEL COLOR GLOBAL.
 */
        val inputAccentRed =
            findViewById<EditText>(
                R.id.inputAccentRed
            )

        val inputAccentGreen =
            findViewById<EditText>(
                R.id.inputAccentGreen
            )

        val inputAccentBlue =
            findViewById<EditText>(
                R.id.inputAccentBlue
            )



        /*
         * ENTRADA MANUAL DE PROGRAMACIÓN.
         */
        val inputProgrammingAccentRed =
            findViewById<EditText>(
                R.id.inputProgrammingAccentRed
            )

        val inputProgrammingAccentGreen =
            findViewById<EditText>(
                R.id.inputProgrammingAccentGreen
            )

        val inputProgrammingAccentBlue =
            findViewById<EditText>(
                R.id.inputProgrammingAccentBlue
            )

        /*
         * ENTRADA MANUAL DE MATEMÁTICAS Y FÍSICA.
         */
        val inputMathPhysicsAccentRed =
            findViewById<EditText>(
                R.id.inputMathPhysicsAccentRed
            )

        val inputMathPhysicsAccentGreen =
            findViewById<EditText>(
                R.id.inputMathPhysicsAccentGreen
            )

        val inputMathPhysicsAccentBlue =
            findViewById<EditText>(
                R.id.inputMathPhysicsAccentBlue
            )

        /*
 * RESTABLECER COLORES PREDETERMINADOS.
 */
        val buttonResetGlobalAccentDefault =
            findViewById<Button>(
                R.id.buttonResetGlobalAccentDefault
            )

        val buttonResetProgrammingAccentDefault =
            findViewById<Button>(
                R.id.buttonResetProgrammingAccentDefault
            )

        val buttonResetMathPhysicsAccentDefault =
            findViewById<Button>(
                R.id.buttonResetMathPhysicsAccentDefault
            )

        /*
         * VALORES PREDETERMINADOS GLOBALES.
         */
        val inputDefaultAccentRed =
            findViewById<EditText>(
                R.id.inputDefaultAccentRed
            )

        val inputDefaultAccentGreen =
            findViewById<EditText>(
                R.id.inputDefaultAccentGreen
            )

        val inputDefaultAccentBlue =
            findViewById<EditText>(
                R.id.inputDefaultAccentBlue
            )

        /*
         * VALORES PREDETERMINADOS DE PROGRAMACIÓN.
         */
        val inputDefaultProgrammingAccentRed =
            findViewById<EditText>(
                R.id.inputDefaultProgrammingAccentRed
            )

        val inputDefaultProgrammingAccentGreen =
            findViewById<EditText>(
                R.id.inputDefaultProgrammingAccentGreen
            )

        val inputDefaultProgrammingAccentBlue =
            findViewById<EditText>(
                R.id.inputDefaultProgrammingAccentBlue
            )

        /*
         * VALORES PREDETERMINADOS DE MATEMÁTICAS Y FÍSICA.
         */
        val inputDefaultMathPhysicsAccentRed =
            findViewById<EditText>(
                R.id.inputDefaultMathPhysicsAccentRed
            )

        val inputDefaultMathPhysicsAccentGreen =
            findViewById<EditText>(
                R.id.inputDefaultMathPhysicsAccentGreen
            )

        val inputDefaultMathPhysicsAccentBlue =
            findViewById<EditText>(
                R.id.inputDefaultMathPhysicsAccentBlue
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

        val originalGlobalTextScale =
            LessonTextSizeSettingsRepository
                .getGlobalScale(
                    this
                )

        val originalGlobalCodeWrap =
            CodeWrapSettingsRepository
                .getGlobalWrap(
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
 * COLOR ORIGINAL DE PROGRAMACIÓN.
 */
        val originalProgrammingAccent =
            AppearanceSettingsRepository
                .getRouteAccentRgb(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )

        val originalProgrammingAllowGlobalAccentOverwrite =
            AppearanceSettingsRepository
                .allowsGlobalAccentOverwrite(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )

        /*
         * COLOR ORIGINAL DE MATEMÁTICAS Y FÍSICA.
         */
        val originalMathPhysicsAccent =
            AppearanceSettingsRepository
                .getRouteAccentRgb(
                    this,
                    AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
                )

        val originalMathPhysicsAllowGlobalAccentOverwrite =
            AppearanceSettingsRepository
                .allowsGlobalAccentOverwrite(
                    this,
                    AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
                )

        /*
 * VALORES PREDETERMINADOS ORIGINALES.
 */
        val originalGlobalDefaultAccent =
            AppearanceSettingsRepository
                .getDefaultAccentRgb(
                    this
                )

        val originalProgrammingDefaultAccent =
            AppearanceSettingsRepository
                .getRouteDefaultAccentRgb(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )

        val originalMathPhysicsDefaultAccent =
            AppearanceSettingsRepository
                .getRouteDefaultAccentRgb(
                    this,
                    AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
                )

        /*
         * VALORES PREDETERMINADOS TEMPORALES.
         *
         * Solo se guardarán realmente al pulsar
         * Guardar cambios.
         */
        var pendingGlobalDefaultAccent =
            originalGlobalDefaultAccent

        var pendingProgrammingDefaultAccent =
            originalProgrammingDefaultAccent

        var pendingMathPhysicsDefaultAccent =
            originalMathPhysicsDefaultAccent

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

        switchGlobalCodeWrap.isChecked =
            originalGlobalCodeWrap

        seekGlobalTextSize.progress =
            originalGlobalTextScale -
                    LessonTextSizeSettingsRepository.MIN_SCALE

        seekAccentRed.progress =
            originalAccentRed

        seekAccentGreen.progress =
            originalAccentGreen

        seekAccentBlue.progress =
            originalAccentBlue

        /*
 * PROGRAMACIÓN.
 */
        switchProgrammingAllowGlobalAccentOverwrite.isChecked =
            originalProgrammingAllowGlobalAccentOverwrite

        seekProgrammingAccentRed.progress =
            originalProgrammingAccent.red

        seekProgrammingAccentGreen.progress =
            originalProgrammingAccent.green

        seekProgrammingAccentBlue.progress =
            originalProgrammingAccent.blue

        /*
         * MATEMÁTICAS Y FÍSICA.
         */
        switchMathPhysicsAllowGlobalAccentOverwrite.isChecked =
            originalMathPhysicsAllowGlobalAccentOverwrite

        seekMathPhysicsAccentRed.progress =
            originalMathPhysicsAccent.red

        seekMathPhysicsAccentGreen.progress =
            originalMathPhysicsAccent.green

        seekMathPhysicsAccentBlue.progress =
            originalMathPhysicsAccent.blue

        /*
 * PREDETERMINADOS GLOBALES.
 */
        inputDefaultAccentRed.setText(
            originalGlobalDefaultAccent.red.toString()
        )

        inputDefaultAccentGreen.setText(
            originalGlobalDefaultAccent.green.toString()
        )

        inputDefaultAccentBlue.setText(
            originalGlobalDefaultAccent.blue.toString()
        )

        /*
         * PREDETERMINADOS DE PROGRAMACIÓN.
         */
        inputDefaultProgrammingAccentRed.setText(
            originalProgrammingDefaultAccent.red.toString()
        )

        inputDefaultProgrammingAccentGreen.setText(
            originalProgrammingDefaultAccent.green.toString()
        )

        inputDefaultProgrammingAccentBlue.setText(
            originalProgrammingDefaultAccent.blue.toString()
        )

        /*
         * PREDETERMINADOS DE MATEMÁTICAS Y FÍSICA.
         */
        inputDefaultMathPhysicsAccentRed.setText(
            originalMathPhysicsDefaultAccent.red.toString()
        )

        inputDefaultMathPhysicsAccentGreen.setText(
            originalMathPhysicsDefaultAccent.green.toString()
        )

        inputDefaultMathPhysicsAccentBlue.setText(
            originalMathPhysicsDefaultAccent.blue.toString()
        )

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

            val textSizeHasChanges =
                (
                        seekGlobalTextSize.progress +
                                LessonTextSizeSettingsRepository.MIN_SCALE
                        ) != originalGlobalTextScale

            val codeWrapHasChanges =
                switchGlobalCodeWrap.isChecked !=
                        originalGlobalCodeWrap

            val appearanceHasChanges =
                seekAccentRed.progress !=
                        originalAccentRed ||

                        seekAccentGreen.progress !=
                        originalAccentGreen ||

                        seekAccentBlue.progress !=
                        originalAccentBlue

            val programmingAccentHasChanges =
                seekProgrammingAccentRed.progress !=
                        originalProgrammingAccent.red ||

                        seekProgrammingAccentGreen.progress !=
                        originalProgrammingAccent.green ||

                        seekProgrammingAccentBlue.progress !=
                        originalProgrammingAccent.blue ||

                        switchProgrammingAllowGlobalAccentOverwrite.isChecked !=
                        originalProgrammingAllowGlobalAccentOverwrite

            val mathPhysicsAccentHasChanges =
                seekMathPhysicsAccentRed.progress !=
                        originalMathPhysicsAccent.red ||

                        seekMathPhysicsAccentGreen.progress !=
                        originalMathPhysicsAccent.green ||

                        seekMathPhysicsAccentBlue.progress !=
                        originalMathPhysicsAccent.blue ||

                        switchMathPhysicsAllowGlobalAccentOverwrite.isChecked !=
                        originalMathPhysicsAllowGlobalAccentOverwrite

            val defaultAccentHasChanges =
                pendingGlobalDefaultAccent !=
                        originalGlobalDefaultAccent ||

                        pendingProgrammingDefaultAccent !=
                        originalProgrammingDefaultAccent ||

                        pendingMathPhysicsDefaultAccent !=
                        originalMathPhysicsDefaultAccent

            val hasChanges =
                flowHasChanges ||
                        appearanceHasChanges ||
                        textSizeHasChanges ||
                        codeWrapHasChanges ||
                        programmingAccentHasChanges ||
                        mathPhysicsAccentHasChanges ||
                        defaultAccentHasChanges

            buttonSave.isEnabled =
                hasChanges

            buttonSave.alpha =
                if (hasChanges) {
                    1f
                } else {
                    0.35f
                }
        }

        fun updateGlobalTextSizePreview() {

            val scale =
                seekGlobalTextSize.progress +
                        LessonTextSizeSettingsRepository.MIN_SCALE

            textGlobalTextSize.text =
                "Tamaño del contenido · $scale %"

            textGlobalTextSizePreview.textSize =
                LessonTextSizeSettingsRepository
                    .scaleTextSize(
                        17f,
                        scale
                    )
        }

        var updatingGlobalAccentInputs =
            false

        var updatingProgrammingAccentInputs =
            false

        var updatingMathPhysicsAccentInputs =
            false

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


            if (
                !updatingGlobalAccentInputs
            ) {

                updatingGlobalAccentInputs =
                    true

                if (
                    !inputAccentRed.hasFocus()
                ) {
                    inputAccentRed.setText(
                        red.toString()
                    )
                }

                if (
                    !inputAccentGreen.hasFocus()
                ) {
                    inputAccentGreen.setText(
                        green.toString()
                    )
                }

                if (
                    !inputAccentBlue.hasFocus()
                ) {
                    inputAccentBlue.setText(
                        blue.toString()
                    )
                }

                updatingGlobalAccentInputs =
                    false
            }

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

        fun updateProgrammingAccentPreview() {

            val red =
                seekProgrammingAccentRed.progress

            val green =
                seekProgrammingAccentGreen.progress

            val blue =
                seekProgrammingAccentBlue.progress

            textProgrammingAccentRed.text =
                "Rojo · $red"

            textProgrammingAccentGreen.text =
                "Verde · $green"

            textProgrammingAccentBlue.text =
                "Azul · $blue"


            if (
                !updatingProgrammingAccentInputs
            ) {

                updatingProgrammingAccentInputs =
                    true

                if (
                    !inputProgrammingAccentRed.hasFocus()
                ) {
                    inputProgrammingAccentRed.setText(
                        red.toString()
                    )
                }

                if (
                    !inputProgrammingAccentGreen.hasFocus()
                ) {
                    inputProgrammingAccentGreen.setText(
                        green.toString()
                    )
                }

                if (
                    !inputProgrammingAccentBlue.hasFocus()
                ) {
                    inputProgrammingAccentBlue.setText(
                        blue.toString()
                    )
                }

                updatingProgrammingAccentInputs =
                    false
            }

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
                14f *
                        resources
                            .displayMetrics
                            .density

            programmingAccentPreview.background =
                background

            textProgrammingAccentPreviewSecondary
                .setTextColor(
                    secondaryColor
                )
        }

        fun updateMathPhysicsAccentPreview() {

            val red =
                seekMathPhysicsAccentRed.progress

            val green =
                seekMathPhysicsAccentGreen.progress

            val blue =
                seekMathPhysicsAccentBlue.progress

            textMathPhysicsAccentRed.text =
                "Rojo · $red"

            textMathPhysicsAccentGreen.text =
                "Verde · $green"

            textMathPhysicsAccentBlue.text =
                "Azul · $blue"

            if (
                !updatingMathPhysicsAccentInputs
            ) {

                updatingMathPhysicsAccentInputs =
                    true

                inputMathPhysicsAccentRed.setText(
                    red.toString()
                )

                inputMathPhysicsAccentGreen.setText(
                    green.toString()
                )

                inputMathPhysicsAccentBlue.setText(
                    blue.toString()
                )

                updatingMathPhysicsAccentInputs =
                    false
            }

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
                14f *
                        resources
                            .displayMetrics
                            .density

            mathPhysicsAccentPreview.background =
                background

            textMathPhysicsAccentPreviewSecondary
                .setTextColor(
                    secondaryColor
                )
        }

        /*
 * CONECTA UNA CAJA R, G O B
 * CON SU SLIDER.
 */
        fun bindAccentChannelInput(
            input: EditText,
            seekBar: SeekBar,
            isInternalUpdate: () -> Boolean,
            refreshPreview: () -> Unit
        ) {

            /*
             * Evita recursión cuando esta misma
             * función corrige, por ejemplo:
             *
             * 999 → 255
             */
            var normalizingInput =
                false

            input.addTextChangedListener(
                object : TextWatcher {

                    override fun beforeTextChanged(
                        s: CharSequence?,
                        start: Int,
                        count: Int,
                        after: Int
                    ) {
                    }

                    override fun onTextChanged(
                        s: CharSequence?,
                        start: Int,
                        before: Int,
                        count: Int
                    ) {
                    }

                    override fun afterTextChanged(
                        editable: Editable?
                    ) {

                        if (
                            isInternalUpdate() ||
                            normalizingInput
                        ) {
                            return
                        }

                        val text =
                            editable
                                ?.toString()
                                ?.trim()
                                .orEmpty()

                        if (
                            text.isEmpty()
                        ) {
                            return
                        }

                        val value =
                            text.toIntOrNull()
                                ?: return

                        val limitedValue =
                            value.coerceIn(
                                0,
                                255
                            )

                        /*
                         * Si el usuario escribió un
                         * valor fuera del rango,
                         * normalizamos la propia caja
                         * inmediatamente.
                         */
                        if (
                            value != limitedValue
                        ) {

                            normalizingInput =
                                true

                            input.setText(
                                limitedValue.toString()
                            )

                            input.setSelection(
                                input.text.length
                            )

                            normalizingInput =
                                false
                        }

                        seekBar.progress =
                            limitedValue

                        refreshPreview()
                        updateSaveButtonState()
                    }
                }
            )
        }

        /*
 * CONECTA UNA CAJA DE VALOR PREDETERMINADO.
 *
 * No modifica inmediatamente el color actual.
 * Solo actualiza el valor pendiente que se
 * guardará con Guardar cambios.
 */
        fun bindDefaultAccentInput(
            input: EditText,
            onValueChanged: (Int) -> Unit
        ) {

            var normalizingInput =
                false

            input.addTextChangedListener(
                object : TextWatcher {

                    override fun beforeTextChanged(
                        s: CharSequence?,
                        start: Int,
                        count: Int,
                        after: Int
                    ) {
                    }

                    override fun onTextChanged(
                        s: CharSequence?,
                        start: Int,
                        before: Int,
                        count: Int
                    ) {
                    }

                    override fun afterTextChanged(
                        editable: Editable?
                    ) {

                        if (
                            normalizingInput
                        ) {
                            return
                        }

                        val text =
                            editable
                                ?.toString()
                                ?.trim()
                                .orEmpty()

                        if (
                            text.isEmpty()
                        ) {
                            return
                        }

                        val value =
                            text.toIntOrNull()
                                ?: return

                        val limitedValue =
                            value.coerceIn(
                                0,
                                255
                            )

                        if (
                            value != limitedValue
                        ) {

                            normalizingInput =
                                true

                            input.setText(
                                limitedValue.toString()
                            )

                            input.setSelection(
                                input.text.length
                            )

                            normalizingInput =
                                false
                        }

                        onValueChanged(
                            limitedValue
                        )

                        updateSaveButtonState()
                    }
                }
            )
        }

        /*
 * PREDETERMINADOS GLOBALES.
 */
        bindDefaultAccentInput(
            inputDefaultAccentRed
        ) { value ->

            pendingGlobalDefaultAccent =
                pendingGlobalDefaultAccent.copy(
                    red = value
                )
        }

        bindDefaultAccentInput(
            inputDefaultAccentGreen
        ) { value ->

            pendingGlobalDefaultAccent =
                pendingGlobalDefaultAccent.copy(
                    green = value
                )
        }

        bindDefaultAccentInput(
            inputDefaultAccentBlue
        ) { value ->

            pendingGlobalDefaultAccent =
                pendingGlobalDefaultAccent.copy(
                    blue = value
                )
        }

        /*
 * PREDETERMINADOS DE PROGRAMACIÓN.
 */
        bindDefaultAccentInput(
            inputDefaultProgrammingAccentRed
        ) { value ->

            pendingProgrammingDefaultAccent =
                pendingProgrammingDefaultAccent.copy(
                    red = value
                )
        }

        bindDefaultAccentInput(
            inputDefaultProgrammingAccentGreen
        ) { value ->

            pendingProgrammingDefaultAccent =
                pendingProgrammingDefaultAccent.copy(
                    green = value
                )
        }

        bindDefaultAccentInput(
            inputDefaultProgrammingAccentBlue
        ) { value ->

            pendingProgrammingDefaultAccent =
                pendingProgrammingDefaultAccent.copy(
                    blue = value
                )
        }

        /*
 * PREDETERMINADOS DE MATEMÁTICAS Y FÍSICA.
 */
        bindDefaultAccentInput(
            inputDefaultMathPhysicsAccentRed
        ) { value ->

            pendingMathPhysicsDefaultAccent =
                pendingMathPhysicsDefaultAccent.copy(
                    red = value
                )
        }

        bindDefaultAccentInput(
            inputDefaultMathPhysicsAccentGreen
        ) { value ->

            pendingMathPhysicsDefaultAccent =
                pendingMathPhysicsDefaultAccent.copy(
                    green = value
                )
        }

        bindDefaultAccentInput(
            inputDefaultMathPhysicsAccentBlue
        ) { value ->

            pendingMathPhysicsDefaultAccent =
                pendingMathPhysicsDefaultAccent.copy(
                    blue = value
                )
        }

        /*
         * APLICA UNA ENTRADA RGB COMPLETA.
         *
         * Acepta, por ejemplo:
         *
         * 228, 50, 72
         * RGB(228, 50, 72)
         */
        fun applyCompleteRgbInput(
            input: EditText,
            redSeekBar: SeekBar,
            greenSeekBar: SeekBar,
            blueSeekBar: SeekBar,
            isInternalUpdate: () -> Boolean,
            refreshPreview: () -> Unit
        ): Boolean {

            if (
                isInternalUpdate()
            ) {
                return false
            }

            val values =
                Regex(
                    """\d+"""
                )
                    .findAll(
                        input.text
                            .toString()
                    )
                    .map {
                        it.value.toIntOrNull()
                    }
                    .filterNotNull()
                    .toList()

            if (
                values.size != 3
            ) {

                input.error =
                    "Escribe tres valores RGB"

                return false
            }

            val red =
                values[0].coerceIn(
                    0,
                    255
                )

            val green =
                values[1].coerceIn(
                    0,
                    255
                )

            val blue =
                values[2].coerceIn(
                    0,
                    255
                )

            redSeekBar.progress =
                red

            greenSeekBar.progress =
                green

            blueSeekBar.progress =
                blue

            /*
             * Normalizamos también la caja RGB
             * completa aunque todavía conserve
             * el foco.
             */
            input.setText(
                "RGB($red, $green, $blue)"
            )

            input.setSelection(
                input.text.length
            )

            input.error =
                null

            refreshPreview()
            updateSaveButtonState()

            return true
        }

        /*
 * COLOR GLOBAL.
 */
        bindAccentChannelInput(
            inputAccentRed,
            seekAccentRed,
            {
                updatingGlobalAccentInputs
            },
            {
                updateAccentPreview()
            }
        )

        bindAccentChannelInput(
            inputAccentGreen,
            seekAccentGreen,
            {
                updatingGlobalAccentInputs
            },
            {
                updateAccentPreview()
            }
        )

        bindAccentChannelInput(
            inputAccentBlue,
            seekAccentBlue,
            {
                updatingGlobalAccentInputs
            },
            {
                updateAccentPreview()
            }
        )

        /*
         * PROGRAMACIÓN.
         */
        bindAccentChannelInput(
            inputProgrammingAccentRed,
            seekProgrammingAccentRed,
            {
                updatingProgrammingAccentInputs
            },
            {
                updateProgrammingAccentPreview()
            }
        )

        bindAccentChannelInput(
            inputProgrammingAccentGreen,
            seekProgrammingAccentGreen,
            {
                updatingProgrammingAccentInputs
            },
            {
                updateProgrammingAccentPreview()
            }
        )

        bindAccentChannelInput(
            inputProgrammingAccentBlue,
            seekProgrammingAccentBlue,
            {
                updatingProgrammingAccentInputs
            },
            {
                updateProgrammingAccentPreview()
            }
        )

        /*
         * MATEMÁTICAS Y FÍSICA.
         */
        bindAccentChannelInput(
            inputMathPhysicsAccentRed,
            seekMathPhysicsAccentRed,
            {
                updatingMathPhysicsAccentInputs
            },
            {
                updateMathPhysicsAccentPreview()
            }
        )

        bindAccentChannelInput(
            inputMathPhysicsAccentGreen,
            seekMathPhysicsAccentGreen,
            {
                updatingMathPhysicsAccentInputs
            },
            {
                updateMathPhysicsAccentPreview()
            }
        )

        bindAccentChannelInput(
            inputMathPhysicsAccentBlue,
            seekMathPhysicsAccentBlue,
            {
                updatingMathPhysicsAccentInputs
            },
            {
                updateMathPhysicsAccentPreview()
            }
        )

        /*
 * RGB COMPLETO GLOBAL.
 */



        /*
         * RGB COMPLETO DE PROGRAMACIÓN.
         */

        /*
         * RGB COMPLETO DE MATEMÁTICAS Y FÍSICA.
         */

        /*
 * Al entrar no existe nada pendiente.
 */
        updateGlobalTextSizePreview()
        updateAccentPreview()
        updateProgrammingAccentPreview()
        updateMathPhysicsAccentPreview()
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

        switchGlobalCodeWrap
            .setOnCheckedChangeListener { _, _ ->

                updateSaveButtonState()
            }

        seekGlobalTextSize
            .setOnSeekBarChangeListener(
                object :
                    SeekBar.OnSeekBarChangeListener {

                    override fun onProgressChanged(
                        seekBar: SeekBar?,
                        progress: Int,
                        fromUser: Boolean
                    ) {

                        updateGlobalTextSizePreview()
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

        /*
 * PROGRAMACIÓN.
 */
        val programmingAccentSeekListener =
            object :
                SeekBar.OnSeekBarChangeListener {

                override fun onProgressChanged(
                    seekBar: SeekBar?,
                    progress: Int,
                    fromUser: Boolean
                ) {

                    updateProgrammingAccentPreview()
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

        seekProgrammingAccentRed
            .setOnSeekBarChangeListener(
                programmingAccentSeekListener
            )

        seekProgrammingAccentGreen
            .setOnSeekBarChangeListener(
                programmingAccentSeekListener
            )

        seekProgrammingAccentBlue
            .setOnSeekBarChangeListener(
                programmingAccentSeekListener
            )

        switchProgrammingAllowGlobalAccentOverwrite
            .setOnCheckedChangeListener { _, _ ->

                updateSaveButtonState()
            }

        /*
         * MATEMÁTICAS Y FÍSICA.
         */
        val mathPhysicsAccentSeekListener =
            object :
                SeekBar.OnSeekBarChangeListener {

                override fun onProgressChanged(
                    seekBar: SeekBar?,
                    progress: Int,
                    fromUser: Boolean
                ) {

                    updateMathPhysicsAccentPreview()
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

        seekMathPhysicsAccentRed
            .setOnSeekBarChangeListener(
                mathPhysicsAccentSeekListener
            )

        seekMathPhysicsAccentGreen
            .setOnSeekBarChangeListener(
                mathPhysicsAccentSeekListener
            )

        seekMathPhysicsAccentBlue
            .setOnSeekBarChangeListener(
                mathPhysicsAccentSeekListener
            )

        switchMathPhysicsAllowGlobalAccentOverwrite
            .setOnCheckedChangeListener { _, _ ->

                updateSaveButtonState()
            }

        buttonResetGlobalAccentDefault
            .setOnClickListener {

                seekAccentRed.progress =
                    pendingGlobalDefaultAccent.red

                seekAccentGreen.progress =
                    pendingGlobalDefaultAccent.green

                seekAccentBlue.progress =
                    pendingGlobalDefaultAccent.blue

                updateAccentPreview()
                updateSaveButtonState()
            }

        buttonResetProgrammingAccentDefault
            .setOnClickListener {

                seekProgrammingAccentRed.progress =
                    pendingProgrammingDefaultAccent.red

                seekProgrammingAccentGreen.progress =
                    pendingProgrammingDefaultAccent.green

                seekProgrammingAccentBlue.progress =
                    pendingProgrammingDefaultAccent.blue

                updateProgrammingAccentPreview()
                updateSaveButtonState()
            }

        buttonResetMathPhysicsAccentDefault
            .setOnClickListener {

                seekMathPhysicsAccentRed.progress =
                    pendingMathPhysicsDefaultAccent.red

                seekMathPhysicsAccentGreen.progress =
                    pendingMathPhysicsDefaultAccent.green

                seekMathPhysicsAccentBlue.progress =
                    pendingMathPhysicsDefaultAccent.blue

                updateMathPhysicsAccentPreview()
                updateSaveButtonState()
            }

        buttonBack.setOnClickListener {

            finish()
        }

        /*
         * COMPRA PREMIUM.
         *
         * Si el usuario ya tiene Premium,
         * no necesita volver a comprarlo.
         */
        buttonPremiumPurchase.setOnClickListener {

            if (
                PremiumRepository.isPremium(this)
            ) {

                Toast.makeText(
                    this,
                    "Premium ya está activo",
                    Toast.LENGTH_SHORT
                ).show()

            } else {

                PremiumBillingManager.launchPremiumPurchase(
                    this
                )
            }
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
 * RESTABLECER EXCEPCIONES DE AJUSTE
 * DE LÍNEA DE PROGRAMACIÓN.
 *
 * Es una acción inmediata.
 * No participa en Guardar cambios.
 */
        buttonResetProgrammingWrapExceptions
            .setOnClickListener {

                AlertDialog.Builder(
                    this
                )
                    .setTitle(
                        "Restablecer excepciones de Programación"
                    )
                    .setMessage(
                        "Todos los cursos de Programación volverán a permitir " +
                                "que las Opciones globales sobrescriban su ajuste " +
                                "de línea.\n\n" +
                                "Además, todos recibirán inmediatamente el ajuste " +
                                "de línea predeterminado actualmente guardado."
                    )
                    .setNegativeButton(
                        "Cancelar",
                        null
                    )
                    .setPositiveButton(
                        "Restablecer"
                    ) { _, _ ->

                        val programmingCourseIds =
                            CourseRepository
                                .loadCourses(
                                    this,
                                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                                )
                                .map {
                                    it.id
                                }

                        CodeWrapSettingsRepository
                            .resetRouteExceptions(
                                this,
                                programmingCourseIds
                            )

                        Toast.makeText(
                            this,
                            "Excepciones de Programación restablecidas",
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                    .show()
            }

        /*
 * RESTABLECER EXCEPCIONES DE AJUSTE
 * DE LÍNEA DE MATEMÁTICAS Y FÍSICA.
 *
 * La ruta todavía no tiene contenido.
 * Esta acción no participa en Guardar cambios.
 */
        buttonResetMathPhysicsWrapExceptions
            .setOnClickListener {

                AlertDialog.Builder(
                    this
                )
                    .setTitle(
                        "Matemáticas y Física"
                    )
                    .setMessage(
                        "Todavía no hay cursos disponibles en esta ruta.\n\n" +
                                "Cuando se agreguen cursos, este botón permitirá " +
                                "restablecer sus excepciones de ajuste de línea."
                    )
                    .setPositiveButton(
                        "Aceptar",
                        null
                    )
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

            LessonTextSizeSettingsRepository
                .saveGlobalScale(
                    this,
                    seekGlobalTextSize.progress +
                            LessonTextSizeSettingsRepository.MIN_SCALE
                )

            if (
                switchGlobalCodeWrap.isChecked !=
                originalGlobalCodeWrap
            ) {

                val programmingCourseIds =
                    CourseRepository
                        .loadCourses(
                            this,
                            AppearanceSettingsRepository.ROUTE_PROGRAMMING
                        )
                        .map {
                            it.id
                        }

                CodeWrapSettingsRepository
                    .saveGlobalWrapAndApplyToCourses(
                        this,
                        switchGlobalCodeWrap.isChecked,
                        programmingCourseIds
                    )
            }

            /*
 * Guardamos primero las protecciones.
 *
 * Así, si el usuario cambia en la misma
 * operación el color global y la protección,
 * se respeta inmediatamente el nuevo estado.
 */
            AppearanceSettingsRepository
                .saveAllowsGlobalAccentOverwrite(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING,
                    switchProgrammingAllowGlobalAccentOverwrite.isChecked
                )

            AppearanceSettingsRepository
                .saveAllowsGlobalAccentOverwrite(
                    this,
                    AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
                    switchMathPhysicsAllowGlobalAccentOverwrite.isChecked
                )

            val globalAccentHasChanges =
                seekAccentRed.progress !=
                        originalAccentRed ||

                        seekAccentGreen.progress !=
                        originalAccentGreen ||

                        seekAccentBlue.progress !=
                        originalAccentBlue

            /*
             * Solo propagamos a las rutas si el color
             * global cambió realmente.
             */
            if (
                globalAccentHasChanges
            ) {

                AppearanceSettingsRepository
                    .saveGlobalAccentAndApplyToRoutes(
                        this,
                        seekAccentRed.progress,
                        seekAccentGreen.progress,
                        seekAccentBlue.progress,
                        listOf(
                            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
                            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
                        )
                    )
            }

            /*
             * Los cambios manuales de cada ruta tienen
             * prioridad sobre una propagación global
             * realizada en este mismo guardado.
             */
            val programmingAccentHasChanges =
                seekProgrammingAccentRed.progress !=
                        originalProgrammingAccent.red ||

                        seekProgrammingAccentGreen.progress !=
                        originalProgrammingAccent.green ||

                        seekProgrammingAccentBlue.progress !=
                        originalProgrammingAccent.blue

            if (
                programmingAccentHasChanges
            ) {

                AppearanceSettingsRepository
                    .saveRouteAccentColor(
                        this,
                        AppearanceSettingsRepository.ROUTE_PROGRAMMING,
                        seekProgrammingAccentRed.progress,
                        seekProgrammingAccentGreen.progress,
                        seekProgrammingAccentBlue.progress
                    )
            }

            val mathPhysicsAccentHasChanges =
                seekMathPhysicsAccentRed.progress !=
                        originalMathPhysicsAccent.red ||

                        seekMathPhysicsAccentGreen.progress !=
                        originalMathPhysicsAccent.green ||

                        seekMathPhysicsAccentBlue.progress !=
                        originalMathPhysicsAccent.blue

            if (
                mathPhysicsAccentHasChanges
            ) {

                AppearanceSettingsRepository
                    .saveRouteAccentColor(
                        this,
                        AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
                        seekMathPhysicsAccentRed.progress,
                        seekMathPhysicsAccentGreen.progress,
                        seekMathPhysicsAccentBlue.progress
                    )
            }

            /*
 * GUARDAR VALORES PREDETERMINADOS.
 */
            AppearanceSettingsRepository
                .saveDefaultAccentColor(
                    this,
                    pendingGlobalDefaultAccent.red,
                    pendingGlobalDefaultAccent.green,
                    pendingGlobalDefaultAccent.blue
                )

            AppearanceSettingsRepository
                .saveRouteDefaultAccentColor(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING,
                    pendingProgrammingDefaultAccent.red,
                    pendingProgrammingDefaultAccent.green,
                    pendingProgrammingDefaultAccent.blue
                )

            AppearanceSettingsRepository
                .saveRouteDefaultAccentColor(
                    this,
                    AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
                    pendingMathPhysicsDefaultAccent.red,
                    pendingMathPhysicsDefaultAccent.green,
                    pendingMathPhysicsDefaultAccent.blue
                )

            Toast.makeText(
                this,
                "Configuración guardada",
                Toast.LENGTH_SHORT
            ).show()

            finish()
        }
    }

    private fun updatePremiumUi() {

        val isPremium =
            PremiumRepository.isPremium(
                this
            )

        findViewById<Button>(
            R.id.buttonPremiumPurchase
        ).visibility =
            if (isPremium) View.GONE else View.VISIBLE

        val premiumStatusButton =
            findViewById<Button>(
                R.id.buttonPremiumStatus
            )

        premiumStatusButton.visibility =
            if (isPremium) View.VISIBLE else View.GONE

        premiumStatusButton.background =
            androidx.appcompat.content.res.AppCompatResources.getDrawable(
                this,
                R.drawable.bg_premium_gold
            )

        premiumStatusButton.backgroundTintList =
            null

        premiumStatusButton.setTextColor(
            Color.rgb(
                32,
                22,
                0
            )
        )

        findViewById<TextView>(
            R.id.textPremiumPaymentNote
        ).visibility =
            if (isPremium) View.GONE else View.VISIBLE
    }

    override fun onStart() {

        super.onStart()

        getSharedPreferences(
            "premium_state",
            MODE_PRIVATE
        ).registerOnSharedPreferenceChangeListener(
            premiumStateListener
        )
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.apply(
            this
        )

        updatePremiumUi()
    }

    override fun onStop() {

        getSharedPreferences(
            "premium_state",
            MODE_PRIVATE
        ).unregisterOnSharedPreferenceChangeListener(
            premiumStateListener
        )

        super.onStop()
    }
}
package com.learngraphicsprogramming.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject
import android.view.MotionEvent
import android.widget.EditText
import android.view.DragEvent
import android.view.ViewGroup
import androidx.activity.OnBackPressedCallback
import android.content.Intent
import android.widget.ImageButton
import androidx.activity.result.contract.ActivityResultContracts
import android.text.Editable
import android.text.TextWatcher
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import android.view.ViewTreeObserver
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import android.util.TypedValue
import android.widget.SeekBar

class LessonActivity : AppCompatActivity() {

    private val lessonOptionsLauncher =
        registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->

            if (
                result.resultCode ==
                RESULT_OK &&
                ::currentLessonId.isInitialized
            ) {

                val courseSettingsChanged =
                    result.data
                        ?.getBooleanExtra(
                            "courseSettingsChanged",
                            false
                        ) == true

                if (
                    courseSettingsChanged
                ) {

                    /*
                     * Se cambiaron ajustes del curso.
                     *
                     * Conservamos exactamente las Views
                     * y el estado actual de la lección.
                     */
                    refreshLessonContentTextScale()

                } else {

                    /*
                     * RESULT_OK sin courseSettingsChanged
                     * corresponde a Reiniciar lección.
                     */
                    pageViewCache.clear()

                    pageRequiredExercises.clear()

                    pageCompletedExercises.clear()

                    currentPage =
                        0

                    requiredExercises =
                        0

                    completedExercises =
                        0

                    ignoreCurrentSwipe =
                        false

                    loadLesson(
                        currentLessonId
                    )
                }
            }
        }

    private lateinit var buttonSwipeToggle: TextView

    private var swipeNavigationEnabled = true

    /*
     * Conserva las Views exactas de cada página.
     *
     * Esto permite volver a una página sin reconstruirla.
     * Por tanto se conservan:
     *
     * - respuestas seleccionadas
     * - respuestas ya correctas
     * - texto escrito
     * - bloques ordenados
     * - bloques a medio ordenar
     */
    private val pageViewCache =
        mutableMapOf<Int, List<View>>()

    private val pageRequiredExercises =
        mutableMapOf<Int, Int>()

    private val pageCompletedExercises =
        mutableMapOf<Int, Int>()

    private val courseWrapControllers =
        mutableListOf<(Boolean) -> Unit>()

    private var ignoreCurrentSwipe = false

    /*
 * Mientras el compilador está en fullscreen,
 * los gestos jamás navegan entre páginas.
 */
    private var compilerFullscreenActive =
        false

    /*
 * FULLSCREEN GENÉRICO DE EJERCICIOS.
 *
 * Es independiente del fullscreen
 * del compilador real.
 */
    private var exerciseFullscreenActive =
        false

    /*
     * Indica si el ejercicio fullscreen actual
     * utiliza teclado.
     *
     * Más adelante permitirá distinguir:
     *
     * - escribir código       -> true
     * - adivinar salida       -> true
     * - ordenar bloques       -> false
     * - completar con bloques -> false
     */
    private var exerciseFullscreenUsesKeyboard =
        false

    /*
     * Overlay actualmente utilizado por
     * un ejercicio fullscreen.
     */
    private var exerciseFullscreenOverlay:
            FrameLayout? =
        null

    /*
 * Referencias a las piezas reales de una
 * consola fake creada por addCode().
 *
 * Nos permiten mover esas mismas Views
 * al fullscreen sin reconstruirlas.
 */
    private data class FakeCodeBlockHandle(
        val card: LinearLayout,
        val header: LinearLayout,
        val codeBody: LinearLayout,
        val fullscreenButton: TextView,
        val resetButton: TextView?
    )

    /*
     * Estado necesario para devolver cada View
     * exactamente a su padre, posición y
     * LayoutParams originales.
     */
    private data class ExerciseFullscreenViewState(
        val view: View,
        val originalParent: ViewGroup,
        val originalIndex: Int,
        val originalLayoutParams: ViewGroup.LayoutParams,
        val originalVisibility: Int
    )

    private val exerciseFullscreenMovedViews =
        mutableListOf<ExerciseFullscreenViewState>()

    private var exerciseFullscreenSystemUiVisibility =
        0

    private lateinit var lessonContent: LinearLayout
    private lateinit var lessonScroll: ScrollView

    private lateinit var programmingKeyboardBar:
            HorizontalScrollView

    private lateinit var programmingKeyboardBarContent:
            LinearLayout

    private var activeCodeInput:
            EditText? =
        null

    private var keyboardVisible =
        false

    private var lessonNumber = 1

    private var currentPage = 0
    private var totalPages = 1

    private lateinit var currentCourseId: String

    private var appliedContentTextScale =
        LessonTextSizeSettingsRepository.DEFAULT_SCALE

    private lateinit var currentLessonId: String

    private var requiredExercises = 0
    private var completedExercises = 0

    private lateinit var lessonJson: JSONObject

    private var touchStartX = 0f
    private var touchStartY = 0f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContentView(R.layout.activity_lesson)

        /*
 * Dentro de una lección desactivamos
 * el Atrás del sistema Android.
 *
 * Esto evita que el gesto desde el borde
 * cierre accidentalmente la lección.
 *
 * La salida voluntaria seguirá realizándose
 * mediante nuestra flecha superior.
 */
        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {

                override fun handleOnBackPressed() {

                    /*
                     * Intencionadamente vacío.
                     *
                     * El botón Atrás del sistema
                     * y su gesto asociado no abandonan
                     * una lección.
                     */
                }
            }
        )

        val buttonBack =
            findViewById<TextView>(R.id.buttonBack)

        val buttonLessonSettings =
            findViewById<ImageButton>(
                R.id.buttonLessonSettings
            )

        val buttonContinue =
            findViewById<Button>(R.id.buttonContinue)

        val textTopLesson =
            findViewById<TextView>(R.id.textTopLesson)

        lessonContent =
            findViewById(R.id.lessonContent)

        lessonScroll =
            findViewById(R.id.lessonScroll)

        programmingKeyboardBar =
            findViewById(
                R.id.programmingKeyboardBar
            )

        programmingKeyboardBarContent =
            findViewById(
                R.id.programmingKeyboardBarContent
            )

        setupProgrammingKeyboardBar()

        buttonSwipeToggle =
            findViewById(
                R.id.buttonSwipeToggle
            )

        val preferences =
            getSharedPreferences(
                "app_settings",
                MODE_PRIVATE
            )

        swipeNavigationEnabled =
            preferences.getBoolean(
                "swipeNavigationEnabled",
                true
            )

        updateSwipeToggle()

        buttonSwipeToggle.setOnClickListener {

            swipeNavigationEnabled =
                !swipeNavigationEnabled

            preferences
                .edit()
                .putBoolean(
                    "swipeNavigationEnabled",
                    swipeNavigationEnabled
                )
                .apply()

            updateSwipeToggle()
        }

        currentCourseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        currentLessonId =
            intent.getStringExtra(
                "lessonId"
            ) ?: return

        appliedContentTextScale =
            LessonTextSizeSettingsRepository
                .getEffectiveScale(
                    this,
                    currentCourseId
                )

        appliedContentTextScale =
            LessonTextSizeSettingsRepository
                .getEffectiveScale(
                    this,
                    currentCourseId
                )

        lessonNumber =
            intent.getIntExtra(
                "lessonNumber",
                1
            )

        NavigationStateRepository.saveLesson(
            this,
            currentLessonId
        )

        textTopLesson.text =
            "Lección $lessonNumber"

        buttonBack.setOnClickListener {

            finish()
        }

        buttonLessonSettings.setOnClickListener {

            val intent =
                Intent(
                    this,
                    LessonOptionsActivity::class.java
                )

            intent.putExtra(
                "courseId",
                currentCourseId
            )

            intent.putExtra(
                "lessonId",
                currentLessonId
            )

            intent.putExtra(
                "lessonNumber",
                lessonNumber
            )

            lessonOptionsLauncher.launch(
                intent
            )
        }

        buttonContinue.setOnClickListener {

            tryGoNextPage()
        }

        loadLesson(currentLessonId)
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.applyRoute(
            this,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING
        )

        if (
            ::programmingKeyboardBarContent.isInitialized
        ) {

            rebuildProgrammingKeyboardBar()
        }

        if (
            ::currentLessonId.isInitialized
        ) {

            NavigationStateRepository.saveLesson(
                this,
                currentLessonId
            )
        }
    }

    private fun setupProgrammingKeyboardBar() {

        rebuildProgrammingKeyboardBar()

        protectProgrammingInteractionFromPageSwipe(
            programmingKeyboardBar
        )

        val root =
            findViewById<View>(
                android.R.id.content
            )

        /*
         * Detectamos qué EditText tiene el foco.
         */
        root.viewTreeObserver
            .addOnGlobalFocusChangeListener { _, newFocus ->

                activeCodeInput =
                    (newFocus as? EditText)
                        ?.takeIf {

                            it.tag ==
                                    CODE_INPUT_TAG
                        }

                updateProgrammingKeyboardBarVisibility()
            }

        /*
         * Comprobamos el estado real del teclado
         * cada vez que cambia el layout.
         *
         * Esto funciona mejor en algunos teléfonos
         * que depender solamente del listener
         * de WindowInsets.
         */
        root.viewTreeObserver
            .addOnGlobalLayoutListener {

                /*
                 * Guardamos el estado ANTERIOR
                 * antes de consultar nuevamente el IME.
                 */
                val keyboardWasVisible =
                    keyboardVisible

                val insets =
                    ViewCompat.getRootWindowInsets(
                        root
                    )

                keyboardVisible =
                    insets?.isVisible(
                        WindowInsetsCompat.Type.ime()
                    ) == true

                updateProgrammingKeyboardBarVisibility()
            }

        root.post {

            val insets =
                ViewCompat.getRootWindowInsets(
                    root
                )

            keyboardVisible =
                insets?.isVisible(
                    WindowInsetsCompat.Type.ime()
                ) == true

            updateProgrammingKeyboardBarVisibility()
        }
    }

    private fun rebuildProgrammingKeyboardBar() {

        programmingKeyboardBarContent
            .removeAllViews()

        /*
         * TAB siempre es fijo.
         */
        addProgrammingShortcutButton(
            label = "TAB",
            insertedText =
                KeyboardShortcutSettingsRepository
                    .getTabText()
        )

        /*
         * Después vienen los atajos
         * configurables del usuario.
         */
        val shortcuts =
            KeyboardShortcutSettingsRepository
                .getShortcuts(
                    this
                )

        for (
        shortcut in shortcuts
        ) {

            addProgrammingShortcutButton(
                label = shortcut,
                insertedText = shortcut
            )
        }
    }

    private fun addProgrammingShortcutButton(
        label: String,
        insertedText: String
    ) {

        val button =
            TextView(this)

        button.text =
            label

        button.typeface =
            Typeface.MONOSPACE

        button.textSize =
            16f

        button.gravity =
            Gravity.CENTER

        button.setTextColor(
            Color.WHITE
        )

        button.minWidth =
            if (
                label == "TAB"
            ) {
                dp(56)
            } else {
                dp(40)
            }

        button.minimumHeight =
            dp(40)

        button.setPadding(
            dp(10),
            dp(6),
            dp(10),
            dp(6)
        )

        /*
         * No roba el foco al EditText.
         */
        button.isFocusable =
            false

        button.isFocusableInTouchMode =
            false

        button.isClickable =
            true

        protectProgrammingInteractionFromPageSwipe(
            button
        )

        val background =
            GradientDrawable()

        background.setColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        background.cornerRadius =
            dp(8).toFloat()

        background.setStroke(
            dp(1),
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )
        )

        button.background =
            background

        val params =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                dp(40)
            )

        params.setMargins(
            dp(3),
            0,
            dp(3),
            0
        )

        button.layoutParams =
            params

        button.setOnClickListener {

            insertProgrammingShortcut(
                insertedText
            )
        }

        programmingKeyboardBarContent.addView(
            button
        )
    }

    private fun insertProgrammingShortcut(
        text: String
    ) {

        val input =
            activeCodeInput
                ?: return

        val editable =
            input.text

        val selectionStart =
            input.selectionStart
                .coerceAtLeast(
                    0
                )

        val selectionEnd =
            input.selectionEnd
                .coerceAtLeast(
                    0
                )

        val start =
            minOf(
                selectionStart,
                selectionEnd
            )

        val end =
            maxOf(
                selectionStart,
                selectionEnd
            )

        editable.replace(
            start,
            end,
            text
        )

        input.setSelection(
            start +
                    text.length
        )
    }

    private fun updateProgrammingKeyboardBarVisibility() {

        /*
         * En fullscreen + horizontal + teclado
         * usamos toda la altura posible para código.
         */
        val compactLandscapeCompiler =
            compilerFullscreenActive &&
                    resources.configuration.orientation ==
                    android.content.res.Configuration.ORIENTATION_LANDSCAPE

        val shouldShow =
            keyboardVisible &&
                    activeCodeInput
                        ?.hasFocus() ==
                    true &&
                    !compactLandscapeCompiler

        programmingKeyboardBar.visibility =
            if (
                shouldShow
            ) {
                View.VISIBLE
            } else {
                View.GONE
            }
    }

    private fun registerCourseWrapController(
        controller: (Boolean) -> Unit
    ) {

        courseWrapControllers.add(
            controller
        )

        controller(
            CodeWrapSettingsRepository
                .getCourseWrap(
                    this,
                    currentCourseId
                )
        )
    }

    private fun setCurrentCourseWrap(
        enabled: Boolean
    ) {

        CodeWrapSettingsRepository
            .saveCourseWrap(
                this,
                currentCourseId,
                enabled
            )

        courseWrapControllers
            .toList()
            .forEach {
                it(enabled)
            }
    }

    private fun setPuzzleWrapMode(
        enabled: Boolean,
        horizontalScroll: HorizontalScrollView,
        codeLines: LinearLayout,
        flowRows: List<CodeFlowLayout>,
        wrapButton: TextView
    ) {

        if (enabled) {

            horizontalScroll.scrollTo(
                0,
                0
            )

            horizontalScroll.isFillViewport =
                true

            horizontalScroll.isHorizontalScrollBarEnabled =
                false

            /*
             * Esperamos a conocer el ancho real
             * visible del HorizontalScrollView.
             */
            horizontalScroll.post {

                /*
                 * Ancho TOTAL visible del editor.
                 */
                val viewportWidth =
                    horizontalScroll.width
                        .coerceAtLeast(
                            1
                        )

                /*
                 * Ancho REAL disponible para las filas.
                 *
                 * codeLines ocupa todo el viewport,
                 * pero sus filas viven dentro de su padding.
                 */
                val availableWidth =
                    (
                            viewportWidth -
                                    codeLines.paddingLeft -
                                    codeLines.paddingRight
                            ).coerceAtLeast(
                            1
                        )

                /*
                 * codeLines debe ocupar el viewport completo.
                 *
                 * No usamos availableWidth aquí porque
                 * eso volvería a descontar su padding.
                 */
                val codeParams =
                    codeLines.layoutParams

                codeParams.width =
                    viewportWidth

                codeLines.layoutParams =
                    codeParams

                for (
                row in flowRows
                ) {

                    /*
                     * CodeFlowLayout recibe exactamente
                     * el mismo ancho interior que realmente
                     * tiene dentro de codeLines.
                     */
                    row.wrapWidthPx =
                        availableWidth

                    row.wrapEnabled =
                        true

                    row.layoutParams =
                        LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            LinearLayout.LayoutParams.WRAP_CONTENT
                        )

                    row.requestLayout()
                }

                codeLines.requestLayout()
            }

            wrapButton.text =
                ""

            wrapButton.setCompoundDrawablesWithIntrinsicBounds(
                R.drawable.ic_code_wrap_on,
                0,
                0,
                0
            )

            wrapButton.contentDescription =
                "Ajuste de línea activado"

        } else {

            for (row in flowRows) {

                row.wrapWidthPx =
                    0

                row.wrapEnabled =
                    false

                row.layoutParams =
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT
                    )

                row.requestLayout()
            }

            val codeParams =
                codeLines.layoutParams

            codeParams.width =
                ViewGroup.LayoutParams.WRAP_CONTENT

            codeLines.layoutParams =
                codeParams

            horizontalScroll.isFillViewport =
                false

            horizontalScroll.isHorizontalScrollBarEnabled =
                true

            horizontalScroll.isScrollbarFadingEnabled =
                false

            wrapButton.text =
                ""

            wrapButton.setCompoundDrawablesWithIntrinsicBounds(
                R.drawable.ic_code_wrap_off,
                0,
                0,
                0
            )

            wrapButton.contentDescription =
                "Ajuste de línea desactivado"

            codeLines.requestLayout()
        }
    }

    private fun enableCppAutoClose(
        input: EditText
    ) {

        /*
         * Desactiva autocorrección, sugerencias y
         * espaciado inteligente del teclado.
         *
         * Esto es especialmente importante para
         * símbolos de programación.
         */
        val isMultiline =
            input.maxLines != 1

        input.inputType =
            android.text.InputType.TYPE_CLASS_TEXT or
                    android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
                    android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or
                    if (isMultiline) {
                        android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    } else {
                        0
                    }

        var internalChange =
            false

        var changeStart =
            0

        var removedCount =
            0

        var addedCount =
            0

        var previousText =
            ""

        /*
         * Algunos teclados insertan automáticamente
         * un espacio justo después de escribir
         * determinados signos.
         *
         * Esta protección vive solamente durante
         * el mismo ciclo de entrada del teclado.
         */
        var suppressAutoSpaceAfterSkip =
            false

        var suppressAutoSpacePosition =
            -1

        input.addTextChangedListener(
            object : TextWatcher {

                override fun beforeTextChanged(
                    text: CharSequence?,
                    start: Int,
                    count: Int,
                    after: Int
                ) {

                    if (internalChange) {
                        return
                    }

                    previousText =
                        text?.toString()
                            .orEmpty()

                    changeStart =
                        start

                    removedCount =
                        count

                    addedCount =
                        after
                }

                override fun onTextChanged(
                    text: CharSequence?,
                    start: Int,
                    before: Int,
                    count: Int
                ) {
                }

                override fun afterTextChanged(
                    editable: Editable?
                ) {

                    if (
                        internalChange ||
                        editable == null
                    ) {
                        return
                    }

                    /*
 * El teclado puede hacer esto:
 *
 * 1. escribir ")"
 * 2. nosotros saltamos sobre ")"
 * 3. el teclado inserta " " automáticamente
 *
 * Si ese espacio pertenece todavía al mismo
 * ciclo de entrada, lo eliminamos.
 */
                    if (
                        suppressAutoSpaceAfterSkip &&
                        removedCount == 0 &&
                        addedCount > 0 &&
                        changeStart ==
                        suppressAutoSpacePosition &&
                        changeStart + addedCount <=
                        editable.length
                    ) {

                        val insertedText =
                            editable.subSequence(
                                changeStart,
                                changeStart + addedCount
                            )
                                .toString()

                        val onlyWhitespace =
                            insertedText.all {
                                it == ' ' ||
                                        it == '\t'
                            }

                        if (onlyWhitespace) {

                            internalChange =
                                true

                            editable.delete(
                                changeStart,
                                changeStart + addedCount
                            )

                            input.setSelection(
                                changeStart.coerceAtMost(
                                    editable.length
                                )
                            )

                            internalChange =
                                false

                            suppressAutoSpaceAfterSkip =
                                false

                            suppressAutoSpacePosition =
                                -1

                            return
                        }
                    }

                    /*
                     * BORRADO DEL PAR VACÍO
                     *
                     * (|)
                     *
                     * borrar "(" elimina también ")".
                     */
                    if (
                        removedCount == 1 &&
                        addedCount == 0 &&
                        changeStart in previousText.indices
                    ) {

                        val deletedCharacter =
                            previousText[
                                changeStart
                            ]

                        val expectedClosing =
                            when (
                                deletedCharacter
                            ) {

                                '(' -> ')'
                                '{' -> '}'
                                '[' -> ']'
                                '"' -> '"'
                                '\'' -> '\''

                                else -> null
                            }

                        if (
                            expectedClosing != null &&
                            changeStart <
                            editable.length &&
                            editable[
                                changeStart
                            ] == expectedClosing
                        ) {

                            internalChange =
                                true

                            editable.delete(
                                changeStart,
                                changeStart + 1
                            )

                            input.setSelection(
                                changeStart.coerceAtMost(
                                    editable.length
                                )
                            )

                            internalChange =
                                false
                        }

                        return
                    }

                    /*
 * Algunos teclados Android pueden insertar
 * un cierre junto con un espacio:
 *
 * ") "
 *
 * Si delante ya existe ese mismo cierre,
 * eliminamos TODO lo que acaba de insertar
 * el teclado y simplemente avanzamos sobre
 * el cierre existente.
 */
                    if (
                        removedCount == 0 &&
                        addedCount >= 1 &&
                        changeStart >= 0 &&
                        changeStart + addedCount <= editable.length
                    ) {

                        val insertedText =
                            editable.subSequence(
                                changeStart,
                                changeStart + addedCount
                            )
                                .toString()

                        val firstInsertedCharacter =
                            insertedText
                                .firstOrNull()

                        val isClosingCharacter =
                            firstInsertedCharacter == ')' ||
                                    firstInsertedCharacter == '}' ||
                                    firstInsertedCharacter == ']' ||
                                    firstInsertedCharacter == '"' ||
                                    firstInsertedCharacter == '\''

                        val remainingInsertedTextIsWhitespace =
                            insertedText
                                .drop(1)
                                .all {
                                    it.isWhitespace()
                                }

                        val existingClosingPosition =
                            changeStart +
                                    addedCount

                        if (
                            isClosingCharacter &&
                            remainingInsertedTextIsWhitespace &&
                            existingClosingPosition <
                            editable.length &&
                            editable[
                                existingClosingPosition
                            ] == firstInsertedCharacter
                        ) {

                            internalChange =
                                true

                            android.view.inputmethod
                                .BaseInputConnection
                                .removeComposingSpans(
                                    editable
                                )

                            /*
                             * Eliminamos tanto el cierre recién
                             * escrito como cualquier espacio
                             * añadido por el teclado.
                             */
                            editable.delete(
                                changeStart,
                                changeStart + addedCount
                            )

                            /*
                             * El cierre original ahora vuelve
                             * a estar en changeStart.
                             */
                            val cursorAfterClosing =
                                (
                                        changeStart + 1
                                        ).coerceAtMost(
                                        editable.length
                                    )

                            input.setSelection(
                                cursorAfterClosing
                            )

                            /*
                             * Si el teclado solamente introdujo
                             * el cierre, existe la posibilidad de
                             * que inmediatamente después intente
                             * agregar un espacio automático.
                             *
                             * Lo bloqueamos únicamente durante
                             * este mismo ciclo del Looper.
                             *
                             * Un espacio que el usuario pulse
                             * posteriormente sí será permitido.
                             */
                            if (
                                addedCount == 1
                            ) {

                                suppressAutoSpaceAfterSkip =
                                    true

                                suppressAutoSpacePosition =
                                    cursorAfterClosing

                                input.post {

                                    suppressAutoSpaceAfterSkip =
                                        false

                                    suppressAutoSpacePosition =
                                        -1
                                }

                            } else {

                                /*
                                 * Si ") " llegó completo en la misma
                                 * modificación, ya fue eliminado arriba.
                                 */
                                suppressAutoSpaceAfterSkip =
                                    false

                                suppressAutoSpacePosition =
                                    -1
                            }

                            internalChange =
                                false

                            return
                        }
                    }

                    /*
                     * Desde aquí, el resto de la lógica
                     * corresponde a una sola tecla escrita.
                     */
                    if (
                        removedCount != 0 ||
                        addedCount != 1 ||
                        changeStart !in editable.indices
                    ) {

                        return
                    }

                    val typedCharacter =
                        editable[
                            changeStart
                        ]

                    /*
                     * ENTER:
                     *
                     * conserva la sangría actual.
                     *
                     * Si la línea termina en "{",
                     * agrega cuatro espacios.
                     */
                    if (
                        typedCharacter == '\n' &&
                        isMultiline
                    ) {

                        val previousLineStart =
                            previousText
                                .lastIndexOf(
                                    '\n',
                                    (
                                            changeStart - 1
                                            ).coerceAtLeast(
                                            0
                                        )
                                ) + 1

                        val previousLine =
                            previousText.substring(
                                previousLineStart,
                                changeStart.coerceAtMost(
                                    previousText.length
                                )
                            )

                        val baseIndent =
                            previousLine
                                .takeWhile {
                                    it == ' ' ||
                                            it == '\t'
                                }

                        val trimmedPreviousLine =
                            previousLine
                                .trimEnd()

                        val opensBlock =
                            trimmedPreviousLine
                                .endsWith(
                                    "{"
                                )

                        val childIndent =
                            if (opensBlock) {

                                baseIndent +
                                        "    "

                            } else {

                                baseIndent
                            }

                        /*
                         * Caso especial:
                         *
                         * {
                         *     |
                         * }
                         *
                         * Si Enter se pulsa entre un par
                         * {}, dejamos automáticamente la
                         * llave de cierre en su propia línea.
                         */
                        val closingBraceImmediatelyAfter =
                            changeStart + 1 <
                                    editable.length &&
                                    editable[
                                        changeStart + 1
                                    ] == '}'

                        internalChange =
                            true

                        if (
                            opensBlock &&
                            closingBraceImmediatelyAfter
                        ) {

                            editable.insert(
                                changeStart + 1,
                                childIndent +
                                        "\n" +
                                        baseIndent
                            )

                            val newCursorPosition =
                                changeStart +
                                        1 +
                                        childIndent.length

                            input.setSelection(
                                newCursorPosition
                            )

                            /*
                             * SwiftKey/Android puede reajustar la selección
                             * durante el mismo ciclo del IME.
                             *
                             * La reafirmamos después de que termine
                             * esa actualización para que el cursor quede
                             * realmente en la nueva línea.
                             */
                            input.post {

                                val safePosition =
                                    newCursorPosition.coerceIn(
                                        0,
                                        input.text?.length ?: 0
                                    )

                                input.setSelection(
                                    safePosition
                                )

                                input.bringPointIntoView(
                                    safePosition
                                )
                            }

                        } else {

                            editable.insert(
                                changeStart + 1,
                                childIndent
                            )

                            val newCursorPosition =
                                changeStart +
                                        1 +
                                        childIndent.length

                            input.setSelection(
                                newCursorPosition
                            )

                            input.post {

                                val safePosition =
                                    newCursorPosition.coerceIn(
                                        0,
                                        input.text?.length ?: 0
                                    )

                                input.setSelection(
                                    safePosition
                                )

                                input.bringPointIntoView(
                                    safePosition
                                )
                            }
                        }

                        internalChange =
                            false

                        return
                    }

                    /*
                     * Una comilla escapada:
                     *
                     * \"
                     *
                     * no genera un cierre automático.
                     */
                    if (
                        (
                                typedCharacter == '"' ||
                                        typedCharacter == '\''
                                ) &&
                        changeStart > 0 &&
                        editable[
                            changeStart - 1
                        ] == '\\'
                    ) {

                        return
                    }

                    /*
                     * Si escribimos } al principio lógico
                     * de una línea indentada, reducimos
                     * automáticamente un nivel.
                     *
                     * Ejemplo:
                     *
                     *     |
                     *
                     * escribir "}" produce:
                     *
                     * }
                     */
                    if (
                        typedCharacter == '}' &&
                        isMultiline
                    ) {

                        val lineStart =
                            editable
                                .lastIndexOf(
                                    '\n',
                                    (
                                            changeStart - 1
                                            ).coerceAtLeast(
                                            0
                                        )
                                ) + 1

                        val textBeforeBrace =
                            editable.substring(
                                lineStart,
                                changeStart
                            )

                        val onlyWhitespaceBefore =
                            textBeforeBrace.all {
                                it == ' ' ||
                                        it == '\t'
                            }

                        if (
                            onlyWhitespaceBefore &&
                            textBeforeBrace.isNotEmpty()
                        ) {

                            var charactersToRemove =
                                0

                            var position =
                                changeStart - 1

                            while (
                                position >= lineStart &&
                                charactersToRemove < 4 &&
                                editable[
                                    position
                                ] == ' '
                            ) {

                                charactersToRemove++
                                position--
                            }

                            if (
                                charactersToRemove > 0
                            ) {

                                internalChange =
                                    true

                                editable.delete(
                                    changeStart -
                                            charactersToRemove,
                                    changeStart
                                )

                                val newBracePosition =
                                    changeStart -
                                            charactersToRemove

                                input.setSelection(
                                    (
                                            newBracePosition + 1
                                            ).coerceAtMost(
                                            editable.length
                                        )
                                )

                                internalChange =
                                    false

                                return
                            }
                        }
                    }

                    /*
                     * AUTOCIERRE
                     */
                    val closingCharacter =
                        when (
                            typedCharacter
                        ) {

                            '(' -> ')'
                            '{' -> '}'
                            '[' -> ']'
                            '"' -> '"'
                            '\'' -> '\''

                            else -> null
                        }

                    if (
                        closingCharacter ==
                        null
                    ) {

                        return
                    }

                    internalChange =
                        true

                    editable.insert(
                        changeStart + 1,
                        closingCharacter
                            .toString()
                    )

                    input.setSelection(
                        changeStart + 1
                    )

                    internalChange =
                        false
                }
            }
        )
    }

    private fun editorHasHorizontalOverflow(
        editor: EditText
    ): Boolean {

        val layout =
            editor.layout
                ?: return false

        val availableWidth =
            (
                    editor.width -
                            editor.totalPaddingLeft -
                            editor.totalPaddingRight
                    ).coerceAtLeast(
                    1
                )

        var widestLine =
            0f

        for (
        line in 0 until
                layout.lineCount
        ) {

            widestLine =
                maxOf(
                    widestLine,
                    layout.getLineWidth(
                        line
                    )
                )
        }

        return widestLine >
                availableWidth
    }

    private fun isTouchOnEditorText(
        editor: EditText,
        touchX: Float,
        touchY: Float
    ): Boolean {

        val layout =
            editor.layout
                ?: return false

        /*
         * Convertimos la posición tocada
         * a coordenadas internas del texto.
         */
        val textX =
            touchX +
                    editor.scrollX -
                    editor.totalPaddingLeft

        val textY =
            touchY +
                    editor.scrollY -
                    editor.totalPaddingTop

        /*
         * Si tocamos por encima, debajo
         * o fuera del área vertical ocupada
         * por texto, estamos en espacio vacío.
         */
        if (
            textY < 0f ||
            textY >
            layout.height
        ) {

            return false
        }

        val line =
            layout.getLineForVertical(
                textY.toInt()
            )

        val lineLeft =
            minOf(
                layout.getLineLeft(
                    line
                ),
                layout.getLineRight(
                    line
                )
            )

        val lineRight =
            maxOf(
                layout.getLineLeft(
                    line
                ),
                layout.getLineRight(
                    line
                )
            )

        /*
         * Solo consideramos "texto" la zona
         * horizontal realmente ocupada
         * por esa línea.
         */
        return textX >=
                lineLeft &&
                textX <=
                lineRight
    }

    private fun protectHorizontalEditorFromPageSwipe(
        editor: EditText
    ) {

        var downX =
            0f

        var downY =
            0f

        var initialScrollX =
            0

        var horizontalDrag =
            false

        var verticalDrag =
            false

        editor.setOnTouchListener {
                _,
                event ->

            when (
                event.actionMasked
            ) {

                MotionEvent.ACTION_DOWN -> {

                    /*
                     * Desde el instante en que el gesto
                     * comienza dentro del editor deja de
                     * pertenecer a la navegación de páginas.
                     *
                     * Esto incluye:
                     *
                     * - tocar código
                     * - tocar una línea vacía
                     * - tocar debajo del código
                     * - arrastrar desde cualquier zona
                     */
                    ignoreCurrentSwipe =
                        true

                    downX =
                        event.x

                    downY =
                        event.y

                    initialScrollX =
                        editor.scrollX

                    horizontalDrag =
                        false

                    verticalDrag =
                        false

                    /*
                     * false permite al EditText seguir
                     * procesando cursor, selección,
                     * teclado y desplazamiento vertical.
                     */
                    false
                }

                MotionEvent.ACTION_MOVE -> {

                    ignoreCurrentSwipe =
                        true

                    val movementX =
                        event.x -
                                downX

                    val movementY =
                        event.y -
                                downY

                    /*
                     * Esperamos a que exista una dirección
                     * suficientemente clara antes de decidir
                     * qué tipo de desplazamiento hacemos.
                     */
                    if (
                        !horizontalDrag &&
                        !verticalDrag
                    ) {

                        if (
                            kotlin.math.abs(
                                movementX
                            ) >
                            kotlin.math.abs(
                                movementY
                            ) +
                            dp(4)
                        ) {

                            horizontalDrag =
                                true

                        } else if (
                            kotlin.math.abs(
                                movementY
                            ) >
                            kotlin.math.abs(
                                movementX
                            ) +
                            dp(4)
                        ) {

                            verticalDrag =
                                true
                        }
                    }

                    if (
                        horizontalDrag
                    ) {

                        /*
                         * El gesto horizontal pertenece
                         * exclusivamente al editor.
                         */
                        editor.parent
                            ?.requestDisallowInterceptTouchEvent(
                                true
                            )

                        val layout =
                            editor.layout

                        if (
                            layout != null
                        ) {

                            val availableWidth =
                                (
                                        editor.width -
                                                editor.totalPaddingLeft -
                                                editor.totalPaddingRight
                                        ).coerceAtLeast(
                                        1
                                    )

                            var widestLine =
                                0f

                            for (
                            line in 0 until
                                    layout.lineCount
                            ) {

                                widestLine =
                                    maxOf(
                                        widestLine,
                                        layout.getLineWidth(
                                            line
                                        )
                                    )
                            }

                            val maximumScroll =
                                (
                                        widestLine -
                                                availableWidth
                                        )
                                    .toInt()
                                    .coerceAtLeast(
                                        0
                                    )

                            /*
                             * Funciona como nuestra barra
                             * horizontal aunque el dedo esté
                             * sobre una zona donde no hay texto.
                             */
                            val newScrollX =
                                (
                                        initialScrollX +
                                                downX -
                                                event.x
                                        )
                                    .toInt()
                                    .coerceIn(
                                        0,
                                        maximumScroll
                                    )

                            editor.scrollTo(
                                newScrollX,
                                editor.scrollY
                            )
                        }

                        true

                    } else {

                        /*
                         * En vertical dejamos que el propio
                         * EditText realice su scroll.
                         *
                         * El padre no debe convertir el gesto
                         * en navegación.
                         */
                        if (
                            verticalDrag
                        ) {

                            editor.parent
                                ?.requestDisallowInterceptTouchEvent(
                                    true
                                )
                        }

                        false
                    }
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> {

                    editor.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )

                    horizontalDrag =
                        false

                    verticalDrag =
                        false

                    /*
                     * El siguiente gesto decidirá de nuevo
                     * si pertenece o no a la navegación.
                     */
                    ignoreCurrentSwipe =
                        false

                    false
                }

                else -> {

                    false
                }
            }
        }
    }

    private fun protectProgrammingInteractionFromPageSwipe(
        view: View
    ) {

        view.setOnTouchListener { _, event ->

            if (
                event.actionMasked ==
                MotionEvent.ACTION_DOWN
            ) {

                /*
                 * Este gesto pertenece a una zona
                 * interactiva de programación.
                 *
                 * No debe interpretarse como
                 * navegación entre páginas.
                 */
                ignoreCurrentSwipe =
                    true
            }

            /*
             * false permite que la vista siga
             * procesando normalmente el toque:
             *
             * - mover el cursor
             * - seleccionar texto
             * - pulsar botones
             * - desplazar horizontalmente
             */
            false
        }
    }

    private fun isTouchInsideActiveHorizontalScroll(
        view: View,
        rawX: Float,
        rawY: Float
    ): Boolean {

        if (
            view.visibility !=
            View.VISIBLE
        ) {
            return false
        }

        if (
            view is HorizontalScrollView
        ) {

            val location =
                IntArray(2)

            view.getLocationOnScreen(
                location
            )

            val left =
                location[0].toFloat()

            val top =
                location[1].toFloat()

            val right =
                left +
                        view.width

            val bottom =
                top +
                        view.height

            val touchInside =
                rawX >= left &&
                        rawX <= right &&
                        rawY >= top &&
                        rawY <= bottom

            val canScrollHorizontally =
                view.isHorizontalScrollBarEnabled &&
                        (
                                view.canScrollHorizontally(-1) ||
                                        view.canScrollHorizontally(1)
                                )

            val alwaysProtectFromPageSwipe =
                view.tag ==
                        "order_code_swipe_protected"

            if (
                touchInside &&
                (
                        canScrollHorizontally ||
                                alwaysProtectFromPageSwipe
                        )
            ) {

                return true
            }
        }

        if (
            view is ViewGroup
        ) {

            for (
            index in 0 until
                    view.childCount
            ) {

                if (
                    isTouchInsideActiveHorizontalScroll(
                        view.getChildAt(
                            index
                        ),
                        rawX,
                        rawY
                    )
                ) {

                    return true
                }
            }
        }

        return false
    }

    private fun protectHorizontalScrollFromPageSwipe(
        scrollView: HorizontalScrollView
    ) {

        scrollView.setOnTouchListener { _, event ->

            if (
                event.actionMasked ==
                MotionEvent.ACTION_DOWN
            ) {

                /*
                 * Solo protegemos el gesto cuando:
                 *
                 * 1. La barra horizontal está activada.
                 * 2. El contenido realmente puede desplazarse.
                 *
                 * Si el código está ajustado a línea o cabe
                 * completamente en pantalla, el swipe vuelve
                 * a funcionar para cambiar de página.
                 */

                val horizontalScrollActive =
                    scrollView.isHorizontalScrollBarEnabled &&
                            (
                                    scrollView.canScrollHorizontally(-1) ||
                                            scrollView.canScrollHorizontally(1)
                                    )

                if (horizontalScrollActive) {

                    ignoreCurrentSwipe =
                        true
                }
            }

            false
        }
    }

    override fun onPause() {

        saveCurrentPageProgress()

        super.onPause()
    }

    private fun saveCurrentPageProgress() {

        ProgressRepository.saveCurrentPage(
            this,
            currentLessonId,
            currentPage
        )
    }

    private fun moveViewAnimated(
        view: View,
        newParent: ViewGroup
    ) {

        val root =
            findViewById<ViewGroup>(
                android.R.id.content
            )

        val oldParent =
            view.parent as? ViewGroup
                ?: return

        val tokenView =
            view as? TextView

        val compactWidth =
            if (tokenView != null) {

                kotlin.math.ceil(
                    tokenView.paint.measureText(
                        tokenView.text.toString()
                    ).toDouble()
                ).toInt().coerceAtLeast(1)

            } else {

                view.width
            }

        val compactHeight =
            if (tokenView != null) {

                val metrics =
                    tokenView.paint.fontMetricsInt

                (
                        metrics.bottom -
                                metrics.top
                        ).coerceAtLeast(1)

            } else {

                view.height
            }

        /*
         * Posición inicial.
         */
        val startLocation =
            IntArray(2)

        view.getLocationOnScreen(
            startLocation
        )

        val rootLocation =
            IntArray(2)

        root.getLocationOnScreen(
            rootLocation
        )

        val startX =
            (
                    startLocation[0] -
                            rootLocation[0]
                    ).toFloat()

        val startY =
            (
                    startLocation[1] -
                            rootLocation[1]
                    ).toFloat()

        /*
         * Posición del destino.
         */
        val destinationLocation =
            IntArray(2)

        newParent.getLocationOnScreen(
            destinationLocation
        )

        val endX: Float
        val endY: Float

        if (
            newParent is FrameLayout
        ) {

            endX =
                (
                        destinationLocation[0] -
                                rootLocation[0] +
                                (
                                        newParent.width -
                                                view.width
                                        ) / 2f
                        )

            endY =
                (
                        destinationLocation[1] -
                                rootLocation[1] +
                                (
                                        newParent.height -
                                                view.height
                                        ) / 2f
                        )

        } else {

            /*
             * La paleta normal es vertical,
             * pero en fullscreen se convierte
             * en una banda horizontal.
             *
             * La animación debe respetar
             * ambas orientaciones.
             */
            if (
                newParent is LinearLayout &&
                newParent.orientation ==
                LinearLayout.HORIZONTAL
            ) {

                var calculatedX =
                    destinationLocation[0] -
                            rootLocation[0] +
                            newParent.paddingLeft

                for (
                i in 0 until
                        newParent.childCount
                ) {

                    val child =
                        newParent.getChildAt(i)

                    if (
                        child !== view
                    ) {

                        calculatedX +=
                            child.width

                        val params =
                            child.layoutParams

                        if (
                            params is
                                    ViewGroup.MarginLayoutParams
                        ) {

                            calculatedX +=
                                params.leftMargin +
                                        params.rightMargin
                        }
                    }
                }

                endX =
                    calculatedX.toFloat()

                endY =
                    (
                            destinationLocation[1] -
                                    rootLocation[1] +
                                    newParent.paddingTop
                            ).toFloat()

            } else {

                endX =
                    (
                            destinationLocation[0] -
                                    rootLocation[0] +
                                    newParent.paddingLeft
                            ).toFloat()

                var calculatedY =
                    destinationLocation[1] -
                            rootLocation[1] +
                            newParent.paddingTop

                for (
                i in 0 until
                        newParent.childCount
                ) {

                    val child =
                        newParent.getChildAt(i)

                    if (
                        child !== view
                    ) {

                        calculatedY +=
                            child.height

                        val params =
                            child.layoutParams

                        if (
                            params is
                                    ViewGroup.MarginLayoutParams
                        ) {

                            calculatedY +=
                                params.topMargin +
                                        params.bottomMargin
                        }
                    }
                }

                endY =
                    calculatedY.toFloat()
            }
        }

        /*
         * Creamos una copia visual exacta
         * del bloque.
         */
        val bitmap =
            android.graphics.Bitmap.createBitmap(
                view.width.coerceAtLeast(1),
                view.height.coerceAtLeast(1),
                android.graphics.Bitmap.Config.ARGB_8888
            )

        val canvas =
            android.graphics.Canvas(
                bitmap
            )

        view.draw(
            canvas
        )

        val flyingView =
            android.widget.ImageView(
                this
            )

        flyingView.setImageBitmap(
            bitmap
        )

        flyingView.layout(
            0,
            0,
            view.width,
            view.height
        )

        flyingView.x =
            startX

        flyingView.y =
            startY

        /*
         * La vista real queda invisible
         * en su sitio mientras viaja la copia.
         */
        view.visibility =
            View.INVISIBLE

        root.overlay.add(
            flyingView
        )

        /*
         * Si sale de otro hueco,
         * éste recupera suavemente
         * su tamaño normal.
         */
        if (
            oldParent is FrameLayout
        ) {

            oldParent.postDelayed({

                animateCodeSlotSize(
                    oldParent,
                    dp(18),
                    dp(22),
                    100L
                )

            }, 40L)
        }

        /*
         * El hueco destino empieza a crecer
         * solamente cuando el bloque
         * está cerca de llegar.
         */
        if (
            newParent is FrameLayout
        ) {

            newParent.postDelayed({

                newParent.minimumWidth =
                    0

                newParent.minimumHeight =
                    0

                val desiredWidth =
                    compactWidth

                val desiredHeight =
                    compactHeight

                animateCodeSlotSize(
                    newParent,
                    desiredWidth,
                    desiredHeight,
                    110L
                )

            }, 190L)
        }

        /*
         * Recorrido completo.
         */
        flyingView.animate()
            .x(endX)
            .y(endY)
            .setDuration(300L)
            .withEndAction {

                root.overlay.remove(
                    flyingView
                )

                oldParent.removeView(
                    view
                )

                when (newParent) {

                    is FrameLayout -> {

                        view.setPadding(
                            0,
                            0,
                            0,
                            0
                        )

                        view.minimumWidth =
                            0

                        view.minimumHeight =
                            0

                        view.layoutParams =
                            FrameLayout.LayoutParams(
                                compactWidth,
                                compactHeight
                            ).apply {

                                gravity =
                                    Gravity.CENTER
                            }
                    }

                    is LinearLayout -> {

                        view.setPadding(
                            dp(12),
                            dp(10),
                            dp(12),
                            dp(10)
                        )

                        /*
                         * En una paleta horizontal todos
                         * los bloques deben compartir
                         * exactamente la misma coordenada Y.
                         *
                         * El margen vertical de 4 dp
                         * solamente pertenece a las listas
                         * verticales.
                         */
                        val verticalMargin =
                            if (
                                newParent.orientation ==
                                LinearLayout.HORIZONTAL
                            ) {
                                0
                            } else {
                                dp(4)
                            }

                        view.layoutParams =
                            LinearLayout.LayoutParams(
                                LinearLayout.LayoutParams.WRAP_CONTENT,
                                LinearLayout.LayoutParams.WRAP_CONTENT
                            ).apply {

                                setMargins(
                                    0,
                                    verticalMargin,
                                    0,
                                    verticalMargin
                                )
                            }
                    }
                }

                newParent.addView(
                    view
                )

                if (
                    newParent is FrameLayout
                ) {

                    newParent.minimumWidth =
                        0

                    newParent.minimumHeight =
                        0

                    newParent.setPadding(
                        0,
                        0,
                        0,
                        0
                    )

                    val slotParams =
                        newParent.layoutParams

                    slotParams.width =
                        compactWidth

                    slotParams.height =
                        compactHeight

                    newParent.layoutParams =
                        slotParams

                    newParent.requestLayout()
                }

                view.visibility =
                    View.VISIBLE

                view.alpha =
                    1f

                if (
                    newParent is FrameLayout
                ) {

                    view.post {

                        newParent.minimumWidth =
                            0

                        newParent.minimumHeight =
                            0

                        newParent.setPadding(
                            0,
                            0,
                            0,
                            0
                        )

                        val slotParams =
                            newParent.layoutParams

                        slotParams.width =
                            view.width

                        slotParams.height =
                            view.height

                        newParent.layoutParams =
                            slotParams

                        newParent.requestLayout()
                    }
                }

                bitmap.recycle()
            }
            .start()
    }

    private fun animateCodeSlotSize(
        slot: FrameLayout,
        targetWidth: Int,
        targetHeight: Int,
        duration: Long
    ) {

        val startWidth =
            slot.width

        val startHeight =
            slot.height

        val animator =
            android.animation.ValueAnimator.ofFloat(
                0f,
                1f
            )

        animator.duration =
            duration

        animator.addUpdateListener {

            val progress =
                it.animatedValue as Float

            val newWidth =
                (
                        startWidth +
                                (
                                        targetWidth -
                                                startWidth
                                        ) * progress
                        ).toInt()

            val newHeight =
                (
                        startHeight +
                                (
                                        targetHeight -
                                                startHeight
                                        ) * progress
                        ).toInt()

            val params =
                slot.layoutParams

            params.width =
                newWidth

            params.height =
                newHeight

            slot.layoutParams =
                params
        }

        animator.start()
    }

    private fun moveViewToIndexAnimated(
        view: View,
        parent: LinearLayout,
        targetIndex: Int
    ) {

        val startLocation =
            IntArray(2)

        view.getLocationOnScreen(
            startLocation
        )

        parent.removeView(
            view
        )

        val safeIndex =
            targetIndex.coerceIn(
                0,
                parent.childCount
            )

        parent.addView(
            view,
            safeIndex
        )

        view.post {

            val endLocation =
                IntArray(2)

            view.getLocationOnScreen(
                endLocation
            )

            view.translationX =
                (
                        startLocation[0] -
                                endLocation[0]
                        ).toFloat()

            view.translationY =
                (
                        startLocation[1] -
                                endLocation[1]
                        ).toFloat()

            view.animate()
                .translationX(0f)
                .translationY(0f)
                .setDuration(220)
                .start()
        }
    }

    private fun updateSwipeToggle() {

        val background =
            GradientDrawable()

        background.cornerRadius =
            dp(18).toFloat()

        buttonSwipeToggle.text =
            "Deslizar"

        if (
            swipeNavigationEnabled
        ) {

            val accentColor =
                AppearanceSettingsRepository
                    .getRouteAccentColor(
                        this,
                        AppearanceSettingsRepository.ROUTE_PROGRAMMING
                    )

            background.setColor(
                accentColor
            )

            /*
             * Elegimos automáticamente texto
             * negro o blanco según el brillo
             * del color elegido.
             */
            val red =
                Color.red(
                    accentColor
                )

            val green =
                Color.green(
                    accentColor
                )

            val blue =
                Color.blue(
                    accentColor
                )

            val brightness =
                (
                        red * 299 +
                                green * 587 +
                                blue * 114
                        ) / 1000

            buttonSwipeToggle.setTextColor(
                if (
                    brightness >= 160
                ) {
                    Color.BLACK
                } else {
                    Color.WHITE
                }
            )

        } else {

            background.setColor(
                Color.rgb(
                    72,
                    74,
                    82
                )
            )

            buttonSwipeToggle.setTextColor(
                Color.rgb(
                    170,
                    174,
                    183
                )
            )
        }

        buttonSwipeToggle.background =
            background
    }

    private fun saveCurrentPageState() {

        pageRequiredExercises[currentPage] =
            requiredExercises

        pageCompletedExercises[currentPage] =
            completedExercises
    }

    private fun markExerciseCompleted() {

        completedExercises += 1

        pageCompletedExercises[currentPage] =
            completedExercises

        pageRequiredExercises[currentPage] =
            requiredExercises
    }
    private fun addNote(text: String) {

        val view = TextView(this)

        view.text =
            "Nota\n\n$text"

        view.setTextColor(
            Color.rgb(225, 225, 235)
        )

        view.textSize = 17f

        view.setPadding(
            dp(18),
            dp(16),
            dp(18),
            dp(16)
        )

        val background =
            GradientDrawable()

        background.setColor(
            Color.rgb(38, 35, 48)
        )

        background.cornerRadius =
            dp(12).toFloat()

        view.background =
            background

        val params =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        params.setMargins(
            0,
            dp(8),
            0,
            dp(24)
        )

        view.layoutParams =
            params

        lessonContent.addView(view)
    }

    private fun addFillCode(
        block: JSONObject,
        blockIndex: Int
    ) {

        requiredExercises++

        var answeredCorrectly =
            false

        val savedState =
            ProgressRepository.getExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex
            )

        val savedAnswer =
            savedState?.optString(
                "answer",
                ""
            ) ?: ""

        val savedCorrect =
            savedState?.optBoolean(
                "correct",
                false
            ) ?: false

        addHeading(
            block.getString(
                "question"
            )
        )

        /*
 * CÓDIGO DEL EJERCICIO.
 *
 * Conservamos las referencias de sus Views
 * para poder llevar este ejercicio completo
 * al fullscreen.
 */
        val codeHandle =
            addCode(
                block.getString(
                    "code"
                ),
                block.optString(
                    "language",
                    "cpp"
                )
            )

        /*
         * TARJETA DONDE EL USUARIO
         * ESCRIBE SU RESPUESTA.
         */
        val answerCard =
            LinearLayout(this)

        answerCard.orientation =
            LinearLayout.VERTICAL

        val cardBackground =
            GradientDrawable()

        cardBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        cardBackground.cornerRadius =
            dp(12).toFloat()

        answerCard.background =
            cardBackground

        val cardParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        cardParams.setMargins(
            0,
            dp(8),
            0,
            dp(18)
        )

        answerCard.layoutParams =
            cardParams

        /*
         * CABECERA.
         */
        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        /*
         * </> C++
         */
        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f

        /*
         * REINICIAR.
         */
        val resetButton =
            TextView(this)

        resetButton.text =
            ""

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar texto"

        resetButton.isEnabled =
            false

        resetButton.alpha =
            0.35f

        /*
         * ESPACIO CENTRAL.
         */
        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )

        /*
         * COPIAR.
         */
        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar texto"

        copyButton.isClickable =
            true

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            copyButton
        )

        answerCard.addView(
            header
        )

        /*
         * CAMPO DE RESPUESTA.
         */
        val input =
            EditText(this)

        input.tag =
            CODE_INPUT_TAG

        enableCppAutoClose(
            input
        )

        protectProgrammingInteractionFromPageSwipe(
            input
        )

        input.hint =
            "Escribe el código faltante"

        input.setTextColor(
            Color.WHITE
        )

        input.setHintTextColor(
            Color.rgb(
                140,
                145,
                155
            )
        )

        input.typeface =
            Typeface.MONOSPACE

        input.textSize =
            17f

        input.setPadding(
            dp(18),
            dp(16),
            dp(18),
            dp(16)
        )

        input.background =
            null

        input.setText(
            savedAnswer
        )

        answerCard.addView(
            input
        )

        lessonContent.addView(
            answerCard
        )

        /*
 * FULLSCREEN DEL EJERCICIO.
 *
 * Mostramos:
 *
 * - cabecera de la consola
 * - código
 * - sección de respuesta
 * - barra de atajos cuando aparece teclado
 *
 * NO movemos el botón Comprobar.
 */

        /*
         * COMPROBAR.
         */
        val button =
            Button(this)

        button.text =
            "Comprobar"

        button.isAllCaps =
            false

        button.isEnabled =
            savedAnswer.isNotEmpty() &&
                    !savedCorrect

        lessonContent.addView(
            button
        )

        /*
         * RESULTADO.
         */
        val result =
            TextView(this)

        result.textSize =
            16f

        result.setPadding(
            0,
            dp(12),
            0,
            dp(24)
        )

        lessonContent.addView(
            result
        )

        /*
         * ESTADO DE REINICIAR.
         *
         * Incluso un espacio cuenta
         * como contenido.
         */
        fun updateResetButton() {

            val enabled =
                answeredCorrectly ||
                        input.text.isNotEmpty()

            resetButton.isEnabled =
                enabled

            resetButton.alpha =
                if (
                    enabled
                ) {
                    1f
                } else {
                    0.35f
                }
        }

        /*
         * COPIAR EXACTAMENTE LO ESCRITO.
         */
        copyButton.setOnClickListener {

            val clipboard =
                getSystemService(
                    Context.CLIPBOARD_SERVICE
                ) as ClipboardManager

            val clip =
                ClipData.newPlainText(
                    "C++",
                    input.text.toString()
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                "Texto copiado",
                Toast.LENGTH_SHORT
            ).show()
        }

        /*
         * ACTUALIZAR REINICIAR
         * AL ESCRIBIR O BORRAR.
         */
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
                    s: Editable?
                ) {

                    updateResetButton()

                    button.isEnabled =
                        input.text.isNotEmpty() &&
                                !answeredCorrectly
                }
            }
        )

        /*
         * RESTAURAR ESTADO CORRECTO.
         */
        if (
            savedCorrect
        ) {

            answeredCorrectly =
                true

            completedExercises +=
                1

            input.isEnabled =
                false

            button.isEnabled =
                false

            button.text =
                "Correcto ✓"

            result.text =
                "✓ Correcto\n\n" +
                        block.optString(
                            "explanation"
                        )

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        updateResetButton()

        /*
         * REINICIAR RESPUESTA.
         */
        resetButton.setOnClickListener {

            if (
                input.text.isEmpty() &&
                !answeredCorrectly
            ) {

                return@setOnClickListener
            }

            if (
                answeredCorrectly
            ) {

                answeredCorrectly =
                    false

                completedExercises =
                    (
                            completedExercises - 1
                            ).coerceAtLeast(
                            0
                        )

                pageCompletedExercises[
                    currentPage
                ] =
                    completedExercises
            }

            input.isEnabled =
                true

            input.setText(
                ""
            )

            button.isEnabled =
                true

            button.text =
                "Comprobar"

            result.text =
                ""

            val state =
                JSONObject()

            state.put(
                "answer",
                ""
            )

            state.put(
                "correct",
                false
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            updateResetButton()
        }

        /*
         * COMPROBAR RESPUESTA.
         */
        button.setOnClickListener {

            val userAnswer =
                normalizeAnswer(
                    input.text.toString()
                )

            val answers =
                block.getJSONArray(
                    "answers"
                )

            var correct =
                false

            for (
            i in 0 until
                    answers.length()
            ) {

                if (
                    userAnswer ==
                    normalizeAnswer(
                        answers.getString(
                            i
                        )
                    )
                ) {

                    correct =
                        true

                    break
                }
            }

            val state =
                JSONObject()

            state.put(
                "answer",
                input.text.toString()
            )

            state.put(
                "correct",
                correct
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    "✓ Correcto\n\n" +
                            block.optString(
                                "explanation"
                            )

                result.setTextColor(
                    Color.rgb(
                        180,
                        220,
                        190
                    )
                )

                if (
                    !answeredCorrectly
                ) {

                    answeredCorrectly =
                        true

                    markExerciseCompleted()

                    input.isEnabled =
                        false

                    button.isEnabled =
                        false

                    button.text =
                        "Correcto ✓"
                }

            } else {

                result.text =
                    "✗ Incorrecto"

                result.setTextColor(
                    Color.rgb(
                        240,
                        150,
                        150
                    )
                )
            }

            updateResetButton()
        }
    }

    private fun addOutputPrediction(
        block: JSONObject,
        blockIndex: Int
    ) {

        requiredExercises++

        var answeredCorrectly =
            false

        val savedState =
            ProgressRepository.getExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex
            )

        val savedAnswer =
            savedState?.optString(
                "answer",
                ""
            ) ?: ""

        val savedCorrect =
            savedState?.optBoolean(
                "correct",
                false
            ) ?: false

        addHeading(
            block.getString(
                "question"
            )
        )

        /*
         * CÓDIGO QUE EL USUARIO DEBE ANALIZAR.
         */
        val codeHandle =
            addCode(
                block.getString(
                    "code"
                ),
                block.optString(
                    "language",
                    "cpp"
                )
            )

        /*
         * TARJETA DE RESPUESTA.
         */
        val answerCard =
            LinearLayout(this)

        answerCard.orientation =
            LinearLayout.VERTICAL

        val cardBackground =
            GradientDrawable()

        cardBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        cardBackground.cornerRadius =
            dp(12).toFloat()

        answerCard.background =
            cardBackground

        val cardParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        cardParams.setMargins(
            0,
            dp(8),
            0,
            dp(18)
        )

        answerCard.layoutParams =
            cardParams

        /*
         * CABECERA.
         */
        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f

        /*
         * REINICIAR TEXTO.
         */
        val resetButton =
            TextView(this)

        resetButton.text =
            ""

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar texto"

        resetButton.isEnabled =
            false

        resetButton.alpha =
            0.35f

        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )

        /*
         * COPIAR TEXTO.
         */
        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar texto"

        copyButton.isClickable =
            true

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            copyButton
        )

        answerCard.addView(
            header
        )

        /*
         * CAMPO DE RESPUESTA.
         */
        val input =
            EditText(this)

        input.hint =
            "Escribe la salida"

        input.setTextColor(
            Color.WHITE
        )

        input.setHintTextColor(
            Color.rgb(
                140,
                145,
                155
            )
        )

        input.typeface =
            Typeface.MONOSPACE

        input.textSize =
            17f

        input.setPadding(
            dp(18),
            dp(16),
            dp(18),
            dp(16)
        )

        input.background =
            null

        input.setText(
            savedAnswer
        )

        answerCard.addView(
            input
        )

        lessonContent.addView(
            answerCard
        )

        /*
 * Fullscreen:
 *
 * código + campo de respuesta.
 *
 * El botón Comprobar queda fuera.
 */
        configureFakeCodeFullscreen(
            codeHandle,
            answerCard
        )

        val button =
            Button(this)

        button.text =
            "Comprobar"

        button.isAllCaps =
            false

        button.isEnabled =
            input.text
                .toString()
                .isNotBlank() &&
                    !savedCorrect

        lessonContent.addView(
            button
        )

        val result =
            TextView(this)

        result.textSize =
            16f

        result.setPadding(
            0,
            dp(12),
            0,
            dp(24)
        )

        lessonContent.addView(
            result
        )

        /*
         * ACTUALIZAR REINICIAR.
         *
         * NO usamos trim().
         * Un espacio también cuenta.
         */
        fun updateResetButton() {

            val enabled =
                answeredCorrectly ||
                        input.text.isNotEmpty()

            resetButton.isEnabled =
                enabled

            resetButton.alpha =
                if (
                    enabled
                ) {
                    1f
                } else {
                    0.35f
                }
        }

        /*
         * COPIAR EXACTAMENTE LO ESCRITO.
         */
        copyButton.setOnClickListener {

            val clipboard =
                getSystemService(
                    Context.CLIPBOARD_SERVICE
                ) as ClipboardManager

            val clip =
                ClipData.newPlainText(
                    "Texto",
                    input.text.toString()
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                "Texto copiado",
                Toast.LENGTH_SHORT
            ).show()
        }

        /*
         * ACTIVAR REINICIAR DESDE EL
         * PRIMER CARÁCTER, INCLUSO " ".
         */
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
                    s: Editable?
                ) {

                    updateResetButton()

                    button.isEnabled =
                        input.text
                            .toString()
                            .isNotBlank() &&
                                !answeredCorrectly
                }
            }
        )

        /*
         * RESTAURAR CORRECTO.
         */
        if (
            savedCorrect
        ) {

            answeredCorrectly =
                true

            completedExercises +=
                1

            input.isEnabled =
                false

            button.isEnabled =
                false

            button.text =
                "Correcto ✓"

            result.text =
                "✓ Correcto\n\n" +
                        block.optString(
                            "explanation"
                        )

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        updateResetButton()

        /*
         * REINICIAR.
         */
        resetButton.setOnClickListener {

            if (
                input.text.isEmpty() &&
                !answeredCorrectly
            ) {

                return@setOnClickListener
            }

            if (
                answeredCorrectly
            ) {

                answeredCorrectly =
                    false

                completedExercises =
                    (
                            completedExercises - 1
                            ).coerceAtLeast(
                            0
                        )

                pageCompletedExercises[
                    currentPage
                ] =
                    completedExercises
            }

            input.isEnabled =
                true

            input.setText(
                ""
            )

            button.isEnabled =
                false

            button.text =
                "Comprobar"

            result.text =
                ""

            val state =
                JSONObject()

            state.put(
                "answer",
                ""
            )

            state.put(
                "correct",
                false
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            updateResetButton()
        }

        /*
         * COMPROBAR.
         */
        button.setOnClickListener {

            val userAnswer =
                input.text
                    .toString()
                    .trim()

            val answers =
                block.getJSONArray(
                    "answers"
                )

            var correct =
                false

            for (
            i in 0 until
                    answers.length()
            ) {

                if (
                    userAnswer ==
                    answers
                        .getString(i)
                        .trim()
                ) {

                    correct =
                        true

                    break
                }
            }

            val state =
                JSONObject()

            state.put(
                "answer",
                input.text.toString()
            )

            state.put(
                "correct",
                correct
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    "✓ Correcto\n\n" +
                            block.optString(
                                "explanation"
                            )

                result.setTextColor(
                    Color.rgb(
                        180,
                        220,
                        190
                    )
                )

                if (
                    !answeredCorrectly
                ) {

                    answeredCorrectly =
                        true

                    markExerciseCompleted()

                    input.isEnabled =
                        false

                    button.isEnabled =
                        false

                    button.text =
                        "Correcto ✓"
                }

            } else {

                result.text =
                    "✗ Incorrecto"

                result.setTextColor(
                    Color.rgb(
                        240,
                        150,
                        150
                    )
                )
            }

            updateResetButton()
        }
    }

    private fun addOrderCode(
        block: JSONObject,
        blockIndex: Int
    ) {

        requiredExercises++

        var answeredCorrectly =
            false

        var nextTapPosition =
            0

        val savedState =
            ProgressRepository.getExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex
            )

        val savedOrder =
            savedState?.optJSONArray(
                "order"
            )

        val savedCorrect =
            savedState?.optBoolean(
                "correct",
                false
            ) ?: false

        addHeading(
            block.getString("question")
        )

        val instruction =
            TextView(this)

        instruction.text =
            "Mantén pulsado un bloque y arrástralo hasta la posición correcta."

        instruction.setTextColor(
            Color.rgb(180, 185, 195)
        )

        instruction.textSize =
            15f

        instruction.setPadding(
            0,
            0,
            0,
            dp(14)
        )

        lessonContent.addView(
            instruction
        )

        val codeCard =
            LinearLayout(this)

        codeCard.orientation =
            LinearLayout.VERTICAL

        val codeBackground =
            GradientDrawable()

        codeBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        codeBackground.cornerRadius =
            dp(12).toFloat()

        codeCard.background =
            codeBackground


        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )


        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f


        val resetButton =
            TextView(this)

        resetButton.text =
            ""

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar ejercicio"

        resetButton.isClickable =
            true


        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )


        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar código"

        copyButton.isClickable =
            true

        /*
 * PANTALLA COMPLETA.
 */
        val fullscreenButton =
            TextView(this)

        fullscreenButton.gravity =
            Gravity.CENTER

        fullscreenButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        fullscreenButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_fullscreen,
            0,
            0,
            0
        )

        fullscreenButton.contentDescription =
            "Pantalla completa"

        fullscreenButton.isClickable =
            true

        /*
         * Nos permitirá recuperar el nuevo botón
         * después de Reiniciar y reconstruir la página.
         */
        fullscreenButton.tag =
            "order_code_fullscreen_$blockIndex"

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            copyButton
        )

        header.addView(
            fullscreenButton
        )

        codeCard.addView(
            header
        )


        val fragments =
            block.getJSONArray(
                "fragments"
            )

        val correctOrder =
            block.getJSONArray(
                "correctOrder"
            )

        var puzzleStarted =
            savedOrder != null &&
                    (
                            0 until
                                    savedOrder.length()
                            ).any { index ->

                            savedOrder.optInt(
                                index,
                                index
                            ) != index
                        }

        val checkButton =
            Button(this)

        checkButton.text =
            "Comprobar"

        checkButton.isAllCaps =
            false

        checkButton.isEnabled =
            puzzleStarted &&
                    !savedCorrect

        /*
         * CUERPO COMPLETO DEL EDITOR.
         */
        val codeBody =
            LinearLayout(this)

        codeBody.orientation =
            LinearLayout.HORIZONTAL

        /*
         * COLUMNA DE NÚMEROS.
         */
        val lineNumberColumn =
            LinearLayout(this)

        lineNumberColumn.orientation =
            LinearLayout.VERTICAL

        lineNumberColumn.setBackgroundColor(
            Color.rgb(
                24,
                26,
                31
            )
        )

        lineNumberColumn.layoutParams =
            LinearLayout.LayoutParams(
                dp(18),
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        val lineNumberViews =
            mutableListOf<TextView>()

        for (
        i in 0 until fragments.length()
        ) {

            val number =
                TextView(this)

            number.text =
                "${i + 1}"

            number.typeface =
                Typeface.MONOSPACE

            number.textSize =
                16f

            number.includeFontPadding =
                false

            number.gravity =
                Gravity.CENTER_VERTICAL or
                        Gravity.END

            number.setTextColor(
                Color.rgb(
                    111,
                    119,
                    130
                )
            )

            number.setPadding(
                0,
                0,
                dp(3),
                0
            )

            number.layoutParams =
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )

            lineNumberViews.add(
                number
            )

            lineNumberColumn.addView(
                number
            )
        }

        /*
         * SCROLL HORIZONTAL DEL CÓDIGO.
         */
        val horizontalScroll =
            HorizontalScrollView(this)

        horizontalScroll.isHorizontalScrollBarEnabled =
            true

        horizontalScroll.isScrollbarFadingEnabled =
            false

        horizontalScroll.isFillViewport =
            true

        horizontalScroll.layoutParams =
            LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            )

        protectHorizontalScrollFromPageSwipe(
            horizontalScroll
        )

        /*
         * FILAS REORDENABLES.
         */
        val orderContainer =
            LinearLayout(this)

        orderContainer.orientation =
            LinearLayout.VERTICAL

        horizontalScroll.addView(
            orderContainer
        )

        codeBody.addView(
            lineNumberColumn
        )

        codeBody.addView(
            horizontalScroll
        )

        codeCard.addView(
            codeBody
        )

        lessonContent.addView(
            codeCard
        )

        makeCodeCardFullWidth(
            codeCard
        )

        /*
 * FULLSCREEN DEL PUZZLE DE ORDENAR.
 *
 * Movemos las Views reales.
 * Por tanto se conserva:
 *
 * - orden actual
 * - listeners
 * - drag and drop
 * - toque para ordenar
 */
        fullscreenButton.setOnClickListener {

            /*
             * SALIR.
             */
            if (
                exerciseFullscreenActive
            ) {

                closeExerciseFullscreenHost()

                fullscreenButton.contentDescription =
                    "Pantalla completa"

                return@setOnClickListener
            }

            /*
             * ENTRAR.
             *
             * Este ejercicio no utiliza teclado.
             */
            openExerciseFullscreenHost(
                usesKeyboard = false
            ) {
                    headerHost,
                    bodyHost,
                    _ ->

                /*
                 * Scroll vertical para permitir
                 * ejercicios de muchas líneas.
                 */
                val verticalCodeScroll =
                    ScrollView(this)

                verticalCodeScroll.isFillViewport =
                    true

                verticalCodeScroll.isVerticalScrollBarEnabled =
                    true

                verticalCodeScroll.isScrollbarFadingEnabled =
                    false

                verticalCodeScroll.layoutParams =
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        0,
                        1f
                    )

                bodyHost.addView(
                    verticalCodeScroll
                )

                /*
                 * codeBody era el hijo 1 de codeCard.
                 * Lo retiramos primero para conservar
                 * correctamente su índice original.
                 */
                moveViewToExerciseFullscreen(
                    codeBody,
                    verticalCodeScroll,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                /*
                 * header era el hijo 0.
                 * Visualmente permanece fijo arriba.
                 */
                moveViewToExerciseFullscreen(
                    header,
                    headerHost,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                fullscreenButton.contentDescription =
                    "Salir de pantalla completa"
            }
        }

        fun saveOrderState(
            correct: Boolean =
                answeredCorrectly
        ) {

            val orderJson =
                org.json.JSONArray()

            for (
            i in 0 until
                    orderContainer.childCount
            ) {

                val row =
                    orderContainer
                        .getChildAt(i)

                orderJson.put(
                    row.tag as Int
                )
            }

            val state =
                JSONObject()

            state.put(
                "order",
                orderJson
            )

            state.put(
                "correct",
                correct
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )
        }

        for (
        i in 0 until fragments.length()
        ) {

            val row =
                TextView(this)

            row.text =
                fragments.getString(i)

            // Guardamos cuál era el bloque original.
            row.tag = i

            row.typeface =
                Typeface.MONOSPACE

            row.textSize =
                16f

            row.setTextColor(
                Color.rgb(225, 230, 235)
            )

            row.setPadding(
                dp(16),
                dp(8),
                dp(16),
                dp(8)
            )

            val background =
                GradientDrawable()

            background.setColor(
                Color.rgb(30, 33, 39)
            )

            background.cornerRadius =
                dp(10).toFloat()

            background.setStroke(
                dp(1),
                Color.rgb(65, 69, 78)
            )

            row.background =
                background

            val params =
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )

            params.setMargins(
                0,
                dp(2),
                0,
                dp(2)
            )

            row.layoutParams =
                params

            var dragStarted =
                false

            var downX =
                0f

            var downY =
                0f

            val dragThreshold =
                maxOf(
                    dp(2),
                    android.view.ViewConfiguration
                        .get(this)
                        .scaledTouchSlop / 2
                )

            row.setOnTouchListener { view, event ->

                if (answeredCorrectly) {

                    return@setOnTouchListener false
                }

                when (event.actionMasked) {

                    MotionEvent.ACTION_DOWN -> {

                        ignoreCurrentSwipe =
                            true

                        dragStarted =
                            false

                        downX =
                            event.x

                        downY =
                            event.y

                        view.parent
                            ?.requestDisallowInterceptTouchEvent(
                                true
                            )

                        true
                    }

                    MotionEvent.ACTION_MOVE -> {

                        ignoreCurrentSwipe =
                            true

                        view.parent
                            ?.requestDisallowInterceptTouchEvent(
                                true
                            )

                        val distanceX =
                            kotlin.math.abs(
                                event.x - downX
                            )

                        val distanceY =
                            kotlin.math.abs(
                                event.y - downY
                            )

                        if (
                            !dragStarted &&
                            (
                                    distanceX >= dragThreshold ||
                                            distanceY >= dragThreshold
                                    )
                        ) {

                            dragStarted =
                                true

                            val data =
                                ClipData.newPlainText(
                                    "code_order",
                                    ""
                                )

                            view.alpha =
                                0.55f

                            view.startDragAndDrop(
                                data,
                                View.DragShadowBuilder(
                                    view
                                ),
                                view,
                                0
                            )
                        }

                        true
                    }

                    MotionEvent.ACTION_UP -> {

                        view.parent
                            ?.requestDisallowInterceptTouchEvent(
                                false
                            )

                        if (
                            !dragStarted
                        ) {

                            view.performClick()
                        }

                        true
                    }

                    MotionEvent.ACTION_CANCEL -> {

                        view.parent
                            ?.requestDisallowInterceptTouchEvent(
                                false
                            )

                        if (
                            !dragStarted
                        ) {

                            view.alpha =
                                1f
                        }

                        true
                    }

                    else ->
                        true
                }
            }

            row.setOnClickListener {

                if (
                    answeredCorrectly
                ) {

                    return@setOnClickListener
                }

                /*
                 * Cada toque fija el siguiente puesto:
                 *
                 * primer toque  -> posición 0
                 * segundo toque -> posición 1
                 * tercero       -> posición 2
                 * ...
                 */

                if (
                    nextTapPosition <
                    orderContainer.childCount
                ) {

                    moveViewToIndexAnimated(
                        row,
                        orderContainer,
                        nextTapPosition
                    )

                    nextTapPosition++

                    puzzleStarted =
                        true

                    checkButton.isEnabled =
                        true

                    saveOrderState()
                }
            }

            row.setOnDragListener { target, event ->

                val source =
                    event.localState as? TextView
                        ?: return@setOnDragListener false

                when (event.action) {

                    DragEvent.ACTION_DRAG_STARTED -> {
                        true
                    }

                    DragEvent.ACTION_DRAG_ENTERED -> {

                        if (
                            source !== target &&
                            source.parent === orderContainer &&
                            target.parent === orderContainer
                        ) {

                            val sourceIndex =
                                orderContainer.indexOfChild(
                                    source
                                )

                            val targetIndex =
                                orderContainer.indexOfChild(
                                    target
                                )

                            orderContainer.removeView(
                                source
                            )

                            val newIndex =
                                if (
                                    sourceIndex <
                                    targetIndex
                                ) {

                                    minOf(
                                        targetIndex,
                                        orderContainer.childCount
                                    )

                                } else {

                                    targetIndex
                                }

                            orderContainer.addView(
                                source,
                                newIndex
                            )

                            puzzleStarted =
                                true

                            checkButton.isEnabled =
                                true

                            saveOrderState()
                        }

                        true
                    }

                    DragEvent.ACTION_DRAG_ENDED -> {

                        source.alpha =
                            1f

                        true
                    }

                    else -> true
                }
            }

            orderContainer.addView(
                row
            )
        }

        fun syncOrderLineNumbers() {

            orderContainer.post {

                val amount =
                    minOf(
                        lineNumberViews.size,
                        orderContainer.childCount
                    )

                for (
                i in 0 until amount
                ) {

                    val row =
                        orderContainer.getChildAt(
                            i
                        )

                    val rowParams =
                        row.layoutParams
                                as? ViewGroup.MarginLayoutParams

                    val totalHeight =
                        row.height +
                                (
                                        rowParams?.topMargin
                                            ?: 0
                                        ) +
                                (
                                        rowParams?.bottomMargin
                                            ?: 0
                                        )

                    val number =
                        lineNumberViews[i]

                    val numberParams =
                        number.layoutParams

                    numberParams.height =
                        totalHeight

                    number.layoutParams =
                        numberParams
                }
            }
        }

        syncOrderLineNumbers()

        if (
            savedOrder != null &&
            savedOrder.length() ==
            orderContainer.childCount
        ) {

            val rowsByOriginalIndex =
                mutableMapOf<Int, View>()

            for (
            i in 0 until
                    orderContainer.childCount
            ) {

                val row =
                    orderContainer.getChildAt(i)

                rowsByOriginalIndex[
                    row.tag as Int
                ] = row
            }

            orderContainer.removeAllViews()

            for (
            i in 0 until
                    savedOrder.length()
            ) {

                val originalIndex =
                    savedOrder.getInt(i)

                val row =
                    rowsByOriginalIndex[
                        originalIndex
                    ]

                if (
                    row != null
                ) {

                    orderContainer.addView(
                        row
                    )
                }
            }
        }

        syncOrderLineNumbers()

        copyButton.setOnClickListener {

            val currentCode =
                buildString {

                    for (
                    i in 0 until
                            orderContainer.childCount
                    ) {

                        if (
                            i > 0
                        ) {

                            append(
                                "\n"
                            )
                        }

                        val line =
                            orderContainer
                                .getChildAt(i)
                                    as TextView

                        append(
                            line.text.toString()
                        )
                    }
                }

            copyCodeToClipboard(
                currentCode
            )
        }

        resetButton.setOnClickListener {

            /*
             * Si Reiniciar se pulsa desde fullscreen,
             * cerramos primero las Views actuales.
             */
            val reopenFullscreen =
                exerciseFullscreenActive

            if (
                reopenFullscreen
            ) {

                closeExerciseFullscreenHost()
            }

            val originalOrder =
                org.json.JSONArray()

            for (
            i in 0 until
                    fragments.length()
            ) {

                originalOrder.put(
                    i
                )
            }

            val resetState =
                JSONObject()

            resetState.put(
                "order",
                originalOrder
            )

            resetState.put(
                "correct",
                false
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                resetState
            )

            pageViewCache.remove(
                currentPage
            )

            pageRequiredExercises.remove(
                currentPage
            )

            pageCompletedExercises.remove(
                currentPage
            )

            renderCurrentPage()

            /*
             * Si estábamos en fullscreen,
             * volvemos automáticamente al nuevo
             * puzzle recién reiniciado.
             */
            if (
                reopenFullscreen
            ) {

                lessonContent.post {

                    val newFullscreenButton =
                        lessonContent.findViewWithTag<View>(
                            "order_code_fullscreen_$blockIndex"
                        )

                    newFullscreenButton
                        ?.performClick()
                }
            }
        }

        val checkParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        checkParams.setMargins(
            0,
            dp(18),
            0,
            dp(8)
        )

        checkButton.layoutParams =
            checkParams

        lessonContent.addView(
            checkButton
        )

        val result =
            TextView(this)

        result.textSize =
            16f

        result.setPadding(
            0,
            dp(8),
            0,
            dp(24)
        )

        lessonContent.addView(
            result
        )

        if (
            savedCorrect
        ) {

            answeredCorrectly =
                true

            completedExercises +=
                1

            checkButton.isEnabled =
                false

            checkButton.text =
                "Correcto ✓"

            result.text =
                "✓ Correcto\n\n" +
                        block.optString(
                            "explanation"
                        )

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )

            for (
            i in 0 until
                    orderContainer.childCount
            ) {

                orderContainer
                    .getChildAt(i)
                    .setOnLongClickListener(
                        null
                    )
            }
        }

        checkButton.setOnClickListener {

            var correct =
                true

            for (
            i in 0 until
                    orderContainer.childCount
            ) {

                val row =
                    orderContainer.getChildAt(i)

                val actualText =
                    (row as TextView)
                        .text
                        .toString()

                val expectedFragmentIndex =
                    correctOrder.getInt(i)

                val expectedText =
                    fragments.getString(
                        expectedFragmentIndex
                    )

                if (
                    actualText !=
                    expectedText
                ) {

                    correct = false
                    break
                }
            }

            saveOrderState(
                correct
            )

            if (correct) {

                result.text =
                    "✓ Correcto\n\n" +
                            block.optString(
                                "explanation"
                            )

                result.setTextColor(
                    Color.rgb(
                        180,
                        220,
                        190
                    )
                )

                if (!answeredCorrectly) {

                    answeredCorrectly =
                        true

                    markExerciseCompleted()

                    checkButton.isEnabled =
                        false

                    checkButton.text =
                        "Correcto ✓"

                    for (
                    i in 0 until
                            orderContainer.childCount
                    ) {

                        orderContainer
                            .getChildAt(i)
                            .setOnLongClickListener(
                                null
                            )
                    }
                }

            } else {

                result.text =
                    "✗ El orden todavía no es correcto."

                result.setTextColor(
                    Color.rgb(
                        240,
                        150,
                        150
                    )
                )
            }
        }
    }

    private fun addCppCompiler(
        block: JSONObject
    ) {

        val initialCode =
            block.optString(
                "initialCode",
                ""
            )

        /*
         * TARJETA DEL EDITOR.
         */
        val codeCard =
            LinearLayout(this)

        codeCard.orientation =
            LinearLayout.VERTICAL

        val cardBackground =
            GradientDrawable()

        cardBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        cardBackground.cornerRadius =
            dp(12).toFloat()

        codeCard.background =
            cardBackground

        codeCard.clipToOutline =
            true

        /*
         * CABECERA.
         */
        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f

        /*
         * REINICIAR.
         */
        val resetButton =
            TextView(this)

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar código"

        resetButton.isClickable =
            true

        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )

        /*
         * AJUSTE DE LÍNEA.
         */
        val wrapButton =
            TextView(this)

        wrapButton.gravity =
            Gravity.CENTER

        wrapButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        wrapButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_wrap_off,
            0,
            0,
            0
        )

        wrapButton.contentDescription =
            "Ajuste de línea desactivado"

        wrapButton.isClickable =
            true

        /*
         * COPIAR.
         */
        val copyButton =
            TextView(this)

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar código"

        copyButton.isClickable =
            true

        /*
 * PANTALLA COMPLETA.
 */
        val fullscreenButton =
            TextView(this)

        fullscreenButton.gravity =
            Gravity.CENTER

        fullscreenButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        fullscreenButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_fullscreen,
            0,
            0,
            0
        )

        fullscreenButton.contentDescription =
            "Pantalla completa"

        fullscreenButton.isClickable =
            true

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            wrapButton
        )

        header.addView(
            copyButton
        )

        header.addView(
            fullscreenButton
        )

        codeCard.addView(
            header
        )

        /*
         * CUERPO DEL EDITOR.
         */
        val editorBody =
            LinearLayout(this)

        editorBody.orientation =
            LinearLayout.HORIZONTAL

        editorBody.setBackgroundColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        /*
         * NÚMEROS.
         */
        val lineNumbers =
            TextView(this)

        lineNumbers.typeface =
            Typeface.MONOSPACE

        lineNumbers.textSize =
            16f

        lineNumbers.includeFontPadding =
            false

        lineNumbers.gravity =
            Gravity.TOP or
                    Gravity.END

        lineNumbers.setTextColor(
            Color.rgb(
                111,
                119,
                130
            )
        )

        lineNumbers.setBackgroundColor(
            Color.rgb(
                24,
                26,
                31
            )
        )

        lineNumbers.setPadding(
            0,
            dp(16),
            dp(3),
            dp(16)
        )

        /*
 * El TextView contiene TODOS los números.
 * Su contenedor será quien se desplace.
 */
        lineNumbers.layoutParams =
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            )

        val lineNumberScroll =
            ScrollView(this)

        lineNumberScroll.isVerticalScrollBarEnabled =
            false

        lineNumberScroll.isFillViewport =
            false

        lineNumberScroll.isClickable =
            false

        lineNumberScroll.isFocusable =
            false

        lineNumberScroll.layoutParams =
            LinearLayout.LayoutParams(
                dp(18),
                LinearLayout.LayoutParams.MATCH_PARENT
            )

        lineNumberScroll.addView(
            lineNumbers
        )

        /*
         * EDITOR REAL.
         */
        val sourceCode =
            FreeScrollEditText(this)

        sourceCode.tag =
            CODE_INPUT_TAG

        enableCppAutoClose(
            sourceCode
        )

        sourceCode.typeface =
            Typeface.MONOSPACE

        sourceCode.textSize =
            16f

        sourceCode.includeFontPadding =
            false

        sourceCode.gravity =
            Gravity.TOP or
                    Gravity.START

        sourceCode.setTextColor(
            Color.rgb(
                230,
                237,
                243
            )
        )

        sourceCode.setBackgroundColor(
            Color.TRANSPARENT
        )

        sourceCode.inputType =
            android.text.InputType.TYPE_CLASS_TEXT or
                    android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                    android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS

        /*
 * Impide que Android/SwiftKey sustituya
 * nuestro editor por su editor especial
 * de pantalla completa al usar el teléfono
 * horizontalmente.
 *
 * Debe permanecer visible nuestra interfaz:
 *
 * - cabecera
 * - números de línea
 * - barras
 * - comportamiento real de wrap
 */
        sourceCode.imeOptions =
            sourceCode.imeOptions or
                    android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI or
                    android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN

        sourceCode.setPadding(
            dp(10),
            dp(16),
            dp(16),
            dp(16)
        )

        sourceCode.minHeight =
            dp(220)

        sourceCode.setHorizontallyScrolling(
            true
        )

        /*
         * No utilizamos la scrollbar nativa
         * del EditText.
         *
         * En algunos dispositivos Android
         * su Drawable interno puede ser nulo
         * y provocar un crash al dibujarse.
         */
        sourceCode.isHorizontalScrollBarEnabled =
            false

        /*
 * Las scrollbars nativas del EditText
 * permanecen desactivadas.
 *
 * Utilizaremos una barra vertical propia,
 * igual que hacemos con la horizontal.
 */
        sourceCode.isVerticalScrollBarEnabled =
            false

        sourceCode.layoutParams =
            LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            )

        sourceCode.setText(
            initialCode
        )

        editorBody.addView(
            lineNumberScroll
        )

        /*
         * CONTENEDOR DEL EDITOR.
         *
         * Nos permite superponer una barra vertical
         * propia sobre el lado derecho del código.
         */
        val editorContainer =
            FrameLayout(this)

        editorContainer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.MATCH_PARENT,
                1f
            )

        /*
         * El EditText ocupa todo el contenedor.
         */
        sourceCode.layoutParams =
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )

        editorContainer.addView(
            sourceCode
        )

        /*
         * BARRA VERTICAL PERSONALIZADA.
         */
        val editorVerticalBar =
            SeekBar(this)

        editorVerticalBar.rotation =
            90f

        editorVerticalBar.visibility =
            View.GONE

        editorVerticalBar.setSplitTrack(
            false
        )

        val verticalBarParams =
            FrameLayout.LayoutParams(
                dp(180),
                dp(28),
                Gravity.END or
                        Gravity.CENTER_VERTICAL
            )

        /*
         * Como la barra está rotada 90 grados,
         * sus dimensiones visuales se intercambian.
         */
        verticalBarParams.marginEnd =
            -dp(76)

        editorVerticalBar.layoutParams =
            verticalBarParams

        editorContainer.addView(
            editorVerticalBar
        )

        editorBody.addView(
            editorContainer
        )

        codeCard.addView(
            editorBody
        )

        /*
         * BARRA HORIZONTAL DEL EDITOR.
         *
         * Es independiente de la scrollbar
         * nativa del EditText.
         */
        val editorHorizontalBar =
            SeekBar(this)

        editorHorizontalBar.visibility =
            View.GONE

        editorHorizontalBar.setSplitTrack(
            false
        )

        editorHorizontalBar.setPadding(
            dp(18),
            0,
            dp(18),
            0
        )

        editorHorizontalBar.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dp(28)
            )

        codeCard.addView(
            editorHorizontalBar
        )

        lessonContent.addView(
            codeCard
        )

        makeCodeCardFullWidth(
            codeCard
        )

        /*
 * ESPACIO INFERIOR SEGURO DEL EDITOR.
 *
 * La última línea nunca debe quedar
 * debajo ni cortada por la barra horizontal.
 *
 * El espacio depende de lineHeight,
 * por lo que también aumenta automáticamente
 * cuando aumenta el tamaño del texto.
 */
        fun updateEditorBottomSafety() {

            val safeBottomPadding =
                if (
                    compilerFullscreenActive
                ) {

                    /*
                     * En fullscreen dejamos una zona
                     * adicional para proteger completamente
                     * la última línea y el cursor.
                     */
                    maxOf(
                        sourceCode.lineHeight,
                        lineNumbers.lineHeight
                    ) +
                            dp(12)

                } else {

                    /*
                     * En la ventana normal no necesitamos
                     * otra línea artificial debajo.
                     *
                     * La propia altura de 10 líneas ya
                     * reservará correctamente el viewport.
                     */
                    dp(16)
                }

            /*
             * Conservamos izquierda, arriba y derecha.
             * Solo cambia el espacio inferior.
             */
            if (
                sourceCode.paddingBottom !=
                safeBottomPadding
            ) {

                sourceCode.setPadding(
                    sourceCode.paddingLeft,
                    sourceCode.paddingTop,
                    sourceCode.paddingRight,
                    safeBottomPadding
                )
            }

            /*
             * Los números necesitan exactamente
             * la misma zona inferior para seguir
             * alineados con el código.
             */
            if (
                lineNumbers.paddingBottom !=
                safeBottomPadding
            ) {

                lineNumbers.setPadding(
                    lineNumbers.paddingLeft,
                    lineNumbers.paddingTop,
                    lineNumbers.paddingRight,
                    safeBottomPadding
                )
            }
        }

        /*
 * ALTURA DEL EDITOR EN VISTA NORMAL.
 *
 * La ventana normal siempre equivale
 * a 10 líneas del tamaño de texto actual.
 *
 * Si el código necesita más espacio,
 * el editor no crece:
 * aparece desplazamiento vertical.
 */
        fun updateNormalCompilerEditorHeight() {

            /*
             * Fullscreen controla su propia altura.
             */
            if (
                compilerFullscreenActive
            ) {
                return
            }

            /*
             * Primero actualizamos el espacio inferior
             * de seguridad porque forma parte de la
             * altura real necesaria del editor.
             */
            updateEditorBottomSafety()

            val visibleLines =
                10

            /*
 * La altura TOTAL de la caja equivale
 * exactamente a 10 alturas de línea.
 *
 * No añadimos los padding porque entonces
 * la caja terminaría midiendo más de
 * las 10 líneas solicitadas.
 */
            /*
 * Queremos 10 líneas COMPLETAS visibles.
 *
 * Por eso la altura total debe contener:
 *
 * padding superior
 * + 10 alturas reales de línea
 * + padding inferior
 *
 * lineHeight cambia automáticamente cuando
 * cambia el tamaño del texto del curso.
 */
            val desiredHeight =
                sourceCode.totalPaddingTop +
                        sourceCode.lineHeight *
                        visibleLines +
                        sourceCode.totalPaddingBottom

            val params =
                editorBody.layoutParams
                        as? LinearLayout.LayoutParams
                    ?: LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        desiredHeight
                    )

            /*
             * Evitamos provocar otro layout
             * si ya tenemos exactamente la altura.
             */
            if (
                params.height !=
                desiredHeight
            ) {

                params.width =
                    LinearLayout.LayoutParams.MATCH_PARENT

                params.height =
                    desiredHeight

                params.weight =
                    0f

                editorBody.layoutParams =
                    params

                editorBody.requestLayout()
            }
        }

        fun updateEditorHorizontalBar() {

            val layout =
                sourceCode.layout

            if (
                layout == null ||
                sourceCode.width <= 0
            ) {

                editorHorizontalBar.visibility =
                    View.GONE

                return
            }

            val availableWidth =
                (
                        sourceCode.width -
                                sourceCode.totalPaddingLeft -
                                sourceCode.totalPaddingRight
                        ).coerceAtLeast(
                        1
                    )

            var widestLine =
                0f

            for (
            line in 0 until
                    layout.lineCount
            ) {

                widestLine =
                    maxOf(
                        widestLine,
                        layout.getLineWidth(
                            line
                        )
                    )
            }

            val maximumScroll =
                (
                        widestLine -
                                availableWidth
                        )
                    .toInt()
                    .coerceAtLeast(
                        0
                    )

            if (
                maximumScroll <= 0
            ) {

                editorHorizontalBar.visibility =
                    View.GONE

                editorHorizontalBar.progress =
                    0

                /*
                 * Si activamos wrap o el texto
                 * vuelve a caber, regresamos
                 * al inicio horizontal.
                 */
                if (
                    sourceCode.scrollX != 0
                ) {

                    sourceCode.scrollTo(
                        0,
                        sourceCode.scrollY
                    )
                }

                return
            }

            editorHorizontalBar.max =
                maximumScroll.coerceAtLeast(
                    1
                )

            editorHorizontalBar.progress =
                sourceCode.scrollX.coerceIn(
                    0,
                    maximumScroll
                )

            /*
             * En fullscreen horizontal con teclado
             * ocultamos solamente la barra VISUAL.
             *
             * El max se conserva, por lo que nuestros
             * gestos horizontales continúan funcionando.
             */
            val hideHorizontalBar =
                compilerFullscreenActive &&
                        keyboardVisible &&
                        resources.configuration.orientation ==
                        android.content.res.Configuration.ORIENTATION_LANDSCAPE

            editorHorizontalBar.visibility =
                if (
                    hideHorizontalBar
                ) {
                    View.GONE
                } else {
                    View.VISIBLE
                }
        }

        fun updateEditorVerticalBar() {

            /*
             * Primero reservamos espacio suficiente
             * para que la última línea y el cursor
             * puedan mostrarse completamente.
             */
            updateEditorBottomSafety()

            val layout =
                sourceCode.layout

            if (
                layout == null ||
                sourceCode.height <= 0
            ) {

                editorVerticalBar.visibility =
                    View.GONE

                return
            }

            /*
             * Altura completa ocupada por todo
             * el texto del editor.
             */
            /*
 * Altura REAL disponible únicamente
 * para dibujar texto.
 *
 * Quitamos el padding porque éste no forma
 * parte del viewport de las líneas.
 */
            val visibleTextHeight =
                (
                        sourceCode.height -
                                sourceCode.totalPaddingTop -
                                sourceCode.totalPaddingBottom
                        ).coerceAtLeast(
                        1
                    )

            /*
             * layout.height representa la altura que
             * realmente necesita el texto.
             *
             * Si wrap está activado, las líneas visuales
             * adicionales también están incluidas aquí.
             */
            val contentTextHeight =
                layout.height

            val maximumScroll =
                (
                        contentTextHeight -
                                visibleTextHeight
                        ).coerceAtLeast(
                        0
                    )

            if (
                maximumScroll <=
                dp(1)
            ) {

                editorVerticalBar.visibility =
                    View.GONE

                editorVerticalBar.progress =
                    0

                if (
                    sourceCode.scrollY != 0
                ) {

                    sourceCode.scrollTo(
                        sourceCode.scrollX,
                        0
                    )
                }

                return
            }

            editorVerticalBar.visibility =
                View.VISIBLE

            editorVerticalBar.max =
                maximumScroll.coerceAtLeast(
                    1
                )

            editorVerticalBar.progress =
                sourceCode.scrollY.coerceIn(
                    0,
                    maximumScroll
                )
        }

        fun updateCompilerCompactLandscapeUi() {

            val compactMode =
                compilerFullscreenActive &&
                        keyboardVisible &&
                        resources.configuration.orientation ==
                        android.content.res.Configuration.ORIENTATION_LANDSCAPE

            /*
             * Cabecera del compilador:
             *
             * </> C++
             * Reiniciar
             * Wrap
             * Copiar
             * Fullscreen
             */
            header.visibility =
                if (
                    compactMode
                ) {
                    View.GONE
                } else {
                    View.VISIBLE
                }

            /*
             * Recalculamos la barra horizontal.
             *
             * En modo compacto permanecerá oculta,
             * pero seguirá conservando su max.
             */
            updateEditorHorizontalBar()
        }

        editorHorizontalBar
            .setOnSeekBarChangeListener(
                object :
                    SeekBar.OnSeekBarChangeListener {

                    override fun onProgressChanged(
                        seekBar: SeekBar?,
                        progress: Int,
                        fromUser: Boolean
                    ) {

                        if (
                            fromUser
                        ) {

                            sourceCode.scrollTo(
                                progress,
                                sourceCode.scrollY
                            )
                        }
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

        editorVerticalBar
            .setOnSeekBarChangeListener(
                object :
                    SeekBar.OnSeekBarChangeListener {

                    override fun onProgressChanged(
                        seekBar: SeekBar?,
                        progress: Int,
                        fromUser: Boolean
                    ) {

                        if (
                            fromUser
                        ) {

                            sourceCode.scrollTo(
                                sourceCode.scrollX,
                                progress
                            )
                        }
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

        editorVerticalBar.setOnTouchListener {
                view,
                event ->

            ignoreCurrentSwipe =
                true

            when (
                event.actionMasked
            ) {

                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_MOVE -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            true
                        )
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )
                }
            }

            false
        }

        editorHorizontalBar.setOnTouchListener {
                view,
                event ->

            /*
             * Cualquier gesto iniciado sobre esta barra
             * pertenece exclusivamente al editor.
             */
            ignoreCurrentSwipe =
                true

            when (
                event.actionMasked
            ) {

                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_MOVE -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            true
                        )
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )
                }
            }

            /*
             * Con teclado abierto se mantiene
             * el comportamiento anterior:
             * la barra es solo referencia visual.
             */
            keyboardVisible
        }

        /*
 * GESTOS DIRECTAMENTE SOBRE EL EDITOR.
 *
 * FULLSCREEN:
 *
 * toque sin arrastrar
 *     -> coloca el cursor
 *
 * arrastre horizontal
 *     -> NUNCA mueve el cursor
 *
 * si existe scroll horizontal
 *     -> desplaza el código
 *
 * si no existe scroll horizontal
 *     -> simplemente consume el gesto
 *
 * arrastre vertical con scroll vertical
 *     -> desplaza el código
 */
        var editorTouchStartX =
            0f

        var editorTouchStartY =
            0f

        var editorInitialScrollX =
            0

        var editorInitialScrollY =
            0

        /*
 * 0 = posible toque
 * 1 = scroll horizontal
 * 2 = scroll vertical
 * 3 = gesto horizontal fullscreen sin recorrido
 * 4 = gesto vertical fullscreen sin recorrido
 *
 * Los modos 3 y 4 consumen el gesto
 * sin mover el cursor.
 */
        var editorGestureMode =
            0

        val editorDragThreshold =
            maxOf(
                dp(2),
                android.view.ViewConfiguration
                    .get(this)
                    .scaledTouchSlop / 2
            ).toFloat()

        sourceCode.setOnTouchListener {
                view,
                event ->

            when (
                event.actionMasked
            ) {

                MotionEvent.ACTION_DOWN -> {

                    /*
                     * Todo gesto iniciado dentro
                     * del editor pertenece al editor.
                     */
                    ignoreCurrentSwipe =
                        true

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            true
                        )

                    editorTouchStartX =
                        event.x

                    editorTouchStartY =
                        event.y

                    editorInitialScrollX =
                        sourceCode.scrollX

                    editorInitialScrollY =
                        sourceCode.scrollY

                    editorGestureMode =
                        0

                    /*
                     * Consumimos DOWN.
                     *
                     * El cursor se colocará manualmente
                     * en ACTION_UP solamente si realmente
                     * fue un toque.
                     */
                    true
                }

                MotionEvent.ACTION_MOVE -> {

                    ignoreCurrentSwipe =
                        true

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            true
                        )

                    val movementX =
                        event.x -
                                editorTouchStartX

                    val movementY =
                        event.y -
                                editorTouchStartY

                    val distanceX =
                        kotlin.math.abs(
                            movementX
                        )

                    val distanceY =
                        kotlin.math.abs(
                            movementY
                        )

                    /*
                     * Decidimos la intención apenas
                     * el dedo supera el pequeño umbral.
                     */
                    if (
                        editorGestureMode ==
                        0 &&
                        (
                                distanceX >=
                                        editorDragThreshold ||
                                        distanceY >=
                                        editorDragThreshold
                                )
                    ) {

                        if (
                            distanceX >
                            distanceY
                        ) {

                            /*
 * Desde que sabemos que el gesto
 * es horizontal, el cursor deja
 * de controlar el scroll.
 */
                            sourceCode.suppressCursorAutoScroll =
                                true

                            /*
                             * MOVIMIENTO HORIZONTAL.
                             *
                             * En fullscreen jamás puede
                             * convertirse en movimiento
                             * del cursor.
                             */
                            if (
                                compilerFullscreenActive
                            ) {

                                editorGestureMode =
                                    if (
                                        editorHorizontalBar.max >
                                        0
                                    ) {
                                        1
                                    } else {
                                        3
                                    }

                            } else {

                                /*
                                 * Fuera de fullscreen:
                                 * si realmente existe recorrido,
                                 * hacemos scroll horizontal.
                                 */
                                editorGestureMode =
                                    if (
                                        editorHorizontalBar.max >
                                        0
                                    ) {
                                        1
                                    } else {
                                        0
                                    }
                            }

                        } else {

                            /*
                             * MOVIMIENTO VERTICAL.
                             *
                             * Igual que en horizontal:
                             * desde que detectamos un arrastre
                             * dejamos de permitir que el cursor
                             * controle automáticamente la vista.
                             */
                            sourceCode.suppressCursorAutoScroll =
                                true

                            if (
                                compilerFullscreenActive
                            ) {

                                /*
                                 * FULLSCREEN:
                                 *
                                 * cualquier arrastre vertical
                                 * deja de pertenecer al cursor.
                                 */
                                editorGestureMode =
                                    if (
                                        editorVerticalBar.max >
                                        0
                                    ) {

                                        /*
                                         * Existe recorrido vertical.
                                         */
                                        2

                                    } else {

                                        /*
                                         * No existe recorrido vertical,
                                         * pero tampoco permitimos que
                                         * el gesto mueva el cursor.
                                         */
                                        4
                                    }

                            } else {

                                /*
                                 * Vista normal:
                                 *
                                 * si existe desplazamiento vertical,
                                 * navegamos libremente.
                                 *
                                 * Si no existe, conservamos el
                                 * comportamiento normal del toque.
                                 */
                                editorGestureMode =
                                    if (
                                        editorVerticalBar.max >
                                        0
                                    ) {
                                        2
                                    } else {

                                        sourceCode.suppressCursorAutoScroll =
                                            false

                                        0
                                    }
                            }
                        }
                    }

                    when (
                        editorGestureMode
                    ) {

                        /*
                         * SCROLL HORIZONTAL.
                         */
                        1 -> {

                            val movement =
                                editorTouchStartX -
                                        event.x

                            val newScrollX =
                                (
                                        editorInitialScrollX +
                                                movement
                                        )
                                    .toInt()
                                    .coerceIn(
                                        0,
                                        editorHorizontalBar.max
                                    )

                            sourceCode.scrollTo(
                                newScrollX,
                                sourceCode.scrollY
                            )
                        }

                        /*
                         * SCROLL VERTICAL.
                         */
                        2 -> {

                            val movement =
                                editorTouchStartY -
                                        event.y

                            val newScrollY =
                                (
                                        editorInitialScrollY +
                                                movement
                                        )
                                    .toInt()
                                    .coerceIn(
                                        0,
                                        editorVerticalBar.max
                                    )

                            /*
                             * Código y números se desplazan
                             * exactamente la misma cantidad
                             * durante el mismo gesto.
                             */
                            sourceCode.scrollTo(
                                sourceCode.scrollX,
                                newScrollY
                            )

                            editorVerticalBar.progress =
                                newScrollY.coerceIn(
                                    0,
                                    editorVerticalBar.max
                                )
                        }

                        /*
                         * HORIZONTAL FULLSCREEN
                         * SIN RECORRIDO.
                         *
                         * No hacemos nada.
                         *
                         * Lo importante es que tampoco
                         * moveremos el cursor.
                         */
                        3 -> {
                        }

                        /*
 * VERTICAL FULLSCREEN
 * SIN RECORRIDO.
 *
 * Consumimos el gesto.
 *
 * No desplazamos la vista
 * y tampoco movemos el cursor.
 */
                        4 -> {
                        }
                    }

                    true
                }

                MotionEvent.ACTION_UP -> {

                    val wasSimpleTap =
                        editorGestureMode ==
                                0

                    /*
                     * Solamente un toque verdadero
                     * puede colocar el cursor.
                     */
                    if (
                        wasSimpleTap
                    ) {

                        /*
     * Un toque real sí significa:
     * "quiero trabajar aquí".
     *
     * Ahora permitimos nuevamente
     * que el cursor controle su posición.
     */
                        sourceCode.suppressCursorAutoScroll =
                            false

                        sourceCode.requestFocus()

                        val offset =
                            sourceCode.getOffsetForPosition(
                                event.x,
                                event.y
                            )

                        sourceCode.setSelection(
                            offset.coerceIn(
                                0,
                                sourceCode.text?.length ?: 0
                            )
                        )

                        val inputMethodManager =
                            getSystemService(
                                Context.INPUT_METHOD_SERVICE
                            ) as
                                    android.view.inputmethod.InputMethodManager

                        inputMethodManager.showSoftInput(
                            sourceCode,
                            android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT
                        )
                    }

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )

                    editorGestureMode =
                        0

                    ignoreCurrentSwipe =
                        false

                    true
                }

                MotionEvent.ACTION_CANCEL -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )

                    editorGestureMode =
                        0

                    ignoreCurrentSwipe =
                        false

                    true
                }

                else -> {

                    true
                }
            }
        }

        sourceCode.setOnScrollChangeListener {
                _,
                scrollX,
                scrollY,
                _,
                _ ->

            /*
             * Números sincronizados verticalmente.
             */
            /*
 * El contenedor permanece fijo.
 *
 * Movemos directamente el contenido de los
 * números exactamente la misma cantidad
 * que se desplazó el código.
 *
 * Así no existe un segundo límite de scroll
 * capaz de desalinear números y editor.
 */
            lineNumberScroll.scrollTo(
                0,
                0
            )

            lineNumbers.translationY =
                -scrollY.toFloat()

            /*
             * Barra horizontal sincronizada.
             */
            if (
                editorHorizontalBar.visibility ==
                View.VISIBLE
            ) {

                editorHorizontalBar.progress =
                    scrollX.coerceIn(
                        0,
                        editorHorizontalBar.max
                    )
            }

            /*
             * Barra vertical sincronizada.
             */
            if (
                editorVerticalBar.visibility ==
                View.VISIBLE
            ) {

                editorVerticalBar.progress =
                    scrollY.coerceIn(
                        0,
                        editorVerticalBar.max
                    )
            }
        }

        sourceCode.addOnLayoutChangeListener {
                _,
                _,
                _,
                _,
                _,
                _,
                _,
                _,
                _ ->

            updateNormalCompilerEditorHeight()

            updateCompilerCompactLandscapeUi()

            updateEditorVerticalBar()
        }

        sourceCode.post {

            updateNormalCompilerEditorHeight()

            updateEditorHorizontalBar()

            updateEditorVerticalBar()
        }

        /*
         * NÚMEROS DE LÍNEA.
         */
        fun updateLineNumbers() {

            /*
             * Esperamos a que Android haya calculado
             * el layout real del EditText.
             *
             * Esto es necesario porque una línea lógica
             * puede ocupar varias filas visuales cuando
             * el ajuste de línea está activado.
             */
            sourceCode.post {

                val layout =
                    sourceCode.layout
                        ?: return@post

                val text =
                    sourceCode.text
                        ?.toString()
                        .orEmpty()

                /*
                 * Cantidad REAL de líneas lógicas.
                 *
                 * Solo '\n' crea una línea nueva.
                 * El ajuste visual NO cuenta.
                 */
                val logicalLineAmount =
                    text.count {
                        it == '\n'
                    } + 1

                var logicalLineNumber =
                    1

                val displayedNumbers =
                    buildString {

                        for (
                        visualLine in
                        0 until layout.lineCount
                        ) {

                            val startOffset =
                                layout.getLineStart(
                                    visualLine
                                )

                            /*
                             * Primera fila visual del documento.
                             */
                            if (
                                visualLine == 0
                            ) {

                                append(
                                    logicalLineNumber
                                )

                            } else {

                                /*
                                 * Una nueva línea lógica solamente
                                 * empieza si justo antes existe '\n'.
                                 *
                                 * Si no existe '\n', significa que
                                 * esta fila es únicamente la
                                 * continuación visual de la misma
                                 * línea debido al ajuste.
                                 */
                                val startsNewLogicalLine =
                                    startOffset > 0 &&
                                            text.getOrNull(
                                                startOffset - 1
                                            ) == '\n'

                                if (
                                    startsNewLogicalLine
                                ) {

                                    logicalLineNumber++

                                    append(
                                        logicalLineNumber
                                    )

                                } else {

                                    /*
                                     * Continuación visual.
                                     *
                                     * Dejamos esta posición vacía
                                     * para conservar la alineación.
                                     */
                                    append("")
                                }
                            }

                            if (
                                visualLine <
                                layout.lineCount - 1
                            ) {

                                append('\n')
                            }
                        }
                    }

                lineNumbers.text =
                    displayedNumbers

                /*
                 * El ancho se calcula usando el número
                 * lógico más grande, no la cantidad de
                 * filas visuales.
                 */
                val widestNumber =
                    logicalLineAmount.toString()

                val textWidth =
                    kotlin.math.ceil(
                        lineNumbers.paint.measureText(
                            widestNumber
                        ).toDouble()
                    ).toInt()

                val requiredWidth =
                    maxOf(
                        dp(18),
                        textWidth +
                                dp(8)
                    )

                val params =
                    lineNumberScroll.layoutParams
                            as LinearLayout.LayoutParams

                if (
                    params.width !=
                    requiredWidth
                ) {

                    params.width =
                        requiredWidth

                    lineNumberScroll.layoutParams =
                        params
                }
            }
        }

        updateLineNumbers()

        sourceCode.addTextChangedListener(
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

                    /*
                     * Si el usuario estaba navegando
                     * libremente lejos del cursor,
                     * al comenzar a escribir volvemos
                     * inmediatamente hacia él.
                     */
                    if (
                        sourceCode.suppressCursorAutoScroll
                    ) {

                        sourceCode.suppressCursorAutoScroll =
                            false

                        sourceCode.post {

                            val cursorPosition =
                                sourceCode.selectionStart

                            if (
                                cursorPosition >= 0
                            ) {

                                sourceCode.bringPointIntoView(
                                    cursorPosition
                                )
                            }
                        }
                    }

                    updateLineNumbers()

                    sourceCode.post {

                        updateNormalCompilerEditorHeight()

                        updateEditorHorizontalBar()

                        updateEditorVerticalBar()
                    }
                }

                override fun afterTextChanged(
                    s: Editable?
                ) {
                }
            }
        )

        /*
         * BOTÓN COMPILAR.
         */
        val runButton =
            Button(this)

        runButton.text =
            "Compilar y ejecutar"

        runButton.isAllCaps =
            false

        runButton.isEnabled =
            true

        val runParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        runParams.setMargins(
            0,
            dp(20),
            0,
            0
        )

        runButton.layoutParams =
            runParams

        lessonContent.addView(
            runButton
        )

        /*
         * ESTADO.
         */
        val statusText =
            TextView(this)

        statusText.text =
            "Listo para ejecutar."

        statusText.textSize =
            15f

        statusText.setTextColor(
            Color.rgb(
                174,
                180,
                190
            )
        )

        statusText.setPadding(
            0,
            dp(20),
            0,
            dp(10)
        )

        lessonContent.addView(
            statusText
        )

        /*
         * CONSOLA.
         */
        val outputText =
            TextView(this)

        outputText.typeface =
            Typeface.MONOSPACE

        outputText.textSize =
            15f

        outputText.setTextColor(
            Color.rgb(
                121,
                226,
                157
            )
        )

        outputText.setBackgroundColor(
            Color.rgb(
                5,
                6,
                7
            )
        )

        outputText.setPadding(
            dp(16),
            dp(16),
            dp(16),
            dp(16)
        )

        outputText.minHeight =
            dp(120)

        outputText.setTextIsSelectable(
            true
        )

        lessonContent.addView(
            outputText
        )

        /*
 * PANTALLA COMPLETA DEL EDITOR.
 *
 * En fullscreen solamente permanecen:
 *
 * - cabecera C++
 * - Reiniciar
 * - Ajuste de línea
 * - Copiar
 * - Salir de fullscreen
 * - editor de código
 * - barra horizontal del editor
 *
 * Compilar, estado y consola permanecen
 * disponibles únicamente en la vista normal.
 */

        val lessonProgressView =
            findViewById<ProgressBar>(
                R.id.lessonProgress
            )

        val lessonTopBar =
            lessonProgressView.parent
                    as View

        val continueButton =
            findViewById<Button>(
                R.id.buttonContinue
            )

        val normalPaddingLeft =
            lessonContent.paddingLeft

        val normalPaddingTop =
            lessonContent.paddingTop

        val normalPaddingRight =
            lessonContent.paddingRight

        val normalPaddingBottom =
            lessonContent.paddingBottom

        val normalChildVisibilities =
            mutableMapOf<View, Int>()

        var normalSystemUiVisibility =
            window.decorView.systemUiVisibility

        var normalTopBarVisibility =
            lessonTopBar.visibility

        var normalContinueVisibility =
            continueButton.visibility

        /*
         * Ajusta la geometría del editor fullscreen.
         *
         * También se puede ejecutar después de
         * una rotación porque LessonActivity ya
         * no será destruida.
         */
        fun updateCompilerFullscreenGeometry() {

            if (
                !compilerFullscreenActive ||
                lessonScroll.height <= 0
            ) {
                return
            }

            /*
             * La tarjeta ocupa exactamente la altura
             * visible disponible.
             */
            val cardParams =
                codeCard.layoutParams
                        as LinearLayout.LayoutParams

            cardParams.width =
                LinearLayout.LayoutParams.MATCH_PARENT

            cardParams.height =
                lessonScroll.height

            codeCard.layoutParams =
                cardParams

            codeCard.translationX =
                0f

            /*
             * La cabecera conserva su altura normal.
             *
             * editorBody recibe TODO el espacio restante.
             * Por eso la cabecera nunca se desplaza aunque
             * existan miles de líneas.
             */
            editorBody.layoutParams =
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    0,
                    1f
                )

            /*
             * La columna de números ocupa toda
             * la altura del editor.
             */
            val numberParams =
                lineNumberScroll.layoutParams
                        as LinearLayout.LayoutParams

            numberParams.height =
                LinearLayout.LayoutParams.MATCH_PARENT

            lineNumberScroll.layoutParams =
                numberParams

            /*
             * El EditText ocupa toda la altura disponible.
             *
             * El desplazamiento vertical ocurre dentro
             * del propio editor, no moviendo la cabecera.
             */
            sourceCode.layoutParams =
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )

            sourceCode.minHeight =
                0

            sourceCode.requestLayout()

            editorBody.requestLayout()

            codeCard.requestLayout()

            sourceCode.post {

                updateEditorVerticalBar()

                updateLineNumbers()
            }
        }

        fun enterCompilerFullscreen() {

            if (
                compilerFullscreenActive
            ) {
                return
            }

            compilerFullscreenActive =
                true

            normalTopBarVisibility =
                lessonTopBar.visibility

            normalContinueVisibility =
                continueButton.visibility

            normalSystemUiVisibility =
                window.decorView.systemUiVisibility

            normalChildVisibilities.clear()

            /*
             * Guardamos la visibilidad normal de
             * todo el contenido de la página.
             */
            for (
            index in 0 until
                    lessonContent.childCount
            ) {

                val child =
                    lessonContent.getChildAt(
                        index
                    )

                normalChildVisibilities[
                    child
                ] =
                    child.visibility

                /*
                 * En fullscreen únicamente queda
                 * visible la tarjeta del editor.
                 *
                 * Esto oculta también:
                 *
                 * - Compilar y ejecutar
                 * - estado
                 * - consola
                 */
                child.visibility =
                    if (
                        child === codeCard
                    ) {
                        View.VISIBLE
                    } else {
                        View.GONE
                    }
            }

            lessonTopBar.visibility =
                View.GONE

            continueButton.visibility =
                View.GONE

            lessonContent.setPadding(
                0,
                0,
                0,
                0
            )

            /*
             * Ocultamos la barra de estado Android.
             */
            window.decorView.systemUiVisibility =
                normalSystemUiVisibility or
                        View.SYSTEM_UI_FLAG_FULLSCREEN or
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

            fullscreenButton.contentDescription =
                "Salir de pantalla completa"

            /*
             * Esperamos al nuevo tamaño disponible.
             */
            lessonScroll.post {

                lessonScroll.scrollTo(
                    0,
                    0
                )

                updateCompilerFullscreenGeometry()

                updateCompilerCompactLandscapeUi()

                updateProgrammingKeyboardBarVisibility()
            }
        }

        fun exitCompilerFullscreen() {

            if (
                !compilerFullscreenActive
            ) {
                return
            }

            compilerFullscreenActive =
                false

            /*
             * Recuperamos exactamente la visibilidad
             * que tenía cada elemento.
             */
            for (
            entry in normalChildVisibilities
            ) {

                entry.key.visibility =
                    entry.value
            }

            lessonTopBar.visibility =
                normalTopBarVisibility

            continueButton.visibility =
                normalContinueVisibility

            lessonContent.setPadding(
                normalPaddingLeft,
                normalPaddingTop,
                normalPaddingRight,
                normalPaddingBottom
            )

            /*
             * Restauramos la geometría normal.
             */
            val cardParams =
                codeCard.layoutParams
                        as LinearLayout.LayoutParams

            cardParams.height =
                LinearLayout.LayoutParams.WRAP_CONTENT

            codeCard.layoutParams =
                cardParams

            val numberParams =
                lineNumberScroll.layoutParams
                        as LinearLayout.LayoutParams

            numberParams.height =
                LinearLayout.LayoutParams.MATCH_PARENT

            lineNumberScroll.layoutParams =
                numberParams

            sourceCode.layoutParams =
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )

            sourceCode.minHeight =
                dp(220)

            window.decorView.systemUiVisibility =
                normalSystemUiVisibility

            fullscreenButton.contentDescription =
                "Pantalla completa"

            /*
             * Recuperamos el mismo ancho especial
             * que normalmente utiliza la tarjeta.
             */
            codeCard.post {

                makeCodeCardFullWidth(
                    codeCard
                )

                /*
                 * Al abandonar fullscreen recuperamos
                 * la ventana normal de exactamente
                 * 10 líneas.
                 */
                updateNormalCompilerEditorHeight()

                sourceCode.requestLayout()

                updateLineNumbers()

                updateCompilerCompactLandscapeUi()

                updateEditorHorizontalBar()

                updateEditorVerticalBar()

                updateProgrammingKeyboardBarVisibility()
            }
        }

        /*
         * Si giramos el teléfono estando en fullscreen,
         * Android conserva esta Activity.
         *
         * Cuando lessonScroll recibe sus nuevas
         * dimensiones, recalculamos la tarjeta.
         */
        lessonScroll.addOnLayoutChangeListener {
                _,
                _,
                _,
                _,
                _,
                _,
                _,
                _,
                _ ->

            if (
                compilerFullscreenActive
            ) {

                updateCompilerFullscreenGeometry()
            }
        }

        fullscreenButton.setOnClickListener {

            if (
                compilerFullscreenActive
            ) {

                exitCompilerFullscreen()

            } else {

                enterCompilerFullscreen()
            }
        }

        /*
         * REINICIAR.
         */
        resetButton.setOnClickListener {

            sourceCode.setText(
                initialCode
            )

            val cursorPosition =
                initialCode.indexOf(
                    "\n    \n"
                )

            if (
                cursorPosition >= 0
            ) {

                sourceCode.setSelection(
                    cursorPosition + 5
                )
            }

            statusText.text =
                "Listo para ejecutar."

            outputText.text =
                ""

            updateLineNumbers()
        }

        /*
 * AJUSTE DE LÍNEA DEL CURSO.
 */
        var wrapEnabled =
            false

        fun applyCourseWrap(
            enabled: Boolean
        ) {

            wrapEnabled =
                enabled

            sourceCode.setHorizontallyScrolling(
                !wrapEnabled
            )

            if (
                wrapEnabled
            ) {

                sourceCode.scrollTo(
                    0,
                    sourceCode.scrollY
                )
            }

            wrapButton.setCompoundDrawablesWithIntrinsicBounds(
                if (
                    wrapEnabled
                ) {
                    R.drawable.ic_code_wrap_on
                } else {
                    R.drawable.ic_code_wrap_off
                },
                0,
                0,
                0
            )

            wrapButton.contentDescription =
                if (
                    wrapEnabled
                ) {
                    "Ajuste de línea activado"
                } else {
                    "Ajuste de línea desactivado"
                }

            sourceCode.post {

                updateEditorHorizontalBar()

                updateLineNumbers()
            }
        }

        registerCourseWrapController { enabled ->

            applyCourseWrap(
                enabled
            )
        }

        wrapButton.setOnClickListener {

            setCurrentCourseWrap(
                !wrapEnabled
            )
        }

        /*
         * COPIAR.
         */
        copyButton.setOnClickListener {

            copyCodeToClipboard(
                sourceCode.text.toString()
            )
        }

        /*
         * COMPILAR Y EJECUTAR.
         */
        runButton.setOnClickListener {

            val source =
                sourceCode.text.toString()

            runButton.isEnabled =
                false

            statusText.text =
                "Compilando y ejecutando..."

            outputText.text =
                ""

            Thread {

                val result =
                    runCatching {

                        CppExecutionEngine(
                            this
                        ).compileAndRun(
                            source
                        )
                    }

                runOnUiThread {

                    runButton.isEnabled =
                        true

                    result.onSuccess { engineResult ->

                        val compilation =
                            engineResult.compilation

                        val execution =
                            engineResult.execution

                        statusText.text =
                            if (
                                execution == null
                            ) {

                                "La compilación falló " +
                                        "(código ${compilation.exitCode})."

                            } else {

                                "Ejecución terminada " +
                                        "(código ${execution.exitCode})."
                            }

                        outputText.text =
                            buildString {

                                append(
                                    compilation.stdout
                                )

                                append(
                                    compilation.stderr
                                )

                                if (
                                    execution != null
                                ) {

                                    append(
                                        execution.stdout
                                    )

                                    append(
                                        execution.stderr
                                    )
                                }
                            }.trimEnd()
                    }

                    result.onFailure { error ->

                        statusText.text =
                            "No se pudo ejecutar."

                        outputText.text =
                            error.stackTraceToString()
                    }
                }
            }.start()
        }
    }

    private fun normalizeAnswer(
        text: String
    ): String {

        return text
            .trim()
            .replace(
                Regex("\\s+"),
                " "
            )
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {

        /*
 * FULLSCREEN DEL COMPILADOR.
 *
 * En este modo ningún gesto pertenece
 * a la navegación entre páginas.
 *
 * El evento sigue llegando normalmente
 * al editor, barras, botones y teclado.
 */
        /*
 * Ningún fullscreen permite que un gesto
 * se convierta en navegación entre páginas.
 */
        if (
            compilerFullscreenActive ||
            exerciseFullscreenActive
        ) {

            ignoreCurrentSwipe =
                true

            return super.dispatchTouchEvent(
                event
            )
        }

        when (event.action) {

            MotionEvent.ACTION_DOWN -> {

                /*
                 * Antes de que cualquier hijo procese
                 * el gesto comprobamos si comenzó dentro
                 * de una zona que realmente puede
                 * desplazarse horizontalmente.
                 *
                 * Si es así, ese gesto pertenece al
                 * código/puzzle y nunca debe convertirse
                 * en navegación entre páginas.
                 */
                val root =
                    findViewById<View>(
                        android.R.id.content
                    )

                ignoreCurrentSwipe =
                    isTouchInsideActiveHorizontalScroll(
                        root,
                        event.rawX,
                        event.rawY
                    )

                touchStartX =
                    event.x

                touchStartY =
                    event.y
            }

            MotionEvent.ACTION_UP -> {

                if (ignoreCurrentSwipe) {
                    return super.dispatchTouchEvent(event)
                }

                if (!swipeNavigationEnabled) {

                    return super.dispatchTouchEvent(
                        event
                    )
                }

                val differenceX =
                    event.x - touchStartX

                val differenceY =
                    event.y - touchStartY

                val horizontalDistance =
                    kotlin.math.abs(differenceX)

                val verticalDistance =
                    kotlin.math.abs(differenceY)

                if (
                    horizontalDistance > 180 &&
                    horizontalDistance > verticalDistance * 1.5f
                ) {

                    if (differenceX < 0) {

                        // Deslizar hacia la izquierda:
                        // avanzar, excepto en la última página.
                        trySwipeNextPage()

                    } else {

                        // Deslizar hacia la derecha:
                        // regresar a la página anterior.
                        goPreviousPage()
                    }

                    return true
                }
            }
        }

        return super.dispatchTouchEvent(event)
    }

    private fun trySwipeNextPage() {

        /*
         * Estamos en la última página.
         *
         * Solo permitimos que el swipe
         * termine la lección si la opción
         * correspondiente está activada.
         */
        if (
            currentPage >=
            totalPages - 1
        ) {

            val settings =
                getCurrentFlowSettings()

            if (
                settings.swipeCanFinishLesson
            ) {

                tryGoNextPage()
            }

            return
        }

        if (
            completedExercises <
            requiredExercises
        ) {

            Toast.makeText(
                this,
                "Responde correctamente todos los ejercicios antes de continuar.",
                Toast.LENGTH_SHORT
            ).show()

            return
        }

        saveCurrentPageState()

        currentPage++

        saveCurrentPageProgress()

        renderCurrentPage()
    }

    private fun tryGoNextPage() {

        if (
            completedExercises <
            requiredExercises
        ) {

            Toast.makeText(
                this,
                "Responde correctamente todos los ejercicios antes de continuar.",
                Toast.LENGTH_SHORT
            ).show()

            return
        }

        saveCurrentPageState()

        /*
         * Todavía quedan páginas
         * dentro de esta misma lección.
         */
        if (
            currentPage <
            totalPages - 1
        ) {

            currentPage++

            saveCurrentPageProgress()

            renderCurrentPage()

            return
        }

        /*
         * Última página.
         *
         * Llegamos aquí mediante:
         *
         * - botón "Terminar lección"
         *
         * o
         *
         * - swipe final, si está permitido.
         */

        saveCurrentPageProgress()

        ProgressRepository.completeLesson(
            this,
            currentLessonId
        )

        val currentParts =
            currentLessonId.split(
                "__",
                limit = 3
            )

        if (
            currentParts.size != 3
        ) {

            return
        }

        val currentCourseId =
            currentParts[0]

        val currentUnitId =
            "${currentParts[0]}__${currentParts[1]}"

        val settings =
            LearningFlowSettingsRepository
                .getSettingsForCourse(
                    this,
                    currentCourseId
                )

        val nextLesson =
            NavigationRepository
                .getNextLesson(
                    this,
                    currentLessonId
                )

        /*
         * No existe ninguna lección posterior.
         */
        if (
            nextLesson == null
        ) {

            returnToCourses()

            return
        }

        /*
         * FRONTERA ENTRE CURSOS
         */
        if (
            nextLesson.courseId !=
            currentCourseId
        ) {

            if (
                settings.automaticBetweenCourses
            ) {

                openLessonInAnotherCourse(
                    nextLesson
                )

            } else {

                returnToCourses()
            }

            return
        }

        /*
         * FRONTERA ENTRE UNIDADES
         */
        if (
            nextLesson.unitId !=
            currentUnitId
        ) {

            if (
                settings.automaticBetweenUnits
            ) {

                openLessonInAnotherUnit(
                    nextLesson
                )

            } else {

                returnToCourse(
                    nextLesson
                )
            }

            return
        }

        /*
         * FRONTERA ENTRE LECCIONES
         * DE LA MISMA UNIDAD
         */
        if (
            settings.automaticBetweenLessons
        ) {

            openNextLesson(
                nextLesson
            )

        } else {

            returnToUnit(
                nextLesson
            )
        }
    }

    private fun getCurrentFlowSettings():
            LearningFlowSettings {

        val courseId =
            currentLessonId
                .substringBefore(
                    "__"
                )

        return LearningFlowSettingsRepository
            .getSettingsForCourse(
                this,
                courseId
            )
    }

    private fun openNextLesson(
        nextLesson: NavigationLesson
    ) {

        currentLessonId =
            nextLesson.lessonId

        lessonNumber =
            nextLesson.lessonNumber

        pageViewCache.clear()

        pageRequiredExercises.clear()

        pageCompletedExercises.clear()

        requiredExercises =
            0

        completedExercises =
            0

        NavigationStateRepository.saveLesson(
            this,
            currentLessonId
        )

        findViewById<TextView>(
            R.id.textTopLesson
        ).text =
            "Lección $lessonNumber"

        loadLesson(
            currentLessonId
        )
    }

    private fun returnToUnit(
        nextLesson: NavigationLesson
    ) {

        NavigationStateRepository.saveUnit(
            this,
            nextLesson.courseId,
            nextLesson.unitId
        )

        val intent =
            Intent(
                this,
                UnitActivity::class.java
            )

        intent.putExtra(
            "courseId",
            nextLesson.courseId
        )

        intent.putExtra(
            "unitId",
            nextLesson.unitId
        )

        intent.putExtra(
            "unitNumber",
            nextLesson.unitNumber
        )

        intent.putExtra(
            "unitTitle",
            nextLesson.unitTitle
        )

        intent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
        )

        startActivity(
            intent
        )
    }

    private fun openLessonInAnotherUnit(
        nextLesson: NavigationLesson
    ) {

        /*
         * Eliminamos del stack la unidad
         * anterior y la lección anterior.
         *
         * Después construimos:
         *
         * CourseActivity
         *      ↓
         * nueva UnitActivity
         *      ↓
         * nueva LessonActivity
         */

        val courseIntent =
            Intent(
                this,
                CourseActivity::class.java
            )

        courseIntent.putExtra(
            "courseId",
            nextLesson.courseId
        )

        courseIntent.putExtra(
            "courseNumber",
            nextLesson.courseNumber
        )

        courseIntent.putExtra(
            "courseTitle",
            nextLesson.courseTitle
        )

        courseIntent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
        )

        val unitIntent =
            Intent(
                this,
                UnitActivity::class.java
            )

        unitIntent.putExtra(
            "courseId",
            nextLesson.courseId
        )

        unitIntent.putExtra(
            "unitId",
            nextLesson.unitId
        )

        unitIntent.putExtra(
            "unitNumber",
            nextLesson.unitNumber
        )

        unitIntent.putExtra(
            "unitTitle",
            nextLesson.unitTitle
        )

        val lessonIntent =
            Intent(
                this,
                LessonActivity::class.java
            )

        lessonIntent.putExtra(
            "lessonId",
            nextLesson.lessonId
        )

        lessonIntent.putExtra(
            "lessonNumber",
            nextLesson.lessonNumber
        )

        startActivities(
            arrayOf(
                courseIntent,
                unitIntent,
                lessonIntent
            )
        )
    }

    private fun openLessonInAnotherCourse(
        nextLesson: NavigationLesson
    ) {

        /*
         * Al cruzar de curso reconstruimos:
         *
         * CoursesActivity
         *      ↓
         * nuevo CourseActivity
         *      ↓
         * nueva UnitActivity
         *      ↓
         * nueva LessonActivity
         */

        val coursesIntent =
            Intent(
                this,
                CoursesActivity::class.java
            )

        coursesIntent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
        )

        val courseIntent =
            Intent(
                this,
                CourseActivity::class.java
            )

        courseIntent.putExtra(
            "courseId",
            nextLesson.courseId
        )

        courseIntent.putExtra(
            "courseNumber",
            nextLesson.courseNumber
        )

        courseIntent.putExtra(
            "courseTitle",
            nextLesson.courseTitle
        )

        val unitIntent =
            Intent(
                this,
                UnitActivity::class.java
            )

        unitIntent.putExtra(
            "courseId",
            nextLesson.courseId
        )

        unitIntent.putExtra(
            "unitId",
            nextLesson.unitId
        )

        unitIntent.putExtra(
            "unitNumber",
            nextLesson.unitNumber
        )

        unitIntent.putExtra(
            "unitTitle",
            nextLesson.unitTitle
        )

        val lessonIntent =
            Intent(
                this,
                LessonActivity::class.java
            )

        lessonIntent.putExtra(
            "lessonId",
            nextLesson.lessonId
        )

        lessonIntent.putExtra(
            "lessonNumber",
            nextLesson.lessonNumber
        )

        startActivities(
            arrayOf(
                coursesIntent,
                courseIntent,
                unitIntent,
                lessonIntent
            )
        )
    }

    private fun returnToCourse(
        nextLesson: NavigationLesson
    ) {

        /*
         * La unidad acaba de terminar.
         *
         * Volvemos al nivel donde
         * el usuario elige unidades.
         */
        NavigationStateRepository.saveCourse(
            this,
            nextLesson.courseId
        )

        val intent =
            Intent(
                this,
                CourseActivity::class.java
            )

        intent.putExtra(
            "courseId",
            nextLesson.courseId
        )

        intent.putExtra(
            "courseNumber",
            nextLesson.courseNumber
        )

        intent.putExtra(
            "courseTitle",
            nextLesson.courseTitle
        )

        /*
         * CourseActivity ya existe normalmente
         * debajo de UnitActivity y LessonActivity.
         *
         * CLEAR_TOP elimina esos niveles
         * superiores y recupera ese CourseActivity.
         */
        intent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
        )

        startActivity(
            intent
        )
    }

    private fun returnToCourses() {

        /*
         * El curso acaba de terminar.
         *
         * Volvemos al nivel donde
         * el usuario elige cursos.
         */
        NavigationStateRepository.saveCourses(
            this
        )

        val intent =
            Intent(
                this,
                CoursesActivity::class.java
            )

        intent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
        )

        startActivity(
            intent
        )
    }

    private fun goPreviousPage() {

        if (currentPage > 0) {

            /*
             * Aquí también guardamos.
             *
             * Esto es lo que permite abandonar
             * una página con un ejercicio
             * parcialmente hecho sin perderlo.
             */

            saveCurrentPageState()

            currentPage--

            saveCurrentPageProgress()

            renderCurrentPage()
        }
    }

    private fun applyLessonContentTextScale(
        root: View
    ) {

        if (
            !::currentCourseId.isInitialized
        ) {
            return
        }

        val scale =
            LessonTextSizeSettingsRepository
                .getEffectiveScale(
                    this,
                    currentCourseId
                )

        /*
         * 100 % significa utilizar exactamente
         * los tamaños definidos actualmente
         * por cada elemento.
         */
        if (
            scale ==
            LessonTextSizeSettingsRepository.DEFAULT_SCALE
        ) {
            return
        }

        fun scaleView(
            view: View
        ) {

            /*
             * Button y EditText también heredan
             * de TextView, por lo que quedan
             * incluidos automáticamente.
             */
            if (
                view is TextView
            ) {

                val scaledPixels =
                    view.textSize *
                            scale /
                            100f

                view.setTextSize(
                    TypedValue.COMPLEX_UNIT_PX,
                    scaledPixels
                )
            }

            /*
             * Recorremos también todos los hijos:
             *
             * - bloques de código
             * - puzzles
             * - RadioButtons
             * - EditText
             * - compilador
             * - consola
             * - números de línea
             * - resultados
             */
            if (
                view is ViewGroup
            ) {

                for (
                index in 0 until
                        view.childCount
                ) {

                    scaleView(
                        view.getChildAt(
                            index
                        )
                    )
                }
            }
        }

        scaleView(
            root
        )
    }

    private fun refreshLessonContentTextScale() {

        if (
            !::currentCourseId.isInitialized
        ) {
            return
        }

        val newScale =
            LessonTextSizeSettingsRepository
                .getEffectiveScale(
                    this,
                    currentCourseId
                )

        val oldScale =
            appliedContentTextScale

        if (
            newScale ==
            oldScale
        ) {
            return
        }

        val ratio =
            newScale.toFloat() /
                    oldScale.toFloat()

        fun rescaleView(
            view: View
        ) {

            if (
                view is TextView
            ) {

                view.setTextSize(
                    TypedValue.COMPLEX_UNIT_PX,
                    view.textSize *
                            ratio
                )

                /*
                 * Algunos números de línea tienen
                 * una altura calculada explícitamente
                 * a partir de su fila.
                 *
                 * También ajustamos esa altura para
                 * conservar la alineación.
                 */
                val params =
                    view.layoutParams

                if (
                    params != null &&
                    params.height > 0
                ) {

                    params.height =
                        (
                                params.height *
                                        ratio
                                )
                            .toInt()
                            .coerceAtLeast(
                                1
                            )

                    view.layoutParams =
                        params
                }
            }

            if (
                view is ViewGroup
            ) {

                for (
                index in 0 until
                        view.childCount
                ) {

                    rescaleView(
                        view.getChildAt(
                            index
                        )
                    )
                }
            }
        }

        /*
         * Actualizamos todas las páginas ya
         * construidas en memoria.
         *
         * No reconstruimos los ejercicios,
         * por lo que se conserva código escrito,
         * selección, bloques, etc.
         */
        pageViewCache.values
            .forEach { pageViews ->

                pageViews.forEach { view ->

                    rescaleView(
                        view
                    )
                }
            }

        appliedContentTextScale =
            newScale

        lessonContent.requestLayout()

        lessonContent.invalidate()
    }

    private fun loadLesson(
        lessonId: String
    ) {

        courseWrapControllers.clear()

        lessonJson =
            try {

                LessonRepository.loadLessonJson(
                    this,
                    lessonId
                )

            } catch (
                exception: Exception
            ) {

                Toast.makeText(
                    this,
                    "No se encontró la lección.",
                    Toast.LENGTH_SHORT
                ).show()

                finish()

                return
            }

        ProgressRepository.markLessonStarted(
            this,
            lessonId
        )

        totalPages =
            lessonJson
                .getJSONArray(
                    "pages"
                )
                .length()

        if (
            totalPages <= 0
        ) {

            finish()

            return
        }

        currentPage =
            ProgressRepository
                .getCurrentPage(
                    this,
                    lessonId
                )
                .coerceIn(
                    0,
                    totalPages - 1
                )

        renderCurrentPage()
    }

    private fun renderCurrentPage() {

        lessonContent.removeAllViews()

        val pages =
            lessonJson.getJSONArray(
                "pages"
            )

        val textProgress =
            findViewById<TextView>(
                R.id.textProgress
            )

        val lessonProgress =
            findViewById<ProgressBar>(
                R.id.lessonProgress
            )

        val buttonContinue =
            findViewById<Button>(
                R.id.buttonContinue
            )

        /*
         * Barra superior.
         */

        textProgress.text =
            "${currentPage + 1} de $totalPages"

        lessonProgress.max =
            totalPages

        lessonProgress.progress =
            currentPage + 1

        /*
         * Texto del botón inferior.
         */

        buttonContinue.text =
            if (
                currentPage ==
                totalPages - 1
            ) {

                "Terminar lección"

            } else {

                "Continuar"
            }

        /*
         * ¿Esta página ya había sido creada?
         *
         * Si existe en caché NO volvemos a construirla.
         * Recuperamos exactamente las mismas Views.
         */

        val cachedViews =
            pageViewCache[currentPage]

        if (cachedViews != null) {

            requiredExercises =
                pageRequiredExercises[
                    currentPage
                ] ?: 0

            completedExercises =
                pageCompletedExercises[
                    currentPage
                ] ?: 0

            for (view in cachedViews) {

                /*
                 * Por seguridad eliminamos cualquier
                 * padre anterior antes de volver
                 * a colocar la View.
                 */

                val parent =
                    view.parent as? ViewGroup

                parent?.removeView(
                    view
                )

                lessonContent.addView(
                    view
                )
            }

            lessonScroll.post {

                lessonScroll.scrollTo(
                    0,
                    0
                )
            }

            AppearanceUi.applyRoute(
                this,
                AppearanceSettingsRepository.ROUTE_PROGRAMMING
            )

            return
        }

        /*
         * Página nueva.
         */

        requiredExercises =
            0

        completedExercises =
            0

        val page =
            pages.getJSONObject(
                currentPage
            )

        val blocks =
            page.getJSONArray(
                "blocks"
            )

        if (currentPage == 0) {

            addLessonTitle(
                lessonJson.getString(
                    "title"
                )
            )
        }

        for (
        i in 0 until
                blocks.length()
        ) {

            val block =
                blocks.getJSONObject(i)

            when (
                block.getString("type")
            ) {

                "inline_fill_code" -> {

                    addInlineFillCode(
                        block,
                        i
                    )
                }

                "heading" -> {

                    addHeading(
                        block.getString(
                            "text"
                        )
                    )
                }

                "paragraph" -> {

                    addParagraph(
                        block.getString(
                            "text"
                        )
                    )
                }

                "code" -> {

                    val codeHandle =
                        addCode(
                            block.getString(
                                "code"
                            ),
                            block.optString(
                                "language",
                                "cpp"
                            )
                        )

                    configureFakeCodeFullscreen(
                        codeHandle
                    )
                }

                "multiple_choice" -> {
                    addMultipleChoice(
                        block,
                        i
                    )
                }

                "note" -> {

                    addNote(
                        block.getString(
                            "text"
                        )
                    )
                }

                "fill_code" -> {
                    addFillCode(
                        block,
                        i
                    )
                }

                "output_prediction" -> {
                    addOutputPrediction(
                        block,
                        i
                    )
                }

                "order_code" -> {

                    addOrderCode(
                        block,
                        i
                    )
                }

                "drag_fill_code" -> {

                    addDragFillCode(
                        block,
                        i
                    )
                }

                "cpp_compiler" -> {

                    addCppCompiler(
                        block
                    )
                }
            }
        }

        /*
         * Aplicamos el tamaño efectivo de texto
         * únicamente después de haber construido
         * completamente una página nueva.
         *
         * Las páginas recuperadas desde caché
         * salen antes de este punto, por lo que
         * nunca se escalan dos veces.
         */
        applyLessonContentTextScale(
            lessonContent
        )

        /*
         * Guardamos cuántos ejercicios
         * tiene esta página.
         */

        pageRequiredExercises[currentPage] =
            requiredExercises

        pageCompletedExercises[currentPage] =
            completedExercises

        /*
         * Guardamos referencias a TODAS
         * las Views creadas.
         *
         * No son copias.
         * Son las mismas Views.
         */

        val views =
            mutableListOf<View>()

        for (
        i in 0 until
                lessonContent.childCount
        ) {

            views.add(
                lessonContent.getChildAt(i)
            )
        }

        pageViewCache[currentPage] =
            views

        lessonScroll.post {

            lessonScroll.scrollTo(
                0,
                0
            )
        }

        AppearanceUi.applyRoute(
            this,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING
        )
    }

    private fun createDragCodeToken(
        text: String,
        index: Int
    ): TextView {

        val token =
            TextView(this)

        token.text =
            compactCodeSpaces(
                text
            )

        token.tag =
            index

        token.typeface =
            Typeface.MONOSPACE

        token.textSize =
            16f

        token.includeFontPadding =
            false

        token.setTextColor(
            Color.WHITE
        )

        token.setPadding(
            dp(12),
            dp(10),
            dp(12),
            dp(10)
        )

        token.minWidth =
            0

        token.minHeight =
            0

        token.gravity =
            Gravity.CENTER

        val background =
            GradientDrawable()

        background.setColor(
            Color.rgb(
                30,
                33,
                39
            )
        )

        background.cornerRadius =
            dp(4).toFloat()

        background.setStroke(
            dp(1),
            /*
            Ese pertenece específicamente a los bloques del puzzle. Lo dejamos para el punto:
            10. Aplicar colores también a consolas/puzzles
             */
            AppearanceSettingsRepository
                .getAccentColor(
                    this
                )
        )

        token.background =
            background

        var downX =
            0f

        var downY =
            0f

        var dragging =
            false

        val dragThreshold =
            maxOf(
                dp(2),
                android.view.ViewConfiguration
                    .get(this)
                    .scaledTouchSlop / 2
            )

        token.setOnTouchListener { view, event ->

            when (
                event.actionMasked
            ) {

                MotionEvent.ACTION_DOWN -> {

                    ignoreCurrentSwipe =
                        true

                    downX =
                        event.x

                    downY =
                        event.y

                    dragging =
                        false

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            true
                        )

                    true
                }

                MotionEvent.ACTION_MOVE -> {

                    val distanceX =
                        kotlin.math.abs(
                            event.x - downX
                        )

                    val distanceY =
                        kotlin.math.abs(
                            event.y - downY
                        )

                    if (
                        !dragging &&
                        (
                                distanceX >= dragThreshold ||
                                        distanceY >= dragThreshold
                                )
                    ) {

                        dragging =
                            true

                        val data =
                            ClipData.newPlainText(
                                "code_fragment",
                                text
                            )

                        view.alpha =
                            0.55f

                        view.startDragAndDrop(
                            data,
                            View.DragShadowBuilder(
                                view
                            ),
                            view,
                            0
                        )
                    }

                    true
                }

                MotionEvent.ACTION_UP -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )

                    if (
                        !dragging
                    ) {

                        view.performClick()
                    }

                    true
                }

                MotionEvent.ACTION_CANCEL -> {

                    view.parent
                        ?.requestDisallowInterceptTouchEvent(
                            false
                        )

                    view.alpha =
                        1f

                    true
                }

                else -> true
            }
        }

        return token
    }

    private fun moveCodeToken(
        token: TextView,
        newParent: ViewGroup
    ) {

        val oldParent =
            token.parent as? ViewGroup

        oldParent?.removeView(
            token
        )

        /*
         * Si salió de un hueco,
         * ese hueco vuelve a ser pequeño.
         */
        if (
            oldParent is FrameLayout
        ) {

            oldParent.minimumWidth =
                0

            oldParent.minimumHeight =
                0

            val oldParams =
                oldParent.layoutParams

            oldParams.width =
                dp(18)

            oldParams.height =
                dp(22)

            oldParent.layoutParams =
                oldParams
        }

        when (newParent) {

            is FrameLayout -> {

                token.setPadding(
                    0,
                    0,
                    0,
                    0
                )

                token.minimumWidth =
                    0

                token.minimumHeight =
                    0

                val compactWidth =
                    kotlin.math.ceil(
                        token.paint.measureText(
                            token.text.toString()
                        ).toDouble()
                    ).toInt()
                        .coerceAtLeast(1)

                val metrics =
                    token.paint.fontMetricsInt

                val compactHeight =
                    (
                            metrics.bottom -
                                    metrics.top
                            ).coerceAtLeast(1)

                token.layoutParams =
                    FrameLayout.LayoutParams(
                        compactWidth,
                        compactHeight
                    ).apply {

                        gravity =
                            Gravity.CENTER
                    }

                token.requestLayout()
            }

            is LinearLayout -> {

                token.setPadding(
                    dp(12),
                    dp(10),
                    dp(12),
                    dp(10)
                )

                token.layoutParams =
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT
                    ).apply {

                        setMargins(
                            0,
                            0,
                            0,
                            0
                        )
                    }
            }
        }

        newParent.addView(
            token
        )

        /*
         * Si entró en un hueco,
         * el hueco toma EXACTAMENTE
         * el ancho y alto del bloque.
         */
        if (
            newParent is FrameLayout
        ) {

            token.post {

                newParent.minimumWidth =
                    0

                newParent.minimumHeight =
                    0

                val params =
                    newParent.layoutParams

                params.width =
                    token.width

                params.height =
                    token.height

                newParent.layoutParams =
                    params
            }
        }
    }

    private fun addDragFillCode(
        block: JSONObject,
        blockIndex: Int
    ) {

        requiredExercises++

        var answeredCorrectly =
            false

        val savedState =
            ProgressRepository.getExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex
            )

        val savedCorrect =
            savedState?.optBoolean(
                "correct",
                false
            ) ?: false

        val savedSlots =
            savedState?.optJSONArray(
                "slots"
            )

        addHeading(
            block.getString("question")
        )

        val instruction =
            TextView(this)

        instruction.text =
            "Mantén pulsado un bloque y arrástralo hacia un espacio vacío."

        instruction.setTextColor(
            Color.rgb(180, 185, 195)
        )

        instruction.textSize =
            15f

        instruction.setPadding(
            0,
            0,
            0,
            dp(14)
        )

        lessonContent.addView(
            instruction
        )

        /*
         * PALETA DE BLOQUES
         */

        val palette =
            LinearLayout(this)

        palette.orientation =
            LinearLayout.VERTICAL

        palette.setPadding(
            dp(12),
            dp(12),
            dp(12),
            dp(12)
        )

        val paletteBackground =
            GradientDrawable()

        paletteBackground.setColor(
            Color.rgb(24, 26, 31)
        )

        paletteBackground.cornerRadius =
            dp(10).toFloat()

        palette.background =
            paletteBackground

        /*
         * TARJETA DEL CÓDIGO
         */

        val codeCard =
            LinearLayout(this)

        codeCard.orientation =
            LinearLayout.VERTICAL

        val codeBackground =
            GradientDrawable()

        codeBackground.setColor(
            Color.rgb(28, 31, 36)
        )

        codeBackground.cornerRadius =
            dp(12).toFloat()

        codeCard.background =
            codeBackground

        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f

        languageLabel.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        val resetButton =
            TextView(this)

        resetButton.text =
            ""

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar ejercicio"

        resetButton.isClickable =
            true


        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )


        val actionsContainer =
            LinearLayout(this)

        actionsContainer.orientation =
            LinearLayout.HORIZONTAL

        actionsContainer.gravity =
            Gravity.CENTER_VERTICAL


        val wrapButton =
            TextView(this)

        wrapButton.text =
            ""

        wrapButton.gravity =
            Gravity.CENTER

        wrapButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        wrapButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_wrap_off,
            0,
            0,
            0
        )

        wrapButton.contentDescription =
            "Ajuste de línea desactivado"

        wrapButton.isClickable =
            true


        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar código"

        copyButton.visibility =
            View.VISIBLE

        copyButton.isClickable =
            true

        /*
 * PANTALLA COMPLETA DEL PUZZLE.
 */
        val fullscreenButton =
            TextView(this)

        fullscreenButton.gravity =
            Gravity.CENTER

        fullscreenButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        fullscreenButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_fullscreen,
            0,
            0,
            0
        )

        fullscreenButton.contentDescription =
            "Pantalla completa"

        fullscreenButton.isClickable =
            true

        /*
 * Identificador estable para poder encontrar
 * nuevamente este botón después de reconstruir
 * la página mediante Reiniciar.
 */
        fullscreenButton.tag =
            "drag_fill_fullscreen_$blockIndex"

        actionsContainer.addView(
            wrapButton
        )

        actionsContainer.addView(
            copyButton
        )

        actionsContainer.addView(
            fullscreenButton
        )

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            actionsContainer
        )

        codeCard.addView(
            header
        )

        val horizontalScroll =
            HorizontalScrollView(this)

        protectHorizontalScrollFromPageSwipe(
            horizontalScroll
        )

        horizontalScroll.clipToPadding =
            false

        horizontalScroll.isFillViewport =
            true

        horizontalScroll.isHorizontalScrollBarEnabled =
            true

        horizontalScroll.isScrollbarFadingEnabled =
            false

        val codeBody =
            LinearLayout(this)

        codeBody.orientation =
            LinearLayout.HORIZONTAL

        /*
         * COLUMNA DE NÚMEROS.
         */
        val lineNumberColumn =
            LinearLayout(this)

        lineNumberColumn.orientation =
            LinearLayout.VERTICAL

        lineNumberColumn.setBackgroundColor(
            Color.rgb(
                24,
                26,
                31
            )
        )

        lineNumberColumn.layoutParams =
            LinearLayout.LayoutParams(
                dp(18),
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        /*
         * CÓDIGO INTERACTIVO.
         */
        val codeLines =
            LinearLayout(this)

        val flowRows =
            mutableListOf<CodeFlowLayout>()

        codeLines.orientation =
            LinearLayout.VERTICAL

        codeLines.setPadding(
            dp(10),
            0,
            0,
            0
        )

        horizontalScroll.layoutParams =
            LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            )

        horizontalScroll.addView(
            codeLines
        )

        codeBody.addView(
            lineNumberColumn
        )

        codeBody.addView(
            horizontalScroll
        )

        codeCard.addView(
            codeBody
        )

        val cardParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        cardParams.setMargins(
            0,
            0,
            0,
            dp(16)
        )

        codeCard.layoutParams =
            cardParams

        lessonContent.addView(
            codeCard
        )

        lessonContent.clipToPadding =
            false

        lessonContent.clipChildren =
            false

        lessonScroll.clipToPadding =
            false

        lessonScroll.clipChildren =
            false

        codeCard.post {

            val params =
                codeCard.layoutParams
                        as LinearLayout.LayoutParams

            params.width =
                lessonContent.width

            codeCard.layoutParams =
                params

            codeCard.translationX =
                -lessonContent.paddingLeft.toFloat()
        }

        val slots =
            mutableListOf<FrameLayout>()

        /*
         * BOTÓN COMPROBAR.
         *
         * Se crea aquí para que toda la lógica del puzzle
         * pueda activarlo o desactivarlo en cuanto el usuario
         * coloque o retire un bloque.
         */
        val checkButton =
            Button(this)

        checkButton.text =
            "Comprobar"

        checkButton.isAllCaps =
            false

        checkButton.isEnabled =
            false

        fun updateDragCheckButton() {

            val hasAnyBlock =
                slots.any {
                    it.childCount > 0
                }

            checkButton.isEnabled =
                hasAnyBlock &&
                        !answeredCorrectly
        }

        fun saveDragState(
            correct: Boolean =
                answeredCorrectly
        ) {

            val slotsJson =
                org.json.JSONArray()

            for (
            slot in slots
            ) {

                if (
                    slot.childCount == 0
                ) {

                    slotsJson.put(
                        -1
                    )

                } else {

                    val token =
                        slot.getChildAt(0)
                                as TextView

                    slotsJson.put(
                        token.tag as Int
                    )
                }
            }

            val state =
                JSONObject()

            state.put(
                "slots",
                slotsJson
            )

            state.put(
                "correct",
                correct
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )
        }

        val template =
            block.getString("template")

        val lines =
            template.split("\n")

        val lineNumberViews =
            mutableListOf<TextView>()

        for (
        lineIndex in lines.indices
        ) {

            val number =
                TextView(this)

            number.text =
                "${lineIndex + 1}"

            number.typeface =
                Typeface.MONOSPACE

            number.textSize =
                16f

            number.includeFontPadding =
                false

            number.gravity =
                Gravity.TOP or
                        Gravity.END

            number.setTextColor(
                Color.rgb(
                    111,
                    119,
                    130
                )
            )

            number.setPadding(
                0,
                0,
                dp(3),
                0
            )

            number.layoutParams =
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )

            lineNumberViews.add(
                number
            )

            lineNumberColumn.addView(
                number
            )
        }

        /*
         * CONSTRUCCIÓN DE CADA LÍNEA.
         * Los espacios iniciales se conservan,
         * por lo que también se conserva la sangría.
         */

        for (
        lineIndex in lines.indices
        ) {

            val line =
                lines[lineIndex]

            val row =
                CodeFlowLayout(this)

            row.wrapEnabled =
                false

            flowRows.add(
                row
            )

            var position =
                0

            while (true) {

                val blankPosition =
                    line.indexOf(
                        "[]",
                        position
                    )

                if (
                    blankPosition == -1
                ) {

                    val remainingText =
                        line.substring(
                            position
                        )

                    if (
                        remainingText.isNotEmpty()
                    ) {

                        val text =
                            createInlineCodeText(
                                remainingText
                            )

                        row.addView(
                            text
                        )
                    }

                    break
                }

                val before =
                    line.substring(
                        position,
                        blankPosition
                    )

                if (
                    before.isNotEmpty()
                ) {

                    val text =
                        createInlineCodeText(
                            before
                        )

                    row.addView(
                        text
                    )
                }

                /*
                 * HUECO
                 */

                val slot =
                    FrameLayout(this)

                slot.minimumWidth =
                    0

                slot.minimumHeight =
                    0

                slot.setPadding(
                    0,
                    0,
                    0,
                    0
                )

                val slotBackground =
                    GradientDrawable()

                slotBackground.setColor(
                    Color.TRANSPARENT
                )

                slotBackground.cornerRadius =
                    dp(3).toFloat()

                slotBackground.setStroke(
                    dp(1),
                    Color.rgb(
                        120,
                        125,
                        140
                    )
                )

                slot.background =
                    slotBackground

                val slotParams =
                    ViewGroup.MarginLayoutParams(
                        dp(18),
                        dp(22)
                    )

                slotParams.leftMargin =
                    0

                slotParams.topMargin =
                    0

                slotParams.rightMargin =
                    0

                slotParams.bottomMargin =
                    0

                slot.layoutParams =
                    slotParams

                slots.add(
                    slot
                )

                row.addView(
                    slot
                )

                position =
                    blankPosition + 2
            }

            /*
             * Una línea vacía todavía conserva altura.
             */

            if (
                row.childCount == 0
            ) {

                val empty =
                    TextView(this)

                empty.text =
                    " "

                empty.textSize =
                    16f

                row.addView(
                    empty
                )
            }

            codeLines.addView(
                row
            )

            val numberView =
                lineNumberViews[
                    lineIndex
                ]

            row.addOnLayoutChangeListener {
                    _,
                    _,
                    _,
                    _,
                    _,
                    _,
                    _,
                    _,
                    _ ->

                if (
                    row.height <= 0
                ) {
                    return@addOnLayoutChangeListener
                }

                val params =
                    numberView.layoutParams

                if (
                    params.height !=
                    row.height
                ) {

                    params.height =
                        row.height

                    numberView.layoutParams =
                        params
                }
            }
        }

        var expandedDragSlot: FrameLayout? =
            null

        fun getCompactSlotSize(
            slot: FrameLayout
        ): Pair<Int, Int> {

            if (
                slot.childCount > 0
            ) {

                val token =
                    slot.getChildAt(0) as? TextView

                if (
                    token != null
                ) {

                    val width =
                        kotlin.math.ceil(
                            token.paint.measureText(
                                token.text.toString()
                            ).toDouble()
                        ).toInt()
                            .coerceAtLeast(1)

                    val metrics =
                        token.paint.fontMetricsInt

                    val height =
                        (
                                metrics.bottom -
                                        metrics.top
                                ).coerceAtLeast(1)

                    return Pair(
                        width,
                        height
                    )
                }
            }

            return Pair(
                dp(18),
                dp(22)
            )
        }

        fun compactDragSlot(
            slot: FrameLayout
        ) {

            val size =
                getCompactSlotSize(
                    slot
                )

            slot.minimumWidth =
                0

            slot.minimumHeight =
                0

            val params =
                slot.layoutParams

            params.width =
                size.first

            params.height =
                size.second

            slot.layoutParams =
                params
        }

        fun expandDragSlot(
            slot: FrameLayout
        ) {

            val compactSize =
                getCompactSlotSize(
                    slot
                )

            val params =
                slot.layoutParams

            params.width =
                maxOf(
                    compactSize.first +
                            dp(24),
                    dp(46)
                )

            params.height =
                maxOf(
                    compactSize.second +
                            dp(18),
                    dp(42)
                )

            slot.layoutParams =
                params
        }

        fun findNearestDragSlot(
            eventX: Float,
            eventY: Float
        ): FrameLayout? {

            val codeLocation =
                IntArray(2)

            codeLines.getLocationOnScreen(
                codeLocation
            )

            val fingerX =
                codeLocation[0] +
                        eventX

            val fingerY =
                codeLocation[1] +
                        eventY

            var nearestSlot: FrameLayout? =
                null

            var nearestDistanceSquared =
                Float.MAX_VALUE

            for (
            slot in slots
            ) {

                val slotLocation =
                    IntArray(2)

                slot.getLocationOnScreen(
                    slotLocation
                )

                val centerX =
                    slotLocation[0] +
                            slot.width / 2f

                val centerY =
                    slotLocation[1] +
                            slot.height / 2f

                val differenceX =
                    fingerX -
                            centerX

                val differenceY =
                    fingerY -
                            centerY

                val distanceSquared =
                    differenceX *
                            differenceX +
                            differenceY *
                            differenceY

                if (
                    distanceSquared <
                    nearestDistanceSquared
                ) {

                    nearestDistanceSquared =
                        distanceSquared

                    nearestSlot =
                        slot
                }
            }

            val maximumDistance =
                dp(52).toFloat()

            return if (
                nearestDistanceSquared <=
                maximumDistance *
                maximumDistance
            ) {

                nearestSlot

            } else {

                null
            }
        }

        codeLines.setOnDragListener { _, event ->

            val token =
                event.localState as? TextView
                    ?: return@setOnDragListener false

            when (
                event.action
            ) {

                DragEvent.ACTION_DRAG_STARTED -> {

                    true
                }

                DragEvent.ACTION_DRAG_LOCATION -> {

                    val nearestSlot =
                        findNearestDragSlot(
                            event.x,
                            event.y
                        )

                    if (
                        nearestSlot !==
                        expandedDragSlot
                    ) {

                        expandedDragSlot?.let {
                            compactDragSlot(
                                it
                            )
                        }

                        expandedDragSlot =
                            nearestSlot

                        nearestSlot?.let {
                            expandDragSlot(
                                it
                            )
                        }
                    }

                    true
                }

                DragEvent.ACTION_DROP -> {

                    val destination =
                        findNearestDragSlot(
                            event.x,
                            event.y
                        )

                    if (
                        destination != null
                    ) {

                        val sourceParent =
                            token.parent
                                    as? ViewGroup

                        if (
                            sourceParent !==
                            destination
                        ) {

                            val existing =
                                if (
                                    destination.childCount >
                                    0
                                ) {

                                    destination
                                        .getChildAt(0)
                                            as? TextView

                                } else {

                                    null
                                }

                            if (
                                existing != null &&
                                sourceParent != null
                            ) {

                                destination.removeView(
                                    existing
                                )

                                moveCodeToken(
                                    existing,
                                    sourceParent
                                )
                            }

                            moveCodeToken(
                                token,
                                destination
                            )

                            saveDragState()

                            updateDragCheckButton()

                            destination.post {

                                compactDragSlot(
                                    destination
                                )
                            }

                            if (
                                sourceParent is FrameLayout
                            ) {

                                sourceParent.post {

                                    compactDragSlot(
                                        sourceParent
                                    )
                                }
                            }
                        }
                    }

                    expandedDragSlot?.let {

                        it.post {

                            compactDragSlot(
                                it
                            )
                        }
                    }

                    expandedDragSlot =
                        null

                    true
                }

                DragEvent.ACTION_DRAG_ENDED -> {

                    token.alpha =
                        1f

                    expandedDragSlot?.let {

                        it.post {

                            compactDragSlot(
                                it
                            )
                        }
                    }

                    expandedDragSlot =
                        null

                    true
                }

                else -> true
            }
        }

        copyButton.setOnClickListener {

            copyCodeToClipboard(
                buildDragFilledCode(
                    template,
                    slots
                )
            )
        }

        var wrapEnabled =
            false

        /*
         * El ancho de horizontalScroll puede cambiar
         * durante la construcción inicial de la página.
         *
         * Si el ajuste de línea ya está activado,
         * recalculamos su ancho cuando el viewport
         * alcanza un tamaño diferente.
         *
         * Así el ajuste restaurado/global y el botón
         * manual utilizan exactamente la misma medida.
         */
        horizontalScroll.addOnLayoutChangeListener {
                _,
                left,
                _,
                right,
                _,
                oldLeft,
                _,
                oldRight,
                _ ->

            val newWidth =
                right -
                        left

            val oldWidth =
                oldRight -
                        oldLeft

            if (
                wrapEnabled &&
                newWidth > 0 &&
                newWidth != oldWidth
            ) {

                horizontalScroll.post {

                    if (
                        wrapEnabled
                    ) {

                        setPuzzleWrapMode(
                            true,
                            horizontalScroll,
                            codeLines,
                            flowRows,
                            wrapButton
                        )
                    }
                }
            }
        }

        registerCourseWrapController { enabled ->

            wrapEnabled =
                enabled

            setPuzzleWrapMode(
                wrapEnabled,
                horizontalScroll,
                codeLines,
                flowRows,
                wrapButton
            )
        }

        wrapButton.setOnClickListener {

            setCurrentCourseWrap(
                !wrapEnabled
            )
        }

        /*
         * BLOQUES DISPONIBLES
         */

        val choices =
            block.getJSONArray(
                "choices"
            )

        val tokens =
            mutableListOf<TextView>()

        for (
        i in 0 until
                choices.length()
        ) {

            val token =
                createDragCodeToken(
                    choices.getString(i),
                    i
                )

            tokens.add(
                token
            )

            moveCodeToken(
                token,
                palette
            )

            /*
             * TOQUE SIMPLE
             *
             * Si el bloque está en la paleta,
             * lo colocamos automáticamente en
             * el primer hueco disponible.
             *
             * Los slots ya están almacenados
             * de izquierda a derecha y después
             * de arriba hacia abajo.
             */

            token.setOnClickListener {

                val currentParent =
                    token.parent

                if (
                    currentParent === palette
                ) {

                    val firstEmptySlot =
                        slots.firstOrNull {
                            it.childCount == 0
                        }

                    if (
                        firstEmptySlot != null
                    ) {

                        moveViewAnimated(
                            token,
                            firstEmptySlot
                        )

                        firstEmptySlot.postDelayed(
                            {

                                saveDragState()

                                updateDragCheckButton()
                            },
                            350L
                        )
                    }

                } else {

                    /*
                     * Si tocamos un bloque que
                     * ya está dentro del código,
                     * vuelve a la paleta.
                     */

                    moveViewAnimated(
                        token,
                        palette
                    )

                    palette.postDelayed(
                        {

                            saveDragState()

                            updateDragCheckButton()
                        },
                        350L
                    )
                }
            }
        }

        if (
            savedSlots != null
        ) {

            val amount =
                minOf(
                    savedSlots.length(),
                    slots.size
                )

            /*
             * Primero restauramos únicamente
             * la posición lógica de cada bloque.
             */
            for (
            slotIndex in 0 until
                    amount
            ) {

                val tokenIndex =
                    savedSlots.optInt(
                        slotIndex,
                        -1
                    )

                if (
                    tokenIndex >= 0
                ) {

                    val token =
                        tokens.firstOrNull {

                            (it.tag as Int) ==
                                    tokenIndex
                        }

                    if (
                        token != null
                    ) {

                        moveCodeToken(
                            token,
                            slots[slotIndex]
                        )
                    }
                }
            }

            /*
             * moveCodeToken() realiza parte de su
             * ajuste mediante post.
             *
             * Además, al abrir nuevamente la aplicación,
             * el tamaño de texto y el ajuste de línea
             * todavía están terminando su primera
             * reconstrucción.
             *
             * Esperamos dos ciclos de la interfaz para
             * que todo eso termine y DESPUÉS dejamos
             * los bloques exactamente compactos.
             */
            codeLines.post {

                codeLines.post {

                    for (
                    slot in slots
                    ) {

                        val token =
                            if (
                                slot.childCount > 0
                            ) {

                                slot.getChildAt(0)
                                        as? TextView

                            } else {

                                null
                            }

                        if (
                            token != null
                        ) {

                            token.setPadding(
                                0,
                                0,
                                0,
                                0
                            )

                            token.minimumWidth =
                                0

                            token.minimumHeight =
                                0

                            val compactWidth =
                                kotlin.math.ceil(
                                    token.paint.measureText(
                                        token.text.toString()
                                    ).toDouble()
                                ).toInt()
                                    .coerceAtLeast(
                                        1
                                    )

                            val metrics =
                                token.paint.fontMetricsInt

                            val compactHeight =
                                (
                                        metrics.bottom -
                                                metrics.top
                                        ).coerceAtLeast(
                                        1
                                    )

                            token.layoutParams =
                                FrameLayout.LayoutParams(
                                    compactWidth,
                                    compactHeight
                                ).apply {

                                    gravity =
                                        Gravity.CENTER
                                }

                            token.requestLayout()

                            compactDragSlot(
                                slot
                            )

                            slot.requestLayout()
                        }
                    }

                    for (
                    row in flowRows
                    ) {

                        row.requestLayout()
                    }

                    codeLines.requestLayout()

                    horizontalScroll.requestLayout()

                    codeCard.requestLayout()
                }
            }
        }

        /*
         * También podemos devolver un bloque
         * desde un hueco hacia la paleta.
         */

        palette.setOnDragListener { _, event ->

            val token =
                event.localState as? TextView
                    ?: return@setOnDragListener false

            when (event.action) {

                DragEvent.ACTION_DRAG_STARTED -> {
                    true
                }

                DragEvent.ACTION_DROP -> {

                    if (
                        token.parent !== palette
                    ) {

                        moveCodeToken(
                            token,
                            palette
                        )

                        saveDragState()

                        updateDragCheckButton()
                    }

                    true
                }

                DragEvent.ACTION_DRAG_ENDED -> {

                    token.alpha =
                        1f

                    true
                }

                else -> true
            }
        }

        val paletteParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        paletteParams.setMargins(
            0,
            0,
            0,
            dp(16)
        )

        palette.layoutParams =
            paletteParams

        lessonContent.addView(
            palette
        )

        /*
 * FULLSCREEN INTERACTIVO DEL PUZZLE.
 *
 * Se mueven las Views REALES:
 *
 * - cabecera
 * - código interactivo
 * - paleta de bloques
 *
 * El botón Comprobar permanece fuera.
 */
        fullscreenButton.setOnClickListener {

            /*
             * SALIR.
             */
            if (
                exerciseFullscreenActive
            ) {

                /*
                 * En vista normal la paleta recupera
                 * su disposición vertical.
                 */
                palette.orientation =
                    LinearLayout.VERTICAL

                closeExerciseFullscreenHost()

                fullscreenButton.contentDescription =
                    "Pantalla completa"

                return@setOnClickListener
            }

            /*
             * ENTRAR.
             *
             * Este puzzle utiliza bloques,
             * no teclado.
             */
            openExerciseFullscreenHost(
                usesKeyboard = false
            ) {
                    headerHost,
                    bodyHost,
                    floatingHost ->

                /*
                 * CABECERA FIJA.
                 */
                /*
 * Primero retiramos codeBody.
 *
 * Así conserva su índice original 1
 * dentro de codeCard.
 */
                /*
 * SCROLL VERTICAL DEL CÓDIGO.
 *
 * Debe existir ANTES de intentar
 * mover codeBody dentro de él.
 */
                val verticalCodeScroll =
                    ScrollView(this)

                verticalCodeScroll.isFillViewport =
                    true

                verticalCodeScroll.isVerticalScrollBarEnabled =
                    true

                verticalCodeScroll.isScrollbarFadingEnabled =
                    false

                verticalCodeScroll.layoutParams =
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        0,
                        1f
                    )

                bodyHost.addView(
                    verticalCodeScroll
                )

                /*
                 * IMPORTANTE:
                 *
                 * codeBody originalmente ocupa el índice 1
                 * dentro de codeCard.
                 *
                 * Lo retiramos PRIMERO para conservar
                 * ese índice original correctamente.
                 */
                moveViewToExerciseFullscreen(
                    codeBody,
                    verticalCodeScroll,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                /*
                 * Después retiramos header,
                 * que originalmente ocupa el índice 0.
                 *
                 * Visualmente seguirá apareciendo arriba
                 * porque lo colocamos en headerHost.
                 */
                moveViewToExerciseFullscreen(
                    header,
                    headerHost,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                /*
                 * BANDEJA FLOTANTE INFERIOR.
                 */
                floatingHost.visibility =
                    View.VISIBLE

                floatingHost.setBackgroundColor(
                    Color.rgb(
                        24,
                        26,
                        31
                    )
                )

                /*
                 * La bandeja puede desplazarse
                 * horizontalmente si existen
                 * muchos bloques.
                 */
                val paletteScroll =
                    HorizontalScrollView(this)

                paletteScroll.isFillViewport =
                    false

                paletteScroll.isHorizontalScrollBarEnabled =
                    true

                paletteScroll.isScrollbarFadingEnabled =
                    false

                paletteScroll.layoutParams =
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )

                floatingHost.addView(
                    paletteScroll
                )

                /*
                 * En fullscreen los bloques forman
                 * una sola banda horizontal.
                 *
                 * Sigue siendo la MISMA palette,
                 * por lo que conserva sus listeners
                 * de drag/drop.
                 */
                palette.orientation =
                    LinearLayout.HORIZONTAL

                moveViewToExerciseFullscreen(
                    palette,
                    paletteScroll,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.WRAP_CONTENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                fullscreenButton.contentDescription =
                    "Salir de pantalla completa"
            }
        }

        /*
         * COMPROBAR
         */

        /*
         * Si restauramos una página parcialmente resuelta,
         * el botón debe recuperar inmediatamente su estado.
         */
        updateDragCheckButton()

        lessonContent.addView(
            checkButton
        )

        val result =
            TextView(this)

        result.textSize =
            16f

        result.setPadding(
            0,
            dp(12),
            0,
            dp(24)
        )

        lessonContent.addView(
            result
        )

        resetButton.setOnClickListener {

            /*
             * Recordamos si estábamos trabajando
             * en fullscreen.
             *
             * Reiniciar reconstruye este ejercicio,
             * por lo que primero debemos retirar
             * correctamente las Views viejas.
             */
            val reopenFullscreen =
                exerciseFullscreenActive

            if (
                reopenFullscreen
            ) {

                closeExerciseFullscreenHost()
            }

            val emptySlots =
                org.json.JSONArray()

            for (
            i in slots.indices
            ) {

                emptySlots.put(
                    -1
                )
            }

            val resetState =
                JSONObject()

            resetState.put(
                "slots",
                emptySlots
            )

            resetState.put(
                "correct",
                false
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                resetState
            )

            pageViewCache.remove(
                currentPage
            )

            pageRequiredExercises.remove(
                currentPage
            )

            pageCompletedExercises.remove(
                currentPage
            )

            renderCurrentPage()

            /*
             * Si Reiniciar se pulsó desde fullscreen,
             * la página nueva vuelve a abrir
             * automáticamente el mismo puzzle.
             */
            if (
                reopenFullscreen
            ) {

                lessonContent.post {

                    val newFullscreenButton =
                        lessonContent.findViewWithTag<View>(
                            "drag_fill_fullscreen_$blockIndex"
                        )

                    newFullscreenButton
                        ?.performClick()
                }
            }
        }

        if (
            savedCorrect
        ) {

            answeredCorrectly =
                true

            completedExercises +=
                1

            checkButton.isEnabled =
                false

            checkButton.text =
                "Correcto ✓"

            result.text =
                "✓ Correcto"

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )

            for (
            token in tokens
            ) {

                token.setOnTouchListener(
                    null
                )

                token.setOnClickListener(
                    null
                )
            }
        }

        checkButton.setOnClickListener {

            val correctOrder =
                block.getJSONArray(
                    "correctOrder"
                )

            /*
             * Primero verificamos que todos
             * los huecos estén ocupados.
             */

            for (slot in slots) {

                if (
                    slot.childCount == 0
                ) {

                    Toast.makeText(
                        this,
                        "Completa todos los espacios primero.",
                        Toast.LENGTH_SHORT
                    ).show()

                    return@setOnClickListener
                }
            }

            var correct =
                true

            for (
            i in slots.indices
            ) {

                val token =
                    slots[i]
                        .getChildAt(0)
                            as TextView

                val actualText =
                    token.text
                        .toString()

                val expectedChoiceIndex =
                    correctOrder.getInt(i)

                val expectedText =
                    choices.getString(
                        expectedChoiceIndex
                    )

                if (
                    actualText !=
                    expectedText
                ) {

                    correct = false
                    break
                }
            }

            saveDragState(
                correct
            )

            if (correct) {

                result.text =
                    "✓ Correcto"

                result.setTextColor(
                    Color.rgb(
                        180,
                        220,
                        190
                    )
                )

                copyButton.visibility =
                    View.VISIBLE

                if (
                    !answeredCorrectly
                ) {

                    answeredCorrectly =
                        true

                    markExerciseCompleted()

                    checkButton.isEnabled =
                        false

                    checkButton.text =
                        "Correcto ✓"

                    for (
                    token in tokens
                    ) {

                        token.setOnTouchListener(
                            null
                        )

                        token.setOnClickListener(
                            null
                        )
                    }
                }

            } else {

                result.text =
                    "✗ Algunos bloques todavía no están en el lugar correcto."

                result.setTextColor(
                    Color.rgb(
                        240,
                        150,
                        150
                    )
                )
            }
        }
    }

    private fun addLessonTitle(
        text: String
    ) {

        val view =
            TextView(this)

        view.text =
            text

        view.setTextColor(
            Color.WHITE
        )

        view.textSize =
            32f

        view.setTypeface(
            null,
            Typeface.BOLD
        )

        view.setPadding(
            0,
            0,
            0,
            dp(28)
        )

        lessonContent.addView(
            view
        )
    }

    private fun addHeading(
        text: String
    ) {

        val view =
            TextView(this)

        view.text =
            text

        view.setTextColor(
            Color.WHITE
        )

        view.textSize =
            23f

        view.setTypeface(
            null,
            Typeface.BOLD
        )

        view.setPadding(
            0,
            dp(18),
            0,
            dp(10)
        )

        lessonContent.addView(
            view
        )
    }

    private fun addParagraph(
        text: String
    ) {

        val view =
            TextView(this)

        view.text =
            text

        view.setTextColor(
            Color.rgb(
                205,
                208,
                215
            )
        )

        view.textSize =
            18f

        view.setLineSpacing(
            0f,
            1.25f
        )

        view.setPadding(
            0,
            0,
            0,
            dp(18)
        )

        lessonContent.addView(
            view
        )
    }

    private fun makeCodeCardFullWidth(
        card: View
    ) {

        lessonContent.clipToPadding =
            false

        lessonContent.clipChildren =
            false

        lessonScroll.clipToPadding =
            false

        lessonScroll.clipChildren =
            false

        card.post {

            val params =
                card.layoutParams
                        as? LinearLayout.LayoutParams
                    ?: return@post

            params.width =
                lessonContent.width

            card.layoutParams =
                params

            card.translationX =
                -lessonContent.paddingLeft.toFloat()
        }
    }

    /*
 * Mueve una View REAL al fullscreen.
 *
 * No hacemos copias.
 *
 * De esta manera se conserva automáticamente:
 *
 * - texto escrito
 * - selección
 * - posición de bloques
 * - estado de puzzles
 * - listeners
 * - animaciones
 * - respuestas parciales
 */
    private fun moveViewToExerciseFullscreen(
        view: View,
        destination: ViewGroup,
        fullscreenLayoutParams: ViewGroup.LayoutParams
    ) {

        val originalParent =
            view.parent as? ViewGroup
                ?: return

        val originalIndex =
            originalParent.indexOfChild(
                view
            )

        val originalLayoutParams =
            view.layoutParams

        val originalVisibility =
            view.visibility

        exerciseFullscreenMovedViews.add(
            ExerciseFullscreenViewState(
                view = view,
                originalParent = originalParent,
                originalIndex = originalIndex,
                originalLayoutParams = originalLayoutParams,
                originalVisibility = originalVisibility
            )
        )

        originalParent.removeView(
            view
        )

        view.layoutParams =
            fullscreenLayoutParams

        view.visibility =
            View.VISIBLE

        destination.addView(
            view
        )
    }

    /*
 * CREA EL HOST FULLSCREEN DE UN EJERCICIO.
 *
 * El host tiene tres zonas:
 *
 * 1. headerHost
 *      cabecera fija
 *
 * 2. bodyHost
 *      código + interacción principal
 *
 * 3. floatingHost
 *      zona flotante inferior opcional
 *
 * floatingHost servirá posteriormente para
 * la bandeja horizontal de bloques.
 */
    private fun openExerciseFullscreenHost(
        usesKeyboard: Boolean,
        onReady: (
            headerHost: FrameLayout,
            bodyHost: LinearLayout,
            floatingHost: FrameLayout
        ) -> Unit
    ) {

        if (
            exerciseFullscreenActive ||
            compilerFullscreenActive
        ) {
            return
        }

        exerciseFullscreenActive =
            true

        exerciseFullscreenUsesKeyboard =
            usesKeyboard

        exerciseFullscreenMovedViews.clear()

        exerciseFullscreenSystemUiVisibility =
            window.decorView.systemUiVisibility

        val activityRoot =
            findViewById<ViewGroup>(
                android.R.id.content
            )

        /*
         * OVERLAY GENERAL.
         */
        val overlay =
            FrameLayout(this)

        overlay.setBackgroundColor(
            Color.rgb(
                18,
                20,
                24
            )
        )

        overlay.layoutParams =
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )

        /*
         * Impide que cualquier gesto llegue
         * accidentalmente a la lección que
         * permanece debajo.
         */
        overlay.isClickable =
            true

        overlay.isFocusable =
            true

        exerciseFullscreenOverlay =
            overlay

        /*
         * ESTRUCTURA VERTICAL PRINCIPAL.
         */
        val fullscreenColumn =
            LinearLayout(this)

        fullscreenColumn.orientation =
            LinearLayout.VERTICAL

        fullscreenColumn.layoutParams =
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )

        /*
         * CABECERA FIJA.
         */
        val headerHost =
            FrameLayout(this)

        headerHost.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        /*
         * CUERPO.
         *
         * Recibe todo el espacio restante.
         */
        val bodyHost =
            LinearLayout(this)

        bodyHost.orientation =
            LinearLayout.VERTICAL

        bodyHost.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0,
                1f
            )

        bodyHost.clipChildren =
            false

        bodyHost.clipToPadding =
            false

        /*
         * BANDEJA FLOTANTE INFERIOR.
         *
         * Comienza oculta.
         *
         * Más adelante aquí vivirán los bloques
         * disponibles de drag-fill / inline-fill.
         */
        val floatingHost =
            FrameLayout(this)

        floatingHost.visibility =
            View.GONE

        floatingHost.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        fullscreenColumn.addView(
            headerHost
        )

        fullscreenColumn.addView(
            bodyHost
        )

        fullscreenColumn.addView(
            floatingHost
        )

        overlay.addView(
            fullscreenColumn
        )

        activityRoot.addView(
            overlay
        )

        /*
         * Pantalla realmente completa.
         */
        window.decorView.systemUiVisibility =
            exerciseFullscreenSystemUiVisibility or
                    View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

        /*
         * El ejercicio concreto decide qué Views
         * reales colocar en cada zona.
         */
        onReady(
            headerHost,
            bodyHost,
            floatingHost
        )

        updateProgrammingKeyboardBarVisibility()
    }

    /*
 * CIERRA EL FULLSCREEN DE EJERCICIO.
 *
 * Cada View vuelve exactamente al mismo:
 *
 * - padre
 * - índice
 * - LayoutParams
 * - estado de visibilidad
 *
 * No reconstruimos el ejercicio.
 */
    private fun closeExerciseFullscreenHost() {

        if (
            !exerciseFullscreenActive
        ) {
            return
        }

        /*
         * Primero retiramos las Views
         * de sus padres fullscreen.
         */
        for (
        state in exerciseFullscreenMovedViews
        ) {

            val currentParent =
                state.view.parent
                        as? ViewGroup

            currentParent?.removeView(
                state.view
            )
        }

        /*
         * Restauramos por índice ascendente.
         *
         * Esto conserva el orden original
         * incluso si varias Views pertenecían
         * al mismo padre.
         */
        exerciseFullscreenMovedViews
            .sortedWith(
                compareBy<ExerciseFullscreenViewState> {

                    System.identityHashCode(
                        it.originalParent
                    )
                }
                    .thenBy {
                        it.originalIndex
                    }
            )
            .forEach { state ->

                state.view.layoutParams =
                    state.originalLayoutParams

                state.view.visibility =
                    state.originalVisibility

                val safeIndex =
                    state.originalIndex.coerceIn(
                        0,
                        state.originalParent.childCount
                    )

                state.originalParent.addView(
                    state.view,
                    safeIndex
                )
            }

        exerciseFullscreenMovedViews.clear()

        exerciseFullscreenOverlay
            ?.let { overlay ->

                val parent =
                    overlay.parent
                            as? ViewGroup

                parent?.removeView(
                    overlay
                )
            }

        exerciseFullscreenOverlay =
            null

        exerciseFullscreenActive =
            false

        exerciseFullscreenUsesKeyboard =
            false

        window.decorView.systemUiVisibility =
            exerciseFullscreenSystemUiVisibility

        updateProgrammingKeyboardBarVisibility()
    }

    private fun configureFakeCodeFullscreen(
        codeHandle: FakeCodeBlockHandle,
        responseView: View? = null
    ) {

        codeHandle.fullscreenButton
            .setOnClickListener {

                if (
                    exerciseFullscreenActive
                ) {

                    closeExerciseFullscreenHost()

                    codeHandle.fullscreenButton
                        .contentDescription =
                        "Pantalla completa"

                    return@setOnClickListener
                }

                openExerciseFullscreenHost(
                    usesKeyboard =
                        responseView != null
                ) {
                        headerHost,
                        bodyHost,
                        _ ->

                    val fullscreenScroll =
                        ScrollView(this)

                    fullscreenScroll.isFillViewport =
                        true

                    fullscreenScroll.layoutParams =
                        LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            0,
                            1f
                        )

                    val fullscreenContent =
                        LinearLayout(this)

                    fullscreenContent.orientation =
                        LinearLayout.VERTICAL

                    fullscreenContent.layoutParams =
                        FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.WRAP_CONTENT
                        )

                    fullscreenScroll.addView(
                        fullscreenContent
                    )

                    bodyHost.addView(
                        fullscreenScroll
                    )

                    /*
                     * Zona real del código.
                     */
                    val fullscreenCodeSection =
                        LinearLayout(this)

                    fullscreenCodeSection.orientation =
                        LinearLayout.VERTICAL

                    fullscreenCodeSection.setBackgroundColor(
                        Color.rgb(
                            28,
                            31,
                            36
                        )
                    )

                    fullscreenCodeSection.layoutParams =
                        LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            LinearLayout.LayoutParams.WRAP_CONTENT
                        )

                    fullscreenContent.addView(
                        fullscreenCodeSection
                    )

                    /*
                     * Movemos primero el cuerpo.
                     */
                    moveViewToExerciseFullscreen(
                        codeHandle.codeBody,
                        fullscreenCodeSection,
                        LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            LinearLayout.LayoutParams.WRAP_CONTENT
                        )
                    )

                    /*
                     * La cabecera permanece fija.
                     */
                    moveViewToExerciseFullscreen(
                        codeHandle.header,
                        headerHost,
                        FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.WRAP_CONTENT
                        )
                    )

                    /*
                     * Algunos ejercicios también
                     * llevan respuesta.
                     */
                    if (
                        responseView != null
                    ) {

                        moveViewToExerciseFullscreen(
                            responseView,
                            fullscreenContent,
                            LinearLayout.LayoutParams(
                                LinearLayout.LayoutParams.MATCH_PARENT,
                                LinearLayout.LayoutParams.WRAP_CONTENT
                            ).apply {

                                setMargins(
                                    dp(12),
                                    dp(12),
                                    dp(12),
                                    dp(12)
                                )
                            }
                        )
                    }

                    codeHandle.fullscreenButton
                        .contentDescription =
                        "Salir de pantalla completa"
                }
            }
    }

    private fun addCode(
        code: String,
        language: String,
        showReset: Boolean = false
    ): FakeCodeBlockHandle {

        val card =
            LinearLayout(this)

        card.orientation =
            LinearLayout.VERTICAL

        val cardBackground =
            GradientDrawable()

        cardBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        cardBackground.cornerRadius =
            dp(12).toFloat()

        card.background =
            cardBackground

        val cardParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        cardParams.setMargins(
            0,
            dp(8),
            0,
            dp(24)
        )

        card.layoutParams =
            cardParams

        /*
         * CABECERA DEL BLOQUE DE CÓDIGO
         */

        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(10),
            dp(8),
            dp(10)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        val languageLabel =
            TextView(this)

        val readableLanguage =
            when (
                language.lowercase()
            ) {

                "cpp",
                "c++" -> "C++"

                else ->
                    language.uppercase()
            }

        languageLabel.text =
            "</>  $readableLanguage"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.textSize =
            15f

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        val spacer =
            View(this)

        var resetButton: TextView? =
            null

        if (showReset) {

            resetButton =
                TextView(this)

            resetButton.text =
                ""

            resetButton.gravity =
                Gravity.CENTER

            resetButton.setPadding(
                dp(10),
                dp(8),
                dp(10),
                dp(8)
            )

            resetButton.setCompoundDrawablesWithIntrinsicBounds(
                R.drawable.ic_code_reset,
                0,
                0,
                0
            )

            resetButton.contentDescription =
                "Reiniciar ejercicio"

            resetButton.isEnabled =
                false

            resetButton.alpha =
                0.35f
        }

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )

        /*
         * BOTÓN AJUSTAR LÍNEA
         */

        val wrapButton =
            TextView(this)

        wrapButton.text =
            ""

        wrapButton.gravity =
            Gravity.CENTER

        wrapButton.isClickable =
            true

        wrapButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        wrapButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_wrap_off,
            0,
            0,
            0
        )

        wrapButton.contentDescription =
            "Ajuste de línea desactivado"

        /*
         * BOTÓN COPIAR
         */

        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.isClickable =
            true

        /*
 * PANTALLA COMPLETA.
 */
        val fullscreenButton =
            TextView(this)

        fullscreenButton.gravity =
            Gravity.CENTER

        fullscreenButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        fullscreenButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_fullscreen,
            0,
            0,
            0
        )

        fullscreenButton.contentDescription =
            "Pantalla completa"

        fullscreenButton.isClickable =
            true

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar código"

        header.addView(
            languageLabel
        )

        if (
            resetButton != null
        ) {

            header.addView(
                resetButton
            )
        }

        header.addView(
            spacer
        )

        header.addView(
            wrapButton
        )

        header.addView(
            copyButton
        )

        header.addView(
            fullscreenButton
        )

        card.addView(
            header
        )

        /*
         * ÁREA DESPLAZABLE DEL CÓDIGO
         */

        val codeScroll =
            HorizontalScrollView(this)

        protectHorizontalScrollFromPageSwipe(
            codeScroll
        )

        codeScroll.isHorizontalScrollBarEnabled =
            true

        codeScroll.isScrollbarFadingEnabled =
            false

        codeScroll.setPadding(
            0,
            0,
            0,
            dp(4)
        )

        val codeBody =
            LinearLayout(this)

        codeBody.orientation =
            LinearLayout.HORIZONTAL

        /*
         * NÚMEROS DE LÍNEA.
         */
        val lineNumbers =
            TextView(this)

        val lineCount =
            code.count {
                it == '\n'
            } + 1

        lineNumbers.text =
            (1..lineCount)
                .joinToString(
                    "\n"
                )

        lineNumbers.typeface =
            Typeface.MONOSPACE

        lineNumbers.textSize =
            16f

        lineNumbers.includeFontPadding =
            false

        lineNumbers.gravity =
            Gravity.TOP or
                    Gravity.END

        lineNumbers.setTextColor(
            Color.rgb(
                111,
                119,
                130
            )
        )

        lineNumbers.setBackgroundColor(
            Color.rgb(
                24,
                26,
                31
            )
        )

        lineNumbers.setPadding(
            0,
            dp(18),
            dp(3),
            dp(18)
        )

        lineNumbers.layoutParams =
            LinearLayout.LayoutParams(
                dp(18),
                LinearLayout.LayoutParams.MATCH_PARENT
            )

        /*
         * CÓDIGO.
         */
        val codeView =
            TextView(this)

        codeView.text =
            code

        codeView.setTextColor(
            Color.rgb(
                225,
                230,
                235
            )
        )

        codeView.typeface =
            Typeface.MONOSPACE

        codeView.textSize =
            16f

        codeView.includeFontPadding =
            false

        codeView.setTextIsSelectable(
            true
        )

        codeView.setPadding(
            dp(10),
            dp(18),
            dp(18),
            dp(18)
        )

        codeView.setHorizontallyScrolling(
            true
        )

        val codeParams =
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            )

        codeScroll.addView(
            codeView,
            codeParams
        )

        codeScroll.layoutParams =
            LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            )

        codeBody.addView(
            lineNumbers
        )

        codeBody.addView(
            codeScroll
        )

        card.addView(
            codeBody
        )

        lessonContent.addView(
            card
        )

        makeCodeCardFullWidth(
            card
        )

        /*
         * AJUSTE DE LÍNEA
         */

        var wrapEnabled =
            false

        fun applyCourseWrap(
            enabled: Boolean
        ) {

            wrapEnabled =
                enabled

            val params =
                codeView.layoutParams
                        as FrameLayout.LayoutParams

            if (
                wrapEnabled
            ) {

                codeView.setHorizontallyScrolling(
                    false
                )

                params.width =
                    FrameLayout.LayoutParams.MATCH_PARENT

                codeScroll.isFillViewport =
                    true

                codeScroll.isHorizontalScrollBarEnabled =
                    false

                codeScroll.scrollTo(
                    0,
                    0
                )

                wrapButton.text =
                    ""

                wrapButton.setCompoundDrawablesWithIntrinsicBounds(
                    R.drawable.ic_code_wrap_on,
                    0,
                    0,
                    0
                )

                wrapButton.contentDescription =
                    "Ajuste de línea activado"

            } else {

                codeView.setHorizontallyScrolling(
                    true
                )

                params.width =
                    FrameLayout.LayoutParams.WRAP_CONTENT

                codeScroll.isFillViewport =
                    false

                codeScroll.isHorizontalScrollBarEnabled =
                    true

                codeScroll.isScrollbarFadingEnabled =
                    false

                wrapButton.text =
                    ""

                wrapButton.setCompoundDrawablesWithIntrinsicBounds(
                    R.drawable.ic_code_wrap_off,
                    0,
                    0,
                    0
                )

                wrapButton.contentDescription =
                    "Ajuste de línea desactivado"
            }

            codeView.layoutParams =
                params
        }

        registerCourseWrapController { enabled ->

            applyCourseWrap(
                enabled
            )
        }

        wrapButton.setOnClickListener {

            setCurrentCourseWrap(
                !wrapEnabled
            )
        }

        /*
         * COPIAR CÓDIGO
         */

        copyButton.setOnClickListener {

            val clipboard =
                getSystemService(
                    Context.CLIPBOARD_SERVICE
                ) as ClipboardManager

            val clip =
                ClipData.newPlainText(
                    "C++",
                    code
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                getString(
                    R.string.code_copied
                ),
                Toast.LENGTH_SHORT
            ).show()
        }
        return FakeCodeBlockHandle(
            card = card,
            header = header,
            codeBody = codeBody,
            fullscreenButton = fullscreenButton,
            resetButton = resetButton
        )
    }

    private fun addMultipleChoice(
        block: JSONObject,
        blockIndex: Int
    ) {

        requiredExercises++

        var answeredCorrectly =
            false

        val savedState =
            ProgressRepository.getExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex
            )

        val savedSelectedIndex =
            if (
                savedState != null &&
                savedState.has("selectedIndex")
            ) {
                savedState.optInt(
                    "selectedIndex",
                    -1
                )
            } else {
                -1
            }

        val savedCorrect =
            savedState?.optBoolean(
                "correct",
                false
            ) ?: false

        addHeading(
            block.getString(
                "question"
            )
        )

        /*
         * TARJETA ESTILO CÓDIGO
         */
        val answerCard =
            LinearLayout(this)

        answerCard.orientation =
            LinearLayout.VERTICAL

        val cardBackground =
            GradientDrawable()

        cardBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        cardBackground.cornerRadius =
            dp(12).toFloat()

        answerCard.background =
            cardBackground

        val cardParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        cardParams.setMargins(
            0,
            dp(8),
            0,
            dp(18)
        )

        answerCard.layoutParams =
            cardParams

        /*
         * CABECERA
         */
        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f

        val resetButton =
            TextView(this)

        resetButton.text =
            ""

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar ejercicio"

        resetButton.isEnabled =
            savedSelectedIndex >= 0 || savedCorrect

        resetButton.alpha =
            if (
                resetButton.isEnabled
            ) {
                1f
            } else {
                0.35f
            }

        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )

        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar ejercicio"

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            copyButton
        )

        answerCard.addView(
            header
        )

        /*
         * CONTENIDO DEL EJERCICIO
         */
        val contentContainer =
            LinearLayout(this)

        contentContainer.orientation =
            LinearLayout.VERTICAL

        contentContainer.setPadding(
            dp(18),
            dp(16),
            dp(18),
            dp(16)
        )

        val radioGroup =
            RadioGroup(this)

        radioGroup.orientation =
            RadioGroup.VERTICAL

        val options =
            block.getJSONArray(
                "options"
            )

        val optionButtons =
            mutableListOf<RadioButton>()

        for (
        i in 0 until options.length()
        ) {

            val radioButton =
                RadioButton(this)

            radioButton.id =
                View.generateViewId()

            radioButton.text =
                options.getString(i)

            radioButton.setTextColor(
                Color.WHITE
            )

            radioButton.textSize =
                16f

            radioButton.setPadding(
                0,
                dp(8),
                0,
                dp(8)
            )

            radioGroup.addView(
                radioButton
            )

            optionButtons.add(
                radioButton
            )
        }

        if (
            savedSelectedIndex in 0 until optionButtons.size
        ) {
            radioGroup.check(
                optionButtons[
                    savedSelectedIndex
                ].id
            )
        }

        contentContainer.addView(
            radioGroup
        )

        answerCard.addView(
            contentContainer
        )

        lessonContent.addView(
            answerCard
        )

        /*
         * BOTÓN COMPROBAR
         */
        val button =
            Button(this)

        button.text =
            "Comprobar"

        button.isAllCaps =
            false

        button.isEnabled =
            radioGroup.checkedRadioButtonId != -1 &&
                    !savedCorrect

        lessonContent.addView(
            button
        )

        /*
         * RESULTADO
         */
        val result =
            TextView(this)

        result.textSize =
            16f

        result.setPadding(
            0,
            dp(12),
            0,
            dp(24)
        )

        lessonContent.addView(
            result
        )

        fun updateResetButton() {

            val enabled =
                answeredCorrectly ||
                        radioGroup.checkedRadioButtonId != -1

            resetButton.isEnabled =
                enabled

            resetButton.alpha =
                if (
                    enabled
                ) {
                    1f
                } else {
                    0.35f
                }
        }

        fun saveCurrentSelectionState() {

            val checkedId =
                radioGroup.checkedRadioButtonId

            var selectedIndex =
                -1

            for (
            i in optionButtons.indices
            ) {
                if (
                    optionButtons[i].id ==
                    checkedId
                ) {
                    selectedIndex = i
                    break
                }
            }

            val state =
                JSONObject()

            state.put(
                "selectedIndex",
                selectedIndex
            )

            state.put(
                "correct",
                answeredCorrectly
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )
        }

        radioGroup.setOnCheckedChangeListener { _, _ ->

            button.isEnabled =
                radioGroup.checkedRadioButtonId != -1 &&
                        !answeredCorrectly

            updateResetButton()

            saveCurrentSelectionState()
        }

        copyButton.setOnClickListener {

            val builder =
                StringBuilder()

            builder.append(
                block.getString(
                    "question"
                )
            )

            builder.append("\n\n")

            for (
            i in 0 until options.length()
            ) {
                builder.append("• ")
                builder.append(
                    options.getString(i)
                )
                builder.append("\n")
            }

            val checkedId =
                radioGroup.checkedRadioButtonId

            if (
                checkedId != -1
            ) {
                for (
                i in optionButtons.indices
                ) {
                    if (
                        optionButtons[i].id ==
                        checkedId
                    ) {
                        builder.append("\nSeleccionado: ")
                        builder.append(
                            options.getString(i)
                        )
                        break
                    }
                }
            }

            val clipboard =
                getSystemService(
                    Context.CLIPBOARD_SERVICE
                ) as ClipboardManager

            val clip =
                ClipData.newPlainText(
                    "Ejercicio",
                    builder.toString()
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                "Ejercicio copiado",
                Toast.LENGTH_SHORT
            ).show()
        }

        if (
            savedCorrect
        ) {

            answeredCorrectly =
                true

            completedExercises += 1

            for (
            radio in optionButtons
            ) {
                radio.isEnabled =
                    false
            }

            button.isEnabled =
                false

            button.text =
                "Correcto ✓"

            result.text =
                "✓ Correcto\n\n" +
                        block.optString(
                            "explanation"
                        )

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        updateResetButton()

        resetButton.setOnClickListener {

            if (
                radioGroup.checkedRadioButtonId == -1 &&
                !answeredCorrectly
            ) {
                return@setOnClickListener
            }

            if (
                answeredCorrectly
            ) {

                answeredCorrectly =
                    false

                completedExercises =
                    (
                            completedExercises - 1
                            ).coerceAtLeast(0)

                pageCompletedExercises[
                    currentPage
                ] =
                    completedExercises
            }

            radioGroup.clearCheck()

            for (
            radio in optionButtons
            ) {
                radio.isEnabled =
                    true
            }

            button.isEnabled =
                false

            button.text =
                "Comprobar"

            result.text =
                ""

            val state =
                JSONObject()

            state.put(
                "selectedIndex",
                -1
            )

            state.put(
                "correct",
                false
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            updateResetButton()
        }

        button.setOnClickListener {

            val checkedId =
                radioGroup.checkedRadioButtonId

            if (
                checkedId == -1
            ) {

                Toast.makeText(
                    this,
                    "Selecciona una respuesta.",
                    Toast.LENGTH_SHORT
                ).show()

                return@setOnClickListener
            }

            var selectedIndex =
                -1

            for (
            i in optionButtons.indices
            ) {
                if (
                    optionButtons[i].id ==
                    checkedId
                ) {
                    selectedIndex = i
                    break
                }
            }

            val correctIndex =
                block.getInt(
                    "correctIndex"
                )

            val correct =
                selectedIndex == correctIndex

            val state =
                JSONObject()

            state.put(
                "selectedIndex",
                selectedIndex
            )

            state.put(
                "correct",
                correct
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    "✓ Correcto\n\n" +
                            block.optString(
                                "explanation"
                            )

                result.setTextColor(
                    Color.rgb(
                        180,
                        220,
                        190
                    )
                )

                if (
                    !answeredCorrectly
                ) {

                    answeredCorrectly =
                        true

                    markExerciseCompleted()

                    for (
                    radio in optionButtons
                    ) {
                        radio.isEnabled =
                            false
                    }

                    button.isEnabled =
                        false

                    button.text =
                        "Correcto ✓"
                }

            } else {

                result.text =
                    "✗ Incorrecto"

                result.setTextColor(
                    Color.rgb(
                        240,
                        150,
                        150
                    )
                )
            }

            updateResetButton()
        }
    }

    private fun dp(
        value: Int
    ): Int {

        return (
                value *
                        resources.displayMetrics.density
                ).toInt()
    }

    private fun addInlineFillCode(
        block: JSONObject,
        blockIndex: Int
    ) {

        requiredExercises++

        var answeredCorrectly =
            false

        val savedState =
            ProgressRepository.getExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex
            )

        val savedCorrect =
            savedState?.optBoolean(
                "correct",
                false
            ) ?: false

        addHeading(
            block.getString(
                "question"
            )
        )

        /*
         * CONTENEDOR GENERAL DEL CÓDIGO
         */

        val codeCard =
            LinearLayout(this)

        codeCard.orientation =
            LinearLayout.VERTICAL

        val cardBackground =
            GradientDrawable()

        cardBackground.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        cardBackground.cornerRadius =
            dp(12).toFloat()

        codeCard.background =
            cardBackground

        val cardParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        cardParams.setMargins(
            0,
            dp(8),
            0,
            dp(18)
        )

        codeCard.layoutParams =
            cardParams

        /*
         * CABECERA
         */

        val header =
            LinearLayout(this)

        header.orientation =
            LinearLayout.HORIZONTAL

        header.gravity =
            Gravity.CENTER_VERTICAL

        header.setPadding(
            dp(14),
            dp(8),
            dp(8),
            dp(8)
        )

        header.setBackgroundColor(
            Color.rgb(
                37,
                40,
                46
            )
        )

        /*
         * ETIQUETA DEL LENGUAJE
         */

        val languageLabel =
            TextView(this)

        languageLabel.text =
            "</>  C++"

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.setTypeface(
            null,
            Typeface.BOLD
        )

        languageLabel.textSize =
            15f

        languageLabel.layoutParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        /*
         * BOTÓN AJUSTAR LÍNEA
         */

        val resetButton =
            TextView(this)

        resetButton.text =
            ""

        resetButton.gravity =
            Gravity.CENTER

        resetButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        resetButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_reset,
            0,
            0,
            0
        )

        resetButton.contentDescription =
            "Reiniciar ejercicio"

        resetButton.isClickable =
            true

        resetButton.isEnabled =
            false

        resetButton.alpha =
            0.35f

        val spacer =
            View(this)

        spacer.layoutParams =
            LinearLayout.LayoutParams(
                0,
                1,
                1f
            )


        val actionsContainer =
            LinearLayout(this)

        actionsContainer.orientation =
            LinearLayout.HORIZONTAL

        actionsContainer.gravity =
            Gravity.CENTER_VERTICAL


        val wrapButton =
            TextView(this)

        wrapButton.text =
            ""

        wrapButton.gravity =
            Gravity.CENTER

        wrapButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        wrapButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_wrap_off,
            0,
            0,
            0
        )

        wrapButton.contentDescription =
            "Ajuste de línea desactivado"

        wrapButton.isClickable =
            true


        val copyButton =
            TextView(this)

        copyButton.text =
            ""

        copyButton.gravity =
            Gravity.CENTER

        copyButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        copyButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_copy,
            0,
            0,
            0
        )

        copyButton.contentDescription =
            "Copiar código"

        copyButton.visibility =
            View.VISIBLE

        copyButton.isClickable =
            true

        /*
 * PANTALLA COMPLETA.
 */
        val fullscreenButton =
            TextView(this)

        fullscreenButton.gravity =
            Gravity.CENTER

        fullscreenButton.setPadding(
            dp(10),
            dp(8),
            dp(10),
            dp(8)
        )

        fullscreenButton.setCompoundDrawablesWithIntrinsicBounds(
            R.drawable.ic_code_fullscreen,
            0,
            0,
            0
        )

        fullscreenButton.contentDescription =
            "Pantalla completa"

        fullscreenButton.isClickable =
            true

        /*
         * Identificador estable para recuperar
         * este ejercicio después de Reiniciar.
         */
        fullscreenButton.tag =
            "inline_fill_fullscreen_$blockIndex"

        actionsContainer.addView(
            wrapButton
        )

        actionsContainer.addView(
            copyButton
        )

        actionsContainer.addView(
            fullscreenButton
        )

        header.addView(
            languageLabel
        )

        header.addView(
            resetButton
        )

        header.addView(
            spacer
        )

        header.addView(
            actionsContainer
        )

        codeCard.addView(
            header
        )

        /*
         * SCROLL HORIZONTAL
         */

        val horizontalScroll =
            HorizontalScrollView(this)

        horizontalScroll.tag =
            "order_code_swipe_protected"

        horizontalScroll.isHorizontalScrollBarEnabled =
            true

        horizontalScroll.isScrollbarFadingEnabled =
            false

        horizontalScroll.clipToPadding =
            false

        protectHorizontalScrollFromPageSwipe(
            horizontalScroll
        )

        val codeBody =
            LinearLayout(this)

        codeBody.orientation =
            LinearLayout.HORIZONTAL

        /*
         * Columna estrecha de números.
         */
        val lineNumberColumn =
            LinearLayout(this)

        lineNumberColumn.orientation =
            LinearLayout.VERTICAL

        lineNumberColumn.setBackgroundColor(
            Color.rgb(
                24,
                26,
                31
            )
        )

        lineNumberColumn.setPadding(
            0,
            dp(12),
            0,
            dp(12)
        )

        lineNumberColumn.layoutParams =
            LinearLayout.LayoutParams(
                dp(18),
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        /*
         * Contenido real del código.
         */
        val codeLines =
            LinearLayout(this)

        val flowRows =
            mutableListOf<CodeFlowLayout>()

        codeLines.orientation =
            LinearLayout.VERTICAL

        codeLines.setPadding(
            dp(10),
            dp(12),
            dp(60),
            dp(12)
        )

        horizontalScroll.layoutParams =
            LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            )

        horizontalScroll.addView(
            codeLines
        )

        codeBody.addView(
            lineNumberColumn
        )

        codeBody.addView(
            horizontalScroll
        )

        codeCard.addView(
            codeBody
        )

        lessonContent.addView(
            codeCard
        )

        makeCodeCardFullWidth(
            codeCard
        )

        /*
 * FULLSCREEN DEL INLINE_FILL_CODE.
 *
 * Los EditText forman parte de codeBody,
 * así que al mover codeBody movemos también
 * los campos reales y conservamos:
 *
 * - texto escrito
 * - selección
 * - listeners
 * - foco
 */

        /*
         * CAMPOS EDITABLES.
         */

        val inputs =
            mutableListOf<EditText>()

        val template =
            block.getString(
                "template"
            )

        copyButton.setOnClickListener {

            copyCodeToClipboard(
                buildInlineFilledCode(
                    template,
                    inputs
                )
            )
        }

        val lines =
            template.split("\n")

        val lineNumberViews =
            mutableListOf<TextView>()

        for (
        lineIndex in lines.indices
        ) {

            val lineNumber =
                TextView(this)

            lineNumber.text =
                "${lineIndex + 1}"

            lineNumber.typeface =
                Typeface.MONOSPACE

            lineNumber.textSize =
                16f

            lineNumber.includeFontPadding =
                false

            lineNumber.gravity =
                Gravity.CENTER_VERTICAL or
                        Gravity.END

            lineNumber.setTextColor(
                Color.rgb(
                    111,
                    119,
                    130
                )
            )

            lineNumber.setPadding(
                0,
                0,
                dp(3),
                0
            )

            lineNumber.layoutParams =
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )

            lineNumberViews.add(
                lineNumber
            )

            lineNumberColumn.addView(
                lineNumber
            )
        }

        var inputIndex =
            0

        for (
        line in lines
        ) {

            val row =
                CodeFlowLayout(this)

            row.wrapEnabled =
                false

            row.minimumHeight =
                0

            flowRows.add(
                row
            )

            var position =
                0

            while (true) {

                val blankPosition =
                    line.indexOf(
                        "[]",
                        position
                    )

                if (
                    blankPosition == -1
                ) {

                    val remaining =
                        line.substring(
                            position
                        )

                    if (
                        remaining.isNotEmpty()
                    ) {

                        val text =
                            createInlineCodeText(
                                remaining
                            )

                        row.addView(
                            text
                        )
                    }

                    break
                }

                /*
                 * Texto anterior al hueco.
                 */

                val before =
                    line.substring(
                        position,
                        blankPosition
                    )

                if (
                    before.isNotEmpty()
                ) {

                    val text =
                        createInlineCodeText(
                            before
                        )

                    row.addView(
                        text
                    )
                }

                /*
                 * Campo editable.
                 */

                val input =
                    EditText(this)

                input.tag =
                    CODE_INPUT_TAG

                /*
                 * Este campo contiene código,
                 * no credenciales.
                 *
                 * Evitamos que Android lo considere
                 * candidato para contraseñas/autorrelleno.
                 */
                if (
                    android.os.Build.VERSION.SDK_INT >=
                    android.os.Build.VERSION_CODES.O
                ) {

                    input.importantForAutofill =
                        View.IMPORTANT_FOR_AUTOFILL_NO
                }

                /*
                 * Campo de texto normal para código,
                 * sin autocorrección ni sugerencias
                 * lingüísticas del teclado.
                 */
                input.inputType =
                    android.text.InputType.TYPE_CLASS_TEXT or
                            android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS

                enableCppAutoClose(
                    input
                )

                protectProgrammingInteractionFromPageSwipe(
                    input
                )

                input.setSingleLine(
                    true
                )

                input.typeface =
                    Typeface.MONOSPACE

                input.textSize =
                    16f

                input.setTextColor(
                    Color.WHITE
                )

                input.setHintTextColor(
                    Color.rgb(
                        130,
                        135,
                        145
                    )
                )

                input.hint =
                    "..."

                input.gravity =
                    Gravity.CENTER

                input.setPadding(
                    dp(8),
                    0,
                    dp(8),
                    0
                )

                input.minWidth =
                    dp(56)

                input.minimumHeight =
                    0

                val inputBackground =
                    GradientDrawable()

                inputBackground.setColor(
                    Color.rgb(
                        42,
                        45,
                        53
                    )
                )

                inputBackground.cornerRadius =
                    dp(7).toFloat()

                inputBackground.setStroke(
                    dp(1),
                    Color.rgb(
                        120,
                        125,
                        140
                    )
                )

                input.background =
                    inputBackground

                val inputParams =
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        dp(28)
                    )

                inputParams.setMargins(
                    dp(3),
                    0,
                    dp(3),
                    0
                )

                input.layoutParams =
                    inputParams

                /*
                 * Restaurar lo escrito.
                 */

                val savedAnswers =
                    savedState?.optJSONArray(
                        "answers"
                    )

                if (
                    savedAnswers != null &&
                    inputIndex <
                    savedAnswers.length()
                ) {

                    input.setText(
                        savedAnswers.optString(
                            inputIndex,
                            ""
                        )
                    )
                }

                inputs.add(
                    input
                )

                row.addView(
                    input
                )

                inputIndex++

                position =
                    blankPosition + 2
            }

            /*
             * Incluso una línea vacía
             * conserva su altura.
             */

            if (
                row.childCount == 0
            ) {

                val empty =
                    createInlineCodeText(
                        " "
                    )

                row.addView(
                    empty
                )
            }

            codeLines.addView(
                row
            )
        }

        fun updateResetButton() {

            val hasText =
                inputs.any { input ->

                    input.text.isNotEmpty()
                }

            val enabled =
                answeredCorrectly ||
                        hasText

            resetButton.isEnabled =
                enabled

            resetButton.alpha =
                if (
                    enabled
                ) {
                    1f
                } else {
                    0.35f
                }
        }

        /*
         * BOTÓN COMPROBAR.
         *
         * Se crea antes de los TextWatcher
         * porque éstos necesitan activarlo
         * inmediatamente al empezar a escribir.
         */
        val checkButton =
            Button(this)

        checkButton.text =
            "Comprobar"

        checkButton.isAllCaps =
            false

        checkButton.isEnabled =
            inputs.any {
                it.text.isNotEmpty()
            } &&
                    !savedCorrect


        for (
        input in inputs
        ) {

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
                        s: Editable?
                    ) {

                        updateResetButton()

                        checkButton.isEnabled =
                            inputs.any {
                                it.text.isNotEmpty()
                            } &&
                                    !answeredCorrectly
                    }
                }
            )
        }


        updateResetButton()

        fun syncLineNumberHeights() {

            codeLines.post {

                for (
                index in lineNumberViews.indices
                ) {

                    if (
                        index >=
                        codeLines.childCount
                    ) {
                        continue
                    }

                    val codeRow =
                        codeLines.getChildAt(
                            index
                        )

                    val numberView =
                        lineNumberViews[
                            index
                        ]

                    val params =
                        numberView.layoutParams

                    params.height =
                        codeRow.height

                    numberView.layoutParams =
                        params
                }
            }
        }

        syncLineNumberHeights()

        var wrapEnabled =
            false

        registerCourseWrapController { enabled ->

            wrapEnabled =
                enabled

            setPuzzleWrapMode(
                wrapEnabled,
                horizontalScroll,
                codeLines,
                flowRows,
                wrapButton
            )

            codeLines.postDelayed(
                {
                    syncLineNumberHeights()
                },
                80L
            )
        }

        wrapButton.setOnClickListener {

            setCurrentCourseWrap(
                !wrapEnabled
            )
        }

        fullscreenButton.setOnClickListener {

            /*
             * SALIR.
             */
            if (
                exerciseFullscreenActive
            ) {

                closeExerciseFullscreenHost()

                fullscreenButton.contentDescription =
                    "Pantalla completa"

                return@setOnClickListener
            }

            /*
             * ENTRAR.
             *
             * Este ejercicio SÍ utiliza teclado.
             */
            openExerciseFullscreenHost(
                usesKeyboard = true
            ) {
                    headerHost,
                    bodyHost,
                    floatingHost ->

                /*
                 * Scroll vertical general del código.
                 *
                 * Esto permite ejercicios muy largos
                 * sin desplazar la cabecera.
                 */
                val verticalCodeScroll =
                    ScrollView(this)

                verticalCodeScroll.isFillViewport =
                    true

                verticalCodeScroll.isVerticalScrollBarEnabled =
                    true

                verticalCodeScroll.isScrollbarFadingEnabled =
                    false

                verticalCodeScroll.layoutParams =
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        0,
                        1f
                    )

                bodyHost.addView(
                    verticalCodeScroll
                )

                /*
                 * codeBody originalmente ocupa el índice 1
                 * dentro de codeCard.
                 *
                 * Lo retiramos primero para conservar
                 * correctamente ese índice.
                 */
                moveViewToExerciseFullscreen(
                    codeBody,
                    verticalCodeScroll,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                /*
                 * header ocupa el índice 0.
                 *
                 * Visualmente permanece fija arriba.
                 */
                moveViewToExerciseFullscreen(
                    header,
                    headerHost,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                /*
                 * Barra real de atajos del teclado.
                 *
                 * No creamos una segunda.
                 */
                floatingHost.visibility =
                    View.VISIBLE

                moveViewToExerciseFullscreen(
                    programmingKeyboardBar,
                    floatingHost,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT
                    )
                )

                fullscreenButton.contentDescription =
                    "Salir de pantalla completa"

                /*
                 * Recalculamos el ajuste de línea porque
                 * el viewport acaba de cambiar de tamaño.
                 */
                horizontalScroll.post {

                    setPuzzleWrapMode(
                        wrapEnabled,
                        horizontalScroll,
                        codeLines,
                        flowRows,
                        wrapButton
                    )

                    syncLineNumberHeights()
                }

                updateProgrammingKeyboardBarVisibility()
            }
        }

        /*
 * BOTÓN COMPROBAR
 */

        lessonContent.addView(
            checkButton
        )

        val result =
            TextView(this)

        result.textSize =
            16f

        result.setPadding(
            0,
            dp(12),
            0,
            dp(24)
        )

        lessonContent.addView(
            result
        )

        resetButton.setOnClickListener {

            /*
             * Recordamos si el usuario estaba
             * trabajando en fullscreen.
             */
            val reopenFullscreen =
                exerciseFullscreenActive

            /*
             * Antes de reconstruir la página
             * restauramos correctamente todas
             * las Views movidas.
             */
            if (
                reopenFullscreen
            ) {

                closeExerciseFullscreenHost()
            }

            val resetState =
                JSONObject()

            resetState.put(
                "answers",
                org.json.JSONArray()
            )

            resetState.put(
                "correct",
                false
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                resetState
            )

            pageViewCache.remove(
                currentPage
            )

            pageRequiredExercises.remove(
                currentPage
            )

            pageCompletedExercises.remove(
                currentPage
            )

            renderCurrentPage()

            /*
             * Si Reiniciar ocurrió desde fullscreen,
             * abrimos nuevamente el ejercicio recién
             * reconstruido.
             */
            if (
                reopenFullscreen
            ) {

                lessonContent.post {

                    val newFullscreenButton =
                        lessonContent.findViewWithTag<View>(
                            "inline_fill_fullscreen_$blockIndex"
                        )

                    newFullscreenButton
                        ?.performClick()
                }
            }
        }

        /*
         * Restaurar ejercicio ya correcto.
         */

        if (
            savedCorrect
        ) {

            answeredCorrectly =
                true

            completedExercises += 1

            copyButton.visibility =
                View.VISIBLE

            for (
            input in inputs
            ) {

                input.isEnabled =
                    false
            }

            checkButton.isEnabled =
                false

            checkButton.text =
                "Correcto ✓"

            result.text =
                "✓ Correcto"

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        checkButton.setOnClickListener {

            val expectedAnswers =
                block.getJSONArray(
                    "answers"
                )

            var correct =
                inputs.size ==
                        expectedAnswers.length()

            if (correct) {

                for (
                i in inputs.indices
                ) {

                    val actual =
                        normalizeAnswer(
                            inputs[i]
                                .text
                                .toString()
                        )

                    val expected =
                        normalizeAnswer(
                            expectedAnswers
                                .getString(i)
                        )

                    if (
                        actual != expected
                    ) {

                        correct =
                            false

                        break
                    }
                }
            }

            /*
             * Guardamos TODOS los campos.
             */

            val state =
                JSONObject()

            val answersJson =
                org.json.JSONArray()

            for (
            input in inputs
            ) {

                answersJson.put(
                    input.text.toString()
                )
            }

            state.put(
                "answers",
                answersJson
            )

            state.put(
                "correct",
                correct
            )

            ProgressRepository.saveExerciseState(
                this,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    "✓ Correcto"

                result.setTextColor(
                    Color.rgb(
                        180,
                        220,
                        190
                    )
                )

                copyButton.visibility =
                    View.VISIBLE

                if (
                    !answeredCorrectly
                ) {

                    answeredCorrectly =
                        true

                    markExerciseCompleted()

                    for (
                    input in inputs
                    ) {

                        input.isEnabled =
                            false
                    }

                    checkButton.isEnabled =
                        false

                    checkButton.text =
                        "Correcto ✓"
                }

            } else {

                result.text =
                    "✗ Revisa los espacios del código."

                result.setTextColor(
                    Color.rgb(
                        240,
                        150,
                        150
                    )
                )
            }
        }
    }

    private fun copyCodeToClipboard(
        code: String
    ) {

        val clipboard =
            getSystemService(
                CLIPBOARD_SERVICE
            ) as android.content.ClipboardManager

        val clip =
            android.content.ClipData.newPlainText(
                "code",
                code
            )

        clipboard.setPrimaryClip(
            clip
        )

        Toast.makeText(
            this,
            "Código copiado",
            Toast.LENGTH_SHORT
        ).show()
    }

    private fun buildInlineFilledCode(
        template: String,
        inputs: List<EditText>
    ): String {

        var inputIndex = 0

        return template
            .split("\n")
            .joinToString("\n") { line ->

                val builder =
                    StringBuilder()

                var position = 0

                while (true) {

                    val blankPosition =
                        line.indexOf(
                            "[]",
                            position
                        )

                    if (
                        blankPosition == -1
                    ) {

                        builder.append(
                            line.substring(
                                position
                            )
                        )

                        break
                    }

                    builder.append(
                        line.substring(
                            position,
                            blankPosition
                        )
                    )

                    val value =
                        if (
                            inputIndex < inputs.size
                        ) {

                            inputs[inputIndex]
                                .text
                                .toString()

                        } else {
                            ""
                        }

                    builder.append(
                        value
                    )

                    inputIndex++

                    position =
                        blankPosition + 2
                }

                builder.toString()
            }
    }

    private fun buildDragFilledCode(
        template: String,
        slots: List<FrameLayout>
    ): String {

        var slotIndex = 0

        return template
            .split("\n")
            .joinToString("\n") { line ->

                val builder =
                    StringBuilder()

                var position = 0

                while (true) {

                    val blankPosition =
                        line.indexOf(
                            "[]",
                            position
                        )

                    if (
                        blankPosition == -1
                    ) {

                        builder.append(
                            line.substring(
                                position
                            )
                        )

                        break
                    }

                    builder.append(
                        line.substring(
                            position,
                            blankPosition
                        )
                    )

                    val value =
                        if (
                            slotIndex < slots.size &&
                            slots[slotIndex].childCount > 0
                        ) {

                            (
                                    slots[slotIndex]
                                        .getChildAt(0)
                                            as TextView
                                    ).text.toString()

                        } else {
                            ""
                        }

                    builder.append(
                        value
                    )

                    slotIndex++

                    position =
                        blankPosition + 2
                }

                builder.toString()
            }
    }

    private fun compactCodeSpaces(
        text: String
    ): CharSequence {

        val result =
            android.text.SpannableString(
                text
            )

        for (
        i in text.indices
        ) {

            if (
                text[i] == ' '
            ) {

                result.setSpan(
                    android.text.style.ScaleXSpan(
                        0.55f
                    ),
                    i,
                    i + 1,
                    android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
        }

        return result
    }

    private fun createInlineCodeText(
        text: String
    ): TextView {

        val view =
            TextView(this)

        view.text =
            compactCodeSpaces(
                text
            )

        view.typeface =
            Typeface.MONOSPACE

        view.textSize =
            16f

        view.includeFontPadding =
            false

        view.setPadding(
            0,
            0,
            0,
            0
        )

        view.minWidth =
            0

        view.minHeight =
            0

        view.setTextColor(
            Color.rgb(
                225,
                230,
                235
            )
        )

        return view
    }
    private companion object {

        const val CODE_INPUT_TAG =
            "cpp_code_input"
    }
}
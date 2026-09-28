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

                /*
                 * ProgressRepository ya eliminó
                 * todo el progreso persistente.
                 *
                 * Ahora eliminamos también
                 * el estado que LessonActivity
                 * conserva actualmente en memoria.
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

                /*
                 * Volvemos a cargar la lección.
                 *
                 * Como su página guardada ya fue
                 * eliminada, loadLesson recuperará
                 * automáticamente la página 1.
                 */
                loadLesson(
                    currentLessonId
                )
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

    private var ignoreCurrentSwipe = false
    private lateinit var lessonContent: LinearLayout
    private lateinit var lessonScroll: ScrollView

    private var lessonNumber = 1

    private var currentPage = 0
    private var totalPages = 1

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

        currentLessonId =
            intent.getStringExtra("lessonId") ?: return

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

        AppearanceUi.apply(
            this
        )

        if (
            ::currentLessonId.isInitialized
        ) {

            NavigationStateRepository.saveLesson(
                this,
                currentLessonId
            )
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

                val availableWidth =
                    (
                            horizontalScroll.width -
                                    codeLines.paddingLeft -
                                    codeLines.paddingRight
                            ).coerceAtLeast(
                            1
                        )

                val codeParams =
                    codeLines.layoutParams

                codeParams.width =
                    availableWidth

                codeLines.layoutParams =
                    codeParams

                for (row in flowRows) {

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
            i in 0 until newParent.childCount
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
                        params is ViewGroup.MarginLayoutParams
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

                        view.layoutParams =
                            LinearLayout.LayoutParams(
                                LinearLayout.LayoutParams.WRAP_CONTENT,
                                LinearLayout.LayoutParams.WRAP_CONTENT
                            ).apply {

                                setMargins(
                                    0,
                                    dp(4),
                                    0,
                                    dp(4)
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
                    .getAccentColor(
                        this
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
         */
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
         * COMPROBAR.
         */
        val button =
            Button(this)

        button.text =
            "Comprobar"

        button.isAllCaps =
            false

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

        val button =
            Button(this)

        button.text =
            "Comprobar"

        button.isAllCaps =
            false

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

        codeCard.addView(
            header
        )


        val horizontalScroll =
            HorizontalScrollView(this)

        horizontalScroll.isHorizontalScrollBarEnabled =
            true

        horizontalScroll.isScrollbarFadingEnabled =
            false

        horizontalScroll.isFillViewport =
            true

        protectHorizontalScrollFromPageSwipe(
            horizontalScroll
        )

        codeCard.addView(
            horizontalScroll
        )

        lessonContent.addView(
            codeCard
        )

        val fragments =
            block.getJSONArray("fragments")

        val correctOrder =
            block.getJSONArray("correctOrder")

        val orderContainer =
            LinearLayout(this)

        orderContainer.orientation =
            LinearLayout.VERTICAL

        horizontalScroll.addView(
            orderContainer
        )

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
                dp(14),
                dp(16),
                dp(14)
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
                dp(4),
                0,
                dp(4)
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
        }

        val checkButton =
            Button(this)

        checkButton.text =
            "Comprobar"

        checkButton.isAllCaps =
            false

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

        when (event.action) {

            MotionEvent.ACTION_DOWN -> {

                ignoreCurrentSwipe = false

                touchStartX = event.x
                touchStartY = event.y
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

    private fun loadLesson(
        lessonId: String
    ) {

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

            AppearanceUi.apply(
                this
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

                    addCode(
                        block.getString(
                            "code"
                        ),
                        block.optString(
                            "language",
                            "text"
                        )
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
            }
        }

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

        AppearanceUi.apply(
            this
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


        actionsContainer.addView(
            wrapButton
        )

        actionsContainer.addView(
            copyButton
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

        val codeLines =
            LinearLayout(this)

        val flowRows =
            mutableListOf<CodeFlowLayout>()

        codeLines.orientation =
            LinearLayout.VERTICAL

        codeLines.setPadding(
            0,
            0,
            0,
            0
        )

        horizontalScroll.addView(
            codeLines
        )

        codeCard.addView(
            horizontalScroll
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

        /*
         * CONSTRUCCIÓN DE CADA LÍNEA.
         * Los espacios iniciales se conservan,
         * por lo que también se conserva la sangría.
         */

        for (line in lines) {

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

        wrapButton.setOnClickListener {

            wrapEnabled =
                !wrapEnabled

            setPuzzleWrapMode(
                wrapEnabled,
                horizontalScroll,
                codeLines,
                flowRows,
                wrapButton
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

                        slots[slotIndex].post {

                            compactDragSlot(
                                slots[slotIndex]
                            )
                        }
                    }
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
         * COMPROBAR
         */

        val checkButton =
            Button(this)

        checkButton.text =
            "Comprobar"

        checkButton.isAllCaps =
            false

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

    private fun addCode(
        code: String,
        language: String,
        showReset: Boolean = false
    ): TextView? {

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

        codeView.setTextIsSelectable(
            true
        )

        codeView.setPadding(
            dp(18),
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

        card.addView(
            codeScroll
        )

        lessonContent.addView(
            card
        )

        /*
         * AJUSTE DE LÍNEA
         */

        var wrapEnabled =
            false

        wrapButton.setOnClickListener {

            wrapEnabled =
                !wrapEnabled

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
        return resetButton
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
                true

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


        actionsContainer.addView(
            wrapButton
        )

        actionsContainer.addView(
            copyButton
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

        horizontalScroll.isHorizontalScrollBarEnabled =
            true

        horizontalScroll.isScrollbarFadingEnabled =
            false

        horizontalScroll.clipToPadding =
            false

        protectHorizontalScrollFromPageSwipe(
            horizontalScroll
        )

        val codeLines =
            LinearLayout(this)

        val flowRows =
            mutableListOf<CodeFlowLayout>()

        codeLines.orientation =
            LinearLayout.VERTICAL

        codeLines.setPadding(
            dp(18),
            dp(16),
            dp(60),
            dp(16)
        )

        horizontalScroll.addView(
            codeLines
        )

        codeCard.addView(
            horizontalScroll
        )

        lessonContent.addView(
            codeCard
        )

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
                dp(46)

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
                    dp(3),
                    dp(8),
                    dp(3)
                )

                input.minWidth =
                    dp(60)

                input.minimumHeight =
                    dp(40)

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
                        dp(42)
                    )

                inputParams.setMargins(
                    dp(3),
                    dp(2),
                    dp(3),
                    dp(2)
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
                    }
                }
            )
        }


        updateResetButton()

        var wrapEnabled =
            false

        wrapButton.setOnClickListener {

            wrapEnabled =
                !wrapEnabled

            setPuzzleWrapMode(
                wrapEnabled,
                horizontalScroll,
                codeLines,
                flowRows,
                wrapButton
            )
        }

        /*
         * BOTÓN COMPROBAR
         */

        val checkButton =
            Button(this)

        checkButton.text =
            "Comprobar"

        checkButton.isAllCaps =
            false

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
}
// LGP_READER_INTERACTIONS_V1: feedback gates, bounded bookmarks and floating controls.
package com.learngraphicsprogramming.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.res.Configuration
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Rect
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.Gravity
import android.view.KeyEvent
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
import android.content.SharedPreferences
import android.widget.ImageButton
import android.widget.ImageView
import androidx.activity.result.contract.ActivityResultContracts
import android.text.Editable
import android.text.TextWatcher
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import com.learngraphicsprogramming.app.runtime.CppBuildPhase
import android.view.ViewTreeObserver
import android.view.inputmethod.InputMethodManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsAnimationCompat
import android.util.TypedValue
import android.widget.SeekBar
import com.learngraphicsprogramming.app.math.mathErrorMessage
import com.learngraphicsprogramming.app.math.MathPuzzleRenderer
import com.learngraphicsprogramming.app.math.MathPuzzleTypes
import com.learngraphicsprogramming.app.math.MathTextScaleAware
import com.learngraphicsprogramming.app.math.MathInputFocusOrder
import com.learngraphicsprogramming.app.math.MathKeyboardHost
import com.learngraphicsprogramming.app.narration.LessonNarrationController
import com.learngraphicsprogramming.app.narration.LessonNarrationControls
import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.ReadingTextHost
import com.learngraphicsprogramming.app.narration.ReadingRadioButton
import com.learngraphicsprogramming.app.narration.NarrationUiInstructions
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.LessonReadingProgressModel
import com.learngraphicsprogramming.app.narration.LessonReadingTextColors
import com.learngraphicsprogramming.app.narration.NarrationPlaybackProgress
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationEngine
import com.learngraphicsprogramming.app.narration.NarrationSegments
import com.learngraphicsprogramming.app.narration.NarrationResumePolicy
import com.learngraphicsprogramming.app.narration.readingPlan
import com.learngraphicsprogramming.app.narration.ReadingTextView
import com.learngraphicsprogramming.app.math.MathTextRenderer
import com.learngraphicsprogramming.app.math.MathDevelopmentMode
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.google.android.material.button.MaterialButton

class LessonActivity : AppCompatActivity() {

    private val puzzleEnterShortcut = PuzzleEnterShortcut()

    private var calculatorResultTarget:
            View? =
        null

    private val calculatorResultLauncher =
        registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->

            val target =
                calculatorResultTarget

            calculatorResultTarget =
                null

            if (
                result.resultCode !=
                RESULT_OK ||
                target == null
            ) {
                return@registerForActivityResult
            }

            val resultLatex =
                result.data
                    ?.getStringExtra(
                        CalculatorActivity.EXTRA_RESULT_LATEX
                    )
                    ?: return@registerForActivityResult

            when (
                target
            ) {

                is EditText -> {

                    target.setText(
                        resultLatex
                    )

                    target.setSelection(
                        target.text.length
                    )
                }

                is MathExpressionEditor -> {

                    target.setLatex(
                        resultLatex
                    )
                }
            }
        }

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
                    disposeCppEditorViewports()
                    pageViewCache.clear()

                    pageRequiredExercises.clear()

                    pageCompletedExercises.clear()

                    exerciseAttempts.clear()

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

    private lateinit var buttonNarrationToggle: ImageView

    private lateinit var imageAttemptRoute: ImageView
    private lateinit var imageAttemptFill: ImageView
    private lateinit var attemptFillClip: FrameLayout
    private lateinit var textAttemptCount: TextView
    private lateinit var textAttemptPercent: TextView
    private lateinit var attemptIndicator: FrameLayout

    private var attemptDetailsPopup:
            android.widget.PopupWindow? =
        null

    private var attemptDetailsContent:
            LinearLayout? =
        null

    private var swipeNavigationEnabled = true

    // Keep penalty history separate from visual feedback and persistent course progress.
    // Cached or rebuilt pages retain it until the lesson is explicitly restarted.
    private val exerciseAttempts = ExerciseAttemptLedger()

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
    private var swipeStartedInEmptyCppEditor = false

    /*
 * Mientras el compilador está en fullscreen,
 * los gestos jamás navegan entre páginas.
 */
    private var compilerFullscreenActive =
        false

    /*
     * La salida fullscreen del compilador depende de las Views de la página.
     * Solo se guarda mientras el editor está realmente expandido.
     */
    private var compilerFullscreenBackExit: (() -> Unit)? = null

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

    private val programmingImeAnimations = mutableSetOf<WindowInsetsAnimationCompat>()
    private var programmingWindowInsets: WindowInsetsCompat? = null
    private var normalProgrammingBarHeight = 0
    private var programmingLessonHeaderHeight = 0
    private var programmingImeFullscreenSystemUi: Int? = null
    private var programmingImeFullscreenWindowFlag = false
    private data class ProgrammingKeySize(val height: Int, val minimumHeight: Int, val top: Int, val bottom: Int)
    private val programmingKeySizes = mutableMapOf<TextView, ProgrammingKeySize>()
    private val programmingViewportUiControllers = mutableListOf<() -> Unit>()
    private val cppEditorViewportControllers = mutableListOf<CppEditorViewportController>()
    private var programmingFullscreenHeader: View? = null
    private var programmingFullscreenFloating: View? = null
    private val programmingFullscreenPaddings = mutableMapOf<View, Rect>()
    private val programmingFullscreenMargins = mutableMapOf<View, Rect>()
    private val programmingInputPaddings = mutableMapOf<EditText, Rect>()
    private data class ProgrammingTextSize(val size: Float, val lineHeight: Int)
    private data class ProgrammingReparentFocus(val input: EditText, val selectionStart: Int,
        val selectionEnd: Int, val imeWasVisible: Boolean)
    private val programmingTextSizes = mutableMapOf<TextView, ProgrammingTextSize>()
    private val programmingCodeTextCompanions = mutableMapOf<EditText, TextView>()
    private var lastProgrammingRevealInput: EditText? = null
    private var lastProgrammingRevealViewport = ""
    private data class CodeSurfaceWidth(val portraitWidth: Int, val portraitTranslation: Float, val extendInPortrait: Boolean)
    private val codeSurfaceWidths = mutableMapOf<View, CodeSurfaceWidth>()

    private var lessonNumber = 1

    private var currentPage = 0
    private var totalPages = 1

    private lateinit var currentCourseId: String

    private var currentRouteId =
        AppearanceSettingsRepository.ROUTE_PROGRAMMING

    private var appliedContentTextScale =
        LessonTextSizeSettingsRepository.DEFAULT_SCALE

    private lateinit var currentLessonId: String

    private lateinit var lessonContinueButton: Button
    private val lessonAdvanceEnterGate = LessonPageAdvanceEnterGate()
    private var requiredExercises = 0
        set(value) {
            field = value
            updateContinueButtonState()
        }
    private var completedExercises = 0
        set(value) {
            field = value
            updateContinueButtonState()
        }

    /** The existing AppearanceUi state list renders disabled buttons in gray. */
    private fun updateContinueButtonState() {
        if (::lessonContinueButton.isInitialized) {
            lessonContinueButton.isEnabled =
                LessonPageAdvancePolicy.canAdvance(requiredExercises, completedExercises)
        }
    }

    private lateinit var lessonJson: JSONObject

    private var mathKeyboardHost: MathKeyboardHost? = null

    private var narrationController: LessonNarrationController? = null
    private var narrationControls: LessonNarrationControls? = null
    private val narrationUiPreferences by lazy {
        applicationContext.getSharedPreferences("lesson_narration_settings", MODE_PRIVATE)
    }
    private val narrationUiSettingsListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ ->
        runOnUiThread {
            if (!isDestroyed && ::buttonNarrationToggle.isInitialized) {
                narrationControls?.refresh(narrationController?.isPlaying == true)
                updateNarrationVisibility()
            }
        }
    }
    private var narrationExerciseObserver: com.learngraphicsprogramming.app.narration.LessonNarrationExerciseState? = null
    private val readingProgress = LessonReadingProgressModel()
    private val readingTextPages = mutableMapOf<String, LessonReadingTextColors>()
    private val mathNarrationSources = mutableMapOf<String, MutableMap<Int, () -> JSONObject>>()
    private data class ReadingPresentation(val configuration: String, val script: LessonNarrationScript)
    private val readingPresentations = mutableMapOf<String, ReadingPresentation>()
    /** Presentation for the controller's one current review, never another reader bookmark. */
    private data class IncorrectReadingPresentation(
        val pageIdentity: String, val blockIndex: Int, val script: LessonNarrationScript,
        val language: NarrationLanguage, val engine: NarrationEngine,
        var token: Long? = null, var identity: String? = null, var playbackKey: String? = null
    )
    private var incorrectReadingPresentation: IncorrectReadingPresentation? = null
    private var relocatingReadingPresentation = false
    private var deferredReadingPresentation = false
    private var readingRenderBlockIndex: Int? = null
    private var readingPlaybackOwner: Any? = null
    private val floatingInteractions by lazy { LessonFloatingInteractions(this) }

    private fun narrationScript(language: com.learngraphicsprogramming.app.narration.NarrationLanguage): LessonNarrationScript {
        val selected = NarrationSettingsRepository.forLanguage(this, language)
        val neural = com.learngraphicsprogramming.app.narration.MixedSpanishG2P.productionEnabled &&
            selected.engine != com.learngraphicsprogramming.app.narration.NarrationEngine.ANDROID
        val identity = narrationPageIdentity()
        val script = LessonNarrationScript.presentedPage(lessonJson,currentPage,language,
            forKokoro = neural,
            instruction = { type -> NarrationUiInstructions.forBlock(this,type) },
            presentation = { index, block -> mathNarrationSources[identity]?.get(index)?.invoke() ?: block })
        val configuration = "${language.tag}/${selected.engine}/${selected.kokoroVoice}/${selected.androidVoice}"
        val known = readingPresentations[identity]?.takeIf { it.configuration == configuration }?.script
        val previousPresentations = if (known != null) listOf(known) else listOf(
            LessonNarrationScript.presentedPageVersioned(lessonJson, currentPage, language, neural,
                instruction = { type -> NarrationUiInstructions.forBlock(this, type) },
                completeVisibleLabels = false,
                presentation = { index, block -> mathNarrationSources[identity]?.get(index)?.invoke() ?: block }),
            LessonNarrationScript.presentedPageVersioned(lessonJson, currentPage, language, neural,
                instruction = { type -> if (type in setOf("order_code", "drag_fill_code"))
                    NarrationUiInstructions.forBlock(this, type) else "" },
                completeVisibleLabels = false,
                presentation = { _, block -> com.learngraphicsprogramming.app.math.MathNarrationPresentation.legacyBlock(block) })
        )
        var update: com.learngraphicsprogramming.app.narration.NarrationPresentationUpdate? = null
        var deferred: LessonNarrationScript? = null
        try {
            relocatingReadingPresentation = true
            for (previous in previousPresentations) {
                val candidate = narrationController?.relocatePresentation(previous, script, language, identity)
                if (candidate?.deferred == true) {
                    deferred = previous
                    break
                }
                if (candidate?.rebind == true) update = candidate
            }
        } finally { relocatingReadingPresentation = false }
        if (deferred != null) {
            deferredReadingPresentation = true
            return deferred
        }
        deferredReadingPresentation = false
        readingPresentations[identity] = ReadingPresentation(configuration, script)
        prepareReadingPage(script, language)
        if (update?.rebind == true) {
            if (update?.resume == true) narrationController?.toggle(script.texts, language, identity, script.gates, narrationCorrectBlocks())
            else narrationController?.bindPausedPresentation(script.texts, language, identity, script.gates, narrationCorrectBlocks())
        }
        return script
    }

    /** Runtime decoration and reading references over the original curricular characters. */
    private fun registerReadingText(view: TextView, blockIndex: Int? = readingRenderBlockIndex,
                                    mathMode: String = "auto") {
        if (view is ReadingTextHost) {
            LessonTechnicalText.apply(view,
                currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
                NarrationSettingsRepository.get(this).language, mathMode)
        }
        if (blockIndex == null) return
        readingTextPages[narrationPageIdentity()]?.register(blockIndex, view)
    }

    /** A result keeps its native ink and actions while the current spoken review follows playback. */
    private fun exerciseFeedbackText(blockIndex: Int): ReadingTextView {
        val identity = narrationPageIdentity()
        var previousText = ""
        return object : ReadingTextView(this) {
            override fun onTextChanged(content: CharSequence?, start: Int, lengthBefore: Int, lengthAfter: Int) {
                super.onTextChanged(content, start, lengthBefore, lengthAfter)
                val value = content?.toString().orEmpty()
                if (value == previousText) return
                previousText = value
                readingTextPages[identity]?.unregister(this)
                post {
                    if (identity != narrationPageIdentity() || isDestroyed || !isAttachedToWindow) return@post
                    updatePuzzleText(this, blockIndex,
                        if (text.isBlank()) MathPuzzleRenderer.TextRole.CLEARED
                        else if (currentTextColor == Color.rgb(180, 220, 190)) MathPuzzleRenderer.TextRole.CORRECT_FEEDBACK
                        else MathPuzzleRenderer.TextRole.INCORRECT_FEEDBACK)
                }
            }
        }
    }

    private fun updatePuzzleText(view: TextView, blockIndex: Int, role: MathPuzzleRenderer.TextRole) {
        val page = readingTextPages[narrationPageIdentity()] ?: return
        if (role == MathPuzzleRenderer.TextRole.BODY) {
            registerReadingText(view, blockIndex)
            return
        }
        page.unregister(view)
        if (role != MathPuzzleRenderer.TextRole.CLEARED) {
            LessonTechnicalText.apply(view,
                currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS,
                NarrationSettingsRepository.get(this).language)
        }
        if (role == MathPuzzleRenderer.TextRole.CORRECT_FEEDBACK || role == MathPuzzleRenderer.TextRole.INCORRECT_FEEDBACK) {
            page.register(blockIndex, view, feedback = true,
                incorrect = role == MathPuzzleRenderer.TextRole.INCORRECT_FEEDBACK)
        }
        bindIncorrectReadingPresentation()
        // Presentation mappings can grow when feedback appears, retaining the same real
        // playback intervals and alignments. This neither restarts nor synthesizes audio.
        narrationScript(NarrationSettingsRepository.get(this).language)
    }

    private fun prepareReadingPage(script: LessonNarrationScript, language: NarrationLanguage) {
        val selected = NarrationSettingsRepository.forLanguage(this, language)
        val plan = script.readingPlan(selected.engine)
        if (plan == null) {
            readingProgress.forget(narrationPageIdentity())
            readingTextPages[narrationPageIdentity()]?.restore()
            return
        }
        val parts = if (selected.engine == NarrationEngine.ANDROID) NarrationSegments.prepare(script.texts, 250)
            else NarrationSegments.sentences(script.texts)
        val key = NarrationResumePolicy.key(narrationPageIdentity(), language.tag, selected.engine.name,
            if (selected.engine == NarrationEngine.ANDROID) selected.androidVoice.orEmpty() else selected.kokoroVoice, parts)
        readingProgress.bind(narrationPageIdentity(), plan, key,
            readingTextPages[narrationPageIdentity()]?.wordMappings(parts, plan.origins, language).orEmpty())
        applyReadingPresentation()
    }

    private fun readingPlaybackChanged(progress: NarrationPlaybackProgress) {
        if (relocatingReadingPresentation) return
        if (progress.feedbackToken != null) {
            val review = incorrectReadingPresentation ?: return
            if (progress.pageIdentity != review.pageIdentity || progress.feedbackBlockIndex != review.blockIndex ||
                (review.token != null && review.token != progress.feedbackToken)) return
            if (progress.phase == com.learngraphicsprogramming.app.narration.NarrationPlaybackPhase.STOPPED) {
                // Replacing a review may dismiss its old token after the UI has already
                // supplied the next visible transcript for this same puzzle.
                if (review.token != progress.feedbackToken) return
                review.identity?.let(readingProgress::forget)
                incorrectReadingPresentation = null
            } else {
                review.token = progress.feedbackToken
                review.identity = "${review.pageIdentity}:incorrect:${progress.feedbackToken}"
                review.playbackKey = progress.playbackKey
                bindIncorrectReadingPresentation()
                readingProgress.update(progress.copy(pageIdentity = requireNotNull(review.identity)))
            }
            if (::lessonJson.isInitialized && progress.pageIdentity == narrationPageIdentity()) applyReadingPresentation()
            return
        }
        readingProgress.update(progress)
        if (::lessonJson.isInitialized && progress.pageIdentity == narrationPageIdentity()) applyReadingPresentation()
    }

    private fun bindIncorrectReadingPresentation() {
        val review = incorrectReadingPresentation ?: return
        val identity = review.identity ?: return
        val plan = review.script.readingPlan(review.engine) ?: return
        val parts = if (review.engine == NarrationEngine.ANDROID) NarrationSegments.prepare(review.script.texts, 250)
            else NarrationSegments.sentences(review.script.texts)
        readingProgress.bind(identity, plan, review.playbackKey,
            readingTextPages[review.pageIdentity]?.wordMappings(parts, plan.origins, review.language,
                incorrectFeedback = true).orEmpty())
    }

    private fun applyReadingPresentation() {
        if (!::lessonJson.isInitialized) return
        val identity = narrationPageIdentity()
        val reviewIdentity = incorrectReadingPresentation?.takeIf { it.pageIdentity == identity }?.identity
        readingTextPages[identity]?.show(readingProgress.states(identity),
            NarrationSettingsRepository.get(this).enabled, readingProgress.unreadWords(identity) +
                (reviewIdentity?.let(readingProgress::unreadWords).orEmpty()),
            readingProgress.presentationCompletedSegments(identity),
            reviewIdentity?.let(readingProgress::presentationCompletedSegments).orEmpty())
    }

    private fun clearReadingPages() {
        readingTextPages.values.forEach { it.clear() }
        readingTextPages.clear()
        mathNarrationSources.clear()
        readingPresentations.clear()
        deferredReadingPresentation = false
        incorrectReadingPresentation = null
        readingProgress.clear()
        readingRenderBlockIndex = null
    }

    private fun narrationCorrectBlocks(): Set<Int> =
        narrationExerciseObserver?.watch(narrationPageIdentity(),currentLessonId,lessonJson,currentPage) ?: emptySet()


    private fun narrationPageIdentity(): String =
        "$currentRouteId/$currentLessonId/$currentPage"

    /** Identical text for page prefetch, Play and Restart. */
    private fun pageNarrationText(
        language: com.learngraphicsprogramming.app.narration.NarrationLanguage
    ): List<String> = LessonNarrationText.page(lessonJson, currentPage, language) { blockType ->
        NarrationUiInstructions.forBlock(this, blockType)
    }

    private fun prefetchVisibleNarration() {
        if (!::lessonJson.isInitialized) return
        com.learngraphicsprogramming.app.narration.NarrationAudioPlanner.enter(
            this, currentRouteId, currentCourseId, currentLessonId, currentPage)
        if (!NarrationSettingsRepository.get(this).enabled) {
            applyReadingPresentation()
            return
        }
        val language = com.learngraphicsprogramming.app.narration.NarrationLanguage.forLesson(
            lessonJson, currentPage, NarrationSettingsRepository.get(this).language
        )
        val script = narrationScript(language)
        narrationController?.prefetchPage(
            script.texts,
            language,
            pageIdentity = narrationPageIdentity(),
            feedbackGates = script.gates,
            correctBlocks = narrationCorrectBlocks()
        )
        val pageQueue = com.learngraphicsprogramming.app.narration.KokoroPagePreparationQueue.snapshot(this)
        val voice = NarrationSettingsRepository.forLanguage(this, language).kokoroVoice
        val expected = NarrationSegments.sentences(script.texts).map {
            com.learngraphicsprogramming.app.narration.KokoroNarrationCache.key(language.tag, voice, it)
        }.distinct()
        val pageRevision = pageQueue?.takeIf {
            it.sentences.map(com.learngraphicsprogramming.app.narration.NarrationPageQueue::hash).distinct() == expected
        }?.revision
        com.learngraphicsprogramming.app.narration.NarrationAudioPlanner.enter(
            this, currentRouteId, currentCourseId, currentLessonId, currentPage, pageRevision)
    }

    private fun setupLessonNarration() {
        val playbackOwner = Any()
        readingPlaybackOwner = playbackOwner
        readingProgress.newController()
        narrationExerciseObserver?.close()
        narrationExerciseObserver = com.learngraphicsprogramming.app.narration.LessonNarrationExerciseState(this) { identity,correct ->
            narrationController?.updateSolvedExercises(identity,correct)
        }
        narrationController = LessonNarrationController(
            this,
            onState = { playing ->
                narrationControls?.refresh(playing)
                if (!playing && deferredReadingPresentation) lessonContent.post {
                    if (deferredReadingPresentation && !isDestroyed && ::lessonJson.isInitialized) {
                        narrationScript(NarrationSettingsRepository.get(this).language)
                    }
                }
            },
            onError = { message -> Toast.makeText(this, message, Toast.LENGTH_LONG).show() },
            onNotice = { message -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show() },
            onProgress = { progress ->
                if (readingPlaybackOwner === playbackOwner) readingPlaybackChanged(progress)
            },
            trackWords = true
        )
        narrationControls = LessonNarrationControls(
            this,
            onListen = {
                if (::lessonJson.isInitialized) {
                    val language = com.learngraphicsprogramming.app.narration.NarrationLanguage.forLesson(
                        lessonJson, currentPage, NarrationSettingsRepository.get(this).language)
                    val script = narrationScript(language)
                    narrationController?.toggle(
                        script.texts,
                        language,
                        pageIdentity = narrationPageIdentity(),
                        feedbackGates = script.gates,
                        correctBlocks = narrationCorrectBlocks()
                    )
                }
            },
            onRateChanged = { narrationController?.settingsChanged() },
            onSeek = { seconds -> narrationController?.seekBy(seconds) },
            onRestart = {
                if (::lessonJson.isInitialized) {
                    val language = com.learngraphicsprogramming.app.narration.NarrationLanguage.forLesson(
                        lessonJson, currentPage, NarrationSettingsRepository.get(this).language)
                    val script = narrationScript(language)
                    narrationController?.toggle(
                        script.texts,
                        language,
                        pageIdentity = narrationPageIdentity(),
                        feedbackGates = script.gates,
                        correctBlocks = narrationCorrectBlocks(),
                        restartFromBeginning = true
                    )
                }
            }
        )
        updateNarrationVisibility()
    }

    private fun updateNarrationVisibility() {
        applyReadingPresentation()
        val controlsVisible = NarrationSettingsRepository.controlsVisible(this)
        narrationControls?.setVisible(controlsVisible && !keyboardVisible && programmingImeAnimations.isEmpty() &&
            mathKeyboardHost?.isKeyboardVisible != true && !compilerFullscreenActive &&
            !exerciseFullscreenActive && hasWindowFocus())
        updateNarrationToggleAppearance()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        updateNarrationVisibility()
    }

    private var touchStartX = 0f
    private var touchStartY = 0f

    private val attemptIndicatorRefreshRunnable =
        object : Runnable {

            override fun run() {

                refreshAttemptIndicator()

                findViewById<View>(
                    R.id.attemptIndicator
                ).postDelayed(
                    this,
                    1_000L
                )
            }
        }

    override fun onStop() {
        floatingInteractions.dismissPopup()
        val power = getSystemService(android.os.PowerManager::class.java)
        val keyguard = getSystemService(android.app.KeyguardManager::class.java)
        if (!isChangingConfigurations && power?.isInteractive == true &&
            keyguard?.isKeyguardLocked != true) {
            // Leaving the lesson keeps only bookmarks, not an Activity's audio thread/player.
            narrationController?.pauseKeepingPosition()
            narrationController?.close()
            narrationController = null
            narrationExerciseObserver?.close()
            narrationExerciseObserver = null
            narrationControls?.close()
            narrationControls = null
        } else narrationController?.checkpointPlayback()
        super.onStop()
    }

    override fun onDestroy() {
        narrationUiPreferences.unregisterOnSharedPreferenceChangeListener(narrationUiSettingsListener)
        readingPlaybackOwner = null
        clearReadingPages()
        floatingInteractions.close()
        narrationExerciseObserver?.close()
        narrationExerciseObserver = null

        disposeCppEditorViewports()
        narrationController?.close()
        narrationControls?.close()

        attemptDetailsPopup?.dismiss()

        mathKeyboardHost?.close()

        super.onDestroy()
    }

    private fun disposeCppEditorViewports() {
        cppEditorViewportControllers.forEach { it.dispose() }
        cppEditorViewportControllers.clear()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        mathKeyboardHost?.onConfigurationChanged()
        updateProgrammingKeyboardBarVisibility()
        updateNarrationVisibility()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContentView(R.layout.activity_lesson)
        floatingInteractions.install()

        /*
         * Atrás del sistema (botón o deslizamiento desde el borde).
         *
         * Ambas rutas siguen la misma jerarquía:
         * 1. Cerrar el fullscreen genérico sin destruir sus Views.
         * 2. Salir del fullscreen del compilador usando su salida normal.
         * 3. Fuera de fullscreen, delegar la navegación a Android.
         *
         * No interpretamos gestos táctiles de forma manual. Así conservamos
         * los arrastres del editor y el swipe entre páginas de la lección.
         */
        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    if (exerciseFullscreenActive) {
                        closeExerciseFullscreenHost()
                        return
                    }

                    if (compilerFullscreenActive) {
                        compilerFullscreenBackExit?.invoke()
                        return
                    }

                    // Igual que el botón Volver de la lección.
                    // Evitamos recursión: Android puede enviar una Activity
                    // raíz al fondo sin marcarla como isFinishing.
                    finish()
                }
            }
        )

        val buttonBack =
            findViewById<TextView>(R.id.buttonBack)

        val buttonLessonSettings =
            findViewById<ImageButton>(
                R.id.buttonLessonSettings
            )

        imageAttemptRoute =
            findViewById(
                R.id.imageAttemptRoute
            )

        imageAttemptFill =
            findViewById(
                R.id.imageAttemptFill
            )

        attemptFillClip =
            findViewById(
                R.id.attemptFillClip
            )

        textAttemptCount =
            findViewById(
                R.id.textAttemptCount
            )

        textAttemptPercent =
            findViewById(
                R.id.textAttemptPercent
            )

        attemptIndicator =
            findViewById(
                R.id.attemptIndicator
            )

        attemptIndicator.setOnClickListener {

            toggleAttemptDetails()
        }

        val buttonContinue =
            findViewById<Button>(R.id.buttonContinue)
        lessonContinueButton = buttonContinue

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

        buttonNarrationToggle =
            findViewById(R.id.buttonNarrationToggle)
        narrationUiPreferences.registerOnSharedPreferenceChangeListener(narrationUiSettingsListener)

        buttonNarrationToggle.setOnClickListener {
            val enabled = NarrationSettingsRepository.get(this).enabled
            if (!enabled) return@setOnClickListener
            val showControls = !NarrationSettingsRepository.controlsVisible(this) ||
                narrationControls?.isOutsideViewport() == true
            NarrationSettingsRepository.saveControlsVisible(this, showControls)
            if (showControls) {
                // An unchanged page keeps its segments and saved playback position.
                // Showing controls alone never starts or restarts playback.
                prefetchVisibleNarration()
            } else {
                narrationController?.pauseKeepingPosition()
            }
            updateNarrationVisibility()
            if (showControls) narrationControls?.restoreVisiblePosition()
        }

        // La navegación de páginas sigue utilizando exactamente la misma
        // preferencia; ahora se cambia desde los ajustes de la lección.
        swipeNavigationEnabled =
            getSharedPreferences("app_settings", MODE_PRIVATE)
                .getBoolean("swipeNavigationEnabled", true)

        currentCourseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        currentRouteId =
            intent.getStringExtra(
                "routeId"
            ) ?: AppearanceSettingsRepository.ROUTE_PROGRAMMING

        refreshAttemptIndicator()

        updateNarrationToggleAppearance()
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            lessonContent.setPadding(0, dp(24), 0, dp(16))
            mathKeyboardHost = MathKeyboardHost.install(this)
        } else {
            setupProgrammingViewportInsets()
        }

        currentLessonId =
            intent.getStringExtra(
                "lessonId"
            ) ?: return

        setupLessonNarration()

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
            currentLessonId,
            currentRouteId
        )

        textTopLesson.text =
            getString(R.string.nav_lesson_number, lessonNumber)

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
                "routeId",
                currentRouteId
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
            if (LessonPageAdvancePolicy.canAdvance(requiredExercises, completedExercises)) {
                tryGoNextPage()
            }
        }
        updateContinueButtonState()

        loadLesson(currentLessonId)
        savedInstanceState?.getString(ATTEMPT_HISTORY_STATE)?.let { saved ->
            runCatching { exerciseAttempts.restore(JSONObject(saved)) }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        narrationController?.checkpointPlayback()
        outState.putString(ATTEMPT_HISTORY_STATE, exerciseAttempts.save().toString())
        super.onSaveInstanceState(outState)
    }

    override fun onResume() {

        super.onResume()

        // Los ajustes de la lección pueden modificar esta preferencia
        // mientras LessonActivity está pausada.
        swipeNavigationEnabled =
            getSharedPreferences("app_settings", MODE_PRIVATE)
                .getBoolean("swipeNavigationEnabled", true)

        if (narrationController == null) setupLessonNarration()
        narrationController?.settingsChanged()
        // The page cache is consulted before loading the large ONNX model.
        // A cached lesson starts promptly without an unnecessary warm-up.
        prefetchVisibleNarration()
        narrationControls?.refresh(narrationController?.isPlaying == true)
        updateNarrationVisibility()

        AppearanceUi.applyRoute(
            this,
            currentRouteId
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
                currentLessonId,
                currentRouteId
            )
        }

        refreshAttemptIndicator()

        findViewById<View>(
            R.id.attemptIndicator
        ).removeCallbacks(
            attemptIndicatorRefreshRunnable
        )

        findViewById<View>(
            R.id.attemptIndicator
        ).postDelayed(
            attemptIndicatorRefreshRunnable,
            1_000L
        )
    }

    private fun refreshAttemptIndicator() {

        val attemptState =
            AttemptRepository
                .getState(
                    this,
                    currentRouteId
                )

        /*
         * PREMIUM:
         *
         * Conservamos el corazón y el símbolo ∞,
         * pero ocultamos el porcentaje de
         * recuperación y desactivamos su panel.
         */
        if (
            attemptState.unlimited
        ) {

            textAttemptPercent.visibility =
                View.GONE

            attemptIndicator.isClickable =
                false

            attemptDetailsPopup?.dismiss()

        } else {

            textAttemptPercent.visibility =
                View.VISIBLE

            attemptIndicator.isClickable =
                true
        }

        val attemptColor =
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    this,
                    currentRouteId
                )

        val iconResource =
            if (
                currentRouteId ==
                AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
            ) {
                R.drawable.ic_attempt_math
            } else {
                R.drawable.ic_attempt_programming
            }

        imageAttemptRoute.setImageResource(
            iconResource
        )

        imageAttemptFill.setImageResource(
            iconResource
        )

        imageAttemptRoute.setColorFilter(
            Color.WHITE
        )

        imageAttemptFill.setColorFilter(
            attemptColor
        )

        val fillProgress =
            if (
                attemptState.unlimited
            ) {
                1f
            } else {
                attemptState.totalReserveProgress
            }

        val fullSize =
            dp(
                43
            )

        val fillWidth =
            (
                    fullSize *
                            fillProgress
                    )
                .toInt()
                .coerceIn(
                    0,
                    fullSize
                )

        attemptFillClip.layoutParams =
            attemptFillClip.layoutParams
                .apply {
                    width =
                        fullSize

                    height =
                        fullSize
                }

        imageAttemptFill.clipBounds =
            Rect(
                0,
                0,
                fillWidth,
                fullSize
            )

        val premiumGold =
            Color.rgb(
                212,
                175,
                55
            )

        imageAttemptRoute.contentDescription =
            if (
                attemptState.unlimited
            ) {
                getString(R.string.exercise_unlimited_attempts)
            } else {
                getString(R.string.nav_available_attempts, attemptState.available, attemptState.maximum)
            }

        /*
         * Premium necesita más espacio para
         * mostrar el infinito a 32sp.
         *
         * Gratis conserva sus dimensiones
         * y posición originales.
         */
        textAttemptCount.layoutParams =
            textAttemptCount.layoutParams.apply {

                width =
                    if (
                        attemptState.unlimited
                    ) {
                        dp(48)
                    } else {
                        dp(22)
                    }
            }

        textAttemptCount.translationX =
            if (
                attemptState.unlimited
            ) {
                dp(18).toFloat()
            } else {
                dp(12).toFloat()
            }

        textAttemptCount.text =
            if (
                attemptState.unlimited
            ) {
                getString(R.string.lesson_infinity_symbol)
            } else {
                getString(R.string.lesson_integer, attemptState.available)
            }

        if (
            attemptState.unlimited
        ) {

            textAttemptCount.setTextColor(
                premiumGold
            )

            textAttemptCount.setTextSize(
                TypedValue.COMPLEX_UNIT_SP,
                32f
            )

            textAttemptCount.setTypeface(
                null,
                Typeface.BOLD
            )

        } else {

            textAttemptCount.setTextColor(
                attemptColor
            )

            textAttemptCount.setTextSize(
                TypedValue.COMPLEX_UNIT_SP,
                15f
            )

            textAttemptCount.setTypeface(
                null,
                Typeface.BOLD
            )
        }

        /*
         * INDICADOR PREMIUM.
         *
         * Gratis: muestra el contador numérico.
         * Premium: muestra el icono de infinito
         * con degradado dorado definido en XML.
         */
        val imageAttemptInfinity =
            findViewById<ImageView>(
                R.id.imageAttemptInfinity
            )

        if (
            attemptState.unlimited
        ) {

            textAttemptCount.visibility =
                View.GONE

            imageAttemptInfinity.visibility =
                View.VISIBLE

        } else {

            textAttemptCount.visibility =
                View.VISIBLE

            imageAttemptInfinity.visibility =
                View.GONE

            textAttemptCount.text =
                getString(R.string.lesson_integer, attemptState.available)
        }

        val percentage =
            (
                    fillProgress *
                            100f
                    )
                .toInt()
                .coerceIn(
                    0,
                    100
                )

        textAttemptPercent.text =
            getString(R.string.lesson_percentage, percentage)

        textAttemptPercent.setTextColor(
            attemptColor
        )

        // The count and percentage are inside the 80%-scaled indicator.
        // The countdown lives outside it: copy their real, visible style.
        val remaining = findViewById<TextView>(R.id.textAttemptRemaining)
        val visibleSizePx = textAttemptPercent.textSize * attemptIndicator.scaleY
        if (kotlin.math.abs(remaining.textSize - visibleSizePx) > 0.1f) {
            remaining.setTextSize(TypedValue.COMPLEX_UNIT_PX, visibleSizePx)
        }
        if (remaining.currentTextColor != textAttemptPercent.currentTextColor) {
            remaining.setTextColor(textAttemptPercent.currentTextColor)
        }
        if (remaining.typeface != textAttemptPercent.typeface) {
            remaining.typeface = textAttemptPercent.typeface
        }

        val label = AttemptRecoveryLabel.format(attemptState.maximum,attemptState.reserveUnits,
            attemptState.unlimited,AttemptRepository.RECOVERY_MILLIS)
        if (remaining.text.toString() != label) remaining.text = label
        remaining.visibility = if (label.isEmpty()) View.INVISIBLE else View.VISIBLE
        refreshAttemptDetails()
    }

    private fun canEvaluateAttempt(): Boolean {

        val state =
            AttemptRepository.getState(
                this,
                currentRouteId
            )

        if (
            state.canEvaluate
        ) {
            return true
        }

        Toast.makeText(
            this,
            getString(R.string.nav_no_attempts),
            Toast.LENGTH_SHORT
        ).show()

        refreshAttemptIndicator()

        return false
    }

    private fun registerEvaluatedResult(
        correct: Boolean
    ) {

        if (
            correct
        ) {
            return
        }

        AttemptRepository.consumeIncorrect(
            this,
            currentRouteId
        )

        refreshAttemptIndicator()
    }

    private fun <T : Any> evaluateExerciseAttempt(
        pageIndex: Int,
        blockIndex: Int,
        answer: Any,
        evaluation: () -> T,
        verdict: (T) -> Boolean?
    ): T? = try {
        exerciseAttempts.evaluate(
            ExerciseAttemptKey(currentRouteId, currentLessonId, pageIndex, blockIndex),
            ExerciseAnswerIdentity.of(answer),
            canEvaluate = ::canEvaluateAttempt,
            evaluation = evaluation,
            // Premium nunca genera una penalización de intentos.
            verdict = { evaluated -> if (PremiumRepository.isPremium(this)) null else verdict(evaluated) },
            consumeIncorrect = { registerEvaluatedResult(false) }
        )
    } catch (exception: Exception) {
        android.util.Log.w("LessonAttempts", "Could not evaluate an exercise", exception)
        Toast.makeText(this, R.string.attempt_evaluation_error, Toast.LENGTH_SHORT).show()
        null
    }

    private fun showIncompleteAttempt() {
        Toast.makeText(this, R.string.attempt_response_incomplete, Toast.LENGTH_SHORT).show()
    }

    private fun toggleAttemptDetails() {

        /*
         * Premium no necesita mostrar
         * información de recuperación.
         */
        if (
            PremiumRepository.isPremium(this)
        ) {

            attemptDetailsPopup?.dismiss()

            return
        }

        val currentPopup =
            attemptDetailsPopup

        if (
            currentPopup != null &&
            currentPopup.isShowing
        ) {

            currentPopup.dismiss()

            return
        }

        val scroll =
            HorizontalScrollView(
                this
            )

        scroll.isHorizontalScrollBarEnabled =
            false

        scroll.isFillViewport =
            true

        scroll.overScrollMode =
            View.OVER_SCROLL_NEVER

        scroll.setBackgroundColor(
            Color.rgb(
                24,
                26,
                31
            )
        )

        val content =
            LinearLayout(
                this
            )

        content.orientation =
            LinearLayout.HORIZONTAL

        content.gravity =
            Gravity.CENTER

        val verticalPadding =
            (
                    0.24f *
                            resources.displayMetrics.density +
                            0.24f
                    )
                .toInt()

        content.setPadding(
            0,
            verticalPadding,
            0,
            verticalPadding
        )

        scroll.addView(
            content,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.MATCH_PARENT,
                Gravity.START or
                        Gravity.CENTER_VERTICAL
            )
        )

        scroll.post {

            content.minimumWidth =
                scroll.width
        }

        attemptDetailsContent =
            content

        val popup =
            android.widget.PopupWindow(
                scroll,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                dp(43),
                false
            )

        popup.setBackgroundDrawable(
            android.graphics.drawable.ColorDrawable(
                Color.TRANSPARENT
            )
        )

        popup.isOutsideTouchable =
            false
        if (android.os.Build.VERSION.SDK_INT >= 29) popup.isTouchModal = false

        popup.elevation =
            dp(8).toFloat()

        popup.setOnDismissListener {
            floatingInteractions.dismissed(popup)

            attemptDetailsPopup =
                null

            attemptDetailsContent =
                null
        }

        attemptDetailsPopup =
            popup

        refreshAttemptDetails()

        popup.showAsDropDown(
            findViewById(
                R.id.lessonHeader
            )
        )
        floatingInteractions.shown(popup)
    }

    private fun refreshAttemptDetails() {

        val content =
            attemptDetailsContent
                ?: return

        val state =
            AttemptRepository.getState(
                this,
                currentRouteId
            )

        val attemptColor =
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    this,
                    currentRouteId
                )

        val iconResource =
            if (
                currentRouteId ==
                AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
            ) {
                R.drawable.ic_attempt_math
            } else {
                R.drawable.ic_attempt_programming
            }

        content.removeAllViews()

        for (
        index in
        0 until state.maximum
        ) {

            val progress =
                if (
                    state.unlimited
                ) {
                    1f
                } else {
                    (
                            state.reserveUnits -
                                    index.toFloat()
                            )
                        .coerceIn(
                            0f,
                            1f
                        )
                }

            val slot =
                FrameLayout(
                    this
                )

            val slotParams =
                LinearLayout.LayoutParams(
                    dp(43),
                    dp(43)
                )

            slotParams.marginEnd =
                (
                        0.24f *
                                resources.displayMetrics.density +
                                0.24f
                        )
                    .toInt()

            slot.layoutParams =
                slotParams

            val base =
                ImageView(
                    this
                )

            base.setImageResource(
                iconResource
            )

            base.setColorFilter(
                Color.WHITE
            )

            slot.addView(
                base,
                FrameLayout.LayoutParams(
                    dp(43),
                    dp(43),
                    Gravity.CENTER
                )
            )

            val fillWidth =
                (
                        dp(43) *
                                progress
                        )
                    .toInt()
                    .coerceIn(
                        0,
                        dp(43)
                    )

            val fillClip =
                FrameLayout(
                    this
                )

            fillClip.clipChildren =
                true

            fillClip.clipBounds =
                Rect(
                    0,
                    0,
                    fillWidth,
                    dp(43)
                )

            slot.addView(
                fillClip,
                FrameLayout.LayoutParams(
                    dp(43),
                    dp(43),
                    Gravity.CENTER
                )
            )

            val fill =
                ImageView(
                    this
                )

            fill.setImageResource(
                iconResource
            )

            fill.setColorFilter(
                attemptColor
            )

            fillClip.addView(
                fill,
                FrameLayout.LayoutParams(
                    dp(43),
                    dp(43),
                    Gravity.CENTER
                )
            )

            slot.contentDescription =
                if (
                    progress >=
                    1f
                ) {
                    getString(R.string.exercise_attempt_available)
                } else {
                    getString(R.string.exercise_attempt_recharge, (progress * 100f).toInt())
                }

            content.addView(
                slot
            )
        }
    }

    private fun setupProgrammingKeyboardBar() {

        normalProgrammingBarHeight = programmingKeyboardBar.layoutParams.height
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

                activeCodeInput?.let { input ->
                    programmingInputPaddings.putIfAbsent(input, Rect(input.paddingLeft, input.paddingTop,
                        input.paddingRight, input.paddingBottom))
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
                updateCodeSurfaceWidths()
            }

        lessonContent.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            updateCodeSurfaceWidths()
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

        programmingKeySizes.clear()
        programmingKeyboardBarContent
            .removeAllViews()

        /*
         * TAB siempre es fijo.
         */
        addProgrammingShortcutButton(
            label = getString(R.string.lesson_tab_label),
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
                label == getString(R.string.lesson_tab_label)
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
                    currentRouteId
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

        programmingKeySizes[button] = ProgrammingKeySize(params.height, button.minimumHeight,
            button.paddingTop, button.paddingBottom)

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

        if (text == "\t") { CppCodeInputRules.insertTab(input); return }
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
        // Focus can clear after ActivityThread has removed the old decor during recreation.
        if (isDestroyed || isFinishing) return
        val lessonHeader = findViewById<View>(R.id.lessonHeader) ?: return
        updateNarrationVisibility()
        if (!::programmingKeyboardBar.isInitialized) return
        val shouldShow =
            (keyboardVisible || programmingImeAnimations.isNotEmpty()) &&
                    activeCodeInput
                        ?.hasFocus() ==
                    true

        programmingKeyboardBar.visibility =
            if (
                shouldShow
            ) {
                View.VISIBLE
            } else {
                View.GONE
            }

        if (currentRouteId != AppearanceSettingsRepository.ROUTE_PROGRAMMING) return

        updateProgrammingFullscreenResize()

        val compact =
            isCompactProgrammingFullscreen() &&
                    shouldShow

        /*
         * Las TECLAS se compactan siempre que:
         *
         * - estamos en horizontal
         * - el IME está mostrando esta barra
         *
         * No exigimos fullscreen para reducir
         * solamente la geometría de las teclas.
         */
        val compactKeys =
            resources.configuration.orientation ==
                    Configuration.ORIENTATION_LANDSCAPE &&
                    shouldShow

        val available =
            programmingAvailableViewport()

        /*
         * Altura responsiva de la barra.
         *
         * 70% de su tamaño normal,
         * pero nunca más de la mitad
         * del viewport real disponible.
         */
        /*
         * La barra debe tener exactamente la altura
         * de su tecla compacta más alta.
         *
         * Si usamos el 70% de la barra normal:
         *
         * barra: 52 -> 36
         * tecla:  40 -> 28
         *
         * quedarían 8 px/dp equivalentes de aire.
         *
         * Calculamos por tanto desde las TECLAS.
         */
        val responsiveKeyHeight =
            programmingKeySizes
                .values
                .map {
                    (
                            it.height *
                                    0.70f
                            )
                        .toInt()
                        .coerceAtLeast(1)
                }
                .maxOrNull()
                ?: normalProgrammingBarHeight

        val responsiveBarHeight =
            minOf(
                responsiveKeyHeight,

                (
                        available.height() /
                                2
                        )
                    .coerceAtLeast(1)
            )

        val barHeight =
            if (compactKeys) {
                responsiveBarHeight
            } else {
                normalProgrammingBarHeight
            }

        if (
            programmingKeyboardBar
                .layoutParams
                .height !=
            barHeight
        ) {

            programmingKeyboardBar.layoutParams =
                programmingKeyboardBar
                    .layoutParams
                    .apply {
                        height =
                            barHeight
                    }
        }

        programmingKeySizes
            .forEach {
                    (button, normal) ->

                /*
                 * 70% de SU tamaño normal real.
                 *
                 * No usamos dp absolutos.
                 */
                val responsiveKeyHeight =
                    minOf(
                        (
                                normal.height *
                                        0.70f
                                )
                            .toInt()
                            .coerceAtLeast(1),

                        barHeight
                    )

                val keyHeight =
                    if (compactKeys) {
                        responsiveKeyHeight
                    } else {
                        normal.height
                    }

                if (
                    button.layoutParams.height !=
                    keyHeight
                ) {

                    button.layoutParams =
                        button.layoutParams
                            .apply {
                                height =
                                    keyHeight
                            }
                }

                val minimumHeight =
                    if (compactKeys) {
                        0
                    } else {
                        normal.minimumHeight
                    }

                if (
                    button.minimumHeight !=
                    minimumHeight
                ) {
                    button.minimumHeight =
                        minimumHeight
                }

                /*
                 * Horizontal:
                 *
                 * padding vertical = 0
                 *
                 * padding horizontal permanece
                 * EXACTAMENTE como estaba.
                 */
                val top =
                    if (compactKeys) {
                        0
                    } else {
                        normal.top
                    }

                val bottom =
                    if (compactKeys) {
                        0
                    } else {
                        normal.bottom
                    }

                if (
                    button.paddingTop != top ||
                    button.paddingBottom != bottom
                ) {

                    button.setPadding(
                        button.paddingLeft,
                        top,
                        button.paddingRight,
                        bottom
                    )
                }
            }

        // Configuration can change before the resized content receives its new height.
        // Restoring the header into the old landscape viewport would clear focus again.
        if (lessonHeader.visibility == View.VISIBLE && lessonHeader.measuredHeight > 0) {
            programmingLessonHeaderHeight = lessonHeader.measuredHeight
        }
        val landscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val imeOccupiesViewport = keyboardVisible || programmingImeAnimations.isNotEmpty()
        val actualContentHeight = (lessonScroll.parent as? View)?.height ?: available.height()
        val readyHeight = minOf(available.height(), actualContentHeight)
        val editableLineHeight = activeCodeInput?.let { input ->
            maxOf(input.lineHeight, programmingTextSizes[input]?.lineHeight ?: input.lineHeight) +
                input.compoundPaddingTop + input.compoundPaddingBottom
        } ?: 1
        val headerFitsPortrait = readyHeight >= programmingLessonHeaderHeight + barHeight + editableLineHeight
        val hideLessonHeader = compilerFullscreenActive ||
            (imeOccupiesViewport && (landscape || (shouldShow && !headerFitsPortrait)))
        lessonHeader.visibility = if (hideLessonHeader) View.GONE else View.VISIBLE
        val hideFullscreenHeader = compact ||
            (!landscape && imeOccupiesViewport && shouldShow && !headerFitsPortrait)
        programmingFullscreenHeader?.visibility = if (hideFullscreenHeader) View.GONE else View.VISIBLE
        programmingFullscreenFloating?.visibility = if (shouldShow) View.VISIBLE else View.GONE
        programmingFullscreenPaddings.forEach { (view, padding) ->
            val top = if (compact) 0 else padding.top
            val bottom = if (compact) 0 else padding.bottom
            if (view.paddingTop != top || view.paddingBottom != bottom) {
                view.setPadding(padding.left, top, padding.right, bottom)
            }
        }
        programmingFullscreenMargins.forEach { (view, margins) ->
            val params = view.layoutParams as? ViewGroup.MarginLayoutParams ?: return@forEach
            val left = if (landscape) 0 else margins.left
            val right = if (landscape) 0 else margins.right
            val top = if (compact) 0 else margins.top
            val bottom = if (compact) 0 else margins.bottom
            if (params.leftMargin != left || params.rightMargin != right ||
                params.topMargin != top || params.bottomMargin != bottom) {
                params.setMargins(left, top, right, bottom)
                view.layoutParams = params
            }
        }
        programmingInputPaddings.forEach { (input, padding) ->
            // The compiler controller owns its editor and line-number padding together.
            if (programmingCodeTextCompanions.containsKey(input)) return@forEach
            val removeVerticalPadding = landscape && shouldShow && input === activeCodeInput
            val top = if (removeVerticalPadding) 0 else padding.top
            val bottom = if (removeVerticalPadding) 0 else padding.bottom
            if (input.paddingTop != top || input.paddingBottom != bottom) {
                input.setPadding(padding.left, top, padding.right, bottom)
            }
        }
        programmingViewportUiControllers.forEach { it() }
        updateProgrammingCodeTextSize(available, shouldShow)
        updateCodeSurfaceWidths()
        programmingKeyboardBar.post { revealProgrammingCaretForViewport() }
    }

    private fun isCompactProgrammingFullscreen(): Boolean =
        resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
            (keyboardVisible || programmingImeAnimations.isNotEmpty()) &&
            (compilerFullscreenActive || (exerciseFullscreenActive && exerciseFullscreenUsesKeyboard))

    private fun updateProgrammingFullscreenResize() {
        val fullscreen = compilerFullscreenActive || (exerciseFullscreenActive && exerciseFullscreenUsesKeyboard)
        val resizeForIme = fullscreen && (keyboardVisible || programmingImeAnimations.isNotEmpty())
        val decor = window.decorView
        val fullscreenFlag = android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN
        if (resizeForIme) {
            if (programmingImeFullscreenSystemUi == null) {
                programmingImeFullscreenSystemUi = decor.systemUiVisibility
                programmingImeFullscreenWindowFlag = window.attributes.flags and fullscreenFlag != 0
            }
            // Legacy fullscreen ignores adjustResize. Let the window apply its real
            // IME inset instead of placing the app's shortcut bar behind the keyboard.
            val resizedFlags = decor.systemUiVisibility and View.SYSTEM_UI_FLAG_FULLSCREEN.inv() and
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN.inv()
            if (decor.systemUiVisibility != resizedFlags) decor.systemUiVisibility = resizedFlags
            if (window.attributes.flags and fullscreenFlag != 0) window.clearFlags(fullscreenFlag)
        } else {
            programmingImeFullscreenSystemUi?.let { savedFlags ->
                if (fullscreen) {
                    if (decor.systemUiVisibility != savedFlags) decor.systemUiVisibility = savedFlags
                    if (programmingImeFullscreenWindowFlag) window.addFlags(fullscreenFlag)
                }
                // A real fullscreen exit already restored the normal window flags.
                programmingImeFullscreenSystemUi = null
                programmingImeFullscreenWindowFlag = false
            }
        }
    }

    private fun setupProgrammingViewportInsets() {
        val decor = window.decorView
        fun observe(insets: WindowInsetsCompat?) {
            if (insets != null) {
                programmingWindowInsets = insets
                keyboardVisible = insets.isVisible(WindowInsetsCompat.Type.ime())
            }
            updateProgrammingKeyboardBarVisibility()
        }
        // Observe above AppCompat's optional-fit content, before its legacy resize.
        ViewCompat.setOnApplyWindowInsetsListener(decor) { view, insets ->
            observe(insets)
            ViewCompat.onApplyWindowInsets(view, insets)
        }
        ViewCompat.setWindowInsetsAnimationCallback(decor,
            object : WindowInsetsAnimationCompat.Callback(DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
                override fun onPrepare(animation: WindowInsetsAnimationCompat) {
                    if (animation.typeMask and WindowInsetsCompat.Type.ime() != 0) {
                        programmingImeAnimations.add(animation)
                        updateProgrammingKeyboardBarVisibility()
                    }
                }
                override fun onProgress(insets: WindowInsetsCompat,
                    runningAnimations: MutableList<WindowInsetsAnimationCompat>): WindowInsetsCompat {
                    observe(insets)
                    return insets
                }
                override fun onEnd(animation: WindowInsetsAnimationCompat) {
                    programmingImeAnimations.remove(animation)
                    observe(ViewCompat.getRootWindowInsets(decor))
                }
            })
        ViewCompat.requestApplyInsets(decor)
    }

    private fun programmingAvailableViewport(): Rect {
        val frame = Rect().also { window.decorView.getWindowVisibleDisplayFrame(it) }
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            val viewport = Rect(windowManager.currentWindowMetrics.bounds)
            val insets = programmingWindowInsets ?: ViewCompat.getRootWindowInsets(window.decorView)
            insets?.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout() or
                WindowInsetsCompat.Type.ime())?.let { safe ->
                viewport.set(viewport.left + safe.left, viewport.top + safe.top,
                    viewport.right - safe.right, viewport.bottom - safe.bottom)
            }
            if (frame.width() > 0 && frame.height() > 0) viewport.intersect(frame)
            if (viewport.width() > 0 && viewport.height() > 0) return viewport
        }
        return frame
    }

    private fun revealProgrammingCaretForViewport() {
        val input = activeCodeInput ?: return
        if (!keyboardVisible || !input.hasFocus() || !input.isAttachedToWindow ||
            programmingImeAnimations.isNotEmpty() || input.isLayoutRequested || lessonScroll.isLayoutRequested) return
        val viewport = programmingAvailableViewport()
        val geometry = "${viewport.width()}:${viewport.height()}:${programmingKeyboardBar.height}:" +
            "$compilerFullscreenActive:$exerciseFullscreenActive"
        if (lastProgrammingRevealInput === input && lastProgrammingRevealViewport == geometry) return
        val layout = input.layout ?: return
        val offset = input.selectionEnd.coerceIn(0, input.text.length)
        val line = layout.getLineForOffset(offset)
        val top = layout.getLineTop(line)
        val bottom = layout.getLineBottom(line)
        val textHeight = (input.height - input.totalPaddingTop - input.totalPaddingBottom).coerceAtLeast(1)
        val textWidth = (input.width - input.totalPaddingLeft - input.totalPaddingRight).coerceAtLeast(1)
        val x = layout.getPrimaryHorizontal(offset).toInt()
        var scrollY = input.scrollY
        if (top < scrollY) scrollY = top
        if (bottom > scrollY + textHeight) scrollY = bottom - textHeight
        var scrollX = input.scrollX
        if (x < scrollX) scrollX = x
        if (x >= scrollX + textWidth) scrollX = x - textWidth + 1
        input.scrollTo(scrollX.coerceAtLeast(0), scrollY.coerceAtLeast(0))
        // Reveal the actual endpoint without changing selection or enabling continuous following.
        // Android offsets the child's scroll when propagating this content-coordinate rectangle.
        input.requestRectangleOnScreen(Rect(input.totalPaddingLeft + x,
            input.totalPaddingTop + top, input.totalPaddingLeft + x + 2,
            input.totalPaddingTop + bottom), true)
        lastProgrammingRevealInput = input
        lastProgrammingRevealViewport = geometry
    }

    private fun updateProgrammingCodeTextSize(viewport: Rect, toolbarShown: Boolean) {
        val landscapeIme = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
            (keyboardVisible || programmingImeAnimations.isNotEmpty()) && toolbarShown
        val input = activeCodeInput
        val activeViews = if (landscapeIme && input != null)
            listOfNotNull<TextView>(input, programmingCodeTextCompanions[input]) else emptyList()
        val availableLineHeight = (viewport.height() - programmingKeyboardBar.layoutParams.height).coerceAtLeast(1)
        activeViews.forEach { view ->
            programmingTextSizes.putIfAbsent(view, ProgrammingTextSize(view.textSize, view.lineHeight))
        }
        programmingTextSizes.forEach { (view, normal) ->
            // The normal shortcut bar stays unchanged. Scale only when a complete line
            // physically cannot fit its remaining landscape viewport, and restore in portrait.
            val size = if (view in activeViews && normal.lineHeight > availableLineHeight)
                normal.size * availableLineHeight / normal.lineHeight else normal.size
            if (kotlin.math.abs(view.textSize - size) > 0.1f) view.setTextSize(TypedValue.COMPLEX_UNIT_PX, size)
        }
    }

    private fun captureProgrammingReparentFocus(): ProgrammingReparentFocus? {
        if (currentRouteId != AppearanceSettingsRepository.ROUTE_PROGRAMMING) return null
        val input = (currentFocus as? EditText)?.takeIf { it.tag == CODE_INPUT_TAG && it.hasFocus() }
            ?: return null
        return ProgrammingReparentFocus(input, input.selectionStart, input.selectionEnd,
            keyboardVisible || ViewCompat.getRootWindowInsets(window.decorView)
                ?.isVisible(WindowInsetsCompat.Type.ime()) == true)
    }

    private fun restoreProgrammingReparentFocus(state: ProgrammingReparentFocus?) {
        if (state == null || !state.input.isAttachedToWindow) return
        // Removing a focused subtree clears native focus. Restore only that same
        // editor after its actual reparent; viewport resizing never uses this path.
        val input = state.input
        if (!input.hasFocus()) input.requestFocus()
        if (state.selectionStart in 0..input.text.length && state.selectionEnd in 0..input.text.length) {
            input.setSelection(state.selectionStart, state.selectionEnd)
        }
        updateProgrammingKeyboardBarVisibility()
        if (state.imeWasVisible && input.hasFocus()) {
            (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
                .showSoftInput(input, 0)
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
                getString(R.string.exercise_wrap_enabled)

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
                getString(R.string.exercise_wrap_disabled)

            codeLines.requestLayout()
        }
    }

    private fun enableCppAutoClose(
        input: EditText
    ) {
        CppCodeInputRules.install(input)
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

    internal fun openCalculatorFor(
        target: View
    ) {

        calculatorResultTarget =
            target

        calculatorResultLauncher.launch(
            Intent(
                this,
                CalculatorActivity::class.java
            ).putExtra(
                CalculatorActivity.EXTRA_RETURN_RESULT,
                true
            )
        )
    }


    override fun onPause() {

        // Screen lock also calls onPause. Do NOT stop narration here:
        // it would discard the current audio and restart from the title.
        findViewById<View>(
            R.id.attemptIndicator
        ).removeCallbacks(
            attemptIndicatorRefreshRunnable
        )

        saveCurrentPageProgress()

        super.onPause()
    }

    // onStop is intentionally not used to stop the reader: Android can call
    // both onPause and onStop when the screen locks. The controller is closed
    // in onDestroy or stopped explicitly on a page change.

    private fun saveCurrentPageProgress() {

        ProgressRepository.saveCurrentPage(
            this,
            currentRouteId,
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

    /*
     * El icono de la cabeza hablando sustituye al antiguo botón Deslizar.
     * Usa la misma preferencia que los ajustes generales de Narración.
     * El estado refleja la preferencia incluso si el teclado oculta
     * temporalmente la barra flotante.
     */
    private fun updateNarrationToggleAppearance() {
        if (!::buttonNarrationToggle.isInitialized) return

        val masterEnabled = NarrationSettingsRepository.get(this).enabled
        val enabled = masterEnabled &&
            NarrationSettingsRepository.controlsVisible(this)
        val accent = AppearanceSettingsRepository.getRouteAccentColor(
            this, currentRouteId
        )

        // El color corresponde a la silueta y a las ondas del icono;
        // nunca coloreamos su fondo ni lo convertimos en una cápsula.
        buttonNarrationToggle.setBackgroundColor(Color.TRANSPARENT)
        buttonNarrationToggle.imageTintList = ColorStateList.valueOf(
            if (enabled) accent else Color.rgb(170, 174, 183)
        )
        buttonNarrationToggle.isActivated = enabled
        buttonNarrationToggle.isEnabled = masterEnabled
        buttonNarrationToggle.alpha = if (masterEnabled) 1f else 0.65f
        buttonNarrationToggle.contentDescription = getString(
            if (enabled) R.string.lesson_narration_hide_controls
            else R.string.lesson_narration_show_controls
        )
    }

    private fun saveCurrentPageState() {

        pageRequiredExercises[currentPage] =
            requiredExercises

        pageCompletedExercises[currentPage] =
            completedExercises
    }

    private fun markExerciseCompleted() {
        completedExercises += 1
        pageCompletedExercises[currentPage] = completedExercises
        pageRequiredExercises[currentPage] = requiredExercises

        // Comprobar disables the answered fields or buttons in the same callback.
        // The now-obsolete focused View/IME can otherwise swallow the first Enter
        // intended for the next puzzle or Continue. Retire only focus belonging to
        // this lesson's completed interaction, never a real multiline compiler.
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_PROGRAMMING) {
            val previous = currentFocus
            var member: View? = previous
            var belongsToLesson = false
            while (member != null) {
                if (member === lessonContent) {
                    belongsToLesson = true
                    break
                }
                member = member.parent as? View
            }
            val codeInput = previous as? EditText
            val protectedEditor = codeInput != null &&
                (codeInput.tag != CODE_INPUT_TAG || !PuzzleAnswerSubmit.isSingleLine(codeInput))
            if (previous != null && belongsToLesson && !protectedEditor) {
                val neutralHost = findViewById<View>(android.R.id.content)
                neutralHost.isFocusableInTouchMode = true
                if (neutralHost.requestFocus() && codeInput != null) {
                    (getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager)
                        ?.hideSoftInputFromWindow(codeInput.windowToken, 0)
                }
            }
        }
    }
    private fun addNote(text: String, mathMode: String = "auto") {

        val view = ReadingTextView(this)

        view.text =
            getString(R.string.lesson_note, text)
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            MathTextRenderer.apply(view, getString(R.string.lesson_note, text), mathMode)
        }

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

        registerReadingText(view, mathMode = mathMode)
        lessonContent.addView(view)
    }

    private fun cppIncorrectGuidance(block: JSONObject, selectedIndex: Int? = null): String {
        val fallback = when (block.getString("type")) {
            "multiple_choice" -> R.string.cpp_review_choice
            "fill_code" -> R.string.cpp_review_fill
            "output_prediction" -> R.string.cpp_review_output
            "order_code" -> R.string.cpp_review_order
            "drag_fill_code" -> R.string.cpp_review_drag
            else -> error("Unsupported C++ feedback type")
        }
        return CppExerciseFeedback.review(block, selectedIndex, getString(fallback))
    }

    private fun cppIncorrectReview(block: JSONObject, selectedIndex: Int? = null): String {
        val prefix = when (block.getString("type")) {
            "order_code" -> R.string.exercise_wrong_order
            "drag_fill_code" -> R.string.exercise_wrong_blocks
            else -> R.string.exercise_incorrect
        }
        return getString(prefix) + "\n" + cppIncorrectGuidance(block, selectedIndex)
    }

    /** An explicit wrong submission starts its visible review. Rendering or restoring
     * feedback never starts audio, and success remains behind its original gate. */
    private fun narrateIncorrectAttempt(pageIndex: Int, blockIndex: Int, guidance: List<String>) {
        if (!::lessonJson.isInitialized || currentPage != pageIndex || isDestroyed || isFinishing) return
        val settings = NarrationSettingsRepository.get(this)
        if (!settings.enabled) return
        if (guidance.none { it.isNotBlank() }) return
        val controller = narrationController ?: return
        val language = com.learngraphicsprogramming.app.narration.NarrationLanguage.forLesson(
            lessonJson, currentPage, settings.language)
        val selected = NarrationSettingsRepository.forLanguage(this, language)
        val texts = com.learngraphicsprogramming.app.narration.IncorrectFeedbackNarration.prepare(
            guidance, language, forKokoro = selected.engine != NarrationEngine.ANDROID)
        if (texts.isEmpty()) return
        val script = narrationScript(language)
        incorrectReadingPresentation?.identity?.let(readingProgress::forget)
        incorrectReadingPresentation = IncorrectReadingPresentation(narrationPageIdentity(), blockIndex,
            LessonNarrationScript(texts, emptyList(), List(texts.size) {
                com.learngraphicsprogramming.app.narration.NarrationTextOrigin(blockIndex, feedback = true)
            }), language, selected.engine)
        // Bind the real page, including its existing success gates, even when its first
        // play action is a review. This is silent and never creates a second bookmark.
        controller.readIncorrectFeedback(texts, language, narrationPageIdentity(), blockIndex,
            pageTexts = script.texts, feedbackGates = script.gates,
            correctBlocks = narrationCorrectBlocks())
    }

    private fun restoreCppReview(block: JSONObject, state: JSONObject?, answer: Any,
                                 result: TextView, selectedIndex: Int? = null) {
        if (!CppExerciseFeedback.wasReviewed(state, answer)) return
        // A restored draft never evaluates an answer or consumes another attempt.
        runCatching { cppIncorrectReview(block, selectedIndex) }.getOrNull()?.let {
            result.text = it
            result.setTextColor(Color.rgb(240, 150, 150))
        }
    }

    private fun saveCppReviewDraft(page: Int, block: Int, field: String, answer: Any) {
        // Update a previous submission's draft; a first technical/incomplete attempt
        // must keep the existing no-saved-response guarantee.
        if (ProgressRepository.getExerciseState(this, currentLessonId, page, block) == null) return
        val state = JSONObject().put(field, answer).put("correct", false)
        ProgressRepository.saveExerciseState(this, currentRouteId, currentLessonId, page, block, state)
    }

    private fun addFillCode(
        block: JSONObject,
        blockIndex: Int
    ) {

        val exercisePage = currentPage

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
            getString(R.string.lesson_code_heading, getString(R.string.lesson_cpp_label))

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
            18f

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
            getString(R.string.exercise_reset_text)

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
            getString(R.string.exercise_copy_text)

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
        val input: EditText =
            FreeScrollEditText(this)

        input.tag =
            CODE_INPUT_TAG

        enableCppAutoClose(
            input
        )

        protectProgrammingInteractionFromPageSwipe(
            input
        )

        input.hint =
            getString(R.string.exercise_missing_code)

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
        makeCodeCardFullWidth(answerCard, extendInPortrait = false)

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
            MaterialButton(this)
        puzzleEnterShortcut.register(button, answerCard)

        button.text =
            getString(R.string.exercise_check)

        button.isAllCaps =
            false

        button.textSize =
            18f

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
            exerciseFeedbackText(blockIndex)

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
                    getString(R.string.lesson_cpp_label),
                    input.text.toString()
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                getString(R.string.exercise_text_copied),
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

                    if (!answeredCorrectly) {
                        result.text = ""
                        saveCppReviewDraft(exercisePage, blockIndex, "answer", input.text.toString())
                    }
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
                getString(R.string.exercise_correct)

            result.text =
                getString(R.string.lesson_correct_explanation, block.optString("explanation"))

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        restoreCppReview(block, savedState, savedAnswer, result)

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
                getString(R.string.exercise_check)

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
                currentRouteId,
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
                CppExerciseAnswers.normalizeFill(block, input.text.toString())

            if (answeredCorrectly) return@setOnClickListener
            if (userAnswer.isBlank()) {
                showIncompleteAttempt()
                return@setOnClickListener
            }

            var incorrectReview = ""
            val correct = evaluateExerciseAttempt(exercisePage, blockIndex, userAnswer,
                evaluation = {
                    incorrectReview = cppIncorrectReview(block)
                    CppExerciseAnswers.fillMatches(block, userAnswer)
                }, verdict = { it }) ?: return@setOnClickListener

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

            if (!correct) CppExerciseFeedback.recordIncorrect(state, input.text.toString())

            ProgressRepository.saveExerciseState(
                this,
                currentRouteId,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    getString(R.string.lesson_correct_explanation, block.optString("explanation"))

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
                        getString(R.string.exercise_correct)
                }

            } else {

                result.text =
                    incorrectReview

                narrateIncorrectAttempt(exercisePage, blockIndex, listOf(result.text.toString()))

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

        val exercisePage = currentPage

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
            getString(R.string.lesson_code_heading, getString(R.string.lesson_cpp_label))

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
            18f

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
            getString(R.string.exercise_reset_text)

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
            getString(R.string.exercise_copy_text)

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
            getString(R.string.exercise_expected_output)

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
            18f

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
        makeCodeCardFullWidth(answerCard, extendInPortrait = false)

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
            MaterialButton(this)
        puzzleEnterShortcut.register(button, answerCard)

        button.text =
            getString(R.string.exercise_check)

        button.isAllCaps =
            false

        button.textSize =
            18f

        button.isEnabled =
            input.text
                .toString()
                .isNotBlank() &&
                    !savedCorrect

        lessonContent.addView(
            button
        )

        val result =
            exerciseFeedbackText(blockIndex)

        result.textSize =
            18f

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
                    getString(R.string.exercise_clipboard_text),
                    input.text.toString()
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                getString(R.string.exercise_text_copied),
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

                    if (!answeredCorrectly) {
                        result.text = ""
                        saveCppReviewDraft(exercisePage, blockIndex, "answer", input.text.toString())
                    }
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
                getString(R.string.exercise_correct)

            result.text =
                getString(R.string.lesson_correct_explanation, block.optString("explanation"))

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        restoreCppReview(block, savedState, savedAnswer, result)

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
                getString(R.string.exercise_check)

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
                currentRouteId,
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
                if (block.optString("outputComparison", "trimmed") == "trimmed") {
                    input.text.toString().trim()
                } else input.text.toString()

            if (answeredCorrectly) return@setOnClickListener
            if (userAnswer.isBlank()) {
                showIncompleteAttempt()
                return@setOnClickListener
            }

            var incorrectReview = ""
            val correct = evaluateExerciseAttempt(exercisePage, blockIndex, userAnswer,
                evaluation = {
                    incorrectReview = cppIncorrectReview(block)
                    CppExerciseAnswers.outputMatches(block, userAnswer)
                }, verdict = { it }) ?: return@setOnClickListener

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

            if (!correct) CppExerciseFeedback.recordIncorrect(state, input.text.toString())

            ProgressRepository.saveExerciseState(
                this,
                currentRouteId,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    getString(R.string.lesson_correct_explanation, block.optString("explanation"))

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
                        getString(R.string.exercise_correct)
                }

            } else {

                result.text =
                    incorrectReview

                narrateIncorrectAttempt(exercisePage, blockIndex, listOf(result.text.toString()))

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

        val exercisePage = currentPage
        val codeLanguage = CodeExerciseLanguage.order(block)

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
            ReadingTextView(this)

        instruction.text =
            getString(R.string.exercise_order_instruction)

        instruction.setTextColor(
            Color.rgb(180, 185, 195)
        )

        instruction.textSize =
            18f

        instruction.setPadding(
            0,
            0,
            0,
            dp(14)
        )

        registerReadingText(instruction, blockIndex)
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

        languageLabel.text = getString(R.string.lesson_code_heading, getString(
            if (codeLanguage == CodeExerciseLanguage.Language.CMAKE) R.string.lesson_cmake_label
            else R.string.lesson_cpp_label))
        languageLabel.tag = "order_code_language_$blockIndex"

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
            18f


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
            getString(R.string.exercise_reset)

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
            getString(R.string.lesson_copy_code)

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
            getString(R.string.exercise_fullscreen)

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
            MaterialButton(this)
        puzzleEnterShortcut.register(checkButton, codeCard)

        checkButton.text =
            getString(R.string.exercise_check)

        checkButton.isAllCaps =
            false

        checkButton.textSize =
            18f

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
            CodeLineNumberColumn(this)

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
                getString(R.string.lesson_integer, i + 1)

            number.typeface =
                Typeface.MONOSPACE

            number.textSize =
                18f

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
                    getString(R.string.exercise_fullscreen)

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
                    getString(R.string.exercise_exit_fullscreen)
            }
        }

        val result = exerciseFeedbackText(blockIndex)
        fun currentOrderAnswer() = org.json.JSONArray((0 until orderContainer.childCount).map {
            orderContainer.getChildAt(it).tag as Int
        })

        fun saveOrderState(
            correct: Boolean =
                answeredCorrectly,
            reviewed: Boolean = false
        ) {

            val orderJson = currentOrderAnswer()
            if (!correct && !reviewed) result.text = ""

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

            if (reviewed && !correct) CppExerciseFeedback.recordIncorrect(state, orderJson)

            ProgressRepository.saveExerciseState(
                this,
                currentRouteId,
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

            val fragmentText =
                fragments.getString(i)

            row.text =
                compactCodeSpaces(
                    fragmentText
                )

            // Guardamos cuál era el bloque original.
            row.tag = i

            row.typeface =
                Typeface.MONOSPACE

            row.textSize =
                18f

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

            DirectDragTouch.bind(
                row,
                canInteract = { !answeredCorrectly },
                startDrag = { view ->
                    view.alpha = 0.55f
                    view.startDragAndDrop(ClipData.newPlainText("code_order", ""),
                        View.DragShadowBuilder(view), view, 0)
                },
                onGestureClaim = { ignoreCurrentSwipe = true }
            )

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
                currentRouteId,
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

        result.textSize =
            18f

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
                getString(R.string.exercise_correct)

            result.text =
                getString(R.string.lesson_correct_explanation, block.optString("explanation"))

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

        restoreCppReview(block, savedState, currentOrderAnswer(), result)

        checkButton.setOnClickListener {

            if (answeredCorrectly || !puzzleStarted) return@setOnClickListener
            val actual = List(orderContainer.childCount) {
                (orderContainer.getChildAt(it) as TextView).text.toString()
            }
            var incorrectReview = ""
            val correct = evaluateExerciseAttempt(exercisePage, blockIndex, org.json.JSONArray(actual),
                evaluation = {
                    incorrectReview = cppIncorrectReview(block)
                    require(actual.isNotEmpty() && actual.size == fragments.length() &&
                        actual.size == correctOrder.length()) { "Invalid ordering configuration" }
                    val expected = List(actual.size) {
                        val index = correctOrder.getInt(it)
                        require(index in 0 until fragments.length()) { "Invalid fragment index" }
                        fragments.getString(index)
                    }
                    actual == expected
                }, verdict = { it }) ?: return@setOnClickListener

            saveOrderState(
                correct, reviewed = true
            )

            if (correct) {

                result.text =
                    getString(R.string.lesson_correct_explanation, block.optString("explanation"))

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
                        getString(R.string.exercise_correct)

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
                // Reconsider the current draft with another tap sequence; keep every row.
                nextTapPosition = 0

                result.text =
                    incorrectReview

                narrateIncorrectAttempt(exercisePage, blockIndex, listOf(result.text.toString()))

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
        block: JSONObject,
        blockIndex: Int
    ) {

        val cmakeConfiguration = if (block.has("cmake")) block.getJSONObject("cmake") else null
        require(cmakeConfiguration == null || block.has("projectFiles")) { "CMake requires projectFiles" }
        var cmakeLab: CppCmakeLabController? = null

        val initialCode =
            block.optString(
                "initialCode",
                ""
            )

        val initialStandardInput = block.optString("initialStandardInput", "")
        val draftKey = "$currentRouteId/$currentLessonId/$currentPage/$blockIndex"
        val labDrafts = getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
        var projectControls: View? = null
        val inputLabel = TextView(this).apply {
            // Retain the CMake insertion anchor without a developer-facing explanation.
            visibility = View.GONE
        }
        val standardInput = EditText(this).apply {
            tag = "cpp_input"
            setTextColor(Color.rgb(230, 237, 243))
            setHintTextColor(Color.rgb(174, 180, 190))
            contentDescription = getString(R.string.cpp_input_hint)
            hint = getString(R.string.cpp_input_hint)
            background = null
            setPadding(dp(8), dp(4), dp(8), dp(4))
            inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI
            gravity = Gravity.TOP or Gravity.START
            typeface = Typeface.MONOSPACE
            minLines = 1
            setText(labDrafts.getString("$draftKey/input", initialStandardInput))
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
                override fun afterTextChanged(s: Editable?) {
                    labDrafts.edit().putString("$draftKey/input", s.toString()).apply()
                }
            })
        }

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
            getString(R.string.lesson_code_heading, getString(R.string.lesson_cpp_label))

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
            18f

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
            getString(R.string.exercise_reset_code)

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
            getString(R.string.exercise_wrap_disabled)

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
            getString(R.string.lesson_copy_code)

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
            getString(R.string.exercise_fullscreen)

        // Identificador estable para pruebas y lectores de interfaz.
        fullscreenButton.tag = "cpp_compiler_fullscreen"

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
            18f

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

        sourceCode.textSize =
            18f

        sourceCode.includeFontPadding =
            false

        sourceCode.gravity =
            Gravity.TOP or
                    Gravity.START

        sourceCode.setTextColor(
            Color.rgb(
                216,
                216,
                216
            )
        )

        sourceCode.setBackgroundColor(
            Color.TRANSPARENT
        )

        sourceCode.inputType =
            android.text.InputType.TYPE_CLASS_TEXT or
                    android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                    android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS

        sourceCode.typeface =
            Typeface.MONOSPACE

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

        val hasSourceDraft = labDrafts.contains("$draftKey/source")
        val restoredSource = labDrafts.getString("$draftKey/source", initialCode) ?: initialCode
        sourceCode.setText(CppIndentationUnits.loadedText(restoredSource,
            if (hasSourceDraft) labDrafts.getString("$draftKey/sourceIndentation_v2", null) else null,
            seedPredefined = !hasSourceDraft))
        sourceCode.text?.let(CppSyntaxHighlighter::applyTo)

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

        val viewportController = CppEditorViewportController(
            sourceCode, lineNumbers, lineNumberScroll, editorBody,
            editorVerticalBar, editorHorizontalBar,
            isFullscreen = { compilerFullscreenActive },
            isKeyboardVisible = { keyboardVisible || programmingImeAnimations.isNotEmpty() },
            compactNormalContent = true,
            onEditorGesture = { ignoreCurrentSwipe = true }
        )
        cppEditorViewportControllers.add(viewportController)

        fun updateEditorBottomSafety() = viewportController.updateBottomSafety()
        fun updateNormalCompilerEditorHeight() = viewportController.updateNormalHeight()
        fun updateEditorHorizontalBar() = viewportController.updateHorizontalBar()
        fun updateEditorVerticalBar() = viewportController.updateVerticalBar()
        fun updateLineNumbers() = viewportController.updateLineNumbers()

        fun updateCompilerCompactLandscapeUi() {

            val compactMode =
                compilerFullscreenActive && isCompactProgrammingFullscreen()

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
            projectControls?.visibility = if (compactMode) View.GONE else View.VISIBLE

            val landscapeIme = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE &&
                (keyboardVisible || programmingImeAnimations.isNotEmpty()) && sourceCode.hasFocus()
            val topPadding = if (landscapeIme) 0 else dp(16)
            if (sourceCode.paddingTop != topPadding) {
                sourceCode.setPadding(sourceCode.paddingLeft, topPadding, sourceCode.paddingRight,
                    sourceCode.paddingBottom)
            }
            if (lineNumbers.paddingTop != topPadding) {
                lineNumbers.setPadding(lineNumbers.paddingLeft, topPadding, lineNumbers.paddingRight,
                    lineNumbers.paddingBottom)
            }
            updateEditorBottomSafety()

            /*
             * Recalculamos la barra horizontal.
             *
             * En modo compacto permanecerá oculta,
             * pero seguirá conservando su max.
             */
            updateEditorHorizontalBar()
        }

        programmingViewportUiControllers.add {
            if (codeCard.isAttachedToWindow) updateCompilerCompactLandscapeUi()
        }
        programmingCodeTextCompanions[sourceCode] = lineNumbers

        sourceCode.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            updateCompilerCompactLandscapeUi()
        }
        viewportController.refresh()

        sourceCode.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(text: Editable?) {
                if (text == null) return
                CppSyntaxHighlighter.applyTo(text)
                if (!block.has("projectFiles")) {
                    labDrafts.edit().putString("$draftKey/source", text.toString())
                        .putString("$draftKey/sourceIndentation_v2", CppIndentationUnits.snapshot(text)).apply()
                }
            }
        })

        if (!block.has("projectFiles")) sourceCode.cppIndentationRestored = {
            labDrafts.edit().putString("$draftKey/source", sourceCode.text.toString())
                .putString("$draftKey/sourceIndentation_v2", CppIndentationUnits.snapshot(sourceCode.text ?: "")).apply()
        }

        resetButton.tag = "cpp_reset"
        val projectEditor = if (block.has("projectFiles")) {
            CppLessonProjectEditor(this, block, draftKey, sourceCode) {
                viewportController.refresh()
                updateLineNumbers()
            }.also {
                projectControls = it.controls
                codeCard.addView(it.controls, 1)
                updateCompilerCompactLandscapeUi()
            }
        } else null
        val compilerVariants = mutableListOf(CppCompilerVariant.DEFAULT)
        val compilerLabels = mutableListOf(getString(
            if (BuildConfig.LGP_CLANG23_ENABLED) R.string.clang_compiler_23 else R.string.clang_compiler_21_default
        ))
        val savedVariant = labDrafts.getString("$draftKey/compiler", CppCompilerVariant.DEFAULT.name)
        if (BuildConfig.LGP_CLANG23_ENABLED &&
            (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED || savedVariant == CppCompilerVariant.OPTIONAL_CLANG_21.name)) {
            compilerVariants.add(CppCompilerVariant.OPTIONAL_CLANG_21)
            compilerLabels.add(getString(R.string.clang_compiler_21))
        }
        val compilerSelector = android.widget.Spinner(this).apply {
            tag = "cpp_compiler_variant"
            contentDescription = getString(R.string.clang_compiler_selector_title)
            adapter = android.widget.ArrayAdapter(this@LessonActivity,
                android.R.layout.simple_spinner_dropdown_item, compilerLabels)
            setSelection(compilerVariants.indexOfFirst { it.name == savedVariant }.coerceAtLeast(0))
            onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                    labDrafts.edit().putString("$draftKey/compiler", compilerVariants[position].name).apply()
                    cmakeLab?.draftChanged()
                }
            }
        }
        // Preserve the saved choice and existing provider without exposing an
        // irrelevant control. No current curricular lab requests version changes.
        compilerSelector.visibility = View.GONE
        header.addView(compilerSelector)

        /*
         * BOTÓN COMPILAR.
         */
        val runButton =
            MaterialButton(this)
        runButton.tag = "cpp_run"
        lessonContent.addView(inputLabel)
        lessonContent.addView(standardInput)

        runButton.text =
            getString(R.string.cpp_runtime_run)

        runButton.isAllCaps =
            false

        runButton.textSize =
            18f

        runButton.isEnabled =
            true

        val runParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )

        runParams.setMargins(
            0,
            dp(8),
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
        statusText.tag = "cpp_status"

        statusText.text =
            getString(R.string.cpp_runtime_ready)

        statusText.textSize =
            18f

        statusText.setTextColor(
            Color.rgb(
                174,
                180,
                190
            )
        )

        statusText.setPadding(
            0,
            dp(8),
            0,
            dp(8)
        )

        lessonContent.addView(
            statusText
        )

        // One visible console; all original channels stay independently available.
        val console = CppConsoleView(this)
        val outputText = console.diagnostics
        val programOutput = console.stdout
        val programError = console.stderr
        lessonContent.addView(console)
        makeCodeCardFullWidth(console, extendInPortrait = false)
        fun offerCompilerRecovery() {
            // A preserved choice may outlive its optional installation. Recovery
            // stays explicit and does not add a permanent selector to this lab.
            androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.clang_compiler_selector_title)
                .setMessage(R.string.clang21_selected_unavailable)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(R.string.cpp_compiler_use_default) { _, _ -> compilerSelector.setSelection(0) }
                .show()
        }
        if (cmakeConfiguration != null) {
            cmakeLab = CppCmakeLabController(this, cmakeConfiguration, draftKey, requireNotNull(projectEditor),
                sourceCode, { compilerVariants[compilerSelector.selectedItemPosition] }, lessonContent,
                inputLabel, runButton, statusText, outputText, programOutput, programError, standardInput) { result ->
                console.present(when {
                    result.execution == null -> CppLabPresentationPolicy.Channel.DIAGNOSTICS
                    result.exitCode != 0 -> CppLabPresentationPolicy.Channel.STDERR
                    else -> CppLabPresentationPolicy.Channel.STDOUT
                })
                if (result.exitCode != 0 && compilerVariants[compilerSelector.selectedItemPosition] ==
                    CppCompilerVariant.OPTIONAL_CLANG_21 && !CppCompiler.isOptionalClang21Available(this)) {
                    offerCompilerRecovery()
                }
            }
        }

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

            var geometryChanged = false

            /*
             * La tarjeta ocupa exactamente la altura
             * visible disponible.
             */
            val cardParams =
                codeCard.layoutParams
                        as LinearLayout.LayoutParams

            if (cardParams.width != LinearLayout.LayoutParams.MATCH_PARENT ||
                cardParams.height != lessonScroll.height) {
                cardParams.width = LinearLayout.LayoutParams.MATCH_PARENT
                cardParams.height = lessonScroll.height
                codeCard.layoutParams = cardParams
                geometryChanged = true
            }

            codeCard.translationX =
                0f

            /*
             * La cabecera conserva su altura normal.
             *
             * editorBody recibe TODO el espacio restante.
             * Por eso la cabecera nunca se desplaza aunque
             * existan miles de líneas.
             */
            val bodyParams = editorBody.layoutParams as LinearLayout.LayoutParams
            if (bodyParams.width != LinearLayout.LayoutParams.MATCH_PARENT ||
                bodyParams.height != 0 || bodyParams.weight != 1f) {
                bodyParams.width = LinearLayout.LayoutParams.MATCH_PARENT
                bodyParams.height = 0
                bodyParams.weight = 1f
                editorBody.layoutParams = bodyParams
                geometryChanged = true
            }

            /*
             * La columna de números ocupa toda
             * la altura del editor.
             */
            val numberParams =
                lineNumberScroll.layoutParams
                        as LinearLayout.LayoutParams

            if (numberParams.height != LinearLayout.LayoutParams.MATCH_PARENT) {
                numberParams.height = LinearLayout.LayoutParams.MATCH_PARENT
                lineNumberScroll.layoutParams = numberParams
                geometryChanged = true
            }

            /*
             * El EditText ocupa toda la altura disponible.
             *
             * El desplazamiento vertical ocurre dentro
             * del propio editor, no moviendo la cabecera.
             */
            val sourceParams = sourceCode.layoutParams as FrameLayout.LayoutParams
            if (sourceParams.width != FrameLayout.LayoutParams.MATCH_PARENT ||
                sourceParams.height != FrameLayout.LayoutParams.MATCH_PARENT) {
                sourceParams.width = FrameLayout.LayoutParams.MATCH_PARENT
                sourceParams.height = FrameLayout.LayoutParams.MATCH_PARENT
                sourceCode.layoutParams = sourceParams
                geometryChanged = true
            }
            if (sourceCode.minHeight != 0) {
                sourceCode.minHeight = 0
                geometryChanged = true
            }

            if (geometryChanged) {
                sourceCode.post {
                    updateEditorVerticalBar()
                    updateLineNumbers()
                }
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

            updateNarrationVisibility()

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
                getString(R.string.exercise_exit_fullscreen)

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
            compilerFullscreenBackExit = null

            updateNarrationVisibility()

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
                getString(R.string.exercise_fullscreen)

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

                // Usamos exactamente el mismo cierre para el botón
                // fullscreen y para Atrás del sistema.
                compilerFullscreenBackExit = { exitCompilerFullscreen() }
                enterCompilerFullscreen()
            }
        }

        /*
         * REINICIAR.
         */
        resetButton.setOnClickListener {

            standardInput.setText(initialStandardInput)
            programOutput.text = ""
            programError.text = ""
            console.present()

            if (projectEditor != null) projectEditor.reset() else sourceCode.setText(
                CppIndentationUnits.loadedText(initialCode, seedPredefined = true))
            cmakeLab?.reset()

            val cursorPosition =
                initialCode.indexOf(
                    "\n    \n"
                )

            if (
                cursorPosition >= 0 && projectEditor == null
            ) {

                sourceCode.setSelection(
                    cursorPosition + 5
                )
            }

            statusText.text =
                getString(R.string.cpp_runtime_ready)

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

            viewportController.setWrapEnabled(wrapEnabled)

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
                    getString(R.string.exercise_wrap_enabled)
                } else {
                    getString(R.string.exercise_wrap_disabled)
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
        if (cmakeLab == null) runButton.setOnClickListener {

            outputText.text = ""
            programOutput.text = ""
            programError.text = ""
            console.present()

            val source =
                sourceCode.text.toString()
            val preparedInput = standardInput.text.toString()
            val compilerVariant = compilerVariants[compilerSelector.selectedItemPosition]
            if (compilerVariant == CppCompilerVariant.OPTIONAL_CLANG_21 &&
                !CppCompiler.isOptionalClang21Available(this)) {
                statusText.text = getString(R.string.clang21_selected_unavailable)
                offerCompilerRecovery()
                return@setOnClickListener
            }
            val projectSnapshot = runCatching { projectEditor?.project() }
            if (projectSnapshot.isFailure) {
                statusText.text = getString(R.string.cpp_project_invalid)
                statusText.setTag(R.id.cpp_lab_build_phase, "CONFIGURATION")
                outputText.text = getString(R.string.cpp_project_invalid)
                return@setOnClickListener
            }
            runButton.isEnabled =
                false

            statusText.text =
                getString(R.string.exercise_compiling)
            statusText.setTag(R.id.cpp_lab_build_phase, "BUILDING")

            outputText.text =
                ""

            Thread {

                val result =
                    runCatching {

                        val engine = CppExecutionEngine(this)
                        val project = projectSnapshot.getOrThrow()
                        if (project == null) engine.compileAndRun(source, preparedInput, compilerVariant)
                        else engine.compileAndRun(project, preparedInput, compilerVariant)
                    }

                runOnUiThread {

                    runButton.isEnabled =
                        true

                    result.onSuccess { engineResult ->

                        val compilation =
                            engineResult.compilation

                        val execution =
                            engineResult.execution
                        statusText.setTag(R.id.cpp_lab_build_phase, compilation.phase.name)

                        statusText.text =
                            if (
                                execution == null
                            ) {

                                getString(if (compilation.phase == CppBuildPhase.LINK)
                                    R.string.cpp_link_failed else R.string.cpp_compilation_failed, compilation.exitCode)

                            } else {

                                getString(if (execution.exitCode == 0) R.string.cpp_runtime_finished
                                    else R.string.cpp_execution_failed, execution.exitCode)
                            }

                        outputText.text =
                            buildString {

                                append(
                                    compilation.stdout
                                )

                                append(
                                    compilation.stderr
                                )

                                if (compilation.timedOut) {
                                    append('\n')
                                    append(getString(R.string.math_ui_compiler_timeout))
                                }

                            }.trimEnd()
                        programOutput.text = execution?.stdout.orEmpty()
                        programError.text = execution?.stderr.orEmpty()
                        console.present(when {
                            execution == null -> CppLabPresentationPolicy.Channel.DIAGNOSTICS
                            execution.exitCode != 0 -> CppLabPresentationPolicy.Channel.STDERR
                            else -> CppLabPresentationPolicy.Channel.STDOUT
                        })
                    }

                    result.onFailure { error ->

                        statusText.text =
                            getString(R.string.exercise_execution_failed)

                        android.util.Log.w("LessonCompiler", "Could not compile or run the source", error)
                        outputText.text = getString(R.string.exercise_execution_failed)
                        console.present(CppLabPresentationPolicy.Channel.DIAGNOSTICS)
                    }
                }
            }.start()
        }
    }

    /** One Enter checks or advances on its first DOWN. UP only closes that action;
     * no focus change or completed-puzzle callback can cause a second click.
     */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode != KeyEvent.KEYCODE_ENTER &&
            event.keyCode != KeyEvent.KEYCODE_NUMPAD_ENTER ||
            !event.hasNoModifiers() ||
            event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) {
            return super.dispatchKeyEvent(event)
        }

        // The previous page may have detached a focused compiler EditText.
        // Only an attached, visible editor in the current page owns this Enter.
        val focused = currentFocus?.takeIf { it.isAttachedToWindow && it.isShown }
        val editorOwnsEnter = compilerFullscreenActive ||
            focused is MathExpressionEditor ||
            focused is EditText && !PuzzleAnswerSubmit.isSingleLine(focused)
        // A fullscreen puzzle may still use Enter to check, but must never
        // advance the page behind its editing/interaction overlay.
        val canNavigate = !exerciseFullscreenActive &&
            ::lessonContinueButton.isInitialized &&
            lessonContinueButton.isEnabled && lessonContinueButton.isShown &&
            LessonPageAdvancePolicy.canAdvance(requiredExercises, completedExercises)
        val pageIdentity = "${currentRouteId}/${currentLessonId}/$currentPage"
        val route = when (event.action) {
            KeyEvent.ACTION_DOWN -> lessonAdvanceEnterGate.down(
                pageIdentity, event.keyCode, event.downTime, event.eventTime, event.repeatCount,
                canNavigate, editorOwnsEnter
            )
            else -> lessonAdvanceEnterGate.up(
                pageIdentity, event.keyCode, event.downTime, event.eventTime, event.isCanceled
            )
        }
        return when (route) {
            LessonPageAdvanceEnterGate.Route.NAVIGATE -> {
                // This is the *only* navigation path for this physical key press.
                if (canNavigate && !isFinishing && !isDestroyed) lessonContinueButton.performClick()
                true
            }
            LessonPageAdvanceEnterGate.Route.PUZZLE ->
                puzzleEnterShortcut.dispatch(event, focused, compilerFullscreenActive) ||
                    super.dispatchKeyEvent(event)
            LessonPageAdvanceEnterGate.Route.EDITOR -> super.dispatchKeyEvent(event)
            LessonPageAdvanceEnterGate.Route.CONSUME -> true
        }
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            // LDPlayer's one-touch Pause mapping hits the floating narration
            // overlay, which may sit directly above a different puzzle.
            // Never let a tap on the reader select a puzzle hidden beneath it.
            val narrationOverlay = findViewById<View>(R.id.lessonNarrationControls)
            val touchedReader = narrationOverlay?.isShown == true && Rect().let { bounds ->
                narrationOverlay.getGlobalVisibleRect(bounds) &&
                    bounds.contains(event.rawX.toInt(), event.rawY.toInt())
            }
            if (!touchedReader) puzzleEnterShortcut.trackTouch(event.rawX, event.rawY)
        }

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

        when (event.actionMasked) {

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

                swipeStartedInEmptyCppEditor = LessonGesturePolicy.startsInEmptyCppEditor(root, event.rawX, event.rawY)
                ignoreCurrentSwipe = LessonGesturePolicy.isOwnedByControl(root, event.rawX, event.rawY) ||
                    (currentRouteId != AppearanceSettingsRepository.ROUTE_MATH_PHYSICS &&
                        isTouchInsideActiveHorizontalScroll(root, event.rawX, event.rawY))

                touchStartX =
                    event.x

                touchStartY =
                    event.y
            }

            MotionEvent.ACTION_POINTER_DOWN -> ignoreCurrentSwipe = true
            MotionEvent.ACTION_MOVE -> {
                // Once a gesture commits to vertical reading, it cannot later turn into a page swipe.
                val selectedText = currentFocus as? TextView
                if (!swipeStartedInEmptyCppEditor &&
                    event.eventTime - event.downTime >= android.view.ViewConfiguration.getLongPressTimeout() &&
                    selectedText?.isEnabled == true && selectedText.hasSelection()) ignoreCurrentSwipe = true
                val dx = kotlin.math.abs(event.x - touchStartX)
                val dy = kotlin.math.abs(event.y - touchStartY)
                if (dy > dp(16) && dy > dx * 1.2f) ignoreCurrentSwipe = true
            }
            MotionEvent.ACTION_CANCEL -> ignoreCurrentSwipe = true
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
                    horizontalDistance > dp(64) &&
                    horizontalDistance > verticalDistance * 1.5f
                ) {

                    // Cancel the original child gesture before replacing the page.
                    val cancel = MotionEvent.obtain(event)
                    cancel.action = MotionEvent.ACTION_CANCEL
                    super.dispatchTouchEvent(cancel)
                    cancel.recycle()
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
                getString(R.string.exercise_finish_required),
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
                getString(R.string.exercise_finish_required),
                Toast.LENGTH_SHORT
            ).show()

            return
        }

        saveCurrentPageState()

        if (
            currentPage <
            totalPages - 1
        ) {

            currentPage++

            saveCurrentPageProgress()

            renderCurrentPage()

            return
        }

        saveCurrentPageProgress()

        ProgressRepository.completeLesson(
            this,
            currentRouteId,
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

        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            // The mathematics route used to bypass per-course learning-flow preferences
            // and advance within a unit even when automaticBetweenLessons was disabled.
            val next = NavigationRepository.getNextLessonInUnit(this, currentRouteId, currentLessonId)
            val flow = LearningFlowSettingsRepository.getSettingsForCourse(this, currentCourseId)
            if (next != null && flow.automaticBetweenLessons) openNextLesson(next)
            else returnToContainingMathUnit(currentCourseId, currentUnitId)
            return
        }

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
                    currentRouteId,
                    currentLessonId
                )

        if (
            nextLesson == null
        ) {

            returnToCourses()

            return
        }

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

    private fun returnToContainingMathUnit(courseId: String, unitId: String) {
        val unit = NavigationRepository.getContainingUnit(this, currentRouteId, currentLessonId)
        NavigationStateRepository.saveUnit(this, courseId, unitId, currentRouteId)
        val unitIntent = Intent(this, UnitActivity::class.java)
            .putExtra("routeId", currentRouteId)
            .putExtra("courseId", courseId)
            .putExtra("unitId", unitId)
            .putExtra("unitNumber", unit?.number ?: 1)
            .putExtra("unitTitle", unit?.title.orEmpty())
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        startActivity(unitIntent)
        finish()
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
        currentCourseId = nextLesson.courseId
        intent.putExtra("routeId", currentRouteId)
            .putExtra("courseId", nextLesson.courseId)
            .putExtra("unitId", nextLesson.unitId)
            .putExtra("lessonId", nextLesson.lessonId)
            .putExtra("lessonNumber", nextLesson.lessonNumber)

        disposeCppEditorViewports()
        pageViewCache.clear()

        pageRequiredExercises.clear()

        pageCompletedExercises.clear()

        exerciseAttempts.clear()

        requiredExercises =
            0

        completedExercises =
            0

        NavigationStateRepository.saveLesson(
            this,
            currentLessonId,
            currentRouteId
        )

        findViewById<TextView>(
            R.id.textTopLesson
        ).text =
            getString(R.string.nav_lesson_number, lessonNumber)

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
            nextLesson.unitId,
            currentRouteId
        )

        val intent =
            Intent(
                this,
                UnitActivity::class.java
            )

        intent.putExtra(
            "routeId",
            currentRouteId
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

        val courseIntent =
            Intent(
                this,
                CourseActivity::class.java
            )

        courseIntent.putExtra(
            "routeId",
            currentRouteId
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
            "routeId",
            currentRouteId
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
            "routeId",
            currentRouteId
        )

        lessonIntent.putExtra(
            "courseId",
            nextLesson.courseId
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

        val coursesIntent =
            Intent(
                this,
                CoursesActivity::class.java
            )

        coursesIntent.putExtra(
            "routeId",
            currentRouteId
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
            "routeId",
            currentRouteId
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
            "routeId",
            currentRouteId
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
            "routeId",
            currentRouteId
        )

        lessonIntent.putExtra(
            "courseId",
            nextLesson.courseId
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

        NavigationStateRepository.saveCourse(
            this,
            nextLesson.courseId,
            currentRouteId
        )

        val intent =
            Intent(
                this,
                CourseActivity::class.java
            )

        intent.putExtra(
            "routeId",
            currentRouteId
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

        intent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
        )

        startActivity(
            intent
        )
    }

    private fun returnToCourses() {

        NavigationStateRepository.saveCourses(
            this,
            currentRouteId
        )

        val intent =
            Intent(
                this,
                CoursesActivity::class.java
            )

        intent.putExtra(
            "routeId",
            currentRouteId
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

            if (view is MathTextScaleAware) view.scaleMathText(scale / 100f)

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

            if (view is MathTextScaleAware) view.scaleMathText(ratio)

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

        clearReadingPages()

        disposeCppEditorViewports()
        courseWrapControllers.clear()
        programmingViewportUiControllers.clear()
        codeSurfaceWidths.clear()
        programmingCodeTextCompanions.clear()
        programmingTextSizes.clear()
        programmingInputPaddings.clear()

        lessonJson =
            try {

                LessonRepository.loadLessonJson(
                    this,
                    currentRouteId,
                    lessonId
                )

            } catch (
                exception: Exception
            ) {

                Toast.makeText(
                    this,
                    getString(R.string.nav_lesson_not_found),
                    Toast.LENGTH_SHORT
                ).show()

                finish()

                return
            }

        ProgressRepository.markLessonStarted(
            this,
            currentRouteId,
            currentLessonId
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

        val savedPage =
            ProgressRepository
                .getCurrentPage(
                    this,
                    lessonId
                )
                .coerceIn(
                    0,
                    totalPages - 1
                )

        currentPage =
            MathDevelopmentMode
                .initialPage(
                    this,
                    lessonId,
                    savedPage,
                    totalPages
                )

        renderCurrentPage()
    }

    private fun renderCurrentPage() {

        narrationController?.stop()
        readingRenderBlockIndex = null
        readingTextPages.values.forEach { it.restore() }

        // Retain Check shortcuts for reattached cached Views, not old pages.
        puzzleEnterShortcut.activatePage(
            "${currentRouteId}/${currentLessonId}", currentPage,
            reused = pageViewCache.containsKey(currentPage)
        )
        // Retain the current DOWN ownership until its matching UP, even if the
        // visible page changes while a hardware key is held.
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
            getString(R.string.exercise_page_count, currentPage + 1, totalPages)

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

                getString(R.string.nav_finish_lesson)

            } else {

                getString(R.string.continue_lesson)
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
                currentRouteId
            )
            prefetchVisibleNarration()

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

        mathNarrationSources.remove(narrationPageIdentity())
        readingTextPages.remove(narrationPageIdentity())?.clear()
        readingTextPages[narrationPageIdentity()] = LessonReadingTextColors()

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

            readingRenderBlockIndex = i

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
                        ),
                        block.optString("mathMode", "auto")
                    )
                }

                "paragraph" -> {

                    addParagraph(
                        block.getString(
                            "text"
                        ),
                        block.optString("mathMode", "auto")
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
                        ),
                        block.optString("mathMode", "auto")
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
                        block,
                        i
                    )
                }

                else -> {
                    if (MathPuzzleTypes.isExercise(block.getString("type"))) {
                        addMathPuzzle(block, i)
                    }
                }
            }
        }

        readingRenderBlockIndex = null

        /*
         * Aplicamos el tamaño efectivo de texto
         * únicamente después de haber construido
         * completamente una página nueva.
         *
         * Las páginas recuperadas desde caché
         * salen antes de este punto, por lo que
         * nunca se escalan dos veces.
         */
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            MathInputFocusOrder.configure(lessonContent)
        }
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
            currentRouteId
        )
        prefetchVisibleNarration()
    }

    private fun addMathPuzzle(block: JSONObject, blockIndex: Int) {
        requiredExercises++
        val puzzlePage = currentPage
        try {
            val renderer = MathPuzzleRenderer(
                this, currentRouteId, currentLessonId, puzzlePage, blockIndex, block,
                onTextReady = { view, role -> updatePuzzleText(view, blockIndex, role) },
                onIncorrectAttempt = { guidance -> narrateIncorrectAttempt(puzzlePage, blockIndex, guidance) },
                attemptEvaluation = { identity, evaluate ->
                    evaluateExerciseAttempt(puzzlePage, blockIndex, identity,
                        evaluation = evaluate,
                        verdict = { results ->
                            if (results.all { it.evaluable }) results.all { it.correct } else null
                        })
                }
            ) { correct ->
                val count = if (currentPage == puzzlePage) completedExercises
                    else pageCompletedExercises[puzzlePage] ?: 0
                val updated = (count + if (correct) 1 else -1).coerceAtLeast(0)
                pageCompletedExercises[puzzlePage] = updated
                if (currentPage == puzzlePage) completedExercises = updated
            }
            if (renderer.isCorrect) completedExercises++
            renderer.narrationSpokenAnchors.forEach { (position, label) ->
                readingTextPages[narrationPageIdentity()]?.registerSpokenAnchor(blockIndex, position, label)
            }
            mathNarrationSources.getOrPut(narrationPageIdentity()) { mutableMapOf() }[blockIndex] =
                renderer::narrationSourceBlock
            lessonContent.addView(renderer.view)
            puzzleEnterShortcut.register(renderer.checkActionView, renderer.view)
            renderer.fullscreenButton.setOnClickListener {
                if (exerciseFullscreenActive) {
                    closeExerciseFullscreenHost()
                    renderer.fullscreenButton.contentDescription = getString(R.string.exercise_fullscreen)
                } else {
                    openExerciseFullscreenHost(usesKeyboard = true) { _, bodyHost, footerHost ->
                        val scroll = ScrollView(this).apply {
                            isFillViewport = true
                            layoutParams = LinearLayout.LayoutParams(-1, 0, 1f)
                        }
                        val content = LinearLayout(this).apply {
                            orientation = LinearLayout.VERTICAL
                            layoutParams = FrameLayout.LayoutParams(-1, -2)
                            setPadding(0, dp(6), 0, dp(6))
                        }
                        scroll.addView(content)
                        bodyHost.addView(scroll)
                        moveViewToExerciseFullscreen(renderer.view, content, LinearLayout.LayoutParams(-1, -2))
                        MathInputFocusOrder.configure(renderer.view)
                        footerHost.visibility = View.VISIBLE
                        moveViewToExerciseFullscreen(renderer.fullscreenButton, footerHost, FrameLayout.LayoutParams(dp(48), dp(48)).apply { gravity = Gravity.RIGHT })
                        renderer.fullscreenButton.contentDescription = getString(R.string.exercise_exit_fullscreen)
                    }
                }
            }
        } catch (exception: Exception) {
            // Invalid content is visible and remains required instead of silently completing a page.
            android.util.Log.w("LessonMath", "Could not load a math puzzle", exception)
            addNote(getString(R.string.lesson_math_load_failed, mathErrorMessage(exception)))
        }
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
            18f

        token.includeFontPadding =
            false

        token.setTextColor(
            Color.rgb(
                216,
                216,
                216
            )
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
            // El borde de los bloques del puzzle sigue el tema
            // de la ruta del curso, no el acento global de la app.
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    this,
                    currentRouteId
                )
        )

        token.background =
            background

        DirectDragTouch.bind(
            token,
            {
                token.isEnabled
            },
            { target ->

                target.alpha =
                    0.55f

                target.startDragAndDrop(
                    ClipData.newPlainText(
                        "code_fragment",
                        text
                    ),
                    View.DragShadowBuilder(
                        target
                    ),
                    target,
                    0
                )
            },
            {
                ignoreCurrentSwipe =
                    true
            }
        )

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

        val exercisePage = currentPage

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
            ReadingTextView(this)

        instruction.text =
            getString(R.string.exercise_fill_instruction)

        instruction.setTextColor(
            Color.rgb(180, 185, 195)
        )

        instruction.textSize =
            18f

        instruction.setPadding(
            0,
            0,
            0,
            dp(14)
        )

        registerReadingText(instruction, blockIndex)
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
            getString(R.string.lesson_code_heading, getString(R.string.lesson_cpp_label))

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
            18f

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
            getString(R.string.exercise_reset)

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
            getString(R.string.exercise_wrap_disabled)

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
            getString(R.string.lesson_copy_code)

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
            getString(R.string.exercise_fullscreen)

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
            CodeLineNumberColumn(this)

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

        makeCodeCardFullWidth(codeCard)

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
            MaterialButton(this)
        puzzleEnterShortcut.register(checkButton, codeCard)

        checkButton.text =
            getString(R.string.exercise_check)

        checkButton.isAllCaps =
            false

        checkButton.textSize =
            18f

        checkButton.isEnabled =
            false

        val result = exerciseFeedbackText(blockIndex).apply {
            tag = "drag_fill_feedback_$blockIndex"
        }

        fun currentSlotsAnswer() = org.json.JSONArray(slots.map { slot ->
            if (slot.childCount == 0) -1 else (slot.getChildAt(0) as TextView).tag as Int
        })

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
                answeredCorrectly,
            reviewed: Boolean = false
        ) {
            val slotsJson = currentSlotsAnswer()
            if (!correct && !reviewed) result.text = ""

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

            if (reviewed && !correct) CppExerciseFeedback.recordIncorrect(state, slotsJson)

            ProgressRepository.saveExerciseState(
                this,
                currentRouteId,
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
                getString(R.string.lesson_integer, lineIndex + 1)

            number.typeface =
                Typeface.MONOSPACE

            number.textSize =
                18f

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
                    18f

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
                    getString(R.string.exercise_fullscreen)

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
                    getString(R.string.exercise_exit_fullscreen)
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

        result.textSize =
            18f

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
                currentRouteId,
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
                getString(R.string.exercise_correct)

            val explanation = CppExerciseFeedback.dragSuccessExplanation(block)
            result.text = if (explanation.isBlank()) getString(R.string.lesson_correct_feedback)
                else getString(R.string.lesson_correct_explanation, explanation)

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

        restoreCppReview(block, savedState, currentSlotsAnswer(), result)

        checkButton.setOnClickListener {

            if (answeredCorrectly) return@setOnClickListener

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
                        getString(R.string.exercise_complete_blanks),
                        Toast.LENGTH_SHORT
                    ).show()

                    return@setOnClickListener
                }
            }

            val actual = slots.map { (it.getChildAt(0) as TextView).text.toString() }
            var incorrectReview = ""
            var successExplanation = ""
            val correct = evaluateExerciseAttempt(exercisePage, blockIndex, org.json.JSONArray(actual),
                evaluation = {
                    incorrectReview = cppIncorrectReview(block)
                    successExplanation = CppExerciseFeedback.dragSuccessExplanation(block)
                    val correctOrder = block.getJSONArray("correctOrder")
                    require(actual.isNotEmpty() && actual.size == correctOrder.length()) {
                        "Invalid fill configuration"
                    }
                    val expected = List(actual.size) {
                        val index = correctOrder.getInt(it)
                        require(index in 0 until choices.length()) { "Invalid choice index" }
                        choices.getString(index)
                    }
                    actual == expected
                }, verdict = { it }) ?: return@setOnClickListener

            saveDragState(
                correct, reviewed = true
            )

            if (correct) {

                result.text = if (successExplanation.isBlank()) getString(R.string.lesson_correct_feedback)
                    else getString(R.string.lesson_correct_explanation, successExplanation)

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
                        getString(R.string.exercise_correct)

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

                result.text = incorrectReview

                narrateIncorrectAttempt(exercisePage, blockIndex, listOf(result.text.toString()))

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
            ReadingTextView(this)

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

        registerReadingText(view, -1)
        lessonContent.addView(
            view
        )
    }

    private fun addHeading(
        text: String,
        mathMode: String = "auto"
    ) {

        val view =
            ReadingTextView(this)

        view.text =
            text
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            MathTextRenderer.apply(view, text, mathMode)
        }

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

        registerReadingText(view, mathMode = mathMode)
        lessonContent.addView(
            view
        )
    }

    private fun addParagraph(
        text: String,
        mathMode: String = "auto"
    ) {

        val view =
            ReadingTextView(this)

        view.text =
            text
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            MathTextRenderer.apply(view, text, mathMode)
        }

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

        registerReadingText(view, mathMode = mathMode)
        lessonContent.addView(
            view
        )
    }

    private fun makeCodeCardFullWidth(
        card: View,
        extendInPortrait: Boolean = true
    ) {

        lessonContent.clipToPadding =
            false

        lessonContent.clipChildren =
            false

        lessonScroll.clipToPadding =
            false

        lessonScroll.clipChildren =
            false

        codeSurfaceWidths.putIfAbsent(card, CodeSurfaceWidth(card.layoutParams.width,
            card.translationX, extendInPortrait))
        card.post { updateCodeSurfaceWidths() }
    }

    private fun updateCodeSurfaceWidths() {
        if (!::lessonContent.isInitialized || lessonContent.width <= 0 || compilerFullscreenActive) return
        val landscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        codeSurfaceWidths.forEach { (card, original) ->
            if (card.parent !== lessonContent) return@forEach
            val params = card.layoutParams as? LinearLayout.LayoutParams ?: return@forEach
            val extend = original.extendInPortrait || landscape
            val width = if (extend) lessonContent.width else original.portraitWidth
            if (params.width != width) card.layoutParams = params.apply { this.width = width }
            card.translationX = if (extend) -lessonContent.paddingLeft.toFloat() else original.portraitTranslation
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

        val reparentFocus = captureProgrammingReparentFocus()

        exerciseFullscreenActive =
            true

        updateNarrationVisibility()

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

        if (currentRouteId == AppearanceSettingsRepository.ROUTE_PROGRAMMING) {
            overlay.tag = "programming_fullscreen_overlay"
            bodyHost.tag = "programming_fullscreen_body"
            headerHost.tag = "programming_fullscreen_header"
            programmingFullscreenHeader = headerHost
        }

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

        if (currentRouteId == AppearanceSettingsRepository.ROUTE_PROGRAMMING) {
            programmingFullscreenFloating = if (usesKeyboard) floatingHost else null
        }

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

        if (currentRouteId == AppearanceSettingsRepository.ROUTE_PROGRAMMING && usesKeyboard) {
            // Keep the same shortcut bar above the IME, including fake-console responses.
            if (programmingKeyboardBar.parent !== floatingHost) {
                moveViewToExerciseFullscreen(programmingKeyboardBar, floatingHost,
                    FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, normalProgrammingBarHeight))
            }
            fun rememberPadding(view: View) {
                programmingFullscreenPaddings[view] = Rect(view.paddingLeft, view.paddingTop,
                    view.paddingRight, view.paddingBottom)
                if (view is ViewGroup) {
                    for (index in 0 until view.childCount) rememberPadding(view.getChildAt(index))
                }
            }
            rememberPadding(bodyHost)
        }

        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            fun applyMathFullscreenInsets(insets: WindowInsetsCompat) {
                val safe = insets.getInsets(WindowInsetsCompat.Type.systemBars() or
                    WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.mandatorySystemGestures())
                overlay.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            }
            ViewCompat.setOnApplyWindowInsetsListener(overlay) { view, insets ->
                applyMathFullscreenInsets(insets)
                insets
            }
            // A new overlay can be added after AppCompat has already dispatched/consumed
            // this window's insets. Apply the cached window geometry as well.
            ViewCompat.getRootWindowInsets(window.decorView)?.let(::applyMathFullscreenInsets)
            overlay.post { ViewCompat.getRootWindowInsets(window.decorView)?.let(::applyMathFullscreenInsets) }
            ViewCompat.requestApplyInsets(overlay)
        }

        updateProgrammingKeyboardBarVisibility()
        restoreProgrammingReparentFocus(reparentFocus)
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

        val reparentFocus = captureProgrammingReparentFocus()

        programmingFullscreenPaddings.forEach { (view, padding) ->
            view.setPadding(padding.left, padding.top, padding.right, padding.bottom)
        }
        programmingFullscreenMargins.forEach { (view, margins) ->
            (view.layoutParams as? ViewGroup.MarginLayoutParams)?.let { params ->
                params.setMargins(margins.left, margins.top, margins.right, margins.bottom)
                view.layoutParams = params
            }
        }
        programmingFullscreenPaddings.clear()
        programmingFullscreenMargins.clear()
        programmingFullscreenHeader = null
        programmingFullscreenFloating = null

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

                if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS &&
                    state.view.contentDescription == getString(R.string.exercise_exit_fullscreen)) {
                    state.view.contentDescription = getString(R.string.exercise_fullscreen)
                }

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

        updateNarrationVisibility()

        exerciseFullscreenUsesKeyboard =
            false

        window.decorView.systemUiVisibility =
            exerciseFullscreenSystemUiVisibility

        if (currentRouteId == AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) {
            MathInputFocusOrder.configure(lessonContent)
        }
        updateProgrammingKeyboardBarVisibility()
        restoreProgrammingReparentFocus(reparentFocus)
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
                        getString(R.string.exercise_fullscreen)

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
                        programmingFullscreenMargins[responseView] = Rect(dp(12), dp(12), dp(12), dp(12))
                    }

                    codeHandle.fullscreenButton
                        .contentDescription =
                        getString(R.string.exercise_exit_fullscreen)
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
                "c++" -> getString(R.string.lesson_cpp_label)

                else ->
                    language.uppercase()
            }

        languageLabel.text =
            getString(R.string.lesson_code_heading, readableLanguage)

        languageLabel.setTextColor(
            Color.rgb(
                225,
                228,
                235
            )
        )

        languageLabel.textSize =
            18f

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
                getString(R.string.exercise_reset)

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
            getString(R.string.exercise_wrap_disabled)

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
            getString(R.string.exercise_fullscreen)

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
            getString(R.string.lesson_copy_code)

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
            CodeLineNumbersView(this)

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
            18f

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
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT
            )

        /*
         * CÓDIGO.
         */
        val codeView =
            TextView(this)

        codeView.text =
            if (
                language.equals(
                    "cpp",
                    ignoreCase = true
                ) ||
                language.equals(
                    "c++",
                    ignoreCase = true
                )
            ) {

                compactCodeSpaces(
                    code
                )

            } else {

                code
            }

        codeView.setTextColor(
            Color.rgb(
                216,
                216,
                216
            )
        )

        codeView.typeface =
            Typeface.MONOSPACE

        codeView.textSize =
            18f

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
                    getString(R.string.exercise_wrap_enabled)

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
                    getString(R.string.exercise_wrap_disabled)
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
                    getString(R.string.lesson_cpp_label),
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

        val exercisePage = currentPage

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
            getString(R.string.lesson_code_heading, getString(R.string.lesson_cpp_label))

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
            18f

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
            getString(R.string.exercise_reset)

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
            getString(R.string.exercise_copy_exercise)

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
                ReadingRadioButton(this)

            radioButton.id =
                View.generateViewId()

            radioButton.text =
                options.getString(i)

            radioButton.setTextColor(
                Color.WHITE
            )

            radioButton.textSize =
                18f

            radioButton.setPadding(
                0,
                dp(8),
                0,
                dp(8)
            )

            registerReadingText(radioButton, blockIndex)
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
        if (currentRouteId == AppearanceSettingsRepository.ROUTE_PROGRAMMING) {
            makeCodeCardFullWidth(answerCard, extendInPortrait = false)
        }

        /*
         * BOTÓN COMPROBAR
         */
        val button =
            MaterialButton(this)
        puzzleEnterShortcut.register(button, answerCard)

        button.text =
            getString(R.string.exercise_check)

        button.isAllCaps =
            false

        button.textSize =
            18f

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
            exerciseFeedbackText(blockIndex)

        result.textSize =
            18f

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
                currentRouteId,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )
        }

        radioGroup.setOnCheckedChangeListener { _, _ ->
            if (!answeredCorrectly) result.text = ""

            button.isEnabled =
                radioGroup.checkedRadioButtonId != -1 &&
                        !answeredCorrectly

            // A response is the authoritative puzzle choice. Native RadioButton
            // focus may be stale after LDPlayer maps Pause to a synthetic touch.
            if (button.isEnabled) puzzleEnterShortcut.select(button)
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
                        builder.append(getString(R.string.lesson_selected_prefix))
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
                    getString(R.string.exercise_clipboard_exercise),
                    builder.toString()
                )

            clipboard.setPrimaryClip(
                clip
            )

            Toast.makeText(
                this,
                getString(R.string.exercise_copied),
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
                getString(R.string.exercise_correct)

            result.text =
                getString(R.string.lesson_correct_explanation, block.optString("explanation"))

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        restoreCppReview(block, savedState, savedSelectedIndex, result, savedSelectedIndex)

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
                getString(R.string.exercise_check)

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
                currentRouteId,
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

            if (answeredCorrectly) return@setOnClickListener

            if (
                checkedId == -1
            ) {

                Toast.makeText(
                    this,
                    getString(R.string.exercise_select_answer),
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

            var incorrectReview = ""
            val correct = evaluateExerciseAttempt(exercisePage, blockIndex, selectedIndex,
                evaluation = {
                    incorrectReview = cppIncorrectReview(block, selectedIndex)
                    val correctIndex = block.getInt("correctIndex")
                    require(selectedIndex in optionButtons.indices && correctIndex in optionButtons.indices) {
                        "Invalid answer index"
                    }
                    selectedIndex == correctIndex
                }, verdict = { it }) ?: return@setOnClickListener

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

            if (!correct) CppExerciseFeedback.recordIncorrect(state, selectedIndex)

            ProgressRepository.saveExerciseState(
                this,
                currentRouteId,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    getString(R.string.lesson_correct_explanation, block.optString("explanation"))

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
                        getString(R.string.exercise_correct)
                }

            } else {

                result.text =
                    incorrectReview

                narrateIncorrectAttempt(exercisePage, blockIndex, listOf(result.text.toString()))

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

        val exercisePage = currentPage

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
            getString(R.string.lesson_code_heading, getString(R.string.lesson_cpp_label))

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
            18f

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
            getString(R.string.exercise_reset)

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
            getString(R.string.exercise_wrap_disabled)

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
            getString(R.string.lesson_copy_code)

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
            getString(R.string.exercise_fullscreen)

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
            CodeLineNumberColumn(this)

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
                getString(R.string.lesson_integer, lineIndex + 1)

            lineNumber.typeface =
                Typeface.MONOSPACE

            lineNumber.textSize =
                18f

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

                val input: EditText =
                    FreeScrollEditText(this)

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
                    18f

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
            MaterialButton(this)
        puzzleEnterShortcut.register(checkButton, codeCard)

        checkButton.text =
            getString(R.string.exercise_check)

        checkButton.isAllCaps =
            false

        checkButton.textSize =
            18f

        checkButton.isEnabled =
            inputs.any {
                it.text.isNotEmpty()
            } &&
                    !savedCorrect


        val inlineResult = exerciseFeedbackText(blockIndex)
        // Only multi-blank inline puzzles treat Tab as field navigation. The
        // floating Tab and native Tab share CppCodeInputRules; no TAB character,
        // autoindent marker, draft text or selection is rewritten by navigation.
        if (inputs.size > 1) {
            inputs.forEachIndexed { index, input ->
                fun focusBlank(target: EditText) {
                    if (!target.isEnabled || !target.isAttachedToWindow) return
                    target.requestFocus()
                    if (target.selectionStart < 0) target.setSelection(target.text.length)
                    target.requestRectangleOnScreen(Rect(0, 0, target.width, target.height), true)
                }
                CppCodeInputRules.configureBlankNavigation(
                    input,
                    forward = { focusBlank(inputs[(index + 1) % inputs.size]) },
                    backward = { focusBlank(inputs[(index - 1 + inputs.size) % inputs.size]) }
                )
            }
        }
        inputs.forEach { PuzzleAnswerSubmit.install(it, checkButton) }
        fun currentInlineAnswers() = org.json.JSONArray(inputs.map { it.text.toString() })
        fun inlineIncorrectReview() = getString(R.string.exercise_wrong_spacing) + "\n" + getString(R.string.cpp_review_fill)

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

                        inlineResult.text = ""
                        saveCppReviewDraft(exercisePage, blockIndex, "answers", currentInlineAnswers())

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
                    getString(R.string.exercise_fullscreen)

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
                    getString(R.string.exercise_exit_fullscreen)

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

        val result = inlineResult

        result.textSize =
            18f

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
                currentRouteId,
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
                getString(R.string.exercise_correct)

            result.text =
                getString(R.string.lesson_correct_feedback)

            result.setTextColor(
                Color.rgb(
                    180,
                    220,
                    190
                )
            )
        }

        if (CppExerciseFeedback.wasReviewed(savedState, currentInlineAnswers())) {
            result.text = inlineIncorrectReview()
            result.setTextColor(Color.rgb(240, 150, 150))
        }

        checkButton.setOnClickListener {

            if (answeredCorrectly) return@setOnClickListener
            val actual = CppCodeAnswerNormalizer.normalizeInline(
                block.getString("template"), inputs.map { it.text.toString() }
            )
            if (actual.isEmpty() || actual.any { it.isBlank() }) {
                showIncompleteAttempt()
                return@setOnClickListener
            }

            val correct = evaluateExerciseAttempt(exercisePage, blockIndex, org.json.JSONArray(actual),
                evaluation = {
                    CppExerciseAnswers.inlineMatches(block, actual)
                }, verdict = { it }) ?: return@setOnClickListener

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

            if (!correct) CppExerciseFeedback.recordIncorrect(state, answersJson)

            ProgressRepository.saveExerciseState(
                this,
                currentRouteId,
                currentLessonId,
                currentPage,
                blockIndex,
                state
            )

            if (
                correct
            ) {

                result.text =
                    getString(R.string.lesson_correct_feedback)

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
                        getString(R.string.exercise_correct)
                }

            } else {

                result.text = inlineIncorrectReview()

                narrateIncorrectAttempt(exercisePage, blockIndex, listOf(result.text.toString()))

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
                getString(R.string.exercise_cpp_code),
                code
            )

        clipboard.setPrimaryClip(
            clip
        )

        Toast.makeText(
            this,
            getString(R.string.code_copied),
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
            CppSyntaxHighlighter.highlight(
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
                        0.80f
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
            18f

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

        const val ATTEMPT_HISTORY_STATE = "exercise_attempt_history"

        const val CODE_INPUT_TAG =
            "cpp_code_input"
    }
}

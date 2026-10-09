package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Color
import android.view.View
import android.view.Gravity
import android.widget.LinearLayout
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.AppearanceUi
import com.learngraphicsprogramming.app.ExerciseAnswerIdentity
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.json.JSONArray

/** One host for any combination of representations, tools and independent responses. */
class MathPuzzleRenderer(
    private val context: Context,
    private val routeId: String,
    private val lessonId: String,
    private val pageIndex: Int,
    private val blockIndex: Int,
    block: JSONObject,
    private val attemptEvaluation: ((String, () -> List<MathValidationResult>) -> List<MathValidationResult>?)? = null,
    private val onCompletionChanged: (Boolean) -> Unit
) {
    val view = MathWidgetUi.column(context)
    private val accent = AppearanceSettingsRepository.getRouteAccentColor(context, routeId)
    val fullscreenButton = MathWidgetUi.icon(context, R.drawable.ic_code_fullscreen, context.getString(R.string.math_ui_fullscreen))
    private val checkButton = MathWidgetUi.button(context, context.getString(R.string.math_ui_check), accent).apply {
        tag = CHECK_ACTION_TAG
    }
    private val resetButton = MathWidgetUi.icon(context, R.drawable.ic_code_reset, context.getString(R.string.math_ui_restart), accent)
    private val feedback = MathWidgetUi.text(context, "")
    private val spec = MathPuzzleParser.parse(block)
    private val fingerprint = MathPuzzleState.fingerprint(block)
    private val responses = linkedMapOf<String, MathResponseControl>()
    private val tools = linkedMapOf<String, MathResponseControl>()
    private val initialResponses = linkedMapOf<String, MathAnswer>()
    private val initialTools = linkedMapOf<String, MathAnswer>()
    private var initializing = true
    var isCorrect: Boolean = false
        private set

    init {
        // Identifies math cards for layout/tests. Gesture ownership belongs to interactive children.
        view.tag = INTERACTION_TAG
        view.background = MathWidgetUi.background(context)
        view.setPadding(MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 8), MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 12))
        view.layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 24) }
        val toolbar = MathWidgetUi.row(context)
        toolbar.gravity = Gravity.END or Gravity.CENTER_VERTICAL
        toolbar.addView(fullscreenButton)
        toolbar.addView(resetButton)
        view.addView(toolbar)
        view.addView(MathWidgetUi.text(context, spec.prompt, true))
        spec.representations.forEach { view.addView(MathRepresentationRenderer.render(context, it, accent)) }
        spec.tools.forEachIndexed { index, tool ->
            if (tool.optBoolean("interactive", false)) {
                val type = when (tool.getString("type")) {
                    "math_coordinate_graph" -> "math_coordinate_graph_builder"
                    "math_number_line" -> "math_number_line"
                    "math_slider_interaction" -> "math_slider_interaction"
                    else -> throw IllegalArgumentException("Herramienta interactiva desconocida: ${tool.getString("type")}")
                }
                val id = tool.optString("id", "tool_$index")
                require(id.isNotBlank() && id !in tools) { "ID de herramienta vacío o repetido: $id" }
                val config = tool.optJSONObject("config") ?: tool
                val control = MathVisualControls.create(context, MathResponseSpec(id, type, config, JSONObject()), accent, ::changed)
                    ?: throw IllegalArgumentException("Herramienta no disponible: $type")
                tools[id] = control
                initialTools[id] = control.answer()
                view.addView(MathWidgetUi.text(context, tool.optString("label", context.getString(R.string.math_ui_auxiliary_tool))))
                view.addView(control.view)
            } else view.addView(MathRepresentationRenderer.render(context, tool, accent))
        }
        spec.responses.forEach { response ->
            val control = MathInputControls.create(context, response, accent, ::changed)
                ?: MathDragControls.create(context, response, accent, ::changed)
                ?: MathVisualControls.create(context, response, accent, ::changed)
                ?: throw IllegalArgumentException("Interacción no disponible: ${response.type}")
            responses[response.id] = control
            initialResponses[response.id] = control.answer()
            view.addView(control.view)
        }
        view.addView(checkButton.apply { layoutParams = LinearLayout.LayoutParams(-1, -2) })
        feedback.accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
        view.addView(feedback)
        MathInputFocusOrder.configure(view)

        val developmentAnswers =
            MathDevelopmentMode.answersFor(
                context,
                lessonId,
                pageIndex,
                block.optString("id")
            )

        val saved =
            if (developmentAnswers == null) {

                ProgressRepository
                    .getExerciseState(
                        context,
                        lessonId,
                        pageIndex,
                        blockIndex
                    )
                    ?.takeIf {
                        it.optString(
                            "contentFingerprint"
                        ) == fingerprint
                    }

            } else {
                null
            }

        val savedAnswers =
            saved?.optJSONObject(
                "responses"
            )

        responses.forEach { (id, control) ->
            savedAnswers
                ?.optJSONObject(id)
                ?.let {
                    MathAnswerJson.decode(it)
                }
                ?.let(control::restore)
        }

        val savedTools =
            saved?.optJSONObject(
                "tools"
            )

        tools.forEach { (id, control) ->
            savedTools
                ?.optJSONObject(id)
                ?.let {
                    MathAnswerJson.decode(it)
                }
                ?.let(control::restore)
        }

// Borradores normales.
        if (developmentAnswers == null) {

            val responseDrafts =
                saved?.optJSONObject(
                    "responseDrafts"
                )

            responses.forEach { (id, control) ->
                responseDrafts
                    ?.optJSONObject(id)
                    ?.let(control::restoreDraft)
            }

            val toolDrafts =
                saved?.optJSONObject(
                    "toolDrafts"
                )

            tools.forEach { (id, control) ->
                toolDrafts
                    ?.optJSONObject(id)
                    ?.let(control::restoreDraft)
            }
        }

// ------------------------------------------------------------
// MODO DE DESARROLLO
//
// Restauramos respuestas REALES en los controles.
// No hacemos simplemente isCorrect = true.
// ------------------------------------------------------------

        developmentAnswers?.forEach { (id, answer) ->
            responses[id]
                ?.restore(answer)
        }

        isCorrect =
            if (developmentAnswers != null) {

                spec.responses.all { response ->

                    val control =
                        responses.getValue(
                            response.id
                        )

                    developmentAnswers.containsKey(
                        response.id
                    ) &&
                            control.draftError() == null &&
                            MathValidators.validate(
                                response.validator,
                                control.answer()
                            ).correct
                }

            } else {

                saved
                    ?.optBoolean(
                        "correct",
                        false
                    ) == true &&
                        spec.responses.all { response ->

                            val control =
                                responses.getValue(
                                    response.id
                                )

                            savedAnswers
                                ?.has(
                                    response.id
                                ) == true &&
                                    control.draftError() == null &&
                                    MathValidators.validate(
                                        response.validator,
                                        control.answer()
                                    ).correct
                        }
            }

        if (isCorrect) {
            showCorrect()
        }
        initializing = false
        checkButton.setOnClickListener { check() }
        resetButton.setOnClickListener { reset() }
        AppearanceUi.applyRouteToViewTree(context, view, routeId)
        updateActionState()
        save()
    }

    private fun changed() {
        if (initializing || isCorrect) return
        MathTextRenderer.apply(feedback, "")
        updateActionState()
        save()
    }

    private fun check() {
        if (isCorrect || !hasResponseChanges()) return
        val validations = try {
            // Capture one deliberate submission. Cursor, selection, IME metadata and
            // auxiliary tools are not part of an answer's identity.
            val answers = spec.responses.associate { response -> response.id to responses.getValue(response.id).answer() }
            val draftErrors = spec.responses.associate { response -> response.id to responses.getValue(response.id).draftError() }
            val identity = ExerciseAnswerIdentity.of(JSONArray().apply {
                spec.responses.sortedBy { it.id }.forEach { response ->
                    put(JSONObject().put("id", response.id).put("answer", MathAnswerJson.encode(answers.getValue(response.id))))
                }
            })
            val evaluate = {
                spec.responses.map { response ->
                    draftErrors[response.id]?.let { MathValidationResult(false, it, evaluable = false) }
                        ?: MathValidators.validate(response.validator, answers.getValue(response.id))
                }
            }
            val checked = if (attemptEvaluation == null) evaluate() else attemptEvaluation.invoke(identity, evaluate) ?: return
            require(checked.size == spec.responses.size) { "La evaluación debe devolver un resultado por respuesta." }
            checked
        } catch (_: Exception) {
            spec.responses.map { MathValidationResult(false, "evaluation failed", evaluable = false, uiMessage = MathUiMessage(R.string.math_ui_evaluation_error)) }
        }
        val results = spec.responses.zip(validations)
        if (results.all { it.second.correct }) {
            isCorrect = true
            showCorrect()
            updateActionState()
            save()
            onCompletionChanged(true)
        } else {
            val details = results.filterNot { it.second.correct }.map { (response, result) ->
                val definitions = response.config.optJSONArray("fields")
                var message = result.resolveMessage(context)
                result.fieldId?.let { message = "$it: $message" }
                if (definitions != null) for (index in 0 until definitions.length()) {
                    val field = definitions.getJSONObject(index)
                    val prefix = "${field.getString("id")}: "
                    if (message.startsWith(prefix)) {
                        message = "${field.optString("label", context.getString(R.string.math_ui_field, index + 1))}: ${message.removePrefix(prefix)}"
                        break
                    }
                }
                "${response.config.optString("label", response.id)}: $message"
            }
            MathTextRenderer.apply(feedback, (listOf("✗ ${spec.incorrectFeedback.ifBlank { context.getString(R.string.math_ui_incorrect) }}") + details).filter { it.isNotBlank() }.joinToString("\n"))
            feedback.setTextColor(Color.rgb(240, 166, 166))
            save()
        }
    }

    private fun showCorrect() {
        MathTextRenderer.apply(feedback, context.getString(R.string.math_ui_correct_heading) + spec.correctFeedback.takeIf { it.isNotBlank() }?.let { "\n$it" }.orEmpty())
        feedback.setTextColor(Color.rgb(180, 220, 190))
        checkButton.text = context.getString(R.string.math_ui_correct_button)
        checkButton.isEnabled = false
        responses.values.forEach { it.setEditable(false) }
        tools.values.forEach { it.setEditable(false) }
    }

    private fun reset() {
        val wasCorrect = isCorrect
        initializing = true
        isCorrect = false
        responses.forEach { (id, control) -> control.setEditable(true); control.restore(initialResponses.getValue(id)) }
        tools.forEach { (id, control) -> control.setEditable(true); control.restore(initialTools.getValue(id)) }
        checkButton.text = context.getString(R.string.math_ui_check)
        MathTextRenderer.apply(feedback, "")
        initializing = false
        updateActionState()
        save()
        if (wasCorrect) onCompletionChanged(false)
    }

    /** Compare complete typed answers, including non-text controls; undoing an edit also restores
     * the initial disabled state. Tool changes can be reset but do not enable answer checking. */
    private fun hasResponseChanges(): Boolean = responses.any { (id, control) ->
        control.answer() != initialResponses[id] || control.hasDraftChanges()
    }

    private fun updateActionState() {
        checkButton.isEnabled = !isCorrect && hasResponseChanges()
        resetButton.isEnabled = isCorrect || hasResponseChanges() || tools.any { (id, control) ->
            control.answer() != initialTools[id] || control.hasDraftChanges()
        }
    }

    private fun save() {
        if (initializing) return
        val state = JSONObject().put("schemaVersion", 1).put("contentFingerprint", fingerprint).put("correct", isCorrect)
        val answers = JSONObject()
        responses.forEach { (id, control) -> answers.put(id, MathAnswerJson.encode(control.answer())) }
        val auxiliary = JSONObject()
        tools.forEach { (id, control) -> auxiliary.put(id, MathAnswerJson.encode(control.answer())) }
        state.put("responses", answers).put("tools", auxiliary)
        val responseDrafts = JSONObject()
        responses.forEach { (id, control) -> control.saveDraft()?.let { responseDrafts.put(id, it) } }
        val toolDrafts = JSONObject()
        tools.forEach { (id, control) -> control.saveDraft()?.let { toolDrafts.put(id, it) } }
        if (responseDrafts.length() > 0) state.put("responseDrafts", responseDrafts)
        if (toolDrafts.length() > 0) state.put("toolDrafts", toolDrafts)
        ProgressRepository.saveExerciseState(context, routeId, lessonId, pageIndex, blockIndex, state)
    }

    companion object {
        const val INTERACTION_TAG = "math_puzzle_interaction"
        const val CHECK_ACTION_TAG = "math_puzzle_check_action"
    }
}

package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Color
import android.view.View
import android.widget.LinearLayout
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.AppearanceUi
import com.learngraphicsprogramming.app.ProgressRepository
import org.json.JSONObject

/** One host for any combination of representations, tools and independent responses. */
class MathPuzzleRenderer(
    private val context: Context,
    private val routeId: String,
    private val lessonId: String,
    private val pageIndex: Int,
    private val blockIndex: Int,
    block: JSONObject,
    private val onCompletionChanged: (Boolean) -> Unit
) {
    val view = MathWidgetUi.column(context)
    private val accent = AppearanceSettingsRepository.getRouteAccentColor(context, routeId)
    val fullscreenButton = MathWidgetUi.button(context, "Pantalla completa", accent)
    private val checkButton = MathWidgetUi.button(context, "Comprobar", accent)
    private val resetButton = MathWidgetUi.button(context, "Reiniciar", accent)
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
        // The activity's swipe detector recognizes this container before children handle touches.
        view.tag = INTERACTION_TAG
        view.background = MathWidgetUi.background(context)
        view.setPadding(MathWidgetUi.dp(context, 14), MathWidgetUi.dp(context, 10), MathWidgetUi.dp(context, 14), MathWidgetUi.dp(context, 14))
        view.layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 24) }
        val toolbar = MathWidgetUi.row(context)
        fullscreenButton.layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
        resetButton.layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
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
                view.addView(MathWidgetUi.text(context, tool.optString("label", "Herramienta auxiliar")))
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

        val saved = ProgressRepository.getExerciseState(context, lessonId, pageIndex, blockIndex)
            ?.takeIf { it.optString("contentFingerprint") == fingerprint }
        val savedAnswers = saved?.optJSONObject("responses")
        responses.forEach { (id, control) -> savedAnswers?.optJSONObject(id)?.let { MathAnswerJson.decode(it) }?.let(control::restore) }
        val savedTools = saved?.optJSONObject("tools")
        tools.forEach { (id, control) -> savedTools?.optJSONObject(id)?.let { MathAnswerJson.decode(it) }?.let(control::restore) }
        // Never trust an old 'correct' flag without revalidating the current content and controls.
        isCorrect = saved?.optBoolean("correct", false) == true && spec.responses.all {
            savedAnswers?.has(it.id) == true && MathValidators.validate(it.validator, responses.getValue(it.id).answer()).correct
        }
        if (isCorrect) showCorrect()
        initializing = false
        checkButton.setOnClickListener { check() }
        resetButton.setOnClickListener { reset() }
        AppearanceUi.applyRouteToViewTree(context, view, routeId)
        save()
    }

    private fun changed() {
        if (initializing || isCorrect) return
        feedback.text = ""
        save()
    }

    private fun check() {
        if (isCorrect) return
        val results = spec.responses.map { it to MathValidators.validate(it.validator, responses.getValue(it.id).answer()) }
        if (results.all { it.second.correct }) {
            isCorrect = true
            showCorrect()
            save()
            onCompletionChanged(true)
        } else {
            val details = results.filterNot { it.second.correct }.map { (response, result) ->
                "${response.config.optString("label", response.id)}: ${result.message}"
            }
            feedback.text = (listOf("✗ ${spec.incorrectFeedback}") + details).filter { it.isNotBlank() }.joinToString("\n")
            feedback.setTextColor(Color.rgb(240, 166, 166))
            save()
        }
    }

    private fun showCorrect() {
        feedback.text = "✓ Correcto" + spec.correctFeedback.takeIf { it.isNotBlank() }?.let { "\n$it" }.orEmpty()
        feedback.setTextColor(Color.rgb(180, 220, 190))
        checkButton.text = "Correcto ✓"
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
        checkButton.isEnabled = true
        checkButton.text = "Comprobar"
        feedback.text = ""
        initializing = false
        save()
        if (wasCorrect) onCompletionChanged(false)
    }

    private fun save() {
        if (initializing) return
        val state = JSONObject().put("schemaVersion", 1).put("contentFingerprint", fingerprint).put("correct", isCorrect)
        val answers = JSONObject()
        responses.forEach { (id, control) -> answers.put(id, MathAnswerJson.encode(control.answer())) }
        val auxiliary = JSONObject()
        tools.forEach { (id, control) -> auxiliary.put(id, MathAnswerJson.encode(control.answer())) }
        state.put("responses", answers).put("tools", auxiliary)
        ProgressRepository.saveExerciseState(context, routeId, lessonId, pageIndex, blockIndex, state)
    }

    companion object { const val INTERACTION_TAG = "math_puzzle_interaction" }
}

package lgaed.narration.windows

import com.learngraphicsprogramming.app.math.*
import org.json.JSONArray
import org.json.JSONObject

/** A platform boundary around the original Android-independent parser and evaluators. */
internal object WindowsMathBridge {
    private fun block(request: JSONObject) = request.optJSONObject("block")
        ?: JSONObject(request.getString("blockJson"))
    private fun language(request: JSONObject): String = request.optString("language","es").also {
        require(it in setOf("es","fr","en-US","en-GB","en")) { "Unsupported mathematical UI language" }
    }

    private fun originalStrings(language: String, code: String, vararg values: Any) =
        MathLocaleResources.resolve(code,values.toList(),language)

    private fun uiJson(message: MathUiMessage): JSONObject = JSONObject()
        .put("code",message.resourceId).put("arguments",JSONArray(message.arguments.map {
            if (it is MathUiMessage) uiJson(it) else it
        }))

    fun validate(request: JSONObject): JSONObject {
        val spec = MathPuzzleParser.parse(block(request))
        val language = language(request)
        fun text(code: String, vararg values: Any) = originalStrings(language,code,*values)
        fun resolve(result: MathValidationResult): String = result.uiMessage?.let {
            MathLocaleResources.resolve(it.resourceId,it.arguments,language)
        } ?: result.message
        val raw = request.optJSONObject("responses") ?: JSONObject(request.getString("responsesJson"))
        require(raw.keys().asSequence().all { key -> spec.responses.any { it.id == key } }) {
            "An answer refers to an unknown mathematical response"
        }
        val answers = spec.responses.associate { response ->
            response.id to raw.optJSONObject(response.id)?.let(MathAnswerJson::decode)
        }
        val results = spec.responses.map { response ->
            val answer = answers[response.id]
            response to if (answer == null) {
                val message = MathUiMessage("math_ui_complete_answer")
                MathValidationResult(false,message.diagnostic,evaluable=false,uiMessage=message)
            } else MathValidators.validate(response.validator,answer)
        }
        val evaluable = results.all { it.second.evaluable }
        val correct = evaluable && results.all { it.second.correct }
        fun field(response: MathResponseSpec, result: MathValidationResult, message: String) =
            MathPuzzleFeedback.fieldMessage(response,result.fieldId,message) { text("math_ui_field",it) }
        fun label(response: MathResponseSpec, index: Int) =
            MathPuzzleFeedback.responseLabel(response,text("math_ui_response",index+1))
        val feedback = when {
            !evaluable -> results.mapIndexedNotNull { index,(response,result) ->
                if (result.evaluable) null else "${label(response,index)}: ${field(response,result,resolve(result))}"
            }.joinToString("\n")
            correct -> text("math_ui_correct_heading") + spec.correctFeedback.takeIf { it.isNotBlank() }
                ?.let { "\n$it" }.orEmpty()
            else -> {
                // Reuse the same selected-option safeguards as Android. In an incorrect
                // mixed submission, no absent alternative or correct subset is disclosed.
                val selected = spec.responses.any(MathPuzzleFeedback::hasSelection)
                val details = results.mapIndexedNotNull { index,(response,result) ->
                    fun process() = MathPuzzleFeedback.processMessage(result,text("math_ui_review_process")) { resolve(result) }
                    val message = if (MathPuzzleFeedback.hasSelection(response)) {
                        val reviews = answers[response.id]?.let { answer ->
                            MathPuzzleFeedback.selectedReviews(response,answer,text("math_ui_review_selected_option")) {
                                text("math_ui_field",it)
                            }
                        }.orEmpty()
                        val diagnostic = result.fieldId?.takeIf {
                            !result.correct && !MathPuzzleFeedback.isDropdownField(response,it)
                        }?.let { field(response,result,process()) }
                        (reviews+listOfNotNull(diagnostic)).joinToString("\n")
                    } else {
                        if (result.correct) return@mapIndexedNotNull null
                        field(response,result,process())
                    }
                    message.takeIf { it.isNotBlank() }?.let { "${label(response,index)}: $it" }
                }
                val heading = if (selected) text("math_ui_incorrect")
                    else spec.incorrectFeedback.ifBlank { text("math_ui_incorrect") }
                val guidance = MathPuzzleFeedback.incorrectGuidance(selected,spec.incorrectFeedback,details,
                    text("math_ui_review_process"))
                val visible = if (details.isEmpty() && (selected || spec.incorrectFeedback.isBlank())) guidance else details
                (listOf("✗ $heading")+visible).filter { it.isNotBlank() }.joinToString("\n")
            }
        }
        return JSONObject().put("correct",correct).put("evaluable",evaluable)
            .put("feedback",feedback).put("feedbackKind",if (!evaluable) "error" else if (correct) "correct" else "incorrect")
            .put("results",JSONArray(results.map { (response,result) ->
                JSONObject().put("id",response.id).put("correct",result.correct).put("evaluable",result.evaluable)
                    .put("message",resolve(result)).apply {
                        result.uiMessage?.let { message ->
                            val ui = uiJson(message)
                            put("code",ui.getString("code")).put("arguments",ui.getJSONArray("arguments"))
                        }
                        result.fieldId?.let { put("fieldId",it) }
                    }
            }))
    }

    private val visibleKeys = setOf("id","type","text","label","prompt","question","hint","unit",
        "instructions","instruction","description","inputMode","keyboardMode","keyboardSymbols",
        "options","items","fields","segments","rows","headers","layout","initial","initialOrder",
        "min","max","step","closed","direction","mode","sources","targets","parameters","visual",
        "prefix","suffix","placeholder","selectable","value","x","y","name","title","from","to")

    private fun visible(value: Any?): Any? = when (value) {
        is JSONObject -> JSONObject().apply {
            value.keys().asSequence().filter { it in visibleKeys }.forEach { key -> put(key,visible(value.get(key))) }
        }
        is JSONArray -> JSONArray((0 until value.length()).map { visible(value.get(it)) })
        else -> value
    }

    /** Validated UI shape only; expected answers, validators and review text stay private. */
    fun describe(request: JSONObject): JSONObject {
        val spec = MathPuzzleParser.parse(block(request))
        language(request)
        return JSONObject().put("prompt",spec.prompt)
            .put("representations",JSONArray(spec.representations))
            .put("tools",JSONArray(spec.tools))
            .put("responses",JSONArray(spec.responses.map {
                JSONObject().put("id",it.id).put("type",it.type).put("config",visible(it.config))
            }))
    }
}

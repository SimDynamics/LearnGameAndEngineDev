package lgaed.narration.windows

import com.learngraphicsprogramming.app.math.*
import com.learngraphicsprogramming.app.calculator.*
import org.json.JSONArray
import org.json.JSONObject

/** A platform boundary around the original Android-independent parser and evaluators. */
internal object WindowsMathBridge {
    private data class NativeTextSelection(val path: List<MathCursorStep>,val start: Int,val end: Int)
    private data class EditorSession(val model: MathExpressionModel,
        var template: MathExpression.Sequence? = null, var revision: Long = 0,
        var textSelection: NativeTextSelection? = null)
    // UI lifetime, not a second persistence/history system. The canonical draft
    // is returned after each edit; close discards this transient model only.
    private val editors = mutableMapOf<String,EditorSession>()

    private fun parsed(source: String): MathExpression.Sequence = when(val result=MathLatexParser.parse(source)) {
        is MathLatexParser.Result.Success -> result.expression
        is MathLatexParser.Result.Failure -> error(result.uiMessage.diagnostic)
    }
    private fun setLatex(model: MathExpressionModel,source: String) {
        if(source.isBlank()){model.clear();return}
        // Same import boundary and final cursor position as MathExpressionEditor.setLatex.
        val imported=MathExpressionModel()
        require(imported.insertLatex(source) is MathLatexParser.Result.Success){"Invalid mathematical input"}
        model.setExpression(imported.expression,MathCursor(offset=imported.expression.children.size))
    }
    private fun path(value: JSONArray): List<MathCursorStep> {
        require(value.length()<=60){"Mathematical cursor path is too deep"}
        return (0 until value.length()).map { index -> value.getJSONObject(index).let {
            MathCursorStep(it.getInt("node"),it.getString("slot"))
        } }
    }
    private fun path(value: List<MathCursorStep>)=JSONArray(value.map {
        JSONObject().put("node",it.nodeIndex).put("slot",it.slot)
    })
    private fun scope(value: MathContentScope?): JSONObject? = when(value) {
        null -> null
        MathContentScope.Slot -> JSONObject().put("type","slot")
        is MathContentScope.Atom -> JSONObject().put("type","atom").put("start",value.start).put("end",value.end)
    }
    private fun leafText(node: MathExpression): String = when(node) {
        is MathExpression.Number -> node.value
        is MathExpression.Variable -> node.name
        is MathExpression.Symbol -> node.value
        is MathExpression.Operator -> node.value
        is MathExpression.Relation -> node.value
        else -> "\uFFFC"
    }
    private fun replaceTextRange(model: MathExpressionModel,start: Int,end: Int,text: String): Boolean {
        // The transport has already resolved the exact UTF-16 range. The
        // canonical replacement then creates fresh AST boundaries around it.
        // Carrying the old leaf-local offset onto those new boundaries can
        // advance into the suffix in nested Slot scope (12|34 -> 1234|9).
        // Retain the original path/scope and supply only the resolved range.
        val previous=model.cursor
        if(previous.leafTextOffset!=null)model.cursor=previous.copy(leafTextOffset=null)
        if(model.replaceTextRangeInCurrentSlot(start,end,text))return true
        model.cursor=previous
        return false
    }
    private fun insertText(model: MathExpressionModel,text: String): Boolean {
        // The Android IME commits text through its active-slot UTF-16 projection.
        // A structural insertion uses insertText; a leaf caret must use the
        // original bounded replacement operation, otherwise 12|34 inserts at |1234.
        if(model.cursor.level!=MathCursorLevel.INTERIOR||model.selectionRange!=null)return model.insertText(text)
        val sequence=model.sequenceAt(model.cursor.path)?:return false
        val offset=sequence.children.take(model.cursor.offset).sumOf { leafText(it).length }+(model.cursor.leafTextOffset?:0)
        val beforeLength=sequence.children.sumOf { leafText(it).length }
        if(!replaceTextRange(model,offset,offset,text))return false
        val updated=model.sequenceAt(model.cursor.path)?:return true
        val inserted=(updated.children.sumOf { leafText(it).length }-beforeLength).coerceAtLeast(0)
        return model.setTextCursorInCurrentSlot(offset+inserted)
    }
    private fun replaceSelection(session: EditorSession,text: String): Boolean {
        val selected=session.textSelection?:return insertText(session.model,text)
        val model=session.model;val sequence=model.sequenceAt(selected.path)?:return false
        val beforeLength=sequence.children.sumOf { leafText(it).length }
        if(!replaceTextRange(model,selected.start,selected.end,text))return false
        session.textSelection=null
        if(model.cursor.path!=selected.path)return true
        val afterLength=model.sequenceAt(selected.path)?.children?.sumOf { leafText(it).length }?:return true
        return model.setTextCursorInCurrentSlot(selected.start+(afterLength-beforeLength+selected.end-selected.start).coerceAtLeast(0))
    }
    private fun cursor(value: JSONObject): MathCursor {
        val scope=value.optJSONObject("scope")?.let {
            when(it.getString("type")) {
                "slot" -> MathContentScope.Slot
                "atom" -> MathContentScope.Atom(it.getInt("start"),it.getInt("end"))
                else -> error("Unknown mathematical cursor scope")
            }
        }
        return MathCursor(path(value.optJSONArray("path")?:JSONArray()),value.getInt("offset"),
            MathCursorLevel.valueOf(value.optString("level","NEUTRAL")),scope,
            if(value.has("leafTextOffset")&&!value.isNull("leafTextOffset"))value.getInt("leafTextOffset") else null)
    }
    private fun snapshot(id: String,session: EditorSession,accepted: Boolean): JSONObject {
        val model=session.model;val draft=model.saveDraft();val saved=JSONObject(draft)
        val current=model.cursor
        val caret=JSONObject().put("path",path(current.path)).put("offset",current.offset).put("level",current.level.name)
        scope(current.scope)?.let { caret.put("scope",it) }
        current.leafTextOffset?.let { caret.put("leafTextOffset",it) }
        val presentation=when(val value=model.cursorPresentation){
            is MathCursorPresentation.Interior -> JSONObject().put("type","interior").apply {
                scope(value.scope)?.let { put("scope",it) };value.leafTextOffset?.let { put("leafTextOffset",it) }
            }
            MathCursorPresentation.Neutral -> JSONObject().put("type","neutral")
            is MathCursorPresentation.ParentBoundary -> JSONObject().put("type","parent").put("path",path(value.path))
                .put("nodeIndex",value.nodeIndex).put("includePrevious",value.includePrevious)
            is MathCursorPresentation.ChildBoundary -> JSONObject().put("type","child").put("slotPath",path(value.slotPath)).put("after",value.after)
            MathCursorPresentation.ContinuationBoundary -> JSONObject().put("type","continuation")
        }
        return JSONObject().put("editorId",id).put("revision",session.revision).put("accepted",accepted)
            .put("draft",draft).put("latex",MathLatexSerializer.serialize(model.expression)).put("complete",model.isComplete())
            .put("expression",saved.getJSONObject("expression")).put("cursor",caret).put("presentation",presentation)
            .put("canPrevious",model.canNavigatePrevious).put("canNext",model.canNavigateNext).put("hasNavigableSlots",model.hasNavigableSlots)
            .apply { saved.optJSONObject("selection")?.let { put("selection",it) } }
            .apply { session.textSelection?.let { put("textSelection",JSONObject().put("path",path(it.path)).put("start",it.start).put("end",it.end)) } }
    }
    fun editor(request: JSONObject): JSONObject {
        val id=request.getString("editorId")
        require(id.length in 1..128&&id.all { it.isLetterOrDigit()||it in "_.-" }){"Invalid mathematical editor identity"}
        val action=request.optString("action","state")
        if(action=="close")return JSONObject().put("editorId",id).put("closed",editors.remove(id)!=null)
        if(action=="create"){
            require(id !in editors){"Mathematical editor identity is already active"}
            require(editors.size<128){"Too many active mathematical editors"}
            val session=EditorSession(MathExpressionModel())
            if(request.has("draft"))require(session.model.restoreDraft(request.getString("draft"))){"Invalid mathematical editor draft"}
            else if(request.has("latex"))setLatex(session.model,request.getString("latex"))
            if(request.has("expectedTemplate"))session.template=parsed(request.getString("expectedTemplate"))
            editors[id]=session
            return snapshot(id,session,true)
        }
        val session=editors[id]?:error("Mathematical editor is not open")
        val model=session.model;val before=model.saveDraft()
        val previousTextSelection=session.textSelection
        // Like Android's ImeSelection, this is a transient input-transport range,
        // not an AST selection or a second persisted editor/history model.
        if(action !in setOf("state","copy","cut","text","insertLatex","textSelection","backspace","deleteForward"))session.textSelection=null
        var clipboard: String?=null
        val accepted=when(action){
            "state" -> true
            "restore" -> if(request.has("draft"))model.restoreDraft(request.getString("draft"))
                else { setLatex(model,request.getString("latex"));true }
            "text" -> replaceSelection(session,request.getString("text"))
            "insertLatex" -> if(session.textSelection==null)model.insertLatex(request.getString("latex")) is MathLatexParser.Result.Success
                else replaceSelection(session,request.getString("latex"))
            "left" -> model.stepLeft()
            "right" -> model.stepRight()
            "backspace" -> if(session.textSelection!=null)replaceSelection(session,"") else {model.deleteBackward();true}
            "deleteForward" -> if(session.textSelection!=null)replaceSelection(session,"") else model.deleteSurroundingTextInCurrentSlot(0,1)
            "copy", "cut" -> {
                // Copy reads the original selection without restoring a model:
                // restoring solely to copy would lose private navigation edges.
                val selection=model.selectionRange
                val selected=selection?.let { range -> model.sequenceAt(range.path)?.let {
                    MathExpression.Sequence(it.children.subList(range.start,range.end))
                } }
                val native=session.textSelection
                clipboard=if(native!=null)model.sequenceAt(native.path)?.children?.joinToString("") { leafText(it) }
                    ?.substring(native.start,native.end) else MathLatexSerializer.serialize(selected?:model.expression)
                if(action=="cut"&&native!=null)replaceSelection(session,"") else if(action=="cut") {
                    if(selection==null)model.clear() else model.takeSelection()
                }
                true
            }
            "clear" -> {model.clear();true}
            "selectAll" -> {model.selectAll();true}
            "selection" -> {model.select(path(request.optJSONArray("path")?:JSONArray()),request.getInt("start"),request.getInt("end"));true}
            "textSelection" -> {
                val selectedPath=path(request.optJSONArray("path")?:JSONArray())
                val sequence=model.sequenceAt(selectedPath)?:error("Unknown mathematical selection slot")
                fun position(nodeKey: String,leafKey: String): Int {
                    val node=request.getInt(nodeKey);require(node in 0..sequence.children.size)
                    val leaf=request.optInt(leafKey,-1)
                    require(leaf<0||node<sequence.children.size&&leaf<=leafText(sequence.children[node]).length)
                    return sequence.children.take(node).sumOf { leafText(it).length }+leaf.coerceAtLeast(0)
                }
                var start=minOf(position("startNode","startLeaf"),position("endNode","endLeaf"))
                var end=maxOf(position("startNode","startLeaf"),position("endNode","endLeaf"))
                val projection=sequence.children.joinToString("") { leafText(it) }
                // Retain complete code points and never let native text selection
                // acquire ownership of structural objects (the Android IME rule).
                val insidePair=start>0&&start<projection.length&&Character.isHighSurrogate(projection[start-1])&&Character.isLowSurrogate(projection[start])
                if(start==end&&insidePair){start++;end++}else {
                    if(insidePair)start--
                    if(end>0&&end<projection.length&&Character.isHighSurrogate(projection[end-1])&&Character.isLowSurrogate(projection[end]))end++
                }
                if('\uFFFC' in projection.substring(start,end))false else {
                    model.clearSelection()
                    val endNode=request.getInt("endNode");val endLeaf=request.optInt("endLeaf",-1)
                    val placed=model.placeTextCursor(MathCursor(selectedPath,endNode),endLeaf.takeIf { it>=0 })
                    if(placed)session.textSelection=if(start==end)null else NativeTextSelection(selectedPath,start,end)
                    placed
                }
            }
            "cursor" -> {
                model.clearSelection();val position=cursor(request.getJSONObject("cursor"))
                model.placeTextCursor(position,if(request.has("leafTextOffset"))request.getInt("leafTextOffset") else position.leafTextOffset,
                    request.optBoolean("sequenceInsertion",false))
            }
            "key" -> {
                val key=request.getString("key")
                require(key in setOf("Fraction","Power","Subscript","Root","NthRoot","Parentheses","AbsoluteValue","Brackets","Braces",
                    "Integral","Sum","Product","Limit","Matrix","Cases","Derivative","Function","Symbol")){"Unknown mathematical key"}
                WindowsMathActions.apply(model,key,request.optString("name"),request.optString("text",request.optString("name")),session.template)
            }
            else -> error("Unknown mathematical editor action")
        }
        if(!accepted){if(before!=model.saveDraft())check(model.restoreDraft(before));session.textSelection=previousTextSelection}
        if(before!=model.saveDraft()||previousTextSelection!=session.textSelection)session.revision++
        return snapshot(id,session,accepted).apply { clipboard?.let { put("clipboardLatex",it) } }
    }
    fun keyboard(request: JSONObject): JSONObject {
        val config=request.optJSONObject("config")?:JSONObject(request.getString("configJson"))
        val validator=config.optJSONObject("validator")
        val structured=request.optBoolean("structured",config.optString("inputMode")=="structured")
        val mode=config.optString("keyboard",config.optString("keyboardMode","ALGEBRA"))
        val symbols=config.optJSONArray("keyboardSymbols")
        val shortcuts=if(structured)MathShortcutResolver.resolveStructured(validator,mode,symbols)
            else MathShortcutResolver.resolve(validator,mode,symbols)
        val expected=validator?.optString("expected").orEmpty()
        val template=(MathLatexParser.parse(expected) as? MathLatexParser.Result.Success)?.expression
        val language=language(request)
        val keys=shortcuts.map { shortcut ->
            val action=MathKeyboardAction.fromShortcut(shortcut)
            val key=action.javaClass.simpleName
            JSONObject().put("shortcut",shortcut).put("key",key).put("label",action.label)
                .put("description",action.description?.let { MathLocaleResources.resolve(it.resourceId,it.arguments,language) }
                    ?:action.contentDescription).apply {
                    when(action) {
                        is MathKeyboardAction.Function -> put("name",action.name)
                        is MathKeyboardAction.Symbol -> put("text",action.label).put("name",action.label)
                        else -> Unit
                    }
                    action.previewExpression(template)?.let { preview ->
                        put("preview",JSONObject(MathExpressionModel(preview).saveDraft()).getJSONObject("expression"))
                    }
                }
        }
        val literal=validator?.optString("type") in setOf("text","exact_text")
        return JSONObject().put("structured",structured).put("keys",JSONArray(keys))
            .put("minusInsertion",if(literal&&'−' in expected)"−" else "-")
    }
    fun calculate(request: JSONObject): JSONObject {
        val model=MathExpressionModel()
        if(request.has("draft"))require(model.restoreDraft(request.getString("draft"))){"Invalid mathematical calculator draft"}
        else setLatex(model,request.getString("latex"))
        val before=model.saveDraft()
        val result=MathCalculator().evaluate(model.expression,CalculatorAngleMode.valueOf(request.optString("angle","RAD")))
        check(model.saveDraft()==before){"Calculator changed its input model"}
        return when(result){
            is CalculatorResult.Success -> {
                val output=MathExpressionModel(result.expression)
                JSONObject().put("success",true).put("latex",result.latex).put("result",result.latex)
                    .put("draft",output.saveDraft()).put("expression",JSONObject(output.saveDraft()).getJSONObject("expression"))
            }
            is CalculatorResult.Failure -> JSONObject().put("success",false).put("message",result.uiMessage?.let {
                MathLocaleResources.resolve(it.resourceId,it.arguments,language(request))
            }?:result.message).apply { result.uiMessage?.let { put("code",it.resourceId) } }
        }
    }

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

    fun validate(request: JSONObject, originalRequest: String): JSONObject {
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
        val originalBlock = if(request.has("blockJson")) request.getString("blockJson")
            else lgaed.narration.androidjson.JSONObject(originalRequest).getJSONObject("block").toString()
        val fingerprint = MathPuzzleState.fingerprint(lgaed.narration.androidjson.JSONObject(originalBlock))
        return JSONObject().put("correct",correct).put("evaluable",evaluable).put("contentFingerprint",fingerprint)
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

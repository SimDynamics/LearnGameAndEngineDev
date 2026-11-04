package lgaed.narration.windows

import com.learngraphicsprogramming.app.math.*
import com.learngraphicsprogramming.app.calculator.*
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.*

/** A platform boundary around the original Android-independent parser and evaluators. */
internal object WindowsMathBridge {
    /** Two exact reviewed labels,
     * not a new response contract. These are Android's complete, insertion-order
     * fingerprints; every other field and key order remains significant. The
     * original JSON still supplies the visible labels, narration and validator. */
    private fun compatibleContentFingerprint(block: lgaed.narration.androidjson.JSONObject): String {
        val fingerprint = MathPuzzleState.fingerprint(block)
        return if (block.optString("id") ==
            "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos__reglas_de_los_caminos_publicos" &&
            fingerprint == "5a55c3f981833aabcc7f047765a3db66d1f9e6993bf69a14d69e8ae08c190095")
            "978c6526c69d056658f4ee8455eaa4c37f5e87d97d9feebd5ecf93bac352ed13"
        else if (block.optString("id") ==
            "cpp_fundamentals__texto_interpretacion_y_flujos__interprete_historial__casos_y_contratos" &&
            fingerprint == "a7d0293021148d49a4fe25719292b52dc7becdb63d75b5f84e5dc93575abb77e")
            "34045982a2a7b895013785d3756ebbee81e4da9b5e08de8cd22f53285c2186fb"
        else fingerprint
    }
    private data class NativeTextSelection(val path: List<MathCursorStep>,val start: Int,val end: Int)
    private data class EditorSession(val model: MathExpressionModel,
        var template: MathExpression.Sequence? = null, var revision: Long = 0,
        var textSelection: NativeTextSelection? = null)
    // UI lifetime, not a second persistence/history system. The canonical draft
    // is returned after each edit; close discards this transient model only.
    private val editors = mutableMapOf<String,EditorSession>()
    // Pure presentation results only. No answer/editor/model/audio state is
    // cached here, and every range keeps its original UTF-16 source owner.
    private val textLayouts = linkedMapOf<String,List<WindowsMathFragment>>()

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
        if(request.optString("action")=="diagram_layout")return diagramLayout(request)
        if(request.optString("action")=="rigid_layout")return rigidLayout(request)
        if(request.optString("action")=="segment_layout")return WindowsSegmentTransport.layout(request,::drawingJson)
        if(request.optString("action")=="text_layout")return textLayout(request)
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
            // Android hardware Up/Down/Tab visit slots, whereas Left/Right
            // retain the model's finer parent/child/leaf navigation.
            "previousSlot" -> model.previousSlot()
            "nextSlot" -> model.nextSlot()
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
    /** The same pure Android layout; Windows supplies its actual font metrics. */
    private fun textLayout(request: JSONObject): JSONObject {
        val source=request.getString("text");val mode=request.optString("mode","auto")
        val key=mode+'\u0000'+source
        val fragments=textLayouts[key]?:WindowsMathFractionLayout.render(source,mode).also {
            if(textLayouts.size>=2048)textLayouts.remove(textLayouts.keys.first())
            textLayouts[key]=it
        }
        return JSONObject().put("fragments",JSONArray(fragments.map {
            JSONObject().put("start",it.start).put("end",it.end).put("formula",it.formula).put("literal",it.literal)
        }))
    }
    private fun diagramLayout(request: JSONObject): JSONObject {
        val diagram=EuclideanDiagram.parse(request.getJSONObject("config"))
        val width=request.getDouble("width");val height=request.getDouble("height")
        val unit=request.optDouble("unit",1.0);val size=request.optDouble("textSize",15.0)
        val supplied=request.optJSONObject("labelMetrics")
        val drawing=if(supplied==null)EuclideanDiagramLayout.layout(diagram,width,height,unit,size)
            else EuclideanDiagramLayout.layout(diagram,width,height,unit,size){ text,_ ->
                val metric=supplied.getJSONObject(text);val lines=metric.getJSONArray("lines")
                EuclideanLabelMetrics(List(lines.length()){lines.getString(it)},metric.getDouble("width"),
                    metric.getDouble("lineHeight"),metric.getDouble("ascent"))
            }
        return drawingJson(drawing)
    }
    private fun drawingJson(drawing: EuclideanDrawingSnapshot): JSONObject {
        fun point(value: EuclideanPosition)=JSONObject().put("x",value.x).put("y",value.y)
        fun bounds(value: EuclideanRect)=JSONObject().put("left",value.left).put("top",value.top)
            .put("right",value.right).put("bottom",value.bottom)
        return JSONObject().put("viewport",bounds(drawing.viewport)).put("plot",bounds(drawing.plotBounds))
            .put("points",JSONArray(drawing.points.map{JSONObject().put("id",it.id).put("center",point(it.center))}))
            .put("paths",JSONArray(drawing.paths.map{JSONObject().put("id",it.id).put("start",point(it.start)).put("end",point(it.end))}))
            .put("arrows",JSONArray(drawing.arrows.map{JSONObject().put("id",it.objectId).put("tip",point(it.tip))
                .put("wingA",point(it.wingA)).put("wingB",point(it.wingB))}))
            .put("circles",JSONArray(drawing.circles.map{JSONObject().put("id",it.id).put("center",point(it.center))
                .put("radius",it.radius).put("filled",it.filled)}))
            .put("arcs",JSONArray(drawing.arcs.map{JSONObject().put("id",it.id).put("center",point(it.center))
                .put("radius",it.radius).put("startDegrees",it.startDegrees).put("sweepDegrees",it.sweepDegrees)}))
            .put("labels",JSONArray(drawing.labels.map{JSONObject().put("id",it.ownerId).put("text",it.text)
                .put("lines",JSONArray(it.lines)).put("bounds",bounds(it.bounds)).put("ascent",it.ascent).put("lineHeight",it.lineHeight)}))
    }
    /** Android's rigid view uses these same fixed bounds, motions and pure
     * diagram/label layouts. Placements remain the existing typed answer. */
    private fun rigidLayout(request: JSONObject): JSONObject {
        val spec=RigidAngleCopySpec.parse(request.getJSONObject("config"))
        val width=request.getDouble("width");val height=request.getDouble("height")
        val unit=request.optDouble("unit",1.0);val size=request.optDouble("textSize",15.0)
        require(width>=1&&height>=1&&width<=32768&&height<=32768&&unit>0&&unit<=16&&size>0&&size<=2048&&listOf(width,height,unit,size).all(Double::isFinite))
        val initialPoints=spec.reference.diagram.points.map { it.position }+spec.copy.moved(spec.initial,"").points.map { it.position }
        val radius=max(spec.reference.radius,spec.copy.radius)
        val center=EuclideanPosition((initialPoints.minOf { it.x }+initialPoints.maxOf { it.x })/2,
            (initialPoints.minOf { it.y }+initialPoints.maxOf { it.y })/2)
        val halfWidth=max(radius,(initialPoints.maxOf { it.x }-initialPoints.minOf { it.x })/2+radius*.25)
        val halfHeight=max(radius,(initialPoints.maxOf { it.y }-initialPoints.minOf { it.y })/2+radius*.25)
        // Normalize only drawing coordinates before using the original layout's
        // one-unit minimum. The authored geometry and placement remain untouched.
        val normalizer=min(halfWidth,halfHeight)*2
        val bounds=EuclideanRect(-halfWidth/normalizer,-halfHeight/normalizer,halfWidth/normalizer,halfHeight/normalizer)
        val padding=min(max(30*unit,size*1.8),min(width,height)*.24)
        val factor=min((width-2*padding)/(halfWidth*3.6),(height-2*padding)/(halfHeight*3.6))
        require(factor.isFinite()&&factor>0&&normalizer.isFinite()&&normalizer>0)
        fun project(point: EuclideanPosition)=EuclideanPosition(width/2+(point.x-center.x)*factor,height/2-(point.y-center.y)*factor)
        fun unproject(point: EuclideanPosition)=EuclideanPosition((point.x-width/2)/factor+center.x,-(point.y-height/2)/factor+center.y)
        fun normalized(point: EuclideanPosition)=EuclideanPosition((point.x-center.x)/normalizer,(point.y-center.y)/normalizer)
        fun finite(point: EuclideanPosition)=point.x.isFinite()&&point.y.isFinite()
        val renderMargin=32*unit
        fun visible(point: EuclideanPosition)=finite(point)&&point.x>=-renderMargin&&point.x<=width+renderMargin&&point.y>=-renderMargin&&point.y<=height+renderMargin
        val original=request.optJSONObject("placement")
        var placement=if(original==null)spec.initial else RigidAnglePlacement(original.getDouble("x"),original.getDouble("y"),original.getDouble("rotation"))
        val gesture=request.optJSONObject("gesture")
        if(gesture!=null){
            fun position(key: String)=gesture.getJSONObject(key).let {
                val pixel=EuclideanPosition(it.getDouble("x"),it.getDouble("y"))
                require(finite(pixel)&&abs(pixel.x)<=1e7&&abs(pixel.y)<=1e7) { "Invalid rigid-copy pointer coordinate" }
                unproject(pixel)
            }
            val before=position("previous");val after=position("current");val mode=gesture.getString("mode")
            require(mode in setOf("move","rotate","view"))
            val margin=min(padding+16*unit,min(width,height)*.4)
            if(mode=="move"){
                val next=placement.copy(x=placement.x+after.x-before.x,y=placement.y+after.y-before.y)
                val low=unproject(EuclideanPosition(margin,height-margin));val high=unproject(EuclideanPosition(width-margin,margin))
                val points=spec.copy.moved(next,"").points.map { it.position }
                fun correction(low: Double,high: Double)=if(low<=high)0.0.coerceIn(low,high)else(low+high)/2
                placement=next.copy(x=next.x+correction(low.x-points.minOf { it.x },high.x-points.maxOf { it.x }),
                    y=next.y+correction(low.y-points.minOf { it.y },high.y-points.maxOf { it.y }))
            }else if(mode=="rotate"){
                val vertex=EuclideanPosition(placement.x,placement.y);val first=before-vertex;val second=after-vertex
                if(hypot(first.x,first.y)*factor>=20*unit&&hypot(second.x,second.y)*factor>=20*unit){
                    // A finite far-away saved placement must not overflow the
                    // cross/dot products and poison a valid gesture with NaN.
                    fun direction(value: EuclideanPosition): EuclideanPosition {
                        val magnitude=max(abs(value.x),abs(value.y))
                        return EuclideanPosition(value.x/magnitude,value.y/magnitude)
                    }
                    val one=direction(first);val two=direction(second)
                    val next=placement.copy(rotation=placement.rotation+atan2(one.x*two.y-one.y*two.x,one.x*two.x+one.y*two.y))
                    if(spec.copy.moved(next,"").points.all { val pixel=project(it.position);pixel.x>=margin&&pixel.y>=margin&&pixel.x<=width-margin&&pixel.y<=height-margin })placement=next
                }
            }
        }
        val fixed=spec.reference.moved(spec.referencePlacement,"reference:");val moving=spec.copy.moved(placement,"copy:")
        val groups=(fixed.points+moving.points).groupBy { it.position }.values
        val owners=groups.flatMap { group -> group.map { it.id to group.first().id } }.toMap()
        val points=groups.mapNotNull { group ->
            val position=group.first().position;val pixel=project(position)
            if(!finite(pixel))return@mapNotNull null
            group.first().copy(position=normalized(position),labels=if(finite(pixel)&&pixel.x in 0.0..width&&pixel.y in 0.0..height)group.flatMap { it.labels }.distinct()else emptyList())
        }.filter { finite(it.position) }
        val objects=(fixed.objects+moving.objects).filter { finite(project(it.origin)) }.map { value -> value.copy(origin=normalized(value.origin),direction=value.direction*(1/normalizer),
            definingPointIds=value.definingPointIds.map { owners.getValue(it) },contains=value.contains.map { owners.getValue(it) }.toSet())
        }.filter { finite(it.origin)&&finite(it.direction) }
        val diagram=EuclideanDiagram(points,objects);val metrics=request.optJSONObject("labelMetrics")
        val snapshot=if(metrics==null)EuclideanDiagramLayout.layoutInBounds(diagram,width,height,unit,size,bounds)
            else EuclideanDiagramLayout.layoutInBounds(diagram,width,height,unit,size,bounds){ text,_ ->
                val metric=metrics.getJSONObject(text);val lines=metric.getJSONArray("lines")
                EuclideanLabelMetrics(List(lines.length()){lines.getString(it)},metric.getDouble("width"),metric.getDouble("lineHeight"),metric.getDouble("ascent"))
            }
        // Win32 consumes these as integer pixels before its native zoom/pan.
        // That pan is restricted to the original viewport, so hidden markers
        // and names cannot become visible through the later transform.
        val paths=snapshot.paths.filter { visible(it.start)&&visible(it.end) }
        val pathIds=paths.map { it.id }.toSet()
        val drawing=RigidAngleLabelLayout.clear(snapshot.copy(points=snapshot.points.filter { visible(it.center) },paths=paths,
            arrows=snapshot.arrows.filter { it.objectId in pathIds&&visible(it.tip)&&visible(it.wingA)&&visible(it.wingB) },
            labels=snapshot.labels.filter { label -> listOf(label.bounds.left,label.bounds.top,label.bounds.right,label.bounds.bottom).all(Double::isFinite)&&
                label.bounds.left>=0&&label.bounds.top>=0&&label.bounds.right<=width&&label.bounds.bottom<=height }),unit)
        fun region(figure: RigidAngleFigure,position: RigidAnglePlacement,id: String): JSONObject? {
            val at=project(EuclideanPosition(position.x,position.y));val first=position.rotate(figure.sides.first().direction)
            if(!visible(at))return null
            return JSONObject().put("id",id).put("center",JSONObject().put("x",at.x).put("y",at.y))
                .put("radius",(radius*.28*factor).coerceAtLeast(14*unit)).put("startDegrees",-Math.toDegrees(atan2(first.y,first.x)))
                .put("sweepDegrees",Math.toDegrees(figure.sweep)*if(figure.counterclockwise)-1 else 1)
        }
        val result=drawingJson(drawing).put("placement",JSONObject().put("type","rigid_angle").put("x",placement.x).put("y",placement.y).put("rotation",placement.rotation))
            .put("regions",JSONArray(listOfNotNull(region(spec.reference,spec.referencePlacement,"reference:"),region(spec.copy,placement,"copy:"))))
            .put("factor",factor)
        val copyCenter=project(EuclideanPosition(placement.x,placement.y))
        if(finite(copyCenter))result.put("copyCenter",JSONObject().put("x",copyCenter.x).put("y",copyCenter.y))
        return result
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
            // The last valid plotted points remain visible while a coordinate
            // is partial. Its exact editor draft is not an evaluable answer.
            val coordinates = raw.optJSONObject(response.id)?.optJSONObject("interactionDraft")?.optJSONArray("coords")
            val invalidCoordinates = response.type in setOf("math_coordinate_graph_builder", "math_geometry_builder") &&
                coordinates != null && (coordinates.length() != (answer as? MathAnswer.Graph)?.points?.size ||
                (0 until coordinates.length()).any { index ->
                    val point = coordinates.optJSONObject(index)
                    listOf("x", "y").any { key -> point?.optString(key)?.trim()?.replace(',', '.')
                        ?.toDoubleOrNull()?.takeIf(Double::isFinite) == null }
                })
            response to if (invalidCoordinates) {
                val message = MathUiMessage("math_ui_valid_coordinates")
                MathValidationResult(false,message.diagnostic,evaluable=false,uiMessage=message)
            } else if (answer == null) {
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
        val fingerprint = compatibleContentFingerprint(lgaed.narration.androidjson.JSONObject(originalBlock))
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
                val config=visible(it.config) as JSONObject
                if(it.type=="math_segment_transport") {
                    // Give the view only authored givens and initial positions.
                    // Steps, tolerances and calculated solution endpoints stay private.
                    val construction=WindowsSegmentTransportSpec.parse(it.validator)
                    fun named(value: SegmentTransportPoint)=JSONObject().put("x",value.position.x).put("y",value.position.y).put("label",value.label)
                    config.put("sources",JSONArray(construction.sources.map { source ->
                        JSONObject().put("id",source.id).put("from",named(source.from)).put("to",named(source.to))
                    })).put("target",JSONObject().put("origin",named(construction.origin)).apply {
                        construction.end?.let { value -> put("end",named(value)) }
                            ?:put("direction",JSONObject().put("x",construction.direction.x).put("y",construction.direction.y))
                    }).put("pointLabels",JSONArray(construction.pointLabels))
                        .put("initial",JSONObject().put("points",JSONArray(construction.initial.map { point -> JSONObject().put("x",point.x).put("y",point.y) })))
                }
                JSONObject().put("id",it.id).put("type",it.type).put("config",config)
            }))
    }
}

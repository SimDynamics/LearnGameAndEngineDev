package com.learngraphicsprogramming.app.math

import android.content.ClipData
import android.content.Context
import android.view.DragEvent
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import com.learngraphicsprogramming.app.DirectDragTouch
import org.json.JSONObject

internal object MathDragControls {
    fun create(context: Context, spec: MathResponseSpec, accent: Int, onChange: () -> Unit): MathResponseControl? = when (spec.type) {
        "math_ordering_drag" -> OrderingControl(context, spec, accent, onChange)
        "math_matching_drag" -> MatchingControl(context, spec, accent, onChange)
        else -> null
    }
}

/** localState scopes the payload to this response, never to another puzzle on the page. */
private data class MathDragToken(val owner: Any, val id: String)

private class OrderingControl(context: Context, spec: MathResponseSpec, accent: Int, private val onChange: () -> Unit) : MathResponseControl {
    override val view = MathWidgetUi.column(context)
    private val list = MathWidgetUi.column(context)
    private val owner = Any()
    private val order = mutableListOf<String>()
    private val rows = linkedMapOf<String, LinearLayout>()
    private var nextTapPosition = 0
    private var editable = true
    init {
        view.addView(MathWidgetUi.text(context, spec.config.optString("label", "Arrastra una tarjeta directamente o toca las tarjetas en el orden deseado.")))
        val items = MathWidgetUi.objects(spec.config.optJSONArray("items"))
        require(items.isNotEmpty()) { "Faltan tarjetas para ordenar." }
        items.forEach { item ->
            val id = item.getString("id")
            require(id.isNotBlank() && id !in rows) { "ID de tarjeta vacío o repetido: $id" }
            order.add(id)
            val card = MathTouchCard(context).apply {
                tag = MathInputFocusOrder.GESTURE_OWNER_TAG
                background = MathWidgetUi.background(context)
                setPadding(MathWidgetUi.dp(context, 10), MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 10), MathWidgetUi.dp(context, 6))
                layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 8) }
            }
            card.addView(MathWidgetUi.rich(context, item, accent))
            card.isFocusable = true
            card.contentDescription = "Ordenar tarjeta $id: ${item.optString("label", id)}"
            card.setOnClickListener {
                if (editable && nextTapPosition < order.size) {
                    move(id, nextTapPosition, notify = false)
                    nextTapPosition++
                    onChange()
                }
            }
            DirectDragTouch.bind(card, { editable }, { target ->
                target.startDragAndDrop(ClipData.newPlainText("math_order", id), View.DragShadowBuilder(target), MathDragToken(owner, id), 0)
            })
            card.setOnDragListener { _, event ->
                val token = event.localState as? MathDragToken
                if (!editable || token?.owner !== owner) false
                else when (event.action) {
                    DragEvent.ACTION_DRAG_STARTED -> true
                    DragEvent.ACTION_DRAG_ENTERED -> { move(token.id, order.indexOf(id)); true }
                    DragEvent.ACTION_DROP -> { move(token.id, order.indexOf(id)); true }
                    DragEvent.ACTION_DRAG_ENDED -> { rows[token.id]?.let(DirectDragTouch::finish); true }
                    else -> true
                }
            }
            rows[id] = card
        }
        val initial = spec.config.optJSONArray("initialOrder")
        if (initial != null) {
            val ids = (0 until initial.length()).map { initial.getString(it) }
            require(ids.size == order.size && ids.toSet() == order.toSet()) { "initialOrder debe contener cada tarjeta una vez." }
            order.clear()
            order.addAll(ids)
        }
        view.addView(list)
        refresh()
    }
    private fun move(id: String, destination: Int, notify: Boolean = true) {
        if (!editable || destination !in order.indices || order.indexOf(id) == destination) return
        order.remove(id)
        order.add(destination, id)
        // Keep the other native drop targets attached while moving only the dragged View.
        val row = rows.getValue(id)
        list.removeView(row)
        list.addView(row, destination)
        if (notify) onChange()
    }
    private fun refresh() {
        // Reuse the same Views, preserving font scaling and accessibility focus.
        list.removeAllViews()
        order.forEach { id -> list.addView(rows.getValue(id)) }
    }
    override fun answer(): MathAnswer = MathAnswer.Order(order.toList())
    override fun restore(answer: MathAnswer) {
        if (answer is MathAnswer.Order && answer.ids.size == order.size && answer.ids.toSet() == order.toSet()) {
            order.clear()
            order.addAll(answer.ids)
            nextTapPosition = 0
            refresh()
        }
    }
    override fun saveDraft(): JSONObject = JSONObject().put("nextTapPosition", nextTapPosition)
    override fun restoreDraft(state: JSONObject) {
        nextTapPosition = state.optInt("nextTapPosition", 0).coerceIn(0, order.size)
    }
    override fun setEditable(enabled: Boolean) {
        editable = enabled
        rows.values.forEach { it.isEnabled = enabled }
    }
}

private class MatchingControl(context: Context, spec: MathResponseSpec, private val accent: Int, private val onChange: () -> Unit) : MathResponseControl {
    override val view = MathWidgetUi.column(context)
    private val owner = Any()
    private val sources = linkedMapOf<String, LinearLayout>()
    private val sourceList = MathWidgetUi.column(context)
    private val targets = linkedMapOf<String, LinearLayout>()
    private val assignedLabels = linkedMapOf<String, TextView>()
    private val assignedSlots = linkedMapOf<String, LinearLayout>()
    private val pairs = linkedMapOf<String, String>()
    private var selectedSource: String? = null
    private var editable = true
    init {
        view.addView(MathWidgetUi.text(context, spec.config.optString("label", "Arrastra una ficha a su destino, o toca primero la ficha y después el destino. Toca un destino sin ficha seleccionada para vaciarlo.")))
        val sourceItems = MathWidgetUi.objects(spec.config.optJSONArray("sources"))
        val targetItems = MathWidgetUi.objects(spec.config.optJSONArray("targets"))
        require(sourceItems.isNotEmpty() && targetItems.isNotEmpty()) { "Faltan fichas o destinos." }
        view.addView(MathWidgetUi.text(context, "Fichas disponibles", true))
        sourceList.minimumHeight = MathWidgetUi.dp(context, 48)
        sourceList.contentDescription = "Fichas disponibles. Arrastra aquí una ficha asignada para liberarla."
        sourceList.setOnDragListener { _, event ->
            val token = event.localState as? MathDragToken
            if (!editable || token?.owner !== owner) false
            else when (event.action) {
                DragEvent.ACTION_DRAG_STARTED -> true
                DragEvent.ACTION_DROP -> { unassign(token.id); true }
                else -> true
            }
        }
        view.addView(sourceList)
        sourceItems.forEach { item ->
            val id = item.getString("id")
            require(id.isNotBlank() && id !in sources) { "ID de ficha vacío o repetido: $id" }
            val card = card(context)
            card.tag = MathInputFocusOrder.GESTURE_OWNER_TAG
            card.contentDescription = "Seleccionar ficha ${item.optString("label", id)}"
            card.addView(MathWidgetUi.rich(context, item, accent))
            card.setOnClickListener {
                if (editable) { selectedSource = if (selectedSource == id) null else id; refresh() }
            }
            DirectDragTouch.bind(card, { editable }, { target ->
                target.startDragAndDrop(ClipData.newPlainText("math_match", id), View.DragShadowBuilder(target), MathDragToken(owner, id), 0)
            })
            sources[id] = card
            sourceList.addView(card)
        }
        view.addView(MathWidgetUi.text(context, "Destinos", true))
        targetItems.forEach { item ->
            val id = item.getString("id")
            require(id.isNotBlank() && id !in targets) { "ID de destino vacío o repetido: $id" }
            val card = card(context)
            card.contentDescription = "Destino ${item.optString("label", id)}"
            card.addView(MathWidgetUi.rich(context, item, accent))
            val assigned = MathWidgetUi.text(context, "Sin asignar")
            assigned.setTextColor(MathWidgetUi.muted)
            card.addView(assigned)
            val slot = MathWidgetUi.column(context)
            card.addView(slot)
            card.setOnClickListener {
                if (editable) {
                    val source = selectedSource
                    if (source != null) assign(source, id)
                    else {
                        val removed = pairs.entries.firstOrNull { it.value == id }?.key
                        if (removed != null) unassign(removed)
                    }
                }
            }
            card.setOnDragListener { _, event ->
                val token = event.localState as? MathDragToken
                if (!editable || token?.owner !== owner) false
                else when (event.action) {
                    DragEvent.ACTION_DRAG_STARTED -> true
                    DragEvent.ACTION_DROP -> { assign(token.id, id); true }
                    DragEvent.ACTION_DRAG_ENDED -> { sources[token.id]?.let(DirectDragTouch::finish); true }
                    else -> true
                }
            }
            targets[id] = card
            assignedLabels[id] = assigned
            assignedSlots[id] = slot
            view.addView(card)
        }
        refresh()
    }
    private fun card(context: Context) = MathTouchCard(context).apply {
        isFocusable = true
        background = MathWidgetUi.background(context)
        setPadding(MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 6))
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 8) }
    }
    private fun assign(source: String, target: String) {
        if (!editable || source !in sources || target !in targets) return
        if (pairs[source] == target) { selectedSource = null; refresh(); return }
        val displaced = pairs.entries.firstOrNull { it.value == target }?.key
        if (displaced != null) pairs.remove(displaced)
        pairs[source] = target
        selectedSource = null
        refresh()
        onChange()
    }
    private fun unassign(source: String) {
        if (!editable || pairs.remove(source) == null) return
        selectedSource = null
        refresh()
        onChange()
    }
    private fun refresh() {
        // A source has exactly one View. Move it into a destination; never leave a clone behind.
        sources.values.forEach { (it.parent as? ViewGroup)?.removeView(it) }
        sources.forEach { (id, source) ->
            source.background = MathWidgetUi.background(source.context, selectedSource == id, accent)
            val destination = pairs[id]?.let { assignedSlots[it] } ?: sourceList
            destination.addView(source)
        }
        targets.forEach { (id, view) ->
            val source = pairs.entries.firstOrNull { it.value == id }?.key
            assignedLabels.getValue(id).visibility = if (source == null) View.VISIBLE else View.GONE
            view.background = MathWidgetUi.background(view.context, source != null, accent)
        }
    }
    override fun answer(): MathAnswer = MathAnswer.Matching(pairs.toMap())
    override fun restore(answer: MathAnswer) {
        if (answer is MathAnswer.Matching) {
            pairs.clear()
            answer.pairs.forEach { (source, target) -> if (source in sources && target in targets && target !in pairs.values) pairs[source] = target }
            selectedSource = null
            refresh()
        }
    }
    override fun setEditable(enabled: Boolean) {
        editable = enabled
        sourceList.isEnabled = enabled
        sources.values.forEach { it.isEnabled = enabled }
        targets.values.forEach { it.isEnabled = enabled }
        selectedSource = null
        refresh()
    }
}

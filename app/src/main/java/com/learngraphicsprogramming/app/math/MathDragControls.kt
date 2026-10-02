package com.learngraphicsprogramming.app.math

import android.content.ClipData
import android.content.Context
import android.view.DragEvent
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

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
    private val ranks = linkedMapOf<String, TextView>()
    private val arrows = linkedMapOf<String, Pair<Button, Button>>()
    private var editable = true
    init {
        view.addView(MathWidgetUi.text(context, spec.config.optString("label", "Mantén pulsada una tarjeta y arrástrala, o usa ↑ y ↓.")))
        val items = MathWidgetUi.objects(spec.config.optJSONArray("items"))
        require(items.isNotEmpty()) { "Faltan tarjetas para ordenar." }
        items.forEach { item ->
            val id = item.getString("id")
            require(id.isNotBlank() && id !in rows) { "ID de tarjeta vacío o repetido: $id" }
            order.add(id)
            val card = MathWidgetUi.column(context).apply {
                background = MathWidgetUi.background(context)
                setPadding(MathWidgetUi.dp(context, 10), MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 10), MathWidgetUi.dp(context, 6))
                layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 8) }
            }
            val header = MathWidgetUi.row(context)
            val rank = MathWidgetUi.text(context, "")
            rank.layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
            header.addView(rank)
            val up = MathWidgetUi.button(context, "↑", accent)
            val down = MathWidgetUi.button(context, "↓", accent)
            up.contentDescription = "Subir tarjeta $id"
            down.contentDescription = "Bajar tarjeta $id"
            up.setOnClickListener { move(id, order.indexOf(id) - 1) }
            down.setOnClickListener { move(id, order.indexOf(id) + 1) }
            header.addView(up)
            header.addView(down)
            card.addView(header)
            card.addView(MathWidgetUi.rich(context, item, accent))
            card.isFocusable = true
            card.setOnLongClickListener {
                editable && card.startDragAndDrop(ClipData.newPlainText("math_order", id), View.DragShadowBuilder(card), MathDragToken(owner, id), 0)
            }
            card.setOnDragListener { _, event ->
                val token = event.localState as? MathDragToken
                if (!editable || token?.owner !== owner) false
                else when (event.action) {
                    DragEvent.ACTION_DRAG_STARTED -> true
                    DragEvent.ACTION_DROP -> { move(token.id, order.indexOf(id)); true }
                    else -> true
                }
            }
            rows[id] = card
            ranks[id] = rank
            arrows[id] = up to down
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
    private fun move(id: String, destination: Int) {
        if (!editable || destination !in order.indices || order.indexOf(id) == destination) return
        order.remove(id)
        order.add(destination, id)
        refresh()
        onChange()
    }
    private fun refresh() {
        // Reuse the same Views, preserving font scaling and accessibility focus.
        list.removeAllViews()
        order.forEachIndexed { index, id ->
            ranks.getValue(id).text = "${index + 1}."
            arrows.getValue(id).first.isEnabled = editable && index > 0
            arrows.getValue(id).second.isEnabled = editable && index < order.lastIndex
            list.addView(rows.getValue(id))
        }
    }
    override fun answer(): MathAnswer = MathAnswer.Order(order.toList())
    override fun restore(answer: MathAnswer) {
        if (answer is MathAnswer.Order && answer.ids.size == order.size && answer.ids.toSet() == order.toSet()) {
            order.clear()
            order.addAll(answer.ids)
            refresh()
        }
    }
    override fun setEditable(enabled: Boolean) { editable = enabled; refresh() }
}

private class MatchingControl(context: Context, spec: MathResponseSpec, private val accent: Int, private val onChange: () -> Unit) : MathResponseControl {
    override val view = MathWidgetUi.column(context)
    private val owner = Any()
    private val sources = linkedMapOf<String, LinearLayout>()
    private val sourceLabels = linkedMapOf<String, String>()
    private val targets = linkedMapOf<String, LinearLayout>()
    private val assignedLabels = linkedMapOf<String, TextView>()
    private val pairs = linkedMapOf<String, String>()
    private var selectedSource: String? = null
    private var editable = true
    init {
        view.addView(MathWidgetUi.text(context, spec.config.optString("label", "Arrastra una ficha a su destino, o toca primero la ficha y después el destino. Toca un destino sin ficha seleccionada para vaciarlo.")))
        val sourceItems = MathWidgetUi.objects(spec.config.optJSONArray("sources"))
        val targetItems = MathWidgetUi.objects(spec.config.optJSONArray("targets"))
        require(sourceItems.isNotEmpty() && targetItems.isNotEmpty()) { "Faltan fichas o destinos." }
        sourceItems.forEach { item ->
            val id = item.getString("id")
            require(id.isNotBlank() && id !in sources) { "ID de ficha vacío o repetido: $id" }
            val card = card(context)
            card.contentDescription = "Seleccionar ficha ${item.optString("label", id)}"
            sourceLabels[id] = item.optString("label", id)
            card.addView(MathWidgetUi.rich(context, item, accent))
            card.setOnClickListener {
                if (editable) { selectedSource = if (selectedSource == id) null else id; refresh() }
            }
            card.setOnLongClickListener {
                editable && card.startDragAndDrop(ClipData.newPlainText("math_match", id), View.DragShadowBuilder(card), MathDragToken(owner, id), 0)
            }
            sources[id] = card
            view.addView(card)
        }
        view.addView(MathWidgetUi.text(context, "Destinos", true))
        targetItems.forEach { item ->
            val id = item.getString("id")
            require(id.isNotBlank() && id !in targets) { "ID de destino vacío o repetido: $id" }
            val card = card(context)
            card.addView(MathWidgetUi.rich(context, item, accent))
            val assigned = MathWidgetUi.text(context, "Sin asignar")
            assigned.setTextColor(MathWidgetUi.muted)
            card.addView(assigned)
            card.setOnClickListener {
                if (editable) {
                    val source = selectedSource
                    if (source != null) assign(source, id)
                    else {
                        val removed = pairs.entries.firstOrNull { it.value == id }?.key
                        if (removed != null) { pairs.remove(removed); refresh(); onChange() }
                    }
                }
            }
            card.setOnDragListener { _, event ->
                val token = event.localState as? MathDragToken
                if (!editable || token?.owner !== owner) false
                else when (event.action) {
                    DragEvent.ACTION_DRAG_STARTED -> true
                    DragEvent.ACTION_DROP -> { assign(token.id, id); true }
                    else -> true
                }
            }
            targets[id] = card
            assignedLabels[id] = assigned
            view.addView(card)
        }
        refresh()
    }
    private fun card(context: Context) = MathWidgetUi.column(context).apply {
        isFocusable = true
        background = MathWidgetUi.background(context)
        setPadding(MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 6))
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 8) }
    }
    private fun assign(source: String, target: String) {
        if (!editable || source !in sources || target !in targets) return
        val displaced = pairs.entries.firstOrNull { it.value == target }?.key
        if (displaced != null) pairs.remove(displaced)
        pairs[source] = target
        selectedSource = null
        refresh()
        onChange()
    }
    private fun refresh() {
        sources.forEach { (id, view) -> view.background = MathWidgetUi.background(view.context, selectedSource == id, accent) }
        targets.forEach { (id, view) ->
            val source = pairs.entries.firstOrNull { it.value == id }?.key
            assignedLabels.getValue(id).text = source?.let { "Asignado: ${sourceLabels[it]}" } ?: "Sin asignar"
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
        sources.values.forEach { it.isEnabled = enabled }
        targets.values.forEach { it.isEnabled = enabled }
        selectedSource = null
        refresh()
    }
}

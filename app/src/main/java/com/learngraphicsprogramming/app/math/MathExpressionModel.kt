package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject

data class MathSelection(val path: List<MathCursorStep>, val start: Int, val end: Int)

/** All edits replace AST nodes. LaTeX is used only at import/export boundaries. */
class MathExpressionModel(initialExpression: MathExpression.Sequence = MathExpression.Sequence()) {
    var expression: MathExpression.Sequence = initialExpression
        private set
    var cursor: MathCursor = MathCursor(offset = initialExpression.children.size)
        set(value) { field = safeCursor(value) }
    var selectionRange: MathSelection? = null
        private set

    fun setExpression(value: MathExpression.Sequence, position: MathCursor = MathCursor(offset = value.children.size)) {
        expression = value
        cursor = position
        selectionRange = null
    }

    fun isComplete(): Boolean = expression.isComplete() && MathLatexParser.parse(MathLatexSerializer.serialize(expression)) is MathLatexParser.Result.Success
    fun clear() = setExpression(MathExpression.Sequence())

    fun selectAll() {
        selectionRange = MathSelection(emptyList(), 0, expression.children.size)
        cursor = MathCursor(offset = expression.children.size)
    }

    fun select(path: List<MathCursorStep>, start: Int, end: Int) {
        val sequence = sequenceAt(path) ?: return
        val left = minOf(start, end).coerceIn(0, sequence.children.size)
        val right = maxOf(start, end).coerceIn(left, sequence.children.size)
        selectionRange = if (left == right) null else MathSelection(path.toList(), left, right)
        cursor = MathCursor(path, right)
    }

    fun clearSelection() { selectionRange = null }

    fun takeSelection(): MathExpression.Sequence {
        val selection = selectionRange ?: return MathExpression.Sequence()
        val sequence = sequenceAt(selection.path) ?: return MathExpression.Sequence().also { selectionRange = null }
        val selected = MathExpression.Sequence(sequence.children.subList(selection.start, selection.end).toList())
        replaceSequence(selection.path, sequence.copy(children = sequence.children.take(selection.start) + sequence.children.drop(selection.end)))
        selectionRange = null
        cursor = MathCursor(selection.path, selection.start)
        return selected
    }

    fun takePrevious(): MathExpression.Sequence {
        if (selectionRange != null) return takeSelection()
        val current = sequenceAt(cursor.path) ?: return MathExpression.Sequence()
        if (cursor.offset == 0) return MathExpression.Sequence()
        val end = cursor.offset
        var start = end - 1
        val previous = current.children[start]
        if (previous is MathExpression.Number) {
            while (start > 0 && current.children[start - 1] is MathExpression.Number) start--
        }
        val selected = current.children.subList(start, end)
        val result = if (previous is MathExpression.Number) listOf(MathExpression.Number(selected.joinToString("") { (it as MathExpression.Number).value })) else selected.toList()
        replaceSequence(cursor.path, current.copy(children = current.children.take(start) + current.children.drop(end)))
        cursor = cursor.copy(offset = start)
        return MathExpression.Sequence(result)
    }

    fun insert(node: MathExpression, enterSlot: String? = null): Boolean {
        val selection = selectionRange
        val position = selection?.let { MathCursor(it.path, it.start) } ?: cursor
        val current = sequenceAt(position.path) ?: return false
        val end = selection?.end ?: position.offset
        val removed = current.children.subList(position.offset, end)
        val added = if (node is MathExpression.Sequence) node.children else listOf(node)
        if (position.path.size + structuralDepth(node) > 60 ||
            nodeCount(expression) - removed.sumOf(::nodeCount) + added.sumOf(::nodeCount) > 4_096 ||
            MathLatexSerializer.serialize(expression).length - removed.sumOf { MathLatexSerializer.serialize(it).length } +
            added.sumOf { MathLatexSerializer.serialize(it).length } > 32_768) return false
        replaceSequence(position.path, current.copy(children = current.children.take(position.offset) + added + current.children.drop(end)))
        selectionRange = null
        val slot = enterSlot?.takeIf { node.childSlots().any { child -> child.first == it } }
        cursor = if (slot != null && added.size == 1) MathCursor(position.path + MathCursorStep(position.offset, slot), 0)
            else position.copy(offset = position.offset + added.size)
        return true
    }

    /** Power/subscript actions share the same base, even while editing the other script. */
    fun addScript(upper: Boolean): Boolean {
        val originalExpression = expression
        val originalCursor = cursor
        val originalSelection = selectionRange
        val slot = if (upper) "upper" else "lower"
        if (selectionRange == null) {
            for (depth in cursor.path.indices.reversed()) {
                val step = cursor.path[depth]
                val parentPath = cursor.path.take(depth)
                val scripts = sequenceAt(parentPath)?.children?.getOrNull(step.nodeIndex) as? MathExpression.Scripts ?: continue
                val existing = if (upper) scripts.upper else scripts.lower
                if (existing != null) {
                    cursor = MathCursor(parentPath + step.copy(slot = slot), existing.children.size)
                    return true
                }
                val updated = if (upper) scripts.copy(upper = MathExpression.Sequence()) else scripts.copy(lower = MathExpression.Sequence())
                select(parentPath, step.nodeIndex, step.nodeIndex + 1)
                if (insert(updated, slot)) return true
                expression = originalExpression
                cursor = originalCursor
                selectionRange = originalSelection
                return false
            }
        }
        val base = takePrevious()
        val previous = base.children.singleOrNull() as? MathExpression.Scripts
        val scripts = previous ?: MathExpression.Scripts(base)
        val existing = if (upper) scripts.upper else scripts.lower
        val updated = if (upper) scripts.copy(upper = existing ?: MathExpression.Sequence())
            else scripts.copy(lower = existing ?: MathExpression.Sequence())
        val target = if (updated.base.children.isEmpty()) "base" else slot
        if (insert(updated, target)) {
            if (existing != null && target == slot) cursor = cursor.copy(offset = existing.children.size)
            return true
        }
        expression = originalExpression
        cursor = originalCursor
        selectionRange = originalSelection
        return false
    }

    /** IME text becomes glyph nodes or imported supported LaTeX, never a mutable LaTeX string. */
    fun insertText(text: String): Boolean {
        if (text.isEmpty()) return true
        if (text.length > 32_768 || MathLatexSerializer.serialize(expression).length + text.length > 32_768) return false
        if (text.length > 1 || text.contains('\\')) {
            val parsed = MathLatexParser.parse(text)
            if (parsed is MathLatexParser.Result.Success) return insert(parsed.expression)
            if (text.contains('\\')) return false
        }
        // Check the complete incoming batch before editing so unsupported paste is atomic.
        if (text.any { !(it.isLetterOrDigit() || it.isWhitespace() || it.toString() in MathLatexSerializer.symbols || it in ".,+-−*/×÷·=<>≤≥≠≈→π∞∂()[]|^_!:%;") }) return false
        if (text.count { !it.isWhitespace() } + nodeCount(expression) > 4_096) return false
        val originalExpression = expression
        val originalCursor = cursor
        val originalSelection = selectionRange
        for (char in text) {
            val inserted = when {
                char.isWhitespace() -> true
                char.isDigit() || char == '.' || char == ',' -> insertNumericCharacter(if (char == ',') "." else char.toString())
                (char.toString() in MathLatexSerializer.symbols && char !in "≤≥≠≈→") || char == 'e' -> insert(MathExpression.Symbol(char.toString()))
                char.isLetter() -> insert(MathExpression.Variable(char.toString()))
                char in "+-−*/×÷·" -> insert(MathExpression.Operator(char.toString()))
                char in "=<>≤≥≠≈→" -> insert(MathExpression.Relation(char.toString()))
                char == '(' || char == '[' || char == '|' -> {
                    val delimiter = if (char == '(') MathDelimiter.PARENTHESES else if (char == '[') MathDelimiter.BRACKETS else MathDelimiter.ABSOLUTE
                    insert(MathExpression.Group(takeSelection(), delimiter), "content")
                }
                char == ')' || char == ']' -> { exitCurrentSlot(); true }
                char == '^' || char == '_' -> addScript(upper = char == '^')
                else -> insert(MathExpression.Symbol(char.toString()))
            }
            if (!inserted) {
                expression = originalExpression
                cursor = originalCursor
                selectionRange = originalSelection
                return false
            }
        }
        return true
    }

    private fun insertNumericCharacter(value: String): Boolean {
        takeSelection()
        val sequence = sequenceAt(cursor.path) ?: return false
        val previous = sequence.children.getOrNull(cursor.offset - 1) as? MathExpression.Number
        if (previous == null) return insert(MathExpression.Number(value))
        val children = sequence.children.toMutableList()
        children[cursor.offset - 1] = previous.copy(value = previous.value + value)
        replaceSequence(cursor.path, sequence.copy(children = children))
        return true
    }

    fun insertLatex(latex: String): MathLatexParser.Result {
        val parsed = MathLatexParser.parse(latex)
        return if (parsed is MathLatexParser.Result.Success && !insert(parsed.expression))
            MathLatexParser.Result.Failure("Expression exceeds the editable resource limits", 0, latex.length)
        else parsed
    }

    /** True while movement is still inside this expression, false at its outer boundary. */
    fun moveRight(): Boolean {
        selectionRange = null
        val before = cursor
        val sequence = sequenceAt(cursor.path) ?: return false
        if (cursor.offset < sequence.children.size) {
            val slots = sequence.children[cursor.offset].childSlots()
            cursor = if (slots.isNotEmpty()) MathCursor(cursor.path + MathCursorStep(cursor.offset, slots.first().first), 0)
                else cursor.copy(offset = cursor.offset + 1)
        } else moveBetweenSlots(forward = true)
        return cursor != before
    }

    fun moveLeft(): Boolean {
        selectionRange = null
        val before = cursor
        val sequence = sequenceAt(cursor.path) ?: return false
        if (cursor.offset > 0) {
            val index = cursor.offset - 1
            val slots = sequence.children[index].childSlots()
            cursor = if (slots.isNotEmpty()) MathCursor(cursor.path + MathCursorStep(index, slots.last().first), slots.last().second.children.size)
                else cursor.copy(offset = index)
        } else moveBetweenSlots(forward = false)
        return cursor != before
    }

    /** Tab/IME navigation visits nested slots before leaving the current response field. */
    fun nextSlot(): Boolean = moveToSlot(forward = true)
    fun previousSlot(): Boolean = moveToSlot(forward = false)

    private fun moveToSlot(forward: Boolean): Boolean {
        selectionRange = null
        val before = cursor
        val sequence = sequenceAt(cursor.path) ?: return false
        val indices = if (forward) cursor.offset until sequence.children.size else (cursor.offset - 1 downTo 0)
        val index = indices.firstOrNull { sequence.children[it].childSlots().isNotEmpty() }
        if (index != null) {
            val slots = sequence.children[index].childSlots()
            val target = if (forward) slots.first() else slots.last()
            cursor = MathCursor(cursor.path + MathCursorStep(index, target.first), if (forward) 0 else target.second.children.size)
        } else if (cursor.path.isNotEmpty()) {
            moveBetweenSlots(forward)
        } else {
            cursor = MathCursor(offset = if (forward) sequence.children.size else 0)
        }
        return cursor != before
    }

    private fun moveBetweenSlots(forward: Boolean) {
        if (cursor.path.isEmpty()) return
        val step = cursor.path.last()
        val parentPath = cursor.path.dropLast(1)
        val parent = sequenceAt(parentPath) ?: return
        val slots = parent.children.getOrNull(step.nodeIndex)?.childSlots() ?: return
        val current = slots.indexOfFirst { it.first == step.slot }
        val target = current + if (forward) 1 else -1
        cursor = if (target in slots.indices) MathCursor(parentPath + step.copy(slot = slots[target].first), if (forward) 0 else slots[target].second.children.size)
            else MathCursor(parentPath, step.nodeIndex + if (forward) 1 else 0)
    }

    fun exitCurrentSlot() {
        selectionRange = null
        if (cursor.path.isNotEmpty()) cursor = MathCursor(cursor.path.dropLast(1), cursor.path.last().nodeIndex + 1)
    }

    fun deleteBackward() {
        if (selectionRange != null) { takeSelection(); return }
        val sequence = sequenceAt(cursor.path) ?: return
        if (cursor.offset > 0) {
            val index = cursor.offset - 1
            val previous = sequence.children[index]
            val shortened = if (previous is MathExpression.Number && previous.value.length > 1) previous.copy(value = previous.value.dropLast(1)) else null
            val slots = previous.childSlots()
            if (slots.isNotEmpty() && !structurallyEmpty(previous)) {
                cursor = MathCursor(cursor.path + MathCursorStep(index, slots.last().first), slots.last().second.children.size)
                deleteBackward()
                return
            }
            replaceSequence(cursor.path, sequence.copy(children = sequence.children.take(index) + listOfNotNull(shortened) + sequence.children.drop(index + 1)))
            cursor = cursor.copy(offset = if (shortened == null) index else index + 1)
        } else if (cursor.path.isNotEmpty()) {
            val step = cursor.path.last()
            val parentPath = cursor.path.dropLast(1)
            val parent = sequenceAt(parentPath) ?: return
            val node = parent.children.getOrNull(step.nodeIndex) ?: return
            if (structurallyEmpty(node)) {
                replaceNodeAt(parentPath, step.nodeIndex, emptyList())
            } else if (sequence.children.isEmpty() && removeEmptyOptionalSlot(parentPath, step, node)) {
                // Removing an optional script/base/index must keep its populated siblings.
            } else if (sequence.children.isEmpty() && node is MathExpression.Fraction) {
                val retained = if (step.slot == "numerator") node.denominator else node.numerator
                replaceNodeAt(parentPath, step.nodeIndex, retained.children)
            } else if (node is MathExpression.Group && step.slot == "content") {
                replaceNodeAt(parentPath, step.nodeIndex, node.content.children)
            } else if (node is MathExpression.Root && node.index == null && step.slot == "radicand") {
                replaceNodeAt(parentPath, step.nodeIndex, node.radicand.children)
            } else if (node is MathExpression.Function && node.base == null && step.slot == "argument") {
                replaceNodeAt(parentPath, step.nodeIndex, node.argument.children)
            } else moveBetweenSlots(forward = false)
        }
    }

    private fun replaceNodeAt(path: List<MathCursorStep>, index: Int, replacement: List<MathExpression>) {
        val parent = sequenceAt(path) ?: return
        replaceSequence(path, parent.copy(children = parent.children.take(index) + replacement + parent.children.drop(index + 1)))
        cursor = MathCursor(path, index + replacement.size)
    }

    private fun removeEmptyOptionalSlot(parentPath: List<MathCursorStep>, step: MathCursorStep, node: MathExpression): Boolean {
        val updated = when (node) {
            is MathExpression.Scripts -> when (step.slot) {
                "upper" -> node.copy(upper = null)
                "lower" -> node.copy(lower = null)
                else -> return false
            }
            is MathExpression.Root -> if (step.slot == "index") node.copy(index = null) else return false
            is MathExpression.Function -> if (step.slot == "base") node.copy(base = null) else return false
            is MathExpression.LargeOperator -> when (step.slot) {
                "lower" -> node.copy(lower = null)
                "upper" -> node.copy(upper = null)
                else -> return false
            }
            is MathExpression.Derivative -> if (step.slot == "order") node.copy(order = null) else return false
            else -> return false
        }
        if (updated is MathExpression.Scripts && updated.upper == null && updated.lower == null) {
            replaceNodeAt(parentPath, step.nodeIndex, updated.base.children)
            return true
        }
        val parent = sequenceAt(parentPath) ?: return false
        val children = parent.children.toMutableList()
        children[step.nodeIndex] = updated
        replaceSequence(parentPath, parent.copy(children = children))
        val previousNames = node.childSlots().takeWhile { it.first != step.slot }.map { it.first }
        val slots = updated.childSlots()
        val previous = slots.lastOrNull { it.first in previousNames }
        val target = previous ?: slots.first()
        cursor = MathCursor(parentPath + step.copy(slot = target.first), if (previous != null) target.second.children.size else 0)
        return true
    }

    private fun structurallyEmpty(node: MathExpression): Boolean = when (node) {
        is MathExpression.Sequence -> node.children.all { structurallyEmpty(it) }
        else -> node.childSlots().takeIf { it.isNotEmpty() }?.all { structurallyEmpty(it.second) } ?: false
    }

    private fun nodeCount(node: MathExpression): Int = when (node) {
        is MathExpression.Sequence -> 1 + node.children.sumOf { nodeCount(it) }
        else -> 1 + node.childSlots().sumOf { nodeCount(it.second) }
    }

    private fun structuralDepth(node: MathExpression): Int = when (node) {
        is MathExpression.Sequence -> node.children.maxOfOrNull { structuralDepth(it) } ?: 0
        else -> 1 + (node.childSlots().maxOfOrNull { structuralDepth(it.second) } ?: 0)
    }

    fun saveDraft(): String = JSONObject()
        .put("version", 1)
        .put("expression", encode(expression))
        .put("cursor", encodeCursor(cursor))
        .apply { selectionRange?.let { put("selection", JSONObject().put("path", encodePath(it.path)).put("start", it.start).put("end", it.end)) } }
        .toString()

    /** Invalid or unsupported drafts leave the current tree unchanged. */
    fun restoreDraft(draft: String): Boolean {
        if (draft.length > 262_144 || !boundedJsonNesting(draft)) return false
        return try {
            val json = JSONObject(draft)
            require(json.getInt("version") == 1)
            val tree = decode(json.getJSONObject("expression"), 0) as? MathExpression.Sequence ?: return false
            require(nodeCount(tree) <= 4_096 && structuralDepth(tree) <= 60 && MathLatexSerializer.serialize(tree).length <= 32_768)
            val restoredCursor = decodeCursor(json.getJSONObject("cursor"))
            val selection = json.optJSONObject("selection")?.let {
                MathSelection(decodePath(it.getJSONArray("path")), it.getInt("start"), it.getInt("end"))
            }
            setExpression(tree, restoredCursor)
            if (selection != null) select(selection.path, selection.start, selection.end)
            true
        } catch (_: Exception) { false }
    }

    private fun boundedJsonNesting(value: String): Boolean {
        var depth = 0
        var quoted = false
        var escaped = false
        for (char in value) {
            if (quoted) {
                if (escaped) escaped = false else if (char == '\\') escaped = true else if (char == '"') quoted = false
            } else when (char) {
                '"' -> quoted = true
                '{', '[' -> if (++depth > 256) return false
                '}', ']' -> if (--depth < 0) return false
            }
        }
        return depth == 0 && !quoted
    }

    /** Immutable slot contents; the cursor remains the sole active-slot state. */
    internal fun sequenceAt(path: List<MathCursorStep>): MathExpression.Sequence? {
        var result = expression
        for (step in path) result = result.children.getOrNull(step.nodeIndex)?.childSlots()?.firstOrNull { it.first == step.slot }?.second ?: return null
        return result
    }

    private fun safeCursor(value: MathCursor): MathCursor {
        var sequence = expression
        val validPath = mutableListOf<MathCursorStep>()
        for (step in value.path) {
            val node = sequence.children.getOrNull(step.nodeIndex)
            val child = node?.childSlots()?.firstOrNull { it.first == step.slot }?.second
                ?: return MathCursor(validPath.toList(), (step.nodeIndex + if (node != null) 1 else 0).coerceIn(0, sequence.children.size))
            validPath += step
            sequence = child
        }
        return MathCursor(validPath.toList(), value.offset.coerceIn(0, sequence.children.size))
    }

    private fun replaceSequence(path: List<MathCursorStep>, value: MathExpression.Sequence) {
        fun replace(current: MathExpression.Sequence, depth: Int): MathExpression.Sequence {
            if (depth == path.size) return value
            val step = path[depth]
            val node = current.children.getOrNull(step.nodeIndex) ?: return current
            val child = node.childSlots().firstOrNull { it.first == step.slot }?.second ?: return current
            val children = current.children.toMutableList()
            children[step.nodeIndex] = node.withChildSlot(step.slot, replace(child, depth + 1))
            return current.copy(children = children)
        }
        expression = replace(expression, 0)
    }

    private fun encodeCursor(value: MathCursor) = JSONObject().put("path", encodePath(value.path)).put("offset", value.offset)
    private fun encodePath(path: List<MathCursorStep>) = JSONArray().apply { path.forEach { put(JSONObject().put("node", it.nodeIndex).put("slot", it.slot)) } }
    private fun decodeCursor(value: JSONObject) = MathCursor(decodePath(value.getJSONArray("path")), value.getInt("offset"))
    private fun decodePath(value: JSONArray): List<MathCursorStep> {
        require(value.length() <= 64)
        return (0 until value.length()).map { value.getJSONObject(it).let { step -> MathCursorStep(step.getInt("node"), step.getString("slot")) } }
    }

    private fun encode(node: MathExpression): JSONObject {
        val json = JSONObject()
        when (node) {
            is MathExpression.Sequence -> json.put("type", "sequence").put("children", JSONArray().apply { node.children.forEach { put(encode(it)) } })
            is MathExpression.Number -> json.put("type", "number").put("value", node.value)
            is MathExpression.Variable -> json.put("type", "variable").put("value", node.name)
            is MathExpression.Symbol -> json.put("type", "symbol").put("value", node.value)
            is MathExpression.Operator -> json.put("type", "operator").put("value", node.value)
            is MathExpression.Relation -> json.put("type", "relation").put("value", node.value)
            is MathExpression.Fraction -> json.put("type", "fraction")
            is MathExpression.Scripts -> json.put("type", "scripts")
            is MathExpression.Root -> json.put("type", "root")
            is MathExpression.Group -> json.put("type", "group").put("delimiter", node.delimiter.name)
            is MathExpression.Function -> json.put("type", "function").put("name", node.name)
            is MathExpression.LargeOperator -> json.put("type", "largeOperator").put("kind", node.kind.name)
            is MathExpression.Limit -> json.put("type", "limit")
            is MathExpression.Derivative -> json.put("type", "derivative")
            is MathExpression.Matrix -> json.put("type", "matrix").put("rows", node.rows.size).put("columns", node.rows.first().size)
            is MathExpression.Cases -> json.put("type", "cases").put("rows", node.rows.size)
        }
        node.childSlots().forEach { (name, child) -> json.put(name, encode(child)) }
        return json
    }

    private fun decode(json: JSONObject, depth: Int): MathExpression {
        require(depth < 128)
        fun child(name: String) = decode(json.getJSONObject(name), depth + 1) as MathExpression.Sequence
        fun optional(name: String) = json.optJSONObject(name)?.let { decode(it, depth + 1) as MathExpression.Sequence }
        return when (json.getString("type")) {
            "sequence" -> json.getJSONArray("children").let { array -> require(array.length() <= 4_096); MathExpression.Sequence((0 until array.length()).map { decode(array.getJSONObject(it), depth + 1) }) }
            "number" -> MathExpression.Number(json.getString("value"))
            "variable" -> MathExpression.Variable(json.getString("value"))
            "symbol" -> MathExpression.Symbol(json.getString("value"))
            "operator" -> MathExpression.Operator(json.getString("value"))
            "relation" -> MathExpression.Relation(json.getString("value"))
            "fraction" -> MathExpression.Fraction(child("numerator"), child("denominator"))
            "scripts" -> MathExpression.Scripts(child("base"), optional("upper"), optional("lower"))
            "root" -> MathExpression.Root(child("radicand"), optional("index"))
            "group" -> MathExpression.Group(child("content"), MathDelimiter.valueOf(json.getString("delimiter")))
            "function" -> MathExpression.Function(json.getString("name"), child("argument"), optional("base"))
            "largeOperator" -> MathExpression.LargeOperator(MathLargeOperator.valueOf(json.getString("kind")), child("body"), optional("lower"), optional("upper"))
            "limit" -> MathExpression.Limit(child("body"), child("condition"))
            "derivative" -> MathExpression.Derivative(child("body"), child("variable"), optional("order"))
            "matrix" -> {
                val rows = json.getInt("rows"); val columns = json.getInt("columns")
                require(rows in 1..32 && columns in 1..32)
                MathExpression.Matrix((0 until rows).map { r -> (0 until columns).map { c -> child("r${r}c$c") } })
            }
            "cases" -> {
                val rows = json.getInt("rows"); require(rows in 1..32)
                MathExpression.Cases((0 until rows).map { MathExpression.Case(child("expression$it"), child("condition$it")) })
            }
            else -> throw IllegalArgumentException("Unsupported draft node")
        }
    }
}

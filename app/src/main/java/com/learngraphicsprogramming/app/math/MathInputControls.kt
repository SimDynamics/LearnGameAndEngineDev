package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.res.ColorStateList
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.view.Gravity
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.CheckBox
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.TextView
import org.json.JSONObject
import androidx.appcompat.widget.AppCompatSpinner

internal object MathInputControls {
    fun create(context: Context, spec: MathResponseSpec, accent: Int, onChange: () -> Unit): MathResponseControl? =
        when (spec.type) {
            "math_multiple_choice_single", "math_multiple_choice_multi" -> ChoiceControl(context, spec, accent, onChange)
            "math_numeric_input", "math_expression_input" -> {
                if (spec.config.has("fields")) FieldsControl(context, spec, accent, onChange)
                else TextControl(context, spec, accent, onChange)
            }
            "math_multi_input", "math_structured_answer", "math_equation_fill_blanks" -> FieldsControl(context, spec, accent, onChange)
            else -> null
        }
}

private fun input(context: Context, accent: Int, hint: String, keyboardMode: String): EditText = EditText(context).apply {
    tag = MathInputFocusOrder.GESTURE_OWNER_TAG
    setTextColor(MathWidgetUi.foreground)
    setHintTextColor(MathWidgetUi.muted)
    this.hint = hint
    textSize = 18f
    // Keep the unrestricted TextKeyListener for fractions/symbol shortcuts. Only tell the
    // Android IME to show its signed decimal keypad for numeric/fraction contexts.
    inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
    isSingleLine = true
    if (keyboardMode.uppercase() in setOf("NUMERIC", "FRACTION")) {
        setRawInputType(InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED or InputType.TYPE_NUMBER_FLAG_DECIMAL)
    }
    setSelectAllOnFocus(true)
    minWidth = MathWidgetUi.dp(context, 100)
    minHeight = MathWidgetUi.dp(context, 48)
    background = MathWidgetUi.background(context, true, accent)
    setPadding(MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 8), MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 8))
    layoutParams = LinearLayout.LayoutParams(-1, -2)
}

private fun EditText.changed(callback: () -> Unit) {
    addTextChangedListener(object : TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = callback()
        override fun afterTextChanged(s: Editable?) = Unit
    })
}

/** Newly created spinner rows inherit the lesson scale already applied to this control. */
private class MathDropdown(context: Context) :
    AppCompatSpinner(context),
    MathTextScaleAware {

    var mathTextScale = 1f
        private set

    override fun scaleMathText(factor: Float) {
        mathTextScale *= factor
    }
}

private class TextControl(
    context: Context,
    spec: MathResponseSpec,
    accent: Int,
    onChange: () -> Unit
) : MathResponseControl {

    override val view =
        MathWidgetUi.column(context)

    private val fieldType =
        if (spec.type == "math_expression_input") {
            "expression"
        } else {
            "numeric"
        }

    private val inputMode =
        MathInputMode.parse(
            spec.config.optString("inputMode", "simple"),
            fieldType
        )

    private val keyboardMode =
        spec.config.optString(
            "keyboard",
            if (fieldType == "expression") {
                "ALGEBRA"
            } else {
                "NUMERIC"
            }
        )

    /*
     * FUTURO:
     *
     * when (inputMode) {
     *     SIMPLE -> EditText
     *     STRUCTURED -> MathExpressionEditor
     * }
     *
     * Por ahora el editor estructurado todavía no existe.
     */
    private val edit =
        when (inputMode) {
            MathInputMode.SIMPLE ->
                input(
                    context,
                    accent,
                    spec.config.optString(
                        "hint",
                        "Respuesta"
                    ),
                    keyboardMode
                )

            MathInputMode.STRUCTURED ->
                throw IllegalStateException(
                    "MathExpressionEditor todavía no está disponible."
                )
        }

    private val keyboard =
        MathKeyboardView(
            context,
            accent
        )

    init {
        val label =
            spec.config.optString("label")

        if (label.isNotBlank()) {
            view.addView(
                MathWidgetUi.text(
                    context,
                    label
                )
            )
        }

        val unit =
            spec.config.optString("unit")

        if (unit.isNotBlank()) {
            view.addView(
                MathWidgetUi.text(
                    context,
                    "Unidad: $unit"
                )
            )
        }

        edit.contentDescription =
            label.ifBlank {
                "Respuesta matemática"
            }

        MathKeyboardContext.configure(
            edit,
            spec.validator,
            keyboardMode,
            spec.config.optJSONArray(
                "keyboardSymbols"
            )
        )

        view.addView(edit)
        view.addView(keyboard)

        edit.setOnFocusChangeListener { _, focused ->
            if (focused) {
                keyboard.bind(
                    edit,
                    keyboardMode
                )
            } else {
                keyboard.hide()
            }
        }

        edit.changed(onChange)
    }

    override fun answer(): MathAnswer =
        MathAnswer.Text(
            edit.text.toString()
        )

    override fun restore(
        answer: MathAnswer
    ) {
        if (answer is MathAnswer.Text) {
            edit.setText(answer.value)
        }
    }

    override fun setEditable(
        enabled: Boolean
    ) {
        edit.isEnabled = enabled

        if (!enabled) {
            keyboard.hide()
        }
    }
}

private class ChoiceControl(context: Context, spec: MathResponseSpec, private val accent: Int, private val onChange: () -> Unit) : MathResponseControl {
    override val view = MathWidgetUi.column(context)
    private val multi = spec.type == "math_multiple_choice_multi"
    private val selected = linkedSetOf<String>()
    private val buttons = linkedMapOf<String, android.widget.CompoundButton>()
    private val cards = linkedMapOf<String, LinearLayout>()
    private var refreshing = false
    private var editable = true
    init {
        val options = MathWidgetUi.objects(spec.config.optJSONArray("options"))
        require(options.isNotEmpty()) { "La selección necesita opciones." }
        val label = spec.config.optString("label", if (multi) "Selecciona todas las respuestas correctas." else "Selecciona una respuesta.")
        view.addView(MathWidgetUi.text(context, label))
        options.forEach { option ->
            val id = option.getString("id")
            require(id.isNotBlank() && !buttons.containsKey(id)) { "ID de opción vacío o repetido: $id" }
            val card = MathTouchCard(context).apply {
                background = MathWidgetUi.background(context)
                setPadding(MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 6), MathWidgetUi.dp(context, 12), MathWidgetUi.dp(context, 8))
                layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = MathWidgetUi.dp(context, 8) }
            }
            val button = if (multi) CheckBox(context) else RadioButton(context)
            // The rich body owns the visible answer. Keep the label for accessibility only.
            button.text = ""
            button.contentDescription = option.optString("label", "Opción ${buttons.size + 1}")
            button.setTextColor(MathWidgetUi.foreground)
            button.buttonTintList = ColorStateList.valueOf(accent)
            button.minHeight = MathWidgetUi.dp(context, 48)
            button.setOnCheckedChangeListener { _, checked ->
                if (!refreshing && editable) {
                    if (!multi) selected.clear()
                    if (checked) selected.add(id) else selected.remove(id)
                    refresh(context, accent)
                    onChange()
                }
            }
            cards[id] = card
            buttons[id] = button
            val optionRow = MathWidgetUi.row(context).apply { isBaselineAligned = false }
            optionRow.addView(button, LinearLayout.LayoutParams(MathWidgetUi.dp(context, 48), -2))
            optionRow.addView(MathWidgetUi.rich(context, option, accent), LinearLayout.LayoutParams(0, -2, 1f))
            card.addView(optionRow)
            card.isFocusable = true
            card.setOnClickListener { if (editable) button.isChecked = !button.isChecked }
            view.addView(card)
        }
    }
    private fun refresh(context: Context = view.context, accent: Int) {
        refreshing = true
        buttons.forEach { (id, button) ->
            button.isChecked = id in selected
            cards.getValue(id).background = MathWidgetUi.background(context, id in selected, accent)
        }
        refreshing = false
    }
    override fun answer(): MathAnswer = MathAnswer.Choice(selected.toSet())
    override fun restore(answer: MathAnswer) {
        if (answer !is MathAnswer.Choice) return
        selected.clear()
        selected.addAll(answer.selected.filter { it in buttons }.let { if (multi) it else it.take(1) })
        // Listener guards keep restoration separate from user edits.
        refresh(accent = accent)
    }
    override fun setEditable(enabled: Boolean) {
        editable = enabled
        buttons.values.forEach { it.isEnabled = enabled }
        cards.values.forEach { it.isEnabled = enabled }
    }
}

private class FieldsControl(context: Context, spec: MathResponseSpec, accent: Int, onChange: () -> Unit) : MathResponseControl {
    override val view = MathWidgetUi.column(context)
    private data class Field(val view: View, val read: () -> String, val write: (String) -> Unit, val editable: Boolean)
    private val fields = linkedMapOf<String, Field>()
    private val keyboard = MathKeyboardView(context, accent)

    init {
        val label = spec.config.optString("label")
        if (label.isNotBlank()) view.addView(MathWidgetUi.text(context, label))
        val definitions = MathWidgetUi.objects(spec.config.optJSONArray("fields"))
        require(definitions.isNotEmpty()) { "Faltan campos de respuesta." }
        val inlineFields = spec.config.optJSONArray("segments") != null
        definitions.forEach { definition ->
            val id = definition.getString("id")
            require(id.isNotBlank() && !fields.containsKey(id)) { "ID de campo vacío o repetido: $id" }
            val fieldColumn = MathWidgetUi.column(context).apply {
                orientation = if (inlineFields) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
                gravity = Gravity.CENTER_VERTICAL
                isBaselineAligned = false
            }
            val fieldLabel = definition.optString("label")
            // An inline answer is one visual line; descriptions remain on the controls.
            if (!inlineFields && fieldLabel.isNotBlank()) fieldColumn.addView(MathWidgetUi.text(context, fieldLabel))
            fun addControl(control: View) {
                fieldColumn.addView(control, LinearLayout.LayoutParams(if (inlineFields) -2 else -1, -2))
            }
            val field = when (definition.optString("type", "numeric")) {
                "fixed" -> {
                    val text = definition.getString("text")
                    addControl(MathWidgetUi.text(context, text).apply { contentDescription = fieldLabel.ifBlank { text } })
                    Field(fieldColumn, { text }, {}, false)
                }
                "dropdown" -> {
                    val options = MathWidgetUi.objects(definition.optJSONArray("options"))
                    require(options.isNotEmpty() && options.map { it.getString("id") }.toSet().size == options.size) { "Opciones inválidas en $id" }
                    val ids = listOf("") + options.map { it.getString("id") }
                    val labels = listOf("Selecciona…") + options.map { it.optString("label", it.getString("id")) }
                    val dropdown = MathDropdown(context).apply {
                        val owner = this
                        minimumHeight = MathWidgetUi.dp(context, 48)
                        contentDescription = fieldLabel.ifBlank { id }
                        adapter = object : ArrayAdapter<String>(context, android.R.layout.simple_spinner_item, labels) {
                            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View =
                                super.getView(position, convertView, parent).also {
                                    (it as TextView).apply {
                                        setTextColor(MathWidgetUi.foreground)
                                        gravity = Gravity.CENTER_VERTICAL
                                        minimumHeight = MathWidgetUi.dp(context, 48)
                                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f * owner.mathTextScale)
                                        MathTextRenderer.apply(this, labels[position])
                                    }
                                }
                            override fun getDropDownView(position: Int, convertView: View?, parent: ViewGroup): View =
                                super.getDropDownView(position, convertView, parent).also {
                                    (it as TextView).apply {
                                        gravity = Gravity.CENTER_VERTICAL
                                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f * owner.mathTextScale)
                                        MathTextRenderer.apply(this, labels[position])
                                    }
                                }
                        }.apply { setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
                        onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) = onChange()
                            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
                        }
                    }
                    addControl(dropdown)
                    Field(fieldColumn, { ids[dropdown.selectedItemPosition.coerceAtLeast(0)] }, { dropdown.setSelection(ids.indexOf(it).coerceAtLeast(0)) }, true)
                }
                "numeric", "expression" -> {

                    val fieldType =
                        definition.optString(
                            "type",
                            "numeric"
                        )

                    val inputMode =
                        MathInputMode.parse(
                            definition.optString(
                                "inputMode",
                                "simple"
                            ),
                            fieldType
                        )

                    val keyboardMode =
                        definition.optString(
                            "keyboard",
                            if (fieldType == "expression") {
                                "ALGEBRA"
                            } else {
                                "NUMERIC"
                            }
                        )

                    val edit =
                        when (inputMode) {
                            MathInputMode.SIMPLE ->
                                input(
                                    context,
                                    accent,
                                    definition.optString(
                                        "hint",
                                        if (inlineFields) {
                                            fieldLabel.ifBlank {
                                                "Respuesta"
                                            }
                                        } else {
                                            "Respuesta"
                                        }
                                    ),
                                    keyboardMode
                                )

                            MathInputMode.STRUCTURED ->
                                throw IllegalStateException(
                                    "MathExpressionEditor todavía no está disponible."
                                )
                        }

                    edit.contentDescription =
                        fieldLabel.ifBlank {
                            id
                        }

                    MathKeyboardContext.configure(
                        edit,
                        spec.validator
                            .optJSONObject("fields")
                            ?.optJSONObject(id),
                        keyboardMode,
                        definition.optJSONArray(
                            "keyboardSymbols"
                        ) ?: spec.config.optJSONArray(
                            "keyboardSymbols"
                        )
                    )

                    edit.changed(onChange)

                    edit.setOnFocusChangeListener { _, focused ->
                        if (focused) {
                            keyboard.bind(
                                edit,
                                keyboardMode
                            )
                        } else {
                            keyboard.hide()
                        }
                    }

                    addControl(edit)

                    Field(
                        fieldColumn,
                        { edit.text.toString() },
                        { edit.setText(it) },
                        true
                    )
                }
                else -> throw IllegalArgumentException("Tipo de campo desconocido en $id")
            }
            val unit = definition.optString("unit")
            if (unit.isNotBlank()) addControl(MathWidgetUi.text(context, unit))
            fields[id] = field
        }

        val used = mutableSetOf<String>()
        fun fieldView(id: String): View {
            require(used.add(id)) { "El campo $id aparece dos veces en la plantilla." }
            return fields[id]?.view ?: throw IllegalArgumentException("Campo de plantilla inexistente: $id")
        }
        val segments = spec.config.optJSONArray("segments")
        val rows = spec.config.optJSONArray("rows")
        when {
            segments != null -> {
                val line = MathWidgetUi.row(context).apply { isBaselineAligned = false }
                for (index in 0 until segments.length()) {
                    val segment = segments.getJSONObject(index)
                    val child = if (segment.has("field")) fieldView(segment.getString("field")) else MathWidgetUi.text(context, segment.getString("text"))
                    child.layoutParams = LinearLayout.LayoutParams(-2, -2).apply {
                        gravity = Gravity.CENTER_VERTICAL
                        marginEnd = MathWidgetUi.dp(context, 8)
                    }
                    line.addView(child)
                }
                view.addView(HorizontalScrollView(context).apply {
                    addView(line)
                })
            }
            spec.config.optString("layout") == "table" && rows != null -> {
                val table = MathWidgetUi.column(context)
                val headers = spec.config.optJSONArray("headers")
                if (headers != null) {
                    val header = MathWidgetUi.row(context)
                    for (index in 0 until headers.length()) header.addView(MathWidgetUi.text(context, headers.getString(index), true).apply { layoutParams = LinearLayout.LayoutParams(MathWidgetUi.dp(context, 150), -2) })
                    table.addView(header)
                }
                for (rowIndex in 0 until rows.length()) {
                    val row = MathWidgetUi.row(context).apply { isBaselineAligned = false }
                    val cells = rows.getJSONArray(rowIndex)
                    for (index in 0 until cells.length()) {
                        val cell = cells.optJSONObject(index)
                        val child = if (cell?.has("field") == true) fieldView(cell.getString("field")) else MathWidgetUi.text(context, cells.optString(index))
                        child.layoutParams = LinearLayout.LayoutParams(MathWidgetUi.dp(context, 150), -2)
                        row.addView(child)
                    }
                    table.addView(row)
                }
                view.addView(HorizontalScrollView(context).apply {
                    addView(table)
                })
            }
            else -> fields.forEach { (id, _) -> view.addView(fieldView(id)) }
        }
        // Extra fields remain visible even if omitted from a partial template.
        fields.forEach { (id, _) -> if (id !in used) view.addView(fieldView(id)) }
        view.addView(keyboard)
    }
    override fun answer(): MathAnswer = MathAnswer.Fields(fields.filterValues { it.editable }.mapValues { it.value.read() })
    override fun restore(answer: MathAnswer) {
        if (answer is MathAnswer.Fields) answer.values.forEach { (id, value) -> fields[id]?.write?.invoke(value) }
    }
    override fun setEditable(enabled: Boolean) {
        fun setTree(view: View) {
            view.isEnabled = enabled
            if (view is ViewGroup) for (index in 0 until view.childCount) setTree(view.getChildAt(index))
        }
        fields.values.filter { it.editable }.forEach { setTree(it.view) }
        if (!enabled) keyboard.hide()
    }
}

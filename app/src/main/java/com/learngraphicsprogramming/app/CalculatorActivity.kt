package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.graphics.Color
import android.os.Bundle
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.LinearLayout
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import com.learngraphicsprogramming.app.calculator.CalculatorAngleMode
import com.learngraphicsprogramming.app.calculator.CalculatorEditorAction
import com.learngraphicsprogramming.app.calculator.CalculatorKeyOrder
import com.learngraphicsprogramming.app.calculator.CalculatorResult
import com.learngraphicsprogramming.app.calculator.MathCalculator
import com.learngraphicsprogramming.app.databinding.ActivityCalculatorBinding
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.learngraphicsprogramming.app.math.MathExpressionView
import com.learngraphicsprogramming.app.math.MathKeyboardAction
import com.learngraphicsprogramming.app.math.MathLatexParser
import com.learngraphicsprogramming.app.math.MathWidgetUi
import com.google.android.material.button.MaterialButton
import org.json.JSONObject
import org.json.JSONArray
import androidx.core.graphics.ColorUtils
import java.util.concurrent.Executors
import java.util.concurrent.Future

/** A calculator surface using the shared AST editor and a separate result renderer. */
class CalculatorActivity : AppCompatActivity() {
    private lateinit var binding: ActivityCalculatorBinding
    private lateinit var editor: MathExpressionEditor
    private lateinit var resultView: MathExpressionView
    private val calculator = MathCalculator()
    private val worker = Executors.newSingleThreadExecutor()
    private var evaluation: Future<*>? = null
    private var revision = 0L
    private var request = 0L
    private var angleMode = CalculatorAngleMode.RAD
    private var advancedMode = false
    private var editingKeys = false
    private var selectedKey: String? = null
    private lateinit var keys: Map<String, Key>
    private lateinit var basicOrder: CalculatorKeyOrder
    private lateinit var advancedOrder: CalculatorKeyOrder
    private var basicScroll = 0
    private var advancedScroll = 0
    private var reopenSystemKeyboard = false
    private var observedExpression = MathExpression.Sequence()
    private var resultLatex: String? = null
    private var resultInput: String? = null
    private var resultMode: CalculatorAngleMode? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        reopenSystemKeyboard = savedInstanceState?.getBoolean(STATE_IME_VISIBLE) == true
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE or
            if (reopenSystemKeyboard) WindowManager.LayoutParams.SOFT_INPUT_STATE_UNCHANGED
            else WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN)
        binding = ActivityCalculatorBinding.inflate(layoutInflater)
        setContentView(binding.root)
        window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        installInsets()
        editor = MathExpressionEditor(this).apply {
            id = R.id.calculator_input
            showSystemKeyboardOnTouch = false
            minimumHeight = dp(44)
            textSizePx = sp(18)
            setPadding(dp(8), dp(4), dp(8), dp(4))
            background = MathWidgetUi.background(this@CalculatorActivity)
            // The ordinary calculator log is base ten; its base is not an empty editor slot.
            configureExpected("\\log(1)")
        }
        resultView = MathExpressionView(this).apply {
            id = R.id.calculator_result
            textSizePx = sp(18)
            textColor = MathWidgetUi.foreground
            minimumHeight = dp(32)
            setPadding(dp(8), dp(4), dp(8), dp(4))
            background = MathWidgetUi.background(this@CalculatorActivity)
        }
        binding.calculatorInputHost.addView(editor, ViewGroup.LayoutParams(-2, -2))
        binding.calculatorResultHost.addView(resultView, ViewGroup.LayoutParams(-2, -2))
        initializeKeypad()
        arrangePanels()

        binding.calculatorPasteResult.visibility =
            if (
                intent.getBooleanExtra(
                    EXTRA_RETURN_RESULT,
                    false
                )
            ) {
                View.VISIBLE
            } else {
                View.GONE
            }

        binding.calculatorPasteResult.setOnClickListener {
            returnResultToLesson()
        }

        restoreCalculator(savedInstanceState)
        updatePasteResultButton()
        binding.calculatorAdvancedMode.setOnCheckedChangeListener { _, checked ->
            if (advancedMode != checked) {
                rememberKeypadScroll()
                advancedMode = checked
                selectedKey = null
                updateAdvancedMode()
            }
        }
        binding.calculatorEditKeys.setOnClickListener {
            editingKeys = !editingKeys
            selectedKey = null
            updateEditingControls()
        }
        binding.calculatorEditScrollUp.setOnClickListener { scrollKeys(forward = false) }
        binding.calculatorEditScrollDown.setOnClickListener { scrollKeys(forward = true) }

        observedExpression = editor.model.expression
        editor.onExpressionChanged = {
            val expression = editor.model.expression
            if (observedExpression != expression) {
                observedExpression = expression
                invalidateResult()
            }
        }
        editor.onImeAction = { action ->
            if (action == EditorInfo.IME_ACTION_DONE) { evaluate(); true } else false
        }
        binding.calculatorEvaluate.setOnClickListener { dispatch(CalculatorEditorAction.Evaluate) }
        binding.calculatorBack.setOnClickListener { finish() }
        binding.calculatorAngleMode.setOnClickListener {
            angleMode = if (angleMode == CalculatorAngleMode.RAD) CalculatorAngleMode.DEG else CalculatorAngleMode.RAD
            updateAngleMode()
            invalidateResult()
        }
        binding.calculatorNativeKeyboard.setOnClickListener {
            editor.showSystemKeyboard()
        }
        binding.calculatorImeLeft.contentDescription = MathKeyboardAction.MoveLeft.contentDescription
        binding.calculatorImeRight.contentDescription = MathKeyboardAction.MoveRight.contentDescription
        binding.calculatorImeLeft.setOnClickListener { edit(MathKeyboardAction.MoveLeft) }
        binding.calculatorImeRight.setOnClickListener { edit(MathKeyboardAction.MoveRight) }
    }

    private fun installInsets() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { view, insets ->
            val safe = insets.getInsets(WindowInsetsCompat.Type.systemBars() or
                WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.ime())
            // Match-parent content uses the available window. Apply safe insets once;
            // never cache display geometry across rotation, resizing, or IME changes.
            val imeVisible = insets.isVisible(WindowInsetsCompat.Type.ime())
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            binding.calculatorKeypad.visibility = if (imeVisible) View.GONE else View.VISIBLE
            binding.calculatorKeypadArea.visibility = if (imeVisible) View.GONE else View.VISIBLE
            binding.calculatorImeNavigation.visibility = if (imeVisible) View.VISIBLE else View.GONE
            insets
        }
        ViewCompat.requestApplyInsets(binding.root)
    }

    private fun arrangePanels() {
        val landscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        binding.calculatorPanels.orientation = if (landscape) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
        binding.calculatorExpressionPanel.layoutParams = LinearLayout.LayoutParams(
            if (landscape) 0 else -1, -2, if (landscape) 1f else 0f)
        binding.calculatorKeypadArea.layoutParams = LinearLayout.LayoutParams(
            if (landscape) 0 else -1, if (landscape) -1 else 0, if (landscape) 1.4f else 1f).apply {
            marginStart = if (landscape) dp(2) else 0
        }
    }

    private data class Key(val id: Int, val label: String, val action: CalculatorEditorAction)

    private fun initializeKeypad() {
        fun key(id: Int, label: String, action: MathKeyboardAction) =
            Key(id, label, CalculatorEditorAction.Edit(action))
        fun symbol(id: Int, label: String) = key(id, label, MathKeyboardAction.Symbol(label))
        val functions = listOf(
            key(R.id.calculator_key_sin, "sin", MathKeyboardAction.Function("sin")),
            key(R.id.calculator_key_cos, "cos", MathKeyboardAction.Function("cos")),
            key(R.id.calculator_key_tan, "tan", MathKeyboardAction.Function("tan")),
            key(R.id.calculator_key_log, "log", MathKeyboardAction.Function("log")),
            key(R.id.calculator_key_ln, "ln", MathKeyboardAction.Function("ln")),
            key(R.id.calculator_key_subscript, "xᵢ", MathKeyboardAction.Subscript)
        )
        val structures = listOf(
            key(R.id.calculator_key_parentheses, "( )", MathKeyboardAction.Parentheses),
            key(R.id.calculator_key_power, "xʸ", MathKeyboardAction.Power),
            key(R.id.calculator_key_root, "√", MathKeyboardAction.Root),
            key(R.id.calculator_key_fraction, "a/b", MathKeyboardAction.Fraction),
            symbol(R.id.calculator_key_pi, "π"), symbol(R.id.calculator_key_e, "e")
        )
        val numbers = listOf(
            symbol(R.id.calculator_key_7, "7"), symbol(R.id.calculator_key_8, "8"), symbol(R.id.calculator_key_9, "9"),
            symbol(R.id.calculator_key_divide, "÷"), symbol(R.id.calculator_key_percent, "%"),
            symbol(R.id.calculator_key_4, "4"), symbol(R.id.calculator_key_5, "5"), symbol(R.id.calculator_key_6, "6"),
            symbol(R.id.calculator_key_multiply, "×"), Key(R.id.calculator_clear, "C", CalculatorEditorAction.Clear),
            symbol(R.id.calculator_key_1, "1"), symbol(R.id.calculator_key_2, "2"), symbol(R.id.calculator_key_3, "3"),
            symbol(R.id.calculator_key_subtract, "−"), key(R.id.calculator_key_backspace, "⌫", MathKeyboardAction.Backspace),
            symbol(R.id.calculator_key_0, "0"), symbol(R.id.calculator_key_decimal, "."),
            key(R.id.calculator_key_left, "←", MathKeyboardAction.MoveLeft),
            key(R.id.calculator_key_right, "→", MathKeyboardAction.MoveRight), symbol(R.id.calculator_key_add, "+")
        )
        val advanced = listOf(
            key(R.id.calculator_key_asin, "sin⁻¹", MathKeyboardAction.Function("arcsin")),
            key(R.id.calculator_key_acos, "cos⁻¹", MathKeyboardAction.Function("arccos")),
            key(R.id.calculator_key_atan, "tan⁻¹", MathKeyboardAction.Function("arctan")),
            key(R.id.calculator_key_nth_root, "ⁿ√", MathKeyboardAction.NthRoot),
            key(R.id.calculator_key_absolute, "|x|", MathKeyboardAction.AbsoluteValue),
            symbol(R.id.calculator_key_factorial, "!"),
            symbol(R.id.calculator_key_combinations, "nCr"),
            symbol(R.id.calculator_key_permutations, "nPr")
        )
        keys = (functions + structures + numbers + advanced).associateBy { resources.getResourceEntryName(it.id) }
        fun slots(row: List<Key>): List<String?> = row.map { resources.getResourceEntryName(it.id) } +
            List(CalculatorKeyOrder.COLUMNS - row.size) { null }
        val basicSlots = (listOf(functions, structures) + numbers.chunked(5)).flatMap(::slots)
        basicOrder = CalculatorKeyOrder(basicSlots)
        advancedOrder = CalculatorKeyOrder(basicSlots + advanced.chunked(CalculatorKeyOrder.COLUMNS).flatMap(::slots))
    }

    private fun currentKeyOrder() = if (advancedMode) advancedOrder else basicOrder

    private fun renderKeypad() {
        val compact = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val keyHeight = dp(if (compact) 36 else 52)
        binding.calculatorKeypad.removeAllViews()
        for (rowKeys in currentKeyOrder().slots.chunked(CalculatorKeyOrder.COLUMNS)) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            for (name in rowKeys) {
                val key = name?.let(keys::get)
                val cell = if (key == null) View(this).apply { importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO }
                else MaterialButton(this).apply {
                    id = key.id
                    tag = name
                    text = key.label
                    isAllCaps = false
                    textSize = if (compact) 15f else 17f
                    minimumWidth = 0
                    minimumHeight = keyHeight
                    insetTop /= 4
                    insetBottom /= 4
                    setPadding(0, 0, 0, 0)
                    setOnClickListener { pressKey(requireNotNull(name), key) }
                }
                row.addView(cell, LinearLayout.LayoutParams(0, keyHeight, 1f).apply {
                    // One shared gap replaces the previous margins on both sides.
                    marginEnd = dp(1)
                    bottomMargin = dp(1)
                })
            }
            binding.calculatorKeypad.addView(row, LinearLayout.LayoutParams(-1, -2))
        }
        updateEditingControls()
        val position = if (advancedMode) advancedScroll else basicScroll
        binding.calculatorKeypadScroll.post { binding.calculatorKeypadScroll.scrollTo(0, position) }
    }

    private fun pressKey(name: String, key: Key) {
        if (!editingKeys) {
            dispatch(key.action)
            return
        }
        when (val first = selectedKey) {
            null -> selectedKey = name
            name -> selectedKey = null
            else -> {
                rememberKeypadScroll()
                currentKeyOrder().swap(first, name)
                selectedKey = null
                persistKeyOrders()
                renderKeypad()
            }
        }
        updateEditingControls()
    }

    private fun keyDescription(key: Key): String = when (key.id) {
        R.id.calculator_key_percent -> getString(R.string.calculator_percent_description)
        R.id.calculator_key_factorial -> getString(R.string.calculator_factorial_description)
        R.id.calculator_key_combinations -> getString(R.string.calculator_combinations_description)
        R.id.calculator_key_permutations -> getString(R.string.calculator_permutations_description)
        else -> when (val action = key.action) {
            is CalculatorEditorAction.Edit -> action.action.contentDescription
            CalculatorEditorAction.Clear -> getString(R.string.calculator_clear)
            CalculatorEditorAction.Evaluate -> getString(R.string.calculator_evaluate)
        }
    }

    private fun updateEditingControls() {
        binding.calculatorEditScrollControls.visibility = if (editingKeys) View.VISIBLE else View.GONE
        binding.calculatorEditKeys.isActivated = editingKeys
        binding.calculatorEditKeys.contentDescription = getString(if (editingKeys)
            R.string.calculator_finish_edit_keys_description else R.string.calculator_edit_keys_description)
        val accent = AppearanceSettingsRepository.getAccentColor(this)
        val selectedColor = Color.rgb(112, 117, 125)
        val onAccent = if (ColorUtils.calculateLuminance(accent) > .45) Color.BLACK else Color.WHITE
        val states = arrayOf(intArrayOf(android.R.attr.state_selected), intArrayOf(android.R.attr.state_enabled), intArrayOf())
        val backgrounds = ColorStateList(states, intArrayOf(selectedColor, accent, Color.rgb(72, 74, 82)))
        val foregrounds = ColorStateList(states, intArrayOf(Color.WHITE, onAccent, Color.rgb(170, 174, 183)))
        for ((name, key) in keys) {
            val button = binding.calculatorKeypad.findViewById<MaterialButton>(key.id) ?: continue
            button.isSelected = editingKeys && selectedKey == name
            button.backgroundTintList = backgrounds
            button.setTextColor(foregrounds)
            val description = keyDescription(key)
            button.contentDescription = if (button.isSelected) getString(R.string.calculator_selected_key_description, description) else description
        }
        binding.calculatorEditKeys.backgroundTintList = ColorStateList.valueOf(if (editingKeys) selectedColor else accent)
        binding.calculatorEditKeys.setTextColor(if (editingKeys) Color.WHITE else onAccent)
        binding.calculatorEditKeys.iconTint = binding.calculatorEditKeys.textColors
    }

    private fun rememberKeypadScroll() {
        if (advancedMode) advancedScroll = binding.calculatorKeypadScroll.scrollY
        else basicScroll = binding.calculatorKeypadScroll.scrollY
    }

    private fun scrollKeys(forward: Boolean) {
        val rowHeight = dp(if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) 37 else 53)
        binding.calculatorKeypadScroll.scrollBy(0, (if (forward) 1 else -1) * rowHeight * 2)
    }

    private fun encodedOrder(order: CalculatorKeyOrder) = JSONArray(order.slots.map { it ?: JSONObject.NULL }).toString()

    private fun restoreOrder(order: CalculatorKeyOrder, encoded: String?) {
        val slots = runCatching {
            require(encoded != null && encoded.length <= 32768)
            val array = JSONArray(encoded)
            require(array.length() <= 256)
            List(array.length()) { index -> if (array.isNull(index)) null else array.optString(index) }
        }.getOrNull()
        order.restore(slots)
    }

    private fun persistKeyOrders() {
        getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit()
            .putString(STATE_ORDER_BASIC, encodedOrder(basicOrder))
            .putString(STATE_ORDER_ADVANCED, encodedOrder(advancedOrder))
            .apply()
    }

    private fun updateAdvancedMode() {
        binding.calculatorAdvancedMode.isChecked = advancedMode
        binding.calculatorAdvancedMode.contentDescription = getString(
            if (advancedMode) R.string.calculator_advanced_mode_description else R.string.calculator_basic_mode_description)
        renderKeypad()
    }
    private fun dispatch(action: CalculatorEditorAction) = when (action) {
        is CalculatorEditorAction.Edit -> edit(action.action)
        CalculatorEditorAction.Clear -> editor.setLatex("")
        CalculatorEditorAction.Evaluate -> evaluate()
    }

    private fun edit(action: MathKeyboardAction) {
        editor.requestFocus()
        editor.perform(action)
        editor.lastError?.let { showMessage(it, error = true) }
    }

    private fun invalidateResult() {
        revision++
        request++
        evaluation?.cancel(true)
        resultLatex = null
        resultInput = null
        resultMode = null
        resultView.expression = MathExpression.Sequence()
        showResult(false)
        binding.calculatorEvaluate.isEnabled = true
        updatePasteResultButton()
        showMessage("")
    }

    private fun evaluate() {
        val input = editor.model.expression
        val inputLatex = editor.getLatex()
        val mode = angleMode
        val version = revision
        val token = ++request
        evaluation?.cancel(true)
        binding.calculatorEvaluate.isEnabled = false
        showResult(false)
        showMessage(getString(R.string.calculator_evaluating))
        evaluation = worker.submit {
            val result = runCatching { calculator.evaluate(input, mode) }
                .getOrElse { CalculatorResult.Failure(getString(R.string.calculator_unexpected_error)) }
            runOnUiThread {
                if (isDestroyed || isFinishing || revision != version || request != token) return@runOnUiThread
                binding.calculatorEvaluate.isEnabled = true
                when (result) {
                    is CalculatorResult.Success -> {
                        resultLatex = result.latex
                        resultInput = inputLatex
                        resultMode = mode
                        resultView.expression = result.expression
                        showResult(true)
                        updatePasteResultButton()
                        showMessage("")
                    }
                    is CalculatorResult.Failure -> {
                        resultLatex = null
                        resultInput = null
                        resultMode = null
                        updatePasteResultButton()
                        showMessage(result.message, error = true)
                    }
                }
            }
        }
    }

    private fun updatePasteResultButton() {

        binding.calculatorPasteResult.isEnabled =
            intent.getBooleanExtra(
                EXTRA_RETURN_RESULT,
                false
            ) &&
                    !resultLatex.isNullOrBlank()
    }

    private fun returnResultToLesson() {

        val result =
            resultLatex
                ?: return

        setResult(
            RESULT_OK,
            Intent().putExtra(
                EXTRA_RESULT_LATEX,
                result
            )
        )

        finish()
    }

    private fun showMessage(message: String, error: Boolean = false) {
        binding.calculatorError.text = message
        binding.calculatorError.setTextColor(if (error) Color.rgb(255, 166, 166) else MathWidgetUi.muted)
        binding.calculatorError.visibility = if (message.isBlank()) View.GONE else View.VISIBLE
    }

    private fun updateAngleMode() {
        binding.calculatorAngleMode.text = getString(if (angleMode == CalculatorAngleMode.RAD) R.string.calculator_rad else R.string.calculator_deg)
        val description = getString(if (angleMode == CalculatorAngleMode.RAD) R.string.calculator_rad_description else R.string.calculator_deg_description)
        binding.calculatorAngleMode.contentDescription = getString(R.string.calculator_angle_description, description)
    }

    private fun showResult(visible: Boolean) {
        val visibility = if (visible) View.VISIBLE else View.GONE
        binding.calculatorResultHost.visibility = visibility
        binding.calculatorResultRow.visibility = visibility
    }

    private fun restoreCalculator(state: Bundle?) {
        val prefs = getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        fun saved(key: String): String? = if (state != null) state.getString(key) else prefs.getString(key, null)
        val draft = saved(STATE_DRAFT)
        runCatching { if (draft != null) editor.restoreDraft(JSONObject(draft)) }
        val mode = saved(STATE_MODE)
        angleMode = runCatching { CalculatorAngleMode.valueOf(mode.orEmpty()) }.getOrDefault(CalculatorAngleMode.RAD)
        advancedMode = if (state != null) state.getBoolean(STATE_ADVANCED) else prefs.getBoolean(STATE_ADVANCED, false)
        restoreOrder(basicOrder, saved(STATE_ORDER_BASIC))
        restoreOrder(advancedOrder, saved(STATE_ORDER_ADVANCED))
        editingKeys = state?.getBoolean(STATE_EDITING) == true
        selectedKey = state?.getString(STATE_SELECTED_KEY)?.takeIf { editingKeys && it in currentKeyOrder().slots }
        basicScroll = state?.getInt(STATE_SCROLL_BASIC)?.coerceAtLeast(0) ?: 0
        advancedScroll = state?.getInt(STATE_SCROLL_ADVANCED)?.coerceAtLeast(0) ?: 0
        updateAngleMode()
        updateAdvancedMode()
        val savedResult = saved(STATE_RESULT)
        val savedInput = saved(STATE_RESULT_INPUT)
        val savedMode = saved(STATE_RESULT_MODE)
        val parsed = savedResult?.let { MathLatexParser.parse(it) }
        if (savedInput == editor.getLatex() && savedMode == angleMode.name && parsed is MathLatexParser.Result.Success) {
            resultLatex = savedResult
            resultInput = savedInput
            resultMode = angleMode
            resultView.expression = parsed.expression
            showResult(true)
            showMessage("")
        }
    }

    override fun onResume() {
        super.onResume()
        AppearanceUi.apply(this)
        // The legacy appearance walker treats SwitchCompat as Button. Keep the
        // calculator's switch track/thumb distinct without changing other screens.
        binding.calculatorAdvancedMode.backgroundTintList = null
        val accent = AppearanceSettingsRepository.getAccentColor(this)
        val states = arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf())
        binding.calculatorAdvancedMode.thumbTintList = ColorStateList(states,
            intArrayOf(accent, Color.rgb(145, 148, 158)))
        binding.calculatorAdvancedMode.trackTintList = ColorStateList(states,
            intArrayOf((accent and 0x00ffffff) or (135 shl 24), Color.argb(90, 120, 124, 134)))
        updateEditingControls()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && reopenSystemKeyboard && ::editor.isInitialized) {
            reopenSystemKeyboard = false
            editor.post { if (!isDestroyed && hasWindowFocus()) editor.showSystemKeyboard() }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        rememberKeypadScroll()
        outState.putString(STATE_DRAFT, editor.saveDraft().toString())
        outState.putString(STATE_MODE, angleMode.name)
        outState.putBoolean(STATE_ADVANCED, advancedMode)
        outState.putString(STATE_ORDER_BASIC, encodedOrder(basicOrder))
        outState.putString(STATE_ORDER_ADVANCED, encodedOrder(advancedOrder))
        outState.putBoolean(STATE_EDITING, editingKeys)
        outState.putString(STATE_SELECTED_KEY, selectedKey)
        outState.putInt(STATE_SCROLL_BASIC, basicScroll)
        outState.putInt(STATE_SCROLL_ADVANCED, advancedScroll)
        outState.putBoolean(STATE_IME_VISIBLE, reopenSystemKeyboard ||
            ViewCompat.getRootWindowInsets(binding.root)?.isVisible(WindowInsetsCompat.Type.ime()) == true)
        outState.putString(STATE_RESULT, resultLatex)
        outState.putString(STATE_RESULT_INPUT, resultInput)
        outState.putString(STATE_RESULT_MODE, resultMode?.name)
    }

    override fun onStop() {
        getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit()
            .putString(STATE_DRAFT, editor.saveDraft().toString())
            .putString(STATE_MODE, angleMode.name)
            .putBoolean(STATE_ADVANCED, advancedMode)
            .putString(STATE_ORDER_BASIC, encodedOrder(basicOrder))
            .putString(STATE_ORDER_ADVANCED, encodedOrder(advancedOrder))
            .putString(STATE_RESULT, resultLatex)
            .putString(STATE_RESULT_INPUT, resultInput)
            .putString(STATE_RESULT_MODE, resultMode?.name)
            .apply()
        super.onStop()
    }

    override fun onDestroy() {
        request++
        evaluation?.cancel(true)
        worker.shutdownNow()
        super.onDestroy()
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    private fun sp(value: Int) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, value.toFloat(), resources.displayMetrics)

    companion object {
        const val EXTRA_RETURN_RESULT =
            "calculator_return_result"

        const val EXTRA_RESULT_LATEX =
            "calculator_result_latex"

        private const val PREFERENCES =
            "scientific_calculator"
        private const val STATE_DRAFT = "calculator_draft"
        private const val STATE_MODE = "calculator_mode"
        private const val STATE_ADVANCED = "calculator_advanced"
        private const val STATE_ORDER_BASIC = "calculator_order_basic"
        private const val STATE_ORDER_ADVANCED = "calculator_order_advanced"
        private const val STATE_EDITING = "calculator_editing_keys"
        private const val STATE_SELECTED_KEY = "calculator_selected_key"
        private const val STATE_SCROLL_BASIC = "calculator_scroll_basic"
        private const val STATE_SCROLL_ADVANCED = "calculator_scroll_advanced"
        private const val STATE_IME_VISIBLE = "calculator_ime_visible"
        private const val STATE_RESULT = "calculator_result"
        private const val STATE_RESULT_INPUT = "calculator_result_input"
        private const val STATE_RESULT_MODE = "calculator_result_mode"
    }
}

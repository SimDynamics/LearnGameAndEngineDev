package com.learngraphicsprogramming.app

import android.content.Context
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.FrameLayout
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import com.learngraphicsprogramming.app.calculator.CalculatorAngleMode
import com.learngraphicsprogramming.app.calculator.CalculatorEditorAction
import com.learngraphicsprogramming.app.calculator.CalculatorResult
import com.learngraphicsprogramming.app.calculator.MathCalculator
import com.learngraphicsprogramming.app.databinding.ActivityCalculatorBinding
import com.learngraphicsprogramming.app.math.MathExpression
import com.learngraphicsprogramming.app.math.MathExpressionEditor
import com.learngraphicsprogramming.app.math.MathExpressionView
import com.learngraphicsprogramming.app.math.MathKeyboardAction
import com.learngraphicsprogramming.app.math.MathLatexParser
import com.learngraphicsprogramming.app.math.MathWidgetUi
import org.json.JSONObject
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
    private var observedExpression = MathExpression.Sequence()
    private var resultLatex: String? = null
    private var resultInput: String? = null
    private var resultMode: CalculatorAngleMode? = null
    private var panelWidth = 0
    private var panelHeight = 0
    private var screenHeight = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityCalculatorBinding.inflate(layoutInflater)
        setContentView(binding.root)
        sizeFloatingWindow()
        installInsets()
        editor = MathExpressionEditor(this).apply {
            id = R.id.calculator_input
            minimumHeight = dp(72)
            textSizePx = sp(26)
            background = MathWidgetUi.background(this@CalculatorActivity)
            // The ordinary calculator log is base ten; its base is not an empty editor slot.
            configureExpected("\\log(1)")
        }
        resultView = MathExpressionView(this).apply {
            id = R.id.calculator_result
            textSizePx = sp(27)
            textColor = MathWidgetUi.foreground
            minimumHeight = dp(64)
            setPadding(dp(12), dp(10), dp(12), dp(10))
            background = MathWidgetUi.background(this@CalculatorActivity)
        }
        binding.calculatorInputHost.addView(editor, ViewGroup.LayoutParams(-2, -2))
        binding.calculatorResultHost.addView(resultView, ViewGroup.LayoutParams(-2, -2))
        buildKeypad()
        arrangePanels()
        restoreCalculator(savedInstanceState)
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
            editor.requestFocus()
            getSystemService(InputMethodManager::class.java).showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT)
        }
        binding.calculatorImeLeft.contentDescription = MathKeyboardAction.MoveLeft.contentDescription
        binding.calculatorImeRight.contentDescription = MathKeyboardAction.MoveRight.contentDescription
        binding.calculatorImeLeft.setOnClickListener { edit(MathKeyboardAction.MoveLeft) }
        binding.calculatorImeRight.setOnClickListener { edit(MathKeyboardAction.MoveRight) }
    }

    private fun sizeFloatingWindow() {
        val landscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val bounds = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) windowManager.maximumWindowMetrics.bounds else null
        val screenWidth = bounds?.width() ?: resources.displayMetrics.widthPixels
        screenHeight = bounds?.height() ?: resources.displayMetrics.heightPixels
        panelWidth = minOf((screenWidth * .95f).toInt(), dp(if (landscape) 900 else 600))
        panelHeight = (screenHeight * .9f).toInt()
        binding.calculatorScroll.background = GradientDrawable().apply {
            setColor(Color.rgb(16, 17, 20))
            cornerRadius = dp(16).toFloat()
        }
        window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        binding.calculatorScroll.layoutParams = FrameLayout.LayoutParams(panelWidth, panelHeight, Gravity.CENTER)
    }

    private fun installInsets() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { view, insets ->
            val safe = insets.getInsets(WindowInsetsCompat.Type.systemBars() or
                WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.ime())
            // A translucent activity supplies reliable IME insets, with a separate centered
            // calculator panel above the remaining visible area of the lesson.
            val height = minOf(panelHeight, (screenHeight - safe.top - safe.bottom - dp(24)).coerceAtLeast(dp(48)))
            if (binding.calculatorScroll.layoutParams.height != height) {
                binding.calculatorScroll.layoutParams = FrameLayout.LayoutParams(panelWidth, height, Gravity.CENTER)
            }
            val imeVisible = insets.isVisible(WindowInsetsCompat.Type.ime())
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            binding.calculatorKeypad.visibility = if (imeVisible) View.GONE else View.VISIBLE
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
        binding.calculatorKeypad.layoutParams = LinearLayout.LayoutParams(
            if (landscape) 0 else -1, -2, if (landscape) 1.1f else 0f).apply {
            topMargin = if (landscape) 0 else dp(12)
            marginStart = if (landscape) dp(16) else 0
        }
    }

    private data class Key(val id: Int, val label: String, val action: CalculatorEditorAction)

    private fun buildKeypad() {
        val compact = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val keyHeight = if (compact) 36 else 52
        fun key(id: Int, label: String, action: MathKeyboardAction) =
            Key(id, label, CalculatorEditorAction.Edit(action))
        fun symbol(id: Int, label: String) = key(id, label, MathKeyboardAction.Symbol(label))
        val keys = listOf(
            key(R.id.calculator_key_sin, "sin", MathKeyboardAction.Function("sin")),
            key(R.id.calculator_key_cos, "cos", MathKeyboardAction.Function("cos")),
            key(R.id.calculator_key_tan, "tan", MathKeyboardAction.Function("tan")),
            key(R.id.calculator_key_log, "log", MathKeyboardAction.Function("log")),
            key(R.id.calculator_key_ln, "ln", MathKeyboardAction.Function("ln")),
            key(R.id.calculator_key_parentheses, "( )", MathKeyboardAction.Parentheses),
            key(R.id.calculator_key_power, "xʸ", MathKeyboardAction.Power),
            key(R.id.calculator_key_root, "√", MathKeyboardAction.Root),
            key(R.id.calculator_key_fraction, "a/b", MathKeyboardAction.Fraction),
            symbol(R.id.calculator_key_pi, "π"),
            symbol(R.id.calculator_key_7, "7"), symbol(R.id.calculator_key_8, "8"), symbol(R.id.calculator_key_9, "9"),
            symbol(R.id.calculator_key_divide, "÷"), symbol(R.id.calculator_key_e, "e"),
            symbol(R.id.calculator_key_4, "4"), symbol(R.id.calculator_key_5, "5"), symbol(R.id.calculator_key_6, "6"),
            symbol(R.id.calculator_key_multiply, "×"), Key(R.id.calculator_clear, "C", CalculatorEditorAction.Clear),
            symbol(R.id.calculator_key_1, "1"), symbol(R.id.calculator_key_2, "2"), symbol(R.id.calculator_key_3, "3"),
            symbol(R.id.calculator_key_subtract, "−"), key(R.id.calculator_key_backspace, "⌫", MathKeyboardAction.Backspace),
            symbol(R.id.calculator_key_0, "0"), symbol(R.id.calculator_key_decimal, "."),
            key(R.id.calculator_key_left, "←", MathKeyboardAction.MoveLeft),
            key(R.id.calculator_key_right, "→", MathKeyboardAction.MoveRight), symbol(R.id.calculator_key_add, "+")
        )
        keys.chunked(5).forEach { rowKeys ->
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            rowKeys.forEach { key ->
                row.addView(Button(this).apply {
                    id = key.id
                    text = key.label
                    isAllCaps = false
                    textSize = if (compact) 15f else 17f
                    minimumWidth = 0
                    minimumHeight = dp(keyHeight)
                    setPadding(0, 0, 0, 0)
                    contentDescription = when (val action = key.action) {
                        is CalculatorEditorAction.Edit -> action.action.contentDescription
                        CalculatorEditorAction.Clear -> getString(R.string.calculator_clear)
                        CalculatorEditorAction.Evaluate -> getString(R.string.calculator_evaluate)
                    }
                    setOnClickListener { dispatch(key.action) }
                }, LinearLayout.LayoutParams(0, dp(keyHeight), 1f).apply { setMargins(dp(2), dp(2), dp(2), dp(2)) })
            }
            binding.calculatorKeypad.addView(row, LinearLayout.LayoutParams(-1, -2))
        }
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
        binding.calculatorResultHost.visibility = View.GONE
        binding.calculatorEvaluate.isEnabled = true
        showMessage(getString(R.string.calculator_empty_result))
    }

    private fun evaluate() {
        val input = editor.model.expression
        val inputLatex = editor.getLatex()
        val mode = angleMode
        val version = revision
        val token = ++request
        evaluation?.cancel(true)
        binding.calculatorEvaluate.isEnabled = false
        binding.calculatorResultHost.visibility = View.GONE
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
                        binding.calculatorResultHost.visibility = View.VISIBLE
                        showMessage("")
                    }
                    is CalculatorResult.Failure -> {
                        resultLatex = null
                        resultInput = null
                        resultMode = null
                        showMessage(result.message, error = true)
                    }
                }
            }
        }
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

    private fun restoreCalculator(state: Bundle?) {
        val prefs = getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        fun saved(key: String): String? = if (state != null) state.getString(key) else prefs.getString(key, null)
        val draft = saved(STATE_DRAFT)
        runCatching { if (draft != null) editor.restoreDraft(JSONObject(draft)) }
        val mode = saved(STATE_MODE)
        angleMode = runCatching { CalculatorAngleMode.valueOf(mode.orEmpty()) }.getOrDefault(CalculatorAngleMode.RAD)
        updateAngleMode()
        val savedResult = saved(STATE_RESULT)
        val savedInput = saved(STATE_RESULT_INPUT)
        val savedMode = saved(STATE_RESULT_MODE)
        val parsed = savedResult?.let { MathLatexParser.parse(it) }
        if (savedInput == editor.getLatex() && savedMode == angleMode.name && parsed is MathLatexParser.Result.Success) {
            resultLatex = savedResult
            resultInput = savedInput
            resultMode = angleMode
            resultView.expression = parsed.expression
            binding.calculatorResultHost.visibility = View.VISIBLE
            showMessage("")
        }
    }

    override fun onResume() {
        super.onResume()
        AppearanceUi.apply(this)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(STATE_DRAFT, editor.saveDraft().toString())
        outState.putString(STATE_MODE, angleMode.name)
        outState.putString(STATE_RESULT, resultLatex)
        outState.putString(STATE_RESULT_INPUT, resultInput)
        outState.putString(STATE_RESULT_MODE, resultMode?.name)
    }

    override fun onStop() {
        getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit()
            .putString(STATE_DRAFT, editor.saveDraft().toString())
            .putString(STATE_MODE, angleMode.name)
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
        private const val PREFERENCES = "scientific_calculator"
        private const val STATE_DRAFT = "calculator_draft"
        private const val STATE_MODE = "calculator_mode"
        private const val STATE_RESULT = "calculator_result"
        private const val STATE_RESULT_INPUT = "calculator_result_input"
        private const val STATE_RESULT_MODE = "calculator_result_mode"
    }
}

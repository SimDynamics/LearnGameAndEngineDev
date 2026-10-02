package com.learngraphicsprogramming.app.math

import org.json.JSONArray
import org.json.JSONObject
import org.json.JSONTokener
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/** Audits real lesson assets through the same shortcut contract used by editable controls. */
class MathShortcutContentTest {
    @Test fun everyCurrentMathPuzzleAndEditableLeafHasConsistentShortcuts() {
        var puzzleCount = 0
        var responseCount = 0
        var textInputCount = 0
        var visualInputCount = 0
        var piInputCount = 0
        val assets = assetDirectory()
        val mathFiles = assets.resolve("content/es/math_physics").walkTopDown()
            .filter { it.isFile && it.extension == "json" }.toList()
        assertTrue("The audit includes the existing course/unit/lesson metadata", mathFiles.size >= 41)
        mathFiles.forEach { file ->
            val content = JSONTokener(file.readText(Charsets.UTF_8).removePrefix("\uFEFF")).nextValue()
            puzzles(content).forEach { block ->
                val puzzle = MathPuzzleParser.parse(block)
                puzzleCount++
                puzzle.responses.forEach { response ->
                    responseCount++
                    try {
                        MathShortcutResolver.validateResponseConfiguration(response.config, response.validator, response.type)
                        when {
                            response.config.has("fields") -> {
                                val definitions = response.config.getJSONArray("fields")
                                for (index in 0 until definitions.length()) {
                                    val field = definitions.getJSONObject(index)
                                    if (field.optString("type", "numeric") in setOf("numeric", "expression")) {
                                        val validator = response.validator.getJSONObject("fields").getJSONObject(field.getString("id"))
                                        val mode = field.optString("keyboard", if (field.optString("type") == "expression") "ALGEBRA" else "NUMERIC")
                                        val symbols = MathShortcutResolver.resolve(validator, mode,
                                            field.optJSONArray("keyboardSymbols") ?: response.config.optJSONArray("keyboardSymbols"))
                                        assertNoLettersOrDigits(symbols)
                                        if ("π" in symbols) piInputCount++
                                        textInputCount++
                                    }
                                }
                            }
                            response.type in setOf("math_numeric_input", "math_expression_input") -> {
                                val symbols = MathShortcutResolver.resolve(response.validator,
                                    response.config.optString("keyboard", if (response.type == "math_expression_input") "ALGEBRA" else "NUMERIC"),
                                    response.config.optJSONArray("keyboardSymbols"))
                                assertNoLettersOrDigits(symbols)
                                if ("π" in symbols) piInputCount++
                                textInputCount++
                            }
                            response.type in setOf("math_coordinate_graph_builder", "math_geometry_builder") -> {
                                val points = response.config.getJSONArray("points")
                                for (index in 0 until points.length()) {
                                    if (points.getJSONObject(index).optBoolean("draggable", true)) visualInputCount += 2
                                }
                                for (axis in listOf("x", "y")) {
                                    assertVisualNumericSymbols(MathShortcutResolver.visualSymbols(response.config, response.validator, axis))
                                }
                            }
                            response.type == "math_number_line" -> {
                                visualInputCount++
                                assertVisualNumericSymbols(MathShortcutResolver.visualSymbols(response.config, response.validator))
                            }
                        }
                    } catch (failure: Exception) {
                        throw AssertionError("${file.relativeTo(assets)}: ${block.optString("id")}/${response.id}: ${failure.message}", failure)
                    }
                }
            }
        }
        // New valid lessons may increase this inventory without failing the generic audit.
        assertTrue("Audited math puzzle inventory", puzzleCount >= 47)
        assertTrue("Audited response controls", responseCount >= 51)
        assertTrue("Ordinary EditText inputs", textInputCount >= 44)
        assertTrue("Visual numeric EditText inputs", visualInputCount >= 31)
        assertTrue("All editable text inputs", textInputCount + visualInputCount >= 75)
        assertTrue("The existing pi product is included in the audit", piInputCount >= 1)
        println("Math shortcut content audit: ${mathFiles.size} JSON; $puzzleCount puzzles; $responseCount responses; " +
            "$textInputCount ordinary + $visualInputCount visual = ${textInputCount + visualInputCount} EditTexts; " +
            "$piInputCount pi context(s); zero invalid configurations.")
    }

    @Test fun piIsDerivedFromTheExpectedExpressionRegardlessOfBroadKeyboardMode() {
        for ((expected, answer) in listOf("4π" to "4π", "4pi" to "4π", "4pix" to "4πx", "pipi" to "ππ")) {
            val validator = validator("equivalent_expression", expected)
            assertEquals(listOf("π"), MathShortcutResolver.resolve(validator, "ALGEBRA"))
            assertTrue(MathValidators.validate(validator, MathAnswer.Text(answer)).correct)
        }
    }

    @Test fun integerAnswerDoesNotOfferPiDigitsVariablesOrUnrelatedOperators() {
        for (mode in listOf("NUMERIC", "ALGEBRA", "TRIG", "INEQUALITY", "CALCULUS")) {
            val symbols = MathShortcutResolver.resolve(validator("numeric", "24"), mode)
            assertEquals(emptyList<String>(), symbols)
            assertNoLettersOrDigits(symbols)
            assertFalse("π" in symbols)
        }
    }

    @Test fun exactFractionOffersSlashWithoutUnneededSymbols() {
        val validator = validator("exact_fraction", "3/4")
        assertEquals(listOf("/"), MathShortcutResolver.resolve(validator, "FRACTION"))
        MathShortcutResolver.validateConfiguration(validator, "FRACTION", JSONArray().put("/"))
        assertTrue(MathValidators.validate(validator, MathAnswer.Text("3/4")).correct)
        assertEquals(setOf("−", "/"), MathShortcutResolver.resolve(validator("exact_fraction", "-3/4"), "FRACTION").toSet())
        val numericFraction = validator("numeric", "3/4")
        expectInvalid {
            MathShortcutResolver.validateConfiguration(numericFraction, "FRACTION", JSONArray().put("÷"))
        }
        // The legacy fraction action inserts a slash; a literal division glyph would fail Rational.parse.
        MathShortcutResolver.validateConfiguration(numericFraction, "FRACTION", JSONArray().put("a/b"))
        assertTrue(MathValidators.validate(numericFraction, MathAnswer.Text("3/4")).correct)
        assertFalse(MathValidators.validate(numericFraction, MathAnswer.Text("3÷4")).correct)
    }

    @Test fun numericApproximationUsesAnAcceptedDecimalWitness() {
        val validator = validator("tolerance", "1/3").put("tolerance", 0.005)
        val symbols = MathShortcutResolver.resolve(validator, "NUMERIC")
        assertTrue("." in symbols)
        assertFalse("/" in symbols)
        assertTrue(MathValidators.validate(validator, MathAnswer.Text("0.33")).correct)
        assertFalse(MathValidators.validate(validator, MathAnswer.Text("0.3")).correct)
        assertEquals(listOf("."), MathShortcutResolver.resolve(validator("numeric", "7.5"), "NUMERIC"))
    }

    @Test fun equationContainsRelevantPunctuationAndLeavesVariablesToTheIme() {
        val validator = validator("equivalent_expression", "y = 3x + 2")
        val symbols = MathShortcutResolver.resolve(validator, "ALGEBRA")
        assertEquals(setOf("+", "="), symbols.toSet())
        assertNoLettersOrDigits(symbols)
        assertTrue(MathValidators.validate(validator, MathAnswer.Text("y=3x+2")).correct)
    }

    @Test fun literalInequalityOffersOnlyItsRelevantRelation() {
        for (relation in listOf("<", ">", "≤", "≥", "=", "≠")) {
            val validator = validator("exact_text", "x${relation}3")
            val symbols = MathShortcutResolver.resolve(validator, "INEQUALITY")
            assertEquals(listOf(relation), symbols)
            assertNoLettersOrDigits(symbols)
            assertTrue(MathValidators.validate(validator, MathAnswer.Text("x${relation}3")).correct)
        }
    }

    @Test fun missingIndispensablePiOrFractionShortcutIsRejected() {
        expectInvalid {
            MathShortcutResolver.validateConfiguration(validator("equivalent_expression", "4π"), "ALGEBRA", JSONArray())
        }
        expectInvalid {
            MathShortcutResolver.validateConfiguration(validator("exact_fraction", "3/4"), "FRACTION", JSONArray())
        }
    }

    @Test fun overridesRejectUnnecessarySymbolsVariablesDigitsAndInvalidValues() {
        val integer = validator("numeric", "24")
        for (invalid in listOf("π", "%", "≤", "√", "+", "x", "y", "a", "0", "9", "12", "unknown")) {
            expectInvalid { MathShortcutResolver.validateConfiguration(integer, "NUMERIC", JSONArray().put(invalid)) }
        }
        expectInvalid { MathShortcutResolver.validateConfiguration(integer, "NUMERIC", JSONArray().put(4)) }
        expectInvalid { MathShortcutResolver.validateConfiguration(integer, "NUMERIC", JSONArray().put(JSONObject.NULL)) }
        val malformed = JSONObject().put("id", "result").put("type", "math_numeric_input")
            .put("keyboardSymbols", "π").put("validator", validator("equivalent_expression", "4π"))
        expectInvalid { MathShortcutResolver.validateResponseConfiguration(malformed, malformed.getJSONObject("validator"), "math_numeric_input") }
    }

    @Test fun compoundResponsesUseEachFieldValidatorAndItsOverride() {
        val validators = JSONObject().put("type", "fields").put("fields", JSONObject()
            .put("whole", validator("numeric", "6"))
            .put("fraction", validator("exact_fraction", "3/4"))
            .put("product", validator("equivalent_expression", "4π"))
            .put("comparison", validator("text", "less")))
        val response = JSONObject().put("id", "combined").put("type", "math_structured_answer")
            .put("keyboardSymbols", JSONArray())
            .put("fields", JSONArray()
                .put(JSONObject().put("id", "whole").put("type", "numeric"))
                .put(JSONObject().put("id", "fraction").put("type", "numeric").put("keyboard", "FRACTION").put("keyboardSymbols", JSONArray().put("/")))
                .put(JSONObject().put("id", "product").put("type", "expression").put("keyboardSymbols", JSONArray().put("π")))
                .put(JSONObject().put("id", "comparison").put("type", "dropdown").put("options", JSONArray()
                    .put(JSONObject().put("id", "less").put("label", "<")))))
            .put("validator", validators)
        MathShortcutResolver.validateResponseConfiguration(response, validators, "math_structured_answer")
        val product = response.getJSONArray("fields").getJSONObject(2)
        product.put("keyboardSymbols", JSONArray())
        expectInvalid { MathShortcutResolver.validateResponseConfiguration(response, validators, "math_structured_answer") }
    }

    @Test fun visualEditorSymbolsRespectAxisBoundsAndDecimalSnap() {
        val config = JSONObject().put("mode", "line").put("snap", 1)
            .put("bounds", JSONObject().put("xMin", 0).put("xMax", 5).put("yMin", -5).put("yMax", 5))
            .put("points", JSONArray().put(JSONObject().put("x", 0).put("y", 0)).put(JSONObject().put("x", 1).put("y", 1)))
        val validator = JSONObject().put("type", "coordinates").put("mode", "line")
            .put("expected", JSONArray().put(JSONObject().put("x", 0).put("y", 1)).put(JSONObject().put("x", 2).put("y", 5)))
        val x = MathShortcutResolver.visualSymbols(config, validator, "x")
        val y = MathShortcutResolver.visualSymbols(config, validator, "y")
        assertFalse("x cannot move into negative coordinates", "−" in x || "-" in x)
        assertTrue("y can express alternative negative points on the line", "−" in y || "-" in y)
        assertFalse("Integer snap requires no decimal separator", "." in x)
        assertFalse("Integer snap requires no decimal separator", "." in y)
        config.put("snap", 0.25)
        val fractional = MathShortcutResolver.visualSymbols(config, validator, "x")
        assertTrue("Fractional visual coordinates use decimal syntax", "." in fractional)
        assertVisualNumericSymbols(fractional)
    }

    private fun assertVisualNumericSymbols(symbols: List<String>) {
        assertNoLettersOrDigits(symbols)
        assertTrue("Double editor only accepts sign/decimal syntax: $symbols", symbols.all { it in setOf("−", "-", ".", ",") })
        assertFalse("/" in symbols)
        assertFalse("π" in symbols)
        assertFalse("%" in symbols)
    }

    private fun assertNoLettersOrDigits(symbols: List<String>) {
        assertTrue("Digits and variables belong to Android IME: $symbols", symbols.all { symbol ->
            symbol.none { it in '0'..'9' || it in 'a'..'z' || it in 'A'..'Z' }
        })
    }

    private fun validator(type: String, expected: Any) = JSONObject().put("type", type).put("expected", expected)

    private fun expectInvalid(action: () -> Unit) {
        val thrown = runCatching(action).exceptionOrNull()
        assertTrue("Invalid shortcut configuration must throw IllegalArgumentException; got $thrown", thrown is IllegalArgumentException)
    }

    private fun assetDirectory(): File = listOf(File("src/main/assets"), File("app/src/main/assets"))
        .firstOrNull { it.isDirectory } ?: error("Could not locate checked-in lesson assets")

    private fun puzzles(value: Any): Sequence<JSONObject> = sequence {
        when (value) {
            is JSONObject -> {
                if (value.optString("type") == "math_puzzle") yield(value)
                value.keys().forEach { key ->
                    val child = value.get(key)
                    if (child is JSONObject || child is JSONArray) yieldAll(puzzles(child))
                }
            }
            is JSONArray -> for (index in 0 until value.length()) {
                val child = value.get(index)
                if (child is JSONObject || child is JSONArray) yieldAll(puzzles(child))
            }
        }
    }
}

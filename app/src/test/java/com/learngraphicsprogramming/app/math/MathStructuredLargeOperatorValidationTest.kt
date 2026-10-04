package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

class MathStructuredLargeOperatorValidationTest {

    private fun sequence(
        vararg children: MathExpression
    ) =
        MathExpression.Sequence(
            children.toList()
        )

    private fun parsed(
        source: String
    ) =
        (
                MathLatexParser.parse(source)
                        as MathLatexParser.Result.Success
                ).expression

    @Test
    fun sandboxIntegralBuiltWithIndependentIntegrandIsAccepted() {

        val relative =
            "src/main/assets/content/es/math_physics/" +
                    "math_course_01/arithmetic_fundamentals/" +
                    "lesson_math_puzzle_sandbox.json"

        val lesson =
            JSONObject(
                sequenceOf(
                    File(relative),
                    File("app/$relative")
                )
                    .first { it.isFile }
                    .readText(Charsets.UTF_8)
            )

        val pages =
            lesson.getJSONArray("pages")

        val block =
            (0 until pages.length())
                .asSequence()
                .flatMap { page ->

                    val blocks =
                        pages
                            .getJSONObject(page)
                            .getJSONArray("blocks")

                    (0 until blocks.length())
                        .asSequence()
                        .map(blocks::getJSONObject)
                }
                .first {
                    it.optString("id") ==
                            "structured_editor_advanced_qa"
                }

        val field =
            MathPuzzleParser
                .parse(block)
                .responses
                .single()
                .validator
                .getJSONObject("fields")
                .getJSONObject("integral")

        val expected =
            field.getString("expected")

        /*
         * Nueva arquitectura:
         *
         * Sequence
         * ├── Integral
         * │   ├── upper = 1
         * │   └── lower = 0
         * │
         * ├── x²
         * ├── d
         * └── x
         *
         * x² ya NO pertenece al LargeOperator.
         */
        val actual =
            MathLatexSerializer.serialize(
                sequence(
                    MathExpression.LargeOperator(
                        kind =
                            MathLargeOperator.INTEGRAL,

                        lower =
                            parsed("0"),

                        upper =
                            parsed("1")
                    ),

                    *parsed("x^2")
                        .children
                        .toTypedArray(),

                    MathExpression.Variable("d"),
                    MathExpression.Variable("x")
                )
            )

        assertEquals(
            "\\int_{0}^{1}{x}^{2}dx",
            actual
        )

        val expectedCanonical =
            MathExpressionSemantics.canonical(
                expected
            )

        val actualCanonical =
            MathExpressionSemantics.canonical(
                actual
            )

        assertEquals(
            expectedCanonical,
            actualCanonical
        )

        assertTrue(
            """
            expected=$expected
            actual=$actual
            expectedCanonical=$expectedCanonical
            actualCanonical=$actualCanonical
            """.trimIndent(),

            MathValidators.validate(
                field,
                MathAnswer.Text(actual)
            ).correct
        )
    }
}
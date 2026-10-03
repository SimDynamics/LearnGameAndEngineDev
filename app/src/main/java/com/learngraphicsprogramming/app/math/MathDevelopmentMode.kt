package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.pm.ApplicationInfo

/**
 * Ayudas TEMPORALES para QA del sandbox matemático.
 *
 * Cambiar ENABLE_SANDBOX_AUTO_SOLVE a false devuelve
 * inmediatamente el comportamiento normal.
 */
internal object MathDevelopmentMode {

    const val ENABLE_SANDBOX_AUTO_SOLVE = true

    // Cantidad de páginas iniciales que no queremos
    // responder manualmente durante cada prueba.
    const val AUTO_SOLVE_PAGE_COUNT = 16

    // Si está activado, al abrir el sandbox saltamos
    // directamente a la primera página no automática:
    //
    // 16 páginas automáticas -> abrir página 17.
    const val OPEN_FIRST_MANUAL_PAGE = true

    private const val SANDBOX_LESSON_ID =
        "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"

    private fun enabledFor(
        context: Context,
        lessonId: String
    ): Boolean {

        val debugBuild =
            context.applicationInfo.flags and
                    ApplicationInfo.FLAG_DEBUGGABLE != 0

        return debugBuild &&
                ENABLE_SANDBOX_AUTO_SOLVE &&
                lessonId == SANDBOX_LESSON_ID
    }

    fun shouldAutoSolve(
        context: Context,
        lessonId: String,
        pageIndex: Int
    ): Boolean =
        enabledFor(
            context,
            lessonId
        ) &&
                pageIndex in
                0 until AUTO_SOLVE_PAGE_COUNT

    fun initialPage(
        context: Context,
        lessonId: String,
        savedPage: Int,
        totalPages: Int
    ): Int {

        if (
            !enabledFor(
                context,
                lessonId
            ) ||
            !OPEN_FIRST_MANUAL_PAGE
        ) {
            return savedPage
        }

        return AUTO_SOLVE_PAGE_COUNT
            .coerceAtMost(totalPages - 1)
            .coerceAtLeast(0)
    }

    fun answersFor(
        context: Context,
        lessonId: String,
        pageIndex: Int,
        puzzleId: String
    ): Map<String, MathAnswer>? {

        if (
            !shouldAutoSolve(
                context,
                lessonId,
                pageIndex
            )
        ) {
            return null
        }

        return answers[puzzleId]
    }

    private fun text(
        value: String
    ): MathAnswer =
        MathAnswer.Text(value)

    private fun fields(
        vararg values: Pair<String, String>
    ): MathAnswer =
        MathAnswer.Fields(
            mapOf(*values)
        )

    private fun graph(
        vararg coordinates: Pair<Int, Int>
    ): MathAnswer =
        MathAnswer.Graph(
            coordinates.map {
                MathPoint(
                    it.first.toDouble(),
                    it.second.toDouble()
                )
            }
        )

    private val answers:
            Map<String, Map<String, MathAnswer>> =
        mapOf(

            "numbers_and_fractions" to
                    mapOf(
                        "difference" to text("7,5"),
                        "fraction" to text("6/8"),
                        "approximation" to text("0.33")
                    ),

            "single_rich_option" to
                    mapOf(
                        "choice" to
                                MathAnswer.Choice(
                                    setOf("linear")
                                )
                    ),

            "multi_rich_option" to
                    mapOf(
                        "choices" to
                                MathAnswer.Choice(
                                    setOf(
                                        "fraction",
                                        "decimal",
                                        "ratio"
                                    )
                                )
                    ),

            "editable_table" to
                    mapOf(
                        "table" to
                                fields(
                                    "a" to "-1",
                                    "b" to "1",
                                    "c" to "5"
                                )
                    ),

            "equivalent_expression" to
                    mapOf(
                        "equation" to
                                text("2*y = 4 + 6*x")
                    ),

            "pi_expression" to
                    mapOf(
                        "product" to
                                text("2π + 2π")
                    ),

            "structured_comparison" to
                    mapOf(
                        "comparison" to
                                fields(
                                    "operator" to "lt",
                                    "right" to "1"
                                )
                    ),

            "equation_blanks" to
                    mapOf(
                        "blanks" to
                                fields(
                                    "added" to "4",
                                    "total" to "9"
                                )
                    ),

            "ordering_and_median" to
                    mapOf(
                        "order" to
                                MathAnswer.Order(
                                    listOf(
                                        "n3",
                                        "n0",
                                        "n2",
                                        "n5",
                                        "n8"
                                    )
                                ),

                        "median" to
                                text("2")
                    ),

            "matching_expressions" to
                    mapOf(
                        "pairs" to
                                MathAnswer.Matching(
                                    mapOf(
                                        "sum" to "five",
                                        "product" to "twelve",
                                        "difference" to "seven"
                                    )
                                )
                    ),

            "inequality_builder" to
                    mapOf(
                        "inequality" to
                                MathAnswer.NumberLine(
                                    2.0,
                                    true,
                                    "left"
                                )
                    ),

            "number_line_absolute" to
                    mapOf(
                        "distance" to
                                text("3")
                    ),

            "point_and_coordinate" to
                    mapOf(
                        "point" to
                                graph(
                                    2 to 3
                                ),

                        "x" to
                                text("2")
                    ),

            "line_builder" to
                    mapOf(
                        "line" to
                                graph(
                                    -1 to -1,
                                    1 to 3
                                )
                    ),

            "scatter_builder" to
                    mapOf(
                        "scatter" to
                                graph(
                                    2 to 2,
                                    -2 to 1,
                                    0 to 3
                                )
                    ),

            "polygon_builder" to
                    mapOf(
                        "triangle" to
                                graph(
                                    3 to 0,
                                    0 to 2,
                                    0 to 0
                                )
                    ),

            "parabola_builder" to
                    mapOf(
                        "parabola" to
                                graph(
                                    1 to -1,
                                    0 to 1
                                )
                    ),

            "rectangle_area" to
                    mapOf(
                        "rectangle" to
                                graph(
                                    0 to 0,
                                    4 to 0,
                                    4 to 2,
                                    0 to 2
                                )
                    ),

            "rectangle_sliders" to
                    mapOf(
                        "dimensions" to
                                MathAnswer.Sliders(
                                    mapOf(
                                        "width" to 4.0,
                                        "height" to 3.0
                                    )
                                )
                    ),

            "venn_and_table" to
                    mapOf(
                        "totals" to
                                fields(
                                    "a" to "6",
                                    "b" to "5",
                                    "union" to "9"
                                )
                    ),

            "static_geometry_volume" to
                    mapOf(
                        "volume" to
                                text("24")
                    )
        )
}
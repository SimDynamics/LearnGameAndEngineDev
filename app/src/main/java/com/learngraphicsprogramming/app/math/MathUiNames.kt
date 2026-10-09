package com.learngraphicsprogramming.app.math

import android.content.Context
import com.learngraphicsprogramming.app.R

/** Localizes engine identifiers, preserving any author-supplied labels elsewhere. */
internal fun Context.mathGraphModeName(mode: String): String = when (mode) {
    "point" -> getString(R.string.math_ui_mode_point)
    "line" -> getString(R.string.math_ui_mode_line)
    "polygon" -> getString(R.string.math_ui_mode_polygon)
    "scatter" -> getString(R.string.math_ui_mode_scatter)
    "parabola" -> getString(R.string.math_ui_mode_parabola)
    else -> mode
}

internal fun Context.mathShapeName(shape: String): String = when (shape) {
    "rectangle" -> getString(R.string.math_ui_shape_rectangle)
    "circle" -> getString(R.string.math_ui_shape_circle)
    "triangle" -> getString(R.string.math_ui_shape_triangle)
    "cube" -> getString(R.string.math_ui_shape_cube)
    "prism" -> getString(R.string.math_ui_shape_prism)
    "cylinder" -> getString(R.string.math_ui_shape_cylinder)
    "cone" -> getString(R.string.math_ui_shape_cone)
    "sphere" -> getString(R.string.math_ui_shape_sphere)
    else -> shape
}

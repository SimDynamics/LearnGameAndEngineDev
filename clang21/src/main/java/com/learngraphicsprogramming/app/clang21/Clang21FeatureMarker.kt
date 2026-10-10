package com.learngraphicsprogramming.app.clang21

/**
 * Lightweight marker for the on-demand dynamic feature.
 *
 * The base APK must continue compiling with Clang 23 when this module
 * is absent. The native Clang 21 payload and its loading path will be
 * integrated only after validating the App Bundle's split boundaries.
 */
object Clang21FeatureMarker {
    const val MODULE_NAME = "clang21"
}

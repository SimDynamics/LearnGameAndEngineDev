package com.learngraphicsprogramming.app

import org.json.JSONObject

/** Lesson input is an explicit curricular capability, never inferred from editable C++ source. */
internal object CppLabInputPolicy {
    fun requested(block: JSONObject, outputCheck: CppOutputCheck.Specification?): Boolean =
        block.has("initialStandardInput") ||
            outputCheck?.cases?.any { it.standardInput.isNotEmpty() } == true

    /** Keep a hidden draft intact in storage, but do not feed it to a task without input. */
    fun effectiveInput(requested: Boolean, draftText: String): String =
        if (requested) draftText else ""
}

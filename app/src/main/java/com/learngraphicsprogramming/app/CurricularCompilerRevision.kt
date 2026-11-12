package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

/**
 * Strict projection of published compiler expansions and paired static C++
 * references. Only these historical *additions* may reuse the old state map.
 * Current authored cases and starter code stay visible and executable.
 * Compiler verdicts require fresh executions, never a projected old receipt.
 */
internal object CurricularCompilerRevision {
    private const val END = "    return 0;\n}\n"

    private fun same(a: Any?, b: Any?): Boolean =
        LessonStateLayoutJson.fingerprint(a) == LessonStateLayoutJson.fingerprint(b)

    private fun verifiedStarterExpansion(previous: String, current: String) {
        if (previous == current) return
        require(previous.endsWith(END) && current.endsWith(END))
        val base = previous.removeSuffix(END)
        require(current.startsWith(base))
        val inserted = current.substring(base.length, current.length - END.length)
        require(inserted.isNotBlank() && !inserted.contains("#include") &&
            !inserted.contains("#define") && !inserted.contains("return "))
    }

    private fun caseRequirements(value: JSONObject): JSONObject {
        val copy = JSONObject(value.toString())
        copy.remove("explanation")
        copy.remove("reviewFeedback")
        return copy
    }

    /** Original, unchanged cases are protected, and extra cases never inherit credit. */
    private fun verifiedCaseExtension(previous: JSONObject, current: JSONObject) {
        val oldCases = previous.getJSONArray("cases")
        val newCases = current.getJSONArray("cases")
        require(oldCases.length() > 0 && newCases.length() >= oldCases.length())
        val oldShape = JSONObject(previous.toString())
        val newShape = JSONObject(current.toString())
        oldShape.remove("cases")
        newShape.remove("cases")
        require(same(oldShape, newShape))
        if (newCases.length() > oldCases.length()) {
            for (i in 0 until oldCases.length()) {
                require(same(caseRequirements(oldCases.getJSONObject(i)),
                    caseRequirements(newCases.getJSONObject(i))))
            }
        } else {
            // Closed reviewed objectives extend the sole old stdout with a
            // nonempty suffix, not an alternative, relaxed alias or new exit.
            require(oldCases.length() == 1)
            val oldCase = oldCases.getJSONObject(0)
            val nextCase = newCases.getJSONObject(0)
            val outputs = oldCase.getJSONArray("outputs")
            val currentOutputs = nextCase.getJSONArray("outputs")
            require(outputs.length() == 1 && currentOutputs.length() == 1)
            val previousText = outputs.getString(0)
            val nextText = currentOutputs.getString(0)
            require(nextText.startsWith(previousText) && nextText.length > previousText.length)
            val restored = caseRequirements(nextCase)
            restored.put("outputs", JSONArray(outputs.toString()))
            require(same(caseRequirements(oldCase), restored))
        }
    }

    fun projectCompiler(current: JSONObject, historical: JSONObject): JSONObject {
        require(historical.getString("type") == "cpp_compiler" &&
            current.getString("type") == "cpp_compiler")
        require(historical.has("outputCheck") && current.has("outputCheck"))
        require(historical.has("initialCode") && current.has("initialCode"))
        verifiedStarterExpansion(historical.getString("initialCode"), current.getString("initialCode"))
        verifiedCaseExtension(historical.getJSONObject("outputCheck"),
            current.getJSONObject("outputCheck"))
        val restored = JSONObject(current.toString())
        restored.put("initialCode", historical.getString("initialCode"))
        restored.put("outputCheck", JSONObject(historical.getJSONObject("outputCheck").toString()))
        require(LessonStateLayoutJson.stateFingerprint(restored) ==
            LessonStateLayoutJson.stateFingerprint(historical)) {
            "A compiler revision changed another protected requirement"
        }
        return restored
    }

    fun projectReference(current: JSONObject, historical: JSONObject): JSONObject {
        require(current.getString("type") == "code" && historical.getString("type") == "code")
        val previous = historical.getString("code")
        val next = current.getString("code")
        require(previous != next)
        verifiedStarterExpansion(previous, next)
        val restored = JSONObject(current.toString())
        restored.put("code", previous)
        require(LessonStateLayoutJson.stateFingerprint(restored) ==
            LessonStateLayoutJson.stateFingerprint(historical))
        return restored
    }
}

package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject

/**
 * Projections for immutable published compiler contracts. Only recognized
 * complete editors may recover their earlier digest; no student data or
 * executable lesson is modified.
 */
internal object CurricularPublishedLegacyCompat {
    private data class Diagnostic(val originalSha256: String, val project: Boolean = false)

    private val diagnostics = mapOf(
        "const-member-call" to Diagnostic("05cdb3e534649bd7e3efd1e080e5199443deace1e074d56d45490a8ff32c4c27"),
        "explicit-copy-initialization" to Diagnostic("ac0365d7452a3a6ff52a0f6c306d01815a09196f9f49e2bc90cdd4ec5dbc9fe9"),
        "explicit-argument-conversion" to Diagnostic("9f7db3f3a1004a235926bc12e06e9d0bf1a93b707f3ca92f6de2876677fd9c58"),
        "explicit-assignment" to Diagnostic("3e8eb728eac0d493c864596ec2364fe6c69ff85ce01408dd0020cec36ad6b087"),
        "swapped-named-arguments" to Diagnostic("b35436bcbac5b5e98731d953fb50499d5e7d3ad66ed1ad88771d56e481a8c9ed"),
        "u6-difference-unselected-definition" to Diagnostic("d17f1d73d7217ea45e1468f6ed0d1a5ef3565ecce33410a8022660baff950eba", true),
        "u6-difference-conflicting-return" to Diagnostic("e5b83315920efce2c1a6092721096ef596e2c58d7c96089ba01075bceb3986df", true),
        "u6-difference-missing-header" to Diagnostic("3d0f8c6e8fc84ec145f1c9a6c79ec7d5fbe50d8a70294c9f6effc0b4d8eb755c", true),
        "u6-envelopes-missing-header" to Diagnostic("ae23ef7cc266649cbad08efc8736916121dc404e132520a0c386ea73b23d7782", true),
        "u6-envelopes-missing-declaration" to Diagnostic("1c69caf83414219854952076e951e9adb3f9b6168503f60113a2d858103ef650", true),
        "u6-envelopes-unselected-definitions" to Diagnostic("b263e83bb0b912281964eb60261d33713e9cb6d5df046b375f5f008544c4a405", true),
        "u6-envelopes-duplicate-definition" to Diagnostic("72ac435d8a7399bd39c91be5a72ae2034aeb9653a8a78ec45fa3f82e6ac9470e", true),
        "u6-series-unknown-call" to Diagnostic("b84a7b928030171e03178f3300a1fd3dbbd86388ff998fcd8ddea9d247f9effe", true),
        "u6-series-unselected-definitions" to Diagnostic("157dc0f18f12db33cc3a7787009c408bb067ea833544df42ebdc76e2e2c00e34", true),
        "u6-series-duplicate-definition" to Diagnostic("26416e82df9c1bd708b840b6d940c79db644f69bcd2acecc56f595f23cf48d20", true),
        "u6-shelf-unselected-definition" to Diagnostic("2c568ff3ab456b8111cb9a338fc9c0d6f9ccff45a1e1cd3f840cee872900f7e3", true),
        "u6-shelf-conflicting-return" to Diagnostic("5b1315f399da68b066291bf96a7f1ed8cfd1aadf3964b2d2ca55cd1f6bae50ec", true)
    )
    private data class IntentionalExit(val originalSha256: String, val output: String)
    private val exits = listOf(
        IntentionalExit("d263ac4c841168cb7723bdc2daaeaf6ddc839c4830f17a26f6166ff919fb252c", "Entrada invalida|9:4"),
        IntentionalExit("6f796c5c55683ac266ae3f83c54f935273c341c34da087161276c7e361524ae8", "Error al abrir salida")
    )
    private const val CRT_BEFORE = "b475e21c42a6b93cb635f5bfd5197b66b7c1107145a413f2fa9582ab142cda35"
    private const val CRT_AFTER = "53648380e15d1148a779de47c0f94dc08f63e2319f79bdc08e72d25a6992ed31"
    private val stderrRevisions = setOf(
        "08d8b8ec762e2261137b3829ef00f7a005a31c199d92c76bfda25493ab15d47a",
        "e044e0f7fd932e66b5d7050160405f6c45a94d1d91a3a31983913340938a6b7e"
    )

    private fun fields(value: JSONObject) = value.keys().asSequence().toSet()
    private fun copy(value: JSONObject) = JSONObject(value.toString())
    private fun digest(value: JSONObject) = LessonStateLayoutJson.fingerprint(value)

    private fun withoutGuidance(block: JSONObject): JSONObject = copy(block).apply {
        optJSONObject("compileCheck")?.remove("explanation")
        optJSONObject("outputCheck")?.optJSONArray("cases")?.let { cases ->
            for (index in 0 until cases.length()) {
                cases.getJSONObject(index).remove("explanation")
                cases.getJSONObject(index).remove("reviewFeedback")
            }
        }
    }

    private fun restoreOriginalCrt(block: JSONObject): JSONObject {
        if (digest(withoutGuidance(block)) != CRT_AFTER) return copy(block)
        val result = copy(block)
        val cases = result.getJSONObject("outputCheck").getJSONArray("cases")
        require(cases.length() == 1)
        val case = cases.getJSONObject(0)
        require(case.getString("standardInput") == "")
        val outputs = case.getJSONArray("outputs")
        require(outputs.length() == 2)
        val original = outputs.getString(0)
        val windows = outputs.getString(1)
        require(original.toByteArray(Charsets.UTF_8).size == 802 &&
            original.count { it == '\n' } == 42 && original.count { it == '\r' } == 3 &&
            windows == original.replace("\n", "\r\n") &&
            windows.toByteArray(Charsets.UTF_8).size == 844)
        case.put("outputs", JSONArray().put(original))
        require(digest(withoutGuidance(result)) == CRT_BEFORE)
        return result
    }

    private fun restoreDiagnostic(block: JSONObject) {
        if (!block.has("compileCheck")) return
        val objective = block.getJSONObject("compileCheck")
        require(fields(objective).let { "diagnostic" in it &&
            it.all { key -> key == "diagnostic" || key == "explanation" } })
        val specification = diagnostics[objective.getString("diagnostic")]
            ?: error("Unknown historical compiler diagnostic")
        if (objective.has("explanation")) {
            require((objective.get("explanation") as? String)?.isNotBlank() == true)
        }
        require(!block.has("outputCheck") && !block.has("cmake") && !block.has("initialStandardInput"))
        require(block.has("projectFiles") == specification.project &&
            block.has("sourceFiles") == specification.project)
        val before = copy(block).apply { remove("compileCheck") }
        require(digest(before) == specification.originalSha256)
        block.remove("compileCheck")
    }

    private fun restoreIntentionalExit(block: JSONObject) {
        if (!block.has("outputCheck")) return
        val before = copy(block).apply { remove("outputCheck") }
        val specification = exits.firstOrNull { it.originalSha256 == digest(before) } ?: return
        val objective = block.getJSONObject("outputCheck")
        require(fields(objective) == setOf("cases"))
        val cases = objective.getJSONArray("cases")
        require(cases.length() == 1)
        val goal = cases.getJSONObject(0)
        require(fields(goal) == setOf("standardInput", "outputs", "exitCode", "explanation"))
        require(goal.getString("standardInput") == "" &&
            goal.getJSONArray("outputs").let { it.length() == 1 && it.getString(0) == specification.output } &&
            (goal.get("exitCode") as? Int) == 1 &&
            (goal.get("explanation") as? String)?.isNotBlank() == true)
        block.remove("outputCheck")
    }

    /** Return a contract view without touching the lesson displayed to the student. */
    fun compilerState(block: JSONObject): JSONObject {
        require(block.optString("type") == "cpp_compiler")
        val result = restoreOriginalCrt(block)
        restoreDiagnostic(result)
        restoreIntentionalExit(result)
        // Never discard standardErrorExact. Only two verified nonBlank goals
        // were added after the original saved-contract signatures.
        if (digest(result) in stderrRevisions) {
            val cases = result.getJSONObject("outputCheck").getJSONArray("cases")
            require(cases.length() == 1)
            val check = cases.getJSONObject(0)
            require(check.getString("standardError") == "nonBlank" &&
                !check.has("standardErrorExact"))
            check.remove("standardError")
        }
        return result
    }
}

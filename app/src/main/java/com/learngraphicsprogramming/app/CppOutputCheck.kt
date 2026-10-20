package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.runtime.CppCmakePhase
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import com.learngraphicsprogramming.app.runtime.CppEngineResult
import org.json.JSONArray
import org.json.JSONObject

/** Optional stdout oracles, independent of source implementation and exercise progress. */
internal object CppOutputCheck {
    data class Review(val outputs: List<String>, val text: String)
    data class Case(val standardInput: String, val outputs: List<String>,
                    val explanation: String?, val reviews: List<Review>, val target: String? = null)
    data class Verdict(val correct: Boolean, val explanation: String?)
    data class Snapshot(val files: Map<String, String>, val sourceFiles: List<String>,
                        val compiler: String, val configuration: String? = null, val target: String? = null)
    fun snapshot(files: Map<String, String>, sources: List<String>, compiler: String) =
        Snapshot(files.toMap(), sources.toList(), compiler)
    class Specification(val inputComparison: String, val cases: List<Case>) {
        fun caseFor(input: String, target: String? = null): Case? = cases.singleOrNull {
            (it.target == null || it.target == target) &&
                inputKey(it.standardInput, inputComparison) == inputKey(input, inputComparison)
        }
    }

    fun fromBlock(block: JSONObject): Specification? {
        if (!block.has("outputCheck")) return null
        val value = block.get("outputCheck") as? JSONObject ?: invalid("outputCheck must be an object")
        keys(value, setOf("inputComparison", "cases"))
        val mode = if (value.has("inputComparison")) text(value, "inputComparison") else "exact"
        require(mode in setOf("exact", "tokens")) { "Invalid outputCheck inputComparison" }
        val items = value.get("cases") as? JSONArray ?: invalid("outputCheck cases must be an array")
        require(items.length() > 0) { "outputCheck needs at least one case" }
        val cases = List(items.length()) { index ->
            val item = items.get(index) as? JSONObject ?: invalid("Invalid outputCheck case")
            keys(item, setOf("standardInput", "outputs", "explanation", "reviewFeedback", "target"))
            val input = text(item, "standardInput")
            val target = if (item.has("target")) text(item, "target").also {
                require(block.opt("cmake") is JSONObject) { "outputCheck target requires a CMake project" }
                require(it.matches(Regex("[A-Za-z_][A-Za-z0-9_.+-]{0,127}"))) {
                    "Invalid outputCheck CMake target"
                }
            } else null
            val outputs = strings(item.get("outputs"))
            val explanation = optionalText(item, "explanation")
            val rules = if (item.has("reviewFeedback")) {
                val list = item.get("reviewFeedback") as? JSONArray ?: invalid("Invalid outputCheck reviewFeedback")
                List(list.length()) { ruleIndex ->
                    val rule = list.get(ruleIndex) as? JSONObject ?: invalid("Invalid outputCheck review")
                    keys(rule, setOf("outputs", "text"))
                    Review(strings(rule.get("outputs")), text(rule, "text").also {
                        require(it.isNotBlank()) { "Empty outputCheck review" }
                    })
                }
            } else emptyList()
            val reviewed = rules.flatMap(Review::outputs)
            require(reviewed.distinct().size == reviewed.size && reviewed.none { it in outputs }) {
                "Overlapping outputCheck feedback conditions"
            }
            Case(input, outputs, explanation, rules, target)
        }
        cases.forEachIndexed { index, current ->
            require(cases.take(index).none { previous ->
                inputKey(previous.standardInput, mode) == inputKey(current.standardInput, mode) &&
                    (previous.target == null || current.target == null || previous.target == current.target)
            }) { "Ambiguous outputCheck target/input cases" }
        }
        return Specification(mode, cases)
    }

    /** ASCII whitespace is the C++ prepared-token contract; no trimming of stdout. */
    private fun inputKey(value: String, mode: String): List<String> = if (mode == "exact") listOf(value)
        else value.split(Regex("[ \\t\\n\\r\\u000B\\u000C]+" )).filter(String::isNotEmpty)

    private fun keys(value: JSONObject, allowed: Set<String>) = require(value.keys().asSequence().all { it in allowed }) {
        "Unknown outputCheck property"
    }
    private fun text(value: JSONObject, key: String): String = value.get(key) as? String
        ?: invalid("outputCheck $key must be text")
    private fun optionalText(value: JSONObject, key: String): String? = if (!value.has(key)) null else
        text(value, key).also { require(it.isNotBlank()) { "Empty outputCheck $key" } }
    private fun strings(value: Any): List<String> {
        val array = value as? JSONArray ?: invalid("outputCheck outputs must be an array")
        require(array.length() > 0) { "Empty outputCheck outputs" }
        return List(array.length()) { array.get(it) as? String ?: invalid("outputCheck output must be text") }
            .also { require(it.distinct().size == it.size) { "Duplicate outputCheck output" } }
    }
    private fun invalid(message: String): Nothing = throw IllegalArgumentException(message)

    fun successfulOutput(result: CppEngineResult): String? = result.execution?.takeIf {
        result.compilation.exitCode == 0 && !result.compilation.timedOut && it.exitCode == 0
    }?.stdout

    fun successfulOutput(result: CppCmakeResult): String? = result.execution?.takeIf {
        result.phase == CppCmakePhase.EXECUTION && result.exitCode == 0 && !result.timedOut && it.exitCode == 0
    }?.stdout

    /** Exactly one ephemeral result, never a draft history, saved answer or heart transaction.
     * An edit invalidates its ticket even if Undo later restores identical source bytes. */
    class Session(private val specification: Specification) {
        class Ticket internal constructor(val snapshot: Snapshot, val input: String, val case: Case?)
        private var snapshot: Snapshot? = null
        private var input = ""
        private var pending: Ticket? = null
        private var completed: Pair<Case, String>? = null
        val canCheck get() = completed != null

        fun observe(currentSnapshot: Snapshot?, currentInput: String): Boolean {
            if (snapshot == currentSnapshot && input == currentInput) return false
            snapshot = currentSnapshot
            input = currentInput
            invalidate()
            return true
        }

        fun invalidate() { pending = null; completed = null }

        fun begin(currentSnapshot: Snapshot?, currentInput: String): Ticket? {
            observe(currentSnapshot, currentInput)
            invalidate()
            return currentSnapshot?.let { Ticket(it, currentInput, specification.caseFor(currentInput, it.target)).also { pending = it } }
        }

        fun complete(ticket: Ticket?, currentSnapshot: Snapshot?, currentInput: String, stdout: String?): Boolean {
            observe(currentSnapshot, currentInput)
            if (ticket == null || ticket !== pending) return false
            pending = null
            val target = ticket.case
            if (stdout == null || target == null || ticket.snapshot != snapshot || ticket.input != input) return false
            completed = target to stdout
            return true
        }

        fun check(): Verdict? = completed?.let { (target, actual) ->
            if (actual in target.outputs) Verdict(true, target.explanation)
            else Verdict(false, target.reviews.singleOrNull { actual in it.outputs }?.text)
        }
    }
}

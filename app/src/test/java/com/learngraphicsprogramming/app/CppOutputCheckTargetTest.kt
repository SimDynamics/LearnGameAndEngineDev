package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CppOutputCheckTargetTest {
    @Test fun distinctExecutablesCanUseTheSameInputButNeverEachOthersOutputOrFeedback() {
        val spec = specification(case("battery", "Bateria: OK\n"), case("client", "1:2"))
        assertEquals("Bateria: OK\n", spec.caseFor("", "battery")!!.outputs.single())
        assertEquals("1:2", spec.caseFor("", "client")!!.outputs.single())
        assertTrue(verdict(spec, "battery", "Bateria: OK\n")!!.correct)
        assertTrue(verdict(spec, "client", "1:2")!!.correct)
        assertEquals(CppOutputCheck.Verdict(false, null), verdict(spec, "battery", "1:2"))
        assertEquals(CppOutputCheck.Verdict(false, null), verdict(spec, "client", "Bateria: OK\n"))
        assertFalse(verdict(spec, "battery", "Bateria: OK")!!.correct)
    }

    @Test fun unknownOrMissingTargetNeverSelectsOneOfTheOtherTargets() {
        val spec = specification(case("battery", "Bateria: OK\n"), case("client", "1:2"))
        assertNull(spec.caseFor(""))
        assertNull(spec.caseFor("", "other"))
        assertNull(spec.caseFor("", "CLIENT"))
        assertNull(verdict(spec, null, "1:2"))
        assertNull(verdict(spec, "other", "1:2"))
    }

    @Test fun legacyCasesContinueToApplyWithoutNewMetadataIncludingExistingCmakeChecks() {
        val legacy = case(null, "OK")
        val spec = specification(legacy)
        for (target in listOf(null, "client", "battery")) {
            assertSame(spec.cases.single(), spec.caseFor("", target))
            assertTrue(verdict(spec, target, "OK")!!.correct)
        }
        assertNotNull(CppOutputCheck.fromBlock(JSONObject().put("outputCheck",
            JSONObject().put("cases", JSONArray().put(legacy)))))
    }

    @Test fun sameTargetCanHaveSeveralInputsAndTokenModeDoesNotMergeOtherTargets() {
        val spec = specification(case("battery", "A").put("standardInput", "Leo 2"),
            case("client", "B").put("standardInput", "Leo\n2"),
            case("battery", "C").put("standardInput", "Rita 4"), mode = "tokens")
        assertEquals(listOf("A"), spec.caseFor("\tLeo 2\n", "battery")!!.outputs)
        assertEquals(listOf("B"), spec.caseFor("\tLeo 2\n", "client")!!.outputs)
        assertEquals(listOf("C"), spec.caseFor("Rita\n4", "battery")!!.outputs)
        assertNull(spec.caseFor("Leo\u00a02", "client"))
        assertNull(spec.caseFor("Leo 2 3", "battery"))
    }

    @Test fun duplicateOrWildcardCasesCannotAmbiguouslyShareTargetAndInput() {
        val collisions = listOf(
            listOf(case("client", "A"), case("client", "B")),
            listOf(case(null, "A"), case("client", "B")),
            listOf(case("client", "A"), case(null, "B")))
        for (items in collisions) reject(*items.toTypedArray())
        reject(case("client", "A").put("standardInput", "A B"),
            case("client", "B").put("standardInput", "A\nB"), mode = "tokens")
        // A global oracle for another input is unambiguous and remains compatible.
        assertNotNull(specification(case(null, "A").put("standardInput", "other"), case("client", "B")))
    }

    @Test fun targetRequiresCmakeAndAnExactPortableNameRatherThanAnInferredSourceIdentifier() {
        val value = JSONObject().put("outputCheck", JSONObject().put("cases", JSONArray().put(case("client", "A"))))
        try { CppOutputCheck.fromBlock(value); fail("A plain compiler has no CMake target") }
        catch (_: IllegalArgumentException) { }
        for (target in listOf("", " client", "client ", "../client", "client;other", "a/b", "9client", "a".repeat(129)))
            reject(case(target, "A"))
        for (target in listOf(JSONObject.NULL, 7, true, JSONArray())) reject(case("client", "A").put("target", target))
        for (target in listOf("client", "_a", "client.test-2+debug", "a".repeat(128)))
            assertEquals(target, specification(case(target, "A")).cases.single().target)
    }

    @Test fun changingOnlyTheTargetInvalidatesBothTheVerdictAndLateResultEvenWhenSourcesMatch() {
        val spec = specification(case("battery", "A"), case("client", "B"))
        val session = CppOutputCheck.Session(spec)
        val battery = snapshot("battery")
        val client = battery.copy(target = "client")
        val old = session.begin(battery, "")
        assertTrue(session.observe(client, ""))
        assertFalse(session.complete(old, client, "", "A"))
        val fresh = session.begin(client, "")
        assertTrue(session.complete(fresh, client, "", "B"))
        assertTrue(session.check()!!.correct)
        session.observe(battery, "")
        assertNull(session.check())
        assertFalse(session.complete(fresh, battery, "", "B"))
        assertFalse(CppOutputCheck.Session(spec).canCheck)
    }

    @Test fun targetSpecificExplanationsStayWithTheirOwnCase() {
        val battery = case("battery", "A").put("explanation", "Battery explanation")
            .put("reviewFeedback", JSONArray().put(JSONObject().put("outputs", JSONArray().put("Wrong"))
                .put("text", "Battery guide")))
        val client = case("client", "B").put("explanation", "Client explanation")
        val spec = specification(battery, client)
        assertEquals("Battery explanation", verdict(spec, "battery", "A")!!.explanation)
        assertEquals("Client explanation", verdict(spec, "client", "B")!!.explanation)
        assertEquals("Battery guide", verdict(spec, "battery", "Wrong")!!.explanation)
        assertNull(verdict(spec, "client", "Wrong")!!.explanation)
    }

    private fun snapshot(target: String?) = CppOutputCheck.snapshot(mapOf("CMakeLists.txt" to "project(qa)"),
        emptyList(), "clang23").copy(target = target)
    private fun verdict(spec: CppOutputCheck.Specification, target: String?, output: String): CppOutputCheck.Verdict? {
        val session = CppOutputCheck.Session(spec)
        val current = snapshot(target)
        session.complete(session.begin(current, ""), current, "", output)
        return session.check()
    }
    private fun case(target: String?, output: String) = JSONObject().put("standardInput", "")
        .put("outputs", JSONArray().put(output)).apply { target?.let { put("target", it) } }
    private fun specification(vararg cases: JSONObject, mode: String = "exact") = requireNotNull(CppOutputCheck.fromBlock(
        JSONObject().put("cmake", JSONObject().put("target", "client")).put("outputCheck",
            JSONObject().put("inputComparison", mode).put("cases", JSONArray(cases.toList())))))
    private fun reject(vararg cases: JSONObject, mode: String = "exact") {
        try { specification(*cases, mode = mode); fail("Ambiguous or malformed target check") }
        catch (_: IllegalArgumentException) { }
    }
}

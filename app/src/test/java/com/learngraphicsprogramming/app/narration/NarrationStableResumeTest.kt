package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.LessonStateLayoutJson
import com.learngraphicsprogramming.app.StableLessonStateLayout
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NarrationStableResumeTest {
    private val selected = NarrationSettings(engine = NarrationEngine.ANDROID,
        androidVoice = "installed-voice", language = NarrationLanguage.AMERICAN_ENGLISH)
    private val scope = "programming/fixture/original-lineage"
    private val configuration = "c".repeat(64)
    private fun part(block: String, audio: Char, feedback: Boolean = false, occurrence: Int = 0) =
        NarrationStablePart(block, feedback, audio.toString().repeat(64), occurrence)
    private fun page(vararg parts: NarrationStablePart) = NarrationStablePage(scope, configuration, parts.toList())
    private fun history(key: String, point: NarrationResumePoint, stable: NarrationStablePage? = null): NarrationResumeHistory {
        var result = NarrationResumePolicy.begin(NarrationResumeHistory(), "older", "older-owner", NarrationResumePoint())
        result = NarrationResumePolicy.begin(result, key, "source-owner", point, stable)
        return NarrationResumePolicy.begin(result, "newer", "newer-owner", NarrationResumePoint(1, 71))
    }

    @Test fun legacyReorderedReleaseMigratesDirectlyWithoutAnIntermediateInstall() {
        val original = lesson(false)
        val oldScript = LessonNarrationScript.page(original, 1, selected.language)
        val oldParts = NarrationSegments.prepare(oldScript.texts, 250)
        val oldKey = NarrationResumePolicy.key("programming/fixture/1", selected.language.tag,
            selected.engine.name, selected.androidVoice.orEmpty(), oldParts)
        val oldPosition = oldParts.indexOf("Pause here.")
        assertTrue(oldPosition >= 0)
        val reordered = lesson(true)
        val layout = requireNotNull(NarrationStableLesson.layout(reordered))
        val script = LessonNarrationScript.page(reordered, 0, selected.language)
        val current = requireNotNull(NarrationStableLesson.page(layout, "programming", "fixture", 0, script, selected))
        val legacy = NarrationStableLesson.legacy(reordered, layout, "programming", "fixture", selected,
            setOf(oldKey), false, { "" }, { _, _, block -> block })
        assertTrue(legacy.containsKey(oldKey))
        val previous = history(oldKey, NarrationResumePoint(oldPosition, 1_537))
        val moved = NarrationStableResume.prepare(previous, current.key, current.stable, legacy)
        assertEquals(listOf("newer", current.key, "older"), moved.entries.map { it.key })
        assertEquals(NarrationResumePoint(current.texts.indexOf("Pause here."), 1_537), moved.entries[1].point)
        assertEquals(previous.entries.first(), moved.entries.first())
        assertEquals(previous.entries.last(), moved.entries.last())
        assertEquals(previous.activeKey, moved.activeKey)
        assertEquals(previous.activeOwner, moved.activeOwner)
        assertEquals(moved, NarrationStableResume.prepare(moved, current.key, current.stable, legacy))
    }

    @Test fun subsequentMovesUseTheSavedStableAnchorWithoutAReplayOrExtraBookmark() {
        val a = part("a", 'a'); val b = part("b", 'b'); val c = part("c", 'c')
        val original = history("old", NarrationResumePoint(1, 579), page(a, b, c))
        val one = NarrationStableResume.prepare(original, "new", page(b, c))
        val two = NarrationStableResume.prepare(one, "twice", page(c, a, b))
        assertEquals(NarrationResumePoint(2, 579), two.entries[1].point)
        assertEquals(listOf("newer", "twice", "older"), two.entries.map { it.key })
        val fourth = NarrationResumePolicy.begin(two, "fourth", "owner", NarrationResumePoint())
        assertEquals(listOf("fourth", "newer", "twice"), fourth.entries.map { it.key })
    }

    @Test fun neuralLegacyBookmarksFollowTheExactSentenceWithinAMovedBlock() {
        val neural = selected.copy(engine = NarrationEngine.KOKORO, kokoroVoice = selected.language.defaultVoice)
        val text = "An earlier sentence. Pause here. A later sentence."
        val original = lesson(false, text)
        val oldScript = LessonNarrationScript.page(original, 1, neural.language, forKokoro = true)
        val oldParts = NarrationSegments.sentences(oldScript.texts)
        val oldPosition = oldParts.indexOf("Pause here.")
        assertTrue(oldPosition > 0)
        val oldKey = NarrationResumePolicy.key("programming/fixture/1", neural.language.tag,
            neural.engine.name, neural.kokoroVoice, oldParts)
        val reordered = lesson(true, text)
        val layout = requireNotNull(NarrationStableLesson.layout(reordered))
        val script = LessonNarrationScript.page(reordered, 0, neural.language, forKokoro = true)
        val current = requireNotNull(NarrationStableLesson.page(layout, "programming", "fixture", 0, script, neural))
        val legacy = NarrationStableLesson.legacy(reordered, layout, "programming", "fixture", neural,
            setOf(oldKey), true, { "" }, { _, _, block -> block })
        val moved = NarrationStableResume.prepare(history(oldKey, NarrationResumePoint(oldPosition, 913)),
            current.key, current.stable, legacy)
        assertEquals(NarrationResumePoint(current.texts.indexOf("Pause here."), 913), moved.entries[1].point)
        assertEquals(current.key, moved.entries[1].key)
    }

    @Test fun aMatchingSentenceInADifferentBlockNeverClaimsThePause() {
        val original = history("old", NarrationResumePoint(0, 800), page(part("a", 'a')))
        assertEquals(original, NarrationStableResume.prepare(original, "new", page(part("b", 'a'))))
    }

    @Test fun mappedPlaybackIdentityCannotCollideWithLegacyPositionsOrAnotherEqualTextBlock() {
        val original = lesson(false)
        val layout = requireNotNull(NarrationStableLesson.layout(original))
        val first = NarrationStableLesson.identity(layout, "programming", "fixture", 0)
        val second = NarrationStableLesson.identity(layout, "programming", "fixture", 1)
        assertNotEquals("programming/fixture/0", first)
        assertNotEquals(first, second)
        val pageSwap = StableLessonStateLayout(layout.origin, layout.current.reversed())
        assertEquals(first, NarrationStableLesson.identity(pageSwap, "programming", "fixture", 1))
        val reordered = requireNotNull(NarrationStableLesson.layout(lesson(true)))
        assertNotEquals(first, NarrationStableLesson.identity(reordered, "programming", "fixture", 1))
    }

    @Test fun duplicateSentencesKeepTheirOccurrenceWithinTheSameBlock() {
        val first = part("a", 'a'); val second = part("a", 'a', occurrence = 1); val other = part("b", 'b')
        assertEquals(NarrationResumePoint(2, 153), NarrationStableResume.remap(
            NarrationResumePoint(1, 153), page(first, second, other), page(other, first, second)))
    }

    @Test fun changedAudioAndDifferentLanguageVoiceOrLessonCannotReuseMilliseconds() {
        val source = page(part("a", 'a'))
        val original = history("old", NarrationResumePoint(0, 800), source)
        val invalid = listOf(page(part("a", 'b')), source.copy(scope = "other-lesson"),
            source.copy(configuration = "d".repeat(64)))
        invalid.forEach { assertEquals(original, NarrationStableResume.prepare(original, "new", it)) }
    }

    @Test fun finishedWholePageKeepsItsExactEndAndReleasedBoundedSlot() {
        val source = page(part("a", 'a'), part("b", 'b'))
        var before = NarrationResumePolicy.begin(NarrationResumeHistory(), "old", "owner", NarrationResumePoint(), source)
        before = NarrationResumePolicy.update(before, "old", "owner", NarrationResumePoint(1, 5_271, finished = true))
        val moved = NarrationStableResume.prepare(before, "new", source)
        assertEquals(NarrationResumePoint(1, 5_271, finished = true), moved.entries.single().point)
        assertEquals("", moved.activeKey)
        assertEquals("", moved.activeOwner)
    }

    @Test fun addedBodyAndRepositionedFinalSegmentDoNotInheritAnotherPagesFinishedFlag() {
        val a = part("a", 'a'); val b = part("b", 'b'); val c = part("c", 'c')
        val point = NarrationResumePoint(1, 5_271, finished = true)
        assertEquals(point.copy(segment = 2, finished = false),
            NarrationStableResume.remap(point, page(a, b), page(c, a, b)))
        assertEquals(point.copy(segment = 0, finished = false),
            NarrationStableResume.remap(point, page(a, b), page(b, a)))
    }

    @Test fun unheardEarlierFeedbackIsNeverCreditedByMovingAnAlreadyHeardExercise() {
        val qa = part("a", 'a'); val fa = part("a", 'b', feedback = true)
        val qb = part("b", 'c'); val fb = part("b", 'd', feedback = true)
        val point = NarrationResumePoint(2, 71, feedbackHeardThrough = 2)
        assertEquals(point.copy(segment = 0, feedbackHeardThrough = 0),
            NarrationStableResume.remap(point, page(qa, fa, qb, fb), page(qb, fb, qa, fa)))
        assertEquals(point.copy(segment = 0, feedbackHeardThrough = 4), NarrationStableResume.remap(
            point.copy(feedbackHeardThrough = 4), page(qa, fa, qb, fb), page(qb, fb, qa, fa)))
    }

    @Test fun exactDestinationHasPriorityOverAnyOlderCandidate() {
        val source = page(part("a", 'a'))
        val original = NarrationResumePolicy.begin(history("old", NarrationResumePoint(0, 3_000), source),
            "new", "new-owner", NarrationResumePoint(0, 900), source)
        assertEquals(original, NarrationStableResume.prepare(original, "new", source))
    }

    @Test fun collisionsUseMostRecentExistingSlotWithoutPromotionOrEviction() {
        val a = part("a", 'a'); val b = part("b", 'b')
        var old = NarrationResumePolicy.begin(NarrationResumeHistory(), "a", "owner-a", NarrationResumePoint(0, 19), page(a))
        old = NarrationResumePolicy.begin(old, "b", "owner-b", NarrationResumePoint(0, 23), page(b))
        val moved = NarrationStableResume.prepare(old, "merged", page(a, b))
        assertEquals(listOf("merged", "a"), moved.entries.map { it.key })
        assertEquals(NarrationResumePoint(1, 23), moved.entries.first().point)
        assertEquals(moved, NarrationResumePolicy.update(moved, "b", "owner-b", NarrationResumePoint(0, 99)))
    }

    @Test fun additiveMetadataRoundTripsAndMalformedMetadataCannotReplaceLegacyPoint() {
        val source = page(part("a", 'a'), part("a", 'b', true))
        val encoded = NarrationStablePageJson.encode(source)
        assertEquals(source, NarrationStablePageJson.decode(encoded))
        listOf(null, "{}", "[]", encoded.replace("\"version\":1", "\"version\":2"))
            .forEach { assertNull(NarrationStablePageJson.decode(it)) }
    }

    @Test fun runtimePresentationRelocationInstallsTheCorrespondingNewProvenance() {
        val before = page(part("a", 'a')); val after = page(part("a", 'a'), part("b", 'b'))
        val source = history("old", NarrationResumePoint(0, 23), before)
        val moved = NarrationResumePolicy.relocate(source, "old", "new", after) { it }
        assertEquals(after, moved.entries[1].stable)
        assertEquals(NarrationResumePoint(0, 23), moved.entries[1].point)
    }

    private fun lesson(moved: Boolean, pauseText: String = "Pause here."): JSONObject {
        val blocks = linkedMapOf("a" to JSONObject().put("type", "paragraph").put("text", "First explanation."),
            "b" to JSONObject().put("type", "paragraph").put("text", "The same sentence."),
            "c" to JSONObject().put("type", "paragraph").put("text", pauseText),
            "d" to JSONObject().put("type", "paragraph").put("text", "The same sentence."))
        val origin = listOf("p0" to listOf("a", "b"), "p1" to listOf("c", "d"))
        val current = if (moved) listOf("p1" to listOf("c", "a"), "p0" to listOf("b", "d")) else origin
        val pages = JSONArray(); val old = JSONArray(); val now = JSONArray()
        origin.forEach { (id, ids) -> old.put(JSONObject().put("id", id).put("blocks", JSONArray().also { array ->
            ids.forEach { array.put(JSONObject().put("id", it)
                .put("sha256", LessonStateLayoutJson.stateFingerprint(blocks.getValue(it)))) }
        })) }
        current.forEach { (id, ids) ->
            pages.put(JSONObject().put("blocks", JSONArray().also { array -> ids.forEach { array.put(blocks.getValue(it)) } }))
            now.put(JSONObject().put("id", id).put("blocks", JSONArray(ids)))
        }
        return JSONObject().put("id", "fixture").put("title", "Fixture.").put("pages", pages)
            .put("stateLayout", JSONObject().put("version", 1).put("origin", old).put("current", now))
    }
}

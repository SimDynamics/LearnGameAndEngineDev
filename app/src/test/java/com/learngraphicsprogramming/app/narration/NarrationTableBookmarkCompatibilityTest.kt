package com.learngraphicsprogramming.app.narration

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Relocates the existing three slots after removing nonvisible table separators. */
class NarrationTableBookmarkCompatibilityTest {
    private val resources = File(System.getProperty("translation.resources") ?: "src/main/res")
    private val lesson = JSONObject(resources.absoluteFile.parentFile!!.resolve(
        "assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/" +
            "lesson_construir_y_comprobar_un_modelo_de_pedidos.json").readText())
    private val page = "programming/${lesson.getString("id")}/1"

    private data class Plans(val language: NarrationLanguage, val engine: NarrationEngine,
                             val previous: LessonNarrationScript, val current: LessonNarrationScript,
                             val before: List<String>, val after: List<String>,
                             val oldKey: String, val newKey: String)

    private fun plans(): List<Plans> = NarrationLanguage.entries.flatMap { language ->
        listOf(NarrationEngine.KOKORO, NarrationEngine.ANDROID).map { engine ->
            fun script(separateCells: Boolean) = LessonNarrationScript.presentedPageVersioned(
                lesson, 1, language, engine == NarrationEngine.KOKORO, { "" },
                completeVisibleLabels = true, separateTableCells = separateCells,
                presentation = { _, block -> block })
            fun parts(script: LessonNarrationScript) = if (engine == NarrationEngine.ANDROID)
                NarrationSegments.prepare(script.texts, 250) else NarrationSegments.sentences(script.texts)
            val previous = script(false)
            val current = script(true)
            val before = parts(previous)
            val after = parts(current)
            fun key(texts: List<String>) = NarrationResumePolicy.key(page, language.tag, engine.name,
                if (engine == NarrationEngine.ANDROID) "installed-voice" else language.defaultVoice, texts)
            Plans(language, engine, previous, current, before, after, key(before), key(after))
        }
    }

    private fun history(key: String, point: NarrationResumePoint): NarrationResumeHistory {
        var value = NarrationResumePolicy.begin(NarrationResumeHistory(), "older-page", "older-owner",
            NarrationResumePoint(1, 328))
        value = NarrationResumePolicy.begin(value, key, "table-owner", point)
        return NarrationResumePolicy.begin(value, "newer-page", "newer-owner", NarrationResumePoint(2, 957))
    }

    private fun relocate(history: NarrationResumeHistory, plans: Plans): NarrationResumeHistory =
        NarrationResumePolicy.relocate(history, plans.oldKey, plans.newKey) {
            NarrationPresentationRemap.remap(it, plans.previous, plans.current, plans.engine)
        }

    @Test fun coldTableBookmarkKeepsExactUnchangedAudioOffsetAndTheSameThreeSlots() {
        assertTrue(LessonNarrationScript.hasJoinedTableRows(lesson, 1))
        for (plans in plans()) {
            assertNotEquals("Only the affected page plan changes", plans.oldKey, plans.newKey)
            val oldIndex = plans.before.indexOfFirst {
                ManualEnglishSpeech.strip(it).startsWith("Motivo de la solicitud 5:")
            }
            assertTrue(oldIndex >= 0)
            val newIndex = plans.after.indexOf(plans.before[oldIndex])
            assertTrue(newIndex >= 0)
            val point = NarrationResumePoint(oldIndex, 1_537)
            val original = history(plans.oldKey, point)
            val moved = relocate(original, plans)
            assertEquals(listOf("newer-page", plans.newKey, "older-page"), moved.entries.map { it.key })
            assertEquals(NarrationResumePoint(newIndex, 1_537),
                NarrationResumePolicy.restore(moved, plans.newKey, plans.after.size))
            assertEquals(original.entries.first(), moved.entries.first())
            assertEquals(original.entries.last(), moved.entries.last())
            assertEquals(original.activeKey, moved.activeKey)
            assertEquals(original.activeOwner, moved.activeOwner)
            val fourth = NarrationResumePolicy.begin(moved, "fourth-page", "fourth-owner", NarrationResumePoint())
            assertEquals(listOf("fourth-page", "newer-page", plans.newKey), fourth.entries.map { it.key })
        }
    }

    @Test fun completedTableRetainsItsFinalPositionFeedbackCreditAndBoundedReleasedSlot() {
        for (plans in plans()) {
            assertEquals(plans.before.last(), plans.after.last())
            val oldGate = NarrationScriptSegments.gates(plans.previous.texts, plans.engine, plans.previous.gates).last()
            val newGate = NarrationScriptSegments.gates(plans.current.texts, plans.engine, plans.current.gates).last()
            var original = history(plans.oldKey, NarrationResumePoint())
            original = NarrationResumePolicy.begin(original, plans.oldKey, "finishing-owner", NarrationResumePoint())
            original = NarrationResumePolicy.update(original, plans.oldKey, "finishing-owner",
                NarrationResumePoint(plans.before.lastIndex, 4_319, finished = true,
                    feedbackHeardThrough = oldGate.endExclusive))
            val moved = relocate(original, plans)
            assertEquals(3, moved.entries.size)
            assertEquals(plans.newKey, moved.entries.first().key)
            assertEquals(NarrationResumePoint(plans.after.lastIndex, 4_319, finished = true,
                feedbackHeardThrough = newGate.endExclusive),
                NarrationResumePolicy.restore(moved, plans.newKey, plans.after.size))
            assertEquals("", moved.activeKey)
            assertEquals("", moved.activeOwner)
            assertEquals(original.entries.drop(1), moved.entries.drop(1))
        }
    }

    @Test fun changedCellRecordingRestartsOnlyItsOriginalBlockWithoutInventingMilliseconds() {
        for (plans in plans()) {
            val changed = plans.before.indexOfFirst { it !in plans.after }
            assertTrue("The removed synthetic separator really changes a recording", changed >= 0)
            val moved = requireNotNull(NarrationPresentationRemap.remap(
                NarrationResumePoint(changed, 2_907), plans.previous, plans.current, plans.engine))
            val prefix = plans.current.origins.indexOfFirst { it.blockIndex == 4 && !it.feedback }
            val expected = if (plans.engine == NarrationEngine.ANDROID)
                NarrationSegments.prepare(plans.current.texts.take(prefix), 250).size
                else NarrationSegments.sentences(plans.current.texts.take(prefix)).size
            assertEquals(NarrationResumePoint(expected, 0), moved)
        }
    }

    @Test fun aNewerDestinationOrAnUnrelatedPageCanNeverBeOverwrittenByTheLegacyCandidate() {
        for (plans in plans()) {
            var original = NarrationResumePolicy.begin(NarrationResumeHistory(), plans.newKey, "new-owner",
                NarrationResumePoint(1, 877))
            original = NarrationResumePolicy.begin(original, "other-page", "other-owner", NarrationResumePoint(2, 945))
            original = NarrationResumePolicy.begin(original, plans.oldKey, "old-owner", NarrationResumePoint(3, 1_200))
            val moved = relocate(original, plans)
            assertEquals(original.entries.filterNot { it.key == plans.oldKey }, moved.entries)
            assertEquals(NarrationResumePoint(1, 877),
                NarrationResumePolicy.restore(moved, plans.newKey, plans.after.size))
            val unrelated = history("different-page-same-text", NarrationResumePoint(2, 1_330))
            assertEquals(unrelated, relocate(unrelated, plans))
        }
    }

    @Test fun everyUnchangedRecordingRetainsItsOriginalKokoroCacheIdentity() {
        for (plans in plans().filter { it.engine == NarrationEngine.KOKORO }) {
            val shared = plans.before.filter { it in plans.after }
            assertTrue(shared.isNotEmpty())
            val oldKeys = plans.before.associateWith {
                KokoroNarrationCache.key(plans.language.tag, plans.language.defaultVoice, it)
            }
            for (text in shared) assertEquals(oldKeys[text],
                KokoroNarrationCache.key(plans.language.tag, plans.language.defaultVoice, text))
            assertTrue("New cell recordings receive their own text-derived identity", plans.after.any { it !in oldKeys })
            fun sentences(parts: List<String>) = parts.mapIndexed { index, text ->
                NarrationCurriculum.Sentence(text, plans.language, plans.language.defaultVoice, "table/$index")
            }
            val queue = NarrationPageQueue()
            val previous = queue.submit(sentences(plans.before))
            previous.sentences.forEach { queue.markCompleted(previous.revision, NarrationPageQueue.hash(it)) }
            val current = queue.submit(sentences(plans.after))
            val retained = oldKeys.values.toSet().intersect(current.sentences.map { NarrationPageQueue.hash(it) }.toSet())
            assertEquals("Existing priority queue retains completion for every unchanged cached sentence",
                retained, current.completed)
            assertTrue("Only new cell audio remains pending", current.pending > 0)
        }
    }
}

package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.math.MathTextSemantics
import org.junit.Assert.*
import org.junit.Test

class ContextualSlashReadingMappingTest {
    @Test fun currentRepartoSlashRemainsPendingAfterTheNumeratorUntilItsFollowingAtom() {
        for (source in listOf("Si ambos operandos fueran enteros, 9 / 2 produciría 4;", "9 / 2")) {
            val slash = source.indexOf('/')
            assertTrue(MathTextSemantics.ranges(source).any { slash in it.start until it.endExclusive })
            val words = NarrationSpokenWords.lexemes(source)
            val mapping = NarrationWordMappings.forBlock(0, source, mapOf(0 to source)).getValue(0)
            val nine = words.indexOfFirst { it.canonical == "9" }
            val two = words.indexOfFirst { it.canonical == "2" }
            assertTrue("The slash must not be revealed along with the earlier numerator", mapping.displayed[nine].endExclusive <= slash)
            assertEquals("With legacy Spanish lexical atoms, the intervening sign remains pending through its actual sound",
                slash, mapping.displayed[two].start)
            assertEquals(source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
        }
    }

    @Test fun datesPathsCommentMarkersAndExplicitCodeKeepTheirEstablishedSlashOwnership() {
        for (source in listOf("Fecha 20/10/2026.", "Abre src/main.cpp.", "Fuera del literal, // comienza un comentario.",
            "Lee `9 / 2` en el código.")) {
            val mapping = NarrationWordMappings.forBlock(0, source, mapOf(0 to source)).getValue(0)
            val slash = source.indexOf('/')
            assertFalse(source, MathTextSemantics.ranges(source).any { slash in it.start until it.endExclusive })
            assertTrue("A non-mathematical slash keeps its old previous-word range: $source",
                mapping.displayed.any { it.start < slash && it.endExclusive > slash })
            assertEquals(source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
        }
    }

    @Test fun sharedMappingKeepsMathPendingAndAllProseAndTechnicalGlyphsInAllFourLanguages() {
        for (language in NarrationLanguage.entries) {
            for (source in listOf("9 / 2", "La división 9 / 2 continúa.", "6 + 3 = 9",
                "Lee `std::cout` y `#include <iostream>`; continúa.",
                "Ruta src/main.cpp; fecha 20/10/2026; // comentario.")) {
                val words = NarrationSpokenWords.technicalAtoms(source, language)
                val mapping = NarrationWordMappings.forBlock(0, source, mapOf(0 to source), language).getValue(0)
                assertEquals("$language/$source", words.map { it.start to it.endExclusive },
                    mapping.spoken.map { it.start to it.endExclusive })
                assertEquals("Every original glyph remains covered once: $language/$source",
                    source.indices.toList(), mapping.displayed.flatMap { it.start until it.endExclusive })
                if (source.contains("9 / 2")) {
                    val slash = source.indexOf('/')
                    val numerator = words.indexOfFirst { it.canonical == "9" }
                    assertTrue("The denominator/operator remains pending after the numerator: $language/$source",
                        mapping.displayed[numerator].endExclusive <= slash)
                    val owner = mapping.displayed.indexOfFirst { slash in it.start until it.endExclusive }
                    assertTrue(owner > numerator)
                    assertEquals("The native frontend's existing operator atom or following operand owns the slash",
                        slash, mapping.displayed[owner].start)
                }
            }
        }
    }
}

package com.learngraphicsprogramming.app.narration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class EnglishAuxResourcesTest {
    private val expected = mapOf(
        "en_gb_bart_decoder.onnx" to (1737441L to "aa31f579ab8b75ec0f6571a4e85b15d0f501f2fea2415b77fbd000f1950e34be"),
        "en_gb_bart_encoder.onnx" to (1415363L to "bef604b9a32df6a270a3abc9641da78e74f3d57dfcaf6ed1a500085758bbedc0"),
        "en_gb_bart.json" to (1015L to "42176b75d06462a383549aabc84451974c70f5abf5cd00013a872332252b1fdc"),
        "en_us_bart_decoder.onnx" to (1737441L to "9e940b23aea9afa51196e40e47faf81ce14fb8663d513bc5fdae9cb1c29c63c0"),
        "en_us_bart_encoder.onnx" to (1415363L to "4191ca322086422164f652bdaeab2bf2c2a46c79b7e77126242ba28fe10fb195"),
        "en_us_bart.json" to (1019L to "c76a6851fffbca82f4856d331e478758d168bc9b8f0d51a57290ea9f344395ab"),
        "spacy_pos.json" to (316435L to "99d3610a6e997705627bdf7ee271af72aa61131bb4d1c48acf267d732eef1d01"),
        "spacy_pos.onnx" to (6291815L to "c5cc2867055c09b2825cf5370d171fbd46c085bd4e03e3f238a030b81ba86399"),
    )

    @Test fun distributionPreservesEveryValidatedArtifactIdentity() {
        assertEquals(expected, EnglishAuxResources.artifacts.associate { it.fileName to (it.bytes to it.sha256) })
        assertEquals(8, EnglishAuxResources.artifacts.size)
        assertEquals(12_915_892L, EnglishAuxResources.artifacts.sumOf { it.bytes })
    }

    @Test fun eachArtifactHasItsOwnPublicVersionedHttpsDownload() {
        val base = "https://github.com/SimDynamics/LearnGraphicsProgramming-Resources/releases/download/english-misaki-fp32-v1"
        EnglishAuxResources.artifacts.forEach {
            assertEquals("$base/${it.fileName}", it.url)
        }
        assertEquals(8, EnglishAuxResources.artifacts.map { it.url }.toSet().size)
    }

    @Test fun optionalResourcesDoNotDuplicateSharedKokoroOrVoices() {
        val shared = KokoroModelManifest.sharedArtifacts.map { it.fileName }.toSet()
        val voices = KokoroModelManifest.voiceArtifacts.map { it.fileName }.toSet()
        EnglishAuxResources.artifacts.forEach {
            assertFalse(it.fileName in shared)
            assertFalse(it.fileName in voices)
        }
    }
}

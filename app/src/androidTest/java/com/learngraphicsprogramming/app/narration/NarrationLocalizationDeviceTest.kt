package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.content.res.Configuration
import android.os.LocaleList
import android.speech.tts.Voice
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.Locale

@RunWith(AndroidJUnit4::class)
class NarrationLocalizationDeviceTest {
    private fun context(tag: String): Context {
        val app = ApplicationProvider.getApplicationContext<Context>()
        val configuration = Configuration(app.resources.configuration)
        configuration.setLocales(LocaleList(Locale.forLanguageTag(tag)))
        return app.createConfigurationContext(configuration)
    }

    @Test fun storedResourceProblemsRenderInTheCurrentLanguage() {
        val encoded = NarrationResourceMessages.encode(
            NarrationResourceProblem.IMPORT_INTERRUPTED, "model.onnx"
        )
        assertEquals("IMPORT_INTERRUPTED", JSONObject(encoded).getString("code"))
        assertFalse(encoded.contains("interrumpida"))
        val messages = listOf("es", "en", "fr").map { tag ->
            val localized = context(tag)
            val message = NarrationResourceMessages.render(localized, encoded)
            assertEquals(localized.getString(R.string.narration_resource_import_interrupted, "model.onnx"), message)
            assertTrue(message.orEmpty().contains("model.onnx"))
            message
        }
        assertEquals(3, messages.distinct().size)

        val numeric = NarrationResourceMessages.encode(
            NarrationResourceProblem.DOWNLOAD_FAILED, "voice.bin", 404
        )
        for (tag in listOf("es", "en", "fr")) {
            assertTrue(NarrationResourceMessages.render(context(tag), numeric).orEmpty().contains("404"))
        }
    }

    @Test fun legacyPersistedErrorsUseLocalizedControlledMessages() {
        for (tag in listOf("es", "en", "fr")) {
            val localized = context(tag)
            assertEquals(localized.getString(R.string.narration_resource_unknown),
                NarrationResourceMessages.render(localized, "Error anterior de la descarga"))
            assertEquals(null, NarrationResourceMessages.render(localized, null))
        }
    }

    @Test fun localizedVoiceDescriptionsKeepNarrationMetadataIndependent() {
        val voice = Voice("provider-voice-id", Locale.forLanguageTag("es-ES"),
            Voice.QUALITY_HIGH, Voice.LATENCY_NORMAL, false, emptySet())
        val labels = listOf("es", "en", "fr").map { tag ->
            val localized = context(tag)
            val described = SpanishVoices.describe(localized, voice)
            assertEquals("provider-voice-id", described.name)
            assertFalse(described.requiresNetwork)
            assertTrue(described.label.contains(localized.getString(R.string.narration_quality_high)))
            assertTrue(described.label.contains(localized.getString(R.string.narration_offline)))
            assertEquals(NarrationLanguage.AMERICAN_ENGLISH,
                NarrationLanguage.forLesson(JSONObject().put("language", "en-US"), 0, NarrationLanguage.FRENCH))
            NarrationLanguage.AMERICAN_ENGLISH.label(localized)
        }
        assertEquals(3, labels.distinct().size)
        assertEquals("en-US", NarrationLanguage.AMERICAN_ENGLISH.tag)
        assertEquals("af_heart", NarrationLanguage.AMERICAN_ENGLISH.defaultVoice)
        assertNotEquals(NarrationSettingsRepository.rateLabel(context("fr"), 0.5f),
            NarrationSettingsRepository.rateLabel(context("en"), 0.5f))
    }
}

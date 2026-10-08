package com.learngraphicsprogramming.app.narration

/** Synthesized resources and the implementation's language limits are separate states. */
object KokoroAvailability {
    val isAvailable: Boolean = true

    val reason get() = reasonFor(NarrationLanguage.SPANISH)

    fun reasonFor(language: NarrationLanguage, englishResourcesInstalled: Boolean = false): String = when (language) {
        NarrationLanguage.SPANISH -> "FluidAudio SpanishG2P. Síntesis FP32 y reproducción comprobadas en Android 14 ARM64; instala el modelo, el diccionario y una voz."
        NarrationLanguage.AMERICAN_ENGLISH, NarrationLanguage.BRITISH_ENGLISH ->
            if (englishResourcesInstalled) "Inglés estadounidense y británico con Misaki, análisis gramatical y pronunciación neuronal de palabras desconocidas. Instala también el modelo compartido, los diccionarios y una voz."
            else "Para leer en inglés con Kokoro, instala los diccionarios e importa los recursos neuronales de Misaki desde el ZIP verificado. Mientras falten recursos se utiliza Android TTS."
        NarrationLanguage.FRENCH ->
            "FluidAudio FrenchG2P con diccionario y reglas. Las palabras fuera del diccionario utilizan Android TTS; el modelo auxiliar Charsiu está pendiente de licencia y adaptación."
    }
}

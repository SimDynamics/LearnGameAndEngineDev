package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.speech.tts.TextToSpeech
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.learngraphicsprogramming.app.R
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts

/** Preferences and optional resources share the existing global options screen. */
class NarrationOptionsSection(private val activity: Activity) {
    private val host = activity.findViewById<LinearLayout>(R.id.narrationSettingsSection)
    private val catalog = AndroidVoiceCatalog(activity)
    private val models = KokoroModelStore(activity)
    private val importLauncher = (activity as ComponentActivity).registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null && !closed) {
            Toast.makeText(activity, "Comprobando e instalando recursos…", Toast.LENGTH_SHORT).show()
            models.importResources(uri) { error ->
                Toast.makeText(activity, error ?: "Recursos instalados y verificados", Toast.LENGTH_LONG).show()
                inspect()
            }
        }
    }
    private val handler = Handler(Looper.getMainLooper())
    private var active = false
    private var closed = false
    private var androidVoices = emptyList<SpanishVoice>()
    private var states = emptyMap<String, KokoroModelState>()
    private val engineButton = button("Motor") { chooseEngine() }
    private val languageButton = button("Idioma") { chooseLanguage() }
    private val voiceButton = button("Voz instalada") { chooseVoice() }
    private val voicesButton = button("Instalar o eliminar voces Kokoro") { manageVoices() }
    private val systemStatus = text("Comprobando voces de Android…", 15f)
    private val modelStatus = text("Comprobando modelo…", 16f)
    private val languageStatus = text("", 15f)
    private val compatibility = text("", 15f)
    private val auxiliaryStatus = text("", 15f)
    private val importButton = button("Importar recursos neuronales de inglés (ZIP)") {
        importLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed"))
    }
    private val removeAuxButton = button("Eliminar recursos neuronales de inglés") {
        confirmRemove("recursos neuronales de inglés", EnglishAuxResources.artifacts)
    }
    private val progress = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100; visibility = View.GONE }
    private val downloadButton = button("Descargar modelo compartido (311 MiB)") { confirmDownload("modelo compartido", KokoroModelManifest.sharedArtifacts) }
    private val cancelButton = button("Cancelar descargas") { models.cancelDownload(); inspect() }
    private val removeButton = button("Eliminar modelo compartido") { confirmRemove("modelo compartido", KokoroModelManifest.sharedArtifacts) }
    private val installLanguageButton = button("Instalar recursos del idioma") {
        val language = NarrationSettingsRepository.get(activity).language
        confirmDownload("recursos de ${language.label}", KokoroModelManifest.resources(language))
    }
    private val removeLanguageButton = button("Eliminar recursos del idioma") {
        val language = NarrationSettingsRepository.get(activity).language
        confirmRemove("recursos de ${language.label}", KokoroModelManifest.resources(language))
    }
    private val poll = object : Runnable {
        override fun run() {
            if (!active || closed) return
            inspect()
            handler.postDelayed(this, 2_000L)
        }
    }

    init {
        host.addView(text("Lectura en voz alta", 23f).apply { setTypeface(typeface, android.graphics.Typeface.BOLD) })
        host.addView(Switch(activity).apply {
            text = "Mostrar controles de lectura"
            setTextColor(Color.WHITE)
            minHeight = dp(48)
            isChecked = NarrationSettingsRepository.get(activity).enabled
            setOnCheckedChangeListener { _, enabled -> NarrationSettingsRepository.saveEnabled(activity, enabled) }
        })
        listOf(engineButton, languageButton, voiceButton, systemStatus,
            text("Kokoro completo FP32 · un modelo para todos los idiomas", 16f), modelStatus, progress,
            downloadButton, cancelButton, removeButton, languageStatus, installLanguageButton,
            removeLanguageButton, voicesButton, compatibility, auxiliaryStatus, importButton, removeAuxButton).forEach { host.addView(it) }
        host.addView(button("Configurar voces de Android") { openSystem(Intent("com.android.settings.TTS_SETTINGS")) })
        host.addView(button("Instalar datos de voz de Android") { openSystem(Intent(TextToSpeech.Engine.ACTION_INSTALL_TTS_DATA)) })
        host.addView(button("Licencias y atribuciones de lectura") { showLicenses() })
        refreshSelections()
    }

    fun onResume() {
        active = true
        refreshSelections()
        loadAndroidVoices()
        handler.removeCallbacks(poll)
        handler.post(poll)
    }

    fun onStop() { active = false; handler.removeCallbacks(poll) }
    fun close() { closed = true; onStop(); catalog.close(); models.close() }

    private fun loadAndroidVoices() {
        val language = NarrationSettingsRepository.get(activity).language
        androidVoices = emptyList()
        catalog.load({ available ->
            if (!closed && NarrationSettingsRepository.get(activity).language == language) {
                androidVoices = available
                systemStatus.text = if (available.isEmpty()) "No hay voces de ${language.label} instaladas en Android."
                    else "${available.size} voces de Android disponibles; se indica si requieren Internet."
                refreshSelections()
            }
        }, { message -> if (!closed) { systemStatus.text = message; refreshSelections() } }, language)
    }

    private fun refreshSelections() {
        val settings = NarrationSettingsRepository.get(activity)
        engineButton.text = when (settings.engine) {
            NarrationEngine.AUTO -> "Motor: automático"
            NarrationEngine.KOKORO -> "Motor: Kokoro · alternativa Android si falta algún recurso"
            NarrationEngine.ANDROID -> "Motor: Android TTS"
        }
        languageButton.text = "Idioma: ${settings.language.label}"
        voiceButton.text = if (settings.engine != NarrationEngine.ANDROID) "Voz Kokoro: ${settings.kokoroVoice} · ${stateLabel(states[settings.kokoroVoice])}"
            else "Voz Android: ${androidVoices.firstOrNull { it.name == settings.androidVoice }?.label ?: "automática"}"
        compatibility.text = compatibilityReason(settings.language)
        val model = states["model"]
        modelStatus.text = "Modelo compartido: ${stateLabel(model)}"
        progress.visibility = if (model?.status == KokoroModelStatus.DOWNLOADING) View.VISIBLE else View.GONE
        progress.progress = model?.percentage ?: 0
        cancelButton.visibility = if (states.values.any { it.status == KokoroModelStatus.DOWNLOADING }) View.VISIBLE else View.GONE
        downloadButton.isEnabled = model?.status !in listOf(KokoroModelStatus.DOWNLOADING, KokoroModelStatus.INSTALLED)
        downloadButton.text = if (model?.status == KokoroModelStatus.ERROR) "Reintentar modelo compartido (311 MiB)" else "Descargar modelo compartido (311 MiB)"
        removeButton.isEnabled = hasFiles(model)
        val language = states[settings.language.resourceGroup]
        languageStatus.text = "Diccionario de ${settings.language.label}: ${stateLabel(language)}"
        installLanguageButton.isEnabled = language?.status !in listOf(KokoroModelStatus.DOWNLOADING, KokoroModelStatus.INSTALLED)
        installLanguageButton.text = "Instalar recursos de ${settings.language.label} (${size(KokoroModelManifest.resources(settings.language).sumOf { it.bytes })})"
        removeLanguageButton.isEnabled = hasFiles(language)
        val english = settings.language.resourceGroup == "en"
        listOf(auxiliaryStatus, importButton, removeAuxButton).forEach { it.visibility = if (english) View.VISIBLE else View.GONE }
        auxiliaryStatus.text = "Recursos neuronales Misaki: ${stateLabel(states["en_neural"])}. Importa el ZIP de recursos verificados para completar la lectura inglesa sin conexión."
        importButton.isEnabled = EnglishAuxResources.artifacts.isNotEmpty()
        removeAuxButton.isEnabled = hasFiles(states["en_neural"])
    }

    private fun hasFiles(state: KokoroModelState?) = state != null && (state.downloadedBytes > 0 || state.status != KokoroModelStatus.NOT_INSTALLED)

    private fun compatibilityReason(language: NarrationLanguage) = KokoroAvailability.reasonFor(
        language, states["en_neural"]?.status == KokoroModelStatus.INSTALLED
    )

    private fun stateLabel(state: KokoroModelState?): String = when (state?.status) {
        null -> "comprobando…"
        KokoroModelStatus.NOT_INSTALLED -> "sin instalar"
        KokoroModelStatus.DOWNLOADING -> "descargando ${state.percentage}%"
        KokoroModelStatus.INSTALLED -> "instalado y verificado"
        KokoroModelStatus.ERROR -> "error: ${state.error ?: "descarga interrumpida"}"
        KokoroModelStatus.UPDATE_AVAILABLE -> "actualización disponible"
    }

    private fun chooseEngine() {
        val engines = NarrationEngine.entries.toTypedArray()
        AlertDialog.Builder(activity).setTitle("Motor de voz")
            .setSingleChoiceItems(arrayOf("Automático", "Kokoro neuronal", "Android TTS"), engines.indexOf(NarrationSettingsRepository.get(activity).engine)) { dialog, index ->
                NarrationSettingsRepository.saveEngine(activity, engines[index]); dialog.dismiss(); refreshSelections()
            }.setNegativeButton("Cancelar", null).show()
    }

    private fun chooseLanguage() {
        val choices = NarrationLanguage.entries
        AlertDialog.Builder(activity).setTitle("Idioma de lectura")
            .setSingleChoiceItems(choices.map { it.label }.toTypedArray(), choices.indexOf(NarrationSettingsRepository.get(activity).language)) { dialog, index ->
                NarrationSettingsRepository.saveLanguage(activity, choices[index]); dialog.dismiss(); refreshSelections(); loadAndroidVoices()
            }.setNegativeButton("Cancelar", null).show()
    }

    private fun chooseVoice() {
        val settings = NarrationSettingsRepository.get(activity)
        if (settings.engine != NarrationEngine.ANDROID) {
            val choices = KokoroVoiceCatalog.forLanguage(settings.language).filter { states[it.id]?.status == KokoroModelStatus.INSTALLED }
            if (choices.isEmpty()) { manageVoices(); return }
            AlertDialog.Builder(activity).setTitle("Voces Kokoro instaladas")
                .setSingleChoiceItems(choices.map { "${it.label} · ${it.id}" }.toTypedArray(), choices.indexOfFirst { it.id == settings.kokoroVoice }) { dialog, index ->
                    NarrationSettingsRepository.saveKokoroVoice(activity, choices[index].id); dialog.dismiss(); refreshSelections()
                }.setNegativeButton("Cancelar", null).show()
            return
        }
        val labels = arrayOf("Automática · prioriza voz sin Internet") + androidVoices.map { it.label }
        AlertDialog.Builder(activity).setTitle("Voz de ${settings.language.label} en Android")
            .setSingleChoiceItems(labels, androidVoices.indexOfFirst { it.name == settings.androidVoice } + 1) { dialog, index ->
                NarrationSettingsRepository.saveAndroidVoice(activity, if (index == 0) null else androidVoices[index - 1].name)
                dialog.dismiss(); refreshSelections()
            }.setNegativeButton("Cancelar", null).show()
    }

    private fun manageVoices() {
        val language = NarrationSettingsRepository.get(activity).language
        val choices = KokoroVoiceCatalog.forLanguage(language)
        AlertDialog.Builder(activity).setTitle("Voces Kokoro · ${language.label}")
            .setItems(choices.map { "${it.label} (${it.id}) · ${stateLabel(states[it.id])}" }.toTypedArray()) { _, index ->
                val voice = choices[index]
                val installed = states[voice.id]?.status == KokoroModelStatus.INSTALLED
                AlertDialog.Builder(activity).setTitle("${voice.label} · ${voice.id}")
                    .setMessage("${stateLabel(states[voice.id])}\n522 KB. Utiliza el modelo compartido y el diccionario del idioma.\n${compatibilityReason(language)}")
                    .setPositiveButton(if (installed) "Seleccionar" else "Descargar") { _, _ ->
                        if (installed) { NarrationSettingsRepository.saveKokoroVoice(activity, voice.id); refreshSelections() }
                        else confirmDownload("voz ${voice.id}", listOf(voice.artifact))
                    }
                    .setNeutralButton("Eliminar") { _, _ -> confirmRemove("voz ${voice.id}", listOf(voice.artifact)) }
                    .setNegativeButton("Cerrar", null).show()
            }.setNegativeButton("Cerrar", null).show()
    }

    private fun inspect() = models.inspectResources { snapshots ->
        if (!closed && active) { states = snapshots; refreshSelections() }
    }

    private fun confirmDownload(label: String, artifacts: List<KokoroModelManifest.Artifact>) {
        if (artifacts.isEmpty()) return
        AlertDialog.Builder(activity).setTitle("Descargar $label")
            .setMessage("Se descargarán ${size(artifacts.sumOf { it.bytes })}. Los archivos ya verificados se reutilizan. Puedes cancelar y eliminar estos recursos.\n\n${compatibilityReason(NarrationSettingsRepository.get(activity).language)}")
            .setNegativeButton("Cancelar", null)
            .setPositiveButton("Descargar") { _, _ -> models.startDownload(true, artifacts); inspect() }.show()
    }

    private fun confirmRemove(label: String, artifacts: List<KokoroModelManifest.Artifact>) {
        AlertDialog.Builder(activity).setTitle("Eliminar $label")
            .setMessage("Se borrarán únicamente estos recursos del dispositivo. Android TTS seguirá disponible.")
            .setNegativeButton("Cancelar", null)
            .setPositiveButton("Eliminar") { _, _ -> models.remove(artifacts); inspect() }.show()
    }

    private fun openSystem(intent: Intent) {
        try { activity.startActivity(intent) }
        catch (_: ActivityNotFoundException) { Toast.makeText(activity, "No se encontró la configuración de voz del sistema", Toast.LENGTH_LONG).show() }
    }

    private fun showLicenses() {
        val resources = listOf(
            "Kokoro y voces" to R.raw.kokoro_model_notice,
            "Apache License 2.0" to R.raw.french_fluidaudio_license,
            "Voz francesa Siwis" to R.raw.french_siwis_attribution,
            "FluidAudio · recursos franceses" to R.raw.french_fluid_resources_notices,
            "Misaki · modelos BART de inglés" to R.raw.misaki_bart_attribution,
            "spaCy · atribución del análisis gramatical" to R.raw.spacy_pos_attribution,
            "spaCy · MIT" to R.raw.spacy_pos_spacy_license,
            "Thinc · MIT" to R.raw.spacy_pos_thinc_license,
            "Modelo de spaCy · MIT" to R.raw.spacy_pos_model_license,
            "ONNX Runtime · MIT" to R.raw.onnxruntime_license,
            "ONNX Runtime · componentes" to R.raw.onnxruntime_third_party_notices,
            "ONNX Runtime · fuentes y avisos adicionales" to R.raw.onnxruntime_source_information
        )
        val assets = listOf("FluidAudio · español" to "licenses/FluidAudio-SpanishG2P.txt",
            "Misaki · inglés" to "narration/misaki/NOTICE.txt", "Misaki · Apache 2.0" to "narration/misaki/LICENSE.txt")
        val labels = resources.map { it.first } + assets.map { it.first }
        AlertDialog.Builder(activity).setTitle("Licencias y atribuciones")
            .setItems(labels.toTypedArray()) { _, index ->
                val pending = AlertDialog.Builder(activity).setTitle(labels[index]).setMessage("Cargando…").setPositiveButton("Cerrar", null).show()
                Thread({
                    val contents = runCatching {
                        if (index < resources.size) activity.resources.openRawResource(resources[index].second).bufferedReader().use { it.readText() }
                        else activity.assets.open(assets[index - resources.size].second).bufferedReader().use { it.readText() }
                    }.getOrDefault("No se pudo abrir el aviso de licencia.")
                    handler.post { if (!closed && pending.isShowing && !activity.isDestroyed) pending.setMessage(contents) }
                }, "NarrationLicenses").start()
            }.setNegativeButton("Cerrar", null).show()
    }
    private fun size(bytes: Long) = String.format(java.util.Locale.ROOT, "%.1f MiB", bytes / 1_048_576.0)
    private fun text(value: String, size: Float) = TextView(activity).apply {
        text = value; textSize = size; setTextColor(Color.rgb(210, 214, 220)); setPadding(0, dp(8), 0, dp(8))
    }
    private fun button(label: String, click: () -> Unit) = Button(activity).apply {
        text = label; isAllCaps = false; minHeight = dp(48); setOnClickListener { click() }
    }
    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()
}

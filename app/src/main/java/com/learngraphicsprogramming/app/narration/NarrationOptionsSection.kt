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
    private fun s(id: Int, vararg args: Any): String = activity.getString(id, *args)
    private val host = activity.findViewById<LinearLayout>(R.id.narrationSettingsSection)
    private val catalog = AndroidVoiceCatalog(activity)
    private val models = KokoroModelStore(activity)
    private val importLauncher = (activity as ComponentActivity).registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null && !closed) {
            Toast.makeText(activity, s(R.string.narration_checking_install), Toast.LENGTH_SHORT).show()
            models.importResources(uri) { error ->
                Toast.makeText(activity, error ?: s(R.string.narration_installed_verified), Toast.LENGTH_LONG).show()
                inspect()
            }
        }
    }
    private val handler = Handler(Looper.getMainLooper())
    private var active = false
    private var closed = false
    private var androidVoices = emptyList<SpanishVoice>()
    private var states = emptyMap<String, KokoroModelState>()
    private val engineButton = button(s(R.string.narration_engine)) { chooseEngine() }
    private val languageButton = button(s(R.string.narration_language)) { chooseLanguage() }
    private val voiceButton = button(s(R.string.narration_installed_voice)) { chooseVoice() }
    private val voicesButton = button(s(R.string.narration_manage_voices)) { manageVoices() }
    private val systemStatus = text(s(R.string.narration_checking_android), 15f)
    private val modelStatus = text(s(R.string.narration_checking_model), 16f)
    private val languageStatus = text("", 15f)
    private val compatibility = text("", 15f)
    private val auxiliaryStatus = text("", 15f)
    private val importButton = button(s(R.string.narration_import_english)) {
        importLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed"))
    }
    private val removeAuxButton = button(s(R.string.narration_remove_english)) {
        confirmRemove(s(R.string.narration_english_resources), EnglishAuxResources.artifacts)
    }
    private val progress = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100; visibility = View.GONE }
    private val downloadButton = button(s(R.string.narration_download_shared)) { confirmDownload(s(R.string.narration_shared), KokoroModelManifest.sharedArtifacts) }
    private val cancelButton = button(s(R.string.narration_cancel_downloads)) { models.cancelDownload(); inspect() }
    private val removeButton = button(s(R.string.narration_remove_shared)) { confirmRemove(s(R.string.narration_shared), KokoroModelManifest.sharedArtifacts) }
    private val installLanguageButton = button(s(R.string.narration_install_language)) {
        val language = NarrationSettingsRepository.get(activity).language
        confirmDownload(s(R.string.narration_language_resources, language.label(activity)), KokoroModelManifest.resources(language))
    }
    private val removeLanguageButton = button(s(R.string.narration_remove_language)) {
        val language = NarrationSettingsRepository.get(activity).language
        confirmRemove(s(R.string.narration_language_resources, language.label(activity)), KokoroModelManifest.resources(language))
    }
    private val poll = object : Runnable {
        override fun run() {
            if (!active || closed) return
            inspect()
            handler.postDelayed(this, 2_000L)
        }
    }

    init {
        host.addView(text(s(R.string.narration_title), 23f).apply { setTypeface(typeface, android.graphics.Typeface.BOLD) })
        host.addView(Switch(activity).apply {
            text = s(R.string.narration_show_controls)
            setTextColor(Color.WHITE)
            minHeight = dp(48)
            isChecked = NarrationSettingsRepository.get(activity).enabled
            setOnCheckedChangeListener { _, enabled -> NarrationSettingsRepository.saveEnabled(activity, enabled) }
        })
        listOf(engineButton, languageButton, voiceButton, systemStatus,
            text(s(R.string.narration_model_description), 16f), modelStatus, progress,
            downloadButton, cancelButton, removeButton, languageStatus, installLanguageButton,
            removeLanguageButton, voicesButton, compatibility, auxiliaryStatus, importButton, removeAuxButton).forEach { host.addView(it) }
        host.addView(button(s(R.string.narration_configure_android)) { openSystem(Intent("com.android.settings.TTS_SETTINGS")) })
        host.addView(button(s(R.string.narration_install_android)) { openSystem(Intent(TextToSpeech.Engine.ACTION_INSTALL_TTS_DATA)) })
        host.addView(button(s(R.string.narration_licenses_reading)) { showLicenses() })
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
                systemStatus.text = if (available.isEmpty()) s(R.string.narration_no_android_voices, language.label(activity))
                    else activity.resources.getQuantityString(R.plurals.narration_android_voices, available.size, available.size)
                refreshSelections()
            }
        }, { message -> if (!closed) { systemStatus.text = message; refreshSelections() } }, language)
    }

    private fun refreshSelections() {
        val settings = NarrationSettingsRepository.get(activity)
        engineButton.text = when (settings.engine) {
            NarrationEngine.AUTO -> s(R.string.narration_engine_auto)
            NarrationEngine.KOKORO -> s(R.string.narration_engine_kokoro)
            NarrationEngine.ANDROID -> s(R.string.narration_engine_android)
        }
        languageButton.text = s(R.string.narration_selected_language, settings.language.label(activity))
        voiceButton.text = if (settings.engine != NarrationEngine.ANDROID) s(R.string.narration_selected_kokoro, settings.kokoroVoice, stateLabel(states[settings.kokoroVoice]))
            else s(R.string.narration_selected_android, androidVoices.firstOrNull { it.name == settings.androidVoice }?.label ?: s(R.string.narration_auto_voice))
        compatibility.text = compatibilityReason(settings.language)
        val model = states["model"]
        modelStatus.text = s(R.string.narration_shared_status, stateLabel(model))
        progress.visibility = if (model?.status == KokoroModelStatus.DOWNLOADING) View.VISIBLE else View.GONE
        progress.progress = model?.percentage ?: 0
        cancelButton.visibility = if (states.values.any { it.status == KokoroModelStatus.DOWNLOADING }) View.VISIBLE else View.GONE
        downloadButton.isEnabled = model?.status !in listOf(KokoroModelStatus.DOWNLOADING, KokoroModelStatus.INSTALLED)
        downloadButton.text = if (model?.status == KokoroModelStatus.ERROR) s(R.string.narration_retry_shared) else s(R.string.narration_download_shared)
        removeButton.isEnabled = hasFiles(model)
        val language = states[settings.language.resourceGroup]
        languageStatus.text = s(R.string.narration_dictionary_status, settings.language.label(activity), stateLabel(language))
        installLanguageButton.isEnabled = language?.status !in listOf(KokoroModelStatus.DOWNLOADING, KokoroModelStatus.INSTALLED)
        installLanguageButton.text = s(R.string.narration_install_language_size, settings.language.label(activity), size(KokoroModelManifest.resources(settings.language).sumOf { it.bytes }))
        removeLanguageButton.isEnabled = hasFiles(language)
        val english = settings.language.resourceGroup == "en"
        listOf(auxiliaryStatus, importButton, removeAuxButton).forEach { it.visibility = if (english) View.VISIBLE else View.GONE }
        auxiliaryStatus.text = s(R.string.narration_aux_status, stateLabel(states["en_neural"]))
        importButton.isEnabled = EnglishAuxResources.artifacts.isNotEmpty()
        removeAuxButton.isEnabled = hasFiles(states["en_neural"])
    }

    private fun hasFiles(state: KokoroModelState?) = state != null && (state.downloadedBytes > 0 || state.status != KokoroModelStatus.NOT_INSTALLED)

    private fun compatibilityReason(language: NarrationLanguage) = KokoroAvailability.reasonFor(
        activity, language, states["en_neural"]?.status == KokoroModelStatus.INSTALLED
    )

    private fun stateLabel(state: KokoroModelState?): String = when (state?.status) {
        null -> s(R.string.narration_checking)
        KokoroModelStatus.NOT_INSTALLED -> s(R.string.narration_not_installed)
        KokoroModelStatus.DOWNLOADING -> s(R.string.narration_downloading, state.percentage)
        KokoroModelStatus.INSTALLED -> s(R.string.narration_verified)
        KokoroModelStatus.ERROR -> s(R.string.narration_state_error, state.error ?: s(R.string.narration_interrupted))
        KokoroModelStatus.UPDATE_AVAILABLE -> s(R.string.narration_update_available)
    }

    private fun chooseEngine() {
        val engines = NarrationEngine.entries.toTypedArray()
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_voice_engine))
            .setSingleChoiceItems(arrayOf(s(R.string.narration_automatic), s(R.string.narration_kokoro_neural), s(R.string.narration_android_tts)), engines.indexOf(NarrationSettingsRepository.get(activity).engine)) { dialog, index ->
                NarrationSettingsRepository.saveEngine(activity, engines[index]); dialog.dismiss(); refreshSelections()
            }.setNegativeButton(s(R.string.narration_cancel), null).show()
    }

    private fun chooseLanguage() {
        val choices = NarrationLanguage.entries
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_reading_language))
            .setSingleChoiceItems(choices.map { it.label(activity) }.toTypedArray(), choices.indexOf(NarrationSettingsRepository.get(activity).language)) { dialog, index ->
                NarrationSettingsRepository.saveLanguage(activity, choices[index]); dialog.dismiss(); refreshSelections(); loadAndroidVoices()
            }.setNegativeButton(s(R.string.narration_cancel), null).show()
    }

    private fun chooseVoice() {
        val settings = NarrationSettingsRepository.get(activity)
        if (settings.engine != NarrationEngine.ANDROID) {
            val choices = KokoroVoiceCatalog.forLanguage(settings.language).filter { states[it.id]?.status == KokoroModelStatus.INSTALLED }
            if (choices.isEmpty()) { manageVoices(); return }
            AlertDialog.Builder(activity).setTitle(s(R.string.narration_installed_kokoro))
                .setSingleChoiceItems(choices.map { s(R.string.narration_voice_name_id, it.label, it.id) }.toTypedArray(), choices.indexOfFirst { it.id == settings.kokoroVoice }) { dialog, index ->
                    NarrationSettingsRepository.saveKokoroVoice(activity, choices[index].id); dialog.dismiss(); refreshSelections()
                }.setNegativeButton(s(R.string.narration_cancel), null).show()
            return
        }
        val labels = arrayOf(s(R.string.narration_automatic_offline)) + androidVoices.map { it.label }
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_android_voice_language, settings.language.label(activity)))
            .setSingleChoiceItems(labels, androidVoices.indexOfFirst { it.name == settings.androidVoice } + 1) { dialog, index ->
                NarrationSettingsRepository.saveAndroidVoice(activity, if (index == 0) null else androidVoices[index - 1].name)
                dialog.dismiss(); refreshSelections()
            }.setNegativeButton(s(R.string.narration_cancel), null).show()
    }

    private fun manageVoices() {
        val language = NarrationSettingsRepository.get(activity).language
        val choices = KokoroVoiceCatalog.forLanguage(language)
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_kokoro_language, language.label(activity)))
            .setItems(choices.map { s(R.string.narration_voice_name_status, it.label, it.id, stateLabel(states[it.id])) }.toTypedArray()) { _, index ->
                val voice = choices[index]
                val installed = states[voice.id]?.status == KokoroModelStatus.INSTALLED
                AlertDialog.Builder(activity).setTitle(s(R.string.narration_voice_name_id, voice.label, voice.id))
                    .setMessage(s(R.string.narration_voice_details, stateLabel(states[voice.id]), compatibilityReason(language)))
                    .setPositiveButton(if (installed) s(R.string.narration_select) else s(R.string.narration_download)) { _, _ ->
                        if (installed) { NarrationSettingsRepository.saveKokoroVoice(activity, voice.id); refreshSelections() }
                        else confirmDownload(s(R.string.narration_voice_resource, voice.id), listOf(voice.artifact))
                    }
                    .setNeutralButton(s(R.string.narration_remove)) { _, _ -> confirmRemove(s(R.string.narration_voice_resource, voice.id), listOf(voice.artifact)) }
                    .setNegativeButton(s(R.string.narration_close), null).show()
            }.setNegativeButton(s(R.string.narration_close), null).show()
    }

    private fun inspect() = models.inspectResources { snapshots ->
        if (!closed && active) { states = snapshots; refreshSelections() }
    }

    private fun confirmDownload(label: String, artifacts: List<KokoroModelManifest.Artifact>) {
        if (artifacts.isEmpty()) return
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_download_title, label))
            .setMessage(s(R.string.narration_download_confirmation, size(artifacts.sumOf { it.bytes }), compatibilityReason(NarrationSettingsRepository.get(activity).language)))
            .setNegativeButton(s(R.string.narration_cancel), null)
            .setPositiveButton(s(R.string.narration_download)) { _, _ -> models.startDownload(true, artifacts); inspect() }.show()
    }

    private fun confirmRemove(label: String, artifacts: List<KokoroModelManifest.Artifact>) {
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_remove_title, label))
            .setMessage(s(R.string.narration_remove_confirmation))
            .setNegativeButton(s(R.string.narration_cancel), null)
            .setPositiveButton(s(R.string.narration_remove)) { _, _ -> models.remove(artifacts); inspect() }.show()
    }

    private fun openSystem(intent: Intent) {
        try { activity.startActivity(intent) }
        catch (_: ActivityNotFoundException) { Toast.makeText(activity, s(R.string.narration_system_settings_missing), Toast.LENGTH_LONG).show() }
    }

    private fun showLicenses() {
        val resources = listOf(
            s(R.string.narration_license_kokoro) to R.raw.kokoro_model_notice,
            s(R.string.narration_license_apache) to R.raw.french_fluidaudio_license,
            s(R.string.narration_license_siwis) to R.raw.french_siwis_attribution,
            s(R.string.narration_license_french_resources) to R.raw.french_fluid_resources_notices,
            s(R.string.narration_license_bart) to R.raw.misaki_bart_attribution,
            s(R.string.narration_license_spacy_pos) to R.raw.spacy_pos_attribution,
            s(R.string.narration_license_spacy) to R.raw.spacy_pos_spacy_license,
            s(R.string.narration_license_thinc) to R.raw.spacy_pos_thinc_license,
            s(R.string.narration_license_spacy_model) to R.raw.spacy_pos_model_license,
            s(R.string.narration_license_onnx) to R.raw.onnxruntime_license,
            s(R.string.narration_license_onnx_components) to R.raw.onnxruntime_third_party_notices,
            s(R.string.narration_license_onnx_sources) to R.raw.onnxruntime_source_information
        )
        val assets = listOf(s(R.string.narration_license_spanish) to "licenses/FluidAudio-SpanishG2P.txt",
            s(R.string.narration_license_english) to "narration/misaki/NOTICE.txt", s(R.string.narration_license_misaki) to "narration/misaki/LICENSE.txt")
        val labels = resources.map { it.first } + assets.map { it.first }
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_licenses))
            .setItems(labels.toTypedArray()) { _, index ->
                val pending = AlertDialog.Builder(activity).setTitle(labels[index]).setMessage(s(R.string.narration_loading)).setPositiveButton(s(R.string.narration_close), null).show()
                Thread({
                    val contents = runCatching {
                        if (index < resources.size) activity.resources.openRawResource(resources[index].second).bufferedReader().use { it.readText() }
                        else activity.assets.open(assets[index - resources.size].second).bufferedReader().use { it.readText() }
                    }.getOrDefault(s(R.string.narration_license_open_error))
                    handler.post { if (!closed && pending.isShowing && !activity.isDestroyed) pending.setMessage(contents) }
                }, "NarrationLicenses").start()
            }.setNegativeButton(s(R.string.narration_close), null).show()
    }
    private fun size(bytes: Long) = s(R.string.narration_size_mib, bytes / 1_048_576.0)
    private fun text(value: String, size: Float) = TextView(activity).apply {
        text = value; textSize = size; setTextColor(Color.rgb(210, 214, 220)); setPadding(0, dp(8), 0, dp(8))
    }
    private fun button(label: String, click: () -> Unit) = Button(activity).apply {
        text = label; isAllCaps = false; minHeight = dp(48); setOnClickListener { click() }
    }
    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()
}

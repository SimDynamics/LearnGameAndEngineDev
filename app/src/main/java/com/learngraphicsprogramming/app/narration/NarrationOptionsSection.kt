package com.learngraphicsprogramming.app.narration

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.DialogInterface
import android.content.res.ColorStateList
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.speech.tts.TextToSpeech
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.AppearanceSettingsRepository

/** Preferences and optional resources share the existing global options screen. */
class NarrationOptionsSection(private val activity: Activity) {
    private fun s(id: Int, vararg args: Any): String = activity.getString(id, *args)
    private val host = activity.findViewById<LinearLayout>(R.id.narrationSettingsSection)
    private var catalog: AndroidVoiceCatalog? = null
    private val models = KokoroModelStore(activity)
    private val handler = Handler(Looper.getMainLooper())
    private var active = false
    private var closed = false
    private val availability = NarrationControlAvailability()
    private val masterSwitch = Switch(activity)
    private val advanceSwitch = Switch(activity).apply {
        text = s(R.string.narration_advance_generation)
        setTextColor(Color.WHITE)
        minHeight = dp(48)
        isChecked = NarrationSettingsRepository.advanceGenerationEnabled(activity)
        setOnCheckedChangeListener { _, enabled ->
            if (!bindingAdvance && !closed) {
                NarrationSettingsRepository.saveAdvanceGenerationEnabled(activity, enabled)
                KokoroBackgroundNarrationWorker.start(activity.applicationContext)
            }
        }
    }
    private var bindingAdvance = false
    private var bindingMaster = false
    private var openDialog: AlertDialog? = null
    private val preferences = activity.applicationContext.getSharedPreferences(
        "lesson_narration_settings", Activity.MODE_PRIVATE
    )
    private val settingsListener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        handler.post {
            if (closed) return@post
            refreshSelections()
            if (!narrationEnabled()) {
                openDialog?.dismiss()
                catalog?.close()
                catalog = null
                storageDragging = false
            } else if (active && key in setOf("enabled", "language", "engine")) {
                loadAndroidVoices()
                inspect()
            }
        }
    }
    private var androidVoices = emptyList<SpanishVoice>()
    private var states = emptyMap<String, KokoroModelState>()
    private val engineButton = button(s(R.string.narration_engine)) { chooseEngine() }
    private val languageButton = button(s(R.string.narration_language)) { chooseLanguage() }
    private val voiceButton = button(s(R.string.narration_installed_voice)) { chooseVoice() }
    private val voicesButton = button(s(R.string.narration_manage_voices)) { manageVoices() }
    private val storageTitle = text(s(R.string.narration_storage_title), 17f).apply {
        setTypeface(typeface, android.graphics.Typeface.BOLD)
    }
    private val storageDetail = text(s(R.string.narration_storage_checking), 15f)
    private val storageSlider = SeekBar(activity).apply { max = 0 }
    private val storageMaximum = text("", 14f)
    private val storageExplanation = text(s(R.string.narration_storage_explanation), 14f)
    private var storageSnapshot: NarrationStorageBudget.Snapshot? = null
    private var storageRefreshRunning = false
    private var storageRefreshRequested = false
    private var storageDragging = false
    private val storageStepBytes = NarrationStorageBudget.MIB
    // A dismissed dialog or a closed settings screen cancels expensive scanning.
    @Volatile private var courseAudioReportRevision = 0L
    private val programmingAudiosButton = button(s(R.string.narration_course_audio_programming)) {
        manageCourseAudio(AppearanceSettingsRepository.ROUTE_PROGRAMMING)
    }
    private val mathAudiosButton = button(s(R.string.narration_course_audio_math)) {
        manageCourseAudio(AppearanceSettingsRepository.ROUTE_MATH_PHYSICS)
    }
    private val systemStatus = text(s(R.string.narration_checking_android), 15f)
    private val modelStatus = text(s(R.string.narration_checking_model), 16f)
    private val languageStatus = text("", 15f)
    private val compatibility = text("", 15f)
    private val auxiliaryStatus = text("", 15f)
    private val installEnglishButton = button(s(R.string.narration_install_english)) {
        confirmDownload(s(R.string.narration_english_resources), EnglishAuxResources.artifacts)
    }
    private val englishProgress = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply {
        max = 100; visibility = View.GONE
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
            if (narrationEnabled()) inspect()
            handler.postDelayed(this, 2_000L)
        }
    }

    init {

        // Shared controls apply to both Kokoro and Android TTS.
        host.addView(sectionHeading(R.string.narration_section_general))
        host.addView(masterSwitch.apply {
            text = s(R.string.narration_show_controls)
            setTextColor(Color.WHITE)
            minHeight = dp(48)
            isChecked = NarrationSettingsRepository.get(activity).enabled
            setOnCheckedChangeListener { _, enabled ->
                if (!bindingMaster) {
                    NarrationSettingsRepository.saveEnabled(activity, enabled)
                    refreshSelections()
                }
            }
        })
        listOf(engineButton, languageButton, voiceButton).forEach { host.addView(it) }

        // Kokoro resources and storage remain together.
        host.addView(sectionHeading(R.string.narration_section_kokoro))
        listOf(voicesButton, text(s(R.string.narration_model_description), 16f),
            modelStatus, progress, downloadButton, cancelButton, removeButton,
            languageStatus, installLanguageButton, removeLanguageButton,
            storageTitle, storageDetail, storageSlider, storageMaximum,
            storageExplanation, advanceSwitch,
            text(s(R.string.narration_course_audio_manage), 16f),
            programmingAudiosButton, mathAudiosButton).forEach { host.addView(it) }
        storageSlider.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onStartTrackingTouch(seekBar: SeekBar) { storageDragging = true }
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) displayStorageLimit(progress.toLong() * storageStepBytes)
            }
            override fun onStopTrackingTouch(seekBar: SeekBar) {
                storageDragging = false
                if (!narrationEnabled()) return
                NarrationStorageBudget.saveLimit(activity, seekBar.progress.toLong() * storageStepBytes)
                // A previously paused queue may continue after raising the limit.
                KokoroBackgroundNarrationWorker.start(activity.applicationContext)
                refreshStorage()
            }
        })

        host.addView(sectionHeading(R.string.narration_section_kokoro_status))
        listOf(compatibility, auxiliaryStatus, englishProgress, installEnglishButton, removeAuxButton).forEach { host.addView(it) }

        host.addView(sectionHeading(R.string.narration_section_android))
        host.addView(systemStatus)
        host.addView(button(s(R.string.narration_configure_android)) { openSystem(Intent("com.android.settings.TTS_SETTINGS")) })
        host.addView(button(s(R.string.narration_install_android)) { openSystem(Intent(TextToSpeech.Engine.ACTION_INSTALL_TTS_DATA)) })

        host.addView(sectionHeading(R.string.narration_section_licenses))
        host.addView(button(s(R.string.narration_licenses_reading)) { showLicenses() })
        preferences.registerOnSharedPreferenceChangeListener(settingsListener)
        refreshSelections()
    }

    fun onResume() {
        active = true
        refreshSelections()
        if (narrationEnabled()) loadAndroidVoices()
        handler.removeCallbacks(poll)
        handler.post(poll)
        refreshStorage()
    }

    private fun refreshStorage() {
        if (closed) return
        if (storageRefreshRunning) {
            storageRefreshRequested = true
            return
        }
        storageRefreshRunning = true
        Thread({
            val snapshot = runCatching { NarrationStorageBudget.snapshot(activity.applicationContext) }.getOrNull()
            handler.post {
                storageRefreshRunning = false
                val refreshAgain = storageRefreshRequested
                storageRefreshRequested = false
                if (!closed && active && snapshot != null && !storageDragging) {
                    storageSnapshot = snapshot
                    storageSlider.max = (snapshot.maximumBytes / storageStepBytes)
                        .coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                    storageSlider.progress = (snapshot.effectiveBytes / storageStepBytes)
                        .coerceAtMost(storageSlider.max.toLong()).toInt()
                    displayStorageLimit(storageSlider.progress.toLong() * storageStepBytes)
                    storageMaximum.text = s(
                        R.string.narration_storage_maximum, formatGiB(snapshot.maximumBytes)
                    )
                }
                if (refreshAgain && !closed && active) refreshStorage()
            }
        }, "NarrationStorageSettings").start()
    }

    private fun displayStorageLimit(selectedBytes: Long) {
        val snapshot = storageSnapshot ?: return
        val limitText = if (selectedBytes == 0L) s(R.string.narration_storage_paused)
            else formatGiB(selectedBytes)
        storageDetail.text = s(
            R.string.narration_storage_usage,
            limitText,
            formatGiB(snapshot.storedBytes)
        ) + if (snapshot.storedBytes >= selectedBytes) {
            " " + s(R.string.narration_storage_over_limit)
        } else ""
    }

    private fun formatGiB(bytes: Long): String =
        s(R.string.narration_storage_gib, bytes.toDouble() / NarrationStorageBudget.RESERVE_BYTES)

    fun onStop() {
        active = false
        courseAudioReportRevision++
        handler.removeCallbacks(poll)
    }
    fun close() {
        closed = true
        onStop()
        preferences.unregisterOnSharedPreferenceChangeListener(settingsListener)
        openDialog?.dismiss()
        catalog?.close()
        catalog = null
        models.close()
    }

    private fun loadAndroidVoices() {
        if (!narrationEnabled() || closed) return
        val language = NarrationSettingsRepository.get(activity).language
        androidVoices = emptyList()
        val voiceCatalog = catalog ?: AndroidVoiceCatalog(activity).also { catalog = it }
        voiceCatalog.load({ available ->
            if (!closed && narrationEnabled() && NarrationSettingsRepository.get(activity).language == language) {
                androidVoices = available
                systemStatus.text = if (available.isEmpty()) s(R.string.narration_no_android_voices, language.label(activity))
                    else activity.resources.getQuantityString(R.plurals.narration_android_voices, available.size, available.size)
                refreshSelections()
            }
        }, { message -> if (!closed && narrationEnabled()) { systemStatus.text = message; refreshSelections() } }, language)
    }

    private fun refreshSelections() {
        val settings = NarrationSettingsRepository.get(activity)
        if (settings.enabled) availability.apply(host, true, masterSwitch)
        bindingMaster = true
        masterSwitch.isChecked = settings.enabled
        bindingAdvance = true
        advanceSwitch.isChecked = NarrationSettingsRepository.advanceGenerationEnabled(activity)
        bindingAdvance = false
        bindingMaster = false
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
        val auxiliary = states["en_neural"]
        listOf(auxiliaryStatus, installEnglishButton, removeAuxButton).forEach { it.visibility = if (english) View.VISIBLE else View.GONE }
        auxiliaryStatus.text = s(R.string.narration_aux_status, stateLabel(auxiliary))
        englishProgress.visibility = if (english && auxiliary?.status == KokoroModelStatus.DOWNLOADING) View.VISIBLE else View.GONE
        englishProgress.progress = auxiliary?.percentage ?: 0
        installEnglishButton.isEnabled = auxiliary?.status !in listOf(KokoroModelStatus.DOWNLOADING, KokoroModelStatus.INSTALLED)
        installEnglishButton.text = s(if (auxiliary?.status == KokoroModelStatus.ERROR)
            R.string.narration_retry_english else R.string.narration_install_english)
        removeAuxButton.isEnabled = hasFiles(auxiliary)
        if (!settings.enabled) availability.apply(host, false, masterSwitch)
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
        if (!narrationEnabled()) return
        val engines = NarrationEngine.entries.toTypedArray()
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_voice_engine))
            .setSingleChoiceItems(arrayOf(s(R.string.narration_automatic), s(R.string.narration_kokoro_neural), s(R.string.narration_android_tts)), engines.indexOf(NarrationSettingsRepository.get(activity).engine)) { dialog, index ->
                if (!narrationEnabled()) return@setSingleChoiceItems
                NarrationSettingsRepository.saveEngine(activity, engines[index]); dialog.dismiss(); refreshSelections()
            }.setNegativeButton(s(R.string.narration_cancel), null).showReaderDialog()
    }

    private fun chooseLanguage() {
        if (!narrationEnabled()) return
        val choices = NarrationLanguage.entries
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_reading_language))
            .setSingleChoiceItems(choices.map { it.label(activity) }.toTypedArray(), choices.indexOf(NarrationSettingsRepository.get(activity).language)) { dialog, index ->
                if (!narrationEnabled()) return@setSingleChoiceItems
                NarrationSettingsRepository.saveLanguage(activity, choices[index]); dialog.dismiss(); refreshSelections(); loadAndroidVoices()
            }.setNegativeButton(s(R.string.narration_cancel), null).showReaderDialog()
    }

    private fun chooseVoice() {
        if (!narrationEnabled()) return
        val settings = NarrationSettingsRepository.get(activity)
        if (settings.engine != NarrationEngine.ANDROID) {
            val choices = KokoroVoiceCatalog.forLanguage(settings.language).filter { states[it.id]?.status == KokoroModelStatus.INSTALLED }
            if (choices.isEmpty()) { manageVoices(); return }
            AlertDialog.Builder(activity).setTitle(s(R.string.narration_installed_kokoro))
                .setSingleChoiceItems(choices.map { s(R.string.narration_voice_name_id, it.label, it.id) }.toTypedArray(), choices.indexOfFirst { it.id == settings.kokoroVoice }) { dialog, index ->
                    if (!narrationEnabled()) return@setSingleChoiceItems
                    NarrationSettingsRepository.saveKokoroVoice(activity, choices[index].id); dialog.dismiss(); refreshSelections()
                }.setNegativeButton(s(R.string.narration_cancel), null).showReaderDialog()
            return
        }
        val labels = arrayOf(s(R.string.narration_automatic_offline)) + androidVoices.map { it.label }
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_android_voice_language, settings.language.label(activity)))
            .setSingleChoiceItems(labels, androidVoices.indexOfFirst { it.name == settings.androidVoice } + 1) { dialog, index ->
                if (!narrationEnabled()) return@setSingleChoiceItems
                NarrationSettingsRepository.saveAndroidVoice(activity, if (index == 0) null else androidVoices[index - 1].name)
                dialog.dismiss(); refreshSelections()
            }.setNegativeButton(s(R.string.narration_cancel), null).showReaderDialog()
    }

    /** Read-only course statistics, computed off the UI thread with no cache mutations. */
    private fun manageCourseAudio(routeId: String) {
        if (!narrationEnabled() || !active || routeId !in NarrationAudioPlan.ROUTES) return
        val revision = ++courseAudioReportRevision
        val waiting = AlertDialog.Builder(activity)
            .setTitle(courseAudioRoute(routeId))
            .setMessage(s(R.string.narration_course_audio_scanning))
            .setNegativeButton(s(R.string.narration_cancel), null)
            .showReaderDialog()
        waiting.setOnDismissListener {
            if (courseAudioReportRevision == revision) courseAudioReportRevision++
        }
        Thread({
            val outcome = runCatching {
                KokoroCourseAudioStats.scan(activity.applicationContext, routeFilter = routeId) {
                    revision != courseAudioReportRevision || Thread.currentThread().isInterrupted
                }
            }
            handler.post {
                if (closed || !active || revision != courseAudioReportRevision ||
                    !waiting.isShowing || openDialog !== waiting || activity.isDestroyed) return@post
                waiting.dismiss()
                outcome.onSuccess { report -> showCourseAudioList(routeId, report) }
                    .onFailure { error ->
                        android.util.Log.w("KokoroCourseAudio", "Could not read course audio coverage", error)
                        Toast.makeText(activity, s(R.string.narration_course_audio_error), Toast.LENGTH_LONG).show()
                    }
            }
        }, "KokoroCourseAudioInventory").start()
    }

    private fun courseAudioRoute(routeId: String): String =
        s(if (routeId == AppearanceSettingsRepository.ROUTE_PROGRAMMING)
            R.string.settings_programming_color_title else R.string.settings_math_color_title)

    private fun courseAudioPercent(value: Int?): String =
        value?.let { "$it%" } ?: s(R.string.narration_course_audio_unknown)

    private fun showCourseAudioList(routeId: String, report: KokoroCourseAudioStats.Report) {
        if (closed || !active || !narrationEnabled() || activity.isDestroyed) return
        val courses = report.courses.filter { it.routeId == routeId }
        val labels = courses.map { course ->
            val coverage = course.coverage
            s(R.string.narration_course_audio_row,
                course.number, course.title, coverage.generatedSentences,
                coverage.expectedSentences, courseAudioPercent(coverage.percent),
                coverage.files, size(coverage.bytes))
        }.ifEmpty { listOf(s(R.string.narration_course_audio_no_courses)) }.toTypedArray()
        AlertDialog.Builder(activity)
            .setTitle(courseAudioRoute(routeId))
            .setItems(labels) { _, index ->
                if (!narrationEnabled()) return@setItems
                courses.getOrNull(index)?.let(::showCourseAudioDetails)
            }
            .setPositiveButton(s(R.string.narration_course_audio_refresh)) { _, _ ->
                manageCourseAudio(routeId)
            }
            .setNeutralButton(s(R.string.narration_course_audio_info)) { _, _ ->
                AlertDialog.Builder(activity)
                    .setTitle(s(R.string.narration_course_audio_manage))
                    .setMessage(s(R.string.narration_course_audio_note))
                    .setPositiveButton(s(R.string.narration_close), null)
                    .showReaderDialog()
            }
            .setNegativeButton(s(R.string.narration_close), null)
            .showReaderDialog()
    }

    private fun showCourseAudioDetails(course: KokoroCourseAudioStats.Course) {
        if (!narrationEnabled()) return
        val coverage = course.coverage
        AlertDialog.Builder(activity)
            .setTitle(s(R.string.narration_course_audio_course_title,
                courseAudioRoute(course.routeId), course.number, course.title))
            .setMessage(s(R.string.narration_course_audio_details,
                coverage.generatedSentences, coverage.expectedSentences,
                courseAudioPercent(coverage.percent), coverage.files, size(coverage.bytes)))
            .setPositiveButton(s(R.string.narration_close), null)
            .showReaderDialog()
    }

    private fun manageVoices() {
        if (!narrationEnabled()) return
        val language = NarrationSettingsRepository.get(activity).language
        val choices = KokoroVoiceCatalog.forLanguage(language)
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_kokoro_language, language.label(activity)))
            .setItems(choices.map { s(R.string.narration_voice_name_status, it.label, it.id, stateLabel(states[it.id])) }.toTypedArray()) { _, index ->
                if (!narrationEnabled()) return@setItems
                val voice = choices[index]
                val installed = states[voice.id]?.status == KokoroModelStatus.INSTALLED
                AlertDialog.Builder(activity).setTitle(s(R.string.narration_voice_name_id, voice.label, voice.id))
                    .setMessage(s(R.string.narration_voice_details, stateLabel(states[voice.id]), compatibilityReason(language)))
                    .setPositiveButton(if (installed) s(R.string.narration_select) else s(R.string.narration_download)) { _, _ ->
                        if (!narrationEnabled()) return@setPositiveButton
                        if (installed) { NarrationSettingsRepository.saveKokoroVoice(activity, voice.id); refreshSelections() }
                        else confirmDownload(s(R.string.narration_voice_resource, voice.id), listOf(voice.artifact))
                    }
                    .setNeutralButton(s(R.string.narration_remove)) { _, _ -> confirmRemove(s(R.string.narration_voice_resource, voice.id), listOf(voice.artifact)) }
                    .setNegativeButton(s(R.string.narration_close), null).showReaderDialog()
            }.setNegativeButton(s(R.string.narration_close), null).showReaderDialog()
    }

    private fun inspect() = models.inspectResources { snapshots ->
        if (!closed && active) { states = snapshots; refreshSelections() }
    }

    private fun confirmDownload(label: String, artifacts: List<KokoroModelManifest.Artifact>) {
        if (!narrationEnabled() || artifacts.isEmpty()) return
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_download_title, label))
            .setMessage(s(R.string.narration_download_confirmation, size(artifacts.sumOf { it.bytes }), compatibilityReason(NarrationSettingsRepository.get(activity).language)))
            .setNegativeButton(s(R.string.narration_cancel), null)
            .setPositiveButton(s(R.string.narration_download)) { _, _ ->
                if (narrationEnabled()) { models.startDownload(true, artifacts); inspect() }
            }.showReaderDialog()
    }

    private fun confirmRemove(label: String, artifacts: List<KokoroModelManifest.Artifact>) {
        if (!narrationEnabled()) return
        AlertDialog.Builder(activity).setTitle(s(R.string.narration_remove_title, label))
            .setMessage(s(R.string.narration_remove_confirmation))
            .setNegativeButton(s(R.string.narration_cancel), null)
            .setPositiveButton(s(R.string.narration_remove)) { _, _ ->
                if (narrationEnabled()) { models.remove(artifacts); inspect() }
            }.showReaderDialog()
    }

    private fun openSystem(intent: Intent) {
        if (!narrationEnabled()) return
        try { activity.startActivity(intent) }
        catch (_: ActivityNotFoundException) { Toast.makeText(activity, s(R.string.narration_system_settings_missing), Toast.LENGTH_LONG).show() }
    }

    private fun showLicenses() {
        if (!narrationEnabled()) return
        val resources = listOf(
            s(R.string.narration_license_kokoro) to R.raw.kokoro_model_notice,
            s(R.string.narration_license_apache) to R.raw.french_fluidaudio_license,
            s(R.string.narration_license_siwis) to R.raw.french_siwis_attribution,
            s(R.string.narration_license_french_resources) to R.raw.french_fluid_resources_notices,
            s(R.string.narration_license_french_rules) to R.raw.french_rule_g2p_notice,
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
                if (!narrationEnabled()) return@setItems
                val pending = AlertDialog.Builder(activity).setTitle(labels[index]).setMessage(s(R.string.narration_loading)).setPositiveButton(s(R.string.narration_close), null).showReaderDialog()
                Thread({
                    val contents = runCatching {
                        if (index < resources.size) activity.resources.openRawResource(resources[index].second).bufferedReader().use { it.readText() }
                        else activity.assets.open(assets[index - resources.size].second).bufferedReader().use { it.readText() }
                    }.getOrDefault(s(R.string.narration_license_open_error))
                    handler.post { if (!closed && pending.isShowing && !activity.isDestroyed) pending.setMessage(contents) }
                }, "NarrationLicenses").start()
            }.setNegativeButton(s(R.string.narration_close), null).showReaderDialog()
    }
    private fun size(bytes: Long) = s(R.string.narration_size_mib, bytes / 1_048_576.0)
    private fun sectionHeading(titleRes: Int) = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
        val firstSection = titleRes == R.string.narration_section_general
        setPadding(0, if (firstSection) 0 else dp(14), 0, dp(6))
        if (!firstSection) {
            addView(View(activity).apply {
                setBackgroundColor(Color.rgb(76, 79, 87))
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1))
            })
        }
        addView(text(s(titleRes), 18f).apply {
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            setTextColor(Color.WHITE)
            setPadding(0, if (firstSection) 0 else dp(12), 0, dp(6))
        })
    }
    private fun text(value: String, size: Float) = TextView(activity).apply {
        text = value; textSize = size; setTextColor(Color.rgb(210, 214, 220)); setPadding(0, dp(8), 0, dp(8))
    }
    private fun button(label: String, click: () -> Unit) = Button(activity).apply {
        text = label; isAllCaps = false; minHeight = dp(48)
        setOnClickListener { if (narrationEnabled()) click() }
    }
    private fun narrationEnabled() = !closed && NarrationSettingsRepository.get(activity).enabled

    private fun AlertDialog.Builder.showReaderDialog(): AlertDialog =
        showWithNeutralActionText().also { openDialog = it }

    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()
}

/**
 * Native dialog buttons otherwise inherit the cyan/route-accent tint. Match the
 * dialog's own text colors instead, which adapt automatically to light/dark UI
 * and preserve disabled text states independently of user-selected accent hues.
 */
internal fun AlertDialog.Builder.showWithNeutralActionText(): AlertDialog {
    val dialog = show()
    val attrs = dialog.context.obtainStyledAttributes(intArrayOf(
        android.R.attr.textColorPrimary, android.R.attr.colorBackground
    ))
    val textColors = try {
        attrs.getColorStateList(0) ?: ColorStateList.valueOf(
            if (Color.luminance(attrs.getColor(1, Color.WHITE)) > 0.179) Color.BLACK else Color.WHITE
        )
    } finally {
        attrs.recycle()
    }
    for (button in intArrayOf(
        DialogInterface.BUTTON_POSITIVE, DialogInterface.BUTTON_NEUTRAL, DialogInterface.BUTTON_NEGATIVE
    )) {
        dialog.getButton(button)?.setTextColor(textColors)
    }
    return dialog
}

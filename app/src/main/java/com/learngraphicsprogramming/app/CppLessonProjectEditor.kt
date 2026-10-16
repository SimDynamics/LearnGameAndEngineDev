package com.learngraphicsprogramming.app

import android.content.Context
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import com.learngraphicsprogramming.app.runtime.CppProject
import org.json.JSONArray
import org.json.JSONObject

/** Owns the lesson's virtual files; the existing C++ editor still owns editing and its viewport. */
internal class CppLessonProjectEditor(
    private val context: Context,
    block: JSONObject,
    private val draftKey: String,
    private val editor: EditText,
    private val onFileChanged: () -> Unit
) {
    private val preferences = context.getSharedPreferences("cpp_lesson_drafts", Context.MODE_PRIVATE)
    private val initialFiles = linkedMapOf<String, String>()
    private val initialSources: List<String>
    private val files = linkedMapOf<String, String>()
    private val sources = linkedSetOf<String>()
    private val cursors = mutableMapOf<String, Int>()
    private val indentation = mutableMapOf<String, String>()
    private var selected = ""
    private var switching = false
    private val usesCmake = block.optJSONObject("cmake") != null
    val controls = LinearLayout(context).apply {
        tag = "cpp_project_controls"
        orientation = LinearLayout.VERTICAL
    }
    private val fileButtons = LinearLayout(context)
    private val compileFile = CheckBox(context).apply {
        tag = "cpp_source_enabled"
        text = context.getString(R.string.cpp_compile_selected_file)
    }

    init {
        val contentFiles = block.getJSONArray("projectFiles")
        for (index in 0 until contentFiles.length()) {
            val file = contentFiles.getJSONObject(index)
            val path = file.getString("path")
            require(!initialFiles.containsKey(path)) { "Duplicate project file: $path" }
            initialFiles[path] = file.getString("initialCode")
        }
        initialSources = block.optJSONArray("sourceFiles")?.let { selectedSources ->
            List(selectedSources.length()) { selectedSources.getString(it) }
        } ?: initialFiles.keys.filter(CppProject::isSourceFile)
        if (usesCmake) {
            require(!block.has("sourceFiles")) { "CMake selects sources from its description, not sourceFiles" }
            require(initialFiles.isNotEmpty())
            CppProject.validateFiles(initialFiles)
        } else CppProject(initialFiles, initialSources)
        files.putAll(initialFiles)
        initialFiles.forEach { (path, code) ->
            indentation[path] = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(code, seedPredefined = true))
        }
        sources.addAll(initialSources)
        selected = initialFiles.keys.firstOrNull { it == "main.cpp" } ?: initialFiles.keys.first()
        preferences.getString("$draftKey/project", null)?.let { saved ->
            runCatching {
                val draft = JSONObject(saved)
                val restored = linkedMapOf<String, String>()
                val entries = draft.getJSONArray("files")
                for (index in 0 until entries.length()) {
                    val entry = entries.getJSONObject(index)
                    val path = entry.getString("path")
                    CppProject.validatePath(path)
                    require(!restored.containsKey(path))
                    restored[path] = entry.getString("code")
                }
                val list = draft.getJSONArray("sources")
                val restoredSources = List(list.length()) { list.getString(it) }
                // An empty selection is an editable state, even though it cannot execute.
                require(restored.isNotEmpty())
                CppProject.validateFiles(restored)
                require(restoredSources.distinct().size == restoredSources.size)
                require(restoredSources.all { it in restored && CppProject.isSourceFile(it) })
                files.clear()
                files.putAll(restored)
                val savedIndentation = draft.optJSONObject("indentationV2")
                indentation.clear()
                restored.forEach { (path, code) ->
                    indentation[path] = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(code,
                        savedIndentation?.opt(path) as? String))
                }
                sources.clear()
                sources.addAll(restoredSources)
                selected = draft.optString("selected").takeIf(files::containsKey) ?: files.keys.first()
                draft.optJSONObject("cursors")?.let { savedCursors ->
                    savedCursors.keys().forEach { path -> cursors[path] = savedCursors.optInt(path) }
                }
            }
        }
        controls.addView(HorizontalScrollView(context).apply {
            isHorizontalScrollBarEnabled = true
            addView(fileButtons)
        })
        controls.addView(compileFile)
        val actions = LinearLayout(context)
        actions.addView(Button(context).apply {
            tag = "cpp_add_file"
            text = context.getString(R.string.cpp_add_file)
            setOnClickListener { promptForFile() }
        })
        actions.addView(Button(context).apply {
            tag = "cpp_delete_file"
            text = context.getString(R.string.cpp_delete_file)
            setOnClickListener {
                if (files.size == 1) {
                    Toast.makeText(context, R.string.cpp_keep_one_file, Toast.LENGTH_SHORT).show()
                } else {
                    files.remove(selected)
                    sources.remove(selected)
                    cursors.remove(selected)
                    indentation.remove(selected)
                    selected = files.keys.first()
                    showSelected()
                }
            }
        })
        controls.addView(actions)
        compileFile.setOnCheckedChangeListener { _, included ->
            if (!switching) {
                if (included) sources.add(selected) else sources.remove(selected)
                save()
            }
        }
        editor.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) {
                if (!switching) {
                    files[selected] = s.toString()
                    indentation[selected] = CppIndentationUnits.snapshot(s ?: "")
                    save()
                }
            }
        })
        (editor as? FreeScrollEditText)?.cppIndentationRestored = {
            if (!switching) {
                captureSelected()
                save()
            }
        }
        showSelected()
    }

    private fun showSelected() {
        switching = true
        editor.setText(CppIndentationUnits.loadedText(files.getValue(selected), indentation[selected]))
        editor.setSelection((cursors[selected] ?: 0).coerceIn(0, editor.length()))
        editor.contentDescription = selected
        compileFile.visibility = if (!usesCmake && CppProject.isSourceFile(selected)) View.VISIBLE else View.GONE
        compileFile.isChecked = selected in sources
        fileButtons.removeAllViews()
        files.keys.forEach { path ->
            fileButtons.addView(Button(context).apply {
                tag = "cpp_file_$path"
                text = if (path == selected) "[$path]" else path
                contentDescription = path
                setOnClickListener {
                    captureSelected()
                    selected = path
                    showSelected()
                }
            })
        }
        switching = false
        save()
        onFileChanged()
    }

    private fun promptForFile() {
        val name = EditText(context).apply {
            tag = "cpp_new_file_name"
            hint = "archivo.cpp"
            inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        }
        val dialog = AlertDialog.Builder(context)
            .setTitle(R.string.cpp_add_file)
            .setView(name)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val path = name.text.toString()
                val checked = runCatching {
                    CppProject.validatePath(path)
                    require(path !in files)
                    val candidate = files.toMutableMap().apply { put(path, "") }
                    CppProject.validateFiles(candidate)
                }
                if (checked.isFailure) {
                    name.error = context.getString(R.string.cpp_invalid_file)
                } else {
                    captureSelected()
                    files[path] = ""
                    indentation[path] = CppIndentationUnits.snapshot("")
                    if (CppProject.isSourceFile(path)) sources.add(path)
                    selected = path
                    showSelected()
                    dialog.dismiss()
                }
            }
        }
        dialog.show()
    }

    fun project(): CppProject {
        captureSelected()
        save()
        return CppProject(files, sources.toList())
    }

    /** The real CMake description chooses sources; an incomplete draft remains editable. */
    fun filesSnapshot(): Map<String, String> {
        // setText fires observers before showSelected restores the saved cursor.
        if (!switching) {
            captureSelected()
            save()
        }
        return LinkedHashMap(files)
    }

    fun reset() {
        files.clear()
        files.putAll(initialFiles)
        indentation.clear()
        initialFiles.forEach { (path, code) ->
            indentation[path] = CppIndentationUnits.snapshot(CppIndentationUnits.loadedText(code, seedPredefined = true))
        }
        sources.clear()
        sources.addAll(initialSources)
        cursors.clear()
        selected = initialFiles.keys.firstOrNull { it == "main.cpp" } ?: initialFiles.keys.first()
        showSelected()
    }

    private fun captureSelected() {
        files[selected] = editor.text.toString()
        cursors[selected] = editor.selectionStart
        indentation[selected] = CppIndentationUnits.snapshot(editor.text)
    }

    private fun save() {
        val entries = JSONArray()
        files.forEach { (path, code) -> entries.put(JSONObject().put("path", path).put("code", code)) }
        val draft = JSONObject().put("files", entries).put("sources", JSONArray(sources.toList()))
            .put("selected", selected).put("cursors", JSONObject(cursors.toMap()))
            .put("indentationV2", JSONObject(indentation.toMap()))
        preferences.edit().putString("$draftKey/project", draft.toString()).apply()
    }
}

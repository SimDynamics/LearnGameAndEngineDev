package com.learngraphicsprogramming.app

import android.content.SharedPreferences
import android.util.Log
import org.json.JSONObject
import java.security.MessageDigest

/** Source is escaped inside the existing preference, not copied into another draft.
 * Some Android/OEM FastXmlSerializer versions indent a closing string tag after a
 * final LF, adding four characters to the value when the next process reads it.
 * JSON keeps that LF escaped and preserves every original source character.
 */
internal object CppLessonSourceDraft {
    internal const val ENCODING = CppDraftTextCodec.ENCODING

    /** Repair unopened drafts only when their own previously stored digest proves
     * the exact original. Escape intact LF-ending drafts without changing text,
     * before a whole-map XML rewrite can add indentation to them as well.
     */
    fun repairProvenDrafts(preferences: SharedPreferences): Int {
        val values = preferences.all
        val repairs = linkedMapOf<String, String>()
        val protections = linkedMapOf<String, String>()
        for ((key, stored) in values) {
            if (!key.endsWith("/source") || stored !is String) continue
            val draftKey = key.removeSuffix("/source")
            if (values.containsKey("$draftKey/sourceEncoding")) continue
            val restored = decode(stored, null, values["$draftKey/sourceIndentation_v2"] as? String)
            if (restored != stored) repairs[draftKey] = restored
            else if (stored.endsWith('\n')) protections[draftKey] = stored
        }
        val editor = preferences.edit()
        val protectedInputs = CppLessonInputDraft.stageLegacyInputProtection(editor, values)
        if (repairs.isEmpty() && protections.isEmpty() && protectedInputs == 0) return 0
        for ((draftKey, restored) in repairs + protections) {
            editor.putString("$draftKey/source", encode(restored))
                .putString("$draftKey/sourceEncoding", ENCODING)
        }
        if (!editor.commit()) {
            Log.w("CppLessonSourceDraft", "Proven source repairs could not be confirmed on disk")
            return 0
        }
        Log.i("CppLessonSourceDraft", "SHA-proven sources repaired=${repairs.size}; " +
            "unchanged LF-ending sources protected=${protections.size}; legacy inputs protected=$protectedInputs")
        return repairs.size
    }

    fun read(preferences: SharedPreferences, draftKey: String, initialCode: String): String {
        // Inputs and sources must be protected in the same whole-map commit.
        repairProvenDrafts(preferences)
        val stored = preferences.getString("$draftKey/source", null) ?: return initialCode
        val encoding = preferences.getString("$draftKey/sourceEncoding", null)
        return decode(stored, encoding,
            preferences.getString("$draftKey/sourceIndentation_v2", null))
    }

    fun write(editor: SharedPreferences.Editor, draftKey: String, source: CharSequence): SharedPreferences.Editor =
        editor.putString("$draftKey/source", encode(source.toString()))
            .putString("$draftKey/sourceEncoding", ENCODING)
            .putString("$draftKey/sourceIndentation_v2", CppIndentationUnits.snapshot(source))

    internal fun encode(source: String): String = CppDraftTextCodec.encode(source, "code")

    internal fun decode(stored: String, encoding: String?, indentation: String?): String {
        if (encoding == ENCODING) return CppDraftTextCodec.decode(stored, "code") ?: stored
        if (encoding != null || !stored.endsWith("\n    ")) return stored

        // Recover only the exact legacy corruption proved by the already-recorded
        // source SHA. Manual whitespace, missing metadata and every other change stay.
        val expected = runCatching {
            val value = JSONObject(indentation ?: return stored)
            require(value.opt("version") == 1)
            (value.get("sha256") as String).also { require(it.matches(Regex("[0-9a-f]{64}"))) }
        }.getOrElse { return stored }
        if (hash(stored) == expected) return stored
        val candidate = stored.dropLast(4)
        return if (hash(candidate) == expected) candidate else stored
    }

    private fun hash(source: String): String = MessageDigest.getInstance("SHA-256")
        .digest(source.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 0xff) }
}

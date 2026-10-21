package com.learngraphicsprogramming.app

import android.content.SharedPreferences
import java.security.MessageDigest

/** Prepared stdin is data: all spaces, tabs and final line breaks are significant.
 * Use the same escaped-text format as source drafts, in the existing input key.
 * A prior digest can prove a repair; no whitespace is removed without that proof.
 */
internal object CppLessonInputDraft {
    /** SharedPreferences rewrites the whole map, including drafts not opened yet.
     * Escape every unmarked input before another field can trigger that rewrite.
     * This preserves its exact current value without inferring the user's intent.
     */
    fun protectLegacyInputs(preferences: SharedPreferences): Boolean {
        val values = preferences.all
        val editor = preferences.edit()
        return if (stageLegacyInputProtection(editor, values) == 0) true else editor.commit()
    }

    /** Stage against the same snapshot as source repairs, so one commit protects
     * the entire map without exposing still-raw sources to an intermediate write.
     */
    internal fun stageLegacyInputProtection(editor: SharedPreferences.Editor, values: Map<String, *>): Int {
        var protectedCount = 0
        for ((key, value) in values) {
            if (!key.endsWith("/input") || value !is String) continue
            val encodingKey = "${key.removeSuffix("/input")}/inputEncoding"
            if (values.containsKey(encodingKey)) continue
            editor.putString(key, CppDraftTextCodec.encode(value, "input"))
                .putString(encodingKey, CppDraftTextCodec.ENCODING)
            protectedCount++
        }
        return protectedCount
    }

    fun read(preferences: SharedPreferences, draftKey: String, initialInput: String): String {
        val stored = preferences.getString("$draftKey/input", null) ?: return initialInput
        val encoding = preferences.getString("$draftKey/inputEncoding", null)
        val input = decode(stored, encoding)
        val checksum = preferences.getString("$draftKey/inputSha256", null)
        val recovered = recoverProvedInput(stored, encoding, checksum)
        if (recovered != null) {
            // The digest predates the damage. Persist the proven original, not just its display.
            if (write(preferences.edit(), draftKey, recovered).commit()) return recovered
            return input
        }
        if (encoding == null) {
            // Protect untouched legacy drafts too: another preference edit can rewrite their XML.
            preferences.edit().putString("$draftKey/input", CppDraftTextCodec.encode(input, "input"))
                .putString("$draftKey/inputEncoding", CppDraftTextCodec.ENCODING).commit()
        }
        return input
    }

    fun write(editor: SharedPreferences.Editor, draftKey: String, input: CharSequence): SharedPreferences.Editor =
        editor.putString("$draftKey/input", CppDraftTextCodec.encode(input.toString(), "input"))
            .putString("$draftKey/inputEncoding", CppDraftTextCodec.ENCODING)
            .putString("$draftKey/inputSha256", checksum(input.toString()))

    internal fun recoverProvedInput(stored: String, encoding: String?, originalChecksum: String?): String? {
        if (originalChecksum == null || !originalChecksum.matches(Regex("[0-9a-f]{64}"))) return null
        if (encoding != null && encoding != CppDraftTextCodec.ENCODING) return null
        if (encoding != null && CppDraftTextCodec.decode(stored, "input") == null) return null
        val input = decode(stored, encoding)
        if (!input.endsWith("\n    ") || checksum(input) == originalChecksum) return null
        return input.dropLast(4).takeIf { checksum(it) == originalChecksum }
    }

    internal fun checksum(input: String): String = MessageDigest.getInstance("SHA-256")
        .digest(input.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 0xff) }

    internal fun decode(stored: String, encoding: String?): String =
        if (encoding == CppDraftTextCodec.ENCODING) CppDraftTextCodec.decode(stored, "input") ?: stored
        else stored
}

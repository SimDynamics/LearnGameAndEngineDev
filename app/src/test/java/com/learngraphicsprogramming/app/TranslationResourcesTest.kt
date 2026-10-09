package com.learngraphicsprogramming.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.*
import org.junit.Test
import org.w3c.dom.Element

/** Runs with the ordinary unit suite; new languages are discovered from the resource catalog. */
class TranslationResourcesTest {
    private val resources = File(System.getProperty("translation.resources", "src/main/res") ?: "src/main/res")

    @Test fun everyPublishedLanguageHasCompleteCompatibleResources() {
        val errors = mutableListOf<String>()
        val defaults = readDirectory(File(resources, "values"), errors)
        val tags = defaults["string-array/ui_language_tags"]!!.values.values.toList()
        assertEquals("Language tags must be unique", tags.size, tags.distinct().size)
        assertTrue("Spanish remains the default", tags.first() == "es")
        val names = defaults["string-array/ui_language_names"]!!.values
        assertEquals("Every tag must have a selector label", tags.size, names.size)
        assertEquals("es", File(resources, "resources.properties").readText().trim().substringAfter('='))
        for (tag in tags.drop(1)) {
            val qualifier = if ('-' in tag) "b+${tag.replace('-', '+')}" else tag
            val translated = readDirectory(File(resources, "values-$qualifier"), errors)
            errors += compatibility(defaults, translated, tag)
        }
        assertTrue(errors.joinToString("\n"), errors.isEmpty())
    }

    @Test fun checkerRejectsMissingDuplicateAndIncompatibleTranslations() {
        val defaults = mapOf("string/example" to entry("Hello %1\$s: %2\$d"))
        assertTrue(compatibility(defaults, emptyMap(), "fr").any { "missing" in it })
        assertTrue(compatibility(defaults, mapOf("string/example" to entry("Bonjour %1\$d: %2\$s")), "fr")
            .any { "format" in it })
        assertTrue(compatibility(defaults, mapOf("string/example" to entry("%2\$d: %1\$s")), "fr").isEmpty())
        assertNotNull("Malformed format directives must also be rejected", invalidFormat(entry("%1\$q")))
        val errors = mutableListOf<String>()
        val result = linkedMapOf<String, Entry>()
        add(result, "string/example", entry("one"), "a.xml", errors)
        add(result, "string/example", entry("two"), "b.xml", errors)
        assertTrue(errors.any { "duplicate" in it })
        val plurals = mapOf("plurals/count" to Entry(true, mapOf("one" to "%1\$d item", "other" to "%1\$d items")))
        assertTrue(compatibility(plurals, mapOf("plurals/count" to Entry(true, mapOf("one" to "%1\$d objet"))), "fr")
            .any { "other" in it })
    }

    @Test fun layoutsDoNotEmbedLanguageDependentInterfaceText() {
        val errors = mutableListOf<String>()
        File(resources, "layout").walkTopDown().filter { it.extension == "xml" }.forEach { file ->
            val document = factory().newDocumentBuilder().parse(file)
            val nodes = document.getElementsByTagName("*")
            for (index in 0 until nodes.length) {
                val node = nodes.item(index) as Element
                for (attribute in listOf("text", "hint", "contentDescription", "title", "summary")) {
                    val text = node.getAttributeNS("http://schemas.android.com/apk/res/android", attribute)
                    if (text.isNotEmpty() && !text.startsWith('@') && !text.startsWith('?') && text.any { it.isLetter() }) {
                        errors += "${file.name}: $attribute embeds '$text'"
                    }
                }
            }
        }
        assertTrue(errors.joinToString("\n"), errors.isEmpty())
    }

    private data class Entry(val translatable: Boolean, val values: Map<String, String>, val formatted: Boolean = true)
    private fun entry(text: String) = Entry(true, mapOf("text" to text))

    private fun factory() = DocumentBuilderFactory.newInstance().apply {
        isNamespaceAware = true
        setFeature("http://apache.org/xml/features/disallow-doctype-decl", true)
        setFeature("http://xml.org/sax/features/external-general-entities", false)
        setFeature("http://xml.org/sax/features/external-parameter-entities", false)
    }

    private fun readDirectory(directory: File, errors: MutableList<String>): Map<String, Entry> {
        val entries = linkedMapOf<String, Entry>()
        if (!directory.isDirectory) { errors += "missing directory ${directory.name}"; return entries }
        directory.listFiles()!!.filter { it.extension == "xml" }.sortedBy { it.name }.forEach { file ->
            val root = factory().newDocumentBuilder().parse(file).documentElement
            for (index in 0 until root.childNodes.length) {
                val node = root.childNodes.item(index) as? Element ?: continue
                val type = if (node.tagName == "item") node.getAttribute("type") else node.tagName
                if (type !in setOf("string", "plurals", "string-array", "array")) continue
                val values = linkedMapOf<String, String>()
                if (type == "string") values["text"] = node.textContent.trim() else {
                    val items = node.getElementsByTagName("item")
                    for (itemIndex in 0 until items.length) {
                        val item = items.item(itemIndex) as Element
                        val key = if (type == "plurals") item.getAttribute("quantity") else itemIndex.toString()
                        if (key in values) errors += "${file.name}: duplicate quantity $key"
                        values[key] = item.textContent.trim()
                    }
                }
                val entry = Entry(
                    node.getAttribute("translatable") != "false", values, node.getAttribute("formatted") != "false"
                )
                invalidFormat(entry)?.let { errors += "${file.name}: invalid format in ${node.getAttribute("name")}: $it" }
                add(entries, "$type/${node.getAttribute("name")}", entry, file.path, errors)
            }
        }
        return entries
    }

    private fun add(entries: MutableMap<String, Entry>, key: String, value: Entry, file: String, errors: MutableList<String>) {
        if (entries.put(key, value) != null) errors += "$file: duplicate $key"
    }

    private fun compatibility(defaults: Map<String, Entry>, translated: Map<String, Entry>, language: String): List<String> {
        val errors = mutableListOf<String>()
        defaults.filterValues { it.translatable }.forEach { (key, base) ->
            val target = translated[key]
            if (target == null) { errors += "$language: missing $key"; return@forEach }
            if (base.formatted != target.formatted) errors += "$language: incompatible formatting flag $key"
            if (key.startsWith("plurals/")) {
                if ("other" !in target.values) errors += "$language: missing other quantity in $key"
                val expected = signature(base.values.getValue("other"), base.formatted)
                target.values.forEach { (quantity, text) ->
                    if (quantity !in setOf("zero", "one", "two", "few", "many", "other")) errors += "$language: invalid quantity $quantity"
                    if (signature(text, target.formatted) != expected) errors += "$language: incompatible format $key/$quantity"
                }
            } else {
                if (base.values.keys != target.values.keys) errors += "$language: incompatible array size $key"
                base.values.forEach { (item, text) ->
                    if (signature(text, base.formatted) != signature(target.values[item].orEmpty(), target.formatted)) {
                        errors += "$language: incompatible format $key/$item"
                    }
                }
            }
        }
        translated.keys.filter { it !in defaults }.forEach { errors += "$language: extra $it without a default resource" }
        return errors
    }

    private fun signature(text: String, formatted: Boolean): Set<Pair<Int, String>> {
        if (!formatted) return emptySet()
        var implicit = 0
        val format = Regex("%(?:(\\d+)\\$)?[-#+ 0,(<]*\\d*(?:\\.\\d+)?([tT]?[a-zA-Z%])")
        return format.findAll(text).mapNotNull { match ->
            val type = match.groupValues[2]
            if (type in setOf("%", "n")) null else {
                val index = match.groupValues[1].toIntOrNull() ?: ++implicit
                index to type.lowercase()
            }
        }.toSet()
    }

    private fun invalidFormat(entry: Entry): String? {
        if (!entry.formatted) return null
        for (text in entry.values.values) {
            val arguments = signature(text, true)
            if (arguments.isEmpty()) continue
            val samples = Array<Any>(arguments.maxOf { it.first }) { index ->
                val types = arguments.filter { it.first == index + 1 }.map { it.second }
                when {
                    types.any { it.startsWith('t') } -> java.util.Date(0)
                    types.any { it in setOf("d", "o", "x", "c") } -> 65
                    types.any { it in setOf("f", "e", "g", "a") } -> 1.5
                    else -> "sample"
                }
            }
            try { String.format(java.util.Locale.ROOT, text, *samples) }
            catch (error: java.util.IllegalFormatException) { return error.message ?: error.javaClass.simpleName }
        }
        return null
    }
}

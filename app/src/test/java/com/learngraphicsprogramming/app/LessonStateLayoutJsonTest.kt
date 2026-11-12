package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class LessonStateLayoutJsonTest {
    private val paragraph = JSONObject().put("type", "paragraph").put("text", "Original explanation.")
    private val editor = JSONObject().put("type", "cpp_compiler").put("initialCode", "int main() { return 0; }")
    private val prediction = JSONObject().put("type", "output_prediction").put("id", "original_prediction")
        .put("question", "Predict the output.").put("answers", JSONArray().put("Original"))
    private val extraEditor = JSONObject().put("type", "cpp_compiler").put("initialCode", "int main() { return 1; }")

    private fun manifest(pages: List<List<Pair<String, JSONObject>>>): JSONArray = JSONArray().apply {
        pages.forEachIndexed { index, blocks -> put(JSONObject().put("id", "p$index").put("blocks",
            JSONArray().apply { blocks.forEach { (id, block) -> put(JSONObject().put("id", id)
                .put("sha256", LessonStateLayoutJson.stateFingerprint(block))) } })) }
    }

    private fun fixture(): JSONObject {
        val original = manifest(listOf(listOf("prose" to paragraph, "editor" to editor),
            listOf("prediction" to prediction)))
        val translated = manifest(listOf(listOf("prose" to paragraph, "editor" to editor),
            listOf("translated_note" to JSONObject().put("type", "note").put("text", "A note, not a puzzle."))))
        val changed = JSONObject(paragraph.toString()).put("text", "A corrected explanation.")
        val family = JSONObject().put("es", JSONObject().put("sha256", "a".repeat(64)).put("origin", original))
            .put("en", JSONObject().put("sha256", "b".repeat(64)).put("origin", translated))
            .put("fr", JSONObject().put("sha256", "c".repeat(64)).put("origin", translated))
        val current = JSONArray().put(JSONObject().put("id", "p1").put("blocks",
            JSONArray().put("prediction").put("prose").put("new_original_run")))
            .put(JSONObject().put("id", "p0").put("blocks", JSONArray().put("editor")))
        val additions = JSONArray().put(JSONObject().put("id", "new_original_run")
            .put("sha256", LessonStateLayoutJson.stateFingerprint(extraEditor))
            .put("storage", JSONObject().put("page", 2).put("block", 0)).put("progressPage", "p1"))
        val editorial = JSONArray().put(JSONObject().put("id", "prose")
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(paragraph))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(changed)))
        return JSONObject().put("id", "fixture").put("pages", JSONArray()
            .put(JSONObject().put("blocks", JSONArray().put(prediction).put(changed).put(extraEditor)))
            .put(JSONObject().put("blocks", JSONArray().put(editor))))
            .put("stateLayout", JSONObject().put("version", 2).put("locale", "es")
                .put("origin", original).put("current", current).put("family", family)
                .put("additions", additions).put("editorial", editorial))
    }

    @Test fun versionOneKeepsItsExactOriginalContractAndAddresses() {
        val original = manifest(listOf(listOf("prose" to paragraph, "editor" to editor),
            listOf("prediction" to prediction)))
        val document = JSONObject().put("pages", JSONArray()
            .put(JSONObject().put("blocks", JSONArray().put(paragraph).put(editor)))
            .put(JSONObject().put("blocks", JSONArray().put(prediction))))
            .put("stateLayout", JSONObject().put("version", 1).put("origin", original)
                .put("current", JSONArray().put(JSONObject().put("id", "p0")
                    .put("blocks", JSONArray().put("prose").put("editor")))
                    .put(JSONObject().put("id", "p1").put("blocks", JSONArray().put("prediction")))))
        val layout = LessonStateLayoutJson.read(document)!!
        assertEquals(1, layout.version)
        assertEquals(layout.legacyLineage, layout.lineage)
        assertNull(layout.locale)
        assertNull(layout.additionNamespace(0, 1))
        assertEquals(LessonStatePosition(0, 1), layout.storagePosition(0, 1))
        document.getJSONObject("stateLayout").put("family", JSONObject())
        assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
            is LessonStateLayoutException)
    }

    @Test fun editorialCorrectionReorderingAndNewActivityShareOneSafeTransition() {
        val layout = LessonStateLayoutJson.read(fixture())!!
        assertEquals(2, layout.version)
        assertEquals("es", layout.locale)
        assertEquals(LessonStatePosition(1, 0), layout.storagePosition(0, 0))
        assertEquals(LessonStatePosition(0, 0), layout.storagePosition(0, 1))
        assertEquals(LessonStatePosition(2, 0), layout.storagePosition(0, 2))
        assertEquals(LessonStatePosition(0, 1), layout.storagePosition(1, 0))
        assertNull(layout.additionNamespace(1, 0))
        assertTrue(layout.additionNamespace(0, 2)!!.matches(Regex("[0-9a-f]{64}")))
        assertEquals(2, layout.progressGroups.size)
        assertEquals(listOf(LessonStatePosition(0, 0), LessonStatePosition(0, 2)), layout.progressGroups[1])
        val read = layout.legacyReadBlocks(true, 1)
        assertFalse("new_original_run" in read)
        assertFalse(layout.progressPageRead(1, layout.legacyReadPages(true, 1), read))
        assertFalse(layout.pageRead(0, layout.legacyReadPages(true, 1), read))
        val (pages, blocks) = layout.visited(0, emptySet(), read)
        assertTrue(layout.pageRead(0, pages, blocks))
        assertEquals("translated_note", layout.family.getValue("en").origin[1].blocks.single())
    }

    @Test fun newActivitiesKeepTheirNamespaceAcrossFurtherAdditionsAndPermutations() {
        val first = LessonStateLayoutJson.read(fixture())!!
        val document = fixture()
        val metadata = document.getJSONObject("stateLayout")
        metadata.getJSONArray("additions").put(JSONObject().put("id", "second_new_editor")
            .put("sha256", LessonStateLayoutJson.stateFingerprint(extraEditor))
            .put("storage", JSONObject().put("page", 2).put("block", 1)).put("progressPage", "p0"))
        metadata.getJSONArray("current").getJSONObject(1).getJSONArray("blocks").put("second_new_editor")
        document.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").put(extraEditor)
        val second = LessonStateLayoutJson.read(document)!!
        assertEquals(first.lineage, second.lineage)
        assertEquals(first.additionNamespace(0, 2), second.additionNamespace(0, 2))
        assertNotEquals(second.additionNamespace(0, 2), second.additionNamespace(1, 1))
        assertNotEquals(first.lineage, first.legacyLineage)
        assertEquals(StableLessonStateLayout(first.origin, first.origin).lineage, first.legacyLineage)
    }

    @Test fun familyAndLocaleArePartOfTheLineageButDisplayOrderIsNot() {
        val first = LessonStateLayoutJson.read(fixture())!!
        val metadata = fixture().getJSONObject("stateLayout")
        val family = first.family.toMutableMap().apply {
            this["fr"] = getValue("fr").copy(sha256 = "d".repeat(64))
        }
        val changed = StableLessonStateLayout(first.origin, first.current, 2, "es", family, first.additions)
        assertNotEquals(first.lineage, changed.lineage)
        val reordered = StableLessonStateLayout(first.origin, first.current.reversed(), 2, "es",
            first.family, first.additions)
        assertEquals(first.lineage, reordered.lineage)
        assertEquals(2, metadata.getInt("version"))
    }

    @Test fun oldSlotsDuplicateNewSlotsForeignFamiliesAndTamperedAnswersAreRejected() {
        val changes: List<(JSONObject) -> Unit> = listOf(
            { it.getJSONObject("stateLayout").getJSONArray("additions").getJSONObject(0)
                .getJSONObject("storage").put("page", 1) },
            { it.getJSONObject("stateLayout").put("locale", "de") },
            { it.getJSONObject("stateLayout").getJSONObject("family").getJSONObject("es").put("sha256", "bad") },
            { it.getJSONObject("stateLayout").getJSONObject("family").getJSONObject("es")
                .getJSONArray("origin").getJSONObject(0).put("id", "foreign_page") },
            { it.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(0)
                .put("answers", JSONArray().put("An altered answer")) },
            { it.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").getJSONObject(0)
                .put("initialCode", "int main() { return 9; }") },
            { it.getJSONObject("stateLayout").getJSONArray("current").getJSONObject(0)
                .getJSONArray("blocks").put(2, "editor") },
            { it.getJSONObject("stateLayout").getJSONArray("editorial").getJSONObject(0)
                .put("afterSha256", "f".repeat(64)) }
        )
        changes.forEach { change ->
            val document = fixture()
            change(document)
            assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }

    @Test fun editorialHistoryCanAccumulateButCannotLoopOrSkipItsPreviousCorrection() {
        val document = fixture()
        val current = document.getJSONArray("pages").getJSONObject(0).getJSONArray("blocks").getJSONObject(1)
        val previous = LessonStateLayoutJson.fingerprint(current)
        current.put("text", "A second corrected explanation.")
        val edits = document.getJSONObject("stateLayout").getJSONArray("editorial")
        edits.put(JSONObject().put("id", "prose").put("beforeSha256", previous)
            .put("afterSha256", LessonStateLayoutJson.fingerprint(current)))
        assertNotNull(LessonStateLayoutJson.read(document))
        edits.getJSONObject(1).put("beforeSha256", "e".repeat(64))
        assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
            is LessonStateLayoutException)
    }

    private fun formattedEditor(): JSONObject {
        val document = fixture()
        val editorBlocks = document.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
        val previous = JSONObject(editorBlocks.getJSONObject(0).toString())
        val revised = JSONObject(previous.toString())
            .put("initialCode", "int main() {\n\n    return 0;\n}\n")
        editorBlocks.put(0, revised)
        val layout = document.getJSONObject("stateLayout")
        layout.getJSONArray("editorial").put(JSONObject().put("id", "editor")
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(previous))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(revised)))
        layout.put("sourceFormatRevisions", JSONArray().put(JSONObject()
            .put("id", "editor").put("block", previous)))
        return document
    }

    @Test fun formattingPreservesTheOldAndroidStateLineageAndStorageKeys() {
        val before = LessonStateLayoutJson.read(fixture())!!
        val document = formattedEditor()
        val after = LessonStateLayoutJson.read(document)!!
        assertEquals(before.legacyLineage, after.legacyLineage)
        assertEquals(before.lineage, after.lineage)
        assertEquals(before.storagePosition(1, 0), after.storagePosition(1, 0))
        assertEquals(before.additionNamespace(0, 2), after.additionNamespace(0, 2))
        assertEquals("int main() {\n\n    return 0;\n}\n",
            document.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
                .getJSONObject(0).getString("initialCode"))
    }

    @Test fun formattingRejectsStaleSnapshotsAlteredResultsAndMissingProvenance() {
        val changes: List<(JSONObject) -> Unit> = listOf(
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).getJSONObject("block")
                .put("initialCode", "int main() { return 2; }") },
            { it.getJSONArray("pages").getJSONObject(1).getJSONArray("blocks")
                .getJSONObject(0).put("initialCode", "int main() { return 9; }") },
            { it.getJSONObject("stateLayout").getJSONArray("editorial").remove(1) },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).put("id", "prediction") },
            { it.getJSONObject("stateLayout").getJSONArray("sourceFormatRevisions")
                .getJSONObject(0).put("another", true) }
        )
        changes.forEach { change ->
            val document = formattedEditor()
            change(document)
            assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
    }

    @Test fun cppCmakeLiteralAndMacroSourceEditsRemainProtected() {
        val before = JSONObject().put("type", "code")
            .put("code", "int main(){int x=2;return x;}")
        val after = JSONObject(before.toString())
            .put("code", "int main() {\n  int x = 2;\n\n  return x;\n}")
        assertEquals(before.getString("code"),
            CurricularSourceFormatCompat.project(after, before).getString("code"))
        for ((original, changed) in listOf(
            "int x=2;" to "int x=3;",
            "int line=__LINE__;" to "\nint line=__LINE__;",
            "auto value=\"abc\"_tag;" to "auto value=\"abc\" _tag;",
            "auto value=R\"x(a\nb)x\";" to "auto value=R\"x(a\n\nb)x\";",
            "#define X(v) \\\n ((v)+1)\nint result=X(2);" to
                "#define X(v) \\\n ((v)+1)\n\nint result=X(2);"
        )) {
            val a = JSONObject().put("type", "code").put("code", original)
            val b = JSONObject(a.toString()).put("code", changed)
            assertTrue(runCatching { CurricularSourceFormatCompat.project(b, a) }.isFailure)
        }
        val cmake = JSONObject().put("type", "code").put("language", "cmake")
            .put("code", "add_executable(game main.cpp)\n")
        val spaced = JSONObject(cmake.toString())
            .put("code", "add_executable(\n  game main.cpp\n)\n")
        assertEquals(cmake.getString("code"),
            CurricularSourceFormatCompat.project(spaced, cmake).getString("code"))
        val line = JSONObject().put("type", "code").put("language", "cmake")
            .put("code", "message(\"\${CMAKE_CURRENT_LIST_LINE}\")")
        val moved = JSONObject(line.toString()).put("code", "\n" + line.getString("code"))
        assertTrue(runCatching { CurricularSourceFormatCompat.project(moved, line) }.isFailure)
    }

    @Test fun imageDescriptionsDoNotRewriteTheHistoricalResourceIdentity() {
        val original = JSONObject().put("type", "image").put("imageId", "lesson_diagram_001")
            .put("alt", "Descripción anterior").put("caption", "Pie anterior")
        val revised = JSONObject(original.toString())
            .put("alt", "Descripción más precisa").put("caption", "Pie actualizado")
        assertEquals(LessonStateLayoutJson.stateFingerprint(original),
            LessonStateLayoutJson.stateFingerprint(revised))
        val replacedResource = JSONObject(revised.toString()).put("imageId", "another_resource")
        assertNotEquals(LessonStateLayoutJson.stateFingerprint(original),
            LessonStateLayoutJson.stateFingerprint(replacedResource))
    }

    @Test fun publishedCompilerDiagnosticsPreserveOnlyTheExactOriginalProgramAndProject() {
        val root = listOf(java.io.File("src/main/assets/content"),
            java.io.File("app/src/main/assets/content")).firstOrNull { it.isDirectory }
            ?: error("Cannot find published curricular assets")
        val lessons = listOf(
            "es/programming/cpp_fundamentals/datos_propios_y_estado_valido/" +
                "lesson_consultas_const_y_objetos_constantes.json",
            "es/programming/cpp_fundamentals/funciones_y_contratos/" +
                "lesson_declaraciones_y_firmas.json"
        )
        var checked = 0
        for (path in lessons) {
            val lesson = JSONObject(java.io.File(root, path).readText(Charsets.UTF_8))
            val pages = lesson.getJSONArray("pages")
            for (pageIndex in 0 until pages.length()) {
                val blocks = pages.getJSONObject(pageIndex).getJSONArray("blocks")
                for (index in 0 until blocks.length()) {
                    val block = blocks.getJSONObject(index)
                    if (!block.has("compileCheck")) continue
                    ++checked
                    val before = JSONObject(block.toString()).apply { remove("compileCheck") }
                    assertEquals("Reviewed diagnostic changed the original historical receipt",
                        LessonStateLayoutJson.stateFingerprint(before),
                        LessonStateLayoutJson.stateFingerprint(block))
                    val altered = JSONObject(block.toString())
                        .put("initialCode", block.getString("initialCode") + "\n// forged source")
                    assertTrue("Changed source was accepted for a reviewed diagnostic",
                        runCatching { LessonStateLayoutJson.stateFingerprint(altered) }.isFailure)
                    val unknown = JSONObject(block.toString())
                    unknown.getJSONObject("compileCheck").put("diagnostic", "forged-compiler-diagnostic")
                    assertTrue("Unknown diagnostic was treated as editorial-only",
                        runCatching { LessonStateLayoutJson.stateFingerprint(unknown) }.isFailure)
                    val removedSource = JSONObject(block.toString())
                    if (block.has("projectFiles")) {
                        removedSource.remove("sourceFiles")
                    } else {
                        removedSource.put("sourceFiles", JSONArray())
                    }
                    assertTrue("A changed compilation source selection was accepted",
                        runCatching { LessonStateLayoutJson.stateFingerprint(removedSource) }.isFailure)
                }
            }
        }
        assertEquals("Expected one single-source and three published project diagnostics", 4, checked)
    }

    @Test fun publishedEditorialReturnsRequireSameOwnerAndCompleteHistoryPrefix() {
        val root = listOf(java.io.File("src/main/assets/content"),
            java.io.File("app/src/main/assets/content")).firstOrNull { it.isDirectory }
            ?: error("Cannot find published curricular assets")
        val path = "es/programming/cpp_fundamentals/objetos_y_estado/" +
            "lesson_estado_asignacion_y_copia.json"
        val bytes = java.io.File(root, path).readText(Charsets.UTF_8)
        val original = JSONObject(bytes)
        val layout = LessonStateLayoutJson.read(original)!!
        assertEquals("es", layout.locale)
        val edits = original.getJSONObject("stateLayout").getJSONArray("editorial")
        val returned = (0 until edits.length()).filter { edits.getJSONObject(it).has("returnTo") }
        assertEquals("Expected two authenticated returns in the published Spanish lesson", 2, returned.size)
        for (index in returned) {
            val pristine = edits.getJSONObject(index)
            val proof = pristine.getJSONObject("returnTo")
            assertEquals(0, proof.getInt("revision"))
            val changes: List<(JSONObject) -> Unit> = listOf(
                { it.remove("returnTo") },
                { it.getJSONObject("returnTo").put("revision", true) },
                { it.getJSONObject("returnTo").put("revision", 999) },
                { it.getJSONObject("returnTo").put("historySha256", "0".repeat(64)) },
                { it.getJSONObject("returnTo").put("previousBlock",
                    JSONObject().put("type", "paragraph").put("text", "Other owner")) },
                { it.getJSONObject("returnTo").put("unexpected", true) }
            )
            for (change in changes) {
                val altered = JSONObject(bytes)
                val changeRecord = altered.getJSONObject("stateLayout")
                    .getJSONArray("editorial").getJSONObject(index)
                change(changeRecord)
                assertTrue("A forged or unsupported editorial return was admitted",
                    runCatching { LessonStateLayoutJson.read(altered) }.isFailure)
            }
        }
    }

    @Test fun publishedIntentionalExitsAndCrtVariantRetainOnlyTheirExactHistoricalContract() {
        val root = listOf(java.io.File("src/main/assets/content"),
            java.io.File("app/src/main/assets/content")).firstOrNull { it.isDirectory }
            ?: error("Cannot find published curricular assets")
        val prefix = "es/programming/cpp_fundamentals/archivos_y_programa_completo/"
        val goals = listOf(
            "lesson_compartir_flujos_sin_acoplar_el_calculo.json" to 1,
            "lesson_construir_un_programa_de_carga_y_guardado.json" to 1,
            "lesson_clasificar_caracteres_y_normalizar_una_copia.json" to 2
        )
        for ((name, outputCount) in goals) {
            val document = JSONObject(java.io.File(root, prefix + name).readText(Charsets.UTF_8))
            val pages = document.getJSONArray("pages")
            val blocks = (0 until pages.length()).flatMap { page ->
                val items = pages.getJSONObject(page).getJSONArray("blocks")
                (0 until items.length()).map { items.getJSONObject(it) }
            }
            val owner = blocks.single { block ->
                block.optJSONObject("outputCheck")?.optJSONArray("cases")?.let { cases ->
                    cases.length() == 1 && (
                        (outputCount == 1 && cases.getJSONObject(0).optInt("exitCode") == 1) ||
                        (outputCount == 2 && cases.getJSONObject(0)
                            .optJSONArray("outputs")?.length() == 2)
                    )
                } == true
            }
            val historical = JSONObject(owner.toString())
            val outputs = historical.getJSONObject("outputCheck")
                .getJSONArray("cases").getJSONObject(0).getJSONArray("outputs")
            if (outputCount == 1) {
                historical.remove("outputCheck")
            } else {
                outputs.remove(1)
            }
            assertEquals("The published Windows-only goal changed Android's historical state",
                LessonStateLayoutJson.stateFingerprint(historical),
                LessonStateLayoutJson.stateFingerprint(owner))
            val changed = JSONObject(owner.toString())
            changed.getJSONObject("outputCheck").getJSONArray("cases")
                .getJSONObject(0).getJSONArray("outputs").put(0, "Forged result")
            if (outputCount == 1) {
                assertTrue("An altered intentional-exit goal gained a historical alias",
                    runCatching { LessonStateLayoutJson.stateFingerprint(changed) }.isFailure)
            } else {
                assertNotEquals("An altered CRT output borrowed the original result",
                    LessonStateLayoutJson.stateFingerprint(owner),
                    LessonStateLayoutJson.stateFingerprint(changed))
            }
        }
    }

    @Test fun publishedStderrGoalsRequireTheOriginalNonBlankChannelOnBothOwners() {
        val root = listOf(java.io.File("src/main/assets/content"),
            java.io.File("app/src/main/assets/content")).firstOrNull { it.isDirectory }
            ?: error("Cannot find published curricular assets")
        val files = listOf(
            "es/programming/cpp_fundamentals/aleatoriedad_y_reproducibilidad/" +
                "lesson_combinar_ensayos_generados_y_casos_elegidos.json" to "block_0004_0007",
            "es/programming/cpp_fundamentals/excepciones_y_recuperacion/" +
                "lesson_limites_comprobados_y_tipos_de_captura.json" to "block_0002_0008"
        )
        for ((path, ownerId) in files) {
            val bytes = java.io.File(root, path).readText(Charsets.UTF_8)
            val original = JSONObject(bytes)
            assertNotNull("Published stderr owner is still incompatible with Android state",
                LessonStateLayoutJson.read(original))
            val positions = original.getJSONObject("stateLayout").getJSONArray("current")
            var ownerPage = -1
            var ownerBlock = -1
            for (page in 0 until positions.length()) {
                val blocks = positions.getJSONObject(page).getJSONArray("blocks")
                for (index in 0 until blocks.length()) if (blocks.getString(index) == ownerId) {
                    ownerPage = page
                    ownerBlock = index
                }
            }
            assertTrue(ownerPage >= 0 && ownerBlock >= 0)
            val owner = original.getJSONArray("pages").getJSONObject(ownerPage)
                .getJSONArray("blocks").getJSONObject(ownerBlock)
            val previous = JSONObject(owner.toString())
            val case = owner.getJSONObject("outputCheck").getJSONArray("cases").getJSONObject(0)
            assertEquals("nonBlank", case.getString("standardError"))
            case.remove("standardError")
            assertEquals(LessonStateLayoutJson.stateFingerprint(previous),
                LessonStateLayoutJson.stateFingerprint(owner))
            assertTrue("A published stderr requirement disappeared unnoticed",
                runCatching { LessonStateLayoutJson.read(original) }.isFailure)
            case.put("standardError", "nonEmpty")
            assertTrue("An unsupported stderr mode reused the historical slot",
                runCatching { LessonStateLayoutJson.read(original) }.isFailure)
        }
    }

    @Test fun publishedRetirementHistoryKeepsOriginalSlotsButNotReadingObligations() {
        val root = listOf(java.io.File("src/main/assets/content"),
            java.io.File("app/src/main/assets/content")).firstOrNull { it.isDirectory }
            ?: error("Cannot find published curricular assets")
        val lessons = listOf(
            "programas_y_ejecucion/lesson_texto_secuencia_y_formato.json",
            "programas_y_ejecucion/lesson_primer_programa_y_salida.json",
            "programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json",
            "programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json",
            "archivos_y_programa_completo/lesson_referencias_del_curso.json"
        )
        for (name in lessons) {
            val path = "es/programming/cpp_fundamentals/" + name
            val source = java.io.File(root, path).readText(Charsets.UTF_8)
            val document = JSONObject(source)
            val layout = LessonStateLayoutJson.read(document)!!
            val metadata = document.getJSONObject("stateLayout")
            val entries = metadata.getJSONArray("retired")
            val retired = (0 until entries.length()).map {
                entries.getJSONObject(it).getString("id")
            }.toSet()
            assertTrue("The published retirement must have archived owners", retired.isNotEmpty())
            assertEquals(retired, layout.retiredIds)
            val current = layout.current.flatMap { it.blocks }.toSet()
            assertTrue("A retired owner remained reachable as a current puzzle",
                retired.intersect(current).isEmpty())
            assertEquals(current.size, layout.progressGroups.sumOf { it.size })
            layout.validateReadState(emptySet(), retired)
            for (alter in listOf<(JSONObject) -> Unit>(
                { d -> val meta = d.getJSONObject("stateLayout")
                    val item = meta.getJSONArray("retired").getJSONObject(0)
                    item.put("next", item.getString("id")) },
                { d -> d.getJSONObject("stateLayout").getJSONArray("retired")
                    .getJSONObject(0).getJSONObject("block").put("type", "forged") },
                { d -> val pages = d.getJSONObject("stateLayout")
                    .getJSONArray("retirementHistory").getJSONObject(0)
                    .getJSONArray("current")
                    .getJSONObject(0).getJSONArray("blocks")
                    pages.put(0, "foreign_owner") }
            )) {
                val tampered = JSONObject(source)
                alter(tampered)
                assertTrue("An altered historical retirement was accepted",
                    runCatching { LessonStateLayoutJson.read(tampered) }.isFailure)
            }
        }
    }

    @Test fun unknownVersionsAndFieldsNeverDowngradeToLegacyBehavior() {
        assertNull(LessonStateLayoutJson.read(JSONObject().put("pages", JSONArray())))
        for (value in listOf<Any>(0, 3, "2", true, JSONObject.NULL)) {
            val document = fixture()
            document.getJSONObject("stateLayout").put("version", value)
            assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
                is LessonStateLayoutException)
        }
        val document = fixture()
        document.getJSONObject("stateLayout").put("unexpected", true)
        assertTrue(runCatching { LessonStateLayoutJson.read(document) }.exceptionOrNull()
            is LessonStateLayoutException)
    }
}

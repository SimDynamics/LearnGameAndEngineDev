package com.learngraphicsprogramming.app

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Focused execution of complete published curricular programs, loaded from assets.
 *
 * This suite must only run against the opt-in, isolated curricularqa application.
 * It does not use the lesson renderer or write progress, preferences or reports.
 * Random checks assert contractual properties, never a host-library draw sequence.
 * Page and block coordinates below are one-based and deliberately fail on drift.
 */
@RunWith(AndroidJUnit4::class)
class CppCurricularProgramsInstrumentedTest {

    private lateinit var appContext: Context

    @Before
    fun requireIsolatedCurricularApplication() {
        val target = InstrumentationRegistry.getInstrumentation().targetContext
        assertEquals(
            "Refusing to execute curricular programs outside the isolated application",
            ISOLATED_PACKAGE,
            target.packageName
        )
        appContext = target
    }

    private data class AssetProgram(
        val lesson: String,
        val page: Int,
        val block: Int,
        val blockType: String = "code"
    ) {
        val assetPath: String get() = "$COURSE_ASSETS/$lesson"
        val location: String get() = "$lesson p${page}b${block}"
    }

    private fun guardedContext(): Context {
        assertTrue("JUnit setup must guard the target context first", ::appContext.isInitialized)
        assertEquals("Isolated target package changed", ISOLATED_PACKAGE, appContext.packageName)
        return appContext
    }

    private fun loadCompleteProgram(context: Context, program: AssetProgram): String {
        val lesson = context.assets.open(program.assetPath).bufferedReader(Charsets.UTF_8).use {
            JSONObject(it.readText())
        }
        val expectedId = "cpp_fundamentals__" + program.lesson
            .removeSuffix(".json")
            .replace("/lesson_", "__")
        assertEquals("Lesson identity at ${program.location}", expectedId, lesson.getString("id"))
        val block = lesson.getJSONArray("pages").getJSONObject(program.page - 1)
            .getJSONArray("blocks").getJSONObject(program.block - 1)
        assertEquals("Block type at ${program.location}", program.blockType, block.getString("type"))
        assertEquals("Language at ${program.location}", "cpp", block.getString("language"))
        val source = block.getString("code")
        assertTrue("Expected a complete program at ${program.location}", source.contains("int main()"))
        assertFalse("Unresolved exercise hole at ${program.location}", source.contains("___"))
        // Return the exact asset source; runProgram applies only an explicitly named variant.
        return source
    }

    private data class SourceTransformation(
        val name: String,
        val transform: (String) -> String
    )

    private fun runProgram(
        program: AssetProgram,
        expectedStdout: String,
        sourceTransformation: SourceTransformation? = null,
        expectedExecutionExitCode: Int = 0,
        verifyWorkspace: (File) -> Unit = {}
    ) {
        val context = guardedContext()
        val publishedSource = loadCompleteProgram(context, program)
        val source = if (sourceTransformation == null) {
            publishedSource
        } else {
            assertTrue("Contract source variant must have a name", sourceTransformation.name.isNotBlank())
            sourceTransformation.transform(publishedSource)
        }
        val location = if (sourceTransformation == null) program.location
            else "${program.location} contract variant ${sourceTransformation.name}"
        val result = CppExecutionEngine(context).compileAndRun(source)
        try {
            assertFalse("Compiler timeout at $location", result.compilation.timedOut)
            assertEquals(
                "Compilation/link failure at $location: ${result.compilation.stderr}",
                0,
                result.compilation.exitCode
            )
            assertNotNull("Missing WASM at $location", result.compilation.wasmFile)
            assertNotNull("Program did not execute at $location", result.execution)
            val execution = result.execution!!
            assertEquals("Runtime failure at $location: ${execution.stderr}", expectedExecutionExitCode, execution.exitCode)
            assertEquals("Runtime stderr at $location", "", execution.stderr)
            // Exact assertions preserve significant internal spaces and newline positions.
            assertEquals("Contract output at $location", expectedStdout, execution.stdout)
            verifyWorkspace(result.compilation.workspace)
        } finally {
            // The compiler owns a fresh direct child of this isolated package's cpp-jobs directory.
            val workspace = result.compilation.workspace.canonicalFile
            val ownedJobs = File(context.cacheDir, "cpp-jobs").canonicalFile
            assertEquals("Refusing cleanup outside the isolated compiler jobs", ownedJobs, workspace.parentFile)
            assertTrue("Could not remove this test's compiler job", workspace.deleteRecursively())
        }
    }

    @Test
    fun randomIntervalDegenerateRangeContinuationAndResetObeyTheirContracts() {
        runProgram(
            AssetProgram("$RANDOM/lesson_producir_enteros_en_un_intervalo.json", 2, 4, "output_prediction"),
            "Datos:12;Dentro:1"
        )
        runProgram(
            AssetProgram("$RANDOM/lesson_producir_enteros_en_un_intervalo.json", 3, 5, "output_prediction"),
            "[7][7][7]"
        )
        runProgram(
            AssetProgram("$RANDOM/lesson_semillas_continuacion_y_reinicio.json", 2, 3, "output_prediction"),
            "1:6"
        )
        runProgram(
            AssetProgram("$RANDOM/lesson_semillas_continuacion_y_reinicio.json", 3, 3, "output_prediction"),
            "1"
        )
        // Full construction reference: range, partition count and reproduction in this environment.
        runProgram(
            AssetProgram("$RANDOM/lesson_construir_un_experimento_reproducible.json", 4, 2),
            "Cantidad:12\nRango:1\nConteo:1\nRepeticion:1"
        )
    }

    @Test
    fun cctypeClassifiesAsciiWhitespaceAndNormalizesAnIndependentCopy() {
        runProgram(
            AssetProgram("$FILES/lesson_clasificar_caracteres_y_normalizar_una_copia.json", 3, 6, "output_prediction"),
            "1:1:1:1"
        )
        runProgram(
            AssetProgram("$FILES/lesson_clasificar_caracteres_y_normalizar_una_copia.json", 7, 2),
            "Original:<CaJa 2.>\n" +
                "Minusculas:<caja 2.>\n" +
                "Mayusculas:<CAJA 2.>\n" +
                "Letras:4;Digitos:1;Blancos:1;Otros:1\n" +
                "Entrada conservada:1"
        )
    }

    @Test
    fun fstreamWritesClosesReopensAndAppendsInsideThePrivateJob() {
        runProgram(
            AssetProgram("$FILES/lesson_abrir_escribir_y_leer_una_muestra.json", 5, 2),
            "Total=11"
        ) { workspace ->
            val written = File(workspace, "datos_recarga.txt")
            assertTrue("The curricular program did not create its private file", written.isFile)
            assertEquals("Saved sample", "4 7", written.readText(Charsets.UTF_8))
        }
        runProgram(
            AssetProgram("$FILES/lesson_cerrar_reemplazar_y_anadir_datos.json", 5, 2),
            "Total=12"
        ) { workspace ->
            val written = File(workspace, "serie_lgp.txt")
            assertTrue("The append program did not create its private file", written.isFile)
            assertEquals("Append must retain the earlier values", "3 4 5", written.readText(Charsets.UTF_8))
        }
    }

    @Test
    fun formatLiteralPatternsNumericBasesWidthAndPrecisionRunAsPublished() {
        runProgram(
            AssetProgram("$FILES/lesson_texto_con_campos_y_patrones_literales.json", 8, 2),
            "pedido {B}: 4 unidades; copia=B\n" +
                "pedido {}: 0 unidades; copia=\n" +
                "pedido {{X}}: 6 unidades; copia={X}"
        )
        runProgram(
            AssetProgram("$FILES/lesson_bases_y_anchuras_en_cada_campo.json", 3, 3, "output_prediction"),
            "0x1f|0X1F|037|0b11111"
        )
        runProgram(
            AssetProgram("$FILES/lesson_bases_y_anchuras_en_cada_campo.json", 5, 3, "output_prediction"),
            "[Luz   ]|[..12]|[5]"
        )
        runProgram(
            AssetProgram("$FILES/lesson_precision_por_campo_y_resumenes_formateados.json", 1, 5, "output_prediction"),
            "4.31|4.31e+00|4.3"
        )
        // Full reference also checks empty data, zero padding, media and value preservation.
        runProgram(
            AssetProgram("$FILES/lesson_precision_por_campo_y_resumenes_formateados.json", 7, 2),
            "[N     ]|[0004]|[8.81]\n" +
                "[E     ]|[0009]|[4.31]\n" +
                "Datos:2;Total:13;Media:6.56\n" +
                "141\nDatos:0;Total:0;Sin media"
        )
    }

    @Test
    fun depositReferencesDistinguishAliasCopiesViewsAndSequentialRequests() {
        val lessonPath = "datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json"
        val lessonId = "cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published deposit append", lessonId, lesson.getString("id"))
        for ((page, suffix) in listOf(
            12 to "alias_rechazo_inicial_y_vista",
            15 to "alias_copia_vista_y_otra_via"
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(2)
            assertEquals("Prediction identity at p${page}b3", "${lessonId}__$suffix", prediction.getString("id"))
        }

        // Separate objects finish at 1:6; the shared object goes from 4 to 1 to 5.
        // Each photograph remains 4, while its const view reads the current object.
        runProgram(
            AssetProgram(lessonPath, 11, 3),
            "Distintos\nResultados=1:1\nEstados=1:6\nObservacion=4:1\n" +
                "Mismo\nResultados=1:1\nEstados=5:5\nObservacion=4:5"
        )
        // Rejected withdrawals must still be followed by accepted additions.
        runProgram(
            AssetProgram(lessonPath, 12, 3, "output_prediction"),
            "Distintos=0:1;1:4;1:1\nMismo=0:1;4:4;1:4"
        )
        // From a=2/b=4, the four requests leave 2:6, 5:6, 4:6 and 5:6.
        // Count each operation separately; a rejection does not end the batch.
        runProgram(
            AssetProgram(lessonPath, 14, 3),
            "Paso=0:1;Estados=2:6\nPaso=1:1;Estados=5:6\n" +
                "Paso=1:0;Estados=4:6\nPaso=0:1;Estados=5:6\n" +
                "Totales=2:3\nObservacion=2:5"
        )
        // A second mutable reference follows a; b and the photograph are copies.
        runProgram(
            AssetProgram(lessonPath, 15, 3, "output_prediction"),
            "Resultados=1:1\nOtros=1:1\nEstados=5:1\nObservacion=4:5"
        )
    }

    @Test
    fun interpreterReferencesRecoverAndPublishOnlyCompletedState() {
        // 4*2 is accepted, 6/0 is rejected, and recovery reaches 3-5.
        runProgram(
            AssetProgram("texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json", 5, 3),
            "Valor: 8\nError: Divisor cero\nValor: -2\n" +
                "Aceptadas: 2\nRechazadas: 1\nSuma: 6\nHistorial: 8 -2"
        )
        // The constant assignment and incomplete lote=9 2 are both rejected.
        // Only 5, 6, 2 and 8 enter history; the committed lote remains 2.
        runProgram(
            AssetProgram("texto_interpretacion_y_flujos/lesson_construir_una_sesion_con_nombres_y_estado.json", 6, 3),
            "Valor: 5\nValor: 6\nValor: 2\nError: Nombre constante\n" +
                "Error: Se esperaba ;\nValor: 8\nAceptadas: 4\nRechazadas: 2\n" +
                "Tabla: unidad=1(C) limite=100(C) lote=2(V) sello=6(C)\n" +
                "Historial: 5 6 2 8\nSuma: 21\nMutables: 1\nConstantes: 3\n"
        )
    }

    @Test
    fun structuredRoundTripAndPrivateFileSavePreserveCompleteData() {
        // Order and the zero record survive composition, extraction and recomposition.
        runProgram(
            AssetProgram("$FILES/lesson_comprobar_ida_y_vuelta_sin_perder_datos.json", 4, 2),
            "1:1:1\n{ conjunto { grupo azul 7[0;0] } { grupo rojo 4[3;1] } }"
        )
        // Two groups, including an empty one, contain three accepted and one rejected.
        // Inspect both actual private files as well as the program's full output.
        runProgram(
            AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 4, 2),
            "Entrada aceptada\nGrupos: 2\nAceptados: 3\nRechazados: 1\nVerificacion: 1"
        ) { workspace ->
            val expected = "{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }"
            for (filename in listOf("inspecciones_entrada.txt", "inspecciones_salida.txt")) {
                val written = File(workspace, filename)
                assertTrue("Missing private curricular file $filename", written.isFile)
                assertEquals("Complete data and canonical text in $filename", expected, written.readText(Charsets.UTF_8))
            }
        }
    }

    @Test
    fun interpreterContractVariantRecoversMissingNameBeforeNextOperation() {
        // Contract variant of the real session reference, not another published main.
        // A missing name must leave n=4 committed and retain the following n+2 operation.
        val preparedLiteral = "\"crea lote=5;fija sello=lote+1;lote=lote%3;unidad=4;lote=9 2;sello+lote;\""
        runProgram(
            AssetProgram("texto_interpretacion_y_flujos/lesson_construir_una_sesion_con_nombres_y_estado.json", 6, 3),
            "Valor: 4\nError: Nombre no definido\nValor: 6\nAceptadas: 2\nRechazadas: 1\n" +
                "Tabla: unidad=1(C) limite=100(C) n=4(V)\nHistorial: 4 6\nSuma: 10\n" +
                "Mutables: 1\nConstantes: 2\n",
            sourceTransformation = SourceTransformation("missing-name-recovery") { source ->
                assertEquals("Unique prepared session literal", 1, source.split(preparedLiteral).size - 1)
                source.replace(preparedLiteral, "\"crea n=4;fantasma;n+2;\"")
            }
        )
    }

    @Test
    fun structuredFileContractVariantsNormalizeRejectAndPreservePreviousState() {
        // These ten cases vary only the prepared input in the real load/save reference.
        // The marker case additionally creates a known prior output before the sample.
        data class FileVariant(
            val name: String,
            val input: String,
            val stdout: String,
            val canonical: String? = null,
            val preserveMarker: Boolean = false
        )
        val acceptedTwoGroups = "Entrada aceptada\nGrupos: 2\nAceptados: 3\nRechazados: 1\nVerificacion: 1"
        val rejected = "Entrada rechazada\nEstado conservado: 1"
        val variants = listOf(
            FileVariant(
                "normalizacion",
                "{ conjunto   { grupo azul +07[00;0] } { grupo rojo 4[+3;01] } }",
                acceptedTwoGroups,
                "{ conjunto { grupo azul 7[0;0] } { grupo rojo 4[3;1] } }"
            ),
            FileVariant(
                "conjunto-vacio", "{ conjunto }",
                "Entrada aceptada\nGrupos: 0\nAceptados: 0\nRechazados: 0\nVerificacion: 1",
                "{ conjunto }"
            ),
            FileVariant(
                "grupo-vacio", "{ conjunto { grupo rojo } }",
                "Entrada aceptada\nGrupos: 1\nAceptados: 0\nRechazados: 0\nVerificacion: 1",
                "{ conjunto { grupo rojo } }"
            ),
            FileVariant(
                "id-local-compartido",
                "{ conjunto { grupo rojo 7[1;0] } { grupo azul 7[2;1] } }",
                acceptedTwoGroups,
                "{ conjunto { grupo rojo 7[1;0] } { grupo azul 7[2;1] } }"
            ),
            FileVariant("archivo-sin-formato", "", rejected),
            FileVariant(
                "grupo-duplicado-marca", "{ conjunto { grupo rojo } { grupo rojo } }", rejected,
                preserveMarker = true
            ),
            FileVariant("registro-duplicado", "{ conjunto { grupo rojo 1[0;0] 1[1;0] } }", rejected),
            FileVariant("cantidad-once", "{ conjunto { grupo rojo 2[11;1] } }", rejected),
            FileVariant("segundo-grupo-truncado", "{ conjunto { grupo rojo 7[0;0] } { grupo azul ", rejected),
            FileVariant("residuo-tras-cierre", "{ conjunto } extra", rejected)
        )
        val preparedLiteral = "\"{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }\""
        val mainOpening = "int main() {\n"
        val markerSetup = """
            int main() {
                {
                    std::ofstream marca{"inspecciones_salida.txt"};
                    if (!marca) { return 1; }
                    marca << "MARCA";
                    marca.close();
                    if (!marca) { return 1; }
                }
        """.trimIndent() + "\n"
        assertEquals("Four accepted and six rejected contract variants", 10, variants.size)
        for (variant in variants) {
            runProgram(
                AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 4, 2),
                variant.stdout,
                sourceTransformation = SourceTransformation(variant.name) { source ->
                    assertEquals("Unique prepared file literal for ${variant.name}", 1, source.split(preparedLiteral).size - 1)
                    var transformed = source.replace(preparedLiteral, "\"${variant.input}\"")
                    if (variant.preserveMarker) {
                        assertEquals("Unique main opening for ${variant.name}", 1, transformed.split(mainOpening).size - 1)
                        transformed = transformed.replace(mainOpening, markerSetup)
                    }
                    transformed
                }
            ) { workspace ->
                val expectedFiles = mutableMapOf("inspecciones_entrada.txt" to variant.input)
                if (variant.canonical != null) expectedFiles["inspecciones_salida.txt"] = variant.canonical
                if (variant.preserveMarker) expectedFiles["inspecciones_salida.txt"] = "MARCA"
                val actualNames = workspace.listFiles().orEmpty()
                    .filter { it.name.startsWith("inspecciones_") && it.name.endsWith(".txt") }
                    .map { it.name }.toSet()
                assertEquals("Created files for ${variant.name}", expectedFiles.keys.toSet(), actualNames)
                for ((filename, expected) in expectedFiles) {
                    val written = File(workspace, filename)
                    assertTrue("Missing private file $filename for ${variant.name}", written.isFile)
                    assertEquals("Exact private contents of $filename for ${variant.name}", expected, written.readText(Charsets.UTF_8))
                }
            }
        }
    }

    @Test
    fun fileOutputOpeningFailureIsTechnicalAndPreservesInput() {
        // The closed sample is a regular file, so it cannot be the output's parent directory.
        // This technical opening failure follows accepted input; it is not a parsing rejection.
        val publishedOpening = "std::ofstream guardado{\"inspecciones_salida.txt\"};"
        runProgram(
            AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 4, 2),
            "Error al abrir salida",
            sourceTransformation = SourceTransformation("output-parent-is-input-file") { source ->
                assertEquals("Unique published output opening", 1, source.split(publishedOpening).size - 1)
                source.replace(
                    publishedOpening,
                    "std::ofstream guardado{\"inspecciones_entrada.txt/inspecciones_salida.txt\"};"
                )
            },
            expectedExecutionExitCode = 1
        ) { workspace ->
            val input = File(workspace, "inspecciones_entrada.txt")
            assertTrue("Accepted input must remain a regular file", input.isFile)
            assertEquals(
                "Technical output failure must preserve the complete input",
                "{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }",
                input.readText(Charsets.UTF_8)
            )
            assertFalse("No regular output or output directory may be created", File(workspace, "inspecciones_salida.txt").exists())
            assertFalse("No nested output may be created beneath the input file", File(input, "inspecciones_salida.txt").exists())
            val actualEntries = workspace.listFiles().orEmpty()
                .filter { it.name.startsWith("inspecciones_") }.map { it.name }.toSet()
            assertEquals("Only the original input entry may remain", setOf("inspecciones_entrada.txt"), actualEntries)
        }
    }

    private companion object {
        const val ISOLATED_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        const val COURSE_ASSETS = "content/es/programming/cpp_fundamentals"
        const val RANDOM = "aleatoriedad_y_reproducibilidad"
        const val FILES = "archivos_y_programa_completo"
    }
}

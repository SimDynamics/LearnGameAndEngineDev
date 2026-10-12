package com.learngraphicsprogramming.app

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import com.learngraphicsprogramming.app.runtime.WasiRuntime
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
        standardInput: String = "",
        verifyStdout: ((String) -> Unit)? = null,
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
        val result = CppExecutionEngine(context).compileAndRun(source, standardInput = standardInput)
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
            if (verifyStdout == null) {
                assertEquals("Contract output at $location", expectedStdout, execution.stdout)
            } else {
                verifyStdout(execution.stdout)
            }
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
    fun chosenCountCasesDetectTheEqualityMutantWithoutFixingRandomFailures() {
        val program = AssetProgram(
            "$RANDOM/lesson_combinar_ensayos_generados_y_casos_elegidos.json", 6, 2
        )
        // U10L4 p5b4 requests this defined mutation; p5b5 is its laboratory.
        runProgram(program, "Fallos elegidos:0\nFallos generados:0")
        val observations = """
                std::cout << "\nIgualdad:" << contar_desde({4, 4}, 4)
                          << ':' << verificar({4, 4}, 4, 2) << '\n';
                std::cout << "Mixta:" << contar_desde({3, 4, 4, 5}, 4)
                          << ':' << verificar({3, 4, 4, 5}, 4, 3) << '\n';
                std::cout << "Controles:" << verificar({}, 4, 0)
                          << ':' << verificar({1, 2}, 3, 0) << '\n';
        """.trimIndent() + "\n"
        runProgram(
            program,
            "Fallos elegidos:2",
            sourceTransformation = SourceTransformation("strict-count-equality-mutant") { source ->
                val comparison = "if (dato >= minimo) {"
                val ending = "    return 0;\n}\n"
                assertEquals("Unique published count comparison", 1, source.split(comparison).size - 1)
                assertEquals("Unique published main ending", 1, source.split(ending).size - 1)
                assertTrue("Published ending closes the program", source.endsWith(ending))
                // Keep all four manual tests, generation, diagnostics and summaries.
                source.replace(comparison, "if (dato > minimo) {")
                    .removeSuffix(ending) + observations + ending
            },
            verifyStdout = { stdout ->
                assertTrue("Technical observations end with one LF", stdout.endsWith("\n"))
                val lines = stdout.dropLast(1).split('\n')
                assertTrue("Complete mutant summary and observations", lines.size >= 5)
                assertEquals("Two discriminative manual failures", "Fallos elegidos:2", lines.first())
                assertEquals("Equality is excluded by the mutant", "Igualdad:0:0", lines[lines.size - 3])
                assertEquals("Only five survives the mixed manual case", "Mixta:1:0", lines[lines.size - 2])
                assertEquals("Empty and no-accepted controls still pass", "Controles:1:1", lines.last())
                val summaryIndex = lines.size - 4
                val summary = Regex("Fallos generados:([0-9]+)").matchEntire(lines[summaryIndex])
                assertNotNull("Generated summary has its exact format", summary)
                val generatedFailures = summary!!.groupValues[1].toInt()
                assertTrue("At most forty generated failures", generatedFailures in 0..40)
                assertEquals("Every reported failure has one diagnostic", summaryIndex - 1, generatedFailures)
                val diagnostic = Regex("Caso:([0-9]+);Minimo:([0-9]);Datos:((?:\\[[0-9]\\])*)")
                val datum = Regex("\\[([0-9])\\]")
                var previousAttempt = -1
                for (line in lines.subList(1, summaryIndex)) {
                    val match = diagnostic.matchEntire(line)
                    assertNotNull("Exact generated diagnostic: $line", match)
                    val attempt = match!!.groupValues[1].toInt()
                    val minimum = match.groupValues[2].toInt()
                    val values = datum.findAll(match.groupValues[3]).map { it.groupValues[1].toInt() }.toList()
                    assertTrue("Attempts are ordered, unique and within forty", attempt in 0..39 && attempt > previousAttempt)
                    assertTrue("Published generated size remains 0..12", values.size in 0..12)
                    assertTrue("Only equality distinguishes this mutant from its contract", values.contains(minimum))
                    previousAttempt = attempt
                }
            }
        )
    }

    @Test
    fun histogramReferenceCountsBoundariesAndPreservesInputsAfterRejection() {
        val program = AssetProgram(
            "$RANDOM/lesson_construir_un_experimento_reproducible.json", 7, 2
        )
        // U10L5 p6b4 specifies variants; p6b5 is the independent laboratory.
        // p4b2 already has device evidence and is deliberately not repeated.
        runProgram(program, "0:1\n2:2\n9:1\nTotal:4\nConservado:1")
        val generationSource = loadCompleteProgram(
            guardedContext(),
            AssetProgram("$RANDOM/lesson_construir_un_experimento_reproducible.json", 4, 2)
        )
        val planOpening = "struct Plan {\n"
        val reportOpening = "struct Informe {\n"
        assertEquals("Unique published Plan opening", 1, generationSource.split(planOpening).size - 1)
        assertEquals("Unique published Informe opening", 1, generationSource.split(reportOpening).size - 1)
        // Copy the published Plan, validator and generator literally; do not run its main.
        val publishedGeneration = planOpening +
            generationSource.substringAfter(planOpening).substringBefore(reportOpening)
        val technicalMain = """
            int main() {
                const std::vector<std::vector<int>> casos{
                    {}, {0}, {9}, {3, 3, 3}, {-1}, {10}, {0, -1, 9}, {9, 10, 0},
                    std::vector<int>(20, 9), std::vector<int>(21, 0)
                };
                int numero = 0;
                for (const std::vector<int>& datos : casos) {
                    const std::vector<int> original = datos;
                    std::cout << "Caso:" << numero << '\n';
                    ++numero;
                    try {
                        const std::vector<int> tabla = frecuencias(datos);
                        std::cout << "Celdas:" << tabla.size() << '\n';
                        int total = 0;
                        for (int i = 0; i < 10; ++i) {
                            total += tabla.at(i);
                            if (tabla.at(i) != 0) {
                                std::cout << i << ':' << tabla.at(i) << '\n';
                            }
                        }
                        std::cout << "Total:" << total << '\n';
                    } catch (const std::invalid_argument&) {
                        std::cout << "Datos rechazados\n";
                    }
                    // Observe actual inputs after either acceptance or rejection.
                    std::cout << "Conservado:" << (datos == original) << '\n';
                }
                const std::vector<int> generados = preparar(Plan{313, 20, 0, 9});
                const std::vector<int> original = generados;
                const std::vector<int> tabla = frecuencias(generados);
                int total = 0;
                bool conteos = true;
                for (int i = 0; i < 10; ++i) {
                    total += tabla.at(i);
                    const auto esperado = std::count(original.begin(), original.end(), i);
                    conteos = conteos && tabla.at(i) == esperado;
                }
                std::cout << "Generados:" << generados.size() << '\n';
                std::cout << "Celdas:" << tabla.size() << '\n';
                std::cout << "Total:" << total << '\n';
                std::cout << "Conteos:" << conteos << '\n';
                std::cout << "Conservado:" << (generados == original) << '\n';
                return 0;
            }
        """.trimIndent() + "\n"
        val expected =
            "Caso:0\nCeldas:10\nTotal:0\nConservado:1\n" +
            "Caso:1\nCeldas:10\n0:1\nTotal:1\nConservado:1\n" +
            "Caso:2\nCeldas:10\n9:1\nTotal:1\nConservado:1\n" +
            "Caso:3\nCeldas:10\n3:3\nTotal:3\nConservado:1\n" +
            (4..7).joinToString("") { "Caso:$it\nDatos rechazados\nConservado:1\n" } +
            "Caso:8\nCeldas:10\n9:20\nTotal:20\nConservado:1\n" +
            "Caso:9\nDatos rechazados\nConservado:1\n" +
            "Generados:20\nCeldas:10\nTotal:20\nConteos:1\nConservado:1\n"
        runProgram(
            program,
            expected,
            sourceTransformation = SourceTransformation("histogram-fixed-boundaries-and-rejected-inputs") { source ->
                val mainOpening = "int main() {\n"
                val publishedInput = "    const std::vector<int> datos{2, 0, 2, 9};\n"
                assertEquals("Unique published histogram main", 1, source.split(mainOpening).size - 1)
                assertEquals("Exact published histogram input anchor", 1, source.split(publishedInput).size - 1)
                // Preserve the complete literal function, including prevalidation.
                source.substringBefore(mainOpening) +
                    "#include <algorithm>\n\n" + publishedGeneration + technicalMain
            }
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
    fun structuredEqualityRejectsValidSingleFieldAndOrderDifferences() {
        // Keep the published comparator and validators unchanged. Every negative
        // contrast is valid and has the same group and record counts as the control.
        val technicalTail = """
            bool contraste_valido(const char* nombre, const Conjunto& original,
                                  const Conjunto& distinto) {
                const bool validos = conjunto_valido(original) && conjunto_valido(distinto);
                bool tamanos = original.grupos.size() == distinto.grupos.size();
                if (tamanos) {
                    for (std::size_t i = 0; i < original.grupos.size(); ++i) {
                        if (original.grupos.at(i).registros.size()
                            != distinto.grupos.at(i).registros.size()) {
                            tamanos = false;
                        }
                    }
                }
                const bool ida = mismos_datos(original, distinto);
                const bool vuelta = mismos_datos(distinto, original);
                std::cout << nombre << ':' << validos << ':' << tamanos
                          << ':' << ida << ':' << vuelta << '\n';
                return validos && tamanos && !ida && !vuelta;
            }

            int main() {
                const Conjunto original{{
                    Grupo{2, {{7, {0, 0}}, {4, {3, 1}}}},
                    Grupo{0, {{7, {2, 4}}, {1, {0, 2}}}}
                }};
                const Conjunto copia = original;
                const Conjunto vacio{};
                const bool copia_ida = mismos_datos(original, copia);
                const bool copia_vuelta = mismos_datos(copia, original);
                const bool vacio_igual = mismos_datos(vacio, Conjunto{});
                std::cout << "Copia:" << copia_ida << ':' << copia_vuelta << '\n';
                std::cout << "Vacio:" << conjunto_valido(vacio) << ':' << vacio_igual << '\n';
                bool correcto = copia_ida && copia_vuelta && conjunto_valido(vacio) && vacio_igual;

                Conjunto distinto = original;
                distinto.grupos.at(0).id = 1;
                correcto = contraste_valido("Grupo inicial", original, distinto) && correcto;
                distinto = original;
                distinto.grupos.at(1).id = 1;
                correcto = contraste_valido("Grupo final", original, distinto) && correcto;

                distinto = original;
                distinto.grupos.at(0).registros.at(0).id = 8;
                correcto = contraste_valido("ID inicial", original, distinto) && correcto;
                distinto = original;
                distinto.grupos.at(1).registros.at(1).id = 2;
                correcto = contraste_valido("ID final", original, distinto) && correcto;

                distinto = original;
                distinto.grupos.at(0).registros.at(0).recuento.aceptados = 10;
                correcto = contraste_valido("Aceptados inicial", original, distinto) && correcto;
                distinto = original;
                distinto.grupos.at(1).registros.at(1).recuento.aceptados = 10;
                correcto = contraste_valido("Aceptados final", original, distinto) && correcto;

                distinto = original;
                distinto.grupos.at(0).registros.at(0).recuento.rechazados = 10;
                correcto = contraste_valido("Rechazados inicial", original, distinto) && correcto;
                distinto = original;
                distinto.grupos.at(1).registros.at(1).recuento.rechazados = 10;
                correcto = contraste_valido("Rechazados final", original, distinto) && correcto;

                distinto = original;
                distinto.grupos.at(0) = original.grupos.at(1);
                distinto.grupos.at(1) = original.grupos.at(0);
                correcto = contraste_valido("Orden grupos", original, distinto) && correcto;
                distinto = original;
                distinto.grupos.at(0).registros.at(0) = original.grupos.at(0).registros.at(1);
                distinto.grupos.at(0).registros.at(1) = original.grupos.at(0).registros.at(0);
                correcto = contraste_valido("Orden registros inicial", original, distinto) && correcto;
                distinto = original;
                distinto.grupos.at(1).registros.at(0) = original.grupos.at(1).registros.at(1);
                distinto.grupos.at(1).registros.at(1) = original.grupos.at(1).registros.at(0);
                correcto = contraste_valido("Orden registros final", original, distinto) && correcto;

                Conjunto invalido = original;
                invalido.grupos.at(0).registros.at(0).recuento.aceptados = 11;
                const bool invalido_valido = conjunto_valido(invalido);
                const bool invalido_igual = mismos_datos(invalido, invalido);
                std::cout << "Fuera de dominio:" << invalido_valido << ':' << invalido_igual << '\n';
                correcto = !invalido_valido && !invalido_igual && correcto;
                return correcto ? 0 : 1;
            }
        """.trimIndent() + "\n"
        runProgram(
            AssetProgram("$FILES/lesson_comprobar_ida_y_vuelta_sin_perder_datos.json", 4, 2),
            "Copia:1:1\nVacio:1:1\n" +
                "Grupo inicial:1:1:0:0\nGrupo final:1:1:0:0\n" +
                "ID inicial:1:1:0:0\nID final:1:1:0:0\n" +
                "Aceptados inicial:1:1:0:0\nAceptados final:1:1:0:0\n" +
                "Rechazados inicial:1:1:0:0\nRechazados final:1:1:0:0\n" +
                "Orden grupos:1:1:0:0\nOrden registros inicial:1:1:0:0\n" +
                "Orden registros final:1:1:0:0\nFuera de dominio:0:0\n",
            sourceTransformation = SourceTransformation("valid-equality-field-and-order-contrasts") { source ->
                val mainOpening = "int main() {\n"
                assertEquals("Unique round-trip reference main", 1, source.split(mainOpening).size - 1)
                source.substringBefore(mainOpening) + technicalTail
            }
        )
    }

    @Test
    fun multiDigitLexerKeepsBoundaryTokensAndRejectsOneHundred() {
        val lessonPath = "texto_interpretacion_y_flujos/lesson_numeros_de_varios_digitos_y_limites.json"
        val reference = AssetProgram(lessonPath, 6, 3)
        val context = guardedContext()
        val lesson = JSONObject(context.assets.open(reference.assetPath).bufferedReader().use { it.readText() })
        val laboratory = lesson.getJSONArray("pages").getJSONObject(4)
            .getJSONArray("blocks").getJSONObject(3)
        assertEquals("U11L10 p5b4 laboratory type", "cpp_compiler", laboratory.getString("type"))
        val initialSource = laboratory.getString("initialCode")
        val referenceSource = loadCompleteProgram(context, reference)
        val mainOpening = "int main() {\n"
        val digitOpening = "        if (actual >= '0' && actual <= '9') {\n"
        val signOpening = "        if (actual == '+' || actual == '-' || actual == '*'\n"
        val singleDigit = digitOpening + "            return Token{'n', actual - '0'};\n        }\n"
        val oldReserveDomain = "return token.valor >= 0 && token.valor <= 9;"
        assertEquals("Unique laboratory digit implementation", 1, initialSource.split(singleDigit).size - 1)
        assertEquals("Unique laboratory reserve domain", 1, initialSource.split(oldReserveDomain).size - 1)
        assertEquals("Unique reference outer digit opening", 1, referenceSource.split("\n" + digitOpening).size - 1)
        assertEquals("Unique reference sign opening", 1, referenceSource.split(signOpening).size - 1)
        assertEquals("Unique laboratory main", 1, initialSource.split(mainOpening).size - 1)
        assertEquals("Unique reference main", 1, referenceSource.split(mainOpening).size - 1)
        val completeDigits = digitOpening + referenceSource.substringAfter(digitOpening).substringBefore(signOpening)
        val completedLaboratory = initialSource.replace(singleDigit, completeDigits)
            .replace(oldReserveDomain, "return token.valor >= 0 && token.valor <= 99;")
        assertEquals(
            "Only multi-digit recognition and reserve-domain changes complete the published laboratory",
            referenceSource.substringBefore(mainOpening), completedLaboratory.substringBefore(mainOpening)
        )
        // The completed laboratory and reference have identical implementations;
        // run that implementation once with new Android boundary and reserve clients.
        val technicalMain = """
            int main() {
                try {
                    Lector_tokens maximo{"99"};
                    const Token numero = maximo.leer();
                    std::cout << "Maximo:" << numero.clase << ':' << numero.valor << '\n';
                    maximo.devolver(numero);
                    std::cout << "Reserva maximo:" << maximo.tiene_pendiente() << '\n';
                    const Token repetido = maximo.leer();
                    std::cout << "Repetido maximo:" << repetido.clase << ':' << repetido.valor
                              << ':' << maximo.tiene_pendiente() << '\n';
                    const Token fin = maximo.leer();
                    std::cout << "EOF:" << fin.clase << ':' << fin.valor
                              << ':' << maximo.tiene_pendiente() << '\n';
                } catch (const std::runtime_error& error) {
                    std::cout << "Error maximo: " << error.what() << '\n';
                    return 1;
                }

                Lector_tokens exceso{"100"};
                bool rechazado = false;
                try {
                    const Token incorrecto = exceso.leer();
                    std::cout << "Exceso aceptado:" << incorrecto.clase << ':' << incorrecto.valor << '\n';
                } catch (const std::runtime_error& error) {
                    rechazado = std::string{error.what()} == "Numero fuera de dominio";
                    std::cout << "Exceso:" << error.what() << ':' << exceso.tiene_pendiente() << '\n';
                }
                if (!rechazado) { return 1; }

                try {
                    Lector_tokens lector{"42+6;"};
                    const Token primero = lector.leer();
                    std::cout << "Numero:" << primero.clase << ':' << primero.valor << '\n';
                    lector.devolver(primero);
                    std::cout << "Reserva numero:" << lector.tiene_pendiente() << '\n';
                    const Token repetido = lector.leer();
                    std::cout << "Repetido:" << repetido.clase << ':' << repetido.valor
                              << ':' << lector.tiene_pendiente() << '\n';
                    const Token signo = lector.leer();
                    std::cout << "Signo:" << signo.clase << ':' << signo.valor << '\n';
                    lector.devolver(signo);
                    std::cout << "Reserva signo:" << lector.tiene_pendiente() << '\n';
                    const Token signo_repetido = lector.leer();
                    std::cout << "Signo repetido:" << signo_repetido.clase << ':' << signo_repetido.valor
                              << ':' << lector.tiene_pendiente() << '\n';
                    const Token siguiente = lector.leer();
                    const Token cierre = lector.leer();
                    const Token fin = lector.leer();
                    std::cout << "Siguiente:" << siguiente.clase << ':' << siguiente.valor << '\n';
                    std::cout << "Cierre:" << cierre.clase << ':' << cierre.valor << '\n';
                    std::cout << "Fin:" << fin.clase << ':' << fin.valor
                              << ':' << lector.tiene_pendiente() << '\n';
                } catch (const std::runtime_error& error) {
                    std::cout << "Error reserva: " << error.what() << '\n';
                    return 1;
                }
                return 0;
            }
        """.trimIndent() + "\n"
        runProgram(
            reference,
            "Maximo:n:99\nReserva maximo:1\nRepetido maximo:n:99:0\nEOF:f:0:0\n" +
                "Exceso:Numero fuera de dominio:0\nNumero:n:42\nReserva numero:1\n" +
                "Repetido:n:42:0\nSigno:+:0\nReserva signo:1\nSigno repetido:+:0:0\n" +
                "Siguiente:n:6\nCierre:;:0\nFin:f:0:0\n",
            sourceTransformation = SourceTransformation("multi-digit-eof-domain-and-token-reserve") { source ->
                assertEquals("Exact published reference used for laboratory completion", referenceSource, source)
                source.substringBefore(mainOpening) + technicalMain
            }
        )
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

    @Test
    fun fileSampleWriteAfterCloseAndRepeatedCloseAreReported() {
        // These are real invalid stream sequences, not physical disk write failures.
        data class StreamVariant(
            val name: String,
            val needle: String,
            val replacement: String,
            val storedText: String
        )
        val baseInput = "{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }"
        val variants = listOf(
            StreamVariant(
                "muestra-cerrada-antes-de-escribir",
                "    muestra << texto_entrada;",
                "    muestra.close();\n    muestra << texto_entrada;",
                ""
            ),
            StreamVariant(
                "muestra-segundo-cierre",
                "    muestra.close();",
                "    muestra.close();\n    muestra.close();",
                baseInput
            )
        )
        for (variant in variants) {
            runProgram(
                AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 4, 2),
                "Error al escribir o cerrar muestra",
                sourceTransformation = SourceTransformation(variant.name) { source ->
                    assertEquals("Unique sample operation for ${variant.name}", 1, source.split(variant.needle).size - 1)
                    source.replace(variant.needle, variant.replacement)
                },
                expectedExecutionExitCode = 1
            ) { workspace ->
                val actualEntries = workspace.listFiles().orEmpty()
                    .filter { it.name.startsWith("inspecciones_") }.map { it.name }.toSet()
                assertEquals("Only the sample may be created for ${variant.name}", setOf("inspecciones_entrada.txt"), actualEntries)
                val sample = File(workspace, "inspecciones_entrada.txt")
                assertTrue("Sample must be a regular file for ${variant.name}", sample.isFile)
                assertEquals("Exact sample after ${variant.name}", variant.storedText, sample.readText(Charsets.UTF_8))
                assertFalse("No output may be created for ${variant.name}", File(workspace, "inspecciones_salida.txt").exists())
            }
        }
    }

    @Test
    fun fileOutputWriteAfterCloseAndRepeatedCloseAreReported() {
        // Input has already been accepted; only the output stream sequence changes.
        // A second close fails even though the first close saved the complete text.
        data class StreamVariant(
            val name: String,
            val needle: String,
            val replacement: String,
            val storedText: String
        )
        val baseInput = "{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }"
        val variants = listOf(
            StreamVariant(
                "salida-cerrada-antes-de-escribir",
                "    guardado << texto_salida;",
                "    guardado.close();\n    guardado << texto_salida;",
                ""
            ),
            StreamVariant(
                "salida-segundo-cierre",
                "    guardado.close();",
                "    guardado.close();\n    guardado.close();",
                baseInput
            )
        )
        for (variant in variants) {
            runProgram(
                AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 4, 2),
                "Error al escribir o cerrar salida",
                sourceTransformation = SourceTransformation(variant.name) { source ->
                    assertEquals("Unique output operation for ${variant.name}", 1, source.split(variant.needle).size - 1)
                    source.replace(variant.needle, variant.replacement)
                },
                expectedExecutionExitCode = 1
            ) { workspace ->
                val expectedFiles = mapOf(
                    "inspecciones_entrada.txt" to baseInput,
                    "inspecciones_salida.txt" to variant.storedText
                )
                val actualEntries = workspace.listFiles().orEmpty()
                    .filter { it.name.startsWith("inspecciones_") }.map { it.name }.toSet()
                assertEquals("Only regular sample and output entries for ${variant.name}", expectedFiles.keys.toSet(), actualEntries)
                for ((filename, expected) in expectedFiles) {
                    val written = File(workspace, filename)
                    assertTrue("Regular private file $filename for ${variant.name}", written.isFile)
                    assertEquals("Exact contents of $filename after ${variant.name}", expected, written.readText(Charsets.UTF_8))
                }
            }
        }
    }

    @Test
    fun fileControlledBadbitRejectsWithoutChangingAcceptedState() {
        // Explicit flag injection checks the technical bad() branch, not a physical read fault.
        // Returning 91 would expose acceptance or mutation before the original exit-1 branch.
        val readOperation = "        aceptado = leer_muestra_completa(entrada, actual);"
        val controlledRead = "        entrada.setstate(std::ios::badbit);\n" +
            "        aceptado = leer_muestra_completa(entrada, actual);\n" +
            "        if (aceptado || !mismos_datos(previo, actual)) { return 91; }"
        runProgram(
            AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 4, 2),
            "Averia de lectura",
            sourceTransformation = SourceTransformation("lectura-badbit-inyectado-estado-intacto") { source ->
                assertEquals("Unique complete-sample read operation", 1, source.split(readOperation).size - 1)
                source.replace(readOperation, controlledRead)
            },
            expectedExecutionExitCode = 1
        ) { workspace ->
            val actualEntries = workspace.listFiles().orEmpty()
                .filter { it.name.startsWith("inspecciones_") }.map { it.name }.toSet()
            assertEquals("Controlled read failure may create only the sample", setOf("inspecciones_entrada.txt"), actualEntries)
            val sample = File(workspace, "inspecciones_entrada.txt")
            assertTrue("Controlled read failure leaves a regular sample", sample.isFile)
            assertEquals(
                "Controlled read failure preserves complete input",
                "{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }",
                sample.readText(Charsets.UTF_8)
            )
            assertFalse("Controlled read failure must not save output", File(workspace, "inspecciones_salida.txt").exists())
        }
    }

    @Test
    fun fileFlowPracticeReferenceSeparatesWriteAndCloseFailures() {
        // Execute the later practice reference exactly as published, with no transformation.
        runProgram(
            AssetProgram("$FILES/lesson_construir_un_programa_de_carga_y_guardado.json", 7, 2),
            "Escritura: 1;Vacio: 1\nCierre: 1;Conservado: 1"
        ) { workspace ->
            val expectedFiles = mapOf(
                "ensayo_escritura.txt" to "",
                "ensayo_cierre.txt" to "PRUEBA"
            )
            val actualEntries = workspace.listFiles().orEmpty()
                .filter { it.name.startsWith("ensayo_") }.map { it.name }.toSet()
            assertEquals("Exactly the two private stream practice files", expectedFiles.keys.toSet(), actualEntries)
            for ((filename, expected) in expectedFiles) {
                val written = File(workspace, filename)
                assertTrue("Regular private stream practice file $filename", written.isFile)
                assertEquals("Exact stream practice content in $filename", expected, written.readText(Charsets.UTF_8))
            }
        }
    }

    @Test
    fun fileReportReferencesPreserveDataFirstTieAndPresentEmptyGroup() {
        val lessonPath = "$FILES/lesson_construir_un_programa_de_carga_y_guardado.json"
        val lessonId = "cpp_fundamentals__archivos_y_programa_completo__construir_un_programa_de_carga_y_guardado"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published file report transfer", lessonId, lesson.getString("id"))
        for ((block, suffix) in listOf(
            2 to "informe_mayor_empate_por_entrada",
            3 to "informe_mayor_presente_vacio"
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(9)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p10b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        // Execute all three complete published sources without transformations.
        // Totals are 4 versus 6; then 5 versus 5 with verde first; then all zero with azul first.
        val programs = listOf(
            Triple(
                AssetProgram(lessonPath, 9, 3),
                "Entrada aceptada\nGrupos: 3\nMayor: verde\nAceptados del grupo: 6\nDatos conservados: 1\nVerificacion: 1",
                "{ conjunto { grupo rojo 6[4;2] } { grupo verde 8[3;1] 1[3;0] } { grupo azul } }"
            ),
            Triple(
                AssetProgram(lessonPath, 10, 2, "output_prediction"),
                "Entrada aceptada\nGrupos: 3\nMayor: verde\nAceptados del grupo: 5\nDatos conservados: 1\nVerificacion: 1",
                "{ conjunto { grupo verde 6[2;1] 0[3;0] } { grupo rojo 8[5;4] } { grupo azul } }"
            ),
            Triple(
                AssetProgram(lessonPath, 10, 3, "output_prediction"),
                "Entrada aceptada\nGrupos: 3\nMayor: azul\nAceptados del grupo: 0\nDatos conservados: 1\nVerificacion: 1",
                "{ conjunto { grupo azul } { grupo rojo 2[0;4] } { grupo verde 9[0;0] } }"
            )
        )
        for ((program, expectedStdout, expectedText) in programs) {
            runProgram(program, expectedStdout) { workspace ->
                val filenames = setOf("inspecciones_entrada.txt", "inspecciones_salida.txt")
                val actualEntries = workspace.listFiles().orEmpty()
                    .filter { it.name.startsWith("inspecciones_") }.map { it.name }.toSet()
                assertEquals("Only the two private report files at ${program.location}", filenames, actualEntries)
                for (filename in filenames) {
                    val written = File(workspace, filename)
                    assertTrue("Regular private report file $filename at ${program.location}", written.isFile)
                    assertEquals("Complete saved report data in $filename at ${program.location}", expectedText, written.readText(Charsets.UTF_8))
                }
            }
        }
    }

    @Test
    fun squareRootDomainAndApproximationReferencesPreserveInputsAndRejectedDestinations() {
        val lessonPath = "funciones_y_contratos/lesson_raiz_cuadrada_dominio_y_resultado_aproximado.json"
        val lessonId = "cpp_fundamentals__funciones_y_contratos__raiz_cuadrada_dominio_y_resultado_aproximado"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published square-root lesson", lessonId, lesson.getString("id"))
        for ((page, block, suffix) in listOf(
            Triple(3, 6, "propiedades_de_raiz_inexacta"),
            Triple(6, 3, "misma_variable_y_copia"),
            Triple(6, 5, "argumento_expresion"),
            Triple(8, 5, "informe_con_cero_rechazo_y_frontera")
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p${page}b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        // Approximate roots are checked through sign, a broad band and square error.
        // The other published programs print only exact roots and contractual counts.
        runProgram(
            AssetProgram(lessonPath, 3, 6, "output_prediction"),
            "No negativo: 1\nEn banda: 1\nCuadrado cercano: 1"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 3),
            "Valida: 1;Destino: 6;Area: 36\nNegativa: 0;Destino: 6;Area: -2\n" +
                "Exceso: 0;Destino: 6;Area: 100.5\nCero: 1;Destino: 0;Area: 0\n"
        )
        runProgram(
            AssetProgram(lessonPath, 6, 3, "output_prediction"),
            "Primera: 1;Dato: 4;Foto: 16\nSegunda: 1;Dato: 2;Foto: 16\nRechazo: 0;Dato: -4"
        )
        // An expression supplies the value while its const base remains unchanged.
        runProgram(
            AssetProgram(lessonPath, 6, 5, "output_prediction"),
            "Aceptada: 1;Lado: 3;Base: 4"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 3),
            "Validas: 4\nRechazadas: 2\nLados hasta 2.5: 2\nDatos conservados: 1\n"
        )
        // Zero must be counted inside the accepted branch of the collection traversal.
        runProgram(
            AssetProgram(lessonPath, 8, 5, "output_prediction"),
            "Validas: 4\nRechazadas: 1\nLados hasta 2.5: 2\nDatos conservados: 1\n"
        )
    }

    @Test
    fun squareRootTechnicalNonfiniteInputsAreRejectedWithoutChangingDestination() {
        // <limits> supplies controlled technical inputs; it is not a curricular prerequisite.
        // Keep the published function verbatim. NaN discriminates the finite guard because
        // neither area < 0 nor area > 100 rejects it. The guarded function never takes its root.
        val technicalMain = """
            int main() {
                double destino{7.0};
                const double nan = std::numeric_limits<double>::quiet_NaN();
                const bool no_numero = calcular_lado(nan, destino);
                std::cout << "NaN: " << no_numero << ";Destino: " << destino << "\n";
                const double infinito = std::numeric_limits<double>::infinity();
                const bool no_finito = calcular_lado(infinito, destino);
                std::cout << "Infinito: " << no_finito << ";Destino: " << destino << "\n";
                return 0;
            }
        """.trimIndent() + "\n"
        runProgram(
            AssetProgram("funciones_y_contratos/lesson_raiz_cuadrada_dominio_y_resultado_aproximado.json", 5, 3),
            "NaN: 0;Destino: 7\nInfinito: 0;Destino: 7\n",
            sourceTransformation = SourceTransformation("technical-nan-and-infinity-destination-seven") { source ->
                val mainOpening = "int main() {"
                assertEquals("Unique published scalar main", 1, source.split(mainOpening).size - 1)
                assertEquals(
                    "Unique published square-root function",
                    1,
                    source.split("bool calcular_lado(double area, double& destino) {").size - 1
                )
                val publishedPrefix = source.substringBefore(mainOpening)
                publishedPrefix + "#include <limits>\n\n" + technicalMain
            }
        )
    }

    @Test
    fun checkedIntegerConversionReferencesPreserveValuesAndRejectFractions() {
        val lessonPath = "funciones_y_contratos/lesson_conversion_comprobada_sin_perdida.json"
        val lessonId = "cpp_fundamentals__funciones_y_contratos__conversion_comprobada_sin_perdida"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published checked-conversion lesson", lessonId, lesson.getString("id"))
        for ((page, block, suffix) in listOf(
            Triple(2, 5, "ida_y_vuelta_de_un_negativo_integral"),
            Triple(5, 3, "rechazos_con_candidato_cero"),
            Triple(5, 5, "expresion_integral_frente_a_base_fraccionaria"),
            Triple(7, 5, "coleccion_con_fraccion_proxima_y_ceros")
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p${page}b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        // All eight complete published sources execute unchanged, including their final LFs.
        runProgram(
            AssetProgram(lessonPath, 1, 4),
            "Original: 12\nCandidato: 12\nVuelta: 12"
        )
        runProgram(
            AssetProgram(lessonPath, 2, 4),
            "Candidato: -4\nVuelta: -4\nMismo valor: 0"
        )
        runProgram(
            AssetProgram(lessonPath, 2, 5, "output_prediction"),
            "Candidato: -6\nVuelta: -6\nMismo valor: 1"
        )
        runProgram(
            AssetProgram(lessonPath, 4, 3),
            "Integral: 1;Destino: -9;Origen: -9\nFraccion positiva: 0;Destino: -9;Origen: 3.75\n" +
                "Fraccion negativa: 0;Destino: -9;Origen: -1.5\nFrontera: 1;Destino: -100;Origen: -100\n" +
                "Fuera: 0;Destino: -100;Origen: -100.5\nCero: 1;Destino: 0;Origen: 0\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 3, "output_prediction"),
            "Fraccion positiva: 0;Destino: 8\nFraccion negativa: 0;Destino: 8\nCero: 1;Destino: 0"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 5, "output_prediction"),
            "Expresion: 1;Destino: 3;Base: 2.25\nBase: 0;Destino: 3;Base: 2.25"
        )
        runProgram(
            AssetProgram(lessonPath, 7, 3),
            "Enteros:-100;0;4;100;\nRechazados: 2\nCeros aceptados: 1\nDatos conservados: 1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 7, 5, "output_prediction"),
            "Enteros:0;5;0;100;\nRechazados: 3\nCeros aceptados: 2\nDatos conservados: 1\n"
        )
    }

    @Test
    fun checkedIntegerConversionTechnicalContractCoversBoundariesAndNonfiniteValues() {
        // Reuse the accredited host harness in one compilation. The published function
        // remains verbatim; <limits>/nextafter supply technical inputs, not student APIs.
        // Its domain guard precedes every cast, including probes far outside int's range.
        val technicalMain = """
            int main() {
                const std::vector<int> anteriores{-37, 0, 64};
                int enteros{0};
                int vecinos{0};
                int no_finitos{0};
                int exteriores{0};
                int fracciones{0};
                bool conservados{true};
                for (int valor{-100}; valor <= 100; ++valor) {
                    const double origen = static_cast<double>(valor);
                    for (const int previo : anteriores) {
                        int destino = previo;
                        const bool aceptada = convertir_entero(origen, destino);
                        if (!aceptada || destino != valor) {
                            std::cerr << "Fallo de integral admitido\n";
                            return 1;
                        }
                    }
                    ++enteros;
                    // Each immediately adjacent representable value differs from this integer.
                    // These are input probes, not a tolerance or a student API prerequisite.
                    const std::vector<double> proximos{
                        std::nextafter(origen, -std::numeric_limits<double>::infinity()),
                        std::nextafter(origen, std::numeric_limits<double>::infinity())
                    };
                    for (const double vecino : proximos) {
                        for (const int previo : anteriores) {
                            int destino = previo;
                            const bool aceptada = convertir_entero(vecino, destino);
                            if (aceptada) {
                                std::cerr << "Fallo: fraccion vecina aceptada\n";
                                return 2;
                            }
                            conservados = conservados && destino == previo;
                        }
                        ++vecinos;
                    }
                }
                const std::vector<double> especiales{
                    std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()
                };
                for (const double origen : especiales) {
                    for (const int previo : anteriores) {
                        int destino = previo;
                        const bool aceptada = convertir_entero(origen, destino);
                        if (aceptada) { return 3; }
                        conservados = conservados && destino == previo;
                    }
                    ++no_finitos;
                }
                const std::vector<double> fuera{
                    -100.5, 100.5, -101.0, 101.0,
                    std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max()
                };
                for (const double origen : fuera) {
                    for (const int previo : anteriores) {
                        int destino = previo;
                        const bool aceptada = convertir_entero(origen, destino);
                        if (aceptada) { return 4; }
                        conservados = conservados && destino == previo;
                    }
                    ++exteriores;
                }
                const std::vector<double> decimales{0.5, -0.5, 99.75, -99.75, 3.0000001, -3.0000001};
                for (const double origen : decimales) {
                    for (const int previo : anteriores) {
                        int destino = previo;
                        const bool aceptada = convertir_entero(origen, destino);
                        if (aceptada) { return 5; }
                        conservados = conservados && destino == previo;
                    }
                    ++fracciones;
                }
                int cero{64};
                const bool cero_numerico = convertir_entero(-0.0, cero) && cero == 0;
                std::cout << "Enteros aceptados: " << enteros << "\n"
                          << "Vecinos rechazados: " << vecinos << "\n"
                          << "No finitos rechazados: " << no_finitos << "\n"
                          << "Fuera rechazados: " << exteriores << "\n"
                          << "Fracciones rechazadas: " << fracciones << "\n"
                          << "Destinos conservados: " << conservados << "\n"
                          << "Cero numerico: " << cero_numerico << "\n";
                return 0;
            }
        """.trimIndent() + "\n"
        runProgram(
            AssetProgram("funciones_y_contratos/lesson_conversion_comprobada_sin_perdida.json", 4, 3),
            "Enteros aceptados: 201\nVecinos rechazados: 402\nNo finitos rechazados: 3\n" +
                "Fuera rechazados: 6\nFracciones rechazadas: 6\nDestinos conservados: 1\nCero numerico: 1\n",
            sourceTransformation = SourceTransformation("technical-integrals-neighbors-and-special-rejections") { source ->
                val mainOpening = "int main() {"
                assertEquals("Unique published checked-conversion main", 1, source.split(mainOpening).size - 1)
                assertEquals(
                    "Unique published checked-conversion function",
                    1,
                    source.split("bool convertir_entero(double origen, int& destino) {").size - 1
                )
                val publishedPrefix = source.substringBefore(mainOpening)
                publishedPrefix + "#include <limits>\n#include <vector>\n\n" + technicalMain
            }
        )
    }

    @Test
    fun deliberateRoundingReferencesPreserveInputsAndUniqueFirstAppearance() {
        val lessonPath = "funciones_y_contratos/lesson_redondeo_deliberado_y_conversion_segura.json"
        val lessonId = "cpp_fundamentals__funciones_y_contratos__redondeo_deliberado_y_conversion_segura"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published deliberate-rounding lesson", lessonId, lesson.getString("id"))
        for ((page, block, suffix) in listOf(
            Triple(2, 5, "truncar_frente_a_redondear_un_negativo"),
            Triple(5, 3, "dos_contratos_mismos_argumentos"),
            Triple(5, 4, "la_receta_positiva_no_resuelve_negativos"),
            Triple(8, 2, "mitades_y_orden_de_primera_aparicion"),
            Triple(8, 5, "cero_aceptado_no_es_rechazo")
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p${page}b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        // These literal oracles follow the contracts, including first occurrence order,
        // each accepted collision and an accepted zero distinct from a rejected input.
        // Execute all eight complete asset sources unchanged, with their published LFs.
        runProgram(
            AssetProgram(lessonPath, 2, 3),
            "Origen: 6.25\nRedondeado: 6"
        )
        runProgram(
            AssetProgram(lessonPath, 2, 5, "output_prediction"),
            "Truncado: -2\nCercano: -3"
        )
        runProgram(
            AssetProgram(lessonPath, 4, 3),
            "Cercano positivo: 1;Destino: 2\nCercano negativo: 1;Destino: -3\n" +
                "Mitad positiva: 1;Destino: 5\nMitad negativa: 1;Destino: -5\n" +
                "Frontera: 1;Destino: 100\nFuera: 0;Destino: 100\nCero: 1;Destino: 0\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 3, "output_prediction"),
            "Fraccion: 0:9|1:5\nCero: 1:0|1:0\nFuera: 0:0|0:0"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 4, "output_prediction"),
            "Receta: -1\nContrato: 1:-2"
        )
        runProgram(
            AssetProgram(lessonPath, 7, 3),
            "Marcas: 1 2 -2 0\nColisiones: 3\nRechazados: 1\nDatos conservados: 1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 2, "output_prediction"),
            "Marcas: 2 3 -2 -3 0\nColisiones: 2\nRechazados: 1\nDatos conservados: 1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 5, "output_prediction"),
            "Marcas: 0 -1\nColisiones: 1\nRechazados: 2\nDatos conservados: 1\n"
        )
    }

    @Test
    fun deliberateRoundingTechnicalContractCoversTiesNeighborsAndNonfiniteValues() {
        // Reuse the author's normative harness, with the published function verbatim.
        // <limits>/nextafter provide technical probes, not new student prerequisites.
        // Expectations come from intervals and signs, never from std::round itself.
        // Interior neighbors of both +/-0.5 must round to zero; a host CRT failure
        // there does not change this contract or the expected Android result.
        val technicalMain = """
            int main() {
                bool cumple{true};
                int enteros{0};
                int mitades_y_vecinos{0};
                int rechazos{0};
                const double infinito = std::numeric_limits<double>::infinity();
                for (int esperado = -100; esperado <= 100; ++esperado) {
                    for (const int previo : std::vector<int>{-37, 0, 64}) {
                        int destino = previo;
                        if (!redondear_entero(static_cast<double>(esperado), destino)
                            || destino != esperado) {
                            cumple = false;
                        }
                    }
                    ++enteros;
                }
                for (int inferior = -100; inferior < 100; ++inferior) {
                    const double mitad = static_cast<double>(inferior) + 0.5;
                    const std::vector<double> origenes{
                        std::nextafter(mitad, -infinito), mitad,
                        std::nextafter(mitad, infinito)
                    };
                    const std::vector<int> esperados{
                        inferior, mitad < 0.0 ? inferior : inferior + 1, inferior + 1
                    };
                    for (int caso = 0; caso < 3; ++caso) {
                        for (const int previo : std::vector<int>{-37, 0, 64}) {
                            int destino = previo;
                            if (!redondear_entero(origenes.at(caso), destino)
                                || destino != esperados.at(caso)) {
                                cumple = false;
                            }
                        }
                        ++mitades_y_vecinos;
                    }
                }
                const std::vector<double> invalidos{
                    std::numeric_limits<double>::quiet_NaN(), infinito, -infinito,
                    std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::lowest(),
                    std::nextafter(100.0, infinito), std::nextafter(-100.0, -infinito),
                    100.25, -100.25, 102.0, -102.0
                };
                for (const double origen : invalidos) {
                    for (const int previo : std::vector<int>{-37, 0, 64}) {
                        int destino = previo;
                        if (redondear_entero(origen, destino) || destino != previo) {
                            cumple = false;
                        }
                    }
                    ++rechazos;
                }
                bool ceros{true};
                for (const double origen : std::vector<double>{0.0, -0.0}) {
                    int destino{11};
                    if (!redondear_entero(origen, destino) || destino != 0) {
                        ceros = false;
                    }
                }
                bool fronteras_interiores{true};
                for (const int extremo : std::vector<int>{-100, 100}) {
                    const double origen = std::nextafter(static_cast<double>(extremo), 0.0);
                    int destino{11};
                    if (!redondear_entero(origen, destino) || destino != extremo) {
                        fronteras_interiores = false;
                    }
                }
                cumple = cumple && ceros && fronteras_interiores;
                std::cout << "Enteros: " << enteros
                          << "\nMitades y vecinos: " << mitades_y_vecinos
                          << "\nRechazos protegidos: " << rechazos
                          << "\nCeros numericos: " << ceros
                          << "\nFronteras interiores: " << fronteras_interiores
                          << "\nContrato: " << cumple << '\n';
                return cumple ? 0 : 1;
            }
        """.trimIndent() + "\n"
        runProgram(
            AssetProgram("funciones_y_contratos/lesson_redondeo_deliberado_y_conversion_segura.json", 4, 3),
            "Enteros: 201\nMitades y vecinos: 600\nRechazos protegidos: 11\n" +
                "Ceros numericos: 1\nFronteras interiores: 1\nContrato: 1\n",
            sourceTransformation = SourceTransformation("technical-rounding-ties-neighbors-and-protected-rejections") { source ->
                val mainOpening = "int main() {"
                assertEquals("Unique published deliberate-rounding main", 1, source.split(mainOpening).size - 1)
                assertEquals(
                    "Unique published deliberate-rounding function",
                    1,
                    source.split("bool redondear_entero(double origen, int& destino) {").size - 1
                )
                val publishedPrefix = source.substringBefore(mainOpening)
                publishedPrefix + "#include <limits>\n#include <vector>\n\n" + technicalMain
            }
        )
    }

    @Test
    fun consecutiveReadingsUseRealInputAndPreservePreviousObservation() {
        val lessonPath = "texto_interpretacion_y_flujos/lesson_comparar_lecturas_con_la_anterior.json"
        val lessonId = "cpp_fundamentals__texto_interpretacion_y_flujos__comparar_lecturas_con_la_anterior"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published consecutive-reading lesson", lessonId, lesson.getString("id"))
        for ((page, block, suffix) in listOf(
            Triple(5, 3, "mayusculas_puntuacion_y_vecinos"),
            Triple(5, 5, "copiar_demasiado_pronto_cambia_la_pregunta"),
            Triple(8, 3, "cr_literal_no_se_borra_al_leer_lineas"),
            Triple(8, 5, "espacios_y_tabuladores_forman_parte_de_la_linea")
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p${page}b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        data class PreparedReadingCase(val name: String, val input: String, val stdout: String)
        fun verifyPreparedCases(workspace: File, cases: List<PreparedReadingCase>) {
            val canonicalWorkspace = workspace.canonicalFile
            val ownedJobs = File(guardedContext().cacheDir, "cpp-jobs").canonicalFile
            assertEquals("Prepared-input cases remain in the isolated compiler job", ownedJobs, canonicalWorkspace.parentFile)
            val module = File(canonicalWorkspace, "program.wasm").canonicalFile
            assertEquals("Prepared-input module remains in its compiler job", canonicalWorkspace, module.parentFile)
            assertTrue("The already compiled reference must remain available", module.isFile)
            val wasmBytes = module.readBytes()
            for (case in cases) {
                // Reuse the exact compiled asset. Each invocation creates a fresh native
                // instance and stdin descriptor; no istringstream replaces std::cin.
                val result = WasiRuntime.run(wasmBytes, canonicalWorkspace, case.input)
                assertEquals("Native prepared-input exit at ${case.name}", 0, result.exitCode)
                assertEquals("Native prepared-input stderr at ${case.name}", "", result.stderr)
                assertEquals("Native prepared-input output at ${case.name}", case.stdout, result.stdout)
                assertEquals(
                    "Prepared input bytes at ${case.name}",
                    case.input,
                    File(canonicalWorkspace, "stdin.txt").readText(Charsets.UTF_8)
                )
            }
        }
        runProgram(
            AssetProgram(lessonPath, 2, 3),
            "Antes de copiar: 0\nDespues de copiar: 1"
        )
        runProgram(
            AssetProgram(lessonPath, 4, 3),
            "Repetida: 3\nRepetida: 4\nPalabras: 5\nRepeticiones: 2\n",
            standardInput = "  faro\tpuente puente\npuente faro  "
        ) { workspace ->
            verifyPreparedCases(workspace, listOf(
                PreparedReadingCase("words-empty", "", "Palabras: 0\nRepeticiones: 0\n"),
                PreparedReadingCase("words-separators-only", " \t\n  ", "Palabras: 0\nRepeticiones: 0\n"),
                PreparedReadingCase("words-first-without-final-LF", "lago", "Palabras: 1\nRepeticiones: 0\n"),
                PreparedReadingCase(
                    "words-nonadjacent-and-last-before-EOF", "a b a a",
                    "Repetida: 4\nPalabras: 4\nRepeticiones: 1\n"
                )
            ))
        }
        runProgram(
            AssetProgram(lessonPath, 5, 3, "output_prediction"),
            "Repetida: 4\nPalabras: 5\nRepeticiones: 1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 5, "output_prediction"),
            "Repetida: 1\nRepetida: 2\nRepetida: 3\nPalabras: 3\nRepeticiones: 3\n"
        )
        runProgram(
            AssetProgram(lessonPath, 7, 3),
            "Repetida: 3\nRepetida: 5\nLineas: 6\nRepeticiones: 2\n",
            standardInput = "\npuerta\npuerta\n\n\npuerta"
        ) { workspace ->
            verifyPreparedCases(workspace, listOf(
                PreparedReadingCase("lines-empty", "", "Lineas: 0\nRepeticiones: 0\n"),
                PreparedReadingCase("lines-first-empty-has-no-previous", "\n", "Lineas: 1\nRepeticiones: 0\n"),
                PreparedReadingCase("lines-two-empty-no-phantom-at-EOF", "\n\n", "Repetida: 2\nLineas: 2\nRepeticiones: 1\n"),
                PreparedReadingCase("lines-first-without-final-LF", "A", "Lineas: 1\nRepeticiones: 0\n"),
                PreparedReadingCase("lines-empty-updates-previous", "A\n\nA", "Lineas: 3\nRepeticiones: 0\n"),
                PreparedReadingCase("lines-spaces-are-content", " \n \n", "Repetida: 2\nLineas: 2\nRepeticiones: 1\n")
            ))
        }
        // Autonomous published predictions retain their explicit istringstream,
        // including the literal CR contrast without external CRLF translation.
        runProgram(
            AssetProgram(lessonPath, 8, 3, "output_prediction"),
            "Repetida: 3\nLineas: 3\nRepeticiones: 1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 5, "output_prediction"),
            "Repetida: 2\nRepetida: 5\nLineas: 6\nRepeticiones: 2\n"
        )
    }

    @Test
    fun selectionAndPairingPublishedProgramsPreservePortableContracts() {
        val lessonPath = "aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json"
        val lessonId = "cpp_fundamentals__aleatoriedad_y_reproducibilidad__seleccionar_sin_repetir_candidatos"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published selection-and-pairing lesson", lessonId, lesson.getString("id"))
        for ((page, block, suffix) in listOf(
            Triple(5, 3, "conservar_elegido_y_delimitar_el_prefijo"),
            Triple(5, 5, "sobrescribir_antes_de_conservar_pierde_el_candidato"),
            Triple(8, 3, "agrupar_conserva_el_orden_y_el_cero"),
            Triple(8, 5, "unicidad_local_no_demuestra_unicidad_global")
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p${page}b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        // The random references print portable properties and relations. Only the
        // prepared-index predictions have fixed selected IDs in their literal oracles.
        runProgram(
            AssetProgram(lessonPath, 4, 3),
            "Cantidad:4\nPropiedades:1\nOriginal:1\nReproduccion:1\n" +
                "Continuacion:1\nVacio:0:1\nRechazo:1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 3, "output_prediction"),
            "Elegido:3\nActivos: 8 6\nTrabajo: 8 6 6 1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 5, "output_prediction"),
            "Guardado:2\nActivos: 2 0 4\n"
        )
        runProgram(
            AssetProgram(lessonPath, 7, 3),
            "Pares:3\nPropiedades:1\nOrden:1\nOriginal:1\nVacio:0:1\nRechazo:1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 3, "output_prediction"),
            "[9:0][4:7]\nOrden:1\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 5, "output_prediction"),
            "Locales:1\nGlobales:0\n"
        )
    }

    @Test
    fun selectionAndPairingTechnicalMatrixPreservesUniqueIdsInputsAndProtectedEngineState() {
        // This is the author's accredited technical tail, not a curricular solution.
        // The checker uses independent per-ID counters and membership observations.
        // No portable random sequence, exact draw count, uniformity statistic or
        // positive-call inequality with an earlier engine state is required.
        val technicalTail = """
            // Technical QA only: independent counters and explicit input matrix.
            bool comprobar(const std::vector<int>& candidatos,
                           const std::vector<int>& elegidos, int cantidad) {
                if (cantidad < 0 || elegidos.size() > 10
                    || static_cast<int>(elegidos.size()) != cantidad) {
                    return false;
                }
                std::vector<int> apariciones(10);
                for (int id : elegidos) {
                    if (id < 0 || id > 9) { return false; }
                    ++apariciones.at(id);
                    if (apariciones.at(id) != 1) { return false; }
                    bool encontrado = false;
                    for (int candidato : candidatos) {
                        if (candidato == id) { encontrado = true; }
                    }
                    if (!encontrado) { return false; }
                }
                return true;
            }

            bool rechaza_seleccion(const std::vector<int>& datos, int cantidad, int semilla) {
                const std::vector<int> original = datos;
                std::mt19937 motor(semilla);
                const std::mt19937 anterior = motor;
                try {
                    const std::vector<int> resultado = seleccionar(motor, datos, cantidad);
                    (void)resultado;
                    return false;
                } catch (const std::invalid_argument&) {
                    return motor == anterior && datos == original;
                }
            }

            bool rechaza_parejas(const std::vector<int>& datos, int cantidad, int semilla) {
                const std::vector<int> original = datos;
                std::mt19937 motor(semilla);
                const std::mt19937 anterior = motor;
                try {
                    const std::vector<Pareja> resultado = formar_parejas(motor, datos, cantidad);
                    (void)resultado;
                    return false;
                } catch (const std::invalid_argument&) {
                    return motor == anterior && datos == original;
                }
            }

            int main() {
                const std::vector<int> semillas{0, 1, 43, 701, 733, 809, 1000};
                int selecciones = 0;
                int parejas = 0;
                int rechazos = 0;
                int ceros = 0;
                bool correcto = true;
                for (int n = 0; n <= 10; ++n) {
                    for (int variante = 0; variante < 3; ++variante) {
                        std::vector<int> datos;
                        for (int i = 0; i < n; ++i) {
                            int id = i;
                            if (variante == 1) { id = n - 1 - i; }
                            if (variante == 2) { id = (i + 2) % n; }
                            datos.push_back(id);
                        }
                        const std::vector<int> original = datos;
                        for (int semilla : semillas) {
                            for (int k = 0; k <= n; ++k) {
                                std::mt19937 motor(semilla);
                                const std::mt19937 inicial = motor;
                                std::mt19937 copia = motor;
                                const std::vector<int> elegido = seleccionar(motor, datos, k);
                                const std::vector<int> repetido = seleccionar(copia, datos, k);
                                correcto = correcto && comprobar(datos, elegido, k)
                                    && datos == original && elegido == repetido && motor == copia;
                                if (k == 0) {
                                    correcto = correcto && motor == inicial;
                                    ++ceros;
                                }
                                std::mt19937 continuar = motor;
                                const int extra = n == 0 ? 0 : 1;
                                const std::vector<int> siguiente = seleccionar(motor, datos, extra);
                                const std::vector<int> replay = seleccionar(continuar, datos, extra);
                                correcto = correcto && comprobar(datos, siguiente, extra)
                                    && siguiente == replay && motor == continuar && datos == original;
                                ++selecciones;
                            }
                            for (int k = 0; k <= n / 2; ++k) {
                                std::mt19937 motor(semilla);
                                const std::mt19937 inicial = motor;
                                std::mt19937 copia = motor;
                                const std::vector<Pareja> grupo = formar_parejas(motor, datos, k);
                                std::vector<int> planos;
                                for (const Pareja& elemento : grupo) {
                                    planos.push_back(elemento.primero);
                                    planos.push_back(elemento.segundo);
                                }
                                const std::vector<int> orden = seleccionar(copia, datos, 2 * k);
                                correcto = correcto && static_cast<int>(grupo.size()) == k
                                    && comprobar(datos, planos, 2 * k) && planos == orden
                                    && motor == copia && datos == original;
                                if (k == 0) {
                                    correcto = correcto && motor == inicial;
                                    ++ceros;
                                }
                                ++parejas;
                            }
                        }
                    }
                }
                const std::vector<std::vector<int>> invalidos{
                    {0, 0}, {5, 2, 5}, {-1, 0}, {0, 10}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0}
                };
                for (const std::vector<int>& datos : invalidos) {
                    for (int semilla : std::vector<int>{0, 43, 997}) {
                        for (int k : std::vector<int>{0, 1}) {
                            correcto = rechaza_seleccion(datos, k, semilla) && correcto;
                            correcto = rechaza_parejas(datos, k, semilla) && correcto;
                            rechazos += 2;
                        }
                    }
                }
                for (int n = 0; n <= 10; ++n) {
                    std::vector<int> datos;
                    for (int i = 0; i < n; ++i) { datos.push_back(i); }
                    for (int semilla : semillas) {
                        for (int k : std::vector<int>{-1, n + 1, 30000}) {
                            correcto = rechaza_seleccion(datos, k, semilla) && correcto;
                            ++rechazos;
                        }
                        for (int k : std::vector<int>{-1, 6, 30000}) {
                            correcto = rechaza_parejas(datos, k, semilla) && correcto;
                            ++rechazos;
                        }
                        if (n / 2 + 1 <= 5) {
                            correcto = rechaza_parejas(datos, n / 2 + 1, semilla) && correcto;
                            ++rechazos;
                        }
                    }
                }
                std::cout << "Selecciones:" << selecciones << '\n';
                std::cout << "Parejas:" << parejas << '\n';
                std::cout << "Rechazos:" << rechazos << '\n';
                std::cout << "Ceros:" << ceros << '\n';
                std::cout << "Contrato:" << correcto << '\n';
                return correcto ? 0 : 1;
            }
        """.trimIndent() + "\n"
        runProgram(
            AssetProgram("aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json", 7, 3),
            "Selecciones:1386\nParejas:756\nRechazos:592\nCeros:462\nContrato:1\n",
            sourceTransformation = SourceTransformation("technical-selection-pairing-input-and-engine-invariants") { source ->
                val mainOpening = "int main() {"
                assertEquals("Unique published selection-and-pairing main", 1, source.split(mainOpening).size - 1)
                assertEquals(
                    "Unique published selection function",
                    1,
                    source.split("std::vector<int> seleccionar(std::mt19937& motor,").size - 1
                )
                assertEquals(
                    "Unique published pairing function",
                    1,
                    source.split("std::vector<Pareja> formar_parejas(std::mt19937& motor,").size - 1
                )
                source.substringBefore(mainOpening) + technicalTail
            }
        )
    }

    @Test
    fun groupingFrequenciesPublishedProgramsPreserveCountsAndAllTies() {
        val lessonPath = "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json"
        val lessonId = "cpp_fundamentals__colecciones_y_recorridos__contar_frecuencias_y_conservar_empates"
        val lesson = guardedContext().assets.open("$COURSE_ASSETS/$lessonPath")
            .bufferedReader(Charsets.UTF_8).use { JSONObject(it.readText()) }
        assertEquals("Identity of the published grouping-frequency lesson", lessonId, lesson.getString("id"))
        for ((page, block, suffix) in listOf(
            Triple(2, 3, "pred_3"),
            Triple(5, 3, "pred_tabla_nueva"),
            Triple(5, 5, "pred_vecinos"),
            Triple(8, 3, "pred_maximo_final"),
            Triple(8, 5, "pred_etiqueta_vacia")
        )) {
            val prediction = lesson.getJSONArray("pages").getJSONObject(page - 1)
                .getJSONArray("blocks").getJSONObject(block - 1)
            assertEquals("Prediction identity at p${page}b$block", "${lessonId}__$suffix", prediction.getString("id"))
        }
        // Literal outputs follow independent counts of each original value. A later
        // larger frequency replaces earlier ties; an equal maximum retains every tie.
        // The empty string is a value, and all original sequences remain observable.
        runProgram(
            AssetProgram(lessonPath, 2, 3, "output_prediction"),
            "Valor:3\nFrecuencia:4\nHay siguiente:1"
        )
        runProgram(
            AssetProgram(lessonPath, 4, 3),
            "Frecuencias:\n[-2]=2\n[6]=3\n[9]=1\nN:6\nDistintos:3\nSuma:6\n" +
                "Original:6;-2;6;9;-2;6;\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 3, "output_prediction"),
            "Frecuencias:\n[-6]=3\n[4]=2\n[9]=3\nN:8\nDistintos:3\nSuma:8\n" +
                "Original:4;-6;4;9;-6;-6;9;9;\n"
        )
        runProgram(
            AssetProgram(lessonPath, 5, 5, "output_prediction"),
            "Iguales al anterior:3\nN:6\n"
        )
        runProgram(
            AssetProgram(lessonPath, 7, 3),
            "Frecuencias:\n[rio]=2\n[sol]=1\n[torre]=2\n[valle]=2\n" +
                "Maxima:2\nMayores:[rio][torre][valle]\nN:7\nDistintas:4\nSuma:7\n" +
                "Original:[torre][rio][rio][torre][valle][valle][sol]\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 3, "output_prediction"),
            "Frecuencias:\n[ala]=2\n[muro]=2\n[rio]=3\n" +
                "Maxima:3\nMayores:[rio]\nN:7\nDistintas:3\nSuma:7\n" +
                "Original:[muro][ala][muro][ala][rio][rio][rio]\n"
        )
        runProgram(
            AssetProgram(lessonPath, 8, 5, "output_prediction"),
            "Frecuencias:\n[]=2\n[eco]=2\n[rio]=1\n" +
                "Maxima:2\nMayores:[][eco]\nN:5\nDistintas:3\nSuma:5\n" +
                "Original:[][eco][eco][][rio]\n"
        )
    }


    @Test
    fun groupingFrequenciesTechnicalMatrixPreservesOriginalsAndAllMaximumTies() {
        val lessonPath = "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json"
        val context = guardedContext()
        val numericSource = loadCompleteProgram(context, AssetProgram(lessonPath, 4, 3))
        val textSource = loadCompleteProgram(context, AssetProgram(lessonPath, 7, 3))
        fun publishedBody(source: String, declaration: String): String {
            val anchor = "int main() {\n    $declaration\n"
            val suffix = "    return 0;\n}\n"
            assertEquals("Unique published main", 1, source.split("int main() {").size - 1)
            assertEquals("Unique published main/declaration anchor", 1, source.split(anchor).size - 1)
            assertEquals("Unique published return/closing suffix", 1, source.split(suffix).size - 1)
            assertTrue("Published return closes the exact program", source.endsWith(suffix))
            // Only capture cout. The grouping, advancement, maximum and tie logic
            // remains the literal body read from the published reference.
            return source.substringAfter(anchor).substringBefore(suffix).replace("std::cout", "salida")
        }
        val numericBody = publishedBody(numericSource, "std::vector<int> original{6, -2, 6, 9, -2, 6};")
        val textBody = publishedBody(
            textSource,
            "std::vector<std::string> original{\"torre\", \"rio\", \"rio\", \"torre\", \"valle\", \"valle\", \"sol\"};"
        )
        // This is the host-accredited technical template, not learner code.
        // Independent map counts of each original produce the complete oracle.
        // The corpus covers empty/final groups, every tie, permutations, empty
        // labels and int extremes without arithmetic on those extreme values.
        val technicalTemplate = """
            std::string esperado_numerico(const std::vector<int>& datos) {
                std::map<int, std::size_t> cuenta;
                for (int valor : datos) { ++cuenta[valor]; }
                std::ostringstream salida;
                salida << "Frecuencias:\n";
                for (const auto& fila : cuenta) {
                    salida << "[" << fila.first << "]=" << fila.second << "\n";
                }
                salida << "N:" << datos.size() << "\nDistintos:" << cuenta.size()
                       << "\nSuma:" << datos.size() << "\nOriginal:";
                for (int valor : datos) { salida << valor << ";"; }
                salida << "\n";
                return salida.str();
            }

            std::string esperado_texto(const std::vector<std::string>& datos) {
                std::map<std::string, std::size_t> cuenta;
                for (const auto& etiqueta : datos) { ++cuenta[etiqueta]; }
                std::size_t maxima = 0;
                std::ostringstream salida;
                salida << "Frecuencias:\n";
                for (const auto& fila : cuenta) {
                    salida << "[" << fila.first << "]=" << fila.second << "\n";
                    if (fila.second > maxima) { maxima = fila.second; }
                }
                salida << "Maxima:" << maxima << "\nMayores:";
                for (const auto& fila : cuenta) {
                    if (fila.second == maxima) { salida << "[" << fila.first << "]"; }
                }
                salida << "\nN:" << datos.size() << "\nDistintas:" << cuenta.size()
                       << "\nSuma:" << datos.size() << "\nOriginal:";
                for (const auto& etiqueta : datos) { salida << "[" << etiqueta << "]"; }
                salida << "\n";
                return salida.str();
            }

            int main() {
                const int minimo = std::numeric_limits<int>::min();
                const int maximo = std::numeric_limits<int>::max();
                std::vector<std::vector<int>> numeros{
                    {}, {5}, {0}, {-8, -8, -8}, {4, 1, 4, 2, 1, 2}, {1, 1, 5, 9},
                    {2, 5, 5, 5}, {9, 9, 1, 1}, {-3, 8, -3, 0, 8, -3, 0},
                    {6, -2, 6, 9, -2, 6}, {minimo, maximo, minimo, 0, maximo},
                    {maximo, minimo}
                };
                std::vector<std::vector<std::string>> etiquetas{
                    {}, {"sol"}, {""}, {"", "a", ""}, {"a", "b", "c"},
                    {"muro", "ala", "muro", "ala", "rio", "rio", "rio"},
                    {"torre", "rio", "rio", "torre", "valle", "valle", "sol"},
                    {"patio", "sala", "patio", "faro", "sala", "patio"},
                    {"z", "z", "z"}, {"rio", "torre", "torre", "rio"},
                    {"faro", "valle", "valle"}, {"", "eco", "eco", "", "rio"}
                };
                const std::vector<int> valores{-5, 0, 8, minimo, maximo};
                const std::vector<std::string> palabras{"", "eco", "rio", "rio", "zeta"};
                const std::vector<std::string> distintas{
                    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
                    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t"
                };
                for (int n = 0; n <= 20; ++n) {
                    for (int patron = 0; patron < 4; ++patron) {
                        std::vector<int> base_n;
                        std::vector<std::string> base_t;
                        for (int i = 0; i < n; ++i) {
                            if (patron == 0) {
                                base_n.push_back(0);
                                base_t.push_back("");
                            } else if (patron == 1) {
                                base_n.push_back(i - 10);
                                base_t.push_back(distintas.at(static_cast<std::size_t>(i)));
                            } else if (patron == 2) {
                                std::size_t posicion = static_cast<std::size_t>(i) % valores.size();
                                base_n.push_back(valores.at(posicion));
                                base_t.push_back(palabras.at(posicion));
                            } else if (i < n / 3) {
                                base_n.push_back(-4);
                                base_t.push_back("ala");
                            } else if (i < 2 * n / 3) {
                                base_n.push_back(2);
                                base_t.push_back("muro");
                            } else {
                                base_n.push_back(99);
                                base_t.push_back("valle");
                            }
                        }
                        numeros.push_back(base_n);
                        etiquetas.push_back(base_t);
                        std::reverse(base_n.begin(), base_n.end());
                        std::reverse(base_t.begin(), base_t.end());
                        numeros.push_back(base_n);
                        etiquetas.push_back(base_t);
                        if (!base_n.empty()) {
                            std::rotate(base_n.begin(), base_n.begin() + 1, base_n.end());
                            std::rotate(base_t.begin(), base_t.begin() + 1, base_t.end());
                        }
                        numeros.push_back(base_n);
                        etiquetas.push_back(base_t);
                    }
                }
                bool correcto = numeros.size() == 264 && etiquetas.size() == 264;
                std::size_t numero_caso = 0;
                for (const auto& caso : numeros) {
                    std::vector<int> original = caso;
                    std::ostringstream salida;
            {{NUMERIC_BODY}}
                    const std::string esperada = esperado_numerico(caso);
                    if (salida.str() != esperada || original != caso) {
                        correcto = false;
                        std::cerr << "Numerico " << numero_caso << "\n";
                    }
                    ++numero_caso;
                }
                std::size_t texto_caso = 0;
                for (const auto& caso : etiquetas) {
                    std::vector<std::string> original = caso;
                    std::ostringstream salida;
            {{TEXT_BODY}}
                    const std::string esperada = esperado_texto(caso);
                    if (salida.str() != esperada || original != caso) {
                        correcto = false;
                        std::cerr << "Texto " << texto_caso << "\n";
                    }
                    ++texto_caso;
                }
                std::cout << "Numeros:" << numero_caso << "\nEtiquetas:" << texto_caso
                          << "\nContrato:" << correcto << "\n";
                return correcto ? 0 : 1;
            }
        """.trimIndent() + "\n"
        runProgram(
            AssetProgram(lessonPath, 7, 3),
            "Numeros:264\nEtiquetas:264\nContrato:1\n",
            sourceTransformation = SourceTransformation("technical-grouping-frequencies-originals-and-all-ties") { source ->
                assertEquals("Exact published text reference used by the technical template", textSource, source)
                val mainOpening = "int main() {\n"
                assertEquals("Unique published text main opening", 1, source.split(mainOpening).size - 1)
                assertEquals("Unique numeric-body placeholder", 1, technicalTemplate.split("{{NUMERIC_BODY}}").size - 1)
                assertEquals("Unique text-body placeholder", 1, technicalTemplate.split("{{TEXT_BODY}}").size - 1)
                val technicalTail = technicalTemplate
                    .replace("{{NUMERIC_BODY}}", numericBody)
                    .replace("{{TEXT_BODY}}", textBody)
                assertFalse("Unresolved technical placeholder", technicalTail.contains("{{"))
                source.substringBefore(mainOpening) +
                    "#include <sstream>\n#include <map>\n#include <limits>\n\n" + technicalTail
            }
        )
    }

    private companion object {
        const val ISOLATED_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        const val COURSE_ASSETS = "content/es/programming/cpp_fundamentals"
        const val RANDOM = "aleatoriedad_y_reproducibilidad"
        const val FILES = "archivos_y_programa_completo"
    }
}

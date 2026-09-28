package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONObject

data class ProgressSnapshot(

    val completed: Int,

    val total: Int
) {

    val percentage: Int
        get() {

            if (
                total <= 0
            ) {
                return 0
            }

            return (
                    completed * 100 /
                            total
                    )
        }
}

object ProgressRepository {

    private const val PREFS_NAME =
        "learning_progress"

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

    /*
     * PÁGINA ACTUAL DE UNA LECCIÓN
     */

    fun saveCurrentPage(
        context: Context,
        lessonId: String,
        pageIndex: Int
    ) {

        preferences(context)
            .edit()
            .putInt(
                "lesson_${lessonId}_page",
                pageIndex
            )
            .apply()

        refreshLessonCompletionFromProgress(
            context,
            lessonId
        )
    }

    fun getCurrentPage(
        context: Context,
        lessonId: String
    ): Int {

        return preferences(context)
            .getInt(
                "lesson_${lessonId}_page",
                0
            )
    }

    /*
 * LECCIÓN INICIADA
 *
 * Necesitamos distinguir entre:
 *
 * - una lección jamás abierta
 * - una lección abierta que está en la página 1
 *
 * En ambos casos currentPage sería 0,
 * por eso este estado es independiente.
 */
    fun markLessonStarted(
        context: Context,
        lessonId: String
    ) {

        preferences(context)
            .edit()
            .putBoolean(
                "lesson_${lessonId}_started",
                true
            )
            .apply()

        refreshLessonCompletionFromProgress(
            context,
            lessonId
        )
    }


    fun isLessonStarted(
        context: Context,
        lessonId: String
    ): Boolean {

        return preferences(context)
            .getBoolean(
                "lesson_${lessonId}_started",
                false
            )
    }

    /*
 * PROGRESO INTERNO DE UNA LECCIÓN
 *
 * Una página de teoría cuenta como completada
 * cuando está dentro de la posición de lectura
 * actual del usuario.
 *
 * Una página con ejercicios cuenta como
 * completada cuando TODOS sus ejercicios
 * están correctos, independientemente de
 * la posición actual de lectura.
 */
    fun getLessonPageProgress(
        context: Context,
        lessonId: String
    ): ProgressSnapshot {

        val lessonJson =
            try {

                LessonRepository.loadLessonJson(
                    context,
                    lessonId
                )

            } catch (
                exception: Exception
            ) {

                return ProgressSnapshot(
                    completed = 0,
                    total = 0
                )
            }

        val pages =
            lessonJson.optJSONArray(
                "pages"
            )
                ?: return ProgressSnapshot(
                    completed = 0,
                    total = 0
                )

        val totalPages =
            pages.length()

        if (
            totalPages <= 0
        ) {

            return ProgressSnapshot(
                completed = 0,
                total = 0
            )
        }

        val started =
            isLessonStarted(
                context,
                lessonId
            )

        val currentPage =
            getCurrentPage(
                context,
                lessonId
            ).coerceIn(
                0,
                totalPages - 1
            )

        var completedPages =
            0

        for (
        pageIndex in
        0 until totalPages
        ) {

            val page =
                pages.optJSONObject(
                    pageIndex
                ) ?: continue

            val blocks =
                page.optJSONArray(
                    "blocks"
                )

            val exerciseBlocks =
                mutableListOf<Int>()

            if (
                blocks != null
            ) {

                for (
                blockIndex in
                0 until blocks.length()
                ) {

                    val block =
                        blocks.optJSONObject(
                            blockIndex
                        ) ?: continue

                    val type =
                        block.optString(
                            "type",
                            ""
                        )

                    if (
                        isExerciseBlockType(
                            type
                        )
                    ) {

                        exerciseBlocks.add(
                            blockIndex
                        )
                    }
                }
            }

            val pageCompleted =
                if (
                    exerciseBlocks.isEmpty()
                ) {

                    /*
                     * PÁGINA DE TEORÍA
                     *
                     * Cuenta únicamente si está
                     * en la posición actual de
                     * lectura o antes de ella.
                     */
                    started &&
                            pageIndex <=
                            currentPage

                } else {

                    /*
                     * PÁGINA CON EJERCICIOS
                     *
                     * La posición de lectura deja
                     * de importar.
                     *
                     * Lo único que importa es que
                     * TODOS sus ejercicios estén
                     * correctamente resueltos.
                     */
                    exerciseBlocks.all {
                            blockIndex ->

                        getExerciseState(
                            context,
                            lessonId,
                            pageIndex,
                            blockIndex
                        )?.optBoolean(
                            "correct",
                            false
                        ) == true
                    }
                }

            if (
                pageCompleted
            ) {

                completedPages++
            }
        }

        return ProgressSnapshot(
            completed =
                completedPages,
            total =
                totalPages
        )
    }

    /*
 * SINCRONIZAR LA FINALIZACIÓN DE UNA LECCIÓN
 * CON SU PROGRESO REAL POR PÁGINAS.
 *
 * La lección está completada cuando todas
 * sus páginas están completadas según las
 * reglas del sistema de progreso.
 */
    fun refreshLessonCompletionFromProgress(
        context: Context,
        lessonId: String
    ) {

        val progress =
            getLessonPageProgress(
                context,
                lessonId
            )

        val completed =
            progress.total > 0 &&
                    progress.completed ==
                    progress.total

        setLessonCompleted(
            context,
            lessonId,
            completed
        )

        val location =
            getLessonLocation(
                lessonId
            ) ?: return

        refreshUnitCompletion(
            context,
            location.courseId,
            location.unitId
        )

        refreshCourseCompletion(
            context,
            location.courseId
        )
    }

    /*
     * ESTADO DE UN EJERCICIO
     */

    fun saveExerciseState(
        context: Context,
        lessonId: String,
        pageIndex: Int,
        blockIndex: Int,
        state: JSONObject
    ) {

        val key =
            exerciseKey(
                lessonId,
                pageIndex,
                blockIndex
            )

        preferences(context)
            .edit()
            .putString(
                key,
                state.toString()
            )
            .apply()

        refreshLessonCompletionFromProgress(
            context,
            lessonId
        )
    }

    fun getExerciseState(
        context: Context,
        lessonId: String,
        pageIndex: Int,
        blockIndex: Int
    ): JSONObject? {

        val key =
            exerciseKey(
                lessonId,
                pageIndex,
                blockIndex
            )

        val json =
            preferences(context)
                .getString(
                    key,
                    null
                )
                ?: return null

        return try {

            JSONObject(
                json
            )

        } catch (
            exception: Exception
        ) {

            null
        }
    }

    /*
     * LECCIÓN COMPLETADA
     */

    fun setLessonCompleted(
        context: Context,
        lessonId: String,
        completed: Boolean
    ) {

        preferences(context)
            .edit()
            .putBoolean(
                "lesson_${lessonId}_completed",
                completed
            )
            .apply()
    }

    fun isLessonCompleted(
        context: Context,
        lessonId: String
    ): Boolean {

        return preferences(context)
            .getBoolean(
                "lesson_${lessonId}_completed",
                false
            )
    }

    /*
     * Esta es la función que utilizaremos
     * cuando el usuario pulse realmente
     * "Terminar lección".
     *
     * No basta con visitar la última página.
     */
    fun completeLesson(
        context: Context,
        lessonId: String
    ) {

        setLessonCompleted(
            context,
            lessonId,
            true
        )

        val location =
            getLessonLocation(
                lessonId
            ) ?: return

        refreshUnitCompletion(
            context,
            location.courseId,
            location.unitId
        )

        refreshCourseCompletion(
            context,
            location.courseId
        )
    }

    fun invalidateLessonCompletion(
        context: Context,
        lessonId: String
    ) {

        /*
         * Si se reinicia o invalida cualquier
         * ejercicio de una lección ya terminada,
         * esa lección deja de contar como completa.
         */
        setLessonCompleted(
            context,
            lessonId,
            false
        )

        val location =
            getLessonLocation(
                lessonId
            ) ?: return

        /*
         * Recalculamos inmediatamente
         * sus niveles superiores.
         */
        refreshUnitCompletion(
            context,
            location.courseId,
            location.unitId
        )

        refreshCourseCompletion(
            context,
            location.courseId
        )
    }

    fun resetExerciseState(
        context: Context,
        lessonId: String,
        pageIndex: Int,
        blockIndex: Int
    ) {

        preferences(context)
            .edit()
            .remove(
                exerciseKey(
                    lessonId,
                    pageIndex,
                    blockIndex
                )
            )
            .apply()

        /*
         * Un ejercicio pendiente significa que
         * la lección debe volver a completarse.
         */
        invalidateLessonCompletion(
            context,
            lessonId
        )
    }

    /*
     * PROGRESO DE UNA UNIDAD
     */

    fun getUnitProgress(
        context: Context,
        courseId: String,
        unitId: String
    ): ProgressSnapshot {

        val lessons =
            try {

                LessonRepository.loadLessons(
                    context,
                    courseId,
                    unitId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        var completed =
            0

        for (
        lesson in lessons
        ) {

            if (
                isLessonCompleted(
                    context,
                    lesson.id
                )
            ) {

                completed++
            }
        }

        return ProgressSnapshot(
            completed =
                completed,

            total =
                lessons.size
        )
    }

    fun getUnitLearningProgress(
        context: Context,
        courseId: String,
        unitId: String
    ): ProgressSnapshot {

        val lessons =
            try {

                LessonRepository.loadLessons(
                    context,
                    courseId,
                    unitId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        if (
            lessons.isEmpty()
        ) {

            return ProgressSnapshot(
                completed = 0,
                total = 0
            )
        }

        /*
         * Cada lección vale 100 puntos.
         *
         * Ejemplo:
         *
         * Lección 1 = 100 %
         * Lección 2 = 40 %
         * Lección 3 = 0 %
         *
         * completed = 140
         * total = 300
         *
         * porcentaje de la unidad = 46 %
         */
        var accumulatedProgress =
            0

        for (
        lesson in lessons
        ) {

            val lessonProgress =
                getLessonPageProgress(
                    context,
                    lesson.id
                )

            accumulatedProgress +=
                lessonProgress.percentage
        }

        return ProgressSnapshot(
            completed =
                accumulatedProgress,

            total =
                lessons.size * 100
        )
    }

    /*
     * UNIDAD COMPLETADA
     */

    fun setUnitCompleted(
        context: Context,
        unitId: String,
        completed: Boolean
    ) {

        preferences(context)
            .edit()
            .putBoolean(
                "unit_${unitId}_completed",
                completed
            )
            .apply()
    }

    fun isUnitCompleted(
        context: Context,
        unitId: String
    ): Boolean {

        return preferences(context)
            .getBoolean(
                "unit_${unitId}_completed",
                false
            )
    }

    fun refreshUnitCompletion(
        context: Context,
        courseId: String,
        unitId: String
    ) {

        val progress =
            getUnitProgress(
                context,
                courseId,
                unitId
            )

        val completed =
            progress.total > 0 &&
                    progress.completed ==
                    progress.total

        setUnitCompleted(
            context,
            unitId,
            completed
        )
    }

    /*
     * PROGRESO DE UN CURSO
     *
     * El porcentaje del curso se calcula
     * utilizando todas sus lecciones.
     *
     * Ejemplo:
     *
     * 24 de 70 lecciones
     * 34 %
     */

    fun getCourseProgress(
        context: Context,
        courseId: String
    ): ProgressSnapshot {

        val units =
            try {

                UnitRepository.loadUnits(
                    context,
                    courseId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        var totalLessons =
            0

        var completedLessons =
            0

        for (
        unit in units
        ) {

            val progress =
                getUnitProgress(
                    context,
                    courseId,
                    unit.id
                )

            totalLessons +=
                progress.total

            completedLessons +=
                progress.completed
        }

        return ProgressSnapshot(
            completed =
                completedLessons,

            total =
                totalLessons
        )
    }

    /*
     * CURSO COMPLETADO
     */

    fun setCourseCompleted(
        context: Context,
        courseId: String,
        completed: Boolean
    ) {

        preferences(context)
            .edit()
            .putBoolean(
                "course_${courseId}_completed",
                completed
            )
            .apply()
    }

    fun isCourseCompleted(
        context: Context,
        courseId: String
    ): Boolean {

        return preferences(context)
            .getBoolean(
                "course_${courseId}_completed",
                false
            )
    }

    fun refreshCourseCompletion(
        context: Context,
        courseId: String
    ) {

        val units =
            try {

                UnitRepository.loadUnits(
                    context,
                    courseId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        if (
            units.isEmpty()
        ) {

            setCourseCompleted(
                context,
                courseId,
                false
            )

            return
        }

        var allCompleted =
            true

        for (
        unit in units
        ) {

            if (
                !isUnitCompleted(
                    context,
                    unit.id
                )
            ) {

                allCompleted =
                    false

                break
            }
        }

        setCourseCompleted(
            context,
            courseId,
            allCompleted
        )
    }

    /*
     * REINICIAR UNA LECCIÓN
     */

    fun resetLesson(
        context: Context,
        lessonId: String
    ) {

        val prefs =
            preferences(
                context
            )

        val editor =
            prefs.edit()

        val prefix =
            "exercise_${lessonId}_"

        for (
        key in prefs.all.keys
        ) {

            if (
                key.startsWith(
                    prefix
                )
            ) {

                editor.remove(
                    key
                )
            }
        }

        editor.remove(
            "lesson_${lessonId}_page"
        )

        editor.remove(
            "lesson_${lessonId}_started"
        )

        editor.remove(
            "lesson_${lessonId}_completed"
        )

        editor.apply()

        /*
         * Si una lección completada se reinicia,
         * su unidad y su curso también deben
         * actualizar inmediatamente su estado.
         */
        val location =
            getLessonLocation(
                lessonId
            ) ?: return

        refreshUnitCompletion(
            context,
            location.courseId,
            location.unitId
        )

        refreshCourseCompletion(
            context,
            location.courseId
        )
    }

    /*
 * REINICIAR UNA UNIDAD COMPLETA
 */

    fun resetUnit(
        context: Context,
        courseId: String,
        unitId: String
    ) {

        val lessons =
            try {

                LessonRepository.loadLessons(
                    context,
                    courseId,
                    unitId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        /*
         * Reiniciamos todas las lecciones
         * pertenecientes a esta unidad.
         */
        for (
        lesson in lessons
        ) {

            resetLesson(
                context,
                lesson.id
            )
        }

        /*
         * La unidad deja de estar completada,
         * incluso si estuviera vacía o todavía
         * no tuviera contenido.
         */
        setUnitCompleted(
            context,
            unitId,
            false
        )

        /*
         * El curso superior también debe
         * recalcular su estado.
         */
        refreshCourseCompletion(
            context,
            courseId
        )
    }


    /*
     * REINICIAR UN CURSO COMPLETO
     */

    fun resetCourse(
        context: Context,
        courseId: String
    ) {

        val units =
            try {

                UnitRepository.loadUnits(
                    context,
                    courseId
                )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        /*
         * Reiniciamos todas las unidades
         * pertenecientes a este curso.
         */
        for (
        unit in units
        ) {

            resetUnit(
                context,
                courseId,
                unit.id
            )
        }

        /*
         * El curso deja de estar marcado
         * como completado.
         *
         * Esto NO toca sus preferencias
         * particulares de navegación.
         */
        setCourseCompleted(
            context,
            courseId,
            false
        )
    }


    /*
     * REINICIAR TODA LA RUTA
     * DE PROGRAMACIÓN
     */

    fun resetProgrammingRoute(
        context: Context
    ) {

        val courses =
            try {

                CourseRepository
                    .loadProgrammingCourses(
                        context
                    )

            } catch (
                exception: Exception
            ) {

                emptyList()
            }

        /*
         * Reiniciamos únicamente los cursos
         * pertenecientes a la ruta Programación.
         *
         * No utilizamos SharedPreferences.clear()
         * porque posteriormente tendremos
         * Matemáticas y Física como otra ruta
         * independiente.
         */
        for (
        course in courses
        ) {

            resetCourse(
                context,
                course.id
            )
        }
    }

    /*
     * Extraemos curso y unidad del ID
     * semántico permanente:
     *
     * curso__unidad__leccion
     */

    private fun getLessonLocation(
        lessonId: String
    ): LessonLocation? {

        val parts =
            lessonId.split(
                "__",
                limit = 3
            )

        if (
            parts.size != 3
        ) {

            return null
        }

        return LessonLocation(

            courseId =
                parts[0],

            unitId =
                "${parts[0]}__${parts[1]}"
        )
    }

    /*
     * CLAVE ÚNICA PARA CADA EJERCICIO
     */

    /*
 * TIPOS DE BLOQUE QUE REPRESENTAN
 * EJERCICIOS REALIZABLES
 *
 * Este es el único lugar que tendremos
 * que ampliar cuando aparezcan nuevos
 * tipos de ejercicio en el futuro.
 */
    private fun isExerciseBlockType(
        type: String
    ): Boolean {

        return when (
            type
        ) {

            "multiple_choice",
            "fill_code",
            "output_prediction",
            "order_code",
            "drag_fill_code",
            "inline_fill_code" ->
                true

            else ->
                false
        }
    }

    private fun exerciseKey(
        lessonId: String,
        pageIndex: Int,
        blockIndex: Int
    ): String {

        return (
                "exercise_" +
                        lessonId +
                        "_page_" +
                        pageIndex +
                        "_block_" +
                        blockIndex
                )
    }

    private data class LessonLocation(

        val courseId: String,

        val unitId: String
    )
}
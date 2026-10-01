package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONObject

object UnitRepository {

    /*
     * COMPATIBILIDAD CON PROGRAMACIÓN.
     *
     * Las llamadas antiguas que únicamente
     * proporcionan context y courseId
     * continúan utilizando Programación.
     */
    fun loadUnits(
        context: Context,
        courseId: String
    ): List<LearningUnit> {

        return loadUnits(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            courseId
        )
    }

    /*
     * CARGAR UNIDADES SEGÚN LA RUTA.
     */
    fun loadUnits(
        context: Context,
        routeId: String,
        courseId: String
    ): List<LearningUnit> {

        val root =
            loadCourseJson(
                context,
                routeId,
                courseId
            )

        val array =
            root.getJSONArray(
                "units"
            )

        val units =
            mutableListOf<LearningUnit>()

        for (
        i in 0 until array.length()
        ) {

            val item =
                array.getJSONObject(
                    i
                )

            units.add(
                LearningUnit(
                    id =
                        item.getString(
                            "id"
                        ),

                    number =
                        item.getInt(
                            "number"
                        ),

                    title =
                        item.getString(
                            "title"
                        )
                )
            )
        }

        return units
    }

    /*
     * COMPATIBILIDAD CON PROGRAMACIÓN.
     */
    fun loadCourseJson(
        context: Context,
        courseId: String
    ): JSONObject {

        return loadCourseJson(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            courseId
        )
    }

    /*
     * CARGAR EL JSON DEL CURSO
     * SEGÚN LA RUTA.
     */
    fun loadCourseJson(
        context: Context,
        routeId: String,
        courseId: String
    ): JSONObject {

        val path =
            "content/es/$routeId/" +
                    "$courseId/" +
                    "course_$courseId.json"

        val json =
            context.assets
                .open(
                    path
                )
                .bufferedReader()
                .use {
                    it.readText()
                }

        return JSONObject(
            json
        )
    }

    /*
     * COMPATIBILIDAD CON PROGRAMACIÓN.
     */
    fun loadUnitJson(
        context: Context,
        courseId: String,
        unitId: String
    ): JSONObject {

        return loadUnitJson(
            context,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            courseId,
            unitId
        )
    }

    /*
     * CARGAR EL JSON DE LA UNIDAD
     * SEGÚN LA RUTA.
     */
    fun loadUnitJson(
        context: Context,
        routeId: String,
        courseId: String,
        unitId: String
    ): JSONObject {

        val prefix =
            "${courseId}__"

        val unitSlug =
            unitId.removePrefix(
                prefix
            )

        val path =
            "content/es/$routeId/" +
                    "$courseId/" +
                    "$unitSlug/" +
                    "unit_$unitSlug.json"

        val json =
            context.assets
                .open(
                    path
                )
                .bufferedReader()
                .use {
                    it.readText()
                }

        return JSONObject(
            json
        )
    }
}
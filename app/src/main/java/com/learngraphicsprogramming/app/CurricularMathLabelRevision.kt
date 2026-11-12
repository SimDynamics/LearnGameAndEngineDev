package com.learngraphicsprogramming.app

import org.json.JSONObject

/**
 * Two closed, published math-label clarifications. These are NOT a general
 * permission to alter answer candidates or representations. Each accepted
 * pair is pinned to the exact full canonical JSON fingerprints reviewed by
 * the curricular validator and to its original lesson/block identity.
 */
internal object CurricularMathLabelRevision {
    private data class Revision(
        val lesson: String, val owner: String, val sourceIndex: Int,
        val before: String, val after: String,
        val beforeLabel: String, val afterLabel: String
    )
    private val known = listOf(
        Revision(
            "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos",
            "block_0007_0001", 0,
            "ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d",
            "20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b",
            "Construcción que termina normalmente",
            "Construcción desde código y unidades que termina normalmente"
        ),
        Revision(
            "cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas",
            "block_0008_0002", 5,
            "fed5b6d2dcfb3321715725fe61a366c8c1f337d1b24a7e3e0b1fd43ef1be0549",
            "d2ec5c530dba31ba0c4d2b3575d609bfbd688fca0dbb4d94a0fd1524a9803187",
            "100+1;", "99+2;"
        )
    )

    fun project(lesson: String, identity: String, active: JSONObject): JSONObject {
        if (active.optString("type") != "math_puzzle") return active
        val spec = known.firstOrNull { it.lesson == lesson && it.owner == identity }
            ?: return active
        // Prior versions already matching the historical origin stay valid.
        val signature = LessonStateLayoutJson.fingerprint(active)
        if (signature != spec.after) return active
        val restored = JSONObject(active.toString())
        val source = restored.getJSONArray("responses").getJSONObject(0)
            .getJSONArray("sources").getJSONObject(spec.sourceIndex)
        require(source.getString("label") == spec.afterLabel)
        source.put("label", spec.beforeLabel)
        require(LessonStateLayoutJson.fingerprint(restored) == spec.before)
        return restored
    }
}

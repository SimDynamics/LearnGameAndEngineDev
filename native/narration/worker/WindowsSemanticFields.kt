package lgaed.narration.windows

import org.json.JSONArray
import org.json.JSONObject

/** Mirrors the presentation inventory, not a recursive walk through validators. */
internal object WindowsSemanticFields {
    private class Inventory {
        val fields = linkedMapOf<String,String>()
        fun objects(values: JSONArray?): List<JSONObject> = if (values == null) emptyList()
            else (0 until values.length()).mapNotNull { values.optJSONObject(it) }
        fun text(obj: JSONObject?, key: String, path: String) {
            (obj?.opt(key) as? String)?.let { fields["$path/$key"] = it }
        }
        fun strings(values: JSONArray?, path: String) {
            if (values != null) for (i in 0 until values.length()) (values.opt(i) as? String)?.let { fields["$path/$i"] = it }
        }
        fun table(obj: JSONObject, path: String) {
            strings(obj.optJSONArray("headers"),"$path/headers")
            obj.optJSONArray("rows")?.let { rows -> for (i in 0 until rows.length()) strings(rows.optJSONArray(i),"$path/rows/$i") }
        }
        fun content(values: JSONArray?, path: String, depth: Int) {
            require(depth <= 32) { "Semantic rich content is too deep" }
            if (values != null) for (i in 0 until values.length()) when (val value = values.opt(i)) {
                is String -> fields["$path/$i"] = value
                is JSONObject -> representation(value,"$path/$i",depth+1)
            }
        }
        fun representation(spec: JSONObject, path: String, depth: Int = 0) {
            val config = if (spec.has("config")) spec.optJSONObject("config") ?: return else spec
            val location = path + if (spec.has("config")) "/config" else ""
            when (if (spec.has("type")) spec.opt("type") as? String else "text") {
                "text", "math_formula" -> for (name in listOf("latex","text","formula","value")) if (config.has(name)) {text(config,name,location);break}
                "math_table" -> table(config,location)
                "rich_answer_option" -> content(config.optJSONArray("content"),"$location/content",depth)
            }
        }
        fun card(item: JSONObject, path: String, reviewed: Boolean) {
            val values = item.optJSONArray("content")
            if (values != null && values.length() > 0) content(values,"$path/content",0)
            else for (name in listOf("label","text","value")) if (item.opt(name) is String) {
                if (item.getString(name).isNotEmpty()) text(item,name,path)
                break
            }
            text(item,"explanation",path)
            if (reviewed) text(item,"reviewFeedback",path)
        }
        fun response(response: JSONObject, path: String) {
            for (name in listOf("label","instruction")) text(response,name,path)
            val kind = response.optString("type","")
            if (kind in setOf("math_numeric_input","math_expression_input") && (response.optJSONArray("fields")?.length() ?: 0) == 0) text(response,"unit",path)
            val groups = when (kind) {
                "math_multiple_choice_single","math_multiple_choice_multi" -> listOf("options")
                "math_matching_drag" -> listOf("sources","targets")
                "math_ordering_drag" -> listOf("items")
                else -> emptyList()
            }
            for (group in groups) response.optJSONArray(group)?.let { items ->
                for (i in 0 until items.length()) items.optJSONObject(i)?.let { card(it,"$path/$group/$i",group=="options") }
            }
            val hasFields = kind in setOf("math_multi_input","math_structured_answer","math_equation_fill_blanks") ||
                kind in setOf("math_numeric_input","math_expression_input") && (response.optJSONArray("fields")?.length() ?: 0) > 0
            if (!hasFields) return
            response.optJSONArray("fields")?.let { values -> for (i in 0 until values.length()) values.optJSONObject(i)?.let { field ->
                val location = "$path/fields/$i"
                for (name in listOf("label","unit")) text(field,name,location)
                if (field.optString("type","numeric") == "fixed") text(field,"text",location)
                if (field.optString("type") == "dropdown") field.optJSONArray("options")?.let { options ->
                    for (j in 0 until options.length()) text(options.optJSONObject(j),"reviewFeedback","$location/options/$j")
                }
            } }
            response.optJSONArray("segments")?.let { segments -> for (i in 0 until segments.length()) segments.optJSONObject(i)?.let {
                if (!it.has("field")) text(it,"text","$path/segments/$i")
            } }
            if (!response.has("segments") && response.optString("layout") == "table" && (response.optJSONArray("rows")?.length() ?: 0) > 0) table(response,path)
        }
        fun block(block: JSONObject) {
            val kind = block.opt("type") as? String ?: return
            when {
                kind in setOf("heading","paragraph","note") -> text(block,"text","")
                kind in setOf("multiple_choice","fill_code","output_prediction","order_code","drag_fill_code","inline_fill_code") -> {
                    for (name in listOf("question","explanation")) text(block,name,"")
                    if (kind=="multiple_choice") strings(block.optJSONArray("options"),"/options")
                    if (block.opt("reviewFeedback") is String) text(block,"reviewFeedback","") else strings(block.optJSONArray("reviewFeedback"),"/reviewFeedback")
                }
                kind=="cpp_compiler" -> {
                    text(block.optJSONObject("compileCheck"),"explanation","/compileCheck")
                    block.optJSONObject("outputCheck")?.optJSONArray("cases")?.let { cases -> for (i in 0 until cases.length()) cases.optJSONObject(i)?.let { case ->
                        val path="/outputCheck/cases/$i";text(case,"explanation",path)
                        case.optJSONArray("reviewFeedback")?.let { reviews -> for (j in 0 until reviews.length()) text(reviews.optJSONObject(j),"text","$path/reviewFeedback/$j") }
                    } }
                }
                kind.startsWith("math_") -> {
                    text(block,if (block.has("prompt")) "prompt" else "question","")
                    for (name in listOf("correct","incorrect")) text(block.optJSONObject("feedback"),name,"/feedback")
                    for (group in listOf("representations","tools")) block.optJSONArray(group)?.let { values ->
                        for (i in 0 until values.length()) values.optJSONObject(i)?.let { representation(it,"/$group/$i") }
                    }
                    if (kind=="math_puzzle") block.optJSONArray("responses")?.let { values ->
                        for (i in 0 until values.length()) values.optJSONObject(i)?.let { response(it,"/responses/$i") }
                    } else response(block,"")
                }
            }
        }
    }
    fun public(block: JSONObject): Map<String,String> = Inventory().also { it.block(block) }.fields
    fun feedbackOrigins(block: JSONObject, fields: Map<String,String>): Set<String> {
        if (!block.optString("type").startsWith("math_")) return emptySet()
        val result = fields.filter { (path,value) -> (path in setOf("/feedback/correct","/feedback/incorrect") || path.endsWith("/reviewFeedback")) && value.isNotBlank() }.keys.toMutableSet()
        fun label(obj: JSONObject, path: String, identity: Any? = obj.opt("id")) {
            val value = obj.opt("label") as? String ?: return
            if ("$path/label" in fields && value.isNotBlank() && value.trim() != identity) result += "$path/label"
        }
        fun response(obj: JSONObject, path: String, standalone: Boolean = false) {
            label(obj,path,if (standalone) "response" else obj.opt("id"));obj.optJSONArray("fields")?.let { values -> for (i in 0 until values.length()) values.optJSONObject(i)?.let { label(it,"$path/fields/$i") } }
        }
        if (block.optString("type")=="math_puzzle") block.optJSONArray("responses")?.let { values ->
            for (i in 0 until values.length()) values.optJSONObject(i)?.let { response(it,"/responses/$i") }
        } else response(block,"",true)
        return result
    }
}

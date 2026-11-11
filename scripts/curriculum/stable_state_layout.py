"""Prepare and verify lossless lesson reorders. No student data or database is opened.

Each immutable identity resolves to its original storage address. State-bearing
blocks (including editors with no curricular ID) therefore keep the same saved
answer/draft keys. A reorder cannot modify response contracts, add or remove activities implicitly.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from importlib import util as import_util

_semantics_spec = import_util.spec_from_file_location("lgaed_text_semantics", Path(__file__).with_name("text_semantics.py"))
text_semantics = import_util.module_from_spec(_semantics_spec)
_semantics_spec.loader.exec_module(text_semantics)
_images_spec = import_util.spec_from_file_location("lgaed_image_resources", Path(__file__).with_name("image_resources.py"))
image_resources = import_util.module_from_spec(_images_spec)
_images_spec.loader.exec_module(image_resources)
_bibliography_spec = import_util.spec_from_file_location("lgaed_bibliography_revision", Path(__file__).with_name("bibliography_revision.py"))
bibliography_revision = import_util.module_from_spec(_bibliography_spec)
_bibliography_spec.loader.exec_module(bibliography_revision)
_format_spec = import_util.spec_from_file_location("lgaed_code_format", Path(__file__).with_name("code_format_compat.py"))
code_format = import_util.module_from_spec(_format_spec)
_format_spec.loader.exec_module(code_format)


class LayoutError(ValueError):
    pass


LOCALES = frozenset({"es", "en", "fr"})
V2_FIELDS = {"version", "locale", "origin", "current", "family", "additions", "editorial"}
V2_OPTIONAL_FIELDS = frozenset({"retired", "retirementHistory", "orderRefinements", "choiceRevisions", "compilerRevisions", "sourceFormatRevisions"})
RETIRABLE_TYPES = frozenset({"heading", "paragraph", "note", "code", "cpp_compiler", "image"})
PRESENTATION_FIELDS = frozenset({"referenceAfter", "activityAfter"})
VISUAL_PRESENTATION_FIELDS = frozenset({"orderVisualSeparators"})
FLOW_FIELDS = frozenset({"compilerPuzzlesRequired", "compilerWorkflowChecks", "compilerRuntimeChecks"})
TEXT_PRESENTATION_FIELDS = frozenset({text_semantics.ROOT_FIELD})
DEPENDENT_ACTIVITY_TYPES = frozenset({"cpp_compiler", "multiple_choice", "fill_code", "output_prediction",
                                      "order_code", "drag_fill_code", "inline_fill_code", "math_puzzle"})


def require(condition: bool, message: str) -> None:
    if not condition:
        raise LayoutError(message)


def fingerprint(value: object) -> str:
    """Matches LessonStateLayoutJson.fingerprint, including UTF-16 key ordering."""
    digest = hashlib.sha256()

    def token(text: str) -> None:
        digest.update(text.encode("utf-8"))

    def string(text: str) -> None:
        encoded = text.encode("utf-8", errors="strict")
        token(f"s{len(encoded)}:")
        digest.update(encoded)

    def visit(node: object, depth: int = 0) -> None:
        require(depth <= 64, "JSON nesting exceeds the runtime limit")
        if node is None:
            token("z")
        elif type(node) is bool:
            token("t" if node else "f")
        elif type(node) is str:
            string(node)
        elif type(node) in (int, float, Decimal):
            number = Decimal(str(node))
            require(number.is_finite(), "A JSON number must be finite")
            # No context-dependent Decimal.normalize rounding of long integers.
            sign, digits, exponent = number.as_tuple()
            require(abs(exponent) <= 10_000 and len(digits) <= 10_000, "Number outside runtime limits")
            text = format(number, "f")
            if "." in text:
                text = text.rstrip("0").rstrip(".")
            if number == 0:
                text = "0"
            token("n" + text + ";")
        elif type(node) is list:
            token("[")
            for item in node:
                visit(item, depth + 1)
            token("]")
        elif type(node) is dict:
            require(all(type(k) is str for k in node), "JSON object keys must be strings")
            token("{")
            for key in sorted(node, key=lambda key: key.encode("utf-16-be")):
                string(key)
                visit(node[key], depth + 1)
            token("}")
        else:
            raise LayoutError("Unsupported JSON value")

    visit(value)
    return digest.hexdigest()


MATH_LABEL_LESSON = "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos"
MATH_LABEL_IDENTITY = "block_0007_0001"
MATH_LABEL_BEFORE = "ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d"
MATH_LABEL_AFTER = "20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b"
MATH_LABEL_REVISIONS = {
    (MATH_LABEL_LESSON, MATH_LABEL_IDENTITY): {
        "before": MATH_LABEL_BEFORE, "after": MATH_LABEL_AFTER,
        "source_index": 0,
        "label_before": "Construcción que termina normalmente",
        "label_after": "Construcción desde código y unidades que termina normalmente",
        "location": ("datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json", 8, 2),
    },
    ("cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas", "block_0008_0002"): {
        "before": "fed5b6d2dcfb3321715725fe61a366c8c1f337d1b24a7e3e0b1fd43ef1be0549",
        "after": "d2ec5c530dba31ba0c4d2b3575d609bfbd688fca0dbb4d94a0fd1524a9803187",
        "source_index": 5, "label_before": "100+1;", "label_after": "99+2;",
        "location": ("texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json", 9, 3),
    },
}


def reviewed_math_label(block: dict) -> dict | None:
    """Identify an exact complete reviewed block, never arbitrary label prose."""
    if block.get("type") != "math_puzzle":
        return None
    digest = fingerprint(block)
    return next((spec for spec in MATH_LABEL_REVISIONS.values() if digest == spec["after"]), None)

# CPP_ACREDITACION_DIAGNOSTICO_INTENCIONAL: a new Windows goal for exactly
# these published editors. Source, owner and historical storage stay fixed.
_DIAGNOSTIC_PREFIX = "cpp_fundamentals__datos_propios_y_estado_valido__"
COMPILE_DIAGNOSTICS = {
    (_DIAGNOSTIC_PREFIX + "consultas_const_y_objetos_constantes", "block_0006_0006"): {
        "diagnostic": "const-member-call",
        "before": "05cdb3e534649bd7e3efd1e080e5199443deace1e074d56d45490a8ff32c4c27",
    },
    (_DIAGNOSTIC_PREFIX + "valores_por_defecto_con_significado", "block_0012_0008"): {
        "diagnostic": "explicit-copy-initialization",
        "before": "ac0365d7452a3a6ff52a0f6c306d01815a09196f9f49e2bc90cdd4ec5dbc9fe9",
    },
    (_DIAGNOSTIC_PREFIX + "valores_por_defecto_con_significado", "block_0012_0011"): {
        "diagnostic": "explicit-argument-conversion",
        "before": "9f7db3f3a1004a235926bc12e06e9d0bf1a93b707f3ca92f6de2876677fd9c58",
    },
    (_DIAGNOSTIC_PREFIX + "valores_por_defecto_con_significado", "block_0014_0011"): {
        "diagnostic": "explicit-assignment",
        "before": "3e8eb728eac0d493c864596ec2364fe6c69ff85ce01408dd0020cec36ad6b087",
    },
    (_DIAGNOSTIC_PREFIX + "argumentos_con_tipos_que_expresan_su_funcion", "block_0006_0006"): {
        "diagnostic": "swapped-named-arguments",
        "before": "b35436bcbac5b5e98731d953fb50499d5e7d3ad66ed1ad88771d56e481a8c9ed",
    },
}
_PROJECT_DIAGNOSTIC_PREFIX = "cpp_fundamentals__funciones_y_contratos__"
PROJECT_COMPILE_DIAGNOSTICS = {
    (_PROJECT_DIAGNOSTIC_PREFIX + "declaraciones_y_firmas", "block_0014_0002"): {
        "diagnostic": "u6-difference-unselected-definition", "project": True,
        "before": "d17f1d73d7217ea45e1468f6ed0d1a5ef3565ecce33410a8022660baff950eba",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "declaraciones_y_firmas", "block_0015_0002"): {
        "diagnostic": "u6-difference-conflicting-return", "project": True,
        "before": "e5b83315920efce2c1a6092721096ef596e2c58d7c96089ba01075bceb3986df",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "declaraciones_y_firmas", "block_0017_0002"): {
        "diagnostic": "u6-difference-missing-header", "project": True,
        "before": "3d0f8c6e8fc84ec145f1c9a6c79ec7d5fbe50d8a70294c9f6effc0b4d8eb755c",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0016_0002"): {
        "diagnostic": "u6-envelopes-missing-header", "project": True,
        "before": "ae23ef7cc266649cbad08efc8736916121dc404e132520a0c386ea73b23d7782",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0017_0002"): {
        "diagnostic": "u6-envelopes-missing-declaration", "project": True,
        "before": "1c69caf83414219854952076e951e9adb3f9b6168503f60113a2d858103ef650",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0018_0002"): {
        "diagnostic": "u6-envelopes-unselected-definitions", "project": True,
        "before": "b263e83bb0b912281964eb60261d33713e9cb6d5df046b375f5f008544c4a405",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0019_0002"): {
        "diagnostic": "u6-envelopes-duplicate-definition", "project": True,
        "before": "72ac435d8a7399bd39c91be5a72ae2034aeb9653a8a78ec45fa3f82e6ac9470e",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0021_0002"): {
        "diagnostic": "u6-series-unknown-call", "project": True,
        "before": "b84a7b928030171e03178f3300a1fd3dbbd86388ff998fcd8ddea9d247f9effe",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0022_0002"): {
        "diagnostic": "u6-series-unselected-definitions", "project": True,
        "before": "157dc0f18f12db33cc3a7787009c408bb067ea833544df42ebdc76e2e2c00e34",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "interfaces_cabeceras_y_programas_con_varios_archivos", "block_0023_0002"): {
        "diagnostic": "u6-series-duplicate-definition", "project": True,
        "before": "26416e82df9c1bd708b840b6d940c79db644f69bcd2acecc56f595f23cf48d20",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "construir_un_proyecto_desde_su_interfaz", "block_0013_0002"): {
        "diagnostic": "u6-shelf-unselected-definition", "project": True,
        "before": "2c568ff3ab456b8111cb9a338fc9c0d6f9ccff45a1e1cd3f840cee872900f7e3",
    },
    (_PROJECT_DIAGNOSTIC_PREFIX + "construir_un_proyecto_desde_su_interfaz", "block_0014_0002"): {
        "diagnostic": "u6-shelf-conflicting-return", "project": True,
        "before": "5b1315f399da68b066291bf96a7f1ed8cfd1aadf3964b2d2ca55cd1f6bae50ec",
    },
}
COMPILE_DIAGNOSTICS.update(PROJECT_COMPILE_DIAGNOSTICS)


def validate_compile_check_contract(block: dict) -> dict | None:
    """Validate a closed goal and its complete published source, without context."""
    if "compileCheck" not in block:
        return None
    require(block.get("type") == "cpp_compiler", "compileCheck belongs to a compiler editor")
    config = block["compileCheck"]
    require(type(config) is dict and {"diagnostic"} <= set(config) <= {"diagnostic", "explanation"},
            "compileCheck requires a diagnostic and optional explanation only")
    diagnostic = config["diagnostic"]
    require(type(diagnostic) is str, "A compiler diagnostic identity must be text")
    spec = next((spec for spec in COMPILE_DIAGNOSTICS.values() if spec["diagnostic"] == diagnostic), None)
    require(spec is not None, "Unknown reviewed compiler diagnostic")
    if "explanation" in config:
        require(type(config["explanation"]) is str and bool(config["explanation"].strip()),
                "A compiler diagnostic explanation must be nonempty text")
    require(not ({"outputCheck", "cmake", "initialStandardInput"} & block.keys()),
            "compileCheck has no output, CMake or standard-input contract")
    if spec.get("project", False):
        require({"projectFiles", "sourceFiles"} <= block.keys(),
                "A reviewed project diagnostic retains its complete files and source selection")
    else:
        require(not ({"projectFiles", "sourceFiles"} & block.keys()),
                "A reviewed single-source diagnostic cannot gain project files or source selection")
    original = {key: value for key, value in block.items() if key != "compileCheck"}
    require(fingerprint(original) == spec["before"], "A reviewed diagnostic must retain its exact complete initial block")
    return spec


INTENTIONAL_EXIT_GOAL = {
    "lesson": "cpp_fundamentals__archivos_y_programa_completo__compartir_flujos_sin_acoplar_el_calculo",
    "identity": "block_0005_0002",
    "before": "d263ac4c841168cb7723bdc2daaeaf6ddc839c4830f17a26f6166ff919fb252c",
    "output": "Entrada invalida|9:4",
}
INTENTIONAL_EXIT_GOALS = (
    INTENTIONAL_EXIT_GOAL,
    {
        "lesson": "cpp_fundamentals__archivos_y_programa_completo__construir_un_programa_de_carga_y_guardado",
        "identity": "block_0004_0017",
        "before": "6f796c5c55683ac266ae3f83c54f935273c341c34da087161276c7e361524ae8",
        "output": "Error al abrir salida",
    },
)


def reviewed_intentional_exit_spec(block: dict) -> dict | None:
    """Only a complete reviewed exploratory editor may gain its return-1 goal.

    The existing source/draft address stays intact; native completion proofs
    independently include stdout and the newly authored nonzero process result.
    """
    if block.get("type") != "cpp_compiler" or "outputCheck" not in block:
        return None
    original = {key: value for key, value in block.items() if key != "outputCheck"}
    before = fingerprint(original)
    spec = next((item for item in INTENTIONAL_EXIT_GOALS if item["before"] == before), None)
    if spec is None:
        return None
    config = block["outputCheck"]
    require(type(config) is dict and set(config) == {"cases"},
            "The reviewed intentional-exit editor needs its exact output contract")
    cases = config["cases"]
    require(type(cases) is list and len(cases) == 1 and type(cases[0]) is dict,
            "The reviewed intentional-exit editor needs exactly one output case")
    case = cases[0]
    require(set(case) == {"standardInput", "outputs", "exitCode", "explanation"}
            and case["standardInput"] == "" and case["outputs"] == [spec["output"]]
            and type(case["exitCode"]) is int and case["exitCode"] == 1
            and type(case["explanation"]) is str and bool(case["explanation"].strip()),
            "The reviewed editor requires exact stdout without a final newline, empty input, return 1 and an explanation")
    return spec


def reviewed_intentional_exit_goal(block: dict) -> bool:
    return reviewed_intentional_exit_spec(block) is not None


CRT_OUTPUT_REVISION = {
    "lesson": "cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia",
    "identity": "block_0005_0004",
    "before": "b475e21c42a6b93cb635f5bfd5197b66b7c1107145a413f2fa9582ab142cda35",
    "after": "53648380e15d1148a779de47c0f94dc08f63e2319f79bdc08e72d25a6992ed31",
}


def without_compiler_guidance(block: dict) -> dict:
    """Project only compiler guidance, retaining every executable byte.

    Case guides and diagnostic explanations already have editorial semantics
    in _compiler_payload; diagnostic identities themselves remain protected.
    The CRT review must use that same boundary before recognizing its exact
    output alias; presentation cannot unreview or replace the execution goal.
    Content validators still check each guide's supported structure.
    """
    result = deepcopy(block)
    diagnostic = result.get("compileCheck")
    if type(diagnostic) is dict:
        diagnostic.pop("explanation", None)
    config = result.get("outputCheck")
    if type(config) is dict and type(config.get("cases")) is list:
        for case in config["cases"]:
            if type(case) is dict:
                case.pop("explanation", None)
                case.pop("reviewFeedback", None)
    return result


def reviewed_crt_output_variant(block: dict) -> bool:
    return block.get("type") == "cpp_compiler" and fingerprint(without_compiler_guidance(block)) == CRT_OUTPUT_REVISION["after"]


def historical_crt_output_payload(block: dict) -> dict:
    """Project one reviewed extra Windows spelling onto its complete old block.

    Every execution byte is pinned, including the three deliberate CR controls.
    Current case guidance is retained in the projected historical payload.
    This neither normalizes an output nor permits arbitrary new alternatives.
    """
    if not reviewed_crt_output_variant(block):
        return block
    result = deepcopy(block)
    cases = result["outputCheck"]["cases"]
    require(len(cases) == 1 and cases[0]["standardInput"] == "" and len(cases[0]["outputs"]) == 2,
            "The reviewed CRT variant requires its original single empty-input case")
    original, variant = cases[0]["outputs"]
    require(len(original.encode("utf-8")) == 802 and original.count("\n") == 42 and original.count("\r") == 3
            and variant == original.replace("\n", "\r\n") and len(variant.encode("utf-8")) == 844,
            "The reviewed CRT variant must expand LF only and retain every original control")
    cases[0]["outputs"] = [original]
    require(fingerprint(without_compiler_guidance(result)) == CRT_OUTPUT_REVISION["before"], "The CRT variant lost its exact complete original block")
    return result


# CPP_DISTRACTOR_EDITORIAL_CON_ESTADO_U11L5, CPP_PRECISION_EXCEPCIONES_U11L14
# and CPP_PRECISION_OPCION_CONSULTA_U11L18: eight individually reviewed
# Spanish options. A protected digest covers every response/code field, not
# merely this string. The whole first revision also pins its exact guidance.
_CHOICE_PREFIX = "cpp_fundamentals__texto_interpretacion_y_flujos__"
CHOICE_OPTION_REVISIONS = {
    (_CHOICE_PREFIX + "estados_del_flujo_y_datos_validos", "block_0006_0004"): {
        "option": 2,
        "before": "Los enteros 4 y 10 no caben en el tipo int de una implementación conforme.",
        "after": "La entrada necesita siempre un espacio después de 10 para que la última extracción sea válida.",
        "feedback": "Revisa qué garantiza el éxito de la extracción y si detectar el final durante esa misma operación invalida necesariamente el dato obtenido.",
        "full_before": "ccba0a5461601602d2c74520a20b3b800156e23c4156c6f9edb613908d4c3a5f",
        "full_after": "0853764d61cd61df3f1ab10f310ff237a8ee4b057151d5c37e99490963589d3d",
        "protected_before": "5393e161f7486d6803e37a6ce4b3cb9ca5fb832d458b7e3303e3477d462d1b25",
        "protected_after": "e817c3fea65911ada1efdb18b7784a76b63d4b5a915530b56ff79a041a1960d6",
    },
    (_CHOICE_PREFIX + "validar_una_operacion_y_recuperar_la_siguiente", "block_0005_0004"): {
        "option": 2,
        "before": "Porque devolver false borra automáticamente todos los flujos del programa.",
        "after": "Porque el bucle vuelve a construir el flujo exterior en cada vuelta.",
        "feedback": "Localiza dónde se construye origen y dónde se crea el flujo local de análisis. Distingue qué objeto se reutiliza en el bucle y cuál se crea para una sola línea.",
        "full_before": "c694da695d8cb9b889ece1b22570c0e25cfcb37c004a3b22d71f3862a61d373f",
        "full_after": "5dfe0ec554c75abe71df341c5fda4a0c68fa41747bb2d9bbefb753ef0f636713",
        "protected_before": "ddc59680893eb3049921f89156aabbf9cbda4dd5ff60d45e51d1dc03e4068272",
        "protected_after": "8db059b96aad937bf75def2d3d18181620bd2fd2215f7d01d923a7a02ec15b74",
    },
    (_CHOICE_PREFIX + "construir_un_lector_de_entrada_preparada", "block_0000_0004"): {
        "option": 0,
        "before": "El campo Entrada preparada, manteniendo el código del lector.",
        "after": "El campo Entrada, manteniendo el código del lector.",
        "feedback": None,
        "full_before": "1588a86d210ef1c95b08fee6120684afc51caa303e932519d401b13981eb65dc",
        "full_after": "35964088aa38c6231c8febb7b1a00da3a8389e4f1605112af426d03cde09d360",
        "protected_before": "6cf40a01fa8a87c4bfcf577dd41fe2e071a0219a26a7571c2d081677d911a260",
        "protected_after": "2bcddeb382c97368786dc91558d6b53de5a2bbf5320ba1672a919fc95e973f9f",
    },
    (_CHOICE_PREFIX + "tokens_con_categoria_y_valor", "block_0003_0002"): {
        "option": 2,
        "before": "Un diagnóstico porque 8 no pertenece al intervalo de un dígito.",
        "after": "Un token numérico de valor 1; el segundo dígito queda descartado al devolver el primero.",
        "feedback": "Distingue terminar una llamada de consumir el resto del flujo. Sigue desde qué posición empieza la llamada siguiente.",
        "full_before": "9b28a5432859a50e1d336098022614188ad75f81eb7cb2db87284b98838efca1",
        "full_after": "4d598fee502105e520f800f975a57486947851153339a448ea92cdff768ac400",
        "protected_before": "1a1a494aa8c49c0255c0c8634e52835d84f6d5c29a2eb533ed257c13ef18b816",
        "protected_after": "b4e00e593e89935b313392a17d7983b2998e64905c37f38c2a18e3e837c699f8",
    },
    (_CHOICE_PREFIX + "tokens_con_categoria_y_valor", "block_0006_0001"): {
        "option": 2,
        "before": "El inspector de tokens calcula los tres resultados y los convierte en cero.",
        "after": "Los tres se rechazan al combinar tokens, porque todo error de una expresión es de gramática.",
        "feedback": "Un orden de unidades permitido puede incumplir las condiciones de una operación. Distingue su forma de los valores con los que se intenta calcular.",
        "full_before": "8f36e0a7b384b20953b45f76011c01c1fab1766eb641ff11ce7f4c94c62e5a91",
        "full_after": "123f9105afc6bd9cab76c21cde2af2d83b4004a89ec91e8593573ae8fe4dd4e1",
        "protected_before": "2c62ea997577dd3e617968b33746ed53576100e3ae0a16b220cdb9f15b59a917",
        "protected_after": "b3a836a39bae7b92db9b357b530681316c69af866e35c3b5f8b7190eeb026bf3",
    },
    (_CHOICE_PREFIX + "diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0007_0001"): {
        "option": 1,
        "before": "Lanzar un diagnóstico de recuperación y terminar ese recorrido.",
        "after": "Lanzar una excepción de recuperación y terminar ese recorrido.",
        "feedback": None,
        "full_before": "1a4a51d3a8dcc2c9ace27e6d9422d8fff1d2fe5acb7e7da49eed6b4efe6a52f6",
        "full_after": "571e7f17ffd3bf84f8a15c500503af9cfeceed04f8e6a23738c9ed2856a4f1ba",
        "protected_before": "8b90e1ce9f7e75fbc2dab47db07e49977857ae41e648abf8d0d03957e03ac021",
        "protected_after": "9f4dcd56c9d45f79d3aa799b7a29ac7b68ee97797d89e32f68d4152ef963223c",
    },
    (_CHOICE_PREFIX + "diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0003_0004"): {
        "option": 0,
        "before": "Para conservarla como la primera unidad que la gramática todavía debe consumir.",
        "after": "Para conservar ese token inicial, que la gramática todavía debe consumir.",
        "feedback": None,
        "full_before": "a2b0f46e6254179eaf9d67047e4c5c6c1dd7acd1807e260a923a24bc44accc9a",
        "full_after": "cb07aff65f16ac14875e50631d73cd447262afbc2a033cafa9bf5958fefcc8a2",
        "protected_before": "2c203b9684c188f398b35cbcdea5e1ef47001f7c76a84b94df1ff3e5be410863",
        "protected_after": "996400efadbb06a1a18764ee4306065a0d7f2697af3cd863493970744a0b53ce",
        "paired_explanation": {
            "before": "El ciclo miró una unidad sólo para distinguir final de una operación disponible. La devuelve porque pertenece a la regla que va a interpretar la expresión.",
            "after": "El ciclo leyó un token para distinguir el final del texto de la presencia de datos por analizar. Lo devuelve para que la gramática pueda consumirlo al interpretar la operación.",
        },
    },
    (_CHOICE_PREFIX + "una_tabla_persistente_de_nombres", "block_0000_0003"): {
        "option": 0,
        "before": "La consulta sigue siendo pura porque devuelve un valor neutro.",
        "after": "Conserva el estado de la tabla porque el valor devuelto es neutro.",
        "feedback": None,
        "full_before": "673e20034fb06a10a562cfc9c6517d0f980803d7101c53e0a7d9b0efa603817e",
        "full_after": "701e1b22c8409d748ac500845fced4328b27b02c16fd90e6e7b164a7400012d4",
        "protected_before": "defd38f10fdaf2b5a1da80501b346a0383ec537ed1e884fd5be5e07bf37339f0",
        "protected_after": "faef1ddda48892fff0bba2e8eccb4de2dc0df0204db7ca5b82137c7d56d78bb7",
    },
}


# CPP_CASOS_ADICIONALES_CON_PROGRESO: ten closed Spanish output contracts.
# Initial code, owner and every existing case are protected independently of
# editable guidance. Only the second U11L15 starter admits its exact initial
# insertion; the complete original block is archived in the v2 map.
COMPILER_CASE_REVISIONS = {
    (_CHOICE_PREFIX + "construir_un_lector_de_entrada_preparada", "block_0003_0005"): {
        "before": "0ff28cd1a7cb92334d0a2486d5fcd773c7727813c84a082f8908950a6a3399c6",
        "after": "e3d81fc9c5d397dfdd1be6df7271b34cba21d56a696cce43ca5ad1c8e5ae088a",
        "append": [
            {"standardInput": "4:0 aviso", "outputs": ["Datos adicionales | Ocupadas: 3\nAceptadas: 0\nOcupadas: 3\n"]},
            {"standardInput": "4;0 aviso", "outputs": ["Formato invalido | Ocupadas: 3\nAceptadas: 0\nOcupadas: 3\n"]},
        ],
    },
    (_CHOICE_PREFIX + "tokens_con_categoria_y_valor", "block_0004_0004"): {
        "before": "a0633caa9e65905eb3216a603cc7bfcb23b766fcad3715756d47c08796f2940e",
        "after": "0800175e415321a20eb95403a67778d11549225a67ba9732cc9358a031138629",
        "suffix": "Numero: 8\nSigno: -\nNumero: 4\nSigno: *\nSigno: (\nNumero: 2\nSigno: )\nSigno: /\nNumero: 1\nSigno: ;\nFin\n",
    },
    (_CHOICE_PREFIX + "numeros_de_varios_digitos_y_limites", "block_0004_0003"): {
        "before": "c28f8e645fd47269c44c418648e69b765ff614441a5cb4c2e3723e6ac29c7e10",
        "after": "27698af9c281a19311b4c1c070cc49a86bf68223d85175aeff0bae4aaffc3d7a",
        "suffix": "Numero: 7\nRepetido: 7\nSigno: +\nSiguiente: 2\n",
    },
    (_CHOICE_PREFIX + "una_gramatica_para_sumar_y_restar", "block_0004_0003"): {
        "before": "3e793a0dcd1ed13fd57e97c68f4a22f960585f9acf8611c7aa09927a5c8404f2",
        "after": "55b38dc4d94d9cea6ed446040c7e043dde447be7a18ead7735df182de91cee01",
        "suffix": "Resultado: 100\nResultado: -100\nError: Resultado fuera de dominio\nError: Resultado fuera de dominio\n",
    },
    (_CHOICE_PREFIX + "terminos_prioridad_y_division_segura", "block_0004_0003"): {
        "before": "9bddda66a2a7024ef1280fc82a82e7ad59cd189a2b3089138b5f06c06de78f1d",
        "after": "dcd52cc803827c671d2d783d6af8e423e79cb8ef7377a365ff5afbcec05a926e",
        "suffix": "Error: Resultado fuera de dominio\n",
    },
    (_CHOICE_PREFIX + "signos_unarios_parentesis_y_progreso_recursivo", "block_0004_0004"): {
        "before": "ffef99d8c0504d333a81d68da83eb473974d43ba573626a4808eab6015bddd30",
        "after": "efcd239dc6e8f02d04a418f2b81e0c18e54e8125c7dc0c6dcac57e45abc57300",
        "suffix": "Error: Resultado fuera de dominio\nError: Anidamiento excesivo\n",
    },
    (_CHOICE_PREFIX + "diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0004_0004"): {
        "before": "dc68860e953dd941d4495f68d295b6a7cbca70fe286ee356675f405bb8bd2502",
        "after": "cf3c9ab1f070195689b4f2bce4bd635ae8368f760384663a2ebecbb5a1f7bfdd",
        "suffix": "Caso 11\nError: Caracter no admitido\nResultado: 4\n",
    },
    (_CHOICE_PREFIX + "construir_un_interprete_con_historial_y_pruebas", "block_0002_0002"): {
        "before": "ebf1067d19fbf87bce3ddef2bae69442972df180055d5572faef4473eb76c34e",
        "after": "7b30da24db7834b2b8cc9ca2014b05de9dfeea69637764e8a9834dbe90b69f5b",
        "suffix": "Caso 15\nError: Resultado fuera de dominio\nAceptadas: 0\nRechazadas: 1\nSuma: 0\nHistorial:\n"
                  "Caso 16\nError: Anidamiento excesivo\nAceptadas: 0\nRechazadas: 1\nSuma: 0\nHistorial:\n"
                  "Caso 17\nError: Caracter no admitido\nValor: 4\nAceptadas: 1\nRechazadas: 1\nSuma: 4\nHistorial: 4\n"
                  "Caso 18\nValor: 14\nAceptadas: 1\nRechazadas: 0\nSuma: 14\nHistorial: 14\n"
                  "Caso 19\nValor: 3\nAceptadas: 1\nRechazadas: 0\nSuma: 3\nHistorial: 3\n",
    },
    (_CHOICE_PREFIX + "construir_un_interprete_con_historial_y_pruebas", "block_0006_0003"): {
        "before": "b7e77211fde9c243eee599d2711c8cf0cb2f11fbb47de347de298147188a2e47",
        "after": "7b9c4229709dc9caf07a9c0bd56bf496e62b0148195f69797290cd8ac63be83f",
        "initial_insert": '    std::cout << "Caso 5\\n";\n    probar("2;2;");\n',
        "suffix": "Caso 5\nValor: 2\nValor: 2\nAceptadas: 2\nRechazadas: 0\nSuma: 4\nHistorial: 2 2\nPositivos: 2\nNegativos: 0\n",
    },
    (_CHOICE_PREFIX + "resto_y_asociacion_en_el_interprete", "block_0003_0004"): {
        # Work's U11L16 JSON-text SHA pins authenticate these same complete
        # objects; stateLayout uses typed canonical framing, as all other cases.
        "before": "c03fef34aa19969bac10860c7229545551733638be93dcc84104a56e61227b22",
        "after": "247c41d8494fe203bcc83908dacb2c4e563ae6959e81438452991a30c31c561b",
        "suffix": "Caso 10\nValor: -2\nAceptadas: 1\nRechazadas: 0\nSuma: -2\nHistorial: -2\n"
                  "Caso 11\nValor: 14\nAceptadas: 1\nRechazadas: 0\nSuma: 14\nHistorial: 14\n"
                  "Caso 12\nError: Anidamiento excesivo\nAceptadas: 0\nRechazadas: 1\nSuma: 0\nHistorial:\n",
    },
}


# The nine in-program batteries need their complete static reference to grow
# with the same reviewed calls. These are paired with their compiler revision.
COMPILER_REFERENCE_REVISIONS = {
    (_CHOICE_PREFIX + "tokens_con_categoria_y_valor", "block_0004_0004"): {
        "id": "block_0007_0001",
        "before": "ddd301c6c0a3c53e70ac18b616b2a3a1aae584590cf7b9e632d550088f6bde5a",
        "after": "4d2480ce9aa48f34eb3b1777ab5d0ad88524436faa22cf666a0abb3c010edb46",
        "insert": '    probar("8-4*(2)/1;");\n',
    },
    (_CHOICE_PREFIX + "numeros_de_varios_digitos_y_limites", "block_0004_0003"): {
        "id": "block_0008_0001",
        "before": "1b9b8dc7b0973c441205dcff6182317983b4dfa6865f15738fa20d61bc165055",
        "after": "10f38c37b8a211edd53a6a9bb059296360bd2c2fe5c11d865a8b48d29d8b5c07",
        "insert": '    trazar("0007+2;");\n',
    },
    (_CHOICE_PREFIX + "una_gramatica_para_sumar_y_restar", "block_0004_0003"): {
        "id": "block_0008_0001",
        "before": "05a7b84c5fe6eecd11e75fb9548d9051cc7575dbe421894fdb9ef3cab207256d",
        "after": "eee52db854d07ecc2e3319731c18c07940ed543f9836b722ba85ea5e790cc5e4",
        "insert": '    probar("99+1;");\n    probar("0-99-1;");\n    probar("99+2-2;");\n    probar("0-99-2+2;");\n',
    },
    (_CHOICE_PREFIX + "terminos_prioridad_y_division_segura", "block_0004_0003"): {
        "id": "block_0008_0001",
        "before": "44f26849307e799bc3263578b3353b4979218eae494c39a906c2fe09bafb2cb5",
        "after": "a396321ffa32d8e9c4ca4cc1df1f01cf8e8acef762295d2ec6a8e8077460074a",
        "insert": '    probar("11*10/2;");\n',
    },
    (_CHOICE_PREFIX + "signos_unarios_parentesis_y_progreso_recursivo", "block_0004_0004"): {
        "id": "block_0009_0001",
        "before": "70662e3574fbc6db855250d8a9579466ef5827e0ea768477076290e6191b577b",
        "after": "44dc46d8aace0591bb180e11022c9a3b171c20e4b792fdc91da672a6618c0a44",
        "insert": '    probar("11*10/2;");\n    probar("(((((((((1)))))))));");\n',
    },
    (_CHOICE_PREFIX + "diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0004_0004"): {
        "id": "block_0008_0001",
        "before": "61c61981819c377ca9bc8194d889e9f8abfff6d1b572da2ac96a74b8658aa7e6",
        "after": "6a9952bd00bb00a74a3b51fba27757940cadcb72708a0ea3af5e8447660f18d6",
        "insert": '    std::cout << "Caso 11\\n";\n    probar("?z;4;");\n',
    },
    (_CHOICE_PREFIX + "construir_un_interprete_con_historial_y_pruebas", "block_0002_0002"): {
        "id": "block_0009_0001",
        "before": "778473599281a1b2165b4dabb9acedde00904b55d9b907c8dedeb9831de20618",
        "after": "96bcd1d3821c7e80908dd178e855705206d366ead6fbe1831fc891a85969087b",
        "insert": '    std::cout << "Caso 15\\n";\n    probar("11*10/2;");\n'
                  '    std::cout << "Caso 16\\n";\n    probar("(((((((((1)))))))));");\n'
                  '    std::cout << "Caso 17\\n";\n    probar("?z;4;");\n'
                  '    std::cout << "Caso 18\\n";\n    probar("2+3*4;");\n'
                  '    std::cout << "Caso 19\\n";\n    probar("9-4-2;");\n',
    },
    (_CHOICE_PREFIX + "construir_un_interprete_con_historial_y_pruebas", "block_0006_0003"): {
        "id": "block_0010_0001",
        "before": "9f88cd2b376b8b869b2fa3dd6fb569fce823ce3deae64c138a0f9a2c643fcccc",
        "after": "80ee77eccf110d4438d2643a1dd2c2cb9be696d5f50e57ef285b162e37a0a8a1",
        "insert": '    std::cout << "Caso 5\\n";\n    probar("2;2;");\n',
    },
    (_CHOICE_PREFIX + "resto_y_asociacion_en_el_interprete", "block_0003_0004"): {
        "id": "block_0006_0001",
        "before": "14fa456ea18c3478fb9fb2476d70f337bdab5ee3cfad5d6b3224b07bc36db0b2",
        "after": "1e16cb31b5885fa0ae2f3faa6ffd2687391d088f304a035a7cd124278316d2e2",
        "insert": '    std::cout << "Caso 10\\n";\n    probar("-14/5;");\n'
                  '    std::cout << "Caso 11\\n";\n    probar("2+3*4;");\n'
                  '    std::cout << "Caso 12\\n";\n    probar("(((((((((1)))))))));");\n',
    },
}
_COMPILER_REFERENCE_TAIL = "    return 0;\n}\n"


def _reviewed_compiler_reference(block: dict) -> dict | None:
    if block.get("type") != "code":
        return None
    signature = fingerprint(block)
    return next((spec for spec in COMPILER_REFERENCE_REVISIONS.values() if signature == spec["after"]), None)


def historical_compiler_reference_payload(block: dict) -> dict:
    """Restore only a complete reviewed reference after contextual validation."""
    spec = _reviewed_compiler_reference(block)
    if spec is None:
        return block
    result = deepcopy(block)
    tail = spec["insert"] + _COMPILER_REFERENCE_TAIL
    require(result["code"].endswith(tail), "Invalid reviewed compiler reference tail")
    result["code"] = result["code"][:-len(tail)] + _COMPILER_REFERENCE_TAIL
    require(fingerprint(result) == spec["before"], "Invalid reviewed compiler reference")
    return result


def _compiler_payload(block: dict) -> dict:
    payload = deepcopy(historical_crt_output_payload(block))
    if validate_compile_check_contract(payload) is not None:
        payload.pop("compileCheck")
    if reviewed_intentional_exit_goal(payload):
        payload.pop("outputCheck")
    config = payload.get("outputCheck")
    if type(config) is dict and type(config.get("cases")) is list:
        for case in config["cases"]:
            if type(case) is dict:
                case.pop("explanation", None)
                case.pop("reviewFeedback", None)
                if "standardError" in case:
                    require(case["standardError"] == "nonBlank", "Unsupported compiler stderr requirement")
                    case.pop("standardError")
                if "standardErrorExact" in case:
                    require(type(case["standardErrorExact"]) is str,
                            "Unsupported exact runtime stderr requirement")
                    case.pop("standardErrorExact")
    return payload


def _reviewed_compiler(block: dict) -> dict | None:
    if block.get("type") != "cpp_compiler":
        return None
    signature = fingerprint(_compiler_payload(block))
    return next((spec for spec in COMPILER_CASE_REVISIONS.values() if signature == spec["after"]), None)


def historical_compiler_payload(block: dict) -> dict:
    """Project only the ten exact expanded contracts, keeping current guidance."""
    spec = _reviewed_compiler(block)
    if spec is None:
        return block
    result = deepcopy(block)
    cases = result["outputCheck"]["cases"]
    if "append" in spec:
        del cases[-len(spec["append"]):]
    else:
        output = cases[0]["outputs"][0]
        require(output.endswith(spec["suffix"]), "Invalid reviewed compiler suffix")
        cases[0]["outputs"][0] = output[:-len(spec["suffix"])]
    if "initial_insert" in spec:
        tail = spec["initial_insert"] + _COMPILER_REFERENCE_TAIL
        require(result["initialCode"].endswith(tail), "Invalid reviewed compiler initial tail")
        result["initialCode"] = result["initialCode"][:-len(tail)] + _COMPILER_REFERENCE_TAIL
    require(fingerprint(_compiler_payload(result)) == spec["before"], "Invalid reviewed compiler revision")
    return result


def _choice_payload(block: dict) -> dict:
    return {key: value for key, value in block.items()
            if key not in {"question", "explanation", "reviewFeedback"}}


def _reviewed_choice(block: dict) -> dict | None:
    if block.get("type") != "multiple_choice":
        return None
    signature = fingerprint(_choice_payload(block))
    return next((spec for spec in CHOICE_OPTION_REVISIONS.values()
                 if signature == spec["protected_after"]), None)


def historical_choice_payload(block: dict) -> dict:
    """Normalize only eight exact response contracts, without relaxing prose."""
    spec = _reviewed_choice(block)
    if spec is None:
        return block
    result = deepcopy(block)
    result["options"][spec["option"]] = spec["before"]
    require(fingerprint(_choice_payload(result)) == spec["protected_before"], "Invalid reviewed choice revision")
    return result


def _validate_choice_pair(block: dict, spec: dict | None) -> None:
    """Keep the one reviewed token option/explanation in its real version.

    The first full editorial edge alone is insufficient: a forged later edge
    must not mix the new option with the old explanation, including archives.
    Other reviewed choices retain their ordinary editorial-text policy.
    """
    if spec is None or "paired_explanation" not in spec:
        return
    signature = fingerprint(_choice_payload(block))
    if signature in (spec["protected_before"], spec["protected_after"]):
        version = "after" if signature == spec["protected_after"] else "before"
        require(block.get("explanation") == spec["paired_explanation"][version],
                "The reviewed token option and its explanation must belong to the same exact version")


def historical_math_payload(block: dict) -> dict:
    """Closed reviewed label revisions; every other mathematical byte stays protected.

    The full block digest includes puzzle/response/source IDs, matching pairs,
    validators and feedback. This is not a general exemption for display labels.
    """
    spec = reviewed_math_label(block)
    if spec is not None:
        result = deepcopy(block)
        result["responses"][0]["sources"][spec["source_index"]]["label"] = spec["label_before"]
        require(fingerprint(result) == spec["before"], "Invalid reviewed mathematics revision")
        return result
    return block


def state_payload(block: dict) -> dict:
    """Only explicit editorial fields may vary without changing a response contract.

    The curricular validator still decides which prose changes are authorized.
    Closed reviewed options, compiler expansions and their nine references
    normalize to historical contracts; document validation additionally requires
    their v2 context, paired archives and proofs. Maths retains its exact
    contract except one reviewed label.
    """
    kind = block.get("type")
    if kind in {"heading", "paragraph", "note"}:
        # Reading coverage belongs to an authored block identity, not its wording.
        return {key: value for key, value in block.items() if key not in {"text", "emphasis"}}
    if kind == "image":
        return {key: value for key, value in block.items() if key not in {"alt", "caption"}}
    if kind in {"multiple_choice", "fill_code", "output_prediction", "order_code",
                "drag_fill_code", "inline_fill_code"}:
        payload = _choice_payload(block)
        return historical_choice_payload(payload) if kind == "multiple_choice" else payload
    if kind == "cpp_compiler":
        return historical_compiler_payload(_compiler_payload(block))
    return historical_compiler_reference_payload(historical_math_payload(block))


def state_fingerprint(block: dict) -> str:
    return fingerprint(state_payload(block))


def order_line_expansion(block: dict) -> tuple[list[str], list[list[int]], list[int]]:
    """Derive independent source lines without changing any code token.

    Blank lines and original separators remain in the immutable snapshot. A
    written backslash-n in a C++ string is never a line separator. The one
    compact curricular if statement has three exact, character-preserving cuts.
    """
    require(type(block) is dict and block.get("type") == "order_code", "Expected an order_code snapshot")
    fragments, order = block.get("fragments"), block.get("correctOrder")
    require(type(fragments) is list and 0 < len(fragments) <= 100_000
            and all(type(fragment) is str and fragment for fragment in fragments), "Malformed original order fragments")
    require(type(order) is list and len(order) == len(fragments)
            and all(type(index) is int for index in order)
            and sorted(order) == list(range(len(fragments))), "Malformed original correct order")
    program = "\n".join(fragments[index] for index in order)
    require("\\\n" not in program and "\\\r\n" not in program,
            "Order line refinement does not support semantic backslash line continuations")
    cursor = 0
    while (cursor := program.find('R"', cursor)) != -1:
        opening = program.find("(", cursor + 2, cursor + 19)
        delimiter = program[cursor + 2:opening] if opening != -1 else ""
        if opening == -1 or len(delimiter) > 16 or any(
                ord(char) < 33 or ord(char) > 126 or char in "()\\" for char in delimiter):
            cursor += 2
            continue
        closing = program.find(")" + delimiter + '"', opening + 1)
        require(closing != -1 and "\n" not in program[cursor:closing],
                "Order line refinement does not support multiline or incomplete raw string literals")
        cursor = closing + len(delimiter) + 2
    lines, groups = [], []
    for fragment in fragments:
        if fragment == "if (validar(candidata)) { tabla = candidata; }":
            parts = ["if (validar(candidata)) {", " tabla = candidata;", " }"]
        else:
            parts = fragment.replace("\r\n", "\n").split("\n")
        require(not any("\r" in part for part in parts), "Unsupported bare carriage return in order fragments")
        pieces = [part for part in parts if part.strip(" \t")]
        require(pieces, "An original fragment must contain a nonempty code line")
        groups.append(list(range(len(lines), len(lines) + len(pieces))))
        lines.extend(pieces)
    require(len(lines) <= 100_000, "Too many refined order lines")
    return lines, groups, [index for group in order for index in groups[group]]


def order_contract_projection(block: dict, original: dict, *, allow_original: bool = False) -> dict:
    """Validate the displayed refinement and recover only its old array contract."""
    lines, _, order = order_line_expansion(original)
    require(block.get("type") == "order_code", "An order refinement belongs to order_code")
    require(type(block.get("correctOrder")) is list and all(type(index) is int for index in block["correctOrder"]),
            "Refined order indices must be integers")
    if allow_original and block.get("fragments") == original["fragments"] and block.get("correctOrder") == original["correctOrder"]:
        return block
    require(block.get("fragments") == lines and block.get("correctOrder") == order,
            "Refined fragments and correct order must be the exact expansion of their original groups")
    projected = deepcopy(block)
    projected["fragments"] = deepcopy(original["fragments"])
    projected["correctOrder"] = deepcopy(original["correctOrder"])
    return projected


def _order_refinements(document: dict) -> dict[str, dict]:
    return {item["id"]: item["block"] for item in document.get("stateLayout", {}).get("orderRefinements", [])}


def _historical_order_contract(document: dict, identity: str, block: dict, *, allow_original: bool = False) -> dict:
    original = _order_refinements(document).get(identity)
    return order_contract_projection(block, original, allow_original=allow_original) if original is not None else block


def _choice_revisions(document: dict) -> dict[str, dict]:
    return {item["id"]: item["block"] for item in document.get("stateLayout", {}).get("choiceRevisions", [])}


def _historical_choice_contract(document: dict, identity: str, block: dict) -> dict:
    # Only callers that have validated the complete document/transition use
    # this projection. Keep current prose/guidance for its ordinary validator.
    if identity not in _choice_revisions(document):
        return block
    result = historical_choice_payload(block)
    spec = CHOICE_OPTION_REVISIONS.get((document.get("id"), identity))
    if spec is not None and "paired_explanation" in spec:
        _validate_choice_pair(block, spec)
        result["explanation"] = spec["paired_explanation"]["before"]
    return result


def _compiler_revisions(document: dict) -> dict[str, dict]:
    return {item["id"]: item["block"] for item in document.get("stateLayout", {}).get("compilerRevisions", [])}


def _compiler_references(document: dict) -> dict[str, dict]:
    return {reference["id"]: reference["block"]
            for item in document.get("stateLayout", {}).get("compilerRevisions", [])
            for reference in item.get("references", [])}


def _historical_compiler_contract(document: dict, identity: str, block: dict) -> dict:
    # Only validated document/transition callers may use this curricular view.
    if identity in _compiler_revisions(document):
        return historical_compiler_payload(block)
    return historical_compiler_reference_payload(block) if identity in _compiler_references(document) else block


def _preserve_compiler_channel_requirements(before: dict, after: dict) -> None:
    """A runtime-channel objective may be added, never silently removed."""
    for identity, block in before.items():
        if block.get("type") != "cpp_compiler" or identity not in after:
            continue
        if "compileCheck" in block:
            require("compileCheck" in after[identity]
                    and without_compiler_guidance(after[identity])["compileCheck"]
                    == without_compiler_guidance(block)["compileCheck"],
                    "A published compiler diagnostic objective cannot be removed or replaced")
        if (spec := reviewed_intentional_exit_spec(block)) is not None:
            require(reviewed_intentional_exit_spec(after[identity]) == spec,
                    "A published intentional-exit objective cannot be removed or replaced")
        if reviewed_crt_output_variant(block):
            require(reviewed_crt_output_variant(after[identity]),
                    "A published reviewed CRT output spelling cannot be silently removed or replaced")
        old_config, config = block.get("outputCheck"), after[identity].get("outputCheck")
        if type(old_config) is not dict or type(config) is not dict:
            continue  # Full response fingerprints protect removal/type changes.
        old_cases, cases = old_config.get("cases"), config.get("cases")
        if type(old_cases) is not list or type(cases) is not list or len(old_cases) != len(cases):
            continue
        for old_case, case in zip(old_cases, cases):
            if type(old_case) is dict and "standardError" in old_case:
                require(type(case) is dict and case.get("standardError") == old_case["standardError"],
                        "A published compiler stderr objective cannot be removed or weakened")
            if type(old_case) is dict and "standardErrorExact" in old_case:
                require(type(case) is dict and case.get("standardErrorExact") == old_case["standardErrorExact"],
                        "A published exact stderr objective cannot be removed or replaced")


def _pages(document: dict) -> list[dict]:
    require(type(document) is dict, "A lesson must be a JSON object")
    pages = document.get("pages")
    require(type(pages) is list and 0 < len(pages) <= 2048, "Expected nonempty pages")
    require(all(type(p) is dict and type(p.get("blocks", [])) is list for p in pages), "Malformed pages")
    require(sum(len(p.get("blocks", [])) for p in pages) <= 100_000, "Too many blocks")
    require(all(type(b) is dict for p in pages for b in p.get("blocks", [])), "A block must be an object")
    return pages


def prepare(document: dict) -> dict:
    """Bootstrap from a published positional layout, before its first reorder."""
    if "stateLayout" in document:
        validate(document)
        return deepcopy(document)
    # A relationship cannot manufacture its own historical identities merely
    # by passing through the bootstrap helper.
    validate(document)
    result = deepcopy(document)
    origin, current = [], []
    for p, page in enumerate(_pages(document)):
        page_id = f"page_{p:04d}"
        blocks = [{"id": f"block_{p:04d}_{b:04d}", "sha256": state_fingerprint(block)}
                  for b, block in enumerate(page.get("blocks", []))]
        origin.append({"id": page_id, "blocks": blocks})
        current.append({"id": page_id, "blocks": [b["id"] for b in blocks]})
    result["stateLayout"] = {"version": 1, "origin": origin, "current": current}
    validate(result)
    return result


def _without_layout(document: dict) -> dict:
    return {key: value for key, value in document.items() if key != "stateLayout"}


def _identity(value: object) -> str:
    require(type(value) is str and re.fullmatch(r"[A-Za-z0-9_-]{1,128}", value) is not None,
            "Invalid stable identity")
    return value


def _digest(value: object) -> str:
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value) is not None,
            "Invalid block digest")
    return value


def _origin(origin: object) -> tuple[list[str], dict[str, tuple[int, int]], dict[str, str]]:
    require(type(origin) is list and 0 < len(origin) <= 2048, "Invalid historical origin")
    pages, blocks, digests = [], {}, {}
    for p, page in enumerate(origin):
        require(type(page) is dict and set(page) == {"id", "blocks"}, "Malformed original page")
        pages.append(_identity(page["id"]))
        require(type(page["blocks"]) is list, "Malformed original blocks")
        for b, block in enumerate(page["blocks"]):
            require(type(block) is dict and set(block) == {"id", "sha256"}, "Malformed original block")
            identity = _identity(block["id"])
            require(identity not in blocks, "Duplicate original block identity")
            blocks[identity] = (p, b)
            digests[identity] = _digest(block["sha256"])
    require(len(set(pages)) == len(pages), "Duplicate original page identity")
    require(len(blocks) <= 100_000, "Too many original blocks")
    return pages, blocks, digests


def _blocks(document: dict) -> dict[str, dict]:
    return {identity: block
            for page, record in zip(document["pages"], document["stateLayout"]["current"])
            for identity, block in zip(record["blocks"], page.get("blocks", []))}


def _all_blocks(document: dict) -> dict[str, dict]:
    """Historical validation includes archived contracts, never their presentation."""
    return {**_blocks(document), **{item["id"]: item["block"]
                                  for item in document["stateLayout"].get("retired", [])}}


def _describe_locale_origin_difference(own: list[dict], family: list[dict],
                                       locale: str, limit: int = 8) -> str:
    """Report concrete historical map disagreements, never reconcile them.

    Only called after both origins pass the immutable identity/digest contract.
    Positions are 1-based for Work's curricular page/block review.
    """
    differences = []
    if len(own) != len(family):
        differences.append(f"number of pages: origin={len(own)}, family.{locale}.origin={len(family)}")
    for pn, (left_page, right_page) in enumerate(zip(own, family), 1):
        if left_page["id"] != right_page["id"]:
            differences.append(
                f"p{pn} page identity: origin={left_page['id']}, "
                f"family.{locale}.origin={right_page['id']}")
        left_blocks, right_blocks = left_page["blocks"], right_page["blocks"]
        if len(left_blocks) != len(right_blocks):
            differences.append(
                f"p{pn} number of blocks: origin={len(left_blocks)}, "
                f"family.{locale}.origin={len(right_blocks)}")
        for bn, (left, right) in enumerate(zip(left_blocks, right_blocks), 1):
            if left != right:
                differences.append(
                    f"p{pn} b{bn} {left['id']}: "
                    f"origin={left['sha256']} "
                    f"family.{locale}.origin={right['sha256']}"
                    + (f" (family identity={right['id']})"
                       if left["id"] != right["id"] else ""))
            if len(differences) > limit:
                return "; ".join(differences[:limit]) + "; further differences omitted"
        if len(differences) > limit:
            return "; ".join(differences[:limit]) + "; further differences omitted"
    return "; ".join(differences) or "different map metadata"


def _validate_v2(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    pages = _pages(document)
    meta = document["stateLayout"]
    require(V2_FIELDS <= set(meta) <= V2_FIELDS | V2_OPTIONAL_FIELDS, "Invalid stateLayout v2 fields")
    require(type(meta["locale"]) is str and meta["locale"] in LOCALES, "Unsupported stateLayout locale")
    page_ids, historical, digests = _origin(meta["origin"])
    family = meta["family"]
    require(type(family) is dict and family and set(family) <= LOCALES,
            "Malformed historical language family")
    require(meta["locale"] in family, "The language family omits this locale")
    family_ids = set()
    for entry in family.values():
        require(type(entry) is dict and set(entry) == {"sha256", "origin"}, "Malformed language source")
        _digest(entry["sha256"])
        _, identities, _ = _origin(entry["origin"])
        family_ids.update(identities)
    if family[meta["locale"]]["origin"] != meta["origin"]:
        # Keep the rejection and immutable history intact, but identify which
        # original slots Work must authenticate. Never repair hashes here.
        raise LayoutError(
            "This locale must keep its own historical origin: "
            + _describe_locale_origin_difference(
                meta["origin"], family[meta["locale"]]["origin"], meta["locale"]))
    reserved_pages = max(len(entry["origin"]) for entry in family.values())
    additions = meta["additions"]
    require(type(additions) is list and len(historical) + len(additions) <= 100_000,
            "Malformed or excessive activity additions")
    storage = dict(historical)
    added_slots = set()
    for addition in additions:
        require(type(addition) is dict and set(addition) == {"id", "sha256", "storage", "progressPage"},
                "Malformed activity addition")
        identity = _identity(addition["id"])
        require(identity not in storage and identity not in family_ids, "An addition reuses a historical identity")
        slot = addition["storage"]
        require(type(slot) is dict and set(slot) == {"page", "block"}, "Malformed new storage slot")
        require(type(slot["page"]) is int and reserved_pages <= slot["page"] <= 1_000_000
                and type(slot["block"]) is int and 0 <= slot["block"] <= 100_000,
                "New activity storage must be outside every historical language origin")
        position = slot["page"], slot["block"]
        require(position not in added_slots, "Duplicate new storage slot")
        require(addition["progressPage"] in page_ids, "Unknown addition progress page")
        added_slots.add(position)
        storage[identity] = position
        digests[identity] = _digest(addition["sha256"])
    refinements = meta.get("orderRefinements", [])
    require(type(refinements) is list and len(refinements) <= 100_000, "Malformed order refinements")
    require(not refinements or meta["locale"] == "es", "Order refinements are currently authorized only for Spanish")
    refined = {}
    for item in refinements:
        require(type(item) is dict and set(item) == {"id", "block"}, "An order refinement needs its identity and original block")
        identity = _identity(item["id"])
        require(identity in storage and identity not in refined, "Unknown or repeated order refinement identity")
        original = item["block"]
        lines, _, order = order_line_expansion(original)
        require(lines != original["fragments"] or order != original["correctOrder"], "An order refinement must split its original presentation")
        require(state_fingerprint(original) == digests[identity], "The order snapshot does not match its immutable response contract")
        refined[identity] = original
    # A complete, original authored block belongs to the old state signature.
    # Later code bytes are displayed and compiled, but only a lexical-format
    # equivalent version can borrow this immutable historical identity.
    formatted = {}
    revisions = meta.get("sourceFormatRevisions", [])
    require(type(revisions) is list and len(revisions) <= 100_000,
            "Malformed source format history")
    active_blocks = _blocks(document)
    for entry in revisions:
        require(type(entry) is dict and set(entry) == {"id", "block"},
                "A source format revision needs its identity and complete original block")
        identity = _identity(entry["id"])
        require(identity in active_blocks and identity not in formatted,
                "Unknown, inactive or duplicate source format owner")
        original = entry["block"]
        require(type(original) is dict, "Source format history must preserve a full original block")
        source_before = (order_contract_projection(original, refined[identity])
                         if identity in refined else original)
        require(state_fingerprint(source_before) == digests[identity],
                "Source formatting archive changed the historical answer contract")
        try:
            code_format.require_source_lexical(original, active_blocks[identity])
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe source formatting for {identity}: {exc}") from exc
        formatted[identity] = original
    choice_revisions = meta.get("choiceRevisions", [])
    require(type(choice_revisions) is list and len(choice_revisions) <= len(CHOICE_OPTION_REVISIONS),
            "Malformed reviewed choice archive")
    require(not choice_revisions or meta["locale"] == "es", "Choice revisions are authorized only for Spanish")
    choices = {}
    for item in choice_revisions:
        require(type(item) is dict and set(item) == {"id", "block"}, "A choice revision needs its identity and complete original block")
        identity = _identity(item["id"])
        require(identity in storage and identity not in choices, "Unknown or repeated choice revision identity")
        spec = CHOICE_OPTION_REVISIONS.get((document.get("id"), identity))
        require(spec is not None, "This choice revision belongs to its reviewed Spanish lesson and identity")
        original = item["block"]
        require(type(original) is dict and original.get("type") == "multiple_choice"
                and fingerprint(original) == spec["full_before"]
                and fingerprint(_choice_payload(original)) == spec["protected_before"],
                "The choice archive must preserve its exact complete published block")
        require(state_fingerprint(original) == digests[identity], "The choice snapshot does not match its immutable response contract")
        choices[identity] = spec
    compiler_revisions = meta.get("compilerRevisions", [])
    require(type(compiler_revisions) is list and len(compiler_revisions) <= len(COMPILER_CASE_REVISIONS),
            "Malformed reviewed compiler archive")
    require(not compiler_revisions or meta["locale"] == "es", "Compiler revisions are authorized only for Spanish")
    compilers, compiler_references = {}, {}
    for item in compiler_revisions:
        require(type(item) is dict and {"id", "block"} <= set(item) <= {"id", "block", "references"},
                "A compiler revision needs its identity and complete original block")
        identity = _identity(item["id"])
        require(identity in storage and identity not in compilers, "Unknown or repeated compiler revision identity")
        spec = COMPILER_CASE_REVISIONS.get((document.get("id"), identity))
        require(spec is not None, "This compiler revision belongs to its reviewed Spanish lesson and identity")
        original = item["block"]
        require(type(original) is dict and original.get("type") == "cpp_compiler"
                and fingerprint(original) == spec["before"]
                and fingerprint(_compiler_payload(original)) == spec["before"],
                "The compiler archive must preserve its exact complete published block")
        require(state_fingerprint(original) == digests[identity],
                "The compiler snapshot does not match its immutable response contract")
        compilers[identity] = spec
        reference_spec = COMPILER_REFERENCE_REVISIONS.get((document.get("id"), identity))
        require(("references" in item) == (reference_spec is not None),
                "A compiler expansion needs exactly its paired reviewed reference archive")
        if reference_spec is not None:
            references = item["references"]
            require(type(references) is list and len(references) == 1,
                    "A compiler revision needs its one complete original reference")
            reference = references[0]
            require(type(reference) is dict and set(reference) == {"id", "block"}
                    and reference["id"] == reference_spec["id"], "Invalid paired compiler reference identity")
            reference_id = _identity(reference["id"])
            require(reference_id in storage and reference_id not in compiler_references,
                    "Unknown or repeated compiler reference identity")
            original_reference = reference["block"]
            require(type(original_reference) is dict and original_reference.get("type") == "code"
                    and fingerprint(original_reference) == reference_spec["before"]
                    and state_fingerprint(original_reference) == digests[reference_id],
                    "The compiler reference archive must preserve its exact complete published block")
            compiler_references[reference_id] = reference_spec
    current = meta["current"]
    require(type(current) is list and len(current) == len(pages) == len(page_ids),
            "This extension preserves historical pages")
    seen_pages, seen_blocks, mapping = [], [], {}
    for p, record in enumerate(current):
        require(type(record) is dict and set(record) == {"id", "blocks"}, "Malformed current page")
        seen_pages.append(_identity(record["id"]))
        require(type(record["blocks"]) is list and len(record["blocks"]) == len(pages[p].get("blocks", [])),
                "Current layout does not match the visible blocks")
        for b, identity in enumerate(record["blocks"]):
            identity = _identity(identity)
            require(identity in storage, "Unknown block identity")
            reviewed_math = reviewed_math_label(pages[p]["blocks"][b])
            if reviewed_math is not None:
                require(meta["locale"] == "es"
                        and MATH_LABEL_REVISIONS.get((document.get("id"), identity)) == reviewed_math,
                        "The reviewed mathematical label belongs to its original Spanish activity")
            block = pages[p]["blocks"][b]
            reviewed = _reviewed_choice(block)
            _validate_choice_pair(block, CHOICE_OPTION_REVISIONS.get((document.get("id"), identity)))
            if reviewed is not None or identity in choices:
                require(meta["locale"] == "es" and identity in choices
                        and CHOICE_OPTION_REVISIONS.get((document.get("id"), identity)) == reviewed,
                        "A revised choice requires its exact lesson, identity and original archive")
            compiler_reviewed = _reviewed_compiler(block)
            if compiler_reviewed is not None or identity in compilers:
                require(meta["locale"] == "es" and identity in compilers
                        and COMPILER_CASE_REVISIONS.get((document.get("id"), identity)) == compiler_reviewed,
                        "A revised compiler requires its exact lesson, identity and original archive")
            reference_reviewed = _reviewed_compiler_reference(block)
            if reference_reviewed is not None or identity in compiler_references:
                require(meta["locale"] == "es" and identity in compiler_references
                        and compiler_references[identity] == reference_reviewed,
                        "A revised compiler reference requires its matching expanded compiler and archive")
            source = (code_format.source_projection(block, formatted[identity])
                      if identity in formatted else block)
            projected = order_contract_projection(source, refined[identity]) if identity in refined else source
            require(state_fingerprint(projected) == digests[identity],
                    "A response/code contract changed or belongs to another identity")
            seen_blocks.append(identity)
            mapping[p, b] = storage[identity]
    require(len(set(seen_pages)) == len(seen_pages) and set(seen_pages) == set(page_ids), "Page identities changed")
    require(refined.keys() <= set(seen_blocks), "Order refinements must remain active under their existing identities")
    require(choices.keys() <= set(seen_blocks), "Choice revisions must remain active under their existing identities")
    require(compilers.keys() <= set(seen_blocks), "Compiler revisions must remain active under their existing identities")
    require(compiler_references.keys() <= set(seen_blocks), "Compiler references must remain active under their existing identities")
    retired = meta.get("retired", [])
    require(type(retired) is list and len(retired) <= 100_000, "Malformed retired activity archive")
    require(not retired or meta["locale"] == "es", "Retirement is currently authorized only for Spanish")
    retired_ids = set()
    for item in retired:
        require(type(item) is dict and set(item) == {"id", "page", "next", "block"},
                "A retired activity needs its identity, page, successor and complete block")
        identity = _identity(item["id"])
        require(identity in storage and identity not in retired_ids and identity not in seen_blocks,
                "A retired identity is unknown, duplicated or still active")
        require(item["page"] in page_ids, "Unknown retired activity page")
        require(type(item["next"]) is str and (item["next"] == "" or item["next"] in storage)
                and item["next"] != identity, "Invalid retired activity successor")
        require(type(item["block"]) is dict and item["block"].get("type") in RETIRABLE_TYPES,
                "Only compiler activities and their presentation blocks can be retired")
        require(_reviewed_compiler(item["block"]) is None,
                "A revised compiler must retain its active revision archive")
        require(_reviewed_compiler_reference(item["block"]) is None,
                "A revised compiler reference must retain its active revision archive")
        require(state_fingerprint(item["block"]) == digests[identity],
                "The retired response/code contract changed")
        retired_ids.add(identity)
    require(len(set(seen_blocks)) == len(seen_blocks) and set(seen_blocks) | retired_ids == set(storage),
            "Block identities changed")
    successors = {item["id"]: item["next"] for item in retired}
    for identity in successors:
        visited = set()
        while identity in successors:
            require(identity not in visited, "Cyclic retired activity successors")
            visited.add(identity)
            identity = successors[identity]
    editorial = meta["editorial"]
    require(type(editorial) is list and len(editorial) <= 100_000, "Malformed editorial proof")
    actual = _all_blocks(document)
    chains = {}
    for ordinal, proof in enumerate(editorial):
        fields = {"id", "beforeSha256", "afterSha256"}
        require(type(proof) is dict and fields <= set(proof) <= fields | {"returnTo", "bibliography"}
                and not {"returnTo", "bibliography"} <= set(proof),
                "Malformed editorial proof")
        identity = _identity(proof["id"])
        require(identity in storage, "Unknown editorial identity")
        before, after = _digest(proof["beforeSha256"]), _digest(proof["afterSha256"])
        require(before != after, "An editorial proof must describe a real revision")
        if identity in chains:
            require(chains[identity][-1] == before, "Broken editorial proof chain")
        else:
            chains[identity] = [before]
        if after in chains[identity]:
            require("returnTo" in proof, "Cyclic editorial proof requires an explicit return")
            target = proof["returnTo"]
            require(type(target) is dict and set(target) == {"revision", "historySha256", "previousBlock"},
                    "Malformed editorial return")
            revision = target["revision"]
            require(type(revision) is int and 0 <= revision < len(chains[identity]) - 1
                    and chains[identity][revision] == after,
                    "An editorial return must target an earlier revision of its own identity")
            require(_digest(target["historySha256"]) == fingerprint({
                "lessonId": document["id"], "locale": meta["locale"], "editorial": editorial[:ordinal]}),
                "Editorial return history does not match its complete published prefix")
            previous_block = target["previousBlock"]
            require(type(previous_block) is dict and fingerprint(previous_block) == before,
                    "An editorial return must archive its complete exact previous block")
            require(state_fingerprint(previous_block) == state_fingerprint(actual[identity]),
                    "An editorial return previous block cannot change its protected owner contract")
            if "compileCheck" in previous_block:
                require("compileCheck" in actual[identity]
                        and without_compiler_guidance(previous_block)["compileCheck"]
                        == without_compiler_guidance(actual[identity])["compileCheck"],
                        "A published compiler diagnostic cannot be removed or replaced by an editorial return")
        else:
            require("returnTo" not in proof, "A new editorial payload cannot claim a return")
        if "bibliography" in proof:
            archive = proof["bibliography"]
            require(type(archive) is dict and set(archive) == {
                "historySha256", "previousBlock", "currentBlock", "previousTextSemantics", "currentTextSemantics"},
                "Malformed bibliography revision archive")
            require(_digest(archive["historySha256"]) == fingerprint({
                "lessonId": document["id"], "locale": meta["locale"], "editorial": editorial[:ordinal]}),
                "Bibliography archive differs from its complete ordered editorial prefix")
            old_block, new_block = archive["previousBlock"], archive["currentBlock"]
            require(type(old_block) is dict and type(new_block) is dict
                    and fingerprint(old_block) == before and fingerprint(new_block) == after,
                    "Bibliography archive must retain both exact complete block versions")
            require(bibliography_revision.is_bibliography_only_revision(old_block, new_block),
                    "A bibliography archive may only remove optional citation locators from an explanation")
            require(state_fingerprint(old_block) == state_fingerprint(new_block)
                    == state_fingerprint(actual[identity]),
                    "A bibliography archive changed its owner's protected exercise contract")
            _validate_bibliography_semantics(archive["previousTextSemantics"], identity, old_block)
            _validate_bibliography_semantics(archive["currentTextSemantics"], identity, new_block)
        chains[identity].append(after)
    for identity, chain in chains.items():
        require(chain[-1] == fingerprint(actual[identity]), "Editorial proof does not describe the current block")
    for identity, original in formatted.items():
        require(fingerprint(original) in chains.get(identity, []),
                "A source format archive needs an authentic exact editorial predecessor")
    for identity, original in refined.items():
        require(fingerprint(original) in chains.get(identity, [])
                and chains[identity][-1] == fingerprint(actual[identity]),
                "An order refinement needs the full published snapshot and current editorial proof")
    for identity, spec in choices.items():
        chain = chains.get(identity, [])
        require(any(chain[index:index + 2] == [spec["full_before"], spec["full_after"]]
                    for index in range(len(chain) - 1)),
                "A choice revision needs its exact original-to-reviewed editorial edge")
    for identity, spec in compilers.items():
        chain = chains.get(identity, [])
        require(any(chain[index:index + 2] == [spec["before"], spec["after"]]
                    for index in range(len(chain) - 1)),
                "A compiler revision needs its exact original-to-reviewed editorial edge")
    for identity, spec in compiler_references.items():
        chain = chains.get(identity, [])
        require(any(chain[index:index + 2] == [spec["before"], spec["after"]]
                    for index in range(len(chain) - 1)),
                "A compiler reference needs its exact original-to-reviewed editorial edge")
    for identity, block in actual.items():
        reviewed_math = reviewed_math_label(block)
        if reviewed_math is not None:
            require(chains.get(identity) == [reviewed_math["before"], reviewed_math["after"]],
                    "The reviewed mathematical label needs its exact published editorial proof")
    return mapping


def _explanation_semantics(document: dict, identity: str):
    """Keep absence distinct from an explicitly authored prose-only field."""
    return next((deepcopy(field) for field in document.get("textSemantics", {}).get("fields", [])
                 if field.get("block") == identity and field.get("path") == "/explanation"), None)


def _validate_bibliography_semantics(descriptor, identity: str, block: dict) -> None:
    if descriptor is None:
        return
    require(type(descriptor) is dict and set(descriptor) == {"block", "path", "sourceSha256", "spans"}
            and descriptor["block"] == identity and descriptor["path"] == "/explanation",
            "A bibliography archive must own its exact explanation descriptor or explicit absence")
    try:
        text_semantics._descriptor(descriptor, block["explanation"])
    except (ValueError, KeyError, TypeError) as error:
        raise LayoutError("Invalid archived bibliography semantics: " + str(error)) from error


def _editorial_proof(document: dict, previous: dict, locale: str, identity: str,
                     before: dict, after: dict, *, archive_bibliography: bool = True) -> dict:
    proof = {"id": identity, "beforeSha256": fingerprint(before), "afterSha256": fingerprint(after)}
    if archive_bibliography and bibliography_revision.is_bibliography_only_revision(before, after):
        proof["bibliography"] = {
            "historySha256": fingerprint({"lessonId": document["id"], "locale": locale,
                                          "editorial": document["stateLayout"]["editorial"]}),
            "previousBlock": deepcopy(before), "currentBlock": deepcopy(after),
            "previousTextSemantics": _explanation_semantics(previous, identity),
            "currentTextSemantics": _explanation_semantics(document, identity)}
    return proof


def prepare_v2(document: dict, locale: str, family_documents: dict[str, dict],
               previous: dict | None = None, *, editorial_returns: dict[str, int] | None = None) -> dict:
    """Anchor real language histories, without changing any translated source file.

    Source prose may be revised in its published slots; all moves are an explicit
    subsequent ID-only plan. Full before/after proofs make both one safe delivery.
    """
    require(type(document) is dict and type(family_documents) is dict
            and all(type(source) is dict for source in family_documents.values()),
            "Expected lesson objects for the published language family")
    require(type(locale) is str and locale in LOCALES and locale in family_documents,
            "Missing published locale source")
    require(set(family_documents) <= LOCALES, "Unsupported language family")
    require(editorial_returns is None or type(editorial_returns) is dict,
            "Editorial returns must map stable identities to earlier revision numbers")
    requested_returns = dict(editorial_returns or {})
    for identity, revision in requested_returns.items():
        _identity(identity)
        require(type(revision) is int and revision >= 0, "An editorial return revision must be a nonnegative integer")
    previous = family_documents[locale] if previous is None else previous
    anchored = [source["stateLayout"]["family"] for source in family_documents.values()
                if type(source.get("stateLayout")) is dict and source["stateLayout"].get("version") == 2]
    if anchored:
        check_localized_family(list(family_documents.values()), list(family_documents))
        family_manifest = deepcopy(anchored[0])
    else:
        family_manifest = {
            language: {"sha256": fingerprint(_without_layout(source)),
                       "origin": deepcopy(prepare(source)["stateLayout"]["origin"])}
            for language, source in sorted(family_documents.items())}
    if type(previous.get("stateLayout")) is dict and previous["stateLayout"].get("version") == 2:
        require(type(document.get("stateLayout")) is dict and document["stateLayout"].get("version") == 2,
                "Published v2 metadata cannot be removed or regenerated")
        result = deepcopy(document)
        require(not requested_returns or result["stateLayout"].get("editorial") == previous["stateLayout"]["editorial"],
                "Explicit editorial returns require the unchanged published proof prefix")
        if result["stateLayout"].get("editorial") == previous["stateLayout"]["editorial"]:
            before, after = _all_blocks(previous), _all_blocks(result)
            recorded = {item["id"] for item in result["stateLayout"].get("sourceFormatRevisions", [])}
            for identity, block in before.items():
                if identity in recorded or code_format.code_slots(block) == code_format.code_slots(after[identity]):
                    continue
                try:
                    changed = code_format.require_source_format(block, after[identity])
                except code_format.SourceFormatError as exc:
                    raise LayoutError(f"Unsafe source edit for {identity}: {exc}") from exc
                if changed:
                    result["stateLayout"].setdefault("sourceFormatRevisions", []).append(
                        {"id": identity, "block": deepcopy(block)})
                    recorded.add(identity)
            for identity, block in before.items():
                require(identity in after, "An existing activity was removed")
                if fingerprint(block) != fingerprint(after[identity]):
                    proof = _editorial_proof(result, previous, locale, identity, block, after[identity],
                                            archive_bibliography=identity not in requested_returns)
                    if identity in requested_returns:
                        prior = [edge for edge in previous["stateLayout"]["editorial"] if edge["id"] == identity]
                        nodes = ([prior[0]["beforeSha256"]] + [edge["afterSha256"] for edge in prior]) if prior else []
                        revision = requested_returns.pop(identity)
                        require(revision < len(nodes) - 1 and nodes[revision] == proof["afterSha256"],
                                "An explicit editorial return must restore its exact earlier payload")
                        proof["returnTo"] = {"revision": revision, "historySha256": fingerprint({
                            "lessonId": result["id"], "locale": locale, "editorial": result["stateLayout"]["editorial"]}),
                            "previousBlock": deepcopy(block)}
                    result["stateLayout"]["editorial"].append(proof)
        require(not requested_returns, "Editorial return identity is unknown or its payload did not change")
        validate_transition(previous, result)
        return result
    require(not requested_returns, "Editorial returns require an existing published v2 history")
    baseline = prepare(previous)
    require(len(_pages(document)) == len(baseline["pages"]), "Historical pages cannot be added or removed")
    require(all(len(a.get("blocks", [])) == len(b.get("blocks", []))
                for a, b in zip(document["pages"], baseline["pages"])),
            "Prepare before adding blocks; use explicit additions with an ID plan")
    result = deepcopy(document)
    result["stateLayout"] = {
        "version": 2, "locale": locale,
        "origin": deepcopy(baseline["stateLayout"]["origin"]),
        "current": deepcopy(baseline["stateLayout"]["current"]),
        "family": family_manifest,
        "additions": [], "editorial": [],
    }
    before, after = _blocks(baseline), _blocks(result)
    for identity, block in before.items():
        if code_format.code_slots(block) != code_format.code_slots(after[identity]):
            try:
                changed = code_format.require_source_format(block, after[identity])
            except code_format.SourceFormatError as exc:
                raise LayoutError(f"Unsafe source edit for {identity}: {exc}") from exc
            if changed:
                result["stateLayout"].setdefault("sourceFormatRevisions", []).append(
                    {"id": identity, "block": deepcopy(block)})
    for identity, block in before.items():
        if fingerprint(block) != fingerprint(after[identity]):
            result["stateLayout"]["editorial"].append(
                _editorial_proof(result, previous, locale, identity, block, after[identity]))
    validate_transition(previous, result)
    return result


def _validate_state_layout(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    pages = _pages(document)
    meta = document.get("stateLayout")
    if type(meta) is not dict or meta.get("version") != 2:
        require(not any(reviewed_math_label(block) is not None
                        for page in pages for block in page.get("blocks", [])),
                "The reviewed mathematical label requires its published v2 identity and proof")
        require(not any(_reviewed_choice(block) is not None for page in pages for block in page.get("blocks", [])),
                "A reviewed choice revision requires its published v2 identity and proof")
        require(not any(_reviewed_compiler(block) is not None for page in pages for block in page.get("blocks", [])),
                "A reviewed compiler revision requires its published v2 identity and proof")
        require(not any(_reviewed_compiler_reference(block) is not None for page in pages for block in page.get("blocks", [])),
                "A reviewed compiler reference requires its published v2 identity and proof")
    if "stateLayout" not in document:
        return {(p, b): (p, b) for p, page in enumerate(pages) for b in range(len(page.get("blocks", [])))}
    meta = document["stateLayout"]
    if type(meta) is dict and type(meta.get("version")) is int and meta["version"] == 2:
        return _validate_v2(document)
    require(type(meta) is dict and set(meta) == {"version", "origin", "current"}, "Invalid stateLayout fields")
    require(type(meta["version"]) is int and meta["version"] == 1, "Unsupported stateLayout version")
    origin, current = meta["origin"], meta["current"]
    require(type(origin) is list and type(current) is list and len(origin) == len(current) == len(pages),
            "A reorder must preserve the page count")
    require(origin, "No original pages")
    origin_pages, origin_blocks, digests = [], {}, {}
    current_pages, current_blocks, mapping = [], [], {}

    def identifier(value: object) -> str:
        require(type(value) is str and re.fullmatch(r"[A-Za-z0-9_-]{1,128}", value) is not None,
                "Invalid stable identity")
        return value

    for p, page in enumerate(origin):
        require(type(page) is dict and set(page) == {"id", "blocks"}, "Malformed original page")
        origin_pages.append(identifier(page["id"]))
        require(type(page["blocks"]) is list, "Malformed original blocks")
        for b, block in enumerate(page["blocks"]):
            require(type(block) is dict and set(block) == {"id", "sha256"}, "Malformed original block")
            block_id = identifier(block["id"])
            require(block_id not in origin_blocks, "Duplicate original block identity")
            require(type(block["sha256"]) is str and re.fullmatch(r"[0-9a-f]{64}", block["sha256"]),
                    "Invalid block digest")
            origin_blocks[block_id] = (p, b)
            digests[block_id] = block["sha256"]
    require(len(origin_blocks) <= 100_000, "Too many original blocks")
    for p, page in enumerate(current):
        require(type(page) is dict and set(page) == {"id", "blocks"}, "Malformed current page")
        current_pages.append(identifier(page["id"]))
        require(type(page["blocks"]) is list and len(page["blocks"]) == len(pages[p].get("blocks", [])),
                "Current layout does not match the visible blocks")
        for b, block_id in enumerate(page["blocks"]):
            block_id = identifier(block_id)
            require(block_id in origin_blocks, "Unknown block identity")
            require(state_fingerprint(pages[p]["blocks"][b]) == digests[block_id],
                    "A response/code contract changed or belongs to another identity")
            current_blocks.append(block_id)
            mapping[p, b] = origin_blocks[block_id]
    require(len(set(origin_pages)) == len(origin_pages), "Duplicate original page identity")
    require(len(set(current_pages)) == len(current_pages) and set(current_pages) == set(origin_pages),
            "Page identities changed")
    require(len(set(current_blocks)) == len(current_blocks) and set(current_blocks) == set(origin_blocks),
            "Block identities changed")
    return mapping


def _validate_presentation_relationships(document: dict) -> None:
    """Windows presentation links use existing identities, never response payloads.

    Empty optional lists retain the native parser's legacy no-op behavior. Any
    actual link needs a fully validated stateLayout, and ordering always follows
    visible positions rather than historical persistence addresses.
    """
    relations = {}
    for field in PRESENTATION_FIELDS:
        if field not in document:
            continue
        value = document[field]
        require(type(value) is list and len(value) <= 100_000,
                f"{field} must be a relationship list")
        relations[field] = value
    if not any(relations.values()):
        return
    require("stateLayout" in document, "Presentation relationships require stable state-layout identities")
    blocks = _blocks(document)
    positions = {identity: (p, b)
                 for p, record in enumerate(document["stateLayout"]["current"])
                 for b, identity in enumerate(record["blocks"])}
    references = set()
    for field, links in relations.items():
        activities = set()
        entries_key = "blocks" if field == "referenceAfter" else "requires"
        for link in links:
            require(type(link) is dict and set(link) == {"activity", entries_key},
                    f"Malformed {field} relationship")
            activity = _identity(link["activity"])
            require(activity not in activities, f"Duplicate {field} activity")
            activities.add(activity)
            require(activity in blocks, f"{field} activity does not exist")
            target = blocks[activity]
            if field == "referenceAfter":
                require(target.get("type") == "cpp_compiler", "Reference activity must be a compiler")
                require(type(target.get("outputCheck")) is dict or type(target.get("compileCheck")) is dict,
                        "Reference activity requires an authored compiler objective")
            else:
                require(target.get("type") in DEPENDENT_ACTIVITY_TYPES,
                        "A dependent activity must be an exercise or compiler")
            entries = link[entries_key]
            require(type(entries) is list and 0 < len(entries) <= 100_000,
                    f"{field} relationship needs a nonempty {entries_key} list")
            seen = set()
            for value in entries:
                identity = _identity(value)
                require(identity != activity and identity not in seen,
                        f"Duplicate or self-referencing {field} identity")
                seen.add(identity)
                require(identity in blocks, f"{field} block does not exist")
                block = blocks[identity]
                if field == "referenceAfter":
                    require(identity not in references, "Duplicate reference presentation block")
                    references.add(identity)
                    require(block.get("type") in {"heading", "paragraph", "note", "code", "image"},
                            "An exercise or editor cannot be hidden as a reference")
                    require(positions[identity] > positions[activity],
                            "A reference must follow its activity in visual order")
                else:
                    require(block.get("type") == "cpp_compiler" and
                            (type(block.get("outputCheck")) is dict or type(block.get("compileCheck")) is dict),
                            "An activity prerequisite requires a compiler with an authored objective")
                    require(positions[identity] < positions[activity],
                            "An activity prerequisite must precede its activity in visual order")


def _validate_retirement_history(document: dict) -> None:
    meta = document.get("stateLayout", {})
    if "retirementHistory" not in meta:
        return
    require(meta.get("version") == 2, "Retirement reading history requires stateLayout v2")
    history = meta["retirementHistory"]
    require(type(history) is list and len(history) <= 100_000, "Malformed retirement reading history")
    archived = [entry["id"] for entry in meta.get("retired", [])]
    available = _all_blocks(document)
    choices = _choice_revisions(document)
    compilers = _compiler_revisions(document)
    compiler_references = _compiler_references(document)
    page_ids = {record["id"] for record in meta["current"]}
    page_shells = {record["id"]: {key: value for key, value in page.items() if key != "blocks"}
                   for page, record in zip(document["pages"], meta["current"])}
    covered, batches = [], []
    for snapshot in history:
        require(type(snapshot) is dict and set(snapshot) == {
            "retired", "current", "pages", "compilerPuzzlesRequired", "referenceAfter", "activityAfter"},
            "A retirement reading snapshot needs the complete previous pages, layout, flow and relationships")
        require(type(snapshot["compilerPuzzlesRequired"]) is bool, "Historical compiler flow must be boolean")
        ids = snapshot["retired"]
        require(type(ids) is list and ids, "An archival reading snapshot needs its retirement batch")
        for identity in ids:
            _identity(identity)
        require(len(set(ids)) == len(ids) and set(ids) <= set(archived), "Unknown or repeated reading-history retirement")
        pages = _pages(snapshot)
        current = snapshot["current"]
        require(type(current) is list and len(current) == len(pages) == len(page_ids), "Historical reading page count changed")
        seen_pages, seen_blocks = set(), set()
        for page, record in zip(pages, current):
            require(type(record) is dict and set(record) == {"id", "blocks"}, "Malformed historical reading page")
            identity = _identity(record["id"])
            require(identity in page_ids and identity not in seen_pages, "Historical reading page identity changed")
            seen_pages.add(identity)
            require({key: value for key, value in page.items() if key != "blocks"} == page_shells[identity],
                    "Historical reading page metadata changed")
            require(type(record["blocks"]) is list and len(record["blocks"]) == len(page.get("blocks", [])),
                    "Historical reading blocks do not match their identities")
            for block_id, block in zip(record["blocks"], page.get("blocks", [])):
                _identity(block_id)
                require(block_id in available and block_id not in seen_blocks, "Unknown or repeated historical reading block")
                reviewed = _reviewed_choice(block)
                _validate_choice_pair(block, CHOICE_OPTION_REVISIONS.get((document.get("id"), block_id)))
                if reviewed is not None:
                    # The global payload alias is not evidence that this
                    # presentation ever owned the new wording. Older, original
                    # choice snapshots remain valid before a later revision.
                    require(meta["locale"] == "es" and block_id in choices
                            and CHOICE_OPTION_REVISIONS.get((document.get("id"), block_id)) == reviewed
                            and _reviewed_choice(available[block_id]) == reviewed,
                            "A historical revised choice requires its matching active revision archive")
                compiler_reviewed = _reviewed_compiler(block)
                if compiler_reviewed is not None:
                    require(meta["locale"] == "es" and block_id in compilers
                            and COMPILER_CASE_REVISIONS.get((document.get("id"), block_id)) == compiler_reviewed
                            and _reviewed_compiler(available[block_id]) == compiler_reviewed,
                            "A historical revised compiler requires its matching active revision archive")
                reference_reviewed = _reviewed_compiler_reference(block)
                if reference_reviewed is not None:
                    expected_reference = next((reference for (lesson, compiler), reference in COMPILER_REFERENCE_REVISIONS.items()
                                               if lesson == document.get("id") and reference["id"] == block_id), None)
                    require(meta["locale"] == "es" and block_id in compiler_references
                            and expected_reference == reference_reviewed
                            and _reviewed_compiler_reference(available[block_id]) == reference_reviewed,
                            "A historical revised compiler reference requires its matching active revision archive")
                require(state_fingerprint(_historical_order_contract(document, block_id, block, allow_original=True))
                        == state_fingerprint(_historical_order_contract(document, block_id, available[block_id])),
                        "Historical reading snapshot changed a response/code contract")
                seen_blocks.add(block_id)
        historical_blocks = {identity: block for page, record in zip(pages, current)
                             for identity, block in zip(record["blocks"], page.get("blocks", []))}
        for (lesson, compiler), reference in COMPILER_REFERENCE_REVISIONS.items():
            if lesson == document.get("id") and compiler in compilers:
                reference_id = reference["id"]
                if compiler in historical_blocks and reference_id in historical_blocks:
                    require((_reviewed_compiler(historical_blocks[compiler]) is not None)
                            == (_reviewed_compiler_reference(historical_blocks[reference_id]) is not None),
                            "A historical compiler revision and reference must preserve the same published version")
        historical = {"pages": pages, "stateLayout": {"current": current},
                      "referenceAfter": snapshot["referenceAfter"], "activityAfter": snapshot["activityAfter"]}
        _validate_presentation_relationships(historical)
        covered.extend(ids)
        batches.append((set(ids), seen_blocks))
    require(len(set(covered)) == len(covered) and (not covered or covered == archived[-len(covered):]),
            "Retirement reading batches must own an exact suffix of the archived identities")
    active = set(_blocks(document))
    additions = {entry["id"] for entry in meta["additions"]}
    for ids, before in reversed(batches):
        # Later v2 additions did not exist in this presentation. Only an
        # addition may be absent; original identities and this batch remain
        # mandatory. Transition validation separately pins the exact snapshot.
        expected = active | ids
        require(ids <= before <= expected and expected - before <= additions and not active & ids,
                "Historical reading layout does not match its retirement batch")
        active = before


def _validate_compile_checks(document: dict) -> None:
    """Bind new diagnostic goals to the exact Spanish owner in every presentation."""
    meta = document.get("stateLayout", {})
    annotated = any("compileCheck" in block for page in _pages(document) for block in page.get("blocks", []))
    if not annotated and type(meta) is dict:
        annotated = any("compileCheck" in item.get("block", {}) for item in meta.get("retired", [])) or any(
            "compileCheck" in block for snapshot in meta.get("retirementHistory", [])
            for page in snapshot.get("pages", []) for block in page.get("blocks", []))
    if not annotated:
        return
    require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es",
            "Compiler diagnostic goals require their existing Spanish stateLayout v2 family")
    available = _all_blocks(document)
    def owner(identity: str, block: dict, *, historical: bool = False) -> None:
        spec = validate_compile_check_contract(block)
        if spec is not None:
            require(COMPILE_DIAGNOSTICS.get((document.get("id"), identity)) == spec,
                    "A reviewed compiler diagnostic belongs to its exact lesson and stable editor identity")
            proofs = [proof for proof in meta["editorial"] if proof["id"] == identity]
            require(proofs and any(proof["beforeSha256"] == spec["before"] for proof in proofs)
                    and (any(fingerprint(block) in (proof["beforeSha256"], proof["afterSha256"]) for proof in proofs)
                         if historical else proofs[-1]["afterSha256"] == fingerprint(block)),
                    "A new compiler diagnostic requires complete published before/after editorial provenance")
    for identity, block in available.items():
        owner(identity, block)
    for snapshot in meta.get("retirementHistory", []):
        for page, record in zip(snapshot["pages"], snapshot["current"]):
            for identity, block in zip(record["blocks"], page.get("blocks", [])):
                owner(identity, block, historical=True)
                if "compileCheck" in block:
                    require("compileCheck" in available[identity]
                            and without_compiler_guidance(available[identity])["compileCheck"]
                            == without_compiler_guidance(block)["compileCheck"],
                            "A historical compiler diagnostic must retain its current archived or active objective")


def _validate_intentional_exit_goals(document: dict) -> None:
    meta = document.get("stateLayout", {})
    available = (_all_blocks(document) if type(meta) is dict and "current" in meta else
                 {f"block_{p:04d}_{b:04d}": block for p, page in enumerate(_pages(document))
                  for b, block in enumerate(page.get("blocks", []))})

    def owner(identity: str, block: dict) -> None:
        spec = reviewed_intentional_exit_spec(block)
        if spec is None:
            return
        require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es"
                and document.get("id") == spec["lesson"] and identity == spec["identity"],
                "The reviewed intentional-exit goal belongs to its original Spanish v2 editor")
        proofs = [proof for proof in meta["editorial"] if proof["id"] == identity]
        require(proofs and any(proof["beforeSha256"] == spec["before"] for proof in proofs),
                "The intentional-exit goal requires its exact complete original/editorial provenance")

    for identity, block in available.items():
        owner(identity, block)
    for snapshot in meta.get("retirementHistory", []) if type(meta) is dict else []:
        for page, record in zip(snapshot["pages"], snapshot["current"]):
            for identity, block in zip(record["blocks"], page.get("blocks", [])):
                owner(identity, block)
                if (spec := reviewed_intentional_exit_spec(block)) is not None:
                    require(reviewed_intentional_exit_spec(available[identity]) == spec,
                            "A historical intentional-exit objective must retain its current archived or active goal")


def _validate_crt_output_variants(document: dict) -> None:
    meta = document.get("stateLayout", {})
    available = (_all_blocks(document) if type(meta) is dict and "current" in meta else
                 {f"block_{p:04d}_{b:04d}": block for p, page in enumerate(_pages(document))
                  for b, block in enumerate(page.get("blocks", []))})

    def owner(identity: str, block: dict) -> None:
        if not reviewed_crt_output_variant(block):
            return
        historical_crt_output_payload(block)
        require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es"
                and document.get("id") == CRT_OUTPUT_REVISION["lesson"] and identity == CRT_OUTPUT_REVISION["identity"],
                "The reviewed CRT output spelling belongs to its exact Spanish v2 editor")
        require(any(proof["id"] == identity and proof["beforeSha256"] == CRT_OUTPUT_REVISION["before"]
                    and proof["afterSha256"] == CRT_OUTPUT_REVISION["after"] for proof in meta["editorial"]),
                "The reviewed CRT output spelling requires its exact original-to-reviewed editorial edge")

    for identity, block in available.items():
        owner(identity, block)
    for snapshot in meta.get("retirementHistory", []) if type(meta) is dict else []:
        for page, record in zip(snapshot["pages"], snapshot["current"]):
            for identity, block in zip(record["blocks"], page.get("blocks", [])):
                owner(identity, block)
                if reviewed_crt_output_variant(block):
                    require(reviewed_crt_output_variant(available[identity]),
                            "A historical reviewed CRT spelling must retain its current archived or active contract")


def _validate_compiler_workflow_checks(document: dict) -> None:
    """Bind U3 History to its existing editor without changing any saved response."""
    if "compilerWorkflowChecks" not in document:
        return
    checks = document["compilerWorkflowChecks"]
    require(type(checks) is list and len(checks) == 1,
            "The U3 History workflow needs exactly one owner")
    item = checks[0]
    require(type(item) is dict and set(item) == {"activity", "kind"}
            and item["activity"] == "block_0010_0003"
            and item["kind"] == "cmakeSelectiveRecoveryV1",
            "Unknown or duplicated U3 History workflow")
    meta = document.get("stateLayout")
    require(document.get("id") ==
            "programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador"
            and type(meta) is dict and meta.get("version") == 2
            and meta.get("locale") == "es",
            "U3 History requires the original Spanish v2 lesson")
    pages = document["pages"]
    require(len(pages) == 13 and len(pages[10]["blocks"]) > 3
            and meta["current"][10]["blocks"][3] == item["activity"],
            "U3 History moved away from its immutable editor")
    owner = pages[10]["blocks"][3]
    cmake = owner.get("cmake")
    require(owner.get("type") == "cpp_compiler"
            and type(owner.get("projectFiles")) is list
            and len(owner["projectFiles"]) == 6
            and type(cmake) is dict
            and cmake.get("target") == "comprobar_cajas"
            and cmake.get("buildType") == "Debug"
            and type(owner.get("outputCheck")) is dict
            and len(owner["outputCheck"].get("cases", [])) == 1,
            "U3 History owner no longer has its reviewed CMake objective")



def _validate_compiler_runtime_checks(document: dict) -> None:
    """Validate the reviewed U2 EOF and opt-in U3L6 semantic runtime goals.

    Both extensions stay at the lesson root, preserving historical editor slots.
    """
    if "compilerRuntimeChecks" not in document:
        return
    records = document["compilerRuntimeChecks"]
    if document.get("id") == (
            "programming_practice_tools__comprobaciones_repetibles__comprobar_acuerdos_internos_con_aserciones"):
        layout = document.get("stateLayout")
        require(type(layout) is dict and layout.get("version") == 2 and
                layout.get("locale") == "es" and
                type(document.get("pages")) is list and len(document["pages"]) == 6,
                "WASI assertion review requires the original Spanish U3L9 state map")
        reviewed = (("block_0002_0003", "wasiAssertionAbortV1", 2, 3),
                    ("block_0003_0007", "wasiAssertionSuppressedV1", 3, 7),
                    ("block_0004_0007", "wasiAssertionRepairV1", 4, 3))
        require(type(records) is list and len(records) == len(reviewed),
                "The U3L9 assertion contract needs all three ordered owners")
        original_code = document["pages"][2]["blocks"][3].get("initialCode")
        for record, (identity, kind, page, position) in zip(records, reviewed):
            require(type(record) is dict and record == {"activity": identity, "kind": kind},
                    "Unknown, unordered or replaced U3L9 assertion owner")
            blocks = document["pages"][page]["blocks"]
            require(len(blocks) > position and layout["current"][page]["blocks"][position] == identity,
                    "The U3L9 assertion owner moved from its original mapped slot")
            owner = blocks[position]
            code = owner.get("initialCode")
            require(owner.get("type") == "cpp_compiler" and type(code) is str and
                    owner.get("initialStandardInput") == "-1 5\n" and
                    not any(field in owner for field in
                        ("outputCheck", "compileCheck", "projectFiles", "sourceFiles", "cmake")) and
                    "assert(cajas * piezas_por_caja >= piezas);" in code and
                    "const int cajas = piezas / piezas_por_caja;" in code and
                    (code.startswith("#define NDEBUG\n#include <cassert>")
                     if page == 3 else code.startswith("#include <cassert>") and
                     "NDEBUG" not in code and (page != 4 or code == original_code)),
                    "The U3L9 source, NDEBUG, assert or prepared input was changed")
        return
    if document.get("id") == (
            "programming_practice_tools__comprobaciones_repetibles__automatizar_la_revision_sin_borrar_evidencia"):
        layout = document.get("stateLayout")
        require(type(layout) is dict and layout.get("version") == 2 and layout.get("locale") == "es",
                "The U3L8 report objectives require the original Spanish v2 state map")
        expected = (("block_0003_0003", "observationCompleteV1", 3,
                     "A 0 0\nA 2 1\nB -1 -1\nA 2 1\nA 1-1\nC;=lote 0 1\nC;=lote 0 1\nA 1 -1\n",
                     "if (hay_anterior && caso == caso_anterior)"),
                    ("block_0006_0003", "observationDifferencesV1", 6,
                     "N 0 0\nR -1 -1\nR 0 -1\nP 3\nR 0 -1\nZ 9 9\n",
                     "if (coincide) {\n            continue;"))
        require(type(records) is list and len(records) == len(expected) and
                type(document.get("pages")) is list and len(document["pages"]) == 9,
                "U3L8 requires both ordered report objectives")
        for record, (identity, kind, page, initial_input, marker) in zip(records, expected):
            require(type(record) is dict and record == {"activity": identity, "kind": kind},
                    "Unknown or unordered U3L8 report declaration")
            blocks = document["pages"][page]["blocks"]
            require(len(blocks) > 3 and layout["current"][page]["blocks"][3] == identity,
                    "The U3L8 report editor moved from its immutable slot")
            owner = blocks[3]
            require(owner.get("type") == "cpp_compiler" and
                    type(owner.get("initialCode")) is str and
                    "bool leer_entero(" in owner["initialCode"] and
                    "bool leer_registro(" in owner["initialCode"] and
                    marker in owner["initialCode"] and
                    owner.get("initialStandardInput") == initial_input and
                    not any(field in owner for field in
                        ("outputCheck", "compileCheck", "projectFiles", "sourceFiles", "cmake")),
                    "The original U3L8 parser, initial source or prepared data changed")
        return
    if document.get("id") == (
            "programming_practice_tools__comprobaciones_repetibles__diagnosticos_para_corregir_una_peticion"):
        layout = document.get("stateLayout")
        require(type(layout) is dict and layout.get("version") == 2 and layout.get("locale") == "es",
                "The U3L6 semantic review requires the original Spanish v2 state map")
        expected = (("block_0003_0003", "semanticRejectionBoxesV1", 3, "-1 0 21 5\n",
                     'destino << "Error\\n";'),
                    ("block_0006_0003", "semanticRejectionBalanceV1", 6, "2 3 10 0 0 -1 5 5\n",
                     'destino << "Error de saldo\\n";'))
        require(type(records) is list and len(records) == len(expected) and
                type(document.get("pages")) is list and len(document["pages"]) == 9,
                "U3L6 requires both ordered semantic diagnostic owners")
        for record, (identity, kind, page, initial_input, marker) in zip(records, expected):
            require(type(record) is dict and record == {"activity": identity, "kind": kind},
                    "Unknown or unordered semantic rejection check")
            blocks = document["pages"][page]["blocks"]
            require(len(blocks) > 3 and layout["current"][page]["blocks"][3] == identity,
                    "The semantic diagnostic owner moved away from its original editor")
            owner = blocks[3]
            require(owner.get("type") == "cpp_compiler" and
                    type(owner.get("initialCode")) is str and marker in owner["initialCode"] and
                    owner.get("initialStandardInput") == initial_input and
                    not any(field in owner for field in
                        ("outputCheck", "compileCheck", "projectFiles", "sourceFiles", "cmake")),
                    "The original U3L6 diagnostic source or prepared input changed")
        return
    require(type(records) is list and len(records) == 1,
            "The source-pinned U2 runtime objective requires exactly one owner")
    item = records[0]
    require(type(item) is dict and set(item) == {"activity", "kind"} and
            item["activity"] == "block_0009_0008" and
            item["kind"] == "exactOriginalProjectRuntimeV1",
            "Unknown compiler runtime owner or kind")
    layout = document.get("stateLayout")
    require(document.get("id") ==
            "programming_practice_tools__construccion_reproducible__opciones_y_revision_de_la_configuracion"
            and type(layout) is dict and layout.get("version") == 2
            and layout.get("locale") == "es",
            "This runtime declaration requires its original Spanish v2 lesson")
    pages = document.get("pages")
    require(type(pages) is list and len(pages) == 14 and
            len(pages[9]["blocks"]) > 8 and
            layout["current"][9]["blocks"][8] == item["activity"],
            "The declared runtime goal moved away from its historical editor")
    block = pages[9]["blocks"][8]
    cmake = block.get("cmake")
    files = block.get("projectFiles")
    require(block.get("type") == "cpp_compiler" and
            "outputCheck" not in block and "compileCheck" not in block and
            block.get("initialStandardInput") == "6" and
            type(cmake) is dict and cmake.get("target") == "revisar" and
            cmake.get("buildType") == "Debug" and
            cmake.get("options") == {"ACEPTAR_IGUAL": "OFF"} and
            type(files) is list and len(files) == 5 and
            "sourceFiles" not in block,
            "The U2 runtime owner changed its source, input or CMake conditions")
    names = {entry.get("path"): entry.get("initialCode") for entry in files
             if type(entry) is dict}
    require(len(names) == 5 and "main.cpp" in names and
            'std::cerr << "Entrada invalida";' in names["main.cpp"] and
            "return 2;" in names["main.cpp"],
            "The published client cannot be evaluated as the reviewed EOF case")


def _validate_order_visual_separators(document: dict) -> None:
    """A visual-only root extension, never an order fragment or progress slot."""
    if "orderVisualSeparators" not in document:
        return
    entries = document["orderVisualSeparators"]
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es",
            "Visual code-order separators require the Spanish v2 map")
    require(type(entries) is list and 0 < len(entries) <= 16,
            "A bounded nonempty visual-separator declaration is required")
    owners = {}
    for page, record in zip(document["pages"], meta["current"]):
        for identity, block in zip(record["blocks"], page["blocks"]):
            require(identity not in owners, "Duplicate original code-order owner")
            owners[identity] = block
    seen = set()
    for record in entries:
        require(type(record) is dict and set(record) == {"activity", "count"},
                "A code-order separator accepts only activity and count")
        identity = _identity(record["activity"])
        count = record["count"]
        require(identity not in seen and type(count) is int and 1 <= count <= 4,
                "Duplicate or invalid visual separator count")
        seen.add(identity)
        source = owners.get(identity)
        require(type(source) is dict and source.get("type") == "order_code"
                and type(source.get("fragments")) is list
                and source["fragments"] and type(source.get("correctOrder")) is list
                and len(source["fragments"]) == len(source["correctOrder"]),
                "Visual separators require an original, evaluated order-code owner")


def validate(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    """Return immutable storage coordinates after validating state and links."""
    if "compilerPuzzlesRequired" in document:
        require(type(document["compilerPuzzlesRequired"]) is bool, "compilerPuzzlesRequired must be boolean")
    mapping = _validate_state_layout(document)
    _validate_order_visual_separators(document)
    _validate_compiler_workflow_checks(document)
    _validate_compiler_runtime_checks(document)
    _validate_presentation_relationships(document)
    _validate_retirement_history(document)
    _validate_compile_checks(document)
    _validate_intentional_exit_goals(document)
    _validate_crt_output_variants(document)
    try:
        for page in _pages(document):
            for block in page["blocks"]:
                if block.get("type") == "image":
                    image_resources.validate_contract(block)
    except (ValueError, KeyError, TypeError) as error:
        raise LayoutError(f"Invalid curricular image: {error}") from error
    try:
        text_semantics.validate(document)
    except (ValueError, KeyError, TypeError) as error:
        raise LayoutError(f"Invalid explicit text semantics: {error}") from error
    return mapping


def _transition_metadata(document: dict) -> dict:
    # Only these validated root presentation extensions may change independently
    # of an immutable origin. All other lesson metadata remains protected.
    return {k: v for k, v in document.items()
            if k not in {"pages", "stateLayout"} | PRESENTATION_FIELDS | VISUAL_PRESENTATION_FIELDS | FLOW_FIELDS | TEXT_PRESENTATION_FIELDS}


def add_compile_check(document: dict, identity: str, explanation: str | None = None) -> dict:
    """Prepare one reviewed new diagnostic objective, preserving the entire source."""
    validate(document)
    meta = document.get("stateLayout", {})
    require(meta.get("version") == 2 and meta.get("locale") == "es",
            "Prepare the published Spanish stateLayout v2 family before adding a diagnostic goal")
    _identity(identity)
    spec = COMPILE_DIAGNOSTICS.get((document.get("id"), identity))
    require(spec is not None, "The editor is not one of the five reviewed diagnostic activities")
    original = _blocks(document).get(identity)
    require(original is not None, "A diagnostic goal requires its existing active editor")
    if explanation is not None:
        require(type(explanation) is str and bool(explanation.strip()), "A diagnostic explanation must be nonempty text")
    if "compileCheck" in original:
        require(explanation is None or original["compileCheck"].get("explanation") == explanation,
                "A published diagnostic explanation cannot be overwritten by the authoring helper")
        return deepcopy(document)
    require(fingerprint(original) == spec["before"], "A diagnostic goal must start from its exact complete published editor")
    result = deepcopy(document)
    actual = _blocks(result)[identity]
    actual["compileCheck"] = {"diagnostic": spec["diagnostic"]}
    if explanation is not None:
        actual["compileCheck"]["explanation"] = explanation
    result["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": fingerprint(original),
                                             "afterSha256": fingerprint(actual)})
    validate_transition(document, result)
    return result


def refine_order_lines(document: dict, identity: str) -> dict:
    """Prepare a compatible line presentation; never open student preferences.

    Bootstrap an unmapped lesson with prepare_v2 and its published language
    family first. The original arrays, prose and all separators remain archived.
    """
    validate(document)
    require(document.get("stateLayout", {}).get("version") == 2,
            "Prepare the published language family as stateLayout v2 before refining order lines")
    _identity(identity)
    blocks = _blocks(document)
    require(identity in blocks, "An order refinement needs an existing active identity")
    if identity in _order_refinements(document):
        return deepcopy(document)
    original = blocks[identity]
    lines, _, order = order_line_expansion(original)
    require(lines != original["fragments"] or order != original["correctOrder"],
            "An order refinement must split its original presentation")
    result = deepcopy(document)
    actual = _blocks(result)[identity]
    actual["fragments"], actual["correctOrder"] = lines, order
    result["stateLayout"].setdefault("orderRefinements", []).append({"id": identity, "block": deepcopy(original)})
    result["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": fingerprint(original),
                                            "afterSha256": fingerprint(actual)})
    validate_transition(document, result)
    return result


def revise_choice_option(document: dict, identity: str) -> dict:
    """Prepare one closed option revision and its exact paired fields in memory."""
    validate(document)
    meta = document.get("stateLayout", {})
    require(meta.get("version") == 2 and meta.get("locale") == "es",
            "Prepare the published Spanish family as stateLayout v2 before revising a choice")
    _identity(identity)
    spec = CHOICE_OPTION_REVISIONS.get((document.get("id"), identity))
    require(spec is not None, "The choice option is not one of the eight reviewed activities")
    if identity in _choice_revisions(document):
        return deepcopy(document)
    original = _blocks(document).get(identity)
    require(original is not None and fingerprint(original) == spec["full_before"],
            "A reviewed choice must start from its exact complete published block")
    result = deepcopy(document)
    actual = _blocks(result)[identity]
    actual["options"][spec["option"]] = spec["after"]
    if spec["feedback"] is not None:
        actual["reviewFeedback"][spec["option"]] = spec["feedback"]
    if "paired_explanation" in spec:
        actual["explanation"] = spec["paired_explanation"]["after"]
    require(fingerprint(actual) == spec["full_after"], "The reviewed choice option/guidance does not match its approved first revision")
    result["stateLayout"].setdefault("choiceRevisions", []).append({"id": identity, "block": deepcopy(original)})
    result["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": spec["full_before"],
                                            "afterSha256": spec["full_after"]})
    validate_transition(document, result)
    return result


def revise_compiler_cases(document: dict, identity: str) -> dict:
    """Prepare exactly one reviewed compiler expansion without editing assets."""
    validate(document)
    meta = document.get("stateLayout", {})
    require(meta.get("version") == 2 and meta.get("locale") == "es",
            "Prepare the published Spanish family as stateLayout v2 before revising a compiler")
    _identity(identity)
    spec = COMPILER_CASE_REVISIONS.get((document.get("id"), identity))
    require(spec is not None, "The compiler is not one of the ten reviewed activities")
    if identity in _compiler_revisions(document):
        return deepcopy(document)
    original = _blocks(document).get(identity)
    require(original is not None and fingerprint(original) == spec["before"],
            "A reviewed compiler must start from its exact complete published block")
    result = deepcopy(document)
    actual = _blocks(result)[identity]
    if "append" in spec:
        actual["outputCheck"]["cases"].extend(deepcopy(spec["append"]))
    else:
        actual["outputCheck"]["cases"][0]["outputs"][0] += spec["suffix"]
    if "initial_insert" in spec:
        require(actual["initialCode"].endswith(_COMPILER_REFERENCE_TAIL), "Invalid published compiler initial ending")
        actual["initialCode"] = actual["initialCode"][:-len(_COMPILER_REFERENCE_TAIL)] + spec["initial_insert"] + _COMPILER_REFERENCE_TAIL
    require(fingerprint(actual) == spec["after"], "The expanded compiler does not match its reviewed first revision")
    archive = {"id": identity, "block": deepcopy(original)}
    result["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": spec["before"],
                                            "afterSha256": spec["after"]})
    reference_spec = COMPILER_REFERENCE_REVISIONS.get((document.get("id"), identity))
    if reference_spec is not None:
        reference_id = reference_spec["id"]
        original_reference = _blocks(document).get(reference_id)
        require(original_reference is not None and fingerprint(original_reference) == reference_spec["before"],
                "A compiler expansion needs its exact complete published reference")
        reference = _blocks(result)[reference_id]
        require(reference["code"].endswith(_COMPILER_REFERENCE_TAIL), "Invalid published reference ending")
        reference["code"] = reference["code"][:-len(_COMPILER_REFERENCE_TAIL)] + reference_spec["insert"] + _COMPILER_REFERENCE_TAIL
        require(fingerprint(reference) == reference_spec["after"], "The compiler reference does not match its reviewed expansion")
        archive["references"] = [{"id": reference_id, "block": deepcopy(original_reference)}]
        result["stateLayout"]["editorial"].append({"id": reference_id, "beforeSha256": reference_spec["before"],
                                                "afterSha256": reference_spec["after"]})
    result["stateLayout"].setdefault("compilerRevisions", []).append(archive)
    validate_transition(document, result)
    return result


def reorder(document: dict, plan: list[dict], additions: list[dict] | None = None) -> dict:
    """Move existing page shells and blocks according to an explicit ID-only plan."""
    before = prepare(document)
    pages_by_id, blocks_by_id = {}, {}
    for page, identity in zip(before["pages"], before["stateLayout"]["current"]):
        pages_by_id[identity["id"]] = {key: deepcopy(value) for key, value in page.items() if key != "blocks"}
        blocks_by_id.update(zip(identity["blocks"], deepcopy(page.get("blocks", []))))
    result = deepcopy(before)
    if additions is not None:
        require(before["stateLayout"]["version"] == 2, "Activity additions require stateLayout v2")
        require(type(additions) is list, "Expected an activity-addition list")
        for addition in additions:
            require(type(addition) is dict and set(addition) == {"id", "storage", "progressPage", "block"},
                    "An authoring addition needs id, storage, progressPage and block")
            identity = _identity(addition["id"])
            require(identity not in blocks_by_id, "An addition cannot replace an existing block")
            require(type(addition["block"]) is dict, "A new block must be an object")
            blocks_by_id[identity] = deepcopy(addition["block"])
            result["stateLayout"]["additions"].append({
                "id": identity, "sha256": state_fingerprint(addition["block"]),
                "storage": deepcopy(addition["storage"]), "progressPage": addition["progressPage"]})
    require(type(plan) is list, "Expected a page-layout list")
    result["pages"] = []
    for identity in plan:
        require(type(identity) is dict and set(identity) == {"id", "blocks"}, "Invalid reorder plan")
        require(identity["id"] in pages_by_id and type(identity["blocks"]) is list, "Unknown page in plan")
        require(all(i in blocks_by_id for i in identity["blocks"]), "Unknown block in plan")
        result["pages"].append({**deepcopy(pages_by_id[identity["id"]]),
                                "blocks": [deepcopy(blocks_by_id[i]) for i in identity["blocks"]]})
    result["stateLayout"]["current"] = deepcopy(plan)
    validate_transition(before, result)
    return result


def retire(document: dict, identities: list[str], *, activity_after: list[dict] | None = None,
           reference_after: list[dict] | None = None,
           compiler_puzzles_required: bool | None = None) -> dict:
    """Archive explicitly selected Spanish activities without recycling their slots.

    Relationship updates are authored by the caller in the same transition. No
    prerequisite is silently removed, and this helper never writes source files.
    """
    validate(document)
    require(document.get("stateLayout", {}).get("version") == 2,
            "Retirement requires an existing stateLayout v2 family")
    require(document["stateLayout"]["locale"] == "es", "Retirement is currently authorized only for Spanish")
    require(type(identities) is list and identities, "Supply a nonempty retirement plan")
    for identity in identities:
        _identity(identity)
    require(len(set(identities)) == len(identities), "Supply a unique retirement plan")
    active = _blocks(document)
    require(set(identities) <= active.keys(), "Only active identities can be retired")
    result = deepcopy(document)
    archive = result["stateLayout"].setdefault("retired", [])
    retiring = set(identities)
    snapshot = {"retired": [], "current": deepcopy(document["stateLayout"]["current"]),
                "pages": deepcopy(document["pages"]),
                "compilerPuzzlesRequired": document.get("compilerPuzzlesRequired", False),
                "referenceAfter": deepcopy(document.get("referenceAfter", [])),
                "activityAfter": deepcopy(document.get("activityAfter", []))}
    for page, record in zip(result["pages"], result["stateLayout"]["current"]):
        old_ids, old_blocks = record["blocks"], page.get("blocks", [])
        for index, identity in enumerate(old_ids):
            if identity not in retiring:
                continue
            require(old_blocks[index].get("type") in RETIRABLE_TYPES,
                    "Only compiler activities and their presentation blocks can be retired")
            successor = next((candidate for candidate in old_ids[index + 1:] if candidate not in retiring), "")
            archive.append({"id": identity, "page": record["id"], "next": successor,
                            "block": deepcopy(old_blocks[index])})
            snapshot["retired"].append(identity)
        page["blocks"] = [block for identity, block in zip(old_ids, old_blocks) if identity not in retiring]
        record["blocks"] = [identity for identity in old_ids if identity not in retiring]
    result["stateLayout"].setdefault("retirementHistory", []).append(snapshot)
    for field, relationships in (("activityAfter", activity_after), ("referenceAfter", reference_after)):
        if relationships is not None:
            result[field] = deepcopy(relationships)
    if compiler_puzzles_required is not None:
        require(type(compiler_puzzles_required) is bool, "compilerPuzzlesRequired must be boolean")
        result["compilerPuzzlesRequired"] = compiler_puzzles_required
    validate_transition(document, result)
    return result


def _validate_transition_v2(previous: dict, current: dict) -> None:
    validate(current)
    baseline = prepare(previous)
    old_meta, meta = baseline["stateLayout"], current["stateLayout"]
    require(meta["origin"] == old_meta["origin"], "The immutable origin was changed; do not reset the baseline")
    old_v2 = old_meta["version"] == 2
    if old_v2:
        require(meta["locale"] == old_meta["locale"], "The historical locale cannot be renamed")
        require(meta["family"] == old_meta["family"], "The immutable historical language family changed")
    else:
        own = meta["family"][meta["locale"]]
        require(own["sha256"] == fingerprint(_without_layout(previous)) and own["origin"] == old_meta["origin"],
                "The historical locale is not anchored to the published source")
    require(_transition_metadata(baseline) == _transition_metadata(current),
            "A reorder cannot modify lesson metadata")
    require(not baseline.get("compilerPuzzlesRequired", False) or current.get("compilerPuzzlesRequired", False),
            "Published mandatory compiler puzzles cannot be silently disabled")
    require(not baseline.get("compilerWorkflowChecks") or
            baseline.get("compilerWorkflowChecks") == current.get("compilerWorkflowChecks"),
            "Published compiler workflow cannot be removed or changed")
    require(not baseline.get("compilerRuntimeChecks") or
            baseline.get("compilerRuntimeChecks") == current.get("compilerRuntimeChecks"),
            "Published compiler runtime objective cannot be removed or rewritten")
    require(not baseline.get("orderVisualSeparators") or
            baseline["orderVisualSeparators"] == current.get("orderVisualSeparators"),
            "Published visual separators cannot lose an established source owner")
    def shells(document):
        return {record["id"]: {k: v for k, v in page.items() if k != "blocks"}
                for page, record in zip(document["pages"], document["stateLayout"]["current"])}
    require(shells(baseline) == shells(current), "Page metadata was changed")
    old_additions = old_meta["additions"] if old_v2 else []
    require(meta["additions"][:len(old_additions)] == old_additions,
            "Activity additions are append-only; identities and storage cannot be recycled")
    old_editorial = old_meta["editorial"] if old_v2 else []
    require(meta["editorial"][:len(old_editorial)] == old_editorial,
            "Editorial proof is append-only; the published provenance cannot be replaced")
    old_retired = old_meta.get("retired", []) if old_v2 else []
    retired = meta.get("retired", [])
    require(retired[:len(old_retired)] == old_retired,
            "Retired activity archives are append-only and cannot be restored or rewritten")
    before, after = _all_blocks(baseline), _all_blocks(current)
    old_refinements = old_meta.get("orderRefinements", []) if old_v2 else []
    refinements = meta.get("orderRefinements", [])
    require(refinements[:len(old_refinements)] == old_refinements,
            "Published order refinements are append-only and cannot be removed or replaced")
    for item in refinements[len(old_refinements):]:
        require(item["id"] in before and fingerprint(item["block"]) == fingerprint(before[item["id"]]),
                "A new order refinement must preserve its complete published block")
    old_choices = old_meta.get("choiceRevisions", []) if old_v2 else []
    choices = meta.get("choiceRevisions", [])
    require(choices[:len(old_choices)] == old_choices,
            "Published choice revisions are append-only and cannot be removed or replaced")
    for item in choices[len(old_choices):]:
        require(item["id"] in before and fingerprint(item["block"]) == fingerprint(before[item["id"]]),
                "A new choice revision must preserve its complete previous published block")
    old_compilers = old_meta.get("compilerRevisions", []) if old_v2 else []
    compilers = meta.get("compilerRevisions", [])
    require(compilers[:len(old_compilers)] == old_compilers,
            "Published compiler revisions are append-only and cannot be removed or replaced")
    for item in compilers[len(old_compilers):]:
        require(item["id"] in before and fingerprint(item["block"]) == fingerprint(before[item["id"]]),
                "A new compiler revision must preserve its complete previous published block")
        for reference in item.get("references", []):
            require(reference["id"] in before and fingerprint(reference["block"]) == fingerprint(before[reference["id"]]),
                    "A new compiler reference must preserve its complete previous published block")
    _preserve_compiler_channel_requirements(before, after)
    old_formats = old_meta.get("sourceFormatRevisions", []) if old_v2 else []
    current_formats = meta.get("sourceFormatRevisions", [])
    require(current_formats[:len(old_formats)] == old_formats,
            "Published source-format archives are append-only")
    archived = {entry["id"] for entry in old_formats}
    for entry in current_formats[len(old_formats):]:
        identity = entry["id"]
        require(identity in before and identity not in archived and entry["block"] == before[identity],
                "A new source-format archive must copy its exact published predecessor")
        try:
            require(code_format.require_source_format(before[identity], after[identity]),
                    "Source formatting archive did not change any source")
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe source edit for {identity}: {exc}") from exc
        archived.add(identity)
    for identity in archived:
        if identity not in before or identity not in after: continue
        try:
            code_format.require_source_lexical(before[identity], after[identity])
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe source edit for {identity}: {exc}") from exc
    old_positions = {identity: (record["id"], index, record["blocks"])
                     for record in old_meta["current"] for index, identity in enumerate(record["blocks"])}
    newly_retired = {item["id"] for item in retired[len(old_retired):]}
    old_history = old_meta.get("retirementHistory", []) if old_v2 else []
    history = meta.get("retirementHistory", [])
    require(history[:len(old_history)] == old_history, "Retirement reading snapshots are append-only")
    appended_history = history[len(old_history):]
    require(not newly_retired or appended_history,
            "A new retirement must archive its previous reading presentation")
    if appended_history:
        require(len(appended_history) == 1 and newly_retired,
                "One transition can archive only its actual previous reading layout")
        snapshot = appended_history[0]
        expected = {"retired": [item["id"] for item in retired[len(old_retired):]],
                    "current": old_meta["current"], "pages": baseline["pages"],
                    "compilerPuzzlesRequired": baseline.get("compilerPuzzlesRequired", False),
                    "referenceAfter": baseline.get("referenceAfter", []),
                    "activityAfter": baseline.get("activityAfter", [])}
        require(fingerprint(snapshot) == fingerprint(expected),
                "Retirement reading snapshot does not match the complete previous published presentation")
    for item in retired[len(old_retired):]:
        identity = item["id"]
        require(identity in old_positions, "A retirement must archive an existing active block")
        require(fingerprint(item["block"]) == fingerprint(before[identity]),
                "A retired block must preserve its complete published snapshot")
        page, index, siblings = old_positions[identity]
        successor = next((candidate for candidate in siblings[index + 1:] if candidate not in newly_retired), "")
        require(item["page"] == page and item["next"] == successor,
                "A retirement must preserve its actual previous position and successor")
    require(before.keys() <= after.keys(), "An existing activity was removed")
    appended = {}
    for proof in meta["editorial"][len(old_editorial):]:
        require(proof["id"] in before, "New activities cannot claim historical editorial provenance")
        appended.setdefault(proof["id"], []).append(proof)
    for identity, proofs in appended.items():
        if not any("bibliography" in proof for proof in proofs):
            continue
        require(all("bibliography" in proof for proof in proofs),
                "New bibliography archives cannot mix unarchived revisions of the same owner")
        archives = [proof["bibliography"] for proof in proofs]
        require(archives[0]["previousBlock"] == before[identity]
                and archives[-1]["currentBlock"] == after[identity]
                and archives[0]["previousTextSemantics"] == _explanation_semantics(previous, identity)
                and archives[-1]["currentTextSemantics"] == _explanation_semantics(current, identity),
                "New bibliography archives must match both actual published and delivered sources and semantics")
        require(all(left["currentBlock"] == right["previousBlock"]
                    and left["currentTextSemantics"] == right["previousTextSemantics"]
                    for left, right in zip(archives, archives[1:])),
                "Consecutive bibliography archives must retain their exact intermediate source and semantics")
    owners = {}
    for identity, block in before.items():
        owners.setdefault(fingerprint(block), set()).add(identity)
    for identity, block in before.items():
        actual = after[identity]
        if bibliography_revision.is_bibliography_only_revision(block, actual):
            require(identity in appended and all("bibliography" in proof for proof in appended[identity]),
                    "A new bibliography-only revision requires its exact authored reading archive")
        protected = actual
        format_original = next((entry["block"] for entry in current_formats if entry["id"] == identity), None)
        if format_original is not None:
            protected = code_format.source_projection(actual, format_original)
        require(state_fingerprint(_historical_order_contract(baseline, identity, block))
                == state_fingerprint(_historical_order_contract(current, identity, protected)),
                "An existing response/code contract changed")
        if str(block.get("type", "")).startswith("math_"):
            serialize = lambda value: json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
            require(serialize(historical_math_payload(block)) == serialize(historical_math_payload(actual)),
                    "Mathematical payloads must preserve their historical JSON key order and values")
        old_hash, new_hash = fingerprint(block), fingerprint(actual)
        proofs = appended.get(identity, [])
        if old_hash != new_hash or proofs:
            require(proofs and proofs[0]["beforeSha256"] == old_hash
                    and proofs[-1]["afterSha256"] == new_hash,
                    "A simultaneous editorial revision needs its explicit published before/after proof")
            require(old_hash != new_hash or any("returnTo" in proof for proof in proofs),
                    "An unchanged block cannot acquire an invented editorial history")
        require(new_hash not in owners or identity in owners[new_hash],
                "Published content was assigned to another identity without a verified move")


# These two AssetCheck lessons were first published with ten incorrect SHA
# values in family.es.origin. Their own origin and literal source were correct.
# Authenticate the exact malformed first deliveries, never a later revision.
_ASSETCHECK_FIRST_DELIVERY_ORIGIN_REPAIRS = {
    "programming_practice_tools__proyecto_assetcheck__preparar_y_comprobar_cmake_en_windows":
        "8a51e32a9094c27e4cd6f3ae685c851774edec54b480808081795359269865cd",
    "programming_practice_tools__proyecto_assetcheck__disenar_probar_y_presentar_variante_propia":
        "35fb6217e01e487d0ec5a976e362b1c86e8bbb560b360ceeeeae3ca7012dfce1",
}


def _literal_first_published_origin(document: dict) -> list[dict]:
    """Recalculate first-delivery slots without discarding required v2 links.

    Calling prepare() on a layout-free copy would invalidate textSemantics and
    referenceAfter, which themselves require stable page/block identities.
    The caller must authenticate the exact original document first.
    """
    return [
        {"id": f"page_{page_index:04d}",
         "blocks": [
             {"id": f"block_{page_index:04d}_{block_index:04d}",
              "sha256": state_fingerprint(block)}
             for block_index, block in enumerate(page["blocks"])
         ]}
        for page_index, page in enumerate(_pages(document))
    ]


def _is_exact_assetcheck_initial_origin_repair(previous: dict, current: dict) -> bool:
    """Accept only the single, source-authenticated correction of two bad manifests.

    The existing original block identities, all source content, answers and
    layout data must remain untouched. A later family rewrite cannot use this.
    """
    published = _ASSETCHECK_FIRST_DELIVERY_ORIGIN_REPAIRS.get(previous.get("id"))
    if published is None or fingerprint(previous) != published:
        return False
    prior = previous.get("stateLayout")
    if (type(prior) is not dict or prior.get("version") != 2 or
            prior.get("locale") != "es" or
            type(prior.get("family")) is not dict or set(prior["family"]) != {"es"}):
        return False
    original_source = _without_layout(previous)
    literal_origin = _literal_first_published_origin(previous)
    require(prior["origin"] == literal_origin,
            "The original AssetCheck owners differ from the first published lesson")
    require(prior["family"]["es"]["sha256"] == fingerprint(original_source),
            "The AssetCheck family does not belong to the first published source")
    repaired = deepcopy(previous)
    repaired["stateLayout"]["family"]["es"]["origin"] = deepcopy(literal_origin)
    if repaired == previous or current != repaired:
        return False
    validate(current)
    return True


def validate_transition(previous: dict, current: dict) -> None:
    """Never authorize a renamed origin or changed answer while allowing a reorder."""
    require("stateLayout" in current, "A reordered lesson needs an identity manifest")
    if type(current["stateLayout"]) is dict and current["stateLayout"].get("version") == 2:
        if _is_exact_assetcheck_initial_origin_repair(previous, current):
            return
        _validate_transition_v2(previous, current)
        return
    require(not (type(previous.get("stateLayout")) is dict and previous["stateLayout"].get("version") == 2),
            "Published v2 metadata cannot be removed or downgraded")
    baseline = prepare(previous)
    validate(current)
    _preserve_compiler_channel_requirements(_blocks(baseline), _blocks(current))
    require(current["stateLayout"]["origin"] == baseline["stateLayout"]["origin"],
            "The immutable origin was changed; do not reset the baseline")
    previous_other = _transition_metadata(baseline)
    current_other = _transition_metadata(current)
    require(previous_other == current_other, "A reorder cannot modify lesson metadata")
    require(not baseline.get("compilerPuzzlesRequired", False) or current.get("compilerPuzzlesRequired", False),
            "Published mandatory compiler puzzles cannot be silently disabled")
    require(not baseline.get("compilerWorkflowChecks") or
            baseline.get("compilerWorkflowChecks") == current.get("compilerWorkflowChecks"),
            "Published compiler workflow cannot be removed or changed")
    require(not baseline.get("compilerRuntimeChecks") or
            baseline.get("compilerRuntimeChecks") == current.get("compilerRuntimeChecks"),
            "Published compiler runtime objective cannot be removed or rewritten")
    require(not baseline.get("orderVisualSeparators") or
            baseline["orderVisualSeparators"] == current.get("orderVisualSeparators"),
            "Published visual separators cannot lose an established source owner")
    before_shells = {item["id"]: {k: v for k, v in page.items() if k != "blocks"}
                     for page, item in zip(baseline["pages"], baseline["stateLayout"]["current"])}
    after_shells = {item["id"]: {k: v for k, v in page.items() if k != "blocks"}
                    for page, item in zip(current["pages"], current["stateLayout"]["current"])}
    require(before_shells == after_shells, "Page metadata was changed")
    before_blocks = {identity: fingerprint(block)
                     for page, record in zip(baseline["pages"], baseline["stateLayout"]["current"])
                     for identity, block in zip(record["blocks"], page.get("blocks", []))}
    after_blocks = {identity: fingerprint(block)
                    for page, record in zip(current["pages"], current["stateLayout"]["current"])
                    for identity, block in zip(record["blocks"], page.get("blocks", []))}
    def math_serializations(document: dict) -> dict[str, str]:
        # Android's existing MathPuzzleState hashes JSONObject.toString(), whose
        # insertion order matters. Keep that legacy fingerprint intact as well
        # as the order-independent state-map digest; do not rewrite saved states.
        return {identity: json.dumps(block, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
                for page, record in zip(document["pages"], document["stateLayout"]["current"])
                for identity, block in zip(record["blocks"], page.get("blocks", []))
                if str(block.get("type", "")).startswith("math_")}
    require(math_serializations(baseline) == math_serializations(current),
            "Mathematical payloads must preserve their historical JSON key order and values")
    if "stateLayout" not in previous or baseline["stateLayout"]["current"] != current["stateLayout"]["current"]:
        # Editorial payloads intentionally omit prose from their state hashes.
        # On first adoption or a move, compare full blocks so a regenerated map
        # cannot attach read coverage to a different, equally shaped paragraph.
        require(before_blocks == after_blocks,
                "First adoption and reorders must preserve complete blocks; "
                "publish editorial revisions separately before or after the move")
    else:
        old_owners = {}
        for identity, signature in before_blocks.items():
            old_owners.setdefault(signature, set()).add(identity)
        for identity, signature in after_blocks.items():
            require(signature not in old_owners or identity in old_owners[signature],
                    "Published content was assigned to another identity without a verified move")


def align_for_previous_validation(previous: dict, current: dict) -> dict:
    """Project new blocks onto HEAD's positions only AFTER validating the whole move.

    The existing validator then continues checking all response/code contracts.
    Legacy content without identity metadata retains the original position rules.
    """
    if "stateLayout" not in previous and "stateLayout" not in current:
        validate(previous)
        validate(current)
        return current
    validate_transition(previous, current)
    old = prepare(previous)
    blocks = _all_blocks(current)
    old_refinements = _order_refinements(old)
    blocks = {identity: _historical_order_contract(current, identity, block)
              if identity not in old_refinements else block for identity, block in blocks.items()}
    old_choices = _choice_revisions(old)
    blocks = {identity: _historical_choice_contract(current, identity, block)
              if identity not in old_choices else block for identity, block in blocks.items()}
    old_compilers = _compiler_revisions(old) | _compiler_references(old)
    blocks = {identity: _historical_compiler_contract(current, identity, block)
              if identity not in old_compilers else block for identity, block in blocks.items()}
    page_by_id = {record["id"]: page for page, record in zip(current["pages"], current["stateLayout"]["current"])}
    result = deepcopy(current)
    result["pages"] = [{**deepcopy(page_by_id[record["id"]]),
                        "blocks": [deepcopy(blocks[i]) for i in record["blocks"]]}
                       for record in old["stateLayout"]["current"]]
    return result


def original_projection(document: dict) -> dict:
    """In-memory lookup of historical sources for existing curricular exceptions.

    This is neither a new lesson nor a regenerated manifest. New activities are
    intentionally absent from the original source lookup, never from validation.
    """
    if "stateLayout" not in document:
        return document
    validate(document)
    blocks = _all_blocks(document)
    blocks = {identity: _historical_order_contract(document, identity, block) for identity, block in blocks.items()}
    blocks = {identity: _historical_choice_contract(document, identity, block) for identity, block in blocks.items()}
    blocks = {identity: _historical_compiler_contract(document, identity, block) for identity, block in blocks.items()}
    shells = {record["id"]: {key: value for key, value in page.items() if key != "blocks"}
              for page, record in zip(document["pages"], document["stateLayout"]["current"])}
    result = deepcopy(_without_layout(document))
    result["pages"] = [{**deepcopy(shells[record["id"]]),
                        "blocks": [deepcopy(blocks[block["id"]]) for block in record["blocks"]]}
                       for record in document["stateLayout"]["origin"]]
    return result


def lineage(document: dict) -> tuple:
    metadata = prepare(document)["stateLayout"]
    return tuple((page["id"], tuple(block["id"] for block in page["blocks"]))
                 for page in metadata["origin"])


def check_localized_family(documents: list[dict], locales: list[str] | None = None) -> None:
    """Call with every shipped translation before publishing the first adoption."""
    require(bool(documents), "No lessons to verify")
    v2 = [d for d in documents if type(d.get("stateLayout")) is dict and d["stateLayout"].get("version") == 2]
    if v2:
        for document in documents:
            validate(document)
        require(len({d["id"] for d in documents}) == 1, "Not the same lesson")
        family = v2[0]["stateLayout"]["family"]
        require(all(d["stateLayout"]["family"] == family for d in v2),
                "Localized v2 maps disagree on the historical language family")
        if locales is None:
            require(len(v2) == len(documents), "Historical translations need their explicit source locales")
            locales = [d["stateLayout"]["locale"] for d in documents]
        require(len(locales) == len(documents) and len(set(locales)) == len(locales),
                "Duplicate or missing localized lesson source")
        require(set(locales) == set(family), "The historical language family must include every shipped translation")
        for locale, document in zip(locales, documents):
            require(locale in LOCALES, "Unsupported historical locale")
            meta = document.get("stateLayout")
            if meta is None and "stateLayout" not in document:
                require(fingerprint(document) == family[locale]["sha256"]
                        and prepare(document)["stateLayout"]["origin"] == family[locale]["origin"],
                        "An unmapped historical translation changed; keep it intact or adopt its own v2 map")
            else:
                require(meta["origin"] == family[locale]["origin"], "A translation rewrote its historical origin")
                if meta["version"] == 2:
                    require(meta["locale"] == locale, "The map locale does not match its lesson source")
        # Slots of additions must not acquire independent owners in another map.
        slots, owners = {}, {}
        for document in v2:
            blocks = _all_blocks(document)
            for addition in document["stateLayout"]["additions"]:
                slot = (addition["storage"]["page"], addition["storage"]["block"])
                require(slot not in slots or slots[slot] == addition["id"],
                        "Localized new activities collide in shared storage")
                slots[slot] = addition["id"]
                block = blocks[addition["id"]]
                descriptor = slot, block.get("type"), block.get("id")
                require(addition["id"] not in owners or owners[addition["id"]] == descriptor,
                        "A localized new identity changed storage or activity ownership")
                owners[addition["id"]] = descriptor
        return
    require(all("stateLayout" in d for d in documents), "Every shipped translation needs a state map")
    for document in documents:
        validate(document)
    require(len({d["id"] for d in documents}) == 1, "Not the same lesson")
    require(len({lineage(d) for d in documents}) == 1, "Translations disagree on stable identities")
    # Existing exercise IDs and block types must represent the same original slots.
    descriptors = []
    for doc in documents:
        mapping = validate(doc)
        descriptors.append({original: (doc["pages"][p]["blocks"][b].get("type"),
                                       doc["pages"][p]["blocks"][b].get("id"))
                            for (p, b), original in mapping.items()})
    require(all(d == descriptors[0] for d in descriptors[1:]), "Translations have different original activities")


def parse_json(text: str):
    def pairs(items):
        result = {}
        for k, v in items:
            require(k not in result, "Duplicate JSON key")
            result[k] = v
        return result
    return json.loads(text, object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(LayoutError("Non-finite number")))


def load(path: Path):
    return parse_json(path.read_text(encoding="utf-8-sig"))


CONTENT_DIRECTORY = "app/src/main/assets/content"


def resolve_base_commit(repository: Path, base_ref: str) -> str:
    """Pin the caller's verified published ref before reading any baseline file."""
    require(bool(base_ref), "Provide the verified published commit with --base-ref")
    result = subprocess.run(
        ["git", "rev-parse", "--verify", "--end-of-options", base_ref + "^{commit}"],
        cwd=repository, capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot resolve the published baseline: " + result.stderr.strip())
    commit = result.stdout.strip()
    require(re.fullmatch(r"[0-9a-f]{40,64}", commit) is not None, "Invalid baseline commit")
    return commit


def _git_document(repository: Path, commit: str, relative: str) -> dict:
    result = subprocess.run(["git", "show", commit + ":" + relative], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot read baseline lesson " + relative)
    return parse_json(result.stdout.lstrip("\ufeff"))


def _lesson_paths_at(repository: Path, commit: str) -> set[str]:
    result = subprocess.run(["git", "ls-tree", "-r", "--name-only", "-z", commit,
                             "--", CONTENT_DIRECTORY], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot list baseline lessons: " + result.stderr.strip())
    return {path for path in result.stdout.split("\0")
            if PurePosixPath(path).name.startswith("lesson_") and path.endswith(".json")}


def _mapped_paths_at(repository: Path, commit: str) -> set[str]:
    # Also inspect the old tree: deleting a manifest must not bypass the gate.
    result = subprocess.run(["git", "grep", "-l", "-z", "-F", '"stateLayout"', commit,
                             "--", CONTENT_DIRECTORY], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode in (0, 1), "Cannot inspect baseline state maps: " + result.stderr.strip())
    prefix = commit + ":"
    return {item[len(prefix):] for item in result.stdout.split("\0")
            if item.startswith(prefix) and item.endswith(".json")
            and PurePosixPath(item).name.startswith("lesson_")}


def _validate_family_baseline(repository: Path, commit: str, family: str, members: list,
                              prior_paths: set[str], prior_documents: dict, identity_families: dict) -> None:
    """Authenticate published histories, or one literal first delivery of a new family."""
    locales = [PurePosixPath(path).relative_to(CONTENT_DIRECTORY).parts[0] for path, _ in members]
    check_localized_family([document for _, document in members], locales)
    v2 = [document for _, document in members
          if type(document.get("stateLayout")) is dict and document["stateLayout"].get("version") == 2]
    if not v2:
        return
    manifest = v2[0]["stateLayout"]["family"]
    historical_paths = {f"{CONTENT_DIRECTORY}/{locale}/{family}" for locale in LOCALES} & prior_paths
    if not historical_paths:
        # Only this branch has no historical student slots. An unchanged,
        # literal first layout is proved from the delivered sources themselves;
        # no unpublished earlier wording, move, revision or addition is inferred.
        identity = v2[0]["id"]
        require(identity_families.get(identity) == {family},
                "A new language family cannot share its lesson identity with another current family")
        for path in sorted(prior_paths):
            if path not in prior_documents:
                prior_documents[path] = _git_document(repository, commit, path)
            require(prior_documents[path].get("id") != identity,
                    "A new language family cannot reuse a published lesson identity from another path")
        for (path, document), locale in zip(members, locales):
            require(path == f"{CONTENT_DIRECTORY}/{locale}/{family}" and path not in prior_paths,
                    "A first language delivery must contain genuinely new lesson files")
            meta = document.get("stateLayout")
            require(type(meta) is dict and meta.get("version") == 2,
                    "Every initial language source must carry its own complete v2 map")
            require(meta["additions"] == [] and meta["editorial"] == [] and
                    all(meta.get(field, []) == [] for field in V2_OPTIONAL_FIELDS),
                    "A first language delivery cannot claim earlier additions, retirements or revisions")
            source = {key: value for key, value in document.items()
                      if key != "stateLayout" and key not in TEXT_PRESENTATION_FIELDS}
            literal = prepare(source)["stateLayout"]
            require(meta["origin"] == literal["origin"] and meta["current"] == literal["current"],
                    "A first language delivery must retain its literal original page and block identities")
            require(manifest[locale] == {"sha256": fingerprint(source), "origin": literal["origin"]},
                    "An initial language source is not anchored to its exact delivered source")
        return
    for locale, source in manifest.items():
        relative = f"{CONTENT_DIRECTORY}/{locale}/{family}"
        require(relative in prior_paths, "A historical language source is absent from the published baseline")
        if relative not in prior_documents:
            prior_documents[relative] = _git_document(repository, commit, relative)
        previous = prior_documents[relative]
        old_meta = previous.get("stateLayout")
        if type(old_meta) is dict and old_meta.get("version") == 2:
            if source != old_meta["family"][locale]:
                corrected = next((document for path, document in members if path == relative), None)
                require(corrected is not None
                        and _is_exact_assetcheck_initial_origin_repair(previous, corrected),
                        "The published language provenance was replaced")
        else:
            require(source["sha256"] == fingerprint(_without_layout(previous))
                    and source["origin"] == prepare(previous)["stateLayout"]["origin"],
                    "A historical language source is not anchored to the published baseline")


def check_repository(repository: Path, base_ref: str | None) -> dict:
    """Check every route and shipped locale against a pinned published tree.

    This is a publication guard, not just a self-consistency check. No network,
    branch, student preference, content, or Git index is changed. A caller must
    fetch/verify the remote HEAD itself and supply that exact baseline.
    """
    repository = repository.resolve()
    content = repository / CONTENT_DIRECTORY
    require(content.is_dir(), "Missing curriculum content directory")
    documents = {path.relative_to(repository).as_posix(): load(path)
                 for path in sorted(content.rglob("lesson_*.json"))}
    for path, document in documents.items():
        if (PRESENTATION_FIELDS | VISUAL_PRESENTATION_FIELDS | FLOW_FIELDS | TEXT_PRESENTATION_FIELDS) & document.keys():
            try:
                validate(document)
            except (ValueError, KeyError, TypeError) as exc:
                raise LayoutError(f"Invalid presentation relationships in {path}: {exc}") from exc
    mapped = {path for path, document in documents.items() if "stateLayout" in document}
    # A metadata-bearing tree cannot claim safety without its published history.
    require(not mapped or base_ref is not None,
            "stateLayout publication requires --base-ref with the verified remote HEAD")
    if base_ref is None:
        if (repository / ".git").exists():
            head = resolve_base_commit(repository, "HEAD")
            require(not any("stateLayout" in _git_document(repository, head, path)
                            for path in _mapped_paths_at(repository, head)),
                    "Published stateLayout metadata requires --base-ref even when it was removed locally")
        return {"base_commit": None, "mapped_lessons": 0, "families": 0}
    commit = resolve_base_commit(repository, base_ref)
    prior_paths = _lesson_paths_at(repository, commit)
    prior_mapped = {}
    for path in _mapped_paths_at(repository, commit):
        previous = _git_document(repository, commit, path)
        if "stateLayout" in previous:
            prior_mapped[path] = previous
    prior_documents = dict(prior_mapped)
    for path in mapped | prior_mapped.keys():
        require(path in documents, "A mapped lesson was removed: " + path)
        current = documents[path]
        try:
            if path in prior_paths:
                previous = prior_mapped.get(path)
                if previous is None:
                    previous = _git_document(repository, commit, path)
                    prior_documents[path] = previous
                validate_transition(previous, current)
            else:
                # A new lesson has no positional state in the published app.
                validate(current)
        except (ValueError, KeyError, TypeError) as exc:
            raise LayoutError(path + ": " + str(exc)) from exc
    families, identity_families = {}, {}
    for path, document in documents.items():
        relative = PurePosixPath(path).relative_to(CONTENT_DIRECTORY)
        require(len(relative.parts) >= 2, "A lesson must belong to a locale: " + path)
        family = PurePosixPath(*relative.parts[1:]).as_posix()
        families.setdefault(family, []).append((path, document))
        if type(document.get("id")) is str:
            identity_families.setdefault(document["id"], set()).add(family)
    checked_families = 0
    for family, members in families.items():
        if not any(path in mapped for path, _ in members):
            continue
        try:
            _validate_family_baseline(repository, commit, family, members, prior_paths, prior_documents, identity_families)
        except (ValueError, KeyError, TypeError) as exc:
            raise LayoutError(family + ": " + str(exc)) from exc
        checked_families += 1
    return {"base_commit": commit, "mapped_lessons": len(mapped), "families": checked_families}


def published_family(source: Path, locale: str, base_ref: str) -> tuple[dict, dict[str, dict]]:
    """Read original assets directly from Git; do not export backup copies."""
    source = source.resolve()
    result = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=source.parent,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "The source must be an asset in its existing Git checkout")
    repository = Path(result.stdout.strip()).resolve()
    try:
        relative = source.relative_to(repository).as_posix()
        asset = PurePosixPath(relative).relative_to(CONTENT_DIRECTORY)
    except ValueError as exc:
        raise LayoutError("The source must be a curricular lesson asset") from exc
    require(len(asset.parts) >= 2 and asset.parts[0] == locale,
            "--locale must match the curricular source path")
    commit = resolve_base_commit(repository, base_ref)
    tail = PurePosixPath(*asset.parts[1:]).as_posix()
    existing = _lesson_paths_at(repository, commit)
    family = {}
    for language in sorted(LOCALES):
        path = f"{CONTENT_DIRECTORY}/{language}/{tail}"
        if path in existing:
            family[language] = _git_document(repository, commit, path)
    require(locale in family, "The published baseline does not contain this locale")
    return family[locale], family


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "reorder", "refine-order-lines", "revise-choice-option", "revise-compiler-cases", "check", "check-tree"))
    parser.add_argument("source", type=Path, help="Lesson JSON; repository directory for check-tree")
    parser.add_argument("--plan", type=Path, help="JSON list of stable page/block IDs in display order")
    parser.add_argument("--output", type=Path, help="New path; never overwrite a Work/Codex file")
    parser.add_argument("--previous", type=Path, help="Required published lesson for prepare/reorder/check")
    parser.add_argument("--base-ref", help="Required verified remote HEAD for check-tree")
    parser.add_argument("--locale", choices=sorted(LOCALES), help="Use v2 and read all published language assets from Git")
    parser.add_argument("--additions", type=Path,
                        help="Authoring JSON list: id, storage, progressPage and block; requires v2 and --plan")
    parser.add_argument("--identity", help="Existing reviewed stable identity for a refinement/revision")
    parser.add_argument("--editorial-return", action="append", default=[], metavar="ID:REVISION",
                        help="Explicit earlier payload of the same identity; prepare with --locale and Git --base-ref only")
    args = parser.parse_args()
    try:
        editorial_returns = {}
        for value in args.editorial_return:
            identity, separator, revision = value.rpartition(":")
            require(separator and re.fullmatch(r"0|[1-9][0-9]*", revision) is not None,
                    "Use --editorial-return ID:REVISION with a nonnegative revision number")
            _identity(identity)
            require(identity not in editorial_returns, "An editorial return identity cannot be repeated")
            editorial_returns[identity] = int(revision)
        require(not editorial_returns or (args.action == "prepare" and args.base_ref is not None),
                "Explicit editorial returns require prepare with --locale and Git --base-ref")
        if args.action == "check-tree":
            require(args.base_ref is not None, "check-tree requires --base-ref with the verified remote HEAD")
            report = check_repository(args.source, args.base_ref)
            print(f"State transitions and shipped translations checked against {report['base_commit']}: "
                  f"{report['mapped_lessons']} mapped lessons in {report['families']} families. "
                  "This does not verify an Android build or device execution.")
            return
        source = load(args.source)
        require(type(source) is dict, "A lesson must be a JSON object")
        locale = args.locale
        if locale is None and type(source.get("stateLayout")) is dict and source["stateLayout"].get("version") == 2:
            locale = source["stateLayout"]["locale"]
        if args.base_ref is not None and locale is not None:
            require(args.previous is None, "Use either Git --base-ref or a manual --previous, not both")
            previous, family = published_family(args.source, locale, args.base_ref)
        else:
            require(args.previous is not None,
                    "prepare/reorder/check requires --previous from the verified published commit, "
                    "or --locale with Git --base-ref")
            require(args.locale is None, "First v2 adoption uses --locale and Git --base-ref")
            previous, family = load(args.previous), None
        require(args.additions is None or args.plan is not None, "Activity additions require an explicit --plan")
        if args.action == "prepare":
            require(not editorial_returns or family is not None, "Explicit editorial returns require a verified v2 language family")
            result = prepare_v2(source, locale, family, previous, editorial_returns=editorial_returns) if family is not None else prepare(source)
            if args.plan is not None:
                result = reorder(result, load(args.plan), load(args.additions) if args.additions else None)
        elif args.action == "reorder":
            require(args.plan is not None, "reorder requires --plan")
            result = reorder(source, load(args.plan), load(args.additions) if args.additions else None)
        elif args.action == "refine-order-lines":
            require(args.identity is not None and args.plan is None and args.additions is None,
                    "refine-order-lines requires --identity without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = refine_order_lines(baseline, args.identity)
        elif args.action == "revise-choice-option":
            require(args.identity is not None and args.plan is None and args.additions is None,
                    "revise-choice-option requires --identity without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_choice_option(baseline, args.identity)
        elif args.action == "revise-compiler-cases":
            require(args.identity is not None and args.plan is None and args.additions is None,
                    "revise-compiler-cases requires --identity without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_compiler_cases(baseline, args.identity)
        else:
            validate(source)
            validate_transition(previous, source)
            print("Lesson transition valid; run check-tree for all shipped translations before publication. "
                  "No student data or files changed.")
            return
        require(args.output is not None, "Use --output with a NEW path")
        validate_transition(previous, result)
        # Exclusive creation keeps unrelated local edits intact.
        with args.output.open("x", encoding="utf-8", newline="\n") as out:
            json.dump(result, out, ensure_ascii=False, indent=2, allow_nan=False)
            out.write("\n")
        print("Prepared without changing the source; verify every shipped translation before publishing.")
    except (LayoutError, OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f"Not completed: {exc}\n")


if __name__ == "__main__":
    main()

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

import identifier_rename_compat as identifier_rename
from importlib import util as import_util

_semantics_spec = import_util.spec_from_file_location("lgaed_text_semantics", Path(__file__).with_name("text_semantics.py"))
text_semantics = import_util.module_from_spec(_semantics_spec)
_semantics_spec.loader.exec_module(text_semantics)
_images_spec = import_util.spec_from_file_location("lgaed_image_resources", Path(__file__).with_name("image_resources.py"))
image_resources = import_util.module_from_spec(_images_spec)
_images_spec.loader.exec_module(image_resources)
_simulation_spec = import_util.spec_from_file_location("lgaed_simulation", Path(__file__).with_name("simulation.py"))
simulation = import_util.module_from_spec(_simulation_spec)
_simulation_spec.loader.exec_module(simulation)
_bibliography_spec = import_util.spec_from_file_location("lgaed_bibliography_revision", Path(__file__).with_name("bibliography_revision.py"))
bibliography_revision = import_util.module_from_spec(_bibliography_spec)
_bibliography_spec.loader.exec_module(bibliography_revision)
_format_spec = import_util.spec_from_file_location("lgaed_code_format", Path(__file__).with_name("code_format_compat.py"))
code_format = import_util.module_from_spec(_format_spec)
_format_spec.loader.exec_module(code_format)
_edition_spec = import_util.spec_from_file_location("lgaed_course_edition", Path(__file__).with_name("course_edition.py"))
course_edition = import_util.module_from_spec(_edition_spec)
_edition_spec.loader.exec_module(course_edition)


class LayoutError(ValueError):
    pass


LOCALES = frozenset({"es", "en", "fr"})
V2_FIELDS = {"version", "locale", "origin", "current", "family", "additions", "editorial"}
V2_OPTIONAL_FIELDS = frozenset({"retired", "retirementHistory", "orderRefinements", "choiceRevisions", "compilerRevisions", "sourceFormatRevisions", "mathTextRevisions", "compilerCaseOrderRevisions", "compilerCaseRevisions", "identifierRenameRevisions", "mathMatchingTextRevisions"})
RETIRABLE_TYPES = frozenset({"heading", "paragraph", "note", "code", "cpp_compiler", "image", "simulation"})
PRESENTATION_FIELDS = frozenset({"referenceAfter", "activityAfter"})
VISUAL_PRESENTATION_FIELDS = frozenset({"orderVisualSeparators"})
FLOW_FIELDS = frozenset({"compilerPolymorphicCloneChecks", "compilerInterfaceCollectionChecks", "compilerPolymorphicDestructionChecks", "compilerVirtualLifetimeDispatchChecks", "compilerDerivedPreparationChecks", "compilerAbstractInterfaceChecks", "compilerPuzzlesRequired", "compilerWorkflowChecks", "compilerRuntimeChecks", "compilerNamedRuleChecks", "compilerIndexedUniqueTransferChecks", "compilerDefaultedMovePolicyChecks", "compilerIndependentCopyAssignmentChecks", "compilerMoveAssignmentInvariantChecks", "compilerArrayOwnerReadChecks", "compilerArrayCopyMoveChecks", "compilerArrayReturnTakeChecks", "compilerOpenBatteryChecks", "compilerReductionChecks", "compilerCmakeGraphChecks", "orderEquivalenceGroups"})
TEXT_PRESENTATION_FIELDS = frozenset({text_semantics.ROOT_FIELD})
ORDINARY_EDITORIAL_TYPES = frozenset({"multiple_choice", "fill_code", "output_prediction",
                                      "order_code", "drag_fill_code", "inline_fill_code"})
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


def require_source_format_with_editorial(original: dict, current: dict) -> bool:
    """Authenticate physical source spacing alongside a separate prose correction.

    Only code tokens may be reformatted. The restored source must retain the
    entire earlier answer/evaluation contract; other changes need the normal
    append-only editorial proof, checked by validate_transition().
    """
    changed = code_format.require_source_lexical(original, current)
    projected = code_format.source_projection(current, original)

    def restore_feedback(before: object, after: object) -> object:
        if type(before) is str and type(after) is str:
            require(before == after or bool(before.strip() and after.strip()),
                    "Revised feedback must retain nonblank authored text")
            return before
        if type(before) is list and type(after) is list:
            require(len(before) == len(after),
                    "An editorial correction cannot add or remove feedback choices")
            return [restore_feedback(a, b) for a, b in zip(before, after)]
        if type(before) is dict and type(after) is dict:
            require(before.keys() == after.keys(),
                    "A reviewed guide cannot add or remove feedback fields")
            restored = deepcopy(after)
            for key, value in before.items():
                if key == "text":
                    restored[key] = restore_feedback(value, after[key])
                else:
                    require(value == after[key],
                            "A reviewed guide changed nontext feedback metadata")
            return restored
        require(before == after, "A reviewed guide changed its data type or contract")
        return deepcopy(before)

    def restore_guides(before: dict, after: dict, *, allow_optional_explanation: bool = False) -> None:
        for key in ("explanation", "reviewFeedback"):
            if key not in before and key not in after:
                continue
            if key == "explanation" and key not in before and allow_optional_explanation:
                # Ordinary exercise explanations are optional prose. Restore
                # their genuine historical absence when source formatting and
                # a newly authored explanation share one editorial edge.
                # Nested compiler/output goals cannot use this exception.
                require(type(after.get(key)) is str and bool(after[key].strip()),
                        "A combined optional explanation must contain nonblank authored text")
                after.pop(key)
                continue
            require(key in before and key in after,
                    "A combined review cannot add or remove guidance fields")
            after[key] = restore_feedback(before[key], after[key])

    # The question is public prose, not a token of the source or an answer.
    # A combined question change is accepted only with its exact authored
    # semantic descriptor and an append-only editorial proof, checked by the
    # whole-lesson validation below. Nested compiler questions remain strict.
    def restore_public_question(before: dict, after: dict) -> None:
        if "question" not in before and "question" not in after:
            return
        require("question" in before and "question" in after,
                "A combined review cannot add or remove a question")
        after["question"] = restore_feedback(before["question"], after["question"])

    restore_public_question(original, projected)
    restore_guides(original, projected,
                   allow_optional_explanation=original.get("type") in ORDINARY_EDITORIAL_TYPES)
    for field in ("compileCheck", "outputCheck"):
        older, newer = original.get(field), projected.get(field)
        if type(older) is dict and type(newer) is dict:
            restore_guides(older, newer)
            if field == "outputCheck":
                earlier_cases, current_cases = older.get("cases"), newer.get("cases")
                if type(earlier_cases) is list and type(current_cases) is list:
                    require(len(earlier_cases) == len(current_cases),
                            "An editorial review cannot change the number of cases")
                    for before_case, current_case in zip(earlier_cases, current_cases):
                        if type(before_case) is dict and type(current_case) is dict:
                            restore_guides(before_case, current_case)
    require(projected == original,
            "A combined source/prose revision changed a question, file, answer or non-guide field")
    require(state_fingerprint(projected) == state_fingerprint(original),
            "A combined source/prose revision changed a response or code contract")
    return changed


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


def _math_text_revisions(document: dict) -> dict[str, dict]:
    return {item["id"]: item for item in
            document.get("stateLayout", {}).get("mathTextRevisions", [])}


def _math_ordered_json(value: object) -> str:
    """Keep mathematical insertion order and numeric types, unlike a state SHA."""
    try:
        fingerprint(value)  # Also enforces finite numbers, Unicode and depth.
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    except (TypeError, ValueError, RecursionError) as exc:
        raise LayoutError("Invalid ordered mathematical JSON source") from exc


def _math_prompt_original(item: dict) -> dict:
    raw = item.get("rawJson")
    require(type(raw) is str, "A math prompt archive needs its original JSON string")
    try:
        require(len(raw.encode("utf-8")) <= 32 * 1024 * 1024,
                "The archived mathematical source exceeds the document limit")
        original = parse_json(raw)  # Reject duplicate keys and non-finite literals.
        _math_ordered_json(original)
        def runtime_numbers(node: object) -> None:
            # Windows v2 currently requires integer JSON tokens in its exact
            # content adapter. An embedded string must not bypass that guard.
            require(type(node) is not float,
                    "Decimal JSON values require an exact decimal content adapter")
            if type(node) is int:
                require(abs(node) <= 9_007_199_254_740_991,
                        "Mathematical JSON integer exceeds the runtime's exact range")
            elif type(node) is dict:
                for value in node.values():
                    runtime_numbers(value)
            elif type(node) is list:
                for value in node:
                    runtime_numbers(value)
        runtime_numbers(original)
    except (TypeError, ValueError, RecursionError) as exc:
        raise LayoutError("Invalid original JSON in math prompt archive") from exc
    require(type(original) is dict and original.get("type") == "math_puzzle"
            and type(original.get("prompt")) is str and bool(original["prompt"].strip())
            and "question" not in original,
            "A math prompt archive needs a complete math_puzzle with one nonblank prompt")
    return original


def _math_prompt_projection(item: dict, block: dict, *, historical: bool = False) -> dict:
    """Project only /prompt; all other JSON order, types and values remain exact."""
    original = _math_prompt_original(item)
    require(type(block) is dict and block.get("type") == "math_puzzle"
            and type(block.get("prompt")) is str and bool(block["prompt"].strip())
            and "question" not in block,
            "A math prompt revision must keep its complete active mathematical owner")
    restored = deepcopy(block)
    restored["prompt"] = original["prompt"]
    before, after = original, restored
    if historical:
        # Older reading snapshots may precede the already authenticated closed
        # label refinements. They never authorize a new label or answer change.
        before, after = historical_math_payload(before), historical_math_payload(after)
    require(_math_ordered_json(before) == _math_ordered_json(after),
            "A math prompt revision changed another field, JSON key order or numeric value/type")
    return original


def _math_prompt_semantic_entry(document: dict, identity: str) -> dict | None:
    overlay = document.get(text_semantics.ROOT_FIELD)
    if overlay is None:
        return None
    require(type(overlay) is dict and type(overlay.get("fields")) is list,
            "Malformed semantic overlay for a math prompt revision")
    matches = [entry for entry in overlay["fields"] if type(entry) is dict
               and entry.get("block") == identity and entry.get("path") == "/prompt"]
    require(len(matches) <= 1, "Duplicate semantic source for revised mathematical prompt")
    return matches[0] if matches else None


def _historical_math_text_contract(document: dict, identity: str, block: dict,
                                   *, reading_snapshot: bool = False) -> dict:
    item = _math_text_revisions(document).get(identity)
    if item is None:
        return block
    if reading_snapshot:
        nodes = {proof[field] for proof in document["stateLayout"]["editorial"]
                 if proof["id"] == identity for field in ("beforeSha256", "afterSha256")}
        require(fingerprint(block) in nodes,
                "A historical math prompt must be an authenticated editorial source")
    return _math_prompt_projection(item, block, historical=reading_snapshot)


def _math_prompt_semantic_projection(document: dict, overlay: dict | None,
                                      *, identities: set[str] | None = None) -> dict | None:
    """Restore original prompt descriptors without borrowing a newer text SHA."""
    result = deepcopy(overlay)
    if result is None:
        return None
    for item in document.get("stateLayout", {}).get("mathTextRevisions", []):
        if identities is not None and item["id"] not in identities:
            continue
        fields = result["fields"]
        matches = [index for index, field in enumerate(fields)
                   if field.get("block") == item["id"] and field.get("path") == "/prompt"]
        require(len(matches) == 1, "A revised math prompt lost its explicit semantic source")
        if item["semanticBefore"] is None:
            fields.pop(matches[0])
        else:
            fields[matches[0]] = deepcopy(item["semanticBefore"])
    # Absence and an explicitly empty overlay both mean no authored descriptors.
    # Normalize only this presentation view, never an immutable state signature.
    return result if result["fields"] or result.get("rootFields") else None


def _require_math_prompt_authorship(previous: dict, current: dict, identity: str) -> None:
    def fields(document: dict) -> dict:
        return {entry["path"]: entry for entry in
                document.get(text_semantics.ROOT_FIELD, {}).get("fields", [])
                if type(entry) is dict and entry.get("block") == identity}
    before, after = fields(previous), fields(current)
    require("/prompt" in after, "A math prompt revision needs explicit authored semantics")
    require({path: field for path, field in before.items() if path != "/prompt"}
            == {path: field for path, field in after.items() if path != "/prompt"},
            "A math prompt revision repainted another public field")
    old = before.get("/prompt")
    require(old is None or not old["spans"] or bool(after["/prompt"]["spans"]),
            "A revised marked prompt cannot silently lose all its authored highlights")


def _general_choice_revision(document: dict, identity: str) -> dict | None:
    return next((item for item in document.get("stateLayout", {}).get("choiceRevisions", [])
                 if item["id"] == identity and "optionIndex" in item), None)


def _choice_semantic_entry(document: dict, identity: str, option_index: int) -> dict | None:
    """Find only the exact existing public-text marker for the revised option."""
    overlay = document.get(text_semantics.ROOT_FIELD)
    if overlay is None:
        return None
    require(type(overlay) is dict and type(overlay.get("fields")) is list,
            "Malformed semantic overlay for an editorial choice revision")
    path = f"/options/{option_index}"
    matches = [field for field in overlay["fields"]
               if type(field) is dict and field.get("block") == identity
               and field.get("path") == path]
    require(len(matches) <= 1, "Duplicate semantic source for revised option")
    return matches[0] if matches else None


def _require_formatted_question_authorship(previous: dict, current: dict,
                                           identity: str, before: dict, after: dict) -> None:
    """Require exact authored semantics for prose changed with source formatting.

    Previously this protected only a revised /question. A new or modified
    /explanation also needs an explicit marker, including an empty span array
    for ordinary unmarked prose. The code/answer and other public descriptors
    remain owned by the original publication. Compiler feedback is checked by
    its separate, immutable diagnostic objective.
    """
    changed_question = before.get("question") != after.get("question")
    changed_explanation = (before.get("type") in ORDINARY_EDITORIAL_TYPES
                           and before.get("explanation") != after.get("explanation"))
    if not changed_question and not changed_explanation:
        return
    paths = set()
    if changed_question:
        require(type(before.get("question")) is str and bool(before["question"].strip())
                and type(after.get("question")) is str and bool(after["question"].strip()),
                "A formatted question revision requires two complete nonblank questions")
        paths.add("/question")
    if changed_explanation:
        # Newly added optional explanation is allowed, but neither removing
        # an existing one nor replacing it with whitespace is an editorial
        # correction. The same rule applies to a previously published guide.
        require(type(after.get("explanation")) is str and bool(after["explanation"].strip())
                and ("explanation" not in before
                     or (type(before["explanation"]) is str
                         and bool(before["explanation"].strip()))),
                "A formatted explanation revision requires complete authored prose")
        paths.add("/explanation")

    def markers(document: dict) -> dict:
        return {entry["path"]: entry for entry in
                document.get(text_semantics.ROOT_FIELD, {}).get("fields", [])
                if type(entry) is dict and entry.get("block") == identity}

    old, revised = markers(previous), markers(current)
    require(paths <= revised.keys(),
            "A combined source/prose revision requires explicit authored text semantics")
    feedback = before.get("reviewFeedback") != after.get("reviewFeedback")
    require(all(old.get(path) == revised.get(path)
                for path in (old.keys() | revised.keys())
                if path not in paths
                and not (feedback and (path == "/reviewFeedback"
                                       or path.startswith("/reviewFeedback/")))),
            "A combined source/prose revision repainted an unchanged public field")


def _general_choice_semantic_projection(
        document: dict, *, identities: set[str] | None = None,
        preserve_unrelated_fields: bool = False) -> dict | None:
    """Restore exact previous option marks, never the revised UTF-16 offsets.

    With identities omitted, recover the original publication as before.
    A transition to a particular preceding edition rewinds only revisions
    introduced by that transition; marks authored for other current fields
    must remain intact for the curricular comparison.
    """
    overlay = deepcopy(document.get(text_semantics.ROOT_FIELD))
    if overlay is None:
        return None
    shadow = {**document, text_semantics.ROOT_FIELD: overlay}
    introduced_root = False
    for item in document.get("stateLayout", {}).get("choiceRevisions", []):
        if "optionIndex" not in item:
            continue
        identity, index = item["id"], item["optionIndex"]
        if identities is not None and identity not in identities:
            continue
        marker = _choice_semantic_entry(shadow, identity, index)
        archived = item["semanticBefore"]
        if "semanticAfter" in item:
            expected = item["semanticAfter"]
            introduced_root = introduced_root or not item["semanticRootBefore"]
        elif archived is None:
            expected = None
        else:
            expected = deepcopy(archived)
            expected["sourceSha256"] = hashlib.sha256(item["replacement"].encode("utf-8")).hexdigest()
        require(marker == expected,
                "Revised choice semantic spans differ from their authenticated archive")
        if archived is None:
            # The option used to have no descriptor. An explicitly added mark
            # belongs only to the new wording and must not enter its history.
            overlay["fields"] = [field for field in overlay["fields"]
                                 if (field["block"], field["path"]) !=
                                 (identity, f"/options/{index}")]
        else:
            require(marker is not None, "A reviewed choice lost its archived semantic owner")
            marker.clear()
            marker.update(deepcopy(archived))
    if introduced_root:
        # For the absolute historical publication there was no semantic root.
        # When aligning only a later transition, preserve descriptors added
        # for OTHER current fields; they are not being projected backward.
        if not preserve_unrelated_fields or (
                not overlay["fields"] and not overlay.get("rootFields", [])):
            return None
    return overlay


def _general_choice_projection(item: dict, block: dict) -> dict:
    """Only the declared option text may differ from the exact archived choice.

    The complete predecessor, response position and other options remain
    immutable. No arbitrary choice/answer contract is normalized.
    """
    original = item["block"]
    expected = deepcopy(original)
    index = item["optionIndex"]
    expected["options"][index] = item["replacement"]
    require(block == original or block == expected,
            "General choice review changed another option, answer or authored field")
    return deepcopy(original)


def _historical_choice_contract(document: dict, identity: str, block: dict) -> dict:
    # Only callers that have validated the complete document/transition use
    # this projection. Keep current prose/guidance for its ordinary validator.
    if identity not in _choice_revisions(document):
        return block
    if (general := _general_choice_revision(document, identity)) is not None:
        return _general_choice_projection(general, block)
    result = historical_choice_payload(block)
    spec = CHOICE_OPTION_REVISIONS.get((document.get("id"), identity))
    if spec is not None and "paired_explanation" in spec:
        _validate_choice_pair(block, spec)
        result["explanation"] = spec["paired_explanation"]["before"]
    return result


def _compiler_case_order_records(document: dict, identity: str | None = None) -> list[dict]:
    records = document.get("stateLayout", {}).get("compilerCaseOrderRevisions", [])
    return records if identity is None else [item for item in records if item["id"] == identity]


def _compiler_case_order_fields(document: dict, identity: str) -> list[dict]:
    return [deepcopy(field) for field in document.get(text_semantics.ROOT_FIELD, {}).get("fields", [])
            if field.get("block") == identity and type(field.get("path")) is str
            and field["path"].startswith("/outputCheck/cases/")]


def _compiler_case_order_after(item: dict) -> dict:
    before, order = item["block"], item["order"]
    require(type(before) is dict and before.get("type") == "cpp_compiler" and "compileCheck" not in before
            and type(before.get("outputCheck")) is dict
            and type(before["outputCheck"].get("cases")) is list
            and len(before["outputCheck"]["cases"]) >= 2,
            "A compiler case order needs a complete multi-case compiler predecessor")
    cases = before["outputCheck"]["cases"]
    require(all(type(case) is dict for case in cases), "A compiler case must be an object")
    require(type(order) is list and len(order) == len(cases)
            and all(type(index) is int for index in order)
            and sorted(order) == list(range(len(cases)))
            and order != list(range(len(cases))),
            "A compiler case order must be a nonidentity integer bijection")
    result = deepcopy(before)
    result["outputCheck"]["cases"] = [deepcopy(cases[index]) for index in order]
    require(fingerprint(result) != fingerprint(before),
            "A compiler case order must change its exact presentation")
    return result


def _compiler_case_order_remap_fields(fields: list[dict], order: list[int]) -> list[dict]:
    inverse = {old: new for new, old in enumerate(order)}
    result = deepcopy(fields)
    for field in result:
        path = field.get("path")
        match = re.fullmatch(r"/outputCheck/cases/(0|[1-9][0-9]*)(/.*)", path or "")
        require(match is not None and int(match[1]) in inverse,
                "A compiler case descriptor needs its canonical existing case index")
        field["path"] = f"/outputCheck/cases/{inverse[int(match[1])]}/{match[2][1:]}"
    return result




def _compiler_case_revision_records(document: dict, identity: str | None = None) -> list[dict]:
    records = document.get("stateLayout", {}).get("compilerCaseRevisions", [])
    return records if identity is None else [item for item in records if item["id"] == identity]


def _compiler_case_events(document: dict, identity: str | None = None) -> list[tuple[str, dict]]:
    """Merge authenticated journals without treating epochs as case indices."""
    result = []
    for kind, field in (("order", "compilerCaseOrderRevisions"), ("cases", "compilerCaseRevisions")):
        records = document.get("stateLayout", {}).get(field, [])
        require(type(records) is list and len(records) <= 100_000,
                "Malformed compiler case history")
        previous = -1
        for item in records:
            require(type(item) is dict and type(item.get("editorialIndex")) is int
                    and item["editorialIndex"] > previous,
                    "Compiler case history must keep chronological integer edges")
            previous = item["editorialIndex"]
            if identity is None or item.get("id") == identity:
                result.append((kind, item))
    result.sort(key=lambda event: event[1]["editorialIndex"])
    require(len({item["editorialIndex"] for _, item in result}) == len(result),
            "Compiler case journals cannot claim the same editorial edge")
    return result


def _validate_compiler_case_payload(block: dict) -> None:
    """Validate the authored output contract before allowing a new case epoch."""
    require(type(block) is dict and block.get("type") == "cpp_compiler" and "compileCheck" not in block,
            "A case revision requires an output compiler with no diagnostic objective")
    config = block.get("outputCheck")
    require(type(config) is dict and set(config) <= {"inputComparison", "cases"}
            and config.get("inputComparison", "exact") in {"exact", "tokens"},
            "A case revision needs a supported output comparison contract")
    cases = config.get("cases")
    require(type(cases) is list and 0 < len(cases) <= 100_000,
            "A case revision needs a nonempty explicit case list")
    seen = []
    def outputs(values):
        require(type(values) is list and values and all(type(value) is str for value in values)
                and len(set(values)) == len(values), "Case outputs must be unique exact strings")
        return set(values)
    for case in cases:
        require(type(case) is dict and {"standardInput", "outputs"} <= set(case) <= {
            "standardInput", "outputs", "explanation", "reviewFeedback", "target",
            "standardError", "standardErrorExact", "exitCode"},
            "A case revision contains an unsupported or incomplete case")
        require(type(case["standardInput"]) is str, "A case input must be exact text")
        target = case.get("target")
        if "target" in case:
            require(type(block.get("cmake")) is dict and type(target) is str
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.+-]{0,127}", target) is not None,
                    "An explicit case target requires a valid CMake project target")
        value = case["standardInput"]
        if config.get("inputComparison", "exact") == "tokens":
            value = tuple(re.findall(r"[^ \t\r\n\v\f]+", value))
        require(not any(prior_input == value and (prior_target is None or target is None or prior_target == target)
                        for prior_target, prior_input in seen),
                "Case targets and inputs must remain unambiguous")
        seen.append((target, value))
        if "exitCode" in case:
            require(type(case["exitCode"]) is int and 0 <= case["exitCode"] <= 255,
                    "Case exitCode must be an integer from 0 through 255")
        if "standardError" in case:
            require(case["standardError"] == "nonBlank" and "standardErrorExact" not in case,
                    "A case stderr condition is unsupported or ambiguous")
        if "standardErrorExact" in case:
            require(type(case["standardErrorExact"]) is str,
                    "A case stderr exact condition must be text")
        claimed = outputs(case["outputs"])
        if "explanation" in case:
            require(type(case["explanation"]) is str and case["explanation"].strip(),
                    "A case explanation must be nonblank text")
        if "reviewFeedback" in case:
            require(type(case["reviewFeedback"]) is list, "Case feedback must be a list")
            for review in case["reviewFeedback"]:
                require(type(review) is dict and set(review) == {"outputs", "text"}
                        and type(review["text"]) is str and review["text"].strip(),
                        "Case feedback must preserve exact outputs and nonblank text")
                values = outputs(review["outputs"])
                require(not claimed.intersection(values), "Case feedback outputs must remain unambiguous")
                claimed.update(values)


def _compiler_client_cmake_config(config) -> None:
    """Mirror native CMake configuration limits without interpreting its files."""
    require(type(config) is dict and {"target"} <= set(config) <= {"target", "options", "buildType"},
            "A client revision needs an exact supported CMake configuration")
    require(type(config["target"]) is str
            and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.+\-]{0,127}", config["target"]) is not None,
            "A client CMake target must be a valid native target name")
    require(config.get("buildType", "Debug") in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"),
            "A client CMake build type is unsupported")
    options = config.get("options", {})
    require(type(options) is dict and len(options) <= 64,
            "A client CMake project supports at most 64 explicit options")
    for name, value in options.items():
        require(type(name) is str and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,127}", name) is not None
                and not name.upper().startswith(("CMAKE_", "LGP_")),
                "A client CMake option name is invalid or reserved")
        require(type(value) is str and len(value.encode("utf-8")) <= 4096
                and not any(ord(char) < 32 or ord(char) == 127 for char in value),
                "A client CMake option must contain at most 4096 UTF-8 bytes without controls")


def _compiler_client_project_index(block: dict, path: str) -> int | None:
    """Select an existing C++ file, or virtual main.cpp in a single-source editor."""
    require(type(block) is dict and block.get("type") == "cpp_compiler" and "compileCheck" not in block,
            "A client revision requires a C++ output editor with no diagnostic objective")
    if "projectFiles" not in block:
        require("cmake" not in block and "sourceFiles" not in block and path == "main.cpp",
                "A single-source client must keep its virtual main.cpp without project metadata")
        require(type(block.get("initialCode")) is str and block["initialCode"]
                and len(block["initialCode"].encode("utf-8")) <= 4 * 1024 * 1024,
                "A single-source client requires nonempty initialCode within the native 4 MiB limit")
        return None
    cmake = "cmake" in block
    if cmake:
        _compiler_client_cmake_config(block["cmake"])
        require("sourceFiles" not in block, "A CMake client cannot override sourceFiles")
        require("initialCode" not in block or type(block["initialCode"]) is str,
                "The independent CMake initialCode must remain text")
    files = block.get("projectFiles")
    require(type(files) is list and 0 < len(files) <= (128 if cmake else 256),
            "A client revision must retain a project within its native file count limit")
    lower = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
    def folded(value):
        return value.translate(lower)
    def valid_path(value):
        if (type(value) is not str or not value or value != value.strip(" \t\r\n\v\f")
                or any(ord(char) < 32 or ord(char) == 127 or char in '\\:<>"|?*' for char in value)):
            return False
        for part in value.split("/"):
            if not part or part in {".", ".."} or part.endswith((".", " ")):
                return False
            stem = folded(part.split(".", 1)[0])
            if (stem in {"con", "prn", "aux", "nul", "conin$", "conout$"}
                    or re.fullmatch(r"(?:com|lpt)[1-9¹²³]", stem)):
                return False
        return True
    def is_source(value):
        return "." in value and folded(value.rsplit(".", 1)[1]) in {"cpp", "cc", "cxx", "c++"}
    names, total = {}, 0
    for index, entry in enumerate(files):
        require(type(entry) is dict and set(entry) == {"path", "initialCode"}
                and valid_path(entry.get("path")) and type(entry.get("initialCode")) is str,
                "A client revision must retain exact valid project files")
        key = folded(entry["path"])
        require(key not in names, "A client project cannot contain ambiguous file paths")
        size = len(entry["initialCode"].encode("utf-8"))
        require(size <= 16 * 1024 * 1024, "A client project file exceeds 16 MiB")
        total += size
        names[key] = (index, entry["path"])
    require(total <= (8 if cmake else 32) * 1024 * 1024,
            "A client project exceeds its native total source byte limit")
    for name in names:
        require(not any(name.startswith(other + "/") for other in names),
                "A client project path cannot also be a directory")
    inferred = [entry["path"] for entry in files if is_source(entry["path"])]
    sources = block.get("sourceFiles", inferred)
    require(type(sources) is list and sources, "A client project must compile at least one source")
    selected = set()
    for source in sources:
        require(valid_path(source) and is_source(source) and folded(source) in names
                and names[folded(source)][1] == source and folded(source) not in selected,
                "A client project needs unique exact existing compiled sources")
        selected.add(folded(source))
    require(type(path) is str and path in sources,
            "The revised client must be an existing selected C++ source")
    index = names[folded(path)][0]
    require(cmake or "initialCode" not in block or (type(block["initialCode"]) is str
            and block["initialCode"] == files[index]["initialCode"]),
            "The client initialCode alias must match the selected source exactly")
    return index


def _compiler_client_source(block: dict, path: str) -> str:
    """Read the authored source slot without changing its storage mode."""
    if "projectFiles" not in block:
        require(path == "main.cpp" and type(block.get("initialCode")) is str,
                "A single-source client must retain its virtual main.cpp")
        return block["initialCode"]
    matches = [entry for entry in block["projectFiles"] if entry.get("path") == path]
    require(len(matches) == 1, "A compiler client must keep its one exact existing file")
    return matches[0]["initialCode"]


def _compiler_client_replace_source(block: dict, path: str, source: str) -> None:
    """Replace the validated source, preserving CMake's independent initialCode."""
    if "projectFiles" not in block:
        require(path == "main.cpp" and "initialCode" in block,
                "A single-source client must retain its virtual main.cpp")
        block["initialCode"] = source
        return
    matches = [entry for entry in block.get("projectFiles", []) if entry.get("path") == path]
    require(len(matches) == 1, "A compiler client must keep its one exact existing file")
    matches[0]["initialCode"] = source
    if "cmake" not in block and "initialCode" in block:
        block["initialCode"] = source


def _compiler_recipe_project_index(block: dict, path: str) -> int:
    """Select one existing recipe while retaining native CMake project limits."""
    require(type(block) is dict and "cmake" in block
            and type(block.get("projectFiles")) is list,
            "A recipe revision requires an existing CMake project")
    # Reuse the unchanged client validator for paths, byte limits, aliases and
    # configuration. Its public API still accepts only a selected C++ source.
    files = block["projectFiles"]
    cpp_path = next((entry.get("path") for entry in files if type(entry) is dict
                    and type(entry.get("path")) is str
                    and entry["path"].rsplit(".", 1)[-1].lower() in {"cpp", "cc", "cxx", "c++"}), "")
    _compiler_client_project_index(block, cpp_path)
    require(any(entry["path"] == "CMakeLists.txt" for entry in files),
            "A recipe revision must retain the root CMakeLists.txt")
    require(type(path) is str and (path.rsplit("/", 1)[-1] == "CMakeLists.txt"
                                  or path.endswith(".cmake")),
            "A recipe revision must select an existing CMakeLists.txt or .cmake file")
    matches = [index for index, entry in enumerate(files) if entry["path"] == path]
    require(len(matches) == 1, "A recipe revision must retain its exact existing recipe path")
    return matches[0]


def _compiler_case_source_revision(item: dict) -> dict | None:
    """Return either authenticated source mode without conflating their APIs."""
    return item.get("client", item.get("recipe"))


def _compiler_case_has_client(document: dict, identity: str) -> bool:
    """Whether source epochs exist; retained helper name also covers recipes."""
    return any("client" in item or "recipe" in item
               for item in _compiler_case_revision_records(document, identity))


def _compiler_case_revision_after(item: dict) -> dict:
    before = item["block"]
    require(type(before) is dict, "A compiler revision requires its complete predecessor block")
    require(not ("client" in item and "recipe" in item),
            "A compiler revision cannot replace a client and a recipe together")
    recipe = "recipe" in item
    if not recipe or "outputCheck" in before:
        _validate_compiler_case_payload(before)
    if recipe:
        require(fingerprint(item["cases"]) == fingerprint(before.get("outputCheck", {}).get("cases"))
                and fingerprint(item["semanticBefore"]) == fingerprint(item["semanticAfter"]),
                "A recipe revision must preserve the exact cases and their semantics")
        if "outputCheck" not in before:
            require(item["cases"] is None and item["semanticBefore"] == [],
                    "An unevaluated recipe must preserve absent cases and empty case semantics")
        else:
            candidate = deepcopy(before)
            candidate["outputCheck"]["cases"] = deepcopy(item["cases"])
            _validate_compiler_case_payload(candidate)
    result = deepcopy(before)
    if "client" in item or recipe:
        source = _compiler_case_source_revision(item)
        require(type(source) is dict and set(source) == {"path", "source"},
                "A source revision needs only its existing path and explicit source")
        select = _compiler_recipe_project_index if recipe else _compiler_client_project_index
        select(before, source["path"])
        require(type(source["source"]) is str and source["source"] and "\0" not in source["source"]
                and source["source"] != _compiler_client_source(before, source["path"]),
                "A source revision must explicitly change its nonempty source")
        _compiler_client_replace_source(result, source["path"], source["source"])
        select(result, source["path"])
    if not recipe:
        result["outputCheck"]["cases"] = deepcopy(item["cases"])
    if "outputCheck" in result:
        _validate_compiler_case_payload(result)
    require(fingerprint(result) != fingerprint(before), "A case revision must change its declared cases")
    return result


def _compiler_case_event_after(kind: str, item: dict) -> dict:
    return _compiler_case_order_after(item) if kind == "order" else _compiler_case_revision_after(item)


def _compiler_case_epoch(document: dict, identity: str, block: dict,
                         events: list[tuple[str, dict]], *, editorial_prefix: int | None = None) -> int:
    if editorial_prefix is not None:
        return sum(item["editorialIndex"] < editorial_prefix for _, item in events)
    signature = fingerprint(block)
    epochs = []
    for index, proof in enumerate(document["stateLayout"]["editorial"]):
        if proof["id"] != identity:
            continue
        if proof["beforeSha256"] == signature:
            epochs.append(sum(item["editorialIndex"] < index for _, item in events))
        if proof["afterSha256"] == signature:
            epochs.append(sum(item["editorialIndex"] <= index for _, item in events))
    require(epochs, "A compiler case source needs an exact node in its owner's editorial chain")
    return max(epochs)


def _compiler_case_order_projection(document: dict, identity: str, block: dict, *,
                                    editorial_prefix: int | None = None) -> dict:
    """Undo only the case events preceding this exact published source node."""
    events = _compiler_case_events(document, identity)
    if not events:
        return block
    epoch = _compiler_case_epoch(document, identity, block, events, editorial_prefix=editorial_prefix)
    expected = (events[0][1]["block"] if epoch == 0
                else _compiler_case_event_after(*events[epoch - 1]))
    if without_compiler_guidance(block).get("outputCheck") != without_compiler_guidance(expected).get("outputCheck"):
        original = _compiler_revisions(document).get(identity)
        if (epoch == 0 and original is not None
                and without_compiler_guidance(block).get("outputCheck")
                == without_compiler_guidance(original).get("outputCheck")
                and state_fingerprint(block) == state_fingerprint(original)):
            return block
        raise LayoutError("Compiler case ordering changed a case, channel, comparison mode or program")
    if any("client" in item or "recipe" in item for _, item in events):
        try:
            if any(entry["id"] == identity for entry in document["stateLayout"].get("sourceFormatRevisions", [])):
                code_format.require_source_lexical(expected, block)
            else:
                require(code_format.code_slots(expected) == code_format.code_slots(block),
                        "A compiler source changed outside its authenticated source epoch")
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe compiler source for {identity}: {exc}") from exc
    result = deepcopy(block)
    for kind, item in reversed(events[:epoch]):
        if kind == "cases":
            if "recipe" not in item:
                result["outputCheck"]["cases"] = deepcopy(item["block"]["outputCheck"]["cases"])
            if "client" in item or "recipe" in item:
                path = _compiler_case_source_revision(item)["path"]
                source = _compiler_client_source(item["block"], path)
                _compiler_client_replace_source(result, path, source)
        else:
            cases = result.get("outputCheck", {}).get("cases")
            require(type(cases) is list and len(cases) == len(item["order"]),
                    "A compiler case order cannot add or remove cases")
            original = [None] * len(cases)
            for new, old in enumerate(item["order"]):
                original[old] = cases[new]
            result["outputCheck"]["cases"] = original
    return result


def _validate_compiler_case_semantics(fields: list[dict], identity: str, block: dict) -> None:
    require(type(fields) is list, "Compiler case semantics must preserve an explicit field list")
    sources = text_semantics.public_text_fields(block)
    seen = set()
    for field in fields:
        require(type(field) is dict and set(field) == {"block", "path", "sourceSha256", "spans"}
                and field["block"] == identity and type(field["path"]) is str
                and field["path"].startswith("/outputCheck/cases/")
                and field["path"] in sources and field["path"] not in seen,
                "Archived compiler case semantics must belong to exact public fields of their owner")
        try:
            text_semantics._descriptor(field, sources[field["path"]])
        except (ValueError, KeyError, TypeError) as exc:
            raise LayoutError("Invalid archived compiler case semantics") from exc
        seen.add(field["path"])


def _validate_compiler_case_order_revisions(document: dict, storage: dict, digests: dict) -> dict[str, list[dict]]:
    """Validate the shared timeline before any epoch is projected to its origin."""
    meta = document["stateLayout"]
    events = _compiler_case_events(document)
    require(not events or meta["locale"] == "es", "Compiler case revisions require Spanish v2")
    active, groups = _blocks(document), {}
    editorial = meta["editorial"]
    require(type(editorial) is list, "Malformed editorial proof")
    for kind, item in events:
        fields = {"id", "block", "editorialIndex", "historySha256", "semanticBefore"}
        fields |= {"order"} if kind == "order" else {"cases", "semanticAfter"}
        allowed = fields | {"client", "recipe"} if kind == "cases" else fields
        require(fields <= set(item) <= allowed,
                "A compiler case revision needs its exact owner, predecessor, edge, cases and semantics")
        identity = _identity(item["id"])
        require(identity in storage and identity in active,
                "A compiler case revision must retain its existing active owner")
        if "recipe" in item and "outputCheck" not in item["block"]:
            root_objectives = document.get("compilerOpenBatteryChecks")
            require(type(root_objectives) is list and any(type(record) is dict
                        and record.get("activity") == identity for record in root_objectives),
                    "An unevaluated recipe revision requires its authenticated root open-battery objective")
        index = item["editorialIndex"]
        require(0 <= index < len(editorial), "Compiler case history names an unknown editorial edge")
        after = _compiler_case_event_after(kind, item)
        proof = editorial[index]
        require(type(proof) is dict and proof.get("id") == identity
                and proof.get("beforeSha256") == fingerprint(item["block"])
                and proof.get("afterSha256") == fingerprint(after),
                "A compiler case revision needs its exact own predecessor-to-successor edge")
        require(_digest(item["historySha256"]) == fingerprint({
            "lessonId": document["id"], "locale": meta["locale"], "editorial": editorial[:index]}),
            "Compiler case history differs from its complete published editorial prefix")
        _validate_compiler_case_semantics(item["semanticBefore"], identity, item["block"])
        if kind == "order":
            _compiler_case_order_remap_fields(item["semanticBefore"], item["order"])
        else:
            _validate_compiler_case_semantics(item["semanticAfter"], identity, after)
        groups.setdefault(identity, []).append((kind, item))
    for identity, own in groups.items():
        client_history = any("client" in item or "recipe" in item for _, item in own)
        baseline = _compiler_case_order_contract(document, identity, own[0][1]["block"],
            editorial_prefix=own[0][1]["editorialIndex"] if client_history else None)
        require(state_fingerprint(baseline) == digests[identity],
                "The compiler case predecessor changed its owner's immutable contract")
        sources = [(item["block"], item["editorialIndex"]) for _, item in own]
        sources += [(_compiler_case_event_after(kind, item), item["editorialIndex"] + 1) for kind, item in own]
        sources.append((active[identity], len(editorial)))
        for block, prefix in sources:
            # Repeated bytes in an explicit return are different timeline nodes.
            # A predecessor cannot borrow a future return to hide a missing client event.
            prefix = prefix if client_history else None
            epoch = _compiler_case_epoch(document, identity, block, own, editorial_prefix=prefix)
            expected = (own[0][1]["block"] if epoch == 0
                        else _compiler_case_event_after(*own[epoch - 1]))
            require(without_compiler_guidance(block).get("outputCheck")
                    == without_compiler_guidance(expected).get("outputCheck"),
                    "Compiler case ordering changed a case, channel, comparison mode or program")
            projected = _compiler_case_order_contract(document, identity, block, editorial_prefix=prefix)
            require(state_fingerprint(projected) == state_fingerprint(baseline),
                    "Compiler case ordering changed a case, channel, comparison mode or program")
            if "outputCheck" in block and any(kind == "cases" for kind, _ in own):
                _validate_compiler_case_payload(block)
    return {identity: [item for _, item in own] for identity, own in groups.items()}


def _compiler_case_order_realign(previous: dict, current: dict, identity: str, block: dict) -> dict:
    """Restore the prior publication's epoch for the existing curricular guards."""
    result = _compiler_case_order_projection(current, identity, block)
    for kind, item in _compiler_case_events(previous, identity):
        if kind == "cases":
            if "recipe" not in item:
                result["outputCheck"]["cases"] = deepcopy(item["cases"])
            if "client" in item or "recipe" in item:
                source = _compiler_case_source_revision(item)
                _compiler_client_replace_source(result, source["path"], source["source"])
        else:
            cases = result["outputCheck"]["cases"]
            require(len(cases) == len(item["order"]), "Compiler case counts changed during alignment")
            result["outputCheck"]["cases"] = [cases[index] for index in item["order"]]
    before = _all_blocks(previous).get(identity)
    if before is not None and any(entry["id"] == identity for entry in
                                  current["stateLayout"].get("sourceFormatRevisions", [])):
        result = code_format.source_projection(result, before)
    return result


def _compiler_case_replace_fields(overlay, identity: str, fields: list[dict]):
    if overlay is None:
        return {"version": 1, "fields": deepcopy(fields)} if fields else None
    result = deepcopy(overlay)
    previous = result["fields"]
    positions = [index for index, field in enumerate(previous) if field.get("block") == identity
                 and field.get("path", "").startswith("/outputCheck/cases/")]
    insertion = positions[0] if positions else len(previous)
    result["fields"] = (previous[:insertion] + deepcopy(fields)
                        + [field for index, field in enumerate(previous[insertion:], insertion)
                           if index not in positions])
    return result


def _validate_compiler_case_order_transition(previous: dict, current: dict) -> set[str]:
    prior_meta, meta = previous["stateLayout"], current["stateLayout"]
    for field in ("compilerCaseOrderRevisions", "compilerCaseRevisions"):
        old, records = prior_meta.get(field, []), meta.get(field, [])
        require(records[:len(old)] == old,
                "Published compiler case histories are append-only and cannot be removed or rewritten")
    additions = []
    for kind, field in (("order", "compilerCaseOrderRevisions"), ("cases", "compilerCaseRevisions")):
        additions.extend((kind, item) for item in meta.get(field, [])[len(prior_meta.get(field, [])):])
    additions.sort(key=lambda event: event[1]["editorialIndex"])
    if not additions:
        return set()
    require(prior_meta.get("version") == 2,
            "A new compiler case revision requires an existing published v2 owner")
    prior_edge_count = len(prior_meta["editorial"])
    prior_blocks, first = _blocks(previous), set()
    expected_overlay = deepcopy(previous.get(text_semantics.ROOT_FIELD))
    for kind, item in additions:
        identity = item["id"]
        require(identity in prior_blocks and item["editorialIndex"] >= prior_edge_count,
                "A new compiler case revision needs a genuinely published active predecessor")
        if identity not in first:
            require(item["block"] == prior_blocks[identity],
                    "A new compiler case revision must archive its exact complete published predecessor")
            first.add(identity)
        fields = ([] if expected_overlay is None else [field for field in expected_overlay.get("fields", [])
                  if field.get("block") == identity and field.get("path", "").startswith("/outputCheck/cases/")])
        require(item["semanticBefore"] == fields,
                "Compiler case semantic history differs from its exact published predecessor")
        if kind == "order":
            moved = iter(_compiler_case_order_remap_fields(fields, item["order"]))
            if expected_overlay is not None:
                expected_overlay["fields"] = [next(moved) if field.get("block") == identity and
                    field.get("path", "").startswith("/outputCheck/cases/") else field
                    for field in expected_overlay.get("fields", [])]
        elif "recipe" not in item:
            expected_overlay = _compiler_case_replace_fields(expected_overlay, identity, item["semanticAfter"])
    retired_before = prior_meta.get("retired", [])
    retiring = {item["id"] for item in meta.get("retired", [])[len(retired_before):]}
    if expected_overlay is not None and retiring:
        expected_overlay["fields"] = [field for field in expected_overlay.get("fields", [])
                                      if field.get("block") not in retiring]
    require(expected_overlay == current.get(text_semantics.ROOT_FIELD),
            "Compiler case revision lost, invented or changed an authored semantic field")
    return first


_UNSET_COMPILER_CASES = object()


def revise_compiler_cases(document: dict, identity: str, cases=_UNSET_COMPILER_CASES, *,
                          case_semantics=_UNSET_COMPILER_CASES, return_to: int | None = None) -> dict:
    """Revise cases explicitly; the original two-argument reviewed API is unchanged."""
    if cases is _UNSET_COMPILER_CASES:
        require(case_semantics is _UNSET_COMPILER_CASES and return_to is None,
                "The closed compiler API does not accept general revision options")
        return _revise_closed_compiler_cases(document, identity)
    return _revise_compiler_case_record(document, identity, cases,
                                      case_semantics=case_semantics, return_to=return_to)


def revise_compiler_client_cases(document: dict, identity: str, client: dict, cases, *,
                                 case_semantics=_UNSET_COMPILER_CASES,
                                 return_to: int | None = None) -> dict:
    """Authenticate one existing C++ client's new source and explicit cases.

    This is a new objective, even when the case list or earlier client bytes
    reappear. It neither edits student drafts nor authorizes old receipts.
    """
    return _revise_compiler_case_record(document, identity, cases, client=client,
                                      case_semantics=case_semantics, return_to=return_to)


def revise_compiler_cmake_recipe(document: dict, identity: str, recipe: dict, *,
                                 return_to: int | None = None) -> dict:
    """Authenticate one CMake file without changing cases, marks or other files.

    Absent outputCheck stays absent. Root-generated objectives are activated by
    their existing loader; this revision neither creates one nor edits a draft.
    """
    validate(document)
    original = _blocks(document).get(identity)
    require(type(original) is dict, "A recipe revision requires an active existing owner")
    return _revise_compiler_case_record(document, identity,
        original.get("outputCheck", {}).get("cases"), recipe=recipe,
        case_semantics=_compiler_case_order_fields(document, identity), return_to=return_to)


def _revise_compiler_case_record(document: dict, identity: str, cases, *,
                                 case_semantics, return_to: int | None,
                                 client=_UNSET_COMPILER_CASES, recipe=_UNSET_COMPILER_CASES) -> dict:
    require(case_semantics is not _UNSET_COMPILER_CASES,
            "A general compiler case revision requires explicit case_semantics, including []")
    validate(document)
    meta = document.get("stateLayout", {})
    require(meta.get("version") == 2 and meta.get("locale") == "es",
            "Compiler case revisions require an existing published Spanish v2 owner")
    _identity(identity)
    original = _blocks(document).get(identity)
    require(type(original) is dict, "Compiler case revisions require an active existing owner")
    require(return_to is None or (type(return_to) is int and return_to >= 0),
            "A compiler case return needs an explicit nonnegative revision")
    record = {"id": identity, "block": deepcopy(original), "cases": deepcopy(cases),
              "editorialIndex": len(meta["editorial"]),
              "historySha256": fingerprint({"lessonId": document["id"], "locale": meta["locale"],
                                             "editorial": meta["editorial"]}),
              "semanticBefore": _compiler_case_order_fields(document, identity),
              "semanticAfter": deepcopy(case_semantics)}
    if client is not _UNSET_COMPILER_CASES:
        record["client"] = deepcopy(client)
    if recipe is not _UNSET_COMPILER_CASES:
        record["recipe"] = deepcopy(recipe)
    changed = _compiler_case_revision_after(record)
    _validate_compiler_case_semantics(record["semanticAfter"], identity, changed)
    result = deepcopy(document)
    target = _blocks(result)[identity]
    target.clear()
    target.update(changed)
    if recipe is _UNSET_COMPILER_CASES:
        overlay = _compiler_case_replace_fields(result.get(text_semantics.ROOT_FIELD), identity, case_semantics)
        if overlay is None:
            result.pop(text_semantics.ROOT_FIELD, None)
        else:
            result[text_semantics.ROOT_FIELD] = overlay
    proof = {"id": identity, "beforeSha256": fingerprint(original), "afterSha256": fingerprint(changed)}
    prior = [edge for edge in meta["editorial"] if edge["id"] == identity]
    nodes = ([prior[0]["beforeSha256"]] + [edge["afterSha256"] for edge in prior]) if prior else []
    if proof["afterSha256"] in nodes:
        require(return_to is not None and return_to < len(nodes) - 1
                and nodes[return_to] == proof["afterSha256"],
                "Returning compiler cases requires their explicit authentic earlier revision")
        proof["returnTo"] = {"revision": return_to, "historySha256": record["historySha256"],
                             "previousBlock": deepcopy(original)}
    else:
        require(return_to is None, "New compiler cases cannot claim an editorial return")
    result["stateLayout"].setdefault("compilerCaseRevisions", []).append(record)
    result["stateLayout"]["editorial"].append(proof)
    validate_transition(document, result)
    return result




def _compiler_case_format_origin(document: dict, identity: str, archive: dict) -> dict:
    """Keep the raw format archive intact, projecting only its contract lookup."""
    return (_compiler_case_order_projection(document, identity, archive)
            if _compiler_case_has_client(document, identity) else archive)


def _compiler_case_format_changed(document: dict, identity: str, archive: dict) -> bool:
    """Find a genuine format inside the archive's authenticated source epoch.

    A later client or recipe replacement may remove that format from the active
    source. Its complete predecessor still records the end of the earlier
    epoch. Cases are normalized independently; source bytes remain visible.
    """
    events = _compiler_case_events(document, identity)
    case_edges = {item["editorialIndex"] for _, item in events}
    signature = fingerprint(archive)
    for index, proof in enumerate(document["stateLayout"]["editorial"]):
        if proof["id"] != identity or proof["beforeSha256"] != signature or index in case_edges:
            continue
        following = next((item for _, item in events if ("client" in item or "recipe" in item)
                          and item["editorialIndex"] > index), None)
        target = following["block"] if following is not None else _all_blocks(document)[identity]
        prefix = following["editorialIndex"] if following is not None else len(document["stateLayout"]["editorial"])
        before = _compiler_case_order_projection(document, identity, archive, editorial_prefix=index)
        after = _compiler_case_order_projection(document, identity, target, editorial_prefix=prefix)
        before = code_format.source_projection(before, archive)
        after = code_format.source_projection(after, target)
        try:
            if require_source_format_with_editorial(before, after):
                return True
        except (code_format.SourceFormatError, LayoutError):
            continue
    return False


def _compiler_case_order_contract(document: dict, identity: str, block: dict, *,
                                  editorial_prefix: int | None = None) -> dict:
    result = _compiler_case_order_projection(document, identity, block, editorial_prefix=editorial_prefix)
    if _compiler_case_events(document, identity) and "outputCheck" in result:
        require(all(type(case) is dict and ("exitCode" not in case or type(case["exitCode"]) is int)
                    for case in result["outputCheck"]["cases"]),
                "Compiler case exit codes must preserve their integer type")
    archive = next((entry["block"] for entry in document.get("stateLayout", {}).get(
        "sourceFormatRevisions", []) if entry["id"] == identity), None)
    if archive is not None:
        original = _compiler_case_format_origin(document, identity, archive)
        try:
            code_format.require_source_lexical(original, result if _compiler_case_has_client(document, identity) else block)
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe compiler case source for {identity}: {exc}") from exc
        result = code_format.source_projection(result, original)
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


def _authenticated_identifier_renames(
        document: dict, active: dict[str, dict],
        storage: dict[str, tuple[int, int]],
        original_digests: dict[str, str]) -> dict[str, dict]:
    """Authenticate source-only C++ renames before restoring published format.

    A rename packet is bound to the exact v2 owner, its immutable storage
    address and original response digest, plus one authenticated editorial
    segment. This deliberately permits only ONE revision of each active owner
    and no later changes to that owner's code in this initial contract.
    """
    meta = document["stateLayout"]
    records = meta.get("identifierRenameRevisions", [])
    require(type(records) is list and len(records) <= 64,
            "Invalid C++ identifier rename history")
    if not records:
        return {}
    require(meta["locale"] == "es" and meta["version"] == 2,
            "C++ rename history requires an authenticated Spanish v2 source")
    fields = {
        "lessonId", "locale", "historicalFamilySha256", "originalStateSha256",
        "stableStorage", "editorialPrefixSha256", "id", "block", "names",
        "occurrences", "beforeSha256", "afterSha256",
        "priorSourceFormatSha256", "confirmed",
    }
    formats = {
        entry["id"]: entry["block"] for entry in
        meta.get("sourceFormatRevisions", [])
    }
    editorial = meta["editorial"]
    require(type(editorial) is list and len(editorial) >= len(records),
            "C++ rename history needs its complete editorial publication")
    predecessor = []
    original_blocks = {}
    for record in records:
        require(type(record) is dict and set(record) == fields,
                "Invalid or incomplete C++ rename provenance")
        identity = _identity(record["id"])
        require(identity in active and identity in storage and
                identity not in original_blocks and identity in formats,
                "C++ rename archive needs a unique active formatted owner")
        old, now = record["block"], active[identity]
        require(type(old) is dict and type(now) is dict,
                "C++ rename history needs full historical and current blocks")
        address = storage[identity]
        require(record["lessonId"] == document["id"]
                and record["locale"] == meta["locale"]
                and record["historicalFamilySha256"] ==
                    meta["family"][meta["locale"]]["sha256"]
                and record["originalStateSha256"] == original_digests[identity]
                and identifier_rename.same_json_value_and_types(
                    record["stableStorage"], {"page": address[0], "block": address[1]}),
                "C++ rename belongs to another lesson, family or storage owner")
        before, after = fingerprint(old), fingerprint(now)
        require(record["beforeSha256"] == before and
                record["afterSha256"] == after and before != after and
                record["priorSourceFormatSha256"] == fingerprint(formats[identity]),
                "C++ rename changed the exact published source or its format archive")
        try:
            confirmed = identifier_rename.require_declared_cpp_renames(
                old, now, record["names"], record["occurrences"])
            code_format.require_source_lexical(formats[identity], old)
        except (identifier_rename.IdentifierRenameError,
                code_format.SourceFormatError) as exc:
            raise LayoutError(f"Unsafe C++ rename for {identity}: {exc}") from exc
        require(identifier_rename.same_json_value_and_types(
                    confirmed, record["confirmed"]),
                "C++ rename falsified its declared token counts")
        predecessor.append({"id": identity, "beforeSha256": before,
                            "afterSha256": after})
        original_blocks[identity] = old

    # An authenticated publication inserts all rename edges consecutively,
    # immediately after the editorial prefix whose digest it carries. A later
    # publication may append other owners' prose proofs without erasing them.
    starts = [
        index for index in range(len(editorial) - len(records) + 1)
        if identifier_rename.same_json_value_and_types(
            editorial[index:index + len(records)], predecessor)
    ]
    require(len(starts) == 1,
            "C++ rename archives need their exact consecutive editorial edges")
    prefix = editorial[:starts[0]]
    digest = fingerprint({
        "lessonId": document["id"], "locale": meta["locale"],
        "editorial": prefix,
    })
    require(all(record["editorialPrefixSha256"] == digest
                for record in records),
            "C++ rename archives cannot borrow another editorial history")
    return original_blocks


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
    case_orders = _validate_compiler_case_order_revisions(document, storage, digests)
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
    # A source-format archive survives its owner's retirement. Its exact final
    # presentation lives in retired; the original source and editorial chain
    # keep their existing immutable identity. Validate these references before
    # using them, without accepting an unknown or duplicated historical owner.
    format_sources = dict(active_blocks)
    rename_originals = _authenticated_identifier_renames(
        document, active_blocks, storage, digests)
    format_sources.update(rename_originals)
    if revisions:
        format_retired = meta.get("retired", [])
        require(type(format_retired) is list and len(format_retired) <= 100_000,
                "Malformed retired activity archive")
        for item in format_retired:
            require(type(item) is dict and set(item) == {"id", "page", "next", "block"},
                    "A retired activity needs its identity, page, successor and complete block")
            identity = _identity(item["id"])
            require(identity in storage and identity not in format_sources,
                    "A retired identity is unknown, duplicated or still active")
            require(type(item["block"]) is dict and item["block"].get("type") in RETIRABLE_TYPES,
                    "Only compiler activities and their presentation blocks can be retired")
            format_sources[identity] = item["block"]
    math_texts = {}
    math_revisions = meta.get("mathTextRevisions", [])
    require(type(math_revisions) is list and len(math_revisions) <= 100_000,
            "Malformed mathematical text archive")
    require("mathTextRevisions" not in meta or meta["locale"] == "es",
            "Math prompt revisions require their Spanish historical family")
    for item in math_revisions:
        require(type(item) is dict and set(item) == {"id", "field", "rawJson", "semanticBefore"}
                and item["field"] == "/prompt",
                "A math text revision needs its owner, /prompt, original JSON and semantic descriptor")
        identity = _identity(item["id"])
        require(identity in storage and identity in active_blocks and identity not in math_texts,
                "Unknown, inactive or duplicate math prompt owner")
        original = _math_prompt_original(item)
        require(state_fingerprint(original) == digests[identity],
                "The math prompt archive differs from its owner's immutable original contract")
        _math_prompt_projection(item, active_blocks[identity])
        semantic_before = item["semanticBefore"]
        if semantic_before is not None:
            require(type(semantic_before) is dict and set(semantic_before) == {
                "block", "path", "sourceSha256", "spans"}
                and semantic_before["block"] == identity and semantic_before["path"] == "/prompt",
                "The archived math descriptor must belong to this original prompt")
            try:
                text_semantics._descriptor(semantic_before, original["prompt"])
            except (KeyError, TypeError, ValueError) as exc:
                raise LayoutError("Archived math prompt semantics are invalid") from exc
        semantic_after = _math_prompt_semantic_entry(document, identity)
        require(semantic_after is not None,
                "A revised math prompt needs its explicit current semantic descriptor")
        require(semantic_before is None or not semantic_before["spans"]
                or bool(semantic_after.get("spans")),
                "A revised marked prompt cannot silently lose all its authored highlights")
        math_texts[identity] = item
    # Imported on demand: the matching authoring helper itself uses this
    # validator to authenticate the unchanged published lesson first.
    import math_matching_text_compat as matching_compat
    try:
        matching_originals = matching_compat.authenticated_matching_text_originals(
            document, active_blocks, digests)
    except matching_compat.MatchingTextError as exc:
        raise LayoutError(f"Unsafe mathematical matching text archive: {exc}") from exc
    for entry in revisions:
        require(type(entry) is dict and set(entry) == {"id", "block"},
                "A source format revision needs its identity and complete original block")
        identity = _identity(entry["id"])
        require(identity in storage and identity in format_sources and identity not in formatted,
                "Unknown, absent or duplicate source format owner")
        original = entry["block"]
        require(type(original) is dict, "Source format history must preserve a full original block")
        ordered_original = _compiler_case_order_projection(document, identity, original)
        source_before = (order_contract_projection(ordered_original, refined[identity])
                         if identity in refined else ordered_original)
        require(state_fingerprint(source_before) == digests[identity],
                "Source formatting archive changed the historical answer contract")
        # A formatted block cannot silently replace the question without
        # declaring exactly which phrase the reader should display and narrate.
        if original.get("question") != format_sources[identity].get("question"):
            semantic_source = document
            if identity not in active_blocks:
                history = meta.get("retirementHistory", [])
                require(type(history) is list,
                        "Malformed retirement reading history")
                sources = [snapshot for snapshot in history if type(snapshot) is dict
                           and type(snapshot.get("retired")) is list
                           and identity in snapshot["retired"]]
                require(len(sources) == 1,
                        "A retired formatted question needs its exact reading snapshot")
                semantic_source = sources[0]
            overlay = semantic_source.get(text_semantics.ROOT_FIELD) or {}
            require(type(overlay) is dict and type(overlay.get("fields", [])) is list,
                    "Formatted question has malformed authored source descriptors")
            question_fields = [entry for entry in overlay.get("fields", [])
                               if type(entry) is dict and entry.get("block") == identity
                               and entry.get("path") == "/question"]
            require(len(question_fields) == 1,
                    "Formatted question has no explicit authored source descriptor")
        try:
            if _compiler_case_has_client(document, identity):
                code_format.require_source_lexical(ordered_original,
                    _compiler_case_order_projection(document, identity, format_sources[identity]))
            else:
                code_format.require_source_lexical(original, format_sources[identity])
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe source formatting for {identity}: {exc}") from exc
        formatted[identity] = original
    choice_revisions = meta.get("choiceRevisions", [])
    require(type(choice_revisions) is list and len(choice_revisions) <= len(CHOICE_OPTION_REVISIONS) + 64,
            "Malformed reviewed choice archive")
    require(not choice_revisions or meta["locale"] == "es", "Choice revisions are authorized only for Spanish")
    choices = {}
    general_choices = {}
    for item in choice_revisions:
        require(type(item) is dict and {"id", "block"} <= set(item),
                "A choice revision needs its identity and complete original block")
        identity = _identity(item["id"])
        require(identity in storage and identity not in choices and identity not in general_choices,
                "Unknown or repeated choice revision identity")
        spec = CHOICE_OPTION_REVISIONS.get((document.get("id"), identity))
        original = item["block"]
        if set(item) == {"id", "block"}:
            require(spec is not None, "This closed choice revision belongs to its reviewed Spanish owner")
            require(type(original) is dict and original.get("type") == "multiple_choice"
                    and fingerprint(original) == spec["full_before"]
                    and fingerprint(_choice_payload(original)) == spec["protected_before"],
                    "The choice archive must preserve its exact complete published block")
            require(state_fingerprint(original) == digests[identity],
                    "The choice snapshot does not match its immutable response contract")
            choices[identity] = spec
        else:
            legacy_fields = {"id", "block", "optionIndex", "replacement", "semanticBefore"}
            explicit_fields = {"semanticAfter", "semanticRootBefore"}
            require(set(item) in (legacy_fields, legacy_fields | explicit_fields)
                    and spec is None,
                    "A general choice review needs its original, one option and authenticated semantic archive")
            require(type(original) is dict and original.get("type") == "multiple_choice"
                    and type(original.get("options")) is list
                    and all(type(option) is str for option in original["options"])
                    and type(original.get("correctIndex")) is int,
                    "General choice review needs a complete original choice block")
            index = item["optionIndex"]
            require(type(index) is int and 0 <= index < len(original["options"])
                    and type(item["replacement"]) is str and bool(item["replacement"].strip())
                    and original["options"][index] != item["replacement"],
                    "General choice review needs exactly one real nonblank text revision")
            require(state_fingerprint(original) == digests[identity],
                    "General choice archive must match this owner's original response contract")
            active = active_blocks.get(identity)
            require(type(active) is dict,
                    "General choice review must keep its original activity active")
            _general_choice_projection(item, active)
            semantic_before = item["semanticBefore"]
            selected_before = original["options"][index]
            if semantic_before is not None:
                require(type(semantic_before) is dict and
                        set(semantic_before) == {"block", "path", "sourceSha256", "spans"} and
                        semantic_before["block"] == identity and
                        semantic_before["path"] == f"/options/{index}",
                        "The choice semantic predecessor needs its exact original identity")
                try:
                    text_semantics._descriptor(semantic_before, selected_before)
                except (KeyError, TypeError, ValueError) as exc:
                    raise LayoutError("Archived choice semantic spans are invalid") from exc
            semantic_after = _choice_semantic_entry(document, identity, index)
            if "semanticAfter" in item:
                expected_semantic = item["semanticAfter"]
                require(type(item["semanticRootBefore"]) is bool
                        and (item["semanticRootBefore"] or semantic_before is None),
                        "An explicit choice revision has an invalid original semantic root")
                require(type(expected_semantic) is dict
                        and set(expected_semantic) == {"block", "path", "sourceSha256", "spans"}
                        and expected_semantic["block"] == identity
                        and expected_semantic["path"] == f"/options/{index}",
                        "An explicit choice descriptor must name its original owner and option")
                try:
                    text_semantics._descriptor(expected_semantic, item["replacement"])
                except (KeyError, TypeError, ValueError) as exc:
                    raise LayoutError("A revised choice has invalid authored UTF-16 spans") from exc
                require(semantic_before is None or not semantic_before["spans"]
                        or bool(expected_semantic["spans"]),
                        "A marked choice cannot silently lose all its authored highlights")
            else:
                expected_semantic = deepcopy(semantic_before)
                if expected_semantic is not None:
                    expected_semantic["sourceSha256"] = hashlib.sha256(
                        item["replacement"].encode("utf-8")).hexdigest()
            require(semantic_after == expected_semantic,
                    "A revised choice changed, inserted or lost authenticated semantic spans")
            general_choices[identity] = item
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
            math_source = (_math_prompt_projection(math_texts[identity], pages[p]["blocks"][b])
                           if identity in math_texts else
                           matching_originals.get(identity, pages[p]["blocks"][b]))
            reviewed_math = reviewed_math_label(math_source)
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
            ordered_source = _compiler_case_order_projection(
                document, identity, matching_originals.get(
                    identity, rename_originals.get(identity, block)))
            source = (code_format.source_projection(ordered_source,
                          _compiler_case_format_origin(document, identity, formatted[identity]))
                      if identity in formatted else ordered_source)
            compiler_reviewed = _reviewed_compiler(source)
            if compiler_reviewed is not None or identity in compilers:
                require(meta["locale"] == "es" and identity in compilers
                        and COMPILER_CASE_REVISIONS.get((document.get("id"), identity)) == compiler_reviewed,
                        "A revised compiler requires its exact lesson, identity and original archive")
            # A reviewed reference has its own C++ extension and may later
            # acquire an authenticated, token-equivalent physical reformat.
            # Check the restored published source, just as compiler owners do.
            reference_reviewed = _reviewed_compiler_reference(source)
            if reference_reviewed is not None or identity in compiler_references:
                require(meta["locale"] == "es" and identity in compiler_references
                        and compiler_references[identity] == reference_reviewed,
                        "A revised compiler reference requires its matching expanded compiler and archive")
            projected = order_contract_projection(source, refined[identity]) if identity in refined else source
            if identity in general_choices:
                projected = _general_choice_projection(general_choices[identity], projected)
            if identity in math_texts:
                projected = _math_prompt_projection(math_texts[identity], projected)
            require(state_fingerprint(projected) == digests[identity],
                    "A response/code contract changed or belongs to another identity")
            seen_blocks.append(identity)
            mapping[p, b] = storage[identity]
    require(len(set(seen_pages)) == len(seen_pages) and set(seen_pages) == set(page_ids), "Page identities changed")
    require(refined.keys() <= set(seen_blocks), "Order refinements must remain active under their existing identities")
    require(choices.keys() <= set(seen_blocks), "Choice revisions must remain active under their existing identities")
    require(rename_originals.keys() <= set(seen_blocks),
            "C++ rename archives cannot belong to retired activities")
    require(matching_originals.keys() <= set(seen_blocks),
            "Matching text archives cannot belong to retired activities")
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
        source = (code_format.source_projection(item["block"], formatted[identity])
                  if identity in formatted else item["block"])
        require(_reviewed_compiler(source) is None,
                "A revised compiler must retain its active revision archive")
        require(_reviewed_compiler_reference(source) is None,
                "A revised compiler reference must retain its active revision archive")
        require(state_fingerprint(source) == digests[identity],
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
            previous_order = _compiler_case_order_projection(document, identity, previous_block)
            current_order = _compiler_case_order_projection(document, identity, actual[identity])
            protected_previous = (_math_prompt_projection(math_texts[identity], previous_order)
                                  if identity in math_texts else previous_order)
            if identity in formatted:
                protected_previous = code_format.source_projection(protected_previous,
                    _compiler_case_format_origin(document, identity, formatted[identity]))
            protected_current = (code_format.source_projection(current_order,
                                     _compiler_case_format_origin(document, identity, formatted[identity]))
                                 if identity in formatted else current_order)
            if identity in math_texts:
                protected_current = _math_prompt_projection(math_texts[identity], protected_current)
            require(state_fingerprint(protected_previous) == state_fingerprint(protected_current),
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
            require(state_fingerprint(_compiler_case_order_contract(document, identity, old_block))
                    == state_fingerprint(_compiler_case_order_contract(document, identity, new_block))
                    == state_fingerprint(_compiler_case_order_contract(document, identity, actual[identity])),
                    "A bibliography archive changed its owner's protected exercise contract")
            _validate_bibliography_semantics(archive["previousTextSemantics"], identity, old_block)
            _validate_bibliography_semantics(archive["currentTextSemantics"], identity, new_block)
        chains[identity].append(after)
    for identity, chain in chains.items():
        require(chain[-1] == fingerprint(actual[identity]), "Editorial proof does not describe the current block")
    for identity, item in math_texts.items():
        require(fingerprint(_math_prompt_original(item)) in chains.get(identity, [])[:-1],
                "A math prompt archive needs an authentic outgoing original editorial edge")
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
        source = _math_prompt_original(math_texts[identity]) if identity in math_texts else block
        reviewed_math = reviewed_math_label(source)
        if reviewed_math is not None:
            edge = [reviewed_math["before"], reviewed_math["after"]]
            chain = chains.get(identity, [])
            require((any(chain[index:index + 2] == edge for index in range(len(chain) - 1))
                     if identity in math_texts else chain == edge),
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
                    changed = require_source_format_with_editorial(block, after[identity])
                except code_format.SourceFormatError as exc:
                    raise LayoutError(f"Unsafe source edit for {identity}: {exc}") from exc
                if changed:
                    result["stateLayout"].setdefault("sourceFormatRevisions", []).append(
                        {"id": identity, "block": deepcopy(block)})
                    recorded.add(identity)
            math_archives = _math_text_revisions(result)
            for identity, block in before.items():
                if (block.get("type") != "math_puzzle" or identity not in after
                        or block.get("prompt") == after[identity].get("prompt")
                        or identity in math_archives):
                    continue
                entry = {"id": identity, "field": "/prompt", "rawJson": _math_ordered_json(block),
                         "semanticBefore": deepcopy(_math_prompt_semantic_entry(previous, identity))}
                _math_prompt_projection(entry, after[identity])
                result["stateLayout"].setdefault("mathTextRevisions", []).append(entry)
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
                changed = require_source_format_with_editorial(block, after[identity])
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
                    require(block.get("type") in {"heading", "paragraph", "note", "code", "image", "simulation"},
                            "An exercise or editor cannot be hidden as a reference")
                    require(positions[identity] > positions[activity],
                            "A reference must follow its activity in visual order")
                else:
                    require(block.get("type") == "cpp_compiler" and
                            (type(block.get("outputCheck")) is dict or type(block.get("compileCheck")) is dict),
                            "An activity prerequisite requires a compiler with an authored objective")
                    require(positions[identity] < positions[activity],
                            "An activity prerequisite must precede its activity in visual order")


def _historical_source_format_contract(document: dict, identity: str, block: dict, *,
                                       editorial_prefix: int | None = None) -> dict:
    """Authenticate a historical source before restoring only its code bytes.

    Retirement snapshots keep the complete published presentation. A formatted
    source can belong to an earlier publication than the current one, but it
    must occur in this owner's editorial chain. Closed compiler/reference and
    ordering revisions also preserve an exact earlier source; never overwrite
    that version with code introduced by a later revision.
    """
    meta = document.get("stateLayout", {})
    original = next((entry["block"] for entry in meta.get("sourceFormatRevisions", [])
                     if entry["id"] == identity), None)
    ordered = _compiler_case_order_projection(document, identity, block, editorial_prefix=editorial_prefix)
    if original is None:
        return ordered
    nodes = {proof[field] for proof in meta["editorial"] if proof["id"] == identity
             for field in ("beforeSha256", "afterSha256")}
    require(fingerprint(block) in nodes,
            "A historical formatted source must be an authenticated editorial source")
    try:
        earlier = (_compiler_revisions(document).get(identity)
                   or _compiler_references(document).get(identity)
                   or _order_refinements(document).get(identity))
        if (earlier is not None
                and code_format.code_slots(block) == code_format.code_slots(earlier)):
            # The ordinary historical contract and paired-revision checks below
            # still verify answers, correctOrder, paths, objectives and owner.
            return ordered
        origin = _compiler_case_format_origin(document, identity, original)
        code_format.require_source_lexical(origin, ordered if _compiler_case_has_client(document, identity) else block)
        return code_format.source_projection(ordered, origin)
    except code_format.SourceFormatError as exc:
        raise LayoutError(f"Unsafe historical source formatting for {identity}: {exc}") from exc


def _retirement_editorial_prefix(document: dict, count: int) -> dict:
    return {"count": count, "sha256": fingerprint({
        "lessonId": document["id"], "locale": document["stateLayout"]["locale"],
        "editorial": document["stateLayout"]["editorial"][:count]})}


def _validate_retirement_editorial_prefix(document: dict, snapshot: dict) -> int | None:
    if "editorialPrefix" not in snapshot:
        return None
    prefix = snapshot["editorialPrefix"]
    require(type(prefix) is dict and set(prefix) == {"count", "sha256"},
            "A retirement editorial prefix needs its exact count and hash")
    count = prefix["count"]
    require(type(count) is int and 0 <= count <= len(document["stateLayout"]["editorial"]),
            "A retirement editorial prefix needs a valid integer count")
    require(prefix == _retirement_editorial_prefix(document, count),
            "A retirement editorial prefix differs from its complete published history")
    return count


def _validate_retirement_editorial_head(document: dict, identity: str, block: dict, count: int) -> None:
    expected = None
    for index, proof in enumerate(document["stateLayout"]["editorial"]):
        if proof["id"] != identity:
            continue
        if expected is None:
            expected = proof["beforeSha256"]
        if index < count:
            expected = proof["afterSha256"]
    require(expected is None or fingerprint(block) == expected,
            "A retirement source differs from its owner's exact editorial prefix")


def _validate_retirement_history(document: dict) -> None:
    meta = document.get("stateLayout", {})
    if "retirementHistory" not in meta:
        return
    require(meta.get("version") == 2, "Retirement reading history requires stateLayout v2")
    history = meta["retirementHistory"]
    require(type(history) is list and len(history) <= 100_000, "Malformed retirement reading history")
    archived = [entry["id"] for entry in meta.get("retired", [])]
    available = _all_blocks(document)
    archives = {item["id"]: _compiler_case_format_origin(document, item["id"], item["block"])
                for item in meta.get("sourceFormatRevisions", [])}
    ordered_available = {identity: _compiler_case_order_projection(document, identity, block)
                         for identity, block in available.items()}
    protected_available = {identity: code_format.source_projection(block, archives[identity])
                           if identity in archives else block
                           for identity, block in ordered_available.items()}
    choices = _choice_revisions(document)
    compilers = _compiler_revisions(document)
    compiler_references = _compiler_references(document)
    page_ids = {record["id"] for record in meta["current"]}
    page_shells = {record["id"]: {key: value for key, value in page.items() if key != "blocks"}
                   for page, record in zip(document["pages"], meta["current"])}
    covered, batches = [], []
    previous_prefix = 0
    for snapshot in history:
        fields = {"retired", "current", "pages", "compilerPuzzlesRequired", "referenceAfter", "activityAfter"}
        require(type(snapshot) is dict and fields <= set(snapshot) <= fields | {text_semantics.ROOT_FIELD, "editorialPrefix"},
            "A retirement reading snapshot needs the complete previous pages, layout, flow and relationships")
        editorial_prefix = _validate_retirement_editorial_prefix(document, snapshot)
        if editorial_prefix is not None:
            require(editorial_prefix >= previous_prefix, "Retirement editorial prefixes cannot move backwards")
            previous_prefix = editorial_prefix
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
        historical_blocks = {}
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
                if text_semantics.ROOT_FIELD in snapshot and block_id in ids:
                    # This batch supplies the retired block's own reading marks.
                    # Exact text alone cannot authenticate its complete source.
                    require(fingerprint(block) == fingerprint(available[block_id]),
                            "A semantic retirement batch must preserve its exact archived block")
                if editorial_prefix is not None:
                    _validate_retirement_editorial_head(document, block_id, block, editorial_prefix)
                source = _historical_source_format_contract(document, block_id, block,
                                                            editorial_prefix=editorial_prefix)
                historical_blocks[block_id] = source
                reviewed = _reviewed_choice(source)
                _validate_choice_pair(source, CHOICE_OPTION_REVISIONS.get((document.get("id"), block_id)))
                if reviewed is not None:
                    # The global payload alias is not evidence that this
                    # presentation ever owned the new wording. Older, original
                    # choice snapshots remain valid before a later revision.
                    require(meta["locale"] == "es" and block_id in choices
                            and CHOICE_OPTION_REVISIONS.get((document.get("id"), block_id)) == reviewed
                            and _reviewed_choice(protected_available[block_id]) == reviewed,
                            "A historical revised choice requires its matching active revision archive")
                compiler_reviewed = _reviewed_compiler(source)
                if compiler_reviewed is not None:
                    require(meta["locale"] == "es" and block_id in compilers
                            and COMPILER_CASE_REVISIONS.get((document.get("id"), block_id)) == compiler_reviewed
                            and _reviewed_compiler(protected_available[block_id]) == compiler_reviewed,
                            "A historical revised compiler requires its matching active revision archive")
                reference_reviewed = _reviewed_compiler_reference(source)
                if reference_reviewed is not None:
                    expected_reference = next((reference for (lesson, compiler), reference in COMPILER_REFERENCE_REVISIONS.items()
                                               if lesson == document.get("id") and reference["id"] == block_id), None)
                    require(meta["locale"] == "es" and block_id in compiler_references
                            and expected_reference == reference_reviewed
                            and _reviewed_compiler_reference(protected_available[block_id]) == reference_reviewed,
                            "A historical revised compiler reference requires its matching active revision archive")
                old_reading = _historical_choice_contract(document, block_id, source)
                active_reading = _historical_choice_contract(document, block_id, protected_available[block_id])
                old_reading = _historical_math_text_contract(
                    document, block_id, old_reading, reading_snapshot=True)
                active_reading = _historical_math_text_contract(document, block_id, active_reading)
                require(state_fingerprint(_historical_order_contract(document, block_id, old_reading, allow_original=True))
                        == state_fingerprint(_historical_order_contract(document, block_id, active_reading)),
                        "Historical reading snapshot changed a response/code contract")
                seen_blocks.add(block_id)
        for (lesson, compiler), reference in COMPILER_REFERENCE_REVISIONS.items():
            if lesson == document.get("id") and compiler in compilers:
                reference_id = reference["id"]
                if compiler in historical_blocks and reference_id in historical_blocks:
                    require((_reviewed_compiler(historical_blocks[compiler]) is not None)
                            == (_reviewed_compiler_reference(historical_blocks[reference_id]) is not None),
                            "A historical compiler revision and reference must preserve the same published version")
        historical = {"pages": pages, "stateLayout": {"version": 2, "current": current},
                      "referenceAfter": snapshot["referenceAfter"], "activityAfter": snapshot["activityAfter"]}
        # The lesson title is immutable across transitions. Never borrow the
        # active overlay: equal text can still have different authored intent,
        # and a legacy snapshot cannot certify that it originally had marks.
        if "title" in document:
            historical["title"] = document["title"]
        overlay = snapshot.get(text_semantics.ROOT_FIELD)
        require(overlay is None or type(overlay) is dict,
                "Historical text semantics must be an object or null")
        if overlay is not None:
            historical[text_semantics.ROOT_FIELD] = overlay
        try:
            text_semantics.validate(historical)
        except (ValueError, KeyError, TypeError) as error:
            raise LayoutError(f"Invalid historical text semantics: {error}") from error
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
    # Authoritative source bytes come from the closed, separately validated
    # sourceFormatRevisions archive; new whitespace is never a new objective.
    format_sources = {entry["id"]: entry["block"]
                      for entry in meta.get("sourceFormatRevisions", [])}
    def owner(identity: str, block: dict, *, historical: bool = False) -> None:
        original = (_historical_source_format_contract(document, identity, block) if historical
                    else code_format.source_projection(block, format_sources[identity])
                    if identity in format_sources else block)
        spec = validate_compile_check_contract(original)
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


def _compiler_selective_recovery_case_mode(owner: dict) -> str:
    """Keep the U3 History oracle closed in aggregate and individual modes."""
    _validate_compiler_case_payload(owner)
    config = owner["outputCheck"]
    require(config.get("inputComparison", "exact") == "exact",
            "U3 History requires exact input comparison")
    cases = config["cases"]
    require(len(cases) in (1, 7),
            "U3 History requires its original aggregate or all seven individual cases")
    for case in cases:
        require(case.get("target") in (None, "comprobar_cajas")
                and case.get("exitCode", 0) == 0
                and "standardError" not in case
                and case.get("standardErrorExact", "") == "",
                "U3 History requires comprobar_cajas, return zero and empty stderr")
    reviewed = ((0, 0), (4, 1), (8, 2), (20, 5), (1, 1), (5, 2), (17, 5))

    def diagnostic(pieces: int, expected: int) -> str:
        return f"piezas={pieces} esperado={expected} obtenido={expected} OK\n"

    if len(cases) == 1:
        aggregate = "".join(diagnostic(*pair) for pair in reviewed) + "Bateria: OK\n"
        require(cases[0]["standardInput"] == "" and cases[0]["outputs"] == [aggregate],
                "U3 History must preserve its exact historical aggregate")
        return "aggregate"
    expected_outputs = {
        f"{pieces}\n": [diagnostic(pieces, expected) + "Bateria: OK\n"]
        for pieces, expected in reviewed
    }
    require(cases[0]["standardInput"] == "1\n"
            and {case["standardInput"] for case in cases} == set(expected_outputs)
            and all(case["outputs"] == expected_outputs[case["standardInput"]] for case in cases),
            "U3 History requires case 1 first and all seven exact independent comparisons")
    return "individual"


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
            and cmake.get("buildType") == "Debug",
            "U3 History owner no longer has its reviewed CMake objective")
    if _compiler_selective_recovery_case_mode(owner) == "individual":
        # State validation has already authenticated this complete journal.
        # Case-only changes and formatting keep the last source epoch; an
        # older checker revision cannot authorize a later unrelated source.
        sources = [record for _, record in _compiler_case_events(document, item["activity"])
                   if "client" in record or "recipe" in record]
        require(sources and "client" in sources[-1]
                and sources[-1]["client"]["path"] == "pruebas/cajas_casos.cpp",
                "Individual U3 History cases require their current authenticated checker epoch")
        require(_compiler_selective_recovery_case_mode(
                    _compiler_case_revision_after(sources[-1])) == "individual",
                "An aggregate checker epoch cannot acquire individual cases without a new client")



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


def _named_rule_battery_tokens(source: str) -> tuple[str, ...]:
    """Recognize instrumentable source, without treating its output as evidence."""
    try:
        source_size = len(source.encode("utf-8")) if type(source) is str else 0
    except UnicodeError as exc:
        raise LayoutError("Named-rule battery source must be valid UTF-8") from exc
    require(0 < source_size <= 4 * 1024 * 1024 and "\x00" not in source and
            "\\\n" not in source and "\\\r\n" not in source,
            "Named-rule battery source is empty, excessive or contains a physical splice")
    try:
        tokens = tuple(token for token in code_format._cpp_tokens(source)
                       if not token.startswith("comment:"))
    except code_format.SourceFormatError as exc:
        raise LayoutError("Named-rule battery source cannot be tokenized: " + str(exc)) from exc
    forbidden = {
        "__LINE__", "__FILE__", "__FILEW__", "__BASE_FILE__", "__COUNTER__",
        "__INCLUDE_LEVEL__", "__DATE__", "__TIME__", "__TIMESTAMP__",
        "__PRETTY_FUNCTION__", "__FUNCSIG__", "__FUNCDNAME__", "__builtin_LINE",
        "__builtin_COLUMN", "__builtin_FILE", "__builtin_FUNCTION",
        "__builtin_source_location", "source_location", "exit", "_Exit", "_exit",
        "quick_exit", "ExitProcess", "TerminateProcess", "ExitThread", "_endthread",
        "_endthreadex", "_c_exit", "__func__", "__FUNCTION__",
    }
    depth = 0
    for token in tokens:
        require(token not in {"op:#", "op:##", "op:%:", "op:%:%:"} and
                not token.startswith(("id:lgaed_named_rule_battery_",
                                      "id:lgaed_boolean_battery_",
                                      "id:lgaed_integer_battery_")) and
                not (token.startswith("literal:") and
                     "LGAED_NAMED_RULE_BATTERY_" in token) and
                not (token.startswith("id:") and token[3:] in forbidden),
                "Named-rule battery source uses private names or unsupported environment control")
        if token.startswith("directive:"):
            require(depth == 0 and re.match(
                r'(?:#|%:)[ \t\r\v\f]*include[ \t\r\v\f]*[<"]', token[10:]),
                "Named-rule battery permits only global #include directives")
        if token in {"op:{", "op:<%"}:
            depth += 1
            require(depth <= 4096, "Named-rule battery source nesting is excessive")
        elif token in {"op:}", "op:%>"}:
            depth -= 1
            require(depth >= 0, "Named-rule battery source has an unmatched closing brace")
    require(depth == 0, "Named-rule battery source has an unclosed brace")
    return tokens


def _named_rule_battery_closing(tokens: tuple[str, ...], start: int,
                                opening: str, ending: str) -> int:
    nesting = 0
    for index in range(start, len(tokens)):
        if tokens[index] == opening or (opening == "op:{" and tokens[index] == "op:<%"):
            nesting += 1
            require(nesting <= 4096, "Named-rule battery source nesting is excessive")
        elif tokens[index] == ending or (ending == "op:}" and tokens[index] == "op:%>"):
            nesting -= 1
            if nesting == 0:
                return index
    raise LayoutError("Named-rule battery source has an unclosed delimiter")


def _named_rule_battery_return_type(tokens: tuple[str, ...], function: str) -> str:
    """Admit one original global int(int) or bool(int), with no fixed implementation."""
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,127}\Z")
    require(type(function) is str and identifier.fullmatch(function) and function != "main",
            "Named-rule battery function must be a bounded C++ identifier")
    definitions, prototypes = [], []
    depth = 0
    for index, token in enumerate(tokens):
        if token == "id:" + function and tokens[index + 1:index + 2] == ("op:(",):
            previous = tokens[index - 1] if index else ""
            call_end = _named_rule_battery_closing(tokens, index + 1, "op:(", "op:)")
            following = tokens[call_end + 1] if call_end + 1 < len(tokens) else ""
            declaration = ((previous.startswith("id:") and
                            previous not in {"id:return", "id:co_return", "id:throw"}) or
                           following in {"op:{", "op:<%"} or
                           (depth == 0 and previous in {"op:&", "op:*", "op:&&"} and
                            index > 1 and tokens[index - 2] in {"id:int", "id:bool"}))
            if declaration:
                require(depth == 0 and previous in {"id:int", "id:bool"} and
                        index + 5 < len(tokens) and
                        (index == 1 or tokens[index - 2] in {"op:;", "op:}", "op:%>"} or
                         tokens[index - 2].startswith("directive:")),
                        "Named-rule battery requires an unqualified global int or bool definition")
                end = index + 4
                signature = tokens[index - 1:end + 1]
                require(len(signature) == 6 and
                        signature[2:4] == ("op:(", "id:int") and
                        signature[4].startswith("id:") and
                        identifier.fullmatch(signature[4][3:]) and
                        signature[4] != "id:main" and signature[5] == "op:)",
                        "Named-rule battery function requires one named int parameter")
                following = tokens[end + 1]
                if following == "op:{":
                    body_end = _named_rule_battery_closing(tokens, end + 1, "op:{", "op:}")
                    require(not any(part in {"id:__func__", "id:__FUNCTION__", "id:__FUNCSIG__"}
                                    for part in tokens[end + 2:body_end]),
                            "Named-rule battery function cannot inspect its instrumented identity")
                    definitions.append(signature)
                else:
                    require(following == "op:;",
                            "Named-rule battery function has an unsupported qualifier or declaration")
                    prototypes.append(signature)
        if token in {"op:{", "op:<%"}:
            depth += 1
        elif token in {"op:}", "op:%>"}:
            depth -= 1
    require(len(definitions) == 1 and all(item[0] == definitions[0][0] for item in prototypes),
            "Named-rule battery needs one definition and matching original prototypes")
    return definitions[0][0][3:]


def _validate_named_rule_battery_source(source: str, cases: list[dict],
                                       single_file: bool = True) -> None:
    tokens = _named_rule_battery_tokens(source)
    returns = {function: _named_rule_battery_return_type(tokens, function)
               for function in dict.fromkeys(case["function"] for case in cases)}
    require(all(returns[case["function"]] != "bool" or case["expected"] in (0, 1)
                for case in cases),
            "Named-rule bool battery expectations must be the integers zero or one")
    require(_named_rule_battery_main_count(tokens) == (1 if single_file else 0),
            "Named-rule battery requires one single-file main or a separate CMake checker")


def _named_rule_battery_main_count(tokens: tuple[str, ...]) -> int:
    depth, definitions = 0, 0
    for index, token in enumerate(tokens):
        if depth == 0 and token == "id:main" and tokens[index + 1:index + 2] == ("op:(",):
            require(index > 0 and tokens[index - 1] == "id:int",
                    "Named-rule checker main must return int")
            end = _named_rule_battery_closing(tokens, index + 1, "op:(", "op:)") + 1
            if end < len(tokens) and tokens[end] in {"op:{", "op:<%"}:
                definitions += 1
            else:
                require(end < len(tokens) and tokens[end] == "op:;",
                        "Named-rule checker main has an unsupported declaration or qualifier")
        if token in {"op:{", "op:<%"}:
            depth += 1
        elif token in {"op:}", "op:%>"}:
            depth -= 1
    return definitions


def _named_rule_battery_driver(source: str) -> None:
    require(_named_rule_battery_main_count(_named_rule_battery_tokens(source)) == 1,
            "Named-rule battery requires one original checker main")


def _validate_named_rule_constant_variations(source: str, rules: list[dict],
                                            base_cases: list[dict], variations: object) -> None:
    """Validate the optional private variations without inspecting the starter's constants."""
    require(source == "" or (len(source) <= 256 and
            re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_./-]*\.cpp", source) and ".." not in source),
            "Constant variations require a safe original relative C++ source path")
    requirements = {rule["function"]: rule for rule in rules}
    require(all(len(name) <= 128 and name != "main" and not name.startswith((
                "lgaed_named_rule_battery_", "lgaed_boolean_battery_",
                "lgaed_integer_battery_")) for name in requirements),
            "Constant variations require bounded non-reserved function names")
    require(type(variations) is list and 1 <= len(variations) <= 8,
            "Constant variations require 1..8 authored alternatives")

    def signed_integer(value: object) -> bool:
        # WinRT preserves numeric value, not whether JSON used 6, 6.0 or 6e0.
        # Check the range before converting: a Python int may exceed float range.
        return (type(value) in (int, float) and -(2 ** 31) <= value <= 2 ** 31 - 1
                and (type(value) is int or value.is_integer()))

    originals = {(case["function"], case["input"]): case["expected"] for case in base_cases}
    seen_variations = set()
    for variation in variations:
        require(type(variation) is dict and set(variation) == {"function", "value", "cases"},
                "Each constant variation requires function, value and cases only")
        target, value = variation["function"], variation["value"]
        require(type(target) is str and target in requirements and signed_integer(value),
                "A constant variation must select a named function and an int32 alternative")
        require(value != requirements[target]["value"],
                "A constant variation must change its original agreement")
        key = target, value
        require(key not in seen_variations, "Repeated function/value constant variation")
        seen_variations.add(key)
        cases = variation["cases"]
        require(type(cases) is list and 1 <= len(cases) <= 32,
                "Each constant variation requires 1..32 comparisons")
        seen_cases, covered, unchanged = set(), set(), set()
        changed_target = False
        for case in cases:
            require(type(case) is dict and set(case) == {"function", "input", "expected"}
                    and type(case["function"]) is str and case["function"] in requirements
                    and signed_integer(case["input"]) and signed_integer(case["expected"]),
                    "Constant-variation cases require named functions and int32 facts")
            key = case["function"], case["input"]
            require(key not in seen_cases, "A constant variation repeated a function/input pair")
            seen_cases.add(key)
            covered.add(case["function"])
            if key in originals:
                if case["function"] == target:
                    changed_target |= case["expected"] != originals[key]
                else:
                    require(case["expected"] == originals[key],
                            "A constant variation changed another function's shared expectation")
                    unchanged.add(case["function"])
        require(covered == set(requirements),
                "Each constant variation must cover every named function")
        require(changed_target,
                "A constant variation needs a shared target input with a different expected result")
        require(unchanged == set(requirements) - {target},
                "Each non-target function needs a preserved shared input in every variation")


def _validate_compiler_named_rule_checks(document: dict) -> None:
    """Validate source-role prerequisites without rewriting editor history."""
    if "compilerNamedRuleChecks" not in document:
        return
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es",
            "Named-rule declarations require an original Spanish v2 lesson")
    checks = document["compilerNamedRuleChecks"]
    require(type(checks) is list and 0 < len(checks) <= 64,
            "Named-rule declarations require 1..64 owners")
    owners, seen = _blocks(document), set()
    for record in checks:
        require(type(record) is dict and
                {"activity", "path", "rules"} <= set(record) <=
                {"activity", "path", "rules", "editablePaths", "commentOnlyPaths",
                 "executionProbe"},
                "Named-rule owner must declare activity, path and rules only")
        identity = _identity(record["activity"])
        require(identity not in seen, "Repeated named-rule owner")
        seen.add(identity)
        block = owners.get(identity)
        require(type(block) is dict and block.get("type") == "cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"]) and "compileCheck" not in block,
                "Named rules require a mapped editor with a real output objective")
        source = record["path"]
        require(type(source) is str, "Named-rule source path must be text")
        files = block.get("projectFiles")
        if files:
            require(type(files) is list and source.endswith(".cpp")
                    and source in {entry.get("path") for entry in files if type(entry) is dict}
                    and type(block.get("cmake")) is dict,
                    "Named-rule project must reference an original C++ source")
        else:
            require(source == "" and not block.get("cmake") and
                    type(block.get("initialCode")) is str,
                    "Single-file named rules must use an empty source path")
        extra = record.get("editablePaths", [])
        require(type(extra) is list and len(extra) <= 32 and
                all(type(path) is str for path in extra) and
                len(set(extra)) == len(extra) and
                (not extra or (bool(files) and source not in extra and
                    set(extra) <= {item.get("path") for item in files if type(item) is dict})),
                "Extra editable paths must be unique original project members")
        comments = record.get("commentOnlyPaths", [])
        require(type(comments) is list and len(comments) <= 32 and
                all(type(path) is str and
                    path.endswith((".h", ".hpp", ".hxx")) for path in comments) and
                len(set(comments)) == len(comments) and set(comments) <= set(extra),
                "Comment-only edits require unique editable original header paths")
        rules = record["rules"]
        require(type(rules) is list and 0 < len(rules) <= 8,
                "Each named-rule source requires 1..8 function contracts")
        functions = set()
        for rule in rules:
            require(type(rule) is dict and set(rule) == {
                "function", "value", "purposeWords", "minUses"},
                "A named rule requires function, value, purposeWords and minUses")
            name = rule["function"]
            require(type(name) is str and re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name)
                    and name not in functions, "Missing or duplicate named-rule function")
            functions.add(name)
            value = rule["value"]
            require(type(value) is int and -(2 ** 31) <= value <= 2 ** 31 - 1,
                    "Named-rule value must be a signed representable integer")
            count = rule["minUses"]
            require(type(count) is int and 1 <= count <= 8,
                    "Named-rule minimum source uses must be between 1 and 8")
            words = rule["purposeWords"]
            require(type(words) is list and 1 <= len(words) <= 12
                    and all(type(w) is str and re.fullmatch(r"[A-Za-z_]{3,32}", w)
                            for w in words) and len(set(words)) == len(words),
                    "Named-rule purpose vocabulary must be bounded and unique")
        if "executionProbe" in record:
            probe = record["executionProbe"]
            require(type(probe) is dict and {"harness", "header", "cases"} <= set(probe) <=
                    {"harness", "header", "cases", "batteryCheck", "constantVariations"},
                    "Independent named-rule probes require harness, header and cases")
            harness, header = probe["harness"], probe["header"]
            single_file = not bool(files)
            if single_file:
                require(harness == "" and header == "" and not block.get("cmake")
                        and source == "",
                        "Single-file probes require empty harness and header paths")
            else:
                require(type(block.get("cmake")) is dict,
                        "Project named-rule probes require an original CMake editor")
                existing = {entry.get("path") for entry in files if type(entry) is dict}
                require(type(harness) is str and harness.endswith(".cpp") and
                        harness in existing and harness != source,
                        "Independent probe must replace a different original C++ file")
                require(type(header) is str and len(header) <= 256 and
                        re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_./-]*", header) and
                        ".." not in header,
                        "Independent probe include path must be a safe relative name")
                main_cmake = next((entry.get("initialCode", "")
                                   for entry in files if type(entry) is dict
                                   and entry.get("path") == "CMakeLists.txt"), "")
                target = block["cmake"].get("target")
                require(type(target) is str and
                        f"add_executable({target} {harness})" in main_cmake,
                        "The independent probe must replace the selected executable source")
            comparisons = probe["cases"]
            require(type(comparisons) is list and 1 <= len(comparisons) <= 32,
                    "Independent source probe needs 1..32 comparisons")
            seen_cases, covered = set(), set()
            for case in comparisons:
                require(type(case) is dict and set(case) == {
                    "function", "input", "expected"} and
                    case["function"] in functions and
                    type(case["input"]) is int and
                    type(case["expected"]) is int and
                    all(-(2 ** 31) <= case[k] <= 2 ** 31 - 1
                        for k in ("input", "expected")),
                    "Independent cases must call named functions with C++ int facts")
                key = case["function"], case["input"]
                require(key not in seen_cases, "An independent probe repeated an input")
                seen_cases.add(key)
                covered.add(case["function"])
            require(covered == functions,
                    "Every named function requires an independent execution case")
            if "constantVariations" in probe:
                graphs = document.get("compilerCmakeGraphChecks", [])
                require(type(graphs) is list and not any(
                    type(graph) is dict and graph.get("activity") == identity for graph in graphs),
                    "Constant variations cannot share an owner with a CMake graph check")
                _validate_named_rule_constant_variations(
                    source, rules, comparisons, probe["constantVariations"])
            if "batteryCheck" in probe:
                first_case = block["outputCheck"]["cases"][0]
                require(block.get("initialStandardInput", "") == "" and
                        type(first_case) is dict and first_case.get("standardInput") == "",
                        "Named-rule batteries require empty initial and first-case standard input")
                require(type(first_case.get("exitCode", 0)) is int and
                        first_case.get("exitCode", 0) == 0 and
                        ("target" not in first_case or
                         (type(block.get("cmake")) is dict and
                          first_case["target"] == block["cmake"].get("target"))),
                        "Named-rule batteries require public success from the original executable target")
                graphs = document.get("compilerCmakeGraphChecks", [])
                require(type(graphs) is list and not any(
                    type(graph) is dict and graph.get("activity") == identity for graph in graphs),
                    "Named-rule batteries cannot share an owner with a CMake graph check")
                battery = probe["batteryCheck"]
                require(type(battery) is dict and {"cases"} <= set(battery) <=
                        {"cases", "failureExitCode"},
                        "Named-rule battery checks require cases and an optional failure exit")
                failure_exit = battery.get("failureExitCode", 1)
                require(type(failure_exit) is int and 1 <= failure_exit <= 0xFFFFFFFF,
                        "Named-rule battery failure exit must be a positive Win32 return code")
                required_cases = battery["cases"]
                require(type(required_cases) is list and 1 <= len(required_cases) <= 32,
                        "Named-rule battery requires 1..32 independent comparisons")
                private_expected = {(case["function"], case["input"]): case["expected"]
                                    for case in comparisons}
                required_seen, required_functions = set(), set()
                for case in required_cases:
                    require(type(case) is dict and set(case) == {
                        "function", "input", "expected"} and
                        type(case["function"]) is str and case["function"] in functions and
                        type(case["input"]) is int and type(case["expected"]) is int and
                        all(-(2 ** 31) <= case[key] <= 2 ** 31 - 1
                            for key in ("input", "expected")),
                        "Required battery cases must use named functions and C++ int facts")
                    case_key = case["function"], case["input"]
                    require(case_key not in required_seen,
                            "A named-rule battery repeated a required input")
                    require(case_key not in private_expected or
                            case["expected"] == private_expected[case_key],
                            "Shared private and student-battery cases cannot have different expected results")
                    required_seen.add(case_key)
                    required_functions.add(case["function"])
                require(required_functions == functions,
                        "Every named function needs a required student-battery comparison")
                require(source == "" or (len(source) <= 256 and
                        re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_./-]*\.cpp", source) and
                        ".." not in source),
                        "Named-rule battery source requires a safe original relative C++ path")
                original_source = block["initialCode"] if single_file else next(
                    entry["initialCode"] for entry in files if entry.get("path") == source)
                _validate_named_rule_battery_source(original_source, required_cases, single_file)
                if single_file:
                    _named_rule_battery_driver(original_source)
                else:
                    original_driver = next(entry["initialCode"] for entry in files
                                           if entry.get("path") == harness)
                    _named_rule_battery_driver(original_driver)
        # An editable .cpp file can itself be the CMake test driver.
        # Never allow it to fabricate stdout unless a separate independent
        # executable verifies the same functions.
        if any(path.endswith(".cpp") for path in extra):
            require("executionProbe" in record and
                    record["executionProbe"]["harness"] in extra,
                    "Editable CMake test drivers require independent execution probes")




def _boolean_battery_definition(source: str, function: str) -> tuple[str, ...]:
    """Recognize the bounded global bool(int, int) definition used by the worker.

    This is an admission grammar, not a C++ compiler. The submitted program
    must later retain this definition's tokens and prove its own comparisons
    through separately compiled mutations. Comments and spacing are free.
    """
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,127}\Z")
    try:
        source_size = len(source.encode("utf-8")) if type(source) is str else 0
    except UnicodeError as exc:
        raise LayoutError("Boolean battery source must be valid UTF-8") from exc
    require(0 < source_size <= 4 * 1024 * 1024 and
            "\x00" not in source and "\\\n" not in source and "\\\r\n" not in source,
            "Boolean battery source is empty, excessive or contains a physical splice")
    require(type(function) is str and identifier.fullmatch(function) and
            not function.startswith("lgaed_boolean_battery_"),
            "Boolean battery function must be a safe C++ identifier")
    try:
        tokens = tuple(token for token in code_format._cpp_tokens(source)
                       if not token.startswith("comment:"))
    except code_format.SourceFormatError as exc:
        raise LayoutError("Boolean battery source cannot be tokenized: " + str(exc)) from exc

    def closing(start: int, opening: str, ending: str) -> int:
        nesting = 0
        for index in range(start, len(tokens)):
            if tokens[index] == opening or (opening == "op:{" and tokens[index] == "op:<%"):
                nesting += 1
                require(nesting <= 4096, "Boolean battery source nesting is excessive")
            elif tokens[index] == ending or (ending == "op:}" and tokens[index] == "op:%>"):
                nesting -= 1
                if nesting == 0:
                    return index
        raise LayoutError("Boolean battery definition has an unclosed delimiter")

    definitions, prototypes = [], []
    depth = 0
    for index, token in enumerate(tokens):
        require(token not in {"op:#", "op:##", "op:%:", "op:%:%:"} and
                not token.startswith("id:lgaed_boolean_battery_"),
                "Boolean battery source uses unsupported preprocessing or private names")
        if token.startswith("directive:"):
            require(depth == 0 and re.match(r'(?:#|%:)[ \t\r\v\f]*include[ \t\r\v\f]*[<"]', token[10:]),
                    "Boolean battery source permits only global #include directives")
        if token == "id:" + function and tokens[index + 1:index + 2] == ("op:(",):
            previous = tokens[index - 1] if index else ""
            call_end = closing(index + 1, "op:(", "op:)")
            following = tokens[call_end + 1] if call_end + 1 < len(tokens) else ""
            # Ordinary calls are not declarations. Other definitions and
            # global overload declarations cannot masquerade as this function.
            declaration = ((previous.startswith("id:") and
                            previous not in {"id:return", "id:co_return", "id:throw"}) or
                           following in {"op:{", "op:<%"} or
                           (depth == 0 and previous in {"op:&", "op:*", "op:&&"} and
                            index > 1 and tokens[index - 2] == "id:bool"))
            if declaration:
                require(depth == 0 and previous == "id:bool" and
                        index + 8 < len(tokens) and
                        (index == 1 or tokens[index - 2] in {"op:;", "op:}"} or
                         tokens[index - 2].startswith("directive:")),
                        "Boolean battery needs an unqualified global bool definition")
                end = index + 7
                signature = tokens[index - 1:end + 1]
                require(len(signature) == 9 and
                        signature[2:4] == ("op:(", "id:int") and
                        signature[5:7] == ("op:,", "id:int") and
                        signature[8] == "op:)" and
                        all(value.startswith("id:") and identifier.fullmatch(value[3:])
                            for value in (signature[4], signature[7])) and
                        signature[4] != signature[7],
                        "Boolean battery function needs two distinct named int parameters")
                following = tokens[end + 1]
                if following == "op:{":
                    body_end = closing(end + 1, "op:{", "op:}")
                    definitions.append(tokens[index - 1:body_end + 1])
                else:
                    require(following == "op:;",
                            "Boolean battery function has an unsupported qualifier or declaration")
                    prototypes.append(signature)
        if token in {"op:{", "op:<%"}:
            depth += 1
            require(depth <= 4096, "Boolean battery source nesting is excessive")
        elif token in {"op:}", "op:%>"}:
            depth -= 1
            require(depth >= 0, "Boolean battery source has an unmatched closing brace")
    require(depth == 0 and len(definitions) == 1,
            "Boolean battery needs exactly one balanced original function definition")
    require(all(signature == definitions[0][:9] for signature in prototypes),
            "Boolean battery prototype differs from its original function signature")
    return definitions[0]


def _validate_boolean_battery_record(document: dict, record: dict, block: dict) -> None:
    require(set(record) == {"activity", "kind", "function", "publishedResult", "cases"}
            and record["kind"] == "boolean-pair-v1",
            "Unknown or missing boolean battery declaration field")
    require(type(block) is dict and block.get("type") == "cpp_compiler" and
            block.get("language", "cpp") == "cpp" and
            not any(field in block for field in
                    ("outputCheck", "compileCheck", "cmake", "projectFiles", "sourceFiles")),
            "A boolean battery must own an unevaluated single-file C++ editor")
    for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                  "compilerNamedRuleChecks", "compilerCmakeGraphChecks",
                  "compilerReductionChecks"):
        declarations = document.get(field)
        require(type(declarations) is not list or
                not any(type(item) is dict and item.get("activity") == record["activity"]
                        for item in declarations),
                "Boolean battery owner conflicts with another compiler objective")
    require(type(record["publishedResult"]) is str and
            record["publishedResult"] in {"success", "failure"},
            "Boolean battery published result must be success or failure")
    _boolean_battery_definition(block.get("initialCode"), record["function"])
    cases = record["cases"]
    require(type(cases) is list and 1 <= len(cases) <= 32,
            "Boolean battery requires 1..32 independently declared cases")
    seen = set()
    for case in cases:
        require(type(case) is dict and set(case) == {"first", "second", "expected"} and
                all(type(case[name]) is int and -(2 ** 31) <= case[name] < 2 ** 31
                    for name in ("first", "second")) and type(case["expected"]) is bool,
                "Boolean battery cases need two signed C++ integers and a boolean expectation")
        pair = case["first"], case["second"]
        require(pair not in seen, "Duplicate boolean battery input pair")
        seen.add(pair)


def _reduction_source_contract(source: str, function: str, state: str,
                               initial_state: int, dependencies: list[dict]) -> tuple[str, ...]:
    """Admit the preserved int(vector<int>, int) source used by reduction QA.

    This bounded grammar does not compile the program or certify its reduction.
    It protects an original function and a global integer, while the native
    worker checks the submitted source, real ordered calls and observation.
    """
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,127}\Z")
    require(all(type(name) is str and identifier.fullmatch(name) and
                not name.startswith("lgaed_reduction_") for name in (function, state))
            and function != state and function != "main",
            "Reduction function and state need different safe C++ identifiers")
    require(type(initial_state) is int and -(2 ** 31) <= initial_state < 2 ** 31,
            "Reduction initial state must be a signed C++ integer")
    try:
        source_size = len(source.encode("utf-8")) if type(source) is str else 0
    except UnicodeError as exc:
        raise LayoutError("Reduction source must be valid UTF-8") from exc
    require(0 < source_size <= 4 * 1024 * 1024 and
            "\x00" not in source and "\\\n" not in source and "\\\r\n" not in source,
            "Reduction source is empty, excessive or contains a physical splice")
    try:
        tokens = tuple(token for token in code_format._cpp_tokens(source)
                       if not token.startswith("comment:"))
    except code_format.SourceFormatError as exc:
        raise LayoutError("Reduction source cannot be tokenized: " + str(exc)) from exc

    def closing(start: int, opening: str, ending: str) -> int:
        nesting = 0
        for index in range(start, len(tokens)):
            if tokens[index] == opening or (opening == "op:{" and tokens[index] == "op:<%"):
                nesting += 1
                require(nesting <= 4096, "Reduction source nesting is excessive")
            elif tokens[index] == ending or (ending == "op:}" and tokens[index] == "op:%>"):
                nesting -= 1
                if nesting == 0:
                    return index
        raise LayoutError("Reduction source has an unclosed delimiter")

    def integer_literal(values: tuple[str, ...]) -> int:
        require((len(values) == 1 and values[0].startswith("number:")) or
                (len(values) == 2 and values[0] in {"op:+", "op:-"} and
                 values[1].startswith("number:")),
                "Reduction state requires one optionally signed decimal literal")
        spelling = "".join(value[3:] if value.startswith("op:") else value[7:]
                           if value.startswith("number:") else "?" for value in values)
        require(re.fullmatch(r"[+-]?(?:0|[1-9][0-9]*)", spelling) is not None
                and len(spelling.lstrip("+-")) <= 10,
                "Reduction state requires a decimal integer initializer")
        result = int(spelling)
        require(-(2 ** 31) <= result < 2 ** 31,
                "Reduction state initializer is outside signed C++ integer range")
        return result

    definitions, prototypes, states = [], [], []
    included_headers = set()
    depth = 0
    for index, token in enumerate(tokens):
        require(token not in {"op:#", "op:##", "op:%:", "op:%:%:"} and
                not token.startswith(("id:lgaed_reduction_", "id:lgaed_boolean_battery_")),
                "Reduction source uses unsupported preprocessing or private names")
        if token.startswith("directive:"):
            directive = token[10:]
            require(depth == 0 and
                    re.match(r'(?:#|%:)[ \t\r\v\f]*include[ \t\r\v\f]*[<"]', directive),
                    "Reduction source permits only global #include directives")
            included = re.match(
                r'(?:#|%:)[ \t\r\v\f]*include[ \t\r\v\f]*(?:<([^>]+)>|"([^"]+)")',
                directive)
            require(included is not None, "Reduction include directive is incomplete")
            included_headers.add(included[1] if included[1] is not None else included[2])
        if token == "id:" + function and tokens[index + 1:index + 2] == ("op:(",):
            previous = tokens[index - 1] if index else ""
            call_end = closing(index + 1, "op:(", "op:)")
            following = tokens[call_end + 1] if call_end + 1 < len(tokens) else ""
            declaration = ((previous.startswith("id:") and
                            previous not in {"id:return", "id:co_return", "id:throw"}) or
                           following in {"op:{", "op:<%"} or
                           (depth == 0 and previous in {"op:&", "op:*", "op:&&"} and
                            index > 1 and tokens[index - 2] == "id:int"))
            if declaration:
                require(depth == 0 and previous == "id:int" and
                        index + 13 < len(tokens) and
                        (index == 1 or tokens[index - 2] in {"op:;", "op:}"} or
                         tokens[index - 2].startswith("directive:")),
                        "Reduction needs an unqualified global int function")
                end = index + 12
                signature = tokens[index - 1:end + 1]
                require(len(signature) == 14 and
                        signature[2:9] == ("op:(", "id:std", "op:::",
                            "id:vector", "op:<", "id:int", "op:>") and
                        signature[10:12] == ("op:,", "id:int") and
                        signature[13] == "op:)" and
                        all(value.startswith("id:") and identifier.fullmatch(value[3:])
                            for value in (signature[9], signature[12])) and
                        signature[9] != signature[12] and
                        "id:" + state not in (signature[9], signature[12]),
                        "Reduction function needs distinct named vector<int> and int parameters")
                following = tokens[end + 1]
                if following == "op:{":
                    body_end = closing(end + 1, "op:{", "op:}")
                    require(not any(part in {"id:__func__", "id:__FUNCTION__", "id:__FUNCSIG__"}
                                    for part in tokens[end + 2:body_end]),
                            "Reduction protected body cannot inspect its enclosing function identity")
                    definitions.append(tokens[index - 1:body_end + 1])
                else:
                    require(following == "op:;",
                            "Reduction function has an unsupported qualifier or declaration")
                    prototypes.append(signature)
        if token == "id:" + state:
            previous = tokens[index - 1] if index else ""
            declaration = ((previous.startswith("id:") and
                            previous not in {"id:return", "id:co_return", "id:throw"}) or
                           (previous in {"op:&", "op:*", "op:&&"} and index > 1 and
                            tokens[index - 2].startswith("id:") and
                            tokens[index - 2] not in {"id:return", "id:co_return", "id:throw"}))
            if declaration:
                require(depth == 0 and previous == "id:int" and
                        (index == 1 or tokens[index - 2] in {"op:;", "op:}"} or
                         tokens[index - 2].startswith("directive:")),
                        "Reduction state must be one unqualified global int without shadows")
                start = index + 1
                require(start < len(tokens), "Reduction state declaration is incomplete")
                if tokens[start] == "op:;":
                    value = 0
                elif tokens[start] in {"op:{", "op:("}:
                    opening = tokens[start]
                    end = closing(start, opening, "op:}" if opening == "op:{" else "op:)")
                    values = tokens[start + 1:end]
                    require(tokens[end + 1:end + 2] == ("op:;",) and
                            (bool(values) or opening == "op:{"),
                            "Reduction state needs a scalar integer initializer")
                    value = integer_literal(values) if values else 0
                else:
                    require(tokens[start] == "op:=", "Reduction state initializer is unsupported")
                    end = start + 1
                    while end < len(tokens) and tokens[end] != "op:;":
                        end += 1
                    require(end < len(tokens), "Reduction state declaration has no terminator")
                    value = integer_literal(tokens[start + 1:end])
                require(value == initial_state,
                        "Reduction state initializer differs from the declared initial state")
                states.append(value)
        if token in {"op:{", "op:<%"}:
            depth += 1
            require(depth <= 4096, "Reduction source nesting is excessive")
        elif token in {"op:}", "op:%>"}:
            depth -= 1
            require(depth >= 0, "Reduction source has an unmatched closing brace")
    require(depth == 0 and len(definitions) == 1,
            "Reduction needs exactly one balanced original function definition")
    require(len(states) == 1, "Reduction needs exactly one original global state definition")
    require(all(signature == definitions[0][:14] for signature in prototypes),
            "Reduction prototype differs from its original function signature")

    # Declaration-driven dependency policy: optional entries may anticipate
    # alternative output facilities. It is not a universal C++ header table.
    code = tokens
    def contains(sequence: tuple[str, ...]) -> bool:
        return any(code[index:index + len(sequence)] == sequence
                   for index in range(len(code) - len(sequence) + 1))
    using_std = contains(("id:using", "id:namespace", "id:std", "op:;"))
    for dependency in dependencies:
        used = False
        for symbol in dependency["symbols"]:
            parts = symbol.split("::")
            sequence = tuple(value for index, part in enumerate(parts)
                             for value in (("op:::", "id:" + part) if index else ("id:" + part,)))
            used = used or contains(sequence)
            if parts[0] == "std" and len(parts) == 2 and using_std:
                used = used or "id:" + parts[-1] in code
        require(not used or dependency["header"] in included_headers,
                "Reduction source lacks a declared directly included header: " + dependency["header"])
    return definitions[0]


def _validate_compiler_reduction_checks(document: dict) -> None:
    """Admit new reduction goals without changing a historical editor."""
    if "compilerReductionChecks" not in document:
        return
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es",
            "Reduction objectives require a Spanish v2 lesson")
    checks = document["compilerReductionChecks"]
    require(type(checks) is list and 1 <= len(checks) <= 32,
            "Reduction declarations require 1..32 owners")
    owners, seen = _blocks(document), set()
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,127}\Z")
    fields = {"activity", "kind", "function", "state", "initialState", "calls",
              "observedCall", "requiredResult", "exitCode", "dependencies"}
    def int32(value: object) -> bool:
        return type(value) is int and -(2 ** 31) <= value < 2 ** 31
    for record in checks:
        require(type(record) is dict and set(record) == fields and
                record["kind"] == "stateful-vector-int-v1",
                "Unknown or missing reduction declaration field or kind")
        identity = _identity(record["activity"])
        require(identity not in seen, "Duplicate reduction owner")
        seen.add(identity)
        block = owners.get(identity)
        require(type(block) is dict and block.get("type") == "cpp_compiler" and
                block.get("language", "cpp") == "cpp" and
                not any(field in block for field in
                        ("outputCheck", "compileCheck", "cmake", "projectFiles", "sourceFiles")),
                "A reduction must own an unevaluated single-file C++ editor")
        for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                      "compilerNamedRuleChecks", "compilerOpenBatteryChecks",
                      "compilerCmakeGraphChecks"):
            declarations = document.get(field)
            require(type(declarations) is not list or
                    not any(type(item) is dict and item.get("activity") == identity
                            for item in declarations),
                    "Reduction owner conflicts with another compiler objective")
        require(int32(record["initialState"]) and int32(record["requiredResult"]) and
                type(record["exitCode"]) is int and record["exitCode"] == 0,
                "Reduction state/result require signed C++ integers and exitCode integer zero")
        calls = record["calls"]
        require(type(calls) is list and 2 <= len(calls) <= 32,
                "Reduction requires 2..32 ordered calls")
        for call in calls:
            require(type(call) is dict and set(call) == {"values", "argument", "result", "stateAfter"}
                    and type(call["values"]) is list and len(call["values"]) <= 64
                    and all(int32(value) for value in call["values"])
                    and all(int32(call[name]) for name in ("argument", "result", "stateAfter")),
                    "Reduction calls require bounded vectors and signed C++ integer observations")
        observed = record["observedCall"]
        require(type(observed) is int and 1 <= observed < len(calls) and
                record["requiredResult"] != calls[observed]["result"],
                "Reduction needs a prepared observed call with a result different from the requirement")
        dependencies = record["dependencies"]
        require(type(dependencies) is list and 1 <= len(dependencies) <= 32,
                "Reduction dependencies require 1..32 header declarations")
        headers, symbols = set(), set()
        for dependency in dependencies:
            require(type(dependency) is dict and set(dependency) == {"header", "symbols"},
                    "Reduction dependencies require header and symbols only")
            header = dependency["header"]
            require(type(header) is str and re.fullmatch(r"[a-z][a-z0-9_.]{0,63}", header)
                    and ".." not in header and header not in headers,
                    "Reduction header must be a unique safe standard-header name")
            headers.add(header)
            names = dependency["symbols"]
            require(type(names) is list and 1 <= len(names) <= 32,
                    "Reduction dependencies need 1..32 qualified symbols")
            for name in names:
                require(type(name) is str and len(name) <= 512 and
                        len(name.split("::")) >= 2 and
                        all(identifier.fullmatch(part) and not part.startswith("lgaed_reduction_")
                            for part in name.split("::")) and name not in symbols,
                        "Reduction dependency needs a unique safe qualified symbol")
                symbols.add(name)
        _reduction_source_contract(block.get("initialCode"), record["function"],
                                   record["state"], record["initialState"], dependencies)


def _integer_battery_definition(source: str, function: str) -> tuple[str, ...]:
    """Admit the published global int(int) definition, without deriving expected values."""
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,127}\Z")
    try:
        source_size = len(source.encode("utf-8")) if type(source) is str else 0
    except UnicodeError as exc:
        raise LayoutError("Integer battery source must be valid UTF-8") from exc
    require(0 < source_size <= 4 * 1024 * 1024 and
            "\x00" not in source and "\\\n" not in source and "\\\r\n" not in source,
            "Integer battery source is empty, excessive or contains a physical splice")
    require(type(function) is str and identifier.fullmatch(function) and
            function != "main" and not function.startswith(
                ("lgaed_boolean_battery_", "lgaed_integer_battery_")),
            "Integer battery function must be a safe C++ identifier")
    try:
        tokens = tuple(token for token in code_format._cpp_tokens(source)
                       if not token.startswith("comment:"))
    except code_format.SourceFormatError as exc:
        raise LayoutError("Integer battery source cannot be tokenized: " + str(exc)) from exc

    def closing(start: int, opening: str, ending: str) -> int:
        nesting = 0
        for index in range(start, len(tokens)):
            if tokens[index] == opening or (opening == "op:{" and tokens[index] == "op:<%"):
                nesting += 1
                require(nesting <= 4096, "Integer battery source nesting is excessive")
            elif tokens[index] == ending or (ending == "op:}" and tokens[index] == "op:%>"):
                nesting -= 1
                if nesting == 0:
                    return index
        raise LayoutError("Integer battery definition has an unclosed delimiter")

    definitions, prototypes = [], []
    depth = 0
    for index, token in enumerate(tokens):
        require(token not in {"op:#", "op:##", "op:%:", "op:%:%:"} and
                not token.startswith(("id:lgaed_boolean_battery_", "id:lgaed_integer_battery_")),
                "Integer battery source uses unsupported preprocessing or private names")
        if token.startswith("directive:"):
            require(depth == 0 and re.match(r'(?:#|%:)[ \t\r\v\f]*include[ \t\r\v\f]*[<"]', token[10:]),
                    "Integer battery source permits only global #include directives")
        if token == "id:" + function and tokens[index + 1:index + 2] == ("op:(",):
            previous = tokens[index - 1] if index else ""
            call_end = closing(index + 1, "op:(", "op:)")
            following = tokens[call_end + 1] if call_end + 1 < len(tokens) else ""
            declaration = ((previous.startswith("id:") and
                            previous not in {"id:return", "id:co_return", "id:throw"}) or
                           following in {"op:{", "op:<%"} or
                           (depth == 0 and previous in {"op:&", "op:*", "op:&&"} and
                            index > 1 and tokens[index - 2] == "id:int"))
            if declaration:
                require(depth == 0 and previous == "id:int" and
                        index + 5 < len(tokens) and
                        (index == 1 or tokens[index - 2] in {"op:;", "op:}", "op:%>"} or
                         tokens[index - 2].startswith("directive:")),
                        "Integer battery needs an unqualified global int definition")
                end = index + 4
                signature = tokens[index - 1:end + 1]
                require(len(signature) == 6 and
                        signature[2:4] == ("op:(", "id:int") and
                        signature[4].startswith("id:") and
                        identifier.fullmatch(signature[4][3:]) and signature[5] == "op:)",
                        "Integer battery function needs one named int parameter")
                following = tokens[end + 1]
                if following == "op:{":
                    body_end = closing(end + 1, "op:{", "op:}")
                    body = tokens[end + 2:body_end]
                    require(not any(token in {"id:__func__", "id:__FUNCTION__", "id:__FUNCSIG__"}
                                    for token in body),
                            "Integer battery function cannot introspect its instrumented identity")
                    definitions.append(tokens[index - 1:body_end + 1])
                else:
                    require(following == "op:;",
                            "Integer battery function has an unsupported qualifier or declaration")
                    prototypes.append(signature)
        if token in {"op:{", "op:<%"}:
            depth += 1
            require(depth <= 4096, "Integer battery source nesting is excessive")
        elif token in {"op:}", "op:%>"}:
            depth -= 1
            require(depth >= 0, "Integer battery source has an unmatched closing brace")
    require(depth == 0 and len(definitions) == 1,
            "Integer battery needs exactly one balanced original function definition")
    require(all(signature == definitions[0][:6] for signature in prototypes),
            "Integer battery prototype differs from its original function signature")
    return definitions[0]


def _validate_integer_battery_record(document: dict, record: dict, block: dict) -> None:
    fields = {"activity", "kind", "libraryPath", "function", "testPath", "target",
              "publishedResult", "failureExitCode", "cases"}
    require(fields <= set(record) <= fields | {"prerequisite"} and
            record["kind"] == "cmake-int-unary-v1",
            "Unknown or missing integer CMake battery declaration field")
    require(type(block) is dict and block.get("type") == "cpp_compiler" and
            block.get("language", "cpp") == "cpp" and
            type(block.get("cmake")) is dict and
            type(block.get("projectFiles")) is list and bool(block["projectFiles"]) and
            not any(field in block for field in ("outputCheck", "compileCheck", "sourceFiles")),
            "An integer battery must own an unevaluated C++ CMake editor")
    for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                  "compilerNamedRuleChecks", "compilerCmakeGraphChecks",
                  "compilerReductionChecks"):
        declarations = document.get(field)
        require(type(declarations) is not list or
                not any(type(item) is dict and item.get("activity") == record["activity"]
                        for item in declarations),
                "Integer battery owner conflicts with another compiler objective")
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,127}\Z")
    path_pattern = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9_./-]{0,255}\Z")

    def target(value: object) -> bool:
        return (type(value) is str and bool(identifier.fullmatch(value)) and
                not value.startswith(("lgaed_boolean_battery_", "lgaed_integer_battery_")))

    def int32(value: object) -> bool:
        return type(value) is int and -(2 ** 31) <= value < 2 ** 31

    def stream(value: object) -> bool:
        if type(value) is not str or "\x00" in value:
            return False
        try:
            return len(value.encode("utf-8")) <= 4 * 1024 * 1024
        except UnicodeError:
            return False

    paths = record["libraryPath"], record["testPath"]
    require(all(type(path) is str and path_pattern.fullmatch(path) and
                path.endswith(".cpp") and ".." not in path and "\\" not in path
                for path in paths) and paths[0] != paths[1],
            "Integer battery needs distinct safe existing C++ library and test paths")
    files = block["projectFiles"]
    require(1 <= len(files) <= 128 and
            all(type(entry) is dict and type(entry.get("path")) is str and
                type(entry.get("initialCode")) is str for entry in files),
            "Integer battery requires the original complete CMake project files")
    by_name = {entry["path"]: entry["initialCode"] for entry in files}
    ascii_lower = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
    require(len({name.translate(ascii_lower) for name in by_name}) == len(files) and
            all(path in by_name for path in (*paths, "CMakeLists.txt")),
            "Integer battery source, test or build description is missing or repeated")
    try:
        require(sum(len(code.encode("utf-8")) for code in by_name.values()) <= 8 * 1024 * 1024,
                "Integer battery CMake project exceeds its original size limit")
    except UnicodeError as exc:
        raise LayoutError("Integer battery project text must be valid UTF-8") from exc
    _integer_battery_definition(by_name[paths[0]], record["function"])
    require(target(record["target"]), "Integer battery target must be a safe identifier")
    require(type(record["publishedResult"]) is str and
            record["publishedResult"] in {"success", "failure"},
            "Integer battery published result must be success or failure")
    failure_exit = record["failureExitCode"]
    require((type(failure_exit) is int and 1 <= failure_exit <= 0xFFFFFFFF) or
            (type(failure_exit) is str and failure_exit == "nonzero"),
            "Integer battery failure exit must be a nonzero DWORD or the exact nonzero policy")
    cases = record["cases"]
    require(type(cases) is list and 1 <= len(cases) <= 32,
            "Integer battery requires 1..32 independently declared cases")
    seen = set()
    for case in cases:
        require(type(case) is dict and set(case) == {"input", "expected"} and
                int32(case["input"]) and int32(case["expected"]),
                "Integer battery cases require signed C++ integers")
        require(case["input"] not in seen, "Duplicate integer battery input")
        seen.add(case["input"])
    initial_target = block["cmake"].get("target")
    if "prerequisite" not in record:
        require(initial_target == record["target"],
                "An integer battery without a prerequisite must retain its initial target")
        return
    prerequisite = record["prerequisite"]
    require(type(prerequisite) is dict and set(prerequisite) == {
        "target", "calls", "standardInput", "standardOutput", "standardError", "exitCode"},
        "Integer battery prerequisite needs its exact client contract")
    require(target(prerequisite["target"]) and prerequisite["target"] != record["target"] and
            initial_target in (record["target"], prerequisite["target"]),
            "Integer battery prerequisite needs a distinct target and the published selection")
    require(type(prerequisite["exitCode"]) is int and prerequisite["exitCode"] == 0 and
            all(stream(prerequisite[field]) for field in
                ("standardInput", "standardOutput", "standardError")) and
            prerequisite["standardInput"] == block.get("initialStandardInput", ""),
            "Integer battery prerequisite requires bounded exact streams, original input and integer exit zero")
    calls = prerequisite["calls"]
    require(type(calls) is list and 1 <= len(calls) <= 32,
            "Integer battery prerequisite requires 1..32 ordered client calls")
    for call in calls:
        require(type(call) is dict and set(call) == {"input", "result"} and
                int32(call["input"]) and int32(call["result"]),
                "Integer battery client observations require signed C++ integers")


def _adjustment_project_header_valid(source: str, function: str) -> bool:
    """The supplied interface is a declaration, never an inline implementation."""
    if type(source) is not str or "\x00" in source or "\\\n" in source or "\\\r\n" in source:
        return False
    try:
        if not 0 < len(source.encode("utf-8")) <= 4 * 1024 * 1024:
            return False
        tokens = tuple(token for token in code_format._cpp_tokens(source)
                       if not token.startswith(("comment:", "directive:")))
    except (UnicodeError, code_format.SourceFormatError):
        return False
    if tokens[:3] != ("id:bool", "id:" + function, "op:("):
        return False
    position = 3
    for index in range(4):
        if tokens[position:position + 1] != ("id:int",):
            return False
        position += 1
        if index == 3:
            if tokens[position:position + 1] != ("op:&",):
                return False
            position += 1
        if position < len(tokens) and tokens[position].startswith("id:"):
            position += 1
        delimiter = "op:)" if index == 3 else "op:,"
        if tokens[position:position + 1] != (delimiter,):
            return False
        position += 1
    return tokens[position:] == ("op:;",)


def _validate_adjustment_project_record(document: dict, record: dict, block: dict) -> None:
    fields = {"activity", "kind", "libraryPath", "headerPath", "header", "function",
              "libraryTarget", "clientPath", "clientTarget", "testPath", "target"}
    require(set(record) == fields and record["kind"] == "cmake-adjustment-project-v1",
            "Unknown or missing adjustment construction declaration field")
    require(type(block) is dict and block.get("type") == "cpp_compiler" and
            block.get("language", "cpp") == "cpp" and
            type(block.get("cmake")) is dict and
            type(block.get("projectFiles")) is list and bool(block["projectFiles"]) and
            not any(field in block for field in ("outputCheck", "compileCheck", "sourceFiles")),
            "Adjustment construction must own an unevaluated C++ CMake editor")
    for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                  "compilerNamedRuleChecks", "compilerReductionChecks"):
        declarations = document.get(field)
        require(type(declarations) is not list or
                not any(type(item) is dict and item.get("activity") == record["activity"]
                        for item in declarations),
                "Adjustment construction owner conflicts with another compiler objective")

    def safe_path(path: object) -> bool:
        return (type(path) is str and 0 < len(path) <= 256 and
                re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_./-]*", path) and
                all(part not in ("", ".", "..") for part in path.split("/")))

    paths = (record["libraryPath"], record["headerPath"], record["clientPath"], record["testPath"])
    require(all(safe_path(path) for path in (*paths, record["header"])) and
            len({path.lower() for path in paths}) == 4 and
            all(record[field].endswith((".cpp", ".cc", ".cxx"))
                for field in ("libraryPath", "clientPath", "testPath")) and
            record["headerPath"].endswith((".h", ".hpp", ".hxx")) and
            record["headerPath"].endswith("/" + record["header"]) and
            len(record["headerPath"]) > len(record["header"]) + 1,
            "Adjustment construction needs distinct safe C++ sources and a public header")
    include_root = record["headerPath"][:-len(record["header"]) - 1]
    require(all(type(record[field]) is str and
                re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]{0,127}", record[field]) and
                record[field] != "main" and not record[field].startswith("lgaed_adjustment_")
                for field in ("function", "libraryTarget", "clientTarget", "target")) and
            len({record[field] for field in ("libraryTarget", "clientTarget", "target")}) == 3 and
            block["cmake"].get("target") == record["clientTarget"],
            "Adjustment construction needs safe distinct targets and the original client selection")
    files = block["projectFiles"]
    require(1 <= len(files) <= 128 and
            all(type(entry) is dict and type(entry.get("path")) is str and
                type(entry.get("initialCode")) is str for entry in files),
            "Adjustment construction requires its original complete project files")
    by_name = {entry["path"]: entry["initialCode"] for entry in files}
    require(len({name.lower() for name in by_name}) == len(files) and
            all(path in by_name for path in (*paths, "CMakeLists.txt")),
            "Adjustment construction source or build description is missing or repeated")
    try:
        require(sum(len(code.encode("utf-8")) for code in by_name.values()) <= 8 * 1024 * 1024,
                "Adjustment construction project exceeds its original size limit")
    except UnicodeError as exc:
        raise LayoutError("Adjustment construction project must be valid UTF-8") from exc
    require(_adjustment_project_header_valid(by_name[record["headerPath"]], record["function"]),
            "Adjustment construction needs its original bool(int,int,int,int&) declaration")
    client_input = block.get("initialStandardInput", "")
    require(type(client_input) is str and len(client_input) <= 4096 and
            re.fullmatch(r"[ \t\r\n\v\f+\-0-9]*", client_input) is not None,
            "Adjustment construction client input must contain bounded integer triples")
    inputs, position = [], 0
    while position < len(client_input):
        if client_input[position] in " \t\r\n\v\f":
            position += 1
            continue
        number = re.match(r"[+-]?[0-9]+", client_input[position:])
        require(number is not None,
                "Adjustment construction client input contains a malformed signed integer")
        inputs.append(int(number.group()))
        position += len(number.group())
    require(len(inputs) <= 24 and len(inputs) % 3 == 0 and
            all(-(2 ** 31) <= value < 2 ** 31 for value in inputs),
            "Adjustment construction client input must contain at most eight signed integer triples")

    graphs = document.get("compilerCmakeGraphChecks")
    require(type(graphs) is list and
            sum(type(item) is dict and item.get("activity") == record["activity"]
                for item in graphs) == 1,
            "Adjustment construction requires exactly one shared CMake graph")
    graph = next(item for item in graphs if type(item) is dict and item.get("activity") == record["activity"])
    minimum = graph.get("minimumCmakeVersion")
    require(type(minimum) is str and len(minimum) <= 40 and
            re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}", minimum) is not None and
            all(int(part) <= 0xFFFFFFFF for part in minimum.split(".")) and
            tuple(int(part) for part in minimum.split(".")) + (0,) * (4 - len(minimum.split("."))) >= (3, 20, 0, 0) and
            graph.get("minimumCmakeVersionAtLeast") is True,
            "Adjustment construction requires a CMake minimum of at least 3.20")
    targets = graph.get("targets")
    require(type(targets) is list and all(type(item) is dict for item in targets),
            "Adjustment construction needs its library and both executable targets")
    for role, path, kind in (("libraryTarget", "libraryPath", "STATIC_LIBRARY"),
                             ("clientTarget", "clientPath", "EXECUTABLE"),
                             ("target", "testPath", "EXECUTABLE")):
        matches = [item for item in targets if item.get("name") == record[role]]
        require(len(matches) == 1, "Adjustment construction has a missing or repeated CMake role")
        target = matches[0]
        require(target.get("type") == kind and target.get("sources") == [record[path]] and
                target.get("exactSources") is True and target.get("explicitCxx20") is True and
                target.get("explicitCxxExtensionsOff") is True,
                "Adjustment construction requires exact C++20 sources with extensions disabled")
        require(target.get("requireExplicitMsvcWarnings") is True,
                "Adjustment construction requires authored effective MSVC warnings")
        if role == "libraryTarget":
            roots = target.get("publicIncludeRoots")
            require(type(roots) is list and include_root in roots,
                    "Adjustment construction must publish its supplied header root")
        else:
            links = target.get("linksTo")
            require(type(links) is list and record["libraryTarget"] in links,
                    "Adjustment construction consumers must link the same library")



def _validate_compiler_indexed_unique_transfer_checks(document: dict) -> None:
    """Bind original C++ member bodies to new private ownership witnesses."""
    field="compilerIndexedUniqueTransferChecks"
    if field not in document:
        return
    meta=document.get("stateLayout")
    require(type(meta) is dict and meta.get("version")==2 and meta.get("locale")=="es",
            "Ownership probes require an existing Spanish v2 lesson")
    entries=document[field]
    require(type(entries) is list and 0<len(entries)<=32,
            "Ownership probes require 1..32 owners")
    required={"activity","signature","className","firstMember","secondMember",
              "transferMethod","putMethod","changeMethod","clearMethod"}
    seen=set()
    owners=_blocks(document)
    for entry in entries:
        require(type(entry) is dict and set(entry)==required,
                "Ownership probe requires the exact nine-field declaration")
        identity=_identity(entry["activity"])
        require(identity not in seen,"Duplicate ownership probe owner")
        seen.add(identity)
        owner=owners.get(identity)
        require(type(owner) is dict and owner.get("type")=="cpp_compiler"
                and type(owner.get("outputCheck")) is dict
                and type(owner["outputCheck"].get("cases")) is list
                and bool(owner["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"} & set(owner)),
                "Ownership probe must belong to an original single-file output editor")
        for collection in ("compilerNamedRuleChecks","compilerOpenBatteryChecks",
                           "compilerReductionChecks","compilerCmakeGraphChecks",
                           "compilerIntegerBatteryChecks"):
            others=document.get(collection,[])
            require(type(others) is list and not any(type(x) is dict and
                x.get("activity")==identity for x in others),
                "Ownership probe conflicts with an existing runtime objective")
        source=owner.get("initialCode")
        require(type(source) is str and bool(source),
                "Ownership probe requires exact original C++ source")
        for key in required-{"activity","signature"}:
            value=entry[key]
            require(type(value) is str and len(value)<=96 and
                    re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value) and
                    not value.startswith("__") and
                    not (value.startswith("_") and len(value)>1 and value[1].isupper()),
                    "Unsafe identifier in unique-owner specification")
        require(entry["firstMember"]!=entry["secondMember"],
                "Source and destination members must be distinct")
        signature=entry["signature"]
        require(type(signature) is str and 0<len(signature)<=512 and
                "::" in signature and "(" in signature and ")" in signature and
                source.count(signature)==1 and
                entry["className"]+"::"+entry["transferMethod"]+"(" in signature,
                "Cannot locate a unique original member-body signature")
        require(source.count("class "+entry["className"]+" {")==1,
                "The original owner class is missing or ambiguous")


def _validate_compiler_defaulted_move_policy_checks(document: dict) -> None:
    """Authenticate a new private move/copy compiler policy without rewriting its editor."""
    field="compilerDefaultedMovePolicyChecks"
    if field not in document:
        return
    meta=document.get("stateLayout")
    require(type(meta) is dict and meta.get("version")==2 and meta.get("locale")=="es",
            "Defaulted move policy requires an existing Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Defaulted move policy requires 1..32 mapped owners")
    required={"activity","className","memberName","putMethod",
              "changeMethod","clearMethod"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Defaulted move policy must declare the exact original owner fields")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate defaulted move policy owner")
        seen.add(identity)
        owner=owners.get(identity)
        require(type(owner) is dict and owner.get("type")=="cpp_compiler"
                and type(owner.get("outputCheck")) is dict
                and type(owner["outputCheck"].get("cases")) is list
                and bool(owner["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(owner)),
                "Defaulted move policy requires an original single-file output editor")
        for key in ("compilerIndexedUniqueTransferChecks","compilerNamedRuleChecks",
                    "compilerOpenBatteryChecks","compilerReductionChecks",
                    "compilerCmakeGraphChecks","compilerIntegerBatteryChecks"):
            others=document.get(key,[])
            require(type(others) is list and not any(
                type(record) is dict and record.get("activity")==identity
                for record in others),
                "Defaulted move policy cannot share another private runtime owner")
        source=owner.get("initialCode")
        require(type(source) is str and bool(source),
                "Defaulted move policy requires complete original source")
        for name in required-{"activity"}:
            word=item[name]
            require(type(word) is str and 0<len(word)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",word)
                    and not word.startswith("__")
                    and not (word.startswith("_") and len(word)>1 and word[1].isupper()),
                    "Invalid identifier in defaulted move policy declaration")
        cls=item["className"]
        require(len(re.findall(r"\b(?:class|struct)\s+"+re.escape(cls)+r"\s*\{",source))==1,
                "A defaulted move policy must identify exactly one authored class")
        member=item["memberName"]
        require(re.search(r"\bstd\s*::\s*unique_ptr\s*<\s*int\s*>\s*"+
                          re.escape(member)+r"\b",source) is not None,
                "Defaulted move policy requires its original unique_ptr<int> member")

def _validate_compiler_independent_copy_assignment_checks(document: dict) -> None:
    """Validate separately authored copy-assignment policy on a stable original editor."""
    field = "compilerIndependentCopyAssignmentChecks"
    if field not in document:
        return
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and
            meta.get("locale") == "es",
            "Independent copy assignment requires an existing Spanish v2 lesson")
    checks = document[field]
    require(type(checks) is list and 0 < len(checks) <= 32,
            "Independent copy assignment requires 1..32 mapped owners")
    required = {"activity","className","memberName","parameterName",
                "hasMethod","valueMethod","accessMethod","putMethod",
                "changeMethod","clearMethod"}
    owners = _blocks(document)
    seen = set()
    for item in checks:
        require(type(item) is dict and set(item) == required,
                "Independent copy assignment requires the documented authoring fields")
        identity = _identity(item["activity"])
        require(identity not in seen, "Duplicate independent copy assignment owner")
        seen.add(identity)
        owner = owners.get(identity)
        require(type(owner) is dict and owner.get("type") == "cpp_compiler"
                and type(owner.get("outputCheck")) is dict
                and type(owner["outputCheck"].get("cases")) is list
                and bool(owner["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"} & set(owner)),
                "Independent copy assignment requires an original single-file output editor")
        for key in ("compilerIndexedUniqueTransferChecks","compilerDefaultedMovePolicyChecks",
                    "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                    "compilerReductionChecks","compilerCmakeGraphChecks",
                    "compilerIntegerBatteryChecks"):
            others = document.get(key, [])
            require(type(others) is list and not any(
                    type(record) is dict and record.get("activity") == identity
                    for record in others),
                "Independent copy assignment cannot share another private owner")
        source = owner.get("initialCode")
        require(type(source) is str and bool(source),
                "Independent copy assignment requires original C++ source")
        for name in required - {"activity"}:
            word = item[name]
            require(type(word) is str and 0 < len(word) <= 96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", word)
                    and not word.startswith("__")
                    and not (word.startswith("_") and len(word)>1 and word[1].isupper()),
                "Invalid independent copy assignment C++ identifier")
        cls = item["className"]
        member = item["memberName"]
        param = item["parameterName"]
        require(len(re.findall(r"\b(?:class|struct)\s+"+re.escape(cls)+r"\s*\{",
                               source)) == 1,
                "Independent copy assignment class missing or ambiguous")
        require(re.search(r"\bstd\s*::\s*unique_ptr\s*<\s*int\s*>\s*"+
                          re.escape(member)+r"\b",source) is not None,
                "Independent copy assignment original member is missing")
        signature = (r"\b"+re.escape(cls)+r"\s*&\s*"+re.escape(cls)+
                     r"\s*::\s*operator\s*=\s*\(\s*const\s+"+
                     re.escape(cls)+r"\s*&\s*"+re.escape(param)+r"\s*\)\s*\{")
        require(len(re.findall(signature, source)) == 1,
                "Independent copy assignment must bind one original operator body")


def _validate_compiler_move_assignment_invariant_checks(document: dict) -> None:
    """Bind a new invariant proof to the original C++23 move-assignment owner."""
    field="compilerMoveAssignmentInvariantChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Move-assignment invariant requires an existing Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Move-assignment invariant requires 1..32 mapped owners")
    required={"activity","className","memberName","labelMember",
              "parameterName","hasMethod","valueMethod","labelMethod",
              "accessMethod","invariantMethod","putMethod","changeMethod",
              "retagMethod","clearMethod"}
    owners=_blocks(document)
    seen=set()
    for entry in checks:
        require(type(entry) is dict and set(entry)==required,
                "Move-assignment invariant requires the exact documented fields")
        owner_id=_identity(entry["activity"])
        require(owner_id not in seen,"Duplicate move-assignment invariant owner")
        seen.add(owner_id)
        block=owners.get(owner_id)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Move-assignment invariant requires a published single-source output editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            items=document.get(other,[])
            require(type(items) is list and not any(
                    type(item) is dict and item.get("activity")==owner_id
                    for item in items),
                "Move-assignment invariant cannot share a private runtime owner")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Move-assignment invariant requires original C++ source")
        for key in required-{"activity"}:
            value=entry[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid move-assignment invariant identifier")
        cls=entry["className"]
        require(len(re.findall(r"\b(?:class|struct)\s+"+re.escape(cls)+r"\s*\{",
                               source))==1,
                "Move-assignment original class missing or ambiguous")
        require(re.search(r"\bstd\s*::\s*unique_ptr\s*<\s*int\s*>\s*"+
                          re.escape(entry["memberName"])+r"\b",source) is not None,
                "Move-assignment original unique owner missing")
        require(re.search(r"\bint\s+"+re.escape(entry["labelMember"])+r"\b",
                          source) is not None,
                "Move-assignment original label field missing")
        signature=(re.escape(cls)+r"\s*&\s*"+re.escape(cls)+
                   r"\s*::\s*operator\s*=\s*\(\s*"+re.escape(cls)+
                   r"\s*&&\s*"+re.escape(entry["parameterName"])+r"\s*\)\s*\{")
        require(len(re.findall(signature,source))==1,
                "Move-assignment original operator body missing or ambiguous")


def _validate_compiler_array_owner_read_checks(document: dict) -> None:
    """Attach one fresh array ownership/read proof to an unchanged v2 editor."""
    field="compilerArrayOwnerReadChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Array owner/read requires the existing Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Array owner/read requires 1..32 mapped owners")
    required={"activity","className","ownerMember","countMember",
              "quantityParameter","indexParameter","outputParameter",
              "configureMethod","readMethod","quantityMethod","accessMethod",
              "writeMethod","clearMethod","sumMethod"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Array owner/read requires exactly its documented authoring fields")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate array owner/read declaration")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Array owner/read must bind an original single-file output editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            entries=document.get(other,[])
            require(type(entries) is list and not any(
                    type(x) is dict and x.get("activity")==identity
                    for x in entries),
                "Array owner/read cannot share another private semantic owner")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Array owner/read requires an authentic original C++ source")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid array owner/read identifier")
        name=item["className"]
        require(len(re.findall(r"\bclass\s+"+re.escape(name)+r"\s*\{",
                               source))==1,
                "Array owner/read original class is missing or ambiguous")
        owner=item["ownerMember"]
        count=item["countMember"]
        require(re.search(r"\bstd\s*::\s*unique_ptr\s*<\s*int\s*\[\s*\]\s*>\s*"+
                          re.escape(owner)+r"\b",source) is not None,
                "Array owner/read original unique_ptr<int[]> missing")
        require(re.search(r"\bint\s+"+re.escape(count)+r"\b",source) is not None,
                "Array owner/read original count missing")
        config=(r"\bbool\s+"+re.escape(name)+r"\s*::\s*"+
                re.escape(item["configureMethod"])+r"\s*\(\s*int\s+"+
                re.escape(item["quantityParameter"])+r"\s*\)\s*noexcept\s*\{")
        reader=(r"\bbool\s+"+re.escape(name)+r"\s*::\s*"+
                re.escape(item["readMethod"])+r"\s*\(\s*int\s+"+
                re.escape(item["indexParameter"])+r"\s*,\s*int\s*&\s*"+
                re.escape(item["outputParameter"])+
                r"\s*\)\s*const\s+noexcept\s*\{")
        require(len(re.findall(config,source))==1 and
                len(re.findall(reader,source))==1,
                "Array owner/read requires the two original C++ method signatures")


def _validate_compiler_array_copy_move_checks(document: dict) -> None:
    """Authenticate two new assignment-body goals on one unchanged v2 editor."""
    field="compilerArrayCopyMoveChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Array copy/move requires the existing Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Array copy/move requires 1..32 original mapped owners")
    required={"activity","className","ownerMember","countMember",
              "copyParameter","moveParameter","quantityMethod",
              "accessMethod","configureMethod","readMethod",
              "writeMethod","clearMethod","sumMethod"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Array copy/move requires exactly the documented authoring fields")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate array copy/move objective owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Array copy/move must attach to an original single-file output editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            entries=document.get(other,[])
            require(type(entries) is list and not any(
                    type(x) is dict and x.get("activity")==identity
                    for x in entries),
                "Array copy/move cannot reuse an existing private probe owner")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Array copy/move requires authentic published C++ source")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid identifier in array copy/move authoring")
        name=item["className"]
        require(len(re.findall(
                    r"\b(?:class|struct)\s+"+re.escape(name)+r"\s*\{",
                    source))==1,
                "Original array class missing or ambiguous")
        require(re.search(
                    r"\bstd\s*::\s*unique_ptr\s*<\s*int\s*\[\s*\]\s*>\s*"+
                    re.escape(item["ownerMember"])+r"\b",source) is not None,
                "Original unique_ptr<int[]> member missing")
        require(re.search(r"\bint\s+"+re.escape(item["countMember"])+r"\b",
                          source) is not None,
                "Original array count field missing")
        copy=(re.escape(name)+r"\s*&\s*"+re.escape(name)+
              r"\s*::\s*operator\s*=\s*\(\s*const\s+"+
              re.escape(name)+r"\s*&\s*"+
              re.escape(item["copyParameter"])+r"\s*\)\s*\{")
        move=(re.escape(name)+r"\s*&\s*"+re.escape(name)+
              r"\s*::\s*operator\s*=\s*\(\s*"+
              re.escape(name)+r"\s*&&\s*"+
              re.escape(item["moveParameter"])+r"\s*\)\s*noexcept\s*\{")
        require(len(re.findall(copy,source))==1 and
                len(re.findall(move,source))==1,
                "Array copy/move requires the two original distinct operator bodies")


def _validate_compiler_array_return_take_checks(document: dict) -> None:
    """Authenticate two global-function bodies on the existing v2 compiler owner."""
    field="compilerArrayReturnTakeChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Array return/take requires the original Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Array return/take requires 1..32 original compiler owners")
    required={"activity","className","ownerMember","countMember",
              "copyFunction","takeFunction","sourceParameter","indexParameter",
              "valueParameter","destinationParameter","offeredParameter",
              "limitParameter","quantityMethod","accessMethod",
              "configureMethod","readMethod","writeMethod","clearMethod"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Array return/take requires exactly the declared original symbols")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate array return/take owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Array return/take must bind a historical single-source output editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            others=document.get(other,[])
            require(type(others) is list and not any(
                    type(x) is dict and x.get("activity")==identity
                    for x in others),
                "Array return/take must not share a different private compiler owner")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Array return/take requires the complete original C++ source")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid identifier in array return/take authoring")
        name=item["className"]
        require(len(re.findall(
                    r"\b(?:class|struct)\s+"+re.escape(name)+r"\s*\{",
                    source))==1,
                "Array return/take original class missing or ambiguous")
        require(re.search(
                    r"\bstd\s*::\s*unique_ptr\s*<\s*int\s*\[\s*\]\s*>\s*"+
                    re.escape(item["ownerMember"])+r"\b",source) is not None,
                "Array return/take original unique_ptr<int[]> member missing")
        require(re.search(r"\bint\s+"+re.escape(item["countMember"])+r"\b",
                          source) is not None,
                "Array return/take original count field missing")
        copy=(re.escape(name)+r"\s+"+re.escape(item["copyFunction"])+
              r"\s*\(\s*const\s+"+re.escape(name)+r"\s*&\s*"+
              re.escape(item["sourceParameter"])+r"\s*,\s*int\s+"+
              re.escape(item["indexParameter"])+r"\s*,\s*int\s+"+
              re.escape(item["valueParameter"])+r"\s*\)\s*\{")
        take=(r"\bbool\s+"+re.escape(item["takeFunction"])+
              r"\s*\(\s*"+re.escape(name)+r"\s*&\s*"+
              re.escape(item["destinationParameter"])+r"\s*,\s*"+
              re.escape(name)+r"\s*&&\s*"+
              re.escape(item["offeredParameter"])+r"\s*,\s*int\s+"+
              re.escape(item["limitParameter"])+r"\s*\)\s*noexcept\s*\{")
        require(len(re.findall(copy,source))==1 and
                len(re.findall(take,source))==1,
                "Array return/take requires both original external function signatures")



def _validate_compiler_polymorphic_destruction_checks(document: dict) -> None:
    """Authenticate three editable function bodies on a published C++ editor.

    The root contract adds a new semantic goal without changing its original
    source, cases, state-layout identities or historical output achievements.
    """
    field="compilerPolymorphicDestructionChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Polymorphic destruction requires a Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Polymorphic destruction requires 1..32 original editors")
    required={"activity","baseClass","derivedClass","cycleClass",
              "ownedClass","ownedMember","factoryFunction","writeMethod",
              "numberParameter","codeParameter","cycleParameter",
              "baseAlive","derivedAlive","ownedAlive",
              "baseDestroyed","derivedDestroyed","ownedDestroyed"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Polymorphic destruction has unknown or missing symbols")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate polymorphic destructor owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Polymorphic destructor proof requires one original C++ editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerArrayReturnTakeChecks","compilerNamedRuleChecks",
                      "compilerOpenBatteryChecks","compilerReductionChecks",
                      "compilerCmakeGraphChecks","compilerIntegerBatteryChecks"):
            others=document.get(other,[])
            require(type(others) is list and not any(
                    type(x) is dict and x.get("activity")==identity for x in others),
                    "Polymorphic destruction must not reuse another private editor")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Polymorphic destruction requires the published complete source")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                    "Invalid polymorphic destructor source identifier")
        base=item["baseClass"]
        derived=item["derivedClass"]
        cycle=item["cycleClass"]
        owner=item["ownedClass"]
        member=item["ownedMember"]
        require(len(re.findall(r"\bclass\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(derived)+r"\s*:\s*public\s+"+
                                   re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bstruct\s+"+re.escape(cycle)+r"\s*\{",source))==1,
                "Polymorphic destruction must preserve a real published hierarchy")
        require("virtual ~"+base+"() noexcept;" in source and
                "~"+derived+"() noexcept override;" in source and
                re.search(r"\bstd\s*::\s*unique_ptr\s*<\s*"+
                          re.escape(owner)+r"\s*>\s*"+re.escape(member)+r"\b",
                          source) is not None,
                "Original virtual lifetime or derived owner member missing")
        require(re.search(r"\b(?:class|struct)\s+"+re.escape(owner)+r"\s*\{",
                          source) is not None and
                re.search(r"\b(?:virtual\s+)?void\s+"+
                          re.escape(item["writeMethod"])+r"\s*\(",
                          source) is not None,
                "Declared owner type or stream-reporting method is absent")
        cycle_declaration=re.search(r"\bstruct\s+"+re.escape(cycle)+
                                    r"\s*\{([^{}]*)\};",source,re.DOTALL)
        require(cycle_declaration is not None,
                "Original cycle record definition is missing or ambiguous")
        for property_name in ("baseAlive","derivedAlive","ownedAlive",
                              "baseDestroyed","derivedDestroyed","ownedDestroyed"):
            require(re.search(r"\bint\s+"+re.escape(item[property_name])+
                              r"\b",cycle_declaration.group(1)) is not None,
                    "Missing original lifetime counter field: "+property_name)
        factory=("std::unique_ptr<"+base+"> "+item["factoryFunction"]+
                 "(int "+item["numberParameter"]+
                 ", int "+item["codeParameter"]+
                 ", "+cycle+"& "+item["cycleParameter"]+")")
        require(source.count(base+"::~"+base+"() noexcept {")==1 and
                source.count(derived+"::~"+derived+"() noexcept {")==1 and
                source.count(factory+" {")==1,
                "Polymorphic destruction needs precisely three original bodies")



def _validate_compiler_virtual_lifetime_dispatch_checks(document: dict) -> None:
    """Bind two published C++ member bodies to an independent dispatch probe."""
    field="compilerVirtualLifetimeDispatchChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Virtual lifetime dispatch requires a Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Virtual lifetime dispatch requires 1..32 existing editors")
    required={"activity","baseClass","derivedClass","registerMethod",
              "writeMethod","phaseParameter","registerStream","writeStream"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Virtual lifetime dispatch requires its exact authoring symbols")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate virtual lifetime dispatch owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Virtual lifetime dispatch needs an original single-source editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerArrayReturnTakeChecks",
                      "compilerPolymorphicDestructionChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            others=document.get(other,[])
            require(type(others) is list and not any(
                    type(x) is dict and x.get("activity")==identity for x in others),
                "Virtual lifetime dispatch cannot share another private editor")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Virtual lifetime dispatch requires original full C++ source")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid virtual dispatch authoring identifier")
        base=item["baseClass"]
        derived=item["derivedClass"]
        register=item["registerMethod"]
        write=item["writeMethod"]
        phase=item["phaseParameter"]
        stream=item["registerStream"]
        derived_stream=item["writeStream"]
        require(len(re.findall(r"\bclass\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(derived)+
                                   r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",
                                   source))==1
                and re.search(r"\bvirtual\s+~"+re.escape(base)+r"\s*\(",source)
                    is not None,
                "Published virtual base and concrete derived class are missing")
        require(re.search(r"\bvirtual\s+void\s+"+re.escape(write)+
                          r"\s*\(\s*std\s*::\s*ostream\s*&",source) is not None
                and re.search(r"\bvoid\s+"+re.escape(write)+
                          r"\s*\(\s*std\s*::\s*ostream\s*&\s*"+
                          re.escape(derived_stream)+r"\s*\)\s*const\s+override\s*;",
                          source) is not None,
                "Original virtual reporting interface or derived override missing")
        register_signature=("void "+base+"::"+register+"(const char* "+
                            phase+", std::ostream& "+stream+") const {")
        write_signature=("void "+derived+"::"+write+"(std::ostream& "+
                         derived_stream+") const {")
        require(source.count(register_signature)==1 and
                source.count(write_signature)==1,
                "Virtual lifetime dispatch needs both original member bodies")



def _validate_compiler_derived_preparation_checks(document: dict) -> None:
    """Authenticate a derived-owner replacement goal without editing its editor."""
    field="compilerDerivedPreparationChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Derived preparation requires an original Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Derived preparation requires 1..32 published compiler owners")
    required={"activity","baseClass","derivedClass","balanceClass",
              "resourceClass","replacementFunction","destinationParameter",
              "numberParameter","codeParameter","modeParameter",
              "balanceParameter","printMethod","resourceAlive",
              "baseCompleted","derivedCompleted","resourceCompleted",
              "baseDestroyed","derivedDestroyed","resourceDestroyed"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Derived preparation requires exactly its declared identifiers")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate derived preparation owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Derived preparation must bind a published single-source editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerArrayReturnTakeChecks",
                      "compilerPolymorphicDestructionChecks",
                      "compilerVirtualLifetimeDispatchChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            others=document.get(other,[])
            require(type(others) is list and not any(
                type(x) is dict and x.get("activity")==identity for x in others),
                "Derived preparation cannot share another private compiler owner")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Derived preparation requires complete original C++ source")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid derived preparation identifier")
        base=item["baseClass"]
        derived=item["derivedClass"]
        balance=item["balanceClass"]
        resource=item["resourceClass"]
        require(len(re.findall(r"\bclass\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(derived)+
                    r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(resource)+r"\s*\{",
                                   source))==1,
                "Original base/derived/resource types missing or ambiguous")
        balance_type=re.search(r"\bstruct\s+"+re.escape(balance)+
                               r"\s*\{([^{}]*)\};",source,re.DOTALL)
        require(balance_type is not None,
                "Original lifetime balance struct is missing or ambiguous")
        for key in ("resourceAlive","baseCompleted","derivedCompleted",
                    "resourceCompleted","baseDestroyed",
                    "derivedDestroyed","resourceDestroyed"):
            require(re.search(r"\bint\s+"+re.escape(item[key])+r"\b",
                              balance_type.group(1)) is not None,
                "Derived preparation lifetime counter missing: "+key)
        require("virtual ~"+base+"() noexcept" in source and
                "~"+derived+"() noexcept override" in source and
                len(re.findall(r"\bstd\s*::\s*unique_ptr\s*<\s*"+
                               re.escape(resource)+r"\s*>",source))>=2 and
                re.search(r"\bvoid\s+"+re.escape(item["printMethod"])+
                          r"\s*\(\s*std\s*::\s*ostream\s*&",source) is not None,
                "Original unique owners or virtual destructor missing")
        signature=("void "+item["replacementFunction"]+
            "(std::unique_ptr<"+base+">& "+item["destinationParameter"]+
            ", int "+item["numberParameter"]+", int "+item["codeParameter"]+
            ", int "+item["modeParameter"]+", "+balance+"& "+
            item["balanceParameter"]+") {")
        require(source.count(signature)==1,
                "Derived preparation must bind one original free function body")





def _validate_compiler_polymorphic_clone_checks(document: dict) -> None:
    """Authorize three clone bodies without changing saved stdout contracts."""
    field="compilerPolymorphicCloneChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Polymorphic cloning requires a Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Polymorphic cloning requires 1..32 original compiler editors")
    required={"activity","baseClass","simpleClass","identifiedClass",
              "vectorAlias","cloneMethod","duplicateFunction",
              "numberMethod","changeMethod","writeMethod","codeMethod",
              "sourceParameter","streamParameter"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Polymorphic cloning needs exact author-declared identifiers")
        identity=_identity(item["activity"])
        require(identity not in seen,"Duplicate polymorphic cloning owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&
                         set(block)),
                "Polymorphic cloning requires an original single-file editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerArrayReturnTakeChecks",
                      "compilerPolymorphicDestructionChecks",
                      "compilerVirtualLifetimeDispatchChecks",
                      "compilerDerivedPreparationChecks",
                      "compilerAbstractInterfaceChecks",
                      "compilerInterfaceCollectionChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            entries=document.get(other,[])
            require(type(entries) is list and not any(
                type(x) is dict and x.get("activity")==identity for x in entries),
                "Polymorphic cloning cannot reuse another compiler proof owner")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and
                             value[1].isupper()),
                    "Invalid polymorphic cloning identifier")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Cloning requires an authentic published initial source")
        base=item["baseClass"]
        simple=item["simpleClass"]
        identified=item["identifiedClass"]
        vector=item["vectorAlias"]
        clone=item["cloneMethod"]
        duplicate=item["duplicateFunction"]
        number=item["numberMethod"]
        change=item["changeMethod"]
        write=item["writeMethod"]
        stream=item["streamParameter"]
        require(len(re.findall(r"\bclass\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(simple)+
                       r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",
                       source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(identified)+
                       r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",
                       source))==1,
                "Polymorphic cloning requires original public class hierarchy")
        require("virtual ~"+base+"() = default;" in source and
                "virtual std::unique_ptr<"+base+"> "+clone+
                    "() const = 0;" in source and
                "virtual int "+number+"() const noexcept = 0;" in source and
                "virtual void "+change+"(int valor) noexcept = 0;" in source and
                "virtual void "+write+"(std::ostream& "+stream+
                    ") const = 0;" in source,
                "Polymorphic cloning requires a valid abstract base")
        alias="using "+vector+" = std::vector<std::unique_ptr<"+base+">>;"
        implementation=("std::unique_ptr<"+base+"> "+clone+
                        "() const override {")
        duplicate_source=(vector+" "+duplicate+"(const "+vector+"& "+
                          item["sourceParameter"]+") {")
        require(source.count(alias)==1 and source.count(implementation)==2
                and source.count(duplicate_source)==1 and
                source.index(alias)<source.index(duplicate_source),
                "Polymorphic cloning requires two concrete bodies and one consumer")
        require(len(re.findall(r"\bint\s+"+re.escape(item["codeMethod"])+
                   r"\s*\(\s*\)\s*const\s+noexcept",source))==1,
                "Polymorphic cloning lost the concrete code getter")


def _validate_compiler_interface_collection_checks(document: dict) -> None:
    """Author-only ownership transfer and virtual reporting, without source edits."""
    field="compilerInterfaceCollectionChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Interface collection authoring requires a Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Interface collection requires 1..32 compiler owners")
    required={"activity","baseClass","simpleClass","identifiedClass",
              "vectorAlias","addFunction","reportFunction","writeMethod",
              "numberMethod","destinationParameter","sourceParameter",
              "dataParameter","streamParameter","countLabel",
              "indexSeparator","numberLabel","codeLabel"}
    identifiers=required-{"activity","countLabel","indexSeparator",
                           "numberLabel","codeLabel"}
    labels={"countLabel","indexSeparator","numberLabel","codeLabel"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Interface collection requires exact declarative symbols")
        identity=_identity(item["activity"])
        require(identity not in seen,"Repeated interface collection owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&
                         set(block)),
                "Interface collection needs the published single-file editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerArrayReturnTakeChecks",
                      "compilerPolymorphicDestructionChecks",
                      "compilerVirtualLifetimeDispatchChecks",
                      "compilerDerivedPreparationChecks",
                      "compilerAbstractInterfaceChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            entries=document.get(other,[])
            require(type(entries) is list and not any(
                type(x) is dict and x.get("activity")==identity for x in entries),
                "Interface collection cannot overlap another private compiler owner")
        for key in identifiers:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and
                             value[1].isupper()),
                    "Invalid interface collection identifier")
        for key in labels:
            value=item[key]
            require(type(value) is str and 0<len(value)<=64
                    and all(32<=ord(ch)<=126 and ch not in '"\\'
                            for ch in value),
                    "Invalid interface collection output label")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Interface collection requires original C++ source")
        base=item["baseClass"]
        simple=item["simpleClass"]
        identified=item["identifiedClass"]
        vector=item["vectorAlias"]
        number=item["numberMethod"]
        write=item["writeMethod"]
        stream=item["streamParameter"]
        require(len(re.findall(r"\bclass\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(simple)+
                       r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",
                       source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(identified)+
                       r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",
                       source))==1,
                "Interface collection types or public inheritance missing")
        require("virtual ~"+base+"() = default;" in source and
                "virtual int "+number+"() const noexcept = 0;" in source and
                "virtual void "+write+"(std::ostream& "+stream+
                    ") const = 0;" in source,
                "Interface collection requires an actual abstract interface")
        alias="using "+vector+" = std::vector<std::unique_ptr<"+base+">>;"
        add=("bool "+item["addFunction"]+"("+vector+"& "+
             item["destinationParameter"]+", std::unique_ptr<"+base+
             ">& "+item["sourceParameter"]+") {")
        report=("void "+item["reportFunction"]+"(const "+vector+"& "+
                item["dataParameter"]+", std::ostream& "+stream+") {")
        require(source.count(alias)==1 and source.count(add)==1 and
                source.count(report)==1 and
                source.index(alias)<source.index(add)<source.index(report),
                "Interface collection needs the original alias and two editable bodies")


def _validate_compiler_abstract_interface_checks(document: dict) -> None:
    """Four immutable-source-bounded bodies of a published abstract interface."""
    field="compilerAbstractInterfaceChecks"
    if field not in document:
        return
    layout=document.get("stateLayout")
    require(type(layout) is dict and layout.get("version")==2 and
            layout.get("locale")=="es",
            "Abstract interface authoring requires the original Spanish v2 lesson")
    checks=document[field]
    require(type(checks) is list and 0<len(checks)<=32,
            "Abstract interface proof requires 1..32 existing compiler editors")
    required={"activity","baseClass","simpleClass","identifiedClass",
              "createFunction","presentFunction","numberMethod","codeMethod",
              "writeMethod","streamParameter","typeParameter",
              "numberParameter","codeParameter","objectParameter"}
    owners=_blocks(document)
    seen=set()
    for item in checks:
        require(type(item) is dict and set(item)==required,
                "Abstract interface authoring requires its exact symbols")
        identity=_identity(item["activity"])
        require(identity not in seen,"Repeated abstract interface editor owner")
        seen.add(identity)
        block=owners.get(identity)
        require(type(block) is dict and block.get("type")=="cpp_compiler"
                and type(block.get("outputCheck")) is dict
                and type(block["outputCheck"].get("cases")) is list
                and bool(block["outputCheck"]["cases"])
                and not ({"compileCheck","cmake","projectFiles","sourceFiles"}&set(block)),
                "Abstract interface proof needs the published single-file editor")
        for other in ("compilerIndexedUniqueTransferChecks",
                      "compilerDefaultedMovePolicyChecks",
                      "compilerIndependentCopyAssignmentChecks",
                      "compilerMoveAssignmentInvariantChecks",
                      "compilerArrayOwnerReadChecks","compilerArrayCopyMoveChecks",
                      "compilerArrayReturnTakeChecks",
                      "compilerPolymorphicDestructionChecks",
                      "compilerVirtualLifetimeDispatchChecks",
                      "compilerDerivedPreparationChecks",
                      "compilerNamedRuleChecks","compilerOpenBatteryChecks",
                      "compilerReductionChecks","compilerCmakeGraphChecks",
                      "compilerIntegerBatteryChecks"):
            others=document.get(other,[])
            require(type(others) is list and not any(
                type(x) is dict and x.get("activity")==identity for x in others),
                "Abstract interface cannot overlap another private compiler owner")
        source=block.get("initialCode")
        require(type(source) is str and bool(source),
                "Abstract interface requires the complete original C++ file")
        for key in required-{"activity"}:
            value=item[key]
            require(type(value) is str and 0<len(value)<=96
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",value)
                    and not value.startswith("__")
                    and not (len(value)>1 and value[0]=="_" and value[1].isupper()),
                "Invalid abstract interface authoring identifier")
        base=item["baseClass"]
        simple=item["simpleClass"]
        identified=item["identifiedClass"]
        number=item["numberMethod"]
        code=item["codeMethod"]
        write=item["writeMethod"]
        stream=item["streamParameter"]
        require(len(re.findall(r"\bclass\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(simple)+
                    r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",source))==1
                and len(re.findall(r"\bclass\s+"+re.escape(identified)+
                    r"\s*:\s*public\s+"+re.escape(base)+r"\s*\{",source))==1,
                "Abstract interface or public derived types missing")
        require("virtual ~"+base+"() = default;" in source and
                "virtual int "+number+"() const noexcept = 0;" in source and
                "virtual void "+write+"(std::ostream& "+stream+
                    ") const = 0;" in source,
                "Abstract interface lost a pure virtual operation or destructor")
        require(len(re.findall(r"\bint\s+"+re.escape(number)+
                   r"\s*\(\s*\)\s*const\s+noexcept\s+override",source))==2 and
                len(re.findall(r"\bvoid\s+"+re.escape(write)+
                   r"\s*\(\s*std\s*::\s*ostream\s*&\s*"+
                   re.escape(stream)+r"\s*\)\s*const\s+override\s*\{",source))==2,
                "Both concrete virtual implementations are required")
        require(len(re.findall(r"\bint\s+"+re.escape(code)+
                   r"\s*\(\s*\)\s*const\s+noexcept",source))==1,
                "Derived code getter is missing")
        factory=("std::unique_ptr<"+base+"> "+item["createFunction"]+
            "(int "+item["typeParameter"]+", int "+item["numberParameter"]+
            ", int "+item["codeParameter"]+") {")
        consumer=("void "+item["presentFunction"]+"(const "+base+"& "+
            item["objectParameter"]+", std::ostream& "+stream+") {")
        require(source.count(factory)==1 and source.count(consumer)==1,
                "Factory and polymorphic consumer must retain their original signatures")


def _validate_compiler_open_battery_checks(document: dict) -> None:
    """Authenticate open battery goals without changing published exercise bytes.

    Typed goals declare independent boolean or integer expectations. Legacy
    CMake goals keep their original fields and authenticated test calls.
    """
    if "compilerOpenBatteryChecks" not in document:
        return
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and
            meta.get("locale") == "es",
            "Open battery objectives require a Spanish v2 lesson")
    checks = document["compilerOpenBatteryChecks"]
    require(type(checks) is list and 1 <= len(checks) <= 32,
            "Open battery declarations require 1..32 owners")
    owners, identities = _blocks(document), set()
    identifier = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")
    path_pattern = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*\Z")
    signed_min, signed_max = -(2 ** 31), 2 ** 31 - 1
    for record in checks:
        require(type(record) is dict, "Open battery declaration must be an object")
        if "kind" in record:
            require(record.get("kind") in ("boolean-pair-v1", "cmake-int-unary-v1", "cmake-adjustment-project-v1") and
                    "activity" in record, "Unknown or missing open battery kind or owner")
            identity = _identity(record["activity"])
            require(identity not in identities, "Duplicate open battery owner")
            identities.add(identity)
            if record["kind"] == "boolean-pair-v1":
                _validate_boolean_battery_record(document, record, owners.get(identity))
            elif record["kind"] == "cmake-int-unary-v1":
                _validate_integer_battery_record(document, record, owners.get(identity))
            else:
                _validate_adjustment_project_record(document, record, owners.get(identity))
            continue
        require(set(record) == {
            "activity", "library", "test", "function", "comparison", "target",
            "originalCases"}, "Unknown or missing CMake battery declaration field")
        identity = _identity(record["activity"])
        require(identity not in identities, "Duplicate open CMake battery owner")
        identities.add(identity)
        block = owners.get(identity)
        require(type(block) is dict and block.get("type") == "cpp_compiler"
                and "outputCheck" not in block and "compileCheck" not in block
                and type(block.get("cmake")) is dict
                and type(block.get("projectFiles")) is list
                and not block.get("sourceFiles"),
                "A new open battery must own an existing unevaluated CMake editor")
        names = (record["library"], record["test"])
        require(all(type(name) is str and name.endswith(".cpp") and
                    len(name) <= 256 and path_pattern.fullmatch(name)
                    and ".." not in name and "\\" not in name
                    for name in names) and names[0] != names[1],
                "Open battery needs two different safe existing C++ files")
        for field in ("function", "comparison", "target"):
            value = record[field]
            require(type(value) is str and len(value) <= 128 and
                    identifier.fullmatch(value),
                    "Open battery names must be valid C++ identifiers")
        require(record["target"] == block["cmake"].get("target"),
                "CMake battery target differs from the executable being checked")
        files = block["projectFiles"]
        by_name = {entry.get("path"): entry.get("initialCode") for entry in files
                   if type(entry) is dict and type(entry.get("path")) is str}
        require(len(by_name) == len(files) and
                names[0] in by_name and names[1] in by_name and
                "CMakeLists.txt" in by_name and
                all(type(by_name[name]) is str for name in (*names, "CMakeLists.txt")),
                "CMake battery source, test or build description is missing")
        cmake = by_name["CMakeLists.txt"]
        require(re.search(r"\badd_executable\s*\(\s*" +
                          re.escape(record["target"]) + r"\s+" +
                          re.escape(record["test"]) + r"\s*\)", cmake) is not None,
                "Open CMake battery must execute the authored shared-library test")
        require(re.search(r"\badd_library\s*\(", cmake) is not None and
                record["function"] in by_name[names[0]],
                "CMake battery lacks the original shared function")
        original = record["originalCases"]
        require(type(original) is list and 1 <= len(original) <= 32,
                "Open battery must describe 1..32 real original test calls")
        pattern = (r"\b" + re.escape(record["comparison"]) +
                   r"\s*\(\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)" +
                   r"\s*,\s*(-?\d+)\s*,\s*(-?\d+)" +
                   r"\s*,\s*(true|false)\s*,\s*(-?\d+)\s*\)")
        matches = list(re.finditer(pattern, by_name[names[1]]))
        require(len(matches) == len(original),
                "Original CMake case count does not match its real test source")
        # Declaration order is independent of the real driver's order.
        # Match the immutable calls by their full tuple; their case numbers
        # remain owned by the authenticated historical driver.
        seen_calls = set()
        for item in original:
            require(type(item) is dict and set(item) == {
                "value", "lower", "upper", "initial"} and
                all(type(item[name]) is int and signed_min <= item[name] <= signed_max
                    for name in ("value", "lower", "upper", "initial")),
                    "Original call fields must be representable signed C++ integers")
            call = (item["value"], item["lower"], item["upper"], item["initial"])
            require(call not in seen_calls, "Duplicate original CMake battery input")
            seen_calls.add(call)
        seen_labels, matched_calls = set(), set()
        for match in matches:
            label, value, lower, upper, initial, accepted, adjusted = match.groups()
            require(int(label) not in seen_labels and
                    all(signed_min <= int(x) <= signed_max
                        for x in (label, value, lower, upper, initial, adjusted)),
                    "Invalid original CMake battery case number or signed integer")
            seen_labels.add(int(label))
            call = tuple(map(int, (value, lower, upper, initial)))
            require(call in seen_calls and call not in matched_calls,
                    "Original case was changed, duplicated or mapped to another input")
            matched_calls.add(call)
            valid = all(-20 <= x <= 20 for x in (call[0], call[1], call[2]))
            valid = valid and call[1] <= call[2]
            expected = (call[1] - call[0] if call[0] < call[1]
                        else call[2] - call[0] if call[0] > call[2] else 0) if valid else call[3]
            require((accepted == "true") == valid and int(adjusted) == expected,
                    "Original CMake battery result contradicts its independent contract")


def _cpp_expression_repair_tokens(source: str) -> tuple[str, ...] | None:
    """Mirror cpp_expression_repair.hpp without changing the legacy lexer."""
    if type(source) is not str:
        return None
    try:
        encoded = source.encode("utf-8")
        if (not 0 < len(encoded) <= 4 * 1024 * 1024 or
                "\x00" in source or "\\\n" in source or "\\\r\n" in source):
            return None
        # Native offsets and raw delimiters count UTF-8 bytes. Its identifier
        # and pp-number scanners accept every non-ASCII byte as a continuation.
        # Mask only already classified code tokens before the second scan, so
        # Python's Unicode isalnum cannot split a native pp-number. Literal,
        # comment and directive bodies remain exact throughout both scans.
        wire = encoded.decode("latin1")

        def positions(text, tokens):
            result, offset = [], 0
            for token in tokens:
                kind, literal = token.split(":", 1)
                begin = text.find(literal, offset)
                if begin < 0 or any(c not in code_format._WS for c in text[offset:begin]):
                    raise code_format.SourceFormatError("Unattributable C++ token")
                offset = begin + len(literal)
                result.append((kind, begin, offset))
            if any(c not in code_format._WS for c in text[offset:]):
                raise code_format.SourceFormatError("Unattributable C++ suffix")
            return result

        masked = list(wire)
        for kind, begin, end in positions(wire, code_format._cpp_tokens(wire)):
            if kind in ("id", "number"):
                for at in range(begin, end):
                    if ord(wire[at]) >= 128:
                        masked[at] = "_"
            elif kind == "literal":
                literal = wire[begin:end]
                raw = next((prefix for prefix in code_format._RAW_PREFIX
                            if literal.startswith(prefix)), None)
                if raw is not None and any(c in code_format._WS
                                           for c in literal[len(raw):literal.find("(")]):
                    return None
        masked = "".join(masked)
        tokens = tuple(kind + ":" + wire[begin:end]
                       for kind, begin, end in positions(masked, code_format._cpp_tokens(masked)))
        if code_format._cpp_position_sensitive_tokens(tokens):
            return None
        return tokens
    except (UnicodeError, code_format.SourceFormatError):
        return None


def _cpp_expression_repair_include(token: str) -> bool:
    source = token.removeprefix("directive:")
    source = source[2:] if source.startswith("%:") else source[1:]
    source = source.lstrip(code_format._WS)
    if not source.startswith("include"):
        return False
    source = source[7:].lstrip(code_format._WS)
    if not source or source[0] not in '<"':
        return False
    end = source.find(">" if source[0] == "<" else '"', 1)
    if end <= 1:
        return False
    try:
        return all(token.startswith("comment:")
                   for token in code_format._cpp_tokens(source[end + 1:]))
    except code_format.SourceFormatError:
        return False


def _cpp_expression_repair_number(value: str) -> bool:
    def digit(character: str, base: int) -> bool:
        return ("0" <= character <= "9" and ord(character) - ord("0") < base or
                base == 16 and character in "abcdefABCDEF")

    clean = []
    for index, character in enumerate(value):
        if character == "'":
            if (index == 0 or index + 1 == len(value) or
                    not digit(value[index - 1], 16) or not digit(value[index + 1], 16)):
                return False
        else:
            clean.append(character)
    value = "".join(clean)
    base, at = 10, 0
    if value.startswith(("0x", "0X")):
        base, at = 16, 2
    elif value.startswith(("0b", "0B")):
        base, at = 2, 2
    first = at
    while at < len(value) and digit(value[at], base):
        at += 1
    has_digits, floating = at != first, False
    if base != 2 and at < len(value) and value[at] == ".":
        floating = True
        at += 1
        first = at
        while at < len(value) and digit(value[at], base):
            at += 1
        has_digits = has_digits or at != first
    if not has_digits:
        return False
    exponent = (at < len(value) and
                (value[at] in "pP" if base == 16 else
                 base == 10 and value[at] in "eE"))
    if exponent:
        floating = True
        at += 1
        if at < len(value) and value[at] in "+-":
            at += 1
        first = at
        while at < len(value) and digit(value[at], 10):
            at += 1
        if at == first:
            return False
    if base == 16 and floating and not exponent:
        return False
    suffix = value[at:].lower()
    return (suffix in ("", "f", "l") if floating else
            suffix in ("", "u", "l", "ul", "lu", "ll", "ull", "llu", "z", "uz", "zu"))


def _cpp_expression_repair_scalar(tokens: tuple[str, ...]) -> bool:
    """Pure scalar expressions: no statements, calls, assignment or UDLs."""
    values = tuple(token for token in tokens if not token.startswith("comment:"))
    keywords = frozenset((
        "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept "
        "auto bitand bitor bool break case catch char char8_t char16_t char32_t "
        "class compl concept const consteval constexpr constinit const_cast continue "
        "co_await co_return co_yield decltype default delete do double dynamic_cast "
        "else enum explicit export extern float for friend goto if inline "
        "int long mutable namespace new noexcept not not_eq operator or or_eq "
        "private protected public reflexpr register reinterpret_cast requires return "
        "short signed sizeof static static_assert static_cast struct switch synchronized "
        "template this thread_local throw try typedef typeid typename union unsigned "
        "using virtual void volatile wchar_t while xor xor_eq"
    ).split())
    precedence = {
        "op:||": 1, "id:or": 1, "op:&&": 2, "id:and": 2,
        "op:|": 3, "id:bitor": 3, "op:^": 4, "id:xor": 4,
        "op:&": 5, "id:bitand": 5, "op:==": 6, "op:!=": 6, "id:not_eq": 6,
        "op:<": 7, "op:<=": 7, "op:>": 7, "op:>=": 7,
        "op:<<": 8, "op:>>": 8, "op:+": 9, "op:-": 9,
        "op:*": 10, "op:/": 10, "op:%": 10,
    }
    at = 0

    def take(token: str) -> bool:
        nonlocal at
        if at == len(values) or values[at] != token:
            return False
        at += 1
        return True

    def character(value: str) -> bool:
        if value.startswith("u8"):
            value = value[2:]
        elif value.startswith(("u", "U", "L")):
            value = value[1:]
        if len(value) < 3 or value[0] != "'" or "\n" in value or "\r" in value:
            return False
        try:
            return code_format._literal_end(value, 0, 0) == len(value)
        except code_format.SourceFormatError:
            return False

    def unary(depth: int) -> bool:
        nonlocal at
        if depth > 128 or at == len(values):
            return False
        token = values[at]
        if token in ("op:+", "op:-", "op:!", "op:~", "id:not", "id:compl"):
            at += 1
            return unary(depth + 1)
        if take("op:("):
            return conditional(depth + 1) and take("op:)")
        at += 1
        if token.startswith("id:"):
            return not token[3:].startswith("__") and token[3:] not in keywords
        if token.startswith("number:"):
            return _cpp_expression_repair_number(token[7:])
        return token.startswith("literal:") and character(token[8:])

    def binary(minimum: int, depth: int) -> bool:
        nonlocal at
        if not unary(depth):
            return False
        while at < len(values):
            level = precedence.get(values[at], 0)
            if level < minimum:
                break
            at += 1
            if not binary(level + 1, depth + 1):
                return False
        return True

    def conditional(depth: int) -> bool:
        if depth > 128 or not binary(1, depth):
            return False
        if not take("op:?"):
            return True
        return conditional(depth + 1) and take("op::") and conditional(depth + 1)

    return bool(values) and conditional(0) and at == len(values)


def _cpp_expression_repair_valid(path: str, source: str, expression: str) -> bool:
    """Authenticate one real token interval in the immutable original source."""
    if (type(path) is not str or not 0 < len(path) <= 256 or
            re.fullmatch(r"[A-Za-z0-9_.+/-]+", path) is None or
            any(not part or part.startswith(".") or part.endswith(".") or
                part.split(".", 1)[0].upper() in
                {"CON", "PRN", "AUX", "NUL", *("COM" + str(i) for i in range(1, 10)),
                 *("LPT" + str(i) for i in range(1, 10))}
                for part in path.split("/")) or
            not path.lower().endswith((".cpp", ".cc", ".cxx", ".c++")) or
            type(expression) is not str):
        return False
    try:
        if not 0 < len(expression.encode("utf-8")) <= 512:
            return False
    except UnicodeError:
        return False
    original = _cpp_expression_repair_tokens(source)
    wanted = _cpp_expression_repair_tokens(expression)
    if (original is None or wanted is None or
            any(token.startswith("directive:") and not _cpp_expression_repair_include(token)
                for token in original) or not _cpp_expression_repair_scalar(wanted)):
        return False
    code = tuple(token for token in original if not token.startswith("comment:"))
    pattern = tuple(token for token in wanted if not token.startswith("comment:"))
    return sum(code[at:at + len(pattern)] == pattern
               for at in range(len(code) - len(pattern) + 1)) == 1


def _validate_compiler_cmake_graph_checks(document: dict) -> None:
    """Bounded, append-only CMake structural objectives, without block edits."""
    if "compilerCmakeGraphChecks" not in document:
        return
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and
            meta.get("locale") == "es", "CMake graphs require Spanish v2")
    records = document["compilerCmakeGraphChecks"]
    require(type(records) is list and 0 < len(records) <= 64,
            "CMake graph checks require 1..64 owners")
    owners, seen = _blocks(document), set()
    for entry in records:
        require(type(entry) is dict and
                {"activity", "targets"} <= set(entry) <=
                {"activity", "project", "minimumCmakeVersion", "minimumCmakeVersionAtLeast", "targets",
                 "optionSelection", "sourceIntegrity", "configureMessageOrder",
                 "targetList", "initialObservation"},
                "CMake graph owner needs bounded optional requirements")
        identity = _identity(entry["activity"])
        require(identity not in seen, "Repeated CMake graph activity")
        seen.add(identity)
        block = owners.get(identity)
        battery_records = document.get("compilerOpenBatteryChecks")
        generated = [item for item in battery_records
                     if type(item) is dict and item.get("activity") == identity and
                     item.get("kind") == "cmake-adjustment-project-v1"] if type(battery_records) is list else []
        if len(generated) == 1:
            _validate_adjustment_project_record(document, generated[0], block)
        require(type(block) is dict and block.get("type") == "cpp_compiler" and
                type(block.get("cmake")) is dict and
                type(block.get("projectFiles")) is list and bool(block["projectFiles"]) and
                ((type(block.get("outputCheck")) is dict and
                  type(block["outputCheck"].get("cases")) is list and
                  bool(block["outputCheck"]["cases"])) or len(generated) == 1) and
                "compileCheck" not in block,
                "CMake graph needs an original or authenticated generated output-check CMake editor")
        project = entry.get("project", "")
        require(type(project) is str and len(project) <= 128,
                "CMake graph project name must be short text")
        if "minimumCmakeVersion" in entry:
            minimum = entry["minimumCmakeVersion"]
            require(type(minimum) is str and len(minimum) <= 40 and
                    re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}", minimum) and
                    all(int(part) <= 0xFFFFFFFF for part in minimum.split(".")),
                    "CMake minimum must be a numeric major.minor version")
        if "minimumCmakeVersionAtLeast" in entry:
            require(type(entry["minimumCmakeVersionAtLeast"]) is bool and
                    "minimumCmakeVersion" in entry,
                    "CMake minimum comparison requires a Boolean and numeric minimum")
        if "configureMessageOrder" in entry:
            messages = entry["configureMessageOrder"]
            source_cmake = {f.get("path") for f in block["projectFiles"]
                            if type(f) is dict and type(f.get("path")) is str
                            and (f["path"] == "CMakeLists.txt" or
                                 f["path"].endswith("/CMakeLists.txt") or
                                 f["path"].endswith(".cmake"))}
            require(type(messages) is list and 0 < len(messages) <= 16 and
                    all(type(item) is dict and
                        set(item) == {"path", "text"} and
                        type(item["path"]) is str and
                        item["path"] in source_cmake and
                        type(item["text"]) is str and
                        0 < len(item["text"]) <= 256 and
                        all(ord(c) >= 32 and ord(c) != 127
                            for c in item["text"])
                        for item in messages),
                    "CMake STATUS order needs original CMake files and text")
        expression_repair = None
        if "sourceIntegrity" in entry:
            integrity = entry["sourceIntegrity"]
            require(type(integrity) is dict and
                    {"preserveNonCmake"} <= set(integrity) <=
                    {"preserveNonCmake", "removePrefix", "replaceExpression"} and
                    integrity["preserveNonCmake"] is True and
                    not {"removePrefix", "replaceExpression"} <= set(integrity) and
                    any(type(path) is str and
                        not (path.endswith("/CMakeLists.txt") or
                             path == "CMakeLists.txt" or
                             path.endswith(".cmake"))
                        for path in (f.get("path") for f in block["projectFiles"]
                                     if type(f) is dict)),
                    "CMake source integrity requires preserved non-CMake files")
            if "removePrefix" in integrity:
                removal = integrity["removePrefix"]
                require(type(removal) is dict and
                        set(removal) == {"path", "text"} and
                        type(removal["path"]) is str and
                        removal["path"].lower().endswith(
                            (".cpp", ".cc", ".cxx", ".c++")) and
                        type(removal["text"]) is str and
                        0 < len(removal["text"]) <= 512 and
                        any(type(f) is dict and
                            f.get("path") == removal["path"] and
                            type(f.get("initialCode")) is str and
                            f["initialCode"].startswith(removal["text"])
                            for f in block["projectFiles"]),
                        "CMake repair can only remove an authentic source prefix")
            if "replaceExpression" in integrity:
                expression_repair = integrity["replaceExpression"]
                require(type(expression_repair) is dict and
                        set(expression_repair) == {"path", "original"} and
                        type(expression_repair["path"]) is str and
                        type(expression_repair["original"]) is str,
                        "CMake expression repair requires its original path and expression")
                sources = [f for f in block["projectFiles"]
                           if type(f) is dict and
                           f.get("path") == expression_repair["path"]]
                require(len(sources) == 1 and
                        _cpp_expression_repair_valid(
                            expression_repair["path"], sources[0].get("initialCode"),
                            expression_repair["original"]),
                        "CMake expression repair must identify one authentic bounded expression")
        targets = entry["targets"]
        require(type(targets) is list and 0 < len(targets) <= 16,
                "CMake graph needs 1..16 targets")
        original_paths = {f.get("path") for f in block["projectFiles"]
                          if type(f) is dict}
        targets_by_name = {}
        for target in targets:
            require(type(target) is dict and
                    {"name", "type"} <= set(target) <=
                    {"name", "type", "sources", "linksTo", "exactSources",
                     "explicitCxx20", "explicitCxxExtensionsOff", "requireExplicitMsvcWarnings",
                     "publicIncludeRoots", "privateIncludeRoots",
                     "sourceDirectory", "authoredCompileOptions"},
                    "CMake target requires a name, type and bounded optional rules")
            name, kind = target["name"], target["type"]
            require(type(name) is str and len(name) <= 128 and
                    re.fullmatch(r"[A-Za-z0-9_.+-]+", name) and
                    name not in targets_by_name and
                    kind in ("EXECUTABLE", "STATIC_LIBRARY"),
                    "Invalid, duplicated or unsupported CMake target")
            targets_by_name[name] = target
            sources = target.get("sources", [])
            require(type(sources) is list and len(sources) <= 32 and
                    all(type(p) is str and
                        p.lower().endswith((".cpp", ".cc", ".cxx", ".c++")) and
                        p in original_paths for p in sources) and
                    len(set(sources)) == len(sources),
                    "CMake required sources must be unique original C++ files")
            links = target.get("linksTo", [])
            require(type(links) is list and len(links) <= 16 and
                    all(type(n) is str and len(n) <= 128 and
                        re.fullmatch(r"[A-Za-z0-9_.+-]+", n) and n != name
                        for n in links) and len(set(links)) == len(links),
                    "CMake linked targets must be unique non-self names")
            exact = target.get("exactSources", False)
            require(type(exact) is bool and (not exact or bool(sources)),
                    "CMake exactSources requires a nonempty source list")
            for key in ("explicitCxx20", "explicitCxxExtensionsOff", "requireExplicitMsvcWarnings"):
                if key in target:
                    require(type(target[key]) is bool,
                            "CMake explicit requirements must be Boolean")
            if "authoredCompileOptions" in target:
                options = target["authoredCompileOptions"]
                require(type(options) is list and 0 < len(options) <= 8 and
                        all(type(option) is str and
                            re.fullmatch(r"[/-][A-Za-z][A-Za-z0-9_.:+-]{0,62}",
                                         option) for option in options) and
                        len(set(options)) == len(options),
                        "CMake authored compile options must be distinct safe flags")
            if "sourceDirectory" in target:
                source_dir = target["sourceDirectory"]
                require(kind == "STATIC_LIBRARY" and
                        type(source_dir) is str and
                        0 < len(source_dir) <= 256 and
                        re.fullmatch(
                            r"[A-Za-z0-9_.+-]+(?:/[A-Za-z0-9_.+-]+)*",
                            source_dir) and
                        all(part not in (".", "..") for part in
                            source_dir.split("/")) and
                        source_dir + "/CMakeLists.txt" in original_paths,
                        "CMake sourceDirectory must identify an original child")
            for key in ("publicIncludeRoots", "privateIncludeRoots"):
                if key not in target:
                    continue
                roots = target[key]
                require(kind == "STATIC_LIBRARY" and type(roots) is list and
                        0 < len(roots) <= 8 and
                        all(type(root) is str and len(root) <= 256 and
                            re.fullmatch(
                                r"[A-Za-z0-9_.+-]+(?:/[A-Za-z0-9_.+-]+)*", root) and
                            all(part not in (".", "..") for part in root.split("/")) and
                            any(type(path) is str and
                                path.startswith(root + "/") and
                                path.lower().endswith((".h", ".hpp", ".hxx"))
                                for path in original_paths) for root in roots) and
                        len(set(roots)) == len(roots),
                        "CMake include roots must be unique original header directories")
            require(not (set(target.get("publicIncludeRoots", [])) &
                         set(target.get("privateIncludeRoots", []))),
                    "CMake include roots cannot be both PUBLIC and PRIVATE")
        if expression_repair is not None:
            require(any(expression_repair["path"] in target.get("sources", [])
                        for target in targets),
                    "CMake expression repair must own an original required C++ source")
        if "initialObservation" in entry:
            for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                          "compilerNamedRuleChecks", "compilerOpenBatteryChecks",
                          "compilerReductionChecks"):
                require(not any(type(record) is dict and record.get("activity") == identity
                                for record in document.get(field, [])),
                        "CMake initial observation cannot share its owner with another runtime or source prerequisite")
            observation = entry["initialObservation"]
            require(expression_repair is not None and
                    type(observation) is dict and
                    {"executable", "input", "output", "exitCode"} <= set(observation) <=
                    {"executable", "input", "output", "exitCode", "warning"},
                    "CMake initial observation needs a protected expression and a complete bounded failure contract")
            executable = observation["executable"]
            require(type(executable) is str and 0 < len(executable) <= 128 and
                    re.fullmatch(r"[A-Za-z0-9_.+-]+", executable) and
                    executable in targets_by_name and
                    targets_by_name[executable]["type"] == "EXECUTABLE" and
                    any(case.get("executable", block["cmake"].get("target")) == executable
                        for case in block.get("outputCheck", {}).get("cases", [])),
                    "CMake initial observation must use an existing output-case executable")
            for channel in ("input", "output"):
                value = observation[channel]
                require(type(value) is str and "\x00" not in value and
                        not any(0xD800 <= ord(c) <= 0xDFFF for c in value) and
                        len(value.encode("utf-8")) <= 65536,
                        "CMake initial observation input and output must fit 65536 UTF-8 bytes without NUL")
            require(type(observation["exitCode"]) in (int, float) and
                    1 <= observation["exitCode"] <= 255 and
                    math.isfinite(observation["exitCode"]) and
                    math.trunc(observation["exitCode"]) == observation["exitCode"],
                    "CMake initial failure exit must be an integer in 1..255")
            if "warning" in observation:
                warning = observation["warning"]
                require(type(warning) is dict and set(warning) == {"code", "path"} and
                        type(warning["code"]) is str and
                        re.fullmatch(r"C4[0-9]{3}", warning["code"]) and
                        type(warning["path"]) is str and
                        warning["path"] == expression_repair["path"],
                        "CMake initial warning must identify an MSVC C4xxx diagnostic in the protected expression file")
                require(any(warning["path"] in target.get("sources", []) and
                            (target.get("requireExplicitMsvcWarnings") is True or
                             any(re.fullmatch(r"[/-]W(?:[1-4]|all)", option)
                                 for option in target.get("authoredCompileOptions", [])))
                            for target in targets),
                        "CMake initial warning requires authored warning options on the compiling target")
        for library in targets:
            if (library.get("publicIncludeRoots") or
                    library.get("privateIncludeRoots")):
                require(any(consumer.get("type") == "EXECUTABLE" and
                            library["name"] in consumer.get("linksTo", [])
                            for consumer in targets),
                        "CMake include scopes need a linked executable")
        if "targetList" in entry:
            list_goal = entry["targetList"]
            require(type(list_goal) is dict and
                    set(list_goal) == {"targets", "requireAppend"} and
                    type(list_goal["targets"]) is list and
                    2 <= len(list_goal["targets"]) <= 16 and
                    all(type(name) is str for name in list_goal["targets"]) and
                    len(set(list_goal["targets"])) ==
                    len(list_goal["targets"]) and
                    type(list_goal["requireAppend"]) is bool and
                    all(name in targets_by_name and
                        targets_by_name[name]["type"] == "EXECUTABLE" and
                        targets_by_name[name].get(
                            "explicitCxxExtensionsOff") is True
                        for name in list_goal["targets"]),
                    "CMake target list needs declared executables with authored OFF")
        selected = block["cmake"].get("target")
        require(selected in targets_by_name and
                targets_by_name[selected]["type"] == "EXECUTABLE",
                "CMake graph must include the selected executable target")
        for target in targets:
            for name in target.get("linksTo", []):
                require(name in targets_by_name and
                        targets_by_name[name]["type"] == "STATIC_LIBRARY" and
                        target["type"] == "EXECUTABLE",
                        "CMake links require a declared static library and executable")
        if "optionSelection" in entry:
            option = entry["optionSelection"]
            require(type(option) is dict and set(option) ==
                    {"name", "default", "target", "offSource", "offStdout"},
                    "CMake binary option requires its complete bounded contract")
            name, target, off_source = (
                option["name"], option["target"], option["offSource"])
            require(type(name) is str and len(name) <= 128 and
                    re.fullmatch(r"[A-Za-z0-9_.+-]+", name) and
                    option["default"] == "OFF" and
                    block["cmake"].get("options", {}).get(name) == "ON" and
                    not block.get("standardInput") and
                    type(option["offStdout"]) is str and
                    len(option["offStdout"]) <= 1024 * 1024,
                    "CMake binary option needs ON editor and OFF default")
            library = targets_by_name.get(target)
            require(type(target) is str and library is not None and
                    library["type"] == "STATIC_LIBRARY" and
                    library.get("exactSources") is True and
                    bool(library.get("sources")) and
                    type(off_source) is str and
                    off_source.lower().endswith((".cpp", ".cc", ".cxx", ".c++")) and
                    off_source in original_paths and
                    off_source not in library["sources"],
                    "CMake binary option needs distinct original library sources")

# Five pre-existing, source-pinned native reviews. This migration guard
# only prevents new root metadata from contradicting their published behavior;
# ordinary future code-order owners are validated by the same generic rules.
LEGACY_ORDER_EQUIVALENCES = {
    ("cpp_fundamentals__objetos_y_estado__representar_procesar_y_recuperar",
     "block_0002_0001"): [[2, 3]],
    ("cpp_fundamentals__colecciones_y_recorridos__acumular_contar_y_seleccionar",
     "block_0005_0004"): [[2, 3], [6, 7]],
    ("cpp_fundamentals__funciones_y_contratos__construir_un_proyecto_desde_su_interfaz",
     "orden_validar_antes_de_calcular"): [[2, 3]],
    ("cpp_fundamentals__excepciones_y_recuperacion__recuperar_por_operacion",
     "block_0002_0003"): [[3, 4], [6, 7], [17, 18]],
    ("cpp_fundamentals__archivos_y_programa_completo__extraer_un_tipo_sin_cambiarlo_en_fallo",
     "block_0004_0003"): [[2, 3, 4]],
}


def _validate_order_equivalence_groups(document: dict) -> None:
    """Bind reviewable independent code cards to their exact evaluated source.

    The groups change accepted permutations, not the fragments, answer key,
    draft address, or the original stateLayout. The author must independently
    establish that the grouped statements can execute in any order.
    """
    if "orderEquivalenceGroups" not in document:
        return
    meta = document.get("stateLayout")
    require(type(meta) is dict and meta.get("version") == 2 and meta.get("locale") == "es",
            "Independent order groups require the existing Spanish v2 map")
    entries = document["orderEquivalenceGroups"]
    require(type(entries) is list and 0 < len(entries) <= 16,
            "Order equivalences require 1..16 reviewed activities")
    owners = _blocks(document)
    seen = set()
    for record in entries:
        require(type(record) is dict and set(record) ==
                {"activity", "sourceSha256", "groups"},
                "Order equivalences need only activity, sourceSha256 and groups")
        identity = _identity(record["activity"])
        require(identity not in seen, "An order equivalence owner was declared twice")
        seen.add(identity)
        block = owners.get(identity)
        require(type(block) is dict and block.get("type") == "order_code",
                "Order equivalences require one existing evaluated order_code owner")
        fragments, order = block.get("fragments"), block.get("correctOrder")
        # Escaped characters in C++ literals do not split a physical card.
        require(type(fragments) is list and 2 <= len(fragments) <= 256 and
                all(type(fragment) is str and fragment.strip() and
                    "\n" not in fragment and "\r" not in fragment
                    for fragment in fragments) and
                type(order) is list and len(order) == len(fragments) and
                all(type(index) is int for index in order) and
                sorted(order) == list(range(len(fragments))),
                "Independent cards require a complete permutation of single physical lines")
        digest = record["sourceSha256"]
        require(type(digest) is str and re.fullmatch("[0-9a-f]{64}", digest),
                "Order equivalences need a valid source hash")
        valid_digest = digest == state_fingerprint(block)
        if not valid_digest:
            # The root rubric is append-only. A later, independently
            # authenticated lexical-format revision must not force the author
            # to rewrite its original source identity just to add whitespace.
            # _validate_state_layout already checks the complete archive,
            # token equivalence, original storage signature and editorial edge.
            formats = [item["block"] for item in meta.get("sourceFormatRevisions", [])
                       if item["id"] == identity]
            if len(formats) == 1:
                restored = code_format.source_projection(block, formats[0])
                valid_digest = digest == state_fingerprint(restored)
        require(valid_digest,
                "Order equivalences do not match their exact evaluated source")
        groups = record["groups"]
        require(type(groups) is list and 0 < len(groups) <= 16,
                "Each order equivalence owner requires 1..16 groups")
        claimed = set()
        for group in groups:
            require(type(group) is list and 2 <= len(group) <= 8 and
                    all(type(index) is int and 0 <= index < len(fragments)
                        for index in group) and
                    len(group) == len(set(group)) and
                    not claimed.intersection(group),
                    "Independent card groups must be distinct, disjoint and bounded")
            claimed.update(group)
            positions = sorted(order.index(index) for index in group)
            require(positions == list(range(positions[0], positions[0] + len(positions))),
                    "Independent card groups must occupy one contiguous canonical span")
        previous = LEGACY_ORDER_EQUIVALENCES.get((document.get("id"), identity))
        require(previous is None or groups == previous,
                "A root order declaration cannot contradict a reviewed historical group")


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
    course_edition.validate_lesson(document, require_layout=True)
    if "compilerPuzzlesRequired" in document:
        require(type(document["compilerPuzzlesRequired"]) is bool, "compilerPuzzlesRequired must be boolean")
    mapping = _validate_state_layout(document)
    _validate_order_visual_separators(document)
    _validate_order_equivalence_groups(document)
    _validate_compiler_workflow_checks(document)
    _validate_compiler_runtime_checks(document)
    _validate_compiler_named_rule_checks(document)
    _validate_compiler_indexed_unique_transfer_checks(document)
    _validate_compiler_defaulted_move_policy_checks(document)
    _validate_compiler_independent_copy_assignment_checks(document)
    _validate_compiler_move_assignment_invariant_checks(document)
    _validate_compiler_array_owner_read_checks(document)
    _validate_compiler_array_copy_move_checks(document)
    _validate_compiler_array_return_take_checks(document)
    _validate_compiler_polymorphic_destruction_checks(document)
    _validate_compiler_virtual_lifetime_dispatch_checks(document)
    _validate_compiler_derived_preparation_checks(document)
    _validate_compiler_abstract_interface_checks(document)
    _validate_compiler_interface_collection_checks(document)
    _validate_compiler_polymorphic_clone_checks(document)
    _validate_compiler_open_battery_checks(document)
    _validate_compiler_reduction_checks(document)
    _validate_compiler_cmake_graph_checks(document)
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
        simulation.validate_document(document)
    except (ValueError, KeyError, TypeError) as error:
        raise LayoutError(f"Invalid curricular simulation: {error}") from error
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


def revise_math_prompt_editorial(document: dict, identity: str, prompt: str, *,
                                 spans: list, return_to: int | None = None) -> dict:
    """Author /prompt while preserving the complete original mathematical source.

    Spans are explicit UTF-16 selections. The immutable raw JSON archive is
    created once; subsequent prompt editions append ordinary editorial edges.
    A return to a previously published full block requires its explicit revision.
    """
    validate(document)
    require(document.get("stateLayout", {}).get("version") == 2
            and document["stateLayout"].get("locale") == "es",
            "Math prompt editorial revision requires a published Spanish v2 lesson")
    _identity(identity)
    original = _blocks(document).get(identity)
    require(type(original) is dict and original.get("type") == "math_puzzle"
            and type(original.get("prompt")) is str and bool(original["prompt"].strip())
            and "question" not in original,
            "Math prompt revisions belong to existing active math_puzzle prompt owners")
    require(type(prompt) is str and bool(prompt.strip()) and prompt != original["prompt"],
            "A math prompt revision needs different nonblank wording")
    require(type(spans) is list,
            "Author an explicit semantic span list, including [] for unmarked prose")
    require(return_to is None or (type(return_to) is int and return_to >= 0),
            "An editorial return must name an explicit nonnegative revision")
    result = deepcopy(document)
    meta = result["stateLayout"]
    if identity not in _math_text_revisions(result):
        meta.setdefault("mathTextRevisions", []).append({
            "id": identity, "field": "/prompt", "rawJson": _math_ordered_json(original),
            "semanticBefore": deepcopy(_math_prompt_semantic_entry(document, identity))})
    revised = _blocks(result)[identity]
    revised["prompt"] = prompt
    overlay = result.setdefault(text_semantics.ROOT_FIELD, {"version": 1, "fields": []})
    marker = text_semantics.make_field(result, identity, "/prompt", spans)
    old_marker = _math_prompt_semantic_entry(result, identity)
    if old_marker is None:
        overlay["fields"].append(marker)
    else:
        require(not old_marker["spans"] or bool(spans),
                "A revised marked prompt cannot silently lose all its authored highlights")
        old_marker.clear()
        old_marker.update(marker)
    try:
        text_semantics.validate(result)
    except (KeyError, TypeError, ValueError) as exc:
        raise LayoutError("Invalid authored mathematical prompt semantics") from exc
    proof = {"id": identity, "beforeSha256": fingerprint(original),
             "afterSha256": fingerprint(revised)}
    history = document["stateLayout"]["editorial"]
    prior = [edge for edge in history if edge["id"] == identity]
    nodes = ([prior[0]["beforeSha256"]] + [edge["afterSha256"] for edge in prior]) if prior else []
    eligible = [index for index, digest in enumerate(nodes[:-1]) if digest == proof["afterSha256"]]
    if eligible:
        require(return_to in eligible,
                "This prompt restores a published block: declare its exact return_to revision")
        proof["returnTo"] = {"revision": return_to, "historySha256": fingerprint({
            "lessonId": result["id"], "locale": "es", "editorial": history}),
            "previousBlock": deepcopy(original)}
    else:
        require(return_to is None, "An explicit editorial return must restore an earlier full block")
    meta["editorial"].append(proof)
    validate_transition(document, result)
    return result


def ordinary_editorial_fields(before: dict, after: dict) -> tuple[str, ...]:
    """Recognize a complete prose-only revision; never authorize it by a hash alone.

    This is a predicate for the ordinary editorial guard, not a dispatch test:
    callers must still send mixed wording/contract changes to that guard so
    they are rejected. Provenance, authored semantics and identity are checked
    by the full document/transition validators.
    """
    if (type(before) is not dict or type(after) is not dict
            or type(before.get("type")) is not str
            or before["type"] not in ORDINARY_EDITORIAL_TYPES):
        return ()
    fields = tuple(field for field in ("question", "explanation")
                   if before.get(field) != after.get(field))
    if not fields:
        return ()
    for field in fields:
        updated = after.get(field)
        if type(updated) is not str or not updated.strip():
            return ()
        # A previously absent optional explanation is new prose, not a
        # change to an exercise contract. Still require an authenticated
        # editorial edge and an explicit exact-source semantic descriptor.
        # Do not extend this to absent questions or malformed old values.
        if field == "explanation" and field not in before:
            continue
        original = before.get(field)
        if type(original) is not str or not original.strip():
            return ()
    expected = {**before, **{field: after[field] for field in fields}}
    if after != expected:
        return ()
    # Python equates True with 1. Keep the existing typed JSON contract too,
    # after comparing the complete payload rather than its state-only digest.
    try:
        return fields if fingerprint(after) == fingerprint(expected) else ()
    except (ValueError, TypeError, UnicodeError):
        return ()


def _compose_ordinary_editorial_semantics(previous: dict, current: dict,
                                          expected: dict | None,
                                          choice_owners: set[str]) -> dict | None:
    """Permit only changed prose paths of other intact ordinary owners.

    Both documents and the append-only proof chain have already been checked
    by the transition validator. The current overlay has also been validated
    against exact text, UTF-16 selections and supported public fields.
    """
    before, after = _blocks(previous), _blocks(current)
    allowed = {(identity, "/" + field)
               for identity, block in before.items() if identity not in choice_owners
               for field in ordinary_editorial_fields(block, after.get(identity))}
    if not allowed:
        return expected
    overlay = current.get(text_semantics.ROOT_FIELD)
    require(type(overlay) is dict,
            "A composed ordinary revision needs explicit authored semantics")
    markers = {(entry["block"], entry["path"]): entry for entry in overlay["fields"]}
    require(allowed <= markers.keys(),
            "Every composed ordinary field needs its exact authored descriptor")
    if expected is None:
        # This is exactly the container created by revise_ordinary_editorial
        # for previously unmarked prose. Root fields cannot be invented here.
        expected = {"version": 1, "fields": []}
    existing = set()
    for field in expected["fields"]:
        key = (field["block"], field["path"])
        existing.add(key)
        if key in allowed:
            replacement = markers[key]
            require(not field["spans"] or bool(replacement["spans"]),
                    "A revised marked field cannot silently lose all its authored highlights")
            field.clear()
            field.update(deepcopy(replacement))
    # Existing entries retain their order. Only newly authored eligible fields
    # may be appended, in the order used by the ordinary authoring helper.
    for field in overlay["fields"]:
        key = (field["block"], field["path"])
        if key in allowed and key not in existing:
            expected["fields"].append(deepcopy(field))
    return expected


def revise_ordinary_editorial(document: dict, identity: str, edits: dict) -> dict:
    """Author a question rewrite and/or optional explanation on a v2 owner.

    An absent optional explanation may be introduced; an absent question,
    empty old wording, answer change or implicit default is never authorized.
    Each field has the exact new "text" and explicitly authored UTF-16
    "spans". This never infers a highlight or rewrites an old narration range.
    If the complete resulting block existed earlier, the optional "returnTo"
    revision must explicitly name it. Multiple field changes are atomic.
    """
    validate(document)
    require(document.get("stateLayout", {}).get("version") == 2
            and document["stateLayout"].get("locale") == "es",
            "Ordinary editorial revision requires a published Spanish v2 lesson")
    _identity(identity)
    original = _blocks(document).get(identity)
    require(type(original) is dict and original.get("type") in ORDINARY_EDITORIAL_TYPES,
            "Ordinary editorial revisions belong to existing graded prose owners")
    require(type(edits) is dict and set(edits) <= {"question", "explanation", "returnTo"},
            "Author only question/explanation prose fields and an optional explicit return")
    return_to = edits.get("returnTo")
    requested = {field: spec for field, spec in edits.items() if field != "returnTo"}
    require(bool(requested) and len(requested) <= 2,
            "Author one or two question/explanation prose fields")
    require(return_to is None or (type(return_to) is int and return_to >= 0),
            "An editorial return must name an explicit nonnegative revision")
    for field, spec in requested.items():
        require(type(spec) is dict and set(spec) == {"text", "spans"},
                "Every revised field needs exact text and authored semantic spans")
        old, updated = original.get(field), spec["text"]
        authored_optional = field == "explanation" and field not in original
        require((authored_optional or (type(old) is str and bool(old.strip())))
                and type(updated) is str and bool(updated.strip()) and updated != old,
                "An editorial revision needs nonblank wording or a newly authored optional explanation")
        require(type(spec["spans"]) is list,
                "Author an explicit semantic span list, including [] for unmarked prose")

    result = deepcopy(document)
    revised = _blocks(result)[identity]
    overlay = result.setdefault(text_semantics.ROOT_FIELD, {"version": 1, "fields": []})
    existing = overlay["fields"]
    for field, spec in requested.items():
        revised[field] = spec["text"]
        path = "/" + field
        matches = [i for i, item in enumerate(existing)
                   if item.get("block") == identity and item.get("path") == path]
        require(len(matches) <= 1, "An authored semantic path cannot be duplicated")
        if matches:
            original_spans = existing[matches[0]]["spans"]
            require(not original_spans or bool(spec["spans"]),
                    "A revised marked field cannot silently lose all its authored highlights")
        marker = text_semantics.make_field(result, identity, path, spec["spans"])
        if matches:
            existing[matches[0]] = marker
        else:
            existing.append(marker)
    text_semantics.validate(result)
    proof = {"id": identity,
             "beforeSha256": fingerprint(original),
             "afterSha256": fingerprint(revised)}
    history = document["stateLayout"]["editorial"]
    same_owner = [edge for edge in history if edge["id"] == identity]
    previous_nodes = ([same_owner[0]["beforeSha256"]]
                      + [edge["afterSha256"] for edge in same_owner]) if same_owner else []
    eligible = [index for index, digest in enumerate(previous_nodes[:-1])
                if digest == proof["afterSha256"]]
    if eligible:
        require(return_to in eligible,
                "This wording restores a previously published block: declare its exact returnTo revision")
        proof["returnTo"] = {
            "revision": return_to,
            "historySha256": fingerprint({"lessonId": result["id"], "locale": "es",
                                          "editorial": history}),
            "previousBlock": deepcopy(original)}
    else:
        require(return_to is None,
                "An explicit editorial return must match an earlier full published block")
    result["stateLayout"]["editorial"].append(proof)
    validate_transition(document, result)
    return result


def revise_choice_editorial(document: dict, identity: str,
                            option_index: int, replacement: str, *,
                            spans: list | None = None) -> dict:
    """Revise one option without changing the protected response contract.

    Omitting spans preserves the published marker exactly (legacy behavior).
    Supplying spans, including an explicit [], authenticates a new descriptor
    for the replacement text and archives both exact semantic versions.
    """
    validate(document)
    meta = document.get("stateLayout", {})
    require(meta.get("version") == 2 and meta.get("locale") == "es",
            "General choice review requires a published Spanish v2 lesson")
    _identity(identity)
    require(identity not in _choice_revisions(document),
            "A reviewed choice cannot be revised twice under one historical archive")
    require(CHOICE_OPTION_REVISIONS.get((document.get("id"), identity)) is None,
            "A closed reviewed choice has its own existing revision contract")
    original = _blocks(document).get(identity)
    require(type(original) is dict and original.get("type") == "multiple_choice"
            and type(original.get("options")) is list
            and type(original.get("correctIndex")) is int,
            "General choice review requires an existing complete choice activity")
    require(type(option_index) is int and 0 <= option_index < len(original["options"])
            and type(replacement) is str and bool(replacement.strip())
            and replacement != original["options"][option_index],
            "General choice review must revise exactly one nonblank option")
    require(spans is None or type(spans) is list,
            "Authored option semantics must be an explicit UTF-16 span list")
    previous_semantic = deepcopy(_choice_semantic_entry(document, identity, option_index))
    if spans is not None:
        require(previous_semantic is None or not previous_semantic["spans"] or bool(spans),
                "A marked option cannot silently lose all highlights")
    result = deepcopy(document)
    actual = _blocks(result)[identity]
    actual["options"][option_index] = replacement
    previous_root = text_semantics.ROOT_FIELD in document
    semantic = _choice_semantic_entry(result, identity, option_index)
    if spans is not None:
        overlay = result.setdefault(text_semantics.ROOT_FIELD, {"version": 1, "fields": []})
        revised = text_semantics.make_field(result, identity, f"/options/{option_index}", spans)
        if semantic is None:
            overlay["fields"].append(revised)
        else:
            semantic.clear()
            semantic.update(deepcopy(revised))
        text_semantics.validate(result)
    elif semantic is not None:
        # Legacy callers retain the existing source selections and narration.
        # If the old offsets no longer describe the new text, validation fails.
        semantic["sourceSha256"] = hashlib.sha256(replacement.encode("utf-8")).hexdigest()
    record = {
        "id": identity, "block": deepcopy(original),
        "optionIndex": option_index, "replacement": replacement,
        "semanticBefore": previous_semantic
    }
    if spans is not None:
        record["semanticAfter"] = deepcopy(revised)
        record["semanticRootBefore"] = previous_root
    result["stateLayout"].setdefault("choiceRevisions", []).append(record)
    result["stateLayout"]["editorial"].append({
        "id": identity, "beforeSha256": fingerprint(original),
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


def reorder_compiler_cases(document: dict, identity: str, order: list[int], *,
                           return_to: int | None = None) -> dict:
    """Permute complete published cases, keeping their exact data and authored marks."""
    validate(document)
    meta = document.get("stateLayout", {})
    require(meta.get("version") == 2 and meta.get("locale") == "es",
            "Compiler case ordering requires an existing published Spanish v2 owner")
    _identity(identity)
    original = _blocks(document).get(identity)
    require(type(original) is dict, "Compiler case ordering requires an active existing owner")
    require(return_to is None or (type(return_to) is int and return_to >= 0),
            "A compiler case return needs an explicit nonnegative revision")
    record = {"id": identity, "block": deepcopy(original), "order": deepcopy(order),
              "editorialIndex": len(meta["editorial"]),
              "historySha256": fingerprint({"lessonId": document["id"], "locale": meta["locale"],
                                             "editorial": meta["editorial"]}),
              "semanticBefore": _compiler_case_order_fields(document, identity)}
    changed = _compiler_case_order_after(record)
    result = deepcopy(document)
    target = _blocks(result)[identity]
    target.clear()
    target.update(changed)
    remapped = iter(_compiler_case_order_remap_fields(record["semanticBefore"], order))
    overlay = result.get(text_semantics.ROOT_FIELD)
    if overlay is not None:
        overlay["fields"] = [next(remapped) if field.get("block") == identity and
            field.get("path", "").startswith("/outputCheck/cases/") else field
            for field in overlay.get("fields", [])]
    proof = {"id": identity, "beforeSha256": fingerprint(original), "afterSha256": fingerprint(changed)}
    prior = [edge for edge in meta["editorial"] if edge["id"] == identity]
    nodes = ([prior[0]["beforeSha256"]] + [edge["afterSha256"] for edge in prior]) if prior else []
    if proof["afterSha256"] in nodes:
        require(return_to is not None and return_to < len(nodes) - 1
                and nodes[return_to] == proof["afterSha256"],
                "Returning a compiler case order requires its explicit authentic earlier revision")
        proof["returnTo"] = {"revision": return_to, "historySha256": record["historySha256"],
                             "previousBlock": deepcopy(original)}
    else:
        require(return_to is None, "A new compiler case order cannot claim an editorial return")
    result["stateLayout"].setdefault("compilerCaseOrderRevisions", []).append(record)
    result["stateLayout"]["editorial"].append(proof)
    validate_transition(document, result)
    return result


def _closed_compiler_case_after(original: dict, spec: dict) -> dict:
    """Reconstruct the exact reviewed endpoint from its complete old compiler."""
    require(type(original) is dict and fingerprint(original) == spec["before"],
            "A reviewed compiler must start from its exact complete published block")
    actual = deepcopy(original)
    if "append" in spec:
        actual["outputCheck"]["cases"].extend(deepcopy(spec["append"]))
    else:
        actual["outputCheck"]["cases"][0]["outputs"][0] += spec["suffix"]
    if "initial_insert" in spec:
        require(actual["initialCode"].endswith(_COMPILER_REFERENCE_TAIL),
                "Invalid published compiler initial ending")
        actual["initialCode"] = (actual["initialCode"][:-len(_COMPILER_REFERENCE_TAIL)]
                                 + spec["initial_insert"] + _COMPILER_REFERENCE_TAIL)
    require(fingerprint(actual) == spec["after"],
            "The expanded compiler does not match its reviewed first revision")
    return actual


def _closed_compiler_reference_after(original: dict, spec: dict) -> dict:
    """Reconstruct only the paired reference's pinned insertion and endpoint."""
    require(type(original) is dict and fingerprint(original) == spec["before"],
            "A compiler expansion needs its exact complete published reference")
    reference = deepcopy(original)
    require(reference["code"].endswith(_COMPILER_REFERENCE_TAIL),
            "Invalid published reference ending")
    reference["code"] = (reference["code"][:-len(_COMPILER_REFERENCE_TAIL)]
                          + spec["insert"] + _COMPILER_REFERENCE_TAIL)
    require(fingerprint(reference) == spec["after"],
            "The compiler reference does not match its reviewed expansion")
    return reference


def _new_closed_compiler_sources(baseline: dict, current: dict) -> dict[str, dict]:
    """Reconstruct newly crossed closed endpoints from their exact old owners."""
    old_meta, meta = baseline["stateLayout"], current["stateLayout"]
    old_compilers = old_meta.get("compilerRevisions", []) if old_meta["version"] == 2 else []
    compilers = meta.get("compilerRevisions", [])
    require(compilers[:len(old_compilers)] == old_compilers,
            "Published compiler revisions are append-only and cannot be removed or replaced")
    before = _all_blocks(baseline)
    sources = {}
    for item in compilers[len(old_compilers):]:
        require(item["id"] in before and fingerprint(item["block"]) == fingerprint(before[item["id"]]),
                "A new compiler revision must preserve its complete previous published block")
        spec = COMPILER_CASE_REVISIONS[(current["id"], item["id"])]
        sources[item["id"]] = _closed_compiler_case_after(item["block"], spec)
        for reference in item.get("references", []):
            require(reference["id"] in before and fingerprint(reference["block"]) == fingerprint(before[reference["id"]]),
                    "A new compiler reference must preserve its complete previous published block")
            reference_spec = COMPILER_REFERENCE_REVISIONS[(current["id"], item["id"])]
            sources[reference["id"]] = _closed_compiler_reference_after(
                reference["block"], reference_spec)
    return sources


def source_format_transition_predecessor(previous: dict, current: dict,
                                         identity: str) -> dict:
    """Return the authenticated source before formatting in this transition.

    A direct A-to-C import may cross a closed compiler expansion A-to-B before
    its whitespace revision B-to-C. Only that newly introduced closed revision
    supplies B, reconstructed from A and the pinned rule. Already published
    revisions keep the exact prior block, including its existing formatting.
    Full transition validation still owns snapshots, contracts and proof edges.
    """
    validate_transition(previous, current)
    baseline = prepare(previous)
    _identity(identity)
    before = _all_blocks(baseline)
    require(identity in before and identity in _all_blocks(current),
            "A source-format predecessor must retain its exact published owner")
    return deepcopy(_new_closed_compiler_sources(baseline, current).get(
        identity, before[identity]))


def _revise_closed_compiler_cases(document: dict, identity: str) -> dict:
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
    actual.clear()
    actual.update(_closed_compiler_case_after(original, spec))
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
        reference.clear()
        reference.update(_closed_compiler_reference_after(original_reference, reference_spec))
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
                "activityAfter": deepcopy(document.get("activityAfter", [])),
                text_semantics.ROOT_FIELD: deepcopy(document.get(text_semantics.ROOT_FIELD)),
                "editorialPrefix": _retirement_editorial_prefix(document, len(document["stateLayout"]["editorial"]))}
    if text_semantics.ROOT_FIELD in result:
        overlay = result[text_semantics.ROOT_FIELD]
        overlay["fields"] = [field for field in overlay["fields"] if field["block"] not in retiring]
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
    require(not baseline.get("compilerNamedRuleChecks") or
            baseline.get("compilerNamedRuleChecks") == current.get("compilerNamedRuleChecks"),
            "Published named-rule requirements cannot be removed or rewritten")
    require(not baseline.get("compilerOpenBatteryChecks") or
            (type(current.get("compilerOpenBatteryChecks")) is list and
             current["compilerOpenBatteryChecks"][:len(baseline["compilerOpenBatteryChecks"])] ==
             baseline["compilerOpenBatteryChecks"]),
            "Published open battery checks cannot be removed, reordered or rewritten")
    require(not baseline.get("compilerReductionChecks") or
            (type(current.get("compilerReductionChecks")) is list and
             current["compilerReductionChecks"][:len(baseline["compilerReductionChecks"])] ==
             baseline["compilerReductionChecks"]),
            "Published reduction checks cannot be removed, reordered or rewritten")
    require(not baseline.get("compilerCmakeGraphChecks") or
            (type(current.get("compilerCmakeGraphChecks")) is list and
             current["compilerCmakeGraphChecks"][:len(baseline["compilerCmakeGraphChecks"])] ==
             baseline["compilerCmakeGraphChecks"]),
            "Published CMake graph requirements cannot be removed, reordered or rewritten")
    require(not baseline.get("orderVisualSeparators") or
            baseline["orderVisualSeparators"] == current.get("orderVisualSeparators"),
            "Published visual separators cannot lose an established source owner")
    require(not baseline.get("orderEquivalenceGroups") or
            (type(current.get("orderEquivalenceGroups")) is list and
             current["orderEquivalenceGroups"][:len(baseline["orderEquivalenceGroups"])] ==
             baseline["orderEquivalenceGroups"]),
            "Published order equivalence groups cannot be removed, reordered or weakened")
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
    old_renames = old_meta.get("identifierRenameRevisions", []) if old_v2 else []
    renamed = meta.get("identifierRenameRevisions", [])
    require(type(renamed) is list and (old_v2 or not renamed) and
            identifier_rename.same_json_value_and_types(
                renamed[:len(old_renames)], old_renames),
            "Published C++ rename archives are append-only")
    old_rename_blocks = {item["id"]: item["block"] for item in old_renames}
    rename_blocks = {item["id"]: item["block"] for item in renamed}
    for item in renamed[len(old_renames):]:
        identity = item["id"]
        require(identity in before and identity in after
                and identity not in old_rename_blocks and
                identifier_rename.same_json_value_and_types(
                    item["block"], before[identity]),
                "A C++ rename archive must retain its exact previous active owner")
    old_matching = old_meta.get("mathMatchingTextRevisions", []) if old_v2 else []
    matching_records = meta.get("mathMatchingTextRevisions", [])
    require(type(matching_records) is list and (old_v2 or not matching_records)
            and identifier_rename.same_json_value_and_types(
                matching_records[:len(old_matching)], old_matching),
            "Published matching text archives must remain append-only")
    for item in matching_records[len(old_matching):]:
        identity = item.get("id")
        require(type(identity) is str and identity in before and identity in after
                and identity in _blocks(baseline)
                and type(item.get("rawJson")) is str
                and item["rawJson"] == _math_ordered_json(before[identity]),
                "Matching text history must archive its exact published owner")
        old_marks = {field["path"]: field for field in baseline.get(
            text_semantics.ROOT_FIELD, {}).get("fields", [])
            if field.get("block") == identity}
        require(identifier_rename.same_json_value_and_types(
            item["semanticBefore"], {
                path: old_marks.get(path) for path in item["paths"]}),
            "A matching text archive changed its published reading marks")
    matching_before = {item["id"]: parse_json(item["rawJson"])
                       for item in matching_records}
    new_matching = matching_records[len(old_matching):]
    if new_matching:
        # A matching-text review can update only the narrator descriptors of
        # its declared changed paths. Unrelated reading and highlight intent
        # belongs to the former published lesson, not the new wording.
        overlay = deepcopy(baseline.get(text_semantics.ROOT_FIELD))
        if overlay is None:
            overlay = {"version": 1, "fields": []}
        for item in new_matching:
            for path in item["paths"]:
                original = item["semanticBefore"][path]
                updated = item["semanticAfter"][path]
                if original is None and updated is None:
                    continue
                indices = [i for i, field in enumerate(overlay["fields"])
                           if field["block"] == item["id"] and field["path"] == path]
                require(len(indices) <= 1,
                        "The former matching text has duplicate public markers")
                if original is None:
                    require(not indices,
                            "Matching review invented an earlier authored marker")
                    overlay["fields"].append(deepcopy(updated))
                else:
                    require(len(indices) == 1 and
                            identifier_rename.same_json_value_and_types(
                                overlay["fields"][indices[0]], original),
                            "Matching review borrowed a foreign published UTF-16 marker")
                    overlay["fields"][indices[0]] = deepcopy(updated)
        expected_overlay = overlay if (overlay["fields"] or overlay.get("rootFields")) else None
        require(identifier_rename.same_json_value_and_types(
                    expected_overlay, current.get(text_semantics.ROOT_FIELD)),
                "Matching review changed another public text marker or narration role")
    old_math = old_meta.get("mathTextRevisions", []) if old_v2 else []
    math_revisions = meta.get("mathTextRevisions", [])
    require(math_revisions[:len(old_math)] == old_math,
            "Math prompt archives are append-only and their original JSON cannot be rewritten")
    new_math_ids = set()
    for item in math_revisions[len(old_math):]:
        identity = item["id"]
        require(old_v2 and identity in before and identity in _blocks(baseline),
                "A math prompt archive must retain an already published active v2 owner")
        require(_math_ordered_json(_math_prompt_original(item)) == _math_ordered_json(before[identity]),
                "A new math prompt archive must retain the exact complete published block and JSON order")
        require(item["semanticBefore"] == _math_prompt_semantic_entry(baseline, identity),
                "The math prompt semantic archive differs from its exact published predecessor")
        new_math_ids.add(identity)
    for item in math_revisions:
        identity = item["id"]
        if identity in before and identity in after and (identity in new_math_ids
                or before[identity].get("prompt") != after[identity].get("prompt")):
            _require_math_prompt_authorship(baseline, current, identity)
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
        if "optionIndex" in item:
            require(item["semanticBefore"] == _choice_semantic_entry(
                baseline, item["id"], item["optionIndex"]),
                "A choice semantic archive differs from the exact prior lesson")
            if "semanticAfter" in item:
                require(item["semanticRootBefore"] ==
                        (text_semantics.ROOT_FIELD in baseline),
                        "A choice revision falsified the original semantic root")
    new_general_choices = [item for item in choices[len(old_choices):] if "optionIndex" in item]
    if new_general_choices:
        # A single response-text precision must not silently repaint another
        # source, move spans, or claim that a new sentence was already read.
        expected_semantics = deepcopy(baseline.get(text_semantics.ROOT_FIELD))
        expectation = {**baseline}
        if expected_semantics is not None:
            # A simultaneous retirement moves only its owners' marks into the
            # exact predecessor snapshot validated below. Other markers remain
            # protected by the existing choice-revision equality check.
            retiring_owners = {item["id"] for item in retired[len(old_retired):]}
            expected_semantics["fields"] = [field for field in expected_semantics["fields"]
                                           if field["block"] not in retiring_owners]
            expectation = {**baseline, text_semantics.ROOT_FIELD: expected_semantics}
        for item in new_general_choices:
            if "semanticAfter" in item:
                if expected_semantics is None:
                    expected_semantics = {"version": 1, "fields": []}
                    expectation = {**expectation, text_semantics.ROOT_FIELD: expected_semantics}
                marker = _choice_semantic_entry(expectation, item["id"], item["optionIndex"])
                if marker is None:
                    expected_semantics["fields"].append(deepcopy(item["semanticAfter"]))
                else:
                    marker.clear()
                    marker.update(deepcopy(item["semanticAfter"]))
            else:
                marker = _choice_semantic_entry(expectation, item["id"], item["optionIndex"])
                if marker is not None:
                    marker["sourceSha256"] = hashlib.sha256(
                        item["replacement"].encode("utf-8")).hexdigest()
        expected_semantics = _compose_ordinary_editorial_semantics(
            baseline, current, expected_semantics, {item["id"] for item in new_general_choices})
        require(expected_semantics == current.get(text_semantics.ROOT_FIELD),
                "Editorial choice revised an unrelated text marker or semantic span")
    new_closed_sources = _new_closed_compiler_sources(baseline, current)
    new_case_orders = _validate_compiler_case_order_transition(baseline, current)
    _preserve_compiler_channel_requirements(
        {identity: _compiler_case_order_projection(baseline, identity, block) for identity, block in before.items()},
        {identity: _compiler_case_order_projection(current, identity, block) for identity, block in after.items()})
    old_formats = old_meta.get("sourceFormatRevisions", []) if old_v2 else []
    current_formats = meta.get("sourceFormatRevisions", [])
    require(current_formats[:len(old_formats)] == old_formats,
            "Published source-format archives are append-only")
    archived = {entry["id"] for entry in old_formats}
    for entry in current_formats[len(old_formats):]:
        identity = entry["id"]
        require(identity in before and identity not in archived,
                "A new source-format archive must retain its original active owner")
        # A reader may import directly across an intermediate prose-only
        # release. Its actual pre-format snapshot is a later editorial node,
        # not necessarily the first published block, but its code is unchanged.
        predecessor = entry["block"]
        published_source = new_closed_sources.get(identity, before[identity])
        if predecessor != published_source:
            try:
                if _compiler_case_has_client(current, identity):
                    expected_source = _compiler_case_order_projection(
                        current, identity, published_source, editorial_prefix=len(old_editorial))
                    events = _compiler_case_events(current, identity)
                    epoch = _compiler_case_epoch(current, identity, predecessor, events)
                    for _, item in events[:epoch]:
                        if "client" in item or "recipe" in item:
                            source = _compiler_case_source_revision(item)
                            _compiler_client_replace_source(expected_source,
                                source["path"], source["source"])
                    require(code_format.code_slots(expected_source) == code_format.code_slots(predecessor),
                            "An intermediate client cannot format its source before archiving")
                else:
                    require(not code_format.require_source_lexical(
                                published_source, predecessor),
                            "An intermediate review cannot format its source before archiving")
                initial = (_compiler_case_order_projection(
                    current, identity, published_source, editorial_prefix=len(old_editorial))
                    if identity in new_closed_sources else
                    _compiler_case_order_projection(baseline, identity, published_source))
                require_source_format_with_editorial(
                    initial, _compiler_case_order_projection(current, identity, predecessor))
            except code_format.SourceFormatError as exc:
                raise LayoutError(f"Unsafe intermediate source for {identity}: {exc}") from exc
            require(fingerprint(predecessor) in {
                        proof["afterSha256"] for proof in meta["editorial"]
                        if proof["id"] == identity},
                    "An intermediate source snapshot lacks its authenticated editorial node")
        try:
            changed = (_compiler_case_format_changed(current, identity, predecessor)
                       if _compiler_case_has_client(current, identity) else
                       require_source_format_with_editorial(
                           _compiler_case_order_projection(current, identity, predecessor),
                           _compiler_case_order_projection(current, identity, after[identity])))
            require(changed,
                    "Source formatting archive did not change any source")
        except code_format.SourceFormatError as exc:
            raise LayoutError(f"Unsafe source edit for {identity}: {exc}") from exc
        archived.add(identity)
    for identity in archived:
        if identity not in before or identity not in after: continue
        published_source = new_closed_sources.get(
            identity, old_rename_blocks.get(identity, before[identity]))
        formatted_current = rename_blocks.get(identity, after[identity])
        try:
            if _compiler_case_has_client(current, identity):
                initial = (_compiler_case_order_projection(
                    current, identity, published_source, editorial_prefix=len(old_editorial))
                    if identity in new_closed_sources else
                    _compiler_case_order_projection(baseline, identity, published_source))
                code_format.require_source_lexical(
                    initial, _compiler_case_order_projection(current, identity, formatted_current))
            else:
                code_format.require_source_lexical(published_source, formatted_current)
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
                    "activityAfter": baseline.get("activityAfter", []),
                    "editorialPrefix": _retirement_editorial_prefix(baseline, len(old_meta["editorial"]))}
        # Existing six-field snapshots retain their bytes and pins. A newly
        # authored overlay, including an explicitly empty one, must be captured;
        # a new helper snapshot records null when the predecessor had none.
        if text_semantics.ROOT_FIELD in snapshot or text_semantics.ROOT_FIELD in baseline:
            expected[text_semantics.ROOT_FIELD] = baseline.get(text_semantics.ROOT_FIELD)
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
        # A prose-only revision must authenticate each changed reading field.
        # The compiler/source-format path has its separate combined guard;
        # requiring a new marker here also protects an unrelated drag-fill
        # explanation authored in the same multi-owner publication.
        bibliography_only = bibliography_revision.is_bibliography_only_revision(block, actual)
        # This precise gap concerns a newly introduced optional explanation.
        # Existing prose edits have their own curricular checks; requiring
        # their markers here would change the established rejection paths.
        new_optional = (not bibliography_only and
                        block.get("type") in ORDINARY_EDITORIAL_TYPES and
                        "explanation" not in block and "explanation" in actual)
        if new_optional:
            require(type(actual["explanation"]) is str and bool(actual["explanation"].strip()),
                    "An optional explanation must contain nonblank authored prose")
            current_overlay = current.get(text_semantics.ROOT_FIELD) or {}
            current_marks = {(entry["block"], entry["path"]): entry
                             for entry in current_overlay.get("fields", [])}
            require((identity, "/explanation") in current_marks,
                    "An optional explanation needs its exact authored text descriptor")
        if bibliography_only:
            require(identity in appended and all("bibliography" in proof for proof in appended[identity]),
                    "A new bibliography-only revision requires its exact authored reading archive")
        protected = _compiler_case_order_projection(
            current, identity, matching_before.get(
                identity, rename_blocks.get(identity, actual)))
        previous_source = _compiler_case_order_projection(
            baseline, identity, old_rename_blocks.get(identity, block))
        format_original = next((entry["block"] for entry in current_formats if entry["id"] == identity), None)
        if format_original is not None:
            if identity in new_closed_sources:
                # Expansions can revise initialCode and cases together. Use the
                # complete B endpoint; B source combined with A cases is no
                # authenticated compiler contract.
                previous_source = _compiler_case_order_projection(
                    current, identity, new_closed_sources[identity],
                    editorial_prefix=len(old_editorial))
            format_original = _compiler_case_format_origin(current, identity, format_original)
            previous_source = code_format.source_projection(previous_source, format_original)
            protected = code_format.source_projection(protected, format_original)
        if identity in archived:
            _require_formatted_question_authorship(
                baseline, current, identity, block, actual)
        previous_review = _historical_choice_contract(baseline, identity, previous_source)
        current_review = _historical_choice_contract(current, identity, protected)
        previous_review = _historical_math_text_contract(baseline, identity, previous_review)
        current_review = _historical_math_text_contract(current, identity, current_review)
        require(state_fingerprint(_historical_order_contract(baseline, identity, previous_review))
                == state_fingerprint(_historical_order_contract(current, identity, current_review)),
                "An existing response/code contract changed")
        if str(block.get("type", "")).startswith("math_"):
            require(_math_ordered_json(historical_math_payload(previous_review))
                    == _math_ordered_json(historical_math_payload(current_review)),
                    "Mathematical payloads must preserve their historical JSON key order and values")
        old_hash, new_hash = fingerprint(block), fingerprint(actual)
        proofs = appended.get(identity, [])
        if old_hash != new_hash or proofs:
            require(proofs and proofs[0]["beforeSha256"] == old_hash
                    and proofs[-1]["afterSha256"] == new_hash,
                    "A simultaneous editorial revision needs its explicit published before/after proof")
            require(old_hash != new_hash or any("returnTo" in proof for proof in proofs),
                    "An unchanged block cannot acquire an invented editorial history")
        if new_hash in owners and identity not in owners[new_hash]:
            # A signed, lexical-only reformat can make two distinct reference
            # blocks byte-identical. Their stable owners have not moved: the
            # original full block, source tokens and editorial edges are pinned.
            # An unarchived replacement still fails closed as a foreign owner.
            require(identity in new_case_orders or (identity in archived
                    and require_source_format_with_editorial(
                        new_closed_sources.get(identity, block), actual)),
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
    require(not baseline.get("compilerNamedRuleChecks") or
            baseline.get("compilerNamedRuleChecks") == current.get("compilerNamedRuleChecks"),
            "Published named-rule requirements cannot be removed or rewritten")
    require(not baseline.get("compilerOpenBatteryChecks") or
            (type(current.get("compilerOpenBatteryChecks")) is list and
             current["compilerOpenBatteryChecks"][:len(baseline["compilerOpenBatteryChecks"])] ==
             baseline["compilerOpenBatteryChecks"]),
            "Published open battery checks cannot be removed, reordered or rewritten")
    require(not baseline.get("compilerReductionChecks") or
            (type(current.get("compilerReductionChecks")) is list and
             current["compilerReductionChecks"][:len(baseline["compilerReductionChecks"])] ==
             baseline["compilerReductionChecks"]),
            "Published reduction checks cannot be removed, reordered or rewritten")
    require(not baseline.get("compilerCmakeGraphChecks") or
            (type(current.get("compilerCmakeGraphChecks")) is list and
             current["compilerCmakeGraphChecks"][:len(baseline["compilerCmakeGraphChecks"])] ==
             baseline["compilerCmakeGraphChecks"]),
            "Published CMake graph requirements cannot be removed, reordered or rewritten")
    require(not baseline.get("orderVisualSeparators") or
            baseline["orderVisualSeparators"] == current.get("orderVisualSeparators"),
            "Published visual separators cannot lose an established source owner")
    require(not baseline.get("orderEquivalenceGroups") or
            (type(current.get("orderEquivalenceGroups")) is list and
             current["orderEquivalenceGroups"][:len(baseline["orderEquivalenceGroups"])] ==
             baseline["orderEquivalenceGroups"]),
            "Published order equivalence groups cannot be removed, reordered or weakened")
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
    blocks = {identity: _compiler_case_order_realign(old, current, identity, block)
              if _compiler_case_events(current, identity) else block
              for identity, block in blocks.items()}
    old_compilers = _compiler_revisions(old) | _compiler_references(old)
    blocks = {identity: _historical_compiler_contract(current, identity, block)
              if identity not in old_compilers else block for identity, block in blocks.items()}
    new_math_ids = _math_text_revisions(current).keys() - _math_text_revisions(old).keys()
    blocks = {identity: _historical_math_text_contract(current, identity, block)
              if identity in new_math_ids else block for identity, block in blocks.items()}
    new_match = {
        item["id"]: parse_json(item["rawJson"])
        for item in current["stateLayout"].get("mathMatchingTextRevisions", [])
        if item["id"] not in {
            old_item["id"] for old_item in old["stateLayout"].get("mathMatchingTextRevisions", [])
        }
    }
    blocks = {identity: deepcopy(new_match.get(identity, block))
              for identity, block in blocks.items()}
    new_renames = {
        item["id"]: item["block"]
        for item in current["stateLayout"].get("identifierRenameRevisions", [])
        if item["id"] not in {
            old_item["id"] for old_item in old["stateLayout"].get("identifierRenameRevisions", [])
        }
    }
    blocks = {identity: deepcopy(new_renames.get(identity, block))
              for identity, block in blocks.items()}
    page_by_id = {record["id"]: page for page, record in zip(current["pages"], current["stateLayout"]["current"])}
    result = deepcopy(current)
    new_choices = _choice_revisions(current).keys() - old_choices.keys()
    new_general_choices = {item["id"] for item in current["stateLayout"].get("choiceRevisions", [])
                           if item["id"] in new_choices and "optionIndex" in item}
    if new_general_choices:
        # The source block above has been restored to the prior wording.
        # Its descriptor must follow that SAME prior wording. A later option
        # with newly moved marks cannot leak its text hash or old reading.
        overlay = _general_choice_semantic_projection(
            current, identities=new_general_choices, preserve_unrelated_fields=True)
        if overlay is None:
            result.pop(text_semantics.ROOT_FIELD, None)
        else:
            result[text_semantics.ROOT_FIELD] = overlay
    if new_math_ids and text_semantics.ROOT_FIELD in result:
        overlay = _math_prompt_semantic_projection(
            current, result[text_semantics.ROOT_FIELD], identities=new_math_ids)
        if overlay is None:
            result.pop(text_semantics.ROOT_FIELD)
        else:
            result[text_semantics.ROOT_FIELD] = overlay
    if new_match and text_semantics.ROOT_FIELD in result:
        import math_matching_text_compat as matching_compat
        overlay = matching_compat.historical_matching_text_semantics(
            current, result[text_semantics.ROOT_FIELD], identities=set(new_match))
        if overlay is None:
            result.pop(text_semantics.ROOT_FIELD)
        else:
            result[text_semantics.ROOT_FIELD] = overlay
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
    math_match_before = {
        item["id"]: parse_json(item["rawJson"])
        for item in document["stateLayout"].get("mathMatchingTextRevisions", [])
    }
    blocks = {identity: math_match_before.get(identity, block)
              for identity, block in blocks.items()}
    renamed_before = {
        item["id"]: item["block"]
        for item in document["stateLayout"].get("identifierRenameRevisions", [])
    }
    blocks = {identity: _compiler_case_order_projection(
        document, identity, renamed_before.get(identity, block))
              for identity, block in blocks.items()}
    blocks = {identity: _historical_order_contract(document, identity, block) for identity, block in blocks.items()}
    blocks = {identity: _historical_choice_contract(document, identity, block) for identity, block in blocks.items()}
    blocks = {identity: _historical_compiler_contract(document, identity, block) for identity, block in blocks.items()}
    blocks = {identity: _historical_math_text_contract(document, identity, block) for identity, block in blocks.items()}
    shells = {record["id"]: {key: value for key, value in page.items() if key != "blocks"}
              for page, record in zip(document["pages"], document["stateLayout"]["current"])}
    result = deepcopy(_without_layout(document))
    if text_semantics.ROOT_FIELD in result:
        overlay = _math_prompt_semantic_projection(document, _general_choice_semantic_projection(document))
        if math_match_before and overlay is not None:
            import math_matching_text_compat as matching_compat
            overlay = matching_compat.historical_matching_text_semantics(document, overlay)
        if overlay is None:
            result.pop(text_semantics.ROOT_FIELD)
        else:
            result[text_semantics.ROOT_FIELD] = overlay
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
    # The separator prevents Git from also statting SHA:path as a working-tree
    # filename, which can exceed Windows MAX_PATH even when the real path fits.
    result = subprocess.run(["git", "show", commit + ":" + relative, "--"], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0,
            "Cannot read baseline lesson " + relative + ": " + result.stderr.strip())
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
            literal = (course_edition.literal_layout(source, pages=_pages, state_fingerprint=state_fingerprint)
                       if "courseEdition" in source else prepare(source)["stateLayout"])
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
    editions = (any("courseEdition" in document or "__edition_" in str(document.get("id", ""))
                    for document in documents.values())
                or any("courseEdition" in load(path)
                       for path in content.glob("*/*/*/course_*.json"))
                or any("courseEdition" in load(path) for path in content.rglob("unit_*.json")))
    require(not editions or base_ref is not None,
            "courseEdition publication requires --base-ref with the verified remote HEAD")
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
    edition_report = course_edition.check_repository(repository, commit, documents=documents)
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
    return {"base_commit": commit, "mapped_lessons": len(mapped), "families": checked_families,
            **edition_report}


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
    parser.add_argument("action", choices=("prepare", "prepare-edition", "reorder", "refine-order-lines", "revise-choice-option", "revise-choice-editorial", "revise-ordinary-editorial", "revise-math-prompt-editorial", "revise-compiler-cases", "revise-compiler-client-cases", "revise-compiler-cmake-recipe", "reorder-compiler-cases", "check", "check-tree"))
    parser.add_argument("source", type=Path, help="Lesson JSON; repository directory for check-tree")
    parser.add_argument("--plan", type=Path, help="JSON list of stable page/block IDs in display order")
    parser.add_argument("--output", type=Path, help="New path; never overwrite a Work/Codex file")
    parser.add_argument("--previous", type=Path, help="Required published lesson for prepare/reorder/check")
    parser.add_argument("--base-ref", help="Required verified remote HEAD for check-tree")
    parser.add_argument("--locale", choices=sorted(LOCALES), help="Use v2 and read all published language assets from Git")
    parser.add_argument("--additions", type=Path,
                        help="Authoring JSON list: id, storage, progressPage and block; requires v2 and --plan")
    parser.add_argument("--identity", help="Existing reviewed stable identity for a refinement/revision")
    parser.add_argument("--case-order", type=Path, help="JSON permutation: new case index to previous case index")
    parser.add_argument("--cases", type=Path, help="Explicit replacement outputCheck case list")
    parser.add_argument("--client", type=Path, help='Explicit client JSON object: {"path":"main.cpp","source":"..."}')
    parser.add_argument("--recipe", type=Path, help='Explicit existing CMake recipe JSON object: {"path":"CMakeLists.txt","source":"..."}')
    parser.add_argument("--case-semantics", type=Path, help="Explicit replacement case descriptors, including []")
    parser.add_argument("--option-index", type=int, help="Index of the existing multiple-choice option to clarify")
    parser.add_argument("--replacement", help="Exact revised nonblank choice or math prompt text")
    parser.add_argument("--semantic-spans", type=Path,
                        help="Explicit JSON list of UTF-16 spans for a revised math prompt or choice option")
    parser.add_argument("--return-to", type=int,
                        help="Explicit earlier full-block revision for a mathematical prompt or compiler case-order return")
    parser.add_argument("--editorial-fields", type=Path,
                        help='JSON {"question":{"text":"...","spans":[...]},"explanation":{...},"returnTo":3}; optional explicit return')
    parser.add_argument("--editorial-return", action="append", default=[], metavar="ID:REVISION",
                        help="Explicit earlier payload of the same identity; prepare with --locale and Git --base-ref only")
    args = parser.parse_args()
    try:
        require(args.action == "revise-choice-editorial" or args.option_index is None,
                "An option index is only valid for revise-choice-editorial")
        require(args.action in {"revise-choice-editorial", "revise-math-prompt-editorial"}
                or args.replacement is None,
                "Replacement text belongs to a choice or mathematical prompt revision")
        require(args.action in {"revise-math-prompt-editorial", "revise-choice-editorial"}
                or args.semantic_spans is None,
                "Explicit semantic spans belong only to a mathematical prompt or choice revision")
        require(args.action in {"revise-math-prompt-editorial", "reorder-compiler-cases", "revise-compiler-cases", "revise-compiler-client-cases", "revise-compiler-cmake-recipe"}
                or args.return_to is None, "Return-to belongs to a supported authenticated editorial return")
        require(args.action in {"revise-compiler-cases", "revise-compiler-client-cases"}
                or (args.cases is None and args.case_semantics is None),
                "Replacement cases and their semantics belong only to explicit case revisions")
        require(args.action == "revise-compiler-client-cases" or args.client is None,
                "An explicit client belongs only to revise-compiler-client-cases")
        require(args.action == "revise-compiler-cmake-recipe" or args.recipe is None,
                "An explicit recipe belongs only to revise-compiler-cmake-recipe")
        require(args.action == "reorder-compiler-cases" or args.case_order is None,
                "Case order belongs only to reorder-compiler-cases")
        require(args.action == "revise-ordinary-editorial" or args.editorial_fields is None,
                "The authored public fields belong only to revise-ordinary-editorial")
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
        if args.action == "prepare-edition":
            require(args.base_ref is not None and args.output is not None,
                    "prepare-edition requires --base-ref and an exclusive --output")
            require(args.previous is None and args.plan is None and args.additions is None
                    and args.identity is None and not editorial_returns,
                    "A new edition cannot claim a previous lesson, reorder or editorial migration")
            result = course_edition.prepare_from_repository(args.source, args.base_ref, locale=args.locale)
            with args.output.open("x", encoding="utf-8", newline="\n") as out:
                json.dump(result, out, ensure_ascii=False, indent=2, allow_nan=False)
                out.write("\n")
            print("Prepared a new edition source without changing the authored asset or student data. "
                  "Publish its course index, unit indices and complete lessons atomically; "
                  "run check-tree against the verified base and reopen the Windows app.")
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
        elif args.action == "revise-choice-editorial":
            require(args.identity is not None and args.option_index is not None
                    and args.replacement is not None and args.plan is None
                    and args.additions is None,
                    "revise-choice-editorial requires --identity, --option-index and "
                    "--replacement without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_choice_editorial(
                baseline, args.identity, args.option_index, args.replacement,
                spans=load(args.semantic_spans) if args.semantic_spans is not None else None)
        elif args.action == "revise-math-prompt-editorial":
            require(args.identity is not None and args.replacement is not None
                    and args.semantic_spans is not None and args.plan is None and args.additions is None,
                    "revise-math-prompt-editorial requires --identity, --replacement and "
                    "--semantic-spans without move/addition plans")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_math_prompt_editorial(baseline, args.identity, args.replacement,
                                                  spans=load(args.semantic_spans), return_to=args.return_to)
        elif args.action == "revise-ordinary-editorial":
            require(args.identity is not None and args.editorial_fields is not None
                    and args.plan is None and args.additions is None,
                    "revise-ordinary-editorial requires --identity and --editorial-fields "
                    "without move/addition plans")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_ordinary_editorial(baseline, args.identity, load(args.editorial_fields))
        elif args.action == "reorder-compiler-cases":
            require(args.identity is not None and args.case_order is not None
                    and args.plan is None and args.additions is None,
                    "reorder-compiler-cases requires --identity and --case-order without move/addition plans")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = reorder_compiler_cases(baseline, args.identity, load(args.case_order), return_to=args.return_to)
        elif args.action == "revise-compiler-cmake-recipe":
            require(args.identity is not None and args.recipe is not None
                    and args.plan is None and args.additions is None,
                    "revise-compiler-cmake-recipe requires --identity and --recipe without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_compiler_cmake_recipe(baseline, args.identity, load(args.recipe),
                                                  return_to=args.return_to)
        elif args.action == "revise-compiler-client-cases":
            require(args.identity is not None and args.client is not None
                    and args.cases is not None and args.case_semantics is not None
                    and args.plan is None and args.additions is None,
                    "revise-compiler-client-cases requires --identity, --client, --cases and "
                    "--case-semantics without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            result = revise_compiler_client_cases(baseline, args.identity, load(args.client), load(args.cases),
                case_semantics=load(args.case_semantics), return_to=args.return_to)
        elif args.action == "revise-compiler-cases":
            require(args.identity is not None and args.plan is None and args.additions is None,
                    "revise-compiler-cases requires --identity without a move/addition plan")
            baseline = prepare_v2(source, locale, family, previous) if family is not None else source
            if args.cases is None and args.case_semantics is None:
                require(args.return_to is None, "A closed compiler revision cannot claim a general return")
                result = revise_compiler_cases(baseline, args.identity)
            else:
                require(args.cases is not None and args.case_semantics is not None,
                        "General compiler cases require both --cases and --case-semantics")
                result = revise_compiler_cases(baseline, args.identity, load(args.cases),
                    case_semantics=load(args.case_semantics), return_to=args.return_to)
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

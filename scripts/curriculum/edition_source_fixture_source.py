"""Export 18 authenticated special-objective lessons and exact new-edition copies.

The provider reads only pinned local Git objects. It never fetches, reads a live
asset as fallback, edits curriculum or inherits historical progress. All exported
programs identify their published source; the few derived attempts name their
exact QA-only mutation. Native Windows QA owns execution and grading.
"""
from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import course_edition
import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches

ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "b92d4d487b7f7deb9a1ff056ddeb6ea9e256d660"
KIND = "course-edition-source"
CONTENT = "content/es/programming/cpp_fundamentals"
DESCRIPTOR = {
    "version": 1, "courseId": "cpp_fundamentals", "locale": "es",
    "generation": 1, "id": "cpp_desde_cero_2026",
}

PUBLICATIONS = {
    "u6l2": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_declaraciones_y_firmas.json",
        "unit": "funciones_y_contratos",
        "sourceUnitNumber": 6,
        "sourceLessonNumber": 2,
        "sha256": "e9c67087a6c2ba34ee7cd5ea4db0700ab1e52124234011627936cc804de98f18",
        "blobSha": "cfabc5f85ed50f1cb40023774b2094cc9c623f03",
        "blocks": [
            {
                "owner": "block_0014_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-difference-unselected-definition",
                "negativeOwner": "block_0006_0002"
            },
            {
                "owner": "block_0015_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-difference-conflicting-return",
                "negativeOwner": "block_0006_0002"
            },
            {
                "owner": "block_0017_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-difference-missing-header",
                "negativeOwner": "block_0006_0002"
            }
        ]
    },
    "u6l19": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json",
        "unit": "funciones_y_contratos",
        "sourceUnitNumber": 6,
        "sourceLessonNumber": 19,
        "sha256": "d566ea60c14409b92bd9e09231a9d12a545b0b0e5c68bdd94b04b84d99366b80",
        "blobSha": "b8113443a50bcdc4f0e861690911150e43e47bef",
        "blocks": [
            {
                "owner": "block_0016_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-envelopes-missing-header",
                "negativeOwner": "block_0012_0002"
            },
            {
                "owner": "block_0017_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-envelopes-missing-declaration",
                "negativeOwner": "block_0012_0002"
            },
            {
                "owner": "block_0018_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-envelopes-unselected-definitions",
                "negativeOwner": "block_0012_0002"
            },
            {
                "owner": "block_0019_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-envelopes-duplicate-definition",
                "negativeOwner": "block_0012_0002"
            },
            {
                "owner": "block_0021_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-series-unknown-call",
                "negativeOwner": "block_0013_0002"
            },
            {
                "owner": "block_0022_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-series-unselected-definitions",
                "negativeOwner": "block_0013_0002"
            },
            {
                "owner": "block_0023_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-series-duplicate-definition",
                "negativeOwner": "block_0013_0002"
            }
        ]
    },
    "u6l20": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json",
        "unit": "funciones_y_contratos",
        "sourceUnitNumber": 6,
        "sourceLessonNumber": 20,
        "sha256": "c89c2afac52787151092d364a9c8427d647ae7f00853a6c7083aa3ba4a4627eb",
        "blobSha": "1eefe77bb95e1ec21fa4612d08fe05f412fd9627",
        "blocks": [
            {
                "owner": "block_0013_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-shelf-unselected-definition",
                "negativeOwner": "block_0012_0002"
            },
            {
                "owner": "block_0014_0002",
                "kind": "project-diagnostic",
                "diagnostic": "u6-shelf-conflicting-return",
                "negativeOwner": "block_0012_0002"
            }
        ]
    },
    "u9l6": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json",
        "unit": "datos_propios_y_estado_valido",
        "sourceUnitNumber": 9,
        "sourceLessonNumber": 6,
        "sha256": "48a2fe71fa4d28d3388964bd7bef12658d83037c282ae2df6accf5205c9c8be4",
        "blobSha": "3b5047ee12a4e2074c434433b092ae390489f671",
        "blocks": [
            {
                "owner": "block_0006_0006",
                "kind": "diagnostic",
                "diagnostic": "const-member-call"
            }
        ]
    },
    "u9l10": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json",
        "unit": "datos_propios_y_estado_valido",
        "sourceUnitNumber": 9,
        "sourceLessonNumber": 10,
        "sha256": "9d2e6fad4d0facc99d31c3322b82d8f2e38598df1a56b1d440e9d39cd2e9012e",
        "blobSha": "51d0d45dfeb080304b14496faea84f7377820eea",
        "blocks": [
            {
                "owner": "block_0012_0008",
                "kind": "diagnostic",
                "diagnostic": "explicit-copy-initialization"
            },
            {
                "owner": "block_0012_0011",
                "kind": "diagnostic",
                "diagnostic": "explicit-argument-conversion"
            },
            {
                "owner": "block_0014_0011",
                "kind": "diagnostic",
                "diagnostic": "explicit-assignment"
            }
        ]
    },
    "u9l11": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_argumentos_con_tipos_que_expresan_su_funcion.json",
        "unit": "datos_propios_y_estado_valido",
        "sourceUnitNumber": 9,
        "sourceLessonNumber": 11,
        "sha256": "adf6588bdb1dd865d894227b041189431ea261a8fe4fab0bf47983c64fe17bcd",
        "blobSha": "7a7e9399a0f42a3b482656a132c890c111369104",
        "blocks": [
            {
                "owner": "block_0006_0006",
                "kind": "diagnostic",
                "diagnostic": "swapped-named-arguments"
            }
        ]
    },
    "u9l13": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json",
        "unit": "datos_propios_y_estado_valido",
        "sourceUnitNumber": 9,
        "sourceLessonNumber": 13,
        "sha256": "94f62385157f5dc4684ac03bba40eca9cc580023853323e9652c79cfa4dab30e",
        "blobSha": "3e4b20699e0d50914d4935fc542ca70c277839d6",
        "blocks": [
            {
                "owner": "block_0007_0001",
                "kind": "matching"
            }
        ]
    },
    "u11l5": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_estados_del_flujo_y_datos_validos.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 5,
        "sha256": "32022398a4c57cbfaa90ab20e61af1b357bf4cc3a04f6e8a90e5bdb925b724c9",
        "blobSha": "988cc552fec73db94772a9b192ecd0b8fe3f38c2",
        "blocks": [
            {
                "owner": "block_0006_0004",
                "kind": "choice"
            }
        ]
    },
    "u11l6": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 6,
        "sha256": "0b6f85938324d2bc22f029429eac311847cbabdf882b6f455c19e18e7356fa36",
        "blobSha": "830cf7b83838d728fc8c5c7933d5242ffd845cbf",
        "blocks": [
            {
                "owner": "block_0005_0004",
                "kind": "choice"
            }
        ]
    },
    "u11l7": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_lector_de_entrada_preparada.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 7,
        "sha256": "d4b2fe8c38730312b273e2faa98f1ee6609ca0f028816f7ac59589be0a7ff4ba",
        "blobSha": "27e64795c3c5274104a73c6ea9842252253c40bb",
        "blocks": [
            {
                "owner": "block_0000_0004",
                "kind": "choice"
            },
            {
                "owner": "block_0003_0005",
                "kind": "output",
                "referenceOwner": "block_0005_0002"
            }
        ]
    },
    "u11l8": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 8,
        "sha256": "5cf2807129ac1b3fbb42fe0dea9576db74a7d5a47db32e89dea386f24ccbf870",
        "blobSha": "41faf89751a8f641c81cba23ad673a2686ea372a",
        "blocks": [
            {
                "owner": "block_0003_0002",
                "kind": "choice"
            },
            {
                "owner": "block_0004_0004",
                "kind": "output",
                "referenceOwner": "block_0007_0001"
            },
            {
                "owner": "block_0006_0001",
                "kind": "choice"
            },
            {
                "owner": "block_0007_0001",
                "kind": "reference",
                "relatedOwner": "block_0004_0004"
            }
        ]
    },
    "u11l14": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 14,
        "sha256": "65b762c8cff56c1fbf5c33094be61dc3d0e989ad1798d110cf80344b21e91393",
        "blobSha": "f3ce85c2af3e1ce34c2490a31533002903452a46",
        "blocks": [
            {
                "owner": "block_0003_0004",
                "kind": "choice"
            },
            {
                "owner": "block_0004_0004",
                "kind": "output",
                "referenceOwner": "block_0008_0001"
            },
            {
                "owner": "block_0007_0001",
                "kind": "choice"
            },
            {
                "owner": "block_0008_0001",
                "kind": "reference",
                "relatedOwner": "block_0004_0004"
            }
        ]
    },
    "u11l15": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 15,
        "sha256": "7b8704f4d738e56cb873358929b36b49ca9f159f2652fa414470395c1408f1a9",
        "blobSha": "ea0626e2cd80e3be7055d4bf6367b2e3f5edb1e2",
        "blocks": [
            {
                "owner": "block_0002_0002",
                "kind": "output",
                "referenceOwner": "block_0009_0001"
            },
            {
                "owner": "block_0006_0003",
                "kind": "output",
                "referenceOwner": "block_0010_0001"
            },
            {
                "owner": "block_0008_0002",
                "kind": "matching"
            },
            {
                "owner": "block_0009_0001",
                "kind": "reference",
                "relatedOwner": "block_0002_0002"
            },
            {
                "owner": "block_0010_0001",
                "kind": "reference",
                "relatedOwner": "block_0006_0003"
            }
        ]
    },
    "u11l16": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 16,
        "sha256": "b7179c7e6a1e5e0a51106878a556a08b7a7606b0552910b97c1d16651db6b205",
        "blobSha": "262ffbe3620aebf388355817fa12d7f7b4e4124e",
        "blocks": [
            {
                "owner": "block_0003_0004",
                "kind": "output",
                "referenceOwner": "block_0006_0001"
            },
            {
                "owner": "block_0006_0001",
                "kind": "reference",
                "relatedOwner": "block_0003_0004"
            }
        ]
    },
    "u11l18": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_una_tabla_persistente_de_nombres.json",
        "unit": "texto_interpretacion_y_flujos",
        "sourceUnitNumber": 11,
        "sourceLessonNumber": 18,
        "sha256": "1ba0496e8d1afafb5d79556112f8d2d91336925b775593e1aef7db6dd9b7b26a",
        "blobSha": "d6e6a7edb5cff98fe1bbcc4699d8ba1fc765690c",
        "blocks": [
            {
                "owner": "block_0000_0003",
                "kind": "choice"
            }
        ]
    },
    "u12l13": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_compartir_flujos_sin_acoplar_el_calculo.json",
        "unit": "archivos_y_programa_completo",
        "sourceUnitNumber": 12,
        "sourceLessonNumber": 13,
        "sha256": "793b441f408f7645fc2b22024cfb8d10bfe28c5d1afc07312ea52969532a206d",
        "blobSha": "a33359fbe803ab156e9c011dcd7a7064b0cd1520",
        "blocks": [
            {
                "owner": "block_0005_0002",
                "kind": "intentional-exit"
            }
        ]
    },
    "u12l20": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json",
        "unit": "archivos_y_programa_completo",
        "sourceUnitNumber": 12,
        "sourceLessonNumber": 20,
        "sha256": "c81c2bdc6faf28736d5208183ca8ca224c54813fc74b3e79127251aa93d1e44f",
        "blobSha": "46d01275dbff424ce326e62385e07d5ac034b374",
        "blocks": [
            {
                "owner": "block_0005_0004",
                "kind": "crt-output",
                "referenceOwner": "block_0008_0001"
            }
        ]
    },
    "u12l26": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_construir_un_programa_de_carga_y_guardado.json",
        "unit": "archivos_y_programa_completo",
        "sourceUnitNumber": 12,
        "sourceLessonNumber": 26,
        "sha256": "73eb2577ac3ed1622aeee92ed54204994927309a7c3c8d4cb0b3daaa4e83f18f",
        "blobSha": "fdd4a0d281ef7b5fa9349bee6f9dc3ac49be4db6",
        "blocks": [
            {
                "owner": "block_0004_0017",
                "kind": "intentional-exit"
            }
        ]
    }
}

INDEXES = {
    "course": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/course_cpp_fundamentals.json",
        "sha256": "f7557d5dfdcf2b644f6a511e128729f9d5a2cd85ade8bdbce7393c423f442576",
        "blobSha": "a26324ca155efcbece1cf731348b0ff7b2367cab"
    },
    "funciones_y_contratos": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/unit_funciones_y_contratos.json",
        "sha256": "1f1462fa433e031da6e7f98364a8c86bca12fbd32fda81329db417dae1cf4e5c",
        "blobSha": "2b791c555e797a11fc3e710c53f2b386784c5ade"
    },
    "datos_propios_y_estado_valido": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/unit_datos_propios_y_estado_valido.json",
        "sha256": "33ad0ac662ad17bd18f929d32f3d961b245ed919e6bbb5b6219292cd865054c9",
        "blobSha": "92edfe9d68067d2332866981915669076098e530"
    },
    "texto_interpretacion_y_flujos": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/unit_texto_interpretacion_y_flujos.json",
        "sha256": "3d08a42986ff43f67cf2ace819310b513af08345426445c557e2aae22a9e99ed",
        "blobSha": "874955d60c24a9e11ab65e6ff154d43625a4be63"
    },
    "archivos_y_programa_completo": {
        "path": "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/unit_archivos_y_programa_completo.json",
        "sha256": "f0824cdc91282535c3bfd6f49b3d9d0de6ca6ed59338e245f3f2bdffd6e1dbfb",
        "blobSha": "1cd804e0b6c5120abac9cfcaf48843f4cda23914"
    }
}

DIAGNOSTIC_STATEMENTS = {
    "const-member-call": "fija.cambiar_a(4,2);",
    "explicit-copy-initialization": "Contador no_permitido = 3;",
    "explicit-argument-conversion": "leer(3);",
    "explicit-assignment": "destino = 3;",
    "swapped-named-arguments": "const Caja mal{Alto{4}, Ancho{3}};",
}
EXPECTED_KINDS = {
    "project-diagnostic": 12, "diagnostic": 5, "choice": 8,
    "matching": 2, "output": 6, "intentional-exit": 2,
    "crt-output": 1, "reference": 5,
}


def sha256(payload: bytes | str) -> str:
    return hashlib.sha256(payload.encode("utf-8") if isinstance(payload, str) else payload).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def verify_bytes(spec: dict, payload: bytes) -> bytes:
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    if sha256(payload) != spec["sha256"] or blob != spec["blobSha"]:
        raise ValueError("Edition-source fixture differs from its complete pinned publication: " + spec["path"])
    return payload


def _published(spec: dict) -> bytes:
    reference = PUBLISHED_COMMIT + ":" + spec["path"]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError("Edition-source fixtures require pinned local Git objects; "
                           "no fetch or current-asset fallback is permitted") from error
    return verify_bytes(spec, payload)


def published_bytes(name: str) -> bytes:
    return _published(PUBLICATIONS[name])


def published_index_bytes(name: str) -> bytes:
    return _published(INDEXES[name])


def _blocks(document: dict) -> dict[str, dict]:
    result = {}
    for page, row in zip(document["pages"], document["stateLayout"]["current"], strict=True):
        for block, owner in zip(page["blocks"], row["blocks"], strict=True):
            if owner in result:
                raise ValueError("A pinned publication repeats a block owner")
            result[owner] = block
    return result


def _attempt(blocks: dict[str, dict], owner: str, field: str, label: str,
             accepted: bool, standard_input: str, *, mutation: dict | None = None,
             project: bool = False) -> dict:
    block = blocks[owner]
    original = block[field]
    source = original
    if mutation:
        if mutation["kind"] == "insert-diagnostic-statement":
            marker = "    return 0;"
            at = source.rfind(marker)
            if at < 0 or source.count("int main()") != 1 or mutation["statement"] in source:
                raise ValueError("The authentic diagnostic starter lost its unique insertion point")
            source = source[:at] + "    " + mutation["statement"] + "\n" + source[at:]
        elif mutation["kind"] == "normal-exit-wrapper":
            marker = "int main()"
            if source.count(marker) != 1 or "edition_source_original_main" in source:
                raise ValueError("The authentic exit source lost its unique main")
            source = source.replace(marker, "int edition_source_original_main()", 1)
            source += "\nint main() { edition_source_original_main(); return 0; }\n"
        else:
            raise ValueError("Unknown declared QA source mutation")
    provenance = {"owner": owner, "field": field, "sourceSha256": sha256(original)}
    if mutation:
        provenance["mutation"] = deepcopy(mutation)
    result = {
        "label": label, "accepted": accepted, "source": source,
        "sourceSha256": sha256(source), "standardInput": standard_input,
        "provenance": provenance,
    }
    if project:
        files = deepcopy(block["projectFiles"])
        selected = deepcopy(block["sourceFiles"])
        if [item["initialCode"] for item in files if item["path"] == "main.cpp"] != [source]:
            raise ValueError("A published project's main.cpp differs from its complete editor source")
        result["projectFiles"] = files
        result["sourceFiles"] = selected
        result["projectFileSha256"] = {item["path"]: sha256(item["initialCode"]) for item in files}
    return result


def block_manifest(name: str, original: dict) -> list[dict]:
    """Inventory the exact 41 reviewed owners without deriving it from live allowlists."""
    blocks = _blocks(original)
    entries = []
    for spec in PUBLICATIONS[name]["blocks"]:
        owner, kind = spec["owner"], spec["kind"]
        block = blocks[owner]
        row = {"owner": owner, "kind": kind, "type": block["type"],
               "blockFingerprint": state.fingerprint(block)}
        if kind in {"project-diagnostic", "diagnostic"}:
            if block.get("compileCheck", {}).get("diagnostic") != spec["diagnostic"]:
                raise ValueError("A pinned diagnostic changed its exact objective")
            row["diagnostic"] = spec["diagnostic"]
            if kind == "project-diagnostic":
                row["attempts"] = [
                    _attempt(blocks, owner, "initialCode", "published-diagnostic", True, "", project=True),
                    _attempt(blocks, spec["negativeOwner"], "initialCode",
                             "published-valid-project", False, "", project=True),
                ]
            else:
                row["attempts"] = [
                    _attempt(blocks, owner, "initialCode", "required-statement", True, "",
                             mutation={"kind": "insert-diagnostic-statement",
                                       "statement": DIAGNOSTIC_STATEMENTS[spec["diagnostic"]]}),
                    _attempt(blocks, owner, "initialCode", "published-starter", False, ""),
                ]
        elif kind == "choice":
            correct = block["correctIndex"]
            if type(correct) is not int or not 0 <= correct < len(block["options"]):
                raise ValueError("A published choice has no authentic correct option")
            row.update(correctIndex=correct,
                       incorrectIndex=next(i for i in range(len(block["options"])) if i != correct),
                       optionCount=len(block["options"]))
        elif kind == "matching":
            if len(block["responses"]) != 1:
                raise ValueError("The reviewed matching must preserve its original single response")
            response = block["responses"][0]
            if response["type"] != "math_matching_drag" or response["validator"]["type"] != "matching":
                raise ValueError("The reviewed label no longer belongs to its exact matching")
            correct = deepcopy(response["validator"]["expected"])
            keys = list(correct)
            if len(keys) < 2 or len(set(correct.values())) != len(correct):
                raise ValueError("The matching contrast needs two different authentic targets")
            incorrect = deepcopy(correct)
            incorrect[keys[0]], incorrect[keys[1]] = incorrect[keys[1]], incorrect[keys[0]]
            row.update(responseId=response["id"],
                       correct={response["id"]: {"type": "matching", "pairs": correct}},
                       incorrect={response["id"]: {"type": "matching", "pairs": incorrect}})
        elif kind in {"output", "intentional-exit", "crt-output"}:
            cases = block["outputCheck"]["cases"]
            if not cases:
                raise ValueError("A published output objective lost its first case")
            first_input = cases[0].get("standardInput", "")
            row["caseCount"] = len(cases)
            row["outputCheck"] = deepcopy(block["outputCheck"])
            if kind == "intentional-exit":
                if cases[0].get("exitCode") != 1:
                    raise ValueError("The intentional exit fixture lost its exact code 1")
                attempts = [
                    _attempt(blocks, owner, "initialCode", "published-source", True, first_input),
                    _attempt(blocks, owner, "initialCode", "normal-exit-wrapper", False, first_input,
                             mutation={"kind": "normal-exit-wrapper"}),
                ]
            else:
                reference = spec["referenceOwner"]
                if blocks[reference].get("type") != "code":
                    raise ValueError("The complete published reference is not a code block")
                row["referenceOwner"] = reference
                attempts = [
                    _attempt(blocks, reference, "code", "published-reference", True, first_input),
                    _attempt(blocks, owner, "initialCode", "published-starter", False, first_input),
                ]
            row["attempts"] = attempts
        elif kind == "reference":
            if block["type"] != "code":
                raise ValueError("A reviewed reference changed its block type")
            row.update(relatedOwner=spec["relatedOwner"], source=block["code"],
                       sourceSha256=sha256(block["code"]))
        else:
            raise ValueError("Unknown edition-source block category")
        entries.append(row)
    return entries


def lesson_payloads(name: str) -> tuple[dict[str, bytes], dict]:
    spec = PUBLICATIONS[name]
    raw = published_bytes(name)
    original = json.loads(raw)
    state.validate(original)
    source = {key: deepcopy(value) for key, value in original.items() if key != "stateLayout"}
    prefix = "cpp_fundamentals__"
    if not original["id"].startswith(prefix) or "__edition_" in original["id"]:
        raise ValueError("The source fixture is not an original legacy lesson")
    source["id"] = "cpp_fundamentals__edition_es_1__" + original["id"][len(prefix):]
    current = course_edition.prepare_edition_lesson(
        source, DESCRIPTOR, source_document=original, source_path=spec["path"])
    if current["stateLayout"]["current"] != original["stateLayout"]["current"]:
        raise ValueError("Copy preparation changed a published local owner")
    if current["stateLayout"]["additions"] or current["stateLayout"]["editorial"]:
        raise ValueError("A new edition copy inherited old additions or editorial history")
    ignored = {"id", "courseEdition", "courseEditionSource", "stateLayout"}
    if ({key: value for key, value in current.items() if key not in ignored}
            != {key: value for key, value in original.items() if key not in ignored}):
        raise ValueError("Copy preparation changed a source field beyond its declared identity")
    original_path = f"originals/{name}.json"
    edition_path = f"{CONTENT}/edition_es_1__{spec['unit']}/{Path(spec['path']).name}"
    encoded = json_bytes(current)
    entry = {
        "id": name, "publishedPath": spec["path"], "publishedBlobSha": spec["blobSha"],
        "original": original_path, "edition": edition_path,
        "originalLessonId": original["id"], "editionLessonId": current["id"],
        "originalSha256": sha256(raw), "editionSha256": sha256(encoded),
        "sourceUnitNumber": spec["sourceUnitNumber"], "sourceLessonNumber": spec["sourceLessonNumber"],
        "blocks": block_manifest(name, original),
    }
    return {original_path: raw, edition_path: encoded}, entry


def export_payloads() -> dict[str, bytes]:
    # Pinned indexes authenticate the source ordinals, titles and actual
    # descriptor. The small active QA catalog is not a publication transition
    # and does not remove the real edition's previously active U1-U4.
    indexes = {name: json.loads(published_index_bytes(name)) for name in INDEXES}
    published_course = indexes["course"]
    if published_course["id"] != DESCRIPTOR["courseId"] or published_course.get("courseEdition") != DESCRIPTOR:
        raise ValueError("The real published edition descriptor differs from generation 1")
    payloads, entries = {}, []
    lesson_documents = {}
    for name, spec in PUBLICATIONS.items():
        files, entry = lesson_payloads(name)
        indexed = next((item for item in indexes[spec["unit"]]["lessons"]
                        if item["id"] == entry["originalLessonId"]), None)
        current = json.loads(files[entry["edition"]])
        if indexed is None or indexed["number"] != spec["sourceLessonNumber"] or indexed["title"] != current["title"]:
            raise ValueError("An authentic lesson differs from its pinned original unit index")
        payloads.update(files)
        entries.append(entry)
        lesson_documents[current["id"]] = current
    counts = Counter(row["kind"] for entry in entries for row in entry["blocks"])
    if counts != EXPECTED_KINDS or len(entries) != 18:
        raise ValueError("The fixture lost one of its 18 lessons or 41 special blocks")
    units, unit_entries, unit_paths = {}, [], []
    for ordinal, unit in enumerate((key for key in INDEXES if key != "course"), 1):
        original_index = indexes[unit]
        identity = f"cpp_fundamentals__edition_es_1__{unit}"
        included = [entry for entry in entries if PUBLICATIONS[entry["id"]]["unit"] == unit]
        lessons = [{"id": entry["editionLessonId"], "number": number,
                    "title": lesson_documents[entry["editionLessonId"]]["title"]}
                   for number, entry in enumerate(included, 1)]
        document = {"courseId": DESCRIPTOR["courseId"], "unitId": identity,
                    "title": original_index["title"], "courseEdition": deepcopy(DESCRIPTOR),
                    "lessons": lessons}
        path = f"{CONTENT}/edition_es_1__{unit}/unit_edition_es_1__{unit}.json"
        units[identity] = document
        payloads[path] = json_bytes(document)
        unit_paths.append(path)
        unit_entries.append({"id": identity, "number": ordinal, "title": document["title"]})
    course = {"id": DESCRIPTOR["courseId"], "title": published_course["title"],
              "courseEdition": deepcopy(DESCRIPTOR), "units": unit_entries}
    course_path = CONTENT + "/course_cpp_fundamentals.json"
    courses_path = "content/es/programming/courses.json"
    payloads[course_path] = json_bytes(course)
    payloads[courses_path] = json_bytes([{"id": course["id"], "number": 1, "title": course["title"]}])
    course_edition.validate_active_course(course, units, lesson_documents)
    manifest = {
        "version": 1, "kind": KIND, "publishedCommit": PUBLISHED_COMMIT,
        "courseEdition": deepcopy(DESCRIPTOR),
        "catalog": {"courses": courses_path, "course": course_path, "units": unit_paths},
        "lessons": entries,
        "files": {name: sha256(payload) for name, payload in payloads.items()},
    }
    payloads["manifest.json"] = json_bytes(manifest)
    return payloads


def export(output_dir: Path) -> tuple[Path, ...]:
    output = checked_output(output_dir)
    payloads = export_payloads()
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    # All source and destination checks finish before the first write.
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    for name, payload in payloads.items():
        target = output / name
        if existing_matches(target, payload):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        try:
            with target.open("xb") as stream:
                stream.write(payload)
        except FileExistsError:
            if not existing_matches(target, payload):
                raise
    return tuple(output / name for name in payloads)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        exported = export(args.output_dir)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Edition-source fixtures not exported: {error}\n")
    print(f"Verified 18 original/copy pairs and 41 special blocks in {len(exported)} QA files.")


if __name__ == "__main__":
    main()


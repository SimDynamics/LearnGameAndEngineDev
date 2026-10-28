"""Comprueba el contrato de assets y conserva las posiciones del contenido previo.

No compila C++, no modifica archivos y no certifica las respuestas pedagógicas.
"""

import argparse
import copy
import json
import hashlib
import math
import re
import subprocess
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from pathlib import Path, PurePosixPath
from importlib import util as import_util


# Also supports this validator's existing importlib-based regression tests.
_state_spec = import_util.spec_from_file_location(
    "lgp_stable_state_layout", Path(__file__).with_name("stable_state_layout.py"))
_state_layout = import_util.module_from_spec(_state_spec)
_state_spec.loader.exec_module(_state_layout)


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
# These five choices pass the ordinary editorial-preservation guard after the
# complete state-layout proof has projected only their reviewed option. This
# does not authorize a detached new option or arbitrary later prose changes.
CHOICE_OPTION_REVISION_LOCATIONS = frozenset(
    ("texto_interpretacion_y_flujos/lesson_" + lesson.split("__")[-1] + ".json",
     int(identity.split("_")[1]) + 1, int(identity.split("_")[2]) + 1)
    for lesson, identity in _state_layout.CHOICE_OPTION_REVISIONS)
FIELDS = {
    "heading": {"type", "text"},
    "paragraph": {"type", "text", "emphasis"},
    "note": {"type", "text", "emphasis"},
    "code": {"type", "language", "code"},
    "multiple_choice": {"type", "id", "question", "options", "correctIndex", "explanation", "reviewFeedback"},
    "fill_code": {"type", "id", "question", "language", "code", "answers", "explanation", "answerComparison", "reviewFeedback"},
    "output_prediction": {"type", "id", "question", "language", "code", "answers", "explanation", "outputComparison", "reviewFeedback"},
    "order_code": {"type", "id", "question", "language", "fragments", "correctOrder", "explanation", "reviewFeedback"},
    "drag_fill_code": {"type", "id", "question", "language", "template", "choices", "correctOrder", "explanation", "reviewFeedback"},
    "inline_fill_code": {"type", "id", "question", "language", "template", "answers", "answerAlternatives", "reviewFeedback", "explanation"},
    # Los proyectos sustituyen initialCode al ejecutar; se conserva el campo legacy.
    "cpp_compiler": {"type", "language", "initialCode", "initialStandardInput", "projectFiles", "sourceFiles", "cmake", "outputCheck", "compileCheck"},
    # Contenedor compartido ya admitido por MathPuzzleParser en ambas rutas.
    "math_puzzle": {"type", "schemaVersion", "version", "id", "prompt", "question", "representations", "tools", "responses", "feedback"},
}
EXERCISES = set(FIELDS) - {"heading", "paragraph", "note", "code", "cpp_compiler"}
COMPILER_EXTENSIONS = {"initialStandardInput", "projectFiles", "sourceFiles", "cmake", "outputCheck", "compileCheck"}
REVIEW_FEEDBACK_TYPES = {"multiple_choice", "fill_code", "output_prediction", "order_code", "drag_fill_code", "inline_fill_code"}
SOURCE_SUFFIXES = {"cpp", "cc", "cxx", "c++"}
STDIN_LESSON = "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json"
# Precisiones revisadas de consigna; todos los demás campos del ejercicio,
# incluidos código, respuestas, identificador y explicación, siguen intactos.
QUESTION_CLARIFICATIONS = {
    ("colecciones_y_recorridos/lesson_extremos_sin_inventar_datos.json", 3, 4):
        "Completa sólo el dígito del índice para iniciar mayor con el primer elemento de esta colección no vacía.",
    ("colecciones_y_recorridos/lesson_mediana_y_posiciones_centrales.json", 2, 5):
        "Completa sólo el dígito del divisor que localiza el índice central para cualquier tamaño impar positivo, no sólo para los tres datos de esta muestra.",
    ("colecciones_y_recorridos/lesson_ordenar_y_conservar_el_original.json", 2, 4):
        "Completa sólo el nombre de la operación de ordenación utilizada en el ejemplo anterior.",
    ("colecciones_y_recorridos/lesson_recorrer_indices_con_seguridad.json", 2, 3):
        "Completa sólo el operador de comparación para exigir que i sea estrictamente menor que datos.size().",
    ("errores_y_construccion_gradual/lesson_limpiar_sin_cambiar_el_comportamiento.json", 2, 5):
        "Extraes la comprobación de dominio. Completa sólo el operador de comparación para aceptar piezas de cero a diez, incluidas ambas fronteras, y rechazar los demás enteros.",
    ("datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json", 3, 5):
        "Completa sólo el símbolo de negación que hace != coherente con ==.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 1, 4):
        "Predice la salida. Escribe únicamente los caracteres que el programa envía a la salida; distingue la separación de las instrucciones del contenido de los literales.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 1, 5):
        "Predice la salida. Escribe el texto producido y conserva los espacios que el programa inserta dentro del mensaje.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 1, 6):
        "Predice la salida. Escribe las partes del mensaje en el orden de ejecución y conserva el signo insertado entre ellas.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 3, 5):
        "Predice la salida. Omite las comillas que delimitan el literal, pero conserva las que el programa produce dentro del mensaje.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 3, 6):
        "Predice la salida. Escribe los caracteres del mensaje después de interpretar el escape de barra inversa del literal.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 4, 4):
        "Predice la salida. Escribe sólo el texto que el programa muestra y conserva el espacio dentro del literal; distingue ese contenido del comentario.",
    ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 4, 5):
        "Predice la salida. Escribe los caracteres que se muestran; interpreta los signos según su posición dentro o fuera del literal.",
    ("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 3, 2):
        "Predice la salida. Conserva el separador y sus espacios tal como los produce el programa; el formato del código fuente no forma parte del mensaje.",
    ("programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json", 3, 3):
        "Predice la salida. Incluye las comillas del mensaje y representa su salto interno de línea; omite las comillas que sólo delimitan el literal.",
}
# Precisiones textuales revisadas. Se conserva el resto del ejercicio entero.
EXPLANATION_CLARIFICATIONS = {
    # Revisiones curriculares de U2L8 verificadas por Work; todos los demás
    # campos, código, respuestas, ID y coordenadas conservan igualdad exacta.
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 1, 5):
        ('fill_code', "`'K'` es un literal de carácter: las comillas simples delimitan el símbolo que recibe `fila`. La consola muestra la letra sin esas comillas (Stroustrup, 2024, sección 2.2, p. 32; sección 2.7, p. 43)."),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 2, 3):
        ('output_prediction', 'La consola muestra `8 8`. La primera cifra procede del carácter guardado en `marca`; la segunda, del entero guardado en `cantidad`. El espacio entre ellas procede del literal `" "`. Ver las mismas cifras no significa que los objetos tengan el mismo tipo (Stroustrup, 2024, sección 2.2, p. 32; sección 2.7, p. 43).'),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 3, 3):
        ('output_prediction', "La primera instrucción muestra `A` y el carácter de salto `'\\n'`. Después, la asignación sustituye el valor de `marca` por `B`, que se muestra en la segunda línea. El programa no imprime las comillas ni los dos signos del escape (Stroustrup, 2024, sección 2.5, p. 37; Köppe et al., 2023, sección 5.13.3, párr. 4 y tabla 10)."),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 4, 3):
        ('output_prediction', '`mensaje` empieza con `Listo`. La operación `+= signo` añade el carácter guardado en `signo` y actualiza esa cadena. Por eso se muestra `Listo!`, sin espacio intermedio: la operación no inventa un separador (Köppe et al., 2023, sección 23.4.3.7.1, párr. 5; sección 23.4.3.7.2, párr. 11).'),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 6, 2):
        ('output_prediction', '`registro` copia `true` cuando se inicializa. Después, la asignación cambia `disponible` a `false`, sin actualizar la copia. La salida predeterminada representa esos valores como `1 0` (Stroustrup, 2024, sección 2.5, p. 37; Köppe et al., 2023, sección 30.4.3.3.3, párr. 5; sección 7.3.7, párr. 6).'),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 6, 3):
        ('fill_code', '`false` indica que la condición «terminada» no se cumple todavía. Es un literal lógico, escrito sin comillas. La salida predeterminada lo presenta como `0` (Stroustrup, 2024, sección 2.2, p. 32; Köppe et al., 2023, sección 30.4.3.3.3, párr. 5; sección 7.3.7, párr. 6).'),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 6, 5):
        ('output_prediction', '`auto` deduce el tipo `bool` a partir del inicializador `true`. La asignación posterior cambia el valor a `false`, pero no cambia el tipo deducido. En la consola aparece `0` por el formato predeterminado (Stroustrup, 2024, sección 2.10, p. 47; sección 2.8, p. 43; Köppe et al., 2023, sección 30.4.3.3.3, párr. 5; sección 7.3.7, párr. 6).'),
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 8, 5):
        ('output_prediction', '`nombre` aporta la secuencia `Modulo`; `inicial` aporta el carácter `M`; `preparado` conserva `false`, que en este formato se muestra como `0`. Los dos espacios están escritos en las instrucciones de salida. Ninguno de los objetos cambia de tipo por aparecer junto a los otros (Stroustrup, 2024, sección 2.2, p. 32; Köppe et al., 2023, sección 30.4.3.3.3, párr. 5; sección 7.3.7, párr. 6).'),
    ("repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json", 3, 3):
        ("output_prediction", "La condición inicial es falsa y el acumulador conserva su valor inicial cero."),
}
# Esta precisión revisa juntos consigna y feedback de una predicción. El
# programa, respuesta, ID y todos los demás campos conservan igualdad exacta.
EXERCISE_TEXT_CLARIFICATIONS = {
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 2, 4): (
        "output_prediction",
        "Predice la salida. Escribe el mensaje sin las comillas del código y conserva el espacio entre las palabras. El programa no añade un salto de línea al mensaje.",
        "Se muestra Archivo preparado, con un espacio entre las palabras. Las comillas, el punto y coma y los saltos de línea que separan las instrucciones del código fuente no forman parte de esta salida.",
    ),
}
# Revisiones editoriales autorizadas, comprobadas contra los commits originales.
# Cada transición fija ubicación, tipo, campo y ambos textos exactos por SHA-256.
# No se acepta un texto futuro distinto ni se altera el resto del ejercicio.
APPROVED_EDITORIAL_REVISIONS = {
    ('objetos_y_estado/lesson_cadenas_y_texto.json', 4, 5, 'inline_fill_code', 'explanation'): (
        '0b00e407d9f1d5fe3423d93e8c290eddc3130f5def742e5676b6c31c3a9cb21d',
        'ff7fd9ab41d8a60284a2432a274db8ddb0dc4e8b0899cb0eeadb73a7f1324d7e'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 1, 5, 'fill_code', 'explanation'): (
        'a8f2b9592f694fffb80d3727d4e57b4018bcab943e33dbd997bfeb45188fcbe7',
        '2efd44ee40319f872137cb3051a05196bf37e325cba411bf9301ac6099a195fa'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 2, 3, 'output_prediction', 'explanation'): (
        'f2d3bbc11d7a244a396b91e4a8278dee2aaa60201d08bff8abf2e8cb28e3c0e4',
        'c1667ea1c68a2f13578370d4e07aa40253d6e8cff0047db1316ca397649e24b5'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 3, 3, 'output_prediction', 'explanation'): (
        '96af27304af8e07e9c7519fc9cadbb0940f11cae8481bcbd7ef12366b60d686e',
        '25f419e89cceb8fbac53952b45b1980c48057e59763a1c7b2f54a3880e8e074a'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 3, 4, 'inline_fill_code', 'explanation'): (
        '1a22244910e8f2f4e7897c17ce4458482c1c24c285de931824f9ec867c62af6b',
        '970e68c637ab36808ddf6407b7f97a72cbb8e33eba26efa96c5c0ef8096b196b'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 6, 2, 'output_prediction', 'explanation'): (
        '45ca7a8464e04a165e484fd5ec56c6838fb6a05533da8e20c9d064185ca07f04',
        'f76b0a7cf1c4a7027e00fb02afdb04349566a86071b770e4071820091b85b12c'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 6, 3, 'fill_code', 'explanation'): (
        '9a8e3fd50b66622ea487de26fbd414f943d6b6c04685d08737ea9a02f47e84af',
        '1885014efb8a6cff68259a9e531dc95be25d227cf1e9fab2c2d725f074587ed0'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 6, 5, 'output_prediction', 'explanation'): (
        '78ce60b5118570727a27c9aebfb81869ffecc39dd7465c92f18356532058d959',
        '6780d56f05919c9030020dff5195e1d89a64a37e6cbb84989b4d3321b76d1612'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_caracteres_y_valores_logicos.json', 8, 5, 'output_prediction', 'explanation'): (
        'ea46217c72bfbcdf6a91f4ce9f0b44cfe9334cb229b6e3505ee5189fbc89daff',
        'de0e1f2c73bdf62a4afc6f1b39d1060fdaa74359746366ae535e4a64ebc58a3d'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_conversiones_y_deduccion.json', 4, 6, 'inline_fill_code', 'explanation'): (
        'cc2330bc9c038bde0b4b5b7005b5c9f657dde993f08b20c09ebd4c0f7648dde9',
        '6e756fb7eb6302efc1f7a959abf1b37bc9b58cba320eee26d773635e203ed526'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_declarar_e_inicializar.json', 3, 5, 'inline_fill_code', 'explanation'): (
        '4119aa2d1fa244c212eb462893c5c271e4fd51bc783a457f1e23f073e2a908e5',
        '556b24cae3458ce1445c06cca12f56cd48fccc9d3a57bafa51b9dccdb2bc1a1b'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_enteros_y_aproximaciones.json', 4, 4, 'inline_fill_code', 'explanation'): (
        '294fe6fba1a293d9ab2d4d890224de0ec41e216a400850145f2ee3d43b093261',
        '29e344a9c4c75c3bf3889bbf736b59a28818b5ba74e10edd62fa313b4e9d826b'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_enteros_y_aproximaciones.json', 8, 7, 'inline_fill_code', 'explanation'): (
        'bcbf2a476d60f2e326154b7b73a6104c9f11d225591cd5973915e54d8a2587fa',
        'a9d3465943dd97701672d0164fd8005f7255824249176acc63d6df33583486eb'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_estado_asignacion_y_copia.json', 6, 5, 'inline_fill_code', 'explanation'): (
        'fc13f615e189b261ccc229062a1e084c72fc434140900e94716392b15ee988ff',
        '83d52151de37084d81e2d5f0197575ea7e730a637478200e7363728a1bd841fd'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_leer_una_entrada_conocida.json', 9, 6, 'drag_fill_code', 'explanation'): (
        '8a6b313147e327407bfa956931ccf313cbe1d0d441053a85832abfc4f28c3137',
        'f06a1f6a8c871f962f1954ae04440ab7f49e77eca9bc863af5b3a9ea191483e3'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_nombres_con_intencion.json', 4, 5, 'inline_fill_code', 'explanation'): (
        'c7e59ef44575bb0209559dc292e858b26b067bebc123d89015b99e39fcd6da60',
        '60826465ccc4e96e14b4c4248689e96135ed49cac8371b5d4e58b96f6955bc31'),  # ad4f362 -> 879f5ce
    ('programas_y_ejecucion/lesson_texto_secuencia_y_formato.json', 5, 2, 'inline_fill_code', 'explanation'): (
        'e128bdfeb5fe91f82f96d4c9b01de833d9e632ac9efe03aee2232e412bc3569f',
        '745e7f84b90234c0926cf061d43da90b04c70acc0b911e30ed8eb249e9b80ee5'),  # ad4f362 -> 879f5ce
    ('objetos_y_estado/lesson_cadenas_y_texto.json', 8, 6, 'output_prediction', 'explanation'): (
        '71b820f683c5bf932dbbd1e1ab362c0d247688b6e23fa01693a749ee3bab90f5',
        '40b2ffffea40a5840a66aefad3ce337ec5d6a827b8960015c822c293026c3bfb'),  # 879f5ce -> HEAD
    ('objetos_y_estado/lesson_cadenas_y_texto.json', 8, 6, 'output_prediction', 'question'): (
        '95de26b71f4a251ce41da5d9ce55a0cf6ba2de19abfe75889329abfa8ea7d6a6',
        'ffd2567e4d964854df7fe1cda0bed34a2cc6179a4a6c9b3b58a52cafe26be492'),  # 879f5ce -> HEAD
    ('objetos_y_estado/lesson_enteros_y_aproximaciones.json', 8, 6, 'output_prediction', 'question'): (
        '0675fd8b649157d5e354779fb5c4674b0ccfa8ae95ec64af35039d5d123aea26',
        '7322b70b9362e5e4c98c4046b45aafba681ee27792e6697955559ea4a80e68c7'),  # 879f5ce -> HEAD
    ('objetos_y_estado/lesson_leer_una_entrada_conocida.json', 7, 5, 'output_prediction', 'question'): (
        '96d0e7e48bb788850554ee18248d42f009a7a5706b00be66496e37ad1d6b67b1',
        'bc683ef3cdb8e33263794d1a080cdec6eb0cd9f59fd4fe121a940cf3806eee5c'),  # 879f5ce -> HEAD
    ('objetos_y_estado/lesson_leer_una_entrada_conocida.json', 9, 6, 'drag_fill_code', 'question'): (
        '32ccd907d2d8d3d0a1efb9cf3b7463fae1065bce5ddc01f367efe3197dd56a4b',
        '51d4fabbb6d93b9c66778232cd363b0b361efb457b9d385fb6f18a91e75acc6e'),  # 879f5ce -> HEAD
    ('objetos_y_estado/lesson_representar_procesar_y_recuperar.json', 4, 3, 'multiple_choice', 'explanation'): (
        'b478018c0c2a50ca5961803c48d21f950ca156fd7c9bb23257243ea03832cf9b',
        '2c392fe6fdf001544822609a0e540d72b7c77ba36d4183486b845acef5cadd73'),  # 879f5ce -> HEAD
    ('programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json', 4, 6, 'output_prediction', 'question'): (
        '60f43916d4f3e314e647e88dbc0baa345ec37e816f62abceadacfb873733c679',
        'daf9bb92af0fb387f61efc5a0062adf550c1bdffd15ab2a4be5b5b3f6b874d34'),  # 879f5ce -> HEAD
    ('programas_y_ejecucion/lesson_primer_programa_y_salida.json', 5, 4, 'output_prediction', 'explanation'): (
        'fcce514cc7299967e071a6740a5c531d358c40ffcf34321aa0310e8eb4bb61d1',
        '2406d5a896fb0e0e871515a0456c6dc4762a02a78b26905184384763e5e35fd1'),  # 879f5ce -> HEAD
    ('programas_y_ejecucion/lesson_primer_programa_y_salida.json', 5, 4, 'output_prediction', 'question'): (
        '7951c377436bffa8a83f65ea8fe6044c1603d28c384af95723d607480c6052e0',
        '3e7fa5f573c566bc17a7308398d90ee0a849aa6cb849bb89f9a2d5d643b7b01c'),  # 879f5ce -> HEAD
    ('programas_y_ejecucion/lesson_texto_secuencia_y_formato.json', 7, 4, 'output_prediction', 'question'): (
        'ade2fccf6105bfd136127f960de80ee2ac9ad02f3d6b0c3630e20c0537257950',
        '7f48db3071af57952889a2c2680f14dba906231d495638855feb71c4bf23c53a'),  # 879f5ce -> HEAD
    # CPP_VALIDACION_EDITORIAL_PREGUNTAS_V2: revisiones españolas revisadas
    # frente a 329f0e4 y d24f7df. El mapa v2 verifica por separado su origen,
    # prueba editorial y contrato; sólo se autorizan estos dos textos exactos.
    ('repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json', 10, 6, 'output_prediction', 'question'): (
        'a54834fb2cefc6b3f3946b7e592175fa3164554ec52ea99ba7ac89f6c97914b2',
        '5a118dbec6bb4eae859123e8b7885c3b62ba8f5c408666e6244d7f36bbaee468'),
    ('colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json', 8, 3, 'output_prediction', 'explanation'): (
        'a3d1d92f090b19d0c97d7bcc0bd7a33bef8cf583d0617a5999c02aa165370abc',
        '926fca6af960c61774ddf1bdf6c97caa1b19cdc02b0a33a9a72aca32a64bbe74'),
}

# Reordenaciones puntuales que retiran pistas de posición, sin cambiar opciones
# ni la respuesta por contenido. Los índices indican el orden nuevo de HEAD.
MULTIPLE_CHOICE_OPTION_PERMUTATIONS = {
    ("objetos_y_estado/lesson_representar_procesar_y_recuperar.json", 1, 4): (2, 1, 0),
    ("objetos_y_estado/lesson_representar_procesar_y_recuperar.json", 4, 3): (1, 0, 2),
    ("expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json", 2, 5): (2, 1, 0),
    ("expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json", 4, 6): (0, 2, 1),
    ("colecciones_y_recorridos/lesson_extremos_sin_inventar_datos.json", 4, 4): (1, 0, 2),
    ("colecciones_y_recorridos/lesson_extremos_sin_inventar_datos.json", 5, 3): (2, 1, 0),
    ("colecciones_y_recorridos/lesson_media_y_cantidad_de_datos.json", 4, 4): (2, 1, 0),
    ("colecciones_y_recorridos/lesson_media_y_cantidad_de_datos.json", 5, 4): (0, 2, 1),
}
# Distractores revisados: se conserva la pregunta, el índice y el acierto literal.
MULTIPLE_CHOICE_DISTRACTOR_REVISIONS = {
    ("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", 8, 4): (
        [
            "Porque una prueba con varios datos demuestra que funciona para cualquier tamaño de vector.",
            "Porque ese caso sustituye las pruebas con un solo dato y con ninguno.",
            "Porque puede revelar reinicializaciones y diferencias entre contar y sumar que un solo dato oculta.",
        ],
        "Con {2, 7, 4, 1}, sumar da 14 y contar da 4; reiniciar la suma en cada visita dejaría sólo 1. El caso {1} no distingue sumar de contar y tampoco comprueba conservar lo acumulado entre visitas. Estos casos no sustituyen las pruebas del vacío y de un solo dato.",
    ),
    ("colecciones_y_recorridos/lesson_informes_y_transferencia_de_colecciones.json", 5, 4): (
        [
            "Publicar cero como media y mediana, porque el total del vacío es cero.",
            "Calcular el centro y la media antes de comprobar el vacío, para decidir después el mensaje.",
            "La rama de ausencia antes de acceder o dividir, no inventar un dato inicial.",
        ],
        "El vacío tiene cantidad y total cero, pero no tiene media, mediana ni extremos. La rama de ausencia debe decidirse antes de acceder a una posición inexistente o de dividir por cero. Que {4} funcione no comprueba esa rama ni justifica publicar ceros como datos.",
    ),
    ("colecciones_y_recorridos/lesson_modificar_con_referencias.json", 8, 4): (
        [
            "Imprimir cada valor justo después de modificarlo dentro del recorrido.",
            "Leer la colección después del recorrido y comparar sus elementos con los de partida.",
            "Cambiar el nombre de valor por original.",
        ],
        "Una copia local también puede cambiar y mostrar el valor transformado dentro del bucle. Para comprobar que el cambio quedó almacenado, lee después la colección en un caso con elementos que deban cambiar. Renombrar valor no cambia la relación que expresa su declaración.",
    ),
    ("funciones_y_contratos/lesson_componer_funciones_pequenas.json", 1, 5): (
        [
            "El valor devuelto, que debe ser -3 al restar 5 - 8.",
            "El valor devuelto, que debe ser 0.",
            "La presencia de cout dentro de faltantes.",
        ],
        "Como hay más disponibles que necesarias, la primera rama devuelve 0: no hay faltantes negativos. Ese valor puede comprobarse sin imprimirlo. El formato corresponde a mostrar_faltantes; añadir cout al cálculo no es la prueba de su resultado.",
    ),
    ("errores_y_construccion_gradual/lesson_estimar_comprobar_y_no_exceder_el_dominio.json", 1, 5): (
        [
            "Una media siempre debe ser igual al último dato.",
            "Una media de esos datos debe estar entre 1 y 5.",
            "Una media de datos positivos debe ser positiva.",
        ],
        "La media queda dentro del rango de los datos; 9 no cumple esa condición necesaria. Comprobar sólo que sea positiva no detecta este error, porque 9 también es positivo. Tampoco se elige siempre el último dato: para 2, 4 y 6, la media es 4 y el último dato es 6.",
    ),
    ("excepciones_y_recuperacion/lesson_propagar_hasta_el_contexto_adecuado.json", 2, 5): (
        [
            "Guardar una copia del valor anterior, modificar pendientes y no restaurarlo cuando falla la comprobación.",
            "Sólo añadir cualquier catch después de modificar.",
            "Colocar la comprobación que puede fallar antes de la modificación correspondiente.",
        ],
        "El catch no restaura pendientes automáticamente. Si pasó de cinco a cuatro, conservar cinco en una copia no lo vuelve a asignar al objeto original. La segunda versión comprueba antes de modificar: el fallo impide alcanzar el decremento.",
    ),
    ("texto_interpretacion_y_flujos/lesson_lineas_y_separadores_pendientes.json", 4, 5): (
        [
            "No perder ninguna línea: los saltos no se consideran espacio en blanco.",
            "Perder la primera palabra de la etiqueta, porque la lectura previa de blancos consume también esa palabra.",
            "Perder líneas vacías que forman parte de los datos.",
        ],
        "Los saltos también son espacio en blanco. Si se eliminan todos, una línea vacía significativa desaparece antes de la lectura que debía conservarla. Consumir blancos no obtiene una palabra: se detiene al llegar al primer carácter que no es blanco.",
    ),
    ("texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json", 4, 2): (
        [
            "Nada: obtener tokens demuestra que la operación matemática es correcta.",
            "Si el orden de los tokens cumple la gramática de una expresión.",
            "Nada más si se borra un asterisco: se puede aceptar la entrada reparada como si la original hubiera sido válida.",
        ],
        "El reconocimiento acepta los signos por separado. La gramática deberá rechazar una multiplicación sin un operando entre ambos signos. Borrar un asterisco cambiaría la entrada; no demostraría que la expresión original era válida.",
    ),
    ("texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json", 3, 3): (
        [
            "Cada vuelta con + consume el signo y un número; otro token se devuelve y la función retorna.",
            "El token reservado garantiza que cada vuelta obtenga una unidad nueva, aunque la regla continúe después de devolverlo.",
            "Devolver cualquier token hace que el flujo retroceda al principio.",
        ],
        "El caso repetido consume unidades; el caso que conserva una unidad termina la función. Una lectura que falla lanza un diagnóstico y abandona la regla.",
    ),
    ("texto_interpretacion_y_flujos/lesson_signos_unarios_parentesis_y_progreso_recursivo.json", 8, 2): (
        [
            "Sólo falta validar el valor: la longitud finita del texto ya garantiza que el recorrido terminará.",
            "El nuevo recorrido puede volver a encontrar la misma apertura sin progreso.",
            "Sólo falta comprobar el cierre: devolver la apertura ya asegura que la llamada siguiente avance.",
        ],
        "La recursión debe avanzar o reducir un problema. Un texto finito no garantiza terminación si se vuelve a preguntar por la misma unidad. Devolver la apertura la deja disponible; repetir la misma pregunta sin consumirla puede reproducir indefinidamente el mismo estado.",
    ),
    ("texto_interpretacion_y_flujos/lesson_asignaciones_con_anticipacion_limitada.json", 5, 3): (
        [
            "Porque un nombre existente sólo puede aparecer como destino y no dentro de una expresión.",
            "Porque el lado derecho admite expresiones sin asignaciones y sobra =.",
            "Porque una asignación debe volver a declarar ambos nombres.",
        ],
        "La gramática sólo permite asignación como sentencia superior. El = después de b impide aceptar el terminador y no publica el candidato.",
    ),
    ("archivos_y_programa_completo/lesson_abrir_escribir_y_leer_una_muestra.json", 2, 5): (
        [
            "Únicamente que el nombre acabe en .txt.",
            "Que la extracción haya obtenido los dos enteros esperados.",
            "Que los destinos se inicialicen en cero, aunque una de las extracciones falle.",
        ],
        "La apertura acredita acceso al archivo, no el formato de sus datos. Inicializar los destinos tampoco demuestra que las dos extracciones hayan obtenido valores válidos.",
    ),
    ("archivos_y_programa_completo/lesson_texto_con_campos_y_patrones_literales.json", 6, 4): (
        [
            "Pasar después otro argumento para el segundo campo.",
            "Añadir {1} al patrón para crear automáticamente el argumento que falta.",
            "Poner A como texto literal tras el segundo {} para darle un valor a ese campo.",
        ],
        "Los caracteres del patrón no proporcionan el valor que necesita el segundo campo. Un índice selecciona un argumento que ya debe existir; no lo crea. Añadir texto literal tampoco completa un campo sin argumento.",
    ),
    ("archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json", 1, 7): (
        [
            "std::isalpha(unidad) == 1, porque todo verdadero debe ser ese entero.",
            "std::isalpha(unidad) != 0, porque cualquier resultado no nulo significa verdadero.",
            "std::isalpha(unidad) == unidad, porque devuelve el código original cuando reconoce una letra.",
        ],
        "La biblioteca promete cero o no cero; la comparación con 1 añadiría una condición que no promete. El resultado comunica una categoría, no devuelve el código del carácter analizado.",
    ),
    ("archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json", 4, 6): (
        [
            "Porque convertir antes a char garantiza que tolower sólo reciba valores de su dominio.",
            "Porque pasar primero por char conserva siempre EOF como marca distinta de cualquier carácter.",
            "Porque hay que mantener separados el dominio del argumento, posibles marcas y la representación del resultado.",
        ],
        "Sólo los resultados de caracteres del dominio establecido se convierten aquí a char. Una marca de fin se conserva como marca. Convertir previamente a char no valida un int arbitrario ni garantiza conservar EOF.",
    ),
}
# Rellenos revisados: ampliaciones y consignas precisas; sólo estos campos.
FILL_CODE_REVISIONS = {
    ("objetos_y_estado/lesson_objetos_valores_y_tipos.json", 2, 4): {
        "question": "Completa sólo el tipo entero usado para las cantidades de esta lección, ahora para conservar nueve bicicletas.",
        "answers": ["int", "signed", "signed int", "int signed"],
    },
    ("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 2, 5): {
        "question": "Entre los dos tipos numéricos estudiados hasta aquí, completa el que conserva 3.5 litros sin convertirlo a entero.",
    },
    ("objetos_y_estado/lesson_conversiones_y_deduccion.json", 3, 5): {
        "question": "Entre los dos tipos numéricos estudiados hasta aquí, completa el que conserva 1.5 en una inicialización con llaves.",
    },
    ("objetos_y_estado/lesson_caracteres_y_valores_logicos.json", 6, 3): {
        "question": "Completa sólo el literal lógico para indicar que la revisión todavía no está terminada.",
    },
    ("errores_y_construccion_gradual/lesson_requisitos_casos_y_primera_version.json", 3, 5): {
        "question": "Escribe un único literal numérico decimal de tipo int o double, sin operaciones ni nombres, que represente exactamente los minutos de preparación para pedidos no vacíos.",
        "answers": ["3", "3.0"],
        "explanation": "La preparación es de tres minutos. 3 y 3.0 representan esa cantidad; aquí el cálculo con 3.0 no genera fracción y su retorno int conserva el resultado. Que una cantidad distinta se trunque al mismo entero no satisface el requisito. La contribución por pieza sigue siendo 2*piezas; el pedido vacío retorna antes de sumarla.",
    },
    ("datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json", 3, 5): {
        "question": "Completa sólo el operador de negación que hace != coherente con ==.",
        "answers": ["!", "not"],
        "explanation": "Diferentes significa que no se cumple la igualdad completa. ! y su escritura alternativa not niegan el mismo resultado bool; ambas mantienen coherencia con ==.",
    },
    ("archivos_y_programa_completo/lesson_etiquetas_y_alcance_de_los_identificadores.json", 2, 4): {
        "code": "bool nombre_grupo(int id, std::string& destino) {\n    if (id < 0 || id ___ 3) { return false; }\n    destino = nombres_grupos.at(id);\n    return true;\n}",
    },
}
# Equivalentes C++ verificados para estas consignas concretas. Sólo se permite
# ampliar answers; el programa, la pregunta, el feedback y la posición se conservan.
ACCEPTED_ANSWER_EXTENSIONS = {
    ("expresiones_y_decisiones/lesson_combinar_condiciones.json", 1, 5):
        (("&&",), ("&&", "and")),
    ("expresiones_y_decisiones/lesson_decisiones_compuestas_y_pruebas.json", 7, 4):
        (("&&",), ("&&", "and")),
    ("colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json", 2, 3):
        (("int",), ("int", "signed", "signed int", "int signed")),
    ("colecciones_y_recorridos/lesson_recorrer_valores_por_rango.json", 2, 4):
        (("int",), ("int", "signed", "signed int", "int signed")),
    ("funciones_y_contratos/lesson_declaraciones_y_firmas.json", 2, 3):
        (("int",), ("int", "signed", "signed int", "int signed")),
    ("errores_y_construccion_gradual/lesson_pruebas_sistematicas_y_regresion.json", 3, 5):
        (("!=",), ("!=", "not_eq")),
    ("errores_y_construccion_gradual/lesson_validar_antes_y_elegir_reporte.json", 4, 3):
        (("||",), ("||", "or")),
    ("archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json", 2, 4):
        (("unsigned char",), ("unsigned char", "char unsigned")),
}
ERRORS = []

# Ampliaciones opcionales: no sustituyen answers ni desplazan huecos/progreso.
EXACT_OUTPUT_LOCATIONS = {
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 2, 4),
    ("repeticion_y_terminacion/lesson_recorridos_anidados.json", 10, 3),
    ("repeticion_y_terminacion/lesson_recorridos_anidados.json", 12, 6),
    ("datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json", 12, 3),
    ("datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json", 15, 3),
}
INLINE_ANSWER_ALTERNATIVES = {
    ("objetos_y_estado/lesson_conversiones_y_deduccion.json", 4, 6): [["double", "float", "long double", "double long"]],
    ("objetos_y_estado/lesson_enteros_y_aproximaciones.json", 4, 4): [["int", "signed", "signed int", "int signed"], ["double"]],
    ("expresiones_y_decisiones/lesson_combinar_condiciones.json", 2, 4): [["&&", "and"], ["||", "or"]],
    ("expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json", 3, 6): [["=="], ["!=", "not_eq"]],
    ("colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json", 4, 5): [["push_back", "emplace_back"], ["9"]],
    ("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", 5, 4): [["push_back", "emplace_back"], ["valor"]],
}
DECIMAL_LITERAL_LOCATIONS = {
    ("errores_y_construccion_gradual/lesson_requisitos_casos_y_primera_version.json", 3, 5),
}
DECIMAL_LITERAL_MODE = "cpp_decimal_literal_value"
DECIMAL_LITERAL_MAX_LENGTH = 16_384
_DECIMAL_DIGITS = r"[0-9](?:'?[0-9])*"
_DECIMAL_INTEGER = re.compile(r"(?:0|[1-9](?:'?[0-9])*)")
_DECIMAL_FLOATS = tuple(re.compile(pattern) for pattern in (
    rf"(?P<whole>{_DECIMAL_DIGITS})\.(?P<fraction>{_DECIMAL_DIGITS})?(?:[eE](?P<exponent>[+-]?{_DECIMAL_DIGITS}))?",
    rf"\.(?P<fraction>{_DECIMAL_DIGITS})(?:[eE](?P<exponent>[+-]?{_DECIMAL_DIGITS}))?",
    rf"(?P<whole>{_DECIMAL_DIGITS})[eE](?P<exponent>[+-]?{_DECIMAL_DIGITS})",
))


class DecimalLiteralResourceLimit(ValueError):
    """Límite técnico del analizador, distinto de una escritura curricular inválida."""


def _add_decimal_magnitudes(left, right):
    digits = []
    carry = 0
    for index in range(1, max(len(left), len(right)) + 1):
        total = carry
        if index <= len(left):
            total += ord(left[-index]) - ord("0")
        if index <= len(right):
            total += ord(right[-index]) - ord("0")
        digits.append(str(total % 10))
        carry = total // 10
    if carry:
        digits.append(str(carry))
    return "".join(reversed(digits))


def _subtract_decimal_magnitudes(left, right):
    """Resta left >= right sin convertir un exponente arbitrario a int."""
    digits = []
    borrow = 0
    for index in range(1, len(left) + 1):
        digit = ord(left[-index]) - ord("0") - borrow
        if index <= len(right):
            digit -= ord(right[-index]) - ord("0")
        borrow = int(digit < 0)
        digits.append(str(digit + 10 * borrow))
    return "".join(reversed(digits)).lstrip("0") or "0"


def _adjust_decimal_exponent(exponent, adjustment):
    exponent = exponent.replace("'", "")
    negative = exponent.startswith("-")
    magnitude = exponent.lstrip("+-").lstrip("0") or "0"
    if magnitude == "0":
        negative = False
    other_negative = adjustment < 0
    other = str(abs(adjustment))
    if negative == other_negative:
        result = _add_decimal_magnitudes(magnitude, other)
    elif (len(magnitude), magnitude) >= (len(other), other):
        result = _subtract_decimal_magnitudes(magnitude, other)
    else:
        result = _subtract_decimal_magnitudes(other, magnitude)
        negative = other_negative
    return "-" + result if negative and result != "0" else result


def parse_cpp_decimal_literal(value):
    """Cantidad decimal exacta (coeficiente, exponente), o None fuera del perfil.

    El perfil admite int decimal sin sufijo hasta 2147483647 y double decimal
    sin sufijo. El exponente puede ser arbitrario: nunca se calcula 10**n ni se
    redondea mediante float. El límite se aplica al token recortado, en unidades
    UTF-16, igual que al texto del control Kotlin.
    """
    if not isinstance(value, str):
        return None
    literal = value.strip()
    if (len(literal) > DECIMAL_LITERAL_MAX_LENGTH
            or sum(2 if ord(char) > 0xFFFF else 1 for char in literal) > DECIMAL_LITERAL_MAX_LENGTH):
        raise DecimalLiteralResourceLimit("literal supera el límite técnico de 16384 unidades UTF-16")
    if _DECIMAL_INTEGER.fullmatch(literal):
        coefficient = literal.replace("'", "")
        if (len(coefficient), coefficient) > (10, "2147483647"):
            return None
        exponent = "0"
        fraction_length = 0
    else:
        match = next((match for pattern in _DECIMAL_FLOATS if (match := pattern.fullmatch(literal))), None)
        if match is None:
            return None
        parts = match.groupdict()
        fraction = (parts.get("fraction") or "").replace("'", "")
        coefficient = (parts.get("whole") or "").replace("'", "") + fraction
        fraction_length = len(fraction)
        exponent = parts.get("exponent") or "0"
    coefficient = coefficient.lstrip("0")
    if not coefficient:
        return "0", "0"
    normalized = coefficient.rstrip("0")
    trailing_zeros = len(coefficient) - len(normalized)
    return normalized, _adjust_decimal_exponent(exponent, trailing_zeros - fraction_length)


def validate_decimal_answer_contract(block, where):
    if "answerComparison" not in block:
        return
    valid_mode = isinstance(block["answerComparison"], str) and block["answerComparison"] == DECIMAL_LITERAL_MODE
    require(valid_mode, where, "answerComparison debe ser cpp_decimal_literal_value; configuración técnica inválida")
    valid_kind = block["type"] == "fill_code"
    require(valid_kind, where, "answerComparison sólo corresponde a fill_code; configuración técnica inválida")
    if not valid_mode or not valid_kind:
        return
    answers = block.get("answers")
    valid_answers = isinstance(answers, list) and bool(answers) and all(isinstance(answer, str) and answer.strip() for answer in answers)
    require(valid_answers, where, "comparación decimal requiere answers textuales no vacías; configuración técnica inválida")
    if not valid_answers:
        return
    values = []
    for index, answer in enumerate(answers, 1):
        try:
            value = parse_cpp_decimal_literal(answer)
        except DecimalLiteralResourceLimit as error:
            require(False, where, f"answer {index}: {error}; configuración técnica inválida")
            return
        require(value is not None, where, f"answer {index} fuera del perfil decimal int/double; configuración técnica inválida")
        values.append(value)
    if any(value is None for value in values):
        return
    require(all(value == values[0] for value in values), where, "answers deben representar exactamente la cantidad de answers[0]; configuración técnica inválida")


def validate_review_feedback_contract(block, where):
    """Optional guidance never changes options or answers and is not a second evaluator."""
    if "reviewFeedback" not in block:
        return
    kind = block.get("type")
    require(kind in REVIEW_FEEDBACK_TYPES, where, "reviewFeedback no corresponde a este tipo de ejercicio")
    if kind not in REVIEW_FEEDBACK_TYPES:
        return
    review = block["reviewFeedback"]
    if kind == "multiple_choice":
        options = block.get("options")
        valid_options = isinstance(options, list) and bool(options) and all(isinstance(option, str) for option in options)
        require(valid_options, where, "reviewFeedback conserva options como lista de textos")
        valid_reviews = isinstance(review, list) and all(isinstance(text, str) and text.strip() for text in review)
        require(valid_reviews, where, "reviewFeedback requiere una lista de textos no vacíos")
        if valid_options and isinstance(review, list):
            require(len(review) == len(options), where, "reviewFeedback requiere una orientación por inciso, en el mismo orden")
    else:
        require(isinstance(review, str) and bool(review.strip()), where, "reviewFeedback debe ser texto no vacío de revisión del proceso")


def without_review_feedback(block):
    """Exclude only the optional review field on its supported exercise types."""
    if block.get("type") not in REVIEW_FEEDBACK_TYPES or "reviewFeedback" not in block:
        return block
    return {key: value for key, value in block.items() if key != "reviewFeedback"}


def validate_math_puzzle_contract(block, where):
    """Check the shared composition used in C++; Android remains the math evaluator.

    Only explicitly checked response families are accepted in this course.
    Additional families require extending this checker and their own native QA;
    recognizing math_puzzle must not silently accept arbitrary nested answers.
    """
    for key in ("schemaVersion", "version"):
        if key in block:
            require(not isinstance(block[key], bool) and block[key] == 1, where,
                    f"{key} debe ser 1")
    prompt = block.get("prompt") if "prompt" in block else block.get("question")
    require(isinstance(prompt, str) and bool(prompt.strip()), where, "math_puzzle requiere prompt o question no vacío")
    if "id" in block:
        require(isinstance(block["id"], str) and bool(block["id"].strip()), where, "ID de math_puzzle vacío")
    for key in ("representations", "tools"):
        values = block.get(key, [])
        require(isinstance(values, list), where, f"{key} requiere una lista de objetos")
        if isinstance(values, list):
            require(all(isinstance(value, dict) and isinstance(value.get("type"), str)
                        and value["type"].strip() for value in values), where,
                    f"{key} requiere objetos con type no vacío")
    if "feedback" in block:
        feedback = block["feedback"]
        require(isinstance(feedback, dict) and set(feedback) <= {"correct", "incorrect"}
                and all(isinstance(value, str) for value in feedback.values()), where,
                "feedback requiere textos correct/incorrect")
    responses = block.get("responses")
    require(isinstance(responses, list) and bool(responses), where, "math_puzzle requiere responses no vacío")
    if not isinstance(responses, list):
        return
    seen = set()
    for response in responses:
        require(isinstance(response, dict), where, "respuesta matemática debe ser objeto")
        if not isinstance(response, dict):
            continue
        identifier = response.get("id")
        valid_id = isinstance(identifier, str) and bool(identifier.strip())
        require(valid_id and identifier not in seen, where, "ID de respuesta vacío o duplicado")
        if valid_id:
            seen.add(identifier)
        kind = response.get("type")
        validator = response.get("validator")
        require(isinstance(validator, dict), where, "respuesta matemática requiere validator objeto")
        if not isinstance(validator, dict):
            continue
        validate_math_tolerance(validator, where)
        if kind in ("math_multiple_choice_single", "math_multiple_choice_multi"):
            validate_math_choice_contract(response, where)
        elif kind == "math_matching_drag":
            sources = math_item_ids(response.get("sources"), where, "sources")
            targets = math_item_ids(response.get("targets"), where, "targets")
            expected = validator.get("expected")
            require(validator.get("type") == "matching" and isinstance(expected, dict)
                    and set(expected) == sources
                    and all(isinstance(value, str) and value in targets for value in expected.values())
                    and len(set(expected.values())) == len(expected), where,
                    "matching requiere cada origen y destinos existentes sin repetir")
        elif kind == "math_multi_input":
            fields = response.get("fields")
            math_item_ids(fields, where, "fields")
            if not isinstance(fields, list):
                continue
            editable = {}
            for field in fields:
                if not isinstance(field, dict) or not isinstance(field.get("id"), str):
                    continue
                field_kind = field.get("type", "numeric")
                require(field_kind in ("numeric", "expression", "dropdown", "fixed"), where,
                        "tipo de campo matemático desconocido")
                if field_kind == "fixed":
                    require(isinstance(field.get("text"), str), where, "campo fijo requiere text")
                else:
                    editable[field["id"]] = field
                if field_kind == "dropdown":
                    math_item_ids(field.get("options"), where, "options")
            expected = validator.get("fields")
            require(validator.get("type") == "fields" and isinstance(expected, dict)
                    and bool(editable) and set(expected) == set(editable), where,
                    "fields requiere exactamente un validador por campo editable")
            if isinstance(expected, dict):
                for key, value in expected.items():
                    require(isinstance(value, dict), where, "validador de campo debe ser objeto")
                    if not isinstance(value, dict):
                        continue
                    family = value.get("type")
                    validate_math_tolerance(value, where)
                    # Las tablas actuales usan número/texto; otras semánticas no
                    # se aceptan de modo implícito como si hubieran sido revisadas.
                    require(family in ("numeric", "text", "exact_text"), where,
                            "validador de campo no comprobado en este contrato")
                    answer = value.get("expected")
                    require(isinstance(answer, (str, int, float)) and not isinstance(answer, bool), where,
                            "valor esperado de campo no textual/numérico")
                    if family in ("text", "exact_text"):
                        require(isinstance(answer, str), where, "validador de texto requiere expected textual")
                    elif family == "numeric":
                        require(is_finite_math_number(answer), where, "validador numérico requiere expected finito")
                    if key in editable and editable[key].get("type") == "dropdown" and family in ("text", "exact_text"):
                        choices = math_item_ids(editable[key].get("options"), where, "options")
                        require(isinstance(answer, str) and answer in choices, where, "respuesta dropdown no existe")
        else:
            require(False, where, f"tipo de respuesta matemática no comprobado: {kind}")


def validate_math_choice_contract(response, where):
    """Match native choice IDs and cardinality without guessing any answers.

    MathValidators.stringList rejects non-text, blank and duplicate expected IDs;
    MathPuzzleParser requires those IDs to exist and limits only single choice to
    one. Multi choice therefore allows one, several or all declared options.
    """
    options = response.get("options")
    option_ids = math_item_ids(options, where, "options")
    if isinstance(options, list):
        for option in options:
            if isinstance(option, dict) and "reviewFeedback" in option:
                review = option["reviewFeedback"]
                require(isinstance(review, str) and bool(review.strip()), where,
                        "reviewFeedback de opción requiere texto no vacío")
    validator = response["validator"]
    expected = validator.get("expected")
    valid_ids = (isinstance(expected, list) and bool(expected)
                 and all(isinstance(value, str) and bool(value.strip()) for value in expected))
    require(validator.get("type") == "choice" and valid_ids
            and len(set(expected)) == len(expected) and all(value in option_ids for value in expected),
            where, "selección requiere opciones correctas existentes, no vacías ni repetidas")
    if response.get("type") == "math_multiple_choice_single":
        require(valid_ids and len(expected) == 1, where,
                "selección única requiere exactamente una opción correcta")


def math_item_ids(values, where, name):
    require(isinstance(values, list) and bool(values), where, f"{name} requiere una lista de objetos no vacía")
    if not isinstance(values, list):
        return set()
    identifiers = [item.get("id") if isinstance(item, dict) else None for item in values]
    valid = all(isinstance(value, str) and value.strip() for value in identifiers)
    require(valid, where, f"{name} requiere IDs textuales no vacíos")
    if not valid:
        return set()
    require(len(set(identifiers)) == len(identifiers), where, f"IDs duplicados en {name}")
    return set(identifiers)


def is_finite_math_number(value):
    """Mirror Rational.parse's bounded decimal/fraction syntax, not an evaluator."""
    if not isinstance(value, (str, int, float)) or isinstance(value, bool):
        return False
    text = str(value).strip().replace("−", "-").replace(",", ".")
    if not text or len(text) > 256:
        return False
    percent = text.endswith("%")
    if percent:
        text = text[:-1].strip()
    parts = text.split("/")
    if len(parts) not in (1, 2):
        return False
    try:
        values = []
        for part in parts:
            # Decimal accepts underscores unlike java.math.BigDecimal.
            if not re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?", part.strip()):
                return False
            number = Decimal(part.strip())
            if not number.is_finite() or abs(number.as_tuple().exponent) > 1000 or len(number.as_tuple().digits) > 256:
                return False
            values.append(Fraction(number))
        result = values[0] / values[1] if len(values) == 2 else values[0]
        return math.isfinite(float(result / 100 if percent else result))
    except (InvalidOperation, ValueError, OverflowError, ZeroDivisionError):
        return False


def validate_math_tolerance(validator, where):
    if "tolerance" not in validator:
        return
    try:
        value = validator["tolerance"]
        valid = not isinstance(value, bool) and math.isfinite(float(value)) and float(value) >= 0
    except (TypeError, ValueError, OverflowError):
        valid = False
    require(valid, where, "tolerance matemática debe ser finita y no negativa")


def validate_paragraph_emphasis_contract(block, where):
    """Exact selections decorate original paragraph/note body, never a UI prefix."""
    if "emphasis" not in block:
        return
    require(block.get("type") in ("paragraph", "note"), where,
            "emphasis sólo está disponible en párrafos y notas")
    items, text = block["emphasis"], block.get("text")
    require(isinstance(items, list) and isinstance(text, str), where,
            "emphasis requiere una lista y el texto original del párrafo")
    if not isinstance(items, list) or not isinstance(text, str):
        return
    for item in items:
        valid = isinstance(item, dict) and set(item) <= {"text", "occurrence", "style"}
        fragment = item.get("text") if isinstance(item, dict) else None
        valid = valid and isinstance(fragment, str) and bool(fragment.strip())
        require(valid, where, "cada énfasis requiere un fragmento textual exacto no vacío")
        if not valid:
            continue
        require(item.get("style", "bold") in ("bold", "italic", "bold_italic"), where,
                "style de énfasis debe ser bold, italic o bold_italic")
        occurrence, starts, index = item.get("occurrence"), [], 0
        while index <= len(text) - len(fragment):
            index = text.find(fragment, index)
            if index < 0:
                break
            starts.append(index)
            index += 1  # Coincidencias superpuestas, igual que el selector Kotlin.
        if "occurrence" in item:
            valid_number = (not isinstance(occurrence, bool) and isinstance(occurrence, (int, float))
                            and occurrence >= 1 and occurrence <= len(starts)
                            and int(occurrence) == occurrence)
            require(valid_number, where, "occurrence debe seleccionar una coincidencia positiva existente")
        else:
            require(len(starts) == 1, where, "un fragmento de énfasis repetido requiere occurrence explícito")


def validate_code_presentation_contract(block, where):
    """Optional presentation does not evaluate a response or infer its solution."""
    if block.get("type") == "order_code" and "language" in block:
        language = block["language"]
        require(isinstance(language, str) and language.strip().lower() in {"cpp", "c++", "cmake"},
                where, "language de order_code debe ser cpp, c++ o cmake")
    if block.get("type") in {"drag_fill_code", "inline_fill_code"} and "explanation" in block:
        explanation = block["explanation"]
        require(isinstance(explanation, str) and bool(explanation.strip()),
                where, "explanation debe ser texto no vacío reservado al acierto")


def validate_answer_contract(block, where):
    validate_decimal_answer_contract(block, where)
    if block["type"] == "output_prediction" and "outputComparison" in block:
        mode = block["outputComparison"]
        require(isinstance(mode, str) and mode in {"trimmed", "exact"}, where, "outputComparison debe ser trimmed o exact")
    if block["type"] == "inline_fill_code" and "answerAlternatives" in block:
        groups = block["answerAlternatives"]
        answers = block["answers"]
        require(isinstance(groups, list) and len(groups) == len(answers), where, "una lista de alternativas por hueco")
        if not isinstance(groups, list):
            return
        for index, group in enumerate(groups):
            valid = isinstance(group, list) and bool(group) and all(isinstance(a, str) and a.strip() for a in group)
            require(valid, where, f"alternativas vacías o no textuales en hueco {index+1}")
            if valid:
                require(len(set(group)) == len(group), where, "alternativa duplicada")
                require(index < len(answers) and group[0] == answers[index], where, "conservar la respuesta previa como primera alternativa")


def require(condition, where, message):
    if not condition:
        ERRORS.append(f"{where}: {message}")


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def validate_project_path(value, where):
    """Rutas virtuales relativas y portables, sin normalización que oculte escapes."""
    valid = (
        isinstance(value, str)
        and bool(value.strip())
        and value == value.strip()
        and not any(ord(char) < 32 or char in '\\:<>"|?*' for char in value)
        and all(part not in {"", ".", ".."} for part in value.split("/"))
    )
    require(valid, where, "ruta de proyecto inválida: debe ser relativa, sin escapes ni separadores de Windows")
    return valid


def is_source_file(path):
    return "." in path and path.rsplit(".", 1)[1].lower() in SOURCE_SUFFIXES


def validate_cpp_compiler(block, where):
    require(isinstance(block.get("initialCode"), str), where, "initialCode debe ser texto")
    validate_cpp_output_check_contract(block, where)
    validate_cpp_compile_check_contract(block, where)
    if "initialStandardInput" in block:
        require(isinstance(block["initialStandardInput"], str), where, "initialStandardInput debe ser texto")

    if "cmake" in block:
        validate_cmake(block["cmake"], where)
        require("projectFiles" in block, where, "cmake requiere projectFiles")
        require("sourceFiles" not in block, where, "CMake selecciona las fuentes desde su descripción; no admite sourceFiles")

    if "projectFiles" not in block:
        require("sourceFiles" not in block, where, "sourceFiles requiere projectFiles")
        return

    files = block["projectFiles"]
    require(isinstance(files, list) and bool(files), where, "projectFiles debe ser una lista no vacía")
    if not isinstance(files, list):
        return
    paths = {}
    for index, item in enumerate(files, 1):
        file_where = f"{where} archivo {index}"
        require(isinstance(item, dict), file_where, "archivo debe ser un objeto")
        if not isinstance(item, dict):
            continue
        require(set(item) == {"path", "initialCode"}, file_where, "archivo requiere sólo path e initialCode")
        require(isinstance(item.get("initialCode"), str), file_where, "initialCode de archivo debe ser texto")
        path = item.get("path")
        if validate_project_path(path, file_where):
            require(path not in paths, file_where, "ruta de archivo duplicada")
            paths[path] = path

    # También se rechaza archivo/carpeta: a.cpp y a.cpp/util.h no pueden coexistir.
    for path in paths:
        for parent in PurePosixPath(path).parents:
            require(str(parent) not in paths, where, "una ruta de archivo también se usa como carpeta")

    if "cmake" in block:
        require(len(files) <= 128, where, "el proyecto CMake supera 128 archivos")
        size = sum(len(item["initialCode"].encode("utf-8")) for item in files
                   if isinstance(item, dict) and isinstance(item.get("initialCode"), str))
        require(size <= 8 * 1024 * 1024, where, "el proyecto CMake supera 8 MiB de texto")
        return

    inferred = [path for path in paths if is_source_file(path)]
    sources = block.get("sourceFiles", inferred)
    require(isinstance(sources, list) and bool(sources), where, "sourceFiles debe seleccionar al menos una fuente")
    if not isinstance(sources, list):
        return
    selected = set()
    for index, source in enumerate(sources, 1):
        source_where = f"{where} fuente {index}"
        if not validate_project_path(source, source_where):
            continue
        require(source not in selected, source_where, "fuente seleccionada más de una vez")
        selected.add(source)
        require(source in paths, source_where, "fuente no existe en projectFiles con ese nombre exacto")
        require(is_source_file(source), source_where, "se seleccionó una cabecera o archivo no fuente")


def normalize_output_check_input(value, mode):
    if mode == "tokens":
        text = value.strip(" \t\r\n\f\v")
        return tuple(re.split(r"[ \t\r\n\f\v]+", text)) if text else ()
    return value


def validate_cpp_compile_check_contract(block, where):
    """A reviewed compile-time rejection is independent of an execution oracle."""
    try:
        _state_layout.validate_compile_check_contract(block)
    except (ValueError, KeyError, TypeError) as error:
        require(False, where, f"objetivo de diagnóstico de compilación inválido: {error}")


def validate_cpp_output_check_contract(block, where):
    """Optional, explicit output oracles; never infer success from source code."""
    if "outputCheck" not in block:
        return
    config = block["outputCheck"]
    require(isinstance(config, dict), where, "outputCheck debe ser un objeto")
    if not isinstance(config, dict):
        return
    require(set(config) <= {"inputComparison", "cases"}, where, "campo outputCheck no admitido")
    mode = config.get("inputComparison", "exact")
    require(mode in ("exact", "tokens"), where, "inputComparison debe ser exact o tokens")
    cases = config.get("cases")
    require(isinstance(cases, list) and bool(cases), where, "outputCheck requiere cases no vacío")
    if not isinstance(cases, list):
        return
    seen = []
    for index, case in enumerate(cases, 1):
        case_where = f"{where} outputCheck caso {index}"
        require(isinstance(case, dict), case_where, "caso debe ser un objeto")
        if not isinstance(case, dict):
            continue
        require(set(case) <= {"standardInput", "outputs", "explanation", "reviewFeedback", "target", "standardError"}, case_where,
                "campo de caso outputCheck no admitido")
        if "standardError" in case:
            require(case["standardError"] == "nonBlank", case_where,
                    "standardError de caso sólo admite nonBlank; omitirlo conserva stdout solamente")
        target = case.get("target")
        if "target" in case:
            require(isinstance(block.get("cmake"), dict), case_where,
                    "target de caso requiere un proyecto CMake")
            require(isinstance(target, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.+-]{0,127}", target) is not None,
                    case_where, "target de caso debe ser un nombre de objetivo CMake válido")
        stdin = case.get("standardInput")
        require(isinstance(stdin, str), case_where, "standardInput de caso debe ser texto")
        if isinstance(stdin, str):
            normalized = normalize_output_check_input(stdin, mode)
            require(not any(previous_input == normalized and
                            (previous_target is None or target is None or previous_target == target)
                            for previous_target, previous_input in seen), case_where,
                    "objetivo/entrada de caso ambiguos según inputComparison")
            seen.append((target, normalized))
        correct = output_check_outputs(case.get("outputs"), case_where)
        if "explanation" in case:
            explanation = case["explanation"]
            require(isinstance(explanation, str) and bool(explanation.strip()), case_where,
                    "explanation de caso debe ser texto no vacío")
        if "reviewFeedback" not in case:
            continue
        reviews = case["reviewFeedback"]
        require(isinstance(reviews, list), case_where, "reviewFeedback de caso debe ser una lista")
        if not isinstance(reviews, list):
            continue
        claimed = set(correct)
        for review in reviews:
            require(isinstance(review, dict), case_where, "guía de caso debe ser objeto")
            if not isinstance(review, dict):
                continue
            require(set(review) == {"outputs", "text"}, case_where,
                    "guía de caso requiere sólo outputs y text")
            text = review.get("text")
            require(isinstance(text, str) and bool(text.strip()), case_where,
                    "texto de guía de caso no vacío")
            outputs = output_check_outputs(review.get("outputs"), case_where)
            require(not claimed.intersection(outputs), case_where,
                    "salida de guía repetida o también admitida como correcta")
            claimed.update(outputs)


def output_check_outputs(values, where):
    valid = isinstance(values, list) and bool(values) and all(isinstance(value, str) for value in values)
    require(valid, where, "outputs requiere una lista no vacía de textos exactos")
    if not valid:
        return set()
    require(len(set(values)) == len(values), where, "salida exacta duplicada en outputs")
    return set(values)


def validate_cmake(config, where):
    """Contrato opcional; los errores de CMakeLists se diagnostican en CMake real."""
    require(isinstance(config, dict), where, "cmake debe ser un objeto")
    if not isinstance(config, dict):
        return
    require(set(config) <= {"target", "options", "buildType"}, where, "campo cmake no admitido")
    target = config.get("target")
    require(isinstance(target, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.+-]{0,127}", target),
            where, "target CMake debe ser un nombre de objetivo")
    require(config.get("buildType", "Debug") in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"),
            where, "buildType CMake no admitido")
    options = config.get("options", {})
    require(isinstance(options, dict), where, "options CMake debe ser un objeto")
    if not isinstance(options, dict):
        return
    require(len(options) <= 64, where, "el proyecto CMake supera 64 opciones")
    for key, value in options.items():
        require(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,127}", key)
                and not key.upper().startswith(("CMAKE_", "LGP_")), where, "nombre de opción CMake reservado o no válido")
        require(isinstance(value, str) and len(value.encode("utf-16-le")) // 2 <= 4096
                and not any(ord(c) <= 31 or 127 <= ord(c) <= 159 for c in value),
                where, "valor de opción CMake debe ser texto sin controles, hasta 4096 unidades UTF-16")


# Correcciones de recorrido revisadas contra dd6787d. Sólo cambian las
# instrucciones de navegación; conservan código, respuestas y posición.
SEQUENCE_EDITORIAL_REVISIONS = {
    ('texto_interpretacion_y_flujos/lesson_comparar_lecturas_con_la_anterior.json', 5, 5, 'output_prediction', 'question'): ('db190e8e9342ec467defcf9183c2a538f3b14b735847c6a6244286dc0cef8885', '95088c79fc6a905cd70ad8c13c062969064c32098d5115aea368cc2c4488471e'),
    ('funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json', 7, 2, 'output_prediction', 'question'): ('af98f6fc02c3aa4db2b2137d5ea5c54d59e711fe9271c2a8edc2877109fcbb66', 'e42d814afe1c9984ecdd50a435cc33ab2e2a1bb2709cb2826148e97f7f2b50ea'),
    ('funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json', 11, 2, 'output_prediction', 'question'): ('3df63566bcee3902219b6128819d624e79af14436c01530b3b403ffcdb6678e0', 'd9ae7a16ab87332db2709232cbbfae584b3b2a1d075da24eeac2feeb6ef8051a'),
    ('objetos_y_estado/lesson_cadenas_y_texto.json', 8, 6, 'output_prediction', 'question'): ('ffd2567e4d964854df7fe1cda0bed34a2cc6179a4a6c9b3b58a52cafe26be492', '3094cccef7d145a3b6005f88c790ada0fbbd5f79f7a6df7f14c6e5ea4d28d62b'),
    ('objetos_y_estado/lesson_enteros_y_aproximaciones.json', 8, 6, 'output_prediction', 'question'): ('7322b70b9362e5e4c98c4046b45aafba681ee27792e6697955559ea4a80e68c7', 'cb57217b66e47ab26d44f2054bda9c44eec1340f9f402a9ab377d16a4299d890'),
    ('objetos_y_estado/lesson_representar_procesar_y_recuperar.json', 9, 4, 'output_prediction', 'question'): ('936e0b42f3571984e0c8273a5479bde496ca2df302bc9c6c5d8cb6ec60955bfa', 'f41a56d2ec85672ad01b19976c28ff8125660e7f5ebe2272395e10fc0b0787d3'),
    ('programas_y_ejecucion/lesson_primer_programa_y_salida.json', 5, 4, 'output_prediction', 'explanation'): ('2406d5a896fb0e0e871515a0456c6dc4762a02a78b26905184384763e5e35fd1', '8397553636b05c72a59949a90221785f4f29315bc2222e72e1e7329d9bb3bd33'),
}

# CPP_VALIDACION_EDITORIAL_RETIRO_U1: cuatro revisiones comprobadas contra
# d1933ed. Además del origen, tipo y campo, fijan el ID propio del ejercicio.
# No reemplazan las transiciones APA anteriores de esas mismas coordenadas.
RETIREMENT_EDITORIAL_REVISIONS = {
    ('programas_y_ejecucion/lesson_primer_programa_y_salida.json', 5, 4, 'output_prediction', 'question', 'prediccion_zona_inicial'): (
        '3e7fa5f573c566bc17a7308398d90ee0a849aa6cb849bb89f9a2d5d643b7b01c',
        'a1299f5fcc562260414ac9136337d77199cb18e4d362eec976c201ed34a2e43f'),
    ('programas_y_ejecucion/lesson_texto_secuencia_y_formato.json', 7, 4, 'output_prediction', 'question', 'prediccion_informe_inicial'): (
        '7f48db3071af57952889a2c2680f14dba906231d495638855feb71c4bf23c53a',
        'aac3dd2c9535276b6513a0adc2736bf5a5c84bc956dfa180fda9543813c524cd'),
    ('programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json', 4, 6, 'output_prediction', 'question', 'prediccion_revision_inicial'): (
        'daf9bb92af0fb387f61efc5a0062adf550c1bdffd15ab2a4be5b5b3f6b874d34',
        'cb36f39faab36ecb05dcf50b3f0a7a2509e1d541caa43a812a7bc5174bb80d00'),
    ('programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json', 4, 6, 'output_prediction', 'explanation', 'prediccion_revision_inicial'): (
        'ba4e030990f073ec920b75f627c1595317a7c58d2d12a6dc946744af32acfb11',
        '0f7908a8cedad6bd6e7ed1a8b9162c6d05752373b614a85b93b1d55b7b2e1761'),
}


# CPP_VALIDACION_EDITORIAL_U11L1: precisión de los escapes visibles en la
# pregunta española. La actividad histórica no tiene ID propio; el origen
# estable es block_0001_0001. No amplía las revisiones APA anteriores.
U11_QUESTION_EDITORIAL_REVISIONS = {
    ('texto_interpretacion_y_flujos/lesson_palabras_y_lecturas_condicionadas.json',
     2, 2, 'output_prediction', 'question', None): (
        '5c5e64db87dbf931030218aa78252bc0a030589e3caa563ab561728525cd94e6',
        'ea7b37861cd558227733f8d151dabcaccbe3c65c0f3b593f33c66eb99a13f207'),
}


# Pedidos de Work: la explicación de U1L2, la pregunta de U11L2 y las dos
# explicaciones de U11L11. Cada par fija texto, coordenada histórica y la
# identidad explícita original (None exige que el campo id no exista).
# La explicación antigua de U1L2 no es una autorización de destino genérico:
# ya coincide con sus fuentes publicadas y sólo admite esta revisión exacta.
WORK_EXACT_EDITORIAL_REVISIONS = {
    ('programas_y_ejecucion/lesson_primer_programa_y_salida.json',
     4, 3, 'order_code', 'explanation', 'armar_programa'): (
        'c50562e326eb30529a30379fdc5fc521f83e6361b8d3cb2b2e4fb4bb53425bb5',
        '60d676de2ed953914099a4dc6288b7466e97a3a70bc11bcb6169b2c86f652822'),
    ('texto_interpretacion_y_flujos/lesson_lineas_y_separadores_pendientes.json',
     3, 2, 'output_prediction', 'question', None): (
        '731496fab5e064157fea4e0108c6990c18392e403b39628d47f556d721cb3e35',
        '6e0a38ef37e7ad479787f5e501679c00568c8f5fdfd91c6b8ecae12851fd9f5a'),
    ('texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json',
     3, 3, 'multiple_choice', 'explanation', None): (
        '54a1c2f05e6cf4af61cb561eb01b9938ed20931e6321cfa783538a9c640635e2',
        'c85463058609727aaf54b4ea6721db53136af0bf37068c86bcc0b8f7b198a2e2'),
    ('texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json',
     7, 3, 'output_prediction', 'explanation', None): (
        '5fcc83da4e6f4dcd16510343f01d4633b95ad901841af88f68b12a9c91554043',
        'eb3bd0aea79408b2146891b3116c41678e8cf098bbd56d3328dba1ba936ebadd'),
}


# CPP_COMPROBACION_STDERR_POR_OBJETIVO: la explicación de acierto revisada
# pertenece sólo al editor histórico block_0002_0008 de U8L3, caso cero.
# Las coordenadas recibidas son las de origen, verificadas por stateLayout.
STDERR_EDITORIAL_REVISION = (
    'app/src/main/assets/content/es/programming/cpp_fundamentals/excepciones_y_recuperacion/lesson_limites_comprobados_y_tipos_de_captura.json',
    2, 8, 0,
    'ff5fd32c054a7359cf7de5af485139da50eb0fa0100ce0733354cb39c68c7f1b',
    'fe30c15ffa1de1660093c32432c9efb7c90fac1b95901f216144d467b3be852f',
)


def _approved_stderr_explanation(relative, pn, bn, index, before, after):
    if (relative, pn, bn, index) != STDERR_EDITORIAL_REVISION[:4] \
            or after.get('standardError') != 'nonBlank':
        return False
    previous, revised = before.get('explanation'), after.get('explanation')
    return isinstance(previous, str) and isinstance(revised, str) and (
        hashlib.sha256(previous.encode('utf-8')).hexdigest(),
        hashlib.sha256(revised.encode('utf-8')).hexdigest(),
    ) == STDERR_EDITORIAL_REVISION[4:]


# CPP_VALIDACION_EDITORIAL_U11L1: only the first success explanation is added.
# These are stable_state_layout's canonical framing digests of the whole case
# producing six reports, including its exact input/output and comparison.
U11_COMPILER_EXPLANATION_REVISION = (
    'app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_palabras_y_lecturas_condicionadas.json',
    4, 3,
    '692e125a7d8b75cdf3abe9c2fddcb1ad462ffc93a6d261cce761a43a45bd747a',
    'b27ec986ffb0d8f18c2b84772251abd95f9d49e5e01fe391f54245ad1ccf16b5',
)


def preserves_previous_compiler(relative, pn, bn, old, current, old_lesson):
    """Permite nuevos campos, manteniendo el programa anterior salvo la migración cin."""
    base = {key: value for key, value in current.items() if key not in COMPILER_EXTENSIONS or key in old}
    if isinstance(old.get('outputCheck'), dict) and isinstance(base.get('outputCheck'), dict):
        # Adding an explicit runtime-channel objective preserves historical draft
        # addresses and stdout answers. Completion proofs independently hash the
        # entire new objective, so an old stdout-only success cannot satisfy it.
        previous_cases = old['outputCheck'].get('cases')
        cases = base['outputCheck'].get('cases')
        if isinstance(previous_cases, list) and isinstance(cases, list) and len(previous_cases) == len(cases):
            projected = copy.deepcopy(base['outputCheck'])
            for index, (before, after) in enumerate(zip(previous_cases, projected['cases'])):
                if isinstance(before, dict) and isinstance(after, dict) \
                        and _approved_stderr_explanation(relative, pn, bn, index, before, after):
                    after['explanation'] = before['explanation']
                if isinstance(before, dict) and isinstance(after, dict) and 'standardError' not in before \
                        and after.get('standardError') == 'nonBlank':
                    del after['standardError']
            base = dict(base, outputCheck=projected)
    if ((relative, pn, bn) == U11_COMPILER_EXPLANATION_REVISION[:3]
            and old.get('type') == current.get('type') == 'cpp_compiler'
            and 'id' not in old and 'id' not in current
            and isinstance(old.get('outputCheck'), dict) and isinstance(current.get('outputCheck'), dict)
            and (_state_layout.fingerprint(old['outputCheck']), _state_layout.fingerprint(current['outputCheck']))
            == U11_COMPILER_EXPLANATION_REVISION[3:]):
        # Restore only this fully verified outputCheck. Everything outside it
        # still passes through the unchanged strict legacy-preservation guard.
        base = dict(base, outputCheck=old['outputCheck'])
    if (relative, pn, bn) == (
        'app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json', 4, 2
    ) and 'outputCheck' in old and 'outputCheck' in current:
        # La guía de Zona segura ya no remite a una «segunda modificación».
        # Contrato completo exacto: no licencia cambios de entrada ni salida.
        def digest_contract(value):
            return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                             separators=(',', ':')).encode('utf-8')).hexdigest()
        if (digest_contract(old['outputCheck']), digest_contract(current['outputCheck'])) == (
            'b4aecf68cd24b98e11a063ab4522d1bc72e026122b15718938cf33a9f0cda20d',
            'fa139154bb8c6ed05b663b3da212137c1d42302c7a6ac4e664e74802689ccb7f'
        ):
            base = dict(base, outputCheck=old['outputCheck'])
    if relative == STDIN_LESSON and (pn, bn) == (6, 2):
        # Única sustitución autorizada: p7 b3 pasa del esqueleto a p8 b2 existente.
        expected = dict(old)
        expected["initialCode"] = old_lesson["pages"][7]["blocks"][1]["code"]
        return base == old or (
            base == expected
            and current.get("initialStandardInput") == "Rita 4"
            and "projectFiles" not in current
            and "sourceFiles" not in current
        )
    return base == old


def restore_approved_editorial_fields(location, old, current):
    """Project only individually approved text transitions onto their old values.

    Every remaining key still passes through the original preservation guards.
    Neither source nor destination can be replaced by an arbitrary explanation.
    """
    result = current
    for field in ("question", "explanation"):
        key = location + (old.get("type"), field)
        exercise_id = old.get("id") if isinstance(old.get("id"), str) else None
        exact_identity = ("id" not in old and "id" not in current) if exercise_id is None \
            else current.get("id") == exercise_id
        transitions = (APPROVED_EDITORIAL_REVISIONS.get(key), SEQUENCE_EDITORIAL_REVISIONS.get(key),
                       RETIREMENT_EDITORIAL_REVISIONS.get(key + (exercise_id,)),
                       U11_QUESTION_EDITORIAL_REVISIONS.get(key + (None,))
                       if "id" not in old and "id" not in current else None,
                       WORK_EXACT_EDITORIAL_REVISIONS.get(key + (exercise_id,)) if exact_identity else None)
        before, after = old.get(field), current.get(field)
        if not isinstance(before, str) or not isinstance(after, str):
            continue
        digests = (hashlib.sha256(before.encode("utf-8")).hexdigest(),
                   hashlib.sha256(after.encode("utf-8")).hexdigest())
        if digests in transitions:
            result = dict(result, **{field: before})
    return result


def previous_exercise_difference(old, current):
    fields = sorted(key for key in old.keys() | current.keys() if old.get(key) != current.get(key))
    return "campos previos modificados sin autorización: " + ", ".join(fields)


def preserves_previous_exercise(relative, pn, bn, old, current):
    old = without_review_feedback(old)
    current = without_review_feedback(current)
    # La explicación opcional nueva de inline no reemplaza ningún dato anterior.
    # Si ya existía en el bloque de referencia, su texto sigue protegido.
    if old.get("type") == "inline_fill_code" and "explanation" not in old:
        current = {key: value for key, value in current.items() if key != "explanation"}
    if current == old:
        return True
    lesson_path = Path(relative).relative_to(CONTENT.relative_to(ROOT)).as_posix()
    location = (lesson_path, pn + 1, bn + 1)
    if (location == ("datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json", 8, 2)
            and old.get("type") == "math_puzzle"
            and _state_layout.fingerprint(old) == _state_layout.MATH_LABEL_BEFORE
            and _state_layout.fingerprint(current) == _state_layout.MATH_LABEL_AFTER):
        return _state_layout.historical_math_payload(current) == old
    current = restore_approved_editorial_fields(location, old, current)
    if current == old:
        return True
    if location in DECIMAL_LITERAL_LOCATIONS and old["type"] == "fill_code":
        if current == dict(old, answerComparison=DECIMAL_LITERAL_MODE):
            return True
    revision = FILL_CODE_REVISIONS.get(location)
    if revision is not None and old["type"] == "fill_code":
        expected = dict(old, **revision)
        explanation = EXPLANATION_CLARIFICATIONS.get(location)
        if explanation is not None and old["type"] == explanation[0]:
            expected["explanation"] = explanation[1]
        return (
            current.get("answers", [])[:len(old["answers"])] == old["answers"]
            and current == expected
        )
    if location in EXACT_OUTPUT_LOCATIONS and old["type"] == "output_prediction":
        if current == dict(old, outputComparison="exact"):
            return True
    alternatives = INLINE_ANSWER_ALTERNATIVES.get(location)
    if alternatives is not None and old["type"] == "inline_fill_code":
        if current == dict(old, answerAlternatives=alternatives):
            return True
    revision = MULTIPLE_CHOICE_DISTRACTOR_REVISIONS.get(location)
    if revision is not None and old["type"] == "multiple_choice":
        options, explanation = revision
        reviewed = WORK_EXACT_EDITORIAL_REVISIONS.get(
            location + ("multiple_choice", "explanation", old.get("id")))
        if (reviewed is not None and isinstance(old.get("explanation"), str)
                and hashlib.sha256(old["explanation"].encode("utf-8")).hexdigest() == reviewed[1]
                and current.get("explanation") != old["explanation"]):
            # The older distractor approval cannot roll a later reviewed
            # explanation back to its original wording.
            return False
        return (
            options[old["correctIndex"]] == old["options"][old["correctIndex"]]
            and current == dict(old, options=options, explanation=explanation)
        )
    extension = ACCEPTED_ANSWER_EXTENSIONS.get((lesson_path, pn + 1, bn + 1))
    if extension is not None and old["type"] == "fill_code":
        previous_answers, accepted_answers = extension
        if (
            old["answers"] == list(previous_answers)
            and current == dict(old, answers=list(accepted_answers))
        ):
            return True
    text = EXERCISE_TEXT_CLARIFICATIONS.get((lesson_path, pn + 1, bn + 1))
    if text is not None and old["type"] == text[0]:
        return current == dict(old, question=text[1], explanation=text[2])
    permutation = MULTIPLE_CHOICE_OPTION_PERMUTATIONS.get((lesson_path, pn + 1, bn + 1))
    if permutation is not None and old["type"] == "multiple_choice":
        if sorted(permutation) != list(range(len(old["options"]))):
            return False
        expected = dict(
            old,
            options=[old["options"][index] for index in permutation],
            correctIndex=permutation.index(old["correctIndex"]),
        )
        return current == expected
    explanation = EXPLANATION_CLARIFICATIONS.get((lesson_path, pn + 1, bn + 1))
    if (
        explanation is not None
        and old["type"] == explanation[0]
        and current == dict(old, explanation=explanation[1])
    ):
        return True
    question = QUESTION_CLARIFICATIONS.get((lesson_path, pn + 1, bn + 1))
    if (
        question is not None
        and old["type"] in {"fill_code", "output_prediction"}
        and current == dict(old, question=question)
    ):
        return True
    if (
        lesson_path == "datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json"
        and (pn, bn) == (1, 1)
        and old["type"] == "output_prediction"
    ):
        # Sólo p2 b2 retira estas definiciones aún no explicadas y no usadas.
        # El resto de la fuente y todos los demás campos deben coincidir con HEAD.
        unused_operators = (
            "bool operator==(const Pedido& a, const Pedido& b) { return mismo_valor(a, b); }\n"
            "bool operator!=(const Pedido& a, const Pedido& b) { return !(a == b); }\n"
        )
        return (
            old["code"].count(unused_operators) == 1
            and current == dict(old, code=old["code"].replace(unused_operators, "", 1))
        )
    return False


def validate_lesson_state_contract(document, where):
    # These optional Windows presentation relationships reuse the immutable
    # layout map. This validator does not add them to the Android runtime.
    try:
        _state_layout.validate(document)
    except (ValueError, KeyError, TypeError) as error:
        require(False, where, f"identidad de estado o relación de presentación inválida: {error}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", help="Verified published commit; mandatory for stateLayout adoption/updates")
    args = parser.parse_args(argv)
    # Check all routes/locales, including metadata removed from the working tree.
    # A self-consistent regenerated origin is not evidence of a safe migration.
    try:
        _state_layout.check_repository(ROOT, args.base_ref)
        base_commit = _state_layout.resolve_base_commit(ROOT, args.base_ref or "HEAD") if (ROOT / ".git").exists() else None
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"Conservación de estado no verificada: {error}\n")
    lessons = {}
    blocks = 0
    # Check the complete visible document first, including v2 additions. The
    # later historical projection only compares existing state; it must never
    # hide a malformed newly authored activity from these structural checks.
    for path in sorted(CONTENT.glob("*/lesson_*.json")):
        doc = load(path)
        validate_lesson_state_contract(doc, path)
        expected = "__".join(("cpp_fundamentals", path.parent.name, path.stem[7:]))
        require(doc["id"] == expected, path, "ID distinto de la ruta del loader")
        require(doc["id"] not in lessons, path, "ID de lección duplicado")
        lessons[doc["id"]] = doc
        require(bool(doc["pages"]), path, "lección sin páginas")
        for pn, page in enumerate(doc["pages"], 1):
            require(bool(page["blocks"]), path, f"página {pn} vacía")
            for bn, block in enumerate(page["blocks"], 1):
                blocks += 1
                where = f"{path.name} p{pn} b{bn}"
                kind = block["type"]
                require(kind in FIELDS, where, "tipo de bloque no soportado")
                if kind not in FIELDS:
                    continue
                require(set(block) <= FIELDS[kind], where, "campo ajeno al contrato comprobado")
                validate_review_feedback_contract(block, where)
                validate_code_presentation_contract(block, where)
                validate_paragraph_emphasis_contract(block, where)
                if kind == "math_puzzle":
                    validate_math_puzzle_contract(block, where)
                if kind == "cpp_compiler":
                    validate_cpp_compiler(block, where)
                if kind in {"heading", "paragraph", "note"}:
                    require(bool(block.get("text", "").strip()), where, "texto vacío")
                if kind == "multiple_choice":
                    options = block["options"]
                    require(len(options) >= 2 and len(set(options)) == len(options), where, "opciones insuficientes o duplicadas")
                    require(0 <= block["correctIndex"] < len(options), where, "índice de respuesta fuera de rango")
                if kind in {"fill_code", "output_prediction", "inline_fill_code"}:
                    answers = block.get("answers")
                    require(isinstance(answers, list) and bool(answers) and all(isinstance(a, str) and a.strip() for a in answers), where, "respuesta vacía o no textual")
                    validate_answer_contract(block, where)
                if kind == "order_code":
                    require(sorted(block["correctOrder"]) == list(range(len(block["fragments"]))), where, "orden no es una permutación")
                if kind in {"drag_fill_code", "inline_fill_code"}:
                    key = "correctOrder" if kind == "drag_fill_code" else "answers"
                    require(block["template"].count("[]") == len(block[key]), where, "huecos y respuestas no coinciden")
                    if kind == "drag_fill_code":
                        order = block[key]
                        require(all(0 <= i < len(block["choices"]) for i in order), where, "ficha fuera de rango")
                        require(len(set(order)) == len(order), where, "una ficha física se reutiliza")

    units = {}
    for path in sorted(CONTENT.glob("*/unit_*.json")):
        unit = load(path)
        expected = "cpp_fundamentals__" + path.parent.name
        require(unit["courseId"] == "cpp_fundamentals" and unit["unitId"] == expected, path, "ID de unidad no corresponde al loader")
        require(bool(unit["lessons"]), path, "unidad sin lecciones")
        units[unit["unitId"]] = unit
        numbers = [lesson["number"] for lesson in unit["lessons"]]
        require(numbers == list(range(1, len(numbers) + 1)), path, "numeración de lecciones discontinua")
        seen = set()
        for item in unit["lessons"]:
            require(item["id"] not in seen, path, "lección repetida en índice")
            seen.add(item["id"])
            require(item["id"] in lessons, path, "referencia a lección inexistente")
            if item["id"] in lessons:
                require(item["title"] == lessons[item["id"]]["title"], path, "título no coincide con la lección")

    course = load(CONTENT / "course_cpp_fundamentals.json")
    require([u["number"] for u in course["units"]] == list(range(1, len(course["units"]) + 1)), "curso", "numeración de unidades discontinua")
    for item in course["units"]:
        require(item["id"] in units, "curso", "referencia a unidad inexistente")
        if item["id"] in units:
            require(item["title"] == units[item["id"]]["title"], "curso", "título de unidad no coincide")

    # Un mapa contrastado permite proyectar bloques a sus posiciones publicadas.
    git = ROOT / ".git"
    if git.exists():
        files = [relative for relative in sorted(_state_layout._lesson_paths_at(ROOT, base_commit))
                 if relative.startswith(CONTENT.relative_to(ROOT).as_posix() + "/")]
        for relative in files:
            old = json.loads(subprocess.check_output(["git", "show", base_commit + ":" + relative], cwd=ROOT, text=True, encoding="utf-8"))
            if not (ROOT / relative).is_file():
                require(False, relative, "lección previa perdida")
                continue
            new = load(ROOT / relative)
            try:
                historical_positions = _state_layout.validate(old)
                historical_lesson = _state_layout.original_projection(old)
                new = _state_layout.align_for_previous_validation(old, new)
            except (ValueError, KeyError, TypeError) as error:
                require(False, relative, f"traslado de estado no verificado: {error}")
                continue
            require(new["id"] == old["id"], relative, "ID previo modificado")
            for pn, page in enumerate(old["pages"]):
                for bn, block in enumerate(page["blocks"]):
                    kind = block["type"]
                    if kind not in EXERCISES | {"code", "cpp_compiler"}:
                        continue
                    try:
                        current = new["pages"][pn]["blocks"][bn]
                    except IndexError:
                        require(False, relative, f"bloque previo perdido p{pn+1} b{bn+1}")
                        continue
                    require(current["type"] == kind and current.get("id") == block.get("id"), relative, f"posición/ID previo modificado p{pn+1} b{bn+1}")
                    # Editorial authorizations belong to immutable coordinates,
                    # even when the published baseline was already rearranged.
                    origin_p, origin_b = historical_positions[pn, bn]
                    if kind == "cpp_compiler":
                        require(preserves_previous_compiler(relative, origin_p, origin_b, block, current, historical_lesson), relative, f"código legacy previo modificado p{pn+1} b{bn+1}")
                    elif kind != "multiple_choice" or (
                        Path(relative).relative_to(CONTENT.relative_to(ROOT)).as_posix(),
                        origin_p + 1,
                        origin_b + 1,
                    ) in (MULTIPLE_CHOICE_OPTION_PERMUTATIONS.keys() | MULTIPLE_CHOICE_DISTRACTOR_REVISIONS.keys()
                          | CHOICE_OPTION_REVISION_LOCATIONS):
                        require(preserves_previous_exercise(relative, origin_p, origin_b, block, current), relative,
                                f"{previous_exercise_difference(block, current)} p{pn+1} b{bn+1}")

    if ERRORS:
        print("\n".join(ERRORS))
        raise SystemExit(1)
    print(f"Contrato, referencias y posiciones previas correctos: {len(lessons)} lecciones, {blocks} bloques. No certifica semántica C++ ni ejecución Android.")


if __name__ == "__main__":
    main()

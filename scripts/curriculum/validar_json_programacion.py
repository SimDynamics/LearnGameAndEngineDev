"""Comprueba el contrato de assets y conserva las posiciones del contenido previo.

No compila C++, no modifica archivos y no certifica las respuestas pedagógicas.
"""

import json
import re
import subprocess
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
FIELDS = {
    "heading": {"type", "text"},
    "paragraph": {"type", "text"},
    "note": {"type", "text"},
    "code": {"type", "language", "code"},
    "multiple_choice": {"type", "id", "question", "options", "correctIndex", "explanation"},
    "fill_code": {"type", "id", "question", "language", "code", "answers", "explanation", "answerComparison"},
    "output_prediction": {"type", "id", "question", "language", "code", "answers", "explanation", "outputComparison"},
    "order_code": {"type", "id", "question", "fragments", "correctOrder", "explanation"},
    "drag_fill_code": {"type", "id", "question", "language", "template", "choices", "correctOrder"},
    "inline_fill_code": {"type", "id", "question", "language", "template", "answers", "answerAlternatives"},
    # Los proyectos sustituyen initialCode al ejecutar; se conserva el campo legacy.
    "cpp_compiler": {"type", "language", "initialCode", "initialStandardInput", "projectFiles", "sourceFiles", "cmake"},
}
EXERCISES = set(FIELDS) - {"heading", "paragraph", "note", "code", "cpp_compiler"}
COMPILER_EXTENSIONS = {"initialStandardInput", "projectFiles", "sourceFiles", "cmake"}
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
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 4, 3):
        ("order_code", "Primero #include <iostream> permite usar el recurso de salida; después se abre main, se escribe la instrucción que muestra el mensaje y se termina el cuerpo."),
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


def preserves_previous_compiler(relative, pn, bn, old, current, old_lesson):
    """Permite nuevos campos, manteniendo el programa anterior salvo la migración cin."""
    base = {key: value for key, value in current.items() if key not in COMPILER_EXTENSIONS or key in old}
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


def preserves_previous_exercise(relative, pn, bn, old, current):
    if current == old:
        return True
    lesson_path = Path(relative).relative_to(CONTENT.relative_to(ROOT)).as_posix()
    location = (lesson_path, pn + 1, bn + 1)
    if location in DECIMAL_LITERAL_LOCATIONS and old["type"] == "fill_code":
        if current == dict(old, answerComparison=DECIMAL_LITERAL_MODE):
            return True
    revision = FILL_CODE_REVISIONS.get(location)
    if revision is not None and old["type"] == "fill_code":
        return (
            current.get("answers", [])[:len(old["answers"])] == old["answers"]
            and current == dict(old, **revision)
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


def main():
    lessons = {}
    blocks = 0
    for path in sorted(CONTENT.glob("*/lesson_*.json")):
        doc = load(path)
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

    # Mantener páginas/bloques evita trasladar las claves de progreso existentes.
    git = ROOT / ".git"
    if git.exists():
        files = subprocess.check_output(["git", "ls-files", "app/src/main/assets/content/es/programming/cpp_fundamentals/*/lesson_*.json"], cwd=ROOT, text=True).splitlines()
        for relative in files:
            old = json.loads(subprocess.check_output(["git", "show", "HEAD:" + relative], cwd=ROOT, text=True, encoding="utf-8"))
            new = load(ROOT / relative)
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
                    if kind == "cpp_compiler":
                        require(preserves_previous_compiler(relative, pn, bn, block, current, old), relative, f"código legacy previo modificado p{pn+1} b{bn+1}")
                    elif kind != "multiple_choice" or (
                        Path(relative).relative_to(CONTENT.relative_to(ROOT)).as_posix(),
                        pn + 1,
                        bn + 1,
                    ) in (MULTIPLE_CHOICE_OPTION_PERMUTATIONS | MULTIPLE_CHOICE_DISTRACTOR_REVISIONS):
                        require(preserves_previous_exercise(relative, pn, bn, block, current), relative, f"código/respuesta previa modificada p{pn+1} b{bn+1}")

    if ERRORS:
        print("\n".join(ERRORS))
        raise SystemExit(1)
    print(f"Contrato, referencias y posiciones previas correctos: {len(lessons)} lecciones, {blocks} bloques. No certifica semántica C++ ni ejecución Android.")


if __name__ == "__main__":
    main()

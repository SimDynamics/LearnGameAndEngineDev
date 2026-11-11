# Legibilidad del código y separadores opcionales

## Alcance y conservación

Esta capacidad se aplica a la presentación del código de las lecciones de
Programación, incluidos los ejemplos `code`, las predicciones, los puzles
`fill_code`, `inline_fill_code`, `drag_fill_code`, `order_code`, los editores
`cpp_compiler` y los archivos de proyectos CMake.

La presentación **nunca** debe editar el código fuente ni el borrador del
estudiante para insertar sangrías, líneas vacías o separadores. Tampoco altera
`fragments`, `choices`, `correctOrder`, las referencias, `stateLayout`,
la identidad de los ejercicios, las respuestas históricas ni el progreso.

## Separación visual frente a contenido real

Para una lección española con `stateLayout` v2, Work puede declarar en la
**raíz** un número acotado de tarjetas visuales vacías:

```json
"orderVisualSeparators": [
  {"activity": "block_0002_0003", "count": 2}
]
```

El identificador debe pertenecer a un `order_code` real de esa lección.
Se admiten de una a cuatro tarjetas por propietario y hasta dieciséis
propietarios por declaración. Ninguna tarjeta visual se añade a
`fragments`, `correctOrder` o al estado persistido. Se pueden mover,
ocultar y restaurar; la secuencia evaluada contiene únicamente las tarjetas
de fuente originales. Por lo tanto, cero, una o varias tarjetas decorativas
no cambian el veredicto ni producen un nuevo intento.

**Importante:** un fragmento vacío que **ya formaba parte del contenido
histórico** es distinto de una tarjeta declarada como separador visual. Su
identidad y su contrato original no se eliminan de la evaluación por el
simple hecho de estar en blanco. Así se conservan respuestas y recibos
preexistentes, así como saltos físicos que podrían importar al código.

En `drag_fill_code` se usan los saltos auténticos de `code_template`
como separación visual. No se inventan huecos `[]` ni elecciones vacías
que pasarían a ser obligatorias.

## Lectura, copia y compilación

Las tarjetas y los editores mantienen los espacios de sangría con el
ancho real de la fuente monoespaciada, las líneas vacías auténticas y
los saltos de línea del contenido. El ajuste de línea y el zoom son
operaciones exclusivamente visuales.

Al copiar la secuencia visible de un `order_code`, el lector reconstruye
las tarjetas de fuente, excluyendo los separadores decorativos.
Una tarjeta fuente vacía mantiene su límite físico; si un fragmento ya
termina en un salto, no se introduce otro automáticamente.
Esto distingue `\\n` escrito en una cadena de un salto de línea físico
y protege directivas `#define` continuadas, comentarios `//`, cadenas
raw y comandos o cadenas CMake. No se normaliza `stdout`, `stderr` ni
se debilita la comparación exacta de las respuestas.

## Comprobaciones automáticas

- `native.order_spacing_contract`: orden canónico y fallos negativos,
  espacios y saltos C++/CMake, copia con tarjetas vacías, identidad
  histórica y permanencia de los huecos `drag_fill_code`.
- `native.order_code_whitespace_view`: RichEdit Windows, medidas de
  sangría y alturas, ajuste/zoom, arrastre/omisión/restauración de
  tarjetas opcionales y representación exacta.
- `test_order_line_refinement.py`: las catorce refinaciones históricas
  y sus restricciones de fuente, respuesta, traducción y estado.
- `native.wasi_runner`: construcción y ejecución C++23 real de
  fuente copiada que incluye macro continuada, cadena raw y
  comentario de línea.

El usuario conserva la revisión visual y auditiva de las lecciones.
Work puede mejorar sus separaciones editoriales mediante mecanismos
curriculares compatibles, sin modificar el motor ni regenerar los
orígenes históricos.

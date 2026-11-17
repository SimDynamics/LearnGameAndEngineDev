# Simulaciones curriculares en Windows

Un bloque `simulation` permite explorar un modelo calculado mediante datos,
variables, agrupaciones, explicaciones y salida. Work puede declarar sus textos,
valores, escenarios y vista en el JSON de una lección. No hace falta añadir una
regla al motor por cada nombre de variable o conjunto de datos.

La primera versión implementa el modelo `arrayRunsV1`: ordenar una copia de un
array, recorrer grupos consecutivos de valores iguales, contar sus frecuencias
y conservar todos los valores que alcanzan la frecuencia máxima. Es un modelo
determinista de ese algoritmo. No interpreta ni ejecuta el código C++ de un
editor, y no puede presentarse como una depuración de un programa arbitrario.

## Incorporar un bloque

El archivo [array_runs_simulation.json](examples/array_runs_simulation.json)
contiene un bloque completo listo para adaptar. Declara datos enteros, permite
editarlos y ofrece escenarios de empate, valores iguales, un elemento y array
vacío. Se incorpora como un bloque nuevo mediante las adiciones del contrato
de [estado estable](curriculum/STABLE_LESSON_STATE.md), conservando las
identidades y direcciones de los bloques existentes.

El contenido tiene esta estructura:

```json
{
  "type": "simulation",
  "id": "recorrer_grupos_y_frecuencias",
  "title": "Recorrer grupos y conservar los más frecuentes",
  "description": "Modifica los datos y observa cómo cambian los grupos.",
  "simulation": {
    "version": 1,
    "model": "arrayRunsV1",
    "view": "walkthrough",
    "valueType": "integer",
    "values": [7, 3, 2, 7, 3, 9, 2, 3, 7],
    "sort": true,
    "editable": true,
    "labels": {
      "array": "Datos",
      "start": "inicio",
      "end": "fin exclusivo",
      "frequency": "frecuencia",
      "maximum": "máximo",
      "leaders": "valores con el máximo",
      "output": "Salida calculada"
    },
    "explanations": {
      "initial": "Hay {size} datos; todavía no se ha contado ningún grupo.",
      "start": "Comienza {value} en {start}. El primer elemento ya está contado.",
      "advance": "El grupo de {value} termina antes de {end} y contiene {frequency} elementos.",
      "newMaximum": "El nuevo máximo vale {maximum}; lo alcanza {leaders}.",
      "tie": "{value} también alcanza {maximum}. Los candidatos son {leaders}.",
      "belowMaximum": "{frequency} no supera el máximo {maximum}.",
      "done": "El recorrido termina con máximo {maximum} y candidatos {leaders}.",
      "empty": "No hay elementos ni un primer grupo que leer."
    },
    "outputTemplate": "{value}: {frequency}",
    "scenarios": [
      {"id": "empate", "label": "Dos valores empatan", "values": [4, 1, 4, 1, 8]},
      {"id": "vacio", "label": "Sin elementos", "values": []}
    ]
  }
}
```

`id` es opcional; no sustituye al propietario de `stateLayout`. Todos los demás
campos del ejemplo son obligatorios salvo `scenarios`, que puede omitirse o ser
una lista vacía. No se admiten propiedades adicionales ni estados de ejecución
escritos a mano.

## Cinco vistas del mismo modelo

Cambiar `view` elige la presentación inicial sin cambiar el algoritmo, los
valores, las frecuencias ni los criterios de empate.

| `view` | Uso |
|---|---|
| `array` | Examinar valores y posiciones del array. |
| `groups` | Examinar los grupos contiguos de valores iguales. |
| `walkthrough` | Recorrer los pasos con índices, variables, explicación y salida. |
| `candidates` | Comparar las decisiones que conservan o cambian los candidatos. |
| `summary` | Consultar el resultado calculado del recorrido completo. |

Las agrupaciones, los índices y las frecuencias se calculan a partir de los
datos. El contenido no declara manualmente qué celdas pertenecen a cada grupo,
qué número debe mostrar una variable ni cuál debe ser la respuesta final.
Las etiquetas permiten presentar nombres adecuados a la explicación del curso;
no cambian el significado de las variables del modelo.

## Datos, escenarios y edición

`valueType: "integer"` admite entre cero y 64 números enteros de 32 bits,
incluidos los extremos −2147483648 y 2147483647. En una lección con `stateLayout`,
escribe `version` y los datos numéricos como enteros JSON **sin punto decimal ni
exponente**: utiliza `1`, no `1.0` ni `1e0`. Esta condición del adaptador
histórico de la lección también se aplica a los escenarios, los bloques
retirados y las instantáneas de lectura. El adaptador de un bloque aislado
admite esas otras escrituras cuando representan un entero, pero eso no las
habilita en una lección con `stateLayout`. Los booleanos, las fracciones, los
valores no finitos y los números fuera del intervalo no se admiten en ninguno
de los dos casos.

`valueType: "text"` utiliza entre cero y 64 cadenas UTF-8 de uno a 128 bytes
cada una. Conserva sus espacios; una cadena que contiene espacios sigue siendo
un dato distinto de la cadena vacía, que no se admite. Un valor no puede
contener NUL, retorno de carro ni salto de línea. No se mezclan textos y
números dentro de una simulación o de sus escenarios.

Para explorar palabras, adapta el ejemplo completo: cambia `valueType` a
`"text"`, usa `values: ["luna", "sol", "luna", "nube", "sol"]` y sustituye
también los valores numéricos de sus escenarios por textos o elimina esos
escenarios. En este caso, `luna` y `sol` empatan con frecuencia dos.

Con `sort: true`, el motor ordena una **copia** y conserva los datos de entrada.
La ordenación numérica es ascendente. La ordenación de texto compara bytes
UTF-8; no aplica ordenación lingüística, conversión de mayúsculas ni eliminación
de acentos. Por ejemplo, `"Z"` precede a `"a"` y `"z"` precede a `"á"`.

Con `sort: false`, el array inicial, cada escenario y cualquier entrada editada
deben estar ya en ese orden ascendente. Los valores repetidos son válidos.
Este requisito garantiza que cada valor forme un único grupo contiguo y que
su frecuencia corresponda al total de apariciones en el array.

`editable: true` habilita el cambio de los datos. Se introduce un valor por
línea. Los enteros admiten signo y espacios exteriores; no admiten expresiones
como `2 + 3`, sufijos de C++ ni decimales en ese campo de entrada. En los datos
de texto se conservan los espacios de cada línea. Una entrada vacía representa
un array vacío. El salto final es opcional; una línea vacía intermedia no
representa un valor de texto válido.

Hay como máximo ocho escenarios. Cada uno declara exclusivamente `id`, `label`
y `values`. Su identificador es único, tiene de uno a 64 caracteres ASCII,
comienza por una letra o guion bajo y continúa con letras, dígitos, guiones
bajos o guiones. Sus valores cumplen las mismas reglas que el array inicial.
Seleccionar un escenario proporciona otra entrada al mismo modelo.

## Índices, pasos y salida

Los índices comienzan en cero. El grupo ocupa el intervalo `[start, end)`:
incluye `start` y termina **antes** de `end`. Su frecuencia es `end - start`.
`end` puede valer el tamaño del array; eso describe el límite posterior al
último elemento y no autoriza leer `datos[end]`.

Cada recorrido no vacío comienza con `initial`, sin grupo observado. La fase
`start` cuenta el primer elemento del grupo. Cada `advance` incorpora otro
elemento igual. Al terminar el grupo, el modelo produce exactamente una
decisión:

- `newMaximum`: la frecuencia supera el máximo anterior y reemplaza los candidatos.
- `tie`: la frecuencia iguala el máximo y añade ese valor a los candidatos.
- `belowMaximum`: el máximo y sus candidatos se conservan.

Después de esa decisión se añade a la salida la expansión de `outputTemplate`
para el grupo, seguida de un salto de línea. La salida de cada paso contiene
solamente los grupos que ya terminaron. `done` conserva el resultado final.
Un array vacío produce únicamente `empty`, sin accesos a elementos ni líneas
de salida.

Anterior y Siguiente recorren los estados calculados. Retroceder recupera
también las variables y la salida de ese paso; no deja visibles resultados
posteriores como si ya se hubieran producido. Editar los datos o cambiar de
escenario inicia un nuevo recorrido derivado de esa entrada. Reiniciar vuelve
al comienzo del recorrido actual.

El estado interactivo pertenece a la vista abierta. La primera versión no
guarda automáticamente datos editados ni el paso elegido como una respuesta,
un borrador de C++ o una acreditación curricular.

## Plantillas y límites

Las ocho explicaciones y `outputTemplate` permiten estas sustituciones:

| Sustitución | Valor calculado |
|---|---|
| `{value}` | Valor del grupo actual; vacío en `initial`, `done` y `empty`. |
| `{start}`, `{end}` | Inicio y límite exclusivo del grupo observado. |
| `{frequency}` | Cantidad observada en el grupo actual. |
| `{maximum}` | Máxima frecuencia de los grupos ya terminados. |
| `{leaders}` | Valores que alcanzan ese máximo, separados por coma y espacio. |
| `{size}` | Cantidad de elementos del array. |
| `{group}` | Número del grupo actual desde uno; cero antes del primero. |
| `{groups}` | Cantidad total de grupos de la copia ordenada. |

Las sustituciones se aplican una sola vez. Las llaves que formen parte de un
valor de texto se conservan como datos y no se vuelven a interpretar. No se
admiten nombres desconocidos, operaciones dentro de llaves, especificadores
de formato, llaves sin pareja ni escapes `{{` o `}}`. Título, descripción,
etiquetas y nombres de escenarios son texto literal.

Los límites se miden en bytes UTF-8, no en número de caracteres:

| Campo | Límite |
|---|---:|
| `id` opcional del bloque | 128 |
| `title` | 120 |
| `description` | 4000 |
| Cada etiqueta y nombre visible de escenario | 80 |
| Cada explicación | 2000 |
| `outputTemplate` | 256 |
| Cada valor de texto | 128 |

Los textos de presentación son obligatorios, no pueden contener NUL ni estar
compuestos sólo por espacios. Cada traza completa tiene un límite adicional
de 8 MiB: se suman la explicación y la salida acumulada de **cada** paso.
Se comprueba por separado el array inicial, cada escenario y las entradas
editadas. Así, una plantilla corta que repite muchas veces `{leaders}` no
puede producir una traza de tamaño descontrolado.

## Evaluación, referencias y conservación

`simulation` es una representación exploratoria. No declara respuestas,
oráculos ni criterios de acierto; completar sus pasos no acredita haber
escrito o comprendido un programa. Los puzzles y editores de la lección
conservan su propia evaluación y sus requisitos de avance.

El bloque puede formar parte de `referenceAfter` después de un editor con
objetivo declarado. En tal caso respeta la misma protección de acceso que
la explicación o el código de referencia que acompaña. No se convierte en
una actividad calificable mediante `activityAfter`.

Todo el bloque se conserva dentro de su contrato histórico, incluidos título,
descripción, valores, plantillas, escenarios y etiquetas. Esta versión no añade
una excepción para reescribir silenciosamente una simulación publicada. Las
adiciones, los cambios de posición y los retiros explícitos utilizan las
operaciones existentes de estado estable y conservan los bloques y las
presentaciones históricas pertinentes.

Para `textSemantics`, sólo son fuentes públicas estáticas `/title` y
`/description`. No se planifican como lectura de la página todas las plantillas
ni todos los estados futuros. La clasificación de esos textos estáticos sigue
siendo una decisión explícita de Work mediante el mecanismo existente.

## Comprobación de autoría

El validador habitual de Programación llama al mismo contrato desde los cursos
estructurales y desde la validación de estado. Se inspeccionan también los
bloques retirados y las presentaciones históricas que contienen simulaciones.

Desde la raíz del repositorio, las pruebas acotadas de autoría se ejecutan con:

```text
python -m unittest discover -s scripts/curriculum -p test_simulation.py
```

Estas pruebas verifican el esquema, los límites, los escenarios, las plantillas,
el presupuesto de expansión y la conservación de las identidades. Las pruebas
nativas del modelo y del lector Windows comprueban por separado el cálculo y
la integración. La validación JSON por sí sola no demuestra una interacción
visual ni una ejecución de C++.

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
      "end": "fin",
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
| `groups` | Examinar los grupos contiguos de valores iguales, también pulsando una de sus casillas. |
| `walkthrough` | Recorrer los pasos con índices, variables, explicación y salida. |
| `candidates` | Comparar las decisiones que conservan o cambian los candidatos, con selección directa del grupo. |
| `summary` | Consultar el resultado calculado del recorrido completo. |

Las agrupaciones, los índices y las frecuencias se calculan a partir de los
datos. El contenido no declara manualmente qué celdas pertenecen a cada grupo,
qué número debe mostrar una variable ni cuál debe ser la respuesta final.
Las etiquetas permiten presentar nombres adecuados a la explicación del curso;
no cambian el significado de las variables del modelo.

En **groups** y **candidates**, pulsar una casilla del array selecciona su grupo
calculado. En **walkthrough**, pulsar una casilla muestra el paso en el que
**se acaba de contar ese elemento**: `start` si es el primero del grupo y
`advance` si es uno de los siguientes. La decisión `newMaximum`, `tie` o
`belowMaximum` no se anticipa, porque se produce después de terminar el grupo.

El clic no requiere acertar un color ni una respuesta. Los arrastres, los clics
modificados y los espacios entre casillas no cambian la selección. El cálculo
utiliza los rectángulos reales, incluso con zoom y varias filas. Los botones de
grupos y el selector de fases mantienen la alternativa accesible por teclado.
En las vistas de resumen y array, las casillas no cambian el estado.

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

`valueType: "text"` utiliza entre cero y 64 cadenas UTF-8 de cero a 128 bytes
cada una. La cadena vacía `""` es un dato válido. Los espacios se conservan
exactamente: `""`, `" "` y `"  "` son tres valores diferentes. Un valor no
puede contener NUL, retorno de carro ni salto de línea. No se mezclan textos y
números dentro de una simulación o de sus escenarios.

Estas diferencias también se conservan al contar y comparar:

| Datos | Grupos de la copia ordenada | Máximo y candidatos finales |
|---|---|---|
| `[]` | Ninguno. | Máximo 0; ningún candidato. |
| `[""]` | La cadena vacía ocupa `[0, 1)`, con frecuencia 1. | Máximo 1; candidato `""`. |
| `["", "patio", "patio"]` | `""` ocupa `[0, 1)`; `"patio"`, `[1, 3)`. | Máximo 2; candidato `"patio"`. |
| `["", " "]` | Dos grupos distintos, cada uno con frecuencia 1. | Máximo 1; candidatos `""` y `" "`. |

Las mismas reglas se aplican a `values`, a todos los escenarios y a los datos
editados. Los textos de presentación, como títulos, etiquetas y explicaciones,
siguen siendo obligatorios y no pueden estar vacíos.

Para explorar palabras, adapta el ejemplo completo: cambia `valueType` a
`"text"`, usa `values: ["luna", "sol", "luna", "nube", "sol"]` y sustituye
también los valores numéricos de sus escenarios por textos o elimina esos
escenarios. En este caso, `luna` y `sol` empatan con frecuencia dos.

Con `sort: true`, el motor ordena una **copia** y conserva los datos de entrada.
Cuando la ordenación cambia el orden, la vista presenta los dos estados:
**entrada aplicada antes de ordenar**, con índices de origen, y **copia
ordenada**, con los índices utilizados durante el recorrido. No altera los
datos del estudiante ni confunde un borrador no aplicado con los datos
vigentes. La comparación se actualiza al elegir un escenario, aplicar datos
o restablecerlos; se oculta cuando no hay diferencia, incluso en arrays
vacíos. En listas largas, el panel tiene un límite de altura, desplazamiento
nativo y acceso por teclado. Los valores de texto conservan comillas y
aclaraciones para distinguir cadenas vacías y espacios.

En cada casilla de la **copia ordenada** aparece también su **índice de origen**
cuando la ordenación ha cambiado las posiciones. Por ejemplo, la entrada
`[7, 3, 7, 2]` produce la copia `[2, 3, 7, 7]`: los índices de origen de
sus casillas son, respectivamente, `3, 1, 0, 2`. Los dos valores `7` se
mantienen en el orden de sus apariciones originales, porque el ordenamiento
es estable. Este rótulo sirve para rastrear cada dato, no para cambiar los
índices `start` y `end` utilizados en la copia ordenada. Si los datos ya
están ordenados o `sort` es `false`, no se añade un rótulo redundante.

La ordenación numérica es ascendente. La ordenación de texto compara bytes
UTF-8; no aplica ordenación lingüística, conversión de mayúsculas ni eliminación
de acentos. La cadena vacía precede a cualquier cadena no vacía. Por ejemplo,
`""` precede a `" "`, `"Z"` precede a `"a"` y `"z"` precede a `"á"`.

Con `sort: false`, el array inicial, cada escenario y cualquier entrada editada
deben estar ya en ese orden ascendente. Los valores repetidos son válidos.
Este requisito garantiza que cada valor forme un único grupo contiguo y que
su frecuencia corresponda al total de apariciones en el array.

`editable: true` habilita el cambio de los datos. Los enteros se introducen
uno por línea: admiten signo y espacios exteriores; no admiten expresiones
como `2 + 3`, sufijos de C++ ni decimales en ese campo de entrada.

Para los datos de texto, el selector ofrece dos formatos explícitos:

| Formato | Entrada y significado |
|---|---|
| **Un texto por línea** | Conserva literalmente cada línea y sus espacios. Una entrada vacía representa `[]`. El salto final es opcional; una línea vacía intermedia sigue siendo inválida. Las comillas son parte del dato. |
| **Lista entre comillas** | Admite una lista JSON de cadenas: `[]` es una colección vacía; `[""]`, una cadena vacía; `[" "]`, una cadena con un espacio. `["", "patio", "patio"]` contiene tres datos. |

El formato se elige mediante el selector; el contenido del campo no se
interpreta automáticamente como otro formato. Por ejemplo, escribir `""`
en **Un texto por línea** sigue introduciendo dos caracteres de comilla.
En **Lista entre comillas** se conservan los espacios dentro de cada cadena;
los espacios y saltos entre los elementos sólo separan la notación. Se admiten
los escapes de cadenas JSON, como `\"` para una comilla, `\\` para una barra
inversa y `\t` para un tabulador. Tras decodificar, siguen vigentes los límites
de 64 valores y 128 bytes UTF-8 por valor y la prohibición de NUL, CR y LF.
Números, booleanos, objetos y listas anidadas no son valores de texto válidos.

Cambiar el selector convierte el borrador válido al otro formato sin aplicarlo
al recorrido. Si la conversión perdería un dato —por ejemplo, una cadena vacía
al pasar a líneas— o el borrador no es válido, se conserva el texto y el formato
anterior y se muestra el motivo. **Aplicar datos** inicia el recorrido con la
entrada revisada. Al abrir una simulación, seleccionar un escenario o
restablecer sus datos, la presencia de una cadena vacía elige **Lista entre
comillas** para que el dato pueda editarse sin ambigüedad.

Hay como máximo ocho escenarios. Cada uno declara exclusivamente `id`, `label`
y `values`. Su identificador es único, tiene de uno a 64 caracteres ASCII,
comienza por una letra o guion bajo y continúa con letras, dígitos, guiones
bajos o guiones. Sus valores cumplen las mismas reglas que el array inicial.
Seleccionar un escenario proporciona otra entrada al mismo modelo.

### Comparar los resultados de dos entradas

Cuando el estudiante selecciona un escenario o aplica datos diferentes a los
de `values`, el simulador puede comparar sus **resultados finales** con los
datos originales del bloque. El panel «Comparación de resultados» muestra
dos valores para cada propiedad, con la dirección
**datos iniciales → datos aplicados**: cantidad de elementos, cantidad de
grupos, frecuencia máxima y todos los candidatos. Si un resultado conserva
el mismo valor, muestra «sin cambio» en lugar de fingir una modificación.

Esta comparación utiliza exclusivamente el modelo ya calculado. El resultado
de referencia se conserva aunque se editen los datos, se elija un escenario o
se restablezca la entrada. Los borradores no aplicados o rechazados no
modifican la comparación; sus mensajes de error permanecen independientes.
Los textos, incluidas las cadenas vacías y los espacios significativos,
se muestran entre comillas, sin confundir «ninguno» con una cadena vacía.

Para evitar revelar resultados del recorrido, el panel sólo aparece al
alcanzar `done` en **walkthrough** o después de seleccionar el **último grupo**
en `groups` o `candidates`. Un array vacío es un estado final en sí mismo.
La vista `summary` presenta el resultado final directamente y puede mostrar
la comparación desde que se aplican los nuevos datos. La vista `array`
no presenta resultados finales ni activa esta comparación.

El panel desaparece al regresar a un paso anterior, así como al restablecer
exactamente los valores originales. Su texto sólo participa en la narración
cuando es visible y tiene una identidad de lectura asociada a los datos
vigentes, no a los estados futuros. No califica la exploración, no cambia
la entrada ni añade propiedades nuevas al JSON curricular.

### Presentación de los textos

Las celdas, los grupos y los candidatos muestran los textos entre comillas,
con escapes JSON cuando son necesarios. La cadena vacía aparece como
`"" (cadena vacía)`; un texto formado sólo por espacios ASCII aparece, por
ejemplo, como `" " (1 espacio)` o `"   " (3 espacios)`. Las comillas, los escapes
y esas aclaraciones permiten reconocer el dato, pero no se añaden a su valor.
El modelo sigue agrupando, ordenando y contando las cadenas originales.

Las explicaciones usan esa misma presentación en `{value}` y `{leaders}`.
La salida calculada conserva los valores crudos: `outputTemplate` no añade
comillas, escapes ni las aclaraciones de vacío o espacios. Por ejemplo, con
la plantilla `[{value}]={frequency}`, el grupo de `""` con frecuencia uno
produce `[]=1`, mientras que el grupo de `" "` produce `[ ]=1`.

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

### Qué cambió en el paso

La vista nativa de recorrido muestra **Cambios desde el estado anterior**
calculados a partir de dos estados consecutivos de la traza. Se enumeran
únicamente las variables que cambiaron: grupo, inicio, límite exclusivo,
frecuencia, máximo y candidatos, usando las etiquetas de la lección para
los campos que pueden personalizarse. Si aparece nueva salida, el panel
indica cuántas líneas se añadieron, sin repetir ni adelantar la salida
futura. Los valores de texto en los candidatos conservan comillas y las
aclaraciones de vacío o espacios.

En las fases `start` y `advance`, el panel identifica el **dato contado**
mediante su índice `end - 1` y el valor calculado de la copia ordenada.
No presenta `end` como un acceso válido, ni afirma que la decisión final
de un grupo haya leído otro elemento. Al terminar cada grupo, el panel
también muestra la comparación efectiva entre su frecuencia y el
**máximo anterior a la decisión**: `>` reemplaza candidatos, `=` agrega
el valor empatado y `<` los conserva. Por ejemplo, `4 > 1` explica
por qué se actualizó el máximo a 4. Esta comparación procede de los
estados calculados, no de los mensajes explicativos redactados por Work.
El caso de array vacío y las vistas estáticas no inventan observaciones.

Al saltar directamente del paso 1 al 9, el panel compara **el paso 8 con
el 9**, no el último paso que el estudiante visitó. En `groups` y
`candidates`, contrasta los grupos terminados consecutivos; el primer
grupo se compara con el estado inicial. La vista `array`, el resumen,
el primer paso del recorrido y los escenarios vacíos no inventan un
cambio anterior.

El panel es una lectura derivada y no calificable, accesible al lector
de Windows sólo en el estado actual. Si el texto ocupa demasiadas
líneas, mantiene desplazamiento nativo y acceso por teclado. No añade
campos al JSON ni modifica respuestas, logros o datos de entrada.

Anterior y Siguiente recorren los estados calculados. El selector **Ir a un paso**
permite saltar directamente a una fase del recorrido, por ejemplo al primer grupo,
a un empate o al resultado final.

En **walkthrough**, los botones **Decisión anterior** y **Siguiente decisión**
permiten visitar sólo las fases en las que termina un grupo y se compara su
frecuencia con el máximo. Se omiten temporalmente los pasos de recuento para
encontrar rápidamente comparaciones, pero siguen disponibles con Anterior,
Siguiente, pulsando una casilla y mediante el selector de fases. Por ejemplo,
los tres grupos de `[10, 3, -8, 3, 3, 3]` producen decisiones en las
posiciones internas de traza `2`, `7` y `9` (pasos visibles 3, 8 y 10).

Cada botón encuentra la decisión estrictamente anterior o posterior al
**paso actual del modelo**, no a la última decisión visitada. Los saltos
conservan exactamente la salida y los candidatos calculados de ese paso;
no acreditan ejercicios ni suponen que el estudiante haya ejecutado los
pasos intermedios. Los destinos se vuelven a calcular al aplicar nuevos
datos, seleccionar un escenario o restablecerlos. Si no hay grupos, los
botones desaparecen. En las vistas de grupos, candidatos, array y resumen,
se mantienen los controles de navegación correspondientes sin duplicarlos.

En la vista **candidates**, el selector muestra
los grupos completados. Funciona también con el teclado y contiene sólo estados
calculados para los datos actuales. Al editar, aplicar o restablecer datos, sus
entradas se recalculan; no conserva posiciones ni resultados de otro escenario.
Retroceder o elegir un paso anterior recupera las variables y la salida de ese
paso; no deja visibles resultados
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
| `{value}` | Valor del grupo actual: presentación legible en explicaciones y dato crudo en `outputTemplate`; sin contenido en `initial`, `done` y `empty`. |
| `{start}`, `{end}` | Inicio y límite exclusivo del grupo observado. |
| `{frequency}` | Cantidad observada en el grupo actual. |
| `{maximum}` | Máxima frecuencia de los grupos ya terminados. |
| `{leaders}` | Valores que alcanzan ese máximo, separados por coma y espacio; presentación legible en explicaciones y datos crudos en `outputTemplate`. |
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

Desde el checkout `LearnGameAndEngineDev-Codex` en Windows, la compilación
y comprobación enfocadas de todo el contrato de simulación se realizan en
PowerShell con:

```powershell
& '.\scripts\windows\Test-Simulation.ps1' -Preset windows-x64-debug
```

El comando usa las herramientas locales autorizadas y el objetivo CMake
`lgaed_simulation_qa_targets`, sin construir la aplicación completa. Exige
que CTest encuentre exactamente cuatro pruebas:
`native.simulation_model`, `native.simulation_view`,
`native.simulation_lesson` y `native.simulation_authoring`. Después las
ejecuta de forma secuencial. Las ventanas de la prueba nativa permanecen
ocultas. Los registros se conservan en `out/build/<preset>/logs/` y las
capturas de regresión en el directorio temporal de CTest para simulaciones.
No modifica perfiles reales ni ejecuta GitHub Actions. No se debe atribuir
una aprobación Windows a la mera publicación del script.

Desde la raíz del repositorio, las pruebas acotadas de autoría se ejecutan con:

```text
python -m unittest discover -s scripts/curriculum -p test_simulation.py
```

Estas pruebas verifican el esquema, los límites, los escenarios, las plantillas,
el presupuesto de expansión (presentación legible y salida cruda), las cadenas
vacías y con espacios, y la conservación de las identidades. Las pruebas
nativas del modelo y del lector Windows comprueban por separado el cálculo y
la integración. La validación JSON por sí sola no demuestra una interacción
visual ni una ejecución de C++.

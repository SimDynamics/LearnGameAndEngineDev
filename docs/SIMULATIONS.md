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
vacío. Para etiquetas de texto, el archivo
[array_runs_empty_text_simulation.json](examples/array_runs_empty_text_simulation.json)
incluye seis escenarios que distinguen `[]`, `[""]` y `["", " "]`,
además de empates y valores repetidos. Ambos ejemplos son contratos de
autoría; Work adapta el contenido y decide dónde integrarlo en la lección.
Se incorpora como un bloque nuevo mediante las adiciones del contrato
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
`advance` si es uno de los siguientes. La casilla recién contada queda
distinguida del resto del grupo mediante un borde más grueso y el rótulo
visible **«contado»**. La descripción accesible añade su índice y el valor
efectivamente observado, sin confundir los índices de la copia ordenada con
los del origen. Este rótulo dispone de espacio propio incluso cuando hay
varias filas, etiquetas largas o zoom de 150 %.

En `initial`, `newMaximum`, `tie`, `belowMaximum`, `done` y `empty`,
la vista no marca ninguna casilla como recién contada. Una decisión compara
la frecuencia que ya se obtuvo, **no vuelve a leer** el último elemento ni
anticipa el resultado de otro grupo. Volver atrás, elegir un escenario o
aplicar datos nuevos vuelve a calcular el marcado a partir del paso actual;
el marcado no cambia el algoritmo, la salida ni el progreso.

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
puede contener NUL, retorno de carro (CR) ni salto de línea LF. No se mezclan textos y
números dentro de una simulación o de sus escenarios.

Los caracteres Unicode que pueden resultar invisibles o interpretarse como
saltos de línea visuales, como U+00A0 (espacio no separable), U+200B (ancho
cero), U+2028 (separador de línea) y U+2029 (separador de párrafo), aparecen
como `\u00a0`, `\u200b`, `\u2028` y `\u2029` en las
etiquetas, la narración y el editor **Lista entre comillas**. Es una escritura
JSON reversible: el dato real, su ordenación UTF-8, la frecuencia, los
candidatos y la salida cruda calculada por `outputTemplate` no cambian.
La cadena vacía sigue mostrando `(cadena vacía)` y los espacios ASCII
mantienen su cuenta explícita. Los rellenos Hangul U+115F, U+1160,
U+3164 y U+FFA0, y el espacio Braille U+2800, también se presentan con
escapes JSON visibles, por ejemplo `\u115f` y `\u2800`. Son valores
distintos de la cadena vacía y de un espacio ASCII. El validador Python
cuenta la representación escapada de esos cinco caracteres al comprobar
el límite de 8 MiB de las explicaciones, igual que el motor C++.
Los caracteres de formato que no ocupan una celda reconocible también se
representan con escapes: vocales inherentes del jemer U+17B4–U+17B5,
selectores y separador mongoles U+180B–U+180F, controles de taquigrafía
U+1BCA0–U+1BCA3 y controles de notación musical U+1D173–U+1D17A.
Los controles suplementarios usan dos sustitutos JSON; por ejemplo, U+1BCA0
se representa con la secuencia \ud82f\udca0. También aquí los datos y salidas
reales conservan sus bytes originales y se cuenta la expansión visual en el
presupuesto de la traza. Los caracteres Unicode de etiqueta, U+E0001 y
U+E0020–U+E007F, también aparecen como escapes reversibles; U+E0020 se
representa como \udb40\udc20. Esas etiquetas pueden ser invisibles aunque
formen parte de un texto visible, y no se eliminan ni se normalizan.
Los selectores de variante Unicode, como
U+FE0F, también se hacen visibles: `\ufe0f`. Los selectores
suplementarios U+E0100–U+E01EF se escriben con la pareja de sustitutos
JSON correspondiente (por ejemplo `\udb40\udd00` para U+E0100), y
el lector recupera exactamente los cuatro bytes UTF-8 originales.
No se cambia la presentación de letras normales ni de emojis ordinarios.

Estas diferencias también se conservan al contar y comparar:

| Datos | Grupos de la copia ordenada | Máximo y candidatos finales |
|---|---|---|
| `[]` | Ninguno. | Máximo 0; ningún candidato. |
| `[""]` | La cadena vacía ocupa `[0, 1)`, con frecuencia 1. | Máximo 1; candidato `""`. |
| `["", "patio", "patio"]` | `""` ocupa `[0, 1)`; `"patio"`, `[1, 3)`. | Máximo 2; candidato `"patio"`. |
| `["", " "]` | Dos grupos distintos, cada uno con frecuencia 1. | Máximo 1; candidatos `""` y `" "`. |

Las mismas reglas se aplican a `values`, a todos los escenarios y a los datos
editados. Los textos de presentación, como títulos, etiquetas y explicaciones,
siguen siendo obligatorios y no pueden estar vacíos ni estar compuestos
únicamente por espacios o caracteres de formato invisibles. Es válido
conservar esos caracteres junto a texto realmente visible. Esta restricción
sólo se aplica a la presentación: no elimina ni sustituye las cadenas
vacías, los espacios o los caracteres invisibles usados como datos.

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

El panel **entrada aplicada antes de ordenar** es una lectura independiente de
la salida calculada. Al avanzar o retroceder en el mismo recorrido, no se vuelve
a escribir su texto: conserva la selección y el punto donde el estudiante estaba
leyendo. También conserva su ubicación cuando cambia el zoom, el DPI o el ancho
disponible, incluyendo un intento de zoom rechazado. Al elegir otro escenario,
aplicar datos o restablecerlos comienza **una sesión de lectura nueva**, incluso
cuando se aplican exactamente los mismos valores y el texto de origen coincide:
la selección y el desplazamiento se reinician. Un borrador inválido no inicia
otra sesión, no sustituye la entrada aplicada ni pierde la lectura anterior.
La regresión Win32 `original_order_reading_session` comprueba
estos límites con una lista de 64 valores; requiere ejecución local en Windows.

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
anterior y se muestra el motivo. El cambio de formato también es transaccional:
se comprueba que RichEdit haya escrito el contenido convertido y que la vista
acepte su presentación antes de confirmarlo. Las notificaciones a observadores
se realizan después: si el formato ya se convirtió y un observador falla, se
conservan los datos, la selección de formato y cualquier paso anidado válido.
Si la entrada era inválida, su diagnóstico conserva prioridad aunque también
falle la notificación. La regresión Win32
`input_format_callback_failure_keeps_converted_draft` cubre ambos casos.
Si RichEdit también rechaza el segundo intento de presentar el aviso después
de recuperar el formato anterior, se conserva el diagnóstico original mediante
el control Win32, incluido el nombre accesible y el texto de narración. El
borrador y la selección anterior deben seguir disponibles, y la siguiente
conversión válida puede repetirse sin alterar el recorrido aplicado. La
regresión Windows `input_format_rejection_survives_repeated_richedit_failure`
cubre los dos rechazos nativos consecutivos; requiere ejecución local.
Un rechazo silencioso de escritura
o un fallo tardío de presentación restablecen formato, borrador, selección,
posición de lectura y ayuda anteriores sin modificar la traza aplicada.
Si incluso la restauración nativa falla, el motor no afirma que el borrador
haya quedado intacto: muestra un aviso accesible para que se revise el texto
visible y se vuelva a introducir lo necesario antes de aplicarlo.

**Añadir cadena vacía** está disponible únicamente en las simulaciones de
texto editables. Convierte primero un borrador válido a **Lista entre comillas**
y añade un elemento `""` inequívoco; cuando `sort: false` exige datos
ordenados, inserta la cadena vacía al principio. Si la entrada era inválida,
ya contenía 64 valores o el recorrido resultante supera su límite, conserva
el borrador, el formato, el recorrido y muestra el diagnóstico. La inserción
no aplica datos automáticamente, no altera un escenario ni consume un
intento: se confirma después con el botón habitual. La prueba portátil
`explicit_blank_text_button_preserves_draft_data` verifica el contrato;
la prueba Win32 `explicit_blank_label_button_prepares_but_does_not_apply`
comprueba el botón y su interacción con RichEdit.

**Aplicar datos** inicia el recorrido con la entrada revisada. Al abrir una
simulación, seleccionar un escenario o restablecer sus datos, el editor elige
**Lista entre comillas** cuando existe una cadena vacía, una cadena formada
únicamente por espacios ASCII, o algún texto necesita escapes para mostrarse
inequívocamente (por ejemplo U+200B, U+2028, U+2800, tabuladores, comillas
o barras inversas). Los textos ordinarios siguen comenzando en
**Un texto por línea**. Esta selección sólo determina la presentación del
borrador nuevo: la entrada, la salida, los datos originales y sus frecuencias
no cambian. El estudiante puede escoger manualmente el otro formato; una
conversión sigue validando íntegramente el contenido antes de aceptarla.

La integración se comprueba también con el ejecutable real de la vista de
simulación Win32 en un árbol QA independiente. El objetivo
`native.simulation_view_full` ejecuta toda la batería existente de navegación,
escenarios, lecturas accesibles, zoom, manejo de entradas inválidas y
recuperación nativa, mientras `native.simulation_blank_button` verifica el
camino abreviado de los valores vacíos. Ambos usan los mismos controles y
modelo de producción. El ejecutor aislado no reemplaza la compilación completa
de la app con las versiones de herramientas fijadas para el PC del usuario.

La vista Windows conserva una **sesión de modelo validado** independiente de
las sesiones de lectura. Al abrir el bloque valida una vez su contrato
completo, sus datos iniciales y todos los escenarios, y transfiere la traza
inicial ya calculada, sin duplicar esa memoria. Al elegir un escenario,
restablecer o aplicar datos, sólo valida y calcula la entrada seleccionada:
no reconstruye otra vez las ocho posibles trazas de escenarios que no han
cambiado. La conversión de formatos valida el borrador y su traza completa
antes de permitir el cambio, incluido el límite de 8 MiB. Una entrada
inválida, un texto mal formado o una ordenación incorrecta con
`sort: false` siguen rechazándose y no sustituyen el modelo aplicado.
El contrato de simulación se conserva como una copia privada, de modo que
ningún cambio externo a la especificación modifica la sesión abierta. Las
funciones de validación independientes siguen comprobando todo el contrato
para quienes no utilizan esa sesión. La prueba `validated_simulation_session`
compara ambos caminos con los mismos datos, límites y rechazos.
La regresión `exhaustive_cached_session_replay` añade 705 entradas
calculadas: 364 arrays de enteros de hasta cinco elementos y 341 arrays
de textos de hasta cuatro. Contrasta grupo, índices originales, fases,
frecuencias, máximos, explicaciones, salida y comportamiento del editor
frente a las funciones estrictas; comprueba asimismo que rechazar un
array desordenado no dañe el siguiente intento válido. No implica
que 705 simulaciones se hayan ejecutado en la app ni constituye una medición
de rapidez. Ambas pruebas requieren ejecución nativa Windows antes de
acreditar sus resultados.

**Aplicar datos** y cambiar de escenario son operaciones de reemplazo
completo, no cambios parciales de la traza. La notificación a observadores
ocurre después de confirmar el nuevo modelo y la vista Win32. Si un
observador falla o navega de forma anidada antes de lanzar una excepción,
el simulador no deshace datos ya aplicados ni el último paso confirmado.
El aviso accesible identifica un fallo de notificación sin afirmar que
**Aplicar datos** haya sido rechazado. Si durante esa notificación se genera
otro diagnóstico, por ejemplo al intentar aplicar un borrador inválido,
el aviso del observador exterior no lo sustituye. Incluso cuando
**Aplicar datos** rechaza directamente un borrador, una excepción del
observador que recibe el diagnóstico no debe ocultar la validación, cambiar
la traza anterior ni impedir corregir la entrada. La regresión Win32
`rejected_apply_observer_cannot_hide_input_diagnostic` cubre este caso.
Si el diagnóstico surge por datos inválidos y RichEdit rechaza mostrarlo,
se conserva la causa inicial (por ejemplo, un entero fuera de intervalo)
mediante un aviso Win32 directo. El texto visible y la narración deben
coincidir; el fallo de presentación sólo se añade como contexto y no
convierte el rechazo en una ejecución válida. La regresión Windows
`rejected_apply_preserves_diagnostic_when_richedit_rejects_feedback`
reproduce ambos errores seguidos y comprueba el reintento con datos corregidos. También se conserva un
diagnóstico previo del editor al navegar por la simulación. Las regresiones
`reentrant_invalid_input_outweighs_observer_failure`,
`applied_data_survive_observer_exception` y
`scenario_observer_reentry_keeps_committed_data` requieren ejecución Windows.

Si el modelo calcula una entrada válida, pero un control Win32 rechaza su presentación posterior, se recuperan
los datos anteriormente aplicados, el paso, el selector, las barras, la salida
y la posición de lectura. El borrador enviado se conserva para corregirlo
o reintentar, y no se informa de que los datos quedaron aplicados. Si el fallo
proviene de la presentación Win32, el aviso visible también actualiza su
nombre accesible y la región de narración, incluso si el formateador RichEdit
continúa rechazando la operación. Cuando se reintenta con éxito, desaparecen
el mensaje y la anotación accesible del rechazo anterior. La regresión
`applying_rejection_survives_repeated_native_presentation_failure` inyecta tres
rechazos consecutivos al aplicar datos: comprueba que no se sustituya el
modelo ya aplicado, que el borrador siga visible, que el error original
permanezca disponible para narración y que el segundo intento correcto
no vuelva a anunciarlo. Requiere ejecución real en Windows.
Si la escritura del editor también falla durante la restauración, se intenta
reponer literalmente el texto previo por `EM_REPLACESEL` sobre todo el
documento, sin volver a utilizar `EM_STREAMIN`; si el control rechaza también
esa escritura, se intenta `WM_SETTEXT` y se coteja la lectura resultante.
Los textos que parezcan encabezados RTF siguen siendo datos literales.
También se coteja el texto tras un intento de restauración: si un segundo
`EM_STREAMIN` silencioso deja los datos nuevos donde debían estar los
anteriores, se activa la recuperación literal, igual que ante un error
nativo explícito. Si el texto anterior ya estaba intacto, no se vuelve a
escribir: se conservan también las operaciones de Deshacer/Rehacer del
borrador y su selección de lectura.
Antes de aceptar un escenario se compara el contenido real del editor con el
texto previsto: un flujo que termina sin error, pero no escribe esos datos,
también se rechaza. Ante un rollback, se restauran además la selección y la
posición de lectura del borrador en el editor. La vista debe recuperar el
modelo aplicado anterior y el error original debe seguir visible y disponible
para narración. Si todas las vías de restauración del borrador fallan al cambiar
de escenario, el aviso conserva la causa original y agrega una advertencia
para revisar el texto visible. No declara conservado un borrador sin verificarlo.
Se restaura la traza por movimiento, sin duplicar una segunda copia de
hasta 8 MiB. El rechazo de un escenario identifica directamente
«No se pudo cambiar el escenario» y conserva la causa original mediante
el aviso accesible, aunque fallen consecutivamente la nueva presentación,
la recuperación del panel anterior y el segundo intento de dibujarlo. No
se sustituye la causa por «No se pudo actualizar la simulación». Si el
observador de esa recuperación navega de nuevo, la navegación más reciente
prevalece sobre el aviso de un intento anterior; tampoco se accede a una
vista destruida por el observador. Las regresiones
`scenario_rejection_survives_repeated_native_presentation_failure` y
`rejected_scenario_notification_keeps_newer_navigation` comprueban estas
condiciones. Las regresiones
`applying_valid_data_is_atomic_after_native_failure` y
`scenario_change_is_atomic_after_native_failure` provocan una única
falla tardía real de RichEdit y comprueban que el intento siguiente pueda
aplicarse normalmente. Todas requieren ejecución local en Windows.

El lector conserva una identidad de narración distinta para la ayuda del
formato **Lista entre comillas**, sin perder la identidad histórica del
formato por líneas. Así, cambiar el selector no reutiliza un audio de ayuda
incompatible. Los mensajes de error de intentos diferentes en el mismo paso
también tienen identidades separadas cuando cambia el diagnóstico visible:
se calcula una clave opaca y estable a partir del **mensaje**, sin incluir
el borrador introducido. Repetir el mismo error conserva su identidad;
corregir los datos lo retira del recorrido de lectura. Si el diagnóstico
permanece igual mientras se avanza o retrocede por el recorrido aplicado,
su documento RichEdit y su selección se conservan, sin retransmitir el
mismo error en cada paso. Un mensaje diferente se presenta normalmente.
Si el texto se mostró mediante recuperación Win32 directa tras un fallo
de RichEdit, el siguiente redibujado válido recupera el formato normal,
en vez de reutilizar una marca visual anterior. La regresión Win32
`repeated_invalid_draft_diagnostic_preserves_reading` cubre la navegación
con error repetido, su sustitución, el zoom y la recuperación tras un fallo
nativo; requiere ejecución local en Windows. El panel de
ayuda mantiene también su selección y documento RichEdit durante pasos,
errores y aplicación de otros datos si el formato sigue siendo el mismo;
no retransmite un texto idéntico. Un cambio real entre líneas y lista
entre comillas sí actualiza la ayuda; zoom, DPI o apariencia actualizan su
formato visual sin atribuirle una escucha anterior. La regresión nativa
`interactive_simulation_feedback_narration_ids` comprueba estas rutas;
su ejecución debe acreditarse en Windows.

En el editor nativo de datos, **Tab** pasa al siguiente control y
**Mayús+Tab** vuelve al anterior; no se añade un tabulador al borrador por
navegar. **Ctrl+rueda** ajusta sólo el zoom de ese campo en pasos de
10 %, dentro de 50 % a 150 %. Las ruedas de alta resolución acumulan sus
fracciones hasta una muesca completa. La rueda sin Ctrl conserva el
desplazamiento ordinario, reinicia esa acumulación y no cambia el zoom.
Un cambio confirmado de escala general del lector o de DPI también
descarta una fracción pendiente del editor: la siguiente muesca empieza
una sesión nueva y no provoca un incremento inesperado. Al mover la ventana
a otro monitor, el rebase de fuentes para su DPI **conserva el zoom local**
del editor de datos; el texto del lector sigue usando su escala global.
En cambio, cuando el estudiante cambia explícitamente la escala general
del lector, ambos controles adoptan ese nuevo porcentaje. Esta diferencia
está protegida por una prueba nativa sin cambiar físicamente de monitor.
Estos gestos no aplican datos ni alteran las variables, el recorrido o las
escalas de los demás paneles. RichEdit acepta el zoom mediante una fracción
cuyo numerador y denominador son como máximo 64. El simulador reduce las
escalas exactas antes de comunicarlas al control (por ejemplo, 150 % es
`3/2`, no `150/100`) y aproxima a la fracción permitida más cercana los
porcentajes sin representación exacta. Comprobar el valor aceptado evita que
la escala visual y la altura calculada diverjan silenciosamente. La escala
del simulador se aplica a todos los paneles de forma transaccional: si uno
rechaza el cambio, se restauran las fracciones anteriores de cada RichEdit,
incluido el zoom local del editor de datos, y se descartan las mediciones
intermedias de altura. La prueba nativa fuerza ese rechazo en un panel
tardío y comprueba que otro simulador y los datos del estudiante no cambien.
La transacción también cubre el fallo **posterior** a la confirmación de
las fuentes: si un RichEdit rechaza el nuevo texto o su escala mientras
la vista se actualiza, se recuperan el porcentaje de lectura anterior,
la fracción nativa independiente del editor y el recorrido ya calculado.
El aviso conserva la causa original del rechazo. La regresión Windows
`late_reader_zoom_update_failure_restores_native_scale` simula un fallo
en la segunda solicitud de zoom, después de que la primera fue aceptada,
y comprueba la selección, el foco, los textos y la posibilidad de volver
a ampliar normalmente. Las comprobaciones de foco y rueda son parte de
`native.simulation_view` y requieren ejecución nativa Windows.

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

### Progreso de elementos recorridos

En la vista **walkthrough**, un control de progreso nativo de Windows
representa cuántas **posiciones del array ordenado** se han recorrido:
`end` de `size`. La barra empieza en cero, avanza únicamente cuando
`start` o `advance` cuenta un elemento nuevo, y retrocede si se
elige un estado anterior. La fase de decisión mantiene ese progreso
porque comparar frecuencias no supone leer otro dato.

Esta cantidad **no es** la frecuencia del grupo actual. Por ejemplo,
si hay seis elementos, al contar la sexta casilla el progreso muestra
**6 de 6**, pero la decisión final del último grupo todavía puede estar
pendiente. Cuando llega `done`, el control conserva 6 de 6 sin fingir
una séptima lectura. El progreso comunica el límite exclusivo ya
alcanzado, no un acceso a `datos[6]`.

El indicador presenta también el recuento mediante texto y una identidad
de narración asociada al paso; no exige distinguir colores ni admite
edición por parte del estudiante. El total y el avance se recalculan
al cambiar de escenario, aplicar datos o restablecerlos. La entrada
inválida conserva el progreso del último recorrido correcto. En un
array vacío y en las otras cuatro vistas no se muestra una barra
que sugiera pasos inexistentes.

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

Dos pasos pueden compartir exactamente la misma explicación calculada aunque
hayan cambiado el límite, la frecuencia o la fase. Si la explicación y su
formato siguen iguales, la vista conserva el documento RichEdit, su selección
y el punto donde se estaba leyendo, sin retransmitir el mismo texto al pasar
de `start` a `advance`. Una explicación diferente sí actualiza el documento.
Los cambios reales de zoom, DPI y apariencia invalidan la presentación
anterior sin atribuir esa lectura al contenido nuevo. Aunque sea necesario
volver a formatear el documento, una explicación larga conserva la selección
y la región del texto que estaba leyendo el estudiante al cambiar la escala,
el DPI o el ancho. La misma recuperación de lectura se aplica a los paneles
desplazables de transición y comparación, sin trasladar esa posición a un
nuevo paso o conjunto de datos. La regresión Win32
`repeated_step_explanation_preserves_native_reading` comprueba la
continuidad de lectura, la actualización de un texto distinto y el redibujado
conservando el contexto tras zoom, DPI y reflujo; requiere ejecución local
en Windows.

Avanzar, retroceder o saltar a un paso tampoco admite una presentación
parcial. Si RichEdit rechaza la actualización después de seleccionar otro
estado, el simulador recupera el paso, la salida, las variables, las barras
y los puntos de lectura anteriores. La escritura del texto enriquecido se
coteja con el contenido que realmente conserva RichEdit, tras las
transformaciones de presentación, antes de aplicar su formato o dar por
confirmado el nuevo paso. Un `EM_STREAMIN` que termine sin error, pero
conserve la explicación anterior, también provoca el rechazo y permite
reintentar. El sello de formato sólo se conserva tras una actualización
completa. La regresión Windows
`silent_richedit_explanation_stream_cannot_commit_step` comprueba la
recuperación y el reintento; requiere ejecutarse localmente en Windows.
También se comprueba que la salida calculada realmente se haya escrito en
el RichEdit de consola. Si `EM_STREAMIN` deja silenciosamente la salida
anterior, no se confirma el nuevo paso. La recuperación no retransmite el
documento anterior cuando éste ya estaba intacto, para conservar su
selección y punto de lectura. El siguiente intento puede actualizar la
salida correctamente. Esta comprobación sólo normaliza los finales de línea
`CR` o `CRLF` a `LF` al comparar con la lectura nativa: no modifica
los bytes de la salida calculada ni su contrato curricular. Las regresiones
Windows `silent_richedit_stdout_stream_cannot_commit_step` y
`calculated_stdout_accepts_crlf_template` verifican el rechazo, el
reintento y las plantillas con `CRLF`; requieren ejecución local.
Un aviso accesible explica el fallo,
sin propagar la excepción fuera de los controles Win32. La limpieza de un
aviso transitorio ocurre antes de notificar a los observadores del modelo:
si un observador inicia otro paso que falla, la notificación exterior no
puede borrar el error del intento interior. La regresión Win32
`reentrant_navigation_keeps_newer_native_error` comprueba esa reentrada.
La notificación de cambio se realiza fuera de la transacción del paso:
si un observador externo lanza una excepción, el paso ya calculado no
retrocede. El aviso accesible distingue el fallo de notificación de un
error de presentación, y no sustituye un error posterior procedente de
una navegación anidada. Las pruebas
`navigation_callback_failure_keeps_committed_steps` y
`reentrant_success_survives_outer_observer_exception` cubren ambos casos.
La regresión `navigation_retry_preserves_previous_invalid_draft` verifica
además que un fallo temporal de navegación no borre el diagnóstico anterior
de datos editados inválidos cuando el siguiente paso sí se complete.
El intento siguiente puede avanzar normalmente y retira ese aviso transitorio; si había un
diagnóstico distinto del editor, vuelve a mostrarlo. La regresión nativa
`step_navigation_rolls_back_native_presentation_failure` verifica esos
efectos sin modificar el conjunto de datos aplicado. Requiere Windows.

Cuando la salida de muchos grupos necesita una barra de desplazamiento,
contar otro elemento sin terminar su grupo **no reemplaza el texto ni
reinicia la posición de lectura**. Si se añade una nueva línea y el
estudiante estaba leyendo el final, la salida se desplaza hasta mostrarla.
Si había vuelto manualmente a una línea anterior para compararla, el
simulador conserva esa posición aunque lleguen nuevas líneas y mantiene
su selección de texto dentro de la misma traza. Cambiar el zoom o
actualizar colores tampoco debe devolverlo involuntariamente al principio.
Al cambiar la escala de lectura, el DPI de la ventana o el ancho disponible,
el simulador toma como referencia el **carácter de la primera línea visible**
y recupera esa misma región después del reflujo, sin depender de los píxeles
anteriores. Como RichEdit puede quedar una línea más abajo al aplicar el
nuevo zoom por redondeo interno, la recuperación comprueba la línea realmente
visible y corrige un desfase restante de forma acotada. Esta regla también
protege las explicaciones largas. Conserva asimismo la selección. Si ya
se estaba leyendo el final,
la salida continúa mostrando el final tras el cambio. La regresión Win32
`output_zoom_preserves_reading_anchor` cubre zoom, rebase de DPI, cambio de
anchura, desplazamiento al final y rechazo tardío de una escala por RichEdit.
En ese rechazo debe recuperarse también la lectura anterior y permitir un nuevo
zoom válido, sin conservar una escala parcial. Su ejecución debe realizarse en
Windows.
En cambio, seleccionar otro escenario o aplicar datos diferentes inicia
una sesión de lectura independiente: la selección y el desplazamiento
de la salida vuelven al inicio, **aunque los dos conjuntos de datos
produzcan exactamente el mismo texto de salida**. No se reutiliza una
posición de lectura perteneciente a los valores anteriores.
Las regresiones nativas `accumulated_output_scroll_follows_new_groups`
y `output_scenario_reading_isolated` comprueban estas condiciones
sin cambiar el contrato de salida ni los datos originales.

### Barras de frecuencia calculadas

La vista Windows muestra un diagrama de barras a partir de las **frecuencias
realmente calculadas por el modelo**, no de números escritos por Work. Cada
barra identifica el grupo y su valor; su altura representa cuántos elementos
contiene. El rótulo de cantidad es numérico, así que no depende únicamente
del color ni del tamaño. Los candidatos al máximo **actual** aparecen
destacados también mediante una estrella. El lector puede acceder al
nombre completo de cada grupo, su valor y frecuencia, incluso cuando un
texto largo se acorta visualmente dentro de una tarjeta.

La tarjeta correspondiente al **grupo cuya decisión se examina ahora**
se distingue con un borde reforzado y la palabra **«Actual»** en su
cabecera. La descripción accesible lo expresa como «grupo de la decisión
actual». Esta marca es distinta de la estrella de **candidato al máximo**:
una decisión `belowMaximum` puede tener como grupo actual uno que perdió
la comparación y no es candidato. En un empate, ambos grupos pueden
mantener la estrella, pero sólo el grupo que se compara en ese paso
recibe la marca «Actual».

La marca aparece únicamente al examinar una decisión de un grupo terminado
en `walkthrough`, o el grupo seleccionado en `candidates`. En los pasos
de recuento `start` y `advance`, en `done` y en `summary` no se inventa
ningún grupo «Actual». Si está activo el filtro **Sólo candidatos** y el
grupo actual no es candidato, su barra queda fuera de ese filtro;
la vista conserva las variables y explicaciones calculadas del paso.

En **walkthrough**, una barra sólo aparece **después de que termina la
decisión de ese grupo**: contar el primer dato o avanzar dentro del grupo
no muestra todavía su frecuencia total. En **candidates** se muestran los
grupos que ya se han examinado; **summary** muestra el resultado de todos
los grupos. La vista de selección **groups** conserva sus propios botones
y la vista **array** sólo presenta las casillas, sin barras duplicadas.

En **walkthrough** y **candidates**, pulsar una tarjeta visible del
histograma permite volver a la decisión ya calculada de ese grupo. La
selección corresponde siempre al **índice real del grupo**, aunque esté
desplazado en el gráfico o se active **Mostrar solo candidatos**.
Al elegir una tarjeta, se recuperan los valores, la explicación, la
salida y el paso exacto de esa decisión; no se vuelven a ejecutar
instrucciones ni se acredita el ejercicio.

Nunca se puede pulsar para llegar a un grupo todavía no terminado.
El clic requiere pulsar y soltar sobre la misma tarjeta, sin arrastrar
ni usar teclas modificadoras; tampoco se navega por doble clic.
Los gestos que delega el simulador al lector de la lección se protegen
en todos sus controles Win32: si falla el observador de un gesto, no se
acepta por accidente el clic de la celda o la barra y la excepción no
escapa del procedimiento nativo. El aviso es accesible y no sustituye
un diagnóstico de datos inválidos que ya estuviera pendiente. La
regresión `passive_gesture_exceptions_are_isolated` comprueba celdas,
barras, variables y textos de lectura, además del siguiente clic válido.
El rechazo de un gesto al presionar o al mover el ratón cancela también
su pulsación pendiente: soltar el botón después no elige accidentalmente
una celda ni una barra. La regresión
`cancelled_passive_gesture_cannot_finish_click` verifica las dos superficies,
los dos momentos de fallo y que el siguiente clic completo funcione.
Cuando una devolución del lector aplica otros datos o selecciona otro paso,
se descarta igualmente el destino del clic original: un evento de ratón
antiguo no puede imponer una selección sobre una operación más reciente.
La protección cubre tanto la reentrada durante un único mensaje como
los cambios independientes que ocurran **entre presionar y soltar**.
Cada pulsación recuerda la época de interacción en que empezó y no se
acepta si el simulador ya cambió de estado, aunque el mismo índice o
tarjeta vuelva a existir después. Una época interna también evita
reutilizar destinos tras una actualización fallida. Las regresiones Win32
`reentrant_reader_gesture_does_not_override_newer_selection` e
`intervening_interaction_cancels_pending_mouse_press` comprueban
navegación anidada, pasos intermedios, Aplicar datos y clics posteriores.
La reorganización visual también cancela las pulsaciones en ambas superficies:
si cambia el ancho de la ventana o la escala y después se suelta el ratón,
no se selecciona una tarjeta antigua, aunque el ancho vuelva al inicial.
La regresión Win32 `layout_reflow_cancels_pending_histogram_and_array_click`
comprueba los dos controles y que sigan admitiendo clics nuevos.
Los espacios entre tarjetas y las áreas ocultas tras el desplazamiento
no son destinos. El puntero indica cuándo una tarjeta es seleccionable.
El selector de pasos, los botones de decisión y las casillas del array
siguen ofreciendo las rutas alternativas por teclado y ratón.

Al **seleccionar directamente una decisión** desde el selector de pasos
o desde los botones **Decisión anterior / siguiente**, el histograma desplaza
lo necesario para mostrar la tarjeta del grupo escogido, incluso después de
saltar muchos grupos. Si esa barra ya está visible, no desplaza el gráfico.
Un grupo que se mantiene oculto por **Mostrar solo candidatos** no aparece
por el simple hecho de seleccionarlo: el filtro sigue respetando el máximo
calculado. En cambio, **Anterior / Siguiente** paso a paso conservan el
desplazamiento manual anterior. Repetir la selección directa del paso actual
permite recuperar su tarjeta sin cambiar el modelo ni acreditar respuestas.
La prueba Win32 `explicit_decision_jumps_reveal_histogram_bar` comprueba
estos límites en `walkthrough` y `candidates`; requiere ejecución en Windows.
En **summary**, las barras son informativas y no cambian el estado.

Las barras utilizan como escala vertical la mayor frecuencia entre los
**grupos ya visibles**. Por eso, una barra anterior puede cambiar de altura
relativa cuando un grupo posterior alcanza un máximo mayor: no cambió la
frecuencia original, cambió la escala de comparación conocida hasta entonces.
La escala se identifica en el título del diagrama.

Cambiar entre **Todos los grupos** y **Sólo candidatos** también constituye
una operación completa de presentación. Si RichEdit rechaza un cambio
tardío, el filtro, el rótulo, el desplazamiento y la identidad de narración
recuperan el modo anterior; no se altera el paso ni la traza aplicada.
Si el observador falla después de un cambio válido, el filtro queda activo
y se muestra un aviso de notificación separado del error de presentación.
La regresión Win32 `candidate_filter_rejects_native_and_observer_failures`
comprueba el rechazo, la recuperación y los reintentos.

Cuando existen grupos terminados con frecuencia inferior al máximo actual,
el botón **Mostrar solo candidatos** permite filtrar el diagrama para
comparar únicamente los grupos que siguen empatados en el máximo. Los
números de grupo y sus valores no cambian: se ocultan visualmente los
grupos no candidatos, no se eliminan de los datos ni de las decisiones.
El botón cambia a **Mostrar todos los grupos** para recuperar la
distribución completa. Si todos los grupos terminados son candidatos, no
se ofrece el filtro inicialmente. Si ya estaba activado al retroceder a
un estado con todos los grupos empatados, el botón sigue disponible para
volver a mostrar todos los grupos.

El filtro utiliza exclusivamente los grupos **ya terminados** y los
candidatos **del paso actual**. No puede adelantar la frecuencia de un grupo
en curso ni mostrar un resultado futuro. Al activarlo o desactivarlo, el
gráfico vuelve al inicio de su desplazamiento y recalcula su tamaño; no
cambia el paso, la salida, los datos originales, los objetivos de
evaluación ni la narración de otras partes de la página. Cambiar de
escenario, aplicar nuevos datos o restablecerlos devuelve el filtro a
**todos los grupos**. En textos conserva la distinción entre `""`,
`" "` y otros datos que no deben mezclarse.

La lectura del gráfico distingue explícitamente **todos** y **sólo candidatos**
aunque se mantenga el mismo paso del algoritmo. Al alternar el filtro,
Windows actualiza el nombre accesible y el lector utiliza una identidad
distinta para cada representación, evitando reutilizar la narración o la
posición de lectura del modo anterior. Regresar al modo previo recupera
su identidad estable; desplazarse dentro del gráfico no crea otra
respuesta ni otro paso de narración.

La disposición se adapta al ancho y al zoom de texto y acepta hasta los
64 grupos permitidos por el modelo. Al cambiar la escala, el DPI o el ancho,
el desplazamiento se reconstruye tomando como referencia el **primer grupo
visible**, no la coordenada de píxeles de la distribución anterior.
Si el gráfico anterior tenía **desplazamiento real** y el estudiante estaba
en su última fila, se conserva el final. Cuando todas las tarjetas cabían
sin desplazamiento, el inicio y el final coincidían: estrechar la ventana
por primera vez **mantiene la primera fila**, sin saltar arbitrariamente a
la última. Esta lectura es independiente de los datos, candidatos y
decisiones computadas. Las regresiones Win32
`histogram_reflow_retains_visible_group` y
`histogram_first_overflow_does_not_jump_to_last_group` comprueban esos
cambios de zoom, DPI y cantidad de columnas; requieren ejecución nativa
en Windows para acreditarse. Para evitar que una lección
tenga un gráfico de miles de píxeles de altura, el área visible de las barras se
limita a 440 unidades de presentación escaladas. Si hay más filas, se
activa una barra de desplazamiento interna. El estudiante puede usar la
rueda del ratón sobre el gráfico, desplazar su barra o alcanzarlo con
Tab y recorrerlo con las flechas, Re Pág, Av Pág, Inicio y Fin. La rueda
con Ctrl conserva su comportamiento de zoom o desplazamiento del lector.
Tanto el histograma como los paneles de texto extenso usan la rueda sin Ctrl
para desplazarse internamente mientras puedan avanzar en esa dirección.
En el histograma, los movimientos de una rueda de alta resolución acumulan
fracciones inferiores a un píxel: varios gestos pequeños no deben perderse
por redondeo. Esa fracción se reinicia al usar directamente la barra o el
teclado, al cambiar la escala, los datos, el filtro o las tarjetas visibles
del recorrido. Si los pasos siguen mostrando las mismas tarjetas, el gesto
continúa acumulándose sin interrupción. Las pruebas Win32
`wheel_edges_resume_reader` y `histogram_wheel_fraction_step_sessions`
cubren estas condiciones y requieren ejecución local en Windows.
Al alcanzar el principio o el final, entregan el gesto a la página para
continuar leyendo la lección sin quedar atrapado en el simulador.

Al cambiar de escenario o aplicar datos nuevos, el gráfico vuelve al
inicio. Al avanzar, si el estudiante estaba viendo el final del histograma,
la barra del nuevo grupo terminado entra en la vista, incluso cuando la
nueva fila provoca el primer desbordamiento. Si había desplazado el gráfico
hacia arriba para consultar barras anteriores, **no** se lo desplaza
automáticamente; podrá volver al final con la rueda o el teclado. Retroceder
a una decisión anterior oculta los grupos que todavía no estaban terminados
y ajusta el desplazamiento al tamaño realmente disponible. La prueba Win32
`histogram_follows_new_groups_only_at_end` cubre 64 grupos, el seguimiento
del final, la lectura manual anterior y el retroceso; su ejecución debe
verificarse en Windows.

La descripción accesible contiene todos los grupos
que ya están terminados, incluidos los que quedan fuera del área gráfica
visible; nunca incluye resultados futuros. Un gráfico corto no añade
un tabulador innecesario. Los paneles RichEdit de lectura, incluidos
explicaciones y salidas largas con desplazamiento propio, **no amplían
su texto por separado al recibir Ctrl+rueda**: entregan ese gesto al
contenedor del lector para mantener una escala y una distribución
coherentes. Su desplazamiento con la rueda sin Ctrl permanece disponible.
Esto se comprueba en `native.simulation_view`, con texto corto y largo.
Los rótulos de cantidad también respetan el singular y el plural:
`0 elementos`, `1 elemento` y `2 elementos`, tanto al iniciar como al
cambiar de escenario, aplicar o restablecer datos. La región narrable
conserva el mismo rótulo visible y una cadena vacía cuenta como un elemento
real. Las regresiones Win32 `array_count_caption_uses_correct_number` y
`neighboring_values_explain_group_boundaries` comprueban estos casos.
La representación no califica ejercicios ni
modifica el progreso o los datos del estudiante.

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
Si ordenar la copia cambió la posición de algún dato, el mismo panel
incluye **«origen índice N»** junto a cada dato contado, utilizando la
procedencia estable que ya comparten las casillas y los marcadores
`{countedIndex}` y `{originIndex}`. Así, al recorrer `[7, 3, 7, 2]`,
la casilla ordenada cero indica que el `2` procede del índice original
tres; las dos apariciones de `7` remiten a los índices originales cero
y dos, respectivamente. No añade un origen redundante cuando los datos
ya están ordenados o `sort` es falso. El texto narrable del panel utiliza
esa misma procedencia, sin introducir información futura.

No presenta `end` como un acceso válido, ni afirma que la decisión final
de un grupo haya leído otro elemento.

Cada paso que cuenta un elemento también explica la **frecuencia actual
del grupo** mediante los límites de su intervalo: `fin - inicio`.
Por ejemplo, si el grupo comienza en el índice 1 y el límite exclusivo
`fin` vale 3, se han contado las posiciones 1 y 2; el panel muestra
**«Recuento de este grupo: fin exclusivo 3 - inicio 1 = 2 elementos
contados.»**. Para un único dato utiliza «1 elemento contado». No
confunde esa cantidad parcial con la longitud completa del array ni
consulta `datos[fin]`. Un paso inicial, una decisión sobre un grupo
terminado o el paso final no inventan un recuento adicional. Esta
explicación calculada aparece también en la región narrable. La
regresión Win32 `calculated_step_changes` comprueba el singular, plural,
la edición, las cadenas vacías, los escenarios y la ausencia de
lecturas futuras; pasó en GitHub Actions con el MSVC exacto del
proyecto (ejecución `44363844966`).

Además, muestra la **relación con el elemento anterior**. Para el índice
cero indica que todavía no existe un elemento previo. En los demás
índices ya contados presenta los dos índices y sus valores: si coinciden,
explica por qué el grupo continúa; si son diferentes, explica por qué
comienza un grupo nuevo. Por ejemplo, con `[-8, 3, 3]`, la lectura del
índice 1 compara `-8` con `3` y abre un grupo; la del índice 2
compara `3` con `3` y mantiene el mismo grupo. Sólo se consultan los
índices `end - 2` y `end - 1` cuando existen; nunca el elemento futuro
`datos[end]`. Las cadenas vacías y los espacios conservan su
representación explícita entre comillas.

Si la copia ordenada cambió el orden de los datos, la comparación de
vecinos incluye otra línea: **Procedencia de la comparación**. Distingue
la posición **ordenada** y la posición **original** de ambos elementos
que ya se contaron. Con `[7, 3, 7, 2]`, la comparación de las posiciones
ordenadas 2 y 3 enfrenta dos valores `7` procedentes de las posiciones
originales 0 y 2; no son un único elemento repetido por el renderizador.
La línea no aparece cuando la entrada ya estaba ordenada, ni al cerrar
un grupo, ni en el paso inicial o final. La narración utiliza el mismo
texto, sin anticipar el siguiente elemento. La regresión Win32
`neighboring_values_explain_group_boundaries` cubre valores numéricos,
etiquetas vacías y repetidas, escenarios, datos editados y lectura
accesible; pasó en la ejecución Windows `44363844966`.

Las observaciones de recuento descritas arriba proceden ahora de una
única función portable, `simulation_count_observation_changes`, en
`native/core/simulation.cpp`. La vista Windows consume esas líneas
calculadas sin repetir las reglas de índices y vecinos. La batería de
`native/core/simulation_tests.cpp` comprueba el texto completo y su
orden, límites exclusivos, posiciones originales estables, singular y
plural, igualdad y diferencia, cadena vacía, espacios significativos,
entradas ya ordenadas y trazas inválidas. La ejecución
`44355364985` pasó sus cuatro tareas Python, GCC, Clang y MSVC
portable. Esto **sí verifica el cálculo narrable del núcleo**, pero
**no acredita aún** la presentación RichEdit ni los gestos y la
narración nativos de `native.simulation_view` con el toolchain Windows
exigido por el proyecto.

Al terminar cada grupo, el panel
también muestra la comparación efectiva entre su frecuencia y el
**máximo anterior a la decisión**: `>` reemplaza candidatos, `=` agrega
el valor empatado y `<` los conserva. Por ejemplo, `4 > 1` explica
por qué se actualizó el máximo a 4. Esta comparación procede de los
estados calculados, no de los mensajes explicativos redactados por Work.
El caso de array vacío y las vistas estáticas no inventan observaciones.

La comparación `frecuencia >, = o < máximo anterior` también usa una
función del núcleo portable, `simulation_group_decision_comparison`.
Ésta conserva el máximo que existía antes de tomar la decisión, incluso
en las vistas de grupos completos, y rechaza estados incompatibles con
su relación matemática. La vista Win32 comparte el mismo texto y las
pruebas C++ lo contrastan para máximos nuevos, empates, grupos menores,
saltos, rótulos curriculares y decisiones falsificadas. La ejecución
`44356007903` terminó correctamente en Python, GCC, Clang y MSVC
portable. La aprobación portable no equivale a una ejecución de los
controles RichEdit o a la validación integral del lector Win32.

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
| `{previousMaximum}` | Máximo **antes** de comparar la frecuencia del grupo recién terminado, no el máximo que resulta de esa decisión. Solo tiene contenido en `newMaximum`, `tie`, `belowMaximum` y `outputTemplate`. |
| `{leaders}` | Valores que alcanzan ese máximo, separados por coma y espacio; presentación legible en explicaciones y datos crudos en `outputTemplate`. |
| `{size}` | Cantidad de elementos del array. |
| `{group}` | Número del grupo actual desde uno; cero antes del primero. |
| `{groups}` | Cantidad total de grupos de la copia ordenada. |
| `{processedCount}` | Cantidad de elementos ya contados en la copia ordenada, de cero hasta `{size}`. Coincide con el límite exclusivo `{end}` del rango observado. |
| `{remainingCount}` | Cantidad que falta por contar: `{size}` menos `{processedCount}`. |
| `{countedIndex}` | Índice, desde cero, de la casilla **recién contada** en la copia ordenada. |
| `{originIndex}` | Índice original, desde cero, del mismo elemento recién contado **antes de ordenar**. |

Los marcadores de cantidades funcionan en **todas las fases**, incluidas
`initial` y `empty` (cero elementos procesados), `start`, `advance`,
las decisiones de grupo y `done` (no quedan elementos por contar).
El motor los calcula a partir del rango realmente observado; una decisión
sobre el grupo terminado **no vuelve a contar** su último elemento.
También pueden usarse en `outputTemplate`, que se evalúa al cerrar cada
grupo. Por ejemplo, `"{value}: {processedCount}/{remainingCount}"`
genera `"2: 1/2"` si se escribe un espacio literal después de los dos
puntos y el primer grupo de `[5, 2, 5]` contiene sólo el valor `2`.
No cambia el dato ni las frecuencias: sólo muestra cuánto recorrido se
ha completado. Las cantidades se recalculan después de editar, restablecer
o cambiar de escenario, incluso con etiquetas vacías.

`{previousMaximum}` permite explicar la comparación que realmente toma
la decisión de cada grupo: su frecuencia se compara con el máximo
obtenido **antes** de terminar ese grupo. Por ejemplo, al ordenar
`[5, 2, 5]`, el grupo de `2` tiene frecuencia `1` y supera al
máximo anterior de `0`; después, el grupo de `5` alcanza frecuencia
`2` y supera el máximo anterior de `1`. El marcador conserva
el número previo, mientras `{maximum}` muestra el resultado
**posterior** a la decisión. En un empate, ambos números coinciden;
si la frecuencia es menor, el máximo no cambia. El motor obtiene ese
número del paso de recuento inmediatamente anterior, con independencia
de la redacción elegida por Work.

En `initial`, `start`, `advance`, `done` y `empty`,
`{previousMaximum}` se sustituye por una cadena vacía, pues no se
está tomando una decisión nueva. En `outputTemplate` sí tiene valor:
la salida de un grupo se calcula precisamente al tomar esa decisión.
Funciona con números, textos, empates, colecciones vacías y escenarios
editados sin cambiar los oráculos ni adelantar datos futuros.

Los dos índices de procedencia sólo tienen contenido en `start` y `advance`,
que son las fases donde se cuenta un elemento. Durante `initial`, las
decisiones `newMaximum`, `tie` y `belowMaximum`, `done` y `empty`
su sustitución es una cadena vacía: **una decisión no vuelve a leer la
última casilla**. Por el mismo motivo, el contrato **rechaza** esos
marcadores en `outputTemplate`, que se evalúa al terminar cada grupo,
sin contar allí ningún elemento nuevo. Los demás marcadores siguen
disponibles para la salida calculada.
Si `sort` es `false` o los datos ya están ordenados, los índices
coinciden; si se ordenan, `originIndex` se calcula conservando el
orden original entre valores iguales.

Por ejemplo, si la entrada es `[7, 3, 7, 2]`, la copia ordenada es
`[2, 3, 7, 7]`. Sus posiciones originales son `[3, 1, 0, 2]`.
En el primer `start`, `{countedIndex}` vale `0` y `{originIndex}`
vale `3`. Al contar los dos `7`, los pares son `(2, 0)` y
`(3, 2)`. Work puede describir cada vuelta sin escribir posiciones
fijas ni confundir la copia ordenada con el array original. El mismo
cálculo se aplica a escenarios, entradas editadas y textos, incluso
cuando el valor es una cadena vacía.

La regresión nativa `counted_source_origin_explanation` abre un lector
Win32 aislado y comprueba que las casillas, las explicaciones visibles y sus
regiones narrables señalen el **mismo índice original** al avanzar, hacer
clic, retroceder, cambiar de escenario, editar datos y restablecerlos.
Restablecer recupera los datos del escenario seleccionado; regresar a los
datos iniciales requiere elegir el escenario inicial. La regresión también
cubre una cadena vacía, repeticiones y una colección sin elementos. Estos
controles están incorporados a `native.simulation_view`. La comprobación
independiente en Windows los ejecuta también mediante
`native.simulation_view_full`, con las mismas fuentes de producción y de
prueba, como se detalla al final de este documento.

La regresión `processed_remaining_progress_in_reader` comprueba también
el recuento acumulado y pendiente en el lector Win32, sus regiones narrables
y la salida de cada grupo, antes y después de modificar datos, seleccionar un
escenario vacío y restablecer los valores. También verifica el
`{previousMaximum}` anterior a cada decisión en las explicaciones
visibles y las regiones narrables, sin reutilizar el máximo ya actualizado.
Las comprobaciones independientes de C++ y Python incluyen intervalos
ordenados, textos repetidos, cadenas
vacías y la suma del tamaño de todas las explicaciones y salidas.

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

Si el checkout se trasladó de carpeta y CMake informa que su caché pertenece
a la ruta anterior, añadir `-Fresh` al mismo comando. Renueva únicamente
la configuración generada del preset antes de compilar; no modifica fuentes,
perfiles ni el checkout de Work. No es necesario en las ejecuciones siguientes.

El comando usa las herramientas locales autorizadas y el objetivo CMake
`lgaed_simulation_qa_targets`, sin construir la aplicación completa. Exige
que CTest encuentre exactamente cuatro pruebas:
`native.simulation_model`, `native.simulation_view`,
`native.simulation_lesson` y `native.simulation_authoring`. Después las
ejecuta de forma secuencial. Las ventanas de la prueba nativa permanecen
ocultas. Los registros se conservan en `out/build/<preset>/logs/` y las
capturas de regresión en el directorio temporal de CTest para simulaciones.
El ejecutor exige tanto el directorio físico de QA como la rama
`Codex` del checkout **antes de configurar o compilar**: tener una
carpeta llamada `LearnGameAndEngineDev-Codex` con `main` activo no
autoriza ejecutar el conjunto de pruebas. También preserva y restaura
las variables de entorno temporales que modifica el descubrimiento de
herramientas, incluso cuando la construcción o CTest falla.

No modifica perfiles reales ni ejecuta GitHub Actions. No se debe atribuir
una aprobación Windows a la mera publicación del script.

Los cambios de zoom que comparten el simulador, el lector y las actividades
de matemáticas, completar código y ordenar fragmentos también necesitan
una comprobación transversal. En el checkout nativo `Codex` de Windows:

```powershell
& '.\scripts\windows\Test-TextZoom.ps1' -Preset windows-x64-debug
```

Este segundo comando compila el objetivo `lgaed_text_zoom_qa_targets`,
sin incluir el ejecutable de la aplicación como dependencia directa. Exige
exactamente seis pruebas CTest: `native.rich_text`, `native.cpp_fill_view`,
`native.order_code_view`, `native.math_puzzle_view`,
`native.simulation_view` y `native.simulation_lesson`. Se ejecutan de
forma secuencial, con límite de tiempo, y escriben registros
`text-zoom-qa.stdout.log` y `text-zoom-qa.stderr.log` en
`out/build/<preset>/logs/`. El comando comprueba Windows local, el checkout
autorizado, la rama `Codex` y la ausencia de CI; conserva el entorno del
proceso original. Permite `-Fresh` en caso de caché CMake trasladada. No
sustituye a `Test-Simulation.ps1`, pues el primero comprueba los controles de
texto y el segundo incluye el núcleo matemático y la autoría. Ningún comando
se considera acreditado antes de ejecutarse en el PC Windows.

La prueba `native.simulation_lesson` incluye casos de integración del
**lector Windows real** con datos `[]`, `[""]`, `["", "patio", "patio"]`
y `["", " "]`. Verifica que el cargador completo y el de progreso
reconozcan el mismo contrato; que el simulador muestre valores vacíos
sin descartarlos; que avance y retroceda manteniendo la salida exacta
de cada estado; y que la edición explícita entre comillas conserve el
dato vacío y los espacios. Comprueba también que la exploración no
genera respuestas ni modifica el progreso de la lección. Utiliza
lecciones sintéticas y perfiles QA, no datos del estudiante. La
implementación de la prueba se ha ejecutado en Windows Server de
GitHub Actions con MSVC 14.51.36231. Esto acredita el entorno de
integración desechable, no una comprobación manual en el PC del usuario.

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

### Evidencia de simulación en Windows con el toolchain fijado

La ejecución de GitHub Actions
[`44363844966`](https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/44363844966)
registró **4 de 4** pruebas nativas de simulación aprobadas:
`native.simulation_model`, `native.simulation_view`,
`native.simulation_lesson` y `native.simulation_authoring`. El
mismo entorno declaró MSVC `14.51.36231` con
`cl.exe 19.51.36260.0`. También pasaron las dos pruebas
`native.lesson_menu` y `native.lesson_scroll_render`.
La integración comprobada utiliza lecciones y perfiles aislados de QA;
no modifica el progreso del usuario.

La ejecución posterior de ReaderBuild
[`44557576767`](https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/44557576767)
terminó correctamente y volvió a aprobar esas cuatro pruebas de simulación
con MSVC `14.51.36231` y `cl.exe 19.51.36260.0`. Es evidencia de esa revisión
en un entorno Windows de integración; la aceptación visual y auditiva humana
en el PC del usuario sigue siendo una comprobación distinta.

### Comprobación independiente en Windows con MSVC

El flujo `.github/workflows/simulation-core-codex.yml` tiene cuatro trabajos,
todos en Windows 2022:

- Autoría y parser: ejecuta `scripts/curriculum/test_simulation.py` con Python.
- Modelo C++: compila y ejecuta `native/core/simulation.cpp` junto con
  `native/core/simulation_tests.cpp` mediante MSVC.
- Compilación Win32: compila la vista real y sus pruebas de interacción.
- Controles Win32: construye el ejecutor aislado de
  `cmake/PortableSimulationViewQA` y ejecuta `native.simulation_blank_button`
  y `native.simulation_view_full`, incluidos la edición de cadenas vacías
  y los recorridos con los controles reales.

La ejecución
[`45365936311`](https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/45365936311),
de la revisión `ab3a87f3`, completó correctamente los cuatro trabajos:
**34 de 34** pruebas de autoría, el modelo C++ y **2 de 2** pruebas de controles
Win32. El ejecutor de controles declaró MSVC `19.44.35230.0`, toolset
`14.44.35207`; el flujo utiliza `/std:c++latest`.

Este conjunto usa las herramientas alojadas y las fuentes reales del modelo
y de la vista, con datos y ventanas aislados de QA. El perfil local de la
aplicación conserva las versiones de `toolchains/windows.versions.json`.
La integración del lector completo, incluido `native.simulation_lesson`,
se comprueba por separado mediante `Test-Simulation.ps1` o la batería
ReaderBuild con su toolchain declarado. La ejecución de estos controles
no supone una compilación integral de la aplicación ni una escucha humana.

# Comprobación de objetivos del compilador

`cpp_compiler` admite `outputCheck` para declarar un objetivo de stdout. Con él,
el botón muestra «Compilar, ejecutar y comprobar»: una pulsación construye,
ejecuta y comprueba la salida nueva. No se añade un segundo botón. Work incorpora
los casos después de verificar su objetivo; el motor no deduce respuestas de la
prosa, de una predicción contigua ni del código de referencia.

El contrato Windows incluye también `compileCheck` para los rechazos intencionales
revisados. Todo editor curricular es obligatorio, aunque falte el antiguo flag
`compilerPuzzlesRequired`; la falta de evaluador es un bloqueo técnico, no permiso
para omitirlo. El Laboratorio libre conserva la exploración sin objetivos. Los
apartados de Windows siguientes describen la implementación nativa; las notas
identificadas como Android conservan únicamente su alcance histórico.

```json
"outputCheck": {
  "inputComparison": "exact",
  "cases": [
    {
      "standardInput": "",
      "outputs": ["Caja recibida"],
      "explanation": "Las comillas delimitan el literal y no forman parte del mensaje.",
      "reviewFeedback": [
        {
          "outputs": ["Zona segura"],
          "text": "Revisa si realizaste y ejecutaste la segunda modificación."
        }
      ]
    }
  ]
}
```

El ejemplo comprueba únicamente la construcción final de esa etapa. Las ejecuciones del original y de la primera variación siguen disponibles, sin exigir otro historial ni otra comprobación. No se añaden identificadores al bloque ni se modifica el progreso existente.

## Criterio curricular posterior: primera comprobación suficiente

La regla de avance indicada el 11 de noviembre de 2026 es acreditar **únicamente
el primer caso** de un editor para continuar. Las demás entradas y salidas deben
aparecer en el texto curricular y quedar disponibles para pruebas **opcionales**,
sin exigir otra pulsación ni bloquear el lector, las referencias o las páginas.
Las sondas internas que se ejecutan automáticamente durante la primera
comprobación pueden seguir protegiendo los criterios semánticos: no son nuevas
pruebas manuales que deba realizar el estudiante.

**Implementación Windows:** el primer elemento de `outputCheck.cases` es el
caso obligatorio. Su ejecución correcta y verificada concede un recibo de avance
sin exigir las entradas posteriores. Éstas siguen disponibles para ejecutarse
y observarse voluntariamente; un caso posterior nunca sustituye al primero.
Los recibos antiguos que acreditaron todos los casos se reconocen sin cambios.
Los requisitos semánticos del primer caso, incluidas sondas internas, se siguen
comprobando antes de conceder el logro. La incorporación de declaraciones
curriculares U3L6/U3L8 requiere las verificaciones específicas de Work.

## Contrato

- `cases` es un array no vacío. Cada caso declara `standardInput` de tipo texto (puede ser vacío) y `outputs`, una lista no vacía de salidas completas y distintas. Una salida vacía también es válida.
- La comparación de **stdout siempre es literal**: espacios, tabuladores, CR y LF se conservan. Para permitir exclusivamente un LF final, declarar ambas alternativas, por ejemplo `"Informe\nZona Este\nFin"` y `"Informe\nZona Este\nFin\n"`. No se recortan los extremos.
- `inputComparison` es `exact` por defecto. `tokens` se usa sólo cuando la consigna permite separar los mismos datos con espacios en blanco ASCII (` `, TAB, LF, CR, VT, FF). Por ejemplo, un caso `"Leo 2"` también identifica `"Leo\n2\n"`. No normaliza stdout ni admite espacios Unicode como si fueran separadores de C++.
- Los casos deben identificar inequívocamente la ejecución bajo el modo seleccionado. En un proyecto CMake, cada caso admite `target` opcional: el nombre exacto y sensible a mayúsculas del ejecutable que debe producir esa salida. Dos ejecutables pueden compartir entrada y tener salidas distintas. Se usa el objetivo realmente capturado al pulsar ejecutar, sin deducirlo del código ni añadir un selector curricular de respuestas.
- Sin `target`, se conserva el contrato anterior: el caso se identifica por entrada y es aplicable a cualquier objetivo del bloque. No se permite mezclar un caso global y otro con `target` para la misma entrada, ni repetir un mismo par objetivo/entrada. No existe precedencia implícita entre ellos. `target` sólo es válido en bloques con `cmake` y acepta el mismo formato que `cmake.target`: `[A-Za-z_][A-Za-z0-9_.+-]{0,127}`. No obliga a que sea el objetivo inicial: el alumno puede cambiar de ejecutable y reconstruirlo.
- `explanation` es texto opcional no vacío de acierto. `reviewFeedback` es un array opcional, también puede ser vacío: cada regla declara `outputs` y `text` no vacío. Sólo una coincidencia literal con esas salidas incorrectas permite mostrar su orientación. Las condiciones no pueden solaparse entre reglas ni con las salidas correctas.
- En Windows, cada caso admite además `standardError: "nonBlank"` para exigir información adicional en stderr durante una ejecución correcta. Se detalla más adelante; omitirlo mantiene exactamente el contrato de stdout anterior.
- En Windows, `exitCode` es un entero de `0` a `255`, con valor predeterminado
  `0`. Un retorno intencional se comprueba junto con stdout y stderr de la misma
  ejecución terminada. No convierte cualquier fallo del proceso en un acierto.
- Sin explicación se muestra sólo el veredicto. Un error no previsto muestra sólo «Incorrecto». No se infiere una causa, no se expone la salida esperada y los campos internos no entran en el guion normal de la lección. Se rechazan tipos y propiedades desconocidos como errores de configuración, no como respuestas incorrectas.

## Ejecución y conservación

«Compilar, ejecutar y comprobar» utiliza únicamente la ejecución nueva del
código/proyecto, compilador, configuración y entrada capturados por esa
pulsación. Nunca comprueba una salida que ya estuviera en la consola. El proceso
debe terminar y disponer de capturas completas. Para acertar debe satisfacer
también el código de salida declarado. Fallos de compilación, enlace,
configuración, cancelaciones y límites
técnicos no son respuestas evaluables. Una ejecución no satisfactoria sólo es
evaluable como retorno intencional cuando el caso declara un retorno distinto de
cero; otro código terminado de `0` a `255` produce «Incorrecto» en ese objetivo.
Un fallo no cero bajo un caso que exige cero sigue sin ser evaluable. Los
avisos y los canales de salida se conservan sin reescribirlos.

En Windows, las herramientas de compilación y enlace utilizan una consola privada
oculta configurada en UTF-8, tanto en fuentes simples como en proyectos directos,
CMake y rechazos intencionales. Así se conservan los acentos del diagnóstico y
de sus rutas sin confundir la salida OEM de MSVC con texto ANSI o UTF-8. La
captura guarda los bytes emitidos por cada herramienta. La ejecución del programa
mantiene separados y exactos sus canales binarios de entrada, stdout y stderr,
incluidos NUL y bytes que no formen texto UTF-8. La consola privada pertenece al
intento; no cambia la configuración de Windows ni la del terminal del usuario.

Los laboratorios CMake conservan sus controles explícitos de fases. Con `outputCheck`, la acción combinada configura si hace falta, construye el objetivo vigente mediante CMake y ejecuta un programa nuevo antes de comparar; se conservan los diagnósticos de cada fase. Un resultado restaurado por el observador del proyecto puede volver a mostrar su consola, pero nunca genera una comprobación. Sin contrato se conserva el funcionamiento de sus controles de configuración, construcción y ejecución.

Editar una fuente, cabecera, selección de fuentes, entrada, configuración CMake
o compilador invalida la ejecución corriente. Deshacer o rehacer no recupera una
ejecución antigua: hay que ejecutar de nuevo. Cambiar sólo de pestaña conserva la
identidad del programa. Un resultado tardío de otra revisión no habilita una
comprobación. Windows conserva por separado el logro acreditado, los parciales
del mismo programa y la última revisión pedagógica; las reglas se detallan en
«Acreditación y avance». La consola transitoria no es un recibo de acreditación.

Estas comprobaciones **no consumen corazones** ni responden la predicción
vecina. En Windows acreditan el objetivo propio del compilador y controlan el
avance de las actividades curriculares. Una coincidencia de stdout admite
diferentes implementaciones y no demuestra por sí sola que se hayan utilizado
las variables o la estrategia solicitadas.

Con el lector activado, el veredicto visible se narra aunque no tenga explicación.
Un objetivo obligatorio pendiente detiene el recorrido después de su consigna;
el contenido posterior espera. Un nuevo intento lleva la revisión al veredicto
actual, incluso si ya existía un logro y el lector había avanzado. El error se
lee y espera otro intento; el acierto lee su explicación antes de continuar.
Una pausa manual permanece pausada. Las explicaciones anteriores pendientes se
atienden en su orden; buscar o adelantar el audio no acredita una escucha.
Consola y código conservan sus exclusiones. Se usa el mismo reproductor y los
tres marcadores acotados de página, sin crear un historial paralelo.

El contrato es independiente de `output_prediction`, de los datos de evaluación matemática y del estilo del texto. Ningún JSON antiguo necesita migración.

## Varios ejecutables de un proyecto CMake

Work puede declarar un caso por consumidor, conservando su entrada y salida literal. Por ejemplo, dos programas con entrada vacía:

```json
"outputCheck": {
  "cases": [
    { "target": "consultar_cajas", "standardInput": "", "outputs": ["1:2"] },
    {
      "target": "comprobar_cajas",
      "standardInput": "",
      "outputs": ["piezas=0 esperado=0 obtenido=0 OK\npiezas=4 esperado=1 obtenido=1 OK\npiezas=8 esperado=2 obtenido=2 OK\npiezas=20 esperado=5 obtenido=5 OK\npiezas=1 esperado=1 obtenido=1 OK\npiezas=5 esperado=2 obtenido=2 OK\npiezas=17 esperado=5 obtenido=5 OK\nBateria: OK\n"]
    }
  ]
}
```

La salida del cliente no acredita que se haya ejecutado la batería: cada objetivo y entrada conserva su veredicto independiente. Un objetivo o entrada sin caso mantiene la ejecución disponible, sin veredicto ni aceptación implícita de otro consumidor. Para avanzar basta la primera ejecución declarada en `cases`; las restantes son ensayos opcionales, no pruebas fabricadas ni acreditaciones de una actividad de construcción distinta. Los casos ejecutados se conservan al seleccionar otro objetivo del mismo proyecto sin alterar fuentes o configuración. La reapertura conserva esos datos y sus pruebas auténticas, sin inventar una ejecución nueva. Configuración, generación, construcción y ejecución conservan sus diagnósticos y no consumen corazones. Las explicaciones y orientaciones opcionales pertenecen a su caso, sin revelar el resultado de otro ejecutable.

Las siete prácticas de `datos_y_listas_de_objetivos` y `reorganizar_la_descripcion_conservando_el_comprobador` que solicitan ambos consumidores declaran dos casos por editor, con entrada vacía, objetivo explícito, stdout literal y retorno cero. Las lecciones requieren completar sus objetivos de compilación. Estas comprobaciones verifican las ejecuciones; los criterios de las actividades también piden inspeccionar las propiedades y la organización de CMake. Obtener la salida correcta no demuestra por sí solo que se haya realizado esa reorganización.

## Diagnóstico adicional de ejecución en Windows

Cuando la actividad pide observar un diagnóstico de biblioteca cuya redacción puede variar, Windows permite añadir un requisito al mismo caso sin fijar esa frase:

```json
"outputCheck": {
  "cases": [
    {
      "standardInput": "",
      "outputs": ["Indice invalido:Fin"],
      "standardError": "nonBlank"
    }
  ]
}
```

El caso exige simultáneamente stdout literal y al menos un carácter de stderr distinto de espacio, TAB, CR, LF, FF o VT ASCII. Una cadena vacía o formada sólo por esos separadores no acredita el diagnóstico. No se recorta ni se reescribe la consola. `standardError` sólo admite el literal `"nonBlank"`; los valores desconocidos o de otro tipo son errores de configuración. Para no comprobar stderr se omite la propiedad. El requisito pertenece al caso identificado por entrada y, si existe, `target`; no se hereda de otro caso o ejecutable.

Sólo se utilizan los dos canales de la **misma ejecución nueva**, terminada con código cero y correspondiente al programa/configuración/entrada capturados. Los diagnósticos de compilación, enlace o CMake nunca satisfacen stderr, aunque contengan texto. Una ejecución correcta que escribe en stderr sigue siendo una ejecución correcta; ese canal no es por sí mismo un fallo. Si stdout coincide y falta el diagnóstico requerido, el objetivo completo continúa pendiente y no habilita sus referencias ni el avance. La retroalimentación distingue la coincidencia de stdout del requisito adicional pendiente; no impone una frase de biblioteca ni revela otra solución.

Un logro anterior de stdout no acredita el requisito nuevo. La identidad de la prueba de finalización incluye `standardError`; al incorporarlo se conservan borradores, respuestas históricas y direcciones de estado, pero se necesita una ejecución que satisfaga el objetivo completo. Una vez conseguido, conserva las mismas reglas de acierto persistente de Windows: sólo el botón de reinicio lo revoca.

Work puede añadir el requisito a un caso existente sin cambiar sus `outputs`, entrada, objetivo CMake, programa ni identificadores. En lecciones con `stateLayout` v2, `prepare_v2` registra el cambio completo mediante la procedencia antes/después habitual, conservando origen, familia y direcciones. El validador permite esta ampliación compatible y rechaza retirar o debilitar un requisito publicado, así como cambiar las respuestas de stdout o el código bajo la misma identidad. No se regeneran mapas ni se convierten aciertos antiguos en pruebas del nuevo canal. Esta ampliación corresponde al motor Windows; no amplía por sí misma el contrato del motor Android conservado.

### Diagnóstico literal en stderr para clientes con contrato fijo

Cuando el ejercicio exige una frase concreta del **programa ejecutado** (no
un mensaje variable de la biblioteca o del compilador), cada caso admite
`standardErrorExact` como texto UTF-8, incluso la cadena vacía. Por ejemplo:

```json
"outputCheck": {
  "cases": [{
    "standardInput": "2x\n",
    "outputs": ["Resumen: validos=0\n"],
    "standardErrorExact": "Linea=1:FORMATO\n",
    "exitCode": 1
  }]
}
```

El verificador exige simultáneamente stdout literal, stderr literal,
entrada, objetivo CMake cuando exista y el código de salida **de la misma
ejecución nueva, terminada normalmente**. Se comparan caracteres exactos:
espacios, tildes, CR y LF son significativos; no se aplica trim ni
normalización. La cadena vacía comprueba que el programa no escribió
en stderr. Un fallo de construcción, timeout, aborto o una ejecución
anterior no aporta evidencia del diagnóstico solicitado.

El campo antiguo `standardError: "nonBlank"` permanece compatible.
Cada caso puede declarar uno de esos dos criterios, **nunca ambos**.
Al añadir el objetivo exacto a un ejercicio existente se mantienen
identificadores, borradores y progreso histórico, pero una prueba anterior
de stdout no acredita el requisito nuevo: cambia la identidad sellada de
finalización. Un objetivo exacto ya publicado no puede sustituirse,
retirarse o degradarse silenciosamente.

**Alcance:** comprueba el texto observable, no demuestra que se haya
realizado una validación interna específica ni que el contenido se haya
calculado en vez de fijarse como literal. Las demás capacidades abiertas
de Programación 2 siguen requiriendo comprobadores adicionales. Work
deberá revisar y publicar cada caso curricular antes de activarlo.
### Declaración raíz de U2L3: rechazo EOF sin modificar el proyecto

Para el propietario histórico `block_0009_0008` de
`programming_practice_tools__construccion_reproducible__opciones_y_revision_de_la_configuracion`,
el cargador de Windows reconoce esta declaración en la **raíz** de la lección:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0009_0008", "kind": "exactOriginalProjectRuntimeV1"}
]
```

El registro no se añade al bloque `cpp_compiler` ni modifica sus `projectFiles`,
`stateLayout`, entradas, traducciones o referencias. El contrato derivado exige
el proyecto **sin editar**, el objetivo CMake `revisar` con
`ACEPTAR_IGUAL=OFF`, entrada literal `6`, stdout vacío, stderr literal
`Entrada invalida` (sin salto de línea) y terminación normal con código `2`.
Una edición que produzca la misma salida no acredita este objetivo. El botón
del lector mantiene las fases **Configurar y generar → Construir objetivo →
Ejecutar objetivo construido**; el resultado pertenece a la ejecución real
de los archivos y opciones actuales.

La validación de `stable_state_layout.py` permite incorporar únicamente esa
declaración a la lección española v2 correspondiente. No autoriza reemplazar
las fuentes ni cambiar el objetivo para imitar el diagnóstico. Una vez
publicado, el registro no puede retirarse o reescribirse en una transición.

La declaración de U2L3 ya está integrada en el currículo español de `main`.
Activa una única ejecución obligatoria: el rechazo de la segunda extracción al
llegar a EOF. Las dos funciones abiertas de U3L6 necesitan una evaluación
semántica de los hechos del mensaje
y del flujo `std::ostream&` recibido. `standardErrorExact` o `nonBlank` no
son sustitutos de esa capacidad. Los informes U3L8 requieren, además,
conservar filas, índices y contadores, no solamente imprimir una cadena fija.

### Diagnósticos abiertos U3L6: hechos y flujo recibido

La capacidad de Windows para los dos editores históricos de
`programming_practice_tools__comprobaciones_repetibles__diagnosticos_para_corregir_una_peticion`
se activa **únicamente** mediante esta declaración en la raíz de la lección
española v2, después de la integración y revisión curricular por Work:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0003_0003", "kind": "semanticRejectionBoxesV1"},
  {"activity": "block_0006_0003", "kind": "semanticRejectionBalanceV1"}
]
```

La declaración no modifica `cpp_compiler`, su programa inicial, sus borradores
ni el `stateLayout`. La incorporación genera objetivos protegidos de
**tres casos para cajas** (rechazos, cuatro valores permitidos y entrada vacía)
y **cuatro para saldos** (rechazos, cuatro pares permitidos, entrada vacía y
tres pares propios). **Sólo el primer caso correcto de cada editor es necesario
para continuar.** Los otros se documentan con entrada y resultado esperado y
pueden ejecutarse voluntariamente, sin bloquear la página. Cada caso ejecutado
calcula los resultados y exige el código de salida normal correspondiente.

Los mensajes abiertos se comprueban por operación, número de petición,
cantidad o par de campos con sus valores, intervalo 0..20 o 0..9 y acción
de revisión, sin imponer una frase literal. Se comprueba el número y el orden
de ocurrencias, sin confundir un saldo negativo válido con una entrada rechazada.
La variante de saldos requiere, por orden, un rechazo inferior, una aceptación y
un rechazo superior, con los otros operandos válidos y sin reutilizar los pares
preparados.

Para acreditar el editor también debe compilarse y ejecutarse **otra fuente
aislada** que llama a la misma función del estudiante con dos objetos
`std::ostringstream` distintos. La prueba rechaza una función que escribe
directamente en `std::cerr`, incluso si la ejecución original muestra el mismo
texto. Sólo se permiten modificaciones dentro del cuerpo de la función de
diagnóstico asignada; cambiar `main`, el componente, la firma o los datos del
programa no acredita el objetivo. La ejecución, la prueba independiente y los
casos se sellan con la revisión y la fuente vigente.

**Estado editorial:** la activación de la capacidad técnica es optativa hasta
que Work publique ambas declaraciones y compruebe el primer caso y sus sondas
semánticas independientes. Los editores
históricos publicados no se acreditan automáticamente ni se convierten en
opcionales. Esta capacidad tampoco sustituye la comprobación de conservación
de filas y contadores de U3L8.

### U3L8: informes completos y diferencias sin perder observaciones

Los editores originales de `programming_practice_tools__comprobaciones_repetibles__automatizar_la_revision_sin_borrar_evidencia`
admiten, tras revisión e integración curricular de Work, esta declaración **sólo en la raíz**
de la lección española v2:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0003_0003", "kind": "observationCompleteV1"},
  {"activity": "block_0006_0003", "kind": "observationDifferencesV1"}
]
```

No se modifican sus `cpp_compiler`, código inicial, funciones de lectura, respuestas,
borradores, `stateLayout`, mapas de idiomas ni referencias. Cada editor recibe
cuatro casos verificables: entrada publicada, dos registros coincidentes con
última línea sin `LF`, entrada vacía y un lote con repeticiones, campos
malformados y observaciones posteriores. **El primer caso correcto permite
continuar**; los otros tres son pruebas opcionales cuyos datos y resultados
esperados siguen disponibles. Para cada entrada, un cálculo separado
deduce filas, diagnóstico `Linea=N:FORMATO`, contadores y código de salida
normal. El informe completo muestra cada fila válida; la vista de diferencias
cuenta todas las válidas, pero sólo muestra las discrepantes.

También se ejecuta **el mismo programa** con dos entradas de control
independientes de los casos anteriores, dentro del rango curricular de doce
líneas físicas. Incluyen etiquetas repetidas antes y después de un error,
línea vacía, sólo blancos, enteros incompletos, campos unidos y una cuarta
columna. El veredicto exige igualdad de las dos salidas y códigos de salida de
estas ejecuciones, con los recibos asociados a la fuente y revisión capturadas.
En la consola nativa, se admite únicamente la traducción textual propia del
CRT de `LF` a `CRLF`; no se recortan caracteres ni se ignora el diagnóstico.

El editor sólo puede corregir el cuerpo de `main`: `leer_entero` y
`leer_registro`, sus firmas, sus inclusiones y el resto del archivo deben
permanecer intactos. Una salida preparada codificada como constante no
acredita las entradas independientes. Una ejecución que no termine normalmente
o un error de construcción tampoco cuenta como resultado esperado.

**Estado:** el motor y sus pruebas se integran en `Codex`, pero la ficha
`P2_U3_DIAGNOSTICOS_E_INFORMES` permanece abierta hasta que Work publique las
declaraciones compatibles y se comprueben los cuatro editores reales sin
dar crédito por ejecuciones anteriores. La revisión visual y auditiva
personal continúa a cargo del estudiante.

### Aserciones U3L9: aborto real, `NDEBUG` y corrección

La capacidad de Windows para los tres editores originales de
`programming_practice_tools__comprobaciones_repetibles__comprobar_acuerdos_internos_con_aserciones`
se activa únicamente mediante estas declaraciones de raíz en la lección española v2:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0002_0003", "kind": "wasiAssertionAbortV1"},
  {"activity": "block_0003_0007", "kind": "wasiAssertionSuppressedV1"},
  {"activity": "block_0004_0007", "kind": "wasiAssertionRepairV1"}
]
```

Cada editor utiliza su identidad histórica y **un solo caso visible obligatorio**.
Su código inicial y la entrada `-1 5\n` se conservan. El lector selecciona
específicamente WASI, con la versión de SDK y runtime fijadas para el proyecto;
no sustituye la terminación por un código de MSVC ni acepta un número distinto
de cero como demostración suficiente de una aserción.

**Aserción activa:** se ejecuta la fuente original sin edición. La prueba
comprueba una captura de ejecución real con fuente y artefacto WebAssembly,
el rechazo público anterior, la expresión que falló, el archivo y función
`cajas_para`, la ruta por `__assert_fail` y la trampa de Wasmtime. No se
establece universalmente el código de aborto: se verifica la causa observable
de esta compilación concreta. Una salida inventada, timeout o fallo genérico
del runtime no acredita la actividad.

**`NDEBUG`:** se mantiene la directiva antes de `<cassert>` y se usa la fuente
original. La entrada preparada produce stdout `Cajas=1\n`, el rechazo público
exacto en stderr y terminación normal con código `1`. Es un rechazo de
solicitud manejado, no un aborto. El hecho de que el cálculo defectuoso llegue
a imprimirse también evidencia que la condición de `assert` no detuvo el programa.

**Reparación:** sólo se admite modificar la expresión inicializadora de
`cajas` dentro de `cajas_para`; se conservan `assert`, las funciones públicas,
las firmas y las demás instrucciones. El caso preparado debe producir
`Cajas=2\n`, su diagnóstico público y retorno normal `1`. Además se
ejecuta exactamente la fuente editada con dos entradas automáticas que
cubren cero, múltiplos, sobrantes, la frontera superior y rechazos exteriores.
El comprobador deriva independientemente los valores correctos y no admite
una expresión literal que sólo acierte el primer caso.

Work integró las tres declaraciones en la raíz del JSON español y conservó los
editores, las entradas, las referencias, los identificadores y las direcciones
históricas de progreso. La comprobación funcional en Windows y la aprobación
de las nuevas ejecuciones de CI son requisitos separados para cerrar la ficha:
la integración curricular por sí sola no acredita el aborto ni la reparación.

## Entrada del laboratorio

En un `cpp_compiler` curricular, el campo «Entrada» sólo aparece si el bloque declara `initialStandardInput` —también una cadena vacía, para permitir una actividad con EOF— o si algún caso de `outputCheck` declara una entrada no vacía. No se inspecciona el código del alumno para decidirlo. Sin esas declaraciones, la ejecución recibe entrada vacía y termina en EOF si intenta leer. Cualquier borrador de entrada anterior se conserva en almacenamiento, pero no se envía al programa mientras el campo no corresponda a esa actividad. El compilador libre del menú principal conserva su campo de entrada.

### Conservación histórica de Android

El guardado Android de entrada comparte el códec de texto escapado del código fuente, manteniendo la clave de borrador existente. Conserva espacios manuales, tabuladores y saltos finales sin depender de la indentación del XML de Android. Antes de guardar otro campo se protegen también las entradas y fuentes antiguas aún no abiertas, sin cambiar sus caracteres salvo reparación demostrada por una huella previa. Se utiliza una única escritura para todo el mapa: Android reescribe todas las preferencias y una protección parcial dejaría otras fuentes expuestas.

El antiguo serializador podía añadir cuatro espacios después de un LF final. Cuando una huella previa demuestra el original, la reparación exacta queda persistida. No hay menú de revisión ni recorte general de entradas. Work confirmó que los cuatro espacios de su prueba `Leo\n2\n    ` eran un defecto: ese caso concreto se corrige manualmente a `Leo\n2\n`, sin quitar el salto final ni cambiar otros datos. La intención del currículo no permite deducir la intención de cualquier borrador personal. Si aparece otro caso ambiguo, hay que indicar su ubicación y consultar a Work sobre la consigna, sin añadir decisiones al lector ni eliminar datos por su forma. Los guardados nuevos registran la huella del texto exacto introducido, incluidos espacios deliberados, tabuladores y saltos. El código fuente conserva sus huellas y marcas de tabulación anteriores; las reparaciones demostradas también se guardan definitivamente. Todo este formato es privado; Work no añade metadatos a las lecciones.

## Acreditación y avance en Windows

En Windows, el primer acierto completo de un `cpp_compiler` con `outputCheck` o
`compileCheck` se conserva hasta pulsar **Reiniciar código**. Editar, deshacer,
cambiar Entrada o volver a compilar no revoca ese logro. Cada nuevo intento se
comprueba de nuevo: un rechazo pedagógico posterior muestra «Incorrecto» y su
guía, separados del logro histórico. La reapertura conserva borrador, recibo y
última revisión válida; no fabrica una ejecución ni restaura una consola
transitoria. Un intento interrumpido antes de evaluarse no borra la revisión
anterior. Una nueva comprobación correcta sustituye la revisión de error.

La acreditación conserva el objetivo y una instantánea del programa que lo resolvió, con una prueba autenticada de su primera entrada y objetivo. Un antiguo `correct=true`, una predicción, un marcador de página o una salida sin ejecución satisfactoria no equivalen a esa prueba. En `outputCheck` el primer caso correctamente comprobado basta para avanzar; los casos posteriores son pruebas voluntarias, que pueden tener sus propios recibos sin modificar el logro ganado. Los antiguos sellos que exigían todos los casos siguen siendo válidos. Cambiar sólo Entrada o elegir otro ejecutable del mismo proyecto permite conservar los casos comprobados; editar fuentes o configuración antes del primer acierto invalida el intento incompleto y Undo no lo recupera. Una vez ganado el logro, sólo el reinicio expreso lo revoca. Los errores técnicos siguen siendo diagnósticos y no consumen corazones.

La propiedad raíz histórica sigue admitiéndose para conservar los assets:

```json
"compilerPuzzlesRequired": true
```

Su valor predeterminado de datos es `false`, pero no vuelve opcional un editor
curricular Windows: `compiler_requires_completion` requiere todos ellos.
Programación 1 ya declara la obligatoriedad y objetivos evaluables en
sus 371 editores españoles activos: 354 de stdout y 17 diagnósticos. Este censo
de metadatos no acredita su ejecución ni aceptación visual. Los tres editores
retirados de U1 conservan sus datos históricos y no generan obligaciones activas.
Los diagnósticos tienen `compileCheck`; no se les inventa stdout.

Cada compilador curricular activo con `outputCheck` o `compileCheck`
forma parte del progreso de su grupo histórico y debe quedar resuelto para
continuar o finalizar. En la misma página, el contenido posterior espera al
editor anterior. Un marcador posterior vuelve al primer objetivo pendiente,
conservando sus datos. El lector respeta además la lectura de la retroalimentación;
no confunde el logro del programa con haber escuchado su explicación. El acceso
a páginas teóricas también debe respetar el orden de páginas alcanzadas, sin
imponer escucha de audio como condición para estudiarlas.

El prototipo general sigue libre. Los bloques retirados conservan sus datos históricos y no crean controles, audio ni obligaciones activas. El contrato de Windows no cambia las reglas de ejecución o conservación Android descritas en los apartados anteriores.

## Rechazos de compilación intencionales en Windows

Diecisiete editores españoles admiten un objetivo independiente de stdout:
cinco de fuente única en U9 y doce multifichero en U6.

```json
"compileCheck": {
  "diagnostic": "const-member-call",
  "explanation": "La llamada requiere un objeto modificable. El programa fue rechazado antes de ejecutarse."
}
```

`diagnostic` selecciona uno de los contratos revisados. `explanation` es
texto de acierto opcional y no vacío, redactado por Work; no se muestra ni se
narra antes del acierto. No se infiere un objetivo de la consigna ni se activa
automáticamente en un asset.

| Lección de `datos_propios_y_estado_valido` | Editor estable | `diagnostic` | Modificación comprobada |
| --- | --- | --- | --- |
| `consultas_const_y_objetos_constantes` | `block_0006_0006` | `const-member-call` | Añadir `fija.cambiar_a(4,2);`, conservando `const`. |
| `valores_por_defecto_con_significado` | `block_0012_0008` | `explicit-copy-initialization` | Añadir `Contador no_permitido = 3;`, conservando `explicit`. |
| `valores_por_defecto_con_significado` | `block_0012_0011` | `explicit-argument-conversion` | Añadir `leer(3);`, conservando `explicit`. |
| `valores_por_defecto_con_significado` | `block_0014_0011` | `explicit-assignment` | Añadir `destino = 3;`, conservando `explicit`. |
| `argumentos_con_tipos_que_expresan_su_funcion` | `block_0006_0006` | `swapped-named-arguments` | Añadir `const Caja mal{Alto{4}, Ancho{3}};`, conservando el constructor `(Ancho, Alto)`. |

### Doce contratos multifichero de U6

Todos pertenecen a `funciones_y_contratos` y conservan `projectFiles` y
`sourceFiles` publicados, incluida la selección de fuentes que causa el fallo.

| Lección | Editor estable | `diagnostic` |
| --- | --- | --- |
| `declaraciones_y_firmas` | `block_0014_0002` | `u6-difference-unselected-definition` |
| `declaraciones_y_firmas` | `block_0015_0002` | `u6-difference-conflicting-return` |
| `declaraciones_y_firmas` | `block_0017_0002` | `u6-difference-missing-header` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0016_0002` | `u6-envelopes-missing-header` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0017_0002` | `u6-envelopes-missing-declaration` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0018_0002` | `u6-envelopes-unselected-definitions` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0019_0002` | `u6-envelopes-duplicate-definition` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0021_0002` | `u6-series-unknown-call` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0022_0002` | `u6-series-unselected-definitions` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0023_0002` | `u6-series-duplicate-definition` |
| `construir_un_proyecto_desde_su_interfaz` | `block_0013_0002` | `u6-shelf-unselected-definition` |
| `construir_un_proyecto_desde_su_interfaz` | `block_0014_0002` | `u6-shelf-conflicting-return` |

La incorporación exige el editor, la lección y el mapa v2 español revisados, sin
cambiar su fuente inicial completa. Ningún `compileCheck` admite `outputCheck`,
`initialStandardInput` o `cmake`. Los cinco de U9 no admiten archivos de proyecto;
los doce de U6 requieren los suyos. La identidad del diagnóstico no es un patrón
abierto para aprobar cualquier programa que falle.

Windows utiliza el MSVC educativo configurado. En los cinco contratos U9, cada intento válido compara un
control y un candidato nuevos, compilados solamente con `/c`, sin enlazarlos ni
ejecutarlos. El control debe compilar; el candidato debe producir el rechazo
semántico previsto, en el archivo y las líneas de la sentencia introducida.
Los códigos primarios observados deben corresponder a ese objetivo e intervalo.
Se preservan los diagnósticos completos sin comparar frases ni idioma de MSVC.
Cancelación, límite técnico, timeout, herramienta ausente, control inválido,
sintaxis ajena y enlace no acreditan el objetivo ni consumen corazones.

Los elementos léxicos conservan el programa inicial salvo la sentencia
solicitada dentro de `main`; espacios y comentarios no imponen una única
presentación. Esto comprueba cinco modificaciones cerradas, no cualquier
programa que falle. Quitar `const` o `explicit`, cambiar la interfaz o introducir
otro error no cumple la práctica. El editor sigue guardando los otros
borradores y mostrando sus diagnósticos, sin acreditarlos por ese fallo.

En U6 se compilan controles y candidatos nuevos por unidad de traducción. Los
contratos de enlace exigen que todas las unidades compilen y que el control
enlace; el candidato debe presentar el símbolo y el rechazo de enlace previstos.
Los de cabecera, declaración o tipo comprueban el rechazo en la unidad y fase
esperadas. Se autentican los archivos y las modificaciones admitidas por cada
receta; una fase anterior fallida, otro error, una captura truncada o un control
inválido no acreditan el objetivo. No se ejecutan estos programas rechazados ni
se comparan frases localizadas de MSVC.

La nueva prueba corresponde al objetivo y al programa capturados por la misma
pulsación; no usa una consola anterior ni un booleano antiguo. Un acierto completo
y su instantánea permanecen hasta **Reiniciar código**, separados del borrador
actual y de la última revisión. Cambiar o volver a compilar no revoca el logro;
un intento posterior puede mostrar un veredicto incorrecto y su guía. Su acreditación propia
controla referencias, avance y detención/continuación del lector cuando
corresponde.

La importación Android conserva datos y borradores sin fabricar pruebas Windows.
La procedencia nativa impide adoptar después un asset que retire o cambie un
objetivo ya incorporado; rechazarlo conserva los datos. Work integra los diecisiete
contratos y completa presentación, interacción y escucha por separado. Esta
capacidad no amplía el motor Android conservado.

## Retornos intencionales y representación CRT

En U12L13, `compartir_flujos_sin_acoplar_el_calculo`, `block_0005_0002`,
el objetivo revisado exige entrada vacía, stdout `Entrada invalida|9:4` sin LF
final y `exitCode: 1`. En U12L26,
`construir_un_programa_de_carga_y_guardado`, `block_0004_0017`, exige igualmente
entrada vacía, stdout `Error al abrir salida` sin LF final y retorno `1`. La
incorporación de estos objetivos se autentica contra sus editores completos
publicados; no admite cambiar el programa para facilitar su evaluación.

En U12L20, `clasificar_caracteres_y_normalizar_una_copia`, el primer editor
`block_0005_0004` conserva las dos alternativas revisadas de LF/CRLF de la CRT
Windows y todos sus controles literales. Es una revisión cerrada de ese objetivo,
no una normalización general: los demás stdout siguen comparándose byte a byte.
La guía pública y su marcado pueden editarse sin cambiar esos bytes ejecutables.

## Autoría y recuperación de guías públicas

`outputCheck.cases[i].explanation`,
`outputCheck.cases[i].reviewFeedback[j].text` y `compileCheck.explanation` son
texto público condicional. No se leen antes de obtener el resultado pertinente;
`outputs`, `standardInput`, `target`, `standardError`, `exitCode` y `diagnostic`
siguen siendo datos del contrato, nunca texto del guion curricular.

Work puede revisar las guías mediante la cadena editorial v2 sin cambiar el
programa, los casos, sus condiciones o las identidades. La proyección de guías
retira únicamente `explanation` y `reviewFeedback` de los casos y `explanation`
del diagnóstico al comparar el contrato ejecutable; no retira `compileCheck`.
Esto no autoriza revisiones de respuestas ni rebaja las guardas históricas.

Un recibo completo autenticado de un objetivo de un solo caso o de diagnóstico
puede mostrar su explicación vigente al reabrir, sin recompilar ni inventar otro
acierto. Una última revisión incorrecta conserva prioridad. Los objetivos de
varios casos mantienen su retroalimentación acreditada, sin elegir arbitrariamente
la explicación de uno de ellos. Un cambio de guía puede exigir volver a leer
esa explicación desde su inicio; no acredita escucha ni duplica marcadores.

Los rechazos nuevos usan reglas privadas v2 que no dependen de las explicaciones
de acierto. Editar sólo ese texto conserva la prioridad y la procedencia de un
rechazo válido, también en diagnósticos. Sus condiciones ejecutables y las
reglas/textos de error siguen comprobándose completos. Los registros v1 se
aceptan únicamente cuando sus reglas originales completas aún coinciden; no se
reinterpretan como v2 ni se reescriben. Si cambia una guía de error o su selector,
no se deduce otra guía desde el texto anterior: hace falta una nueva comprobación
para registrar la revisión correspondiente.

Las guías admiten el mismo `textSemantics` raíz que los demás textos públicos.
La referencia estable del bloque y el JSON Pointer identifican su campo exacto;
la igualdad de dos frases no decide el propietario. La anticipación prepara sólo
las guías explícitamente marcadas, con el mismo veredicto, separador y proyección
que la lectura inmediata, sin exponerlas en pantalla antes del intento. Véase
el contrato completo en [STABLE_LESSON_STATE.md](curriculum/STABLE_LESSON_STATE.md#autoría-explícita-de-texto-público-en-windows).

## Ampliaciones revisadas con conservación en Windows

La extensión `stateLayout.compilerRevisions` admite las diez ampliaciones
cerradas de `CPP_CASOS_ADICIONALES_CON_PROGRESO`; su contrato de autoría está en
`docs/curriculum/STABLE_LESSON_STATE.md`. No es una excepción general para editar
los esperados de un compilador publicado.

En U11L7 se conservan los 19 casos originales y se añaden dos. Una prueba anterior
válida puede reutilizarse exclusivamente para el mismo caso del mismo programa
capturado; no acredita ninguno de los dos casos nuevos. Una edición de fuentes
antes de completar el objetivo ampliado mantiene las reglas de invalidación de
intentos. En U11L8 y U11L10–L16, incluidos ambos editores de L15, el caso único exige un stdout ampliado: el logro
anterior se conserva como histórico, separado de la nueva comprobación completa.
Ni una salida anterior ni un booleano antiguo aprueban silenciosamente el objetivo
ampliado. El botón Reiniciar elimina el intento activo y evita que una prueba
histórica vuelva a proyectarse como acreditación corriente.

Las nueve ampliaciones de stdout modifican simultáneamente la referencia estática
posterior: únicamente añaden sus llamadas revisadas antes del `return 0;` final
de `main`. El mapa conserva el bloque anterior completo en el archivo
`references` de su revisión. No se permite ampliar el esperado dejando una
referencia incompatible ni usar este contrato para cambiar otros programas.
Sólo el segundo editor U11L15 amplía además su inicial exacto con la quinta
llamada revisada. Los borradores anteriores no se sustituyen automáticamente:
el alumno conserva su programa y dispone del fragmento adicional dentro de la
misma lección. Reiniciar es la acción que adopta el inicial nuevo y limpia los
recibos de ese objetivo, sin afectar al primero.

U11L12 añade el rechazo del parcial `11*10/2;`; U11L13 añade ese contraste y la
novena agrupación; U11L14 añade `?z;4;`, que exige descartar sin retokenizar y
recuperar la operación siguiente. Conservan los stdout anteriores completos y
añaden exclusivamente los sufijos revisados de 34, 62 y 49 bytes LF. Son baterías
dentro del programa con stdin vacío: un logro anterior completo conserva su
1/1 histórico y deja la nueva comprobación en 0/1. Los estados sin comprobar
siguen sin crédito; los informes impresos no son recibos individuales.
El helper prepara propuestas sin escribir los assets; Work integra conjuntamente
consigna, esperado y referencia después de la ampliación técnica.

Las tres ampliaciones restantes son U11L15 primero, de catorce a diecinueve
informes; U11L15 segundo, de cuatro a cinco; y U11L16, de nueve a doce. También
son una ejecución por batería, no un recibo por línea impresa. Sus archivos,
llamadas y referencias exactas se detallan en el contrato de autoría enlazado;
los logros anteriores se conservan como históricos sin acreditar esas ampliaciones.

La ampliación no crea otro editor, no copia borradores ni altera las traducciones.
Work sigue siendo responsable de integrar las consignas y referencias revisadas;
las pruebas del contrato no equivalen a su aceptación visual o escucha.

## Actividad integrada

En `programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json`,
el editor estable `block_0004_0002` comprueba `Caja recibida` en español, sin salto
final ni comillas; la referencia aparece después del intento. No se deduce el
esperado de la predicción del programa inicial ni se cambia esa respuesta.
Los programas retirados conservan sus datos, sin controles ni obligaciones
activas. El segundo editor conserva su dependencia real del primero. La ausencia
de `outputCheck` sólo conserva exploración libre en el Laboratorio independiente;
un editor curricular sin evaluador continúa pendiente técnicamente.

## Requisitos generales de constantes nombradas

Un bloque `cpp_compiler` existente que ya dispone de `outputCheck` puede
recibir, desde la raíz de la lección española con `stateLayout` v2, una
declaración `compilerNamedRuleChecks`. No se cambian el bloque original, sus
respuestas, `initialCode`, `projectFiles` ni las direcciones históricas.

```json
{
  "compilerNamedRuleChecks": [
    {
      "activity": "block_0002_0004",
      "path": "componente/src/cajas.cpp",
      "rules": [
        {
          "function": "cajas_para",
          "value": 4,
          "purposeWords": ["capacidad", "plaza", "cupo"],
          "minUses": 2
        }
      ]
    }
  ]
}
```

El objeto anterior es un **ejemplo de la extensión raíz**, no un JSON
curricular publicado. `activity` identifica un editor original; `path`
identifica un archivo C++ ya presente en el proyecto. Para editores de un solo
archivo, `path` es la cadena vacía. `function` identifica la función;
`value` fija el acuerdo entero; `purposeWords` permite varios nombres
de propósito; `minUses` exige un mínimo de referencias de la constante.

El lector exige una definición de la función, una constante local `int` de
valor correspondiente, un identificador de propósito y referencias reales al
resultado, incluso cuando se devuelve mediante variables intermedias.
Admite las grafías equivalentes `const int` e `int const`; no exige un
orden exclusivo de los especificadores de tipo. Los resultados parciales
pueden conservarse en variables intermedias `const int`, `int const`,
`const auto` o `auto const`, siempre que esas referencias lleguen al
resultado de la función. Los comentarios, las cadenas y las constantes
declaradas pero no utilizadas no
acreditan el objetivo. La constante puede inicializarse también con
aritmética entera acotada sobre literales decimales, incluidos paréntesis,
signos y los operadores `+`, `-`, `*`, `/` y `%`. Se respetan
precedencia y división entera; se rechazan desbordamiento, divisor cero
y literales de base ambigua para el comprobador, sin interpretar macros
ni evaluar expresiones C++ arbitrarias. El analizador sólo admite
directivas `#include` en el archivo evaluado: una función excluida con
`#if 0`, una macro o una declaración generada no constituyen evidencia
fuente acreditable. Los comentarios que mencionan esas directivas siguen
siendo texto ordinario.

Las sondas independientes compilan el mismo código en otro contexto.
Por ello, el requisito de fuente no admite dependencias activas de
marcadores que puedan identificar esa segunda compilación, como
`__LINE__`, `__FILE__`, `__COUNTER__`, `__DATE__`, `__TIME__` o
`std::source_location`. Las menciones dentro de comentarios y literales
de texto no constituyen tal dependencia y siguen permitidas.
Esta restricción evita que una función detecte la sonda y devuelva allí
un resultado distinto del programa del estudiante; no sustituye las
pruebas conductuales ni afirma detectar todos los posibles trucos de
diferenciación del entorno.

Además, el motor comprueba la compilación, la ejecución y el primer caso
válido de `outputCheck` antes de registrar el acierto. Los casos
posteriores permanecen optativos.

Los usos algebraicamente neutros más directos, como `capacidad -
capacidad`, `capacidad / capacidad` o multiplicaciones por expresiones
enteras constantes de valor cero, no cuentan como referencias útiles.
Esto incluye factores cero que anulan expresiones completas con
paréntesis, incluso anidadas: `0 * (capacidad + capacidad)` o
`(capacidad + capacidad) * (2 - 2)` no acreditan dos referencias
productivas. La comprobación conserva las referencias reales que
acompañen a una expresión neutra. Si una variable intermedia derivada de la constante
se sobrescribe con un cálculo que ya no depende de ella, ese resultado
descartado no acredita el `return`. Una asignación posterior que
vuelva a utilizar la constante sí puede establecer la dependencia.
El mínimo `minUses` se cuenta sobre las referencias útiles que llegan
al valor devuelto, directamente o a través de variables locales; una
operación auxiliar que no llega al resultado no incrementa el mínimo.
Tampoco se acreditan las referencias de un `return` situado directamente
en una rama `if (false)`, `if (0)` o `if constexpr (false)`, ni de
un `return` posterior a otro que termina definitivamente esa vía.
En condiciones constantes como `if (true)` o `if (1)` se comprueba
el retorno inmediato ejecutado; una guarda condicional de entrada
(por ejemplo, `if (piezas <= 0) return 0;`) no impide analizar el
cálculo posterior en la vía ordinaria. Cuando `if/else` devuelve
directamente en ambas ramas, las dos deben satisfacer `minUses`;
no basta con escribir la constante en una rama y dejar cifras fijas en
la otra. Si sólo hay retornos condicionales cuya cobertura no puede
probar este analizador acotado, no se acredita el requisito de fuente.
Esta detección es acotada:
no reemplaza la compilación, la ejecución ni las sondas independientes
que correspondan a cada actividad.

**Límites de la primera integración:** el análisis léxico de usos no
demuestra por sí solo que todas las operaciones sean correctas o que otra
parte del programa no fabrique una salida. Las prácticas que requieren
conservar otros archivos, ampliar baterías, demostrar dependencias
efectivas o comprobar funciones con entradas distintas deben añadir sus
sondas y restricciones pertinentes antes de recibir acreditación completa.
La ficha técnica sigue abierta hasta esa verificación.

Modificar por primera vez `compilerNamedRuleChecks` crea un objetivo
nuevo, con huella de recibo distinta de los logros históricos de salida.
Una vez publicado, el autor no puede retirarlo ni modificarlo
silenciosamente. El validador Python, el cargador Windows, el comprobador
de ejecución y las pruebas reales deben coincidir. Los archivos JSON los
integra Work o GPT Work cuando la capacidad técnica esté acreditada.

### Sonda independiente en editores C++ de archivo único

En un editor de un solo archivo, el alumno también puede modificar `main`
y fabricar las cinco líneas esperadas sin llamar a sus funciones. Una
declaración optativa `executionProbe` permite comprobar directamente esas
funciones con casos adicionales, sin confiar en la salida publicada:

```json
"executionProbe": {
  "harness": "",
  "header": "",
  "cases": [
    {"function": "precio_total", "input": 11, "expected": 77},
    {"function": "alcanza_minimo", "input": 4, "expected": 0},
    {"function": "alcanza_minimo", "input": 5, "expected": 1}
  ]
}
```

Los campos `harness` y `header` deben estar vacíos en esta modalidad;
el editor continúa teniendo `path` vacío y no utiliza CMake.
El motor compila el programa entregado para la comprobación visible y
ejecuta una segunda compilación de su código con un `main` privado
generado por el motor. El `main` original se renombra sólo en esa
segunda copia, sin modificar el borrador del alumno. La prueba llama
directamente a las funciones con los argumentos y esperados declarados
por Work. La ejecución debe terminar correctamente, conservar vacía la
salida estándar y emitir únicamente el testigo interno final en stderr,
después de la última comparación. Ese testigo distingue una sonda
completada de una función que ejecutó `exit(0)` antes de comprobar
todos los casos. Se verifica también la procedencia criptográfica del
código exacto presentado.

El nuevo requisito se incorpora a la huella del objetivo y no revalida
recibos anteriores de salida. La sonda debe cubrir todas las funciones
nombradas; se rechazan llamadas repetidas o nombres que inyecten código.
Un único intento correcto en el editor sigue bastando para avanzar.
No se exige al estudiante ejecutar manualmente cada caso adicional.

### Archivos adicionales autorizados en proyectos CMake

De manera predeterminada, sólo puede cambiar el archivo C++ identificado
por `path`. El resto de los archivos originales, incluido el cliente,
la batería de pruebas y `CMakeLists.txt`, debe conservarse literalmente.
Esto evita sustituir el comprobador por un programa que imprima la salida
esperada sin ejecutar los cálculos del estudiante.

Cuando una práctica pide también actualizar la cabecera y ampliar la
batería, el autor puede declarar `editablePaths` con rutas de archivos
existentes y diferentes de `path`:

```json
"editablePaths": [
  "componente/publico/envio/cajas.h",
  "pruebas/cajas_casos.cpp"
]
```

La ausencia de `editablePaths` equivale a una lista vacía. Una ruta
nueva, repetida, inexistente o ajena al proyecto es inválida. Estas
excepciones forman parte de la huella del objetivo; no alteran los IDs
históricos, pero un recibo obtenido con permisos diferentes no puede
acreditar el objetivo actualizado. Los nuevos archivos autorizados
todavía deben compilar, ejecutarse y cumplir el primer caso de salida.

### Sonda independiente cuando el comprobador es editable

Si `editablePaths` incluye un archivo C++ que puede reemplazar el
comprobador, **es obligatorio** incluir `executionProbe`. El ejecutor
genera un segundo `.cpp` de prueba sin copiar instrucciones del estudiante
y lo compila con MSVC usando la misma biblioteca del proyecto. El archivo
de pruebas original del alumno permanece intacto; sólo se sustituye una
copia en el directorio de ejecución aislado.

```json
"executionProbe": {
  "harness": "pruebas/cajas_casos.cpp",
  "header": "envio/cajas.h",
  "cases": [
    {"function": "cajas_para", "input": 6, "expected": 1},
    {"function": "cajas_para", "input": 7, "expected": 2},
    {"function": "cajas_para", "input": 19, "expected": 4},
    {"function": "llega_a_tiempo", "input": 4, "expected": 1},
    {"function": "llega_a_tiempo", "input": 5, "expected": 0}
  ]
}
```

La sonda debe sustituir el archivo de código fuente que participa en el
objetivo ejecutable CMake, referenciar una cabecera con ruta segura y cubrir
cada función solicitada con datos independientes. Las entradas y salidas
enteras son datos declarados por el currículo, no cálculos obtenidos de la
función evaluada. Un programa que imprime el resultado esperado pero
implementa mal las funciones no puede ganar el recibo nuevo. El testigo
final también se exige en esta modalidad, sin saltos de línea para no
confundir las convenciones LF y CRLF de los compiladores. El código
de salida cero por sí solo no acredita que se hayan ejecutado todas las
comparaciones; un testigo estático tampoco sustituye el aislamiento
frente a código deliberadamente hostil.

Para objetivos con `executionProbe`, la comprobación de fuente rechaza
referencias ejecutables a funciones capaces de finalizar anticipadamente
el proceso o el hilo, como `exit`, `_Exit`, `quick_exit`,
`ExitProcess` y `TerminateProcess`. También rechaza tomar su dirección
para invocarlas mediante un alias. La restricción se aplica a las
fuentes sometidas a la sonda, no a cualquier actividad C++; los
comentarios y literales de texto que mencionen esos nombres no
constituyen llamadas. Es una protección adicional contra falsificar
el testigo y terminar con código cero; no convierte al ejecutor en una
barrera de seguridad contra programas hostiles ni impide todas las
formas posibles de finalización indirecta.

Se conservan por separado el resultado público, el código original, la
compilación de la sonda, el programa/objetivo y las huellas de ambas
ejecuciones. La huella de acreditación de las sondas pasa a la versión
`v2`: un recibo anterior basado sólo en terminación cero no se
interpreta como prueba de finalización nueva. Las direcciones del
borrador y las identidades históricas del ejercicio no cambian. Los demás casos del `outputCheck` continúan siendo optativos;
la sonda del primer caso forma parte de su propio criterio de corrección.

Para los editores de un solo archivo, `path` y `editablePaths` deben
ser vacíos; la prueba sigue evaluando el código que el estudiante editó.

### Cabeceras de edición limitada a comentarios

Las actividades que piden corregir una descripción, sin alterar firmas ni
la interfaz C++, pueden añadir `commentOnlyPaths`:

```json
"commentOnlyPaths": ["componente/publico/envio/cajas.h"]
```

Las rutas deben estar incluidas en `editablePaths` y ser cabeceras `.h`,
`.hpp` o `.hxx` preexistentes. Se admite corregir un comentario como
«4 plazas» por «6 plazas», pero no se admite cambiar una declaración,
introducir `#define`, redirigir una llamada ni modificar literales. El
comprobador léxico compara el código, incluyendo directivas, y descarta
únicamente las diferencias de comentarios. Rechaza cambios que involucren
macros sensibles a las líneas del código.

El alcance y los permisos forman parte de la identidad del objetivo
evaluado: no se reutilizan aciertos de otra revisión. Es una capacidad
general, no una modificación automática del material histórico.

## Comprobación de la estructura CMake generada (raíz de la lección)

Los editores históricos pueden recibir un objetivo estructural complementario sin
modificar su `cpp_compiler`, los casos `outputCheck` ni sus direcciones. Work declara
`compilerCmakeGraphChecks` en la raíz de una lección española con `stateLayout`
versión 2. Un ejemplo, referido a las rutas originales de un proyecto educativo, es:

```json
"compilerCmakeGraphChecks": [
  {
    "activity": "block_0004_0004",
    "project": "Preparacion",
    "targets": [
      {
        "name": "revisar",
        "type": "EXECUTABLE",
        "sources": ["main.cpp"],
        "exactSources": true,
        "linksTo": ["faltantes"]
      },
      {
        "name": "faltantes",
        "type": "STATIC_LIBRARY",
        "sources": ["faltantes.cpp"],
        "exactSources": true
      }
    ]
  }
]
```

El ejemplo muestra un contrato técnico disponible, no una declaración añadida
automáticamente a la lección. Los nombres de objetivos y las rutas deben existir
en la autoría real; la biblioteca se identifica por su objetivo y su artefacto
generado, no por una coincidencia de nombres dentro de `CMakeLists.txt`.

El motor consulta **CMake File API codemodel v2** de la configuración realmente
generada por CMake. Separa fuentes compiladas, tipo de objetivo, dependencia de
construcción y fragmentos de enlace. `add_dependencies()` por sí solo no
acredita `target_link_libraries()`. Una ejecución que coincida en stdout tampoco
acredita esta organización si la estructura declarada no coincide. Sólo una
compilación y ejecución de la revisión actual pueden producir evidencia de
la nueva rúbrica. El archivo histórico, el editor y las respuestas antiguas
permanecen intactos; un acierto anterior de salida no satisface automáticamente
la nueva comprobación. Para avanzar sigue bastando el primer caso correcto
que cumpla todos sus criterios vigentes. Los demás casos permanecen opcionales.

`project` es opcional. Cada objetivo requiere `name` y `type`
(`EXECUTABLE` o `STATIC_LIBRARY`); `sources`, `linksTo` y
`exactSources` son opcionales. Las fuentes declaradas son rutas originales
de `projectFiles`; `exactSources` exige que no existan fuentes de
compilación adicionales. `linksTo` referencia objetivos de biblioteca
estática también declarados, y se acredita mediante dependencia y enlace del
artefacto real. `minimumCmakeVersion` es una propiedad opcional del
propietario, por ejemplo `"minimumCmakeVersion": "3.20"`. Comprueba la versión
mínima declarada por `cmake_minimum_required(VERSION)` en el directorio raíz,
según el dato `minimumCMakeVersion.string` de **CMake File API codemodel v2**.
Se comparan componentes numéricos: `3.20` y `3.20.0` son equivalentes, pero
`3.19` y `3.21` no. No se deduce esta versión de la instalación de CMake,
ni se sustituye por una búsqueda textual del código fuente. Para exigir los requisitos explícitos de los objetivos, un elemento de
`targets` puede declarar `"explicitCxx20": true` y
`"explicitCxxExtensionsOff": true`. Estas propiedades optativas obligan a
comprobar que **CMake ejecutó** una declaración de C++20 y una desactivación
de extensiones sobre el objetivo, además de construirlo y ejecutar su primer
caso correctamente. El perfil Windows puede aportar C++23 y extensiones
desactivadas, pero eso **no** satisface las propiedades del alumno.

En la configuración autenticada, el motor guarda una traza JSON privada de
CMake limitada a los archivos CMake originales del proyecto. Sólo acepta
órdenes ejecutadas y argumentos expandidos: `target_compile_features(objetivo
PRIVATE cxx_std_20)`, `set_target_properties(objetivo PROPERTIES
CXX_STANDARD 20)` y `set_property(TARGET objetivo PROPERTY
CXX_STANDARD 20)` son formas equivalentes para el mínimo. Para extensiones
se admiten `set_target_properties(... PROPERTIES CXX_EXTENSIONS OFF)` y
`set_property(TARGET ... PROPERTY CXX_EXTENSIONS OFF)`, con los valores
falsos admitidos por CMake. Una orden posterior que active las extensiones
invalida el requisito. Las rutas, fuentes y enlaces siguen contrastándose
mediante File API. No se evalúan solamente las cadenas impresas por el alumno,
ni se concede crédito desde una configuración anterior sin traza autenticada.

La traza está limitada al proyecto y a los objetivos declarados. Esta primera
capacidad no certifica todavía asignaciones por ámbito de directorio,
propagación por interfaces de otros objetivos ni cualquier macro dinámica que
no pueda atribuirse con certeza al objetivo. En esos casos se necesita una
comprobación adicional en lugar de conceder un acierto supuesto.

Cada contrato ya publicado permanece inmutable. Es posible añadir
nuevos propietarios al final de la declaración mediante una transición
autenticada, sin modificar ni reordenar los anteriores.

Esta versión no acredita todavía todo el programa de Programación 2:
requisitos públicos/privados de encabezados, opciones, lista de objetivos,
avisos del compilador, cronología de reconstrucciones y prácticas abiertas
necesitan sus propias comprobaciones. No deducirlas de este grafo.

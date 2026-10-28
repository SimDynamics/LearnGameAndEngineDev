# Comprobación opcional de salida en el Laboratorio

`cpp_compiler` admite `outputCheck` opcional. Sin él, el botón existente muestra «Compilar y ejecutar» y conserva la exploración libre sin evaluación. Con él, ese mismo botón muestra «Compilar, ejecutar y comprobar»: una pulsación construye, ejecuta y comprueba la salida nueva. No se añade un segundo botón. Work incorpora los casos a las actividades después de verificar su objetivo; el motor no deduce respuestas de la prosa, de una predicción contigua ni del código de referencia.

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

## Contrato

- `cases` es un array no vacío. Cada caso declara `standardInput` de tipo texto (puede ser vacío) y `outputs`, una lista no vacía de salidas completas y distintas. Una salida vacía también es válida.
- La comparación de **stdout siempre es literal**: espacios, tabuladores, CR y LF se conservan. Para permitir exclusivamente un LF final, declarar ambas alternativas, por ejemplo `"Informe\nZona Este\nFin"` y `"Informe\nZona Este\nFin\n"`. No se recortan los extremos.
- `inputComparison` es `exact` por defecto. `tokens` se usa sólo cuando la consigna permite separar los mismos datos con espacios en blanco ASCII (` `, TAB, LF, CR, VT, FF). Por ejemplo, un caso `"Leo 2"` también identifica `"Leo\n2\n"`. No normaliza stdout ni admite espacios Unicode como si fueran separadores de C++.
- Los casos deben identificar inequívocamente la ejecución bajo el modo seleccionado. En un proyecto CMake, cada caso admite `target` opcional: el nombre exacto y sensible a mayúsculas del ejecutable que debe producir esa salida. Dos ejecutables pueden compartir entrada y tener salidas distintas. Se usa el objetivo realmente capturado al pulsar ejecutar, sin deducirlo del código ni añadir un selector curricular de respuestas.
- Sin `target`, se conserva el contrato anterior: el caso se identifica por entrada y es aplicable a cualquier objetivo del bloque. No se permite mezclar un caso global y otro con `target` para la misma entrada, ni repetir un mismo par objetivo/entrada. No existe precedencia implícita entre ellos. `target` sólo es válido en bloques con `cmake` y acepta el mismo formato que `cmake.target`: `[A-Za-z_][A-Za-z0-9_.+-]{0,127}`. No obliga a que sea el objetivo inicial: el alumno puede cambiar de ejecutable y reconstruirlo.
- `explanation` es texto opcional no vacío de acierto. `reviewFeedback` es un array opcional, también puede ser vacío: cada regla declara `outputs` y `text` no vacío. Sólo una coincidencia literal con esas salidas incorrectas permite mostrar su orientación. Las condiciones no pueden solaparse entre reglas ni con las salidas correctas.
- En Windows, cada caso admite además `standardError: "nonBlank"` para exigir información adicional en stderr durante una ejecución correcta. Se detalla más adelante; omitirlo mantiene exactamente el contrato de stdout anterior.
- Sin explicación se muestra sólo el veredicto. Un error no previsto muestra sólo «Incorrecto». No se infiere una causa, no se expone la salida esperada y los campos internos no entran en el guion normal de la lección. Se rechazan tipos y propiedades desconocidos como errores de configuración, no como respuestas incorrectas.

## Ejecución y conservación

«Compilar, ejecutar y comprobar» utiliza únicamente la ejecución nueva terminada correctamente del código/proyecto, compilador, configuración y entrada capturados por esa pulsación. Nunca comprueba una salida que ya estuviera en la consola. Compilación, enlace, configuración, ejecución fallida o límites técnicos conservan sus diagnósticos y dejan la comprobación sin resultado evaluable. Los avisos de compilación y stderr se conservan sin reescribirlos.

En Windows, las herramientas de compilación y enlace utilizan una consola privada
oculta configurada en UTF-8, tanto en fuentes simples como en proyectos directos,
CMake y rechazos intencionales. Así se conservan los acentos del diagnóstico y
de sus rutas sin confundir la salida OEM de MSVC con texto ANSI o UTF-8. La
captura guarda los bytes emitidos por cada herramienta. La ejecución del programa
mantiene separados y exactos sus canales binarios de entrada, stdout y stderr,
incluidos NUL y bytes que no formen texto UTF-8. La consola privada pertenece al
intento; no cambia la configuración de Windows ni la del terminal del usuario.

Los laboratorios CMake conservan sus controles explícitos de fases. Con `outputCheck`, la acción combinada configura si hace falta, construye el objetivo vigente mediante CMake y ejecuta un programa nuevo antes de comparar; se conservan los diagnósticos de cada fase. Un resultado restaurado por el observador del proyecto puede volver a mostrar su consola, pero nunca genera una comprobación. Sin contrato se conserva el funcionamiento de sus controles de configuración, construcción y ejecución.

Editar una fuente, cabecera, selección de fuentes, entrada, configuración CMake o compilador invalida la ejecución y el veredicto. Deshacer o rehacer no recupera una ejecución antigua: hay que ejecutar de nuevo. Cambiar sólo la pestaña de archivo, sin alterar el proyecto, conserva la identidad del programa. Reiniciar invalida incluso si los bytes ya coinciden con el original. Un resultado tardío de otra revisión no habilita la comprobación. Al reabrir se conservan los borradores existentes y se requiere otra ejecución; no se guarda un historial paralelo ni se restaura un veredicto sin ejecutar.

Estas comprobaciones **no consumen corazones**, no completan puzzles, no responden la predicción vecina y no alteran los requisitos de avance. Una coincidencia de stdout admite diferentes implementaciones y no demuestra por sí sola que se hayan utilizado las variables o la estrategia solicitadas.

Con el lector activado, todo el veredicto visible —incluida una respuesta sin explicación— se lee mediante la revisión temporal ya existente. Comparte el formato de acierto/error, los colores, la sintaxis técnica y el sombreado por palabras de las demás retroalimentaciones. Se conserva el mismo reproductor, las posiciones de las tres páginas, pausa con liberación y controles. Al terminar un acierto, se retoma la posición de la página si estaba reproduciéndose o esperando un ejercicio, respetando sus límites. Si estaba pausada, permanece pausada y la siguiente pulsación de reproducir permite continuar; una página ya finalizada no reinicia. El error conserva la detención y puede volver a escucharse con retroceso y reproducir. Esta lectura no acredita retroalimentación curricular pendiente ni desbloquea otros ejercicios. Consola y código conservan sus exclusiones.

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

La salida del cliente nunca acredita la batería. Un objetivo o entrada sin caso mantiene la ejecución disponible, sin veredicto ni aceptación implícita de otro consumidor. Cambiar el objetivo invalida la comprobación anterior, incluso si todos los archivos siguen iguales; volver al objetivo anterior requiere ejecutar de nuevo. La reapertura conserva el objetivo y los archivos, pero no recupera un veredicto. Configuración, generación, construcción y ejecución conservan sus diagnósticos y no consumen corazones. Las explicaciones y orientaciones opcionales pertenecen a su caso, sin revelar el resultado de otro ejecutable.

Este contrato prepara las siete prácticas registradas en `CPP_COMPROBACION_POR_OBJETIVO_CMAKE`. Su incorporación curricular corresponde a Work: el motor y las pruebas no añaden `outputCheck` a esos assets.

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

## Entrada del laboratorio

En un `cpp_compiler` curricular, el campo «Entrada» sólo aparece si el bloque declara `initialStandardInput` —también una cadena vacía, para permitir una actividad con EOF— o si algún caso de `outputCheck` declara una entrada no vacía. No se inspecciona el código del alumno para decidirlo. Sin esas declaraciones, la ejecución recibe entrada vacía y termina en EOF si intenta leer. Cualquier borrador de entrada anterior se conserva en almacenamiento, pero no se envía al programa mientras el campo no corresponda a esa actividad. El compilador libre del menú principal conserva su campo de entrada.

El guardado de entrada comparte el códec de texto escapado del código fuente, manteniendo la clave de borrador existente. Conserva espacios manuales, tabuladores y saltos finales sin depender de la indentación del XML de Android. Antes de guardar otro campo se protegen también las entradas y fuentes antiguas aún no abiertas, sin cambiar sus caracteres salvo reparación demostrada por una huella previa. Se utiliza una única escritura para todo el mapa: Android reescribe todas las preferencias y una protección parcial dejaría otras fuentes expuestas.

El antiguo serializador podía añadir cuatro espacios después de un LF final. Cuando una huella previa demuestra el original, la reparación exacta queda persistida. No hay menú de revisión ni recorte general de entradas. Work confirmó que los cuatro espacios de su prueba `Leo\n2\n    ` eran un defecto: ese caso concreto se corrige manualmente a `Leo\n2\n`, sin quitar el salto final ni cambiar otros datos. La intención del currículo no permite deducir la intención de cualquier borrador personal. Si aparece otro caso ambiguo, hay que indicar su ubicación y consultar a Work sobre la consigna, sin añadir decisiones al lector ni eliminar datos por su forma. Los guardados nuevos registran la huella del texto exacto introducido, incluidos espacios deliberados, tabuladores y saltos. El código fuente conserva sus huellas y marcas de tabulación anteriores; las reparaciones demostradas también se guardan definitivamente. Todo este formato es privado; Work no añade metadatos a las lecciones.

## Acreditación y avance en Windows

En Windows, el primer acierto completo de un `cpp_compiler` con `outputCheck` se conserva hasta pulsar **Reiniciar código**. Editar, deshacer, cambiar Entrada, volver a compilar o producir después un diagnóstico no revoca ese logro ni sustituye su retroalimentación por «Incorrecto». La consola sigue mostrando cada ejecución real y el editor permanece disponible. La reapertura conserva el acierto y el borrador actual de forma independiente.

La acreditación conserva el objetivo y una instantánea del programa que lo resolvió, con una prueba por cada entrada/objetivo declarado. Un antiguo `correct=true`, una predicción, un marcador de página o una salida sin ejecución satisfactoria no equivalen a esa prueba. Si hay varios casos, deben comprobarse todos con el mismo programa. Cambiar sólo Entrada o elegir otro ejecutable del mismo proyecto conserva los casos ya comprobados; editar las fuentes o la configuración antes de completar el conjunto los invalida y Undo no los recupera. Los errores técnicos siguen siendo diagnósticos y no consumen corazones.

La lección puede activar el contrato de avance con la propiedad raíz opcional:

```json
"compilerPuzzlesRequired": true
```

Su valor predeterminado es `false` para mantener compatible el recorrido publicado. Work activa esta propiedad junto con el retiro compatible de las tres ejecuciones originales redundantes de U1; así el cambio no convierte esas repeticiones en barreras nuevas. No se modifican automáticamente los assets ni se inventan criterios para los compiladores sin `outputCheck`: los ejercicios de diagnóstico necesitan un contrato evaluable propio.

Al activar el contrato, cada compilador activo con `outputCheck` forma parte del progreso de su grupo histórico y debe quedar resuelto para continuar, saltar hacia delante o finalizar. En la misma página, el contenido posterior se habilita al resolver el editor anterior. Un marcador posterior vuelve al primer objetivo anterior pendiente, conservando sus datos. El lector se detiene en la consigna/editor pendiente; lee el error y se detiene, o lee el acierto y su explicación antes de continuar. Una pausa manual permanece pausada y los desplazamientos del audio no acreditan fragmentos que no se escucharon.

El prototipo general sigue libre. Los bloques retirados conservan sus datos históricos y no crean controles, audio ni obligaciones activas. El contrato de Windows no cambia las reglas de ejecución o conservación Android descritas en los apartados anteriores.

## Rechazos de compilación intencionales en Windows

Cinco editores españoles de U9 admiten un objetivo independiente de stdout:

```json
"compileCheck": {
  "diagnostic": "const-member-call",
  "explanation": "La llamada requiere un objeto modificable. El programa fue rechazado antes de ejecutarse."
}
```

`diagnostic` selecciona uno de los cinco contratos revisados. `explanation` es
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

Se incorpora únicamente al editor publicado correspondiente y a su mapa v2
español, sin cambiar su fuente inicial completa. No se combina con `outputCheck`,
`initialStandardInput`, `projectFiles`, `sourceFiles` ni `cmake`: esta práctica
no pide salida, entrada estándar ni un fallo de enlace. El resto de los
compiladores mantiene su contrato anterior.

Windows utiliza el MSVC educativo configurado. Cada intento válido compara un
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

La nueva prueba corresponde al objetivo y al programa capturados por la misma
pulsación; no usa una consola anterior ni un booleano antiguo. Un acierto completo
y su explicación permanecen hasta **Reiniciar código**, separados del borrador
actual. Cambiar o volver a compilar no revoca el logro. Su acreditación propia
controla referencias, avance y detención/continuación del lector cuando
corresponde.

La importación Android conserva datos y borradores sin fabricar pruebas Windows.
La procedencia nativa impide adoptar después un asset que retire o cambie un
objetivo ya incorporado; rechazarlo conserva los datos. Work integra los cinco
contratos y completa presentación, interacción y escucha por separado. Esta
capacidad no amplía el motor Android conservado.

## Ampliaciones revisadas con conservación en Windows

La extensión `stateLayout.compilerRevisions` admite las cuatro ampliaciones
cerradas de `CPP_CASOS_ADICIONALES_CON_PROGRESO`; su contrato de autoría está en
`docs/curriculum/STABLE_LESSON_STATE.md`. No es una excepción general para editar
los esperados de un compilador publicado.

En U11L7 se conservan los 19 casos originales y se añaden dos. Una prueba anterior
válida puede reutilizarse exclusivamente para el mismo caso del mismo programa
capturado; no acredita ninguno de los dos casos nuevos. Una edición de fuentes
antes de completar el objetivo ampliado mantiene las reglas de invalidación de
intentos. En U11L8, U11L10 y U11L11 el caso único exige un stdout ampliado: el logro
anterior se conserva como histórico, separado de la nueva comprobación completa.
Ni una salida anterior ni un booleano antiguo aprueban silenciosamente el objetivo
ampliado. El botón Reiniciar elimina el intento activo y evita que una prueba
histórica vuelva a proyectarse como acreditación corriente.

Las tres ampliaciones de stdout modifican simultáneamente la referencia estática
posterior: únicamente añaden sus llamadas revisadas antes del `return 0;` final
de `main`. El mapa conserva el bloque anterior completo en el archivo
`references` de su revisión. No se permite ampliar el esperado dejando una
referencia incompatible ni usar este contrato para cambiar otros programas.

La ampliación no crea otro editor, no copia borradores ni altera las traducciones.
Work sigue siendo responsable de integrar las consignas y referencias revisadas;
las pruebas del contrato no equivalen a su aceptación visual o escucha.

## Actividad integrada

`programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json`, página 5, bloque 3, declara la última modificación que ya solicitan sus instrucciones y muestra su referencia en la página 6: `Caja recibida` (español), `Box received` (inglés) o `Boîte reçue` (francés), sin salto final ni comillas. No se deduce el esperado de la predicción del programa inicial ni se cambia esa respuesta. El programa original y la primera modificación siguen disponibles para editar, ejecutar, comparar y reiniciar; esta comprobación no exige completar una etapa previa ni bloquea otra ejecución. Cada laboratorio utiliza la modalidad de sus propios metadatos; la ausencia de `outputCheck` conserva la exploración libre.

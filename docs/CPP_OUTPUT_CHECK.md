# Comprobación opcional de stdout en el Laboratorio

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
- Sin explicación se muestra sólo el veredicto. Un error no previsto muestra sólo «Incorrecto». No se infiere una causa, no se expone la salida esperada y los campos internos no entran en el guion normal de la lección. Se rechazan tipos y propiedades desconocidos como errores de configuración, no como respuestas incorrectas.

## Ejecución y conservación

«Compilar, ejecutar y comprobar» utiliza únicamente la ejecución nueva terminada correctamente del código/proyecto, compilador, configuración y entrada capturados por esa pulsación. Nunca comprueba una salida que ya estuviera en la consola. Compilación, enlace, configuración, ejecución fallida o límites técnicos conservan sus diagnósticos y dejan la comprobación sin resultado evaluable. Los avisos de compilación y stderr se conservan sin reescribirlos.

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

## Entrada del laboratorio

En un `cpp_compiler` curricular, el campo «Entrada» sólo aparece si el bloque declara `initialStandardInput` —también una cadena vacía, para permitir una actividad con EOF— o si algún caso de `outputCheck` declara una entrada no vacía. No se inspecciona el código del alumno para decidirlo. Sin esas declaraciones, la ejecución recibe entrada vacía y termina en EOF si intenta leer. Cualquier borrador de entrada anterior se conserva en almacenamiento, pero no se envía al programa mientras el campo no corresponda a esa actividad. El compilador libre del menú principal conserva su campo de entrada.

El guardado de entrada comparte el códec de texto escapado del código fuente, manteniendo la clave de borrador existente. Conserva espacios manuales, tabuladores y saltos finales sin depender de la indentación del XML de Android. Antes de guardar otro campo se protegen también las entradas y fuentes antiguas aún no abiertas, sin cambiar sus caracteres salvo reparación demostrada por una huella previa. Se utiliza una única escritura para todo el mapa: Android reescribe todas las preferencias y una protección parcial dejaría otras fuentes expuestas.

El antiguo serializador podía añadir cuatro espacios después de un LF final. Cuando una huella previa demuestra el original, la reparación exacta queda persistida. No hay menú de revisión ni recorte general de entradas. Work confirmó que los cuatro espacios de su prueba `Leo\n2\n    ` eran un defecto: ese caso concreto se corrige manualmente a `Leo\n2\n`, sin quitar el salto final ni cambiar otros datos. La intención del currículo no permite deducir la intención de cualquier borrador personal. Si aparece otro caso ambiguo, hay que indicar su ubicación y consultar a Work sobre la consigna, sin añadir decisiones al lector ni eliminar datos por su forma. Los guardados nuevos registran la huella del texto exacto introducido, incluidos espacios deliberados, tabuladores y saltos. El código fuente conserva sus huellas y marcas de tabulación anteriores; las reparaciones demostradas también se guardan definitivamente. Todo este formato es privado; Work no añade metadatos a las lecciones.

## Actividad integrada

`programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json`, página 5, bloque 3, declara la última modificación que ya solicitan sus instrucciones y muestra su referencia en la página 6: `Caja recibida` (español), `Box received` (inglés) o `Boîte reçue` (francés), sin salto final ni comillas. No se deduce el esperado de la predicción del programa inicial ni se cambia esa respuesta. El programa original y la primera modificación siguen disponibles para editar, ejecutar, comparar y reiniciar; esta comprobación no exige completar una etapa previa ni bloquea otra ejecución. Cada laboratorio utiliza la modalidad de sus propios metadatos; la ausencia de `outputCheck` conserva la exploración libre.

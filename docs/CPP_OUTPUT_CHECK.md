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
- Los casos deben tener entradas inequívocas bajo el modo seleccionado. La entrada vigente identifica automáticamente un único caso; no se añade un selector de respuestas ni de etapas. Un objetivo distinto con la misma entrada corresponde a otro bloque/etapa curricular, no a varios casos ambiguos.
- `explanation` es texto opcional no vacío de acierto. `reviewFeedback` es un array opcional, también puede ser vacío: cada regla declara `outputs` y `text` no vacío. Sólo una coincidencia literal con esas salidas incorrectas permite mostrar su orientación. Las condiciones no pueden solaparse entre reglas ni con las salidas correctas.
- Sin explicación se muestra sólo el veredicto. Un error no previsto muestra sólo «Incorrecto». No se infiere una causa, no se expone la salida esperada y los campos internos no entran en el guion normal de la lección. Se rechazan tipos y propiedades desconocidos como errores de configuración, no como respuestas incorrectas.

## Ejecución y conservación

«Compilar, ejecutar y comprobar» utiliza únicamente la ejecución nueva terminada correctamente del código/proyecto, compilador, configuración y entrada capturados por esa pulsación. Nunca comprueba una salida que ya estuviera en la consola. Compilación, enlace, configuración, ejecución fallida o límites técnicos conservan sus diagnósticos y dejan la comprobación sin resultado evaluable. Los avisos de compilación y stderr se conservan sin reescribirlos.

Los laboratorios CMake conservan sus controles explícitos de fases. Con `outputCheck`, la acción combinada configura si hace falta, construye el objetivo vigente mediante CMake y ejecuta un programa nuevo antes de comparar; se conservan los diagnósticos de cada fase. Un resultado restaurado por el observador del proyecto puede volver a mostrar su consola, pero nunca genera una comprobación. Sin contrato se conserva el funcionamiento de sus controles de configuración, construcción y ejecución.

Editar una fuente, cabecera, selección de fuentes, entrada, configuración CMake o compilador invalida la ejecución y el veredicto. Deshacer o rehacer no recupera una ejecución antigua: hay que ejecutar de nuevo. Cambiar sólo la pestaña de archivo, sin alterar el proyecto, conserva la identidad del programa. Reiniciar invalida incluso si los bytes ya coinciden con el original. Un resultado tardío de otra revisión no habilita la comprobación. Al reabrir se conservan los borradores existentes y se requiere otra ejecución; no se guarda un historial paralelo ni se restaura un veredicto sin ejecutar.

Estas comprobaciones **no consumen corazones**, no completan puzzles, no responden la predicción vecina y no alteran los requisitos de avance. Una coincidencia de stdout admite diferentes implementaciones y no demuestra por sí sola que se hayan utilizado las variables o la estrategia solicitadas.

Con el lector activado, todo el veredicto visible —incluida una respuesta sin explicación— se lee mediante la revisión temporal ya existente. Se conserva el mismo reproductor, las posiciones de las tres páginas, pausa con liberación y controles. Esta lectura no acredita retroalimentación curricular pendiente ni desbloquea otros ejercicios. Las palabras del resultado usan el sombreado existente; consola y código conservan sus exclusiones.

El contrato es independiente de `output_prediction`, de los datos de evaluación matemática y del estilo del texto. Ningún JSON antiguo necesita migración.

## Entrada del laboratorio

En un `cpp_compiler` curricular, el campo «Entrada» sólo aparece si el bloque declara `initialStandardInput` —también una cadena vacía, para permitir una actividad con EOF— o si algún caso de `outputCheck` declara una entrada no vacía. No se inspecciona el código del alumno para decidirlo. Sin esas declaraciones, la ejecución recibe entrada vacía y termina en EOF si intenta leer. Cualquier borrador de entrada anterior se conserva en almacenamiento, pero no se envía al programa mientras el campo no corresponda a esa actividad. El compilador libre del menú principal conserva su campo de entrada.

## Actividad integrada

`programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json`, página 5, bloque 3, declara la última modificación que ya solicitan sus instrucciones y muestra su referencia en la página 6: `Caja recibida` (español), `Box received` (inglés) o `Boîte reçue` (francés), sin salto final ni comillas. No se deduce el esperado de la predicción del programa inicial ni se cambia esa respuesta. El programa original y la primera modificación siguen disponibles para editar, ejecutar, comparar y reiniciar; esta comprobación no exige completar una etapa previa ni bloquea otra ejecución. Los demás laboratorios conservan su modalidad actual hasta que declaren su propio `outputCheck`.

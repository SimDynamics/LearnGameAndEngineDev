# Construcción y transporte libre de segmentos en Windows

El componente optativo `math_segment_transport` se utiliza dentro de un
`math_puzzle` o como bloque independiente. Su validador es `segment_transport`.
Es una capacidad Windows: el adaptador genera la extensión del parser y del
evaluador sin modificar el proyecto Android de origen ni sus recursos.

La capacidad permite colocar y arrastrar extremos libremente, tomar una apertura
de una figura dada o de un tramo ya construido, y trasladar o girar esa herramienta
sin estirarla. Las figuras comparten una sola escala. La interfaz no muestra ejes,
cuadrícula, coordenadas, medidas numéricas ni una lista de posiciones solución.
La herramienta y la vista se guardan separadas de los puntos que se evalúan.

## Transporte, composición y repetición

`mode: "chain"` da uno o varios segmentos fuente y una semirrecta de origen fijo.
Cada entrada de `steps` identifica la fuente cuya longitud corresponde al tramo
siguiente. Una entrada sirve para transportar una longitud; varias fuentes
permiten componer longitudes; repetir un ID permite concatenar varias copias.

Ejemplo técnico de dos tramos, sin añadir una actividad al currículo:

```json
{
  "type": "math_puzzle",
  "prompt": "Construye los dos tramos consecutivos con las longitudes dadas.",
  "responses": [{
    "id": "construction",
    "type": "math_segment_transport",
    "validator": {
      "type": "segment_transport",
      "mode": "chain",
      "sources": [
        {"id": "ab", "from": {"x": 0, "y": 3, "label": "A"}, "to": {"x": 4, "y": 3, "label": "B"}},
        {"id": "cd", "from": {"x": 6, "y": 3, "label": "C"}, "to": {"x": 8, "y": 3, "label": "D"}}
      ],
      "target": {"origin": {"x": 0, "y": 0, "label": "M"}, "direction": {"x": 1, "y": 0}},
      "steps": ["ab", "cd"],
      "pointLabels": ["N", "P"],
      "positionTolerance": 0.04,
      "lengthTolerance": 0.04
    }
  }]
}
```

El primer punto libre representa N y el segundo P. El evaluador comprueba MN y NP
por separado, además de la pertenencia a la semirrecta, el sentido, el orden y el
total. Un extremo final correcto no compensa deformar o solapar las partes.
`steps: ["ab", "ab", "ab"]` con tres etiquetas declara tres copias consecutivas.
La interfaz no dibuja de antemano los extremos pedidos.

## Particiones y punto medio

`mode: "partition"` proporciona ambos extremos del segmento total. El estudiante
construye únicamente sus marcas interiores. `partitionCount: 2` corresponde al
punto medio; un número mayor exige todas las partes congruentes.

```json
{
  "id": "partition",
  "type": "math_segment_transport",
  "prompt": "Construye las marcas que dividen el segmento en tres partes iguales.",
  "validator": {
    "type": "segment_transport",
    "mode": "partition",
    "target": {
      "origin": {"x": 0, "y": 0, "label": "M"},
      "end": {"x": 6, "y": 0, "label": "T"}
    },
    "partitionCount": 3,
    "pointLabels": ["N", "P"],
    "positionTolerance": 0.04,
    "lengthTolerance": 0.04
  }
}
```

Se comprueban MN, NP y PT, la pertenencia de N y P al interior del total y su orden
estricto. Contar dos marcas o conservar sólo MT no acredita la partición. Puede
haber fuentes adicionales en `sources` como datos de la actividad; la partición
no entrega automáticamente una herramienta con la longitud de una parte correcta.
La herramienta inicial toma el total cuando no se han declarado fuentes.

## Datos de autoría y evaluación

- Las coordenadas pertenecen al modelo interno del dibujo. No son campos para el
  estudiante ni un prerrequisito curricular.
- Origen, extremos y etiquetas son obligatorios; la dirección de una semirrecta
  debe ser finita y no degenerada. Las fuentes tienen IDs distintos y longitud
  positiva. Un mismo nombre dado sólo puede identificar una misma posición.
- `pointLabels` contiene una etiqueta única por punto libre, sin reutilizar un
  nombre dado. En una cadena coincide con el número de `steps`; en una partición
  contiene `partitionCount - 1` etiquetas. Se admiten hasta 32 fuentes, 32 tramos
  de cadena o 32 partes.
- `initial.points` es opcional. Es un prefijo ordenado de posiciones iniciales,
  destinado a actividades que necesitan un borrador inicial. Su ausencia comienza
  sin puntos construidos. No es una lista de alternativas ni un campo `expected`.
- `positionTolerance` y `lengthTolerance` son obligatorias, finitas, positivas y
  menores que la cuarta parte de cada tramo requerido. Se expresan en las mismas
  unidades internas de la geometría, independientemente del tamaño de ventana,
  DPI y zoom. La primera limita la separación respecto de la recta; la segunda
  limita cada longitud y también el total. La tolerancia práctica del control no
  constituye una demostración de igualdad geométrica exacta.
- Una respuesta incompleta o un estado no evaluable no consume un intento. Una
  construcción completa incorrecta utiliza el mecanismo ordinario de intentos
  matemáticos. La guía señala la condición que debe revisarse y conserva los
  puntos; no proporciona posiciones ni longitudes solución.
- El parser rechaza familias incompatibles y contratos degenerados antes de
  pedir una respuesta. La extensión no cambia el significado de `geometry`,
  `coordinates` ni de los ejercicios existentes.

Los límites numéricos de autoría son explícitos: coordenadas en valor absoluto
hasta `1e9`, longitudes desde `1e-9` y total hasta `1e9`. La separación entre los
datos dados e iniciales, dividida por la menor longitud relevante, no puede
superar `1e10`. Las tolerancias deben alcanzar al menos ocho unidades de precisión
de `Double` en las posiciones implicadas; una configuración que no lo permita se
rechaza antes del intento. Estos límites evitan aceptar tareas que el control no
pueda representar o alcanzar. Una figura homogéneamente reducida conserva su
encuadre relativo. La cámara admite zoom entre `1/64` y `1e12`; navegar nunca
modifica ni recorta una respuesta guardada fuera del encuadre actual.
Una medida tomada de un intento puede abarcar hasta la diagonal entre los límites
de coordenadas, `hypot(2e9, 2e9)`, aunque exceda el total permitido para la autoría.
La herramienta conserva esa apertura al guardarse, reabrirse o corregir los puntos;
no la trunca para hacerla evaluable.

## Controles y conservación

«Colocar puntos» coloca el punto seleccionado o permite arrastrar una marca ya
construida. El selector «Punto» recorre los puntos existentes y el siguiente por
crear; ofrece nombres, no posiciones correctas. El doble clic de colocación no
abre otra vista ni coloca una solución automáticamente.

«Longitud» captura la apertura de una fuente o de un tramo construido actualmente.
Cambiar después ese tramo no estira la herramienta ya capturada: es necesario
medirlo de nuevo. «Mover herramienta» y «Girar herramienta» conservan su apertura.
Se pueden contrastar las partes construidas mediante el mismo instrumento.

«Vista», «Acercar», «Alejar» y la rueda en modo Vista cambian únicamente la cámara.
Permiten llegar fuera del encuadre inicial. Ampliar la representación mueve las
mismas ventanas y controles al panel expandido; conserva respuesta, cámara y
herramienta. Restablecer la vista o la herramienta tampoco coloca puntos.

Los controles se distribuyen en una, dos o tres columnas según el ancho y el
tamaño real del texto. La vista ampliada conserva espacio para el dibujo y permite
desplazar su panel de acciones. Tab recorre los controles y Escape cierra la vista.
Un fallo temporal al actualizar el dibujo conserva el último estado válido y
muestra un aviso neutro; volver a operar permite reintentar sin perder puntos ni
añadir una evaluación.

Tras acertar, los puntos quedan bloqueados. La vista y la herramienta siguen
disponibles para examinar la construcción conservando el resultado y su
retroalimentación. «Reiniciar» revoca explícitamente el resultado. El control
«Restablecer construcción» restaura el borrador inicial de esta respuesta mientras
es editable, sin modificar otras respuestas de una composición.

La respuesta usa el formato existente:

```json
{"type":"graph","points":[{"x":4,"y":0},{"x":6,"y":0}]}
```

El orden corresponde a `pointLabels`. Origen y extremo fijo de la partición no se
duplican en la respuesta. `interactionDraft` conserva modo, punto seleccionado,
cámara y herramienta; esos datos no participan en la calificación. El host
comprueba la identidad semántica antes de guardar una actualización de vista,
conserva la revisión y evita cobrar otra vez por navegar sobre el mismo intento.
La reapertura vuelve a validar el contenido y los puntos guardados.

## Verificación técnica e integración curricular

`native.segment_transport_worker` contrasta los contratos mediante el worker real.
`native.segment_transport_view` ejercita controles Win32 y el recorrido de una
lección QA con persistencia, rechazo, reintento, acierto y reinicio. Se usan datos
aislados; ninguna prueba incorpora actividades ni progreso al currículo.

Work puede integrar prácticas compatibles con este contrato conservando los IDs,
páginas y puzzles existentes. La selección de actividades, sus procedimientos y
prerrequisitos siguen siendo tareas curriculares. Las pruebas técnicas no
acreditan la aceptación visual o la escucha humana, ni resultados en Honor.

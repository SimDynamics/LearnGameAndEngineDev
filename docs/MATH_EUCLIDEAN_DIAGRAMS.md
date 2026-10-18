# Diagramas euclidianos de lectura

Contrato implementado el 2026-10-13 para `MATH_DIAGRAMAS_EUCLIDIANOS_SIN_COORDENADAS`. Amplía de forma optativa las representaciones de `schemaVersion: 1`; no cambia los diagramas cartesianos, `geometry_2d_diagram`, los constructores ni sus valores predeterminados.

Este documento describe el componente de aplicación y ofrece representaciones de ejemplo. **Work integrará y verificará las tres actividades curriculares de Geometría después.** Los ejemplos no son lecciones, no sustituyen los bloques preparatorios y no modifican índices, identificadores ni progreso.

## Forma de una representación

`type: "euclidean_diagram"` se puede usar en `representations` y en el `content` enriquecido compatible. Sus datos pueden estar directamente en la representación o dentro de `config`, como en los demás renderers. Si hay un objeto `config`, éste contiene todos los datos del diagrama; no se combinan sus propiedades con las exteriores. El `type` queda en el objeto exterior.

| Propiedad | Contrato |
| --- | --- |
| `points` | Array obligatorio de 1 a 32 puntos. |
| `points[].id` | ID estable del punto, único dentro del diagrama. |
| `points[].x`, `points[].y` | Números internos para disponer el dibujo, obligatorios. No se muestran al alumno. |
| `points[].labels` | Array opcional de 0 a 8 nombres visibles completos. Un ID no genera por sí mismo una etiqueta. |
| `objects` | Array obligatorio de 0 a 32 objetos; puede quedar vacío para mostrar sólo puntos. |
| `objects[].id` | ID estable del objeto, único y distinto de todos los IDs de punto. |
| `objects[].kind` | `line`, `ray` o `segment`. |
| `objects[].through` | Dos IDs de puntos distintos y existentes. Define recta, semirrecta o segmento. |
| `objects[].origin`, `objects[].direction` | Alternativa a `through`, sólo para `line` y `ray`: ID de un punto y vector interno `{ "x": número, "y": número }`. |
| `objects[].contains` | Array obligatorio, sin duplicados, que declara todos los puntos del diagrama pertenecientes al objeto. Incluye sus puntos definidores. |
| `objects[].label` | Texto visible opcional del objeto. |
| `description` | Texto opcional para accesibilidad. Conviene describir explícitamente las relaciones; sin él se concatenan los nombres y etiquetas publicados. |

Los IDs siguen `[A-Za-z][A-Za-z0-9_-]{0,63}`. Los nombres y etiquetas son cadenas no vacías ni exclusivamente blancas, de hasta 128 unidades UTF-16, sin caracteres de control, tabuladores ni saltos de línea. Su texto no se sustituye por el ID. Un nombre de punto no puede repetirse en otro punto; nombres diferentes del mismo punto se declaran juntos en `labels`.

Los componentes numéricos son números JSON finitos, con valor absoluto no mayor que `1000000`; no se aceptan cadenas numéricas. Los ceros `0.0` y `-0.0` se normalizan a una sola posición. IDs de punto distintos deben tener posiciones internas distintas. La dirección, explícita o definida por dos puntos, debe tener longitud al menos `1e-9`. Estas posiciones y direcciones son disposición interna del esquema, no un ejercicio de coordenadas ni un editor x/y.

## Pertenencia y límites

`contains` es la declaración de pertenencia, conservada por ID. `EuclideanDiagram.contains(objectId, pointId)` consulta esa declaración; no examina píxeles. Los validadores de selección existentes siguen comprobando los IDs de opción establecidos por el contenido. No se incorpora un evaluador geométrico nuevo ni se genera una respuesta correcta a partir del aspecto de la figura.

El parser contrasta la disposición interna con las pertenencias para impedir dibujos contradictorios: rechaza un punto declarado sobre un objeto que queda fuera de él y un punto dibujado sobre él omitido de `contains`. Para este contraste de disposición usa una tolerancia de distancia de `1e-8` unidades internas y, en los límites de semirrectas y segmentos, una tolerancia paramétrica de `1e-8`. Es una comprobación numérica de coherencia del esquema, no una demostración exacta de las condiciones curriculares. Work debe declarar las condiciones y respuestas matemáticas explícitamente.

Con `through: ["A", "B"]`:

- `line` es la recta determinada por A y B: se prolonga en ambos sentidos y lleva dos flechas.
- `ray` tiene origen A, pasa por B y continúa más allá de B: lleva una flecha en el sentido A→B. Cambiar el orden cambia el origen y el sentido.
- `segment` tiene extremos A y B, sin flechas ni prolongaciones.

Con `origin` y `direction`, la recta pasa por el punto indicado y la semirrecta empieza allí en el sentido del vector. Un segmento siempre requiere dos extremos mediante `through`. No se admite mezclar ambas formas. El recorte paramétrico limita el dibujo al espacio interior reservado del panel; el borde visible no se convierte en un extremo matemático de una recta.

Se rechazan referencias desconocidas, arrays o tipos incorrectos, IDs duplicados, direcciones degeneradas, posiciones coincidentes bajo IDs distintos, nombres de punto ambiguos y pertenencias incoherentes. Dos objetos `line` distintos tampoco pueden designar la misma recta; se comprueba paralelismo y coincidencia de origen en la disposición interna. Esta comprobación no es un motor general de restricciones o demostraciones. Dos direcciones diferentes por un único punto sí son admisibles.

## Ejemplo: `leer_pertenencia_en_el_plano`

La disposición de referencia A=(0,0), B=(2,0), C=(0,1) sólo aparece en los datos internos. A está declarada sobre r y s; B sólo sobre r; C sólo sobre s. El componente dibuja los puntos y nombres, las dos rectas y sus prolongaciones, sin mostrar ejes, cuadrícula, escalas o pares numéricos.

```json
{
  "type": "euclidean_diagram",
  "description": "A pertenece a r y a s. B pertenece sólo a r; C pertenece sólo a s.",
  "points": [
    { "id": "A", "x": 0, "y": 0, "labels": ["A"] },
    { "id": "B", "x": 2, "y": 0, "labels": ["B"] },
    { "id": "C", "x": 0, "y": 1, "labels": ["C"] }
  ],
  "objects": [
    { "id": "r", "kind": "line", "through": ["A", "B"], "contains": ["A", "B"], "label": "r" },
    { "id": "s", "kind": "line", "through": ["A", "C"], "contains": ["A", "C"], "label": "s" }
  ]
}
```

## Ejemplo: `distinguir_nombres_y_posiciones`

Para dos puntos distintos, se usan dos IDs con posiciones diferentes y la recta definida por ambos; el ejemplo de segmento de la sección siguiente puede convertirse en recta cambiando sólo `kind` a `line` y su etiqueta según el significado deseado.

Para A y B que nombran el mismo punto P se declara **un solo punto** con los dos nombres. P es su ID estable y no una tercera marca. El ejemplo muestra dos rectas de direcciones diferentes que pasan por él; contar A y B como dos posiciones produciría un diagrama incompatible.

```json
{
  "type": "euclidean_diagram",
  "description": "A y B son nombres del mismo punto P. Las rectas r y s, de direcciones diferentes, pasan por ese punto.",
  "points": [
    { "id": "P", "x": 0, "y": 0, "labels": ["A", "B"] }
  ],
  "objects": [
    { "id": "r", "kind": "line", "origin": "P", "direction": { "x": 1, "y": 0 }, "contains": ["P"], "label": "r" },
    { "id": "s", "kind": "line", "origin": "P", "direction": { "x": 1, "y": 1 }, "contains": ["P"], "label": "s" }
  ]
}
```

Los nombres se sitúan en etiquetas diferentes alrededor de la misma marca. La posición del texto no altera la posición ideal del punto ni su pertenencia. Para hacer visible también el nombre P, Work puede incluirlo en el mismo array `labels`; no debe duplicar el punto en `points`.

## Ejemplo: `reconocer_recta_semirrecta_y_segmento`

Estas tres representaciones se muestran en paneles separados. Si se superpusieran la recta, la semirrecta y el segmento sobre los mismos A y B, sus trazos se taparían y no permitirían leer sus límites. Cada panel tiene su propio ámbito de IDs; reutilizar A y B entre paneles no crea posiciones adicionales dentro de uno.

```json
[
  {
    "type": "euclidean_diagram",
    "description": "Recta r por A y B, prolongada en ambos sentidos.",
    "points": [
      { "id": "A", "x": 0, "y": 0, "labels": ["A"] },
      { "id": "B", "x": 2, "y": 0, "labels": ["B"] }
    ],
    "objects": [
      { "id": "r", "kind": "line", "through": ["A", "B"], "contains": ["A", "B"], "label": "r" }
    ]
  },
  {
    "type": "euclidean_diagram",
    "description": "Semirrecta de origen A que pasa por B y continúa más allá de B.",
    "points": [
      { "id": "A", "x": 0, "y": 0, "labels": ["A"] },
      { "id": "B", "x": 2, "y": 0, "labels": ["B"] }
    ],
    "objects": [
      { "id": "rayAB", "kind": "ray", "through": ["A", "B"], "contains": ["A", "B"], "label": "AB" }
    ]
  },
  {
    "type": "euclidean_diagram",
    "description": "Segmento AB, limitado por sus extremos A y B.",
    "points": [
      { "id": "A", "x": 0, "y": 0, "labels": ["A"] },
      { "id": "B", "x": 2, "y": 0, "labels": ["B"] }
    ],
    "objects": [
      { "id": "segmentAB", "kind": "segment", "through": ["A", "B"], "contains": ["A", "B"], "label": "AB" }
    ]
  }
]
```

## Adaptación y comprobación

`MathEuclideanDiagramView` dibuja con Canvas nativo. Ajusta uniformemente las posiciones al tamaño disponible, preservando direcciones y proporciones del esquema. No agrega nombres, valores, ejes, cuadrículas, escalas ni editores. El renderer usa el contenedor expandible existente, con proporción inicial 1,4:1; ampliar mueve la misma vista y conserva su modelo. La escala de texto matemática y la escala de fuente Android intervienen en la medición y el dibujo de las etiquetas.

Las etiquetas usan métricas del Paint real, se distribuyen mediante ocho posiciones candidatas y se refluyen conservando todos sus caracteres, sin elipsis. La selección de posiciones penaliza solapamientos con otras etiquetas, puntos y trazos. Es una heurística local: **no garantiza ausencia de colisiones o lectura completa para escenas arbitrariamente densas, etiquetas máximas acumuladas o paneles extremadamente pequeños**. Los límites del parser son límites de datos, no una garantía de composición visual para todas sus combinaciones. Work debe usar esquemas legibles, separar comparaciones que se tapen y revisar las capturas curriculares en los tamaños y escalas pertinentes.

`drawingSnapshot()` expone `viewport`, `plotBounds`, marcas de punto, trazos recortados, flechas y etiquetas con texto íntegro, líneas y límites. El Canvas consume esas mismas primitivas. Así se pueden contrastar posiciones, extremos, prolongaciones y píxeles dibujados; recuperar únicamente una cadena no demuestra la legibilidad del diagrama.

Fuentes: `EuclideanDiagram.kt`, `MathEuclideanDiagramView.kt` y el nuevo caso de `MathRepresentationRenderer.kt`. Las regresiones del modelo y el layout están en `EuclideanDiagramTest.kt`; las pruebas Android y sus evidencias se registran en la ficha de `PENDIENTES_RECURSOS_CURRICULARES.md`. Las condiciones de aceptación de una futura integración curricular se verifican sobre sus bloques concretos y sus controles existentes, conservando IDs, posiciones y progreso. Este contrato no incluye arrastre, construcción con regla y compás, medición automática ni geometría espacial.

## Extensión opcional: circunferencias, discos y arcos

La misma representación `euclidean_diagram` admite ahora dos arrays opcionales. Omitirlos, o dejarlos vacíos, conserva el contrato y el dibujo anteriores. No se cambia `schemaVersion`, no se añade un evaluador y no se convierte el borrador de circunferencias en una lección terminada. Work integrará las representaciones curriculares después de su revisión.

| Propiedad | Contrato |
| --- | --- |
| `circles` | De 0 a 8 circunferencias en la misma escena. |
| `circles[].id` | ID estable, distinto de puntos, objetos, otras circunferencias y arcos. |
| `circles[].center` | ID de un punto existente: es el mismo centro que usan los segmentos. |
| `circles[].radius` | Radio interno finito entre `1e-9` y `1000000`, ambos incluidos. No se imprime como medida curricular. |
| `circles[].contains` | Todos los IDs de puntos de la escena que pertenecen a la **frontera**; puede estar vacío. No incluye el centro de una circunferencia de radio positivo. |
| `circles[].filled` | Booleano opcional, `false` por defecto. `true` dibuja el disco con fondo tenue y conserva su frontera. |
| `circles[].label` | Rótulo visible opcional, con los mismos límites de las etiquetas anteriores. |
| `arcs` | De 0 a 16 arcos anclados a circunferencias de esta escena. |
| `arcs[].id`, `arcs[].circle` | ID estable del arco e ID de su circunferencia existente. |
| `arcs[].through` | Tres IDs distintos: **extremo inicial, punto intermedio, extremo final**. Los tres pertenecen a esa circunferencia. |
| `arcs[].contains` | Todos los puntos de la escena pertenecientes al arco elegido, incluidos sus tres puntos definidores. |
| `arcs[].label` | Rótulo visible opcional. No se generan nombres ni valores angulares. |

`contains` sigue siendo una declaración de pertenencia por ID. Para una circunferencia designa su frontera, también cuando `filled` es `true`: colorear el disco no convierte su centro en un punto de la circunferencia. Las preguntas de interior, frontera y exterior utilizan los datos y validadores curriculares existentes; el renderer no fabrica respuestas a partir de píxeles.

El parser comprueba la coherencia numérica entre posiciones internas, centro, radio y pertenencias. Rechaza radios nulos, negativos o no finitos, referencias desconocidas, IDs repetidos, circunferencias coincidentes bajo IDs distintos y arcos degenerados o incompatibles con sus puntos. Esta comprobación conserva el carácter aproximado del control de disposición: no sustituye la justificación matemática ni el cálculo racional exacto del ejercicio.

Un radio se dibuja con un `segment` entre el centro y un punto de la frontera. Un diámetro utiliza dos puntos opuestos e incluye el centro en su `contains`; una cuerda no diametral conserva otros extremos y no declara el centro. Una secante es una `line` por dos puntos de la circunferencia: sus dos flechas y su recorte se prolongan más allá de las intersecciones. Todos estos objetos y las figuras circulares usan **una sola transformación uniforme**, por lo que cambiar el panel no convierte circunferencias en elipses ni altera sus incidencias.

### Ejemplo técnico de escena compartida

Este fixture ilustra las relaciones, no entrega un puzzle ni una nueva respuesta curricular. Cambiar únicamente `filled` alterna frontera y disco. Las longitudes internas disponen el dibujo; las unidades y medidas del enunciado siguen perteneciendo al contenido original.

```json
{
  "type": "euclidean_diagram",
  "points": [
    { "id": "O", "x": 0, "y": 0, "labels": ["O"] },
    { "id": "A", "x": -5, "y": 0, "labels": ["A"] },
    { "id": "B", "x": 5, "y": 0, "labels": ["B"] },
    { "id": "P", "x": 0, "y": 5, "labels": ["P"] },
    { "id": "Q", "x": 0, "y": -5, "labels": ["Q"] }
  ],
  "objects": [
    { "id": "diameter", "kind": "segment", "through": ["A", "B"], "contains": ["A", "O", "B"] },
    { "id": "radius", "kind": "segment", "through": ["O", "P"], "contains": ["O", "P"] }
  ],
  "circles": [
    { "id": "c", "center": "O", "radius": 5, "contains": ["A", "B", "P", "Q"], "filled": true }
  ],
  "arcs": [
    { "id": "upper", "circle": "c", "through": ["A", "P", "B"], "contains": ["A", "P", "B"], "label": "APB" },
    { "id": "lower", "circle": "c", "through": ["A", "Q", "B"], "contains": ["A", "Q", "B"], "label": "AQB" }
  ]
}
```

El punto intermedio selecciona uno de los dos arcos entre los extremos, incluso cuando estos definen un diámetro. No se solicitan ni muestran ángulos, grados o radianes. El Canvas dibuja sólo el arco, sin radios de cierre ni relleno de un sector. Sus parámetros angulares de dibujo son detalles internos del snapshot, no nuevas magnitudes curriculares.

Las circunferencias completas intervienen en el ajuste de límites, aunque únicamente esté marcado su centro; también se incluyen los puntos exteriores y otras circunferencias desplazadas. El disco se rellena antes de trazar líneas, marcas y etiquetas. Los arcos se distinguen por un trazo algo más grueso sobre la frontera. Se reutilizan la ampliación de panel, la escala de texto y la descripción de accesibilidad existentes. Si no se proporciona `description`, la alternativa accesible contiene únicamente las etiquetas declaradas, sin añadir clasificaciones ni conclusiones.

Los nombres de puntos y trazos conservan sus ocho posiciones locales de anclaje. Las etiquetas de circunferencias y arcos buscan posiciones junto a distintos puntos de su propia frontera, penalizando lugares más próximos a otra curva y el alejamiento de la curva identificada. Pueden considerar también espacios libres en los bordes para acomodar rótulos largos, conservando esos mismos criterios de asociación. La distribución de rótulos continúa siendo una heurística: cada escena curricular debe verificarse en los tamaños y escalas pertinentes. No se garantiza la legibilidad de configuraciones arbitrariamente densas por el solo hecho de respetar los límites de datos.

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

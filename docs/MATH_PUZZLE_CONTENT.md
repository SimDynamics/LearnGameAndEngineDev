# Contenido de puzzles matemáticos

Fecha: 2026-10-01. Infraestructura inicial para la ruta `math_physics`.
Los ejemplos del sandbox son originales y sirven para desarrollo; no sustituyen un curso curricular.

## Lecciones provisionales para QA de navegación

Los cursos 1, 2 y 3 tienen tres unidades disponibles y tres lecciones de prueba por unidad: **27 lecciones estándar**, más el sandbox original de 16 páginas/21 puzzles en Curso 1 / Unidad 1. Cada nueva lección se marca como **Contenido provisional de desarrollo** y contiene un ejemplo mínimo original. Los títulos de cursos/unidades conservan la estructura prevista; estos ejemplos no desarrollan aún su temario curricular.

Curso 1 / Unidad 1 conserva `numbers_operations` y añade contenido real a `fractions_decimals` y `powers_roots`. Las ocho unidades restantes usan IDs `qa_step_01`, `qa_step_02` y `qa_step_03`. La disponibilidad sigue el mecanismo existente de assets locales, sin una lista matemática limitada al Curso 1.

La finalización matemática busca la siguiente lección válida **en la misma unidad** mediante `NavigationRepository.getNextLessonInUnit`. Una cadena recorre Lección 1 → 2 → 3 → unidad contenedora; no salta automáticamente a otra unidad o curso. El sandbox sigue disponible desde su tarjeta, pero su entrada en el JSON de la unidad declara `automaticContinuation: false`: permite ensayar la cadena de tres lecciones sin entrar involuntariamente en sus 16 páginas. Esta metadata opcional excluye esa lección de la continuación automática y no modifica su contenido ni su progreso.

`NavigationStateRepository` guarda también `routeId` junto a pantalla/curso/unidad/lección; sus APIs anteriores mantienen el valor predeterminado de Programming. Un estado anterior sin routeId se migra a Matemáticas si sus IDs empiezan por `math_course_`. `MainActivity` reconstruye la pila de la ruta persistida usando los mismos repositorios y evita guardar la pantalla principal mientras está abriendo el destino. La página de la lección continúa perteneciendo a `ProgressRepository`; no se crea una segunda clave para duplicar ese estado.

Las pruebas de una nueva instancia de MainActivity verifican esta reconstrucción con preferencias persistidas. Una prueba de force-stop/reapertura real y la revisión del botón Atrás en dispositivo siguen siendo parte de QA manual; no equivalen a recrear una Activity mediante instrumentación.

## Abrir el sandbox

En la app: **Matemáticas y Física → Curso 1: Fundamentos aritméticos, álgebra elemental y precálculo → Unidad 1: Fundamentos aritméticos → Lección 4: Sandbox de desarrollo: puzzles matemáticos**.

Archivo de contenido:

`app/src/main/assets/content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json`

La lección tiene 16 páginas y 21 puzzles. Incluye las 13 familias de interacción y composiciones con varias respuestas, además de representaciones de tabla, fórmula, recta, gráfica, geometría y conjuntos.

## Qué se reutilizó del proyecto

La infraestructura nueva utiliza la navegación y carga de JSON existentes. Los motores de programación permanecen disponibles.

| Familia matemática | Base existente observada | Reutilización |
| --- | --- | --- |
| P01 selección única | `addMultipleChoice` | Patrón seleccionar → comprobar → feedback; estado persistente y progreso. El nuevo contenido permite opciones enriquecidas. |
| P02 selección múltiple | Selección única anterior | Mismo contrato de opción y feedback; varias selecciones. |
| P03 entrada numérica | `addFillCode`, `addOutputPrediction` | Patrón de campo, comprobación y restauración; validación matemática separada. |
| P04 varios campos | `addInlineFillCode` | Varios campos dentro de un ejercicio; comprobación conjunta. |
| P05 expresión | Campos de entrada existentes | Mecanismo de entrada; validador de expresiones independiente de la UI. |
| P06 estructurada, P07 huecos | `addInlineFillCode` | Composición de fragmentos fijos y campos. |
| P08 ordenación | `addOrderCode` | Interacción de ordenar fichas, alternativa a arrastre y estado del orden. |
| P09 recta numérica | Sin base visual matemática previa | Componente visual común para representación y construcción de desigualdades. |
| P10 sliders | `SeekBar` y apariencia por ruta | Controles Android existentes; visuales matemáticos controlados por parámetros. |
| P11 relacionar | `addDragFillCode` | Modelo fichas/destinos y persistencia; relaciones genéricas entre contenidos. |
| P12 gráfica, P13 geometría | Sin base cartesiana previa | Base común de cuadrícula, puntos y transformaciones; validadores distintos según respuesta. |

Los patrones se reutilizan sin invocar los renderers que dependen de código C++. También se conserva el host de fullscreen, la caché de Views por página, los ajustes visuales por ruta, la escala de texto y `ProgressRepository`.

## Contrato JSON recomendado

Un bloque se compone de enunciado, representaciones, herramientas opcionales, una o varias respuestas, validadores y feedback.

```json
{
  "type": "math_puzzle",
  "schemaVersion": 1,
  "id": "example_original",
  "prompt": "Resuelve 7 − 3.",
  "representations": [
    { "type": "math_formula", "text": "7 − 3 = ?" }
  ],
  "tools": [],
  "responses": [
    {
      "id": "result",
      "type": "math_numeric_input",
      "label": "Resultado",
      "keyboard": "NUMERIC",
      "validator": { "type": "numeric", "expected": "4" }
    }
  ],
  "feedback": {
    "correct": "7 − 3 = 4.",
    "incorrect": "Revisa la resta."
  }
}
```

- `schemaVersion`: versión del contrato, actualmente 1.
- `id`: identificador descriptivo del puzzle; usar IDs originales y únicos dentro de la lección.
- `prompt`: enunciado; mantener el contenido pedagógico separado de la interacción.
- `representations`: contenido visual de lectura.
- `tools`: apoyo auxiliar de lectura o manipulable con `interactive: true`. Sus valores se guardan, pero no intervienen en la comprobación de las respuestas.
- `responses`: mecanismos independientes; sus IDs deben ser únicos dentro del puzzle.
- `validator`: criterio asociado a cada respuesta.
- `feedback`: explicación después de comprobar.

Una página puede contener varios bloques `math_puzzle`. Cada bloque cuenta como un ejercicio, aunque tenga varias respuestas. Se completa cuando todas sus respuestas son correctas.

Los nombres P01–P13 también sirven como aliases de bloque para contenido sencillo. Para ejercicios compuestos se recomienda siempre el contenedor `math_puzzle`; evita crear tipos nuevos para temas como pendiente, volumen o mediana.

## Respuestas disponibles

| Código | `responses[].type` | Datos principales |
| --- | --- | --- |
| P01 | `math_multiple_choice_single` | `options` con IDs y contenido enriquecido. |
| P02 | `math_multiple_choice_multi` | Mismo contrato de opciones; varias opciones correctas. |
| P03 | `math_numeric_input` | `label`, `unit` opcional, `keyboard`. |
| P04 | `math_multi_input` | `fields`; tabla mediante `layout`, `headers`, `rows`. |
| P05 | `math_expression_input` | Entrada de expresión con validador independiente. |
| P06 | `math_structured_answer` | Campos y `segments` que componen una respuesta. |
| P07 | `math_equation_fill_blanks` | Campos y fragmentos de ecuación mediante `segments`. |
| P08 | `math_ordering_drag` | `items`, `initialOrder`; validador de orden. |
| P09 | `math_number_line` | Modo `build_inequality`: extremo, punto abierto/cerrado y dirección. |
| P10 | `math_slider_interaction` | `parameters`, `visual`; comprobación de valores. |
| P11 | `math_matching_drag` | `sources`, `targets`; mapa de relaciones. |
| P12 | `math_coordinate_graph_builder` | `mode`, `bounds`, `snap`, `points`. |
| P13 | `math_geometry_builder` | Puntos de polígono y validación de propiedades geométricas. |

### Opciones enriquecidas y fichas

`options`, `items`, `sources` y `targets` comparten la forma:

```json
{
  "id": "fraction",
  "content": [
    { "type": "math_formula", "text": "2/4" },
    { "type": "text", "text": "Dos cuartos" }
  ]
}
```

Las opciones se identifican por `id`, no por posición ni por su texto. Una opción puede combinar fórmula, texto, mini tabla o gráfico.

Si `content` contiene la representación completa, `label` se conserva para accesibilidad y no se repite visualmente. Sin contenido enriquecido se muestra `label` una sola vez. Para agregar una explicación sin duplicar la representación, usar `explanation` únicamente en las opciones que la necesitan.

### Campos, fragmentos y celdas

```json
{
  "id": "dimensions",
  "type": "math_multi_input",
  "fields": [
    { "id": "radius", "type": "numeric", "label": "Radio", "unit": "cm", "keyboard": "NUMERIC" },
    { "id": "direction", "type": "dropdown", "label": "Dirección",
      "options": [{ "id": "left", "label": "Izquierda" }, { "id": "right", "label": "Derecha" }] }
  ],
  "validator": {
    "type": "fields",
    "fields": {
      "radius": { "type": "numeric", "expected": "3" },
      "direction": { "type": "text", "expected": "left" }
    }
  }
}
```

Tipos de campo: `numeric`, `expression`, `dropdown` y `fixed`.
Los campos fijos usan `text` y no requieren validador.

Para P06/P07 se pueden usar `segments`:

```json
"segments": [
  { "text": "5 + " },
  { "field": "added" },
  { "text": " = " },
  { "field": "total" }
]
```

Para tabla editable:

```json
"layout": "table",
"headers": ["x", "y"],
"rows": [
  [-1, { "field": "a" }],
  [0, { "field": "b" }]
]
```

Cada referencia `field` debe corresponder a un campo declarado. Validar todas las celdas editables necesarias; la tabla se comprueba como una respuesta.

### Ordenación y relaciones

P08 declara `initialOrder` con todos los IDs de sus fichas y usa:

```json
"validator": { "type": "order", "expected": ["small", "middle", "large"] }
```

P11 usa el mapa de ID de origen a ID de destino:

```json
"validator": { "type": "matching", "expected": { "sum": "five", "product": "twelve" } }
```

### Recta numérica

```json
{
  "id": "inequality",
  "type": "math_number_line",
  "mode": "build_inequality",
  "min": -5,
  "max": 5,
  "step": 1,
  "value": 0,
  "closed": false,
  "direction": "right",
  "validator": { "type": "number_line", "value": 2, "closed": true, "direction": "left" }
}
```

Las direcciones son `left` y `right`. `closed: true` incluye el extremo.
Para lectura de valor, signo, elegir representación, convertir diagrama a ecuación, pista de valor faltante o valor absoluto, usar la misma representación `math_number_line` junto con una respuesta numérica, de expresión o de selección. Esos nombres pedagógicos no requieren motores separados.

### Gráfica cartesiana y geometría

```json
{
  "id": "line",
  "type": "math_coordinate_graph_builder",
  "mode": "line",
  "bounds": { "xMin": -5, "xMax": 5, "yMin": -5, "yMax": 7 },
  "snap": 1,
  "points": [
    { "x": -2, "y": 0, "draggable": true },
    { "x": 2, "y": 0, "draggable": true }
  ],
  "validator": {
    "type": "coordinates",
    "mode": "line",
    "expected": [{ "x": 0, "y": 1 }, { "x": 2, "y": 5 }]
  }
}
```

Modos iniciales: `point`, `line`, `polygon`, `scatter`, `parabola`.
`bounds` define escalas; los máximos deben superar los mínimos.
`snap: 1` encaja en enteros; `snap: 0` desactiva el encaje.

Una recta se valida por la recta que definen dos puntos distintos, no solo por esos dos puntos concretos.
La parábola usa el primer punto como vértice y el segundo como un punto adicional; ese segundo punto necesita una x distinta de la del vértice.
En dispersión, el orden de los puntos no importa.
Una gráfica de lectura usa el mismo contrato como representación, sin convertirse por ello en una respuesta.

P13 reutiliza la base de puntos y segmentos. Ejemplo de validador:

```json
"validator": { "type": "geometry", "expected": 8, "shape": "rectangle" }
```

Aquí `expected` es el área y `shape` es una restricción opcional. No requiere unas coordenadas exactas cuando varias figuras cumplen la propiedad.

### Sliders

```json
{
  "id": "dimensions",
  "type": "math_slider_interaction",
  "parameters": [
    { "id": "width", "label": "Ancho", "min": 1, "max": 6, "step": 1, "value": 2 },
    { "id": "height", "label": "Alto", "min": 1, "max": 6, "step": 1, "value": 2 }
  ],
  "visual": { "type": "rectangle", "widthParameter": "width", "heightParameter": "height" },
  "validator": {
    "type": "sliders",
    "values": {
      "width": { "type": "numeric", "expected": "4" },
      "height": { "type": "numeric", "expected": "3" }
    }
  }
}
```

El visual rectangular puede usar `scaleParameter`, `baseWidth` y `baseHeight`.
El visual `graph` puede asociar `slopeParameter` e `interceptParameter`, con `bounds`.
La interacción sigue siendo de parámetros; el tema puede ser geometría o funciones.
Usar entre 1 y 32 parámetros con IDs distintos y no vacíos. Los límites deben ser finitos, cumplir min < max y estar entre −1000000 y 1000000; step debe ser positivo, no superar el rango y producir como máximo 10000 pasos. El extremo máximo sigue siendo alcanzable cuando rango/step no es entero.

### Herramientas auxiliares interactivas

Una herramienta puede reutilizar una gráfica, un constructor de desigualdad o sliders sin convertirse en la respuesta. Ejemplo:

```json
"tools": [{
  "id": "exploration",
  "type": "math_coordinate_graph",
  "interactive": true,
  "label": "Explora la posición del punto",
  "mode": "point",
  "bounds": { "xMin": -5, "xMax": 5, "yMin": -5, "yMax": 5 },
  "snap": 1,
  "points": [{ "x": 0, "y": 0, "draggable": true }]
}]
```

Los tipos interactivos iniciales de herramienta son `math_coordinate_graph`, `math_number_line` en modo `build_inequality` y `math_slider_interaction`. Se pueden colocar sus propiedades en un objeto `config`. Sin `interactive: true`, la herramienta se muestra como una representación de lectura. El reinicio restaura tanto respuestas como herramientas al estado inicial; tras acertar se bloquean las interacciones del puzzle.

`Comprobar` comienza gris/deshabilitado y se habilita cuando una respuesta actual difiere de su estado inicial; la comparación incluye texto, selección, dropdown, orden, matching, sliders, recta, gráfica y geometría. Deshacer todos los cambios devuelve el botón al estado inicial. Cambiar una herramienta auxiliar habilita `Reiniciar`, pero no `Comprobar`: las herramientas no son respuestas. Restaurar progreso compara las respuestas restauradas con el estado inicial; un puzzle ya correcto sigue bloqueado y puede reiniciarse.

Los editores de coordenadas y del extremo de la recta pueden contener texto parcial, por ejemplo `-`, mientras el modelo visual conserva su último valor válido. Ese borrador también cuenta como modificación y puede reiniciarse. `Comprobar` rechaza un borrador inválido con feedback; nunca valida silenciosamente el valor anterior. La restauración repone primero el modelo y después el texto escrito, sin perder ese borrador.

Para implementar controles que separen edición textual/modelo, `MathResponseControl` proporciona los hooks opcionales `hasDraftChanges()`, `draftError()`, `saveDraft()` y `restoreDraft(state)`. Sus implementaciones por defecto no alteran controles anteriores. El estado persistido admite mapas opcionales `responseDrafts` y `toolDrafts` por ID, conservando `schemaVersion: 1` y la huella del contenido. Un estado correcto restaurado se revalida y requiere que las respuestas no tengan errores de borrador. Esta extensión pertenece al estado de progreso; no exige campos nuevos en el JSON curricular.

## Representaciones y teclado

| `type` | Datos |
| --- | --- |
| `text` | `text`; `mathMode` opcional: `auto`, `text`, `inline`, `block`. |
| `math_formula` | `text`/`formula` Unicode o `latex`; `display`: `inline`/`block` (block por defecto). |
| `math_table` | `headers`, `rows` de lectura. |
| `math_number_line` | `min`, `max`, `value`, `closed`, `direction` opcional. |
| `math_coordinate_graph` | Contrato cartesiano común. |
| `geometry_2d_diagram` | `points` para polígono o `shape`: rectangle, triangle, circle; `labels`. |
| `geometry_3d_diagram` | `shape`: cube, prism, cylinder, cone, sphere; `labels`. |
| `venn_diagram` | `leftLabel`, `rightLabel`, `leftOnly`, `intersection`, `rightOnly`, `outside`, `universeLabel`. |

Los diagramas de geometría son esquemas de apoyo, no dibujos necesariamente a escala.
En la recta, `showValueLabel` permite controlar la etiqueta del punto; por defecto se oculta en los modos `read_value` y `missing_value_hint` para no revelar la respuesta.
Una opción enriquecida es una colección de estas representaciones, no un renderer temático nuevo.
Las transformaciones geométricas avanzadas quedan como extensión futura.

### Render matemático local e inline

`MathTextView` y `MathTextRenderer` componen matemáticas mediante spans dibujados localmente, sin WebView ni servicio remoto. Las fracciones tienen numerador, barra y denominador; los exponentes/subíndices tienen su propia posición y tamaño relativo. Se admiten fracciones numéricas como `1/2`, expresiones/relaciones Unicode y un subconjunto de LaTeX: `\frac{a}{b}`, `\dfrac`, `\tfrac`, `^`, `_`, grupos, `\sqrt{a}`, `\pi`, `\theta`, `\times`, `\cdot`, `\div`, `\leq`, `\geq`, `\neq`, `\infty`, `\cup`, `\cap`, `\in`, `\left`, `\right`, `\text`, `\mathrm` y `\operatorname`. Las raíces cuadradas simples se dibujan con signo y barra superior; no se admiten índices de raíz.

```json
{ "type": "math_formula", "latex": "\\frac{1}{2} + x^{2} \\leq 3", "display": "block" }
```

Los campos nuevos son opcionales y aditivos: el contenido anterior conserva `schemaVersion: 1`. `latex` tiene prioridad sobre `text` cuando ambos existen, sin crear una segunda línea visual. `display: block` usa una escala base moderadamente mayor que el texto y permite desplazamiento horizontal de fórmulas largas; `inline` conserva la escala base de la prosa.

La prosa conserva su fuente normal. En modo automático se reconocen fragmentos inequívocos (fracción numérica, coordenadas, ecuación/relación corta, producto, potencia, símbolos) y snippets completos. Para marcar una expresión dentro de una frase, escribir delimitadores `\( ... \)`, `\[ ... \]` o `$ ... $`; no convertir todo el párrafo a LaTeX. `mathMode: text` conserva literalmente una representación textual. La escala del usuario cambia el Paint del TextView, así que la composición de fracciones/scripts y su espacio vertical se recalculan con la misma proporción que el texto ordinario.

Este renderer es un subconjunto de presentación, no un motor TeX completo ni un editor de fórmulas: no admite matrices, entornos aligned, macros, integrales/sumatorias con límites o saltos TeX. Los comandos no reconocidos permanecen visibles; no se evalúan ni se usan para validar la respuesta. Las respuestas siguen aceptando la sintaxis matemática existente del validador.

Las gráficas, rectas y diagramas usan un contenedor expandible común: el icono se encuentra fuera del canvas y el fullscreen mueve la misma View, conservando sus listeners y estado. La salida de 48 dp está en la esquina inferior derecha, en una fila propia que respeta barras del sistema, cutout y gestos obligatorios. Las alturas inline siguen el ancho disponible mediante un aspect ratio; no dependen de un ancho/alto fijo de teléfono. `ComponentDialog` conecta `OnBackPressedDispatcher` al Back oficial de Android: primero cierra la representación y permanece en la misma lección. La tarjeta matemática completa sigue esta misma prioridad; un Back posterior continúa la navegación habitual.

`MathFullscreenGestureCapability.allowEdgeSwipeToDismiss` declara si sería seguro un swipe de borde propio de la app. Los gráficos y rectas creados interactivos conservan `false` incluso al completarse; geometría manipulable comparte esa política. Los diagramas estáticos conocidos declaran `true`; superficies desconocidas (incluidos futuros matching/ordering ampliados) usan `false`. El wrapper consulta la capacidad del componente, sin IDs de páginas. No se instala un swipe propio ni se bloquea el Back oficial del sistema.

`keyboard` orienta el teclado Android (`NUMERIC`, `FRACTION`, `ALGEBRA`, `INEQUALITY`, `TRIG`, `CALCULUS`); el validador determina qué respuesta es válida. La barra auxiliar excluye dígitos y variables: los introduce el IME normal. `MathShortcutResolver` infiere símbolos de la notación esperada del validador de cada campo, incluidos los hijos de `validator.fields`. Ejemplos: entero positivo → `[]`; `3/4` → `["/"]`; `4π` o expresión simbólica `4pi` → `["π"]`; `y=3x+2` → `["=", "+"]`; literal `x≤3` → `["≤"]`. No añade toda la gramática de expresiones equivalentes: hay infinitas formas válidas que introducirían operaciones redundantes. Una aproximación NUMERIC con validador `tolerance` y esperado `1/3` ofrece separador decimal, manteniendo una respuesta decimal válida dentro del margen actual. El modo sirve de fallback para extensiones sin una notación conocida; no añade desigualdades a un validador numérico o polinómico.

Se reutiliza `keyboardSymbols`, sin una nueva propiedad `shortcuts`: es un array opcional por respuesta o campo, con prioridad del campo. Omitirlo activa inferencia; `[]` es adecuado para un entero positivo. El parser rechaza arrays mal formados, duplicados, letras/dígitos, símbolos requeridos ausentes y símbolos ajenos a la notación esperada. Acepta aliases admitidos por el validador, por ejemplo `/` y el control de fracción `a/b`, o `()` y los dos paréntesis por separado. Los validadores literales conservan su sintaxis exacta. Una extensión de validador registrada puede declarar sus símbolos explícitamente. Declarar teclas no amplía las operaciones que acepta el validador; raíces y desigualdades no se convierten en operaciones del validador polinómico.

Los editores de coordenadas/recta usan otra política compartida: solo signo y separador decimal, derivados de límites, valores iniciales/esperados y snap/step. El signo no se asigna por el índice de un punto porque hay respuestas de gráfica equivalentes, permutadas o trasladadas. Estos editores parsean Double, por lo que no ofrecen `/`, π ni `%`. `MathKeyboardContext` mantiene la resolución del campo al trasladar la barra al popup, sin alterar tags de gestos. `()` envuelve la selección; borrar permanece como utilidad de edición.

`MathShortcutContentTest` recorre todos los puzzles JSON matemáticos y valida cada campo, además de casos de π, enteros, fracciones, decimales, ecuaciones, relaciones literales, overrides inválidos y límites de editores visuales. Al añadir contenido debe mantenerse la auditoría genérica y actualizar el inventario de fixtures de desarrollo cuando cambien las cantidades.

La barra se aloja inmediatamente sobre el IME en un host común de la lección; los botones tienen área táctil cuadrada de 48 dp y desplazamiento horizontal. El scroll del campo activo utiliza los insets del IME y la altura medida de la barra, no un offset específico de un teléfono.

Los campos NUMERIC/FRACTION piden el teclado numérico Android sin filtrar los símbolos insertados por la barra. Los inputs matemáticos desactivan el editor extract/fullscreen del IME para mantener visible la lección en horizontal. Si el espacio disponible no alcanza para campo, margen y barra, el host oculta temporalmente la cabecera de la lección y la restaura al terminar la edición. Next sigue el orden visual y selecciona el contenido del siguiente campo.

## Validadores y límites

| `validator.type` | Uso |
| --- | --- |
| `numeric` | Comparación racional exacta por defecto; aproximación solo con `tolerance` declarada. |
| `tolerance` | Comparación con margen absoluto; declarar `tolerance` explícitamente (valor por defecto 0,000001). |
| `exact_fraction` | Comparación de fracciones exactas. |
| `equivalent_expression` | Equivalencia limitada de expresiones; no es un CAS general. |
| `text` | IDs de dropdown o texto controlado dentro de `fields`. |
| `choice` | `expected` contiene IDs seleccionados. |
| `fields` | Cada entrada de `fields` tiene su propio validador. |
| `order` | Secuencia esperada de IDs. |
| `matching` | Mapa origen → destino. |
| `number_line` | Valor, inclusión del extremo y dirección. |
| `coordinates` | Coordenadas o propiedad del modo gráfico. |
| `geometry` | Área y restricción básica de forma. |
| `sliders` | Validadores por ID de parámetro. |

- Usar números esperados como strings cuando interesa conservar la forma de una fracción.
- Indicar la tolerancia en el contenido cuando sea pedagógicamente relevante.
- `numeric` distingue racionales diferentes incluso cuando al convertirlos a Double se redondean igual; usar `tolerance` cuando se pide una aproximación. Los controles gráficos y los valores Double de sliders emplean una tolerancia pequeña para los errores de punto flotante.
- No utilizar comparación literal como sustituto de equivalencia matemática.
- El parser numérico admite enteros, negativos, coma/punto decimal, porcentajes y fracciones con signo. `exact_fraction` normaliza racionales: 6/8 y 0,75 equivalen exactamente a 3/4.
- `equivalent_expression` normaliza polinomios exactamente, con coeficientes racionales y π formal. Admite +, −, *, /, ^ y multiplicación implícita; división solo entre constantes racionales, exponentes enteros de 0 a 8, grado total máximo 16, hasta 128 términos y entrada de hasta 256 caracteres.
- Las expresiones admiten una variable; las ecuaciones hasta dos, comparando ambos lados por un factor racional no nulo. Las variables son letras a–z salvo e/i. No se admite dividir entre variables o π, ni funciones trigonométricas, exponenciales o logaritmos. La equivalencia no utiliza muestreo de valores.
- No asumir equivalencia simbólica general, demostraciones, integración, sistemas, regiones ni manipulación al estilo de GeoGebra.
- El render local de fórmulas cubre el subconjunto descrito arriba; no hay motor TeX general ni editor bidimensional.
- La geometría inicial trabaja con polígonos simples y propiedades básicas; no autorizar figuras degeneradas o autointersecadas como ejemplos.
- Los modos futuros `function`, `system` y `region` no forman parte de este contrato inicial.
- Un validador incompatible debe corregirse en el JSON; no presentar contenido inválido como una respuesta correcta.

## Soluciones para revisión manual

| Página | Puzzle | Solución |
| --- | --- | --- |
| 1 | Números y fracciones | 7,5; 3/4; 0,33 para la aproximación de 1/3 (tolerancia 0,005). |
| 2 | Selección única | Primera opción: y = 2x + 1, ID `linear`. |
| 3 | Selección múltiple | 2/4, 0,5 y tabla 3 de 6; excluir 2/3. |
| 4 | Tabla editable | −1, 1, 5. |
| 5 | Relación algebraica | y = 3x + 2; ejemplo equivalente y = 2 + 3*x. |
| 5 | Producto con pi | 4π. |
| 6 | Comparación estructurada | −2 < 1. |
| 7 | Huecos | 5 + 4 = 9. |
| 8 | Orden + mediana | −3, 0, 2, 5, 8; mediana 2. |
| 9 | Relacionar | 2 + 3 → 5; 3 × 4 → 12; 9 − 2 → 7. |
| 10 | Desigualdad | Extremo 2, cerrado, dirección izquierda. |
| 10 | Distancia | 3. |
| 11 | Punto + número | (2, 3); coordenada x = 2. |
| 12 | Recta | Dos puntos distintos en y = 2x + 1; por ejemplo (0, 1) y (2, 5). |
| 12 | Dispersión | (−2, 1), (0, 3), (2, 2), en cualquier orden. |
| 13 | Triángulo | (0, 0), (3, 0), (0, 2). |
| 13 | Parábola | Primer punto (1, −1); segundo (2, 1). |
| 14 | Rectángulo | Por ejemplo (0, 0), (4, 0), (4, 2), (0, 2); área 8. |
| 15 | Sliders | Ancho 4, alto 3. |
| 16 | Venn + tabla | Total A = 6; total B = 5; unión = 9. |
| 16 | Caja 3D + número | 24 cm³. |

Revisar una respuesta incorrecta y otra correcta, una entrada parcial, reinicio, salir y volver, cambio de página, bloqueo de Continuar, fullscreen y restauración. En un puzzle compuesto, acertar una sola respuesta todavía debe impedir completar el ejercicio. Comprobar que el arrastre o los sliders no disparan navegación lateral.

## Persistencia y evolución del contenido

- Las respuestas se guardan con la infraestructura existente por ID de lección, índice de página e índice de bloque.
- El ID del puzzle documenta su intención; no sustituye la clave de progreso existente.
- Cada estado matemático incluye una huella SHA-256 del bloque JSON. Tanto el renderer como el cálculo de progreso requieren que coincida con el contenido actual y vuelven a validar todas las respuestas; una bandera `correct` antigua o manipulada no basta.
- Cambiar el enunciado, los datos, un validador o cualquier propiedad del bloque cambia esa huella y descarta la respuesta guardada de ese puzzle. Mantener la revisión del contenido bajo control al publicar actualizaciones.
- Mantener estables el orden y la posición de páginas/bloques publicados, o preparar una migración de estado.
- Los IDs de lecciones y cursos deben ser únicos entre rutas; las claves existentes de progreso no incluyen el ID de ruta.
- No cambiar IDs ni formatos de los puzzles de programación para introducir contenido matemático.
- Las plantillas `error_analysis`, `missing_step_analysis`, `word_problem` y `multiple_representation` se expresan con enunciados, representaciones y respuestas. No necesitan motores temáticos propios.

El resultado del build y el siguiente paso de desarrollo se registran en `MATH_PUZZLE_WORK_STATUS.md`, en la raíz del repositorio.

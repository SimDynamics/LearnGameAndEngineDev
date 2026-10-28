# Conservación de estado al reordenar una lección

## Alcance

`stateLayout` permite permutar páginas y mover bloques existentes entre páginas
sin cambiar la dirección original con la que se guardaron sus respuestas,
intentos y borradores. La misma identidad se utiliza para relacionar la lectura
con el contenido original. El código anterior a esta capacidad guardaba parte
de ese estado por índice de página y bloque; conservar solamente el `id` del
puzzle no protegía los demás datos.

Esta integración no reordena ningún curso automáticamente. Work debe preparar
un mapa de identidades a partir de la versión publicada **antes del primer
reordenamiento** y conservarlo en todos los cambios posteriores. Se puede
publicar el mapa y el primer reordenamiento juntos: el estudiante no necesita
instalar una versión intermedia para que sus coordenadas antiguas sigan siendo
las mismas.

El contrato de versión 1 admite permutaciones. No autoriza añadir, eliminar,
dividir ni fusionar páginas o bloques, modificar respuestas, cambiar opciones
de lugar dentro de un ejercicio, ni sustituir el programa de un laboratorio.
Esas operaciones necesitan una migración diferente y explícita. Los corazones,
las compras y los archivos de audio no forman parte de este contrato.

En versión 1, la primera adopción y cada reordenamiento conservan el contenido
completo de los bloques, incluidas sus explicaciones. Las revisiones editoriales se publican
antes o después, con el orden estable. Separar esas transiciones evita que dos
párrafos con la misma forma intercambien por accidente su cobertura de lectura;
sus firmas de estado excluyen el texto para admitir correcciones posteriores.

## Contrato v1 e invariantes compartidas

El objeto de lección conserva sus campos anteriores y añade `stateLayout` con
estas tres claves:

| Campo | Significado |
| --- | --- |
| `version` | Entero `1`. Otras versiones y `null` se rechazan. |
| `origin` | Lista inmutable de páginas originales. Cada página tiene `id` y una lista `blocks`; cada bloque tiene `id` y `sha256`. El índice de cada entrada es su dirección histórica de almacenamiento. |
| `current` | Lista de páginas en orden visible. Cada página tiene `id` y una lista `blocks` de identificadores, en el orden visible actual. |

El generador asigna `page_0000`, `block_0000_0000`, etcétera, tomando como base
las coordenadas publicadas. Los nombres representan **el origen**, no el índice
que ocupará el elemento después de moverlo. También reciben identidad los
párrafos y los editores que no tenían un ID curricular.

Las páginas actuales deben contener exactamente los mismos identificadores de
página que el origen, una sola vez cada uno. Los bloques actuales deben contener
exactamente los mismos identificadores de bloque, también una sola vez, aunque
se trasladen a otra página. El número de bloques por página puede cambiar; el
número total de páginas y el conjunto total de bloques permanecen iguales.

`sha256` no es el hash del archivo ni de su indentación. Se calcula con la
representación canónica compartida por Python y Kotlin del contrato del bloque.
Protege los tipos, identificadores, código, respuestas, opciones y reglas de
evaluación. Se excluyen campos editoriales concretos de texto o retroalimentación
según `state_payload`; el validador curricular existente sigue decidiendo qué
revisiones editoriales están autorizadas. Los puzzles matemáticos mantienen la
huella de su payload completo, con la excepción exacta descrita abajo. No se deben editar ni recalcular los hashes de
origen para hacer pasar una modificación incompatible.

Una lección sin `stateLayout` conserva el comportamiento posicional anterior.
Un mapa presente pero inválido no se trata como una lección antigua. Una vez
publicado el mapa, no se puede retirarlo ni regenerar el origen después de mover
contenido. Reiniciar el progreso de un estudiante tampoco cambia ese contrato
curricular.

El progreso de una lección con mapa conserva la agrupación y el peso de sus
páginas originales. Redistribuir ejercicios correctos y pendientes entre las
páginas visibles no cambia por sí solo el porcentaje ni la finalización. Para
evaluar cada grupo se localizan sus bloques en la presentación actual y se leen
sus respuestas en las direcciones originales. La cobertura de teoría se calcula
con las identidades de los bloques del grupo; una página original vacía usa su
identidad de página. La navegación y la comprobación de ejercicios pendientes
continúan utilizando las páginas visibles.

Las huellas matemáticas existentes dependen también del orden de las claves
JSON. La puerta de transición conserva ese orden, incluso dentro de objetos
anidados. No se deben ordenar las claves de un bloque matemático al trasladarlo;
su hash canónico de `stateLayout` no sustituye su huella histórica de respuesta.

El lector conserva su límite de tres posiciones, incluidas las lecturas
finalizadas. Las posiciones añaden identidades estables de página y bloque para
reubicar la frase correspondiente cuando cambia el orden. En la actualización
directa, la integración reconstruye el guion en el orden de origen y contrasta
la identidad del guion antiguo. Una pausa solo conserva sus milisegundos cuando
coinciden el bloque, la frase o audio, la ocurrencia, el idioma y la voz. Si no
se demuestra esa correspondencia, se conserva la posición anterior sin inventar
un tiempo de reproducción para un audio distinto. Este mecanismo no amplía la
cuota de posiciones ni borra audios.

## Idiomas en v1

Todos los archivos de idioma que se distribuyan para una misma lección deben
adoptar el mapa en la misma publicación. El origen de ES, EN y FR debe describir
las mismas identidades y actividades. Sus hashes pueden diferir porque cada
idioma tiene su propio texto y respuestas traducidas. Su orden visible también
puede diferir si cada archivo conserva el mapa correcto.

El control de familias recorre todas las rutas y cursos bajo
`app/src/main/assets/content`, no solo Programación 1 en español. Un idioma que
todavía utiliza la lección de respaldo y no tiene archivo propio no necesita un
archivo artificial. Cuando se añada su traducción, debe heredar el mismo origen
que los idiomas ya publicados. Una traducción nueva sin mapa se rechaza si la
familia ya lo utiliza.

## Ampliación v2: idiomas históricos, altas y revisión editorial

La versión 2 permite adoptar el mapa en español conservando intactos los archivos
ingleses y franceses publicados, aunque históricamente tengan otras actividades.
No se reconstruyen traducciones ni se rellenan sus orígenes para hacerlos coincidir.
La primera familia se ancla al SHA publicado; al adoptar después otro idioma se
reutiliza esa misma familia, sin cambiar la referencia histórica.

El objeto `stateLayout` v2 contiene estos campos obligatorios y admite los archivos
opcionales `retired`, `retirementHistory`, `orderRefinements`,
`choiceRevisions` y `compilerRevisions` descritos más abajo:

| Campo | Contrato |
| --- | --- |
| `version` | Entero `2`. |
| `locale` | Idioma real del asset: `es`, `en` o `fr`. El respaldo español sigue siendo español aunque la interfaz use otro idioma. |
| `origin` | Origen inmutable del idioma seleccionado, con el formato v1. |
| `current` | Páginas originales y bloques visibles; incluye explícitamente los bloques nuevos. |
| `family` | Por idioma publicado: `sha256` del documento histórico sin `stateLayout` y su propio `origin`. Se calculan desde Git, sin exportar copias históricas. |
| `additions` | Altas con `id`, `sha256`, `storage: {page, block}` y `progressPage`. |
| `editorial` | Pruebas encadenadas `{id, beforeSha256, afterSha256}` de los bloques completos cuya prosa cambia. |
| `retired` | Opcional. Archivo acumulativo de bloques retirados del recorrido, con `{id, page, next, block}`. No sustituye ni modifica `origin`, `family` o `additions`. |
| `retirementHistory` | Opcional para archivos anteriores. Presentaciones anteriores completas, una por lote nuevo de retiro, para comprobar las identidades y dependencias de lectura publicadas. |
| `choiceRevisions` | Opcional. Archivo acumulativo de las cinco revisiones de opción españolas autorizadas, con `{id, block}` y el bloque publicado anterior íntegro. No permite sustituciones generales de respuestas. |
| `compilerRevisions` | Opcional. Archivo acumulativo de las cuatro ampliaciones españolas de comprobación autorizadas, con `{id, block}` y el editor publicado anterior íntegro. U11L8/L10/L11 incluyen obligatoriamente `references: [{id, block}]` con su referencia completa anterior. No permite cambiar otros programas o esperados. |

Los orígenes y hashes familiares no se regeneran. Los archivos hermanos sin mapa
deben conservar exactamente su documento publicado; la puerta verifica esa
condición. Las fijaciones de identidad y la cobertura de lectura son independientes
por idioma real. Un mapa retirado de su idioma o un origen incompatible se rechaza
sin reinterpretar datos. Los mapas v1 siguen funcionando y pueden adoptar v2 sólo
si se demuestra su origen anterior; sus metadatos se conservan.

Cada alta tiene identidad y dirección reservadas, fuera de las páginas históricas
de todos los idiomas de la familia. Las altas son acumulativas: no se pueden
borrar, reasignar ni reciclar sus direcciones. Su retiro del recorrido requiere
el archivo explícito descrito más abajo. `progressPage` asigna el bloque
a un grupo de progreso original. El peso de las páginas originales se conserva;
una actividad nueva comienza pendiente y puede añadir un requisito a su grupo.
Esta ampliación no añade, divide ni fusiona páginas.

Las respuestas, intentos y borradores de las altas usan además un espacio de
claves derivado de la familia, idioma e identidad de alta. Ni siquiera un dato
posicional antiguo en su dirección reservada se hereda. Los editores históricos
mantienen exactamente sus claves, incluido el propietario del historial de proyecto.

V2 permite revisión editorial y permutación en una misma entrega. El generador
relaciona cada bloque con su identidad anterior y registra hashes completos antes
y después. La puerta exige una cadena continua, conserva las pruebas anteriores
y contrasta el resultado con el archivo publicado. La firma de estado del bloque
debe permanecer igual. El validador curricular sigue autorizando las revisiones:
no se flexibilizan respuestas ni código.

### Corrección exacta de un rótulo matemático en Windows

`CPP_CORRECCION_EDITORIAL_MATH_PUZZLE_CON_ESTADO` admite exclusivamente en
U9L13, `block_0007_0001`, respuesta `contratos`, origen `construccion`, el cambio
de «Construcción que termina normalmente» a «Construcción desde código y unidades
que termina normalmente». Work utiliza `prepare_v2` con la familia publicada y
el documento anterior, conservando `origin`, `family` y las direcciones; el helper
añade la prueba editorial completa. No hay que añadir un campo al puzzle.

La autorización contrasta el bloque completo antes/después. Pares, validadores,
IDs, otros textos y orden de claves JSON se conservan. El cargador Windows exige
su identidad, idioma español y prueba exacta. El puente matemático conserva la
huella de respuesta anterior sólo para el bloque revisado completo; la vista y
el lector reciben el rótulo nuevo. Cualquier otro cambio sigue necesitando su
contrato propio. Esta excepción Windows no cambia el motor Android conservado.

## Cinco revisiones exactas de opciones en Windows

`CPP_DISTRACTOR_EDITORIAL_CON_ESTADO_U11L5` admite exclusivamente una opción en
U11L5 (`block_0006_0004`, índice 2), U11L6 (`block_0005_0004`, índice 2),
U11L7 (`block_0000_0004`, índice 0) y dos en U11L8 (`block_0003_0002` y
`block_0006_0001`, ambas de índice 2). La identidad completa de la lección,
el idioma español, el índice y los contratos protegidos antes/después forman
parte de la autorización cerrada. U11L7 ajusta el nombre del campo a «Entrada»;
las otras cuatro revisan un distractor y su guía correspondiente. Se mantienen
el índice correcto, las demás opciones, la pregunta, la explicación, los IDs,
las dependencias, los programas y las direcciones históricas.

`revise_choice_option(documento_v2, identidad_estable)` prepara una copia en
memoria, añade a `stateLayout.choiceRevisions` una entrada
`{"id": "identidad_estable", "block": bloque_publicado_anterior_completo}`
y añade la arista editorial exacta del bloque anterior a la primera versión
revisada, incluida su guía. El CLI `revise-choice-option` usa `--identity`,
`--locale es`, `--base-ref` publicado y `--output` nuevo. No modifica el asset
de origen ni consulta progreso. Si falta el mapa, primero se prepara v2 con
la familia publicada; no se regeneran familias ni orígenes existentes.

El archivo anterior es inmutable y acumulativo. Una transición que lo añada
debe conservar exactamente el bloque completo de la publicación anterior;
no basta fabricar un snapshot con el mismo tipo de ejercicio. El cargador y
las herramientas exigen su firma completa, la arista editorial revisada dentro
de la cadena y el contrato protegido nuevo exacto. Una versión sin mapa, v1,
otro idioma, otra identidad o una alteración de otra opción se rechaza.
La firma de estado conserva el contrato protegido histórico sólo para esas
cinco sustituciones. No se cambian los hashes de `origin`, `family` o
`additions`, ni se reasignan posiciones.

Las revisiones posteriores de pregunta, explicación o guías conservan las
reglas editoriales ordinarias: añaden su procedencia sin reescribir el snapshot
original ni sustituir la primera arista revisada. La proyección usada por el
validador curricular restaura exclusivamente la opción histórica, después de
validar la transición completa; no oculta cambios de prosa ni añade excepciones
generales para opciones. La revisión de otra opción necesita su propio contrato.

El soporte técnico no activa automáticamente estas sustituciones en los JSON.
Work integra las cinco revisiones y comprueba rechazo, acierto, reapertura,
presentación y escucha. Los intentos históricos conservan su respuesta y su
procedencia; una elección anterior no se presenta como una elección del texto
nuevo. La aceptación curricular y visual queda separada de las pruebas de
contrato Python y de conservación nativa Windows.

## Cuatro ampliaciones exactas de comprobación en Windows

`CPP_CASOS_ADICIONALES_CON_PROGRESO` admite exclusivamente las ampliaciones
revisadas de U11L7 (`block_0003_0005`), U11L8 (`block_0004_0004`), U11L10 y
U11L11 (ambas `block_0004_0003` de sus respectivas lecciones). La primera añade
dos casos al final de los 19 publicados, manteniendo cada caso previo y su orden.
Las otras tres añaden únicamente sus informes revisados al esperado de su caso
existente y las llamadas correspondientes al `main` de su referencia estática
completa posterior. Los programas iniciales del editor, entradas iniciales, modos
de comparación, propietarios y demás campos protegidos permanecen iguales.

`revise_compiler_cases(documento_v2, identidad_estable)` prepara una copia en
memoria y añade a `stateLayout.compilerRevisions` una entrada
`{"id": "identidad_estable", "block": editor_publicado_anterior_completo}`.
Para U11L8/L10/L11 añade también `references: [{"id": "referencia_estable",
"block": referencia_publicada_anterior_completa}]`, modifica esa referencia con
las llamadas exactas antes del `return 0;` final y conserva ambas aristas
editoriales anteriores/ampliadas. U11L8 usa `block_0007_0001`; U11L10/L11 usan
`block_0008_0001` de su lección correspondiente. El CLI
`revise-compiler-cases` recibe `--identity`, `--locale es`, el `--base-ref`
publicado y un `--output` nuevo; nunca sobrescribe el asset ni una propuesta
existente. No consulta preferencias ni progreso del estudiante.

La autorización contrasta la lección española, identidades estables, bloques
originales completos, contratos ampliados y aristas iniciales exactas dentro de
la cadena editorial. El esperado y su referencia crecen juntos; una ampliación
parcial o una referencia prestada de otro editor se rechaza. Ambos bloques
conservan sus identidades activas; no se retiran como parte de esta revisión.
El archivo se conserva acumulativo e inmutable. No se sustituyen `origin`,
`family`, `additions` ni direcciones históricas. Quitar el archivo, cambiar su
propietario, restablecer el contrato antiguo como si no hubiera revisión o
inventar el contrato ampliado dentro de una presentación archivada se rechaza.
Las presentaciones anteriores al cambio conservan su contrato anterior; un
retiro posterior de otros bloques conserva la presentación ampliada real.

La proyección para el validador curricular restaura únicamente esos casos o
esperados y las tres referencias exactas después de validar la transición
completa. Las guías posteriores
siguen pasando por las guardas editoriales existentes. Añadir una exigencia
`standardError: "nonBlank"` conserva su regla propia: una publicación posterior
no puede retirarla. Las cuatro revisiones no autorizan respuestas generales,
otras ampliaciones de casos ni cambios de fuentes.

Las pruebas de ejecución anteriores permanecen vinculadas a su objetivo,
idioma y programa capturado. En U11L7 sólo las pruebas válidas de casos idénticos
del mismo programa pueden contribuir a los 21; los dos casos nuevos siguen
pendientes. En las otras tres actividades el esperado completo cambia: el logro
anterior conserva su procedencia, pero la ampliación necesita una nueva ejecución
completa. Una acreditación anterior no se convierte en prueba del contraste nuevo.
Reiniciar explícitamente impide recuperar pruebas anteriores como intento activo.

El soporte no modifica automáticamente los JSON. Work incorpora las consignas,
casos y referencias correspondientes después de preparar esta transición y
comprueba su aceptación curricular. La capacidad descrita es Windows; no acredita
soporte de `compilerRevisions` en Android ni escucha o aceptación visual completa.

## Piezas independientes de una línea en Windows

`refine_order_lines(documento_v2, identidad_estable)` prepara la presentación
de un `order_code` español como líneas independientes. Para un documento aún
sin mapa, primero se usa `prepare_v2` con los tres idiomas publicados; no se
regenera un origen existente. La opción CLI equivalente es `refine-order-lines`
con `--identity`, `--locale es`, `--base-ref` publicado y `--output` nuevo.

La extensión opcional `stateLayout.orderRefinements` contiene entradas
`{"id": "identidad_estable", "block": bloque_original_completo}`. El bloque
original queda íntegro, incluidas sangría, líneas vacías y separadores. Se
verifica contra su firma histórica y la procedencia editorial antes/después.
Los fragmentos actuales deben ser exactamente las líneas no vacías del original;
`correctOrder` debe ser la expansión de sus grupos. No se divide el escape
escrito `\n` dentro de un literal. Sólo el fragmento compacto exacto
`if (validar(candidata)) { tabla = candidata; }` tiene tres cortes explícitos;
sus espacios originales se conservan al principio de las dos últimas piezas.
Un refinamiento publicado no se elimina ni se reemplaza.
Se rechazan las continuaciones físicas con barra inversa y los literales crudos
multilínea: su separación requiere un contrato adicional para no cambiar el
programa. Los literales crudos de una sola línea conservan sus caracteres.

Windows proyecta los intentos anteriores por grupos, conserva los metadatos y
guarda las respuestas de la representación nueva en una clave separada por
idioma y firma de representación. La respuesta histórica permanece intacta para
las traducciones conservadas. Un acierto sólo se mantiene si la secuencia de
textos sigue siendo correcta. Intercambiar piezas de texto idéntico es válido;
omitir o repetir índices no lo es. Los consumidores de estado deben usar las
sobrecargas con `Lesson` y `Block`, no interpretar la respuesta histórica como
si ya tuviera el nuevo número de piezas. El soporte corresponde a Windows;
las pruebas JVM o de Android anteriores conservan su alcance propio.

### Órdenes equivalentes revisados

Windows acepta también los intercambios independientes revisados en los cinco
puzzles españoles de copiar antes de modificar, acumular y contar, validar antes
de calcular, recuperar por operación y extraer un tipo sin cambiarlo en fallo.
La autorización exige la lección, identidad estable y firma exacta del contrato
de código y respuesta. No se añade un campo JSON ni se infiere equivalencia de
otros programas. Cada pieza física debe aparecer una vez; sólo se intercambian
los textos de los grupos revisados en sus posiciones canónicas. Las dependencias
y el resto de las posiciones siguen siendo obligatorios.

Esta ampliación conserva las claves, representación, borradores, intentos y
veredictos guardados. Un intento antes incorrecto no se convierte en acierto al
abrirlo: el estudiante debe volver a comprobarlo. Las traducciones y las copias
históricas de los grupos anteriores conservan su evaluación propia.

## Procedimiento v2 para Work

Usar el SHA publicado verificado, no un commit que ya contenga el mapa que se
quiere aprobar. El generador lee las versiones publicadas de los idiomas desde
Git en memoria. No necesita respaldos de lecciones, otra rama ni publicaciones
intermedias. Las modificaciones se guardan localmente; el usuario publica.

El plan es una lista con el formato de `current`. Los identificadores históricos
se derivan de sus índices originales desde cero: `page_0000`, `block_0000_0000`.
No se renombran al moverlos. Para un bloque nuevo, el plan incluye también el
identificador que Work elija. Su entrada de autoría en la lista de altas es:

```json
{
  "id": "ejecucion_original",
  "storage": {"page": 6, "block": 0},
  "progressPage": "page_0004",
  "block": {"type": "cpp_compiler", "initialCode": "PROGRAMA_DE_LA_ACTIVIDAD"}
}
```

El ejemplo sólo describe la forma de la entrada; Work define el programa,
objetivo y contrato real de la actividad. El `sha256` lo calcula la herramienta.
El índice reservado `6` sólo es válido si ninguna variante histórica tiene más
de seis páginas. Se comprueban los bloques añadidos con el validador completo.

```sh
python scripts/curriculum/stable_state_layout.py prepare RUTA_DEL_ASSET.json --locale es --base-ref SHA_PUBLICADO --plan plan.json --additions altas.json --output propuesta.json
python scripts/curriculum/stable_state_layout.py check RUTA_DEL_ASSET.json --locale es --base-ref SHA_PUBLICADO
python scripts/curriculum/stable_state_layout.py check-tree . --base-ref SHA_PUBLICADO
python scripts/curriculum/validar_json_programacion.py --base-ref SHA_PUBLICADO
python -m unittest discover -s scripts/curriculum -p "test_*.py"
```

`--additions` es opcional; `--plan` permite preparar y permutar en una operación.
El destino nuevo contiene la propuesta para incorporar al asset original; no es
un respaldo histórico. Después de incorporarla se ejecutan `check`, `check-tree`
y el validador contra el mismo SHA. Las puertas no acreditan ejecución Android.
No se deben generar archivos de traducción cuando no se esté traduciendo.

## Actividades dependientes en Windows

La propiedad raíz opcional `activityAfter` puede proteger un compilador o un
puzzle que recupera la implementación de una construcción anterior. Usa las
identidades estables de `stateLayout.current`, que pueden ser distintas del
campo `id` curricular del puzzle:

```json
"activityAfter": [
  {
    "activity": "puzzle_de_recuperacion",
    "requires": ["constructor_previo"]
  }
]
```

El ejemplo describe la relación, sin añadir esas identidades a ninguna lección.
`activity` admite `cpp_compiler`, `multiple_choice`, `fill_code`,
`output_prediction`, `order_code`, `drag_fill_code`, `inline_fill_code` o
`math_puzzle`. Cada identidad de `requires` debe ser un `cpp_compiler` con
`outputCheck` y debe estar antes del destino en el orden visual vigente, en la
misma página o en una anterior. Se necesitan todos los requisitos declarados.
No se admiten identidades desconocidas o repetidas, listas vacías, relaciones
consigo mismas ni dependencias hacia atrás que formen ciclos. Un puzzle no
puede actuar como requisito bajo este contrato.

Antes de cumplir el requisito, Windows protege el contenido y las acciones del
destino, incluidos su código, narración y accesibilidad. El selector y la
restauración de un marcador posterior respetan ese requisito y permiten volver
a la construcción pendiente. Al resolverla reaparece el puzzle original con
su respuesta e intento guardados; la relación no responde el puzzle ni concede
otro acierto. Un antiguo `correct=true`, una predicción o visitar una página no
acreditan la construcción. Un logro completo y comprobado del compilador sigue
las reglas de Windows: editar o recompilar no lo revoca; su reinicio explícito
vuelve a dejar pendiente el requisito y conserva los datos del destino.

Estas relaciones funcionan también sin `compilerPuzzlesRequired`: expresan la
dependencia pedagógica concreta, no la obligatoriedad global de todos los
compiladores de la lección. Si el destino es un puzzle, sus compiladores
requeridos directos y transitivos son puntos de detención del lector y del
avance hasta acreditar sus objetivos, aunque se omita esa propiedad. Una cadena
aislada de compilador a compilador conserva su comportamiento anterior. El
catálogo carga y valida las fuentes y la disponibilidad de estas relaciones,
manteniendo los conteos y respuestas históricos: un progreso antiguo no autoriza
mostrar el contenido protegido. `referenceAfter` conserva su contrato anterior:
sólo conecta un compilador comprobable con bloques posteriores `heading`,
`paragraph`, `note` o `code`; no se utiliza para esconder puzzles. Los
compiladores dependientes anteriores continúan usando la misma sintaxis.

Añadir o revisar `activityAfter` no modifica los bloques, respuestas, borradores,
origen, familia, direcciones ni pruebas editoriales. Las puertas de transición
siguen protegiendo todos esos datos. Las relaciones se declaran sobre el orden
visual validado después de mover un bloque, nunca sobre su dirección histórica
de almacenamiento. Las presentaciones archivadas en `retirementHistory`
conservan sus relaciones publicadas exactas y no se reescriben al proteger un
nuevo destino activo. Work incorpora las relaciones españolas en los JSON y
realiza su aceptación curricular; no se sincronizan traducciones ni se añaden
dependencias automáticamente a los assets.

## Retiro compatible en Windows

`stateLayout.retired` permite retirar del recorrido español un compilador
redundante y sus bloques de presentación. Las páginas y `current` contienen sólo
los bloques activos. La identidad retirada sigue perteneciendo a su origen o
alta y conserva íntegro su bloque en este archivo:

```json
{
  "id": "identidad_publicada",
  "page": "page_0004",
  "next": "siguiente_identidad_activa",
  "block": {"type": "cpp_compiler", "initialCode": "CODIGO_PUBLICADO_INTACTO"}
}
```

El ejemplo ilustra la forma, no un bloque curricular nuevo. `block` debe ser el
objeto completo de la versión publicada, con todos sus campos y valores.
`page` y `next` prueban la posición anterior; `next` es la primera identidad
posterior que sigue activa en esa página, o la cadena vacía si no existe.
Si esa identidad se retira después, se sigue su propio sucesor. No se admiten
ciclos, identidades duplicadas, identidades a la vez activas y retiradas ni
contratos nuevos bajo una dirección antigua.

La primera transición contrasta el archivo con el bloque publicado completo.
Después el archivo es acumulativo e inmutable: no se puede cambiar, borrar ni
reactivar una entrada bajo el mismo contrato. Se conservan `origin`, `family`,
las altas, sus direcciones y las pruebas editoriales. También se conservan los
datos históricos del estudiante; las respuestas y borradores archivados no se
convierten en aciertos de otra actividad. La persistencia fija además la firma
completa de cada entrada archivada y rechaza su eliminación o sustitución.

Los tipos retirables son `cpp_compiler`, `heading`, `paragraph`, `note` y `code`.
Esta autorización inicial corresponde a español. No habilita borrar puzzles,
cambiar su contrato ni modificar las traducciones. El lector y el catálogo usan
únicamente los bloques activos; una posición de lector archivada se resuelve
hacia contenido activo sin aplicar sus milisegundos a otra frase ni borrar el
registro histórico.

Cada lote nuevo añade una entrada inmutable a `retirementHistory`, con estas
seis claves: `retired` (identidades retiradas por el lote, en el orden del archivo),
`current` (mapa visible anterior completo), `pages` (páginas anteriores completas,
incluidos todos sus bloques), `compilerPuzzlesRequired` (booleano efectivo anterior),
`referenceAfter` y `activityAfter` (relaciones anteriores exactas, o listas vacías).
El generador captura estos valores antes de retirar bloques o cambiar relaciones.
La puerta de transición contrasta esa captura con la versión publicada; no permite
inventar texto anterior, orden, dependencias ni reglas de avance. Una revisión de
prosa simultánea conserva así el texto que realmente escuchó el estudiante.

Este archivo es procedencia histórica para verificar una lectura anterior; sus
bloques no crean controles, actividades ni narración nueva. El lector reconstruye
el guion con el mapa y las relaciones anteriores, comprueba su identidad y sólo
entonces busca un destino activo. Un editor que antes estaba oculto por una
dependencia no se incorpora por accidente al guion anterior. Se preserva el límite
de tres posiciones y ningún tiempo escuchado acredita otro audio. La persistencia
también fija estas capturas completas de forma acumulativa. Los retiros antiguos
sin `retirementHistory` siguen siendo válidos; su ausencia no permite inventar
una presentación anterior ni omitir la captura de un lote nuevo.

La API de autoría no escribe archivos ni deduce qué dependencias deben desaparecer:

```python
from scripts.curriculum import stable_state_layout as state

propuesta = state.retire(
    leccion_publicada,
    ["consigna_redundante", "editor_redundante"],
    activity_after=dependencias_activas_explicitas,
    reference_after=referencias_activas_explicitas,
    compiler_puzzles_required=True,
)
state.validate_transition(leccion_publicada, propuesta)
```

Las relaciones no pueden seguir apuntando al archivo retirado. El autor debe
preservar explícitamente las dependencias entre los intentos útiles y comprobar
la transición contra su referencia publicada habitual. El validador curricular
recupera los bloques archivados en sus coordenadas anteriores para seguir
verificando también su contrato; archivar no elude las guardas de código o
respuestas.

`compilerPuzzlesRequired` es un booleano opcional en la raíz de la lección,
independiente del archivo. Su ausencia conserva el flujo anterior; `true` activa
la obligatoriedad de los objetivos evaluables del compilador. Una publicación
que ya lo activa no puede desactivarlo silenciosamente. La propuesta concreta
de U1 debe integrar el retiro de las tres repeticiones y esta activación juntos;
el mecanismo técnico no modifica automáticamente ningún JSON del curso.

Este contrato y sus verificadores son de Windows. No acreditan soporte ni
ejecución de esta extensión en Android.

## Lectura y alcance de la recuperación

El lector sigue usando tres posiciones recientes, incluidas las finalizadas,
y pausa liberando `MediaPlayer`. El orden nuevo no cambia el audio ni su caché.
Un bloque añadido no hereda lectura ni crédito de retroalimentación antigua.

Las posiciones con identidad estable pueden seguir al mismo bloque y audio
cuando cambian otras partes de la página. Una posición muy antigua sólo contiene
la huella de la página completa, segmento y desplazamiento. Si también cambia
su texto hablado, los hashes no permiten reconstruir el guion anterior: se
conserva ese registro sin aplicarlo a otro audio ni inventar milisegundos. Para
una página hablada intacta se puede verificar el guion de origen y recuperar
exactamente su pausa después de moverla. Los lotes de retiro con
`retirementHistory` aportan la presentación publicada verificable descrita arriba;
fuera de ese contrato no se inventa ni conserva una copia alternativa de la
lección para sortear la falta de procedencia. La política de posiciones se mantiene.

## Compatibilidad de herramientas v1

La API v1 y su opción `--previous` permanecen compatibles para las herramientas
que ya disponen de un documento anterior verificado. No se deben crear ni
conservar respaldos históricos para adoptar v2: se utiliza el procedimiento
anterior con `--locale` y `--base-ref`, que también admite un origen v1 publicado.

`check-tree` exige `--base-ref`, lo resuelve una vez a un commit y compara los
mapas con ese árbol. Detecta lecciones o mapas retirados y familias incompatibles.
El validador curricular exige la misma referencia explícita cuando hay metadatos
de estado actuales o en el HEAD local. Un commit local posterior no reemplaza
el SHA publicado usado como referencia.

El generador no consulta la red. Obtener y comprobar el HEAD remoto corresponde
al flujo de integración. Si otro trabajo publica antes de terminar, hay que
revisar el nuevo HEAD, integrar sin perder sus cambios y repetir las comprobaciones
afectadas contra esa nueva referencia. No se deben aprobar los cambios tomando
como base el propio origen regenerado que se está intentando verificar.

## Qué debe comprobar la verificación

La prueba de actualización directa debe empezar con preferencias de la versión
posicional anterior y cargar la lección ya reordenada con su mapa. Debe comprobar
por separado respuestas correctas e incorrectas, borradores de código, entrada
estándar, proyectos, intentos y progreso de lectura. Debe incluir movimientos
entre páginas, cambio de idioma, segundo reordenamiento, reinicio de la app y
reinicio explícito de la lección. Las direcciones originales se siguen usando;
no se necesita copiar, borrar ni redistribuir los datos para aplicar la permutación.

Las pruebas Python acreditan el contrato, el generador y las transiciones de
archivos. Las pruebas Kotlin de host acreditan solo lo que ejecutan en JVM. Una
compilación Gradle de la app o del APK de pruebas acredita compilación, no
ejecución. Solo una ejecución instrumentada en Android acredita las operaciones
de Android que efectivamente probó. El informe de una publicación debe mantener
separadas esas evidencias y declarar cualquier prueba en dispositivo pendiente.

## Objetivos de diagnóstico intencional en Windows

`cpp_compiler.compileCheck` incorpora un objetivo nuevo a los cinco editores
españoles de `CPP_ACREDITACION_DIAGNOSTICO_INTENCIONAL`; sus ubicaciones y valores
de `diagnostic` figuran en `docs/CPP_OUTPUT_CHECK.md`. No autoriza cambios
generales de fuentes, respuestas, propietarios o mapas.

El bloque inicial completo conserva `type` e `initialCode` exactamente como se
publicaron. Cinco fingerprints anteriores y sus parejas lección/identidad
forman el contrato cerrado. `compileCheck` declara `diagnostic` y opcionalmente
`explanation` no vacía. No se combina con salida, entrada estándar o proyectos.
Origen, familia, coordenadas, IDs, respuestas, borradores y traducciones no se
regeneran ni trasladan.

`add_compile_check(document, identity, explanation=None)` de
`scripts/curriculum/stable_state_layout.py` devuelve una propuesta independiente
con el objetivo revisado y una prueba editorial completa del bloque anterior
al actual. No escribe assets; exige el mapa v2 español existente y el editor
activo exacto. Es idempotente y no sobrescribe una explicación publicada. Work
también puede añadir el contrato al documento y usar `prepare_v2` con la lección
y la familia publicadas; la explicación puede entrar en esa misma transición,
sin un paso intermedio artificial. Validar transición y traducciones antes de
integrar.

La proyección de estado elimina sólo un `compileCheck` validado cuyo bloque
anterior coincide íntegramente con el hash revisado: conserva el contrato del
borrador histórico sin confundirlo con el objetivo nuevo. La validación exige
lección, identidad, idioma y procedencia editorial correctos también en bloques
retirados y presentaciones históricas. `referenceAfter` y `activityAfter` admiten
el objetivo con las mismas reglas de orden de los objetivos de stdout; no se
crean relaciones nuevas automáticamente.

Las transiciones no pueden retirar, sustituir o debilitar un `compileCheck`
publicado. Un retiro explícito conserva el bloque completo. Incorporar después
el objetivo no reescribe una presentación histórica anterior a él.

Windows mantiene en `learning_progress` el pin
`lesson_<lessonId>_state_compile_checks_v1_<locale>` con identidad de editor,
objetivo y SHA-256 de la fuente inicial para objetivos activos y archivados.
Adopción y comprobaciones son atómicas: metadatos o recibos incompatibles no
reemplazan datos anteriores. El pin impide aceptar después un contrato retirado
o cambiado bajo la misma identidad, incluso al cargar un asset anterior. Es
procedencia interna, no un campo de autoría ni otra base de progreso.

Las pruebas acreditan el objetivo y el programa capturados de la compilación
nueva. Un acierto antiguo o importado desde Android no prueba el rechazo
semántico; se conservan los datos y código sin fabricar recibos Windows.
Reiniciar actúa sólo sobre esa actividad. La integración y aceptación visual,
interactiva y de escucha de Work siguen siendo verificaciones independientes.

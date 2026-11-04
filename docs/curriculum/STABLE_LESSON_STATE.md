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
| `choiceRevisions` | Opcional. Archivo acumulativo de las siete revisiones de opción españolas autorizadas, con `{id, block}` y el bloque publicado anterior íntegro. U11L14 token conserva una transición atómica de opción y explicación. No permite sustituciones generales de respuestas. |
| `compilerRevisions` | Opcional. Archivo acumulativo de las siete ampliaciones españolas de comprobación autorizadas, con `{id, block}` y el editor publicado anterior íntegro. Las seis baterías programadas U11L8/L10/L11/L12/L13/L14 incluyen obligatoriamente `references: [{id, block}]` con su referencia completa anterior. No permite cambiar otros programas o esperados. |

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

### Retorno editorial explícito en Windows

Una revisión puede recuperar exactamente la presentación de una revisión anterior
del mismo bloque. No se restauran los metadatos antiguos ni se quitan pruebas de
la lista `editorial`: se añade una arista nueva. Las aristas ordinarias siguen
rechazando firmas repetidas y toda arista sigue exigiendo un cambio real.

El retorno añade a su arista el campo opcional
`returnTo: {revision: N, historySha256: "…", previousBlock: BLOQUE_PREVIO_COMPLETO}`.
`revision` es un entero que señala
un nodo anterior de la cadena de **esa misma identidad**: cero corresponde al
`beforeSha256` de su primera arista y los nodos siguientes a sus sucesivos
`afterSha256`. La firma de destino debe coincidir con ese nodo y la firma de
partida con el extremo actual. El campo no sirve para aprobar un destino nuevo,
un cambio de identidad o un cambio de respuesta, programa u otro dato protegido.

`previousBlock` conserva el bloque completo inmediatamente anterior a esa
arista. Su huella debe ser exactamente `beforeSha256` y su contrato protegido
debe coincidir con el bloque actual de la misma identidad. La herramienta lo
copia del documento anterior; no se reconstruye a partir de una firma. Esto
permite comprobar el guion de lectura anterior incluso cuando quitar formato
cambia la preparación lingüística o los límites de sus frases.

`historySha256` utiliza `fingerprint` sobre el objeto
`{lessonId: ID_DE_LECCION, locale: IDIOMA, editorial: PREFIJO_COMPLETO}`. El prefijo
incluye, en su orden publicado, **todas** las aristas globales anteriores al nuevo
retorno, también las de otros bloques. Así la prueba queda vinculada al
propietario, idioma y procedencia completa. No se interpreta el ordinal como
una posición dentro de esa lista global.

El autor solicita los retornos mediante el argumento de `prepare_v2`
`editorial_returns={identidad: revision}` o la opción repetible de `prepare`
`--editorial-return identidad:revision`. La herramienta calcula la prueba y
conserva el origen, familia, direcciones y prefijo anterior. No marca retornos
automáticamente al encontrar un ciclo. Un salto de publicación A a A→B→A se
acepta sólo con la cadena añadida completa, conectada a los dos documentos y
con el retorno explícito válido.

Para una región textual visible, el lector puede contrastar ese guion archivado
con un marcador existente. Si sólo cambian delimitadores que el renderizador
oculta y el pasaje conserva sus límites visibles, traslada la posición al pasaje
actual dentro del mismo hueco del historial. No reproduce el archivo anterior
ni añade otro marcador. Los otros dos huecos permanecen intactos. Las grabaciones
existentes se conservan; sólo se reutilizan cuando son compatibles. Una fuente
o grabación distinta no hereda certificados de escucha del archivo anterior.

Windows adopta un registro adicional por lección e idioma al encontrar un
retorno: `lesson_<id>_state_editorial_returns_v1_<locale>`. Guarda un array JSON
con las firmas canónicas de todas las aristas publicadas. Las ampliaciones deben
conservarlo como prefijo exacto; recortarlo, reordenarlo, corromperlo o volver a
metadatos anteriores se rechaza sin modificar los datos. La adopción inicial
desde versiones anteriores añade esa protección sin reemplazar respuestas,
borradores, posiciones ni cobertura de lectura. El restablecimiento del progreso
conserva esta procedencia, como las otras fijaciones de estado.

Este contrato pertenece al cargador y herramientas Windows. La app Android
independiente conserva su contrato anterior. El retorno editorial tampoco
declara implementado el color neutro ni el subrayado tipográfico.

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

## Siete revisiones exactas de opciones en Windows

`CPP_DISTRACTOR_EDITORIAL_CON_ESTADO_U11L5` admite exclusivamente una opción en
U11L5 (`block_0006_0004`, índice 2), U11L6 (`block_0005_0004`, índice 2),
U11L7 (`block_0000_0004`, índice 0) y dos en U11L8 (`block_0003_0002` y
`block_0006_0001`, ambas de índice 2). La identidad completa de la lección,
el idioma español, el índice y los contratos protegidos antes/después forman
parte de la autorización cerrada. U11L7 ajusta el nombre del campo a «Entrada»;
las otras cuatro revisan un distractor y su guía correspondiente. Se mantienen
el índice correcto, las demás opciones, la pregunta, la explicación, los IDs,
las dependencias, los programas y las direcciones históricas.

`CPP_PRECISION_EXCEPCIONES_U11L14` añade dos revisiones cerradas de la misma
lección `diagnosticar_y_recuperar_hasta_fin_de_operacion`: `block_0007_0001`,
índice 1, cambia «Lanzar un diagnóstico de recuperación» por «Lanzar una
excepción de recuperación»; `block_0003_0004`, índice 0, cambia la opción sobre
la primera unidad por el token inicial e incorpora simultáneamente su explicación
exacta revisada. Ambas opciones siguen siendo las correctas, con sus índices y
guías intactos. La segunda transición es una pareja indivisible de opción y
explicación; no autoriza otra redacción de esos campos ni cambios independientes
en la explicación. Los bloques anteriores completos permanecen archivados.

`revise_choice_option(documento_v2, identidad_estable)` prepara una copia en
memoria, añade a `stateLayout.choiceRevisions` una entrada
`{"id": "identidad_estable", "block": bloque_publicado_anterior_completo}`
y añade la arista editorial exacta del bloque anterior a la primera versión
revisada, incluida su guía o su explicación pareada cuando corresponda. El CLI `revise-choice-option` usa `--identity`,
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
siete sustituciones. No se cambian los hashes de `origin`, `family` o
`additions`, ni se reasignan posiciones.

Salvo la explicación pareada cerrada de U11L14, las revisiones posteriores de pregunta, explicación o guías conservan las
reglas editoriales ordinarias: añaden su procedencia sin reescribir el snapshot
original ni sustituir la primera arista revisada. La proyección usada por el
validador curricular restaura exclusivamente la opción histórica, después de
validar la transición completa. Sólo para la pareja de token U11L14 restaura
también su explicación exacta anterior; no reemplaza el bloque entero ni oculta
otros cambios de prosa. El cargador y la autoría rechazan versiones mezcladas
de esa pareja, incluso con una arista editorial posterior o dentro de una
presentación histórica. La revisión de otra opción necesita su propio contrato.

El soporte técnico no activa automáticamente estas sustituciones en los JSON.
Work integra las revisiones y comprueba rechazo, acierto, reapertura,
presentación y escucha. Los intentos históricos conservan su respuesta y su
procedencia; una elección anterior no se presenta como una elección del texto
nuevo. La aceptación curricular y visual queda separada de las pruebas de
contrato Python y de conservación nativa Windows.

Las cinco sustituciones anteriores ya tienen integración curricular; las dos de
U11L14 se preparan y verifican en memoria sin escribir los assets activos.
Un acierto histórico conserva su opción y su explicación originales: la nueva
explicación del token sólo corresponde a una elección nueva comprobada. Los
intentos históricos, incluidos borradores, rechazos y aciertos, mantienen sus
datos y procedencia; no heredan el texto de una representación que aún no existía.

La explicación de la predicción U11L14 `block_0006_0002` (p7 b3 histórica)
tiene una autorización editorial independiente y exacta: «lanza» pasa a
«lanza una excepción» dentro de la redacción revisada completa. El validador
fija ambos hashes, la coordenada, el tipo y la ausencia original de `id`;
no autoriza otras explicaciones ni cambios de programa, respuesta o guía.
Esta precisión también permanece como propuesta, sin editar el JSON activo.

## Siete ampliaciones exactas de comprobación en Windows

`CPP_CASOS_ADICIONALES_CON_PROGRESO` admite exclusivamente las ampliaciones
revisadas de U11L7 (`block_0003_0005`), U11L8 (`block_0004_0004`), U11L10 y
U11L11 (ambas `block_0004_0003` de sus respectivas lecciones), U11L12
(`block_0004_0003`) y U11L13/L14 (ambas `block_0004_0004`). La primera añade
dos casos al final de los 19 publicados, manteniendo cada caso previo y su orden.
Las otras seis añaden únicamente sus informes revisados al esperado de su caso
existente y las llamadas correspondientes al `main` de su referencia estática
completa posterior. Los programas iniciales del editor, entradas iniciales, modos
de comparación, propietarios y demás campos protegidos permanecen iguales.

`revise_compiler_cases(documento_v2, identidad_estable)` prepara una copia en
memoria y añade a `stateLayout.compilerRevisions` una entrada
`{"id": "identidad_estable", "block": editor_publicado_anterior_completo}`.
Para U11L8/L10/L11/L12/L13/L14 añade también `references: [{"id": "referencia_estable",
"block": referencia_publicada_anterior_completa}]`, modifica esa referencia con
las llamadas exactas antes del `return 0;` final y conserva ambas aristas
editoriales anteriores/ampliadas. U11L8 usa `block_0007_0001`; U11L10/L11/L12/L14
usan `block_0008_0001`; U11L13 usa `block_0009_0001`. Cada identidad pertenece
exclusivamente a su lección. El CLI
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
esperados y las seis referencias exactas después de validar la transición
completa. Las guías posteriores
siguen pasando por las guardas editoriales existentes. Añadir una exigencia
`standardError: "nonBlank"` conserva su regla propia: una publicación posterior
no puede retirarla. Las siete revisiones no autorizan respuestas generales,
otras ampliaciones de casos ni cambios de fuentes.

Las pruebas de ejecución anteriores permanecen vinculadas a su objetivo,
idioma y programa capturado. En U11L7 sólo las pruebas válidas de casos idénticos
del mismo programa pueden contribuir a los 21; los dos casos nuevos siguen
pendientes. En las otras seis actividades el esperado completo cambia: el logro
anterior conserva su procedencia, pero la ampliación necesita una nueva ejecución
completa. Una acreditación anterior no se convierte en prueba del contraste nuevo.
Reiniciar explícitamente impide recuperar pruebas anteriores como intento activo.

Las tres ampliaciones U11L12–L14 tienen un único caso de ejecución, con stdin
vacío; sus informes son líneas del stdout producido por llamadas en `main`,
no comprobaciones individuales del campo Entrada:

| Lección | Llamadas añadidas al final de la batería | Informes y bytes LF |
| --- | --- | --- |
| U11L12 | `probar("11*10/2;");` | 11→12; 199→233 bytes. Rechaza el parcial 110 antes de dividir. |
| U11L13 | `probar("11*10/2;");` y `probar("(((((((((1)))))))));");` | 14→16; 248→310 bytes. Rechaza el parcial y la novena agrupación. |
| U11L14 | `std::cout << "Caso 11\n";` y `probar("?z;4;");` | 10→11 casos impresos; 357→406 bytes. Descarta sin retokenizar y conserva el resultado 4. |

Conservan íntegros y ordenados todos los informes y llamadas anteriores. El
logro anterior completo conserva su 1/1 histórico; al adoptar la ampliación
deja 0/1 ejecuciones del esperado nuevo. Los estados sin comprobar siguen sin
crédito. No
acredita el contraste añadido por conservar once, catorce o diez informes.
Autoría y cargador contrastan los hashes exactos de ambos bloques completos.
Estas tres transiciones se verifican como propuestas en memoria; su soporte
técnico no significa que Work ya haya modificado los assets activos.

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

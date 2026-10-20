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

La primera adopción y cada reordenamiento conservan el contenido completo de
los bloques, incluidas sus explicaciones. Las revisiones editoriales se publican
antes o después, con el orden estable. Separar esas transiciones evita que dos
párrafos con la misma forma intercambien por accidente su cobertura de lectura;
sus firmas de estado excluyen el texto para admitir correcciones posteriores.

## Formato e invariantes

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
huella de su payload completo. No se deben editar ni recalcular los hashes de
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

## Idiomas

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

## Procedimiento para Work

1. Leer las instrucciones vigentes del proyecto, actualizar la referencia remota
   de `codex-chat/app` e inspeccionar el trabajo local. Anotar el SHA exacto que
   estaba publicado. No sustituir archivos de Work o Codex ni usar un push
   forzado.
2. Extraer desde ese SHA una copia UTF-8 de cada lección publicada que se va a
   mover. Esa copia es `publicada.json` en los ejemplos. Conservar una copia por
   idioma. Si ya existía `stateLayout`, conservarlo completo.
3. Preparar una copia nueva del archivo que se está revisando. El comando no
   sobrescribe ni el origen ni un destino que ya exista:

   ```sh
   python scripts/curriculum/stable_state_layout.py prepare leccion.json --previous publicada.json --output preparada.json
   ```

4. Copiar `stateLayout.current` de `preparada.json` a un archivo `plan.json`.
   Cambiar exclusivamente el orden de las páginas y de los identificadores de
   bloque. Cada identificador debe aparecer exactamente una vez. Por ejemplo,
   para intercambiar dos páginas se intercambian sus objetos completos en esa
   lista; no se renombra `page_0000` a `page_0001`.
5. Generar la propuesta sin modificar el archivo de partida:

   ```sh
   python scripts/curriculum/stable_state_layout.py reorder preparada.json --plan plan.json --previous publicada.json --output reordenada.json
   python scripts/curriculum/stable_state_layout.py check reordenada.json --previous publicada.json
   ```

6. Revisar e incorporar las propuestas de todos los idiomas distribuidos.
   Ejecutar las puertas de publicación desde la raíz del repositorio, usando el
   SHA publicado real en lugar del marcador:

   ```sh
   python scripts/curriculum/stable_state_layout.py check-tree . --base-ref SHA_PUBLICADO
   python scripts/curriculum/validar_json_programacion.py --base-ref SHA_PUBLICADO
   python -m unittest discover -s scripts/curriculum -p "test_*.py"
   ```

`prepare`, `reorder` y `check` exigen `--previous`; una comprobación interna de
consistencia no acredita que el origen provenga de lo publicado. `check-tree`
exige `--base-ref`, lo resuelve una vez a un commit y compara los mapas con ese
árbol. También detecta lecciones o mapas retirados y archivos de idioma sin
mapa. El validador curricular exige la misma referencia explícita cuando hay
metadatos de estado actuales o en el HEAD local. Un commit local posterior no
reemplaza el SHA publicado usado como referencia.

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

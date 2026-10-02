# Estado actual: UX matemática, navegación y QA provisional

- Fecha local: **2026-10-02** (America/Mexico_City).
- Repositorio físico: `E:\PersonalFiles\Projects\LearnGraphicsProgramming`.
- Rama actual comprobada: `feature/math-puzzle-engine`.
- Checkpoint y HEAD actual: `7de4357ce8dc9ded1048d2d7d4cba47730751b7d`, sin cambios.
- El working tree contiene los cambios locales intencionales de esta sesión y de su continuación; se conservaron. No hubo checkout, reset, descarte, clone, bridge, trabajo cloud, commit ni push.
- Base preservada: motor P01–P13 y sandbox original de **16 páginas / 21 puzzles**. El archivo del sandbox y los validadores matemáticos conservan su contenido; las extensiones afectan UX, presentación, estado de edición y navegación.
- **Implementación guardada y validación cerrada:** build debug correcto, 26 pruebas JVM y 36 Android aprobadas. Tras el último ajuste de scroll se repitieron build/JVM y las dos pruebas del IME, también correctas. APK debug final instalado en el teléfono.

## Correcciones de la solicitud manual más reciente

La tabla describe el comportamiento implementado en código. Los resultados de verificación se registran más abajo; no equivale a aprobar manualmente todos los gestos en el dispositivo.

| Corrección | Estado de la implementación |
| --- | --- |
| 1. Continuación entre lecciones | Matemáticas busca una siguiente lección válida dentro de la misma unidad mediante `NavigationRepository.getNextLessonInUnit`; la abre automáticamente. Sin siguiente lección de su secuencia, vuelve a `UnitActivity` de esa unidad. Se conserva route/course/unit/lesson; no se cruza automáticamente de unidad o curso. El flujo existente de Programming sigue separado. |
| 2. Reapertura de la app | `NavigationStateRepository` persiste `routeId` además de pantalla y padres. `MainActivity` reconstruye la pila de la ruta real y evita pisar el destino con `saveRoutes` al arrancar. La página continúa en `ProgressRepository`. Estados antiguos sin ruta se migran por IDs matemáticos; las APIs antiguas conservan Programming por defecto. Al continuar a otra lección también se actualiza el Intent para recreaciones. |
| 3. Ancho útil | Padding/margen horizontal exterior de la lección matemática y de la tarjeta: **0 dp**. La tarjeta principal conserva **6 dp interiores** para legibilidad, sus esquinas, fondo y separación vertical. Los insets del sistema permanecen. |
| 4. Swipe dinámico | El arbitraje utiliza el estado actual: superficies deshabilitadas, completadas o inactivas dejan pasar el swipe. El scroll horizontal solo se apropia del gesto si puede desplazarse realmente. Edición/selección activa, sliders y arrastre/cartesiano interactivo conservan prioridad; texto, tarjetas y espacios ordinarios permiten navegación. Se conserva el gating de progreso y el ajuste de swipe. |
| 5. Opciones con área táctil completa | `MathTouchCard` proporciona una superficie única para toda la opción visible, incluido contenido enriquecido. No depende del hit target de un label o indicador pequeño. Los controles internos que sí necesitan interacción mantienen su acción. |
| 6. Gráfica fullscreen proporcional | Contenedor común negro, visual centrado, relación de aspecto conservada y tamaño máximo compatible con el ancho/alto útil. Se mueve la misma View y se minimiza padding horizontal. |
| 7. Ordering como Programming | `DirectDragTouch` es el mecanismo compartido para arrastre directo, incluido el ordering de Programming. En Matemáticas, tocar sucesivamente fichas las coloca en la siguiente posición disponible; arrastrar permite reordenar directamente. Las flechas no constituyen la interacción principal. |
| 8. Ordering sin números | Las tarjetas de ordenación muestran únicamente su contenido; se retiró la numeración automática interior. |
| 9. Barra encima del teclado | `MathKeyboardHost` aloja la barra sobre el IME, fuera del flujo normal de la página. Sus insets y dimensiones reales participan en la visibilidad del campo activo. Los inputs desactivan extract/fullscreen del IME; si el viewport no alcanza, se oculta temporalmente la cabecera y vuelve al terminar. Verificado con SwiftKey real en horizontal, campo completo y margen sobre la barra. |
| 10. Atajos cuadrados | Botones de **48 × 48 dp**, con desplazamiento horizontal cuando hace falta; sin botones alargados por defecto. |
| 11. Atajos contextuales | `keyboardSymbols` opcional por respuesta/campo controla los símbolos útiles. Su ausencia utiliza el modo contextual; un array vacío evita símbolos innecesarios. Los dígitos se excluyen de esta barra y siguen en el teclado Android. El botón de borrar se conserva. |
| 12. Matching por drag real | Ficha con superficie táctil completa; se arrastra del origen al destino, moviendo la misma View. La fuente desaparece del origen, sin clon permanente; puede reasignarse antes de comprobar. Tap permanece como alternativa. |
| 13. Recta: izquierda/derecha | Opciones con altura mínima de 48 dp y centrado vertical, conservando composición horizontal compacta. |
| 14. Sin numeración de subrespuestas | Coordenadas, campos e incisos internos no reciben automáticamente 1.-/2.-. La numeración editorial se reserva para listas/pasos explícitos, sin alterar decimales ni expresiones. |
| 15. Política fullscreen común | Gráficas, geometry builder, rectas, Venn, diagramas y previews usan el mismo contenedor negro con visual centrado y proporcional. La salida compacta queda abajo a la izquierda, fuera del canvas; Back/destrucción devuelven la View al padre original. |
| 16. Next selecciona contenido | Foco row-major ya conservado. Al avanzar al siguiente campo o enfocarlo programáticamente se selecciona su contenido completo; vacío conserva selección vacía. El último campo usa Done cuando corresponde. |
| 17. Campo elevado sobre IME | El host calcula el espacio con insets reales del IME y altura medida de la barra, evitando un offset particular de un teléfono. Alineación estable en ventanas demasiado pequeñas para evitar vaivén de scroll; foco diferido consulta el campo actual. Verificado en el teléfono conectado; otros teclados/dispositivos quedan como QA adicional. |
| 18. Cursos/unidades para QA | Cursos 1, 2 y 3: tres unidades por curso y tres lecciones estándar por unidad, **27 lecciones estándar**, más sandbox original. Se añadieron 26 lecciones mínimas originales; todas están marcadas como **Contenido provisional de desarrollo**. Disponibilidad por assets locales, sin limitar Matemáticas al Curso 1. |

### Secuencia provisional y sandbox

- Curso 1 / Unidad 1 conserva `numbers_operations` y añade `fractions_decimals` y `powers_roots`, con respuestas mínimas 1/2 y 9.
- Las ocho unidades restantes contienen `qa_step_01`, `qa_step_02`, `qa_step_03`; se valida Lección 1 → 2 → 3 → unidad contenedora.
- El sandbox sigue siendo accesible manualmente desde Curso 1 / Unidad 1 / Lección 4. Su entrada en el JSON de la unidad declara `automaticContinuation: false`, excluyéndolo de esa secuencia sin cambiar su archivo ni su progreso.
- Los títulos/IDs de cursos y unidades respetan la arquitectura prevista. Los ejemplos no constituyen un temario curricular ni copian contenido de Khan.
- Validación estática de assets completada: nueve unidades, tres lecciones estándar válidas por unidad, 28 lecciones accesibles en total contando sandbox.

## Funcionalidad del incremento UX anterior que se mantiene

- Opciones enriquecidas muestran una sola representación visible; `label` queda para accesibilidad y `explanation` permite texto adicional únicamente donde corresponde.
- Render reusable local inline/block con fracciones apiladas, exponentes/subíndices, raíces simples, relaciones, coordenadas, Unicode y subconjunto LaTeX. La prosa no se convierte ciegamente a fórmulas.
- Fracciones/scripts se miden con el Paint actual del TextView y escalan junto al texto; labels de canvas y dropdowns participan en el ajuste de tamaño.
- Respuestas estructuradas alinean fijo/operador/campo mediante layout y centrado vertical, sin offsets dependientes de una fuente. Labels internos pasan a accesibilidad/hint donde su texto duplicaba o desalineaba el segmento.
- Fullscreen/Reiniciar usan iconos compactos accesibles y áreas táctiles de 48 dp.
- Comprobar y Reiniciar empiezan grises/deshabilitados. Se compara el estado real de todas las respuestas P01–P13 con sus valores iniciales; deshacer todos los cambios restaura el estado inicial. Cambiar solo una herramienta auxiliar habilita Reiniciar, sin tratarla como respuesta.
- Los borradores parciales de editores visuales (por ejemplo `-`) cuentan como modificación, se guardan/restauran y producen feedback de dato inválido; no se valida silenciosamente el último modelo válido. `responseDrafts`/`toolDrafts` son extensiones opcionales de estado compatibles con schemaVersion 1.
- Progreso correcto se revalida antes de restaurar bloqueo; Reiniciar restaura respuestas, herramientas y borradores y revoca la finalización correspondiente.
- Ajustes de lección route-aware: Matemáticas oculta “Ajuste de línea” y “Programación / Editar atajos del teclado”; Programming conserva sus opciones.

## Archivos modificados o añadidos

### Navegación y pantalla de lección

- `app/src/main/java/com/learngraphicsprogramming/app/LessonActivity.kt`: swipe/gestos, ancho, integración de teclado, foco/fullscreen, continuación matemática y contexto del Intent.
- `LessonGesturePolicy.kt` (nuevo): arbitraje dinámico y hit testing de la superficie visible.
- `DirectDragTouch.kt` (nuevo): gesto compartido de arrastre directo para fichas/ordering, integrado con Programming.
- `MainActivity.kt`, `NavigationStateRepository.kt`, `NavigationRepository.kt`: ruta persistida, reconstrucción de pila y continuación limitada a unidad.
- `CoursesActivity.kt`, `CourseActivity.kt`, `UnitActivity.kt`: disponibilidad por assets, contexto completo y llamadas de estado con routeId.
- `LessonOptionsActivity.kt` y `app/src/main/res/layout/activity_lesson_options.xml`: ajustes por ruta.

### Controles y presentación matemática

- `math/MathPuzzleRenderer.kt`, `MathResponseControl.kt`: estado inicial/modificado, iconos, feedback, borradores y restauración.
- `math/MathInputControls.kt`, `MathDragControls.kt`, `MathKeyboardView.kt`: opciones completas, composición, ordering/matching y símbolos contextuales.
- Nuevos `math/MathTouchCard.kt`, `MathInputFocusOrder.kt`, `MathKeyboardHost.kt`: hit target, foco/selección y barra sobre IME.
- `math/MathWidgetUi.kt`, `MathRepresentationRenderer.kt` y nuevo `MathTextView.kt`: contenido único y typesetting matemático local.
- `math/MathCartesianView.kt`, `MathNumberLineView.kt`, `MathVisualControls.kt`: ancho, etiquetas, área táctil, interacción y borradores.
- Nuevo `math/MathExpandableRepresentation.kt`: fullscreen común, aspecto/centrado y conservación de la View real.

### Assets y documentación

- Curso 1 / Unidad 1: `lesson_numbers_operations.json` marcado provisional; `unit_arithmetic_fundamentals.json` añade metadata del sandbox; nuevos `lesson_fractions_decimals.json` y `lesson_powers_roots.json`.
- Nuevos assets de Curso 1 / Unidades 2–3, Curso 2 / Unidades 1–3 y Curso 3 / Unidades 1–3: 26 lecciones nuevas, ocho JSON de unidad nuevos y dos JSON de curso nuevos en total.
- `docs/MATH_PUZZLE_CONTENT.md`: extensiones opcionales, contrato de atajos/borradores, navegación QA y límites.
- Este `MATH_PUZZLE_WORK_STATUS.md`.

### Pruebas Android

- Nuevas: `MathLessonUxDeviceTest.kt`, `MathNavigationDeviceTest.kt`.
- Nuevas en `app/src/androidTest/java/com/learngraphicsprogramming/app/math/`: `MathInputUxDeviceTest.kt`, `MathRenderingDeviceTest.kt`, `MathVisualUxDeviceTest.kt`, `MathKeyboardImeDeviceTest.kt`.
- Existente `MathPuzzleDeviceTest.kt`: selectores accesibles, composición y capturas QA actualizados.
- La suite JVM existente del motor/soluciones conserva **26 pruebas**.

## Build, pruebas y problemas encontrados

- Suite completa: `gradlew.bat :app:assembleDebug :app:testDebugUnitTest :app:connectedDebugAndroidTest --console=plain --no-configuration-cache` → **BUILD SUCCESSFUL**, 2 min 48 s. **26/26 JVM y 36/36 Android**, sin fallos, errores ni pruebas omitidas.
- Último cambio: alineación estable cuando una ventana muy corta no permite mostrar el campo completo. Se volvió a ejecutar assembleDebug, JVM y `MathKeyboardImeDeviceTest` → **BUILD SUCCESSFUL**, 35 s; **26/26 JVM y 2/2 pruebas Android del IME**. Éste es el APK final instalado.
- Dispositivo: **Honor CRT-LX3, Android 14 / API 34, arm64-v8a**. Teclado Android observado: Microsoft SwiftKey.
- Cobertura Android: render de fracciones, estado inicial/modificado, borradores, restauración/recreación, cadena Lección 1 → 2 → 3 → unidad, contexto por ruta, migración y restauración de Programming con pila de padres, cursos/unidades disponibles, swipe real sobre texto y opciones completadas, propiedad dinámica de gestos, opciones enriquecidas con touch completo, ordering nativo directo/taps secuenciales, matching nativo con reasignación y una sola View, Next/selección, dropdowns, fullscreen proporcional e interacción expandida.
- Regresión existente de Programming y compilación/ejecución C++ aprobadas en la suite completa.
- Reapertura adicional fuera de instrumentación: se abrió el sandbox desde la UI, se preparó el valor de página 8 para QA, y se realizaron dos lanzamientos **COLD**. El segundo siguió **Home → force-stop → MainActivity**. Se observó `Lección 4 / 8 de 16` y se comprobaron route=`math_physics`, course=`math_course_01`, unit=`math_course_01__arithmetic_fundamentals`, lesson=`math_course_01__arithmetic_fundamentals__math_puzzle_sandbox`, screen=`lesson`. La página quedó guardada por la propia Activity al pasar a Home; no se marcaron ejercicios como correctos.
- Capturas reales revisadas: barra/IME con campo completo seleccionado, reapertura en página 8, opciones/fracciones a 150 % en portrait, composición estructurada, matching sin origen duplicado y gráfica ampliada centrada/proporcional. La relación de aspecto puede requerir bandas negras laterales en landscape.
- APK: `app/build/outputs/apk/debug/app-debug.apk`; `adb install -r` devolvió **Success**. La app quedó disponible en el sandbox/página 8 para revisión. El ajuste temporal de mantener pantalla encendida por USB se restituyó a su valor original (0).
- Fallos intermedios resueltos: selectores de controles pequeños frente a tarjetas completas; gestos iniciados en el borde Atrás/Home del sistema; coordenadas fuera de pantalla/desactualizadas tras drop; inyección nativa con UID/source adecuados; extract fullscreen del IME y espacio insuficiente para campo/barra. No permanecen fallos conocidos en las suites ejecutadas.
- Advertencias SDK/deprecaciones/caché preexistentes; se usa `--no-configuration-cache`. El dispositivo no suministra logcat al harness; los resultados de pruebas y las capturas sí se obtuvieron.
- Validación estática de 27 lecciones estándar y `git diff --check` correctos. Rama y HEAD permanecen iguales al checkpoint, sin commit, push ni staging.

## Límites y revisión manual pendiente

- Render local de un subconjunto LaTeX, no motor TeX/CAS general: sin macros, matrices, aligned ni grandes operadores con límites. Los comandos no soportados permanecen visibles. Las respuestas conservan los límites de equivalencia algebraica/validadores del motor estable.
- Las fórmulas largas pueden necesitar scroll horizontal; solo se apropian del gesto mientras exista desplazamiento real. Las representaciones de geometría son esquemas de apoyo y los puntos se definen en el contenido.
- No se añadió un cargador nuevo de imágenes ni material curricular; el contenedor expandible puede reutilizarse con futuras Views de imágenes.
- QA manual adicional recomendado: tacto/velocidad del drag y tap secuencial de ordering, reasignación de matching, swipe sobre ejercicios completados/deshabilitados y puntos muy próximos. La instrumentación ya verifica estos mecanismos; falta comparar comodidad con el dedo en más dispositivos.
- Probar teclado Android y barra de símbolos con distintos IME, opciones superiores, extract mode, selección/reemplazo, Next/Done, portrait/landscape y escalas 75/100/150 %.
- Probar fullscreen proporcional y centrado con notch/insets de más dispositivos; TalkBack y orientación durante expansión.
- El cierre real con force-stop y reapertura en página 8 se verificó en este teléfono. Ampliar QA a process death gestionado por Android, más cursos/unidades y otros dispositivos; Back por padres de Programming y cadenas matemáticas ya tienen pruebas.
- Mantener regresión de Programming: navegación, progreso/restauración, fullscreen, ordering compartido y ejecución C++.
- Instrumentación y capturas no equivalen a aprobar visualmente todos estos comportamientos con el dedo del usuario.

## Siguiente paso exacto

Revisar manualmente el APK instalado, empezando en Curso 1 / Unidad 1 / Sandbox / página 8: drag y taps de ordering, matching de página 9, teclado/Next en varias respuestas, swipe tras comprobar y expansión en ambas orientaciones. Después revisar las cadenas provisionales de tres lecciones en los Cursos 2 y 3 y las comprobaciones adicionales indicadas arriba. Los motores P01–P13 del checkpoint siguen disponibles; no quedan nuevos motores pendientes para este incremento UX. El contenido curricular definitivo y un motor TeX/CAS general permanecen fuera de este alcance.

Todo debe permanecer físicamente guardado en este repositorio local. Sin commit ni push; el usuario conserva el control de Git.

# Verificación de conservación de estado — 20 de octubre de 2026

## Adaptación de pruebas al currículo integrado — 21 de octubre de 2026

Base local `a973074a533f2e9f69e65d368f792ddce0afc512`: Work ya incorporó los
mapas y aceptó sus diez recorridos en Honor. Las pruebas heredadas que todavía
indexaban los bloques anteriores se adaptaron a sus identidades históricas,
objetivos e IDs. Las fixtures de legado utilizan datos sintéticos propios;
las pruebas del currículo real conservan su familia, origen y altas publicados.

Suite JVM completa: **1325 casos, 1318 aprobados, un fallo y seis omisiones**.
Se corrigieron las quince expectativas afectadas por la reorganización. El fallo
de `FirstUnitNarrationPackTest` conserva siete locuciones actuales sin su Ogg;
no se omitió ni relajó la prueba. Las omisiones siguen siendo cinco casos de
enlaces simbólicos sin permiso Windows y una regeneración Misaki optativa sin
diccionarios externos. Log: `.gradle/work-current-verification-20261020/` con
`jvm-current-layout-before-20261021.log` y `build-current-layout-curricular-20261021.log`.
El APK QA contiene los mismos 878 JSON byte a byte. Esta adaptación no cambia
producción, currículo, traducciones ni la aceptación física de Work.

Android: **28 métodos distintos aprobados**, mediante tandas y repeticiones en
LDPlayer `emulator-5554`, paquetes aislados `curricularqa` y `realdownloadsqa`.
Incluye los métodos originales de lectura y pedidos, borrador de 73 líneas,
reinicio, recursión, siete casos de estado, ocho de compilación/comprobación
y cinco de reproducción Kokoro. Se conservan los fallos anteriores de preparación:
stdin oculto que el contrato conserva, almacenamiento protegido `json-v1`, otra
consola legítima en la página siguiente y generación automática ajena al test.
Se corrigieron sólo sus expectativas y aislamiento; no se relajaron oráculos,
restricciones de puzzles, cierres de trabajadores ni esperas.

En el mismo directorio de logs están `ldplayer-current-layout-curricular-20261021.log`,
`ldplayer-current-layout-reset-recursion-20261021.log`,
`ldplayer-current-layout-state-output-20261021.log`,
`ldplayer-current-layout-output-final-20261021.log`,
`ldplayer-current-layout-audio-input-cmake-final-20261021.log` y
`ldplayer-current-layout-input-final-20261021.log`. Las repeticiones finales
acreditan todos los métodos sin borrar los intentos fallidos. La captura
`current-layout-output-window-20261021.png` se inspeccionó con botón, stdout,
veredicto y explicación completos. No se atribuye escucha humana ni prueba en Honor.

## Ampliación v2 comprobada en el entorno local actual

Base de esta ampliación: `882400602048f459e6460ddf0c6a34710ba33f3d`,
rama `codex-chat/app`. No se modificaron los JSON curriculares ES/EN/FR.
El nuevo orden y la aceptación curricular de Work continúan pendientes.

- 188 pruebas Python y 133 JVM aprobadas, sin omisiones ni fallos.
- APK y APK de pruebas compilados con las herramientas nativas locales existentes.
- LDPlayer `emulator-5554`, x86_64, paquete aislado `realdownloadsqa`: 26 pruebas
  de estado/repositorios y una prueba adicional con Kokoro real aprobadas.
- Familia real U1L2 ES34/EN29/FR29 evaluada sólo en memoria: traducciones intactas,
  respuestas, borradores, entrada y progreso conservados, altas aisladas, idiomas,
  segunda permutación, reapertura, reinicios y compatibilidad v1 comprobados.
- Reproducción real p6→p5: segmento 2 y 398 ms recuperados exactamente; segunda
  pausa a 573 ms, `MediaPlayer` liberado, tres posiciones conservadas y la misma
  grabación reutilizada sin nueva inferencia. SHA-256 de esa grabación:
  `7c370e48eba03363f2af016f1d4b478d9c344f74805eec044ceea697e09a1b2b`.
- Validador actual aprobado: 152 lecciones, 6025 bloques. Puerta de familias:
  cero mapas de producción, porque la incorporación curricular sigue pendiente.
- Una expectativa Android antigua comparaba el formato raw de borrador después
  de la protección `json-v1` ya existente. Se corrigió la expectativa manteniendo
  la comprobación exacta de todos los caracteres, claves históricas y metadatos;
  la repetición final pasó los 26 casos.

Logs en `.gradle/cmake-input-edit-assist-20261020/`:
`python-localized-state-v2-final.log`, `build-localized-state-v2-final.log`, `jvm-localized-state-v2-final.log`,
`android-localized-state-v2-final.log`, `android-localized-state-v2-kokoro.log`.
Los XML JVM están en `app/build/test-results/testDebugUnitTest/`.
No se usó Honor ni se repitió la suite general histórica o la generación Ogg.
Las limitaciones de posiciones antiguas con audio editado se describen en el
contrato. No se presenta esta prueba como aceptación del recorrido aún no integrado.

## Resultados históricos de la entrega v1

Lo siguiente describe la ejecución anterior sobre `617f0347`; sus limitaciones
de entorno y resultados de suite general no son resultados de la ampliación v2.

### Estado de aquella entrega

La integración y el contrato de `stateLayout` están implementados. La compilación
Kotlin utiliza el SDK de Android real. **No se ha completado la construcción del
APK ni se han ejecutado las pruebas instrumentadas en Android.** El paquete de
audio pregenerado también tiene un pendiente real descrito más abajo; la suite
completa de host no se presenta como íntegramente aprobada.

Base publicada comprobada e incorporada:
`617f03472d20a97f2e1e110a3233ad11fd966d01`, «guardar avances locales de Work».
Su padre es `dd6787d190fcb707a8e935abeadda64584d773d9`, la base inicial de esta
sesión. Los 197 archivos publicados por Work se incorporaron antes de repetir
las comprobaciones. El único solapamiento con la integración inicial fue
`scripts/curriculum/validar_json_programacion.py`, fusionado sin conflictos.
Después se actualizó únicamente la entrada `ORDEN_PEDAGOGICO_CON_ESTADO_EXISTENTE`
del registro de pendientes para enlazar el contrato y este informe.

Los assets curriculares de producción y los audios se conservan exactamente
como estaban en esa base. Esta entrega no activa mapas ni reordena el currículo.
Los JSON reordenados usados para integración están exclusivamente en los assets
del APK de pruebas.

## Resultados

| Comprobación | Resultado y alcance |
| --- | --- |
| `python -m unittest discover -s scripts/curriculum -p "test_*.py" -v` | 155 pruebas aprobadas, sin fallos. Incluye los nuevos contratos de Work y 58 pruebas del contrato de estado. |
| `python scripts/curriculum/validar_json_programacion.py --base-ref 617f03472d20a97f2e1e110a3233ad11fd966d01` | 152 lecciones y 6025 bloques correctos. No certifica semántica de C++ ni ejecución Android. |
| `python scripts/curriculum/stable_state_layout.py check-tree . --base-ref 617f03472d20a97f2e1e110a3233ad11fd966d01` | Aprobado; 0 lecciones de producción con mapa y 0 familias con mapa. Ese cero es intencional. Las transiciones no triviales se ejercitan en los tests y fixtures. |
| `:app:compileDebugKotlin` | Aprobado con las fuentes finales y el SDK real. |
| `:app:compileDebugAndroidTestKotlin` | Aprobado. Incluye las 19 pruebas instrumentadas nuevas; compilarlas no equivale a ejecutarlas. |
| `:app:testDebugUnitTest --rerun` | 1291 casos en 171 clases: 1289 aprobados, 1 fallo por los ocho Ogg ausentes y 1 omitido optativo de Misaki; 0 errores. La suite se ejecutó completa, sin excluir el fallo. |
| `:app:assembleDebug :app:assembleDebugAndroidTest` | Intentado con las tareas normales; bloqueado en `:app:prepareCmakeRuntime` por herramientas nativas ausentes. No se obtuvo un APK completo. |
| Pruebas instrumentadas en Android | No ejecutadas: ningún dispositivo conectado y no hay APK completo. |
| `git diff --check` | Sin errores de espacios o conflictos. |

El [resumen de los XML finales](verification/stable-state-host-results.json)
conserva las cifras por clase, el fallo, la omisión y los hashes de los XML de
esa ejecución. Los 1623 archivos de entrada registrados para la compilación y
los tests permanecieron idénticos durante la pasada final. Las dos comprobaciones
curriculares de fonetización aceptan las 36661 frases españolas actuales.

Entorno de compilación preparado: JDK 25.0.3+9, Gradle 9.5.0, plataforma y
Build Tools de Android 37, NDK 28.2.13676358 y CMake del SDK 3.22.1. Los tests
de host se ejecutaron mediante Gradle sobre las fuentes reales. Las pruebas
simuladas de la entrega inicial no se cuentan como evidencia de Android.

El caso optativo
`ManualEnglishPronunciationsTest.compiledPronunciationsMatchThePinnedVerifiedMisakiFrontend`
requiere `LGP_MISAKI_DIRECTORY`, no configurado en esta sesión. Su omisión se
declara en los resultados; no se cuenta como prueba aprobada.

## Cobertura de conservación

Las pruebas de host comprueban las permutaciones, identidades y coordenadas de
almacenamiento, el contrato JSON compartido entre Python y Kotlin, la permanencia
del origen entre versiones e idiomas y la conservación de las agrupaciones
originales de progreso. Mezclar respuestas correctas y pendientes entre páginas
visibles no debe alterar por sí solo el porcentaje anterior.

Las pruebas de narración comprueban la reubicación de posiciones mediante
identidades de bloque y audio, incluido el salto directo desde el formato
anterior. Solo se conserva un tiempo de pausa si la correspondencia de voz,
idioma, frase y ocurrencia es inequívoca. Continúa el límite de tres posiciones.

Las pruebas instrumentadas incorporadas usan preferencias aisladas mediante
UUID. Incluyen respuestas correctas y parciales, borradores y configuración del
editor, claves que colisionan entre posición visible e histórica, lectura,
reinicio, flags de finalización y conservación de datos ajenos. La fixture
`StableLessonRepositoryDeviceTest` comienza con preferencias posicionales
anteriores, carga el JSON reordenado mediante el `AssetManager` y los repositorios
reales, y comprueba progreso 3/5, equivalente al 60 %. Su comprobación JVM valida
los mismos JSON y sus huellas; no sustituye la ejecución de ese test en Android.

La revisión encontró y corrigió una omisión al abrir la primera página: ahora
`loadLesson` registra la página después de renderizarla, sin depender de
`onPause`. Una regresión de conexión de la actividad y otra instrumentada de
repositorio cubren abrir y avanzar sin pausar, manteniendo la lectura de origen.

## Integración con el contenido nuevo de Work

Dos tests de host conservaban supuestos sobre versiones anteriores del contenido:
un laboratorio español ahora tiene un contrato explícito de salida y un ejemplo
de `#include` cambió de lección. Se actualizaron las referencias de esos tests,
manteniendo la comprobación de las salidas, la opcionalidad de `outputCheck`, la
pronunciación de directivas y la inmutabilidad del JSON.

La nueva prosa también contenía viñetas `•` que el fonetizador español rechazaba.
Se trata la viñeta independiente como puntuación, conservando los rangos de
origen y el contenido publicado. Otra entrada usa el símbolo literal `~` como
dato: su token independiente se pronuncia «tilde», como ya sucede dentro de
código. Se mantiene el mecanismo existente cuando no hay una traza inequívoca
de palabras. Se verifica con fonemas y trazas, sin atribuir síntesis o escucha
en Android a esas pruebas de host.

## Pendiente del paquete de audio

`FirstUnitNarrationPackTest.bundledSpanishDoraPackMatchesEveryCurrentFirstUnitSentence`
detecta **ocho frases únicas sin su Ogg** tras los cambios de Work. Las ocho están
en la página con índice 7 —página 8 en la interfaz— de
`intro_programming/lesson_what_is_programming.json`. Hay 38 frases únicas
requeridas y 30 con audio correspondiente. Se conservan los 32 Ogg existentes,
incluidos dos que ya no corresponden a frases requeridas por este test.

El [inventario exacto](verification/stable-state-missing-audio-617f0347.json)
contiene los textos, hashes y nombres de archivo esperados. Se obtuvo usando las
mismas clases JVM que el test y se contrastó con su XML de fallo. El test no se
ha omitido, relajado ni convertido en una aprobación.

El flujo del repositorio requiere audio sintetizado con la app, su fonetizador
Kotlin, la revisión correspondiente de Kokoro, idioma `es` y voz `ef_dora`.
Esta sesión no dispone de un dispositivo, el modelo y la voz locales ni WAV
exportados que permitan regenerarlo fielmente. Los scripts de
`scripts/narration/` exportan y empaquetan audio real; no proporcionan un
generador de escritorio equivalente validado.

## Pendientes de compilación y Android

La tarea normal de ensamblado informó:

```text
Faltan las herramientas Android CMake/Ninja. Ejecuta
toolchains/cmake/build-android-local.ps1 antes de construir el APK.
```

El CMake del SDK compila código nativo del proyecto, pero no sustituye a los
ejecutables CMake/Ninja/LLVM que la aplicación debe empaquetar para ejecutarlos
en Android. Faltan los artefactos y manifiestos locales de
`.gradle/cmake-android-arm64/`; tampoco están los binarios locales esperados de
`.gradle/clang23-android-arm64/`. No se desactivaron las verificaciones ni se
añadieron binarios ficticios.

Con el entorno nativo del proyecto disponible, quedan por ejecutar las tareas
normales de ensamblado, las pruebas instrumentadas y la aceptación curricular
en Honor. Las clases nuevas a ejecutar en un paquete de QA aislado son
`StableLessonStateDeviceTest`, `StableLessonRepositoryDeviceTest` y
`narration.NarrationStableResumeDeviceTest`, bajo
`com.learngraphicsprogramming.app`. Hay que comprobar además reapertura en un
proceso nuevo, cambio de idioma y narración real antes de atribuir aceptación
en dispositivo a un reordenamiento curricular.

El procedimiento para crear los mapas y validar una propuesta se encuentra en
[STABLE_LESSON_STATE.md](STABLE_LESSON_STATE.md).

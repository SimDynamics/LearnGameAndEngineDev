# Pronunciación inglesa manual en narración española

La narración española de Kokoro mantiene la voz elegida por el usuario y sustituye únicamente los términos seleccionados por fonemas ingleses. Se activa para los nuevos guiones españoles de Kokoro y preparación offline. No hay detector de idiomas ni cambios en los cursos.

## Decisión verificada

Se generaron doce WAV reales en una instancia aislada de LDPlayer: cuatro oraciones, cada una con pronunciación original, A (fonemas ingleses con `ef_dora`) y B (fragmentos ingleses con `af_heart`). B recorta silencio de bordes conservando 25 ms, aplica fundidos de 5 ms y limita la compensación de volumen. Ambas variantes usan los mismos fonemas trazados; no sustituyen patrones fonéticos que también pudieran aparecer en palabras españolas.

El usuario escuchó original/A/B para «Utilizamos include para incluir la biblioteca iostream» y confirmó el 11 de octubre de 2026: «A es clara y natural; conservar la voz española». También confirmó que A conserva la pronunciación española de `imprime`/`imprime_total` y una transición natural hacia `std::cout`. Se eligió A por esa evaluación auditiva, integridad, continuidad y coste medido. No se generaliza esa aceptación auditiva a cada una de las 159 entradas.

| Cuatro oraciones, LDPlayer Android 14 | Inferencia total | Llamadas | Audio total |
| --- | ---: | ---: | ---: |
| Original, ef_dora | 49,632 s | 4 | 20,925 s |
| A, ef_dora | 50,130 s | 4 | 21,125 s |
| B, ef_dora + af_heart | 76,511 s | 19 | 27,628 s |

Estas cifras proceden de XNNPACK con dos hilos y puente ARM64 de LDPlayer, no de un teléfono físico. A añade aproximadamente 1 % de inferencia frente al original en este experimento; B necesita más llamadas y añade aproximadamente 53 % frente a A. La producción usa la tabla ya incorporada y no carga los diccionarios ingleses empleados al generar el experimento.

## Selección y flujo

`ManualEnglishTerms.kt` contiene 159 entradas explícitas: identificador estable, grafía exacta, lectura inglesa, etiquetas gramaticales manuales y contextos permitidos. Incluye palabras reservadas, directivas, cabeceras y componentes presentes en los cursos de C++. Las abreviaturas tienen lecturas revisables: `cout` → «see out», `cin` → «see in», `iostream` → «I O stream». Los nombres que no figuran en la lista conservan el frontend español.

La selección ocurre sobre el texto original, antes de verbalizar notación técnica o separar identificadores. Respeta identificadores completos, mayúsculas, literales normales/raw, URLs y matemáticas. `imprime`, `imprime_total`, `mi_string` y `stringUsuario` no se reclasifican después de separar sus componentes. Los homónimos tienen reglas explícitas: `vector` requiere cabecera/tipo/plantilla o `std::`; `define` una directiva; `final` un especificador; `auto` un contexto de tipo; `reserve`/`data` un miembro, namespace estándar o pista nominal. `for` exige una pista de bucle o sintaxis y no coincide con referencias bibliográficas inglesas.

`SpanishTechnicalSpeech.prepareForKokoro` transporta la selección mediante anotaciones internas y conserva la lectura textual anterior. `MixedSpanishG2P` inyecta los fonemas ingleses antes de las reglas españolas; esos fonemas no pasan por la ortografía ni las alofonías españolas. El resto de la oración sigue usando el frontend español continuo. La segmentación cuenta la longitud del texto hablado, no la de sus anotaciones. Los límites de 510 fonemas y la validación estricta del vocabulario ONNX continúan vigentes.

Las reglas son léxicas y sintácticas: no constituyen un analizador semántico de C++. Una plantilla aparente como `vector<limite>otro`, un método propio que coincida con un miembro estándar, o una futura referencia inglesa con nombres permitidos en prosa requieren revisión del contexto. Las expresiones de varias palabras no se seleccionan mediante una inferencia automática. Los términos escritos entre comillas rectas se protegen conservadoramente como datos literales.

## Caché, reanudación y compatibilidad

- Las frases sin términos seleccionados conservan las mismas claves WAV/Opus, lectura española y marcadores de reanudación. Los recursos pregrabados compatibles siguen disponibles.
- Cada frase con pronunciación nueva incluye la revisión de la estrategia, posiciones de los términos, sus identificadores y sus fonemas reales en la clave. Añadir una entrada ajena a una frase no invalida su audio.
- Las grabaciones anteriores se conservan. No se limpia caché, modelos, diccionarios, preferencias ni progreso.
- Las colas durables anteriores sin anotaciones conservan su frontend y claves originales. Un nuevo guion solicitado recibe la selección manual; no se reinterpretan fragmentos antiguos que ya perdieron el contexto de los identificadores.
- Android TTS recibe sólo el texto hablado restaurado. Inglés y francés mantienen sus frontends existentes. La voz y velocidad siguen siendo ajustes del usuario.
- Un audio cuya pronunciación cambia obtiene una identidad de reanudación nueva, porque su duración puede variar; el marcador anterior no se borra por esta modificación.

## Ampliar la lista

1. Revisar el uso en el curso y añadir una fila `term(...)` en `ManualEnglishTerms.kt`. Para homónimos, elegir contextos conservadores. No ampliar `PROSE` por defecto a palabras españolas ambiguas.
2. Añadir una prueba con el uso técnico y otra con un identificador/contexto español que deba conservarse.
3. Regenerar la tabla con los diccionarios Misaki ya existentes y verificados. Las etiquetas POS se proporcionan manualmente; no se necesita BART ni un modelo POS. Las cinco entradas sin palabra en esos diccionarios usan overrides explícitos.

```powershell
$env:LGP_MISAKI_DIRECTORY = 'RUTA_A_RECURSOS_KOKORO_EXISTENTES_VERIFICADOS'
$env:LGP_MANUAL_ENGLISH_GENERATE = 'true'
.\gradlew.bat :app:testDebugUnitTest --tests '*ManualEnglishPronunciationsTest'
Copy-Item -LiteralPath app/build/reports/manual-english/ManualEnglishPronunciations.kt `
    -Destination app/src/main/java/com/learngraphicsprogramming/app/narration/ManualEnglishPronunciations.kt
Remove-Item Env:\LGP_MANUAL_ENGLISH_GENERATE
.\gradlew.bat :app:testDebugUnitTest
```

4. Revisar los fonemas generados y escuchar un WAV real del nuevo término en una oración española. La igualdad de fonemas/vocabulario no prueba su naturalidad. No descargar recursos ni cambiar voces por añadir una entrada.

## Evidencia reproducible

La suite JVM completa terminó con 449 pruebas, cero fallos, cero errores y tres omisiones por soporte de enlaces simbólicos del entorno Windows. Se compiló la aplicación y su APK de instrumentación con `-Plgp.curricularQa=true`.

El test `MixedSpanishCurriculumTest` recorre las 208 lecciones españolas publicadas en los índices de esta rama (143 de Programación y 65 de Matemáticas/Física), 1.292 páginas y 21.653 frases. Verifica selección, fonemas/tokenizador publicado, fragmentación, igualdad exacta de texto y límites de frases, claves selectivas y JSON intacto. Resultado: 1.771 frases con claves nuevas, 19.882 claves sin cambio, cero cambios de texto/límites y cero términos ingleses seleccionados en Matemáticas/Física.

`KokoroManualEnglishComparisonDeviceTest` genera las variantes y exporta WAV PCM mono de 24 kHz con SHA-256, duración, RMS, pico, muestras finitas y fracción recortada. Los doce archivos fueron auditados y tienen fracción recortada cero. Otra prueba comprueba original/A/B con pausa, reanudación, seek, 1,5× y conservación de las dos claves de caché. La prueba del controlador real genera tres frases mixtas con el worker de producción y comprueba pausa, seek en pausa, velocidad, navegación y reutilización sin inferencia adicional.

Las pruebas Android usan únicamente `emulator-5556`, paquete `com.learngraphicsprogramming.app.curricularqa`, recursos existentes copiados a esa instancia y directorios privados aislados para audio/cola/preferencias de prueba. El Honor y la instalación normal de la aplicación no se tocaron. Los informes y audios están en `build/reports/manual-english/device/` (ignorados por Git). Los informes de máquina mantienen la evaluación auditiva como pendiente/false: la aceptación humana indicada arriba se registra por separado.

## Fuentes técnicas consultadas

- [Kokoro pipeline](https://github.com/hexgrad/kokoro/blob/main/kokoro/pipeline.py): fonemización separada de la voz y síntesis desde fonemas.
- [Misaki](https://github.com/hexgrad/misaki) y [alfabeto inglés](https://github.com/hexgrad/misaki/blob/main/EN_PHONES.md): fonemas y overrides explícitos.
- [Voces de Kokoro](https://huggingface.co/hexgrad/Kokoro-82M/blob/main/VOICES.md): limitaciones de fragmentos muy cortos; contexto que motivó evaluar B con audio real.

Se consultó el probe anterior de `main` como referencia de investigación, sin fusionar ni copiar sus cambios indiscriminadamente. Sólo se publica `work/mejoras-app`.

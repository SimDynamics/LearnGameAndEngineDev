# SMO — Sistema Matemático Objetivo

## Implementación experimental

SMO es un lenguaje de programación y un sistema matemático experimental con
notación posicional en base 12, reglas propias y representación gráfica integrada
en LearnGameAndEngineDev para Windows. El prototipo contiene interpretación de
programas, rutas de ejecución nativa y de cómputo, y herramientas para comparar
representaciones, evaluación diferida y agrupación de operaciones.

Jairo Alberto Vera Hernández define el diseño y funcionamiento, crea los prompts
y da seguimiento al desarrollo de principio a fin. La implementación de código
se realiza con IA bajo ese proceso de diseño y seguimiento.

El [núcleo de referencia](../native/core/smo.hpp) documenta la notación de doce
dígitos y las reglas específicas de SMO para multiplicación, división y potencias
con cero, con alternativas explícitas. Las comprobaciones se refieren a esos
contratos y a los dominios admitidos por cada ruta. El proyecto sigue en desarrollo.

## Arquitectura y capacidades presentes en el código

| Área | Implementación y alcance |
| --- | --- |
| Plataforma | Aplicación nativa **Windows x64**, en **C++20 y Win32**, con construcción CMake. Véase la [base Windows](WINDOWS_NATIVE.md). |
| Cómputo común | La interfaz [SmoCompute](../native/presentation/windows/smo_compute.hpp) dispone de backends **Direct3D 11, Direct3D 12, Vulkan y OpenGL**, con fuentes HLSL/GLSL y artefactos SPIR-V según la API. |
| Esferas | [Trazado de rayos por cómputo](../native/presentation/windows/smo_scene_common.inc), con rayos primarios ortográficos, luz direccional y sombra básica. |
| Triángulos | El [trazado de triángulos por cómputo](../native/presentation/windows/smo_program_render.inc) incorpora perspectiva y profundidad; la [ruta nativa de representación](../native/presentation/windows/smo_native_render.inc) incluye transformaciones y rotaciones de triángulos. Son rutas distintas de la escena ortográfica de esferas. |
| Física de esferas | El [laboratorio de escenas](../native/presentation/windows/smo_scene_common.inc) contiene gravedad, contactos, colisiones y rebotes entre cuerpos esféricos y sus límites. |
| Partículas y canales opcionales | Hay [ensayos controlados de desplazamiento procedural por emisor](../native/presentation/windows/smo_benchmark.cpp) y [contratos experimentales de evaluación diferida y canales opcionales](../native/presentation/windows/smo_lazy_task.hpp). No se presentan como un sistema completo de partículas o animación esquelética. |
| Subdivisión duodecimal | El [ensayo de subdivisión](../native/presentation/windows/smo_subdivision_task.cpp) utiliza una cuadrícula `12^pasos`, de uno a seis pasos, con controles racionales/enteros y comparación exacta de estados, puntos e imagen. Sus [pruebas](../native/presentation/windows/smo_subdivision_task_tests.cpp) comparan variantes y trayectorias en las cuatro APIs. |
| Instrumentación | Las [métricas de los ensayos](../native/presentation/windows/smo_subdivision_task.hpp) separan preparación, compilación de shaders, carga, calentamiento, conversión, tiempo GPU de cuadro y tiempo de pared con lecturas de retorno. Registran buffers explícitos y pico de RAM del proceso; este último no es la VRAM total. |
| Configuración admitida | El código del [ensayo de escenas](../native/presentation/windows/smo_scene.cpp) admite dimensiones de hasta `1920 × 1080` y hasta `2048` cuerpos. Son parámetros implementados; las tablas de resultados de este documento no acreditan por sí solas una ejecución física con esa configuración. |

Estas capacidades forman parte de un motor y laboratorio gráfico experimental
integrado en la aplicación educativa. El trazado de rayos se implementa mediante
shaders de cómputo; aquí no se atribuye a núcleos RT dedicados. El alcance de cada
ensayo y sus comprobaciones se conserva separado de la madurez del conjunto.

## Reutilización por cara: ensayos locales en GPU física

El informe conservado del autor describe un ensayo de reutilización de los datos
de cada triángulo sobre una **RTX 3070 Ti**, durante **un minuto por API** e
incluyendo la preparación por cara. Reporta las siguientes reducciones del tiempo
GPU y la conservación exacta de los resultados:

| API | Menor tiempo GPU en el ensayo F12 | Escena reportada de 1 536 triángulos |
| --- | --- | --- |
| Direct3D 11 | 32,9 % | 32,4 % |
| Direct3D 12 | 35,2 % | 28,8 % |
| Vulkan | 30,3 % | 33,3 % |
| OpenGL | 15,9 % | 32,4 % |

El intervalo **15,9–35,2 %** pertenece a la primera columna. La segunda corresponde
a otra escena declarada en el informe. La optimización descrita reutiliza datos
de los triángulos; estos resultados no aíslan una ventaja causada exclusivamente
por la base numérica. El tiempo de dibujo ensayado y los FPS finales de la
aplicación abarcan costes distintos.

## Escalado F12: ensayos locales en GPU física

Un informe conservado de ensayos locales, facilitado y confirmado por el autor
del proyecto, describe pruebas en una **NVIDIA GeForce RTX 3070 Ti** mediante
**Direct3D 11, Direct3D 12, Vulkan y OpenGL**. Esta sección atribuye los resultados
a ese informe; no constituye una nueva ejecución de las pruebas.

El informe reporta aproximadamente **un millón de valores y 60 segundos por
API**. Al comparar el escalado especializado mediante exponente con el producto
general F12, registra un ahorro aproximado del **1–1,6 % del tiempo de esa
operación F12**, conservando exactamente sus resultados y diagnósticos. Reporta
también **36 456 comprobaciones de leyes y dominios**, **33 etapas de aplicaciones
GPU**, **diez regresiones seleccionadas** y compilaciones Debug y Release.

El código público permite comprobar el protocolo implementado: el modo
`hardware large sustained` selecciona **1 048 576 valores**, exige un adaptador
físico y sostiene cada API durante al menos 60 segundos. La comparación se limita
al resultado **F12 PRIMARY, perfil 0**, y sus diagnósticos numéricos, con factores
positivos de la forma `12^k`. El cociente informado se calcula como la mediana del
tiempo del producto general dividida entre la mediana del escalado por exponente.
Los timestamps de producción excluyen verificación y lecturas de retorno; el
tiempo de pared incluye esas tareas.

En esta revisión se comprobó que los cuatro archivos `smo_scale12_experiment*`
del checkout usado por el autor coinciden con los objetos Git del código público.
**No se han recuperado las cuatro medianas originales ni se han vuelto a ejecutar
los ensayos durante esta revisión.** El informe conservado se identifica mediante
el SHA-256 `9ab4cae26c1d4914853ce1d9346e2ebeb6c34913ef18817182659b935968ac00`.
Ese mismo informe es la fuente de la tabla de reutilización por cara. Las
medianas originales por API de ambas comparaciones no se han recuperado en esta
revisión; las cifras se conservan como resultados reportados por el autor. La
conversación completa permanece fuera del repositorio.

Este resultado reportado corresponde a una especialización puntual de una
operación F12. No acredita una mejora global de FPS o del motor, ni una
superioridad general de la base 12. Los ensayos WARP descritos a continuación
corresponden a otro registro y a otros alcances.

## Otro ensayo: evidencia histórica sobre WARP

Se revisó un registro existente de CTest del **25 de octubre de 2026**. No se
ejecutaron nuevamente las pruebas para preparar este resumen. El
[extracto publicado](evidence/smo-validation-excerpt.txt) conserva las líneas
seleccionadas y su numeración original; excluye comandos y rutas locales.

- Archivo de origen: `LastTest.log.tmp3d5c3`.
- Tamaño del archivo completo: **153 829 bytes**.
- SHA-256 del archivo completo:
  `aefe5c312a648a7837de57b0d0f1ff5a7117b56f6a6eddf87a0b7834466e7dcc`.
- Las secciones de cómputo seleccionadas identifican **Direct3D 11 / Microsoft
  Basic Render Driver** y `software=1`: son comprobaciones sobre WARP, el
  adaptador de software de Windows.

| Comprobación | Resultado registrado | Alcance que acredita |
| --- | --- | --- |
| Evaluación diferida y agrupación | En un caso de 129 elementos y 64 activos, las operaciones lógicas de IR pasan de **43 344 a 24 234**. Las cinco variantes conservan los mismos hashes de estado e imagen y `verified=1`. | Reducción del trabajo lógico y conservación del resultado en ese caso. Líneas 10–18. |
| Contrato de reglas y perfiles | **10 541 casos, 48 perfiles**, `equivalent=1` y cero discrepancias en las tres variantes. Se conservan 414 estados no resueltos por variante. | Concordancia con el oráculo en los dominios y tolerancias ensayados. Líneas 1046–1049. |
| Referencia de enteros y racionales | `PASS` en **31 908 comprobaciones exactas** de la ruta CPU de radix 12. | Verificación exacta de los casos enteros y racionales incluidos en esa prueba. Línea 1284. |
| Ablación de representación y algoritmo | Para 129 elementos, un grupo y nueve términos, las evaluaciones pasan de **1 161 a 9** tanto en A/B (`uint32`) como en C/D (radix 12 empaquetado). Las cuatro variantes conservan el mismo hash canónico y cero discrepancias. | La agrupación reduce evaluaciones en ambas representaciones; permite distinguir el efecto del algoritmo del efecto de la base numérica. Líneas 1424–1429. |

## Límites del extracto WARP

Los conteos de operaciones y las comprobaciones de equivalencia no acreditan
una mejora general de FPS ni un porcentaje de rendimiento del motor. Los tiempos
incluidos en el extracto corresponden a esos ensayos sobre un adaptador de
software, con tres muestras en el caso de ablación seleccionado. No son una
medición de rendimiento en una GPU física.

La comparación de perfiles incluye una representación de apoyo FP32. Su criterio
numérico utiliza la tolerancia declarada por el código; `equivalent=1` no significa
que cualquier operación de coma flotante sea exacta. El registro conserva también
el error absoluto máximo informado. Las 31 908 comprobaciones exactas de CPU son
una prueba distinta, dentro de sus dominios enteros y racionales.

La reducción por agrupación aparece tanto con `uint32` como con radix 12. Por
ello, estos datos no permiten atribuir esa reducción exclusivamente a la base 12.
Tampoco sustituyen la validación completa del motor, de todos los programas SMO
o de cualquier entrada matemática. El registro seleccionado es histórico; no
identifica por sí solo el commit exacto del ejecutable y no se presenta como una
nueva validación del HEAD actual.

## Fuentes de implementación y pruebas

- [Notación y reglas del núcleo](../native/core/smo.hpp).
- [Rutas y dominios nativos](../native/core/smo_native.hpp).
- [Evaluación diferida y agrupación](../native/presentation/windows/smo_lazy_task_tests.cpp).
- [Reglas, perfiles y salida de diagnóstico](../native/presentation/windows/smo_rule_gpu_tests.cpp).
- [Tolerancia de la representación de apoyo](../native/presentation/windows/smo_rule_gpu.cpp).
- [Comprobaciones exactas de radix 12 en CPU](../native/presentation/windows/smo_float12_cpu_tests.cpp).
- [Ablación de representación y agrupación](../native/presentation/windows/smo_radix_ablation_tests.cpp).
- [Sesión de programas y representación integrada](../native/presentation/windows/smo_program_session.cpp).
- [Protocolo del experimento de escalado F12](../native/presentation/windows/smo_scale12_experiment_tests.cpp).
- [Ejecución, verificación y cociente de tiempos del escalado](../native/presentation/windows/smo_scale12_experiment.cpp).
- [Opciones y dominio de entradas del escalado](../native/presentation/windows/smo_scale12_experiment.hpp).
- [Kernels del escalado especializado y del producto general](../native/presentation/windows/smo_scale12_experiment.inc).
- [Extracto del registro revisado](evidence/smo-validation-excerpt.txt).

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

## Evidencia histórica seleccionada

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

## Límites de la evidencia

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
- [Extracto del registro revisado](evidence/smo-validation-excerpt.txt).

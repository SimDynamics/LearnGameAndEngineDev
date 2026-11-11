# Validación estructural multicurso de Programación: alcance y bloqueo

Estado verificado: **11 de noviembre de 2026**. Rama de integración técnica: `Codex`.

## Qué implementó Codex

El validador habitual `scripts/curriculum/validar_json_programacion.py` admite
`--course cpp_fundamentals`, `--course programming_practice_tools` y
`--course all` (valor predeterminado). `--list-files` identifica los JSON
estructurales examinados. La puerta de mapas de estado sigue abarcando las
familias e idiomas del repositorio completo antes de comprobar los bloques.

`scripts/curriculum/validate_programming_practice_tools.py` reutiliza los
contratos de ejercicios, respuestas, presentación, imágenes, CMake, índices,
unidades e identificadores del validador original. Cuando existe Git, lee la
publicación mediante `git show <commit>:<ruta>` y comprueba cada bloque anterior
contra ella. Solo permite una nueva redacción de `/explanation` si el resto
del bloque es idéntico y el documento conserva propietario, familia,
transición editorial y descriptor semántico verificados. Las excepciones
históricas de `cpp_fundamentals` no se aplican al segundo curso.

La ejecución CI `42972420741` confirmó **9/9 pruebas multicurso correctas**,
incluidos índices corruptos y revisiones editoriales válidas e inválidas.
También aprobó la batería `test_stable_state_layout` precedente. No se
modificaron los JSON curriculares ni el historial Git de los estudiantes.

## Bloqueo detectado en el contenido publicado

La misma CI NO pasó su comprobación de la publicación real:

```text
Conservación de estado no verificada:
Invalid presentation relationships in
app/src/main/assets/content/es/programming/programming_practice_tools/
proyecto_assetcheck/lesson_disenar_probar_y_presentar_variante_propia.json:
This locale must keep its own historical origin
```

La lección ya presentaba el **mismo blob Git**
`525dd1aa69467125118b29db3a8ce29611e6fd3f` antes de iniciar esta
generalización, en el commit `838fc808c3e84f03497e943215a42c7db1e5cf3c`.
La nueva validación no creó la discrepancia.

Dentro de `stateLayout`, los 18 registros de página y 98 bloques de
`origin` y de `family.es.origin` coinciden en cantidad, pero **difieren
en dos huellas históricas**:

| Posición visible | Identidad estable | Huella en `origin` | Huella en `family.es.origin` |
|---|---|---|---|
| Página 6, bloque 3 | `block_0005_0002` | `c82fd37c3ded99ed44aafb2b757fd07c590def117e3ca8bc04d807fc722213ec` | `c3f7076c01b9585c8046cec57d01f429c6e6208452bab68632616ddafd92db8c` |
| Página 9, bloque 3 | `block_0008_0002` | `fa0460ffd2c3b2296de310bd6ad697e6d79ae4471a2102f8f4bae7cdaa683174` | `76a83448522fa529e325472155480d5dd068348449af73a33fa4f1dd683529ed` |

Los dos bloques afectados son de tipo `code`: el primero contiene una
cabecera de estadísticas `include/assetcheck/estadisticas.hpp`; el segundo,
instrucciones CMake de `assetcheck_estadisticas` y `assetcheck_tipos`.
Estas huellas no pueden igualarse arbitrariamente: se debe examinar el
documento publicado y su historia de familia para determinar el contrato
correcto y preservar los identificadores de progreso.

**Responsabilidad:** Work revisa el origen y corrige el JSON curricular si
procede, sin destruir la cadena histórica. Codex mantiene las protecciones
técnicas. Hasta comprobar el documento corregido, el workflow multicurso
debe permanecer fallido en su validación de contenido real.

Evidencia: https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/42972420741

La prueba de C++ ejecutable, las capturas locales, el ZIP original de
AssetCheck y los cinco pendientes del proyecto independiente son encargos
distintos y no quedan certificados por esta implementación.

## Diagnóstico reforzado: correspondencia con el código real

El bloque `block_0005_0002` es `code` C++ y muestra la cabecera
`include/assetcheck/estadisticas.hpp`. Su huella del contrato del bloque
visible, calculada con el algoritmo canónico de `state_fingerprint`, es
`c82fd37c3ded99ed44aafb2b757fd07c590def117e3ca8bc04d807fc722213ec`.

El bloque `block_0008_0002` es `code` CMake y define los objetivos
`assetcheck_estadisticas`, `assetcheck_tipos` y sus pruebas. Su huella del
contrato visible es
`fa0460ffd2c3b2296de310bd6ad697e6d79ae4471a2102f8f4bae7cdaa683174`.

**Ambas huellas coinciden con `stateLayout.origin`, no con
`stateLayout.family.es.origin`.** Es una pista concreta sobre el origen de
la discrepancia, no autorización para reescribir la familia histórica sin
acreditar su fuente y transición. El mismo blob con la inconsistencia existe
en `main` y `Codex`, y la historia de esa ruta muestra su incorporación
en el commit original
[`8c3cde9c`](https://github.com/SimDynamics/LearnGameAndEngineDev/commit/8c3cde9c79e0acbadb873b92ddb108755bb4f839).

El validador compartido `stable_state_layout.py` ahora informa las
coordenadas, identidades y ambas huellas para cada discrepancia (hasta ocho),
sin modificar ningún mapa ni relajar la condición de rechazo.

**Última evidencia técnica comprobada:** ejecución
[GitHub Actions 42973590898](https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/42973590898):
9/9 pruebas multicurso, 89/89 pruebas previas del mapa y 2/2 pruebas del nuevo
diagnóstico correctas. La validación de la publicación real conserva
`failure` por el mismo origen discordante, ahora identificado en detalle.

**Acción curricular pendiente de Work:** verificar la huella de publicación
autorizada, la identidad de la familia `es`, las dos versiones y su cadena
editorial antes de hacer la corrección. Después ejecutar el workflow completo;
no se aceptará la edición como resuelta hasta que el control real de repositorio
termine sin errores. No se han tocado el curso, sus traducciones ni los cinco
pendientes técnicos del proyecto AssetCheck.

## Barrido global por lecciones: segunda incidencia preexistente

La validación global se detenía en U4L7. El diagnóstico complementario
de solo lectura revisa cada lección visible con el mismo comprobador de
estado, sin sustituir la puerta global y sin modificar JSON.

La ejecución [42974249377](https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/42974249377)
inspeccionó **162 lecciones de Programación 1, sin incidencias de mapa**,
y **28 lecciones de Programación 2, con dos lecciones afectadas**. La
validación auténtica continúa fallando; el informe JSON se conserva como
artefacto de Actions.

**Nueva incidencia: U4L3, «Preparar y comprobar CMake en Windows».**
Archivo: app/src/main/assets/content/es/programming/programming_practice_tools/proyecto_assetcheck/lesson_preparar_y_comprobar_cmake_en_windows.json

El blob 6ba7f1a8c2ab64ffd32461127fc87a3753e1138f es idéntico en
main, Codex y la base anterior 838fc808. La lección se incorporó
mediante el [commit inicial 45f61239](https://github.com/SimDynamics/LearnGameAndEngineDev/commit/45f61239fe30bf5b680a13ca527bbfeaba5c093b).
Este problema es anterior a la nueva auditoría.

El mapa de U4L3 tiene nueve páginas. Ocho bloques de opción múltiple
presentan valores diferentes entre stateLayout.origin y
stateLayout.family.es.origin:

| Página, bloque e identidad | SHA-256 en origin | SHA-256 en family.es.origin |
|---|---|---|
| p1 b5 block_0000_0004 | 6e746e53ea337be355314eabc66b4cbf3eb0fdd96d4517c1cf58a8392ff67b48 | 64a3c87b566eac93ae62ef251eb9f6b11faa599d69b7dd7425a506a03c80d9ed |
| p2 b5 block_0001_0004 | 1c9103e8777bc73bee31e43bf7542c6b12d78495c563895480435c7c518e0f81 | 8d7fc770bb1a0e1244861d641861bba709a10617871f7db9408784163d5161b3 |
| p3 b6 block_0002_0005 | 87d40fde0daf7eade7f7e5f33925e61a4fe6a72d5a165cb371a998c5f1b85f75 | b97fd28dcf38db424c70010f79abeda60adc198e7d4fa0cae223d4dcce133ae2 |
| p4 b5 block_0003_0004 | 04dc9b937d103bc7fee5758ee50f49d93c87adcdb535e1f983dd217f4ab8a3ca | da23ff18ceda6f1688753d91150b9451f56b842a32ad75c24ddec0476989f46b |
| p5 b5 block_0004_0004 | d946280ff79526d1cf07d9d0f94a54b04337a084da9f6a904d59e8b38101eb40 | 259b35e375ff4fbc70c7286981269a07ef7946ca7e2fcae14c6eef3ea42fa3fe |
| p7 b5 block_0006_0004 | a8d204584e3786aa0e692bcd8b7443eadadc7c7f632a32a13e216c5a5fd9b5ef | 8d15cfd2a1b946549bcf29fb1404dc63f59932b67e15e6c201caa9e7efc6d906 |
| p8 b5 block_0007_0004 | 6da66c12ea4b0aed7c926893dafaaf3d26ce17963ce0e983a4e94a3c698b2b0c | ccd3bfa9e80265e63e09e750fcda4d412c9594f79ac0a7d2ab18706da545d365 |
| p9 b5 block_0008_0004 | 61143ac9d84fb02e9a10896830f63ca729e3d22fd5c7b1c8cf3557ac4ddf88ba | 110f0b42987f21ea931460f82dd7cb9e780e9a2cc9cb0a3b6d6a6d509a41c725 |

En total se localizaron **10 discrepancias históricas de huella**,
ocho en U4L3 y dos en U4L7. No equivalen a diez errores de ejecución
del proyecto. El diagnóstico ya no afirma que se omitieron otras
diferencias cuando muestra exactamente ocho: solo anuncia truncamiento
si se supera el límite.

Work debe comprobar la cadena editorial original antes de rectificar
cualquier metadato. Esta auditoría no autoriza regenerar los orígenes,
cambiar las respuestas ni aprobar la validación global.

## Auditoría estructural real separada de la validación histórica

Se comprobó la estructura de los dos cursos con una nueva vía **solo de
lectura y solo de estructura** que reutiliza el contrato original de cada
bloque y de los índices: `scripts/curriculum/auditar_estructura_multicurso.py`.
Su función de validación estructural emplea el mismo código que la puerta de
Programación 2, pero omite explícitamente la comprobación de `stateLayout`
y la comparación histórica contra Git, que siguen siendo obligatorias en la
validación principal. La salida contiene `STRUCTURE_ONLY_NOT_APPROVED`.

La ejecución [42975438082](https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/42975438082)
confirmó las seis pruebas unitarias del auditor y recorrió los archivos
reales de `Codex` con este resultado:

| Curso | Lecciones | Unidades | Bloques | Archivos JSON | Incidencias estructurales |
|---|---:|---:|---:|---:|---:|
| `cpp_fundamentals` | 162 | 15 | 7.438 | 178 | 0 |
| `programming_practice_tools` | 28 | 6 | 1.561 | 35 | 0 |
| **Total** | **190** | **21** | **8.999** | **213** | **0** |

Se comprobaron los contratos admitidos de tipos y campos, el formato
de ejercicios, las respuestas, casos C++/CMake, títulos, índices y
referencias entre lecciones, unidades y cursos. El informe legible por
máquina queda como artefacto temporal `estructura-multicurso-...`.

Esta es una **evidencia positiva independiente** del recorrido real de
estructura, NO una declaración de terminación del encargo. La misma
ejecución sigue fallando en `validate-programming-courses`, porque
las incoherencias de origen de U4L3 y U4L7 persisten. El diagnóstico
de mapas continúa localizando dos lecciones afectadas. No se modificaron
los JSON ni se atribuyó al estudiante trabajo ejecutado.

La puerta de producción sigue siendo
`python3 scripts/curriculum/validar_json_programacion.py --base-ref HEAD`;
no se añadió ninguna opción para desactivar silenciosamente la
comprobación de historia editorial y de familia.

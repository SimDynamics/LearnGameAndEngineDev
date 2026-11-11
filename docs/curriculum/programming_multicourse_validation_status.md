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

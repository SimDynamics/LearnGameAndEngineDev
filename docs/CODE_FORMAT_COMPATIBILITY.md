# Compatibilidad de formato físico del código curricular

## Propósito y alcance

Este contrato permite editar espacios, sangrías y **saltos de línea reales** del
código curricular de los tres idiomas (es/en/fr) sin transformar la edición
en una actividad nueva.
La fuente que recibe el estudiante, copia o compila será la versión corregida.
No se trata de un espaciado visual superpuesto al editor.

El validador autoriza cambios de formato en los campos de código existentes
de los ejemplos, predicciones, `fill_code`, `order_code`, `drag_fill_code`,
`inline_fill_code` y `cpp_compiler`, incluidos `projectFiles` C++ y CMake.
No se inventan campos de respuestas, casos, identificadores ni ejercicios.
Los archivos de proyecto de tipo desconocido son inmutables bajo este contrato.

## Autoría de Work: conservar el origen publicado

Para cada lección con `stateLayout` v2, Work edita únicamente los campos
de fuente reales cuyo formato necesita corregir. Luego ejecuta la
transición curricular usando el **documento anterior publicado como base**,
mediante el flujo habitual `prepare_v2` y validación de transición.

La transición incorpora a `stateLayout.sourceFormatRevisions` un registro por
actividad modificada, de este formato:

```json
"sourceFormatRevisions": [
  {
    "id": "block_0002_0003",
    "block": {
      "type": "code",
      "code": "int main(){return 0;}"
    }
  }
]
```

El `block` debe ser el **bloque anterior completo y exacto**, no un ejemplo
sintético ni un fragmento copiado parcialmente. El ejemplo precedente sólo
ilustra la forma de la propiedad, no sirve como snapshot para una lección real.
El identificador corresponde al `stateLayout.current` de la lección.
Las revisiones son de una sola asignación por identidad y se conservan en
transiciones posteriores. Las pruebas editoriales v2 siguen siendo obligatorias
para toda modificación real del bloque.

No modificar el `origin`, `family`, `additions`, direcciones históricas,
`correctOrder`, respuestas, casos de `outputCheck`, fuentes iniciales de
otros ejercicios ni mapas de progreso. Las traducciones se preparan por
separado, cada una contra su **origen publicado de ese idioma**; una revisión
española no debe reescribir implícitamente las fuentes inglesas o francesas.
No regenerar huellas históricas para hacerlas coincidir artificialmente con
un texto nuevo.

## Protección semántica

La comparación exige conservar los elementos léxicos de la fuente, incluidos
identificadores, valores, operadores, cadenas, comentarios y directivas. Los
saltos físicos dentro de cadenas crudas, literales y argumentos entre corchetes
CMake tienen significado y no pueden retocarse. Se protegen las continuaciones
con barra invertida y salto físico. No se permite usar este mecanismo para
cambiar fórmulas, reparaciones, dependencias, resultados de programa ni
expectativas exactas de stdout/stderr.

El comparador es **conservador**: si no demuestra que los límites léxicos se
conservan, rechaza la revisión. No sustituye compilar, ejecutar y comprobar
los programas. Los ejemplos que requieran una nueva distribución semántica
de comentarios o macros necesitan revisión específica, no forzar la huella.

En `order_code` se conserva la identidad de cada fragmento y la secuencia
de índices, aunque su representación textual pueda mejorar. Los separadores
`orderVisualSeparators` siguen siendo decorativos; no sustituyen las líneas
físicas que Work haya incorporado a la fuente.

## Estado del estudiante

La aplicación conserva las claves históricas de actividad y borrador. El
código actual se presenta o compila con su espaciado corregido; las huellas de
objetivos y de orden consultan la instantánea publicada anterior cuando existe
una revisión de formato autenticada. Se conservan las respuestas e intentos
ganados. El editor no sobrescribe automáticamente borradores del estudiante.
Los ejecutables anteriores pueden volver a construirse con el código nuevo.

En Android, `LessonStateLayoutJson` reconoce el archivo
`sourceFormatRevisions` y proyecta los códigos originales únicamente para
validar las huellas y posiciones históricas. La ejecución y presentación
continúan usando las fuentes curriculares visibles, y los borradores guardados
no se reemplazan. El motor Android utiliza su propio comparador conservador,
con pruebas JUnit de protección contra fuentes modificadas.

## Piloto real acreditado y ejecución curricular

**Piloto técnico completado en Codex, sin alterar los JSON curriculares.**
Se tomaron copias aisladas de la lección publicada de Programación 1
`expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json`
y se insertaron saltos **físicos** entre declaraciones y validación, entre
cálculos y decisiones y antes del retorno final, exclusivamente en los bloques
`block_0003_0002` (p4 b3) y `block_0005_0002` (p6 b3).

La comprobación `test_real_code_format_pilot.py` compiló realmente con C++23
los dos programas antiguos y los dos reformateados, comparó sus salidas,
comprobó las transiciones v2 y rechazó cambios de lógica, respuestas y
archivos de procedencia falsificados. GitHub Actions: 43127749492
(https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43127749492).

`native.code_format_loader` cargó y reabrió la copia real desde Windows,
recuperando una respuesta correcta, el borrador del estudiante, el cursor,
la página, las claves de estado y la identidad de otros objetivos.
La prueba de importación histórica obtuvo resultado correcto:
https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43127931754.

En Windows, la suite del lector superó **15/15 pruebas**, incluido el
indicador del primer caso obligatorio y los posteriores opcionales:
https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43128672107.

En Android, `RealCodeFormatPilotAndroidTest` verificó **esa misma lección
publicada** y las pruebas JUnit históricas conservaron las firmas y
direcciones de estado. Compilación y pruebas JVM correctas:
https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43129514814.

## Compatibilidad combinada con puzles de orden históricos

La lección real `expresiones_y_decisiones/lesson_alternativas_y_fronteras.json`
incluye cinco fuentes reformateadas y un puzle `order_code` ya refinado
mediante `stateLayout.orderRefinements`. Un lector debe comprobar los dos
archivos de procedencia por separado antes de validar la huella original.

Android ahora autentica la instantánea original del puzle de orden, verifica
su expansión física exacta y conserva el estado guardado **sin reescribir
preferencias**. En la representación actual de esta lección, una respuesta
anterior de cuatro tarjetas se muestra como diez líneas, manteniendo el orden
correcto o incorrecto correspondiente. Los intentos originales permanecen
guardados hasta que el estudiante decida modificarlos.

La prueba `PublishedOrderFormatAndroidTest` verifica el bloque real,
las cinco revisiones de código y la detección de índices, fragmentos, respuestas
y procedencias falsificados. La suite JUnit de Android se aprobó en:
https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43131797969.

La verificación es automatizada y no sustituye la prueba visual del usuario
en un teléfono. Una nueva clase de revisión histórica o una estructura
curricular no reconocida **no** queda autorizada implícitamente por este
piloto y requiere su validación específica.

## Revisiones históricas adicionales comprobadas en Android

Algunas lecciones antiguas tienen fuentes C++ protegidas **junto con**
`stateLayout.choiceRevisions` y `stateLayout.compilerRevisions`.
El validador Android ahora reconoce las instantáneas anteriores únicamente
cuando corresponden a la identidad, firma y cadena editorial publicadas.

Las revisiones de opciones conservan `correctIndex` y aceptan únicamente
una sustitución documentada de opción; no se normalizan respuestas distintas.
Las ampliaciones de compilador sólo admiten nuevos casos o extensiones de
salida esperada, y referencias C++ asociadas que conservan íntegramente el
código anterior. No se atribuye una ejecución ni una evaluación nueva a un
logro antiguo. Las correcciones de saltos físicos siguen requiriendo
`sourceFormatRevisions` con su bloque exacto anterior.

La suite usa lecciones publicadas de
`texto_interpretacion_y_flujos`, entre ellas
`lesson_construir_un_lector_de_entrada_preparada.json`,
`lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json` y
`lesson_construir_un_interprete_con_historial_y_pruebas.json`.
Incluye una corrección física real sobre el código inicial de un editor
con una ampliación histórica de `outputCheck`, sin alterar los casos
anteriores ni su clave de progreso.

La última de esas lecciones contiene además una aclaración matemática
histórica. Únicamente se reconocen dos pares exactos de etiquetas
matemáticas ya acreditados, con identidad y huellas completas cerradas.
No existe una nueva autorización general para alterar las respuestas
de `math_puzzle`.

Compilación y pruebas unitarias Android acreditadas en:
https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43137621191.

Estos contratos son **específicos y conservadores**. No habilitan otras
revisiones históricas, cambios de lógica o edición de la evaluación.
Cada lección reformateada por Work debe superar de nuevo sus pruebas
curriculares y de estado antes de publicarse.

**Autorización operativa para Work:** una vez integrados los avances
técnicos de `Codex` mediante la sincronización administrada por el usuario,
Work puede comenzar en `main` la revisión y corrección de saltos físicos,
por lección y por idioma. Cada cambio debe pasar su transición histórica
y la validación de código, respuestas, CMake y compilación que corresponda.
Las fuentes con macro continuada, cadenas sensibles o sintaxis no
demostrada deben quedar intactas y señalarse para revisión específica.
El éxito de este piloto **no** acredita por sí solo todas las lecciones ni
permite cerrar la auditoría transversal. La verificación visual/auditiva
habitual sigue correspondiendo al usuario durante el estudio.

## Auditoría ampliada del lector Android

La prueba de inventario `PublishedStateLayoutInventoryAndroidTest` recorre
todos los archivos `lesson_*.json` de los tres idiomas y usa el mismo
validador de metadatos que la aplicación. **No modifica ningún archivo de
lección ni preferencias del estudiante.** Cada ejecución produce un reporte
diagnóstico en `app/build/reports/curricular-state-inventory.json`, conservado
como artefacto de GitHub Actions.

En la ejecución del commit `31b62170082d2181cfeee6a397b30ea599a64536`,
se examinaron **763 lecciones**, de las cuales **187** contienen mapas
históricos. Las **12 lecciones con `sourceFormatRevisions`** pasaron la
validación Android. Se registraron por separado **28 lecciones con
incompatibilidades de otros contratos históricos**. La ejecución aprobó
únicamente la compatibilidad de formatos, **no** declaró corregidas esas
28 incidencias. El informe contiene las rutas y causas para que Codex
identifique y atienda cada contrato con procedencia y pruebas auténticas.
No eliminar ni reemplazar huellas originales para silenciar el informe.

Resultado y artefacto de la ejecución:
https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/43139232923

## Separación de responsabilidades

Codex implementa la compatibilidad y verifica los contratos en `Codex`.
Work revisa la legibilidad pedagógica de todos los cursos y publica únicamente
sus modificaciones curriculares en `main`. El usuario controla la
sincronización de ramas. Sólo Work modifica
`PENDIENTES_RECURSOS_CURRICULARES.md` y elimina lo efectivamente concluido.

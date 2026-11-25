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

En `projectFiles`, Windows y el validador de autoría reconocen CMake por el
nombre exacto `CMakeLists.txt` del último segmento de la ruta, o por el sufijo
`.cmake`. Esto incluye `CMakeLists.txt`, `componente/CMakeLists.txt` y
`codigo/lotes/CMakeLists.txt`. Se aplica la misma comparación léxica en la raíz
y en subcarpetas; los argumentos, comentarios y límites entre comandos
conservan su significado. Las rutas, el orden de archivos y sus propietarios
siguen siendo los publicados. Este reconocimiento no normaliza mayúsculas ni
convierte nombres como `notCMakeLists.txt` o `CMakeLists.txt.bak` en recetas.

La comprobación `native.nested_cmake_format` usa las tres publicaciones completas
de Programación 2 fijadas en el generador
`scripts/curriculum/nested_cmake_format_fixture_source.py`. Las revisiones de
espaciado se preparan sólo en sus fixtures de QA para los diez editores
afectados. El contenido curricular se integra mediante el flujo de autoría
descrito a continuación.

## Generadores de QA con revisiones coexistentes

Los generadores de compilación seleccionan cada ejercicio por su identidad
estable. La presencia de una revisión anterior o de archivos históricos de
otros ejercicios no exige borrar ni reconstruir el historial de la lección.
Una nueva revisión conserva el bloque canónico completo ya archivado y añade
su prueba al prefijo editorial publicado. Si todavía no existe un archivo de
formato para ese propietario, se incorpora su bloque anterior completo.

`compile_check_source_format_fixture.py` y `test_real_code_format_pilot.py`
usan por defecto la publicación del árbol de trabajo. Su opción
`--publication` permite además comprobar las familias completas de
`921e1df` y `83e2d4e6`, autenticadas por commit y SHA-256. El generador
`code_format_fixture_source.py` conserva el escenario U7 anterior y el
escenario posterior con archivos coexistentes. Una publicación histórica
ausente o con bytes diferentes produce un error; no se sustituye por una
copia actual ni por un documento reconstruido desde bloques sueltos.

El piloto conserva las aristas del formato original de sus dos ejemplos,
los demás archivos y los compiladores retirados. Su vista de comparación de
fuentes antiguas nunca se exporta como una publicación histórica válida.
Los fixtures cargables conservan la publicación completa y el lector obtiene
las fuentes anteriores de sus archivos autenticados.

Las pruebas `native.format_fixture_history_original` y
`native.format_fixture_history_coexisting` ejercitan los dos cargadores y
la conservación del estado. Las correspondientes pruebas
`native.format_fixture_diagnostics_original` y
`native.format_fixture_diagnostics_coexisting` usan el runner original de
MSVC para los 17 diagnósticos antes y después del espaciado. El job adicional
de QA Windows conserva estos resultados y los de autoría sin cambiar el
toolset fijado para el producto ni los contenidos curriculares.

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

## Formato posterior a una ampliación cerrada en Windows

Una revisión cerrada de `compilerRevisions` y una revisión de formato conservan
dos versiones anteriores distintas. La primera archiva el editor original y
su referencia; la segunda archiva el bloque ampliado completo que recibió el
espaciado. Ambas instantáneas permanecen bajo sus propietarios y conservan sus
huellas publicadas.

Por ejemplo, en U11L8 la referencia `block_0007_0001` incorpora una llamada
aprobada junto con la ampliación de su editor. Si después Work añade una línea
vacía antes del retorno final, la comparación léxica corresponde a esa referencia
ampliada. El programa anterior a la llamada conserva su propio contrato y no se
convierte en el origen del espaciado posterior.

La validación admite comprobar los tres pasos, también cuando la base publicada
es anterior a ambos cambios:

- Versión original → ampliación cerrada autenticada.
- Ampliación → mismo código con otro espaciado físico.
- Versión original → resultado acumulado de ampliación y formato.

Para el último caso, el validador reconstruye el bloque ampliado exclusivamente
desde el archivo original y la ampliación exacta ya reconocida. Comprueba la
lección, el propietario, la referencia asociada y las huellas de ambos extremos.
Sólo las ampliaciones nuevas respecto a la base intervienen en esa comparación;
una revisión ya presente no vuelve a aplicarse. Las revisiones posteriores de
espaciado conservan la primera instantánea de formato y añaden sus aristas
editoriales habituales.

Esto incluye el segundo editor U11L15, cuya ampliación cambia a la vez
`initialCode`, el resultado esperado y la referencia. La comprobación de
conservación utiliza un contrato completo de la versión correspondiente: no
combina el inicial ampliado con los casos antiguos. Las revisiones cerradas
mantienen sus propias condiciones de acreditación; el formato no convierte
una ejecución del objetivo anterior en prueba del ampliado.

Work conserva el flujo `prepare_v2` y la validación contra el commit publicado.
No necesita propiedades nuevas, restaurar programas obsoletos, reemplazar
instantáneas ni modificar huellas históricas. Un cambio de tokens fuera de la
ampliación reconocida, un archivo alterado o una referencia de otro propietario
siguen rechazándose. La prueba de autoría
`scripts/curriculum/test_code_format_compat.py` incluye las transiciones de
U11L8 y U11L15; debe ejecutarse en el PC Windows autorizado junto con las
comprobaciones pertinentes de integración.

## Protección semántica

La comparación exige conservar los elementos léxicos de la fuente, incluidos
identificadores, valores, operadores, cadenas, comentarios y directivas. Los
saltos físicos dentro de cadenas crudas, literales y argumentos entre corchetes
CMake tienen significado y no pueden retocarse. Se protegen las continuaciones
con barra invertida y salto físico. No se permite usar este mecanismo para
cambiar fórmulas, reparaciones, dependencias, resultados de programa ni
expectativas exactas de stdout/stderr.

En Windows y en el validador Python, las fuentes que contienen una barra
invertida activa fuera de comentarios, literales y directivas encapsulados
se conservan byte a byte bajo este contrato. El analizador de formato no
resuelve los límites de los nombres universales de carácter: por ejemplo,
`a\u03B1` y `a \u03B1` pueden representar identificadores distintos.
Por eso un cambio de espacios en una fuente con esos nombres no se acredita
como formato equivalente. Una fuente idéntica continúa admitida. Las
menciones dentro de comentarios y literales ordinarios o crudos no activan
esta restricción; sus propios bytes siguen protegidos por la comparación
habitual. La guarda sólo afecta a la equivalencia de formato, sin impedir
que los otros comprobadores analicen ediciones de cuerpos de funciones.
Esta protección no habilita un mecanismo general de renombrado histórico.
La distinción entre nombres y espacios sigue las
[fases de traducción de C++](https://eel.is/c++draft/lex.phases).

El comparador es **conservador**: si no demuestra que los límites léxicos se
conservan, rechaza la revisión. No sustituye compilar, ejecutar y comprobar
los programas. Los ejemplos que requieran una nueva distribución semántica
de comentarios o macros necesitan revisión específica, no forzar la huella.

En `order_code` se conserva la identidad de cada fragmento y la secuencia
de índices, aunque su representación textual pueda mejorar. Los separadores
`orderVisualSeparators` siguen siendo decorativos; no sustituyen las líneas
físicas que Work haya incorporado a la fuente.

## Ejemplos con un literal ordinario sin cierre

El comparador Windows y la autoría Python admiten un caso conservador para un
literal C++ ordinario
que alcanza un salto físico de línea o el final de la fuente sin su comilla de
cierre. Puede cambiar el
espaciado del código anterior al literal si conserva sus tokens. Desde el inicio
del literal —incluido un prefijo como `u8` o `L`— hasta el final del archivo,
todos los caracteres deben permanecer idénticos. No se admite insertar una línea
dentro de ese tramo, añadir la comilla que falta ni cambiar instrucciones
posteriores mediante una revisión de formato.

Esta ruta se utiliza exclusivamente al comparar formato. Los analizadores
léxicos que emplean los evaluadores mantienen su rechazo estricto de fuentes
incompletas. Los literales crudos sin cierre y los comentarios incompletos
siguen fuera de este permiso. Las continuaciones físicas con barra inversa
siguen bloqueadas; también se rechaza una cola opaca con marcadores del
preprocesador o consultas de posición, porque no se puede acreditar que un
cambio anterior de líneas conserve su interpretación.

La autoría usa el mismo `prepare_v2`, archivo completo en
`sourceFormatRevisions` y validación de transición. No necesita una propiedad
JSON nueva ni una excepción por lección. La prueba de U1L4 utiliza
`p1u1l4_codigo_sin_comilla` desde su publicación autenticada y genera copias
aisladas: compara ambos cargadores Windows, conservación del estado, rechazos de
alteraciones e historial, y el error C2001 de MSVC antes y después del formato.
No modifica el ejemplo curricular ni su diagnóstico contiguo.

Si Work añade renglones antes del error, debe revisar también las coordenadas
de línea escritas en la explicación del diagnóstico. Conservar la causa y su
código de error no conserva automáticamente números como `main.cpp(4)`.
Cambiar únicamente espacios anteriores al literal en la misma línea evita ese
desplazamiento. Las fuentes malformadas se compilan sólo para comprobar su
diagnóstico; no se ejecutan como programas correctos.

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


## Prosa posterior a un renombrado autenticado

`identifierRenameRevisions` conserva el bloque anterior, la correspondencia
autoral de nombres y el hash del resultado exacto del renombrado. Ese archivo
permanece inmutable cuando Work corrige después la pregunta, la explicación
o sus orientaciones mediante `revise_ordinary_editorial`. La fuente C++ y el
contrato de respuesta deben conservarse byte por byte. Una segunda operación
de renombrado o un nuevo espaciado de la fuente no pertenecen a esta composición.

Cada arista posterior del mismo propietario añade `identifierRenameProse`: el
prefijo editorial completo autenticado, los bloques completos anterior y actual,
los descriptores de texto de ambas presentaciones y `semanticRootBefore`, que
distingue un contenedor de marcas ausente de uno vacío. No basta con que el
último bloque coincida con una huella conocida: se comprueban todos los
predecesores, los campos ordinarios permitidos y sus rangos UTF-16. Los demás
propietarios pueden intercalarse, y un retorno requiere el `returnTo` explícito
ya existente. Una cadena sin uno de sus archivos intermedios se rechaza.

La proyección de contratos restaura únicamente las fuentes antiguas y conserva
la prosa de cada presentación autenticada. La proyección histórica recupera
además sus propias marcas, sin reordenar los campos de otros propietarios. Abrir,
reabrir o importar conserva las respuestas, los intentos, los borradores y las
pausas previas; una frase nueva no recibe acreditación de lectura de la anterior.

La regresión `identifier_rename_prose_fixture_source.py` parte de la publicación
U2L6 `55e60dcd4c9da2ddf71aad6eddd1c5937e312337` y genera solamente archivos QA.
Incluye los dos renombrados declarados, revisiones separadas de pregunta y
explicación, otro propietario intercalado, un retorno y dieciséis historias
negativas. `test_identifier_rename_prose_history.py` comprueba autoría y
transiciones; el ejecutable `identifier_rename_prose_tests.cpp` ejercita ambos
cargadores Windows y `StateStore`. La existencia de esas pruebas no acredita
su ejecución Windows: el resultado se obtiene del workflow focal correspondiente.

# Comprobación de objetivos del compilador

`cpp_compiler` admite `outputCheck` para declarar un objetivo de stdout. Con él,
el botón muestra «Compilar, ejecutar y comprobar»: una pulsación construye,
ejecuta y comprueba la salida nueva. No se añade un segundo botón. Work incorpora
los casos después de verificar su objetivo; el motor no deduce respuestas de la
prosa, de una predicción contigua ni del código de referencia.

El contrato Windows incluye también `compileCheck` para los rechazos intencionales
revisados. Todo editor curricular es obligatorio, aunque falte el antiguo flag
`compilerPuzzlesRequired`; la falta de evaluador es un bloqueo técnico, no permiso
para omitirlo. El Laboratorio libre conserva la exploración sin objetivos. Los
apartados de Windows siguientes describen la implementación nativa; las notas
identificadas como Android conservan únicamente su alcance histórico.

```json
"outputCheck": {
  "inputComparison": "exact",
  "cases": [
    {
      "standardInput": "",
      "outputs": ["Caja recibida"],
      "explanation": "Las comillas delimitan el literal y no forman parte del mensaje.",
      "reviewFeedback": [
        {
          "outputs": ["Zona segura"],
          "text": "Revisa si realizaste y ejecutaste la segunda modificación."
        }
      ]
    }
  ]
}
```

El ejemplo comprueba únicamente la construcción final de esa etapa. Las ejecuciones del original y de la primera variación siguen disponibles, sin exigir otro historial ni otra comprobación. No se añaden identificadores al bloque ni se modifica el progreso existente.

## Criterio curricular posterior: primera comprobación suficiente

La regla de avance indicada el 11 de noviembre de 2026 es acreditar **únicamente
el primer caso** de un editor para continuar. Las demás entradas y salidas deben
aparecer en el texto curricular y quedar disponibles para pruebas **opcionales**,
sin exigir otra pulsación ni bloquear el lector, las referencias o las páginas.
Las sondas internas que se ejecutan automáticamente durante la primera
comprobación pueden seguir protegiendo los criterios semánticos: no son nuevas
pruebas manuales que deba realizar el estudiante.

**Implementación Windows:** el primer elemento de `outputCheck.cases` es el
caso obligatorio. Su ejecución correcta y verificada concede un recibo de avance
sin exigir las entradas posteriores. Éstas siguen disponibles para ejecutarse
y observarse voluntariamente; un caso posterior nunca sustituye al primero.
Los recibos antiguos que acreditaron todos los casos se reconocen sin cambios.
Los requisitos semánticos del primer caso, incluidas sondas internas, se siguen
comprobando antes de conceder el logro. La incorporación de declaraciones
curriculares U3L6/U3L8 requiere las verificaciones específicas de Work.


## Transferencia de propiedad entre dos miembros: U6L5

Para Programación 3, U6L5, el motor admite la declaración optativa
`compilerIndexedUniqueTransferChecks` en la raíz española de la lección con
`stateLayout` v2. Se refiere al editor original `block_0021_0002`, con
un solo archivo C++ y `outputCheck`. Work puede integrar **exactamente**:

```json
"compilerIndexedUniqueTransferChecks": [
  {
    "activity": "block_0021_0002",
    "signature": "bool Deposito::transferir(int origen, int destino)",
    "className": "Deposito",
    "firstMember": "primero_",
    "secondMember": "segundo_",
    "transferMethod": "transferir",
    "putMethod": "poner",
    "changeMethod": "cambiar",
    "clearMethod": "vaciar"
  }
]
```

El contrato conserva la función original, sus propietarios
`std::unique_ptr<int>`, los casos, el programa `main`, las identidades y
los borradores. Sólo autoriza modificar el cuerpo de `transferir`. La
primera comprobación visible se acompaña de una compilación privada que
verifica la identidad del mismo entero, el vaciado del origen, los rechazos
sin cambios, la destrucción del destino sustituido y la ausencia de
reservas durante el traslado. Una copia con el mismo valor, un intercambio
incompleto o una salida fabricada no acreditan ese contrato.

La regresión Windows `native.u6l5_indexed_unique_transfer_state` comprueba
cargador, estado histórico y recibo independiente. La ejecución
`scripts/curriculum/test_cpp_indexed_unique_transfer_runtime.py` compila
y ejecuta tres soluciones equivalentes y siete variantes rechazadas (una
por contaminar stdout y otra por copiar un entero cuando los valores de ambos
propietarios coinciden) con GCC, Clang
y MSVC disponible en CI. El comprobador privado exige stdout vacío y que stderr sea exactamente
el marcador final emitido tras todas las verificaciones; una coincidencia
parcial del marcador no demuestra éxito. El ejecutor de QA compila además cada una de las tres soluciones correctas
con el `main` original y las contrasta con **todos** los casos públicos
publicados, incluidos los dos que permanecen opcionales para el alumno.
También comprueba que la implementación incorrecta que copia un entero
cuando ambos valores coinciden supera el primer caso de stdout y, pese
a ello, es rechazada por la sonda privada de identidad. La QA no exige
realizar manualmente los casos opcionales. Estas pruebas **no publican** el campo en el JSON.

## Política de movimiento generada: U6L6

Para Programación 3, U6L6, el editor original `block_0018_0002` puede
recibir `compilerDefaultedMovePolicyChecks` como propiedad raíz de su
lección española con `stateLayout` v2:

```json
"compilerDefaultedMovePolicyChecks": [
  {
    "activity": "block_0018_0002",
    "className": "Ficha",
    "memberName": "dato_",
    "putMethod": "poner",
    "changeMethod": "cambiar",
    "clearMethod": "vaciar"
  }
]
```

La sonda comprueba la política de constructor predeterminado, construcción
y asignación por movimiento con `= default`, sin convertir los
propietarios en copiables. También verifica la identidad del
`unique_ptr<int>`, la limpieza del destino, el origen vacío y los
rechazos de copia al compilar. La política forma parte de la comprobación
interna del primer caso, sin pedir casos manuales adicionales. La
regresión `native.u6l6_defaulted_move_policy_state` verifica el estado
y los recibos, y `test_cpp_defaulted_move_policy.py` ejecuta
las variantes C++ con compiladores reales. La misma QA conserva el
`main` original de ambas soluciones correctas y contrasta automáticamente
sus tres casos públicos, sin convertir los dos casos posteriores en
obligatorios para el estudiante ni acreditar el contrato de propiedad por
una coincidencia de stdout.

## Referencia conjunta de autoría de la ficha 14

El archivo
[`p3_ownership_authoring.json`](examples/p3_ownership_authoring.json)
recoge las **14 declaraciones** correspondientes a U6L5–U10L1, una por
lección. Es una guía de integración de Codex, no una modificación de las
lecciones. Su prueba
`test_p3_ownership_authoring.py` aplica cada declaración sólo en memoria
al JSON auténtico, comprueba los campos permitidos y valida la transición
del mapa de estado estable sin alterar las respuestas ni el origen. Work
decide su publicación y verifica las referencias, las variantes correctas
y las incorrectas antes de retirar la ficha. Las regresiones de Python,
las sondas privadas o un éxito de stdout por separado **no** acreditan
la aceptación visual y auditiva de la aplicación instalada.

La prueba compartida `native.p3_ownership_shared_state` ejecuta el
controlador WinRT de los 14 propietarios en un solo perfil QA aislado:
comprueba ambos cargadores (completo y de progreso), recibos históricos,
borradores, declaraciones de origen y ausencia de promoción automática
de aciertos antiguos. Está registrada en CMake para los perfiles
`LGAED_STATE_IMPORT_QA_ONLY`, `LGAED_READER_BUILD_QA_ONLY` y la
compilación Windows normal con `BUILD_TESTING`. Se ejecuta desde el
árbol compilado con
`ctest -R "^native[.]p3_ownership_shared_state$" --output-on-failure`.
`test_p3_ownership_authoring.py` verifica también su registro y la
conexión de los 14 campos al motor; CI lanza esa comprobación si cambian
las declaraciones, CMake o las piezas nativas que las consumen.
Que CI valide el registro no significa que haya ejecutado la prueba WinRT:
ésta requiere una compilación nativa Windows.

## Asignación por copia independiente en una clase

La raíz de una lección española existente con mapa de estado v2 puede declarar
`compilerIndependentCopyAssignmentChecks` para añadir una comprobación
semántica de asignación por copia **sin modificar el editor publicado**.
Se utiliza la identidad actual de un único bloque `cpp_compiler` de archivo
único con `outputCheck`; la declaración no crea otro ejercicio ni reescribe
`initialCode`, `outputCheck`, direcciones, borradores o resultados anteriores.

Para Programación 3, U6L7, la declaración compatible es:

```json
"compilerIndependentCopyAssignmentChecks": [
  {
    "activity": "block_0014_0002",
    "className": "Celda",
    "memberName": "dato_",
    "parameterName": "otra",
    "hasMethod": "hay",
    "valueMethod": "valor",
    "accessMethod": "acceso",
    "putMethod": "poner",
    "changeMethod": "cambiar",
    "clearMethod": "vaciar"
  }
]
```

Los nombres describen símbolos **reales del código original**. Debe existir
exactamente la definición exterior
`Celda& Celda::operator=(const Celda& otra)` y su miembro
`std::unique_ptr<int> dato_`. El motor admite diferentes implementaciones
del cuerpo de ese operador, así como diferencias inocuas de espacios y
comentarios. No admite que el estudiante cambie las declaraciones, los
miembros, el constructor de copia, las demás funciones o `main`.
Tampoco admite otras sondas privadas sobre el mismo propietario.

Una pulsación compila y ejecuta el caso visible elegido. Si se cumple su
contrato de salida, una segunda compilación privada examina también el
comportamiento de la misma clase. Comprueba que copiar una fuente ocupada
crea un entero independiente incluso con valores iguales, que se libera el
destino anterior, que una fuente vacía vacía el destino sin reservar, que
autocopiar (ocupado o vacío) no cambia la identidad ni solicita memoria,
y que un `std::bad_alloc` inyectado de forma controlada conserva origen
y destino. También comprueba devolución por referencia, valor cero presente,
reutilización después de mover y limpieza. No se requiere ejecutar ni
introducir los casos privados manualmente.

La prueba independiente nunca modifica el fuente que entregó el estudiante:
genera una unidad de compilación temporal con ese mismo fuente y un
`main` de QA aislado. El resultado se vincula al contenido completo de
la petición, a su revisión y al hash del driver privado. Un resultado antiguo
de stdout no acredita automáticamente la nueva semántica. El sello
`independent-copy-assignment/v1` se añade solamente tras una comprobación
correcta, conservando los recibos y borradores históricos.

Esta variante se refiere a **copia con `unique_ptr<int>`** y a los métodos
declarados arriba. No constituye una prueba general de todos los operadores,
de propietarios de arrays ni de jerarquías; cada contrato posterior requiere
su ampliación y validación correspondientes. Para que una nueva declaración
sea utilizable por el estudiante, Work debe incorporarla al JSON auténtico de
la lección y validarla. Los ensayos de CI con metadatos temporales no
publican por sí solos la declaración en el currículo.


## Invariantes de asignación por movimiento y propiedad única

El campo raíz español `compilerMoveAssignmentInvariantChecks` puede asociarse
a un editor `cpp_compiler` ya publicado, con `stateLayout` v2, un único
fuente y `outputCheck` original. Su propósito es acreditar la asignación
por movimiento de una clase con `std::unique_ptr<int>` y una etiqueta
que indica el estado. No modifica el código, los casos, los identificadores
ni la posición histórica del bloque.

Declaración concreta para Programación 3, U6L8:

```json
"compilerMoveAssignmentInvariantChecks": [
  {
    "activity": "block_0014_0002",
    "className": "Muestra",
    "memberName": "dato_",
    "labelMember": "etiqueta_",
    "parameterName": "otra",
    "hasMethod": "hay",
    "valueMethod": "valor",
    "labelMethod": "etiqueta",
    "accessMethod": "acceso",
    "invariantMethod": "coherente",
    "putMethod": "poner",
    "changeMethod": "cambiar",
    "retagMethod": "reetiquetar",
    "clearMethod": "vaciar"
  }
]
```

El motor obtiene la definición original de
`Muestra& Muestra::operator=(Muestra&& otra)` desde el editor auténtico
y admite soluciones distintas **dentro de su cuerpo**. Conserva sin
alteraciones los caracteres del fuente proporcionado fuera del cuerpo
editable, incluidos las declaraciones, el constructor de movimiento,
miembros y `main`. No se permite modificar el preprocesador en el cuerpo,
cerrar la función antes de tiempo ni utilizar otra comprobación privada
sobre la misma actividad.

Una pulsación conserva el caso visible y ejecuta automáticamente otra
compilación privada. Esta segunda ejecución prueba que mover entre
objetos distintos transfiere el mismo entero sin reservar otro, libera
el entero anterior del destino y deja vacío el origen con etiqueta cero.
Comprueba directamente las funciones, sin depender de que el programa
principal muestre esa transición. Verifica origen vacío, destinos con
valores iguales y etiquetas distintas, cero como valor válido, automovimiento
ocupado o vacío, referencia retornada, rechazos que conservan el estado,
reutilización del origen y destrucción normal o durante el desenrollado
de una excepción. Se vigilan las reservas reales; el movimiento no necesita
pedir memoria. Nunca se accede a un objeto ya destruido.

La prueba privada tiene su propia huella de código, fuente auténtico,
parámetros, solicitud, revisión y compilación. Un acierto anterior de
`outputCheck` no concede este nuevo objetivo. La acreditación añade
el comprobante `move-invariant/v1` únicamente tras ejecutar correctamente
los contratos de salida y semántica, conservando el recibo histórico
y los borradores. No se piden pruebas manuales adicionales al estudiante.

Esta variante concreta cubre la clase y la política descritas. No se
extiende por sí sola a arrays, copias o jerarquías. Work debe añadir
la declaración al JSON original de la lección y verificar su integración;
los archivos de prueba con metadatos temporales no publican dicha autoría.

La regresión `test_cpp_move_assignment_invariant_core.py` compila el
`main` original y contrasta sus tres casos públicos para las dos
implementaciones válidas y para un contraejemplo cuyo operador omite
reiniciar la etiqueta del parámetro de origen. Ese contraejemplo puede
producir las mismas salidas porque `main` destruye la intermedia
inmediatamente; únicamente la sonda privada directa revela el
incumplimiento del invariante. Las dos entradas posteriores del editor
siguen siendo opcionales para el estudiante: esta batería adicional
pertenece solo a QA.


## Arrays propietarios y lectura con límites

En Programación 3, U6L10, el motor admite añadir a la raíz de la lección
española con `stateLayout` v2 una declaración
`compilerArrayOwnerReadChecks`. Se vincula a un editor `cpp_compiler`
existente de archivo único que ya dispone de `outputCheck`.
No cambia los casos originales, la fuente, los borradores, los identificadores
ni sus direcciones de progreso.

```json
"compilerArrayOwnerReadChecks": [
  {
    "activity": "block_0017_0002",
    "className": "Lote",
    "ownerMember": "datos_",
    "countMember": "cantidad_",
    "quantityParameter": "cantidad",
    "indexParameter": "indice",
    "outputParameter": "resultado",
    "configureMethod": "configurar",
    "readMethod": "leer",
    "quantityMethod": "cantidad",
    "accessMethod": "acceso",
    "writeMethod": "escribir",
    "clearMethod": "vaciar",
    "sumMethod": "sumar"
  }
]
```

La declaración identifica la clase y sus métodos y miembros originales.
El estudiante puede construir implementaciones semánticamente equivalentes
dentro de los **dos cuerpos editables**:
`bool Lote::configurar(int cantidad) noexcept` y
`bool Lote::leer(int indice, int& resultado) const noexcept`.
La comprobación conserva exactamente los bytes externos a ambos cuerpos,
incluidas firmas, prohibiciones de copia y movimiento, `escribir`,
`vaciar`, `sumar` y `main`. Las directivas de preprocesador no
pueden introducirse ocultamente dentro de los cuerpos.

`configurar` admite cero (vacía y libera sin reservar) y cantidades
positivas de uno a cuatro. Una cantidad válida prepara un nuevo
`int[]` inicializado a cero antes de sustituir el array anterior.
Esto incluye repetir la misma cantidad con los mismos valores: escribir
ceros en el array existente no acredita la sustitución. Una cantidad
inválida o una reserva fallida controlada debe conservar exactamente
el propietario, el tamaño y sus valores, y devolver falso sin propagar
`std::bad_alloc` desde el método `noexcept`.

`leer` exige comprobar el índice antes del acceso y modificar la referencia
de salida sólo si es válido. Al rechazar, debe conservar cualquier valor
anterior de esa referencia, incluido `8`, `88` u otro distinto de `-1`.
La sonda independiente comprueba los límites lógicos mediante un array
temporal con memoria física adicional, evitando accesos físicos inválidos
en los contraejemplos preparados. También comprueba limpieza con
`delete[]`, reutilización, cero presente y salida por excepción del
llamador. No expone la sonda ni exige ensayos manuales adicionales.

El motor ejecuta primero el caso visible y después la compilación
privada con el código del estudiante preservado. La prueba privada
lleva una huella propia de fuente, declaración, controlador, solicitud y
revisión. Una comprobación histórica basada únicamente en stdout no
concede este objetivo: sólo una comprobación nueva y válida añade el
sello `array-owner-read/v1`. El resto de logros, respuestas y borradores
permanece intacto.

La implementación admite estos dos métodos y su contrato concreto.
No acredita por sí sola la copia o movimiento de arrays de U6L11.
Work incorpora la declaración raíz al JSON auténtico cuando corresponda,
valida su transición y verifica el ejercicio en la aplicación.
Las pruebas temporales de Codex no publican metadatos curriculares.
La regresión `test_cpp_array_owner_read_core.py` compila el programa
original para las dos soluciones válidas y para un contraejemplo de
`leer` que escribe `-1` cuando rechaza un índice. Los tres programas
pueden coincidir con **los tres casos públicos** porque el `main`
prepara la variable de salida con ese mismo valor. El contraejemplo
se rechaza únicamente al comprobar la referencia de salida con otros
centinelas mediante la sonda privada segura. Los casos posteriores al
primero siguen siendo opcionales para el alumno; esta batería es QA
automática y no una exigencia de ejecución manual.


## Copia y movimiento independientes de un array propietario

Para Programación 3, U6L11, un editor `cpp_compiler` existente, español,
de archivo único y con mapa de estado `v2` puede recibir una declaración
raíz `compilerArrayCopyMoveChecks`. La capacidad se incorpora como
objetivo **independiente** de los antiguos casos de salida, sin cambiar
`initialCode`, `outputCheck`, su ID ni los borradores guardados.

Contrato de autoría para el propietario histórico `block_0020_0002`:

```json
"compilerArrayCopyMoveChecks": [
  {
    "activity": "block_0020_0002",
    "className": "Tramo",
    "ownerMember": "datos_",
    "countMember": "cantidad_",
    "copyParameter": "otra",
    "moveParameter": "otra",
    "quantityMethod": "cantidad",
    "accessMethod": "acceso",
    "configureMethod": "configurar",
    "readMethod": "leer",
    "writeMethod": "escribir",
    "clearMethod": "vaciar",
    "sumMethod": "sumar"
  }
]
```

El motor deriva los dos operadores originales:
`Tramo& Tramo::operator=(const Tramo& otra)` y
`Tramo& Tramo::operator=(Tramo&& otra) noexcept`. El estudiante
puede modificar únicamente sus **cuerpos**. El resto del programa
debe conservarse exactamente, incluidos constructores, destructor,
datos, métodos de consulta y edición, `main` y casos de prueba.
La validación no exige copiar la solución de referencia: admite otros
procedimientos C++ correctos en los cuerpos, siempre que cumplan
el contrato observable.

Una comprobación visible correcta del **primer caso** inicia, en la
misma pulsación, una ejecución nativa separada y privada. Esta verifica:

1. Copiar una fuente ocupada a un destino distinto crea otro
   `int[]` independiente, incluso con los mismos tamaños y valores.
   El origen conserva su array, cantidad y valores; el destino anterior
   se libera al aceptar la copia.
2. Copiar desde vacío vacía el destino sin reservar. Autocopiar,
   con array o sin él, conserva identidad, cantidad y valores sin
   reservar ni liberar. Ambos operadores devuelven referencia al destino.
3. Si falla controladamente la reserva para copiar, `std::bad_alloc`
   llega al llamador y tanto el origen como el destino conservan
   sus arrays, cantidades y valores anteriores. No se agota la memoria.
4. Mover entre objetos distintos transfiere **el mismo array** sin
   reservar ni copiar sus elementos, libera el destino anterior,
   y deja vacío al origen con cantidad cero; esto también vale
   cuando el origen ya está vacío.
5. Automover un objeto, vacío u ocupado, conserva su estado; después
   de mover, el origen puede reutilizarse. Las pruebas cubren ceros,
   valores iguales, cantidades distintas, destrucción normal y
   limpieza al propagarse una excepción del llamador.

Los controladores privados observan únicamente punteros vivos
y no requieren liberar dos veces, acceder fuera del array ni usar
referencias a objetos destruidos. Una salida simulada o una copia
de valores que genere otro propietario durante el movimiento
no acredita el objetivo.

El resultado privado se vincula al código completo, a la fuente
original autenticada, a los identificadores del contrato, al driver
generado y a la solicitud/revisión de ejecución. Un logro anterior
de `outputCheck` **no se promueve automáticamente**: sólo un
resultado nuevo añade el sello `array-copy-move/v1`. Los recibos
históricos, respuestas, borradores y demás cursos se conservan.

Work debe integrar la declaración real en la lección al llegar a este
objetivo, validar su transición y comprobar la app. Las sondas de
Codex con metadatos temporales comprueban la capacidad del motor,
pero no publican la nueva declaración en el currículo. Este contrato
no acredita por sí solo las funciones externas de U6L12 ni las
jerarquías posteriores.

La regresión `test_cpp_array_copy_move_core.py` conserva los dos
operadores auténticos fuera de sus cuerpos editables y contrasta el
`main` original con **sus tres casos públicos**. Dos implementaciones
válidas y tres incorrectas pueden imprimir exactamente las mismas
salidas: reutilizar un array de igual tamaño durante una copia, ocultar
`std::bad_alloc` o devolver la referencia al origen al mover. La sonda
semántica independiente debe aceptar únicamente las correctas, al
comprobar identidad y nuevas reservas, conservación ante fallo y
retorno al destino. Esta batería es automática y no convierte los dos
casos públicos posteriores en requisitos manuales para el alumno.


## Devolver una copia y recibir un propietario sin consumirlo al rechazar

Para Programación 3, U6L12, una lección española `v2` puede añadir
a su raíz `compilerArrayReturnTakeChecks`, sin reemplazar el bloque
histórico de tipo `cpp_compiler`, su `initialCode`, la primera comprobación,
las entradas opcionales, las respuestas o las direcciones del progreso.
El propietario vigente es `block_0021_0002` y el ejercicio conserva
la clase `Tramo` y el programa `main` original.

La declaración general, que Work puede incorporar después de validar
la transición, utiliza los nombres reales de las dos funciones globales:

```json
"compilerArrayReturnTakeChecks": [
  {
    "activity": "block_0021_0002",
    "className": "Tramo",
    "ownerMember": "datos_",
    "countMember": "cantidad_",
    "copyFunction": "copia_modificada",
    "takeFunction": "tomar_si_cabe",
    "sourceParameter": "origen",
    "indexParameter": "indice",
    "valueParameter": "valor",
    "destinationParameter": "destino",
    "offeredParameter": "origen",
    "limitParameter": "limite",
    "quantityMethod": "cantidad",
    "accessMethod": "acceso",
    "configureMethod": "configurar",
    "readMethod": "leer",
    "writeMethod": "escribir",
    "clearMethod": "vaciar"
  }
]
```

El motor identifica en el fuente auténtico estas firmas:

`Tramo copia_modificada(const Tramo& origen, int indice, int valor)`

`bool tomar_si_cabe(Tramo& destino, Tramo&& origen, int limite) noexcept`

Los **dos cuerpos de las funciones** son editables y aceptan
implementaciones equivalentes correctas. Todos los bytes ajenos a
ellos se conservan, incluidas declaraciones, constructores,
operadores miembro, consulta y modificación del array, `main`
y salidas. El código no debe introducir macros, cerrar funciones
anticipadamente ni modificar firmas. La prueba privada nace del
código realmente entregado, con el archivo proporcionado intacto.

`copia_modificada` recibe índice y valor ya comprobados por el llamador.
Debe devolver otro `Tramo` de igual cantidad, con el dato del índice
sustituido y los demás datos conservados. El origen y su propietario
no cambian. Incluso si el dato nuevo coincide con el anterior, la
copia administra otro array independiente. Cuando falla
controladamente la reserva, deja propagarse `std::bad_alloc`:
no consume el origen ni modifica el destino de una asignación
posterior. La comprobación cubre la asignación de esa copia sobre
el propio origen y el fallo con ambos nombres referidos al mismo objeto.
No se exige un número fijo de movimientos o constructores,
que puede depender de la elisión permitida por C++.

`tomar_si_cabe` revisa `limite` **antes** de transferir.
Rechaza números fuera de cero a cuatro y cualquier límite menor
que la cantidad del origen, sin reservar, liberar ni cambiar el
origen o destino. La expresión `std::move(origen)` del llamador
no consume por sí sola ese objeto. Si se acepta, transfiere
el mismo array con sus valores y cantidad, sin copiar elementos
ni solicitar otra reserva; el destino anterior se libera
y el origen queda vacío con cantidad cero. Un origen vacío aceptado
vacía también el destino. Si ambos nombres corresponden al mismo
objeto y se admite la operación, este conserva su identidad y
contenido conforme al contrato de `Tramo`.

Una sola pulsación del caso visible obligatorio puede reunir
la salida esperada y las sondas C++ privadas. Éstas comprueban
identidad de arrays vivos, caso de valor cero, valores iguales,
preservación ante rechazo, fallo de reserva sin agotar memoria,
reutilización de objetos movidos y limpieza normal y excepcional.
No requieren más entradas del estudiante ni permiten dobles liberaciones
o accesos fuera de los límites para obtener evidencia.

La evidencia privada queda vinculada al programa completo,
a la procedencia curricular, al controlador generado y
a la solicitud y revisión concretas. El recibo
`array-return-take/v1` se incorpora sólo tras una ejecución
nueva y correcta. Los aciertos antiguos de `outputCheck`
no se promueven automáticamente. Se conservan borradores
y recibos históricos sin reescribirlos.

Work integra la declaración en el JSON auténtico cuando corresponda,
comprueba sus variantes de referencia y su presentación en la app.
La QA nativa de Codex con la declaración temporal no publica
por sí sola nuevos objetivos dentro de las lecciones.
Esta capacidad no acredita por sí sola las jerarquías posteriores.

La regresión `test_cpp_array_return_take_core.py` contrasta también el
`main` original con los tres casos públicos en dos soluciones válidas
y tres contraejemplos: suprimir `std::bad_alloc`, devolver un array vacío
cuando se copia un valor igual y fabricar otro array antes de fingir
una transferencia. Los tres defectos pueden reproducir el mismo stdout,
por lo que la sonda privada debe comprobar excepciones, identidad real
del propietario, conservación de rechazos y limpieza. Los dos casos
públicos posteriores siguen siendo opcionales para el estudiante;
esta batería adicional se ejecuta únicamente en QA.


## Destrucción polimórfica y propiedad de objetos derivados

Programación 3, U8L5, dispone de un comprobador de propiedad
`compilerPolymorphicDestructionChecks` de carácter general. Work puede
añadir a la raíz de una lección española `stateLayout` v2 la siguiente
declaración sin sustituir el editor C++, su programa inicial, sus cinco
entradas de `outputCheck`, las soluciones originales ni los IDs:

```json
"compilerPolymorphicDestructionChecks": [
  {
    "activity": "block_0020_0002",
    "baseClass": "Medida",
    "derivedClass": "MedidaIdentificada",
    "cycleClass": "Ciclo",
    "ownedClass": "Ficha",
    "ownedMember": "ficha_",
    "factoryFunction": "crear_medida",
    "writeMethod": "escribir",
    "numberParameter": "numero",
    "codeParameter": "codigo",
    "cycleParameter": "ciclo",
    "baseAlive": "bases_vivas",
    "derivedAlive": "derivadas_vivas",
    "ownedAlive": "fichas_vivas",
    "baseDestroyed": "bases_destruidas",
    "derivedDestroyed": "derivadas_destruidas",
    "ownedDestroyed": "fichas_destruidas"
  }
]
```

La declaración localiza las tres definiciones originales completas,
que deben permanecer en ese orden:

```cpp
Medida::~Medida() noexcept
MedidaIdentificada::~MedidaIdentificada() noexcept
std::unique_ptr<Medida> crear_medida(int numero, int codigo, Ciclo& ciclo)
```

Sólo se pueden modificar los **cuerpos** de esas tres funciones,
conservando byte a byte todo el resto: `struct Ciclo`, la clase base,
su destructor virtual público, la clase derivada, su `unique_ptr<Ficha>`,
constructores, miembros, interfaces, declaraciones, `main` y datos
de salida. La firma, los nombres y las llamadas se identifican por la
autoría explícita, no por la forma en que stdout mencione la operación.
Se admiten implementaciones correctas equivalentes. Los cambios
fuera de los tres cuerpos, las directivas nuevas, los cierres
anticipados y los textos mal terminados quedan rechazados.

La ejecución del primer caso visible puede acreditar la salida solicitada
y lanzar una sonda C++ privada que usa **el código verdaderamente entregado**.
Esta prueba verifica que la fábrica construye un objeto concreto derivado,
que el propietario de base conserva su identidad tras moverse, que sus
datos y el miembro `Ficha` son correctos y que la destrucción
finaliza una vez la derivada, el miembro y la base. Comprueba también
dos objetos vivos con contadores preexistentes, registros independientes,
el vaciado explícito repetido y la limpieza automática tras una excepción.
No ejecuta dobles propietarios ni lecturas posteriores a la destrucción.

Si la salida coincide pero falla el comportamiento real, la app no concede
el objetivo. La evidencia se sella con el programa completo, el origen,
la declaración, el controlador privado, el identificador de solicitud y la
revisión ejecutada. El recibo adicional se llama
`polymorphic-destruction/v1`. Un acierto antiguo de
`outputCheck` no se transforma automáticamente en prueba de
destrucción. Se conservan los recibos históricos, los borradores,
las respuestas y el progreso de otros cursos. Para avanzar, sólo el
**primer caso declarado** es obligatorio; los cuatro restantes
continúan disponibles como ensayos optativos.

Codex prueba el contrato general sin modificar los JSON del currículo.
Work puede incorporarlo cuando verifique que sus referencias y
explicaciones corresponden a los campos de origen. La regresión
independiente de escritorio y las pruebas de StateStore no sustituyen
la revisión visual de la app durante el estudio.

La regresión `test_cpp_polymorphic_destruction_core.py` conserva las tres
definiciones originales fuera de los cuerpos editables y compila el `main`
auténtico para **los cinco casos públicos** de dos soluciones correctas y
un contraejemplo: un destructor base que asigna cero al contador de bases
vivas, en lugar de retirar únicamente el objeto destruido. Los cinco casos
pueden tener la salida esperada porque cada uno crea un único propietario.
La sonda privada comprueba dos derivados vivos y contadores previos no
nulos, y debe rechazar la actualización incorrecta. Por ello, coincidencia
de stdout y destrucción polimórfica completa son pruebas distintas. Los
cuatro casos públicos posteriores al primero permanecen opcionales para
el estudiante; esta comparación adicional es QA automática.



## Despacho virtual real durante construcción, uso y destrucción

Programación 3, U8L6, dispone de un comprobador general de dos cuerpos,
`compilerVirtualLifetimeDispatchChecks`. Work puede añadir a la raíz
de una lección española con `stateLayout` v2 la declaración siguiente
sin modificar sus cinco casos, archivos de origen ni identidades:

```json
"compilerVirtualLifetimeDispatchChecks": [
  {
    "activity": "block_0015_0002",
    "baseClass": "Medida",
    "derivedClass": "MedidaIdentificada",
    "registerMethod": "registrar",
    "writeMethod": "escribir",
    "phaseParameter": "etapa",
    "registerStream": "salida",
    "writeStream": "salida"
  }
]
```

Los cuerpos de estas funciones son editables, pero sus firmas, las
declaraciones y el resto del archivo original permanecen idénticos:

```cpp
void Medida::registrar(const char* etapa, std::ostream& salida) const
void MedidaIdentificada::escribir(std::ostream& salida) const
```

La sonda privada se compila desde el código realmente entregado y verifica
que las llamadas virtuales dependen del tipo y del estado del objeto durante
construcción, uso y destrucción. Un objeto derivado que sigue vivo debe usar
la implementación derivada incluso cuando se le entregue el rótulo
`"ConstruirBase"` o `"DestruirBase"`. Una instancia
solo de la clase base mantiene su implementación base al recibir
`"ConstruirDerivada"`. Durante el constructor o destructor de la
base, las llamadas virtuales siguen el comportamiento de esa fase de C++.

La comprobación emplea flujos independientes y también comprueba la
llamada calificada a la base, rótulos desconocidos o vacíos, códigos cero,
números negativos y destrucción a través de un propietario de la base.
Rechaza programas que deducen la función a invocar del texto del rótulo,
que imitan las salidas o que escriben en un flujo distinto del recibido.
Se aceptan cuerpos correctos equivalentes.

Un único acierto del primer caso visible permite avanzar cuando pasa la
sonda. Los otros cuatro casos se conservan como ensayos opcionales.
El recibo adicional `virtual-lifetime/v1` se vincula al código,
la declaración curricular, la solicitud y la revisión concretas.
Los logros anteriores de stdout se conservan, pero no acreditan
automáticamente el nuevo objetivo; tampoco se modifican borradores
ni el progreso de otros cursos.

La QA de Codex no edita el contenido de Work. Work incorporará esta
declaración y comprobará su presentación cuando corresponda. Las
sondas de C++ y las pruebas aisladas de conservación no sustituyen la
validación integral nativa de la app Windows.

La regresión `test_cpp_virtual_lifetime_dispatch_core.py` coteja el
`main` auténtico con sus **cinco casos públicos** para dos soluciones
equivalentes y un contraejemplo: `registrar` selecciona la implementación
base según el texto «ConstruirBase» o «DestruirBase», en lugar de respetar
el despacho virtual de C++. Este atajo reproduce los cinco casos originales
porque allí los rótulos coinciden con las fases del ciclo de vida. La
sonda privada lo rechaza al realizar llamadas ordinarias sobre un objeto
derivado vivo con esos mismos rótulos. Los cuatro casos públicos posteriores
siguen siendo opcionales para el alumno; las ejecuciones adicionales
son únicamente de QA.

## Preparar un objeto derivado y conservar el anterior ante una excepción

Programación 3, U8L7, dispone de una prueba independiente de construcción
segura. Work puede añadir a la raíz de la lección española de estado estable
`v2` una declaración `compilerDerivedPreparationChecks` sin
modificar el bloque C++ original, sus cinco casos ni sus direcciones históricas:

```json
"compilerDerivedPreparationChecks": [
  {
    "activity": "block_0016_0002",
    "baseClass": "Medida",
    "derivedClass": "MedidaIdentificada",
    "balanceClass": "Balance",
    "resourceClass": "Recurso",
    "replacementFunction": "sustituir",
    "destinationParameter": "destino",
    "numberParameter": "numero",
    "codeParameter": "codigo",
    "modeParameter": "modo",
    "balanceParameter": "balance",
    "printMethod": "escribir",
    "resourceAlive": "recursos_vivos",
    "baseCompleted": "bases_completadas",
    "derivedCompleted": "derivadas_completadas",
    "resourceCompleted": "recursos_completados",
    "baseDestroyed": "bases_destruidas",
    "derivedDestroyed": "derivadas_destruidas",
    "resourceDestroyed": "recursos_destruidos"
  }
]
```

El único cuerpo editable es el de:

```cpp
void sustituir(std::unique_ptr<Medida>& destino,
               int numero, int codigo, int modo, Balance& balance)
```

Se conserva byte por byte el resto del archivo original: las clases
`Recurso`, `Medida` y `MedidaIdentificada`,
sus políticas de propiedad, constructores, destructores, contadores
`Balance`, `main`, entradas, salidas y contratos. El
comprobador rechaza cambios fuera de ese cuerpo, escapes de llaves
o nuevas directivas. No se impone una única implementación: preparar
un propietario local y después moverlo o intercambiarlo, o completar
la construcción del lado derecho antes de asignar, son alternativas
correctas bajo el contrato publicado.

Una sonda C++ privada compilada desde la fuente entregada comprueba
que un fallo de construcción en el cuerpo base, en el miembro derivado
o en el cuerpo derivado deja intacto **el mismo propietario de destino**
y sus valores. La excepción controlada debe propagarse. Los recursos
del intento incompleto terminan por las reglas RAII de C++, sin ejecutar
el destructor de una clase cuya construcción no llegó a completarse.
Los contadores registran las operaciones reales y no tienen que
retroceder al estado previo para que la propiedad del destino se conserve.

La sonda prueba destinos inicialmente ocupados y vacíos, aceptación
con datos cero y negativos, nueva identidad cuando los valores se repiten,
destrucción única del destino reemplazado, registros de recursos
independientes y limpieza al salir mediante otra excepción. No utiliza
punteros después de destruir su objeto ni dobles propietarios para
provocar una evidencia.

La salida visible correcta por sí sola no acredita el nuevo objetivo.
La primera entrada `outputCheck` es la única obligatoria
para avanzar; las otras cuatro permanecen opcionales. El recibo
`derived-preparation/v1` se incorpora únicamente después de
una sonda nueva, correcta y vinculada a la fuente original, la declaración
editorial, el controlador privado, la solicitud y la revisión concreta.
Los recibos antiguos se conservan sin atribuirles automáticamente la
acreditación adicional. También permanecen intactos borradores,
respuestas, estado de lectura y progreso de otros cursos.

Codex proporciona el mecanismo técnico sin modificar los JSON de Work.
La prueba independiente de C++ y la conservación de StateStore no
constituyen, por sí mismas, una aceptación de la app nativa completa
en el Windows del usuario.

La regresión `test_cpp_derived_preparation_core.py` contrasta también
los **cinco casos públicos auténticos** para tres implementaciones
válidas y un contraejemplo que construye un objeto correcto, pero lo
descarta cuando el destino recibido está vacío. Las salidas públicas
coinciden porque el `main` original siempre entrega un destino ocupado.
La sonda privada prueba ambos estados del destino y rechaza el programa
que no instala el propietario construido. Esta QA no convierte los
cuatro casos posteriores en pruebas manuales obligatorias.

## Dos interfaces independientes sobre un único objeto vivo

Programación 3, **U10L1**, dispone de la declaración opcional
`compilerDualInterfaceChecks` para una lección española
`stateLayout` v2. Work la incorpora en la raíz de
`lesson_reunir_dos_bases_independientes_en_un_objeto.json`,
sin cambiar el código inicial, los tres casos, las respuestas ni el
propietario estable del editor. Ejemplo exacto de autoría:

```json
"compilerDualInterfaceChecks": [
  {
    "activity": "block_0007_0011",
    "valueInterface": "ConValor",
    "reportInterface": "ConInforme",
    "concreteClass": "Muestra",
    "valueMethod": "valor",
    "writeMethod": "escribir",
    "changeMethod": "cambiar",
    "streamParameter": "salida",
    "changeParameter": "nuevo",
    "codeMember": "codigo_",
    "valueMember": "valor_",
    "constructorCodeParameter": "codigo",
    "constructorValueParameter": "valor",
    "reportSeparator": "="
  }
]
```

El objetivo autoriza editar **solo dos cuerpos** del código C++
publicado: `Muestra::valor() const` y
`Muestra::escribir(std::ostream&) const`. Ambas
interfaces abstractas, sus destructores virtuales, la herencia pública
múltiple, las firmas, los campos, la construcción, `cambiar`,
`mostrar` y `main` permanecen exactamente
iguales. El motor contrasta los intervalos protegidos byte a byte,
rechaza directivas y no depende de que el estudiante copie literalmente
un único cuerpo de referencia.

La sonda semántica se compila a partir del **programa editado realmente
entregado**, más un controlador interno independiente del comprobador
visible de salida. Construye objetos con códigos y valores cero,
negativos, extremos y repetidos. Consulta dos referencias, una
`ConValor` y otra `ConInforme`, y
comprueba que ambas corresponden al **mismo objeto vivo**. Después de
`cambiar`, la primera debe devolver el valor actualizado
y la segunda escribir `código=valor` en **cada uno de
dos flujos independientes**. También se prueba escribir dos veces
seguidas sin modificar el objeto: no se admiten valores constantes,
informes desactualizados ni escribir siempre en `std::cout`
para imitar el primer resultado.

La primera ejecución visible correcta sigue siendo la **única
obligatoria**. Su entrada publicada es `7 3 -2`; las
otras dos entradas, `0 0 0` y `-4 -8 5`,
continúan como pruebas opcionales. La comprobación semántica privada
no exige repetir esas entradas manualmente. La evidencia nueva usa
el recibo `dual-interface/v1`, vinculado a fuente,
declaración, solicitud, revisión y resultado de la sonda. Un logro
histórico por `stdout` no recibe ese sello
automáticamente. El estado de otros cursos, los intentos y los
borradores no se convierten ni se borran.

Para comprobar la semántica y la autoría por separado desde la raíz
de `Codex`:

```text
python -B scripts/curriculum/test_cpp_dual_interface.py --compiler cl --windows
python -B -m unittest discover -s scripts/curriculum -p test_cpp_dual_interface_authoring.py -v
```

El workflow `.github/workflows/codex-dual-interface.yml`
compila el núcleo semántico con GCC, Clang y MSVC desechables.
`native.dual_interface_state_live` comprueba además
el lector completo y ligero, los tres casos originales y la
persistencia de recibos válidos, ausentes y adulterados en StateStore
aislado. Las comprobaciones de núcleo y StateStore no sustituyen la
ejecución visual de la app ni la aceptación en el Windows personal
del estudiante. Un ejecutor Windows alojado utiliza su propio
toolchain; solo acredita el alcance y la versión que realmente ejecuta.

La regresión `test_cpp_dual_interface.py` compara además los **tres
casos públicos auténticos** para las dos implementaciones equivalentes
y un contraejemplo que ignora `salida` y escribe directamente en
`std::cout`. Los tres programas pueden reproducir las salidas del
`main` original porque éste también usa `std::cout`. La comprobación
privada debe rechazar el contraejemplo al pasarle dos flujos de salida
independientes. Esta prueba automática no obliga al alumno a ejecutar
los otros dos casos públicos: solo el primero es necesario para avanzar.

## Clonar sin perder la clase concreta ni duplicar propietarios

Para Programación 3, **U8L10**, la raíz de una lección española con
`stateLayout` v2 admite el contrato de autoría optativo
`compilerPolymorphicCloneChecks`, sobre un editor ya publicado.
Ejemplo de la lección «Copiar objetos sin perder su clase concreta»:

```json
"compilerPolymorphicCloneChecks": [
  {
    "activity": "block_0015_0002",
    "baseClass": "Muestra",
    "simpleClass": "MuestraSimple",
    "identifiedClass": "MuestraIdentificada",
    "vectorAlias": "Muestras",
    "cloneMethod": "clonar",
    "duplicateFunction": "duplicar",
    "numberMethod": "numero",
    "changeMethod": "cambiar_numero",
    "writeMethod": "escribir",
    "codeMethod": "codigo",
    "sourceParameter": "origen",
    "streamParameter": "salida"
  }
]
```

Work mantiene íntegros el `initialCode`, las cinco entradas con sus
salidas, las explicaciones, los identificadores, la clase base abstracta,
las clases concretas, los métodos y miembros facilitados, el generador,
el alias de vector, `informar` y `main`. Sólo son
editables los cuerpos que ya existen de
`MuestraSimple::clonar()`, `MuestraIdentificada::clonar()`
y `duplicar(const Muestras&)`. Los intervalos de código no
editables se comparan byte a byte; no se admiten directivas ni cambios
de declaraciones para fabricar el resultado.

La prueba adicional compila el código realmente entregado con un
controlador privado que nunca reemplaza la clase abstracta ni la
implementación del estudiante. Cada clon concreto debe ser **otro
objeto vivo**, de su misma clase y con todos los datos conservados.
Se prueban cero, negativos, extremos, números y códigos repetidos.
Modificar el original no cambia el clon y modificar el clon no cambia
el original. Al destruir los originales, las copias deben seguir
funcionando.

La función `duplicar` conserva cantidad, orden, identidad de
cada origen y tipo concreto de cada copia, también para una tercera
clase derivada conforme, no contemplada por las dos clases del ejemplo.
Se verifica que invoque la clonación virtual, no una fabricación manual
que únicamente reproduzca los textos conocidos. En un fallo
controlado de `clonar()` tras haber creado dos copias,
el error debe propagarse sin modificar la colección origen ni
sustituir el destino; las copias parciales terminan mediante RAII.
El controlador no agota memoria ni utiliza préstamos a objetos
destruidos. Un resultado de salida idéntico no acredita por sí solo
estas propiedades.

La primera entrada correcta del editor sigue siendo el **único
caso visible obligatorio**. Los otros cuatro son ensayos opcionales.
El recibo independiente `polymorphic-clone/v1` está ligado a
la fuente, la revisión, la declaración y la ejecución privada.
Un logro histórico de `stdout` no adquiere el recibo
automáticamente; se conservan los borradores, intentos, referencias,
pausas y estados de otros cursos.

Comprobaciones portátiles y de autoría desde la raíz de `Codex`:

```text
python -B scripts/curriculum/test_cpp_polymorphic_clone.py --compiler cl --windows
python -B -m unittest discover -s scripts/curriculum -p test_cpp_polymorphic_clone_authoring.py -v
```

El workflow `.github/workflows/codex-polymorphic-clone.yml`
contrasta la fuente y los escenarios de clonación con GCC, Clang
y MSVC sobre ejecutores desechables. La prueba Win32
`native.polymorphic_clone_state_live` comprueba el
cargador completo y de progreso, el recibo independiente, los
casos históricos y el rechazo de recibos alterados en un perfil QA
aislado. Una ejecución portátil aprobada no sustituye la
compilación integral del lector ni la evaluación visual y auditiva
en el Windows del estudiante.

La regresión `test_cpp_polymorphic_clone.py` conserva el `main` y
contrasta los **cinco casos públicos originales** para tres soluciones
válidas y dos contraejemplos: clonar sólo las clases concretas conocidas
en lugar de invocar `clonar()` virtualmente y devolver un prefijo parcial
si `clonar()` lanza `std::bad_alloc`. Ambos errores pueden imprimir
exactamente los resultados públicos, pero fallan al introducir otra
implementación conforme o una excepción controlada durante la
duplicación. La sonda independiente exige tipo concreto, identidad
distinta, limpieza y propagación de fallos. La QA adicional no convierte
los cuatro casos posteriores al primero en pruebas obligatorias para
el estudiante.

## Colecciones con propiedad única e informes por interfaz virtual

Programación 3, **U8L9**, admite el campo raíz
`compilerInterfaceCollectionChecks` en una lección española con
`stateLayout` v2. El contrato se declara sobre un editor ya publicado;
Work no cambia su `initialCode`, sus cinco casos, los identificadores
ni las respuestas originales. Ejemplo correspondiente a la lección actual:

```json
"compilerInterfaceCollectionChecks": [
  {
    "activity": "block_0015_0002",
    "baseClass": "Muestra",
    "simpleClass": "MuestraSimple",
    "identifiedClass": "MuestraIdentificada",
    "vectorAlias": "Muestras",
    "addFunction": "agregar",
    "reportFunction": "informar",
    "writeMethod": "escribir",
    "numberMethod": "numero",
    "destinationParameter": "destino",
    "sourceParameter": "fuente",
    "dataParameter": "datos",
    "streamParameter": "salida",
    "countLabel": "Cantidad:",
    "indexSeparator": ":",
    "numberLabel": "Numero:",
    "codeLabel": ";Codigo:"
  }
]
```

La lista de propietarios es `std::vector<std::unique_ptr<Muestra>>` y
el verificador conserva todas las declaraciones, constructores, consultas,
clases, fábrica, alias, y `main`. Únicamente admite cambiar los
cuerpos ya existentes de `agregar(Muestras&, std::unique_ptr<Muestra>&)`
e `informar(const Muestras&, std::ostream&)`. La protección compara
los segmentos no editables byte a byte y rechaza intentos de introducir
directivas, funciones ajenas o modificaciones de la interfaz. Las dos
implementaciones pueden ser semánticamente equivalentes sin reproducir
literalmente una solución modelo.

La sonda privada compila el programa auténtico con un controlador
independiente. Verifica el rechazo de una fuente vacía sin cambiar la
colección, el traslado del **mismo objeto** a su posición final, el
vaciamiento del propietario fuente, el orden y la identidad de todo el
prefijo después de múltiples inserciones y crecimiento, y la escritura
en dos flujos independientes. Incluye ceros, números negativos, extremos
y repetidos. Otra clase derivada válida, ausente de los ejemplos del
estudiante, comprueba que `informar` invoque realmente el
método virtual en lugar de reconocer únicamente los dos tipos
conocidos. Comprueba la liberación del objeto adicional al vaciar el
vector sin observar punteros liberados. Coincidir con la salida de
`main` sin estas garantías no acredita el objetivo.

Solo la primera entrada del editor publicada es obligatoria para avanzar;
los otros cuatro casos siguen como pruebas opcionales. La evidencia
adicional produce el recibo `interface-collection/v1` ligado al
programa, a la revisión, a la fuente protegida y al controlador privado.
Una acreditación histórica de salida **no obtiene** ese recibo
automáticamente. Se conservan respuestas, intentos, borradores y el
progreso de los demás cursos.

La regresión `test_cpp_interface_collection.py` compara también el
`main` original con sus **cinco casos públicos** para dos soluciones
correctas y una implementación que identifica exclusivamente
`MuestraSimple` y `MuestraIdentificada` con `dynamic_cast`. Los tres
programas imprimen los resultados publicados; el tercero falla al
informar otra implementación legítima de `Muestra`, introducida por la
sonda privada. Se verifica así que imitar las clases presentes en stdout
no acredita el despacho virtual general. Los cuatro casos posteriores al
primero siguen siendo opcionales para el alumno: esta comparación
adicional se ejecuta únicamente en QA, sin modificar sus recibos.

Comprobaciones de alcance separado:

```text
python -B scripts/curriculum/test_cpp_interface_collection.py --compiler cl --windows
python -B -m unittest discover -s scripts/curriculum -p test_cpp_interface_collection_authoring.py -v
```

El flujo `.github/workflows/codex-interface-collection.yml` comprueba
la sonda portátil en GCC, Clang y MSVC con programas de prueba
desechables. La prueba nativa
`native.interface_collection_state_live` de StateStore verifica la
importación completa y ligera, conserva los cinco casos y las identidades,
rechaza recibos falsificados y permite añadir solo una nueva acreditación
tras el primer caso correcto. Una prueba de núcleo, una compilación del
módulo o un ejecutor hospedado no sustituyen la ejecución y la
inspección visual de la app en el PC Windows del estudiante. Work
incorpora la declaración cuando corresponda, conserva el currículo
y acredita sus propias variantes educativas.

## Interfaces abstractas, fábrica de propietarios y consumo virtual

Programación 3, U8L8, puede declarar en la raíz de una lección española
`stateLayout` v2 el contrato general
`compilerAbstractInterfaceChecks`. Work mantiene los cinco casos
del editor publicado, su código inicial, sus respuestas, identidades y
direcciones de progreso.

```json
"compilerAbstractInterfaceChecks": [
  {
    "activity": "block_0017_0002",
    "baseClass": "Muestra",
    "simpleClass": "MuestraSimple",
    "identifiedClass": "MuestraIdentificada",
    "createFunction": "crear",
    "presentFunction": "presentar",
    "numberMethod": "numero",
    "codeMethod": "codigo",
    "writeMethod": "escribir",
    "streamParameter": "salida",
    "typeParameter": "tipo",
    "numberParameter": "numero",
    "codeParameter": "codigo",
    "objectParameter": "dato"
  }
]
```

Los cuatro cuerpos originales que puede editar el estudiante son la
implementación de `MuestraSimple::escribir`, la implementación de
`MuestraIdentificada::escribir`, la función
`crear(int, int, int)` y
`presentar(const Muestra&, std::ostream&)`. Las dos operaciones
`escribir` tienen la misma firma textual, de modo que la
autenticación las identifica por la clase concreta que contiene cada
implementación. No confunde un método con el otro ni permite cambiar la
declaración abstracta, los operadores virtuales puros, el destructor
virtual público, constructores, campos, getters, herencia o `main`.
Los cuatro cuerpos pueden adoptar implementaciones semánticamente
equivalentes; la protección conserva byte por byte todo el resto.

La ejecución independiente compila el programa verdaderamente entregado
junto con una sonda que comprueba las dos clases concretas y una tercera
implementación legítima de `Muestra`. Se verifican la selección
de tipo de la fábrica según el parámetro recibido, la propiedad única,
el número y el código, datos cero, negativos y extremos, informes en
flujos diferentes, y el uso de las operaciones virtuales
`numero()` y `escribir(...)` por parte de
`presentar`. La tercera clase no debe requerir un reconocimiento
por rótulos o una reconstrucción manual de los casos conocidos.
Una salida de consola copiada no acredita la estructura ni la
transferencia del mismo objeto.

La primera entrada correcta del editor es la **única comprobación
visible obligatoria**; las cuatro entradas restantes siguen disponibles
como ensayos opcionales. Para el nuevo objetivo semántico también deben
aprobarse las sondas internas en esa misma comprobación, sin requerir más
entradas del estudiante. El recibo adicional
`abstract-interface/v1` queda vinculado a la fuente original,
declaración de autoría, programa completo, controlador privado,
solicitud y revisión ejecutadas. Un logro antiguo de stdout no recibe
automáticamente el nuevo sello. Se conservan intentos, borradores,
respuestas, referencias, lecturas y progreso de otros cursos.

Codex no modifica los JSON curriculares; Work incorpora y verifica la
declaración cuando corresponda. Las sondas C++ aisladas y la QA de
conservación no acreditan por sí solas la compilación integral de la app
con la versión exacta de MSVC del proyecto ni su inspección visual.

La regresión `test_cpp_abstract_interface_core.py` también compila el
`main` original para **los cinco casos públicos** de dos soluciones
correctas y de un contraejemplo que utiliza `dynamic_cast` para reconocer
únicamente `MuestraSimple` y `MuestraIdentificada`. Los tres programas
producen la misma salida con esos casos. Sin embargo, el contraejemplo
falla al recibir una tercera implementación conforme de `Muestra`,
mientras que las soluciones que invocan las operaciones virtuales pasan
esa sonda privada. Así, `stdout` no sustituye la acreditación del
despacho por interfaz. Solo el primer caso público es obligatorio para
el estudiante; los cuatro restantes pertenecen a la QA automática.

## Baterías abiertas de una función booleana con dos enteros

Windows admite la variante `boolean-pair-v1` dentro de la lista raíz
`compilerOpenBatteryChecks`. Se aplica a un propietario histórico
`cpp_compiler` español con `stateLayout` v2, un único fuente y sin otro objetivo.
La declaración no se añade al bloque ni modifica su `initialCode`, su posición,
su dirección histórica o sus borradores.

La función proporcionada tiene la forma global
`bool nombre(int primero, int segundo)`. El motor obtiene su definición del
inicial autenticado y exige conservar los tokens de la firma y el cuerpo.
Admite cambios de espacios y comentarios. El estudiante puede organizar el resto
del programa con nombres propios, condicionales, funciones auxiliares o
colecciones; no se exige copiar la referencia ni llamar `comprobar` a un helper.
Esta variante conserva una definición única, sin sobrecargas o redefiniciones
por macros; admite directivas `#include` en el fuente.

Cada caso declara dos enteros de 32 bits y una expectativa booleana independiente.
Los pares deben ser distintos y su cantidad debe estar entre uno y treinta y dos.
El autor obtiene las expectativas del acuerdo enseñado: el motor no presupone
que todas las funciones deban implementar una comparación concreta.

Ejemplo de una batería que debe encontrar un defecto en su función publicada:

```json
"compilerOpenBatteryChecks": [
  {
    "activity": "block_0004_0004",
    "kind": "boolean-pair-v1",
    "function": "debe_reponer",
    "publishedResult": "failure",
    "cases": [
      {"first": 4, "second": 5, "expected": true},
      {"first": 5, "second": 5, "expected": true},
      {"first": 6, "second": 5, "expected": false}
    ]
  }
]
```

`publishedResult: "failure"` exige una terminación normal no cero del programa
que conserva la función publicada. `"success"` exige cero. El retorno no cero
no se restringe a uno: la evidencia privada de terminación normal permite
conservar el valor completo devuelto por Windows. Esto no amplía el contrato
ordinario de `outputCheck.exitCode`, que mantiene su intervalo de 0 a 255.

Una sola pulsación compila y ejecuta el programa del estudiante y comprueba:

1. Un control que devuelve las expectativas declaradas debe terminar con cero.
2. Una ejecución independiente por cada par invierte únicamente ese resultado.
   La batería debe detectar la discrepancia, comunicar un resultado que permita
   distinguir el caso y terminar normalmente con un código no cero.
3. Una ejecución instrumentada de la función publicada debe conservar el
   retorno y los canales visibles de la ejecución original y cumplir
   `publishedResult`.

Todas esas ejecuciones deben llamar exactamente una vez a cada par declarado;
un fallo no puede omitir llamadas posteriores. Las expectativas no se obtienen
de otra llamada a la función evaluada. Las salidas de los casos alterados deben
diferir del control y entre sí: un mensaje fijo que no identifica qué comparación
cambió tampoco basta. El texto literal, la numeración y el canal de los
diagnósticos quedan a elección del estudiante.

Los registros privados se separan de la consola. Sus datos numéricos no dependen
de los formatos ni de la configuración regional de los flujos C++. Un testigo
privado acredita el final normal; un aborto, una excepción no atendida, una
cancelación o un límite de ejecución no cuentan como discrepancia detectada.
Las fases conservan la misma ruta de fuente, ejecutable, directorio y entorno.
La ejecución nativa mantiene el alcance y los límites de aislamiento del runner.

La acreditación se vincula al fuente exacto, la configuración de ejecución y el
objetivo `open-bool-pair-battery-independent-mutations-v1`. Una captura anterior,
otra revisión o un antiguo acierto por salida no conceden el nuevo logro.
Los casos anteriores son comprobaciones internas de la batería que construye
el estudiante; no constituyen varios intentos manuales obligatorios del editor.

Las entradas CMake anteriores de `compilerOpenBatteryChecks`, sin `kind`,
conservan su contrato. La lista admite añadir propietarios al final, manteniendo
literalmente el prefijo publicado. No permite retirar, reordenar o reescribir
una declaración anterior para reutilizar su evidencia.

## Reducciones de una reproducción con estado

La raíz española con `stateLayout` v2 puede añadir `compilerReductionChecks`
a un editor C++ de archivo único que todavía no tenga un objetivo. La
declaración utiliza la identidad existente; no cambia su `initialCode`,
mapas, familia de traducciones, respuestas ni borradores. Este ejemplo
corresponde al propietario histórico de la práctica de reducción de U1;
describe la capacidad de autoría y no modifica el recurso curricular:

```json
{
  "compilerReductionChecks": [
    {
      "activity": "block_0002_0003",
      "kind": "stateful-vector-int-v1",
      "function": "contar_altos",
      "state": "acumulado",
      "initialState": 0,
      "calls": [
        {
          "values": [
            2
          ],
          "argument": 2,
          "result": 1,
          "stateAfter": 1
        },
        {
          "values": [],
          "argument": 2,
          "result": 1,
          "stateAfter": 1
        }
      ],
      "observedCall": 1,
      "requiredResult": 0,
      "exitCode": 0,
      "dependencies": [
        {
          "header": "vector",
          "symbols": [
            "std::vector"
          ]
        },
        {
          "header": "iostream",
          "symbols": [
            "std::cout",
            "std::cerr",
            "std::clog"
          ]
        },
        {
          "header": "string",
          "symbols": [
            "std::string",
            "std::to_string"
          ]
        },
        {
          "header": "cstdio",
          "symbols": [
            "std::printf",
            "std::puts"
          ]
        }
      ]
    }
  ]
}
```

`function` identifica una definición global
`int nombre(std::vector<int> datos, int argumento)`. El motor deriva del
inicial su cuerpo protegido y lo compara por tokens, permitiendo espacios
y comentarios distintos. `state` identifica su entero global.
`initialState` declara el estado anterior a la primera llamada.
`calls` fija una secuencia dentro del mismo proceso: cada entrada incluye
la colección, el segundo argumento, el retorno observado y el estado
posterior. `observedCall` cuenta desde cero y selecciona una llamada que
debe tener preparación anterior. `requiredResult` es el valor exigido
por el requisito incumplido; debe diferir del resultado observado.
Esta versión exige terminación normal con `exitCode: 0`.

El contrato admite de 2 a 32 llamadas, hasta 64 enteros por colección y
valores del dominio de `int32_t`. `dependencies` declara entre 1 y 32
relaciones de cabecera y símbolos. Cada cabecera sólo se exige si alguno
de sus símbolos permanece en la candidata, incluso en una función que
ya no se llama; los comentarios y cadenas no cuentan como usos.
Las declaraciones `using` también conservan esa dependencia. Una entrada
puede prever símbolos que el inicial todavía no usa, como
`std::printf`. Estas relaciones comprueban las dependencias declaradas;
no son un catálogo exhaustivo de toda biblioteca que pueda introducir
el estudiante.

### Evidencia de la reducción

La gramática reconoce esa firma global y un entero global sin sombras.
Admite inicialización decimal con signo opcional mediante llaves, igualdad
o paréntesis; la inicialización vacía o implícita equivale a cero. Las
directivas admitidas son inclusiones. Los alias o macros que sustituyan
la definición y los nombres de introspección de función no forman parte
de este contrato instrumental.

El análisis de la fuente exige conservar la función investigada y un
estado global inicial compatible, declarar las dependencias que aún se
usan y retirar estructura accesoria. El perfil distingue almacenamiento,
funciones auxiliares, llamadas, presentación y operaciones. Deben disminuir
tanto su medida estructural como la que normaliza la presentación entre
flujos y formatos de impresión. Juntar dos impresiones manteniendo todas
sus emisiones no basta. No concede
una reducción por cambiar nombres, literales, espacios, comentarios,
calificadores o sólo el retorno explícito de cero. No fija un número
de líneas ni exige retirar una función auxiliar concreta.

La ejecución privada conserva el cuerpo investigado y registra los
argumentos, el estado anterior y posterior y el retorno de cada llamada.
Se comprueba la secuencia completa y su preparación causal. El primer
retorno puede descartarse sin guardarlo ni mostrarlo.

Otras dos ejecuciones cambian únicamente el retorno entregado de la
llamada observada. Comparan los valores enteros visibles para acreditar
que el programa presenta ese resultado calculado. No se fija el texto
de los mensajes ni se exige un canal concreto; se admiten las dos
respuestas o únicamente la segunda. Una impresión constante no adquiere
crédito por acompañarla de llamadas que no participan en lo mostrado.
La comprobación de observación de esta versión utiliza valores enteros
visibles; no evalúa explicaciones libres escritas sólo con palabras ni
representaciones transformadas que oculten el valor observado.

La ejecución publicada debe terminar normalmente con cero. Las
contrapruebas deben terminar normalmente, pero pueden devolver otro
código si el programa decide su retorno a partir del resultado observado.
Un aborto, una interrupción o una ejecución incompleta no acredita la
reproducción.

### Ejecución, conservación y validación

Las fases comparten las rutas físicas de fuente y ejecutable, argumentos,
entrada, directorio, entorno y datos iniciales de herramientas. Los
archivos y salidas de la ejecución pública se conservan y restauran tras
las contrapruebas. La evidencia privada no reemplaza la consola visible.

La captura y el recibo quedan ligados al programa, la revisión, el
contrato, las opciones y la ejecución concreta. El cargador completo y
el lector de progreso reconstruyen el mismo objetivo. Una visita, una
compilación anterior, una salida coincidente o un recibo de otro objetivo
no autorizan la referencia ni el avance. Los logros anteriores se
conservan, pero no sustituyen la acreditación de este nuevo requisito.
Toda la secuencia constituye una sola comprobación del editor.

`test_reduction_contract.py` comprueba la autoría raíz y la conservación
del recurso original sin editarlo. `native.reduction` verifica el
núcleo portable y `native.reduction_runtime` ejecuta el propietario
auténtico con declaraciones derivadas sólo en el perfil QA. Las mismas
pruebas nativas están registradas en ReaderBuild y en la construcción
completa de Windows.

## Baterías CMake con resultados enteros

`compilerOpenBatteryChecks` admite la variante `cmake-int-unary-v1` para
una biblioteca con una función global `int nombre(int dato)`. La declaración
añade un objetivo al propietario español con `stateLayout` v2 sin cambiar
el editor, sus archivos iniciales, mapas, familia, respuestas o borradores.
El estudiante sólo modifica el archivo indicado por `testPath`; la biblioteca,
la interfaz, el cliente y la descripción CMake conservan sus bytes originales.
El bloque no declara `sourceFiles`, ni siquiera como lista vacía, ni otro
objetivo de evaluación. Las declaraciones publicadas se conservan como prefijo
de la lista; añadir otro propietario no permite reescribir las anteriores.

Estas declaraciones corresponden a los dos propietarios de U3L1. Son ejemplos
de autoría del motor; el documento no modifica los recursos curriculares:

```json
{
  "compilerOpenBatteryChecks": [
    {
      "activity": "block_0004_0003",
      "kind": "cmake-int-unary-v1",
      "libraryPath": "src/cajas.cpp",
      "function": "cajas_para",
      "testPath": "pruebas/cajas_casos.cpp",
      "target": "comprobar_cajas",
      "publishedResult": "failure",
      "failureExitCode": "nonzero",
      "cases": [
        {
          "input": 0,
          "expected": 0
        },
        {
          "input": 4,
          "expected": 1
        },
        {
          "input": 8,
          "expected": 2
        },
        {
          "input": 20,
          "expected": 5
        },
        {
          "input": 1,
          "expected": 1
        },
        {
          "input": 5,
          "expected": 2
        },
        {
          "input": 17,
          "expected": 5
        }
      ],
      "prerequisite": {
        "target": "consultar_cajas",
        "calls": [
          {
            "input": 4,
            "result": 1
          },
          {
            "input": 8,
            "result": 2
          }
        ],
        "standardInput": "",
        "standardOutput": "1:2",
        "standardError": "",
        "exitCode": 0
      }
    },
    {
      "activity": "block_0010_0003",
      "kind": "cmake-int-unary-v1",
      "libraryPath": "motor/cantidad.cpp",
      "function": "lotes_para",
      "testPath": "pruebas/lotes_casos.cpp",
      "target": "verificar_lotes",
      "publishedResult": "success",
      "failureExitCode": 1,
      "cases": [
        {
          "input": 0,
          "expected": 0
        },
        {
          "input": 6,
          "expected": 1
        },
        {
          "input": 7,
          "expected": 2
        },
        {
          "input": 18,
          "expected": 3
        }
      ]
    }
  ]
}
```

### Casos independientes y resultados publicados

`libraryPath` y `testPath` identifican archivos distintos del proyecto original.
`function` selecciona la definición protegida en la biblioteca y `target` el
ejecutable que debe comprobarla. `cases` declara entre 1 y 32 entradas enteras
distintas con sus esperados independientes, dentro del dominio de `int32_t`.
`publishedResult` indica si la biblioteca original debe superar la batería
(`success`, código 0) o provocar discrepancias (`failure`).
`failureExitCode` declara el retorno normal exigido cuando falla una
comparación: un entero de 1 a 4294967295 exige ese valor; `"nonzero"` admite
cualquier retorno normal distinto de cero. La consigna de `envios` permite
no-cero y la de `lotes` exige exactamente 1. La misma política rige sus
contrapruebas, sin confundir un aborto con un retorno normal.
No se exige un nombre de helper ni un orden concreto de las comparaciones.

Las rutas son relativas, de 1 a 256 caracteres ASCII, terminan en `.cpp` y
no contienen `..`, barras inversas ni un punto o barra al inicio. Los nombres
de función y objetivos son identificadores ASCII de hasta 128 caracteres.
La definición protegida es global y única, sin sobrecargas, calificadores ni
espacios de nombres; admite prototipos idénticos. La instrumentación admite
directivas `include`, pero no macros ni nombres privados de la evaluación.
El comprobador tiene un único `main` global. La función no utiliza
`__func__`, `__FUNCTION__` ni `__FUNCSIG__`; los fuentes instrumentados no
contienen NUL ni continuación de línea y están limitados a 4 MiB.

La evaluación ejecuta un control que entrega los esperados declarados, una
alteración independiente de cada resultado y la biblioteca publicada
instrumentada. Todas las fases deben observar una llamada por dato declarado
y terminar normalmente. El control debe terminar con 0 y cada alteración
con el retorno de fallo declarado; la publicada conserva el cuerpo original y el resultado de la
declaración. La salida de esa fase publicada debe corresponder a la ejecución
que vio el estudiante.
Un testigo de terminación se instala en el consumidor antes de sus objetos
globales y referencia la biblioteca sin llamar a la función investigada.
Así, un `main` vacío termina normalmente y se rechaza por no comprobar casos,
incluso cuando una biblioteca estática no tendría otra referencia de enlace.

Cada comparación necesita un diagnóstico que permita reconocer el dato, el
esperado, el obtenido y si coinciden. Esta versión admite un registro por
línea o registros de varias líneas separados por una línea vacía. Permite
cambiar nombres, mensajes, orden de campos, orden de casos y canal de salida,
y presentar un informe después de haber calculado todos los resultados.
Los tres valores se expresan como enteros decimales visibles. El veredicto
puede ser textual o numérico; no se exige la palabra `OK` ni `FALLO`.

El análisis asigna registros disjuntos a los casos sin elegir una lectura
favorable cuando hay ambigüedad. La alteración de un caso debe cambiar su
resultado mostrado y su veredicto, conservando las observaciones de los otros
casos. Tras retirar la tripleta observada, se compara el resto del registro
normalizando el orden de sus elementos, los espacios y las mayúsculas ASCII.
Reordenar los mismos campos con una etiqueta fija no cuenta como cambiar el
veredicto. Esta comprobación observa cambios causales entre ejecuciones; no
interpreta el significado de mensajes arbitrarios en lenguaje natural.
Un resumen global, una tabla fija acompañada de un contador variable y
registros con sólo las tres cantidades no acreditan la batería.

Cada canal admite hasta 4 MiB sin NUL; cada registro, hasta 64 KiB y 64 enteros.
Se admiten hasta 1024 registros entre los dos canales. El control determina
si se usan líneas o grupos separados por líneas vacías y las fases siguientes
conservan ese modo de separación.

### Observación anterior del cliente

`prerequisite` es opcional. Cuando existe, identifica otro objetivo del mismo
proyecto que debe ejecutarse antes del comprobador. `calls` declara entre 1 y
32 llamadas en su orden real, con dato y retorno; `standardInput`,
`standardOutput` y `standardError` conservan los canales exactos. El cliente
debe terminar normalmente con `exitCode: 0`. Su entrada coincide con
`initialStandardInput` del editor, o con la cadena vacía cuando se omite.

El target inicial puede ser el cliente o el comprobador declarado. Sin
prerrequisito, debe coincidir con el comprobador. Se conservan los controles
«Configurar y generar», «Construir objetivo» y «Ejecutar objetivo construido»
que nombran estas lecciones; construir o configurar no concede crédito.

La observación del cliente tiene un recibo separado. Queda ligado al objetivo,
al componente compartido, a los archivos protegidos y a la configuración.
Editar únicamente el contenido de `testPath` no lo invalida. La reapertura
conserva ese recibo, mientras que otro componente, cliente, CMake o contrato
no puede reutilizarlo. Reiniciar expresamente el editor retira también ese
prerrequisito de su intento.

Al iniciar el comprobador se captura el recibo previo disponible. Una
ejecución del comprobador que ocurrió antes del cliente no adquiere crédito
retroactivamente por guardar después una observación del cliente. La
observación previa tampoco completa por sí sola la batería ni desbloquea su
referencia.

### Conservación y pruebas

La captura completa del comprobador queda ligada a todos los archivos
presentados, incluido el archivo de pruebas, el objetivo, la configuración
y la ejecución concreta. Las fases privadas conservan rutas, argumentos,
entrada y entorno; al terminar restauran los archivos públicos y sus marcas
de tiempo. Los recibos anteriores y los logros válidos no se sustituyen por
una visita, una compilación anterior ni un resultado de otro objetivo.

`test_integer_battery_contract.py` comprueba la autoría y la conservación
de los propietarios reales. `native.integer_battery` verifica el núcleo
portable; `native.integer_battery_runtime` ejecuta los proyectos originales
con declaraciones incorporadas únicamente en el perfil QA y comprueba la
secuencia del cliente, los recibos y la reapertura. Ambos tests nativos
están registrados en ReaderBuild y en la construcción completa de Windows.

## Ampliar una batería CMake de retorno y destino

La variante original de `compilerOpenBatteryChecks`, sin campo `kind`, acredita
una comparación nueva de una función `bool(int, int, int, int&)`. Su acuerdo es
el ajuste de menor magnitud a un intervalo: los tres datos pertenecen a
`[-20,20]`, los extremos están ordenados y un rechazo conserva el destino.
La declaración identifica la biblioteca, el comprobador y las llamadas
históricas; no sustituye los archivos ni convierte una impresión en evidencia.

En el propietario `block_0006_0011` de U3L10, el contrato tiene esta forma:

```json
{
  "compilerOpenBatteryChecks": [
    {
      "activity": "block_0006_0011",
      "library": "src/ajuste.cpp",
      "test": "pruebas/casos.cpp",
      "function": "intentar_ajuste",
      "comparison": "comprobar",
      "target": "comprobar_ajustes",
      "originalCases": [
        {"value": 2, "lower": 3, "upper": 7, "initial": 77},
        {"value": 5, "lower": 3, "upper": 7, "initial": -8},
        {"value": 9, "lower": 3, "upper": 7, "initial": 19},
        {"value": 4, "lower": 4, "upper": 4, "initial": -4},
        {"value": -20, "lower": 20, "upper": 20, "initial": 8},
        {"value": 20, "lower": -20, "upper": -20, "initial": 17},
        {"value": 0, "lower": 7, "upper": 3, "initial": 77},
        {"value": 21, "lower": 3, "upper": 7, "initial": -8},
        {"value": -21, "lower": 3, "upper": 7, "initial": 19},
        {"value": 0, "lower": -21, "upper": 7, "initial": -4},
        {"value": 0, "lower": 3, "upper": 21, "initial": 8}
      ]
    }
  ]
}
```

La declaración debe corresponder a las comparaciones del archivo original.
El cargador obtiene del archivo fuente autenticado el número de cada comparación
y lo asocia a su tupla `(value, lower, upper, initial)`. Los números históricos
deben ser enteros representables y distintos. No se añade un campo JSON de
numeración: el orden de `originalCases` puede elegirse al declarar el objetivo
y no determina los números. Una declaración ya publicada permanece inmutable,
al igual que los archivos originales y sus identidades.

En el programa presentado se conservan la biblioteca, la cabecera, el cliente
y CMake; sólo puede cambiar el archivo de pruebas. La ejecución debe contener
las once llamadas originales, cada una con su número histórico, y exactamente
una terna nueva con su destino inicial. La comparación añadida necesita un
número propio, distinto de todos los anteriores. Puede ser cualquier `int`
representable que cumpla esa condición: no se exige 12, una secuencia
consecutiva ni un literal concreto.

El comparador declarado necesita una única definición global con la firma
`bool(int, int, int, int, int, bool, int)`. Sus parámetros son, en ese orden:
número de caso, valor, inferior, superior, destino inicial, aceptación esperada
y ajuste esperado. Los nombres de los parámetros son libres. Se admiten
prototipos previos, comentarios y saltos de línea, `const` por valor,
`static`, `inline` y `noexcept`. El cargador comprueba que el comparador
histórico puede instrumentarse antes de activar el objetivo; esta comprobación
no constituye un analizador de todas las formas posibles de C++.

El comprobador puede organizar las llamadas en ciclos, tablas o funciones
auxiliares y calcular sus números y argumentos. La comprobación se basa en
los valores ejecutados, no en buscar llamadas literales en el fuente del
alumno. Cada ejecución del comparador debe contener exactamente una llamada
al componente, con el mismo número al entrar y salir. La instrumentación
privada asocia esos límites a la tupla realmente recibida por el componente;
un rótulo impreso no reemplaza esa observación. Se rechazan los números repetidos,
intercambiados entre llamadas históricas o cambiados entre variantes, así como
las llamadas fuera de una comparación completa.

Cada llamada, incluida la añadida, se comprueba mediante tres mutaciones
independientes: invertir la aceptación, cambiar el destino entre cero y uno,
y sustituir el destino por otro entero distinto de cero. Esta última usa
`ajuste == 1 ? 2 : 1`: siempre cambia el valor y permite detectar una
comparación que reduce ambos ajustes a `bool` y confunde magnitudes distintas.
El comprobador debe detectar cada defecto, comunicarlo y continuar con las
demás llamadas. Conserva en todas las variantes las asociaciones entre números
y tuplas. Con doce comparaciones, la matriz contiene 37 ejecuciones: una
original y tres por comparación. Estas variantes comprueban los defectos
declarados; no demuestran equivalencia semántica para todas las entradas
posibles. El inicial sin ampliar sigue sin acreditar el objetivo aunque
termine con `Fallos=0`, y cambiar sólo una etiqueta tampoco crea una prueba nueva.

Esta acreditación usa
`open-cmake-battery-independent-mutations-v4`. Los recibos v1, v2 y v3 conservan
su identidad y alcance anteriores; no acreditan automáticamente la nueva
mutación del destino. V3 mantiene su alcance de asociación entre números y
llamadas, sin atribuirle esa comprobación adicional. Los borradores y los archivos
del estudiante no se renumeran ni se reescriben al incorporar el objetivo.

### Entrada y acciones de ejecución

El caso obligatorio derivado exige entrada vacía y el objetivo
`comprobar_ajustes`. El `initialStandardInput` histórico conserva el lote del
cliente; no se borra ni se cambia para ajustar la comprobación. El estudiante
vacía «Entrada» como indica la consigna. Esa entrada antigua no acredita la
batería ni provoca sus ejecuciones privadas.

Se admiten tanto «Compilar, ejecutar y comprobar» como la secuencia
«Configurar y generar», «Construir objetivo» y «Ejecutar objetivo construido».
Configurar o construir por separado no concede el logro. Al ejecutar el
objetivo construido se conservan las comprobaciones existentes de la revisión
y del artefacto; cada variante privada se vuelve a compilar para comprobar la
mutación correspondiente, aunque la acción pública haya sido sólo ejecutar.

El recibo se vincula a la revisión pública y al caso de entrada vacía. Los
recibos de un objetivo anterior permanecen en sus claves históricas y no se
convierten en aciertos del objetivo corregido. Los borradores, la entrada
inicial, los propietarios y sus mapas conservan su identidad. La prueba nativa
de este recorrido se registra como `native.open_battery_runtime`; la prueba
de menor alcance del ejecutor sigue en `native.open_battery_runner`.

## Construir el componente, cliente y batería de un proyecto CMake

La variante `cmake-adjustment-project-v1` de `compilerOpenBatteryChecks`
acredita una construcción completa de `bool(int, int, int, int&)`. Es un
contrato distinto de la ampliación anterior: permite escribir la biblioteca,
el cliente, el comprobador y `CMakeLists.txt`. No exige conservar once pruebas
ni utilizar un nombre concreto para la función auxiliar que compara resultados.

Se añade en la raíz de una lección española con `stateLayout` v2 y un editor
C++/CMake todavía sin evaluación. El bloque publicado permanece intacto,
incluidos sus cuerpos pendientes, su descripción CMake vacía, la entrada
inicial, la selección del cliente, los mapas y las relaciones de actividades.
La cabecera proporcionada conserva sus tokens y su declaración
`bool nombre(int, int, int, int&)`; se admite formato equivalente sin convertir
la interfaz en una definición inline. Las rutas siguen siendo las del proyecto
original. Otros archivos originales ajenos a estas cuatro piezas se conservan.

La declaración del constructor de U3L10 tiene once campos:

```json
{
  "compilerOpenBatteryChecks": [
    {
      "activity": "block_0002_0003",
      "kind": "cmake-adjustment-project-v1",
      "libraryPath": "src/ajuste.cpp",
      "headerPath": "publico/control/ajuste.h",
      "header": "control/ajuste.h",
      "function": "intentar_ajuste",
      "libraryTarget": "ajustes",
      "clientPath": "app/consulta.cpp",
      "clientTarget": "consultar_ajustes",
      "testPath": "pruebas/casos.cpp",
      "target": "comprobar_ajustes"
    }
  ]
}
```

El lote del cliente se obtiene de `initialStandardInput`; no hay un segundo
campo editable `clientInput` en la declaración. La lista es acumulativa:
añadir el constructor conserva literalmente las declaraciones anteriores,
incluida la ampliación de `block_0006_0011`. Cada propietario sólo puede tener
un objetivo de esta lista.

### Estructura compartida y avisos

Este contrato requiere también una declaración `compilerCmakeGraphChecks`
para el mismo propietario. La biblioteca y ambos ejecutables se comprueban
en el grafo generado de la revisión pública presentada:

```json
{
  "compilerCmakeGraphChecks": [
    {
      "activity": "block_0002_0003",
      "minimumCmakeVersion": "3.20",
      "minimumCmakeVersionAtLeast": true,
      "targets": [
        {
          "name": "ajustes",
          "type": "STATIC_LIBRARY",
          "sources": ["src/ajuste.cpp"],
          "exactSources": true,
          "publicIncludeRoots": ["publico"],
          "explicitCxx20": true,
          "explicitCxxExtensionsOff": true,
          "requireExplicitMsvcWarnings": true
        },
        {
          "name": "consultar_ajustes",
          "type": "EXECUTABLE",
          "sources": ["app/consulta.cpp"],
          "exactSources": true,
          "linksTo": ["ajustes"],
          "explicitCxx20": true,
          "explicitCxxExtensionsOff": true,
          "requireExplicitMsvcWarnings": true
        },
        {
          "name": "comprobar_ajustes",
          "type": "EXECUTABLE",
          "sources": ["pruebas/casos.cpp"],
          "exactSources": true,
          "linksTo": ["ajustes"],
          "explicitCxx20": true,
          "explicitCxxExtensionsOff": true,
          "requireExplicitMsvcWarnings": true
        }
      ]
    }
  ]
}
```

El mínimo declarado debe ser 3.20 o posterior; C++20 o posterior y extensiones
desactivadas se exigen en los tres objetivos. La raíz pública se deriva de
`headerPath` y `header`. Compilar la implementación directamente en cada
ejecutable, omitir el enlace o sustituir la biblioteca compartida por una copia
no acredita la construcción.

`requireExplicitMsvcWarnings` exige que el proyecto declare y mantenga activo
un nivel MSVC de avisos: `/W1`, `/W2`, `/W3`, `/W4` o `/Wall`. No fija un nivel
literal; las pruebas de esta construcción utilizan `/W4`. `/W0` y `/w`
desactivan los avisos y no satisfacen la exigencia. Véase la
[documentación de niveles de avisos de MSVC](https://learn.microsoft.com/en-us/cpp/build/reference/compiler-option-warning-level?view=msvc-170).
El grafo comprueba las opciones declaradas y las de la construcción real;
las opciones del perfil por sí solas no sustituyen la decisión del proyecto.

### Comprobación única de la misma revisión

El motor genera un único caso obligatorio para `comprobar_ajustes`, con
entrada vacía. La entrada histórica de seis ternas y la selección inicial de
`consultar_ajustes` permanecen disponibles; no se reescriben para obtener ese
caso. Ejecutar el cliente, configurar o construir por separado no concede
el logro del constructor.

La ejecución del comprobador captura el proyecto completo y verifica
automáticamente tres aspectos de esa misma revisión:

- El componente calcula el ajuste de menor magnitud cuando los tres datos están
  en `[-20,20]` y los extremos están ordenados. Un rechazo devuelve `false` y
  conserva el destino. El componente no imprime ni termina el proceso.
- El cliente utiliza esa biblioteca, procesa hasta ocho ternas completas de
  enteros representables y continúa tras un rechazo. Informa los resultados
  aceptados en stdout y los rechazos contextualizados en stderr, cuenta y suma
  sólo resultados nuevos, y termina normalmente con 1 si hubo rechazos o con
  0 en otro caso. Se verifican el lote original y variaciones independientes;
  los mensajes pueden variar conservando los hechos y la orientación exigidos.
  Cada rechazo reutiliza la comprobación de afirmaciones de U3L6: una corrección
  ficticia o una causa de existencias no observada invalida el diagnóstico,
  mientras una negación explícita de esa afirmación conserva su significado.
- El comprobador utiliza la misma biblioteca, ejecuta su propia batería con
  esperados independientes y observa tanto la aceptación como el destino.
  La batería cubre las categorías del contrato y continúa después de una
  comparación fallida. Las mutaciones independientes deben cambiar sus
  observaciones y su veredicto; imprimir un resumen fijo, omitir comparaciones,
  obtener el esperado de la propia función o depender únicamente de `assert`
  no basta.

Las ejecuciones internas son parte de la primera comprobación, sin nuevos
casos manuales obligatorios. Las trazas privadas no se presentan ni se narran.
La evidencia se liga a todos los archivos, la receta, el objetivo ejecutado,
el contrato y su grafo. No se unen una biblioteca de una revisión y recibos
del cliente o de la batería de otra.

La nueva rúbrica tiene su propia identidad de objetivo. Los recibos anteriores,
los borradores y sus direcciones históricas permanecen guardados; no se
convierten en logros del contrato nuevo. La carga de progreso reconstruye
el mismo objetivo que la carga completa y verifica sus recibos al reabrir.

## Contrato

- `cases` es un array no vacío. Cada caso declara `standardInput` de tipo texto (puede ser vacío) y `outputs`, una lista no vacía de salidas completas y distintas. Una salida vacía también es válida.
- La comparación de **stdout siempre es literal**: espacios, tabuladores, CR y LF se conservan. Para permitir exclusivamente un LF final, declarar ambas alternativas, por ejemplo `"Informe\nZona Este\nFin"` y `"Informe\nZona Este\nFin\n"`. No se recortan los extremos.
- `inputComparison` es `exact` por defecto. `tokens` se usa sólo cuando la consigna permite separar los mismos datos con espacios en blanco ASCII (` `, TAB, LF, CR, VT, FF). Por ejemplo, un caso `"Leo 2"` también identifica `"Leo\n2\n"`. No normaliza stdout ni admite espacios Unicode como si fueran separadores de C++.
- Los casos deben identificar inequívocamente la ejecución bajo el modo seleccionado. En un proyecto CMake, cada caso admite `target` opcional: el nombre exacto y sensible a mayúsculas del ejecutable que debe producir esa salida. Dos ejecutables pueden compartir entrada y tener salidas distintas. Se usa el objetivo realmente capturado al pulsar ejecutar, sin deducirlo del código ni añadir un selector curricular de respuestas.
- Sin `target`, se conserva el contrato anterior: el caso se identifica por entrada y es aplicable a cualquier objetivo del bloque. No se permite mezclar un caso global y otro con `target` para la misma entrada, ni repetir un mismo par objetivo/entrada. No existe precedencia implícita entre ellos. `target` sólo es válido en bloques con `cmake` y acepta el mismo formato que `cmake.target`: `[A-Za-z_][A-Za-z0-9_.+-]{0,127}`. No obliga a que sea el objetivo inicial: el alumno puede cambiar de ejecutable y reconstruirlo.
- `explanation` es texto opcional no vacío de acierto. `reviewFeedback` es un array opcional, también puede ser vacío: cada regla declara `outputs` y `text` no vacío. Sólo una coincidencia literal con esas salidas incorrectas permite mostrar su orientación. Las condiciones no pueden solaparse entre reglas ni con las salidas correctas.
- En Windows, cada caso admite además `standardError: "nonBlank"` para exigir información adicional en stderr durante una ejecución correcta. Se detalla más adelante; omitirlo mantiene exactamente el contrato de stdout anterior.
- En Windows, `exitCode` es un entero de `0` a `255`, con valor predeterminado
  `0`. Un retorno intencional se comprueba junto con stdout y stderr de la misma
  ejecución terminada. No convierte cualquier fallo del proceso en un acierto.
- Sin explicación se muestra sólo el veredicto. Un error no previsto muestra sólo «Incorrecto». No se infiere una causa, no se expone la salida esperada y los campos internos no entran en el guion normal de la lección. Se rechazan tipos y propiedades desconocidos como errores de configuración, no como respuestas incorrectas.

## Ejecución y conservación

«Compilar, ejecutar y comprobar» utiliza únicamente la ejecución nueva del
código/proyecto, compilador, configuración y entrada capturados por esa
pulsación. Nunca comprueba una salida que ya estuviera en la consola. El proceso
debe terminar y disponer de capturas completas. Para acertar debe satisfacer
también el código de salida declarado. Fallos de compilación, enlace,
configuración, cancelaciones y límites
técnicos no son respuestas evaluables. Una ejecución no satisfactoria sólo es
evaluable como retorno intencional cuando el caso declara un retorno distinto de
cero; otro código terminado de `0` a `255` produce «Incorrecto» en ese objetivo.
Un fallo no cero bajo un caso que exige cero sigue sin ser evaluable. Los
avisos y los canales de salida se conservan sin reescribirlos.

En Windows, las herramientas de compilación y enlace utilizan una consola privada
oculta configurada en UTF-8, tanto en fuentes simples como en proyectos directos,
CMake y rechazos intencionales. Así se conservan los acentos del diagnóstico y
de sus rutas sin confundir la salida OEM de MSVC con texto ANSI o UTF-8. La
captura guarda los bytes emitidos por cada herramienta. La ejecución del programa
mantiene separados y exactos sus canales binarios de entrada, stdout y stderr,
incluidos NUL y bytes que no formen texto UTF-8. La consola privada pertenece al
intento; no cambia la configuración de Windows ni la del terminal del usuario.

Los laboratorios CMake conservan sus controles explícitos de fases. Con `outputCheck`, la acción combinada configura si hace falta, construye el objetivo vigente mediante CMake y ejecuta un programa nuevo antes de comparar; se conservan los diagnósticos de cada fase. Un resultado restaurado por el observador del proyecto puede volver a mostrar su consola, pero nunca genera una comprobación. Sin contrato se conserva el funcionamiento de sus controles de configuración, construcción y ejecución.

Editar una fuente, cabecera, selección de fuentes, entrada, configuración CMake
o compilador invalida la ejecución corriente. Deshacer o rehacer no recupera una
ejecución antigua: hay que ejecutar de nuevo. Cambiar sólo de pestaña conserva la
identidad del programa. Un resultado tardío de otra revisión no habilita una
comprobación. Windows conserva por separado el logro acreditado, los parciales
del mismo programa y la última revisión pedagógica; las reglas se detallan en
«Acreditación y avance». La consola transitoria no es un recibo de acreditación.

Estas comprobaciones **no consumen corazones** ni responden la predicción
vecina. En Windows acreditan el objetivo propio del compilador y controlan el
avance de las actividades curriculares. Una coincidencia de stdout admite
diferentes implementaciones y no demuestra por sí sola que se hayan utilizado
las variables o la estrategia solicitadas.

Con el lector activado, el veredicto visible se narra aunque no tenga explicación.
Un objetivo obligatorio pendiente detiene el recorrido después de su consigna;
el contenido posterior espera. Un nuevo intento lleva la revisión al veredicto
actual, incluso si ya existía un logro y el lector había avanzado. El error se
lee y espera otro intento; el acierto lee su explicación antes de continuar.
Una pausa manual permanece pausada. Las explicaciones anteriores pendientes se
atienden en su orden; buscar o adelantar el audio no acredita una escucha.
Consola y código conservan sus exclusiones. Se usa el mismo reproductor y los
tres marcadores acotados de página, sin crear un historial paralelo.

El contrato es independiente de `output_prediction`, de los datos de evaluación matemática y del estilo del texto. Ningún JSON antiguo necesita migración.

## Varios ejecutables de un proyecto CMake

Work puede declarar un caso por consumidor, conservando su entrada y salida literal. Por ejemplo, dos programas con entrada vacía:

```json
"outputCheck": {
  "cases": [
    { "target": "consultar_cajas", "standardInput": "", "outputs": ["1:2"] },
    {
      "target": "comprobar_cajas",
      "standardInput": "",
      "outputs": ["piezas=0 esperado=0 obtenido=0 OK\npiezas=4 esperado=1 obtenido=1 OK\npiezas=8 esperado=2 obtenido=2 OK\npiezas=20 esperado=5 obtenido=5 OK\npiezas=1 esperado=1 obtenido=1 OK\npiezas=5 esperado=2 obtenido=2 OK\npiezas=17 esperado=5 obtenido=5 OK\nBateria: OK\n"]
    }
  ]
}
```

La salida del cliente no acredita que se haya ejecutado la batería: cada objetivo y entrada conserva su veredicto independiente. Un objetivo o entrada sin caso mantiene la ejecución disponible, sin veredicto ni aceptación implícita de otro consumidor. Para avanzar basta la primera ejecución declarada en `cases`; las restantes son ensayos opcionales, no pruebas fabricadas ni acreditaciones de una actividad de construcción distinta. Los casos ejecutados se conservan al seleccionar otro objetivo del mismo proyecto sin alterar fuentes o configuración. La reapertura conserva esos datos y sus pruebas auténticas, sin inventar una ejecución nueva. Configuración, generación, construcción y ejecución conservan sus diagnósticos y no consumen corazones. Las explicaciones y orientaciones opcionales pertenecen a su caso, sin revelar el resultado de otro ejecutable.

Las siete prácticas de `datos_y_listas_de_objetivos` y `reorganizar_la_descripcion_conservando_el_comprobador` que solicitan ambos consumidores declaran dos casos por editor, con entrada vacía, objetivo explícito, stdout literal y retorno cero. Las lecciones requieren completar sus objetivos de compilación. Estas comprobaciones verifican las ejecuciones; los criterios de las actividades también piden inspeccionar las propiedades y la organización de CMake. Obtener la salida correcta no demuestra por sí solo que se haya realizado esa reorganización.

## Diagnóstico adicional de ejecución en Windows

Cuando la actividad pide observar un diagnóstico de biblioteca cuya redacción puede variar, Windows permite añadir un requisito al mismo caso sin fijar esa frase:

```json
"outputCheck": {
  "cases": [
    {
      "standardInput": "",
      "outputs": ["Indice invalido:Fin"],
      "standardError": "nonBlank"
    }
  ]
}
```

El caso exige simultáneamente stdout literal y al menos un carácter de stderr distinto de espacio, TAB, CR, LF, FF o VT ASCII. Una cadena vacía o formada sólo por esos separadores no acredita el diagnóstico. No se recorta ni se reescribe la consola. `standardError` sólo admite el literal `"nonBlank"`; los valores desconocidos o de otro tipo son errores de configuración. Para no comprobar stderr se omite la propiedad. El requisito pertenece al caso identificado por entrada y, si existe, `target`; no se hereda de otro caso o ejecutable.

Sólo se utilizan los dos canales de la **misma ejecución nueva**, terminada con código cero y correspondiente al programa/configuración/entrada capturados. Los diagnósticos de compilación, enlace o CMake nunca satisfacen stderr, aunque contengan texto. Una ejecución correcta que escribe en stderr sigue siendo una ejecución correcta; ese canal no es por sí mismo un fallo. Si stdout coincide y falta el diagnóstico requerido, el objetivo completo continúa pendiente y no habilita sus referencias ni el avance. La retroalimentación distingue la coincidencia de stdout del requisito adicional pendiente; no impone una frase de biblioteca ni revela otra solución.

Un logro anterior de stdout no acredita el requisito nuevo. La identidad de la prueba de finalización incluye `standardError`; al incorporarlo se conservan borradores, respuestas históricas y direcciones de estado, pero se necesita una ejecución que satisfaga el objetivo completo. Una vez conseguido, conserva las mismas reglas de acierto persistente de Windows: sólo el botón de reinicio lo revoca.

Work puede añadir el requisito a un caso existente sin cambiar sus `outputs`, entrada, objetivo CMake, programa ni identificadores. En lecciones con `stateLayout` v2, `prepare_v2` registra el cambio completo mediante la procedencia antes/después habitual, conservando origen, familia y direcciones. El validador permite esta ampliación compatible y rechaza retirar o debilitar un requisito publicado, así como cambiar las respuestas de stdout o el código bajo la misma identidad. No se regeneran mapas ni se convierten aciertos antiguos en pruebas del nuevo canal. Esta ampliación corresponde al motor Windows; no amplía por sí misma el contrato del motor Android conservado.

### Diagnóstico literal en stderr para clientes con contrato fijo

Cuando el ejercicio exige una frase concreta del **programa ejecutado** (no
un mensaje variable de la biblioteca o del compilador), cada caso admite
`standardErrorExact` como texto UTF-8, incluso la cadena vacía. Por ejemplo:

```json
"outputCheck": {
  "cases": [{
    "standardInput": "2x\n",
    "outputs": ["Resumen: validos=0\n"],
    "standardErrorExact": "Linea=1:FORMATO\n",
    "exitCode": 1
  }]
}
```

El verificador exige simultáneamente stdout literal, stderr literal,
entrada, objetivo CMake cuando exista y el código de salida **de la misma
ejecución nueva, terminada normalmente**. Se comparan caracteres exactos:
espacios, tildes, CR y LF son significativos; no se aplica trim ni
normalización. La cadena vacía comprueba que el programa no escribió
en stderr. Un fallo de construcción, timeout, aborto o una ejecución
anterior no aporta evidencia del diagnóstico solicitado.

El campo antiguo `standardError: "nonBlank"` permanece compatible.
Cada caso puede declarar uno de esos dos criterios, **nunca ambos**.
Al añadir el objetivo exacto a un ejercicio existente se mantienen
identificadores, borradores y progreso histórico, pero una prueba anterior
de stdout no acredita el requisito nuevo: cambia la identidad sellada de
finalización. Un objetivo exacto ya publicado no puede sustituirse,
retirarse o degradarse silenciosamente.

**Alcance:** comprueba el texto observable, no demuestra que se haya
realizado una validación interna específica ni que el contenido se haya
calculado en vez de fijarse como literal. Las demás capacidades abiertas
de Programación 2 siguen requiriendo comprobadores adicionales. Work
deberá revisar y publicar cada caso curricular antes de activarlo.
### Declaración raíz de U2L3: rechazo EOF sin modificar el proyecto

Para el propietario histórico `block_0009_0008` de
`programming_practice_tools__construccion_reproducible__opciones_y_revision_de_la_configuracion`,
el cargador de Windows reconoce esta declaración en la **raíz** de la lección:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0009_0008", "kind": "exactOriginalProjectRuntimeV1"}
]
```

El registro no se añade al bloque `cpp_compiler` ni modifica sus `projectFiles`,
`stateLayout`, entradas, traducciones o referencias. El contrato derivado exige
el proyecto **sin editar**, el objetivo CMake `revisar` con
`ACEPTAR_IGUAL=OFF`, entrada literal `6`, stdout vacío, stderr literal
`Entrada invalida` (sin salto de línea) y terminación normal con código `2`.
Una edición que produzca la misma salida no acredita este objetivo. El botón
del lector mantiene las fases **Configurar y generar → Construir objetivo →
Ejecutar objetivo construido**; el resultado pertenece a la ejecución real
de los archivos y opciones actuales.

La validación de `stable_state_layout.py` permite incorporar únicamente esa
declaración a la lección española v2 correspondiente. No autoriza reemplazar
las fuentes ni cambiar el objetivo para imitar el diagnóstico. Una vez
publicado, el registro no puede retirarse o reescribirse en una transición.

La declaración de U2L3 ya está integrada en el currículo español de `main`.
Activa una única ejecución obligatoria: el rechazo de la segunda extracción al
llegar a EOF. Las dos funciones abiertas de U3L6 necesitan una evaluación
semántica de los hechos del mensaje
y del flujo `std::ostream&` recibido. `standardErrorExact` o `nonBlank` no
son sustitutos de esa capacidad. Los informes U3L8 requieren, además,
conservar filas, índices y contadores, no solamente imprimir una cadena fija.

### Diagnósticos abiertos U3L6: hechos y flujo recibido

La capacidad de Windows para los dos editores históricos de
`programming_practice_tools__comprobaciones_repetibles__diagnosticos_para_corregir_una_peticion`
se activa **únicamente** mediante esta declaración en la raíz de la lección
española v2, después de la integración y revisión curricular por Work:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0003_0003", "kind": "semanticRejectionBoxesV1"},
  {"activity": "block_0006_0003", "kind": "semanticRejectionBalanceV1"}
]
```

La declaración no modifica `cpp_compiler`, su programa inicial, sus borradores
ni el `stateLayout`. La incorporación genera objetivos protegidos de
**tres casos para cajas** (rechazos, cuatro valores permitidos y entrada vacía)
y **cuatro para saldos** (rechazos, cuatro pares permitidos, entrada vacía y
tres pares propios). **Sólo el primer caso correcto de cada editor es necesario
para continuar.** Los otros se documentan con entrada y resultado esperado y
pueden ejecutarse voluntariamente, sin bloquear la página. Cada caso ejecutado
calcula los resultados y exige el código de salida normal correspondiente.

Los mensajes abiertos se comprueban por operación, número de petición,
cantidad o par de campos con sus valores, intervalo 0..20 o 0..9 y acción
de revisión, sin imponer una frase literal. El intervalo admite notaciones
claras como «de 0 a 9», «0-9», «0 - 9» o «[0, 9]», siempre
que se identifique como rango o dominio permitido. En los mensajes de diagnóstico
se reconocen también guiones y rayas Unicode, como «0–9» y «0 — 9»,
y el signo menos matemático en un dato rechazado, como «devoluciones=−1».
Esta equivalencia se limita a interpretar los hechos del mensaje: no cambia
los enteros de entrada admitidos, ni acepta límites o valores erróneos.
También reconoce los dos extremos expresamente nombrados: «mínimo 0, máximo 9» o «límite
inferior 0, límite superior 9», con esos mismos valores obligatorios.
Un solo límite no basta, ni puede darse por válido otro mínimo o máximo
contradictorio. Si el mismo mensaje incluye «rango 0..9, mínimo 1»,
se rechaza la contradicción aunque también aparezca un intervalo correcto.
La declaración debe comunicar el dominio **admitido**, no negar su vigencia:
«rango no permitido de 0 a 9» y «rango 0..9 no válido» no acreditan el objetivo.
En cambio, «no se permite 21; rango permitido de 0 a 9» conserva la
distinción entre un dato rechazado y el intervalo autorizado.
Si aparecen dos intervalos declarados para el mismo rechazo, ambos
deben expresar los límites correctos: «rango permitido 0..9; rango
permitido 0..8» se rechaza por contradicción. Un ejemplo numérico en
otra oración no se interpreta automáticamente como un segundo intervalo.
Una coma aislada entre datos no acredita un intervalo:
la notación con coma exige ambos corchetes de un intervalo cerrado.
Tampoco se acepta cambiar un límite, intercambiar cobros y devoluciones
o sustituir una acción afirmativa por «no revise». Se comprueba el número y el orden de ocurrencias, sin
confundir un saldo negativo válido con una entrada rechazada.
La variante de saldos requiere, por orden, un rechazo inferior, una aceptación y
un rechazo superior, con los otros operandos válidos y sin reutilizar los pares
preparados.

El diagnóstico describe los datos observados y solicita revisarlos. No acredita
una afirmación de que el programa ya los corrigió o los corrige automáticamente.
El análisis diferencia esa afirmación de una orden como «Corrija la cantidad» o
de su negación explícita, como «No se ha corregido automáticamente el dato».
También distingue negar una causa —«No se atribuye el rechazo a falta de
existencias»— de afirmar una escasez no observada, como «No hay existencias».
La negación se aplica a su propia cláusula: no anula una afirmación contradictoria
que aparezca después de un punto, punto y coma o enlace adversativo.

Para acreditar el editor también debe compilarse y ejecutarse **otra fuente
aislada** que llama a la misma función del estudiante con dos objetos
`std::ostringstream` distintos. La prueba rechaza una función que escribe
directamente en `std::cerr`, incluso si la ejecución original muestra el mismo
texto. Sólo se permiten modificaciones dentro del cuerpo de la función de
diagnóstico asignada; cambiar `main`, el componente, la firma o los datos del
programa no acredita el objetivo. El comprobador valida el alcance de las
llaves C++ y sus dígrafos `<%`/`%>` para impedir cerrar anticipadamente la
función e introducir otras declaraciones. No confunde las llaves que aparecen
dentro de cadenas, literales crudos o comentarios con delimitadores del
programa. La ejecución, la prueba independiente y los casos se sellan con
la revisión y la fuente vigente.

Las pruebas de autoría y de ejecución nativa admiten tanto la lección histórica
como la misma lección con las declaraciones revisadas ya publicadas. Validan
los propietarios, tipos y orden antes de construir en memoria el modelo previo
para contrastar su conservación. No retiran ni reescriben el contrato publicado
en el recurso curricular.

**Estado editorial:** la activación de la capacidad técnica es optativa hasta
que Work publique ambas declaraciones y compruebe el primer caso y sus sondas
semánticas independientes. Los editores
históricos publicados no se acreditan automáticamente ni se convierten en
opcionales. Esta capacidad tampoco sustituye la comprobación de conservación
de filas y contadores de U3L8.

### U3L8: informes completos y diferencias sin perder observaciones

Los editores originales de `programming_practice_tools__comprobaciones_repetibles__automatizar_la_revision_sin_borrar_evidencia`
admiten, tras revisión e integración curricular de Work, esta declaración **sólo en la raíz**
de la lección española v2:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0003_0003", "kind": "observationCompleteV1"},
  {"activity": "block_0006_0003", "kind": "observationDifferencesV1"}
]
```

No se modifican sus `cpp_compiler`, código inicial, funciones de lectura, respuestas,
borradores, `stateLayout`, mapas de idiomas ni referencias. Cada editor recibe
cuatro casos verificables: entrada publicada, dos registros coincidentes con
última línea sin `LF`, entrada vacía y un lote con repeticiones, campos
malformados y observaciones posteriores. **El primer caso correcto permite
continuar**; los otros tres son pruebas opcionales cuyos datos y resultados
esperados siguen disponibles. Para cada entrada, un cálculo separado
deduce filas, diagnóstico `Linea=N:FORMATO`, contadores y código de salida
normal. El informe completo muestra cada fila válida; la vista de diferencias
cuenta todas las válidas, pero sólo muestra las discrepantes.

La conversión de cada entero exige un único signo inicial opcional y un valor
completo representable como `int`, igual que las funciones de lectura conservadas
en esos editores. `+2` y `-2` son válidos; `+-2`, `+-0` y los demás signos
duplicados o combinados producen `FORMATO`. Esas líneas se cuentan como errores
de formato y no como observaciones válidas, coincidencias o diferencias.

También se ejecuta **el mismo programa** con dos entradas de control
independientes de los casos anteriores, dentro del rango curricular de doce
líneas físicas. Incluyen etiquetas repetidas antes y después de un error,
línea vacía, sólo blancos, enteros incompletos, campos unidos y una cuarta
columna, además de signos mezclados en ambos campos numéricos. El veredicto exige
igualdad de las dos salidas y códigos de salida de
estas ejecuciones, con los recibos asociados a la fuente y revisión capturadas.
En la consola nativa, se admite únicamente la traducción textual propia del
CRT de `LF` a `CRLF`; no se recortan caracteres ni se ignora el diagnóstico.

La regresión independiente `native/core/report_observation_check_tests.cpp`,
ejecutada por `.github/workflows/codex-u3-report-observations.yml`,
contrasta con GCC, Clang y MSVC las dos entradas históricas, sus informes
completos y filtrados, los duplicados, las líneas con errores, el final sin
salto, el archivo vacío, los límites de `int` y la frontera de doce líneas.
En la frontera comprueba un lote de doce líneas con etiquetas repetidas, tres
filas mal formadas y una última fila sin salto final: hay nueve observaciones
válidas, seis coincidencias y tres diferencias, aunque la vista filtrada
muestre solamente las tres discrepancias. Se conserva además la posición de
cada diagnóstico de formato, incluso después de un rechazo intermedio.
Comprueba la coincidencia simultánea de `stdout`, `stderr` y
retorno, y la equivalencia exclusivamente textual `LF`/`CRLF` en Windows.
Esta prueba del oráculo no sustituye la compilación y ejecución de la
solución del estudiante ni acredita una declaración que Work aún no haya
integrado en el JSON.

El editor sólo puede corregir el cuerpo de `main`: `leer_entero` y
`leer_registro`, sus firmas, sus inclusiones y el resto del archivo deben
permanecer intactos. La misma protección léxica de llaves C++ detecta el
cierre prematuro de `main` y declaraciones introducidas fuera de su
alcance; admite expresiones y bloques internos correctamente anidados.
Una salida preparada codificada como constante no acredita las entradas
independientes. Una ejecución que no termine normalmente
o un error de construcción tampoco cuenta como resultado esperado.

**Estado:** el motor y sus pruebas se integran en `Codex`, pero la ficha
`P2_U3_DIAGNOSTICOS_E_INFORMES` permanece abierta hasta que Work publique las
declaraciones compatibles y se comprueben los cuatro editores reales sin
dar crédito por ejecuciones anteriores. La revisión visual y auditiva
personal continúa a cargo del estudiante.

### Aserciones U3L9: aborto real, `NDEBUG` y corrección

La capacidad de Windows para los tres editores originales de
`programming_practice_tools__comprobaciones_repetibles__comprobar_acuerdos_internos_con_aserciones`
se activa únicamente mediante estas declaraciones de raíz en la lección española v2:

```json
"compilerRuntimeChecks": [
  {"activity": "block_0002_0003", "kind": "wasiAssertionAbortV1"},
  {"activity": "block_0003_0007", "kind": "wasiAssertionSuppressedV1"},
  {"activity": "block_0004_0007", "kind": "wasiAssertionRepairV1"}
]
```

Cada editor utiliza su identidad histórica y **un solo caso visible obligatorio**.
Su código inicial y la entrada `-1 5\n` se conservan. El lector selecciona
específicamente WASI, con la versión de SDK y runtime fijadas para el proyecto;
no sustituye la terminación por un código de MSVC ni acepta un número distinto
de cero como demostración suficiente de una aserción.

**Aserción activa:** se ejecuta la fuente original sin edición. La prueba
comprueba una captura de ejecución real con fuente y artefacto WebAssembly,
el rechazo público anterior, la expresión que falló, el archivo y función
`cajas_para`, la ruta por `__assert_fail` y la trampa de Wasmtime. No se
establece universalmente el código de aborto: se verifica la causa observable
de esta compilación concreta. Una salida inventada, timeout o fallo genérico
del runtime no acredita la actividad.

**`NDEBUG`:** se mantiene la directiva antes de `<cassert>` y se usa la fuente
original. La entrada preparada produce stdout `Cajas=1\n`, el rechazo público
exacto en stderr y terminación normal con código `1`. Es un rechazo de
solicitud manejado, no un aborto. El hecho de que el cálculo defectuoso llegue
a imprimirse también evidencia que la condición de `assert` no detuvo el programa.

**Reparación:** sólo se admite modificar la expresión inicializadora de
`cajas` dentro de `cajas_para`; se conservan `assert`, las funciones públicas,
las firmas y las demás instrucciones. El caso preparado debe producir
`Cajas=2\n`, su diagnóstico público y retorno normal `1`. Además se
ejecuta exactamente la fuente editada con dos entradas automáticas que
cubren cero, múltiplos, sobrantes, la frontera superior y rechazos exteriores.
El comprobador deriva independientemente los valores correctos y no admite
una expresión literal que sólo acierte el primer caso.

Work integró las tres declaraciones en la raíz del JSON español y conservó los
editores, las entradas, las referencias, los identificadores y las direcciones
históricas de progreso. La comprobación funcional en Windows y la aprobación
de las nuevas ejecuciones de CI son requisitos separados para cerrar la ficha:
la integración curricular por sí sola no acredita el aborto ni la reparación.

## Entrada del laboratorio

En un `cpp_compiler` curricular, el campo «Entrada» sólo aparece si el bloque declara `initialStandardInput` —también una cadena vacía, para permitir una actividad con EOF— o si algún caso de `outputCheck` declara una entrada no vacía. No se inspecciona el código del alumno para decidirlo. Sin esas declaraciones, la ejecución recibe entrada vacía y termina en EOF si intenta leer. Cualquier borrador de entrada anterior se conserva en almacenamiento, pero no se envía al programa mientras el campo no corresponda a esa actividad. El compilador libre del menú principal conserva su campo de entrada.

### Conservación histórica de Android

El guardado Android de entrada comparte el códec de texto escapado del código fuente, manteniendo la clave de borrador existente. Conserva espacios manuales, tabuladores y saltos finales sin depender de la indentación del XML de Android. Antes de guardar otro campo se protegen también las entradas y fuentes antiguas aún no abiertas, sin cambiar sus caracteres salvo reparación demostrada por una huella previa. Se utiliza una única escritura para todo el mapa: Android reescribe todas las preferencias y una protección parcial dejaría otras fuentes expuestas.

El antiguo serializador podía añadir cuatro espacios después de un LF final. Cuando una huella previa demuestra el original, la reparación exacta queda persistida. No hay menú de revisión ni recorte general de entradas. Work confirmó que los cuatro espacios de su prueba `Leo\n2\n    ` eran un defecto: ese caso concreto se corrige manualmente a `Leo\n2\n`, sin quitar el salto final ni cambiar otros datos. La intención del currículo no permite deducir la intención de cualquier borrador personal. Si aparece otro caso ambiguo, hay que indicar su ubicación y consultar a Work sobre la consigna, sin añadir decisiones al lector ni eliminar datos por su forma. Los guardados nuevos registran la huella del texto exacto introducido, incluidos espacios deliberados, tabuladores y saltos. El código fuente conserva sus huellas y marcas de tabulación anteriores; las reparaciones demostradas también se guardan definitivamente. Todo este formato es privado; Work no añade metadatos a las lecciones.

## Acreditación y avance en Windows

En Windows, el primer acierto completo de un `cpp_compiler` con `outputCheck` o
`compileCheck` se conserva hasta pulsar **Reiniciar código**. Editar, deshacer,
cambiar Entrada o volver a compilar no revoca ese logro. Cada nuevo intento se
comprueba de nuevo: un rechazo pedagógico posterior muestra «Incorrecto» y su
guía, separados del logro histórico. La reapertura conserva borrador, recibo y
última revisión válida; no fabrica una ejecución ni restaura una consola
transitoria. Un intento interrumpido antes de evaluarse no borra la revisión
anterior. Una nueva comprobación correcta sustituye la revisión de error.

La acreditación conserva el objetivo y una instantánea del programa que lo resolvió, con una prueba autenticada de su primera entrada y objetivo. Un antiguo `correct=true`, una predicción, un marcador de página o una salida sin ejecución satisfactoria no equivalen a esa prueba. En `outputCheck` el primer caso correctamente comprobado basta para avanzar; los casos posteriores son pruebas voluntarias, que pueden tener sus propios recibos sin modificar el logro ganado. Los antiguos sellos que exigían todos los casos siguen siendo válidos. Cambiar sólo Entrada o elegir otro ejecutable del mismo proyecto permite conservar los casos comprobados; editar fuentes o configuración antes del primer acierto invalida el intento incompleto y Undo no lo recupera. Una vez ganado el logro, sólo el reinicio expreso lo revoca. Los errores técnicos siguen siendo diagnósticos y no consumen corazones.

La propiedad raíz histórica sigue admitiéndose para conservar los assets:

```json
"compilerPuzzlesRequired": true
```

Su valor predeterminado de datos es `false`, pero no vuelve opcional un editor
curricular Windows: `compiler_requires_completion` requiere todos ellos.
Programación 1 ya declara la obligatoriedad y objetivos evaluables en
sus 371 editores españoles activos: 354 de stdout y 17 diagnósticos. Este censo
de metadatos no acredita su ejecución ni aceptación visual. Los tres editores
retirados de U1 conservan sus datos históricos y no generan obligaciones activas.
Los diagnósticos tienen `compileCheck`; no se les inventa stdout.

Cada compilador curricular activo con `outputCheck` o `compileCheck`
forma parte del progreso de su grupo histórico y debe quedar resuelto para
continuar o finalizar. En la misma página, el contenido posterior espera al
editor anterior. Un marcador posterior vuelve al primer objetivo pendiente,
conservando sus datos. El lector respeta además la lectura de la retroalimentación;
no confunde el logro del programa con haber escuchado su explicación. El acceso
a páginas teóricas también debe respetar el orden de páginas alcanzadas, sin
imponer escucha de audio como condición para estudiarlas.

El prototipo general sigue libre. Los bloques retirados conservan sus datos históricos y no crean controles, audio ni obligaciones activas. El contrato de Windows no cambia las reglas de ejecución o conservación Android descritas en los apartados anteriores.

## Rechazos de compilación intencionales en Windows

Diecisiete editores españoles admiten un objetivo independiente de stdout:
cinco de fuente única en U9 y doce multifichero en U6.

```json
"compileCheck": {
  "diagnostic": "const-member-call",
  "explanation": "La llamada requiere un objeto modificable. El programa fue rechazado antes de ejecutarse."
}
```

`diagnostic` selecciona uno de los contratos revisados. `explanation` es
texto de acierto opcional y no vacío, redactado por Work; no se muestra ni se
narra antes del acierto. No se infiere un objetivo de la consigna ni se activa
automáticamente en un asset.

| Lección de `datos_propios_y_estado_valido` | Editor estable | `diagnostic` | Modificación comprobada |
| --- | --- | --- | --- |
| `consultas_const_y_objetos_constantes` | `block_0006_0006` | `const-member-call` | Añadir `fija.cambiar_a(4,2);`, conservando `const`. |
| `valores_por_defecto_con_significado` | `block_0012_0008` | `explicit-copy-initialization` | Añadir `Contador no_permitido = 3;`, conservando `explicit`. |
| `valores_por_defecto_con_significado` | `block_0012_0011` | `explicit-argument-conversion` | Añadir `leer(3);`, conservando `explicit`. |
| `valores_por_defecto_con_significado` | `block_0014_0011` | `explicit-assignment` | Añadir `destino = 3;`, conservando `explicit`. |
| `argumentos_con_tipos_que_expresan_su_funcion` | `block_0006_0006` | `swapped-named-arguments` | Añadir `const Caja mal{Alto{4}, Ancho{3}};`, conservando el constructor `(Ancho, Alto)`. |

### Doce contratos multifichero de U6

Todos pertenecen a `funciones_y_contratos` y conservan `projectFiles` y
`sourceFiles` publicados, incluida la selección de fuentes que causa el fallo.

| Lección | Editor estable | `diagnostic` |
| --- | --- | --- |
| `declaraciones_y_firmas` | `block_0014_0002` | `u6-difference-unselected-definition` |
| `declaraciones_y_firmas` | `block_0015_0002` | `u6-difference-conflicting-return` |
| `declaraciones_y_firmas` | `block_0017_0002` | `u6-difference-missing-header` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0016_0002` | `u6-envelopes-missing-header` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0017_0002` | `u6-envelopes-missing-declaration` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0018_0002` | `u6-envelopes-unselected-definitions` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0019_0002` | `u6-envelopes-duplicate-definition` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0021_0002` | `u6-series-unknown-call` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0022_0002` | `u6-series-unselected-definitions` |
| `interfaces_cabeceras_y_programas_con_varios_archivos` | `block_0023_0002` | `u6-series-duplicate-definition` |
| `construir_un_proyecto_desde_su_interfaz` | `block_0013_0002` | `u6-shelf-unselected-definition` |
| `construir_un_proyecto_desde_su_interfaz` | `block_0014_0002` | `u6-shelf-conflicting-return` |

La incorporación exige el editor, la lección y el mapa v2 español revisados, sin
cambiar su fuente inicial completa. Ningún `compileCheck` admite `outputCheck`,
`initialStandardInput` o `cmake`. Los cinco de U9 no admiten archivos de proyecto;
los doce de U6 requieren los suyos. La identidad del diagnóstico no es un patrón
abierto para aprobar cualquier programa que falle.

Windows utiliza el MSVC educativo configurado. En los cinco contratos U9, cada intento válido compara un
control y un candidato nuevos, compilados solamente con `/c`, sin enlazarlos ni
ejecutarlos. El control debe compilar; el candidato debe producir el rechazo
semántico previsto, en el archivo y las líneas de la sentencia introducida.
Los códigos primarios observados deben corresponder a ese objetivo e intervalo.
Se preservan los diagnósticos completos sin comparar frases ni idioma de MSVC.
Cancelación, límite técnico, timeout, herramienta ausente, control inválido,
sintaxis ajena y enlace no acreditan el objetivo ni consumen corazones.

Los elementos léxicos conservan el programa inicial salvo la sentencia
solicitada dentro de `main`; espacios y comentarios no imponen una única
presentación. Esto comprueba cinco modificaciones cerradas, no cualquier
programa que falle. Quitar `const` o `explicit`, cambiar la interfaz o introducir
otro error no cumple la práctica. El editor sigue guardando los otros
borradores y mostrando sus diagnósticos, sin acreditarlos por ese fallo.

En U6 se compilan controles y candidatos nuevos por unidad de traducción. Los
contratos de enlace exigen que todas las unidades compilen y que el control
enlace; el candidato debe presentar el símbolo y el rechazo de enlace previstos.
Los de cabecera, declaración o tipo comprueban el rechazo en la unidad y fase
esperadas. Se autentican los archivos y las modificaciones admitidas por cada
receta; una fase anterior fallida, otro error, una captura truncada o un control
inválido no acreditan el objetivo. No se ejecutan estos programas rechazados ni
se comparan frases localizadas de MSVC.

La nueva prueba corresponde al objetivo y al programa capturados por la misma
pulsación; no usa una consola anterior ni un booleano antiguo. Un acierto completo
y su instantánea permanecen hasta **Reiniciar código**, separados del borrador
actual y de la última revisión. Cambiar o volver a compilar no revoca el logro;
un intento posterior puede mostrar un veredicto incorrecto y su guía. Su acreditación propia
controla referencias, avance y detención/continuación del lector cuando
corresponde.

La importación Android conserva datos y borradores sin fabricar pruebas Windows.
La procedencia nativa impide adoptar después un asset que retire o cambie un
objetivo ya incorporado; rechazarlo conserva los datos. Work integra los diecisiete
contratos y completa presentación, interacción y escucha por separado. Esta
capacidad no amplía el motor Android conservado.

## Retornos intencionales y representación CRT

En U12L13, `compartir_flujos_sin_acoplar_el_calculo`, `block_0005_0002`,
el objetivo revisado exige entrada vacía, stdout `Entrada invalida|9:4` sin LF
final y `exitCode: 1`. En U12L26,
`construir_un_programa_de_carga_y_guardado`, `block_0004_0017`, exige igualmente
entrada vacía, stdout `Error al abrir salida` sin LF final y retorno `1`. La
incorporación de estos objetivos se autentica contra sus editores completos
publicados; no admite cambiar el programa para facilitar su evaluación.

En U12L20, `clasificar_caracteres_y_normalizar_una_copia`, el primer editor
`block_0005_0004` conserva las dos alternativas revisadas de LF/CRLF de la CRT
Windows y todos sus controles literales. Es una revisión cerrada de ese objetivo,
no una normalización general: los demás stdout siguen comparándose byte a byte.
La guía pública y su marcado pueden editarse sin cambiar esos bytes ejecutables.

## Autoría y recuperación de guías públicas

`outputCheck.cases[i].explanation`,
`outputCheck.cases[i].reviewFeedback[j].text` y `compileCheck.explanation` son
texto público condicional. No se leen antes de obtener el resultado pertinente;
`outputs`, `standardInput`, `target`, `standardError`, `exitCode` y `diagnostic`
siguen siendo datos del contrato, nunca texto del guion curricular.

Work puede revisar las guías mediante la cadena editorial v2 sin cambiar el
programa, los casos, sus condiciones o las identidades. La proyección de guías
retira únicamente `explanation` y `reviewFeedback` de los casos y `explanation`
del diagnóstico al comparar el contrato ejecutable; no retira `compileCheck`.
Esto no autoriza revisiones de respuestas ni rebaja las guardas históricas.

Un recibo completo autenticado de un objetivo de un solo caso o de diagnóstico
puede mostrar su explicación vigente al reabrir, sin recompilar ni inventar otro
acierto. Una última revisión incorrecta conserva prioridad. Los objetivos de
varios casos mantienen su retroalimentación acreditada, sin elegir arbitrariamente
la explicación de uno de ellos. Un cambio de guía puede exigir volver a leer
esa explicación desde su inicio; no acredita escucha ni duplica marcadores.

Los rechazos nuevos usan reglas privadas v2 que no dependen de las explicaciones
de acierto. Editar sólo ese texto conserva la prioridad y la procedencia de un
rechazo válido, también en diagnósticos. Sus condiciones ejecutables y las
reglas/textos de error siguen comprobándose completos. Los registros v1 se
aceptan únicamente cuando sus reglas originales completas aún coinciden; no se
reinterpretan como v2 ni se reescriben. Si cambia una guía de error o su selector,
no se deduce otra guía desde el texto anterior: hace falta una nueva comprobación
para registrar la revisión correspondiente.

Las guías admiten el mismo `textSemantics` raíz que los demás textos públicos.
La referencia estable del bloque y el JSON Pointer identifican su campo exacto;
la igualdad de dos frases no decide el propietario. La anticipación prepara sólo
las guías explícitamente marcadas, con el mismo veredicto, separador y proyección
que la lectura inmediata, sin exponerlas en pantalla antes del intento. Véase
el contrato completo en [STABLE_LESSON_STATE.md](curriculum/STABLE_LESSON_STATE.md#autoría-explícita-de-texto-público-en-windows).

## Ampliaciones revisadas con conservación en Windows

La extensión `stateLayout.compilerRevisions` admite las diez ampliaciones
cerradas de `CPP_CASOS_ADICIONALES_CON_PROGRESO`; su contrato de autoría está en
`docs/curriculum/STABLE_LESSON_STATE.md`. No es una excepción general para editar
los esperados de un compilador publicado.

En U11L7 se conservan los 19 casos originales y se añaden dos. Una prueba anterior
válida puede reutilizarse exclusivamente para el mismo caso del mismo programa
capturado; no acredita ninguno de los dos casos nuevos. Una edición de fuentes
antes de completar el objetivo ampliado mantiene las reglas de invalidación de
intentos. En U11L8 y U11L10–L16, incluidos ambos editores de L15, el caso único exige un stdout ampliado: el logro
anterior se conserva como histórico, separado de la nueva comprobación completa.
Ni una salida anterior ni un booleano antiguo aprueban silenciosamente el objetivo
ampliado. El botón Reiniciar elimina el intento activo y evita que una prueba
histórica vuelva a proyectarse como acreditación corriente.

Las nueve ampliaciones de stdout modifican simultáneamente la referencia estática
posterior: únicamente añaden sus llamadas revisadas antes del `return 0;` final
de `main`. El mapa conserva el bloque anterior completo en el archivo
`references` de su revisión. No se permite ampliar el esperado dejando una
referencia incompatible ni usar este contrato para cambiar otros programas.
Sólo el segundo editor U11L15 amplía además su inicial exacto con la quinta
llamada revisada. Los borradores anteriores no se sustituyen automáticamente:
el alumno conserva su programa y dispone del fragmento adicional dentro de la
misma lección. Reiniciar es la acción que adopta el inicial nuevo y limpia los
recibos de ese objetivo, sin afectar al primero.

U11L12 añade el rechazo del parcial `11*10/2;`; U11L13 añade ese contraste y la
novena agrupación; U11L14 añade `?z;4;`, que exige descartar sin retokenizar y
recuperar la operación siguiente. Conservan los stdout anteriores completos y
añaden exclusivamente los sufijos revisados de 34, 62 y 49 bytes LF. Son baterías
dentro del programa con stdin vacío: un logro anterior completo conserva su
1/1 histórico y deja la nueva comprobación en 0/1. Los estados sin comprobar
siguen sin crédito; los informes impresos no son recibos individuales.
El helper prepara propuestas sin escribir los assets; Work integra conjuntamente
consigna, esperado y referencia después de la ampliación técnica.

Las tres ampliaciones restantes son U11L15 primero, de catorce a diecinueve
informes; U11L15 segundo, de cuatro a cinco; y U11L16, de nueve a doce. También
son una ejecución por batería, no un recibo por línea impresa. Sus archivos,
llamadas y referencias exactas se detallan en el contrato de autoría enlazado;
los logros anteriores se conservan como históricos sin acreditar esas ampliaciones.

La ampliación no crea otro editor, no copia borradores ni altera las traducciones.
Work sigue siendo responsable de integrar las consignas y referencias revisadas;
las pruebas del contrato no equivalen a su aceptación visual o escucha.

## Cambio compatible del primer caso

Una permutación explícita puede cambiar cuál de los casos ya publicados ocupa
la primera posición. `reorder_compiler_cases` conserva cada caso completo y deja
su procedencia en `stateLayout.compilerCaseOrderRevisions`; el esquema y las
guardas están descritos en `docs/curriculum/STABLE_LESSON_STATE.md`.

El primer caso actual sigue siendo el único obligatorio para avanzar. El logro
obtenido con el antiguo primero permanece en su historia, pero no sustituye la
ejecución de una entrada distinta. Si el estudiante ya había comprobado el nuevo
primero como ensayo opcional con el mismo programa, su recibo auténtico puede
acreditarlo. Las pruebas de programas diferentes no se mezclan. Los otros casos
se conservan como ensayos opcionales, con sus salidas, canales y guías originales.

El lector muestra el logro anterior conservado cuando el primer caso actual
está pendiente. No copia código al borrador ni ofrece un fragmento adicional por
una mera permutación. Reiniciar limpia los recibos de todas las versiones de ese
editor y la reapertura no recupera los eliminados. Esta capacidad general no
autoriza cambiar esperados, agrupar casos independientes ni atribuir una
ejecución nueva a una comprobación histórica.

### Separar o sustituir casos conservando su historia

La revisión general `compilerCaseRevisions` permite que Work sustituya la
lista de casos de un editor ya publicado sin cambiar su programa, cliente,
archivos ni otros campos. El helper `revise_compiler_cases` recibe la lista
completa nueva y sus marcas semánticas explícitas; archiva el predecesor
íntegro y autentica la transición editorial. Véase el esquema y el CLI en
`docs/curriculum/STABLE_LESSON_STATE.md`.

Un éxito de una entrada agrupada conserva su valor histórico. No acredita
por sí mismo las entradas separadas. El nuevo primer caso necesita su propia
comprobación correcta, salvo que ya exista un recibo auténtico del caso
completo idéntico y del mismo programa. La adopción puede recuperarlo de una
versión anterior archivada, pero no combina pruebas de programas distintos.
Los casos restantes son opcionales y cada uno conserva su entrada, resultado,
canales, retorno y guía propios.

La interfaz muestra el logro anterior mientras el primer caso de la versión
actual siga pendiente. Conserva el borrador sin insertar una fuente nueva.
Una revisión de casos, una permutación, un formato compatible y un retorno se
aplican en su orden editorial, conservando sus archivos y las pruebas de cada
versión. El reinicio explícito del editor elimina los recibos y marcadores de
sus versiones, de forma que una reapertura no restaure un logro reiniciado.

### Adaptar el cliente al separar los casos

Cuando las llamadas independientes están escritas dentro del cliente de un
editor C++, Work puede preparar una transición explícita con
`revise_compiler_client_cases`. Recibe el propietario estable, un objeto
`client: {path, source}`, la lista completa de casos y su semántica. El
registro se incorpora a `stateLayout.compilerCaseRevisions`, con el bloque
anterior íntegro y su procedencia editorial. El esquema y el comando
`revise-compiler-client-cases` se describen en
`docs/curriculum/STABLE_LESSON_STATE.md`.

La operación conserva la modalidad de almacenamiento y construcción del editor:

| Modalidad | Archivo seleccionado | Campos que conserva |
|---|---|---|
| Fuente única | La ruta virtual exacta `main.cpp` identifica `initialCode`. | No crea `projectFiles`, `sourceFiles` ni una receta CMake. |
| Proyecto C++ ordinario | Un archivo existente de `sourceFiles`, o de su selección implícita de fuentes C++. | Conserva rutas, auxiliares y selección. Si existe un `initialCode` superior, debe coincidir exactamente con el cliente previo y ambos se actualizan juntos. |
| Proyecto CMake | Un archivo C++ existente de `projectFiles`. | Conserva `CMakeLists.txt`, bibliotecas, auxiliares y la configuración `cmake`, incluidos objetivo, opciones y tipo de construcción. El `initialCode` superior es independiente y permanece intacto. |

La fuente única mantiene el límite de 4 MiB. Los proyectos C++ ordinarios
mantienen hasta 256 archivos, 16 MiB por archivo y 32 MiB en total. CMake
conserva sus límites de 128 archivos y 8 MiB totales; la receta determina
las fuentes y no admite `sourceFiles`. La autoría selecciona un archivo C++
existente. CMake resuelve las fuentes de cada objetivo durante la construcción
con la receta conservada; la transición no interpreta por adelantado esa descripción.

Los diagnósticos `compileCheck` conservan su objetivo y quedan fuera de esta
operación. Los requisitos adicionales de un recorrido tampoco se sustituyen
por acertar la salida: `cmakeSelectiveRecoveryV1` mantiene su contrato de un
caso y rechaza una desagregación que no lo respete.

Las tres modalidades conservan la entrada inicial declarada, las reglas de
comparación y los demás campos del editor. Los casos se suministran
expresamente; pueden conservar sus valores cuando el cliente cambia. La API anterior
`revise_compiler_cases` mantiene su contrato de revisión exclusiva de casos.

Cada adaptación del cliente crea un objetivo de ejecución propio. Un logro
anterior permanece en su historia, pero no acredita esa versión, aunque los
casos sean idénticos o una adaptación posterior recupere exactamente los bytes
de un cliente antiguo. Dentro de una misma versión del cliente, las revisiones
exclusivas de casos y sus permutaciones pueden reutilizar recibos de casos
completos idénticos del mismo programa, conforme a las reglas anteriores.

Sólo el primer caso actual es obligatorio. Los demás ensayos siguen disponibles
con sus entradas y resultados separados. Si el bloque no declaraba una entrada
inicial y los casos nuevos contienen datos, el lector presenta el campo Entrada
vacío mediante su regla existente. La carga de la publicación no cambia el
código, la entrada ni los archivos de un borrador guardado. El estudiante adapta
el cliente en el editor existente; sus auxiliares conservan su trabajo. Un
editor sin borrador utiliza los archivos iniciales de la nueva publicación.

Las revisiones de formato mantienen sus archivos originales. El cargador
reconstruye la fuente canónica de cada versión del cliente y conserva por
separado el formato histórico de los auxiliares. También utiliza el prefijo
editorial de cada presentación archivada, de modo que un retiro de otros
bloques no atribuya al historial una versión posterior del programa.

La autenticación comprueba los archivos y la secuencia de cambios autorizados.
Work conserva la responsabilidad de revisar el cliente, sus contratos, los
datos y resultados de cada caso y las referencias posteriores. Esta capacidad
no demuestra equivalencia general entre programas C++ ni realiza por sí misma
la integración curricular.

### Actualizar una receta conservando el proyecto publicado

La modalidad `recipe` de `stateLayout.compilerCaseRevisions` permite revisar
un archivo CMake existente sin cambiar los programas ni los casos. Se prepara
mediante `revise_compiler_cmake_recipe`; su esquema y las guardas están en
`docs/curriculum/STABLE_LESSON_STATE.md`. Es una alternativa explícita a
`client`, no una autorización para editar silenciosamente un bloque protegido.

Esto permite adoptar avisos adecuados para MSVC en editores cuyo contrato
exige conservar CMake, como la ampliación de batería de U3L10
`block_0006_0011`. El cargador conserva cada receta histórica y crea un
objetivo nuevo para la revisión. También admite el objetivo declarado en
`compilerOpenBatteryChecks`, sin añadir `outputCheck` al bloque original.
La reconstrucción de antes y después mantiene ese contrato raíz completo.

Los borradores no se sustituyen al cargar la lección. Los logros anteriores
siguen disponibles en su historia, pero no acreditan la receta nueva, ni un
retorno posterior a sus mismos bytes. La primera ejecución correcta exigida
por el objetivo actual sigue siendo suficiente. Las pruebas nativas utilizan
ahora una transición autenticada de la receta en ambos cargadores; no
necesitan cambiar a mano el modelo después de cargarlo.

## Actividad integrada

En `programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json`,
el editor estable `block_0004_0002` comprueba `Caja recibida` en español, sin salto
final ni comillas; la referencia aparece después del intento. No se deduce el
esperado de la predicción del programa inicial ni se cambia esa respuesta.
Los programas retirados conservan sus datos, sin controles ni obligaciones
activas. El segundo editor conserva su dependencia real del primero. La ausencia
de `outputCheck` sólo conserva exploración libre en el Laboratorio independiente;
un editor curricular sin evaluador continúa pendiente técnicamente.

## Requisitos generales de constantes nombradas

Un bloque `cpp_compiler` existente que ya dispone de `outputCheck` puede
recibir, desde la raíz de la lección española con `stateLayout` v2, una
declaración `compilerNamedRuleChecks`. No se cambian el bloque original, sus
respuestas, `initialCode`, `projectFiles` ni las direcciones históricas.

```json
{
  "compilerNamedRuleChecks": [
    {
      "activity": "block_0002_0004",
      "path": "componente/src/cajas.cpp",
      "rules": [
        {
          "function": "cajas_para",
          "value": 4,
          "purposeWords": ["capacidad", "plaza", "cupo"],
          "minUses": 2
        }
      ]
    }
  ]
}
```

El objeto anterior es un **ejemplo de la extensión raíz**, no un JSON
curricular publicado. `activity` identifica un editor original; `path`
identifica un archivo C++ ya presente en el proyecto. Para editores de un solo
archivo, `path` es la cadena vacía. `function` identifica la función;
`value` fija el acuerdo entero; `purposeWords` permite varios nombres
de propósito; `minUses` exige un mínimo de referencias de la constante.

El lector exige una definición de la función, una constante local `int` de
valor correspondiente, un identificador de propósito y referencias reales al
resultado, incluso cuando se devuelve mediante variables intermedias.
Admite las grafías equivalentes `const int`, `int const`,
`constexpr int` e `int constexpr`; no exige un orden exclusivo de esos
especificadores de tipo. La constante admite `= expresión`,
`(expresión)`, `{expresión}` y `= {expresión}`; las dos formas con
llaves admiten una coma final. Se exige una sola expresión entera explícita,
reconocida por el evaluador acotado descrito a continuación. Una lista vacía
es C++ válido e inicializa el entero a cero, pero no proporciona una expresión
reemplazable para esta comprobación. Las variaciones privadas sustituyen
únicamente la expresión y conservan el signo igual, las llaves, la coma final
y los comentarios o saltos de línea exteriores. Los resultados parciales pueden conservarse en
variables intermedias `const int`, `int const`, `const auto`, `auto const`,
`constexpr int`, `int constexpr`, `constexpr auto` o `auto constexpr`,
siempre que dichas variables sean declaraciones C++ válidas y sus
referencias lleguen al resultado de la función. Un cálculo que depende
de la entrada en tiempo de ejecución no puede inicializar una variable
`constexpr`; en ese caso se puede usar, por ejemplo, `int const`. Los comentarios, las cadenas y las constantes
declaradas pero no utilizadas no
acreditan el objetivo. La constante puede inicializarse también con
aritmética entera acotada sobre literales decimales, incluidos paréntesis,
signos y los operadores `+`, `-`, `*`, `/` y `%`. Se respetan
precedencia y división entera. Los literales enteros C++ en base
decimal, binaria, octal y hexadecimal se interpretan según su base real:
`4`, `0b100`, `04` y `0x4` representan el mismo entero, pero `010`
representa ocho y no diez. Se reconocen separadores entre dígitos y
sufijos enteros válidos; se rechazan bases mal formadas, desbordamientos,
divisores cero y sufijos ajenos, sin interpretar macros ni evaluar
expresiones C++ arbitrarias. El analizador sólo admite
directivas `#include` en el archivo evaluado: una función excluida con
`#if 0`, una macro o una declaración generada no constituyen evidencia
fuente acreditable. Los comentarios que mencionan esas directivas siguen
siendo texto ordinario.

Las sondas independientes compilan el mismo código en otro contexto.
Por ello, el requisito de fuente no admite dependencias activas de
marcadores que puedan identificar esa segunda compilación, como
`__LINE__`, `__FILE__`, `__COUNTER__`, `__DATE__`, `__TIME__` o
`std::source_location`. Las menciones dentro de comentarios y literales
de texto no constituyen tal dependencia y siguen permitidas.
Esta restricción evita que una función detecte la sonda y devuelva allí
un resultado distinto del programa del estudiante; no sustituye las
pruebas conductuales ni afirma detectar todos los posibles trucos de
diferenciación del entorno.

Además, el motor comprueba la compilación, la ejecución y el primer caso
válido de `outputCheck` antes de registrar el acierto. Los casos
posteriores permanecen optativos.

Los usos algebraicamente neutros más directos, como `capacidad -
capacidad`, `capacidad / capacidad` o multiplicaciones por expresiones
enteras constantes de valor cero, no cuentan como referencias útiles.
La misma regla reconoce **una operación neutra completa** aunque sus
operandos tengan paréntesis redundantes: `(capacidad - (capacidad))`,
`((capacidad) / capacidad)` y `((capacidad) % capacidad)` no
acreditan usos del acuerdo. También impide transferir dos referencias
ficticias a un resultado por medio de una variable intermedia calculada
con esa operación. También descarta los inversos aditivos
`capacidad + (-capacidad)` y `(-capacidad) + capacidad`, aunque la
referencia negada contenga más paréntesis. Ni esa suma ni una variable
intermedia derivada exclusivamente de ella acreditan dos usos de la
constante. En cambio, `capacidad - (-capacidad)` y
`capacidad + capacidad` siguen siendo expresiones dependientes de su
valor y se comprueban después mediante ejecución.
El **operador coma** también exige distinguir referencias de valores
utilizados. En `return (capacidad, capacidad);` se evalúa primero la
expresión izquierda, pero su valor se descarta: sólo el operando final
determina el valor retornado. Por eso no se acreditan referencias a
una constante que aparezcan exclusivamente en un operando anterior,
cuando éste sea aritmética pura sin efectos secundarios. Se reconocen
tanto la coma entre paréntesis como `return capacidad, capacidad;`;
se conservan las referencias reales de la última expresión. Las comas
que separan los argumentos de una función o los elementos de una
lista entre llaves no son ese operador. El análisis no intenta borrar
usos de expresiones cuyos posibles efectos secundarios no pueda
descartar y mantiene la verificación mediante compilación y sondas
independientes.
La comprobación se limita a identidades comprobables
en la expresión examinada: `capacidad - (capacidad * 2)` no equivale
a una cancelación. Esto también se aplica a las grafías equivalentes
**sin paréntesis** `capacidad - capacidad * 2`,
`capacidad - capacidad / 2` y `capacidad - capacidad % 3`:
multiplicación, división y resto forman primero el operando derecho.
La mera aparición contigua de dos nombres iguales no permite concluir
que se hayan cancelado; se conservan sus referencias y se someten a
los oráculos de ejecución. Si el segundo factor es cero, por ejemplo
`capacidad - capacidad * 0`, sólo el primer uso influye realmente.
Otros cálculos algebraicos complejos no quedan demostrados sólo por el
análisis de fuente.
Esto incluye factores cero que anulan expresiones completas con
paréntesis, incluso anidadas: `0 * (capacidad + capacidad)` o
`(capacidad + capacidad) * (2 - 2)` no acreditan dos referencias
productivas. El cero conserva su significado con cualquier notación entera
válida de C++: `0x0`, `0b0`, `00u` o `0X0` también descartan factores
decorativos. Asimismo, `if (0x0)` no convierte una rama inalcanzable en
evidencia, mientras que `if (0x1)` no permite ignorar un retorno inmediato.
En expresiones lógicas, los sufijos `&& 0x0` y `|| 0b1` absorben el resultado
de sus respectivos grupos, sin transformar las referencias anteriores en
usos productivos. La comprobación conserva las referencias reales que
acompañen a una expresión neutra. Si una variable intermedia derivada de la constante
se sobrescribe con un cálculo que ya no depende de ella, ese resultado
descartado no acredita el `return`. Una asignación posterior que
vuelva a utilizar la constante sí puede establecer la dependencia.
El mínimo `minUses` se cuenta sobre las referencias útiles que llegan
al valor devuelto, directamente o a través de variables locales; una
operación auxiliar que no llega al resultado no incrementa el mínimo.
Tampoco se acreditan las referencias de un `return` situado directamente
en una rama `if (false)`, `if (0)` o `if constexpr (false)`, ni de
un `return` posterior a otro que termina definitivamente esa vía.
En condiciones constantes como `if (true)` o `if (1)` se comprueba
el retorno inmediato ejecutado; una guarda condicional de entrada
(por ejemplo, `if (piezas <= 0) return 0;`) no impide analizar el
cálculo posterior en la vía ordinaria. Cuando `if/else` devuelve
directamente en ambas ramas, las dos deben satisfacer `minUses`;
no basta con escribir la constante en una rama y dejar cifras fijas en
la otra. Si sólo hay retornos condicionales cuya cobertura no puede
probar este analizador acotado, no se acredita el requisito de fuente.
Esta detección es acotada:
no reemplaza la compilación, la ejecución ni las sondas independientes
que correspondan a cada actividad.

**Límites de la primera integración:** el análisis léxico de usos no
demuestra por sí solo que todas las operaciones sean correctas o que otra
parte del programa no fabrique una salida. Las prácticas que requieren
conservar otros archivos, ampliar baterías, demostrar dependencias
efectivas o comprobar funciones con entradas distintas deben añadir sus
sondas y restricciones pertinentes antes de recibir acreditación completa.
La ficha técnica sigue abierta hasta esa verificación.

Modificar por primera vez `compilerNamedRuleChecks` crea un objetivo
nuevo, con huella de recibo distinta de los logros históricos de salida.
Una vez publicado, el autor no puede retirarlo ni modificarlo
silenciosamente. El validador Python, el cargador Windows, el comprobador
de ejecución y las pruebas reales deben coincidir. Los archivos JSON los
integra Work o GPT Work cuando la capacidad técnica esté acreditada.

### Sonda independiente en editores C++ de archivo único

En un editor de un solo archivo, el alumno también puede modificar `main`
y fabricar las cinco líneas esperadas sin llamar a sus funciones. Una
declaración optativa `executionProbe` permite comprobar directamente esas
funciones con casos adicionales, sin confiar en la salida publicada:

```json
"executionProbe": {
  "harness": "",
  "header": "",
  "cases": [
    {"function": "precio_total", "input": 11, "expected": 77},
    {"function": "alcanza_minimo", "input": 4, "expected": 0},
    {"function": "alcanza_minimo", "input": 5, "expected": 1}
  ]
}
```

Los campos `harness` y `header` deben estar vacíos en esta modalidad;
el editor continúa teniendo `path` vacío y no utiliza CMake.
El motor compila el programa entregado para la comprobación visible y
ejecuta una segunda compilación de su código con un `main` privado
generado por el motor. El `main` original se renombra sólo en esa
segunda copia, sin modificar el borrador del alumno. La prueba llama
directamente a las funciones con los argumentos y esperados declarados
por Work. La ejecución debe terminar correctamente, conservar vacía la
salida estándar y emitir únicamente el testigo interno final en stderr,
después de la última comparación. Ese testigo distingue una sonda
completada de una función que ejecutó `exit(0)` antes de comprobar
todos los casos. Se verifica también la procedencia criptográfica del
código exacto presentado.

El nuevo requisito se incorpora a la huella del objetivo y no revalida
recibos anteriores de salida. La sonda debe cubrir todas las funciones
nombradas; se rechazan llamadas repetidas o nombres que inyecten código.
Un único intento correcto en el editor sigue bastando para avanzar.
No se exige al estudiante ejecutar manualmente cada caso adicional.

### Archivos adicionales autorizados en proyectos CMake

De manera predeterminada, sólo puede cambiar el archivo C++ identificado
por `path`. El resto de los archivos originales, incluido el cliente,
la batería de pruebas y `CMakeLists.txt`, debe conservarse literalmente.
Esto evita sustituir el comprobador por un programa que imprima la salida
esperada sin ejecutar los cálculos del estudiante.

Cuando una práctica pide también actualizar la cabecera y ampliar la
batería, el autor puede declarar `editablePaths` con rutas de archivos
existentes y diferentes de `path`:

```json
"editablePaths": [
  "componente/publico/envio/cajas.h",
  "pruebas/cajas_casos.cpp"
]
```

La ausencia de `editablePaths` equivale a una lista vacía. Una ruta
nueva, repetida, inexistente o ajena al proyecto es inválida. Estas
excepciones forman parte de la huella del objetivo; no alteran los IDs
históricos, pero un recibo obtenido con permisos diferentes no puede
acreditar el objetivo actualizado. Los nuevos archivos autorizados
todavía deben compilar, ejecutarse y cumplir el primer caso de salida.

### Sonda independiente cuando el comprobador es editable

Si `editablePaths` incluye un archivo C++ que puede reemplazar el
comprobador, **es obligatorio** incluir `executionProbe`. El ejecutor
genera un segundo `.cpp` de prueba sin copiar instrucciones del estudiante
y lo compila con MSVC usando la misma biblioteca del proyecto. El archivo
de pruebas original del alumno permanece intacto; sólo se sustituye una
copia en el directorio de ejecución aislado.

```json
"executionProbe": {
  "harness": "pruebas/cajas_casos.cpp",
  "header": "envio/cajas.h",
  "cases": [
    {"function": "cajas_para", "input": 6, "expected": 1},
    {"function": "cajas_para", "input": 7, "expected": 2},
    {"function": "cajas_para", "input": 19, "expected": 4},
    {"function": "llega_a_tiempo", "input": 4, "expected": 1},
    {"function": "llega_a_tiempo", "input": 5, "expected": 0}
  ]
}
```

La sonda debe sustituir el archivo de código fuente que participa en el
objetivo ejecutable CMake, referenciar una cabecera con ruta segura y cubrir
cada función solicitada con datos independientes. Las entradas y salidas
enteras son datos declarados por el currículo, no cálculos obtenidos de la
función evaluada. Un programa que imprime el resultado esperado pero
implementa mal las funciones no puede ganar el recibo nuevo. El testigo
final también se exige en esta modalidad, sin saltos de línea para no
confundir las convenciones LF y CRLF de los compiladores. El código
de salida cero por sí solo no acredita que se hayan ejecutado todas las
comparaciones; un testigo estático tampoco sustituye el aislamiento
frente a código deliberadamente hostil.

Para objetivos con `executionProbe`, la comprobación de fuente rechaza
referencias ejecutables a funciones capaces de finalizar anticipadamente
el proceso o el hilo, como `exit`, `_Exit`, `quick_exit`,
`ExitProcess` y `TerminateProcess`. También rechaza tomar su dirección
para invocarlas mediante un alias. La restricción se aplica a las
fuentes sometidas a la sonda, no a cualquier actividad C++; los
comentarios y literales de texto que mencionen esos nombres no
constituyen llamadas. Es una protección adicional contra falsificar
el testigo y terminar con código cero; no convierte al ejecutor en una
barrera de seguridad contra programas hostiles ni impide todas las
formas posibles de finalización indirecta.

Se conservan por separado el resultado público, el código original, la
compilación de la sonda, el programa/objetivo y las huellas de ambas
ejecuciones. La huella de acreditación de las sondas pasa a la versión
`v2`: un recibo anterior basado sólo en terminación cero no se
interpreta como prueba de finalización nueva. Las direcciones del
borrador y las identidades históricas del ejercicio no cambian. Los demás casos del `outputCheck` continúan siendo optativos;
la sonda del primer caso forma parte de su propio criterio de corrección.

Para los editores de un solo archivo, `path` y `editablePaths` deben
ser vacíos; la prueba sigue evaluando el código que el estudiante editó.

### Variaciones privadas de los acuerdos nombrados

Una constante puede aparecer dos veces en una fórmula y, aun así, dejar
una cifra relacionada escrita directamente. Con una capacidad de cuatro,
`(piezas + capacidad - capacidad / 4) / 4` y
`(piezas + 3) / (capacidad / 2 + capacidad / 2)` producen los resultados
esperados en el dominio de cero a veinte. Ambas expresiones contienen usos
reales de la constante. Contarlos no demuestra que el divisor y el ajuste
se deriven correctamente de ella cuando cambia el acuerdo.

Para comprobar esa relación, `executionProbe` admite el campo optativo
`constantVariations`. Cada variación identifica una de las funciones
declaradas en `rules`, propone otro valor para su constante y contiene
los resultados esperados con ese acuerdo. Por ejemplo, para la regla de
`cajas_para` cuyo valor original es cuatro:

```json
"executionProbe": {
  "harness": "pruebas/cajas_casos.cpp",
  "header": "envio/cajas.h",
  "cases": [
    {"function": "cajas_para", "input": 4, "expected": 1},
    {"function": "cajas_para", "input": 7, "expected": 2},
    {"function": "cajas_para", "input": 19, "expected": 5}
  ],
  "constantVariations": [
    {
      "function": "cajas_para",
      "value": 6,
      "cases": [
        {"function": "cajas_para", "input": 4, "expected": 1},
        {"function": "cajas_para", "input": 7, "expected": 2},
        {"function": "cajas_para", "input": 19, "expected": 4}
      ]
    }
  ]
}
```

Este fragmento es un ejemplo técnico de autoría. No modifica los JSON
existentes. En un editor de archivo único, `harness` y `header` permanecen
vacíos, como en su sonda independiente ordinaria. Los datos esperados los
declara el autor; el motor no los calcula llamando a la función evaluada.
Las variaciones utilizan el backend nativo MSVC y la acción de compilar
y ejecutar. No pueden compartir propietario con
`compilerCmakeGraphChecks`; el cargador rechaza esa combinación.

El motor primero comprueba el requisito de fuente y la sonda con el valor
original. Después localiza la única constante local que cumple el valor,
el propósito y los usos declarados de la función seleccionada. En una
copia privada sustituye únicamente el intervalo del inicializador por el
valor alternativo y compila otra sonda con los casos de esa variación.
La selección utiliza los tokens y sus posiciones en el código original,
sin sustituir globalmente números ni exigir un nombre literal único.
Se admiten los inicializadores enteros equivalentes que reconoce el
analizador, incluidos `0x4`, `04` y `2 + 2`, y las variables intermedias
permitidas por el requisito de fuente. El significado de `minUses` no
cambia.

Cada variación parte de nuevo del programa original del intento. No
acumula los cambios de las variaciones anteriores ni modifica el borrador,
la batería del alumno, sus esperados o los archivos del proyecto guardado.
La batería original, si se declara mediante `batteryCheck`, se comprueba
por separado con el acuerdo original y conserva sus propias pruebas de
sensibilidad. El nuevo requisito comprueba el cálculo al cambiar un
acuerdo; `batteryCheck` comprueba las comparaciones del alumno.

En el ejemplo, al variar cuatro por seis, las dos fórmulas defectuosas
anteriores producen respectivamente tres y uno para siete piezas, cuando
se necesitan dos cajas. El caso de diecinueve piezas, que pasa de cinco
a cuatro cajas, demuestra además que la variación incluye un resultado
que debe cambiar respecto a la sonda base. Una solución equivalente que
derive correctamente las cantidades de la capacidad conserva esos
resultados y puede obtener el acierto.

El contrato de autoría se valida con estas condiciones:

- `constantVariations` contiene entre una y ocho variaciones. Cada objeto
  tiene exactamente `function`, `value` y `cases`. La función selecciona
  su única regla ya declarada; no se admite otro nombre, ruta o selector
  de constante en este campo.
- `value` difiere del valor original de esa regla. No se repite un par
  función y valor alternativo. Puede variarse un subconjunto de las reglas;
  no es obligatorio declarar una variación para cada función.
- Cada `cases` contiene entre uno y treinta y dos objetos con exactamente
  `function`, `input` y `expected`. Sus pares función y entrada son únicos
  y cubren todas las funciones declaradas en `rules`, incluida la
  función cuyo acuerdo cambia.
- Al menos un caso de la función seleccionada comparte la entrada con
  `executionProbe.cases` y declara un esperado distinto del original.
  Esto evita que una variación sin ningún resultado diferente acredite
  sensibilidad al acuerdo.
- Cada otra función tiene al menos un caso compartido con la sonda base.
  Todos sus casos compartidos conservan el esperado original, para
  contrastar que cambiar un acuerdo no cambie las reglas independientes.
- Los nuevos campos numéricos son números JSON de valor entero dentro
  del intervalo de `int` de 32 bits. No admiten booleanos, fracciones ni
  valores fuera de ese intervalo. Las grafías JSON integrales, como
  `6` y `6.0`, tienen el mismo valor en esta extensión. Un resultado
  `bool` se expresa como el entero cero o uno.

El cargador valida la declaración y su relación con las reglas y casos
base. No exige que el código inicial ya contenga la constante correcta:
introducirla puede ser precisamente el objetivo de la práctica. La
localización de su inicializador se realiza sobre el código presentado
por el alumno al intentar obtener el acierto.

Todas las compilaciones y ejecuciones privadas deben completarse y
cumplir sus casos. Cada fase conserva su procedencia, la fuente exacta,
su identidad y el testigo final de la sonda. La acreditación queda ligada
al contrato, al programa y a la revisión de la solicitud. Un resultado
ausente, de otra variante o de un intento anterior no sustituye esa
evidencia. Se mantienen las restricciones frente a terminación anticipada,
macros y dependencias del entorno de la sonda; estas comprobaciones
acotadas no constituyen una barrera frente a código nativo hostil ni una
demostración de la fórmula para cualquier entrada posible.

Si la sonda original pasa y una variación no acredita el requisito, la
retroalimentación identifica la función afectada y pide revisar la
relación entre su constante y las cantidades derivadas. No muestra los
valores ni esperados privados y no confunde la falta de esa evidencia
con un acierto del requisito nuevo.

La extensión añade una huella propia al objetivo sólo cuando está
presente. Su ausencia conserva exactamente los criterios y el alcance
de los contratos históricos. Los IDs, mapas, claves de borrador y
respuestas del editor permanecen iguales, y un recibo anterior no concede
el nuevo requisito. La declaración completa sigue siendo inmutable una
vez publicada: las variaciones se incorporan al autorizar inicialmente
ese contrato, sin habilitar una excepción para alterar silenciosamente
uno ya publicado.

La sonda base y sus variaciones forman parte de la primera comprobación
correcta del primer caso del editor. Los demás casos visibles de
`outputCheck` conservan su carácter optativo y no pasan a ser requisitos
adicionales para avanzar.

### Batería del estudiante con comparaciones sensibles a errores

Comprobar las funciones con un `main` privado no demuestra que el
comprobador escrito por el estudiante realice sus propias comparaciones.
Las funciones pueden ser correctas y, aun así, su `main` puede limitarse
a imprimir el informe esperado. Cuando el objetivo de la práctica incluye
conservar o construir una batería real, `executionProbe` admite el campo
optativo `batteryCheck`:

```json
"batteryCheck": {
  "cases": [
    {"function": "cajas_para", "input": 0, "expected": 0},
    {"function": "cajas_para", "input": 4, "expected": 1},
    {"function": "cajas_para", "input": 8, "expected": 2},
    {"function": "cajas_para", "input": 20, "expected": 4},
    {"function": "cajas_para", "input": 1, "expected": 1},
    {"function": "cajas_para", "input": 5, "expected": 1},
    {"function": "cajas_para", "input": 17, "expected": 3},
    {"function": "cajas_para", "input": 6, "expected": 1},
    {"function": "cajas_para", "input": 7, "expected": 2},
    {"function": "llega_a_tiempo", "input": 4, "expected": 1},
    {"function": "llega_a_tiempo", "input": 5, "expected": 0}
  ],
  "failureExitCode": 1
}
```

Este objeto se sitúa **dentro de `executionProbe`**. No se declara como
otro campo de `compilerNamedRuleChecks`, ni en el bloque `cpp_compiler`.
Es un ejemplo de autoría; incorporar el mecanismo al motor no modifica
automáticamente los JSON curriculares existentes.

Esta batería utiliza el backend nativo MSVC y la acción de compilar y
ejecutar (`compile_run`). La entrada inicial del editor y la entrada del
primer caso de `outputCheck` deben estar vacías: las llamadas se realizan
desde el propio comprobador, sin datos por stdin. Ese primer caso espera
código de salida cero y, si declara un `target`, debe ser el objetivo
ejecutable original del editor CMake. Los ensayos posteriores
del editor pueden conservar entradas opcionales. Un mismo editor no puede
combinar `batteryCheck` con `compilerCmakeGraphChecks`; el cargador rechaza
esa combinación antes de presentar una actividad que no podría acreditarse.

Las dos listas de casos tienen propósitos separados:

| Lista | Qué exige |
| --- | --- |
| `executionProbe.cases` | Las funciones entregadas calculan correctamente los datos de la sonda privada. |
| `executionProbe.batteryCheck.cases` | El comprobador del estudiante llama a las funciones con los datos requeridos, utiliza sus resultados y detecta una discrepancia en cada comparación. |

No se exige que ambas listas coincidan.
Cuando un par función/entrada aparece en ambas listas, su esperado debe
coincidir para que los dos requisitos puedan cumplirse a la vez.
En el ejemplo, la sonda privada
puede conservar la entrada `19` y la batería del estudiante exige sus
nueve casos de piezas y dos de plazo. La entrada privada `19` no se añade
por ello a la batería que debe escribir el estudiante. Para los editores
de precio y mínimo, la batería puede exigir ambas funciones con las
entradas `0`, `4`, `5`, `6` y `10`, mientras la sonda privada comprueba
también el precio con `11`.

El motor conserva el comprobador original del intento y prepara
ejecuciones aisladas de control y de sensibilidad. En la ejecución de
control registra las llamadas y comprueba los resultados requeridos.
Cada par declarado debe llamarse exactamente una vez; el orden de las
llamadas puede variar. Se aplica la misma cobertura en las ejecuciones
de sensibilidad.
En cada ejecución de sensibilidad altera el resultado de un solo par
función/entrada, usando un valor distinto del esperado. La batería debe
detectar el cambio, terminar con `failureExitCode` y continuar hasta
comprobar los demás casos. Esta última condición impide que un primer
fallo o una cadena con cortocircuito omitan el resto de la batería.
Cada alteración debe producir un informe visible no vacío, distinto del
control y de los informes de las demás alteraciones, para poder
identificar qué caso falló. Los testigos privados de instrumentación
no cuentan como parte de ese informe.
Los esperados se declaran por separado de las funciones evaluadas;
calcular el esperado llamando a esa misma función no demuestra
independencia.

Un informe fijo, llamadas cuyos resultados se descartan o una batería
que elimina un caso requerido no acreditan este requisito. Se conservan
además las comprobaciones de constantes nombradas, la sonda privada, el
primer caso público, los permisos de archivos y las restricciones de
cabeceras. No se exige un nombre concreto para las funciones auxiliares
del comprobador.

`batteryCheck.cases` contiene entre 1 y 32 objetos con exactamente
`function`, `input` y `expected`. Cada par función/entrada debe ser único;
las funciones deben estar declaradas en `rules` y todas deben tener
algún caso de batería. Los datos son enteros con signo de 32 bits.
Para una función que devuelve `bool`, `expected` utiliza el entero
`0` o `1`, no un booleano JSON. `failureExitCode` es optativo, vale `1`
cuando falta y admite un entero de `1` a `4294967295`.

La instrumentación admite una definición global por función con firma
`int nombre(int parametro)` o `bool nombre(int parametro)`. El tipo se
obtiene del código real, sin un campo de tipos en JSON. Esta gramática
acotada no admite sobrecargas, parámetros por referencia, `static`,
`inline`, `constexpr`, atributos ni `noexcept` en esa firma. Las
declaraciones de la misma función dentro de ese archivo, si existen,
deben coincidir con su firma. El cuerpo puede utilizar las
implementaciones válidas que admita el análisis de reglas nombradas;
no se conserva una solución de referencia como única respuesta.

El archivo fuente se deriva de `path`; el comprobador y la cabecera
proceden de `executionProbe.harness` y `executionProbe.header`. No existe
un `sourcePath` adicional que pueda desviar la comprobación. En un editor
de archivo único se instrumenta su programa; en CMake se instrumentan
el archivo de funciones y el comprobador declarado. El programa de
archivo único y el comprobador CMake contienen exactamente un `main`
global; el archivo de funciones CMake no contiene `main`. Se rechazan fuentes
que dependan de marcadores del entorno, terminen anticipadamente el
proceso o intenten usar los nombres y testigos privados de esta prueba.
Los límites léxicos de la instrumentación no equivalen a una barrera
de seguridad frente a código nativo deliberadamente hostil.

La especificación y las ejecuciones pertenecen al intento concreto y a
la revisión exacta de sus fuentes. El requisito forma parte de la huella
del objetivo: un recibo obtenido sólo con la sonda de funciones no
acredita posteriormente una batería. Cuando `batteryCheck` falta, se
conservan los criterios y la huella anteriores, sin añadir fases ni
tokens de ausencia. Los IDs, `stateLayout` y las direcciones de los
borradores no cambian. Una declaración publicada conserva la misma
regla de inmutabilidad: no puede retirarse o relajarse silenciosamente.

Estas ejecuciones automáticas forman parte del criterio del primer caso
del editor. Los demás casos de `outputCheck` siguen siendo ensayos
optativos; no se convierten en pasos obligatorios de navegación.

### Cabeceras de edición limitada a comentarios

Las actividades que piden corregir una descripción, sin alterar firmas ni
la interfaz C++, pueden añadir `commentOnlyPaths`:

```json
"commentOnlyPaths": ["componente/publico/envio/cajas.h"]
```

Las rutas deben estar incluidas en `editablePaths` y ser cabeceras `.h`,
`.hpp` o `.hxx` preexistentes. Se admite corregir un comentario como
«4 plazas» por «6 plazas», pero no se admite cambiar una declaración,
introducir `#define`, redirigir una llamada ni modificar literales. El
comprobador léxico compara el código, incluyendo directivas, y descarta
únicamente las diferencias de comentarios. Rechaza cambios que involucren
macros sensibles a las líneas del código.

El alcance y los permisos forman parte de la identidad del objetivo
evaluado: no se reutilizan aciertos de otra revisión. Es una capacidad
general, no una modificación automática del material histórico.

## Comprobación de la estructura CMake generada (raíz de la lección)

Los editores históricos pueden recibir un objetivo estructural complementario sin
modificar su `cpp_compiler`, los casos `outputCheck` ni sus direcciones. Work declara
`compilerCmakeGraphChecks` en la raíz de una lección española con `stateLayout`
versión 2. Un ejemplo, referido a las rutas originales de un proyecto educativo, es:

```json
"compilerCmakeGraphChecks": [
  {
    "activity": "block_0004_0004",
    "project": "Preparacion",
    "targets": [
      {
        "name": "revisar",
        "type": "EXECUTABLE",
        "sources": ["main.cpp"],
        "exactSources": true,
        "linksTo": ["faltantes"]
      },
      {
        "name": "faltantes",
        "type": "STATIC_LIBRARY",
        "sources": ["faltantes.cpp"],
        "exactSources": true
      }
    ]
  }
]
```

El ejemplo muestra un contrato técnico disponible, no una declaración añadida
automáticamente a la lección. Los nombres de objetivos y las rutas deben existir
en la autoría real; la biblioteca se identifica por su objetivo y su artefacto
generado, no por una coincidencia de nombres dentro de `CMakeLists.txt`.

El motor consulta **CMake File API codemodel v2** de la configuración realmente
generada por CMake. Separa fuentes compiladas, tipo de objetivo, dependencia de
construcción y fragmentos de enlace. `add_dependencies()` por sí solo no
acredita `target_link_libraries()`. Una ejecución que coincida en stdout tampoco
acredita esta organización si la estructura declarada no coincide. Sólo una
compilación y ejecución de la revisión actual pueden producir evidencia de
la nueva rúbrica. El archivo histórico, el editor y las respuestas antiguas
permanecen intactos; un acierto anterior de salida no satisface automáticamente
la nueva comprobación. Para avanzar sigue bastando el primer caso correcto
que cumpla todos sus criterios vigentes. Los demás casos permanecen opcionales.

La API de ejecución Windows admite tanto `compile_run` como la secuencia
`configure` → `build` → `execute` para esta comprobación estructural. Configurar
o construir por separado no concede el logro. Al ejecutar el objetivo ya
construido, el motor comprueba que sus fuentes, configuración, objetivo y
artefacto siguen vigentes y evalúa el grafo de esa misma revisión junto con el
resultado nuevo. Una consola restaurada no crea otra evidencia y un cambio de
fuentes u opciones exige reconstruir. El lector utiliza su acción combinada
«Compilar, ejecutar y comprobar»; esta compatibilidad del ejecutor no añade ni
cambia controles de la interfaz.

`project` es opcional. Cada objetivo requiere `name` y `type`
(`EXECUTABLE` o `STATIC_LIBRARY`); `sources`, `linksTo` y
`exactSources` son opcionales. Las fuentes declaradas son rutas originales
de `projectFiles`; `exactSources` exige que no existan fuentes de
compilación adicionales. `linksTo` referencia objetivos de biblioteca
estática también declarados, y se acredita mediante dependencia y enlace del
artefacto real. `minimumCmakeVersion` es una propiedad opcional del
propietario, por ejemplo `"minimumCmakeVersion": "3.20"`. Comprueba la versión
mínima declarada por `cmake_minimum_required(VERSION)` en el directorio raíz,
según el dato `minimumCMakeVersion.string` de **CMake File API codemodel v2**.
Por defecto se comparan componentes numéricos exactos: `3.20` y `3.20.0`
son equivalentes, pero `3.19` y `3.21` no. La propiedad opcional
`"minimumCmakeVersionAtLeast": true`, que requiere `minimumCmakeVersion`,
acepta una versión mínima declarada igual o posterior: con `3.20`, admite
`3.21` y rechaza `3.19`. Omitirla o declararla `false` conserva la comparación
y los recibos históricos. No se deduce esta versión de la instalación de CMake,
ni se sustituye por una búsqueda textual del código fuente. Para exigir los requisitos explícitos de los objetivos, un elemento de
`targets` puede declarar `"explicitCxx20": true` y
`"explicitCxxExtensionsOff": true`. Estas propiedades optativas obligan a
comprobar que **CMake ejecutó** una declaración de C++20 o posterior y una desactivación
de extensiones sobre el objetivo, además de construirlo y ejecutar su primer
caso correctamente. El perfil Windows puede aportar C++23 y extensiones
desactivadas, pero eso **no** satisface las propiedades del alumno.

Cada objetivo admite también `"requireExplicitMsvcWarnings": true`. Exige una
opción de avisos MSVC declarada por ese objetivo y un nivel no nulo efectivo
en su construcción. Acepta `/W1` a `/W4` o `/Wall`; una opción posterior `/W0`
o `/w` no puede anularlos. Omitir la propiedad o declararla `false` conserva
los contratos y recibos anteriores. `authoredCompileOptions` mantiene su
comprobación literal independiente para las rúbricas que la soliciten.

En la configuración autenticada, el motor guarda una traza JSON privada de
CMake limitada a los archivos CMake originales del proyecto. Sólo acepta
órdenes ejecutadas y argumentos expandidos: `target_compile_features(objetivo
PRIVATE cxx_std_20)`, `set_target_properties(objetivo PROPERTIES
CXX_STANDARD 20)` y `set_property(TARGET objetivo PROPERTY
CXX_STANDARD 20)` son formas equivalentes para el mínimo. También se
admiten las declaraciones explícitas de C++23 y C++26 cuando el compilador
permite construirlas. El motor distingue la lista `COMPILE_FEATURES` de la
selección `CXX_STANDARD`: una característica `cxx_std_20` activa sigue
exigiendo al menos C++20 aunque después se seleccione la propiedad 17; una
propiedad 23 también satisface el mínimo. Bajar una propiedad 20 a 17 sin
una característica suficiente, o retirar esa característica, no acredita
el requisito. Los valores heredados del perfil siguen sin contar como una
declaración del alumno. Para extensiones se admiten `set_target_properties(... PROPERTIES CXX_EXTENSIONS OFF)` y
`set_property(TARGET ... PROPERTY CXX_EXTENSIONS OFF)`, con los valores
falsos admitidos por CMake. Una orden posterior que active las extensiones
invalida el requisito. Las rutas, fuentes y enlaces siguen contrastándose
mediante File API. No se evalúan solamente las cadenas impresas por el alumno,
ni se concede crédito desde una configuración anterior sin traza autenticada.

La traza está limitada al proyecto y a los objetivos declarados. Esta primera
capacidad no certifica todavía asignaciones por ámbito de directorio,
propagación por interfaces de otros objetivos ni cualquier macro dinámica que
no pueda atribuirse con certeza al objetivo. En esos casos se necesita una
comprobación adicional en lugar de conceder un acierto supuesto.

Cada contrato ya publicado permanece inmutable. Es posible añadir
nuevos propietarios al final de la declaración mediante una transición
autenticada, sin modificar ni reordenar los anteriores.

### Reparación de una expresión con las demás fuentes conservadas

Una práctica que permite reparar sólo una expresión puede añadir
`replaceExpression` a `sourceIntegrity`. La declaración pertenece a
`compilerCmakeGraphChecks`, en la raíz de la lección:

```json
"sourceIntegrity": {
  "preserveNonCmake": true,
  "replaceExpression": {
    "path": "src/ajuste.cpp",
    "original": "valor - superior"
  }
}
```

`path` debe identificar un archivo C++ original declarado entre las fuentes de
alguno de los objetivos comprobados. `original` selecciona una única expresión
del código publicado. El cargador obtiene el archivo completo del propietario;
la declaración no proporciona una copia alternativa de ese archivo ni una
solución para el estudiante. La coincidencia se autentica mediante los tokens
de C++ y sus posiciones: una aparición dentro de un comentario, literal o
directiva no selecciona una zona editable. Un selector ausente o ambiguo
invalida la declaración.

El estudiante puede sustituir esa expresión y conserva exactamente los bytes
anteriores y posteriores. Los demás archivos no CMake mantienen sus hashes
originales, y el proyecto conserva su conjunto de rutas. `replaceExpression`
y la retirada de prefijo `removePrefix` son alternativas excluyentes. Los
archivos CMake siguen admitiendo las modificaciones necesarias para cumplir
la rúbrica de construcción, incluidas las opciones apropiadas para MSVC.

La sustitución admite expresiones escalares con operadores aritméticos,
relacionales, lógicos y de bits, paréntesis y el operador condicional. Así,
`superior - valor` y `-(valor - superior)` pueden reparar la misma resta;
`dato >= 0 && dato <= 20` y `!(dato < 0 || dato > 20)` pueden reparar la
comparación encadenada. La ejecución contrasta el resultado; el texto de la
solución no se fija de antemano. Este contrato acotado excluye llamadas,
asignaciones, incrementos, comas, accesos a miembros y otras instrucciones que
podrían imprimir un informe o terminar el proceso desde la expresión. Tampoco
permite introducir directivas, llaves, puntos y coma ni escapar de la zona
declarada. No pretende admitir todas las expresiones posibles del lenguaje.

El selector admite entre 1 y 512 bytes; la sustitución, entre 1 y 4096. La
fuente completa se limita a 4 MiB. La ruta relativa admite hasta 256 caracteres
ASCII y una extensión C++ compatible. Se rechazan los nombres reservados de
Windows, NUL, continuaciones físicas de línea, macros locales y consultas que
dependan de la posición del código. El anidamiento se limita a 128 niveles.
Al comprobar la candidata se vuelve a analizar el archivo completo,
para impedir que un comentario o un literal oculte el contexto protegido.

La restricción añade una parte propia a la huella del objetivo únicamente
cuando está presente. Omitirla conserva la identidad y el comportamiento de las
declaraciones anteriores. Una declaración ya publicada sigue siendo inmutable;
las fuentes, los mapas, los borradores y los recibos previos se conservan, pero
un acierto obtenido sin esta protección no acredita automáticamente el objetivo
nuevo.

Cuando `stateLayout.compilerCaseRevisions[].client` adapta un comprobador,
la reconstrucción de cada etapa histórica deriva `sourceIntegrity` y la
expresión protegida de los archivos autenticados de esa misma etapa. No
reutiliza las huellas del cliente más reciente para buscar sus recibos
anteriores. Esta conservación permite reconocer la acreditación que cada
revisión obtuvo; no concede automáticamente requisitos incorporados después.

Las capturas nuevas de comprobaciones CMake incluyen además una huella de la
solicitud, los archivos, la configuración, el contrato y la revisión. La
finalización exige que esa huella corresponda a la solicitud que se acredita.
Una captura positiva de otra revisión o de otros archivos no puede trasladar su
veredicto del grafo, aunque coincidan la entrada y la salida visibles.

Las regresiones de U3L7 y U3L10 se preparan sobre sus propietarios originales
únicamente en memoria. Comparan reparaciones equivalentes con comprobadores que
imprimen informes fijos mientras conservan la biblioteca defectuosa. También
comprueban alteraciones de esperados, ediciones fuera de la expresión, capturas
prestadas y conservación del estado. El primer caso correcto que cumpla la
rúbrica basta para avanzar; los demás casos permanecen opcionales. Registrar
el recorrido de la ejecución defectuosa anterior es un criterio distinto de
esta protección de fuentes y no se deduce de una reparación correcta.

### Observación automática del defecto antes de reparar

Una práctica de reparación puede declarar `initialObservation` junto a
`sourceIntegrity.replaceExpression`, dentro del mismo propietario de
`compilerCmakeGraphChecks`. El contrato identifica el comportamiento del
programa original que el estudiante debe observar antes de corregirlo:

```json
"initialObservation": {
  "executable": "comprobar_cantidad",
  "input": "",
  "output": "dato=-1 esperado=0 obtenido=1 ERROR\ndato=0 esperado=1 obtenido=1 OK\ndato=20 esperado=1 obtenido=1 OK\ndato=21 esperado=0 obtenido=1 ERROR\nBateria: ERROR\n",
  "exitCode": 1,
  "warning": {
    "code": "C4804",
    "path": "cantidad.cpp"
  }
}
```

El ejemplo corresponde a la expresión original `(0 <= dato) <= 20` y al
comprobador histórico de cuatro valores. El autor declara el comportamiento
que debe observarse a partir del programa y de la consigna. La ejecución
capturada se contrasta con esa declaración; no proporciona sus propios valores
esperados. Una revisión compatible del comprobador debe declarar la entrada y
salida del caso pertinente, conservando los informes conjuntos históricos y
la obligatoriedad exclusiva del primer caso correcto tras reparar.

`executable` identifica un objetivo `EXECUTABLE` del grafo y un ejecutable
utilizado por los casos existentes de `outputCheck`; un caso sin ejecutable
explícito utiliza el objetivo inicial del editor. `input` y `output` admiten
como máximo 65536 bytes UTF-8 cada uno, sin NUL. Ambos campos son obligatorios,
incluso cuando están vacíos. `exitCode` es un entero entre 1 y 255: representa
un proceso que termina normalmente y comunica el defecto. Un fallo de
configuración, una opción inválida, un timeout, una cancelación, un límite de
salida o una terminación anormal no cumplen esa observación. La salida se
compara de forma exacta, admitiendo únicamente la conversión habitual de LF a
CRLF de la consola Windows; el canal de errores de ejecución debe quedar vacío.

`warning` es opcional. Cuando está presente, sólo contiene `code` y `path`.
El código tiene la forma `C4xxx` de MSVC y el archivo debe ser el protegido
por `replaceExpression`. La ubicación se obtiene de la expresión auténtica
del archivo original; no se declaran números de línea alternativos. El objetivo
que compila ese archivo debe exigir avisos mediante
`requireExplicitMsvcWarnings: true` o una opción explícita `/W1`, `/W2`, `/W3`,
`/W4` o `/Wall` —también se admiten sus variantes con `-`—. Durante la ejecución
se comprueba que el nivel efectivo de ese objetivo sea válido y que la
construcción haya emitido el aviso solicitado en la expresión. Un aviso sólo
en el ejecutable consumidor, un código citado en otro diagnóstico o texto
impreso por el programa no sustituyen esa evidencia.

Este recorrido tiene un único contrato de ejecución: `initialObservation` no
se combina en el mismo propietario con otros comprobadores privados, reglas
nombradas ni requisitos de Historial. Los demás editores de la lección pueden
conservar sus contratos independientes. El cargador y el validador rechazan
las combinaciones que el ejecutor no puede acreditar.

La app registra automáticamente la construcción pertinente y la ejecución
defectuosa. Admite «Compilar y ejecutar» y las acciones separadas «Construir» y
«Ejecutar»: el aviso de construcción se vincula al mismo proyecto,
configuración y artefacto que se ejecuta después. La observación exige que
todos los archivos no CMake sean los originales del propietario efectivo,
incluida la expresión todavía defectuosa. Los archivos CMake se contrastan
mediante el grafo y las opciones reales del proyecto.

El ejecutor coteja también los archivos materializados con la solicitud al
terminar la configuración y la construcción, y antes de ejecutar o reutilizar
el programa. Si una orden CMake cambia una fuente proporcionada por el editor,
el resultado no acredita esa solicitud: se informa del archivo y de la fase
y se invalida únicamente la disponibilidad de la caché generada. Los archivos,
borradores y recibos previos se conservan. Una nueva configuración y construcción
con los bytes realmente presentados permite continuar sin borrar el proyecto.
Los archivos generados adicionales siguen admitidos. Esta comprobación detecta
las discrepancias presentes en esos momentos; no constituye aislamiento de
órdenes CMake arbitrarias que alteren y restauren archivos dentro de una fase.

El perfil CMake de Windows conserva la compilación incremental de Ninja y
utiliza `/INCREMENTAL:NO` al enlazar los programas. Mantiene la información
de depuración y evita reutilizar el estado interno de un enlace anterior
después de reparar una biblioteca. Esta opción realiza un enlace completo;
no implica limpiar ni recompilar todos los objetos. Véase la
[documentación de `/INCREMENTAL` de Microsoft](https://learn.microsoft.com/en-us/cpp/build/reference/incremental-link-incrementally?view=msvc-170).

La configuración generada guarda `linkProfile: "msvc-full-link-v1"`. Una
caché anterior necesita configurar de nuevo antes de construir o ejecutar;
«Compilar y ejecutar» realiza esa actualización en el mismo workspace.
El marcador pertenece a la caché, no al contenido curricular ni al progreso.
No cambia las huellas de proyecto, los recibos de logros, los prerrequisitos
ya observados, los borradores ni las ejecuciones anteriores. Tras actualizar,
un proyecto sin modificaciones sigue reutilizando sus objetos y ejecutable.

El recibo de observación se conserva al editar la expresión y al reabrir la
lección. Antes de iniciar la ejecución reparada, la solicitud captura su
prerrequisito; una observación posterior no convierte retroactivamente una
ejecución anterior en logro. La primera comprobación correcta del primer caso
declarado obtiene la acreditación cuando cumple el contrato y tiene esa
procedencia. Los ensayos adicionales continúan siendo opcionales. El recorrido
no requiere acciones de Historial.

Omitir `initialObservation` conserva los objetivos anteriores. Añadirlo crea
una exigencia propia y no atribuye una observación a los recibos antiguos de
salida o de conservación de fuentes. Como las demás declaraciones de grafo,
una vez publicado el contrato no puede eliminarse, sustituirse ni debilitarse
silenciosamente. Las pruebas de autoría preparan estas declaraciones sobre
los dos propietarios originales de U3L7 y U3L10 sólo en memoria; no activan
campos ni reescriben contenidos curriculares.

### Opciones de compilación declaradas y efectivas por objetivo

Los ejercicios que enseñan avisos del compilador pueden exigir opciones
concretas mediante `"authoredCompileOptions"`, dentro de cada objetivo
de `compilerCmakeGraphChecks`:

```json
{
  "name": "cantidades",
  "type": "STATIC_LIBRARY",
  "sources": ["cantidad.cpp"],
  "exactSources": true,
  "authoredCompileOptions": ["/W4", "/permissive-"]
}
```

La lista acepta entre una y ocho opciones distintas y sencillas, iniciadas
con `/` o `-`, sin expresiones arbitrarias ni instrucciones incrustadas.
Este contrato se añade sólo al crear la declaración estructural del
propietario: una rúbrica ya publicada no se puede reescribir para exigir
nuevas opciones a aciertos históricos.

La evidencia consta de **dos comprobaciones independientes**. La traza
privada de CMake debe mostrar que el alumno ejecutó
`target_compile_options(cantidades PRIVATE /W4 /permissive-)`, u otra
declaración equivalente `PRIVATE`/`PUBLIC` del objetivo correcto. Además,
los fragmentos de compilación generados por **CMake File API codemodel v2**
deben contener esas opciones para el mismo objetivo. Recibir `/W4` desde
el perfil Windows, declararlo en otro objetivo o escribir una cadena sin
ejecutar no satisface el contrato. Tampoco sirve
`target_compile_options(cantidades INTERFACE /W4 /permissive-)` para exigir
opciones de **compilación de la propia biblioteca**: `INTERFACE` transmite
requisitos a consumidores, pero no los aplica a `cantidades`. En cambio,
`PUBLIC` sí puede ser válido, porque se aplica tanto a la biblioteca como
a los consumidores.

La segunda comprobación rechaza también la simulación de una declaración
correcta cuyo efecto se borra posteriormente, por ejemplo, con
`set_property(TARGET cantidades PROPERTY COMPILE_OPTIONS "")`. Aunque la
traza documente la llamada original y las salidas sigan siendo correctas,
las opciones no llegaron a la compilación del objetivo. Cada contraejemplo
utiliza un proyecto aislado y conserva las fuentes y los recibos históricos.
Los argumentos dinámicos que no puedan atribuirse de forma segura no
conceden crédito.

Para enseñar las mismas intenciones con compiladores distintos, CMake puede
seleccionar opciones apropiadas en sus ramas de configuración:

```cmake
if(MSVC)
  target_compile_options(cantidades PRIVATE /W4 /permissive-)
else()
  target_compile_options(cantidades PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

Esto verifica que se **declararon y recibieron las opciones**, pero no
sustituye el diagnóstico realmente emitido ni demuestra por sí solo que el
programa maneje bien los casos límite. U3L7 exige observar la construcción
defectuosa, reparar la condición y ejecutar la batería original; U3L10
comprueba también la biblioteca enlazada, los once casos y la conservación
de los programas. El éxito se acredita sólo con las evidencias exigidas por
cada objetivo, no mediante una mera salida coincidente.

### Opción binaria con construcción independiente por defecto

Para las prácticas que seleccionan una biblioteca con una opción booleana, el
contrato admite `optionSelection` dentro del mismo propietario del grafo:

```json
"optionSelection": {
  "name": "TARIFA_ESPECIAL",
  "default": "OFF",
  "target": "tarifas",
  "offSource": "normal.cpp",
  "offStdout": "0:6:15"
}
```

El editor original configura `TARIFA_ESPECIAL=ON`. La rúbrica exige que la
biblioteca del grafo original utilice `especial.cpp` con `exactSources` y que
el estudiante declare la opción `OFF` por defecto en sus propias órdenes
CMake ejecutadas. Además, el motor compila y ejecuta una **segunda copia
aislada del mismo proyecto**, sin el override `-DTARIFA_ESPECIAL=ON`. Su
biblioteca debe compilar exactamente `normal.cpp` y conservar el resto de
los objetivos, enlaces, versión mínima, reglas C++ y salida `0:6:15`.
Una coincidencia del resultado `ON`, una opción ficticia o una selección
fijada siempre a la implementación especial no acreditan la construcción.

La ejecución adicional utiliza un espacio de configuración y huella propia,
conserva los proyectos y borradores del estudiante y nunca modifica la
opción seleccionada en el editor. También se construye y ejecuta cuando la acción
pública es `execute`: el artefacto `ON` ya construido no sustituye la
construcción privada `OFF`. Si la compilación o ejecución de la configuración
`OFF` falla, falta evidencia y no se concede el logro. El
primer caso correcto del editor sigue siendo el único obligatorio; esta
segunda ejecución es la comprobación interna del mismo objetivo, no un
segundo caso que el estudiante deba activar manualmente.

### Rutas públicas y privadas de encabezados

En los proyectos U2L4, una biblioteca estática puede declarar
`"publicIncludeRoots": ["include"]` y
`"privateIncludeRoots": ["interno"]` dentro de su objetivo en
`compilerCmakeGraphChecks`. Las rutas han de contener archivos de cabecera
originales del editor, no rutas externas o generadas. El objetivo requiere un
consumidor ejecutable enlazado explícitamente.

El motor utiliza las rutas C++ realmente publicadas por **CMake File API**
en los objetivos compilados, además de la traza privada de las órdenes
`target_include_directories` ejecutadas en los archivos CMake del alumno.
Para `PUBLIC`, exige una declaración pública, la ruta efectiva en la
biblioteca y su propagación al consumidor enlazado. Para `PRIVATE`, exige
la declaración privada y la ruta en la biblioteca, sin permitir que llegue
al consumidor. Una búsqueda `PRIVATE` duplicada manualmente en el cliente,
o la publicación de `interno`, no se acreditan aunque la salida coincida.

La comprobación admite rutas relativas al archivo CMake y rutas absolutas
equivalentes de la fuente. Los valores dinámicos no atribuibles con certeza
al objetivo no conceden crédito por mera coincidencia.

Para conservar también los archivos C++ originales, el propietario puede
declarar `"sourceIntegrity": {"preserveNonCmake": true}`. La raíz
complementaria fija la huella SHA-256 de cada archivo no CMake del editor,
sin impedir que el alumno repare `CMakeLists.txt`. En la actividad cuyo
enunciado solicita **retirar únicamente una inclusión experimental**, es
posible declarar una eliminación precisa de prefijo:

```json
"sourceIntegrity": {
  "preserveNonCmake": true,
  "removePrefix": {
    "path": "app/main.cpp",
    "text": "#include \"ajuste.h\"\n"
  }
}
```

El editor continúa mostrando y guardando su código inicial original. El
nuevo objetivo acepta solamente el contenido esperado tras esa eliminación,
más los demás archivos sin cambios. Un programa con salida idéntica pero
fuentes modificadas fuera de ese permiso no obtiene la acreditación. Los nuevos objetivos
se incorporan en la raíz de la lección sin cambiar los proyectos, direcciones,
borradores o recibos históricos. El primer caso correcto continúa siendo
suficiente para avanzar, siempre que incluya la evidencia estructural.

### Biblioteca definida en un subdirectorio CMake original

Para las lecciones U3L2, el objetivo estático admite la propiedad opcional
`"sourceDirectory": "componente"` o, según el proyecto,
`"sourceDirectory": "codigo/lotes"`. La ruta debe identificar un
`CMakeLists.txt` de subdirectorio presente en los archivos originales.
El objetivo debe aparecer en ese directorio del grafo generado por CMake.

La comprobación se fundamenta en el índice `directoryIndex` de **File API
codemodel v2**, no en el nombre de los archivos de código fuente ni en una
cadena mostrada por el programa. Una biblioteca declarada solo en la raíz
se rechaza aunque compile exactamente los mismos archivos, enlace a los
mismos consumidores y produzca las mismas salidas. La sintaxis específica
de `add_subdirectory` puede variar siempre que el árbol generado demuestre
la estructura solicitada. Una rúbrica puede añadir la propiedad independiente
`configureMessageOrder`, que observa los mensajes `STATUS` realmente
ejecutados y los archivos CMake originales donde se emitieron:

```json
"configureMessageOrder": [
  {"path": "CMakeLists.txt", "text": "raiz_antes"},
  {"path": "componente/CMakeLists.txt", "text": "componente"},
  {"path": "CMakeLists.txt", "text": "raiz_despues"}
]
```

Se utiliza la traza JSON de CMake, que registra el orden de ejecución y
el archivo de origen, no una búsqueda literal de mensajes en los archivos
ni solamente el registro de salida. Una solución que imprime
`componente` desde la raíz sin ejecutarlo dentro del archivo del hijo
se rechaza, incluso si su biblioteca y los resultados son correctos.
Se permiten otros mensajes intermedios, pero no omitir ni reordenar los
hitos declarados.

Los mensajes conservan la semántica de `message` de CMake: los argumentos de
texto se concatenan sin separador, por lo que
`message(STATUS "raiz_" "antes")` equivale a `message(STATUS "raiz_antes")`.
Los puntos y coma dentro de un argumento entre comillas forman parte del
texto. El modo debe ser exactamente `STATUS`; `message(status "raiz_antes")`
es un mensaje ordinario y no acredita ese hito de configuración.
Cuando la traza contiene argumentos vacíos o puntos y coma, se recuperan los
delimitadores de la llamada `message(...)` en su archivo y línea originales.
Una llamada indirecta ambigua que no permite recuperar esos límites no concede
crédito. La referencia de la concatenación y los modos es la documentación
oficial de [`message`](https://cmake.org/cmake/help/latest/command/message.html).

### Lista de objetivos aplicada a las propiedades CMake

Los ejercicios U3L4 pueden incorporar un contrato en la raíz de la lección:

```json
"targetList": {
  "targets": ["consultar_cajas", "comprobar_cajas"],
  "requireAppend": false
}
```

El contrato exige que una **variable real de CMake**, creada durante la
configuración, contenga exactamente los ejecutables declarados y se use
sin comillas como lista de argumentos en una aplicación ejecutada de
`CXX_EXTENSIONS OFF`. No se exige un nombre particular para la variable.
La evidencia combina el evento de la traza JSON (con argumentos expandidos)
con el primer argumento original de ese mismo comando y la variable normal
creada mediante `set` o `list(APPEND)`. La ubicación y la revisión de la fuente quedan vinculadas
a la configuración compilada. No basta declarar la variable, imprimir su
contenido, asignar `OFF` con nombres literales ni heredar el valor del
perfil Windows.

Con `"requireAppend": true`, la lista debe haberse ampliado mediante
`list(APPEND)` **antes** de utilizarla para establecer la propiedad a los
dos consumidores. Se admite aplicar la propiedad después de ampliar la lista
o volver a aplicarla posteriormente con la lista completa. Una ampliación
tardía sin reaplicación no acredita esa consigna, aunque los programas
conserven las mismas salidas. También se admite `set_property(TARGET ...)`
cuando la lista se utiliza y la propiedad se observa en los objetivos
correspondientes. Las otras condiciones de estructura, compilación y salida
continúan vigentes; basta un primer caso correcto con todas ellas satisfechas.

#### Procedencia de la lista durante la configuración

La evidencia de `APPEND` pertenece a la variable normal que se utiliza en
esa aplicación concreta. Una coincidencia entre los nombres finales de los
objetivos y una lista observada antes no demuestra que siga siendo la misma
variable. `unset(clientes)` y `set(clientes)` sin valor terminan la
definición normal; una nueva asignación con `set` inicia su propia
procedencia. Si la lectura posterior obtiene una entrada de caché con los
mismos nombres, no hereda la ampliación de la variable eliminada.

Las llamadas a [`function`](https://cmake.org/cmake/help/v4.4/command/function.html)
tienen variables locales. Una ampliación local no acredita una aplicación
posterior que usa la variable de la raíz. Si no se modifica explícitamente el
ámbito padre, al regresar se conserva la procedencia de la variable exterior.
Un archivo ejecutado con
[`include`](https://cmake.org/cmake/help/v4.4/command/include.html) comparte las
variables del llamador, por lo que una lista ampliada en la raíz puede
aplicarse desde ese archivo. El directorio físico del archivo
no identifica por sí solo el ámbito de una variable.

Las escrituras de caché se distinguen de las asignaciones normales.
Con `CMP0126 NEW`, una escritura de caché conserva la variable normal
existente. Con `OLD`, se tiene en cuenta si la escritura realmente procede y
si elimina esa definición: crear una entrada, completar una entrada sin tipo
o escribir con `FORCE`/`INTERNAL` puede terminar la procedencia normal.
Una entrada ya tipada que no se reemplaza conserva la variable normal.
Estas reglas siguen las referencias oficiales de
[`set`](https://cmake.org/cmake/help/v4.4/command/set.html),
[`unset`](https://cmake.org/cmake/help/v4.4/command/unset.html) y
[`CMP0126`](https://cmake.org/cmake/help/v4.4/policy/CMP0126.html) de la
versión de CMake declarada por el perfil Windows.

La aplicación debe obtener todos los consumidores de la expansión de esa
variable. Añadir un nombre literal después de `${clientes}` no completa
una lista que sólo contiene uno de ellos. La traza conserva la llamada
ejecutada y los argumentos expandidos; el comprobador vincula esa expansión
con la referencia original y su procedencia. Si la evidencia es incompleta o
no permite establecer esa relación, no concede la acreditación de lista.

Las regresiones de `native.cpp_cmake_graph` utilizan el editor original
U3L4 `block_0006_0004`, mantienen sus fuentes y exigen que también las
variantes rechazadas produzcan la salida correcta. Comprueban por separado
la procedencia de la lista, la acreditación exclusiva del primer caso y la
conservación del recibo válido al reabrir el estado de prueba.

Esta versión no acredita todavía todo el programa de Programación 2:
requisitos públicos/privados de encabezados, opciones, lista de objetivos,
avisos del compilador, cronología de reconstrucciones y prácticas abiertas
necesitan sus propias comprobaciones. No deducirlas de este grafo.



## Dependencia real de variables `int` locales

Work puede declarar `compilerLocalIntChecks` en la raíz de una lección
española v2 sin modificar los editores ya publicados. Ejemplo:

```json
"compilerLocalIntChecks": [{
  "activity": "block_0004_0002",
  "probes": [{
    "variable": "llaves",
    "replacement": 13,
    "outputs": ["Llaves: 13", "Llaves: 13\n"]
  }]
}]
```

El modelo admite entre una y ocho variaciones por editor, y hasta cuatro
salidas exactas por variación. Cada salida alternativa debe distinguirse
de las publicadas para el primer caso obligatorio. Los nombres de
variables en las sondas deben ser identificadores léxicos C++ reales:
`9llaves`, `llaves-extra`, `true` o `nullptr` se rechazan antes de
preparar las variantes, y se conservan los identificadores no ASCII ya
admitidos por el analizador de fuentes, como `número`. No basta con
que una cadena de autoría no sea una palabra reservada. Un nombre identifica
un `int` local declarado directamente dentro de `main`; la inicialización
puede emplear `=`, `{}` o `()`, o una primera asignación inequívoca.
También se admiten declaradores enteros agrupados de un mismo tipo, como
`int cajas{4}, sobres{9};` o `int cajas, sobres;` con asignaciones separadas.
La sonda se inserta **después del punto y coma completo**, no entre variables.
El localizador compartido reconoce tanto `int main()` como la firma C++23
`auto main() -> int`, incluidas cláusulas `noexcept` equilibradas; la misma
compatibilidad se aplica a las sondas `std::string`. No admite firmas
`main` con retorno distinto de `int`, ni definiciones duplicadas.
Una primera asignación separada no puede leer el propio entero todavía
sin inicializar: `int llaves; llaves = llaves + 7;` se rechaza antes de
generar o ejecutar variantes. En cambio, `int llaves; llaves = 3 + 4;`
mantiene el comportamiento válido y comprobable.
Si otro declarador usa el entero que se pretende variar (por ejemplo,
`int cajas=4, sobres=cajas+5;`), se rechaza: mutarlo después ocultaría
esa dependencia y podría acreditar un resultado falso. También se rechazan
punteros, referencias, arrays, duplicados, otros tipos, posiciones ambiguas
y preprocesadores que impidan una inserción segura. No se cambian
literales, comentarios ni otros nombres.

Después de la ejecución pública, el mismo trabajador Windows compila y
ejecuta cada variante en un entorno privado. La compilación y la salida
de todas las variantes se contrastan con expectativas autorales, no con
lo que calcule el propio alumno. Un LF puede presentarse como CRLF nativo.
Cambiar `cajas` no altera `sobres`; al comprobar una copia de `aforo`
puede exigirse otra variación de `anterior` para reconocer su uso real.
La pantalla mantiene una sola pulsación de Comprobar y un solo caso
visible obligatorio. El borrador y la ejecución pública se conservan.

Un acierto histórico por stdout se mantiene como registro, pero no
acredita automáticamente el requisito semántico nuevo. Solo una
comprobación privada real puede añadir su comprobante independiente.
Si no existe `compilerLocalIntChecks`, no cambia la evaluación anterior.
Esta capacidad se limita de momento a editores C++ de una sola fuente
y ejecución MSVC nativa. La revisión curricular y sus valores esperados
pertenecen a Work.


#### Incremento real exacto declarado por el autor

La variación opcional `"afterUpdate": true` puede declarar también
`"expectedIncrease": 3` para exigir **un aumento de exactamente tres
unidades en la variable local**, no sólo una subida cualquiera. El testigo
se toma antes y después de la primera actualización no trivial, usando
`long long` para calcular la diferencia sin desbordar un `int`.

Esta comprobación es independiente de la salida: `aforo += 5` seguido
por `std::cout << aforo - 2` puede reproducir el resultado visible y
la salida de todas las variaciones, pero se rechaza si la edición exigía
`expectedIncrease: 3`. Se admiten expresiones equivalentes que producen
realmente ese aumento, por ejemplo `aforo = aforo + 2 + 1` o dos
actualizaciones consecutivas `aforo += 1; aforo += 2;`. Para esta
modalidad, la sonda observa el aumento conjunto de las actualizaciones
contiguas de la misma variable, sin insertar ninguna variación privada
entre ellas. Una copia, una impresión u otra instrucción intermedia
cierra la secuencia: no se atribuye retroactivamente un cambio posterior
a los datos que ya se utilizaron.
El valor debe ser un entero positivo de 32 bits y exige `afterUpdate: true`;
no puede declararse en una sustitución del valor inicial. Las salidas
privadas siguen siendo expectativas independientes y obligatorias.

Este contrato se opta **por sonda**, se incorpora a la huella de ejecución
y al sello del comprobante, sin alterar los contratos históricos que omiten
`expectedIncrease`. Work declara el incremento exacto cuando el ejercicio
lo exige; la app no lo infiere de `stdout` ni modifica borradores.

### Mutación del aumento, además del valor inicial (U2L3)

Una sonda puede incluir `"afterUpdate": true` para expresar una
variación independiente del aumento, sin sustituir la expresión original.
En ese modo, `replacement` es la **cantidad adicional** que se suma
después de la primera actualización completa de la variable local.
Se reconocen actualizaciones directas como `aforo += 3`,
`aforo = aforo + 3`, `aforo = 3 + aforo`, incremento o resta
equivalente. El código se conserva; se inserta únicamente
`aforo += replacement;` después de esa instrucción.
Una asignación directa con `=` solo cuenta como posible actualización
cuando lee el **valor previo de esa misma variable local**; no se
acreditan reinicios fijos (`aforo = 8;`), copias idénticas
(`aforo = aforo;`) ni lecturas de un campo de otro objeto
(`aforo = datos.aforo;`). Un resultado visible correcto seguido
de una suma privada tampoco demuestra un aumento original.
El detector también descarta operaciones aritméticas demostrablemente
neutras como `aforo = aforo + 0`, `aforo += 0`,
`aforo = 0 + aforo` y `aforo = aforo * 1`. Si existe un aumento
posterior inequívoco, la sonda se coloca después de ese aumento, no
después del cálculo neutro. Se preservan expresiones con un cambio real,
como `aforo = aforo + 0 + 3`. Los literales de identidad
solo se reconocen si realmente representan cero o uno en su base:
`01`, `0x01` y `0b01` equivalen a uno, pero `10`, `010`,
`0x10` y `0b10` no. Una multiplicación por diez debe poder
someterse a la variación real, no descartarse como si fuese neutra.
Esta barrera es estructural y conservadora: no sustituye la
compilación real de las variantes ni pretende demostrar el
resultado de expresiones C++ arbitrarias.

Las demás sondas independientes y sus salidas siguen siendo obligatorias
para acreditar la dependencia del valor inicial y de la copia.

Por ejemplo, U2L3 debe exigir a la vez una variación del inicio y otra
del aumento de `aforo`, además de contrastar la salida procedente
de `anterior`. Si `anterior` se copió antes del aumento, la sonda
del incremento deja intacta su salida «Antes» y cambia «Ahora».
Si se copió después, ambas salidas deben cambiar. Los programas
que impriman `aforo` dos veces sin guardar la copia no acreditan
el conjunto. Esta comprobación sigue siendo interna; no añade
casos visibles obligatorios al estudiante.

El campo es opcional; su ausencia preserva el comprobante y las
expectativas históricas de las sondas anteriores. Su presencia
requiere el booleano JSON `true` y se vincula a la huella de
la ejecución y al comprobante semántico separado.


### Sondas de dependencia de cadenas (U2L7)

Work puede añadir `compilerLocalStringChecks` en la raíz de una
lección española con `stateLayout` v2. La declaración pertenece a los
**identificadores estables de los editores existentes**, no a sus posiciones
visuales. Ejemplo de estructura, para añadir mediante la autoría curricular:

```json
"compilerLocalStringChecks": [{
  "activity": "block_0006_0002",
  "probes": [
    {
      "variable": "edificio",
      "replacement": "Sala Norte",
      "moment": "afterInitialization",
      "outputs": ["Sala Norte-04", "Sala Norte-04\n"]
    },
    {
      "variable": "codigo",
      "replacement": "003",
      "moment": "afterInitialization",
      "outputs": ["Aula-003", "Aula-003\n"]
    },
    {
      "variable": "etiqueta",
      "replacement": "ETIQUETA CONTROL",
      "moment": "beforeOutput",
      "outputs": ["ETIQUETA CONTROL", "ETIQUETA CONTROL\n"]
    }
  ]
}]
```

`afterInitialization` inserta la nueva asignación inmediatamente
después de la definición de esa variable `std::string` local de
`main`. `afterFirstAssignment` la inserta después de su primera
asignación posterior a la definición, por ejemplo `codigo = "07";`.
`beforeOutput` inserta un valor de control inmediatamente antes de la
primera salida de `std::cout` que **lee realmente** esa variable. No
basta que el texto coincida con un literal o que se imprima la
concatenación de las variables originales omitiendo `etiqueta`.

Además de declaraciones individuales, la sonda admite declaraciones
completas de varias cadenas con el mismo tipo explícito, como
`std::string edificio{"Aula"}, codigo{"04"};`. La variación se inserta
**después del punto y coma de toda la declaración**, nunca entre
declaradores. También admite `std::string edificio, codigo;` seguido de
asignaciones mediante `=`, `+=` o `codigo.assign(...)`: para
`afterInitialization`, sustituye el valor **después de la primera asignación
completa**, de modo que la instrucción original no borre la variación antes
del cálculo. Lo mismo se aplica a `afterFirstAssignment` cuando la actualización
usa `assign(...)`. Estas formas equivalentes
siguen exigiendo compilaciones y salidas independientes correctas.
Las declaraciones ambiguas que mezclen punteros o propietarios de otro
tipo se rechazan. También se rechaza una declaración agrupada cuyo
inicializador hermano lea la variable intervenida, porque una variación
posterior al punto y coma ocultaría la dependencia real.
`auto` conserva su validación conservadora de una variable por declaración.

La definición y la instrucción de destino deben ser inequívocas en el
ámbito directo de `main`. Los nombres de las sondas `std::string`
siguen las mismas reglas léxicas C++ y conservan los identificadores
Unicode que el editor ya admite. Una macro, un ámbito ambiguo, un nombre
ausente, una lectura que nunca sucede o una variante que no compila
impiden acreditar la capacidad; no producen una aprobación falsa. Las
cadenas se inyectan mediante escapes octales de bytes UTF-8, conservando
el cero inicial y los caracteres originales sin sustituir el
programa guardado. Se autorizan entre una y doce variaciones por
editor, un par `variable`/`moment` por variación y entre una y
cuatro salidas exactas distintas que no coincidan con la salida
visible, admitiendo el salto final opcional. Las nuevas declaraciones
son acumulativas y no pueden reescribir las ya publicadas.

El trabajador Windows ejecuta la muestra visible y después compila y
ejecuta independientemente cada variante con MSVC, sin requerir al alumno
acciones adicionales. Todas las variantes deben producir sus resultados
propios. Una fuente que muestre un literal fijo puede superar la
muestra pública, pero no acredita el requisito. El nuevo comprobante
`local-string/v1` está ligado al propietario, programa y declaración.
Los recibos de salida anteriores se conservan, pero no generan ese
comprobante por sí solos; la reapertura verifica su integridad antes de
acreditar el progreso. Ningún otro curso, borrador o salida se modifica.

Las pruebas de autoría del contrato están en
`test_local_string_probe_authoring.py`; las variantes reales compiladas
con GCC, Clang y MSVC en
`test_local_string_live_compilation.py`; y la integración de los dos
editores originales U2L7, sus ejecuciones privadas y recibos Win32 en
`local_string_state_tests.cpp`. Los tests de GCC y Clang acreditan la
portabilidad de la sonda, no el funcionamiento del motor Windows. La
validación visual habitual y la autoría definitiva de las salidas del
currículo corresponden al usuario y a Work respectivamente.

### Comprobación local de sondas de variables con MSVC

La batería enfocada de dependencia de enteros y cadenas puede ejecutarse
desde el checkout nativo autorizado, en la rama `Codex`, mediante:

```powershell
& '.\scripts\windows\Test-LocalVariableDependency.ps1' -Preset windows-x64-debug
```

El script comprueba el checkout y la rama antes de compilar, resuelve el
toolset MSVC y el Windows SDK fijados en el proyecto, y ejecuta las pruebas
unitarias de `cpp_local_int_probe` y `cpp_local_string_probe`.
A continuación genera y compila con MSVC los **43 casos de enteros** y
**30 casos de cadenas**, contrastando las salidas y los rechazos reales
con los dos validadores Python existentes. Los dos códigos iniciales de
U2L5 también se compilan y se comprueba que no acrediten sus objetivos.

Solo se crean binarios y objetos QA en
`out/build/<preset>/local-variable-qa/` y registros en
`out/build/<preset>/logs/local-variable-qa.*.log`.
No se modifica ningún archivo curricular, borrador, perfil ni estado
del estudiante. Se trabaja en un proceso oculto, sin instalar herramientas
ni iniciar GitHub Actions. Esta comprobación de fuentes aisladas
**no sustituye** las pruebas de integración Win32 del motor,
la ejecución de la aplicación ni la revisión visual y auditiva humana.

### Integración real de variables locales en Win32

Las pruebas de compilación aislada anteriores no verifican la carga del
JSON, los recibos de progreso ni la persistencia de la aplicación. El
CTest nativo incorpora por separado:

- `native.local_int_state`: ambos cargadores de lecciones, los editores
  originales de U2, comprobantes independientes y su integridad.
  Guarda estados reales en **subperfiles QA Win32 exclusivos** y los reabre
  desde disco: verifica el recibo y el progreso de U2L1 y, por separado,
  los dos borradores, las acreditaciones y las referencias de U2L5.
  También conserva el progreso de otro curso y preferencias ajenas.
  No ejecuta el programa del estudiante.
- `native.local_string_state`: dos propietarios U2L7, compilaciones y
  ejecuciones MSVC reales, rechazo de salidas fijas, recibos por editor,
  guardado y reapertura del StateStore en un perfil QA único.

Para construir únicamente estos dos ejecutables y ejecutar exactamente
ambas pruebas en el checkout Windows autorizado de `Codex`:

```powershell
& '.\scripts\windows\Test-LocalVariableState.ps1' -Preset windows-x64-debug
```

El comprobador valida el nombre del checkout, la rama activa y el toolchain
Windows fijado; registra la salida en
`out/build/<preset>/logs/local-variable-state.*.log`. No compila ni
inicia la aplicación completa, no modifica lecciones ni usa el perfil
del alumno. La prueba de U2L7 usa un perfil de QA propio y aislado.

Esta batería CTest es una **prueba de integración nativa**, distinta
de las pruebas de código fuente con GCC, Clang o MSVC hospedado.
Hasta obtener una ejecución y sus registros en el Windows autorizado,
su presencia en CMake no se considera evidencia de éxito.

Para detectar fallos de integración antes de esa ejecución local, el
banco independiente `cmake/PortableCompilerGuidanceQA` incluye también
`native.compiler_local_int_state` y `native.compiler_local_string_state`.
El segundo compila y ejecuta las variantes U2L7 mediante MSVC y el
`lgaed_cpp_tool_host` reales en un **runner Windows hospedado desechable**.
Este banco genera una cabecera QA con las rutas del compilador y del SDK
de ese runner; no cambia el manifiesto ni el toolchain fijados para
la aplicación. El flujo de GitHub comunica por separado el estado
`codex/local-variable-hosted-ctest`, sin equiparar esa evidencia con
una instalación o ejecución en el Windows personal del estudiante.

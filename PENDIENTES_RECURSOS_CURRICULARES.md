# Recursos y capacidades curriculares

El estado corresponde a la incorporación en las lecciones: **pendiente**, **creado pero no integrado** o **integrado y verificado**. Se conserva una entrada por recurso y se añaden sus ubicaciones cuando se reutiliza.

## CPP_STDIN_PREPARADA_LECCIONES

- **PENDIENTE_ID:** `CPP_STDIN_PREPARADA_LECCIONES`
- **Estado:** integrado y verificado en el Honor X8a. El laboratorio curricular entrega entrada preparada al motor y conserva el texto entre reconstrucciones de la pantalla.
- **Curso:** `cpp_fundamentals` — Programar desde cero con C++.
- **Lección:** `cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida`.
- **Archivo:** `E:/PersonalFiles/Projects/LearnGraphicsProgramming/app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json`.
- **Ubicaciones:** página 1, nota sobre entrada preparada y EOF; página 7, bloque 3, laboratorio de lectura real; página 8, código de lectura mediante `cin`; página 9, referencia comparativa que conserva la simulación con constantes. Páginas y bloques numerados desde 1.
- **Objetivo pedagógico:** practicar lectura real, distinguir entrada y salida y combinar lectura, procesamiento y presentación sin sustituir la lectura por constantes.
- **Experiencia deseada:** escribir o modificar todo el texto de entrada antes de ejecutar. La ejecución lo consume en orden hasta EOF. Observar la salida y repetir con otros datos. No requiere escribir durante la ejecución.
- **Datos y condiciones:** `Rita 4` debe producir `Rita: 7`; `Leo 2` debe producir `Leo: 5`. Los dos tokens pueden separarse con espacios o saltos. El ejemplo inicial presupone esos datos válidos: no certifica validación general. Más adelante, las actividades de flujos deben comprobar entrada vacía, dato mal formado y datos adicionales según su formato.
- **Capacidad incorporada:** campo editable de entrada preparada, captura independiente para cada ejecución y envío al parámetro `standardInput` existente. Diagnósticos de construcción, stdout y stderr se presentan por separado.
- **Contrato comprobado:** `cpp_compiler` conserva `initialCode` y admite `initialStandardInput` opcional de tipo texto, vacío por defecto. `LessonActivity` captura el código y la entrada antes de llamar a `compileAndRun`; cada ejecución recibe su propio archivo cerrado de entrada UTF-8. Es entrada preparada, sin consola interactiva. El contrato se comprueba en el renderer, el validador curricular y las pruebas Android.
- **Archivos de implementación relacionados:** `app/src/main/java/com/learngraphicsprogramming/app/LessonActivity.kt` y `app/src/main/java/com/learngraphicsprogramming/app/runtime/CppExecutionEngine.kt`, dentro de este proyecto.
- **Criterios de validación:**
  - Entregar el texto como entrada, sin concatenarlo al código C++ ni confundirlo con stdout.
  - Ejecutar los dos casos desde el renderer de la lección y comprobar sus salidas.
  - Cada ejecución nueva comienza a consumir su propia entrada desde el principio.
  - La entrada vacía alcanza EOF sin esperar teclado; los programas que comprueban la extracción pueden responder a ello.
  - Distinguir diagnóstico de compilación, salida del programa y error de ejecución.
  - Mantener el comportamiento de las lecciones sin entrada, sus identificadores, el progreso y los datos existentes.
  - Actualizar la lección y verificar su funcionamiento antes de cambiar esta entrada a **integrado y verificado**.
- **Feedback pedagógico:** contrastar tokens recibidos, operación realizada y caracteres producidos. Ante formato inválido, justificar la comprobación del flujo; un cero inicial no acredita una lectura correcta.
- **Verificación Android:** `CppCurricularLabsDeviceTest` ejecuta el laboratorio publicado con `Rita 4`, `Leo 2`, saltos de línea y repeticiones; comprueba recreación y comportamiento de fuente única. El mismo renderer verifica EOF vacío, formato inválido, UTF-8, datos adicionales y separación de los canales con un programa que comprueba la extracción. El recorrido curricular de entrada también funciona con Clang 21 opcional; el predeterminado sigue siendo Clang 23 con Wasmtime.
- **Contenido actual conservado:** predicciones, ejercicios y referencia de simulación con constantes en sus posiciones e identificadores previos. El laboratorio de página 7 utiliza ahora `cin` real. Las prácticas reproducibles con `istringstream` no sustituyen esa comprobación ni certifican automáticamente otras actividades de flujos.

## CPP_PROYECTO_MULTIFUENTE

- **PENDIENTE_ID:** `CPP_PROYECTO_MULTIFUENTE`
- **Estado:** integrado y verificado en el Honor X8a. Las lecciones ofrecen archivos virtuales editables, selección de fuentes, conservación del proyecto, compilación por unidad y enlace conjunto.
- **Curso:** `cpp_fundamentals` — Programar desde cero con C++.
- **Lección de fundamento:** `cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas`.
- **Archivo:** `E:/PersonalFiles/Projects/LearnGraphicsProgramming/app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_declaraciones_y_firmas.json`.
- **Ubicación:** página 1, ejemplo original de declaración y definición reunidas en una fuente, conservado; página 7, bloque 3, laboratorio con `diferencia.h`, `diferencia.cpp` y `main.cpp`. Páginas y bloques numerados desde 1.
- **Ubicación de reutilización:** lección `cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos`, archivo `E:/PersonalFiles/Projects/LearnGraphicsProgramming/app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json`. Se conservan las referencias: página 2, bloque 3, `sobres.h`; página 3, bloques 3 y 5, `sobres.cpp` y `main.cpp`; página 4, compilación y enlace; página 10, bloques 3–5, `serie.h`, `serie.cpp`, `main.cpp`. Los laboratorios ejecutables están en página 13, bloque 3, sobres, y página 14, bloque 3, serie; sus fuentes iniciales coinciden exactamente con esas referencias. Página 9, bloque 6, remite a ambos laboratorios. Páginas y bloques numerados desde 1.
- **Objetivo pedagógico:** construir y modificar una interfaz compartida y su implementación; distinguir inclusión, compilación por archivo y enlace, y diagnosticar incoherencias sin confundir una declaración con un cuerpo ejecutable.
- **Experiencia deseada:** un único laboratorio con una lista visible de archivos y edición individual. Ejecutar compila las fuentes del proyecto y enlaza un programa; presentar diagnósticos con archivo y línea, y la salida de una ejecución válida. Conservar el proyecto al cambiar de archivo y al recrear la pantalla.
- **Datos y condiciones iniciales:** separar el ejemplo real en `diferencia.h` (declaración `int diferencia(int primero, int segundo);`, con protección contra inclusión repetida), `diferencia.cpp` (incluye esa cabecera y define la resta) y `main.cpp` (incluye la cabecera e `<iostream>`, llama a `diferencia(9, 2)` y muestra el resultado). Compilar las dos fuentes `.cpp`; la cabecera se incluye, no se trata como otra fuente con `main`. Sólo hay un `main`.
- **Datos de reutilización:** conservar las fuentes exactas del proyecto de sobres en los bloques citados: `sobres.h` incluye `<string>`, declara `int sobres_necesarios(int)` y `std::string descripcion_sobres(int)` y usa la guarda `CPP_FUNDAMENTALS_SOBRES_H`; `sobres.cpp` incluye esa interfaz, calcula sobres de 12 tarjetas en el dominio 0..120 y produce la descripción; `main.cpp` incluye la cabecera e `<iostream>` y utiliza 25 tarjetas. Casos: 0 → `Sin tarjetas: 0`; 12 → `Un sobre: 1`; 13 → `Varios sobres: 2`; 25 → `Varios sobres: 3`; 120 → `Varios sobres: 10`. Cambiar la capacidad a 20, manteniendo la interfaz, debe dar `Varios sobres: 2` con 25. El proyecto de serie posterior permite transferir la misma capacidad a otra interfaz, sin duplicar esta entrada.
- **Capacidad incorporada:** lista de archivos, creación, edición y eliminación dentro del proyecto, selección de fuentes y borradores persistentes por ruta/lección/página/bloque. Cada ejecución materializa sus archivos en un espacio privado nuevo, compila las fuentes seleccionadas con `-c` y enlaza sus objetos nuevos. Las cabeceras se incluyen, y el resultado distingue compilación, enlace y ejecución.
- **Contrato comprobado:** `cpp_compiler` admite `projectFiles`, lista no vacía de objetos `{ "path": "diferencia.h", "initialCode": "..." }`, y `sourceFiles` opcional, lista de rutas de fuentes seleccionadas. Sin `sourceFiles` se seleccionan las extensiones C++ admitidas; las cabeceras no se compilan como fuentes. Con `projectFiles` se ejecuta el proyecto; sin él se conserva `initialCode` y el comportamiento de fuente única. El modelo y el validador rechazan rutas absolutas, escapes, duplicados y conflictos archivo/carpeta. Una selección vacía puede conservarse como borrador, pero no ejecutarse.
- **Archivos de implementación relacionados:** `app/src/main/java/com/learngraphicsprogramming/app/LessonActivity.kt`, `app/src/main/java/com/learngraphicsprogramming/app/CppLessonProjectEditor.kt`, `app/src/main/java/com/learngraphicsprogramming/app/runtime/CppProject.kt`, `CppCompiler.kt` y `CppExecutionEngine.kt`, dentro de este proyecto.
- **Criterios de validación:**
  - El proyecto completo produce `7`; cambiar la implementación de resta a suma, manteniendo su interfaz, produce `11`.
  - Omitir `diferencia.cpp` distingue el fallo de enlace por definición ausente de un fallo de lectura de cabecera.
  - Cambiar el resultado de la definición a `double`, incluyendo la cabecera que declara `int`, produce un diagnóstico de compilación de la incoherencia.
  - Incluir repetidamente la cabecera protegida no duplica declaraciones incompatibles ni definiciones; las fuentes comparten la misma interfaz.
  - Editar una cabecera o una implementación actualiza la ejecución siguiente; no usar objetos compilados de una versión anterior.
  - Los archivos pertenecen al proyecto y su ejecución aislada; no sobrescribir archivos del usuario ni aceptar rutas que salgan de su espacio.
  - Mantener el comportamiento, identificadores y progreso de los laboratorios de una sola fuente.
  - En el proyecto de sobres, quitar la declaración o inclusión necesaria del cliente falla en compilación; omitir `sobres.o` falla al enlazar. Distinguir ambos diagnósticos.
  - Una definición ordinaria de función en la cabecera, aun con guarda, incluida desde ambos `.cpp` debe permitir observar el fallo de enlace por definiciones duplicadas. La guarda protege cada inclusión textual; no reemplaza las reglas de definición del programa completo.
  - Integrar la práctica en su lección y comprobarla en la aplicación antes de cambiar el estado a **integrado y verificado**.
- **Feedback pedagógico:** relacionar cada diagnóstico con la etapa y el archivo responsables. Compilar una llamada con una declaración visible no acredita que exista una definición enlazada.
- **Verificación Android:** `CppCurricularLabsDeviceTest` utiliza los assets publicados y el botón del renderer: diferencia `7` → `11`, cabecera repetida, cambios de cabecera y de implementación, firma incoherente, cabecera ausente y definición sin enlazar; sobres `0/12/13/25/120`, capacidad `20`, declaraciones/inclusiones ausentes y cuerpo ordinario duplicado en la cabecera; serie y transferencia de sus límites. También comprueba creación y eliminación, rutas inválidas, borradores con sólo cabeceras, conservación al cambiar de página/recrear, selección vacía, separación de salidas y editor fullscreen con IME real. El proyecto de diferencia se compila y enlaza también con Clang 21 opcional. Ejecutar estos laboratorios no consume intentos ni completa ejercicios.
- **Límite de esta entrada:** no certifica soporte de módulos nombrados ni de `import std`; esos mecanismos requieren comprobar sus interfaces y proceso de construcción propios cuando una actividad los necesite.

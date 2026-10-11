# Recursos y capacidades curriculares

El estado corresponde a la incorporación en las lecciones: **pendiente**, **creado pero no integrado** o **integrado y verificado**. Se conserva una entrada por recurso y se añaden sus ubicaciones cuando se reutiliza.

## CPP_STDIN_PREPARADA_LECCIONES

- **PENDIENTE_ID:** `CPP_STDIN_PREPARADA_LECCIONES`
- **Estado:** pendiente. El motor acepta entrada preparada; falta exponerla en el laboratorio curricular y verificar su integración en las lecciones.
- **Curso:** `cpp_fundamentals` — Programar desde cero con C++.
- **Lección:** `cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida`.
- **Archivo:** `E:/PersonalFiles/Projects/LearnGraphicsProgramming/app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json`.
- **Ubicaciones:** página 1, nota que delimita la capacidad actual; página 7, laboratorio de simulación y práctica de lectura pendiente; página 8, código de lectura real mediante `cin`; página 9, referencia que distingue la simulación de la lectura. Páginas numeradas desde 1.
- **Objetivo pedagógico:** practicar lectura real, distinguir entrada y salida y combinar lectura, procesamiento y presentación sin sustituir la lectura por constantes.
- **Experiencia deseada:** escribir o modificar todo el texto de entrada antes de ejecutar. La ejecución lo consume en orden hasta EOF. Observar la salida y repetir con otros datos. No requiere escribir durante la ejecución.
- **Datos y condiciones:** `Rita 4` debe producir `Rita: 7`; `Leo 2` debe producir `Leo: 5`. Los dos tokens pueden separarse con espacios o saltos. El ejemplo inicial presupone esos datos válidos: no certifica validación general. Más adelante, las actividades de flujos deben comprobar entrada vacía, dato mal formado y datos adicionales según su formato.
- **Capacidad faltante:** campo de entrada preparada en la interfaz del laboratorio y paso de su texto al parámetro `standardInput` existente. Acordar y verificar el contrato curricular antes de añadir propiedades nuevas a los JSON.
- **Contrato comprobado:** `cpp_compiler` usa `initialCode`; `LessonActivity` llama a `compileAndRun(source)` sin entrada. `CppExecutionEngine` acepta `standardInput`, cuyo valor predeterminado es vacío. El motor recibe un archivo cerrado antes de ejecutar: es entrada preparada, sin consola interactiva.
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
- **Contenido actual conservado:** predicciones con entrada explícita en el enunciado y laboratorio con constantes, identificado como simulación. Las prácticas reproducibles con `istringstream` no sustituyen la comprobación curricular de `cin`.

## CPP_PROYECTO_MULTIFUENTE

- **PENDIENTE_ID:** `CPP_PROYECTO_MULTIFUENTE`
- **Estado:** pendiente. La ejecución curricular actual recibe una sola fuente; falta crear, editar, compilar y enlazar un proyecto de varios archivos propios desde la lección.
- **Curso:** `cpp_fundamentals` — Programar desde cero con C++.
- **Lección de fundamento:** `cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas`.
- **Archivo:** `E:/PersonalFiles/Projects/LearnGraphicsProgramming/app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_declaraciones_y_firmas.json`.
- **Ubicación:** página 1, declaración y definición de `diferencia`, actualmente reunidas en una fuente ejecutable. Página numerada desde 1. La práctica multifuente deberá añadirse con su ubicación real cuando esté incorporada; el ejemplo actual sigue funcionando con el contrato existente.
- **Ubicación de reutilización:** lección `cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos`, archivo `E:/PersonalFiles/Projects/LearnGraphicsProgramming/app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json`. Página 2, bloque 3: `sobres.h`; página 3, bloques 3 y 5: `sobres.cpp` y `main.cpp`; página 4: compilación y enlace; página 9, bloque 6: límite actual del laboratorio y necesidad de práctica multifuente; página 10, bloques 3–5: referencia de construcción `serie.h`, `serie.cpp`, `main.cpp`. Páginas y bloques numerados desde 1. Las actividades textuales actuales están integradas; la ejecución de estos proyectos en el laboratorio sigue pendiente.
- **Objetivo pedagógico:** construir y modificar una interfaz compartida y su implementación; distinguir inclusión, compilación por archivo y enlace, y diagnosticar incoherencias sin confundir una declaración con un cuerpo ejecutable.
- **Experiencia deseada:** un único laboratorio con una lista visible de archivos y edición individual. Ejecutar compila las fuentes del proyecto y enlaza un programa; presentar diagnósticos con archivo y línea, y la salida de una ejecución válida. Conservar el proyecto al cambiar de archivo y al recrear la pantalla.
- **Datos y condiciones iniciales:** separar el ejemplo real en `diferencia.h` (declaración `int diferencia(int primero, int segundo);`, con protección contra inclusión repetida), `diferencia.cpp` (incluye esa cabecera y define la resta) y `main.cpp` (incluye la cabecera e `<iostream>`, llama a `diferencia(9, 2)` y muestra el resultado). Compilar las dos fuentes `.cpp`; la cabecera se incluye, no se trata como otra fuente con `main`. Sólo hay un `main`.
- **Datos de reutilización:** conservar las fuentes exactas del proyecto de sobres en los bloques citados: `sobres.h` incluye `<string>`, declara `int sobres_necesarios(int)` y `std::string descripcion_sobres(int)` y usa la guarda `CPP_FUNDAMENTALS_SOBRES_H`; `sobres.cpp` incluye esa interfaz, calcula sobres de 12 tarjetas en el dominio 0..120 y produce la descripción; `main.cpp` incluye la cabecera e `<iostream>` y utiliza 25 tarjetas. Casos: 0 → `Sin tarjetas: 0`; 12 → `Un sobre: 1`; 13 → `Varios sobres: 2`; 25 → `Varios sobres: 3`; 120 → `Varios sobres: 10`. Cambiar la capacidad a 20, manteniendo la interfaz, debe dar `Varios sobres: 2` con 25. El proyecto de serie posterior permite transferir la misma capacidad a otra interfaz, sin duplicar esta entrada.
- **Capacidad faltante:** editor curricular de archivos virtuales y contrato de contenido para proyectos; materialización de sus fuentes y cabeceras antes de compilar; compilación de las unidades seleccionadas y enlace conjunto. Verificar primero qué puede reutilizarse del compilador actual. No añadir campos a los JSON hasta acordar y comprobar el contrato real.
- **Contrato comprobado:** el renderer de `cpp_compiler` usa `initialCode`; su llamada a `compileAndRun` proporciona una sola fuente. La posibilidad de escribir archivos durante una ejecución no proporciona cabeceras antes de la compilación ni un proyecto editable entre ejecuciones.
- **Archivos de implementación relacionados:** `app/src/main/java/com/learngraphicsprogramming/app/LessonActivity.kt` y `app/src/main/java/com/learngraphicsprogramming/app/runtime/CppExecutionEngine.kt`, dentro de este proyecto.
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
- **Límite de esta entrada:** no certifica soporte de módulos nombrados ni de `import std`; esos mecanismos requieren comprobar sus interfaces y proceso de construcción propios cuando una actividad los necesite.

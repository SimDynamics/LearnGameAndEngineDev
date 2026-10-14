# Laboratorio CMake de Android

El bloque existente `cpp_compiler` puede optar por CMake mediante un objeto
`cmake`. Los bloques sin ese objeto conservan el compilador, la selección
`sourceFiles`, la entrada preparada y los borradores anteriores. No se cambian
identificadores, posiciones ni claves de progreso.

## Contrato opcional

```json
{
  "type": "cpp_compiler",
  "language": "cpp",
  "initialCode": "",
  "projectFiles": [
    {"path": "CMakeLists.txt", "initialCode": "cmake_minimum_required(VERSION 3.20)\nproject(Existencias LANGUAGES CXX)\nadd_executable(consulta main.cpp)\ntarget_compile_features(consulta PRIVATE cxx_std_20)\nset_target_properties(consulta PROPERTIES CXX_EXTENSIONS OFF)\n"},
    {"path": "main.cpp", "initialCode": "int main() { return 0; }\n"}
  ],
  "cmake": {
    "target": "consulta",
    "options": {"USAR_COMPARACION_ESTRICTA": "OFF"},
    "buildType": "Debug"
  }
}
```

Este ejemplo documenta el contrato técnico; no agrega una lección. `initialCode`
sigue siendo texto por compatibilidad. `projectFiles` es obligatorio. CMake
selecciona las fuentes desde su descripción: este perfil no admite `sourceFiles`.
`target` es obligatorio. `options` y `buildType` son opcionales; los valores
predeterminados son un mapa vacío y `Debug`. También se admiten `Release`,
`RelWithDebInfo` y `MinSizeRel`.

Las opciones son pares de nombre/texto; en pantalla se editan como `CLAVE=valor`,
una por línea. Se conserva el valor íntegro, incluidos espacios y signos `=`.
Los nombres `CMAKE_` y `LGP_` están reservados al entorno. Hasta 64 opciones,
4096 unidades UTF-16 por valor, 128 archivos y 8 MiB de texto. Las rutas siguen
las mismas comprobaciones relativas del editor de proyectos anterior. Un
borrador incompleto sigue siendo editable; CMake comunica los errores reales
de su descripción al configurar.

## Herramientas y fases

El perfil disponible utiliza CMake 4.1.2 y Ninja 1.12.1 **nativos Android ARM64**,
con LLVM AR 23.1.3 para bibliotecas estáticas. Requiere Android 9/API 28 o
posterior. Los laboratorios anteriores conservan su mínimo Android API 24.
Los ejecutables se empaquetan como PIE en `nativeLibraryDir`; los módulos y
licencias de CMake se extraen aparte. Las fuentes, parches, comandos de
construcción y comprobaciones SHA-256 están en `toolchains/cmake/`.

El compilador Clang elegido conserva el sysroot WASI existente y genera
`wasm32-wasip1`. La descripción pide sus propias características de C++; no se
impone un `-std` global. Seleccionar el módulo opcional Clang 21 requiere que
esté instalado. El entorno no cambia silenciosamente de compilador.

La toolchain fija `CMAKE_SYSTEM_NAME=Generic` y `CMAKE_SYSTEM_VERSION=1` antes
de `project()`. Así CMake comprueba el compilador WASI para el destino previsto,
sin usar las reglas de un destino Android para esos programas WebAssembly.

1. **Configurar/generar** ejecuta CMake real con `-S`, `-B`, `-G Ninja` y una
   toolchain WASI. CMake comprueba el compilador y produce caché, reglas y su
   respuesta file-api. Cada configuración utiliza `--fresh`, para retirar
   opciones eliminadas y comprobar nuevamente los cambios de compilador.
   CMake puede compilar pruebas de herramienta; no construye el objetivo pedido.
2. **Construir objetivo** ejecuta `cmake --build … --target …`. Solo las reglas
   generadas deciden qué fuentes compilar, qué opciones utilizar y qué enlazar.
   No se sustituye esta operación por una compilación directa de los `.cpp`.
3. **Ejecutar objetivo** identifica el artefacto ejecutable desde file-api,
   comprueba su formato y huella, y lo ejecuta mediante el motor WASI existente
   con entrada, salida y código de terminación propios. Una biblioteca puede
   construirse, pero no se ofrece como programa ejecutable.

Configurar y generar corresponden a una invocación nativa de CMake. El registro
conserva el comando y ambos canales completos. El marcador real `Configuring
done` distingue un fallo de configuración de uno posterior de generación;
los fallos de Ninja/construcción y de ejecución tienen fases independientes.
Un éxito de construcción no ejecuta el programa ni acredita su comportamiento.

## Edición, invalidación y restauración

Cada bloque usa un directorio aislado `cacheDir/cpp-cmake-jobs/<huella de clave>`.
Cambios de fuentes o cabeceras permiten reconstruir con las reglas vigentes,
pero invalidan el programa anteriormente construido. Cambios de descripción,
rutas, opciones, tipo o compilador requieren volver a configurar. Un cambio de
objetivo requiere construirlo. Si CMake se usa para leer fuentes o cabeceras en
la fase de configuración, se debe pulsar de nuevo configurar tras editarlas.

Las huellas delimitan cada texto y ordenan los mapas, sin quitar espacios del
contenido. Al editar se limpian las salidas mostradas del intento previo. Una
operación en curso trabaja sobre una instantánea y no acredita su resultado si
el borrador cambia antes de terminar. Antes de ejecutar también se verifica la
huella del artefacto.

Los archivos, pestaña y cursores usan el borrador `/project` anterior; objetivo,
texto de opciones y tipo usan `/cmake` dentro de `cpp_lesson_drafts`. La entrada
y elección de compilador conservan sus claves. Recrear la actividad o volver
de otra página mantiene estos valores. Los controles no consumen intentos ni
completan ejercicios curriculares.

La operación nativa continúa al recrear la actividad o cambiar de página. El
controlador observa su estado y recupera el resultado al volver, únicamente si
coinciden el proyecto y, para ejecutar, la entrada vigente. Los controles se
desbloquean al terminar; el controlador anterior nunca sobrescribe el borrador
de la actividad recreada.

Este perfil no promete generadores universales, binarios Android de escritorio,
descarga de dependencias ni herramientas externas que no estén instaladas.
Work integrará y comprobará la actividad curricular posteriormente.

# Herramientas reales CMake y Ninja para Android

`build-android-local.ps1` descarga y comprueba las fuentes fijadas en
`provenance.properties`, las compila con el NDK local y genera únicamente en
`.gradle/cmake-android-build/` y `.gradle/cmake-android-arm64/`. No instala
herramientas, cambia el PATH global ni altera los compiladores C++ existentes.

En Windows, con Python 3.12 o posterior, Android SDK, CMake host 4.1.2, Ninja 1.12.1 y NDK
28.2.13676358 instalados, ejecutar desde esta carpeta:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build-android-local.ps1 -Jobs 8
```

El archivador LLVM reutiliza en lectura las fuentes LLVM 23.1.3 y las
bibliotecas estáticas ARM64 del build del compilador existente (`-LlvmRoot`,
por defecto `E:/LGP-LLVM23`). Un proyecto CMake pequeño de esta carpeta compila
solo el driver y el archivador en la carpeta de construcción de la aplicación;
no ejecuta ni modifica el build original. El manifiesto registra el commit
exacto, hashes de fuentes y de las bibliotecas leídas y la licencia LLVM.

La tarea Gradle `prepareCmakeRuntime` comprueba el manifiesto y los SHA-256 de
los resultados antes de empaquetarlos. Las herramientas son ejecutables PIE
ARM64 con libc++ estática; el APK los instala como `liblgp_cmake.so` y
`liblgp_ninja.so` en `nativeLibraryDir`. Se ejecutan desde ese directorio;
el ZIP de datos no contiene herramientas ejecutables. No necesitan Termux,
root, una conexión de red del estudiante ni una modificación del sistema.

El perfil requiere Android API 28 para `posix_spawn`, utilizado por Ninja.
La aplicación conserva su mínimo Android API 24 y sus compiladores anteriores;
el laboratorio CMake comunica la indisponibilidad en dispositivos anteriores.
Los programas construidos son WASIp1 y se ejecutan mediante el runtime WASI
de la aplicación, conservando la separación entre Android y el destino WASI.

`liblgp_llvm_ar.so` añade el archivador real para objetivos `STATIC`.
`CMAKE_AR` debe apuntar al enlace `llvm-ar` del job y `CMAKE_RANLIB` a
`llvm-ranlib`; ambos enlaces apuntan a este binario instalado. El dispatch
original de LLVM utiliza el nombre del enlace para elegir la operación.

El ZIP `runtime/cmake-4.1.2-resources.zip` contiene los `Modules` y `Templates`
originales, las licencias CMake y de sus dependencias vendorizadas, la licencia
Ninja y los avisos del NDK, que incluyen libc++. Se extrae en una carpeta de
datos; `LGP_CMAKE_ROOT` indica su raíz con `Modules/CMake.cmake`.

Los parches versionados adaptan únicamente el empaquetado y el sistema
Android: ubicar los recursos CMake y reutilizar su nombre ejecutable real,
seleccionar las fuentes Linux y el archivo Android `pthread-fixes.c` original
de libuv sin `librt`, usar
`sched_setaffinity` nativo en el hijo de libuv (la variante pthread requiere
API 36), evitar el shim
obsoleto de archivos grandes de libarchive en ARM64 (Bionic ya tiene `off_t`
de 64 bits), y usar el shell
del sistema `/system/bin/sh` en Ninja. Las reglas, objetivos, fuentes y opciones
siguen siendo interpretados por CMake y construidos por Ninja reales. No se
fijan identificadores, versiones ni resultados ficticios del compilador.

Se desactivan GUI, pruebas upstream, debugger y OpenSSL del binario CMake.
Este perfil permite proyectos locales; la descarga HTTPS desde CMake no está
habilitada. El manifiesto generado registra ABI, API, NDK, versiones, commits,
hashes de fuentes, parches, script y artefactos. Los registros de construcción
y de las verificaciones Android pertenecen a las evidencias locales de QA.

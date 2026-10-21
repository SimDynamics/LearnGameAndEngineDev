# Implementación y verificación — Codex

Registro de Codex. Las especificaciones y la aceptación curricular pertenecen
a Work en [PENDIENTES_WORK.md](PENDIENTES_WORK.md).

## WIN_BASE_NATIVA_CPP_CMAKE

- **Referencia:** [especificación de Work](PENDIENTES_WORK.md#win_base_nativa_cpp_cmake).
- **Estado técnico:** implementada y verificada en las copias `Codex` y `Work`
  el 21-10-2026. El usuario autorizó expresamente copiar la base técnica y probar
  ambas. La aceptación curricular corresponde a Work en su registro; esta entrega
  no equivale a recuperar el currículo ni importar progreso.
- **Alcance:** base gráfica Win32 x64 en C++20, CMake y Ninja. Se mantienen
  íntegros el proyecto importado, el currículo y los registros de Work.
- **Separación:** configuración común en la raíz; compilaciones bajo `out/`
  de cada copia y datos propios para los perfiles `Work` y `Codex`.
- **Archivos:** `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `native/` y
  `scripts/windows/`. Uso y aislamiento: [base Windows](docs/WINDOWS_NATIVE.md).
- **Herramientas comprobadas:** MSVC v145 `14.51.36231`, compilador
  `19.51.36260.0`, host/destino x64; SDK/UCRT `10.0.26100.0`;
  CMake `4.3.1-msvc1` y Ninja `1.13.2`.
- **Compilación y ejecución:** Debug, RelWithDebInfo y Release aprobados en ambas
  copias, seis configuraciones en total;
  PE AMD64/PE32+ con subsistema gráfico, ventana Unicode real y manifiesto
  `asInvoker`/`PerMonitorV2`. CTest: 12/12 en total, 2/2 por configuración, incluidos los diez
  grupos de perfiles y la apertura/pintura/cierre automático de una ventana QA.
- **Ventanas y datos:** 109 comprobaciones por configuración y copia, 654 en total:
  perfiles Work/Codex simultáneos desde los ejecutables de cada copia,
  cierre independiente, reapertura desde otros directorios y conservación exacta
  de seis categorías de datos QA. Los procesos registraron `elevated=0` y DPI 216.
  Inspección visual real de la ventana inicial y maximizada, con controles accesibles.
- **Dos checkouts reales:** 133 comprobaciones adicionales aprobadas. Ejecutables
  distintos, simultáneos y sin `--profile`, con sus valores predeterminados propios.
  Cierre y reconstrucción completa de Work con compilación/enlace reales mientras
  Codex permanece abierto; binario, caché CMake, fechas y seis categorías de datos
  de Codex intactos. Reapertura de Work y convivencia verificadas. Las fuentes
  técnicas comunes son idénticas y los seis árboles generados no enlazan al vecino.
- **Compilación incremental:** corregida la comparación de rutas Windows 8.3 y
  la codificación MSVC español/Ninja. Las cabeceras propias figuran en las
  dependencias; cambiar la fecha de `profile.hpp` recompila sus consumidores;
  repetir sin cambios no vuelve a compilar ni enlazar. Rechazos comprobados de
  entorno ausente, SDK incorrecto y cabeceras de otro SDK mezcladas.
- **Conservación:** comparación SHA-256 de los 1.730 archivos preexistentes de
  cada copia. Los 878 JSON, traducciones, mapas de estado y registros permanecen
  idénticos. En ambas copias solo cambió el `.gitignore` preexistente para excluir las
  salidas nuevas; los demás archivos comparados se conservan íntegros. Sin cambios en
  el proyecto Android original ni en los botones del PC; sin commits ni publicación.
- **Evidencias locales:** `out/qa/native-{debug,relwithdebinfo,release}.json`,
  `out/qa/import-preservation.json`, `out/qa/toolchain-rejections.json`,
  `out/qa/release-embedded.manifest`, `out/qa/checkout-isolation.json`,
  `out/qa/build-path-isolation.json` y `out/qa/verification-matrix.json`;
  resultados CTest y registros de compilación
  bajo `out/build/<preset>/`. Todos excluidos de Git.
- **Fuera de esta entrega:** recuperación curricular, importación del progreso,
  renderizado GPU e integraciones con herramientas profesionales.

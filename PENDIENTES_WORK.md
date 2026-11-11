# Notas internas de continuidad de Work

Encargos activos sólo en PENDIENTES_RECURSOS_CURRICULARES.md.

Traslado de carpetas (2026-11-10): el código Git reside ahora bajo `E:\Projects`. En Git se corrigieron las referencias de Codex; falta comprobar la sincronización del maestro físico `E:\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt`. El gestor de ramas, los botones personales y su prueba `scripts/windows/Test-BranchButtons.ps1` quedan deliberadamente sin modificar para la revisión manual indicada por el usuario.

Continuar Programación 2 desde U4L7, posterior a U4L4–U4L6 de AssetCheck ya redactadas. U4L6 incorporó enseñanza de lectura física del manifiesto (LF/CRLF, EOF, CR aislado, 80 bytes, 100/101 líneas), errores globales y cliente transaccional. El proyecto externo de referencia se verificó sólo en Linux: GCC 14.2 y Clang 17 con C++23, cuatro objetivos CTest correctos, 10 pruebas locales + 31 de lote + 215 comprobaciones del manifiesto y pruebas de CLI. No atribuirlo a Windows. La siguiente etapa curricular puede desarrollar la variante autónoma y la documentación/publicación del portafolio, pero **sin declarar acreditada** su reproducción o capturas en Windows. El proyecto educativo usa C++23 de forma explícita en CMake; las menciones de cxx_std_20 en U4L4–L5 fueron mínimos de versiones de referencia, no el estándar del proyecto completo. U4L3 continúa pendiente de reconstrucción tras editar el mensaje y capturas reales; U4L4–L6 necesitan MSVC/CMake/Ninja de Windows y evidencia según `P2_PROYECTO_LECTOR_LOCAL_WINDOWS`. El maestro físico `E:\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt` requiere confirmar sincronización con la copia versionada; no modificar el original local sin acceso. Conservar traducciones, IDs y progreso; Referencias permanece al final. Programación 1 cerrado; no iniciar Programación 3. Leer íntegro `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al reanudar.

## Base conservada de la importación

- Directorio de Work: `E:\Projects\LearnGameAndEngineDev-Work`, rama `main`.
- Origen: `E:\Projects\LearnGraphicsProgramming`, `main` `911118f2a92066569911e6785f4a98660ade0124`. Su historial se conserva en este repositorio local; el proyecto original permanece intacto.
- Importación verificada byte por byte: 1.727 archivos versionados, incluidos los 878 JSON curriculares, pruebas, modelos ingleses y audios Ogg existentes. Se conservaron también los dos archivos locales de `.codex-qa/u4-recorridos-anidados/`, incluido el ejecutable, excluidos de Git. Evidencia local: `.migration-qa/import-verification.json`.
- Registro vigente de pedidos: `PENDIENTES_RECURSOS_CURRICULARES.md`, editado exclusivamente por Work. Los pedidos Windows se trasladaron allí con sus identificadores; Codex informa únicamente en el chat.
- **Estado curricular al importar:** Programación 1 seguía abierto. Se conservan las seis correcciones editoriales, las dos predicciones y el editor de recuperación de frecuencias. Su importación no acredita aceptación Windows ni cierra las comprobaciones anteriores pendientes.
- **Estado de Windows:** base nativa, recorrido U1L2 e importación sintética incorporados. Consultar su aceptación y límites en el registro único. El progreso personal del teléfono permanece intacto y no se ha importado para QA.

## Recursos locales que se conservan en el origen

Las siguientes rutas parten de `E:\Projects\LearnGraphicsProgramming`. Son recursos reutilizables; conservar sus ubicaciones y contrastar su compatibilidad antes de adaptarlos. No publicar snapshots privados ni copiar entornos y cachés completos al nuevo Git.

- `.gradle/cpp-content-qa/` y `.gradle/math-content-qa/`: pruebas y evidencias anteriores. Sus resultados mantienen la plataforma, versión y alcance que acreditaron.
- `build/kokoro/downloads/1939ad2a8e416c0acfeecc08a694d14ef25f2231/`: modelo, voces y léxicos locales. `build/kokoro/audio/`: audios y textos de comprobación. Los recursos ingleses versionados ya están importados bajo `english_misaki/src/main/assets/`.
- `.gradle/clang23-android-arm64/`, `.gradle/cpp-runtime-cache/`, `.gradle/cmake-android-arm64/`, `tools/runtime-spike/` y `tools/wasmer-spike/`: compiladores, descargas y experimentos conservados. No son por sí mismos ejecutables ni una aplicación Windows.
- Submódulo `third_party/wasm-micro-runtime`: versión conservada `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`, origen `https://github.com/wasm-micro-runtime/wasm-micro-runtime.git`. El nuevo checkout conserva su referencia; no se ha inicializado ni se ha copiado el enlace `.git` del checkout anterior.

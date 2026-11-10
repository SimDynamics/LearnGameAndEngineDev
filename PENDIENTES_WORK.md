# Notas internas de continuidad de Work

Encargos activos sólo en PENDIENTES_RECURSOS_CURRICULARES.md.

Traslado de carpetas (2026-11-10): el código Git reside ahora bajo `E:\Projects`. En Git se corrigieron las referencias de Codex; falta comprobar la sincronización del maestro físico `E:\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt`. El gestor de ramas, los botones personales y su prueba `scripts/windows/Test-BranchButtons.ps1` quedan deliberadamente sin modificar para la revisión manual indicada por el usuario.

Continuar Programación 2 desde U4L6, posterior a las lecciones U4L4 (lectura local) y U4L5 (lote y duplicados). La siguiente sección debe incorporar gradualmente la lectura del archivo físico (LF y CRLF, última línea sin terminador, límite de 100 líneas), fallos globales y la publicación transaccional de stdout/stderr conforme al contrato U4L2. U4L3 aún requiere reconstrucción del ejecutable tras modificar el mensaje y capturas reales. La biblioteca local y la ampliación de lote necesitan reproducirse y comprobarse con MSVC/CMake/Ninja en Windows; las referencias de Linux no acreditan esa plataforma. La responsabilidad técnica queda en `P2_PROYECTO_LECTOR_LOCAL_WINDOWS` del registro único. Conserva las traducciones e identidades históricas; Referencias permanece al final. Programación 1 permanece cerrado; no iniciar Programación 3. Leer íntegro `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al reanudar.

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

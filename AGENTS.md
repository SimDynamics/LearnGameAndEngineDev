# LearnGameAndEngineDev: acuerdo de trabajo

Lee íntegramente `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al empezar, al reanudar y después de una compactación. La copia conserva el documento proporcionado por el usuario. Las instrucciones posteriores del usuario sobre Windows, directorios y registros prevalecen sobre las ubicaciones Android y el registro único mencionados en ese documento.

## Directorios y responsabilidades vigentes

- Work: `E:\PersonalFiles\Projects\LearnGameAndEngineDev-Work`, exclusivamente en `main`. Diseña y verifica currículo, estudia referencias, redacta contenido y especifica las capacidades necesarias.
- Codex: `E:\PersonalFiles\Projects\LearnGameAndEngineDev-Codex`, exclusivamente en la rama `Codex`. Implementa y verifica la aplicación Windows y sus componentes técnicos.
- Cada agente trabaja en su directorio. No cambiar de rama el checkout del otro, sobrescribir sus cambios, crear worktrees adicionales ni ejecutar sus botones de publicación.
- Guardar progresivamente en los archivos locales. Work no crea commits ni publica sin una instrucción expresa. La importación conserva los commits anteriores; no constituye una implementación Windows terminada.

## Registros con propietario exclusivo

- `PENDIENTES_WORK.md`: solo Work lo crea y modifica. Contiene los encargos, requisitos pedagógicos, integración curricular y aceptación de Work.
- `ACTUALIZACIONES_CODEX.md`: solo Codex lo crea y modifica. Contiene respuestas a los encargos, implementación, pruebas, evidencias y límites técnicos.
- Ambos pueden leer los dos registros. Enlazar el mismo identificador de encargo y su sección correspondiente; no copiar la especificación para mantener dos versiones ni modificar el registro del otro.
- Codex crea su registro cuando empiece su trabajo. Work no mantiene un segundo registro en su nombre.
- `PENDIENTES_RECURSOS_CURRICULARES.md` se conserva íntegro como referencia histórica Android, en modo de consulta. Los encargos y actualizaciones nuevos se escriben en el registro del propietario.
- Distinguir implementación técnica, integración curricular y aceptación comprobada en Windows. Las pruebas Android mantienen su alcance original y no acreditan Windows.

## Conservación y prioridad de migración

- El proyecto Android de origen permanece en `E:\PersonalFiles\Projects\LearnGraphicsProgramming`; no modificarlo ni limpiar sus recursos durante esta migración.
- Conservar el contenido importado, sus idiomas, IDs, respuestas, índices, mapas `stateLayout`, familias y direcciones históricas. No renombrar esos identificadores al cambiar el nombre del proyecto.
- Work produce y revisa únicamente español. Las traducciones existentes se conservan; su actualización corresponde al usuario.
- El contenido sigue en `app/src/main/assets/content/` hasta que Codex integre una ubicación común compatible y Work compruebe su conservación. No mantener dos copias activas del currículo.
- Primero recuperar en Windows lo ya construido. Programación 1 sigue abierto. No ampliar rutas ni comenzar la evaluación visual de proyectos antes de disponer de la base Windows y sus verificaciones correspondientes.
- Conservar los compiladores, modelos, audios, pruebas, preferencias y evidencias originales. No borrar cachés ni importar datos reales del estudiante para probar. Los recursos locales no versionados y sus ubicaciones constan en `PENDIENTES_WORK.md`.

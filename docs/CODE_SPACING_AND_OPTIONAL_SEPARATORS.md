# Legibilidad del código y separadores visuales opcionales (Windows)

Este contrato distingue los **saltos reales del código** de las **tarjetas vacías de presentación**.
No transforma automáticamente el código del alumno, no cambia la salida estándar y no regenera la identidad histórica.

## Comportamiento de los editores y ejemplos

- El código C++ y CMake mantiene sus espacios de indentación a ancho monoespaciado completo.
- Los saltos `LF` y las líneas vacías de `code`, puzles, fuentes editables y plantillas se conservan de la fuente original.
- No se reemplazan separaciones de `#define` con continuación, comentarios `//`, cadenas normales o raw strings, ni se confunde `\n` escrito en el literal con un salto físico de línea.
- La presentación y el ajuste de ancho no alteran los borradores, cursores, Undo, resultados `stdout`/`stderr`, ni las comparaciones `exact`.

## Declaración opcional, exclusivamente en la raíz

Work puede declarar entre 1 y 4 tarjetas puramente visuales por propietario `order_code` español con mapa v2:

```json
"orderVisualSeparators": [
  {"activity": "block_0003_0002", "count": 2}
]
```

La declaración debe añadirse a la raíz de la lección, **no dentro de** `fragments`,
`correctOrder` ni `stateLayout`. Cada `activity` debe corresponder a un único
`order_code` activo y no puede repetirse. La extensión no crea respuestas ni
coordenadas de progreso. Las listas de fragmentos y el orden correcto siguen
con sus mismos bytes, índices, identificadores y firmas.

## Interacción

Las tarjetas vacías adicionales aparecen después de las instrucciones al abrir.
Se pueden arrastrar, colocar entre grupos o dejar donde están. Con doble clic
o la tecla Supr se oculta una tarjeta opcional; Insert, con una tarjeta de código
enfocada, recupera la primera que se haya ocultado. Reiniciar devuelve la
presentación original. No se publica otro intento al mover, ocultar o restaurar
solamente una de estas tarjetas. El orden que se corrige, narra y guarda
contiene exclusivamente los índices de los fragmentos originales.

**Distinción esencial:** una tarjeta histórica que ya está en `fragments`,
aun cuando contenga espacios, sigue perteneciendo a su contrato histórico.
Su ubicación no se hace opcional por parecer vacía. Únicamente las tarjetas
adicionales declaradas mediante `orderVisualSeparators` son decorativas.
Los grupos de equivalencias previamente revisados tampoco se amplían.

En `drag_fill_code` y `inline_fill_code` se conservan los saltos auténticos
de `template`; un salto de presentación no añade un hueco `[]`, una ficha,
una elección ni una respuesta `correctOrder`. Cuando baste una separación
fija, no se añade una tarjeta arrastrable.

## Verificación automatizada

`scripts/curriculum/test_order_visual_separators.py` prueba la extensión
contra catorce puzles de orden previamente publicados, además de una
plantilla real de arrastre de cuatro huecos. Conserva mapas, respuestas,
traducciones y hashes. `native.order_spacing_contract` comprueba que las
tarjetas históricas siguen evaluándose exactamente igual y que el estado
y las referencias sobreviven la serialización.

`native.order_code_whitespace_view` comprueba con los controles Win32 reales
la indentación completa, las líneas vacías, la interacción con tarjetas
opcionales, la numeración, la narración y que los ejemplos C++/CMake
conservan sus bytes. Ambas pruebas forman parte de ReaderBuild.

El alumno continúa realizando la comprobación visual y auditiva durante
el estudio. Work es responsable de agregar o corregir separaciones de
contenido con las revisiones curriculares compatibles. Codex no cambia los
JSON históricos ni el MD de pendientes.

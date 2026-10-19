# Énfasis parcial opcional en párrafos

Los bloques `paragraph` pueden declarar `emphasis`, un array opcional de fragmentos **exactos del campo `text` existente**. No se interpreta Markdown ni HTML y no se modifica, duplica ni normaliza el texto visible o narrado.

```json
{
  "type": "paragraph",
  "text": "Una biblioteca reúne recursos preparados. Comprueba el resultado.",
  "emphasis": [
    { "text": "biblioteca" },
    { "text": "Comprueba el resultado" }
  ]
}
```

Cada fragmento debe aparecer una sola vez. Si se repite, `occurrence` selecciona explícitamente su aparición, contando desde 1: `{ "text": "valor", "occurrence": 2 }`. Se cuentan coincidencias exactas, incluidas las superpuestas. Las decoraciones ausentes, ambiguas, obsoletas o inválidas se ignoran; el párrafo completo permanece disponible. Los offsets internos conservan los caracteres UTF-16 originales y nunca cortan un par sustituto.

El renderer añade únicamente un `StyleSpan(BOLD)` a los fragmentos designados y conserva los demás spans, colores de sintaxis, saltos, citas y rangos de lectura. El ancho, los márgenes y la separación del componente siguen los parámetros existentes; como cualquier negrita nativa, los glifos designados pueden redistribuirse dentro del mismo ancho. No se adivina qué idea es central ni se cambia código, respuesta, identidad, progreso, narración o caché. El énfasis no cambia las voces ni introduce marcas en el audio.

Los JSON existentes no requieren migración y no adquieren énfasis automático. Work decidirá después qué fragmentos merecen énfasis y reaceptará en Honor los párrafos curriculares registrados; las pruebas técnicas usan fixtures aisladas, sin editar lecciones.

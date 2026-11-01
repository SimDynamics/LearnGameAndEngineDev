# Copia rígida de un ángulo sin coordenadas visibles

Contrato optativo `schemaVersion: 1`, independiente del currículo existente. Una respuesta
`type: "math_rigid_angle_copy"` usa `validator.type: "rigid_angle_copy"`. No se modifica
ningún tipo, valor predeterminado, respuesta ni huella anterior. Work integrará la práctica
y realizará su aceptación curricular; los ejercicios actuales de interpretación de dibujos
no se sustituyen ni acreditan esta producción libre.

La configuración geométrica está únicamente en el validador y se comparte con la vista:

```json
{
  "id": "superposicion",
  "type": "math_rigid_angle_copy",
  "prompt": "Mueve y gira la copia conservando los lados correspondientes y la región.",
  "validator": {
    "type": "rigid_angle_copy",
    "reference": {
      "vertex": "O", "sides": ["oa", "ob"], "region": "counterclockwise",
      "diagram": {
        "points": [
          {"id":"O","x":0,"y":0,"labels":["O"]},
          {"id":"A","x":4,"y":0,"labels":["A"]},
          {"id":"B","x":0,"y":5,"labels":["B"]}
        ],
        "objects": [
          {"id":"oa","kind":"ray","through":["O","A"],"contains":["O","A"]},
          {"id":"ob","kind":"ray","through":["O","B"],"contains":["O","B"]}
        ]
      }
    },
    "copy": {
      "vertex": "V", "sides": ["vc", "vd"], "region": "counterclockwise",
      "diagram": {
        "points": [
          {"id":"V","x":0,"y":0,"labels":["V"]},
          {"id":"C","x":6,"y":0,"labels":["C"]},
          {"id":"D","x":0,"y":3,"labels":["D"]}
        ],
        "objects": [
          {"id":"vc","kind":"ray","through":["V","C"],"contains":["V","C"]},
          {"id":"vd","kind":"ray","through":["V","D"],"contains":["V","D"]}
        ]
      }
    },
    "initial": {"x":7,"y":4,"rotation":0.6},
    "relation":"congruent",
    "positionTolerance":0.15,
    "directionTolerance":0.025
  }
}
```

Cada figura utiliza el modelo euclidiano existente: un vértice declarado, exactamente dos
semirrectas completas originadas allí y sus puntos/nombres/incidencias. Los IDs son locales
a cada figura. `sides[0]` se corresponde con `sides[0]` y `sides[1]` con `sides[1]`; no se
adivina la correspondencia a partir de distancias o nombres. La región se recorre desde el
primer lado hasta el segundo en el sentido interno `clockwise` o `counterclockwise`.
Esta primera versión admite regiones habituales estrictamente entre 0 y π; rechaza las
degeneradas y reflejas antes de crear el control. No se muestran ángulos numéricos, ejes,
cuadrículas, escalas ni campos x/y.

La configuración debe admitir la colocación pedida: mismo sentido de recorrido regional
con la correspondencia declarada, y aperturas iguales para congruencia o compatibles
con el sentido de inclusión solicitado. Se rechazan tareas imposibles como fallos técnicos
antes de pedir respuesta. Con una correspondencia fija, un recorrido regional opuesto no
puede corregirse mediante traslación y rotación; no se penaliza al estudiante por esa configuración.
Esto no impide la comprobación de p6: se puede declarar que el lado inferior de la copia
se corresponde con el primer lado de referencia y su otro lado con el segundo. Invertir
explícitamente el orden de `sides` y describir **la misma región** con el recorrido inverso
permite girar esa copia sin reflejarla ni cambiar su interior. La correspondencia nunca
se cambia automáticamente para aceptar una respuesta.

`initial` sitúa el vértice de la copia en las coordenadas internas declaradas y aplica una
rotación en radianes alrededor de ese vértice. Durante el intento sólo cambian esos tres
parámetros; nunca se mueven independientemente puntos ni se escala/deforma la copia.
Las semirrectas se extienden hasta los límites de la superficie con sus flechas, independientemente
de las distancias de los puntos usados para nombrarlas. Las marcas curvas identifican
las regiones elegidas, sin convertirse en lados adicionales.
Al superponer figuras, se reservan las marcas y puntas de flecha antes de colocar los
nombres. Una búsqueda local de espacios libres recoloca sólo los rótulos, con líderes a
sus puntos originales; sus fondos no borran las marcas ni las alas de las flechas.

Los modos **Mover copia**, **Girar copia** y **Vista** distinguen la transformación geométrica
de la navegación existente. En el modo Giro, arrastrar alrededor del vértice gira la figura
sin trasladarlo; el entorno inmediato del vértice evita una rotación inestable. El modo Vista
y el gesto de dos dedos mantienen el zoom/desplazamiento del viewport existente. La vista
mantiene un encuadre interno fijo durante el intento, por lo que mover la copia no recoloca
la referencia. Ampliar utiliza la misma instancia y conserva sus controles y colocación.
No hay ajuste automático al resultado ni selección de un dibujo ya resuelto. Los arrastres
de traslación mantienen sus marcas dentro del lienzo con un margen; un giro que sacaría
marcas fuera conserva la colocación anterior para permitir trasladar primero la copia.
**Restablecer copia
inicial**, también disponible ampliado, recupera el estado inicial sin mostrar una solución.

`relation` admite `congruent`, `copy_inside_reference` o `reference_inside_copy`.
Siempre se comprueban primero el vértice, el primer lado correspondiente **como semirrecta
dirigida** y el mismo lado de la recta común para la región. Sólo entonces se comprueba el
otro lado: coincidencia completa para congruencia o inclusión en el sentido solicitado.
La inclusión admite igualdad. No se exige que coincidan los puntos auxiliares que nombran
los lados. Un giro completo adicional produce la misma colocación válida.

Ambas tolerancias son obligatorias: `positionTolerance` en unidades internas del dibujo,
positiva y finita; `directionTolerance` en radianes, positiva, finita y menor que 0.1. Son
**tolerancias operativas del gesto**, no medidas mostradas ni una afirmación de igualdad
matemática exacta de dos entradas aproximadas. Deben elegirse pequeñas para el dibujo.
Las posiciones internas se limitan a magnitudes de hasta 10⁹ para conservar cálculos finitos.
El mensaje de aceptación expresa explícitamente ese límite. Un sentido contrario se
rechaza independientemente de la tolerancia de colinealidad. La orientación de un fallo
nombra exclusivamente la condición incumplida, sin dar una transformación final ni una
respuesta alternativa. Una configuración inválida o un tipo de respuesta incorrecto es
un fallo técnico no evaluable, no un error del estudiante.

La respuesta tipada `MathAnswer.RigidAngle` guarda sólo la colocación `{x,y,rotation}`,
no otra fuente de geometría. El códec y el almacén normal de `MathPuzzleRenderer` conservan
el borrador parcial, los reintentos, la respuesta y su validación mediante la huella del
bloque, incluidas reapertura y recreación. No se añade otro historial ni otra preferencia.
Los controles quedan bloqueados tras el acierto por el mecanismo existente.

## Presentación Windows y conservación numérica

El adaptador Windows normaliza únicamente las coordenadas del dibujo antes de
utilizar el trazador euclidiano compartido. Reducir homogéneamente una figura y
su tolerancia de posición, por ejemplo por un factor de `1e-6`, conserva su
encuadre relativo y el significado geométrico de los gestos. La configuración,
las unidades internas, la colocación guardada y el evaluador permanecen intactos.

Una copia guardada fuera del encuadre conserva su colocación exacta. El dibujo
recorta los trazos y omite las marcas, nombres y regiones que no pueden verse;
no convierte posiciones lejanas en enteros gráficos de Windows ni desplaza la
respuesta para hacerla visible. Un giro evita desbordamientos en sus productos
intermedios. Mover la copia o usar Restablecer permite recuperarla mediante los
controles existentes; abrir o pintar el borrador no realiza esa recuperación.

`WindowsRigidAngleLayoutTest` verifica escala, primitivas visibles y conservación
de colocaciones finitas extremas mediante el worker real. `native.math_puzzle_view`
comprueba traslación y giro, pintura GDI, reapertura exacta y Restablecer a anchos
de 850 y 400 píxeles y escalas de texto del 100 y 150 %. Son comprobaciones
técnicas Windows; la integración curricular y su aceptación visual y de escucha
se mantienen separadas.

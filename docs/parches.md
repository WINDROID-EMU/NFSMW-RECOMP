# Los parches

Catálogo de lo que este proyecto cambia en el SDK, por qué, y cómo se comprobó.

Todos se aplican sobre el fuente de `..\rexglue-sdk` antes de compilarlo. Ninguno toca
el código del juego.

## Cómo se usan

```bat
python tools\parche_velocidad.py              aplicar
python tools\parche_velocidad.py --estado     ver qué hay puesto
python tools\parche_velocidad.py --revertir   deshacer
```

`CONSTRUIR.bat` los aplica todos en el orden correcto. El orden importa en un caso:
`parche_anillo.py` va antes que `parche_desatasco.py`, porque el segundo se apoya en las
cabeceras (`<atomic>`, `<chrono>`) que mete el primero. El script de desatasco se niega
a aplicarse si el otro no ha pasado.

## El catálogo

### `parche_desatasco.py` — el cuelgue del audio

**El importante.** Sin él, después del prólogo, al salir del garaje el audio se muere y
al volver al menú el juego se congela.

Toca `xboxkrnl_audio_xma.cpp`. Cuando un contexto XMA lleva más de 250 ms girando sin
entrada y con la lectura pegada a la escritura, le da la señal de "buffer terminado" que
el propio juego sabe interpretar.

Es un apaño deliberado: rompe el atasco en vez de evitarlo, y puede costar un tropiezo
de audio en esa voz. La historia completa —y por qué el arreglo "obvio" era el
equivocado— está en [diario/audio-cuelgue.md](diario/audio-cuelgue.md).

**Comprobado:** el usuario jugó la zona que lo reproducía sin que se colgara.

### `parche_anillo.py` — instrumentación del XMA

Prerrequisito del anterior. Añade trazas a las funciones del kernel del XMA. Con el
nivel de log normal no imprime nada.

Un detalle que costó una tarde: los *getters* van limitados a una traza por segundo,
pero los *setters* no. Limitar los dos por igual escondía justo lo que había que ver
—las entregas de entrada— y el diagnóstico se fue por el camino equivocado.

### `parche_presentador.py` — vsync y límite de fps

De fábrica **ninguno de los dos funciona**:

- `vsync` existe como cvar pero no sincroniza nada. Se lee en un solo sitio y solo
  decide si el procesador de comandos duerme o gira en las esperas del guest. El
  `Present` del presentador de D3D12 llevaba el `SyncInterval` clavado a 0.
- No había ningún limitador de fps. Ninguno.

Este parche arregla las dos cosas. El lanzador avisa en rojo si no está aplicado.

### `parche_gpu_fallback.py` — no morir sin GPU

Si el dispositivo D3D12 no se puede crear, cae a WARP en vez de dar una pantalla de
error. Útil en máquinas sin drivers decentes.

### `parche_backend.py` — selector de API gráfica

Añade el cvar `gpu_backend` (`d3d12` / `vulkan`) y se lo pasa al cargador del plugin.

El plugin ya sabía elegir por nombre; lo único que faltaba era que alguien se lo dijera.
`rex_app.cpp` llamaba a `LoadGpuPlugin` con un solo argumento, así que siempre salía
`any`, que en la práctica es D3D12 por ser el primero del `if`.

Tres cosas que este parche aprendió por las malas:

- Es `kRequiresRestart`, no `kInitOnly`. Con `kInitOnly` el menú lo pintaba en rojo y
  no se podía tocar.
- **No hay opción `any`.** Con `any` no se sabía cuál estaba puesta de verdad, que era
  justo lo que había que enseñar.
- Limpia la lista de reinicios pendientes al terminar el arranque, porque si no el menú
  abría con un aviso falso permanente. Ver [arquitectura.md](arquitectura.md).

Si la API elegida no está compilada, cae a la otra y lo dice en el log en vez de no
arrancar.

**Comprobado:** tres pasadas seguidas idénticas, revertir y volver a aplicar devuelve el
mismo resultado, sin restos de versiones anteriores.

### `parche_restaurar.py` — el menú de F4

Cinco bloques en `settings_overlay.cpp`:

- Aviso de reinicio pendiente, con botón para guardar y reiniciar. El SDK ya llevaba la
  cuenta (`GetPendingRestartFlags`) pero no la enseñaba en ninguna parte, así que
  cambiar la API gráfica parecía no hacer nada.
- Foto de la configuración de arranque, para poder volver a ella.
- Botón "Restore defaults" que restaura **esa** foto, no los valores de fábrica del SDK.
- Deslizador para los ajustes decimales con límites, en vez de una caja de texto.
- La API gráfica en uso, leída del registro de cvars.

Ese último punto tiene una lección cara detrás. La primera versión usaba una variable
global compartida con `rex_app.cpp` y **no enlazaba**:

```
lld-link: error: undefined symbol: rex::ui::g_gpu_backend_en_uso
```

`rex_app.cpp` no se compila dentro del SDK: se **instala como fuente** en
`share/rexglue/` y lo compila cada aplicación. Así que la definición acababa dentro de
`nfsmw.exe` y la referencia dentro de `rexruntime.dll`. Para hablar entre módulos está
el registro de cvars.

### `parche_velocidad.py` — velocidad del juego

Añade `game_speed`, en porcentaje, de 0 a 200. No es un límite de fps: cambia a qué
ritmo pasa el tiempo dentro del juego.

En porcentaje y no en multiplicador porque en la ventana de F4 sale un número pelado y
"1.0" no dice de qué. Con un suelo en 0.1% porque un cero literal no cuelga el juego: lo
tumba, por la división de `RecomputeGuestTickScalar`.

### `parche_privilegios.py` — la puerta del multijugador

De fábrica `XamUserCheckPrivilege` deniega **todos** los privilegios, siempre. El
comentario original lo dice: *"If we deny everything, games should hopefully not try to
do stuff"*. En Most Wanted el efecto es el cartel "Los privilegios que tienes en Xbox
Live no te permiten acceder a esta función".

Añade el ajuste `grant_user_privileges`, **apagado por defecto**. Encendido, contesta
que sí a todo.

Abre la puerta del menú y nada más. Lo que hay detrás no funciona; ver
[diario/red-y-privilegios.md](diario/red-y-privilegios.md).

### `parche_diagnostico.py` — trazas del arranque

Instrumentación general que se quedó porque es barata y útil. Entre otras cosas es lo
que puso nombre y hora al cuelgue del audio.

### `parche_vulkan_opt.py` — otimizações de GPU na API Vulkan

Melhora expressiva de taxa de quadros e eliminação de engasgos (stuttering) no Vulkan:

- **Desativa o descarte de quadros incompletos**: `vulkan_async_skip_incomplete_frames = false`. No código original, qualquer frame que usasse um pipeline com shader assíncrono em compilação era totalmente descartado da apresentação, gerando quedas de 10-20 FPS por segundo.
- **Batching de submissão**: `vulkan_submit_on_primary_buffer_end = false`. Evita submissões repetitivas desnecessárias ao queue Vulkan a cada fim de buffer primário.
- **Caminho EDRAM padrão**: `render_target_path_vulkan = "fbo"` (Host Render Targets, rápido e sem sobrecarga de FSI).
- **Priorização de GPU dedicada**: pontua `VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU` com +1000 pontos em `vulkan_provider.cpp`, garantindo a escolha da GPU de maior desempenho.
- **Limitador de FPS suave**: adiciona suporte a `max_fps` no apresentador Vulkan (`vulkan_presenter.cpp`).

### `tools/diagnostico/parche_xma.py` — instrumentación pesada del XMA

**Fuera del build por defecto.** Traza por segundo del hilo de audio, cada envío y cada
silencio. Se aplica a mano para investigar y se revierte después.

---

## Por qué los parches están escritos así

No es capricho. Cada regla viene de un fallo concreto.

### Sustitución de texto exacta, sin `.original`

La primera versión guardaba una copia del fichero antes de tocarlo. Deja de funcionar en
cuanto **dos parches tocan el mismo fichero**: el segundo guarda como "original" un
fichero que ya estaba parcheado, y revertir deja el árbol en un estado que no es ni el
de antes ni el de después.

Ahora cada parche aplica y deshace por texto, y no guarda nada.

### Bloque a bloque, no un marcador por fichero

Hubo una versión con un solo marcador por fichero: si estaba, el parche se daba por
aplicado. El día que se le añadió un bloque nuevo a un parche ya aplicado, **no hizo
nada** y no dijo nada. El síntoma fue *"abrí el exe y no tenía la barra para cambiar la
velocidad"*.

Ahora cada bloque se comprueba y se aplica por separado.

### Se niegan a escribir si un anclaje no aparece exactamente una vez

Un parche a medias es peor que uno que falla. Si el SDK cambió y el anclaje ya no está,
o está dos veces, el script sale sin tocar nada.

### La regla de migración, que costó tres intentos

Cuando cambias un parche que ya estaba aplicado en algún sitio, hay que quitar la
versión vieja antes de poner la nueva. Y ahí hay dos trampas simétricas:

- **El bloque viejo es un trozo del nuevo** (se le añadió código). Buscar el viejo lo
  encuentra *dentro* del bueno, y sustituirlo por el anclaje le corta la cabeza al
  bloque recién puesto. Luego se vuelve a aplicar y la cola queda **duplicada**. El
  fichero crecía en cada pasada.
- **El bloque nuevo es un trozo del viejo** (se le quitó código). Entonces "el bloque
  bueno está" da que sí aunque lo que hay siga siendo el viejo entero, y el script se da
  por aplicado dejando dentro código muerto.

Intenté resolverlo con una *huella* por versión: un trozo que solo estuviera en esa
versión. No siempre existe: cuando el viejo es prefijo exacto del nuevo, todo lo que hay
en el viejo está también en el nuevo.

La regla que sí vale no necesita huellas:

```python
es_de_verdad_vieja = (viejo in txt) and (viejo not in nuevo or nuevo not in txt)
```

Los dos casos salen bien con eso, y se comprueba solo con los textos.

**Y se prueba corriendo el parche dos veces seguidas y comparando.** El fallo del
duplicado no se ve en la primera pasada, que es la única que se suele mirar.

### Idempotencia

Consecuencia de lo anterior, pero merece decirse aparte: ejecutar un parche N veces
tiene que dar el mismo fichero que ejecutarlo una. Si no, `CONSTRUIR.bat` corrompe el
árbol un poco más en cada reconstrucción.

### Un detalle de Windows

Los parches leen y escriben en modo texto. En Windows eso conserva los CRLF; en Linux
los convierte a LF en la primera escritura. Si comparas resultados entre plataformas,
normaliza los finales de línea antes de gritar.

# Pipeline nativo de Vulkan para NFS Most Wanted

Un camino de pintado hecho a medida de este juego, que sustituye a la emulación
de la GPU de la Xbox 360 en vez de arrastrarla.

> Estado: **en construcción**. La fase 1 (censo) está hecha y medida. El camino
> actual sigue siendo el de por defecto y no se toca.

## Por qué

Lo que nos frena, medido en este proyecto y no supuesto:

| Coste | Cuánto | De dónde sale |
|---|---|---|
| Traducir cada dibujo | **~8 µs de CPU**, 2.500–4.700 dibujos por fotograma en carrera | Estado genérico de Xenia |
| Troceado en franjas | La escena se pinta **tres veces** (la predicación solo se salta el 0,5 %) | El juego reparte 1280x720 con MSAA 4x en los 10 MB de EDRAM |
| MSAA 4x | La mitad del tiempo de GPU del menú | Cada pase mueve cuatro muestras entre GPU y memoria |

### En qué se van esos 8 µs por dibujo

Medido el 2026-09-21 cronometrando las fases de `IssueDraw` por separado, en
carrera (48,8 ms de dibujos, 3.062 dibujos):

| Fase | ms | % |
|---|---|---|
| Cola de `IssueDraw` (tras los descriptores) | ~15 | **~29 %** |
| `primitive_processor_->Process` | 12–14 | **~27 %** |
| `UpdateBindings` (descriptores y constantes subidas) | 8,3–9,2 | ~18 % |
| Cabeza de `IssueDraw` | ~8 | ~16 % |
| `texture_cache_->RequestTextures` | 2,8–3,0 | ~6 % |
| `render_target_cache_->Update` (**EDRAM**) | 1,1–1,3 | **~2 %** |
| `ConfigurePipeline` | 0,6–0,7 | ~1 % |
| `UpdateSystemConstantValues` | 0,4–0,5 | ~1 % |
| `UpdateDynamicState` | 0,1 | ~0 % |
| Barreras y entrada al pase | 0,1 | ~0 % |

Y `GPU 0,2–3,6 ms` de 68–88 ms de fotograma: **limitado por CPU por un factor
de veinte**. Por eso bajar la resolución interna no ayudaría: quita trabajo de
píxel, que ya es casi gratis.

Esto **corrige dos cosas que este documento daba por buenas**:

- La emulación de EDRAM cuesta **~1,2 ms por fotograma, no ~4**. Tirarla sigue
  valiendo para quitar el troceado, pero no es una palanca de rendimiento por
  sí misma.
- **No hay un único punto caliente.** El coste está repartido entre cuatro
  tramos de 16–29 % cada uno, y las piezas que parecían caras (pipeline,
  constantes, estado dinámico) son calderilla. `UpdateBindings` ya lleva
  control de suciedad y solo sube lo que el juego cambia, que en carrera es
  cada dibujo.

De ahí la conclusión que manda el plan: **como el coste es difuso, la palanca
está en hacer MENOS dibujos, no en abaratar cada uno.** Y el 40 % de los
dibujos de un fotograma son repeticiones de franja.

(Aviso sobre la medida: la sonda hace 22 lecturas de reloj por dibujo, así que
infla el remanente. Las proporciones valen; los valores absolutos de los
tramos sin nombre, con reservas.)

## Qué se queda y qué se tira

| Pieza | Qué pasa con ella |
|---|---|
| Recompilación de la CPU | **Se queda tal cual.** Es la base del proyecto y funciona |
| Kernel, audio, sistema de ficheros, entrada | Se quedan |
| Traductor de shaders (microcódigo Xenos → SPIR-V) | **Se queda.** Es lo que permite conservar el aspecto del juego sin reescribir sus shaders |
| Caché de texturas por dirección de invitado | Se queda, adaptada |
| Lectura del anillo y paquetes PM4 | **Se queda**, porque es barata (2,6 ms por fotograma) y es donde llega el estado |
| EDRAM: volcados, resolves, transferencias de propiedad | **Fuera** |
| Caché de objetivos de render de Xenia | **Fuera**: objetivos de Vulkan de verdad, a resolución nativa |
| Troceado en franjas y MSAA del invitado | **Se ignoran**: se pinta una vez |
| Camino genérico por dibujo (descriptores y constantes al vuelo) | **Fuera**: estado específico de este juego, cacheado |

La decisión de fondo: **no se intercepta D3D, se intercepta el estado**. El
juego escribe registros y dibujos en el anillo; ese flujo ya nos llega y es
barato de leer. Lo caro es lo que hacemos después con él. Así que el pipeline
nativo se engancha en `IssueDraw`, no en las funciones de D3D del juego, y eso
evita tener que identificar y reimplementar decenas de funciones de la
biblioteca de D3D dentro del XEX.

## Lo que dice el censo

Medido el 2026-09-20 en el móvil de pruebas con
`tools/diagnostico/censo_pipeline.py`, un fotograma de menú y uno de carrera de
verdad (vuelta 1 de 3, 31 fps, 32,5 ms). Los dos con el MSAA del juego **sin
enmascarar**, para ver lo que pide y no lo que le dejamos pedir.

| | Menú | Carrera |
|---|---|---|
| Dibujos | 803 | **4.570** |
| Pases (cambios de objetivo) | 7 | **9** |
| Parejas de shaders distintas | 15 | **31** |
| Configuraciones de objetivo | 6 | 8 |
| Estados de mezcla/profundidad | 7 | **9** |
| Juegos de texturas | 5 | 7 |
| Resolves | 16 | 23 |

**Esa es la noticia**: 4.570 dibujos para 31 parejas de shaders y 9 estados de
mezcla. Ciento cuarenta dibujos por cada combinación de estado distinta. El
camino actual monta el estado dibujo a dibujo, a ~7,4 µs cada uno (19,18 ms de
op22 para 2.598 dibujos, medido en la misma sesión), y lo que monta se repite
ciento cuarenta veces.

### La forma del fotograma

Un fotograma de carrera son **nueve pases**, ni uno más. Éste tiene 4.570
dibujos:

| Pase | Dibujos | Objetivo | Qué es |
|---|---|---|---|
| 1 | 1.054 | pitch 1600, 1x | Sombras |
| 2 | 313 | pitch 640, 1x | Retrovisor |
| 3 | 438 | pitch 280, MSAA 4x | Reflejos: seis caras de 256x256 |
| 4 | **2.748 = 3 × 916** | pitch 1280, **MSAA 4x** | La escena, **pintada tres veces** |
| 5-8 | 7 | pitch 320 / 80 / 320 / 160 | Cadena de bloom |
| 9 | 10 | pitch 1280, 1x | Posprocesado |

Los resolves confirman a dónde va cada cosa: la escena no se resuelve entera
sino en tres tiras, `1280x256 + 1280x256 + 1280x208` = 720 filas, consecutivas
en memoria (una cada `0x140000`), más otras tres iguales de profundidad. Los
reflejos salen como seis resolves de `256x256` seguidos, uno cada 256 KB, que
es un mapa cúbico. Las sombras, dos de `1600x1600`.

### Lo que sobra, medido

El troceado hasta ahora se deducía. Ahora **se mide**: el censo compara la
secuencia de dibujos del pase consigo misma y encuentra que el pase 4 son tres
bloques **idénticos**, de 916 dibujos cada uno. Los otros ocho pases no se
repiten: una sola pasada cada uno.

Así que de los 4.570 dibujos del fotograma, **1.832 son repeticiones puras**
—el 40 %— y desaparecen en cuanto no haya una EDRAM que obligue a trocear. A
los ~7,4 µs por dibujo que cuesta el camino actual, son **unos 13,6 ms de CPU
por fotograma**, sobre los 32,5 ms que tarda ahora.

Dos cuentas que **no** valen, para que nadie las use:

- "613 dibujos distintos de 2.814". Esa huella es shaders + primitiva + número
  de índices, y dos dibujos legítimamente distintos la comparten. Sirve para
  ver lo repetitivo que es el fotograma, no para medir el troceado.
- La periodicidad sola. Solo detecta bloques **exactamente** iguales, y la
  predicación se come algún dibujo suelto en alguna franja: en un fotograma con
  2.801 dibujos en el pase de la escena no encontró nada, porque 2.801 no es
  divisible por 3.

Lo que lo mide sin ambigüedad son los búferes indirectos, abajo.

### Cómo trocea el juego, exactamente

El juego graba la lista de dibujos en búferes indirectos y **los reenvía tres
veces**. Medido: doce búferes, cada uno ejecutado 3 veces en el fotograma. Entre
una vuelta y la siguiente cambian **dos registros y nada más**:

| Vuelta | `PA_SC_WINDOW_OFFSET` | `PA_SC_WINDOW_SCISSOR` (filas) |
|---|---|---|
| 1 | `00000000` → y = 0 | 0 – 256 |
| 2 | `7F000000` → y = **−256** | 256 – 512 |
| 3 | `7E000000` → y = **−512** | 512 – 720 |

(el offset va como pareja de enteros de 15 bits con signo: `0x7F00` = −256.)

O sea: la misma geometría tres veces, subida 0, 256 y 512 filas, para que el
tercio que toca caiga siempre en las filas 0-255 de la EDRAM, que es lo único
que cabe. 256 + 256 + 208 = 720, y encaja con las tres tiras de los resolves.

El resolve saca su rectángulo de origen de los vértices que manda el propio
juego, **le suma `PA_SC_WINDOW_OFFSET`** y lo recorta con el scissor —que
también lleva el offset sumado (`util/draw.cpp`, `GetResolveInfo` y
`GetScissor`). De ahí sale el camino para pintarlo de una pasada:

1. Forzar `PA_SC_WINDOW_OFFSET` a cero todo el fotograma. Los dibujos caen en
   su sitio real y, de propina, los tres resolves pasan a leer las filas
   0-256, 256-512 y 512-720 en vez de las 0-256 las tres veces.
2. Forzar el scissor a pantalla completa **mientras se dibuja**, para que la
   única vuelta que se ejecute no se recorte a su tira.
3. Ejecutar cada búfer indirecto una sola vez por fotograma.

Con la EDRAM todavía en medio esto **solo cabe sin MSAA**: 1280x720 son 3,5 MB
de color más 3,5 de profundidad, siete de los diez. Con MSAA 4x no cabe, y por
eso existe el troceado. Así que el salto de franjas y `gpu_sin_msaa` van juntos
hasta que la fase 2 ponga objetivos nativos y quite esa atadura.

El orden real de las llamadas confirma la estructura, y es un ciclo de diez:

```
09F260C0 (1654 bytes)     <- monta la franja: mueve la ventana y resuelve
nueve búferes de dibujos  <- franja 1
09F260C0                  <- monta la franja 2
los MISMOS nueve búferes  <- franja 2
09F260C0                  <- monta la franja 3
los MISMOS nueve búferes  <- franja 3
```

### Lo que se implementó, y el error que costó dos cuelgues

Está en `tools/parche_una_pasada.py` (cvar `nfsmw_una_pasada`, apagado por
defecto).

La primera versión **se saltaba el búfer indirecto entero** en las vueltas 2 y
3. El juego se colgaba en el vídeo de arranque, con los hilos esperando
semáforos: dentro de esos búferes no solo hay dibujos, hay *fences* y eventos
que el juego espera. La versión buena deja pasar el búfer completo y **tira
solo los paquetes de dibujo**. Se ahorra lo mismo —el dibujo es lo que cuesta
7,4 µs— y todo lo demás se sigue ejecutando.

El reconocimiento de "esta vuelta es una repetición" no necesita rastrear
punteros: basta con que el juego haya pedido un `PA_SC_WINDOW_OFFSET` distinto
de cero. La primera vuelta siempre va con offset cero, y los pases que no se
trocean (sombras, retrovisor, reflejos) también.

**Medido:** 669–802 dibujos por fotograma tirados en carrera, y ~35 fps frente
a ~30 sin el ajuste (con calor y escenas distintas, así que no es un A/B
limpio).

**Y la imagen NO sale correcta.** La primera captura salió bien y di por bueno
que no corrompía; fue precipitado. Con más capturas aparece **la mitad derecha
en negro con una mancha blanca**, el mismo fallo que tenía `nfsmw_una_franja`.
Es intermitente, lo que apunta a la unión del recorte aprendida: mientras no se
ha aprendido, o si cambia de escena, la única pasada se queda con el recorte de
su tira.

### La causa probable de la corrupción, y su arreglo (2026-09-26, sin probar)

Leyendo `GetResolveInfo` (`util/draw.cpp`) aparecen dos fallos del parche, y
cualquiera de los dos basta para romper la imagen:

1. **El resolve escribía en el sitio equivocado.** Saca la dirección de destino
   de las mismas coordenadas que el origen: el origen, con el desplazamiento de
   ventana sumado (líneas ~1015-1026); el destino, igual (~966-997). Con el
   desplazamiento a cero, la franja 2 lee bien las filas 256-512 de la EDRAM,
   pero las escribe 256 filas por debajo de la dirección que dio el juego para
   ella. La franja 3, 512. Así caen fuera de su textura y machacan memoria.
2. **Los resolves de las franjas 2 y 3 se tiraban.** Un resolve es un paquete
   de dibujo con `RB_MODECONTROL` en modo copia, y va con el desplazamiento de
   su franja puesto. El parche tiraba todo paquete de dibujo con desplazamiento
   distinto de cero, así que esas dos franjas no llegaban nunca a su textura.

Además, el recorte se decidía al escribir el registro, y el juego a veces lo
escribe antes que la superficie nueva. Eso lo apuntaba a la superficie anterior
y le abría el recorte a una que no se trocea.

La versión nueva de `tools/parche_una_pasada.py` (dos ficheros):

- El resolve **lee** las filas de verdad y **escribe** donde el juego lo espera:
  el destino se calcula con el desplazamiento que pidió, que se guarda en
  `nfsmw_una_pasada_ventana`.
- No tira los resolves: solo los dibujos (`edram_mode` distinto de copia).
- El recorte y qué sobra se deciden **al dibujar**, con la superficie de verdad.
- Solo tira dibujos de una superficie a la que ya se le abrió el recorte, así
  que ninguna franja se puede perder. Sin haber aprendido, las vueltas 2 y 3 se
  pintan en su sitio real (el desplazamiento ya es cero) y la imagen sale
  igual, solo que sin la ganancia.

**Probado en el móvil (2026-09-26):**

- Arranca y pinta. Actúa: tira ~640-980 dibujos por fotograma.
- En las capturas de la pantalla de título, la carrera de demostración y una carrera de verdad,
  la imagen sale completa.
- Pero jugando se ve **a ratos media pantalla estropeada**. El parón del arranque (pantalla
  negra, 0 fps) es el de siempre: el vigilante salta igual con el ajuste apagado.

**La causa de verdad (encontrada en el log, 2026-09-26).** Con la corrupción en pantalla, el log
tenía 5.124 veces `Resolve region 640 <= x < 1280 is outside the surface pitch 640`. El juego
resuelve la mitad derecha de la imagen desde una superficie de 640 de ancho, llevándola a x 0-640
con `PA_SC_WINDOW_OFFSET` **x** = −640. El parche ponía el registro entero a cero, la x incluida.
Así ese resolve caía fuera de la superficie, el SDK lo rechazaba y la mitad derecha no llegaba
nunca a la imagen. Además, los dibujos de esa mitad (desplazamiento x distinto de cero) se tiraban
como si fueran repeticiones de franja.

Arreglo: solo se toca la **y** del desplazamiento, que es lo único que mueven las franjas.
Probado: **0** resolves rechazados y la imagen completa en carrera, tirando ~480 dibujos por
fotograma. Los dos arreglos de antes (el destino del resolve y la predicación) salieron de leer
el código, no del fallo que se veía. Se quedan porque corrigen casos reales, pero no eran esto.

**Lo que queda por vigilar: la predicación.** En cada franja el juego solo deja pasar
los dibujos que la tocan (`SET_BIN_MASK`, comprobado en `ExecutePacketType3` con
`bin_select_ & bin_mask_`). Con una sola pasada, un dibujo que solo toca las franjas de abajo se
salta por predicación en la primera y se tira en las otras dos, así que nunca se pinta y deja
basura en esa zona. Depende de lo que haya en pantalla, de ahí que sea a ratos.

Arreglo, también en `tools/parche_una_pasada.py`: en la primera vuelta de una superficie abierta
vale cualquier franja (`bin_mask_ != 0`). Los resolves no, que van atados a la suya.
**Sin probar todavía.**

**Sin medir de forma limpia:** lo que gana en fps. En la única comparación, el apagado dio 34-36
fps con 3.650-4.080 dibujos, y el encendido 29-32 fps con 3.050-3.550. Pero eran carreras
distintas y el encendido empezó en estado térmico 1, así que ese número no vale. Queda un A/B
en frío y en la misma escena. Las pruebas cayeron con el móvil en
estrangulamiento térmico 3 y en escenas no comparables, así que no hay número
honesto todavía. Queda pendiente un A/B en frío, en el menú (que es
determinista) y luego en carrera.

### Lo que se usa ahora: que lo haga el juego (2026-09-28)

`tools/parche_una_pasada.py` le quita las franjas al juego desde fuera, en el
procesador de comandos, y hubo que corregirle tres cosas (el desplazamiento en
x, el destino del resolve, la predicación). Hay una forma más limpia, portada de
[StevensND/nfsmw-nx](https://github.com/StevensND/nfsmw-nx) (port a Switch
derivado de este proyecto, GPL-3.0, con la misma edición PAL España de
referencia): **que el juego no trocee**.

El juego tiene una tabla de seis modos de antialiasing en su renderizador. El
modo 4, el que usa a 720p, son 3 tiras de 1280x256 con MSAA 4x. El modo 2 es
una tira de 1280x736 sin MSAA, y el juego de tienda ya lo usa cuando baja la
calidad. `android/app/src/main/cpp/render_targets.cpp` (cvar
`nfsmw_render_sin_mosaico`):

- engancha `sub_82458850`, que registra los render targets de cada modo, e
  iguala los modos 3-5 al 2 antes de que los lea;
- fuerza el modo 2 después de cada elección del juego (`sub_824402F0`,
  `sub_82441990`, `sub_8245D5F8`);
- quita el MSAA de cualquier conjunto que se registre (`sub_8245D320`), como el
  reflejo de 256x256;
- corrige la referencia de muestras de las consultas de oclusión del destello
  del sol (`sub_82225610`), que el juego calcula con el MSAA del modo que pidió.

El juego pinta una vez **con sus propios resolves**. No hay desplazamiento de
ventana ni predicación que corregir, y no queda nada que adivinar en el
procesador de comandos. El modo de 1080p de nfsmw-nx no se trae: allí no hay
EDRAM, y aquí 1920x1088 no cabe en los 10 MB emulados.

**Medido en la carrera de demostración, sin MSAA en los dos casos:**

| | fps (40/60/80 s) | Dibujos por fotograma |
|---|---|---|
| Con las tiras | 28,4 / 31,5 / 30,0 | 4.782 / 4.188 / 3.374 |
| Una pasada, desde el juego | 41,8 / 36,6 / 45,3 | 2.317 / 3.101 / 2.523 |

Es el mismo recorrido, pero no los mismos fotogramas exactos. La imagen sale
completa en todas las capturas y hay 0 resolves rechazados. Queda **encendido
de fábrica**, y `nfsmw_una_pasada` ya no se usa.

### La cache de pipelines, y un fallo que la anulaba en Android

El SDK guarda los shaders (`.xsh`) y la lista de pipelines usados (`.xpso`), y al
arrancar debería precrearlos. **En Android nunca lo hacía**: abre esos ficheros
con `"a+b"` y lee la cabecera sin rebobinar. En PC (glibc) `"a+"` empieza a leer
por el principio; en Android (bionic, heredada de BSD), **por el final**. La
cabecera no se leía, el fichero se daba por malo y se vaciaba en cada arranque.
El `.xpso` tenía siempre los 125 pipelines de la última sesión, y ninguno se
precreaba.

`tools/parche_cache_pipelines.py` rebobina al abrir, y además añade una
`VkPipelineCache` guardada en disco (un fichero por driver). Antes se creaba cada
pipeline con `VK_NULL_HANDLE`, y el driver lo compilaba entero cada vez.

**Medido, precreado al arrancar:**

| | Pipelines | Tiempo | Por pipeline |
|---|---|---|---|
| Sin la cache del driver | 131 | 1,71 s | ~13 ms |
| Con ella (1,9 MB) | 125 | 0,07 s | ~0,6 ms |

A 30 fps un fotograma son 33 ms: cada pipeline nuevo en plena carrera costaba
casi medio fotograma de tirón. Ahora los que el juego ya usó están creados antes
de empezar, y los nuevos se guardan para la siguiente vez.

## Fases

Cada fase se puede encender y apagar, y el camino actual sigue estando.

**1. Censo.** ~~Volcar de un fotograma todo lo que el juego pide.~~ **Hecho**,
arriba. Se vuelve a sacar cuando haga falta con
`python tools/diagnostico/censo_pipeline.py` y
`--nfsmw_censo=1 --nfsmw_censo_minimo=<dibujos>`.

**2. Objetivos de render nativos.** Sustituir la EDRAM por imágenes de Vulkan a
resolución nativa, con un mapa "dirección de invitado → imagen". Los resolves
del juego dejan de copiar: marcan que esa imagen ya es la textura. El censo ya
da el mapa entero: una docena de destinos, con su tamaño y su formato.

Son **dos cosas distintas**, y conviene no confundirlas:

- *Quitar la EDRAM* ahorra **GPU**: los volcados, los resolves y las
  transferencias de propiedad, ~4 ms por fotograma.
- *Dejar de pintar la escena tres veces* ahorra **CPU**: los 1.832 dibujos de
  arriba, ~13,6 ms por fotograma. Es el premio gordo.

Lo segundo **no sale gratis con lo primero**. Las tres pasadas las manda el
invitado, no nosotros: el D3D del juego graba la lista de dibujos y la reenvía
una vez por franja, y por eso el gancho `nfsmw_una_franja`, que parchea su
`BeginTiling`, sí bajó los dibujos del menú de 830 a 329. Para saltárselas hay
que ignorar los bloques 2 y 3 en el procesador de comandos, y eso **solo es
seguro si el objetivo ya es nativo y de pantalla completa**; si se hace con la
EDRAM todavía troceada, el primer bloque solo cubre su tira. Probablemente sea
justo eso lo que corrompía media pantalla con `nfsmw_una_franja`.

**3. Estado de dibujo cacheado.** Un pipeline de Vulkan por combinación real de
estado del juego —**31 en carrera**, no una por dibujo— en vez de montarlo
dibujo a dibujo. Descriptores persistentes y constantes por rango en vez de por
dibujo. Es la fase que abarata los dibujos que queden.

**4. Geometría.** Búferes de vértices e índices mapeados desde la memoria del
invitado sin copias, con invalidación por escritura (lo que ya hace la memoria
compartida).

**5. Lo específico del juego.** El posprocesado de MW (filtro de color, bloom,
desenfoque) como pases propios de Vulkan, que es donde más se gana y donde más
se nota si se hace mal.

## Cómo se prueba

Cada fase tiene que poder compararse con el camino actual en la misma escena:
el menú 3D (determinista) y una carrera. El banco de pruebas y el reparto de
tiempos por fotograma que ya existen valen tal cual, y la regla es la de
siempre: **medir antes y después, y publicar el número aunque salga cero**.

Para elegir la escena sin pelearse con los menús —que leen el stick y con adb
no hay forma fiable— el censo se dispara por densidad de dibujos:
`--nfsmw_censo_minimo=400` cae en el menú y `=2000` en una carrera, así que se
lanza el juego armado y salta solo cuando el juego llega. Vale igual si entra
a la carrera una persona.

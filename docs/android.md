# Android (Snapdragon + Turnip)

El port a móviles Qualcomm Snapdragon, con la opción de usar Turnip —el driver
Vulkan libre de Mesa para Adreno— en vez del driver de Qualcomm.

## Estado

**Arranca, llega al menú y se juega.** Probado en una nubia NX789J (Snapdragon
8 Elite, Adreno 830, Android 16).

| Pieza | Estado |
|---|---|
| Arranque, ISO sin extraer, VFS | Funciona |
| Menú, carreras, mando Bluetooth | Funciona |
| Orientación apaisada | Funciona |
| Driver de Qualcomm | Funciona |
| Turnip con libadrenotools | Solo arranca con `TU_DEBUG=sysmem`, y va a 44 fps donde Qualcomm va a 60: ver [Turnip](#turnip) |
| Rendimiento | Menú y garaje (Qualcomm, sin MSAA): **60 fps**, el tope. En carrera, 28–33: ahí manda la CPU. Ver [Rendimiento](#rendimiento-lo-que-se-encontró-perfilando) |
| Audio | Suena, pero **entrecortado**: no es rendimiento, es el descodificador XMA. Ver [Audio entrecortado](#audio-entrecortado) |
| Contador de fps en pantalla | Funciona, con tiempo por fotograma y el peor del último medio segundo |
| Parones al cargar | **Sin resolver.** De 5 a 78 s con los 17 hilos del juego dormidos. Ver [Los parones al cargar](#los-parones-al-cargar) |
| Vsync y límite de fps en Vulkan | **No existen.** `parche_presentador.py` solo toca D3D12 |
| Mando táctil | No hay. Hace falta un mando Bluetooth o USB, o un teclado (`--mnk_mode=true`) |
| Salir a la pantalla de inicio y volver | `parche_pausa.py` suelta la superficie; sin probar a fondo |

## Lo que hace falta

En el PC:

| Cosa | Para qué |
|---|---|
| Android Studio (o el SDK de Android suelto) | SDK, `sdkmanager`, el JDK que trae (`jbr`) |
| NDK **28.2.13676358** | Compilar para arm64-v8a. Alinea a 16 KB por defecto |
| CMake **3.31.6** del SDK de Android | El SDK ReXGlue pide 3.25 o más; el 3.22 que trae Android Studio no sirve. `sdkmanager "cmake;3.31.6"` |
| Python 3.10+ y Git | Los scripts |
| **Para el APK con juego:** Visual Studio Build Tools con *Desarrollo para el escritorio con C++* y *Compilador Clang*, y tu ISO | El codegen corre en el PC |

En el móvil: Snapdragon de 64 bits, Android 10 o superior y Vulkan 1.1.

## Los pasos

```bat
python tools\fase1_extraer.py "tu.iso" --solo-xex   :: assets\game_root\default.xex
python tools\android\preparar_sdk.py                :: ..\rexglue-sdk-android parcheado
python tools\android\generar_codigo.py              :: app\generated-android\
cd android
gradlew assembleRelease                             :: el APK
adb install -r app\build\outputs\apk\release\app-release.apk
```

`generar_codigo.py` necesita `clang++`, `cmake` y `ninja` en el PATH: el Clang de
las Build Tools (`VC\Tools\Llvm\x64\bin`) y el CMake del SDK de Android sirven.
Sin código generado, el APK solo lleva la sonda de Vulkan.

Esto es el motor de Xenos, el de por defecto. El APK también se puede compilar con el
**renderizador nativo** de nfsmw-android (`-Pnfsmw.motor=nativo`), que no imita la GPU de
la Xbox 360: ver [motor-nativo.md](motor-nativo.md).

`generar_codigo.py` mira primero de qué **edición del juego** es tu `default.xex` (PAL
España o USA) y, si no es la española, traduce las direcciones del proyecto a las de esa
edición. El APK que sale vale solo para esa edición. Ver [ediciones.md](ediciones.md).

La primera compilación del APK tarda (el SDK entero y 133 ficheros generados).
Los ficheros generados se compilan de 4 en 4 (`NFSMW_JOBS_RECOMP`): con 16 GB de
RAM y 8 a la vez, clang se queda sin memoria.

## En el móvil

1. Copia la ISO a la memoria interna (no a una microSD FAT32: no admite ficheros
   de más de 4 GB). `adb push tu.iso /sdcard/Download/`
2. **Probar Vulkan** (en Avanzado). La sonda dice qué driver se ha cargado, si el
   SDK acepta la GPU y si hay texturas BC. No necesita el juego.
3. **Importar un driver Turnip** (opcional): un `.zip` para adrenotools, con su
   `meta.json`.
4. **Elegir la ISO**, la de la edición con la que compilaste el APK (la pantalla de
   inicio dice cuál, y avisa si la ISO elegida es de otra). No se copia: se lee en su sitio.
5. Conecta un mando y **Jugar**.

Los registros quedan en `Android/data/io.github.nfsmwrecomp/files/logs/nfsmw.log`.
Algunas ROMs (la de nubia, por ejemplo) capan `adb logcat`: el fichero es lo que
vale.

### La pantalla de inicio

Va en inglés, español o portugués: el idioma del móvil, o el que se elija en la
sección Idioma (preferencia `idioma`; lo aplica `Idioma.java` en cada
actividad). Los textos están en `res/values` (inglés, el de por defecto),
`res/values-es` y `res/values-pt`. Una cadena nueva va en los tres.

Los ajustes van agrupados por lo que tocan. Cada uno lleva debajo una línea que
explica qué hace:

| Sección | Ajuste | cvar | Qué hace |
|---|---|---|---|
| Pantalla | Resolución | — | 480p (720×480), 720p (1280×720) o la nativa. Es el tamaño del búfer que se presenta; el juego se pinta igual. Ver abajo |
| Pantalla | Estirar a pantalla completa | — | Apagado: el 16:9 de la Xbox 360 con barras negras. Encendido: toda la pantalla, deformando |
| Pantalla | Mostrar fps | — | Rótulo en la esquina: fps y milisegundos por fotograma |
| Gráficos | Suavizado de bordes | `gpu_sin_msaa`, `gpu_msaa_muestras`, `swap_post_effect` | Apagado, MSAA (2x o 4x) o FXAA (normal o alta). **Apagado de fábrica.** Ver [Suavizado de bordes](#suavizado-de-bordes-msaa-o-fxaa) |
| Gráficos | Posprocesado | `nfsmw_sin_posprocesado` | Filtro de color y desenfoque del juego |
| Gráficos | Filtro anisótropo | `anisotropic_override` | No / 2x / 4x / 16x |
| Gráficos | Resolución interna | `resolution_scale` | 1x o 2x. La 2x lo hunde |
| Rendimiento | Fijar hilos a núcleos | `thread_affinity=auto` | **Encendido de fábrica.** El procesador de comandos y el hilo principal del juego a los núcleos prime, y el resto fuera de ellos. +17 % de fps con muchos dibujos. Ver [Afinidad de hilos](#afinidad-de-hilos-a-núcleos) |
| Rendimiento | Driver de GPU, Turbo de GPU | `android_gpu_*` | El del sistema o un Turnip importado. El turbo solo se puede encender con un Turnip elegido; con el del sistema sale gris y la app manda `--android_gpu_turbo=false`, que devuelve la GPU a su gobernador (el ajuste es de todo el móvil y persiste). Ver [Rendimiento](#rendimiento-lo-que-se-encontró-perfilando) |
| Avanzado | Escena en una pasada | `nfsmw_render_sin_mosaico` | **Encendido de fábrica.** El juego usa su modo sin antialiasing, de una tira, en vez de pintar la escena tres veces: ~30 % menos dibujos y +25-45 % de fps en carrera. No vale con MSAA. Ver [docs/pipeline-nativo.md](pipeline-nativo.md) |
| Avanzado | EDRAM en shader | `render_target_path_vulkan` | Experimental |
| Avanzado | Consultas de oclusión | `occlusion_query_enable` | Destellos del sol y luces; obliga a esperar a la GPU |
| Avanzado | Exposición fiel | `readback_resolve=fast\|none` | Sin ello la imagen sale lavada |
| Avanzado | Lectura de memexport, Refrescar páginas | `readback_memexport`, `clear_memory_page_state` | Coherencia de memoria |
| Avanzado | Registro detallado, Probar Vulkan | `log_level=debug` | Para diagnosticar |

Avanzado va plegado: se abre tocando su título.

#### Resolución y proporción

La **vista** del juego y su **búfer** van por separado (`GameActivity.ajustarSuperficie`):

- **La vista** es el rectángulo de la pantalla que ocupa el juego. Sin estirar, es el 16:9 más
  grande que cabe, centrado sobre negro. Estirando, es la pantalla entera.
- **El búfer** son los píxeles que se presentan: 720×480, 1280×720 o los de la vista. El
  compositor de Android lo escala a la vista sin coste. 720×480 en una vista 16:9 queda
  anamórfico, como el 480p panorámico de la Xbox 360.

La proporción la pone la vista y el presentador llena el búfer entero, con
`--present_letterbox=false`. Hay dos motivos:

- El presentador del SDK supone píxeles cuadrados en el búfer (`presenter.cpp`), y
  `surface_android.cpp` le da el tamaño del búfer. Con 720×480 en una vista 16:9 pondría
  barras donde no tocan.
- SDL mete la superficie con `WRAP_CONTENT`, y un `SurfaceView` con `setFixedSize` en
  `WRAP_CONTENT` mide lo que el búfer: la vista encogía a una esquina de la pantalla. Por eso
  ahora siempre lleva un tamaño exacto.

El log lo confirma con `Superficie: vista WxH, bufer WxH`.

### Banco de pruebas desde adb

Para comparar ajustes sin tocar la pantalla, la pantalla de inicio acepta:

```bat
adb shell am start -n io.github.nfsmwrecomp/.SetupActivity ^
    --ez nfsmw.banco true --ei nfsmw.alto 720 --es nfsmw.driver sistema ^
    --esa nfsmw.args --mnk_mode=true,--occlusion_query_enable=false
```

Lanza la partida con la ISO y los ajustes guardados. `nfsmw.alto` es 480 o 720
para esas resoluciones y cualquier otro valor para la nativa; sin él, la elegida.
`nfsmw.driver` es `sistema` o el nombre de un Turnip importado, sin tocar el
elegido. Cada opción
de `nfsmw.args` **sustituye** a la misma de los ajustes: el parser de cvars
(CLI11) no admite una opción no booleana repetida, y con una repetida dejaba
otras sin aplicar (así se perdía `--game_data_root`).

El juego no pasa solo de los vídeos y la pantalla de título. Con
`--mnk_mode=true` el teclado emula un mando y `adb` puede pulsar:
`input keyevent --longpress KEYCODE_ENTER` es START y `KEYCODE_SPACE` es A.
Tiene que ser `--longpress`: una pulsación normal baja y sube en el mismo
milisegundo y el juego, que consulta una vez por fotograma, no la ve. Con
ENTER se saltan los vídeos y se pasa del título; con SPACE se responde "No" a
crear perfil y se llega al **menú principal, un garaje en 3D**, que es la
escena de medida.

El log tiene una línea `fps:` cada 5 segundos, y debajo el reparto del tiempo
del procesador de comandos (`parche_tiempos.py`). La pantalla en la que está el
juego se reconoce por los paquetes por fotograma: vídeos 3–5 `DRAW_INDX`
(op22), título ~18, pregunta de perfil ~36, menú 3D más de 800.

**Mide en frío.** Este móvil pasa de estado térmico 0 a 3 (severo) en dos
minutos de menú 3D, y en 3 capa los núcleos a 2,4/2,8 GHz. Antes de cada
medida: `adb shell dumpsys thermalservice | findstr "Thermal Status"` hasta que
sea 0 o 1.

## Rendimiento: lo que se encontró perfilando

Todo esto salió de medir, no de suponer. Escena de medida: el menú principal
(garaje 3D), driver de Qualcomm, móvil frío. **De 15 a ~50 fps.**

### Las herramientas

Con la app *profileable* (`<profileable android:shell="true"/>` en el
manifiesto) se perfila sin root:

```bat
adb shell simpleperf record --app io.github.nfsmwrecomp -e cpu-cycles -f 1000 ^
    --call-graph dwarf --duration 6 -o /data/local/tmp/perf.data
adb shell simpleperf report -i /data/local/tmp/perf.data --sort comm,dso,symbol
```

`--app` hace falta: con `-p PID` el shell no tiene permiso. En este móvil solo
hay eventos de hardware (`cpu-cycles`): `cpu-clock` y `task-clock` no existen,
así que `--trace-offcpu` tampoco. Y sin root **simpleperf no ve el tiempo en el
kernel ni el tiempo dormido**: un hilo que se pasa el fotograma esperando sale
casi vacío. Tres cosas lo suplen:

- `-e sched:sched_switch -c 1 --call-graph dwarf`: una muestra con pila cada vez
  que un hilo se duerme. Con `simpleperf report-sample --show-callchain` salen
  las marcas de tiempo, y asignando a cada muestra el tiempo hasta la siguiente
  del mismo hilo se tiene un perfil **fuera de CPU ponderado por tiempo**.
- `parche_tiempos.py`: el reparto del tiempo del procesador de comandos por
  fotograma y por tipo de paquete PM4, en el log debajo de cada línea `fps:`.
- `/proc/<pid>/task/<tid>/stat` y `schedstat`: estado (R/S), núcleo, tiempo de
  usuario y de kernel de cada hilo.

### Lo que había, en orden

| Parche | Qué pasaba | Efecto |
|---|---|---|
| `parche_pipeline.py` | El presentador comparaba el formato del pipeline de salida con uno que **nunca se asignaba**. En cada fotograma esperaba a la GPU, destruía el pipeline y lo recompilaba con el compilador LLVM de Qualcomm: el 84 % del hilo de interfaz | Intro de 20–22 a 30 fps |
| `parche_fallos.py` | Cada fallo de página de la vigilancia de escritura leía y parseaba `/proc/self/maps` entero para descartar una carrera: el 82 % del hilo principal del juego | Menú de 11 a 15 fps |
| `parche_esperas.py` | Las esperas múltiples de POSIX pasaban lo que quedaba de cada rodaja a milisegundos enteros; 0,7 ms se volvía 0 y el hilo no dormía nunca. El trabajador de audio se comía un núcleo entero | Audio Worker del 100 % al 4 % |
| ~~`parche_audio.py`~~ | El móvil anuncia el altavoz como 6 canales y el flujo de 6 de SDL no suena | **Retirado**: el audio va ahora por AAudio (`android/app/src/main/cpp/audio/`), que abre estéreo directamente, y el driver de SDL ya no se instancia |
| `parche_fps.py` | No había forma de medir: logcat capado y `SurfaceFlinger --latency` vacío | Línea `fps:` en el log y valor para el rótulo |
| `parche_teclado.py` | El teclado (`--mnk_mode`) no generaba **nunca** eventos de `XInputGetKeystroke`, que es lo que leen los menús: con teclado no se pasaba del título | Se puede navegar con teclado, y automatizar el banco con `adb` |
| `parche_cola_presentar.py` | El presentador y el procesador de comandos (CP) comparten la única cola de Vulkan, con un mutex. En Android `vkQueuePresentKHR` espera dentro al fotograma anterior (`queueBuffer` → `Fence::waitForever`), **con la cola cogida**: el CP no podía enviar y la GPU se quedaba sin trabajo. El CP pasaba el 81 % del tiempo esperando ese mutex | Presente en una segunda cola |
| `ganchos.cpp` + `parche_espera_anillo.py` | Con el anillo de comandos lleno, el D3D del juego **gira** releyendo el puntero de lectura del CP (`sub_82597690` → `sub_825A5D18`): el 86 % de los ciclos de toda la app, un núcleo prime al 100 % y el móvil en estrangulamiento térmico en minutos | El hilo duerme en un futex hasta que el CP publica; la CPU de la app cae ~15 veces |
| `parche_subidas.py` | Cada subida de datos a la memoria compartida (~240 por fotograma) cerraba el pase de render para copiar y lo reabría; en una GPU de mosaicos eso es volcar y recargar la GMEM. La GPU estaba ocupada el 99 % (timestamps de Vulkan) | Las copias se adelantan a antes del pase si el pase no ha leído esas páginas (el 95 %) |
| `parche_cvars.py` | `gpu_backend` vale `d3d12` por defecto: el plugin de GPU se cargaba, se descargaba y se volvía a cargar para Vulkan, y en esa recarga **sus cvars perdían lo pasado por línea de comandos**. `--occlusion_query_enable=false`, `--resolution_scale=2`, `--anisotropic_override=0`… no hacían nada, tampoco desde la pantalla de ajustes | Los cvars sobreviven a la recarga; además la app pasa `--gpu_backend=vulkan` |
| `parche_turnip.py` (turbo) | `adrenotools_set_turbo(true)` apaga el control de energía de la GPU (`KGSL_PROP_PWRCTRL`) para **todo el móvil y hasta reiniciar**, y nadie lo volvía a encender. En un Snapdragon 8 Elite eso fija un reloj peor que el del gobernador: el menú a 14,6 fps con turbo frente a 42 sin él. Una partida con la casilla de turbo dejaba el móvil así | Se aplica siempre, también con `false`, que lo deshace |

| `parche_fences.py` | Al abrir cada envío el CP preguntaba al driver qué envíos había acabado, con `vkWaitForFences` y tiempo 0. En Turnip cada pregunta costaba ~8 ms: casi todo en un bucle de limpieza de caché dentro del driver | `vkGetFenceStatus`, solo al abrir fotograma, y un hilo vigía que espera los fences por su cuenta. Turnip de 20,3 a 30,7 fps |
| `parche_msaa.py` | El juego pide MSAA 4x. De los ~19 ms de GPU del menú, 13 se iban en mover las cuatro muestras entre GPU y memoria, pase a pase | Ajuste "sin suavizado de bordes": el menú pasa de 46 a **60 fps** |
| `parche_vblank.py` | El CP sondeaba con `Sleep(1ms)` la espera del flip, y el hilo del vblank miraba el reloj cada milisegundo. Más de 1 ms tirado de cada 16,7, justo en la cadena que decide si el fotograma entra en su vblank | El vblank llega a su hora y el CP despierta con él, por futex |
| `parche_registros.py`, `parche_vertices.py`, `parche_constantes.py` | Tres sospechosos del coste por dibujo: invalidar cachés al reescribir el mismo valor, una petición por buffer de vértices y un `memcpy` de 16 bytes por constante | **Medidos: nada.** 7,8–8,1 ms de dibujos por fotograma con y sin ellos. **Retirados del repo**; queda anotado aquí porque la medida descarta esas vías |
| `parche_area.py` | Cada pase de render abría sobre el objetivo entero aunque el juego pintara una franja | Se abre sobre la unión de tijeras y clears. Con Qualcomm no cambia nada (ya lo hacía el driver); queda con interruptor |

`parche_tiempos.py` es solo para medir, y `tools/diagnostico/parche_cerrojo.py`
sirvió para descartar el cerrojo global (el CP esperaba 2,7 ms por segundo).

Para devolver la GPU a su control de energía normal sin abrir el juego (por si
quedó en turbo con un APK viejo), vale un binario de 20 líneas con
`IOCTL_KGSL_SETPROPERTY` / `KGSL_PROP_PWRCTRL = 1` ejecutado desde
`adb shell`: `/dev/kgsl-3d0` es de lectura y escritura para todos.

### Cuánto aporta cada cosa

Menú 3D, driver de Qualcomm, media de 30 s tras llegar al menú (con el móvil
enfriado a estado térmico 1–2 antes de cada medida; durante la medida sube a 3):

| Prueba | fps |
|---|---|
| Todo puesto, **sin MSAA** (`--gpu_sin_msaa=true`) | **60**, el tope |
| Todo puesto, con el MSAA que pide el juego | 46 |
| Sin adelantar subidas (`--vulkan_adelantar_subidas=false`) | 42–43 |
| Sin el gancho del giro (`setprop debug.nfsmw.espera_anillo 0`) | 44,6 |
| Resolución interna x2 | 14–15 |
| Sin exposición, sin anisótropo, sin oclusión, sin memexport o sin refrescar páginas | 50–52 (nada) |
| Con turbo de GPU | 14,6 |

**El MSAA es lo más caro con diferencia.** Perfilando la GPU con timestamps de
Vulkan, pase a pase: de los ~19 ms de GPU por fotograma del menú, 9,8 se iban
en una sola clase de pase —la escena, con color y profundidad a 4 muestras— y
otros 3,3 en pases pequeños del mismo objetivo. Cada pase mueve las cuatro
muestras entre GPU y memoria; en una GPU de mosaicos eso es el peor patrón
posible. `tools/parche_msaa.py` le quita las muestras al registro
`RB_SURFACE_INFO` según entra, y el resto del emulador ve 1x de forma
coherente. La imagen sale correcta; solo se pierde el suavizado de bordes.
También puede dejar 2x en vez de quitarlo del todo (`gpu_msaa_muestras=2`).

Ojo: **en carrera no cambia nada**, porque ahí la GPU espera al procesador de
comandos (0,0 ms de espera de GPU medidos) y el cuello es la CPU. Y el juego
sigue troceando la pantalla en tres franjas, porque esa decisión la toma su
propio código, no el emulador.

- La segunda cola de presentar no tiene interruptor: con ella el CP dejó de
  pasar el 81 % del tiempo esperando la cola. Junto con devolver la GPU a su
  control de energía normal (el turbo atascado), es lo que llevó el menú de
  ~10 a ~42 fps.
- Los ajustes de la sección Rendimiento de la app **no cambian nada** en este
  móvil: el trabajo que cuestan es poco al lado de lo demás. La escala interna
  x2, en cambio, lo hunde.
- **Se probaron y se quitaron**, por no mover nada: poner solo el hilo del CP
  en los núcleos más rápidos (el planificador ya lo tenía ahí; lo que sí gana
  es fijar también el hilo principal, ver
  [Afinidad de hilos](#afinidad-de-hilos-a-núcleos)), una sesión de ADPF (Performance Hint API)
  para él, y pintar solo cuando hay fotograma nuevo (el SDK repinta a cada
  refresco porque el diálogo de avisos de logros siempre existe; con la segunda
  cola eso ya no frena, y la única medida con repintado continuo dio incluso
  algo más: 52 fps frente a 50).

**El calor manda.** En 30 s de menú el móvil llega a estado térmico 3, y en una
prueba de 3 minutos el menú bajó de 52 a ~41 fps al minuto. Cargando a la vez
es peor (la batería llega a 47 °C). El gancho del giro ayuda justo por eso: sin
él un núcleo prime va al 100 % todo el rato.

### Suavizado de bordes: MSAA o FXAA

En la app es una sola opción con tres valores, y debajo solo sale la
configuración del elegido:

| Valor | Cómo | Configuración |
|---|---|---|
| Apagado | `gpu_sin_msaa=true`, `swap_post_effect=none` | — |
| MSAA | el del juego: `gpu_sin_msaa=false` y `gpu_msaa_muestras` | 2x o 4x (lo que pide el juego) |
| FXAA | `swap_post_effect=fxaa` o `fxaa_extreme` | Normal o Alta |

**El MSAA** es el del juego: se pinta con varias muestras por píxel. Es lo más
fino y lo más caro (ver arriba). `tools/parche_msaa.py` recorta las muestras de
`RB_SURFACE_INFO` al tope pedido. Con menos muestras la superficie ocupa menos
EDRAM, así que las direcciones que el juego calculó para 4x no se solapan. No
es compatible con `nfsmw_una_pasada`: una pasada de 1280×720 con MSAA no cabe
en la EDRAM.

**El FXAA** ya venía en el SDK, sin usar: es un filtro sobre la imagen final.
El pase de gamma deja la luminancia en el alfa, y un pase de cómputo suaviza
los bordes que encuentra. Va sobre los 1280×720 del juego, antes de escalar a
la pantalla, y los dos shaders se compilan de GLSL al arrancar
(`GetSwapFxaaComputeSource` en `vulkan/command_processor.cpp`). No es el FXAA
3.11 completo, sino la versión corta: cuatro vecinos en diagonal y cuatro
muestras a lo largo del borde. Las dos calidades solo cambian sus constantes:

| | Umbral de borde | Umbral mínimo | Alcance |
|---|---|---|---|
| Normal (`fxaa`) | 0,166 | 0,0833 | 8 px |
| Alta (`fxaa_extreme`) | 0,063 | 0,0312 | 12 px |

Alta suaviza más bordes, también los de poco contraste, y ablanda algo más la
imagen. Como va después de pintar la escena, el FXAA sí vale con
`nfsmw_una_pasada`.

### El vblank, y por qué los fps saltan de 60 a 30

El juego va a doble búfer con vsync: un `WAIT_REG_MEM` espera a que una palabra
de memoria valga 0, y quien la pone a 0 es su propia interrupción de vblank.
Instrumentado, ese `WAIT_REG_MEM` acaba **siempre** entre 0,15 y 1,07 ms
después de un vblank, y era el 30 % del tiempo del fotograma.

O sea que el tiempo por fotograma está cuantizado: si el trabajo cabe en 16,7
ms salen 60 fps, y si se pasa aunque sea por poco, ese fotograma cuesta 33,3.
Con el trabajo justo al borde, uno de cada tres fotogramas se pasaba, y de ahí
salían los ~45 fps de media.

`tools/parche_vblank.py` quita el milisegundo tonto de los dos extremos: el
hilo del vblank duerme hasta su hora exacta en vez de mirar el reloj cada
milisegundo, y el CP espera en un futex que se despierta con el vblank en vez
de sondear con `Sleep(1ms)`.

Y sobre todo quita el precipicio: con **vsync adaptativo**, si el fotograma ya
llegó tarde el vblank lo sigue en vez de esperar a la rejilla, así que un
fotograma de 17,9 ms cuesta 17,9 (55,8 fps) en vez de 33,3 (30 fps). El tope de
60 se mantiene, porque el vblank sigue sin poder adelantarse a su hora.

### En carrera manda la CPU

Cronometrando `IssueDraw` por tramos dentro de una carrera: **7,7 µs de CPU por
dibujo**, y de 2.500 a 4.700 dibujos por fotograma. Ahí se va todo; la GPU
espera. El reparto de esos 7,7 µs:

| Etapa | µs |
|---|---|
| Shaders y procesado de primitivas | 2,2 |
| Constantes y descriptores (`UpdateBindings`) | 2,0 |
| Buffers de vértices (`RequestRange`) | 1,8 |
| Texturas | 0,6 |
| Objetivos, pipeline, estado dinámico, dibujo | 0,9 |

Y por qué hay tantos dibujos: con MSAA 4x la imagen no cabe en los 10 MB de
EDRAM de la Xbox 360, así que el juego **pinta la escena tres veces**, una por
franja (*predicated tiling*), marcando con `SET_BIN_MASK` qué dibujos tocan
cada franja. El emulador respeta esa predicación y se salta los que no tocan,
pero aun así la lista se recorre tres veces.

### Quitarle el troceado al juego

Y se le puede quitar. La decisión la toma el juego, no el emulador, pero pasa
por una función que se puede interceptar como cualquier otra.

Medido primero si valía la pena: el procesador de comandos ya se salta los
dibujos predicados que no tocan la franja activa, así que el triple podía ser
mentira. No lo es: en carrera se saltan **426 dibujos por segundo de unos
86.000**, el 0,5 %. El juego pinta de verdad tres veces.

El camino, siguiendo el código recompilado:

| Dónde | Qué es |
|---|---|
| `sub_825992F0` | El `BeginTiling` de D3D: guarda el número de franjas en el dispositivo (offset 12992) y sus rectángulos a partir de 12996 |
| `sub_8258AE48` | Quien emite por cada franja su máscara (`SET_BIN_MASK`, `0xC0015000`) y su tijera |
| `sub_8245D5F8` | El juego: un flag de "trocear" en +41 de su estructura, el número de franjas en +124 y los rectángulos en +44 |

El gancho de `ganchos.cpp` (cvar `nfsmw_una_franja`) intercepta `BeginTiling`,
calcula el rectángulo que cubre las tres franjas y le pasa **una sola**. La
escena se pinta una vez. No vale con MSAA (sí con FXAA): sin MSAA es lo que hace que el
emulador trate la superficie como de una muestra, y entonces cabe en la EDRAM
emulada.

En el menú 3D: de 830 a **329 dibujos por fotograma**, de 31 a 17 resolves, y
el tiempo de dibujo baja a 5,2 ms. La imagen sale idéntica.

### Afinidad de hilos a núcleos

El Snapdragon 8 Elite no tiene núcleos pequeños: cpu0–5 van a 3,53 GHz y cpu6–7
(los prime) a 4,32 GHz, un 23 % más. El SDK no reparte hilos por nombre: su
`EnableAffinityConfiguration()` en POSIX está vacía, y un `thread_affinity`
anterior de `android_main.cpp` escribía "Afinidad de hilos configurada" sin fijar
ninguno.

`android/app/src/main/cpp/afinidad.cpp` lo hace desde la app: un hilo
(`NFS afinidad`) recorre `/proc/self/task` cada 2 s, lee el nombre de cada hilo y
le pone su máscara con `sched_setaffinity` si no la tiene ya. Así cubre también
los hilos que no crea el SDK (el driver de Qualcomm, SDL, binder), y repone las
máscaras cuando Android las reajusta al volver de segundo plano. No hace falta
ningún parche del SDK.

El cvar `thread_affinity` admite:

| Valor | Qué hace |
|---|---|
| vacío | no tocar nada (lo que pasa si se apaga el ajuste) |
| `auto` | los núcleos de más `cpu_capacity` son los prime: `GPU Commands` y `Main XThread` van ahí y **todo lo demás a los otros**. Si todos valen lo mismo, no se toca nada |
| reglas | `GPU Commands=6-7;Main XThread=6+7;*=0-5`. Casan por el principio del nombre (el kernel lo corta a 15 caracteres), gana la primera, `*` es el resto |

Lo que aplica queda en el log: `[afinidad] 'auto' -> GPU Commands=6+7; ...` y
una línea por hilo.

**Medido** con el banco de pruebas, en una escena de 2.700–4.200 dibujos por
fotograma, en dos rondas en orden inverso (sin, con, con, sin) y partiendo de
estado térmico ≤ 1:

| | fps | µs por dibujo |
|---|---|---|
| Sin fijar | 24,5 | 8,8 |
| `auto` | 28,6 (**+17 %**) | 7,1 (**−19 %**) |

Muestreando en qué núcleo corre cada hilo, lo que cambia es el **hilo
principal**: sin fijar, `GPU Commands` ya estaba el 100 % del tiempo en un prime
(por eso fijar solo el CP no movía nada), pero `Main XThread` solo el 25–32 %.
Con `auto` los dos están ahí el 100 % y ningún otro hilo los pisa. El calor no
cambia: las dos variantes acaban en estado térmico 3.

La latencia no tiene nada que ver: la espera por despertar de los hilos ya era de
0,02–0,09 ms. Lo que se gana es reloj.

### Lo que queda por probar

- Comprobar el troceado en carrera a fondo: los *resolves* iban por franja.
- Bajar el coste por dibujo: quedan el procesado de primitivas y los
  descriptores de texturas, que se rehacen dibujo a dibujo.
- `parche_tiempos.py` se queda puesto (sus líneas por fotograma cuestan poco).
  El detalle por paquete PM4, que cuesta 1–2 ms por fotograma, va aparte con
  `--medir_paquetes=true`, y el banco de pruebas lo necesita para reconocer las
  pantallas.


### Audio entrecortado

No es rendimiento. Medido durante un minuto entero, incluida la carga con el
juego a 20 fps: el callback de audio del juego entrega sus 187,5 bloques por
segundo sin fallar uno (0,1–0,2 ms cada uno, sobre un presupuesto de 5,3), y
**SDL no se queda sin datos ni una vez**, con la cola siempre llena.

Los atascos de voz sí existían: alguna voz se quedaba 5 segundos sin producir
nada hasta que `parche_desatasco.py` la desbloqueaba desde fuera. Se portaron
los arreglos de Xenia Canary y Xenia Edge (`parche_xma_paquetes.py`,
`parche_xma_edge.py`) y los atascos desaparecieron... **y sigue sonando igual
de entrecortado**.

#### Lo que se descartó midiendo (2026-09-20)

Ocho hipótesis, todas muertas con un número:

| Hipótesis | Cómo murió |
|---|---|
| El códec está sin optimizar | FFmpeg ARM64 compila con `ARCH_AARCH64`, `HAVE_NEON` y el ensamblador NEON (`fft_neon`, `mdct_neon`, `float_dsp_neon`), que es lo que consume WMA Pro |
| El planificador ahoga al audio | Espera por despertar: **0,021 ms** en menú, **<0,089 ms** en carrera a 20 fps |
| Underruns en la salida | Los contadores de AudioFlinger **no crecen** mientras se juega |
| El descodificador se atasca | Sin atascos, y **nunca** se queda sin hueco de salida |
| El juego se queda sin audio que leer | **0 %** de consultas secas, igual en menú que en carrera |
| La salida entrega silencio | **0 %** de rellenos mudos (188 vueltas/s, ninguna muda) |
| Distorsión al plegar 5.1 a estéreo | **0 %** de muestras recortadas; picos 0,47–0,80 sobre 1,0 |
| El audio va atado a los fps | Subir de 20 a 35 fps (`nfsmw_una_pasada`) no cambió nada |

Conclusión incómoda pero firme: **el audio llega al altavoz completo, a la
velocidad correcta y sin distorsión**. El problema no está en el transporte.

Las sondas están en `tools/diagnostico/parche_audio_ritmo.py`,
`parche_audio_huecos.py`, `parche_audio_silencio.py` y `parche_audio_clip.py`,
y `scratchpad/audio_hilos.sh` mide la espera en cola del planificador leyendo
`/proc/<pid>/task/<tid>/schedstat`.

#### `audio_maxqframes`: no subirlo

El cvar `audio_maxqframes` (profundidad de la cola de audio, por defecto **8**)
tiene una descripción que nombra el síntoma —"lower reduces latency but may
cause stuttering"— y Xenia recomienda mínimo 16. Probado en el móvil:

| Valor | Resultado |
|---|---|
| 8 (por defecto) | ~30 fps en carrera |
| 16 | frame time bastante peor |
| 32 | **9–17 fps con el móvil frío**, y el coste por dibujo sube de 7,4 a 9,4 µs |

O sea que aquí la cola de audio **le roba CPU al render**, que es justo el
cuello de botella. Se queda en 8.

#### Por dónde seguir

En XenDroid —puerto Android rebasado sobre **Xenia Edge**, mismo tipo de móvil
(Snapdragon Gen 2+, Adreno 740+)— el audio funciona perfecto. Así que el
arreglo existe y está en código.

Comparado el código de Xenia Edge con el nuestro:

| Pieza | Xenia Edge | Nosotros |
|---|---|---|
| Codec de FFmpeg | `AV_CODEC_ID_XMAFRAMES` | **el mismo** |
| Conversión a PCM | XMA → float → PCM 16 bits BE | la misma |
| `kMaximumQueuedFrames` | 64 | 64, pero con el cvar `audio_maxqframes` a **8** |
| Driver de SDL | callback de **SDL2**, tamaño exacto | flujo de **SDL3**, `SDL_PutAudioStreamData` por trozos |
| Canales en Android | sin caso especial | **forzado a estéreo** (entonces `parche_audio.py`; ahora AAudio abre estéreo) |

O sea que **el descodificador es el mismo**, y la diferencia está en la salida:
el modelo de SDL y el plegado a estéreo, que son justo las dos partes con
parche local y las dos específicas de Android. Ahí hay que mirar.

Falta un dato que se ha escapado tres veces: la línea `audio endpoint '...':
N ch, N Hz, format 0x...` que el driver escribe al arrancar. No aparece en el
log, y debería. O se emite antes de abrir el fichero de log, o el driver no
pasa por ahí; conviene averiguar cuál de las dos.

### Los parones al cargar

Al cargar (entrar en una carrera, cambiar de pantalla) el juego se queda
parado de 5 a 78 segundos. **No es lentitud ni disco**: muestreando el proceso
durante un parón, los 42 hilos están dormidos, la CPU al 7 % y no hay lectura
de disco. El vigilante de `app/src/nfsmw_app.h` vuelca qué espera cada hilo del
juego: dos esperan temporizadores, tres esperan semáforos y el principal duerme
en un bucle de `KeDelayExecutionThread`. Nadie va a despertar a nadie hasta que
salta algún temporizador.

Falta encontrar quién debía avisar. Las aperturas de fichero que fallan en el
log (`D:\GLOBAL\GAMEPLAY.BIN` y compañía) son **normales**: los datos van
empaquetados en `D:\NFS\ZZDATA*.BIN` y el juego prueba primero la ruta suelta.

### Turnip

**No compensa hoy.** Medido con Turnip Gen8 V36, ya con todos los arreglos y
sin MSAA:

| Driver | Menú 3D |
|---|---|
| Qualcomm | **60 fps** |
| Turnip + `TU_DEBUG=sysmem` | 44 fps, e irregular (34–60) |
| Turnip tal cual (camino GMEM) | **se cuelga**: no llega ni al menú |

Los cuelgues del camino de mosaicos no se arreglan con ninguno de los
interruptores de Mesa que valdría la pena probar: `nolrz`, `noubwc`,
`flushall` ni `nobin`. Y `sysmem`, que es lo único que arranca, es justo el
camino lento: pinta directo a memoria, sin mosaicos. La Adreno 830 es muy
nueva y el soporte de Turnip para esa generación todavía está verde.

El cvar `android_turnip_debug` pasa lo que se quiera a `TU_DEBUG` (con `+` en
vez de comas, que CLI11 se come).

Lo que sí salió de mirar Turnip de cerca: consultar un fence costaba ~8 ms por
culpa de una sincronización de caché dentro del driver, y de ahí salió
`tools/parche_fences.py` (el hilo vigía de fences), que subió Turnip de 20 a 31
fps y en Qualcomm no cambia nada.

Medido con Turnip Gen8 V36 **antes** de los arreglos de la cola, las subidas y
los cvars:

- Intro y título a 30 fps (Qualcomm: 60 en el título).
- Menú 3D a **0,7 fps**: 31 `DRAW_INDX_2` a ~36 ms cada uno. El hilo del CP casi
  no usa CPU en modo usuario: el tiempo se va en el kernel (`ioctl` del driver
  y fallos de página en `memcpy`), que simpleperf sin root no ve.
- Parte de eso era la lectura de vuelta de los resolves
  (`IssueCopy_ReadbackResolvePath`), que en Turnip lee memoria de GPU sin
  caché; pero sin ella seguía a ~1 ms por `DRAW_INDX_2`.

La V33 llegaba al menú y caía con `VK_ERROR_DEVICE_LOST` al minuto.

## Qué hay debajo, y por qué

### Un SDK aparte

El parche Android cambia la plantilla del codegen (`pch_h.inja`): barreras de
memoria para ARM64 y el offset de la memoria física decidido en tiempo de
ejecución, que hace falta con páginas de 16 KB. Aplicado sobre `..\rexglue-sdk`
cambiaría también el código generado para Windows. Por eso hay dos árboles.

### Lo que `preparar_sdk.py` pone encima

| Qué | De dónde | Para qué |
|---|---|---|
| Reparar symlinks | este repo | En Windows git deja los enlaces de libmspack como ficheros de texto. Ver [00-entorno.md](00-entorno.md) |
| Parche Android del SDK | [hells-gate-recomp-android](https://github.com/deivid22srk/hells-gate-recomp-android), fijado a un commit | bionic, `ASharedMemory`, páginas de 16 KB, `ucontext` aarch64, logcat, superficie `ANativeWindow` |
| Parche de rendimiento | el mismo repo | caché de `/proc/self/maps`. Sin `mmio_handler.cpp`, que depende de su juego |
| libadrenotools | [bylaws/libadrenotools](https://github.com/bylaws/libadrenotools), BSD-2 | cargar Turnip |
| `diagnostico`, `anillo`, `desatasco`, `restaurar`, `velocidad`, `backend`, `privilegios` | este repo | los mismos arreglos que en Windows |
| `parche_iso.py` | este repo | ISO o URI `content://` como `game_data_root` |
| `parche_turnip.py` | este repo | cargar el driver con adrenotools |
| `parche_pausa.py` | este repo | soltar la superficie al pasar a segundo plano |
| `parche_fps.py`, `parche_pipeline.py`, `parche_fallos.py`, `parche_esperas.py` | este repo | ver la tabla de rendimiento |
| `parche_teclado.py`, `parche_tiempos.py`, `parche_espera_anillo.py`, `parche_cola_presentar.py`, `parche_subidas.py`, `parche_cvars.py` | este repo | ídem |

Se quedan fuera `parche_presentador.py` y `parche_gpu_fallback.py`: solo tocan
Direct3D 12.

**Sobre el parche Android:** es trabajo de otra persona y su repositorio no
declara licencia. Por eso **no está en este repo**: el script lo descarga en tu
máquina. Queda pendiente pedir permiso al autor o sustituirlo por uno propio.

### La inicialización que el SDK no hace sola

El parche Android declara un `AndroidInitialize()` por subsistema (memoria,
hilos, ficheros) y **no lo llama nadie**. Lo hace `android_main.cpp`. Sin el de
memoria, `ASharedMemory_create` queda sin resolver y el arranque muere con
`Unable to reserve the 4gb guest address space`.

### La ISO como URI `content://`

La app pasa la URI del selector del sistema tal cual. Pasar `/proc/self/fd/N`
con el descriptor abierto **no funciona**: `open()` sobre esa ruta reabre el
fichero real por su ruta, y la app no tiene permiso sobre el almacenamiento
compartido. `parche_iso.py` hace que el SDK reconozca la URI en tres sitios:
la comprobación de `ConstructRuntime`, el montaje en `SetupVfs` y
`MappedMemory::Open`, que pide el descriptor a Java (`openContentFd`). También
evita que se busque `default.xex` fuera de la imagen.

### Por qué la app arranca el juego por su nombre

En Android el SDK define `XE_UI_WINDOWED_APPS_IN_LIBRARY`: `REX_DEFINE_APP`
apunta la app en una tabla en vez de definir `GetWindowedAppCreator()`. Por eso
no se compila `windowed_app_main_sdl.cpp` y `android_main.cpp` pide la app con
`WindowedApp::GetCreator("nfsmw")`.

### Por qué la partida va en otro proceso

Las librerías nativas no se pueden recargar en el mismo proceso. `GameActivity`
corre en `:juego` y al cerrarse mata su proceso.

### Por qué `rexruntime` va explícito en `getLibraries()`

SDL3 va enlazado estático **dentro** de `librexruntime.so`, y ahí están sus
métodos JNI. Java solo los encuentra en librerías cargadas con
`System.loadLibrary`.

### Por qué `libmain.so` no enlaza `rexui`

Es una librería OBJECT: enlazarla vuelve a meter sus objetos en `libmain.so` y
duplica el estado global, empezando por el registro de cvars. El propio SDK lo
avisa. El port de referencia sí lo hace; aquí no.

## Los ajustes que pasa la app

La línea de comandos manda sobre `nfsmw.toml`. La app pasa siempre:

```
--gpu_plugin=xenos
--gpu_backend=vulkan                     si no, el plugin se carga dos veces
--render_target_path_vulkan=fbo          como el launcher ARM64 de Linux
--vulkan_sparse_shared_memory=false      idem
--fullscreen=true
--present_letterbox=false                la proporción la pone la vista (ver "Resolución y proporción")
--present_allow_overscan_cutoff=false
--resolution_scale=1|2
--occlusion_query_enable, --readback_resolve, --readback_memexport,
--clear_memory_page_state, --anisotropic_override   los de Rendimiento
--thread_affinity=auto                   con "Fijar hilos a núcleos" (de fábrica)
--user_language, --user_country          los de la edición del APK: 5 y 31 (PAL España), 1 y 103 (USA)
--user_data_root=.../files/datos
--log_file=.../files/logs/nfsmw.log --log_level=info|debug
--log_flush_interval=2                   si no, el fichero va minutos por detrás
--android_native_lib_dir=...             hooks de adrenotools
--android_tmp_dir=...
--android_gpu_driver_dir=... --android_gpu_driver_name=...   solo con Turnip
--android_gpu_turbo=true|false           true solo con un Turnip elegido y el turbo marcado
--game_data_root=content://...           la ISO
```

`log_level=debug` hunde los fps: el plugin de GPU escribe una línea por
fotograma en almacenamiento compartido.

El launcher de Linux pasa además `storage_root`, `vulkan_pipeline_creation_threads`
y varios `audio_*`. **En el SDK v0.10.0 no existen**, así que aquí no se pasan.

## Legal

- El APK **sin** código generado (solo la sonda) no lleva nada del juego.
- El APK **con** código generado lleva dentro el juego traducido, igual que
  `nfsmw.exe`. Es para tus dispositivos: **no se publica ni se comparte**.
- `*.apk` y `app/generated-android/` están en `.gitignore`.

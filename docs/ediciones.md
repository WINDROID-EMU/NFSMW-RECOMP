# Ediciones del juego

Cada `default.xex` distinto es un programa distinto. El de la edición USA no es el
español con otros textos: es otra compilación, con casi todo el código unos cientos de
bytes antes. Y este proyecto nombra funciones y datos del juego **por su dirección**:

- `app/overrides.toml` y `app/huecos.toml`, las funciones que el codegen no encuentra solo;
- los ganchos de C++ (`android/app/src/main/cpp/ganchos.cpp`, `render_targets.cpp`,
  `app/src/nfsmw_app.h`), que sustituyen funciones del juego o pisan datos suyos.

Todas esas direcciones son las de la **PAL España**, la edición de referencia. Un gancho
puesto en la dirección española cae, en otra edición, en mitad de otra función.

Lo que hay aquí lleva esas direcciones a otra edición sin tocar los fuentes, que siguen
escritos para la de referencia. El método es el de
[nfsmw-nx](https://github.com/StevensND/nfsmw-nx) (`tools/editions/`, GPL-3.0),
reescrito para este proyecto: sin numpy, con los límites de las funciones sacados de
`.pdata`, y con una tabla por edición guardada en el repositorio.

Esta página es la del motor de Xenos. El motor nativo traduce otra app, la de
nfsmw-android, con el mismo `emparejar.py` y las herramientas de nfsmw-nx: ver
[motor-nativo.md](motor-nativo.md#otras-ediciones-usa).

## Ediciones

| Edición | Estado | Relación con la de referencia |
|---|---|---|
| PAL España (`pal_es`) | referencia | todas las direcciones de los fuentes son suyas |
| USA (`usa`) | compila; ver "Qué se ha comprobado" | otra compilación: `.text` y `.rdata` se mueven, `.data` no |
| PAL Alemania, Italia | sin probar: no se ha tenido el XEX | según nfsmw-nx, la **misma** compilación que la española con otro idioma; se reconocen por el PDB y se comprueban contra la tabla española |
| PAL Francia | sin probar | probablemente como las dos anteriores; se trata igual |
| PAL inglesa, Japón | sin soporte | otras compilaciones; hace falta su tabla |

Solo en Android. El build de Windows (`CONSTRUIR.bat`) sigue siendo para PAL España.

**Un APK vale para una edición.** `libmain.so` lleva el código traducido de un solo
ejecutable. La pantalla de inicio dice para cuál se compiló y, al elegir la ISO, lee su
índice (`IsoXex.java`): avisa si su `default.xex` es el de otra edición, y también si al
fichero le falta el final, que es lo que deja una descarga o una extracción cortada.

## Usarlo: compilar para tu ISO

No cambia nada respecto a [android.md](android.md):

```
EXTRAER_XEX.bat                          :: deja assets/game_root/default.xex
python tools/android/generar_codigo.py
cd android && gradlew assembleRelease
```

`generar_codigo.py` mira qué edición es tu XEX antes de generar:

```
== Edicion del juego
[ok] USA (usa), idioma 1, pais 103
     direcciones traducidas con tools/ediciones/usa/direcciones.tsv
```

y deja apuntado en `app/generated-android/edicion.json` para qué edición es el código. De
ahí salen dos cosas:

- **Gradle** pasa al juego el idioma y el país de Xbox 360 de esa edición
  (`--user_language`, `--user_country`: 5 y 31 para España, 1 y 103 para USA) y guarda el
  SHA-256 del XEX para el aviso de la ISO.
- **CMake**, si la edición no es la de referencia, compila los ganchos desde copias con
  las direcciones traducidas (`app/generated-android/edicion/src/`). Las copias se rehacen
  solas cuando cambia el original; para eso el build necesita Python.

Si el XEX no es de una edición con soporte, se para ahí y dice su SHA-256 y su PDB.

### Dos ediciones a la vez

Para no pisar el código generado de una con el de la otra:

```
python tools/android/generar_codigo.py --xex D:\usa\default.xex --generado app/generated-android/usa
cd android && gradlew assembleRelease -Pnfsmw.generado=<repo>/app/generated-android/usa
```

Cada carpeta lleva su propia compilación nativa (la primera vez compila el SDK entero otra
vez). Los dos APK tienen el mismo identificador: instalar uno sustituye al otro.

## Cómo se reconoce una edición

1. Por el **SHA-256** del `default.xex`, contra `tools/ediciones/ediciones.json`.
2. Si no está, por el **nombre del PDB** que el ejecutable lleva dentro
   (`NfsMWEuropeSpanRelease.pdb`, `NfsMWRelease.pdb`...), y comprobando el código: la tabla
   de la edición guarda, para cada dirección, un resumen de las 16 instrucciones que tiene
   que haber allí. Si todas cuadran, ese XEX es la misma compilación aunque no se haya
   visto antes (otra tirada del disco, otro idioma europeo). Si no, es otra compilación y
   se rechaza.

Para leer el PDB y el código hay que sacar la **imagen** del XEX, que va cifrado y
comprimido: `tools/ediciones/imagen.py` hace lo mismo que el SDK al cargar el juego
(AES-128-CBC con la clave de sesión, LZX), en Python puro. Tarda unos 6 segundos y la
imagen se guarda en `assets/ediciones/imagenes/`, que no se versiona: es el juego.

## Qué se guarda en el repositorio

```
tools/ediciones/
  ediciones.json            ediciones conocidas: SHA-256, PDB, idioma y país
  pal_es/direcciones.tsv    la de referencia: cada dirección consigo misma, con su resumen
  usa/direcciones.tsv       cada dirección del proyecto y su equivalente en USA
```

Son direcciones y resúmenes, no código del juego. Con la tabla, quien solo tiene la ISO
de USA compila sin necesitar el XEX español.

Solo se traduce lo que está **fuera de los comentarios**. Los comentarios cuentan la
historia de la edición de referencia, y muchos nombran direcciones que en otra edición ni
existen. Cada fichero traducido lo avisa en su cabecera.

## Añadir una edición, o rehacer una tabla

Hace falta rehacer la tabla cuando el proyecto usa una dirección nueva (un gancho nuevo,
una línea nueva en `overrides.toml`). Eso sí pide **los dos ejecutables**, el de
referencia y el de la edición:

```
python tools/ediciones/ediciones.py mapa usa assets/game_root/default.xex D:\usa\default.xex
```

Cómo empareja (`tools/ediciones/emparejar.py`):

- **`.text`**. Las instrucciones PowerPC se normalizan quitando lo que depende de la
  posición: desplazamientos de salto e inmediatos de 16 bits. Se buscan tiras de 12
  instrucciones que salgan una sola vez en cada edición (anclas), se dejan las que van en
  el mismo orden, y cada dirección se traduce con el desplazamiento de su ancla anterior
  o de la siguiente. Cada traducción se comprueba con 16 instrucciones.
- **`.rdata`**, por contenido: tiene que salir una sola vez en la otra edición.
- **`.data`**, misma dirección: no se mueve entre PAL y USA.
- El **final** de una función (`end = 0x...` en `huecos.toml`) se comprueba con las 16
  instrucciones de antes, que son suyas; las de después son de la función siguiente.

Normalizar quita también los inmediatos que son campos de una estructura
(`lwz r3,0x854(r31)`), y un gancho depende de ellos. Por eso las funciones enganchadas
desde C++ se comparan además **enteras y sin normalizar**: solo se acepta que cambien los
destinos de salto y las direcciones absolutas, y tienen que cambiar a lo que diga la tabla.

Una declaración de `huecos.toml` cuyo sitio en la otra edición es dudoso se quita, porque
puesta en mal sitio partiría otra función. Las que hagan falta de verdad las enseña
`tools/huecos.py` tras el codegen y van a `tools/ediciones/<edicion>/huecos.toml`, que se
añade al traducido. Una dirección que el emparejador no resuelve se puede fijar a mano en
`tools/ediciones/<edicion>/correcciones.json`: `{"direcciones": {"REF": "OTRA"}}`.

Después, la edición nueva se apunta en `ediciones.json` con su SHA-256, su PDB y su
idioma y país.

## Qué se ha comprobado (USA)

**Falta lo principal: jugarlo.** La ISO de USA con la que se hizo esto estaba incompleta
(3,8 GB de 7,5: le faltaban `ZZDATA5.BIN` a `ZZDATA9.BIN` y los vídeos), así que el APK de
USA compila pero no se ha visto arrancar. El ejecutable sí estaba entero, que es lo que
hace falta para todo lo demás.

Con el XEX de USA (`aebdf3c1…`, `NfsMWRelease.pdb`, entrada en `0x8262E768`):

- 1.437.790 anclas sobre 2.043.011 instrucciones. De las 886 direcciones del proyecto,
  884 tienen traducción exacta; las otras dos son huecos de una zona donde el código
  cambió (`0x823AF0C0` y `0x823AF1A0`) y se quitan.
- Las 11 funciones enganchadas desde C++ son idénticas instrucción a instrucción, salvo
  direcciones.
- El codegen de USA da 56.575 funciones frente a 56.578 en PAL, los **mismos cuatro
  avisos** en las direcciones equivalentes, y los mismos huecos de código. Las funciones
  sin pareja están todas en las pocas zonas que cambiaron.
- El codegen de PAL por este camino (manifiesto y `.toml` traducidos con la tabla
  identidad) da el código generado **byte a byte igual** que el de siempre.

`0x8262E768` es, por cierto, la dirección que aparecía en el log de quien probaba con otra
ISO (`No function registered at 8262E768`, ver `app/overrides.toml`): era la entrada del
ejecutable de USA.

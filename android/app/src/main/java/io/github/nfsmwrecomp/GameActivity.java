package io.github.nfsmwrecomp;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;
import android.util.TypedValue;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;

/**
 * La actividad del juego (y de la sonda de Vulkan).
 *
 * Corre en su propio proceso (":juego", ver AndroidManifest.xml). Las librerias
 * nativas no se pueden descargar ni volver a cargar en el mismo proceso, asi
 * que cada partida o cada sonda empieza en un proceso limpio y al salir se
 * mata. La pantalla de inicio vive en el proceso principal y no se entera.
 *
 * Los argumentos los monta Ajustes.argumentos() en la pantalla de inicio. Aqui
 * solo se anade uno: la ISO.
 */
public class GameActivity extends SDLActivity {

    static final String EXTRA_ARGUMENTOS = "io.github.nfsmwrecomp.ARGUMENTOS";
    static final String EXTRA_ISO = "io.github.nfsmwrecomp.ISO";
    /** Ajustes.RES_480P, RES_720P o RES_NATIVA. */
    static final String EXTRA_RESOLUCION = "io.github.nfsmwrecomp.RESOLUCION";
    /** Llenar la pantalla entera en vez de 16:9 con barras. */
    static final String EXTRA_ESTIRAR = "io.github.nfsmwrecomp.ESTIRAR";
    static final String EXTRA_MOSTRAR_FPS = "io.github.nfsmwrecomp.MOSTRAR_FPS";

    private static final String TAG = "nfsmw";

    private String[] argumentos = new String[0];

    private final Handler reloj = new Handler(Looper.getMainLooper());
    private TextView textoFps;
    private Runnable actualizarFps;
    private TouchControllerView touchController;

    /** Los fps del juego (tools/parche_fps.py), o -1 si el plugin no ha cargado. */
    private static native float nativeFps();

    /** Milisegundos por fotograma, media del ultimo medio segundo. */
    private static native float nativeMsPorFotograma();

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(Idioma.envolver(base));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Intent intent = getIntent();
        ArrayList<String> lista = new ArrayList<>();
        String[] recibidos = intent.getStringArrayExtra(EXTRA_ARGUMENTOS);
        if (recibidos != null) {
            lista.addAll(Arrays.asList(recibidos));
        }

        // LA ISO NO SE COPIA: se le pasa al juego su URI content:// tal cual.
        // El SDK la monta con DiscImageDevice y, cuando toca mapearla, pide el
        // descriptor de vuelta por JNI a openContentFd (mas abajo). Ver
        // tools/parche_iso.py.
        //
        // Pasar /proc/self/fd/N con el descriptor ya abierto NO funciona: open()
        // sobre esa ruta reabre el fichero de verdad, y la app no tiene permiso
        // sobre el almacenamiento compartido.
        String iso = intent.getStringExtra(EXTRA_ISO);
        if (iso != null) {
            lista.add("--game_data_root=" + iso);
        }

        // Apaisado y bloqueado. El manifiesto ya lo pide, pero algunas ROMs y
        // Android 16 se lo saltan; pedirlo aqui tambien es lo que acaba
        // mandando.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        // Motor nativo: sus carpetas, sus ajustes y sus shaders, antes de que
        // super.onCreate cargue las librerias.
        if (MotorNativo.ACTIVO) {
            MotorNativo.preparar(this);
        }

        argumentos = lista.toArray(new String[0]);
        // Release: sin registro (ver Ajustes.argumentos).
        if (BuildConfig.DEBUG) Log.i(TAG, "Argumentos: " + String.join(" ", argumentos));
        super.onCreate(savedInstanceState);

        ajustarSuperficie(intent.getStringExtra(EXTRA_RESOLUCION),
                intent.getBooleanExtra(EXTRA_ESTIRAR, false));
        if (intent.getBooleanExtra(EXTRA_MOSTRAR_FPS, true)) {
            ponerContadorFps();
        }

        vibracion = new Ajustes(this).vibracion();
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            VibratorManager vm = (VibratorManager) getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
            vibrador = vm == null ? null : vm.getDefaultVibrator();
        } else {
            vibrador = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        }
        if (vibrador != null && !vibrador.hasVibrator()) vibrador = null;

        touchController = new TouchControllerView(this);
        mLayout.addView(touchController, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** Un boton de mando fisico esconde los controles tactiles. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (touchController != null && TouchControllerView.esFuenteDeMando(event.getSource())) {
            touchController.mandoUsado();
        }
        return super.dispatchKeyEvent(event);
    }

    /** Y tambien los sticks y gatillos, que llegan como movimiento. */
    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (touchController != null && TouchControllerView.esFuenteDeMando(event.getSource())) {
            touchController.mandoUsado();
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    protected void onPause() {
        if (touchController != null) touchController.clearInput();
        callarVibracion(CALLADA_SEGUNDO_PLANO, true);
        // En segundo plano no hay nadie mirando el rotulo: no despertar al hilo
        // principal dos veces por segundo para nada.
        reloj.removeCallbacksAndMessages(null);
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        callarVibracion(CALLADA_SEGUNDO_PLANO, false);
        if (actualizarFps != null) {
            reloj.removeCallbacks(actualizarFps);
            reloj.post(actualizarFps);
        }
    }

    /**
     * Donde y a que tamano se ve el juego. Dos cosas por separado:
     *
     *   la VISTA   el rectangulo de la pantalla que ocupa. Sin estirar, el
     *              16:9 mas grande que cabe, centrado sobre negro; estirando,
     *              la pantalla entera.
     *   el BUFER   los pixeles que se presentan: 720x480, 1280x720, o los de
     *              la vista. El compositor de Android escala el bufer a la
     *              vista gratis, asi que 720x480 en una vista 16:9 queda
     *              anamorfico, como el 480p panoramico de la Xbox 360.
     *
     * La proporcion la pone la vista: el presentador va con
     * present_letterbox=false y llena el bufer entero (ver Ajustes).
     *
     * SDL mete la superficie con WRAP_CONTENT, y con setFixedSize un
     * SurfaceView en WRAP_CONTENT mide lo que el bufer (SurfaceView.onMeasure):
     * la vista encogia al tamano del bufer, en una esquina. Por eso aqui la
     * vista lleva siempre un tamano exacto.
     */
    private void ajustarSuperficie(String resolucion, boolean estirar) {
        if (mSurface == null || mLayout == null) {
            return;
        }
        mLayout.setBackgroundColor(Color.BLACK);
        int anchoBufer = Ajustes.ancho(resolucion);
        int altoBufer = Ajustes.alto(resolucion);
        if (anchoBufer > 0) {
            mSurface.getHolder().setFixedSize(anchoBufer, altoBufer);
        } else {
            mSurface.getHolder().setSizeFromLayout();
        }
        // El tamano real de lo que deja Android (recorte de la camara, barras
        // del sistema) solo se sabe al maquetar, y puede cambiar.
        mLayout.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
            private int ultimoAncho = -1;
            private int ultimoAlto = -1;

            @Override
            public void onLayoutChange(View v, int l, int t, int r, int b,
                                       int ol, int ot, int or, int ob) {
                int ancho = r - l;
                int alto = b - t;
                if (ancho <= 0 || alto <= 0 || (ancho == ultimoAncho && alto == ultimoAlto)) {
                    return;
                }
                ultimoAncho = ancho;
                ultimoAlto = alto;
                int w = ancho;
                int h = alto;
                if (!estirar) {
                    if (ancho * 9 > alto * 16) {
                        w = alto * 16 / 9;
                    } else {
                        h = ancho * 9 / 16;
                    }
                }
                RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(w, h);
                lp.addRule(RelativeLayout.CENTER_IN_PARENT);
                // Cambiar la maquetacion desde su propio aviso no se aplica
                // hasta la siguiente pasada: se pide despues.
                v.post(() -> mSurface.setLayoutParams(lp));
                if (BuildConfig.DEBUG) {
                    Log.i(TAG, "Superficie: vista " + w + "x" + h + ", bufer "
                            + (anchoBufer > 0 ? anchoBufer + "x" + altoBufer : w + "x" + h)
                            + (estirar ? " (estirada)" : " (16:9)"));
                }
            }
        });
    }

    /** Un rotulo en la esquina con los fps del juego, cada medio segundo. */
    private void ponerContadorFps() {
        if (mLayout == null) {
            return;
        }
        textoFps = new TextView(this);
        textoFps.setTextColor(Color.WHITE);
        textoFps.setBackgroundColor(0x99000000);
        textoFps.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        textoFps.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        int m = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 6,
                getResources().getDisplayMetrics());
        textoFps.setPadding(m, m / 2, m, m / 2);
        textoFps.setText("-- fps");
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.WRAP_CONTENT, RelativeLayout.LayoutParams.WRAP_CONTENT);
        lp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        lp.addRule(RelativeLayout.ALIGN_PARENT_START);
        lp.setMargins(m * 2, m * 2, 0, 0);
        mLayout.addView(textoFps, lp);
        actualizarFps = new Runnable() {
            // El ultimo texto puesto: setText relanza la maquetacion del rotulo
            // aunque el texto no cambie, y con los fps clavados es lo normal.
            private String ultimo = "";

            @Override
            public void run() {
                float fps, ms;
                try {
                    fps = nativeFps();
                    ms = nativeMsPorFotograma();
                } catch (UnsatisfiedLinkError e) {
                    fps = ms = -1;
                }
                String texto = fps < 0
                        ? "-- fps"
                        : String.format(Locale.ROOT, "%.0f fps  %.1f ms", fps, ms);
                if (!texto.equals(ultimo)) {
                    textoFps.setText(texto);
                    ultimo = texto;
                }
                reloj.postDelayed(this, 500);
            }
        };
        reloj.post(actualizarFps);
    }

    @Override
    protected void onDestroy() {
        if (touchController != null) touchController.disconnect();
        reloj.removeCallbacksAndMessages(null);
        super.onDestroy();
        // Proceso propio: al cerrar la partida, fuera. Asi la siguiente arranca
        // con las librerias nativas sin cargar.
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    /**
     * SDL3 va enlazado ESTATICO dentro de librexruntime.so, y ahi estan sus
     * metodos JNI (nativeSetupJNI y compania). Java solo los encuentra en
     * librerias cargadas con System.loadLibrary, no en las que arrastra el
     * enlazador como dependencia, asi que rexruntime va explicita. La ultima
     * de la lista es la que tiene SDL_main.
     */
    @Override
    protected String[] getLibraries() {
        // Con el motor nativo SDL3 va en su propia libreria, como espera SDLActivity.
        return MotorNativo.ACTIVO
                ? new String[] {"SDL3", "main"}
                : new String[] {"rexruntime", "main"};
    }

    /**
     * SDL pide una orientacion segun el tamano de su ventana al crearla, y con
     * un bufer fijo (720x480, 1280x720) o al volver de segundo plano le puede
     * salir vertical. El juego va siempre apaisado, hacia el lado que se gire
     * el movil. La pantalla de inicio no: esa gira libremente.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getArguments() {
        return argumentos;
    }

    /**
     * Puente que busca el parche Android del SDK (android_runtime.cpp) por esta
     * firma exacta en la clase de la actividad, para abrir URIs content:// desde
     * codigo nativo. Devuelve null si algo falla.
     */
    // --- La vibracion del mando tactil (Ajustes.vibracion).
    private static volatile boolean vibracion;
    private static Vibrator vibrador;
    // Lo ultimo que pidio el juego (0..255), lo que esta puesto y por que se
    // calla (en segundo plano, editando el mando): sin perder lo pedido, para
    // volver a vibrar al acabar.
    private static int vibracionPedida;
    private static int vibracionPuesta;
    private static int vibracionCallada;
    static final int CALLADA_SEGUNDO_PLANO = 1;
    static final int CALLADA_EDITOR = 2;
    // Hay un mando fisico (USB o Bluetooth): vibra el, no el movil.
    static final int CALLADA_MANDO = 4;

    /**
     * El mando tactil vibra: lo pide el juego desde su hilo (nativo_android.cpp),
     * con una fuerza de 0 a 255; 0 la para. Dura hasta que el juego la cambie,
     * como en el mando de la Xbox 360.
     */
    public static synchronized void vibrarMando(int amplitud) {
        vibracionPedida = amplitud;
        aplicarVibracion();
    }

    /** Calla la vibracion por un motivo (CALLADA_*) mientras dure. */
    static synchronized void callarVibracion(int motivo, boolean callar) {
        vibracionCallada = callar ? vibracionCallada | motivo : vibracionCallada & ~motivo;
        aplicarVibracion();
    }

    private static void aplicarVibracion() {
        Vibrator v = vibrador;
        if (v == null) return;
        int a = vibracion && vibracionCallada == 0 ? Math.min(255, vibracionPedida) : 0;
        if (a == vibracionPuesta) return;
        vibracionPuesta = a;
        if (a <= 0) {
            v.cancel();
        } else {
            // Una forma de onda que se repite: sigue hasta la siguiente llamada.
            v.vibrate(VibrationEffect.createWaveform(new long[] {1000}, new int[] {a}, 0));
        }
    }

    public static ParcelFileDescriptor openContentFd(String uri, String mode) {
        SDLActivity yo = mSingleton;
        if (yo == null || uri == null) {
            return null;
        }
        try {
            return yo.getContentResolver()
                    .openFileDescriptor(Uri.parse(uri), mode == null ? "r" : mode);
        } catch (Exception e) {
            return null;
        }
    }
}

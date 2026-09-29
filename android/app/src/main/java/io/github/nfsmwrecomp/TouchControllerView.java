package io.github.nfsmwrecomp;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.hardware.input.InputManager;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * A lightweight multitouch Xbox-style controller layered over the SDL surface.
 *
 * El boton OCULTAR/TACTIL se va a los 4 s de aparecer o de usarse, para no
 * estorbar. Tocar donde esta lo vuelve a sacar (ese toque no lo pulsa).
 *
 * Con un mando fisico (conectado o en uso) se oculta entero, tambien el boton.
 * Entonces cualquier toque lo vuelve a sacar, para poder encender los
 * controles tactiles; usar el mando lo esconde otra vez.
 *
 * EDITOR (EditorTactilActivity, desde la pantalla de inicio): la misma vista
 * en modo editor deja mover cada control con el dedo, cambiar su tamano y la
 * opacidad de todos. Se guarda en nfsmw_touch_controller por control, como
 * fraccion de la pantalla ("<id>_x", "<id>_y") y escala ("<id>_s"), asi que
 * vale para cualquier resolucion. Lo que no se toca no se guarda y sigue la
 * disposicion de fabrica. La partida lo lee al arrancar, en su proceso.
 */
public final class TouchControllerView extends View {
    private static final int DPAD_UP = 1 << 11;
    private static final int DPAD_DOWN = 1 << 12;
    private static final int DPAD_LEFT = 1 << 13;
    private static final int DPAD_RIGHT = 1 << 14;
    private static final int START = 1 << 6;
    private static final int BACK = 1 << 4;
    private static final int L3 = 1 << 7;
    private static final int R3 = 1 << 8;
    private static final int LB = 1 << 9;
    private static final int RB = 1 << 10;
    private static final int A = 1 << 0;
    private static final int B = 1 << 1;
    private static final int X = 1 << 2;
    private static final int Y = 1 << 3;
    private static final String PREFS = "nfsmw_touch_controller";
    // El nombre con el que venia de otro port. Se borra al arrancar.
    private static final String PREFS_VIEJAS = "skate3_touch_controller";

    // Color de acento #6F7432: botones pulsados y borde del boton HIDE/TOUCH.
    // Cada uso conserva su transparencia.
    private static final int PULSADO = Color.argb(190, 0x6F, 0x74, 0x32);
    private static final int PULSADO_CRUCETA = Color.argb(180, 0x6F, 0x74, 0x32);
    private static final int BORDE_TOGGLE = Color.argb(220, 0x6F, 0x74, 0x32);
    private static final int ACENTO = Color.rgb(0x6F, 0x74, 0x32);
    private static final int RELLENO = Color.argb(105, 12, 12, 15);
    private static final int BORDE = Color.argb(205, 255, 255, 255);
    private static final int FONDO_BOTON = Color.argb(175, 15, 15, 18);
    private static final String PREF_VISIBLE = "visible";
    private static final String PREF_OPACIDAD = "opacidad";
    // Lo que dura el boton OCULTAR/TACTIL en pantalla desde que aparece o se usa.
    private static final long TOGGLE_MS = 4000;
    // Editor: pasos y limites del tamano y la opacidad.
    private static final float PASO = .1f;
    private static final float ESCALA_MIN = .5f;
    private static final float ESCALA_MAX = 2f;
    private static final float OPACIDAD_MIN = .2f;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    // El recuadro del control elegido en el editor.
    private final Paint marco = new Paint(Paint.ANTI_ALIAS_FLAG);
    // Reutilizado en cada etiqueta: getFontMetrics() sin argumento crea un
    // objeto nuevo en cada llamada, y hay ~17 etiquetas por dibujado.
    private final Paint.FontMetrics metricas = new Paint.FontMetrics();
    // La densidad no cambia mientras vive la vista: se lee una vez.
    private final float density;
    private final List<Control> controls = new ArrayList<>();
    private final SparseArray<Control> pointers = new SparseArray<>();
    private final SharedPreferences preferences;
    private boolean controlsVisible;
    // Hay un mando fisico: conectado, o se ha usado en esta partida.
    private boolean mandoFisico;
    // El boton OCULTAR/TACTIL: a la vista hasta TOGGLE_MS despues de usarlo.
    private boolean toggleVisible = true;
    private final Runnable ocultarToggle = () -> {
        toggleVisible = false;
        invalidate();
    };
    private final InputManager inputManager;
    private final InputManager.InputDeviceListener oyenteMandos =
            new InputManager.InputDeviceListener() {
                @Override
                public void onInputDeviceAdded(int deviceId) {
                    if (esMando(deviceId)) entrarModoMando();
                }

                @Override
                public void onInputDeviceRemoved(int deviceId) {
                    if (mandoFisico && !hayMandoFisico()) salirModoMando();
                }

                @Override
                public void onInputDeviceChanged(int deviceId) {
                    boolean hay = hayMandoFisico();
                    if (hay && !mandoFisico) entrarModoMando();
                    else if (!hay && mandoFisico) salirModoMando();
                }
            };
    private final RectF toggle = new RectF();
    private float lastWidth;
    private float lastHeight;
    // Textos en el idioma de la app.
    private final String textoOcultar;
    private final String textoMostrar;
    private final String textoTamano;
    private final String textoOpacidad;
    private final String textoRestaurar;
    private final String textoCancelar;
    private final String textoListo;
    private final String textoAyuda;

    // --- Editor. Solo en EditorTactilActivity: en la partida no se edita.
    private final boolean editando;
    // LISTO y CANCELAR: el editor se cierra.
    private final Runnable alTerminar;
    private Control elegido;
    // El dedo que arrastra, y donde se agarro el control respecto a su centro.
    private int dedoEditor = -1;
    private float agarreX;
    private float agarreY;
    // Opacidad de todos los controles (no del boton ni de la barra del editor).
    private float opacidad;
    private final RectF tamMenos = new RectF();
    private final RectF tamMas = new RectF();
    private final RectF opaMenos = new RectF();
    private final RectF opaMas = new RectF();
    private final RectF restaurar = new RectF();
    private final RectF cancelar = new RectF();
    private final RectF listo = new RectF();
    private final RectF fondoAyuda = new RectF();
    private float textoTamX;
    private float textoOpaX;
    private float tamanoAyuda;

    private Stick leftStick;
    private Stick rightStick;
    private Trigger leftTrigger;
    private Trigger rightTrigger;

    /** El mando de la partida. */
    public TouchControllerView(Context context) {
        this(context, null);
    }

    /**
     * Con alTerminar, el editor: todos los controles a la vista y editandose,
     * sin mirar los mandos fisicos y sin hablar con el juego (en la pantalla
     * de inicio no hay librerias nativas).
     */
    public TouchControllerView(Context context, Runnable alTerminar) {
        super(context);
        this.alTerminar = alTerminar;
        editando = alTerminar != null;
        density = getResources().getDisplayMetrics().density;
        setWillNotDraw(false);
        setFocusable(false);
        fill.setStyle(Paint.Style.FILL);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(dp(2));
        marco.setStyle(Paint.Style.STROKE);
        marco.setStrokeWidth(dp(3));
        marco.setColor(ACENTO);
        label.setColor(Color.WHITE);
        label.setTextAlign(Paint.Align.CENTER);
        label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        textoOcultar = context.getString(R.string.tactil_ocultar);
        textoMostrar = context.getString(R.string.tactil_mostrar);
        textoTamano = context.getString(R.string.editor_tamano);
        textoOpacidad = context.getString(R.string.editor_opacidad);
        textoRestaurar = context.getString(R.string.editor_restaurar);
        textoCancelar = context.getString(R.string.editor_cancelar);
        textoListo = context.getString(R.string.editor_listo);
        textoAyuda = context.getString(R.string.editor_ayuda);
        context.deleteSharedPreferences(PREFS_VIEJAS);
        preferences = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        opacidad = preferences.getFloat(PREF_OPACIDAD, 1f);
        inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (editando) {
            controlsVisible = true;
            toggleVisible = false;
            return;
        }
        mandoFisico = hayMandoFisico();
        // Con mando, oculto del todo; sin el, como lo dejara el usuario.
        controlsVisible = !mandoFisico && preferences.getBoolean(PREF_VISIBLE, true);
        toggleVisible = !mandoFisico;
    }

    /**
     * Lo llama GameActivity con cada evento de mando fisico: esconde los
     * controles tactiles y el boton. Barato si ya estaba todo oculto, que es
     * lo normal (los ejes mandan eventos muy a menudo).
     */
    public void mandoUsado() {
        if (mandoFisico && !controlsVisible && !toggleVisible) return;
        entrarModoMando();
    }

    private void entrarModoMando() {
        mandoFisico = true;
        removeCallbacks(ocultarToggle);
        toggleVisible = false;
        if (controlsVisible) {
            controlsVisible = false;
            rehacer();
        } else {
            invalidate();
        }
    }

    private void salirModoMando() {
        mandoFisico = false;
        controlsVisible = preferences.getBoolean(PREF_VISIBLE, true);
        rehacer();
        mostrarToggle();
    }

    /** Saca el boton OCULTAR/TACTIL, y que se vaya a los TOGGLE_MS. */
    private void mostrarToggle() {
        toggleVisible = true;
        removeCallbacks(ocultarToggle);
        postDelayed(ocultarToggle, TOGGLE_MS);
        invalidate();
    }

    /** Un evento de estas fuentes viene de un mando fisico. */
    static boolean esFuenteDeMando(int sources) {
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    public void clearInput() {
        for (Control control : controls) control.release();
        pointers.clear();
        sendState();
        invalidate();
    }

    public void disconnect() {
        for (Control control : controls) control.release();
        pointers.clear();
        TouchControllerBridge.trySetState(-1, 0, 0, 0, 0, 0, 0);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        // null: los avisos llegan por el hilo principal, el de la vista.
        if (!editando) inputManager.registerInputDeviceListener(oyenteMandos, null);
        // Al empezar la partida se ve, y a los TOGGLE_MS se va.
        if (!editando && toggleVisible) mostrarToggle();
        sendState();
    }

    @Override
    protected void onDetachedFromWindow() {
        if (!editando) inputManager.unregisterInputDeviceListener(oyenteMandos);
        removeCallbacks(ocultarToggle);
        disconnect();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        buildControls(width, height);
    }

    /**
     * La disposicion de fabrica, y encima lo guardado con el editor. Las
     * posiciones de fabrica van en fracciones de la pantalla y los tamanos en
     * 'unit', 1/16 del ancho o 1/9 del alto, lo que sea menor.
     */
    private void buildControls(float width, float height) {
        lastWidth = width;
        lastHeight = height;
        controls.clear();
        float unit = Math.min(width / 16f, height / 9f);
        float stickRadius = unit * 0.78f;
        float buttonRadius = unit * 0.38f;

        leftStick = new Stick("ls", "L", width * .16f, height * .73f, stickRadius);
        rightStick = new Stick("rs", "R", width * .67f, height * .77f, stickRadius);
        controls.add(leftStick);
        controls.add(rightStick);

        controls.add(new Dpad("dpad", width * .32f, height * .72f, unit * .98f));

        float faceX = width * .85f;
        float faceY = height * .68f;
        controls.add(new ButtonControl("a", "A", faceX, faceY + unit * .68f, buttonRadius, A));
        controls.add(new ButtonControl("b", "B", faceX + unit * .68f, faceY, buttonRadius, B));
        controls.add(new ButtonControl("x", "X", faceX - unit * .68f, faceY, buttonRadius, X));
        controls.add(new ButtonControl("y", "Y", faceX, faceY - unit * .68f, buttonRadius, Y));

        float shoulderW = unit * 1.25f;
        float shoulderH = unit * .55f;
        leftTrigger = new Trigger("lt", "LT", width * .07f, height * .10f, shoulderW, shoulderH);
        rightTrigger = new Trigger("rt", "RT", width * .93f, height * .10f, shoulderW, shoulderH);
        controls.add(leftTrigger);
        controls.add(new RectButton("lb", "LB", width * .20f, height * .10f,
                                    shoulderW, shoulderH, LB));
        controls.add(new RectButton("rb", "RB", width * .80f, height * .10f,
                                    shoulderW, shoulderH, RB));
        controls.add(rightTrigger);

        controls.add(new RectButton("back", "BACK", width * .44f, height * .79f,
                                    unit * .82f, unit * .45f, BACK));
        controls.add(new RectButton("start", "START", width * .55f, height * .79f,
                                    unit * .92f, unit * .45f, START));
        controls.add(new ButtonControl("l3", "L3", width * .07f, height * .48f,
                                       buttonRadius * .82f, L3));
        controls.add(new ButtonControl("r3", "R3", width * .73f, height * .48f,
                                       buttonRadius * .82f, R3));

        for (Control c : controls) cargar(c);
        colocarBotones(width, height, unit);
        sendState();
        invalidate();
    }

    /** Lo guardado con el editor para este control, si lo hay. */
    private void cargar(Control c) {
        if (!preferences.contains(c.id + "_x")) return;
        c.cx = preferences.getFloat(c.id + "_x", 0f) * lastWidth;
        c.cy = preferences.getFloat(c.id + "_y", 0f) * lastHeight;
        c.escala = preferences.getFloat(c.id + "_s", 1f);
        c.personalizado = true;
        c.colocar();
    }

    /** OCULTAR/TACTIL arriba, y la barra del editor en el centro. */
    private void colocarBotones(float width, float height, float unit) {
        label.setTextSize(dp(11));
        // Lo que pida el texto, que en otros idiomas es mas largo que HIDE.
        float toggleWidth = Math.max(controlsVisible ? unit * 1.05f : unit * 1.35f,
                label.measureText(controlsVisible ? textoOcultar : textoMostrar) + dp(24));
        float toggleTop = Math.max(dp(18), height * .035f);
        toggle.set(width * .5f - toggleWidth * .5f, toggleTop,
                   width * .5f + toggleWidth * .5f, toggleTop + unit * .46f);

        // Dos filas en el centro, donde la disposicion de fabrica no tiene
        // nada: [-] TAMANO [+]  [-] OPACIDAD [+], y debajo las acciones.
        label.setTextSize(dp(12));
        float lado = unit * .6f;
        float hueco = dp(10);
        float anchoTam = label.measureText(textoTamano + " 200 %") + dp(16);
        float anchoOpa = label.measureText(textoOpacidad + " 100 %") + dp(16);
        float fila1 = lado * 4 + anchoTam + anchoOpa + hueco * 7;
        float x = width * .5f - fila1 * .5f;
        // Entre LB/RB (arriba, hasta ~13 %) y L3/R3 (a media altura, desde ~44 %).
        float y = height * .18f;
        tamMenos.set(x, y, x + lado, y + lado);
        x += lado + hueco;
        textoTamX = x + anchoTam * .5f;
        x += anchoTam + hueco;
        tamMas.set(x, y, x + lado, y + lado);
        x += lado + hueco * 3;
        opaMenos.set(x, y, x + lado, y + lado);
        x += lado + hueco;
        textoOpaX = x + anchoOpa * .5f;
        x += anchoOpa + hueco;
        opaMas.set(x, y, x + lado, y + lado);

        float anchoRes = label.measureText(textoRestaurar) + dp(28);
        float anchoCan = label.measureText(textoCancelar) + dp(28);
        float anchoLis = label.measureText(textoListo) + dp(28);
        float fila2 = anchoRes + anchoCan + anchoLis + hueco * 2;
        x = width * .5f - fila2 * .5f;
        y += lado + hueco;
        restaurar.set(x, y, x + anchoRes, y + lado);
        x += anchoRes + hueco;
        cancelar.set(x, y, x + anchoCan, y + lado);
        x += anchoCan + hueco;
        listo.set(x, y, x + anchoLis, y + lado);

        // La ayuda, en una linea: mas pequena si no cabe.
        tamanoAyuda = dp(12);
        label.setTextSize(tamanoAyuda);
        float anchoAyuda = label.measureText(textoAyuda);
        if (anchoAyuda > width * .9f) {
            tamanoAyuda *= width * .9f / anchoAyuda;
            anchoAyuda = width * .9f;
        }
        y += lado + hueco;
        fondoAyuda.set(width * .5f - anchoAyuda * .5f - dp(10), y,
                       width * .5f + anchoAyuda * .5f + dp(10), y + tamanoAyuda * 2f);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (editando) {
            for (Control control : controls) control.draw(canvas);
            if (elegido != null) {
                float r = elegido.alcance() + dp(6);
                canvas.drawRect(elegido.cx - r, elegido.cy - r, elegido.cx + r, elegido.cy + r, marco);
            }
            drawEditor(canvas);
            return;
        }
        if (toggleVisible) drawToggle(canvas);
        if (!controlsVisible) return;
        for (Control control : controls) control.draw(canvas);
    }

    private void drawToggle(Canvas canvas) {
        drawBoton(canvas, toggle, controlsVisible ? textoOcultar : textoMostrar, dp(11));
    }

    private void drawEditor(Canvas canvas) {
        float t = dp(12);
        drawBoton(canvas, tamMenos, "−", dp(18));
        drawBoton(canvas, tamMas, "+", dp(18));
        drawBoton(canvas, opaMenos, "−", dp(18));
        drawBoton(canvas, opaMas, "+", dp(18));
        // Sin control elegido, TAMANO va para todos: no hay un valor que ensenar.
        String tam = elegido == null ? textoTamano
                : textoTamano + " " + Math.round(elegido.escala * 100) + " %";
        drawTexto(canvas, tam, textoTamX, tamMenos.centerY(), t);
        drawTexto(canvas, textoOpacidad + " " + Math.round(opacidad * 100) + " %",
                  textoOpaX, opaMenos.centerY(), t);
        drawBoton(canvas, restaurar, textoRestaurar, t);
        drawBoton(canvas, cancelar, textoCancelar, t);
        drawBoton(canvas, listo, textoListo, t);
        fill.setColor(FONDO_BOTON);
        canvas.drawRoundRect(fondoAyuda, dp(8), dp(8), fill);
        drawLabel(canvas, textoAyuda, fondoAyuda.centerX(), fondoAyuda.centerY(), tamanoAyuda, 1f);
    }

    /** Un boton de la interfaz (no del mando): fondo oscuro y borde de acento. */
    private void drawBoton(Canvas canvas, RectF r, String texto, float tamano) {
        fill.setColor(FONDO_BOTON);
        stroke.setColor(BORDE_TOGGLE);
        canvas.drawRoundRect(r, dp(9), dp(9), fill);
        canvas.drawRoundRect(r, dp(9), dp(9), stroke);
        drawLabel(canvas, texto, r.centerX(), r.centerY(), tamano, 1f);
    }

    /** Un texto de la barra del editor, sobre una pastilla oscura para leerlo. */
    private void drawTexto(Canvas canvas, String texto, float x, float y, float tamano) {
        label.setTextSize(tamano);
        float mitad = label.measureText(texto) * .5f + dp(8);
        fill.setColor(FONDO_BOTON);
        canvas.drawRoundRect(x - mitad, y - tamano, x + mitad, y + tamano, dp(8), dp(8), fill);
        drawLabel(canvas, texto, x, y, tamano, 1f);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (editando) return tocarEditor(event);

        int action = event.getActionMasked();
        int index = event.getActionIndex();
        int pointerId = event.getPointerId(index);
        float x = event.getX(index);
        float y = event.getY(index);

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            boolean enToggle = toggle.contains(x, y);
            if (toggleVisible && enToggle) {
                setControlsVisible(!controlsVisible);
                return true;
            }
            // Oculto: tocar donde esta lo saca, sin pulsarlo. Con mando fisico
            // y sin controles no hay nada mas que tocar, asi que vale cualquier
            // sitio (y mientras se siga tocando, no se va).
            if (enToggle || (mandoFisico && !controlsVisible)) {
                mostrarToggle();
                return true;
            }
            if (!controlsVisible) return false;
            for (int i = controls.size() - 1; i >= 0; --i) {
                Control control = controls.get(i);
                if (!control.inUse && control.contains(x, y)) {
                    pointers.put(pointerId, control);
                    control.press(x, y);
                    sendState();
                    invalidate();
                    return true;
                }
            }
            return false;
        }

        if (action == MotionEvent.ACTION_MOVE) {
            boolean handled = false;
            for (int i = 0; i < event.getPointerCount(); ++i) {
                Control control = pointers.get(event.getPointerId(i));
                if (control != null) {
                    control.move(event.getX(i), event.getY(i));
                    handled = true;
                }
            }
            if (handled) {
                sendState();
                invalidate();
            }
            return handled;
        }

        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            Control control = pointers.get(pointerId);
            if (control == null) return false;
            control.release();
            pointers.remove(pointerId);
            sendState();
            invalidate();
            return true;
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            clearInput();
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    //  Editor
    // ------------------------------------------------------------------------

    /** Un dedo cada vez: agarra un control y lo lleva, o pulsa la barra. */
    private boolean tocarEditor(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        float x = event.getX(index);
        float y = event.getY(index);
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (dedoEditor != -1) return true;
                if (tamMenos.contains(x, y)) {
                    cambiarEscala(-PASO);
                } else if (tamMas.contains(x, y)) {
                    cambiarEscala(PASO);
                } else if (opaMenos.contains(x, y)) {
                    cambiarOpacidad(-PASO);
                } else if (opaMas.contains(x, y)) {
                    cambiarOpacidad(PASO);
                } else if (restaurar.contains(x, y)) {
                    restaurarDisposicion();
                } else if (cancelar.contains(x, y)) {
                    cancelarEdicion();
                } else if (listo.contains(x, y)) {
                    guardarEdicion();
                } else {
                    // El de encima primero, como al jugar. Tocar donde no hay
                    // nada deja sin elegir, y TAMANO vuelve a ir para todos.
                    elegido = null;
                    for (int i = controls.size() - 1; i >= 0; --i) {
                        if (controls.get(i).contains(x, y)) {
                            elegido = controls.get(i);
                            break;
                        }
                    }
                    if (elegido != null) {
                        dedoEditor = event.getPointerId(index);
                        agarreX = x - elegido.cx;
                        agarreY = y - elegido.cy;
                    }
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_MOVE: {
                int i = dedoEditor == -1 ? -1 : event.findPointerIndex(dedoEditor);
                if (i < 0 || elegido == null) return true;
                // El centro no sale de la pantalla: siempre se puede volver a coger.
                elegido.situar(limitar(event.getX(i) - agarreX, 0, lastWidth),
                               limitar(event.getY(i) - agarreY, 0, lastHeight));
                elegido.personalizado = true;
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (event.getPointerId(index) == dedoEditor) dedoEditor = -1;
                return true;
            case MotionEvent.ACTION_CANCEL:
                dedoEditor = -1;
                return true;
            default:
                return true;
        }
    }

    private void cambiarEscala(float paso) {
        if (elegido != null) {
            escalar(elegido, paso);
        } else {
            for (Control c : controls) escalar(c, paso);
        }
        invalidate();
    }

    private void escalar(Control c, float paso) {
        c.escala = limitar(Math.round((c.escala + paso) * 10f) / 10f, ESCALA_MIN, ESCALA_MAX);
        c.personalizado = true;
        c.colocar();
    }

    private void cambiarOpacidad(float paso) {
        opacidad = limitar(Math.round((opacidad + paso) * 10f) / 10f, OPACIDAD_MIN, 1f);
        invalidate();
    }

    /** Todo como de fabrica. No se guarda hasta LISTO. */
    private void restaurarDisposicion() {
        for (Control c : controls) {
            c.cx = c.defX;
            c.cy = c.defY;
            c.escala = 1f;
            c.personalizado = false;
            c.colocar();
        }
        opacidad = 1f;
        elegido = null;
        invalidate();
    }

    /** Sin guardar nada. */
    private void cancelarEdicion() {
        alTerminar.run();
    }

    /**
     * Con commit() y no apply(): la partida va en otro proceso y lee el
     * fichero al arrancar; tiene que estar ya en disco.
     */
    private void guardarEdicion() {
        SharedPreferences.Editor e = preferences.edit();
        for (Control c : controls) {
            if (c.personalizado) {
                e.putFloat(c.id + "_x", c.cx / lastWidth);
                e.putFloat(c.id + "_y", c.cy / lastHeight);
                e.putFloat(c.id + "_s", c.escala);
            } else {
                e.remove(c.id + "_x").remove(c.id + "_y").remove(c.id + "_s");
            }
        }
        e.putFloat(PREF_OPACIDAD, opacidad).commit();
        alTerminar.run();
    }

    private static float limitar(float v, float min, float max) {
        return Math.max(min, Math.min(max, v));
    }

    // ------------------------------------------------------------------------

    private void setControlsVisible(boolean visible) {
        controlsVisible = visible;
        // Con mando no se guarda: la preferencia es la de jugar sin el.
        if (!mandoFisico) preferences.edit().putBoolean(PREF_VISIBLE, visible).apply();
        rehacer();
        // Recien usado: se queda los TOGGLE_MS, por si se quiere deshacer.
        mostrarToggle();
    }

    /** Suelta todo y rehace los controles (el boton cambia de ancho). */
    private void rehacer() {
        for (Control control : controls) control.release();
        pointers.clear();
        if (lastWidth > 0 && lastHeight > 0) buildControls(lastWidth, lastHeight);
        sendState();
        invalidate();
    }

    private void sendState() {
        // En el editor no hay juego al que mandarle nada.
        if (editando) return;
        if (!controlsVisible) {
            TouchControllerBridge.trySetState(-1, 0, 0, 0, 0, 0, 0);
            return;
        }
        int buttons = 0;
        for (Control control : controls) buttons |= control.buttons();
        TouchControllerBridge.trySetState(
            buttons,
            leftStick == null ? 0 : leftStick.xValue,
            leftStick == null ? 0 : leftStick.yValue,
            rightStick == null ? 0 : rightStick.xValue,
            rightStick == null ? 0 : rightStick.yValue,
            leftTrigger != null && leftTrigger.inUse ? 1 : 0,
            rightTrigger != null && rightTrigger.inUse ? 1 : 0);
    }

    /**
     * Mas estricto que SDLControllerManager.isDeviceSDLJoystick, que cuenta
     * tambien lo que solo tiene cruceta (mandos de tele, algunos teclados):
     * aqui solo gamepads y joysticks de verdad, y nada virtual.
     */
    private static boolean esMando(int deviceId) {
        InputDevice d = InputDevice.getDevice(deviceId);
        return d != null && !d.isVirtual() && esFuenteDeMando(d.getSources());
    }

    private static boolean hayMandoFisico() {
        for (int deviceId : InputDevice.getDeviceIds()) {
            if (esMando(deviceId)) return true;
        }
        return false;
    }

    /**
     * Un control: su centro (cx, cy) y su escala, que puede cambiar el editor.
     * Cada subclase guarda su forma ya calculada y la rehace en colocar(), asi
     * que dibujar no crea objetos.
     */
    private abstract class Control {
        final String id;
        // La posicion de fabrica, para RESTAURAR.
        final float defX;
        final float defY;
        float cx;
        float cy;
        float escala = 1f;
        // Movido o cambiado de tamano en el editor: se guarda.
        boolean personalizado;
        boolean inUse;
        Control(String id, float x, float y) {
            this.id = id;
            defX = cx = x;
            defY = cy = y;
        }
        /** Recalcula la forma tras moverlo o cambiarle la escala. */
        abstract void colocar();
        void situar(float x, float y) { cx = x; cy = y; colocar(); }
        /** La mitad de lo que ocupa, para el recuadro del editor. */
        abstract float alcance();
        abstract boolean contains(float x, float y);
        void press(float x, float y) { inUse = true; move(x, y); }
        void move(float x, float y) {}
        void release() { inUse = false; }
        int buttons() { return 0; }
        abstract void draw(Canvas canvas);
    }

    private final class ButtonControl extends Control {
        final String text;
        final float baseRadius;
        final int mask;
        float radius;
        ButtonControl(String id, String text, float x, float y, float radius, int mask) {
            super(id, x, y);
            this.text = text; this.baseRadius = radius; this.mask = mask;
            colocar();
        }
        void colocar() { radius = baseRadius * escala; }
        float alcance() { return radius; }
        boolean contains(float px, float py) {
            float dx = px - cx, dy = py - cy;
            return dx * dx + dy * dy <= radius * radius * 1.35f;
        }
        int buttons() { return inUse ? mask : 0; }
        void draw(Canvas canvas) {
            drawCircle(canvas, cx, cy, radius, inUse);
            drawLabel(canvas, text, cx, cy, radius * .72f, opacidad);
        }
    }

    private class RectButton extends Control {
        final String text;
        final float baseWidth;
        final float baseHeight;
        final int mask;
        final RectF bounds = new RectF();
        RectButton(String id, String text, float x, float y, float width, float height, int mask) {
            super(id, x, y);
            this.text = text; this.baseWidth = width; this.baseHeight = height; this.mask = mask;
            colocar();
        }
        void colocar() {
            float w = baseWidth * escala * .5f, h = baseHeight * escala * .5f;
            bounds.set(cx - w, cy - h, cx + w, cy + h);
        }
        float alcance() { return Math.max(bounds.width(), bounds.height()) * .5f; }
        boolean contains(float x, float y) { return bounds.contains(x, y); }
        int buttons() { return inUse ? mask : 0; }
        void draw(Canvas canvas) {
            float r = Math.min(dp(10), bounds.height() * .5f);
            fill.setColor(conOpacidad(inUse ? PULSADO : RELLENO));
            stroke.setColor(conOpacidad(BORDE));
            canvas.drawRoundRect(bounds, r, r, fill);
            canvas.drawRoundRect(bounds, r, r, stroke);
            drawLabel(canvas, text, bounds.centerX(), bounds.centerY(), dp(11) * escala, opacidad);
        }
    }

    private final class Trigger extends RectButton {
        Trigger(String id, String text, float x, float y, float width, float height) {
            super(id, text, x, y, width, height, 0);
        }
    }

    private final class Stick extends Control {
        final String text;
        final float baseRadius;
        float radius;
        float xValue;
        float yValue;
        Stick(String id, String text, float x, float y, float radius) {
            super(id, x, y);
            this.text = text; this.baseRadius = radius;
            colocar();
        }
        void colocar() { radius = baseRadius * escala; }
        float alcance() { return radius; }
        boolean contains(float x, float y) {
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy <= radius * radius * 1.55f;
        }
        void move(float x, float y) {
            float dx = (x - cx) / radius;
            float dy = (y - cy) / radius;
            float length = (float) Math.sqrt(dx * dx + dy * dy);
            if (length > 1) { dx /= length; dy /= length; }
            float deadzone = .08f;
            // Y como SDL: positivo hacia ABAJO. El SDK le da la vuelta al pasarlo
            // al mando de la Xbox (thumb_ly = ~valor en sdl_input_driver.cpp),
            // igual que con un mando fisico. Con -dy los dos sticks salian
            // invertidos en vertical.
            xValue = Math.abs(dx) < deadzone ? 0 : dx;
            yValue = Math.abs(dy) < deadzone ? 0 : dy;
        }
        void release() { super.release(); xValue = 0; yValue = 0; }
        void draw(Canvas canvas) {
            drawCircle(canvas, cx, cy, radius, false);
            float knobX = cx + xValue * radius * .58f;
            float knobY = cy + yValue * radius * .58f;
            drawCircle(canvas, knobX, knobY, radius * .43f, inUse);
            drawLabel(canvas, text, knobX, knobY, radius * .34f, opacidad);
        }
    }

    private final class Dpad extends Control {
        final float baseRadius;
        float radius;
        // La forma no cambia al jugar, solo el color: se calcula en colocar()
        // en vez de crear dos RectF nuevos en cada dibujado.
        final RectF vertical = new RectF();
        final RectF horizontal = new RectF();
        int mask;
        Dpad(String id, float x, float y, float radius) {
            super(id, x, y);
            this.baseRadius = radius;
            colocar();
        }
        void colocar() {
            radius = baseRadius * escala;
            float arm = radius * .36f;
            vertical.set(cx - arm, cy - radius, cx + arm, cy + radius);
            horizontal.set(cx - radius, cy - arm, cx + radius, cy + arm);
        }
        float alcance() { return radius; }
        boolean contains(float x, float y) {
            return Math.abs(x - cx) <= radius && Math.abs(y - cy) <= radius;
        }
        void move(float x, float y) {
            float dx = x - cx, dy = y - cy;
            float threshold = radius * .20f;
            mask = 0;
            if (dx < -threshold) mask |= DPAD_LEFT;
            if (dx > threshold) mask |= DPAD_RIGHT;
            if (dy < -threshold) mask |= DPAD_UP;
            if (dy > threshold) mask |= DPAD_DOWN;
        }
        void release() { super.release(); mask = 0; }
        int buttons() { return inUse ? mask : 0; }
        void draw(Canvas canvas) {
            fill.setColor(conOpacidad(inUse ? PULSADO_CRUCETA : RELLENO));
            stroke.setColor(conOpacidad(BORDE));
            float r = dp(6);
            canvas.drawRoundRect(vertical, r, r, fill);
            canvas.drawRoundRect(horizontal, r, r, fill);
            canvas.drawRoundRect(vertical, r, r, stroke);
            canvas.drawRoundRect(horizontal, r, r, stroke);
        }
    }

    private void drawCircle(Canvas canvas, float x, float y, float radius, boolean pressed) {
        fill.setColor(conOpacidad(pressed ? PULSADO : RELLENO));
        stroke.setColor(conOpacidad(BORDE));
        canvas.drawCircle(x, y, radius, fill);
        canvas.drawCircle(x, y, radius, stroke);
    }

    /** El color con su transparencia multiplicada por la opacidad del editor. */
    private int conOpacidad(int color) {
        return (color & 0x00FFFFFF) | (Math.round(Color.alpha(color) * opacidad) << 24);
    }

    private void drawLabel(Canvas canvas, String text, float x, float y, float size, float alfa) {
        label.setTextSize(size);
        label.setAlpha(Math.round(255 * alfa));
        label.getFontMetrics(metricas);
        canvas.drawText(text, x, y - (metricas.ascent + metricas.descent) / 2, label);
    }

    private float dp(float value) {
        return value * density;
    }
}

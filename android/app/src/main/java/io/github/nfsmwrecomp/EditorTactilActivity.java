package io.github.nfsmwrecomp;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * Editor del mando tactil, desde la pantalla de inicio: TouchControllerView
 * en modo editor. LISTO guarda y vuelve; CANCELAR (o atras) vuelve sin guardar.
 *
 * Apaisado, a pantalla completa y por debajo del recorte de la camara, como
 * GameActivity (SDLActivity), para que la vista mida lo mismo que en la
 * partida y los controles caigan en el mismo sitio.
 */
public class EditorTactilActivity extends Activity {

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(Idioma.envolver(base));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        pantallaCompleta();

        FrameLayout raiz = new FrameLayout(this);
        raiz.setBackgroundColor(Color.BLACK);
        ViewGroup.LayoutParams todo = new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        raiz.addView(new MarcoJuego(this, new Ajustes(this).estirar()), todo);
        raiz.addView(new TouchControllerView(this, this::finish), todo);
        setContentView(raiz);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Al volver de un dialogo del sistema las barras reaparecen.
        if (hasFocus) pantallaCompleta();
    }

    /** Lo mismo que hace SDLActivity en la partida. */
    @SuppressWarnings("deprecation")  // setSystemUiVisibility: lo que hay en la API 29
    private void pantallaCompleta() {
        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        w.getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    /**
     * Donde ira la imagen del juego, para colocar los controles con ella en
     * mente: el 16:9 mas grande que cabe, o la pantalla entera si se estira
     * (como GameActivity.ajustarSuperficie).
     */
    private static final class MarcoJuego extends View {
        private final boolean estirar;
        private final Paint relleno = new Paint();
        private final Paint borde = new Paint(Paint.ANTI_ALIAS_FLAG);

        MarcoJuego(Context ctx, boolean estirar) {
            super(ctx);
            this.estirar = estirar;
            relleno.setColor(0xFF1C1D14);
            borde.setStyle(Paint.Style.STROKE);
            borde.setStrokeWidth(2 * ctx.getResources().getDisplayMetrics().density);
            borde.setColor(0x806F7432);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float w = getWidth();
            float h = getHeight();
            if (!estirar) {
                if (w * 9 > h * 16) {
                    w = h * 16 / 9;
                } else {
                    h = w * 9 / 16;
                }
            }
            float x = (getWidth() - w) * .5f;
            float y = (getHeight() - h) * .5f;
            canvas.drawRect(x, y, x + w, y + h, relleno);
            canvas.drawRect(x, y, x + w, y + h, borde);
        }
    }
}

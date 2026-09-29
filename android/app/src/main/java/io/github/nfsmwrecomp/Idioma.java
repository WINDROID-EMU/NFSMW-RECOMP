package io.github.nfsmwrecomp;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;

import java.util.Locale;

/**
 * El idioma de la app: el del telefono, o uno forzado desde la pantalla de
 * inicio (ingles, espanol o portugues; los textos estan en res/values*).
 *
 * Cada actividad lo aplica en attachBaseContext. Ahi todavia no hay Intent,
 * asi que GameActivity, que vive en otro proceso, lo lee de las preferencias
 * igual que la pantalla de inicio.
 */
final class Idioma {

    /** "" = el del telefono. */
    static final String[] CODIGOS = {"", "en", "es", "pt"};

    private static final String CLAVE = "idioma";

    private Idioma() {}

    static String elegido(Context ctx) {
        return ctx.getSharedPreferences(Ajustes.PREFS, Context.MODE_PRIVATE).getString(CLAVE, "");
    }

    /**
     * Con commit() y no apply(): la partida arranca en otro proceso y lee el
     * fichero de preferencias al empezar; tiene que estar ya en disco.
     */
    static void elegir(Context ctx, String codigo) {
        ctx.getSharedPreferences(Ajustes.PREFS, Context.MODE_PRIVATE)
                .edit().putString(CLAVE, codigo).commit();
    }

    static Context envolver(Context base) {
        String codigo = elegido(base);
        if (codigo.isEmpty()) {
            // Por si antes se forzo otro en este mismo proceso.
            Locale.setDefault(Resources.getSystem().getConfiguration().getLocales().get(0));
            return base;
        }
        Locale locale = Locale.forLanguageTag(codigo);
        Locale.setDefault(locale);
        Configuration conf = new Configuration(base.getResources().getConfiguration());
        conf.setLocale(locale);
        return base.createConfigurationContext(conf);
    }
}

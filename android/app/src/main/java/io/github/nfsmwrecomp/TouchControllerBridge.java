package io.github.nfsmwrecomp;

final class TouchControllerBridge {
    private static volatile boolean nativeAvailable = true;
    // Solo el motor nativo sabe en que parte del juego se esta (nativo_android.cpp).
    private static volatile boolean contextoDisponible = true;

    /** Lo que devuelve contexto(): en que parte del juego se esta. */
    static final int CONTEXTO_DESCONOCIDO = -1;
    static final int CONTEXTO_MENUS = 0;
    static final int CONTEXTO_CONDUCIENDO = 1;
    static final int CONTEXTO_ACELERACION = 2;

    private TouchControllerBridge() {}

    static native void setState(int buttons,
                                float leftX, float leftY,
                                float rightX, float rightY,
                                float leftTrigger, float rightTrigger);

    static native int contexto();

    /** Diagnostico: lo que se leyo para decidir el contexto, en una linea. */
    static native String contextoDiagnostico();

    /** El juego quieto (o en marcha otra vez) mientras se edita el mando en la partida. */
    static native void pausarJuego(boolean pausa);

    /** Con el motor de Xenos no esta: el juego sigue mientras se edita. */
    static void tryPausarJuego(boolean pausa) {
        if (!nativeAvailable) return;
        try {
            pausarJuego(pausa);
        } catch (UnsatisfiedLinkError error) {
            // Nada que pausar.
        }
    }

    static void trySetState(int buttons,
                            float leftX, float leftY,
                            float rightX, float rightY,
                            float leftTrigger, float rightTrigger) {
        if (!nativeAvailable) return;
        try {
            setState(buttons, leftX, leftY, rightX, rightY,
                     leftTrigger, rightTrigger);
        } catch (UnsatisfiedLinkError error) {
            nativeAvailable = false;
        }
    }

    /** null si la libreria no lo sabe. */
    static String tryContextoDiagnostico() {
        if (!nativeAvailable || !contextoDisponible) return null;
        try {
            return contextoDiagnostico();
        } catch (UnsatisfiedLinkError error) {
            return null;
        }
    }

    /** CONTEXTO_DESCONOCIDO si la libreria no lo sabe (el motor de Xenos). */
    static int tryContexto() {
        if (!nativeAvailable || !contextoDisponible) return CONTEXTO_DESCONOCIDO;
        try {
            return contexto();
        } catch (UnsatisfiedLinkError error) {
            contextoDisponible = false;
            return CONTEXTO_DESCONOCIDO;
        }
    }
}

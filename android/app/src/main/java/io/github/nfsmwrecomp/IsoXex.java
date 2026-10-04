package io.github.nfsmwrecomp;

import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Lo que se puede saber de una ISO de Xbox 360 antes de jugar: de que edicion
 * del juego es y si esta entera.
 *
 * La edicion. Cada una (PAL Espana, USA...) lleva un default.xex distinto, y
 * el APK lleva dentro el codigo traducido de UNO solo. Con la ISO de otra
 * edicion el juego no arranca, sin mas explicacion que una pantalla en negro.
 * El SHA-256 del default.xex dice cual es.
 *
 * Si esta entera. Una descarga o una extraccion cortada deja una ISO que
 * empieza bien -el ejecutable esta al principio- y a la que le falta el final:
 * los datos del juego. Se ve comparando el tamano del fichero con donde acaba
 * el ultimo archivo de su indice.
 *
 * Lee el XDVDFS igual que tools/fase1_extraer.py: el descriptor de volumen en
 * el sector 32 de la particion de juego y el arbol binario de cada directorio.
 */
final class IsoXex {

    /** Lo que se ha leido de una ISO. */
    static final class Info {
        /** SHA-256 del default.xex, o "" si no se pudo leer. */
        final String xex;
        /** Bytes que le faltan al fichero para contener todos sus archivos; 0 si esta entero. */
        final long faltan;

        Info(String xex, long faltan) {
            this.xex = xex;
            this.faltan = faltan;
        }
    }

    private static final int SECTOR = 2048;
    private static final int DIRECTORIO = 0x10;
    private static final byte[] MAGIA = "MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);
    /** Donde empieza la particion de juego: imagen recortada, XGD2, XGD3 y XGD1. */
    private static final long[] BASES = {0L, 0x0FD90000L, 0x02080000L, 0x18300000L};
    private static final int MAX_DIRECTORIO = 1 << 20;
    private static final int MAX_XEX = 64 << 20;
    private static final int MAX_PROFUNDIDAD = 8;

    private IsoXex() {
    }

    /** null si no es una ISO de Xbox 360 o no se puede abrir. */
    static Info mirar(Context ctx, Uri iso) {
        try (ParcelFileDescriptor pfd = ctx.getContentResolver().openFileDescriptor(iso, "r")) {
            if (pfd == null) {
                return null;
            }
            try (FileInputStream in = new FileInputStream(pfd.getFileDescriptor())) {
                return mirar(in.getChannel());
            }
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    private static Info mirar(FileChannel canal) throws IOException {
        for (long base : BASES) {
            ByteBuffer vd = leer(canal, base + 32L * SECTOR, 28);
            if (vd == null || !empiezaPor(vd, MAGIA)) {
                continue;
            }
            long sectorRaiz = vd.getInt(20) & 0xFFFFFFFFL;
            int tamRaiz = vd.getInt(24);
            Recorrido r = new Recorrido(canal, base);
            r.directorio(sectorRaiz, tamRaiz, 0);

            String sha = "";
            if (r.tamXex > 0 && r.tamXex <= MAX_XEX) {
                ByteBuffer datos = leer(canal, base + r.sectorXex * SECTOR, (int) r.tamXex);
                if (datos != null) {
                    sha = hex(resumen(datos));
                }
            }
            // Un proveedor que no sepa el tamano da 0: entonces no se dice nada.
            long tam = canal.size();
            return new Info(sha, tam > 0 && r.fin > tam ? r.fin - tam : 0);
        }
        return null;
    }

    /** Recorre el arbol de directorios apuntando el default.xex de la raiz y donde acaba el ultimo archivo. */
    private static final class Recorrido {
        private final FileChannel canal;
        private final long base;
        long sectorXex;
        long tamXex;
        long fin;

        Recorrido(FileChannel canal, long base) {
            this.canal = canal;
            this.base = base;
        }

        /**
         * Cada entrada lleva los desplazamientos (en palabras de 4 bytes) de
         * sus dos hijas en el arbol binario, el sector, el tamano, los
         * atributos y el nombre.
         */
        void directorio(long sector, long tam, int profundidad) throws IOException {
            fin = Math.max(fin, base + sector * SECTOR + tam);
            if (tam <= 0 || tam > MAX_DIRECTORIO || profundidad > MAX_PROFUNDIDAD) {
                return;
            }
            ByteBuffer tabla = leer(canal, base + sector * SECTOR, (int) tam);
            if (tabla == null) {
                return;   // el directorio cae en lo que falta: ya cuenta en `fin`
            }
            int[] pila = new int[256];
            int n = 0;
            pila[n++] = 0;
            int vueltas = 0;
            while (n > 0 && vueltas++ < 8192) {
                int off = pila[--n];
                if (off + 14 > tabla.limit()) {
                    continue;
                }
                int izq = tabla.getShort(off) & 0xFFFF;
                int der = tabla.getShort(off + 2) & 0xFFFF;
                long sectorHijo = tabla.getInt(off + 4) & 0xFFFFFFFFL;
                long tamHijo = tabla.getInt(off + 8) & 0xFFFFFFFFL;
                int atributos = tabla.get(off + 12) & 0xFF;
                int largo = tabla.get(off + 13) & 0xFF;
                if (izq != 0 && izq != 0xFFFF && n < pila.length) {
                    pila[n++] = izq * 4;
                }
                if (der != 0 && der != 0xFFFF && n < pila.length) {
                    pila[n++] = der * 4;
                }
                if (largo == 0 || off + 14 + largo > tabla.limit()) {
                    continue;
                }
                if ((atributos & DIRECTORIO) != 0) {
                    directorio(sectorHijo, tamHijo, profundidad + 1);
                    continue;
                }
                fin = Math.max(fin, base + sectorHijo * SECTOR + tamHijo);
                if (profundidad == 0 && esNombre(tabla, off + 14, largo, "default.xex")) {
                    sectorXex = sectorHijo;
                    tamXex = tamHijo;
                }
            }
        }
    }

    private static boolean esNombre(ByteBuffer tabla, int desde, int largo, String nombre) {
        if (largo != nombre.length()) {
            return false;
        }
        for (int i = 0; i < largo; i++) {
            if (Character.toLowerCase((char) (tabla.get(desde + i) & 0xFF)) != nombre.charAt(i)) {
                return false;
            }
        }
        return true;
    }

    private static ByteBuffer leer(FileChannel canal, long desde, int cuantos) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(cuantos).order(ByteOrder.LITTLE_ENDIAN);
        long pos = desde;
        while (b.hasRemaining()) {
            int leidos = canal.read(b, pos);
            if (leidos <= 0) {
                return null;
            }
            pos += leidos;
        }
        b.flip();
        return b;
    }

    private static boolean empiezaPor(ByteBuffer b, byte[] prefijo) {
        for (int i = 0; i < prefijo.length; i++) {
            if (b.get(i) != prefijo[i]) {
                return false;
            }
        }
        return true;
    }

    private static byte[] resumen(ByteBuffer datos) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            md.update(datos);
            return md.digest();
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder(b.length * 2);
        for (byte x : b) {
            s.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
        }
        return s.toString();
    }
}

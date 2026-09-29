// =============================================================================
//  NFS Most Wanted - Recompilacion : Lanzador
//
//  Ventana nativa de Windows, en C# con WinForms. Sustituye a lanzador.ps1 y
//  hace exactamente lo mismo, con la portada del juego al lado en plan
//  instalador.
//
//  Se compila con CONSTRUIR_LANZADOR.bat, que usa el csc.exe del .NET
//  Framework que YA VIENE con Windows. No hay que instalar Visual Studio, ni
//  el SDK de .NET, ni nada: el compilador esta en
//  C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe desde Windows 8.
//
//  POR QUE C# Y NO OTRA COSA
//  =========================
//  Hacia falta un .exe de verdad, con su icono, y que no dependa de instalar
//  nada. Las opciones eran:
//
//    - C++ con Win32 a pelo: sale un exe pequeno, pero montar a mano una
//      ventana con veinte controles es muchisimo codigo para lo que es.
//    - Python empaquetado: hay que instalar Python y PyInstaller, y el exe
//      acaba pesando 30 MB.
//    - C# con el compilador que ya trae Windows: un solo fichero, los mismos
//      controles que ya usaba el lanzador de PowerShell -WinForms es lo que
//      habia debajo-, icono y portada dentro del exe, y cero instalaciones.
//
//  ESTO SE COMPILA CON UN csc VIEJO
//  ================================
//  El que trae Windows es de C# 5 (2012). Asi que aqui NO se puede usar nada
//  moderno: ni cadenas interpoladas $"...", ni ?., ni nameof, ni miembros con
//  =>. Todo con string.Format y sintaxis clasica. Si algo de eso se cuela, el
//  error que sale no dice "necesitas un compilador mas nuevo", dice cosas
//  raras sobre ';' que faltan, y se pierde media tarde.
//
//  LOS AJUSTES SE COMPARTEN CON EL LANZADOR VIEJO
//  =============================================
//  Se lee y se escribe el MISMO lanzador.json, con los mismos nombres de
//  campo. Asi que la configuracion que ya tuvieras se conserva, y los dos
//  lanzadores conviven sin pisarse.
// =============================================================================

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace NfsmwRecomp
{
    // -------------------------------------------------------------------------
    //  Un json plano, a mano
    //
    //  El fichero de ajustes es una decena de parejas clave/valor sin anidar.
    //  Para eso no hace falta traerse Newtonsoft (que habria que descargar) ni
    //  JavaScriptSerializer (que obliga a referenciar System.Web.Extensions).
    //  Lo unico con lo que hay que tener cuidado es con las barras invertidas de
    //  las rutas de Windows, que en json van dobladas.
    // -------------------------------------------------------------------------
    internal static class Json
    {
        public static Dictionary<string, string> Leer(string texto)
        {
            Dictionary<string, string> d = new Dictionary<string, string>();
            if (texto == null)
                return d;

            int i = 0;
            while (i < texto.Length)
            {
                // Buscar la comilla que abre una clave.
                while (i < texto.Length && texto[i] != '"')
                    i++;
                if (i >= texto.Length)
                    break;

                string clave = LeerCadena(texto, ref i);

                // Saltar hasta los dos puntos.
                while (i < texto.Length && texto[i] != ':')
                    i++;
                if (i >= texto.Length)
                    break;
                i++;

                while (i < texto.Length && char.IsWhiteSpace(texto[i]))
                    i++;
                if (i >= texto.Length)
                    break;

                string valor;
                if (texto[i] == '"')
                {
                    valor = LeerCadena(texto, ref i);
                }
                else
                {
                    int desde = i;
                    while (i < texto.Length && texto[i] != ',' && texto[i] != '}' &&
                           texto[i] != '\r' && texto[i] != '\n')
                        i++;
                    valor = texto.Substring(desde, i - desde).Trim();
                }

                if (clave.Length > 0)
                    d[clave] = valor;
            }
            return d;
        }

        // Entra apuntando a la comilla de apertura, sale despues de la de cierre.
        private static string LeerCadena(string texto, ref int i)
        {
            StringBuilder sb = new StringBuilder();
            i++;  // la comilla de apertura
            while (i < texto.Length && texto[i] != '"')
            {
                if (texto[i] == '\\' && i + 1 < texto.Length)
                {
                    i++;
                    char c = texto[i];
                    if (c == 'n') sb.Append('\n');
                    else if (c == 'r') sb.Append('\r');
                    else if (c == 't') sb.Append('\t');
                    else if (c == 'u' && i + 4 < texto.Length)
                    {
                        int cod;
                        if (int.TryParse(texto.Substring(i + 1, 4), NumberStyles.HexNumber,
                                         CultureInfo.InvariantCulture, out cod))
                        {
                            sb.Append((char)cod);
                            i += 4;
                        }
                    }
                    else sb.Append(c);   // \\ y \/ y \" caen aqui
                }
                else
                {
                    sb.Append(texto[i]);
                }
                i++;
            }
            i++;  // la comilla de cierre
            return sb.ToString();
        }

        public static string Escapar(string s)
        {
            StringBuilder sb = new StringBuilder();
            foreach (char c in s)
            {
                if (c == '"' || c == '\\') { sb.Append('\\'); sb.Append(c); }
                else if (c == '\n') sb.Append("\\n");
                else if (c == '\r') sb.Append("\\r");
                else if (c == '\t') sb.Append("\\t");
                else if (c < ' ') sb.Append("\\u" + ((int)c).ToString("x4"));
                else sb.Append(c);
            }
            return sb.ToString();
        }
    }

    // -------------------------------------------------------------------------
    //  El panel de la portada
    //
    //  Se pinta a mano en vez de usar un PictureBox porque hace falta control
    //  sobre COMO encaja la imagen. La portada es 760x1064 -relacion 0,71- y el
    //  panel es mucho mas estrecho y alto que eso.
    //
    //  Si se estirase para llenar el panel, habria que recortar por los lados y
    //  se comeria parte del titulo, que ocupa todo el ancho arriba. Asi que se
    //  mete ENTERA, pegada arriba, y el hueco de abajo se aprovecha para poner
    //  el nombre del proyecto. Que es justo la pinta que tiene la banda lateral
    //  de un instalador.
    // -------------------------------------------------------------------------
    // -------------------------------------------------------------------------
    //  Tema oscuro, estilo terminal verde fosforo
    //
    //  Colores centralizados aqui para no repetir el mismo Color.FromArgb en
    //  veinte sitios. AplicarTema() (mas abajo, en Ventana) los reparte solo
    //  recorriendo el arbol de controles, asi que anadir un control nuevo no
    //  obliga a acordarse de colorearlo a mano.
    // -------------------------------------------------------------------------
    internal static class Tema
    {
        public static readonly Color Fondo = Color.FromArgb(8, 13, 8);
        public static readonly Color FondoPanel = Color.FromArgb(14, 22, 14);
        public static readonly Color FondoCampo = Color.FromArgb(4, 8, 4);
        public static readonly Color Borde = Color.FromArgb(58, 105, 58);
        public static readonly Color BordeSuave = Color.FromArgb(34, 60, 34);
        public static readonly Color Texto = Color.FromArgb(160, 225, 160);
        public static readonly Color TextoTitulo = Color.FromArgb(200, 255, 200);
        public static readonly Color TextoNota = Color.FromArgb(95, 145, 95);
        public static readonly Color Aviso = Color.FromArgb(255, 150, 90);
        public static readonly Color Acento = Color.FromArgb(255, 210, 90);
        public static readonly Font Mono = new Font("Consolas", 9f);
    }

    // -------------------------------------------------------------------------
    //  Sustituto de GroupBox para el tema oscuro
    //
    //  GroupBox nativo pinta su borde con el tema visual de Windows (UxTheme),
    //  que da por hecho un fondo claro: sobre un panel oscuro deja un halo o
    //  una linea que no coincide con nada. Se dibuja el borde y el titulo a
    //  mano -mismo truco que ya usaba PanelPortada para la portada- en vez de
    //  pelear con el estilo nativo.
    // -------------------------------------------------------------------------
    internal sealed class PanelSeccion : Panel
    {
        public string Titulo;

        public PanelSeccion(string titulo)
        {
            Titulo = titulo;
            BackColor = Tema.FondoPanel;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            Graphics g = e.Graphics;

            using (SolidBrush fondo = new SolidBrush(Tema.FondoPanel))
                g.FillRectangle(fondo, ClientRectangle);

            using (Font f = new Font("Segoe UI", 8.5f, FontStyle.Bold))
            {
                SizeF medida = string.IsNullOrEmpty(Titulo) ? SizeF.Empty : g.MeasureString(Titulo, f);

                const int y = 7;
                const int huecoInicio = 10;
                int huecoFin = medida.Width > 0 ? (int)(huecoInicio + medida.Width + 8) : huecoInicio;

                // El borde deja un hueco donde va el titulo, como un GroupBox
                // de toda la vida: dos tramos de linea con un espacio en medio
                // en vez de un rectangulo entero.
                using (Pen borde = new Pen(Tema.Borde))
                {
                    g.DrawLine(borde, 0, y, huecoInicio, y);
                    if (huecoFin < Width)
                        g.DrawLine(borde, huecoFin, y, Width - 1, y);
                    g.DrawLine(borde, 0, y, 0, Height - 1);
                    g.DrawLine(borde, Width - 1, y, Width - 1, Height - 1);
                    g.DrawLine(borde, 0, Height - 1, Width - 1, Height - 1);
                }

                if (!string.IsNullOrEmpty(Titulo))
                {
                    using (SolidBrush textoBrush = new SolidBrush(Tema.TextoTitulo))
                        g.DrawString(Titulo, f, textoBrush, huecoInicio + 4, y - medida.Height / 2f);
                }
            }
        }
    }

    internal sealed class PanelPortada : Panel
    {
        private readonly Image portada;

        public PanelPortada(Image portada)
        {
            this.portada = portada;
            BackColor = Color.Black;
            // Sin esto la imagen parpadea al redimensionar y al arrastrar la
            // ventana por encima de otras.
            //
            // ResizeRedraw ES EL QUE IMPORTA AQUI. Sin el, cuando el panel
            // crece -maximizar la ventana, por ejemplo- Windows solo invalida
            // la FRANJA nueva que queda expuesta, no el panel entero: el
            // tramo de arriba se queda con el pixel antiguo, pintado para el
            // alto viejo, y la franja nueva de abajo se pinta aparte con
            // OnPaint recalculando la imagen para el alto NUEVO -distinta
            // escala de "cover"-. El resultado son dos recortes de la misma
            // portada, uno encima del otro, cada uno con su propio texto
            // "Recompilacion nativa": exactamente el "banner duplicado" que
            // se veia al agrandar la ventana. Con ResizeRedraw, cualquier
            // cambio de tamano invalida el panel COMPLETO y OnPaint se repite
            // entero con el alto nuevo, sin restos del anterior.
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            e.Graphics.Clear(Color.Black);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;

            if (portada != null)
            {
                // "Cover", no "fit": se escala por el eje que haga falta para
                // cubrir el panel ENTERO -ancho y alto a la vez-, recortando
                // lo que sobre por el otro eje. Antes se escalaba solo por
                // ancho y se paraba si la imagen no llegaba a la altura del
                // panel: en una columna mas alta que ancha -que es esta-, eso
                // dejaba un tramo negro vacio debajo de la foto. Cubriendo
                // entero no queda ningun hueco, en ninguna proporcion de
                // ventana.
                double escala = Math.Max((double)Width / portada.Width, (double)Height / portada.Height);
                int ancho = (int)Math.Ceiling(portada.Width * escala);
                int alto = (int)Math.Ceiling(portada.Height * escala);
                g.DrawImage(portada, (Width - ancho) / 2, (Height - alto) / 2, ancho, alto);
            }
            else
            {
                // Sin portada.jpg -la caratula es arte de EA y por eso no va en
                // el repositorio, ver CONSTRUIR_LANZADOR.bat- el panel se
                // quedaba en negro liso con dos lineas de texto pegadas abajo:
                // se leia como un hueco vacio, no como una banda lateral.
                //
                // Esto rellena ese hueco con algo propio: un patron de barras
                // -estilo ecualizador/lectura de datos- que no reproduce nada
                // del juego, solo decora en el mismo verde del resto de la
                // ventana. Semilla fija para que no parpadee ni cambie entre
                // repintados.
                DibujarPatronDeRelleno(g, Math.Max(0, Height - 110));
            }

            // Degradado permanente en la franja de abajo, PASE LO QUE PASE con
            // la imagen -cubra entero o no-: es lo que deja el texto legible
            // encima de cualquier foto, no solo tapa una costura.
            {
                int difuminado = Math.Min(110, Height);
                Rectangle r = new Rectangle(0, Height - difuminado, Width, difuminado);
                if (r.Height > 0)
                {
                    using (LinearGradientBrush b = new LinearGradientBrush(
                               r, Color.FromArgb(0, 0, 0, 0), Color.FromArgb(230, 0, 0, 0), 90f))
                        g.FillRectangle(b, r);
                }
            }

            using (Font f1 = new Font("Segoe UI", 12f, FontStyle.Bold))
            using (Font f2 = new Font("Segoe UI", 8.25f))
            using (SolidBrush brillante = new SolidBrush(Tema.TextoTitulo))
            using (SolidBrush apagado = new SolidBrush(Tema.TextoNota))
            {
                int y = Height - 96;
                g.DrawString("Recompilacion nativa", f1, brillante, 18, y);
                g.DrawString("Xbox 360 traducida a PC con ReXGlue.\n" +
                             "Necesita tu propia copia del juego.",
                             f2, apagado, new RectangleF(18, y + 26, Width - 36, 60));
            }
        }

        // Barras verticales de alto pseudoaleatorio -estilo ecualizador-,
        // dentro de un rectangulo de altoDisponible px desde arriba. Random
        // con semilla fija: mismo dibujo siempre, no cambia entre repintados
        // ni parpadea al redimensionar.
        private void DibujarPatronDeRelleno(Graphics g, int altoDisponible)
        {
            if (altoDisponible <= 0 || Width <= 0)
                return;

            Random azar = new Random(454107); // el title id de NFSMW, por ponerle algo fijo
            const int anchoBarra = 5;
            const int hueco = 3;
            int paso = anchoBarra + hueco;

            using (SolidBrush apagada = new SolidBrush(Color.FromArgb(28, Tema.Texto)))
            using (SolidBrush media = new SolidBrush(Color.FromArgb(55, Tema.Texto)))
            {
                for (int x = paso; x < Width - paso; x += paso)
                {
                    double t = (double)x / Width;
                    // Dos "colinas" suaves para que no sea puro ruido plano:
                    // el patron sube hacia el centro y vuelve a bajar.
                    double envolvente = 0.35 + 0.65 * Math.Sin(t * Math.PI);
                    int alturaBase = (int)(altoDisponible * envolvente * (0.25 + azar.NextDouble() * 0.55));
                    if (alturaBase < 4)
                        continue;

                    bool destacada = azar.NextDouble() > 0.82;
                    Brush brocha = destacada ? media : apagada;
                    int y0 = altoDisponible - alturaBase;
                    g.FillRectangle(brocha, x, y0, anchoBarra, alturaBase);
                }
            }
        }
    }

    internal sealed class Ventana : Form
    {
        // ---- Presets, sacados de TryParseResolutionPreset del SDK -------------
        private static readonly string[,] Presets = {
            { "480p  - 640 x 480",   "480p"   },
            { "540p  - 960 x 540",   "540p"   },
            { "720p  - 1280 x 720",  "720p"   },
            { "900p  - 1600 x 900",  "900p"   },
            { "1080p - 1920 x 1080", "1080p"  },
            { "1440p - 2560 x 1440", "1440p"  },
            { "1800p - 3200 x 1800", "1800p"  },
            { "2160p - 3840 x 2160", "2160p"  },
            { "Personalizada",       "custom" },
        };

        // Los textos llevan el "x" delante porque es el numero que la gente
        // busca: es el mismo mando que el "resolucion interna x2" de cualquier
        // emulador. Se guardan tal cual en lanzador.json, asi que cambiarlos
        // rompe la compatibilidad con lo guardado; por eso CargarAjustes cae a
        // la primera opcion cuando no reconoce el texto, en vez de fallar.
        private static readonly string[,] Escalas = {
            { "x1  - la original de Xbox 360", "1" },
            { "x2  - 4 veces los pixeles",     "2" },
            { "x3  - 9 veces los pixeles",     "3" },
            { "x4  - 16 veces los pixeles",    "4" },
        };

        // Antialiasing por postproceso (--swap_post_effect). El valor es el que
        // espera el cvar del recomp: none / fxaa / fxaa_extreme. Se aplica al
        // reiniciar el juego, igual que la resolucion.
        private static readonly string[,] Antialias = {
            { "Desactivado",  "none" },
            { "FXAA",         "fxaa" },
            { "FXAA Extreme", "fxaa_extreme" },
        };

        // Filtrado anisotropico (--anisotropic_override). El recomp fuerza el
        // filtrado de texturas aunque el juego no lo pida; 0 lo apaga.
        private static readonly string[,] Anisotropico = {
            { "Desactivado (bilinear)", "0" },
            { "1x",                     "1" },
            { "2x",                     "2" },
            { "4x",                     "3" },
            { "8x",                     "4" },
            { "16x",                    "5" },
        };

        // Efecto al pasar la imagen final a la ventana (--present_effect). Son
        // los que trae el SDK de ReXGlue (FidelityFX); si este runtime no los
        // tuviera, el cvar rechaza el valor y se queda en bilinear, sin romper.
        private static readonly string[,] Efectos = {
            { "Ninguno (bilinear)", "bilinear" },
            { "CAS (nitidez)",      "cas" },
            { "FSR (FidelityFX)",   "fsr" },
        };

        // ---- Donde estamos ----------------------------------------------------
        private string raiz;
        private string exeJuego;
        private string dirLogs;
        private string ficheroAjustes;
        private string fuentePresentador;
        private string logEjecucion;
        private bool distribuida;

        // ---- Controles --------------------------------------------------------
        private Panel panelContenido;
        private Panel panelColumna;
        private PanelPortada banda;
        private TextBox txtIso;
        private ComboBox cboRes, cboEsc, cboAA, cboAniso, cboEfecto, cboMon;
        private NumericUpDown numAncho, numAlto, numFps, numNitidez;
        private RadioButton rbCompleta, rbVentana;
        private CheckBox chkVsync, chkLimite;
        private RadioButton rbVidAuto, rbVidRtv, rbVidRov;
        private RadioButton rbApiDx, rbApiVk;
        private Label lblParche, lblEstado, lblEscala;
        private TextBox txtCmd;
        private Button btnJugar, btnSalir;
        private bool cargando = true;

        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();

        [STAThread]
        public static void Main()
        {
            // Sin esto, en un monitor con escala de Windows (125%, 150%...)
            // el sistema NO reescala la ventana de verdad: la dibuja a tamano
            // normal y luego estira el bitmap resultante, y todo sale borroso
            // -texto incluido-. Con el proceso marcado DPI-aware, Windows deja
            // de estirar, y AutoScaleMode.Dpi en Ventana (ver Construir) hace
            // que las coordenadas en pixeles de este fichero -pensadas para
            // 96 DPI- se reescalen de verdad, no solo se vean nitidas.
            //
            // SetProcessDPIAware (System-DPI-aware) y no el modo Per-Monitor
            // V2 mas nuevo: ese ultimo pide declararlo en un manifiesto
            // embebido, y csc.exe -el compilador viejo de aqui- no tiene forma
            // sencilla de meter uno sin herramientas aparte. Esto cubre el
            // caso real -abrir en el monitor de siempre con su escala de
            // siempre- sin depender de nada mas.
            try
            {
                SetProcessDPIAware();
            }
            catch
            {
                // Windows muy viejo sin esta API: se sigue sin marcar, y el
                // peor caso es el de siempre (bitmap estirado). No es motivo
                // para no abrir.
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Ventana());
        }

        public Ventana()
        {
            LocalizarTodo();
            Construir();
            CargarAjustes();
            cargando = false;
            EstadoInicial();
            Refrescar();
        }

        // ---------------------------------------------------------------------
        //  Donde esta cada cosa
        //
        //  El exe puede vivir en dos sitios y tiene que funcionar en los dos:
        //
        //    reparto   la carpeta portable, junto a nfsmw.exe. Ahi no hay
        //              proyecto ni SDK: solo el juego, y todo -logs, ajustes,
        //              ISO- cuelga de esa misma carpeta.
        //    proyecto  la raiz de "NFSMW Recomp". El juego esta compilado en
        //              app\out\build\..., y al lado hay SDK que mirar.
        //
        //  EL JUEGO SE LLAMA nfsmw.exe, NO NFS_Most_Wanted.exe
        //  ===================================================
        //  En la carpeta portable, "NFS_Most_Wanted.exe" ES ESTE LANZADOR. El
        //  juego de verdad se llama nfsmw.exe, que ademas es como se llama en el
        //  arbol del proyecto, asi que los dos modos buscan el mismo nombre.
        //
        //  El motivo es solo ese: que al hacer doble clic en el icono del juego
        //  salga esta ventana. Es lo mismo que hace cualquier juego con
        //  lanzador, y el intercambio de nombres es la forma de conseguirlo sin
        //  tocar el codigo del juego.
        //
        //  NO es que el juego no sepa arrancar solo: sabe. nfsmw_app.h le pone
        //  gpu_plugin, mnk_mode y readback_resolve si nadie los pidio, y busca
        //  una ISO en su propia carpeta. Abrir nfsmw.exe a pelo sigue
        //  funcionando, y es una salida util si el lanzador diera problemas.
        //
        //  Un matiz de ese buscador de ISO: prefiere la que se llame IGUAL que
        //  el ejecutable, y si no, coge la primera por orden alfabetico. Al
        //  renombrarlo, una NFS_Most_Wanted.iso deja de ser la preferida y pasa
        //  a entrar por la segunda regla. Con una sola ISO en la carpeta da lo
        //  mismo; con varias, podria coger otra. Da igual cuando se abre desde
        //  aqui, porque esta ventana pasa --game_data_root explicito.
        //
        //  Y no cambia donde guarda sus cosas el juego: el SDK saca esa carpeta
        //  de GetName(), que va en el codigo -"nfsmw"-, no del nombre del
        //  fichero. Ver rex_app.cpp:  user_dir = GetUserFolder() / GetName().
        //  Asi que la cache de shaders sigue donde estaba.
        // ---------------------------------------------------------------------
        private void LocalizarTodo()
        {
            string mio = Path.GetDirectoryName(Application.ExecutablePath);

            // Puede estar en la raiz del proyecto o dentro de tools\; se mira
            // tambien un nivel mas arriba antes de darse por vencido.
            string[] candidatos = { mio, Path.GetFullPath(Path.Combine(mio, "..")) };

            foreach (string c in candidatos)
            {
                if (File.Exists(Path.Combine(c, "nfsmw.exe")))
                {
                    distribuida = true;
                    raiz = c;
                    exeJuego = Path.Combine(c, "nfsmw.exe");
                    dirLogs = Path.Combine(c, "logs");
                    ficheroAjustes = Path.Combine(c, "lanzador.json");
                    fuentePresentador = null;
                    logEjecucion = Path.Combine(dirLogs, "lanzador.log");
                    return;
                }
            }

            foreach (string c in candidatos)
            {
                string j = Path.Combine(c, @"app\out\build\win-amd64-release\nfsmw.exe");
                if (File.Exists(j) || Directory.Exists(Path.Combine(c, "app")))
                {
                    distribuida = false;
                    raiz = c;
                    exeJuego = j;
                    dirLogs = Path.Combine(c, "logs");
                    ficheroAjustes = Path.Combine(c, @"config\lanzador.json");
                    fuentePresentador = Path.Combine(
                        Path.GetFullPath(Path.Combine(c, "..")),
                        @"rexglue-sdk\src\ui\d3d12\d3d12_presenter.cpp");
                    logEjecucion = Path.Combine(dirLogs, "lanzador.log");
                    return;
                }
            }

            // Ni una cosa ni la otra. Se asume proyecto y ya avisara al arrancar
            // de que no encuentra el ejecutable; es mejor abrir la ventana con un
            // aviso que no abrir nada.
            distribuida = false;
            raiz = mio;
            exeJuego = Path.Combine(mio, @"app\out\build\win-amd64-release\nfsmw.exe");
            dirLogs = Path.Combine(mio, "logs");
            ficheroAjustes = Path.Combine(mio, @"config\lanzador.json");
            fuentePresentador = null;
            logEjecucion = Path.Combine(dirLogs, "lanzador.log");
        }

        private static Image CargarRecurso(string nombre)
        {
            try
            {
                Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(nombre);
                if (s == null)
                    return null;
                using (s)
                    return Image.FromStream(s);
            }
            catch
            {
                // Sin portada la ventana se ve rara, pero se ve. No es motivo
                // para no dejar jugar.
                return null;
            }
        }

        // ---------------------------------------------------------------------
        //  La ventana
        // ---------------------------------------------------------------------
        private const int AnchoBanda = 380;
        // Dos columnas de ajustes lado a lado en vez de una sola apilada: casi
        // la mitad de alto -menos scroll vertical- y usa de verdad el ancho
        // que sobra en una pantalla normal, no solo lo centra con un hueco al
        // lado. AltoUtil, X1 y AnchoTotalColumnas se recalculan mas abajo a
        // partir de donde queda cada seccion; los numeros de aqui son el
        // resultado final, anotado para no tener que releer todo Construir()
        // cada vez que se toque el orden de las secciones.
        private const int AltoUtil = 810;
        private const int X0 = 20;   // columna izquierda
        private const int AnchoCol = 580;
        private const int GapCol = 24;
        private const int X1 = X0 + AnchoCol + GapCol;   // columna derecha
        private const int AnchoTotalColumnas = AnchoCol * 2 + GapCol;   // ancho de lo que va a todo lo ancho

        private static string[,] Monitores()
        {
            Screen[] pantallas = Screen.AllScreens;
            string[,] m = new string[pantallas.Length + 1, 2];
            m[0, 0] = "Automatico (predeterminado)";
            m[0, 1] = "0";
            for (int i = 0; i < pantallas.Length; i++)
            {
                m[i + 1, 0] = "Monitor " + (i + 1) + " - " + pantallas[i].Bounds.Width + "x" +
                              pantallas[i].Bounds.Height + " (" + pantallas[i].DeviceName + ")";
                m[i + 1, 1] = (i + 1).ToString(CultureInfo.InvariantCulture);
            }
            return m;
        }

        private void Construir()
        {
            Text = "Need for Speed: Most Wanted - Recompilacion";

            // ------------------------------------------------------------------
            //  Tamano de la ventana: se ajusta a la pantalla, y se puede escalar
            //
            //  AltoUtil (1066) es la altura NATURAL de todos los controles, pero
            //  en un portatil de 1366x768 no cabe entera. La ventana se ABRE mas
            //  baja/estrecha cuando hace falta, y el contenido de la derecha
            //  -todo menos la portada- vive en un panel con scroll (mas abajo).
            //
            //  Ademas la ventana es REDIMENSIONABLE (Sizable, con boton de
            //  maximizar): banda y panelContenido llevan Anchor puesto, asi que
            //  al agrandar la ventana la portada se estira a lo alto y el
            //  panel de ajustes gana ancho y alto de verdad -no es solo mas
            //  scroll-, y si se agranda lo bastante el scroll desaparece solo
            //  porque el contenido ya cabe entero.
            // ------------------------------------------------------------------
            int anchoTotal = AnchoBanda + AnchoTotalColumnas + 40;
            Rectangle area = Screen.PrimaryScreen.WorkingArea;
            int altoForm = Math.Min(AltoUtil, Math.Max(420, area.Height - 60));
            int anchoForm = Math.Min(anchoTotal, Math.Max(760, area.Width - 60));

            ClientSize = new Size(anchoForm, altoForm);
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.Sizable;
            MaximizeBox = true;
            MinimumSize = new Size(760, 480);
            BackColor = Tema.Fondo;
            Font = new Font("Segoe UI", 8.25f);

            try
            {
                Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch
            {
                // Da igual: el icono del exe ya lo pone el compilador.
            }

            banda = new PanelPortada(CargarRecurso("portada.jpg"));
            banda.Location = new Point(0, 0);
            banda.Size = new Size(AnchoBanda, altoForm);
            // SIN Anchor. Ancho, alto y posicion los recalcula AjustarLayout
            // en cada resize (mas abajo) -es el UNICO que los toca-, tanto
            // para la banda como para panelContenido. Mezclar Anchor con
            // asignaciones manuales de Size en el mismo control es la receta
            // clasica para que uno pise al otro en momentos distintos del
            // ciclo de layout: eso fue lo que dejaba la banda mas alta que su
            // contenido real y la portada se repetia para rellenar ese sobra.
            Controls.Add(banda);

            // Todo lo demas -los grupos de ajustes, el comando y los botones-
            // vive aqui dentro. AutoScroll le pone barra vertical sola en cuanto
            // el contenido (AltoUtil) no cabe en altoForm: es lo que deja usar
            // el lanzador en pantallas pequenas sin tocar el resto del layout.
            panelContenido = new Panel();
            panelContenido.Location = new Point(AnchoBanda, 0);
            panelContenido.Size = new Size(anchoForm - AnchoBanda, altoForm);
            // Tambien sin Anchor, mismo motivo que la banda.
            panelContenido.AutoScroll = true;
            panelContenido.BackColor = BackColor;
            Controls.Add(panelContenido);

            // panelColumna es el ancho NATURAL del contenido -el mismo de
            // siempre, AnchoCol+40- metido dentro de panelContenido. En una
            // ventana ancha, panelContenido tiene mas sitio del que hace
            // falta; AjustarLayout centra panelColumna en ese sobrante en vez
            // de dejarlo todo pegado a la izquierda con un hueco enorme a la
            // derecha. Los grupos, botones y demas se cuelgan de AQUI, no de
            // panelContenido directamente.
            panelColumna = new Panel();
            panelColumna.Size = new Size(AnchoTotalColumnas + 40, AltoUtil);
            panelColumna.BackColor = BackColor;
            panelContenido.Controls.Add(panelColumna);
            panelContenido.AutoScrollMinSize = new Size(panelColumna.Width, AltoUtil);

            // ---- Copia del juego: ISO o carpeta ya extraida --------------------
            //
            // El SDK (rex_app.cpp) exige que --game_data_root sea una CARPETA:
            // no sabe montar un .iso directamente. Asi que aqui se aceptan las
            // dos cosas y, si se elige un .iso, se extrae una copia a
            // game_root_cache\ la primera vez (ver ExtractorXdvdfs mas abajo);
            // las siguientes veces con la misma ISO arranca directo, sin volver
            // a extraer.
            PanelSeccion gIso = Grupo("Copia del juego (ISO o carpeta extraida)", X0, 14, 110,
                                      AnchoTotalColumnas);

            txtIso = new TextBox();
            txtIso.Location = new Point(14, 26);
            txtIso.Size = new Size(AnchoTotalColumnas - 224, 23);
            txtIso.TextChanged += delegate { Refrescar(); };
            gIso.Controls.Add(txtIso);

            Button btnIso = new Button();
            btnIso.Text = "ISO...";
            btnIso.Location = new Point(AnchoTotalColumnas - 196, 25);
            btnIso.Size = new Size(94, 25);
            btnIso.Click += ElegirIso;
            gIso.Controls.Add(btnIso);

            Button btnCarpeta = new Button();
            btnCarpeta.Text = "Carpeta...";
            btnCarpeta.Location = new Point(AnchoTotalColumnas - 98, 25);
            btnCarpeta.Size = new Size(94, 25);
            btnCarpeta.Click += ElegirCarpeta;
            gIso.Controls.Add(btnCarpeta);

            gIso.Controls.Add(Nota(14, 58, AnchoTotalColumnas - 40, 44,
                "Puedes elegir un .iso o una carpeta ya extraida (con default.xex dentro, " +
                "como la que arma EXTRAER_XEX.bat). La primera vez con una ISO se extrae una " +
                "copia en game_root_cache\\; las siguientes veces arranca directo con esa copia."));

            // ---- Pantalla y resolucion ---------------------------------------
            //
            // LOS DOS AJUSTES DE AQUI NO SON EL MISMO, Y SE CONFUNDEN
            // =======================================================
            // Es LA confusion de esta ventana, asi que los nombres van elegidos
            // para que no pase:
            //
            //   "Tamano de la ventana"  -> --resolution. Cambia el modo de
            //       video que el juego cree tener y el tamano de la ventana.
            //       NO le pide al juego que dibuje mas fino: Most Wanted, como
            //       casi todo juego de 360, dibuja en sus propios render
            //       targets de tamano fijo y deja que el escalador estire el
            //       resultado. Subir esto agranda la imagen, no la mejora.
            //
            //   "Resolucion interna"    -> --resolution_scale. ESTE es el que
            //       la gente busca: el mismo "x2" de cualquier emulador.
            //       Multiplica el tamano de los render targets y de la EDRAM
            //       emulada, asi que el juego dibuja de verdad mas pixeles.
            //
            // Se llamaban "Resolucion de salida" y "Escala de renderizado", y
            // con esos nombres es facil tocar el primero esperando lo segundo,
            // ver que no cambia nada y darlo por roto.
            PanelSeccion gPant = Grupo("Pantalla y resolucion", 132, 252);

            gPant.Controls.Add(Etiqueta("Tamano de la ventana", 14, 26, 150));
            cboRes = new ComboBox();
            cboRes.DropDownStyle = ComboBoxStyle.DropDownList;
            cboRes.Location = new Point(168, 23);
            cboRes.Size = new Size(200, 23);
            for (int i = 0; i < Presets.GetLength(0); i++)
                cboRes.Items.Add(Presets[i, 0]);
            cboRes.SelectedIndexChanged += delegate { Refrescar(); };
            gPant.Controls.Add(cboRes);

            numAncho = Numero(168, 52, 70, 320, 7680);
            numAlto = Numero(250, 52, 70, 240, 4320);
            gPant.Controls.Add(Etiqueta("Personalizada", 14, 55, 150));
            gPant.Controls.Add(numAncho);
            gPant.Controls.Add(Etiqueta("x", 240, 55, 12));
            gPant.Controls.Add(numAlto);

            gPant.Controls.Add(Etiqueta("Resolucion interna", 14, 87, 150));
            cboEsc = new ComboBox();
            cboEsc.DropDownStyle = ComboBoxStyle.DropDownList;
            cboEsc.Location = new Point(168, 84);
            cboEsc.Size = new Size(200, 23);
            for (int i = 0; i < Escalas.GetLength(0); i++)
                cboEsc.Items.Add(Escalas[i, 0]);
            cboEsc.SelectedIndexChanged += delegate { Refrescar(); };
            gPant.Controls.Add(cboEsc);

            // Lo que hace de verdad la escala elegida, escrito en cada cambio.
            // Sin esto, elegir x2 y elegir x1 se ven igual hasta que arrancas.
            lblEscala = new Label();
            lblEscala.Location = new Point(168, 110);
            lblEscala.Size = new Size(AnchoCol - 190, 32);
            gPant.Controls.Add(lblEscala);

            rbCompleta = Radio("Pantalla completa", 14, 146, 150);
            rbVentana = Radio("En ventana", 168, 146, 150);
            gPant.Controls.Add(rbCompleta);
            gPant.Controls.Add(rbVentana);

            gPant.Controls.Add(Etiqueta("Monitor de salida", 14, 180, 150));
            cboMon = new ComboBox();
            cboMon.DropDownStyle = ComboBoxStyle.DropDownList;
            cboMon.Location = new Point(168, 177);
            cboMon.Size = new Size(200, 23);
            string[,] mon = Monitores();
            for (int i = 0; i < mon.GetLength(0); i++)
                cboMon.Items.Add(mon[i, 0]);
            cboMon.SelectedIndexChanged += delegate { Refrescar(); };
            gPant.Controls.Add(cboMon);

            gPant.Controls.Add(Nota(14, 214, AnchoCol - 40, 36,
                "No son lo mismo: el tamano de la ventana solo AGRANDA la imagen. La que la " +
                "hace mas fina es la resolucion interna, que es el mismo \"x2\" de los " +
                "emuladores, y cuesta cara: x2 son cuatro veces los pixeles a dibujar."));

            // ---- Calidad de imagen ---------------------------------------------
            //
            // Todo son cvars del SDK de ReXGlue que el recomp lee por linea de
            // comandos; aqui no se toca el juego ni el runtime.
            PanelSeccion gCal = Grupo("Calidad de imagen", 388, 196);

            gCal.Controls.Add(Etiqueta("Antialiasing", 14, 26, 150));
            cboAA = new ComboBox();
            cboAA.DropDownStyle = ComboBoxStyle.DropDownList;
            cboAA.Location = new Point(168, 23);
            cboAA.Size = new Size(200, 23);
            for (int i = 0; i < Antialias.GetLength(0); i++)
                cboAA.Items.Add(Antialias[i, 0]);
            cboAA.SelectedIndexChanged += delegate { Refrescar(); };
            gCal.Controls.Add(cboAA);

            gCal.Controls.Add(Etiqueta("Filtrado anisotropico", 14, 58, 150));
            cboAniso = new ComboBox();
            cboAniso.DropDownStyle = ComboBoxStyle.DropDownList;
            cboAniso.Location = new Point(168, 55);
            cboAniso.Size = new Size(200, 23);
            for (int i = 0; i < Anisotropico.GetLength(0); i++)
                cboAniso.Items.Add(Anisotropico[i, 0]);
            cboAniso.SelectedIndexChanged += delegate { Refrescar(); };
            gCal.Controls.Add(cboAniso);

            gCal.Controls.Add(Etiqueta("Efecto de acabado", 14, 90, 150));
            cboEfecto = new ComboBox();
            cboEfecto.DropDownStyle = ComboBoxStyle.DropDownList;
            cboEfecto.Location = new Point(168, 87);
            cboEfecto.Size = new Size(200, 23);
            for (int i = 0; i < Efectos.GetLength(0); i++)
                cboEfecto.Items.Add(Efectos[i, 0]);
            cboEfecto.SelectedIndexChanged += delegate { Refrescar(); };
            gCal.Controls.Add(cboEfecto);

            gCal.Controls.Add(Etiqueta("Nitidez (CAS)", 14, 122, 150));
            numNitidez = Numero(168, 119, 90, 0, 100);
            gCal.Controls.Add(numNitidez);
            gCal.Controls.Add(Etiqueta("%", 264, 122, 20));

            gCal.Controls.Add(Nota(14, 156, AnchoCol - 40, 34,
                "El anisotropico afina las texturas y el acabado remata la imagen al pasarla " +
                "a la ventana. Se aplican al reiniciar el juego."));

            // ---- Fotogramas ---------------------------------------------------
            PanelSeccion gFps = Grupo("Fotogramas", X1, 132, 124);

            chkVsync = Marca("Sincronizacion vertical (vsync)", 14, 24, 250);
            gFps.Controls.Add(chkVsync);

            chkLimite = Marca("Limitar a", 14, 52, 90);
            gFps.Controls.Add(chkLimite);
            numFps = Numero(108, 50, 70, 20, 300);
            gFps.Controls.Add(numFps);
            gFps.Controls.Add(Etiqueta("fps", 184, 53, 40));

            gFps.Controls.Add(Nota(14, 82, AnchoCol - 40, 34,
                "Los dos necesitan parche_presentador.py. La velocidad del juego no depende " +
                "de esto: se ajusta desde el menu de F4."));

            // ---- Motor de video ------------------------------------------------
            PanelSeccion gVideo = Grupo("Motor de video (emulacion de la EDRAM)", X1, 264, 92);

            rbVidAuto = Radio("Automatico", 14, 24, 110);
            rbVidRtv = Radio("Rapido (rtv)", 134, 24, 120);
            rbVidRov = Radio("Exacto (rov)", 264, 24, 120);
            gVideo.Controls.Add(rbVidAuto);
            gVideo.Controls.Add(rbVidRtv);
            gVideo.Controls.Add(rbVidRov);

            gVideo.Controls.Add(Nota(14, 50, AnchoCol - 40, 34,
                "Automatico usa lo que diga nfsmw.toml. Rapido puede duplicar los fps en " +
                "graficas integradas. Exacto se ve bien siempre y va mas lento."));

            // ---- API grafica ----------------------------------------------------
            //
            // ESTE GRUPO ES UNA SALIDA DE EMERGENCIA, Y POR ESO NO TIENE
            // 'AUTOMATICO'. Ver el comentario largo de ConstruirArgumentos.
            PanelSeccion gApi = Grupo("API grafica", X1, 364, 92);

            rbApiDx = Radio("DirectX 12 (recomendada)", 14, 24, 190);
            rbApiVk = Radio("Vulkan (experimental)", 214, 24, 190);
            gApi.Controls.Add(rbApiDx);
            gApi.Controls.Add(rbApiVk);

            gApi.Controls.Add(Nota(14, 50, AnchoCol - 40, 34,
                "Esta ventana manda sobre nfsmw.toml, asi que elegir mal aqui nunca deja el " +
                "juego sin poder abrirse: vuelves y cambias."));

            // A partir de aqui todo va a TODO EL ANCHO, debajo de las dos
            // columnas (la izquierda -Pantalla+Calidad- es la mas alta, hasta
            // y=584; ver los comentarios de gPant/gCal y gFps/gVideo/gApi mas
            // arriba si se cambia el orden de las secciones).
            const int yDebajoColumnas = 600;

            // ---- Aviso del parche -------------------------------------------
            lblParche = new Label();
            lblParche.Location = new Point(X0, yDebajoColumnas);
            lblParche.Size = new Size(AnchoTotalColumnas, 32);
            lblParche.ForeColor = Tema.Aviso;
            panelColumna.Controls.Add(lblParche);

            // ---- Lo que se va a ejecutar -------------------------------------
            PanelSeccion gCmd = Grupo("Lo que se va a ejecutar", X0, yDebajoColumnas + 40, 100,
                                      AnchoTotalColumnas);
            txtCmd = new TextBox();
            txtCmd.Location = new Point(12, 20);
            txtCmd.Size = new Size(AnchoTotalColumnas - 24, 72);
            txtCmd.Multiline = true;
            txtCmd.ReadOnly = true;
            txtCmd.ScrollBars = ScrollBars.Vertical;
            txtCmd.BackColor = Tema.FondoCampo;
            txtCmd.ForeColor = Tema.TextoTitulo;
            txtCmd.Font = new Font("Consolas", 7.5f);
            gCmd.Controls.Add(txtCmd);

            // ---- Botones ------------------------------------------------------
            int yBotones = yDebajoColumnas + 40 + 100 + 10;
            btnJugar = new Button();
            btnJugar.Text = "JUGAR";
            btnJugar.Location = new Point(X1 + AnchoCol - 230, yBotones);
            btnJugar.Size = new Size(120, 30);
            btnJugar.Font = new Font("Segoe UI", 9.75f, FontStyle.Bold);
            btnJugar.Click += Jugar;
            panelColumna.Controls.Add(btnJugar);
            AcceptButton = btnJugar;

            btnSalir = new Button();
            btnSalir.Text = "Salir";
            btnSalir.Location = new Point(X1 + AnchoCol - 100, yBotones);
            btnSalir.Size = new Size(100, 30);
            btnSalir.Click += delegate { Close(); };
            panelColumna.Controls.Add(btnSalir);

            lblEstado = new Label();
            lblEstado.Location = new Point(X0, yBotones + 6);
            lblEstado.Size = new Size(320, 32);
            lblEstado.ForeColor = Tema.TextoNota;
            panelColumna.Controls.Add(lblEstado);

            // Todo lo que cambia la linea de comandos, a refrescarla.
            EventHandler r = delegate { Refrescar(); };
            chkVsync.CheckedChanged += r;
            chkLimite.CheckedChanged += r;
            rbCompleta.CheckedChanged += r;
            rbVidAuto.CheckedChanged += r;
            rbVidRtv.CheckedChanged += r;
            rbVidRov.CheckedChanged += r;
            rbApiDx.CheckedChanged += r;
            rbApiVk.CheckedChanged += r;
            numAncho.ValueChanged += r;
            numAlto.ValueChanged += r;
            numFps.ValueChanged += r;
            numNitidez.ValueChanged += r;

            AplicarTema(this);

            Resize += delegate { AjustarLayout(); };
            AjustarLayout();
        }

        // ---------------------------------------------------------------------
        //  Banda y panel de ajustes se reparten el ancho de la ventana
        //
        //  La banda quiere sus AnchoBanda (380) px de toda la vida, pero en una
        //  ventana estrecha eso deja al panel de ajustes con menos de AnchoCol
        //  y aparece un scroll horizontal ademas del vertical -incomodo, y es
        //  justo lo que "que funcione en todo tipo de pantallas" pide evitar.
        //
        //  Asi que la banda cede: se calcula cuanto le sobra a la ventana
        //  despues de darle al panel su ancho minimo (AnchoCol + 24, lo mismo
        //  que AutoScrollMinSize) y la banda se queda con eso, entre 0 y
        //  AnchoBanda. Se sigue viendo -"conserva el banner del lado"- en
        //  cualquier tamano igual o mayor que MinimumSize; solo se estrecha.
        //
        //  Y AL REVES -ventana MAS ancha de lo que el contenido necesita-
        //  panelColumna (el ancho natural, AnchoCol+40) se CENTRA en el
        //  sobrante en vez de quedarse pegado a la izquierda con un hueco
        //  enorme a la derecha: es la otra mitad de "distribuye mejor el
        //  espacio". La banda no se ensancha mas alla de AnchoBanda -no hay
        //  mas portada que mostrar-, asi que ese sobrante es todo para
        //  centrar la columna.
        //
        //  Se llama una vez al construir y en cada Resize: por eso banda y
        //  panelContenido NO llevan Anchor de ancho (Left+Right compitiendo
        //  con esto daria tirones), solo Top+Bottom para el alto.
        // ---------------------------------------------------------------------
        private void AjustarLayout()
        {
            if (banda == null || panelContenido == null || panelColumna == null)
                return;

            // + el ancho de la barra de scroll vertical: con AltoUtil (1066)
            // casi siempre hay scroll vertical, y esa barra le come ancho de
            // verdad al panel. Sin este margen, el calculo cuadraba justo SIN
            // la barra, la barra aparecia, y esos ~17px que le robaba
            // empujaban tambien un scroll horizontal -exactamente el problema
            // que este metodo existe para evitar.
            int contenidoMinimo = panelColumna.Width + SystemInformation.VerticalScrollBarWidth;
            int anchoBandaReal = Math.Max(0, Math.Min(AnchoBanda, ClientSize.Width - contenidoMinimo));

            // Alto explicito para las dos, siempre el de la ventana actual:
            // es lo que evita que la banda se quede mas alta que el panel de
            // ajustes -y la portada tuviera que rellenar ese sobrante
            // repitiendose- si algo deja el alto desincronizado entre una y
            // otra.
            banda.Size = new Size(anchoBandaReal, ClientSize.Height);
            panelContenido.Location = new Point(anchoBandaReal, 0);
            panelContenido.Size = new Size(ClientSize.Width - anchoBandaReal, ClientSize.Height);

            int sobra = panelContenido.ClientSize.Width - panelColumna.Width;
            panelColumna.Location = new Point(Math.Max(0, sobra / 2), 0);
        }

        // ---------------------------------------------------------------------
        //  Reparte el tema oscuro por todo el arbol de controles
        //
        //  Mas simple y mas dificil de olvidar que colorear cada control en el
        //  sitio donde se crea: un control nuevo que se anada a Construir()
        //  queda tematizado sin tener que acordarse.
        //
        //  Los controles con color DINAMICO -lblParche, lblEstado, lblEscala,
        //  que cambian de color en Refrescar/EstadoInicial/AlTerminar segun el
        //  estado- usan directamente los tonos de Tema en esos sitios, no este
        //  paso: este solo corre una vez, al construir la ventana.
        // ---------------------------------------------------------------------
        private static void AplicarTema(Control raiz)
        {
            foreach (Control c in raiz.Controls)
            {
                if (c is PanelSeccion)
                {
                    // Ya se pinta solo en su propio OnPaint.
                }
                else if (c is PanelPortada)
                {
                    // La portada se queda con su negro de toda la vida.
                }
                else if (c is Panel)
                {
                    c.BackColor = Tema.Fondo;
                }
                else if (c is TextBox)
                {
                    TextBox t = (TextBox)c;
                    t.BackColor = Tema.FondoCampo;
                    t.ForeColor = Tema.Texto;
                    t.BorderStyle = BorderStyle.FixedSingle;
                }
                else if (c is ComboBox)
                {
                    ComboBox cb = (ComboBox)c;
                    cb.BackColor = Tema.FondoCampo;
                    cb.ForeColor = Tema.Texto;
                    cb.FlatStyle = FlatStyle.Flat;
                }
                else if (c is Button)
                {
                    Button b = (Button)c;
                    b.BackColor = Tema.FondoPanel;
                    b.ForeColor = Tema.TextoTitulo;
                    b.FlatStyle = FlatStyle.Flat;
                    b.FlatAppearance.BorderColor = Tema.Borde;
                    b.FlatAppearance.MouseOverBackColor = Tema.BordeSuave;
                }
                else if (c is Label)
                {
                    if (c.BackColor != Tema.FondoCampo)
                        c.BackColor = Color.Transparent;
                }

                if (c.HasChildren)
                    AplicarTema(c);
            }
        }

        // ---- Fabriquitas de controles, para no repetir seis lineas cada vez ----
        // Tres formas, todas caen en la de cuatro argumentos: X0 y AnchoCol
        // (columna izquierda, ancho de una columna) por defecto, para no
        // tener que tocar las llamadas que ya estaban.
        private PanelSeccion Grupo(string texto, int y, int alto)
        {
            return Grupo(texto, X0, y, alto, AnchoCol);
        }

        private PanelSeccion Grupo(string texto, int x, int y, int alto)
        {
            return Grupo(texto, x, y, alto, AnchoCol);
        }

        private PanelSeccion Grupo(string texto, int x, int y, int alto, int ancho)
        {
            PanelSeccion g = new PanelSeccion(texto);
            g.Location = new Point(x, y);
            g.Size = new Size(ancho, alto);
            panelColumna.Controls.Add(g);
            return g;
        }

        private static Label Etiqueta(string texto, int x, int y, int ancho)
        {
            Label l = new Label();
            l.Text = texto;
            l.Location = new Point(x, y);
            l.Size = new Size(ancho, 20);
            l.ForeColor = Tema.Texto;
            l.BackColor = Color.Transparent;
            return l;
        }

        private static Label Nota(int x, int y, int ancho, int alto, string texto)
        {
            Label l = new Label();
            l.Text = texto;
            l.Location = new Point(x, y);
            l.Size = new Size(ancho, alto);
            l.ForeColor = Tema.TextoNota;
            l.BackColor = Color.Transparent;
            return l;
        }

        private static RadioButton Radio(string texto, int x, int y, int ancho)
        {
            RadioButton b = new RadioButton();
            b.Text = texto;
            b.Location = new Point(x, y);
            b.Size = new Size(ancho, 22);
            b.ForeColor = Tema.Texto;
            b.BackColor = Color.Transparent;
            return b;
        }

        private static CheckBox Marca(string texto, int x, int y, int ancho)
        {
            CheckBox c = new CheckBox();
            c.Text = texto;
            c.Location = new Point(x, y);
            c.Size = new Size(ancho, 22);
            c.ForeColor = Tema.Texto;
            c.BackColor = Color.Transparent;
            return c;
        }

        private static NumericUpDown Numero(int x, int y, int ancho, int min, int max)
        {
            NumericUpDown n = new NumericUpDown();
            n.Location = new Point(x, y);
            n.Size = new Size(ancho, 23);
            n.Minimum = min;
            n.Maximum = max;
            n.Increment = 1;
            n.BackColor = Tema.FondoCampo;
            n.ForeColor = Tema.Texto;
            n.BorderStyle = BorderStyle.FixedSingle;
            return n;
        }

        // ---------------------------------------------------------------------
        //  Ajustes: el mismo fichero y los mismos nombres que lanzador.ps1
        // ---------------------------------------------------------------------
        private void CargarAjustes()
        {
            Dictionary<string, string> a = new Dictionary<string, string>();
            try
            {
                if (File.Exists(ficheroAjustes))
                    a = Json.Leer(File.ReadAllText(ficheroAjustes, Encoding.UTF8));
            }
            catch
            {
                // Un json roto no puede impedir abrir el lanzador.
            }

            txtIso.Text = Cadena(a, "iso", "");

            // Por defecto 1080p + x2, no 720p + x1: es lo mismo que ya trae
            // nfsmw.toml de fabrica (video_mode 1920x1080, resolution_scale
            // 2 -"recomendado" segun su propio comentario-), asi que alguien
            // que abre el lanzador por primera vez, sin lanzador.json todavia,
            // ve la MISMA calidad que tendria arrancando nfsmw.exe a pelo. Sin
            // esto el lanzador rebajaba la resolucion real de 1080p a 720p sin
            // que nadie lo pidiera, solo por no coincidir con el toml.
            int i = IndiceDe(cboRes, Cadena(a, "preset", "1080p - 1920 x 1080"));
            cboRes.SelectedIndex = i >= 0 ? i : 4;

            numAncho.Value = Acotar(numAncho, Entero(a, "ancho", 1920));
            numAlto.Value = Acotar(numAlto, Entero(a, "alto", 1080));

            int e = IndiceDe(cboEsc, Cadena(a, "escala", "x2  - 4 veces los pixeles"));
            cboEsc.SelectedIndex = e >= 0 ? e : 1;

            int mo = IndiceDe(cboMon, Cadena(a, "monitor", "Automatico (predeterminado)"));
            cboMon.SelectedIndex = mo >= 0 ? mo : 0;

            int aa = IndiceDe(cboAA, Cadena(a, "antialiasing", "Desactivado"));
            cboAA.SelectedIndex = aa >= 0 ? aa : 0;

            int an = IndiceDe(cboAniso, Cadena(a, "anisotropico", "8x"));
            cboAniso.SelectedIndex = an >= 0 ? an : 4;

            int ef = IndiceDe(cboEfecto, Cadena(a, "efecto", "Ninguno (bilinear)"));
            cboEfecto.SelectedIndex = ef >= 0 ? ef : 0;

            numNitidez.Value = Acotar(numNitidez, Entero(a, "nitidez", 50));

            bool completa = Booleano(a, "pantalla", true);
            rbCompleta.Checked = completa;
            rbVentana.Checked = !completa;

            chkVsync.Checked = Booleano(a, "vsync", false);
            chkLimite.Checked = Booleano(a, "limitar", false);
            numFps.Value = Acotar(numFps, Entero(a, "fps", 60));

            string v = Cadena(a, "video", "auto");
            rbVidRtv.Checked = v == "rtv";
            rbVidRov.Checked = v == "rov";
            rbVidAuto.Checked = !(rbVidRtv.Checked || rbVidRov.Checked);

            bool vulkan = Cadena(a, "api", "d3d12") == "vulkan";
            rbApiVk.Checked = vulkan;
            rbApiDx.Checked = !vulkan;
        }

        private void GuardarAjustes()
        {
            try
            {
                string dir = Path.GetDirectoryName(ficheroAjustes);
                if (!Directory.Exists(dir))
                    Directory.CreateDirectory(dir);

                StringBuilder sb = new StringBuilder();
                sb.AppendLine("{");
                sb.AppendLine("  \"iso\":  \"" + Json.Escapar(txtIso.Text) + "\",");
                sb.AppendLine("  \"preset\":  \"" + Json.Escapar(TextoDe(cboRes)) + "\",");
                sb.AppendLine("  \"ancho\":  " + ((int)numAncho.Value) + ",");
                sb.AppendLine("  \"alto\":  " + ((int)numAlto.Value) + ",");
                sb.AppendLine("  \"escala\":  \"" + Json.Escapar(TextoDe(cboEsc)) + "\",");
                sb.AppendLine("  \"monitor\":  \"" + Json.Escapar(TextoDe(cboMon)) + "\",");
                sb.AppendLine("  \"pantalla\":  " + (rbCompleta.Checked ? "true" : "false") + ",");
                sb.AppendLine("  \"vsync\":  " + (chkVsync.Checked ? "true" : "false") + ",");
                sb.AppendLine("  \"limitar\":  " + (chkLimite.Checked ? "true" : "false") + ",");
                sb.AppendLine("  \"fps\":  " + ((int)numFps.Value) + ",");
                sb.AppendLine("  \"antialiasing\":  \"" + Json.Escapar(TextoDe(cboAA)) + "\",");
                sb.AppendLine("  \"anisotropico\":  \"" + Json.Escapar(TextoDe(cboAniso)) + "\",");
                sb.AppendLine("  \"efecto\":  \"" + Json.Escapar(TextoDe(cboEfecto)) + "\",");
                sb.AppendLine("  \"nitidez\":  " + ((int)numNitidez.Value) + ",");
                sb.AppendLine("  \"video\":  \"" + VideoElegido() + "\",");
                sb.AppendLine("  \"api\":  \"" + ApiElegida() + "\"");
                sb.Append("}");
                File.WriteAllText(ficheroAjustes, sb.ToString(), new UTF8Encoding(false));
            }
            catch
            {
                // Guardar preferencias es un lujo, no una condicion para jugar.
            }
        }

        private static string Cadena(Dictionary<string, string> a, string k, string porDefecto)
        {
            string v;
            if (a.TryGetValue(k, out v) && v != null && v.Length > 0 && v != "null")
                return v;
            return porDefecto;
        }

        private static int Entero(Dictionary<string, string> a, string k, int porDefecto)
        {
            string v;
            int n;
            if (a.TryGetValue(k, out v) && int.TryParse(v, NumberStyles.Integer,
                                                        CultureInfo.InvariantCulture, out n))
                return n;
            return porDefecto;
        }

        private static bool Booleano(Dictionary<string, string> a, string k, bool porDefecto)
        {
            string v;
            if (a.TryGetValue(k, out v))
            {
                if (v == "true" || v == "True" || v == "1") return true;
                if (v == "false" || v == "False" || v == "0") return false;
            }
            return porDefecto;
        }

        private static decimal Acotar(NumericUpDown n, int v)
        {
            if (v < n.Minimum) return n.Minimum;
            if (v > n.Maximum) return n.Maximum;
            return v;
        }

        private static int IndiceDe(ComboBox c, string texto)
        {
            for (int i = 0; i < c.Items.Count; i++)
                if ((string)c.Items[i] == texto)
                    return i;
            return -1;
        }

        private static string TextoDe(ComboBox c)
        {
            return c.SelectedItem == null ? "" : (string)c.SelectedItem;
        }

        // ---------------------------------------------------------------------
        //  La linea de comandos
        // ---------------------------------------------------------------------
        private string SalidaElegida()
        {
            int i = cboRes.SelectedIndex;
            if (i < 0)
                return "720p";
            string v = Presets[i, 1];
            if (v == "custom")
                return string.Format(CultureInfo.InvariantCulture, "{0}x{1}",
                                     (int)numAncho.Value, (int)numAlto.Value);
            return v;
        }

        private int EscalaElegida()
        {
            int i = cboEsc.SelectedIndex;
            if (i < 0)
                return 1;
            return int.Parse(Escalas[i, 1], CultureInfo.InvariantCulture);
        }

        private string VideoElegido()
        {
            if (rbVidRtv.Checked) return "rtv";
            if (rbVidRov.Checked) return "rov";
            return "auto";
        }

        private string ApiElegida()
        {
            return rbApiVk.Checked ? "vulkan" : "d3d12";
        }

        private string AAElegida()
        {
            int i = cboAA.SelectedIndex;
            if (i < 0)
                return "none";
            return Antialias[i, 1];
        }

        private int AnisotropicoElegido()
        {
            int i = cboAniso.SelectedIndex;
            if (i < 0)
                return 4;
            return int.Parse(Anisotropico[i, 1], CultureInfo.InvariantCulture);
        }

        private string EfectoElegido()
        {
            int i = cboEfecto.SelectedIndex;
            if (i < 0)
                return "bilinear";
            return Efectos[i, 1];
        }

        private decimal NitidezElegida()
        {
            return ((decimal)numNitidez.Value) / 100m;
        }

        private int MonitorElegido()
        {
            int i = cboMon.SelectedIndex;
            if (i <= 0 || i > Screen.AllScreens.Length)
                return 0;
            return i;
        }

        // rutaJuego es la CARPETA que se pasa como --game_data_root: o bien la
        // que el usuario eligio directamente (formato .xex ya extraido), o la
        // cache donde ResolverRutaJuego dejo la ISO extraida. Nunca un .iso
        // suelto: el SDK exige un directorio (rex_app.cpp valida con
        // std::filesystem::is_directory) y no sabe montar imagenes.
        private string ConstruirArgumentos(string rutaJuego)
        {
            List<string> a = new List<string>();
            a.Add("--log_level info");
            a.Add("--log_file \"" + logEjecucion + "\"");
            a.Add("--game_data_root \"" + rutaJuego + "\"");
            a.Add("--gpu_plugin xenos");
            a.Add("--mnk_mode");

            // Fijo, y no es una preferencia: sin esto la imagen sale lavada y el
            // sol reventado.
            a.Add("--readback_resolve=fast");

            // SIEMPRE, aunque coincida con lo que ya diga nfsmw.toml.
            //
            // En el orden de prioridad de los cvars del SDK la linea de comandos
            // manda sobre el fichero de configuracion:
            //
            //     kDefault < kConfig < kEnvironment < kCommandLine < kRuntime
            //
            // gpu_backend tambien se puede cambiar desde el menu de F4, y ahi
            // esta el peligro: si eliges una API que en tu equipo da pantalla
            // negra, guardas y reinicias, el valor se queda escrito en el toml y
            // ya no hay forma de volver -para cambiarlo necesitas el menu, y
            // para llegar al menu necesitas ver algo-. Paso de verdad.
            //
            // Pasandolo desde aqui siempre, esta ventana gana al toml y eso no
            // puede ocurrir. Por eso tampoco hay opcion "automatico" en el grupo
            // de la API: un automatico que no pasara nada devolveria el mando al
            // toml, que es justo el agujero.
            a.Add("--gpu_backend=" + ApiElegida());

            a.Add("--resolution " + SalidaElegida());

            int esc = EscalaElegida();
            if (esc > 1)
                a.Add("--resolution_scale " + esc);

            // Antialiasing: SIEMPRE se pasa, como la API. Asi elegir
            // "Desactivado" aqui gana a lo que diga nfsmw.toml, en vez de
            // devolverle el mando al fichero.
            a.Add("--swap_post_effect=" + AAElegida());

            // Calidad de imagen: aniso y nitidez siempre (manda esta ventana);
            // el efecto de acabado solo cuando no es el de siempre.
            a.Add("--anisotropic_override " + AnisotropicoElegido());
            if (EfectoElegido() != "bilinear")
                a.Add("--present_effect=" + EfectoElegido());
            a.Add("--present_cas_additional_sharpness " +
                  string.Format(CultureInfo.InvariantCulture, "{0:0.##}", NitidezElegida()));

            a.Add(rbCompleta.Checked ? "--fullscreen=true" : "--fullscreen=false");
            a.Add("--monitor " + MonitorElegido());
            a.Add(chkVsync.Checked ? "--vsync=true" : "--vsync=false");
            if (chkLimite.Checked)
                a.Add("--max_fps " + ((int)numFps.Value));

            // Estos dos solo si se han elegido a mano. En automatico no se pasa
            // nada y manda el toml, que trae "rtv". Al reves que la API: aqui
            // elegir mal no deja el juego invisible, solo mas lento o con una
            // franja rara, asi que dejar mandar al fichero no tiene peligro.
            if (rbVidRtv.Checked) a.Add("--render_target_path_d3d12=rtv");
            if (rbVidRov.Checked) a.Add("--render_target_path_d3d12=rov");

            return string.Join(" ", a.ToArray());
        }

        private void Refrescar()
        {
            if (cargando)
                return;

            bool esCustom = cboRes.SelectedIndex >= 0 &&
                            Presets[cboRes.SelectedIndex, 1] == "custom";
            numAncho.Enabled = esCustom;
            numAlto.Enabled = esCustom;
            numFps.Enabled = chkLimite.Checked;

            // Que se vea, ANTES de arrancar, que la escala hace algo. Sin esto
            // el unico sitio donde x1 y x2 se distinguen es la linea de
            // comandos de ahi abajo, que casi nadie lee.
            //
            // No se pone la resolucion en pixeles a proposito: la escala NO
            // multiplica el tamano de la ventana, multiplica los render targets
            // del juego, que son de un tamano suyo que desde aqui no se conoce.
            // Poner "2560 x 1440" seria inventarselo.
            int esc = EscalaElegida();
            if (esc <= 1)
            {
                lblEscala.ForeColor = Tema.TextoNota;
                lblEscala.Text = "El juego dibuja a su resolucion original de Xbox 360.";
            }
            else
            {
                lblEscala.ForeColor = Tema.Acento;
                lblEscala.Text = string.Format(
                    "El juego dibuja {0} veces mas ancho y mas alto: {1} veces los pixeles.\n" +
                    "Se ve mas fino, y la GPU trabaja {1} veces mas.", esc, esc * esc);
            }

            // Vista previa: usa tal cual lo que hay escrito en el cuadro de ISO,
            // aunque sea un .iso. La extraccion de verdad (si hace falta) solo
            // ocurre al pulsar JUGAR, en ResolverRutaJuego -hacerlo aqui, en
            // cada tecla, seria carisimo.
            string vista = txtIso.Text.Length > 0 ? txtIso.Text : "(sin elegir)";
            txtCmd.Text = Path.GetFileName(exeJuego) + " " + ConstruirArgumentos(vista);
            if (vista.Length > 0 && vista.EndsWith(".iso", StringComparison.OrdinalIgnoreCase))
            {
                txtCmd.Text += "\r\n(la ISO se extrae a game_root_cache\\ la primera vez que se " +
                               "pulsa JUGAR; luego se usa esa copia)";
            }
        }

        // ---------------------------------------------------------------------
        //  Estado inicial: ISO encontrada sola y aviso del parche
        // ---------------------------------------------------------------------
        private void EstadoInicial()
        {
            if (txtIso.Text.Length == 0)
            {
                try
                {
                    string[] isos = Directory.GetFiles(raiz, "*.iso", SearchOption.TopDirectoryOnly);
                    if (isos.Length > 0)
                    {
                        txtIso.Text = isos[0];
                    }
                    else
                    {
                        // Sin ISO al lado: una carpeta "game_root" ya extraida
                        // tambien vale (mismo criterio que OnConfigurePaths en
                        // nfsmw_app.h).
                        string carpetaGr = Path.Combine(raiz, "game_root");
                        if (Directory.Exists(carpetaGr) &&
                            File.Exists(Path.Combine(carpetaGr, "default.xex")))
                        {
                            txtIso.Text = carpetaGr;
                        }
                    }
                }
                catch
                {
                }
            }

            // Se mira el FUENTE del SDK, no la DLL: es donde vive la verdad y es
            // barato de comprobar.
            //
            // En la carpeta repartible no hay fuente que mirar, pero tampoco
            // duda: esa carpeta se arma desde un arbol ya parcheado.
            bool? parche = null;
            if (distribuida)
            {
                parche = true;
            }
            else if (fuentePresentador != null && File.Exists(fuentePresentador))
            {
                try
                {
                    parche = File.ReadAllText(fuentePresentador)
                                 .Contains("PARCHE LOCAL - vsync real y limitador de fps");
                }
                catch
                {
                }
            }

            if (parche == false)
            {
                lblParche.Text = "AVISO: vsync y el limite de fps NO haran nada todavia. De fabrica " +
                                 "el SDK no sincroniza y no trae limitador. Aplica " +
                                 "tools\\parche_presentador.py y recompila el SDK.";
            }
            else if (parche == null)
            {
                lblParche.ForeColor = Tema.TextoNota;
                lblParche.Text = "No encuentro el fuente del SDK, asi que no se si el parche de " +
                                 "vsync esta puesto.";
            }

            if (!File.Exists(exeJuego))
            {
                lblEstado.Text = "Aviso: no hay ejecutable compilado todavia.";
                lblEstado.ForeColor = Tema.Aviso;
            }
        }

        private void ElegirIso(object s, EventArgs e)
        {
            using (OpenFileDialog d = new OpenFileDialog())
            {
                d.Filter = "Imagen de disco (*.iso)|*.iso|Todos los archivos (*.*)|*.*";
                d.Title = "Elige la ISO de Need for Speed: Most Wanted";
                try
                {
                    if (txtIso.Text.Length > 0 && File.Exists(txtIso.Text))
                        d.InitialDirectory = Path.GetDirectoryName(txtIso.Text);
                    else
                        d.InitialDirectory = raiz;
                }
                catch
                {
                }
                if (d.ShowDialog(this) == DialogResult.OK)
                {
                    txtIso.Text = d.FileName;
                    Refrescar();
                }
            }
        }

        private void ElegirCarpeta(object s, EventArgs e)
        {
            using (FolderBrowserDialog d = new FolderBrowserDialog())
            {
                d.Description =
                    "Elige la carpeta con el juego ya extraido (debe contener default.xex)";
                try
                {
                    if (txtIso.Text.Length > 0 && Directory.Exists(txtIso.Text))
                        d.SelectedPath = txtIso.Text;
                    else
                        d.SelectedPath = raiz;
                }
                catch
                {
                }

                if (d.ShowDialog(this) == DialogResult.OK)
                {
                    txtIso.Text = d.SelectedPath;
                    Refrescar();
                }
            }
        }

        // ---------------------------------------------------------------------
        //  De lo que eligio el usuario a la carpeta que necesita el SDK
        //
        //  Si ya es una carpeta (formato .xex extraido), se usa tal cual. Si es
        //  un .iso, hace falta extraerlo primero: rex_app.cpp exige que
        //  --game_data_root sea un directorio de verdad y en todo rexglue-sdk
        //  no hay ningun lector de .iso (se comprobo a mano: cero referencias a
        //  XDVDFS o a montar imagenes). Sin este paso, pasar la ISO tal cual
        //  produce exactamente "--game_data_root does not exist: ...iso".
        //
        //  La extraccion completa son varios GB y tarda minutos, asi que solo
        //  se repite si la ISO cambio: CacheValida compara ruta y tamano contra
        //  el marcador que deja EscribirMarcador la vez anterior.
        // ---------------------------------------------------------------------
        private string ResolverRutaJuego(string entrada, bool esCarpeta)
        {
            if (esCarpeta)
                return entrada;

            string carpetaCache = Path.Combine(raiz, "game_root_cache",
                ExtractorXdvdfs.SanearNombre(Path.GetFileNameWithoutExtension(entrada)));

            if (ExtractorXdvdfs.CacheValida(carpetaCache, entrada))
                return carpetaCache;

            using (VentanaExtraccion ve = new VentanaExtraccion(entrada, carpetaCache))
            {
                DialogResult r = ve.ShowDialog(this);
                if (r != DialogResult.OK)
                {
                    if (ve.Error != null)
                    {
                        MessageBox.Show(this,
                            "No se pudo extraer el ISO:\n\n" + ve.Error.Message,
                            "Error al extraer", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }
                    return null;
                }
            }
            return carpetaCache;
        }

        // ---------------------------------------------------------------------
        //  Jugar
        //
        //  El juego se espera EN OTRO HILO. El lanzador de PowerShell hacia
        //  WaitForExit en el hilo de la ventana, y mientras jugabas la ventana
        //  se quedaba colgada -Windows la pintaba en blanco y la marcaba como
        //  "no responde"-. Aqui se lanza aparte y se vuelve a la ventana con
        //  Invoke cuando termina.
        // ---------------------------------------------------------------------
        private void Jugar(object s, EventArgs e)
        {
            if (!File.Exists(exeJuego))
            {
                MessageBox.Show(this,
                    "No encuentro el ejecutable:\n\n" + exeJuego + "\n\nCompila primero.",
                    "Falta el ejecutable", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string entrada = txtIso.Text;
            bool esIso = entrada.Length > 0 && File.Exists(entrada) &&
                        entrada.EndsWith(".iso", StringComparison.OrdinalIgnoreCase);
            bool esCarpeta = entrada.Length > 0 && Directory.Exists(entrada);
            if (!esIso && !esCarpeta)
            {
                MessageBox.Show(this,
                    "Elige una ISO o una carpeta con el juego ya extraido (formato .xex) que exista.",
                    "Falta el juego", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            // Guardar ANTES de lanzar: si el juego revienta, las preferencias se
            // quedan puestas igualmente.
            GuardarAjustes();

            // Si es una ISO, ResolverRutaJuego la extrae a game_root_cache\ (o
            // reusa la extraccion anterior si sigue siendo la misma ISO) y
            // devuelve esa carpeta. Null significa que el usuario cancelo la
            // extraccion o que fallo -y ya se aviso-, asi que no se llega a
            // lanzar nada.
            string rutaJuego = ResolverRutaJuego(entrada, esCarpeta);
            if (rutaJuego == null)
                return;

            try
            {
                if (!Directory.Exists(dirLogs))
                    Directory.CreateDirectory(dirLogs);
            }
            catch
            {
            }

            btnJugar.Enabled = false;
            lblEstado.ForeColor = Tema.TextoNota;
            lblEstado.Text = "Jugando... (F3 para ver los fps)";

            string argumentos = ConstruirArgumentos(rutaJuego);
            Thread hilo = new Thread(delegate ()
            {
                int codigo = 0;
                try
                {
                    ProcessStartInfo psi = new ProcessStartInfo(exeJuego, argumentos);
                    psi.WorkingDirectory = Path.GetDirectoryName(exeJuego);
                    psi.UseShellExecute = false;
                    using (Process p = Process.Start(psi))
                    {
                        if (p == null)
                            throw new InvalidOperationException(
                                "Windows no ha llegado a crear el proceso.");

                        // Prioridad de planificacion mas alta que Normal. El
                        // hilo de audio y el de comandos de la GPU son los que
                        // mas sufren si Windows les quita CPU para dar paso a
                        // otra cosa -es literalmente el sintoma del "quejido"
                        // de audio que arreglo el desatasco-, y en una maquina
                        // con el CPU ocupado (Discord, el navegador, un
                        // antivirus escaneando) planificar antes ayuda sin
                        // tocar un solo pixel de lo que se dibuja.
                        //
                        // High y no RealTime: RealTime puede dejar sin CPU al
                        // propio Windows -raton y teclado incluidos- si el
                        // juego se queda en un bucle apretado, que es
                        // justo el tipo de cuelgue que este proyecto ya vigila
                        // por otro lado (ver ArrancarVigilante en nfsmw_app.h).
                        // Si falla -permisos, o el proceso ya termino- no es
                        // motivo para no jugar: se seguiria en Normal.
                        try
                        {
                            p.PriorityClass = ProcessPriorityClass.High;
                        }
                        catch
                        {
                        }

                        p.WaitForExit();
                        codigo = p.ExitCode;
                    }
                }
                catch (Exception ex)
                {
                    string mensaje = ex.Message;
                    EnLaVentana(delegate
                    {
                        MessageBox.Show(this, "No se pudo lanzar:\n\n" + mensaje, "Error",
                                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                        btnJugar.Enabled = true;
                        lblEstado.Text = "";
                    });
                    return;
                }

                int cod = codigo;
                EnLaVentana(delegate { AlTerminar(cod); });
            });
            hilo.IsBackground = true;
            hilo.Start();
        }

        // Volver al hilo de la ventana desde el hilo que espera al juego.
        //
        // Con la comprobacion delante a proposito: si cierras el lanzador
        // mientras juegas, cuando el juego termina ya no hay ventana a la que
        // volver, e Invoke sobre un formulario destruido revienta con una
        // excepcion sin capturar y una ventana de error de .NET. Que el lanzador
        // pete DESPUES de haberlo cerrado tu queda especialmente absurdo.
        private void EnLaVentana(MethodInvoker que)
        {
            try
            {
                if (IsDisposed || !IsHandleCreated)
                    return;
                Invoke(que);
            }
            catch (ObjectDisposedException)
            {
                // Se cerro entre la comprobacion y el Invoke. No hay nada que hacer.
            }
            catch (InvalidOperationException)
            {
                // Idem: el handle se destruyo por el camino.
            }
        }

        private void AlTerminar(int codigo)
        {
            btnJugar.Enabled = true;
            lblEstado.Text = "";

            // Si se pidio escala y la grafica no pudo, el SDK la baja sola y lo
            // deja escrito en el log.
            string bajada = BuscarEnLog(new string[] { "draw resolution scale is not supported" },
                                        true);
            if (bajada != null)
            {
                MessageBox.Show(this,
                    "La escala de renderizado que pediste no la admite tu equipo, asi que el SDK " +
                    "la ha bajado sola:\n\n" + bajada,
                    "Escala reducida", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }

            if (codigo != 0)
            {
                string pistas = BuscarEnLog(new string[] { "[critical]", "FATAL", "unregistered" },
                                            false);
                MessageBox.Show(this,
                    string.Format("El juego termino con codigo {0}.{1}\n\nLog: {2}",
                                  codigo, pistas == null ? "" : "\n\n" + pistas, logEjecucion),
                    "Termino con error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // Devuelve la primera linea que contenga alguna de las agujas, o las
        // ultimas ocho juntas si soloLaPrimera es false. Null si no hay ninguna.
        private string BuscarEnLog(string[] agujas, bool soloLaPrimera)
        {
            try
            {
                if (!File.Exists(logEjecucion))
                    return null;

                List<string> encontradas = new List<string>();
                using (StreamReader r = new StreamReader(logEjecucion))
                {
                    string linea;
                    while ((linea = r.ReadLine()) != null)
                    {
                        foreach (string aguja in agujas)
                        {
                            if (linea.IndexOf(aguja, StringComparison.Ordinal) >= 0)
                            {
                                if (soloLaPrimera)
                                    return linea;
                                encontradas.Add(linea);
                                break;
                            }
                        }
                    }
                }

                if (encontradas.Count == 0)
                    return null;
                int desde = Math.Max(0, encontradas.Count - 8);
                return string.Join("\n", encontradas.GetRange(desde, encontradas.Count - desde)
                                                    .ToArray());
            }
            catch
            {
                return null;
            }
        }
    }

    // ===========================================================================
    //  Extractor XDVDFS: de .iso de Xbox 360 a carpeta, sin dependencias
    //
    //  Puerto a C# de tools\fase1_extraer.py (opcion "2. Extraer TODO"). Existe
    //  porque rexglue-sdk no sabe leer imagenes .iso: Runtime/ReXApp exigen que
    //  --game_data_root sea ya una carpeta (rex_app.cpp, is_directory). Aqui se
    //  hace ese paso solo, sin tener que instalar Python aparte -la build
    //  repartible no puede depender de eso.
    //
    //  Formato (XDVDFS, "MICROSOFT*XBOX*MEDIA"):
    //    - Descriptor de volumen a 32 sectores desde la base de la particion.
    //    - La base varia segun el tipo de disco (XGD1/2/3 o imagen ya
    //      recortada); se prueban los offsets conocidos y, si ninguno cuadra,
    //      se barre la imagen buscando el magic.
    //    - El arbol de cada directorio es un arbol binario plano: cada entrada
    //      trae hijo-izquierdo, hijo-derecho, sector, tamano, atributos y
    //      nombre. Los indices de hijo son "sector logico / 4", no bytes.
    // ===========================================================================
    internal static class ExtractorXdvdfs
    {
        private const int Sector = 2048;
        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");

        // 0 = particion cruda / imagen ya recortada; los demas son XGD2, XGD3 y
        // XGD1 (Xbox original), en ese orden de frecuencia real.
        private static readonly long[] BasesConocidas =
            { 0x00000000L, 0x0FD90000L, 0x02080000L, 0x18300000L };

        private sealed class Entrada
        {
            public string Nombre;
            public uint Sector;
            public uint Tam;
            public bool Dir;
        }

        public delegate void Progreso(string archivo, int hechos, int total);
        public delegate bool Cancelado();

        // -----------------------------------------------------------------
        //  XDVDFS
        // -----------------------------------------------------------------
        private static bool MagicEn(FileStream fh, long offset)
        {
            try
            {
                fh.Seek(offset + 32L * Sector, SeekOrigin.Begin);
                byte[] buf = new byte[Magic.Length];
                int leido = fh.Read(buf, 0, buf.Length);
                if (leido != buf.Length)
                    return false;
                for (int i = 0; i < buf.Length; i++)
                {
                    if (buf[i] != Magic[i])
                        return false;
                }
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static int Buscar(byte[] buf, int longitudValida, int desde)
        {
            if (desde < 0)
                desde = 0;
            int limite = longitudValida - Magic.Length;
            for (int i = desde; i <= limite; i++)
            {
                bool ok = true;
                for (int j = 0; j < Magic.Length; j++)
                {
                    if (buf[i + j] != Magic[j])
                    {
                        ok = false;
                        break;
                    }
                }
                if (ok)
                    return i;
            }
            return -1;
        }

        private static long DetectarBase(FileStream fh)
        {
            for (int i = 0; i < BasesConocidas.Length; i++)
            {
                if (MagicEn(fh, BasesConocidas[i]))
                    return BasesConocidas[i];
            }

            // Ninguno de los offsets conocidos cuadra: barrido por fuerza bruta
            // en trozos de 16 MB, con solape para no perder el magic partido
            // entre dos trozos.
            long tam = fh.Length;
            long tope = Math.Min(tam, 1L << 30);
            const int chunk = 16 << 20;
            int solapa = Magic.Length;
            byte[] buf = new byte[chunk + solapa];
            long pos = 0;
            while (pos < tope)
            {
                fh.Seek(pos, SeekOrigin.Begin);
                int leido = fh.Read(buf, 0, buf.Length);
                if (leido <= 0)
                    break;

                int idx = Buscar(buf, leido, 0);
                while (idx != -1)
                {
                    long absOff = pos + idx;
                    if (absOff % Sector == 0 && absOff >= 32L * Sector)
                    {
                        long candidata = absOff - 32L * Sector;
                        if (MagicEn(fh, candidata))
                            return candidata;
                    }
                    idx = Buscar(buf, leido, idx + 1);
                }
                pos += chunk;
            }

            throw new InvalidOperationException(
                "No se encontro un sistema de archivos XDVDFS en la imagen.\n" +
                "Comprueba que es un ISO de Xbox 360 y no un CCI/GOD/ZAR comprimido.");
        }

        private static void LeerDescriptor(FileStream fh, long baseP, out uint sectorRaiz,
                                           out uint tamRaiz)
        {
            fh.Seek(baseP + 32L * Sector, SeekOrigin.Begin);
            byte[] vd = new byte[Sector];
            int leido = fh.Read(vd, 0, vd.Length);
            if (leido < Sector || !IgualPrefijo(vd, Magic))
                throw new InvalidOperationException("Descriptor de volumen invalido.");

            sectorRaiz = BitConverter.ToUInt32(vd, 0x14);
            tamRaiz = BitConverter.ToUInt32(vd, 0x18);
        }

        private static bool IgualPrefijo(byte[] datos, byte[] patron)
        {
            if (datos.Length < patron.Length)
                return false;
            for (int i = 0; i < patron.Length; i++)
            {
                if (datos[i] != patron[i])
                    return false;
            }
            return true;
        }

        // Nodos crudos del arbol binario de UN directorio (sin recorrer
        // subdirectorios: eso lo hace Recorrer). offsetInicial es 0, la raiz
        // del arbol de esta tabla.
        private static List<KeyValuePair<string, Entrada>> Entradas(byte[] tabla)
        {
            List<KeyValuePair<string, Entrada>> resultado = new List<KeyValuePair<string, Entrada>>();
            Stack<int> pila = new Stack<int>();
            HashSet<int> vistos = new HashSet<int>();
            pila.Push(0);

            while (pila.Count > 0)
            {
                int off = pila.Pop();
                if (vistos.Contains(off))
                    continue;
                if (off + 14 > tabla.Length)
                    continue;
                vistos.Add(off);

                ushort izq = BitConverter.ToUInt16(tabla, off);
                ushort der = BitConverter.ToUInt16(tabla, off + 2);
                uint sector = BitConverter.ToUInt32(tabla, off + 4);
                uint tamEntrada = BitConverter.ToUInt32(tabla, off + 8);
                byte attrs = tabla[off + 12];
                byte largo = tabla[off + 13];

                // 0 y 0xFFFF marcan "sin hijo" (offset 0 solo es valido para la
                // raiz, que ya se proceso al entrar aqui).
                if (izq != 0 && izq != 0xFFFF)
                    pila.Push(izq * 4);
                if (der != 0 && der != 0xFFFF)
                    pila.Push(der * 4);

                int finNombre = off + 14 + largo;
                if (largo == 0 || finNombre > tabla.Length)
                    continue;

                string nombre = Encoding.GetEncoding("ISO-8859-1").GetString(tabla, off + 14, largo);
                Entrada e = new Entrada();
                e.Nombre = nombre;
                e.Sector = sector;
                e.Tam = tamEntrada;
                e.Dir = (attrs & 0x10) != 0;
                resultado.Add(new KeyValuePair<string, Entrada>(nombre, e));
            }
            return resultado;
        }

        private static int CompararNombres(KeyValuePair<string, Entrada> a, KeyValuePair<string, Entrada> b)
        {
            return string.Compare(a.Key, b.Key, StringComparison.OrdinalIgnoreCase);
        }

        private static void Recorrer(FileStream fh, long baseP, uint sector, uint tam, string prefijo,
                                     List<KeyValuePair<string, Entrada>> salida)
        {
            if (tam == 0 || tam > (256 << 20))
                return;

            fh.Seek(baseP + (long)sector * Sector, SeekOrigin.Begin);
            byte[] tabla = new byte[tam];
            fh.Read(tabla, 0, tabla.Length);   // si viene corta, se sigue con lo leido

            List<KeyValuePair<string, Entrada>> hijos = Entradas(tabla);
            hijos.Sort(CompararNombres);

            foreach (KeyValuePair<string, Entrada> par in hijos)
            {
                string ruta = prefijo.Length > 0 ? prefijo + "/" + par.Key : par.Key;
                salida.Add(new KeyValuePair<string, Entrada>(ruta, par.Value));
                if (par.Value.Dir)
                    Recorrer(fh, baseP, par.Value.Sector, par.Value.Tam, ruta, salida);
            }
        }

        private static void ExtraerArchivo(FileStream fh, long baseP, Entrada e, string destino)
        {
            string dir = Path.GetDirectoryName(destino);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            fh.Seek(baseP + (long)e.Sector * Sector, SeekOrigin.Begin);
            long restante = e.Tam;
            byte[] buf = new byte[1 << 20];
            using (FileStream salida = new FileStream(destino, FileMode.Create, FileAccess.Write))
            {
                while (restante > 0)
                {
                    int aLeer = (int)Math.Min(buf.Length, restante);
                    int leido = fh.Read(buf, 0, aLeer);
                    if (leido <= 0)
                    {
                        throw new InvalidOperationException(
                            "Fin de archivo inesperado leyendo " + e.Nombre + ". Imagen incompleta?");
                    }
                    salida.Write(buf, 0, leido);
                    restante -= leido;
                }
            }
        }

        // Extrae TODO el contenido de la particion de juego a destino. El juego
        // necesita el arbol completo en tiempo de ejecucion -no solo el
        // default.xex-, porque game_data_root se monta como el propio D:\.
        public static void Extraer(string isoPath, string destino, Progreso progreso, Cancelado cancelado)
        {
            using (FileStream fh = new FileStream(isoPath, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                long baseP = DetectarBase(fh);
                uint sectorRaiz, tamRaiz;
                LeerDescriptor(fh, baseP, out sectorRaiz, out tamRaiz);

                List<KeyValuePair<string, Entrada>> entradas = new List<KeyValuePair<string, Entrada>>();
                Recorrer(fh, baseP, sectorRaiz, tamRaiz, "", entradas);

                if (entradas.Count == 0)
                {
                    throw new InvalidOperationException(
                        "El sistema de archivos esta vacio. Imagen corrupta?");
                }

                int total = 0;
                foreach (KeyValuePair<string, Entrada> par in entradas)
                {
                    if (!par.Value.Dir)
                        total++;
                }

                int hechos = 0;
                foreach (KeyValuePair<string, Entrada> par in entradas)
                {
                    if (cancelado != null && cancelado())
                        throw new OperationCanceledException();
                    if (par.Value.Dir)
                        continue;

                    string destinoArchivo = Path.Combine(destino,
                        par.Key.Replace('/', Path.DirectorySeparatorChar));
                    ExtraerArchivo(fh, baseP, par.Value, destinoArchivo);
                    hechos++;

                    if (progreso != null && (hechos % 10 == 0 || hechos == total))
                        progreso(par.Key, hechos, total);
                }

                if (!File.Exists(Path.Combine(destino, "default.xex")))
                {
                    throw new InvalidOperationException(
                        "Se extrajeron " + hechos + " archivos pero no aparecio default.xex " +
                        "en la raiz. Puede que la ISO no sea de Xbox 360, o que no sea " +
                        "Need for Speed: Most Wanted.");
                }
            }
        }

        // -----------------------------------------------------------------
        //  Cache: no volver a extraer la misma ISO
        //
        //  El marcador guarda ruta+tamano de la ISO de origen. Si coinciden y
        //  default.xex sigue ahi, se da la cache por buena. No hace falta mas
        //  precision -un hash del archivo entero seria mas fiable pero exige
        //  leer los mismos GB que se quieren evitar releer.
        // -----------------------------------------------------------------
        private static string RutaMarcador(string carpetaCache)
        {
            return Path.Combine(carpetaCache, ".origen_iso.txt");
        }

        public static bool CacheValida(string carpetaCache, string isoPath)
        {
            try
            {
                if (!File.Exists(Path.Combine(carpetaCache, "default.xex")))
                    return false;

                string marcador = RutaMarcador(carpetaCache);
                if (!File.Exists(marcador))
                    return false;

                string[] partes = File.ReadAllText(marcador, Encoding.UTF8).Split('|');
                if (partes.Length < 2)
                    return false;

                long tamGuardado;
                if (!long.TryParse(partes[1], out tamGuardado))
                    return false;

                FileInfo fi = new FileInfo(isoPath);
                return string.Equals(partes[0], Path.GetFullPath(isoPath),
                                     StringComparison.OrdinalIgnoreCase) &&
                       fi.Length == tamGuardado;
            }
            catch
            {
                return false;
            }
        }

        public static void EscribirMarcador(string carpetaCache, string isoPath)
        {
            try
            {
                FileInfo fi = new FileInfo(isoPath);
                File.WriteAllText(RutaMarcador(carpetaCache),
                    Path.GetFullPath(isoPath) + "|" + fi.Length, new UTF8Encoding(false));
            }
            catch
            {
                // Si no se puede escribir el marcador, la proxima vez se
                // vuelve a extraer. Lento, pero no rompe nada.
            }
        }

        public static string SanearNombre(string s)
        {
            StringBuilder sb = new StringBuilder();
            char[] invalidos = Path.GetInvalidFileNameChars();
            foreach (char c in s)
                sb.Append(Array.IndexOf(invalidos, c) >= 0 ? '_' : c);
            return sb.Length > 0 ? sb.ToString() : "iso";
        }
    }

    // ===========================================================================
    //  Ventana modal con el progreso de la extraccion
    //
    //  La extraccion corre en un hilo aparte -igual que el juego en Jugar()-
    //  para que la ventana no se quede "sin responder" mientras se copian
    //  varios GB. El resultado se lee de DialogResult (OK / Cancel) y, si algo
    //  fallo, del campo Error.
    // ===========================================================================
    internal sealed class VentanaExtraccion : Form
    {
        private readonly string iso;
        private readonly string destino;
        private readonly Label lbl;
        private readonly ProgressBar barra;
        private readonly Button btnCancelar;
        private volatile bool cancelar;
        private Exception error;

        public Exception Error { get { return error; } }

        public VentanaExtraccion(string iso, string destino)
        {
            this.iso = iso;
            this.destino = destino;

            Text = "Extrayendo la ISO...";
            ClientSize = new Size(460, 122);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false;
            MinimizeBox = false;
            ControlBox = false;
            Font = new Font("Segoe UI", 8.25f);
            BackColor = Tema.Fondo;

            lbl = new Label();
            lbl.Location = new Point(16, 14);
            lbl.Size = new Size(428, 44);
            lbl.ForeColor = Tema.Texto;
            lbl.Text = "Extrayendo " + Path.GetFileName(iso) + "...\n" +
                      "Solo hace falta la primera vez con esta ISO; puede tardar varios minutos.";
            Controls.Add(lbl);

            barra = new ProgressBar();
            barra.Location = new Point(16, 66);
            barra.Size = new Size(428, 20);
            barra.Style = ProgressBarStyle.Marquee;
            barra.MarqueeAnimationSpeed = 30;
            Controls.Add(barra);

            btnCancelar = new Button();
            btnCancelar.Text = "Cancelar";
            btnCancelar.Location = new Point(360, 92);
            btnCancelar.Size = new Size(84, 24);
            btnCancelar.BackColor = Tema.FondoPanel;
            btnCancelar.ForeColor = Tema.TextoTitulo;
            btnCancelar.FlatStyle = FlatStyle.Flat;
            btnCancelar.FlatAppearance.BorderColor = Tema.Borde;
            btnCancelar.Click += delegate
            {
                cancelar = true;
                btnCancelar.Enabled = false;
                lbl.Text = "Cancelando...";
            };
            Controls.Add(btnCancelar);

            Load += VentanaExtraccion_Load;
        }

        private void VentanaExtraccion_Load(object s, EventArgs e)
        {
            Thread hilo = new Thread(delegate ()
            {
                try
                {
                    if (Directory.Exists(destino))
                    {
                        try { Directory.Delete(destino, true); }
                        catch { /* restos de un intento anterior a medias; se pisan igual */ }
                    }
                    Directory.CreateDirectory(destino);

                    ExtractorXdvdfs.Extraer(iso, destino,
                        delegate (string archivo, int hechos, int total)
                        {
                            ActualizarProgreso(archivo, hechos, total);
                        },
                        delegate { return cancelar; });

                    ExtractorXdvdfs.EscribirMarcador(destino, iso);

                    TerminarEn(delegate { DialogResult = DialogResult.OK; Close(); });
                }
                catch (OperationCanceledException)
                {
                    TerminarEn(delegate { DialogResult = DialogResult.Cancel; Close(); });
                }
                catch (Exception ex)
                {
                    error = ex;
                    TerminarEn(delegate { DialogResult = DialogResult.Cancel; Close(); });
                }
            });
            hilo.IsBackground = true;
            hilo.Start();
        }

        private void ActualizarProgreso(string archivo, int hechos, int total)
        {
            try
            {
                if (IsDisposed || !IsHandleCreated)
                    return;
                Invoke((MethodInvoker)delegate
                {
                    if (IsDisposed)
                        return;
                    if (barra.Style != ProgressBarStyle.Continuous && total > 0)
                    {
                        barra.Style = ProgressBarStyle.Continuous;
                        barra.Minimum = 0;
                        barra.Maximum = total;
                    }
                    if (total > 0)
                        barra.Value = Math.Min(hechos, total);
                    lbl.Text = string.Format("Extrayendo {0}/{1}: {2}", hechos, total, archivo);
                });
            }
            catch (ObjectDisposedException) { }
            catch (InvalidOperationException) { }
        }

        private void TerminarEn(MethodInvoker que)
        {
            try
            {
                if (IsDisposed || !IsHandleCreated)
                    return;
                Invoke(que);
            }
            catch (ObjectDisposedException) { }
            catch (InvalidOperationException) { }
        }
    }
}

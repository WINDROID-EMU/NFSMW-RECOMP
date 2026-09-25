package com.ea.nfsmw;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.Settings;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.Animation;
import android.view.animation.AnimationUtils;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class TitleActivity extends Activity {

    private static final String PREFS_NAME = "NFSMW_PREFS";
    private static final String KEY_ROM_PATH = "game_rom_path";
    private static final String KEY_RESOLUTION = "cfg_resolution";
    private static final String KEY_RESOLUTION_SCALE = "cfg_resolution_scale";
    private static final String KEY_VSYNC = "cfg_vsync";
    private static final String KEY_STRETCH_SCREEN = "cfg_stretch_screen";
    private static final String KEY_EDRAM_PATH = "cfg_edram_path";
    private static final String KEY_PIPELINE_THREADS = "cfg_pipeline_threads";
    private static final String KEY_ASYNC_SHADERS = "cfg_async_shaders";
    private static final String KEY_ASYNC_SKIP = "cfg_async_skip";
    private static final String KEY_TEXTURE_CACHE_LIMIT = "cfg_texture_cache_limit";
    private static final String KEY_GPU_3D_TO_2D = "cfg_gpu_3d_to_2d";
    private static final String KEY_READBACK_RESOLVE = "cfg_readback_resolve";
    private static final String KEY_ANISOTROPIC = "cfg_anisotropic";
    private static final String KEY_ANTIALIASING = "cfg_antialiasing";
    private static final String KEY_NATIVE_MSAA = "cfg_native_msaa";
    private static final String KEY_AUDIO_MUTE = "cfg_audio_mute";
    private static final String KEY_MNK_MODE = "cfg_mnk_mode";
    private static final String KEY_BLACK_EDITION = "cfg_black_edition";
    private static final String KEY_GRANT_PRIVILEGES = "cfg_grant_privileges";
    private static final String KEY_GAMERTAG = "cfg_gamertag";
    private static final String KEY_LOG_LEVEL = "cfg_log_level";
    private static final String KEY_GAME_SPEED = "cfg_game_speed";

    private static final int REQ_CODE_FOLDER          = 1001;
    private static final int REQ_CODE_ISO              = 1002;
    private static final int REQ_CODE_MANAGE_STORAGE   = 1003;
    private static final int REQ_CODE_TURNIP_ZIP       = 1004;

    private static final String TURNIP_SUBDIR   = "turnip";
    private static final String TURNIP_FILENAME = "libvulkan_freedreno.so";

    // Held during the ZIP-picker flow so we can update the status text after install
    private TextView pendingTurnipStatusView = null;

    private View rootLayout;
    private View startPromptContainer;
    private TextView tvPressStart;
    private TextView tvRomStatus;
    private Button btnSelectRom;
    private Button btnSettings;
    private View fadeOverlay;

    private String verifiedGamePath = null;
    private boolean isStartingGame = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        configureFullscreen();
        setContentView(R.layout.activity_title);

        rootLayout = findViewById(R.id.title_root);
        startPromptContainer = findViewById(R.id.start_prompt_container);
        tvPressStart = findViewById(R.id.tv_press_start);
        tvRomStatus = findViewById(R.id.tv_rom_status);
        btnSelectRom = findViewById(R.id.btn_select_rom);
        btnSettings = findViewById(R.id.btn_settings);
        fadeOverlay = findViewById(R.id.fade_overlay);

        if (btnSettings != null) {
            btnSettings.setOnClickListener(v -> showSettingsDialog());
        }

        // Inicia animação pulsante no botão START
        Animation pulseAnim = AnimationUtils.loadAnimation(this, R.anim.pulse_glow);
        if (startPromptContainer != null && pulseAnim != null) {
            startPromptContainer.startAnimation(pulseAnim);
        }

        // Botão para selecionar ROM / Pasta
        btnSelectRom.setOnClickListener(v -> showPickerSelectionDialog());

        // Toque na tela para iniciar o jogo
        if (rootLayout != null) {
            rootLayout.setOnTouchListener((v, event) -> {
                if (event.getAction() == MotionEvent.ACTION_UP) {
                    onAttemptStartGame();
                    return true;
                }
                return true;
            });
        }

        // Verifica permissões e caminhos salvos
        checkStoragePermissions();
        checkAndLoadGamePath();
        writeTomlConfiguration(true);
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    private void configureFullscreen() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
    }

    private void applyImmersiveMode() {
        View decorView = getWindow().getDecorView();
        int uiOptions = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
        decorView.setSystemUiVisibility(uiOptions);
    }

    private void checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivityForResult(intent, REQ_CODE_MANAGE_STORAGE);
                } catch (Exception e) {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivityForResult(intent, REQ_CODE_MANAGE_STORAGE);
                }
            }
        }
    }

    /**
     * Verifica se os arquivos necessários do jogo estão presentes na pasta ou arquivo alvo
     */
    private boolean verifyGameFiles(File target) {
        if (target == null || !target.exists()) {
            return false;
        }

        // Se for um arquivo único, verifica se é uma ISO ou XEX
        if (target.isFile()) {
            String name = target.getName().toLowerCase();
            return name.endsWith(".iso") || name.equals("default.xex");
        }

        // Se for diretório:
        if (target.isDirectory()) {
            // 1. Verifica se default.xex está na raiz selecionada
            if (new File(target, "default.xex").exists() ||
                new File(target, "DEFAULT.XEX").exists()) {
                return true;
            }

            // 2. Verifica se existe a pasta game_root interna com default.xex
            File subRoot = new File(target, "game_root");
            if (subRoot.isDirectory()) {
                if (new File(subRoot, "default.xex").exists() ||
                    new File(subRoot, "DEFAULT.XEX").exists()) {
                    return true;
                }
                return true;
            }

            // 3. Verifica se existe arquivo .iso dentro da pasta
            File[] files = target.listFiles();
            if (files != null) {
                for (File f : files) {
                    if (f.isFile() && f.getName().toLowerCase().endsWith(".iso")) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    /**
     * Carrega e valida o caminho salvo anteriormente ou busca nos locais padrão
     */
    private void checkAndLoadGamePath() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String saved = prefs.getString(KEY_ROM_PATH, null);

        // 1. Testa o caminho previamente salvo pelo usuário
        if (saved != null) {
            File savedFile = new File(saved);
            if (verifyGameFiles(savedFile)) {
                applyVerifiedGamePath(savedFile.getAbsolutePath(), false);
                return;
            }
        }

        // 2. Busca automática em diretórios padrão comuns no Android
        String[] defaultLocations = new String[] {
                "/sdcard/NFSMW/game_root",
                "/sdcard/NFSMW",
                "/sdcard/Download/NFSMW",
                "/storage/emulated/0/NFSMW/game_root",
                "/storage/emulated/0/NFSMW",
                getExternalFilesDir(null) != null ? new File(getExternalFilesDir(null), "game_root").getAbsolutePath() : null
        };

        for (String loc : defaultLocations) {
            if (loc != null) {
                File candidate = new File(loc);
                if (verifyGameFiles(candidate)) {
                    applyVerifiedGamePath(candidate.getAbsolutePath(), true);
                    return;
                }
            }
        }

        // Caso ainda não tenha sido configurado
        tvRomStatus.setText(R.string.status_no_rom);
        tvRomStatus.setTextColor(getResources().getColor(R.color.nfsmw_orange));
        btnSelectRom.setText(R.string.btn_select_rom);
    }

    private void applyVerifiedGamePath(String path, boolean saveToPrefs) {
        verifiedGamePath = path;

        if (saveToPrefs) {
            SharedPreferences prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            prefs.edit().putString(KEY_ROM_PATH, path).apply();
        }

        // Salva para consumo imediato do C++ / AndroidStorage
        savePathForNativeBackend(path);

        File f = new File(path);
        String displayName = f.getName();
        if (displayName.isEmpty()) {
            displayName = path;
        }

        tvRomStatus.setText(String.format(getString(R.string.status_rom_verified), displayName));
        tvRomStatus.setTextColor(getResources().getColor(R.color.nfsmw_green));
        btnSelectRom.setText(R.string.btn_change_rom);
    }

    private void savePathForNativeBackend(String path) {
        try {
            // 1. Salva no armazenamento externo de arquivos do app
            File extDir = getExternalFilesDir(null);
            if (extDir != null) {
                File cfgFile = new File(extDir, "selected_game_path.txt");
                try (OutputStreamWriter writer = new OutputStreamWriter(new FileOutputStream(cfgFile), StandardCharsets.UTF_8)) {
                    writer.write(path);
                }
            }

            // 2. Salva também no armazenamento interno como redundância
            File intDir = getFilesDir();
            if (intDir != null) {
                File cfgFile = new File(intDir, "selected_game_path.txt");
                try (OutputStreamWriter writer = new OutputStreamWriter(new FileOutputStream(cfgFile), StandardCharsets.UTF_8)) {
                    writer.write(path);
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void showPickerSelectionDialog() {
        String[] options = new String[] {
                getString(R.string.dialog_choose_folder),
                getString(R.string.dialog_choose_iso)
        };

        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(R.string.dialog_choose_type_title)
                .setItems(options, (dialog, which) -> {
                    if (which == 0) {
                        launchFolderPicker();
                    } else {
                        launchIsoPicker();
                    }
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void launchFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQ_CODE_FOLDER);
    }

    private void launchIsoPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQ_CODE_ISO);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == REQ_CODE_MANAGE_STORAGE) {
            checkAndLoadGamePath();
            return;
        }

        if (resultCode != RESULT_OK || data == null) {
            return;
        }

        Uri uri = data.getData();
        if (uri == null) {
            return;
        }

        // ── Turnip ZIP picker result ──────────────────────────────────────────
        if (requestCode == REQ_CODE_TURNIP_ZIP) {
            try (InputStream is = getContentResolver().openInputStream(uri)) {
                if (is != null) {
                    extractTurnipFromStream(is, pendingTurnipStatusView);
                } else {
                    Toast.makeText(this,
                        "Não foi possível abrir o arquivo ZIP.",
                        Toast.LENGTH_SHORT).show();
                }
            } catch (Exception e) {
                android.util.Log.e("NFS-Turnip", "Failed to open ZIP URI", e);
                Toast.makeText(this,
                    "Erro ao abrir ZIP: " + e.getMessage(),
                    Toast.LENGTH_LONG).show();
            }
            pendingTurnipStatusView = null;
            return;
        }

        try {
            getContentResolver().takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION
            );
        } catch (Exception ignored) {}


        String resolvedPath = resolveRealPathFromUri(uri, requestCode == REQ_CODE_FOLDER);
        if (resolvedPath != null) {
            File target = new File(resolvedPath);
            if (verifyGameFiles(target)) {
                applyVerifiedGamePath(resolvedPath, true);
                Toast.makeText(this, "✔ Arquivos do jogo verificados com sucesso!", Toast.LENGTH_SHORT).show();
            } else {
                new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                        .setTitle("Arquivos Inválidos")
                        .setMessage(R.string.error_files_missing)
                        .setPositiveButton("OK", null)
                        .show();
            }
        } else {
            Toast.makeText(this, "Não foi possível resolver o caminho da pasta selecionada.", Toast.LENGTH_LONG).show();
        }
    }

    /**
     * Converte URIs de DocumentTree e Document em caminhos absolutos do sistema de arquivos
     */
    private String resolveRealPathFromUri(Uri uri, boolean isTree) {
        try {
            String path = uri.getPath();
            if (isTree) {
                String treeDocId = DocumentsContract.getTreeDocumentId(uri);
                if (treeDocId != null) {
                    if (treeDocId.startsWith("primary:")) {
                        return Environment.getExternalStorageDirectory().getAbsolutePath() + "/" + treeDocId.substring("primary:".length());
                    } else if (treeDocId.contains(":")) {
                        String[] parts = treeDocId.split(":", 2);
                        return "/storage/" + parts[0] + "/" + parts[1];
                    }
                }
            } else {
                if (DocumentsContract.isDocumentUri(this, uri)) {
                    String docId = DocumentsContract.getDocumentId(uri);
                    if (docId != null && docId.startsWith("primary:")) {
                        return Environment.getExternalStorageDirectory().getAbsolutePath() + "/" + docId.substring("primary:".length());
                    } else if (docId != null && docId.contains(":")) {
                        String[] parts = docId.split(":", 2);
                        return "/storage/" + parts[0] + "/" + parts[1];
                    }
                }
            }

            // Fallback: busca via content resolver
            if ("content".equalsIgnoreCase(uri.getScheme())) {
                String[] projection = { MediaStore.MediaColumns.DATA };
                try (Cursor cursor = getContentResolver().query(uri, projection, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst()) {
                        int index = cursor.getColumnIndex(MediaStore.MediaColumns.DATA);
                        if (index >= 0) {
                            return cursor.getString(index);
                        }
                    }
                }
            }

            if (path != null && path.startsWith("/tree/primary:")) {
                return Environment.getExternalStorageDirectory().getAbsolutePath() + "/" + path.substring("/tree/primary:".length());
            }

            return path;
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BUTTON_START ||
            keyCode == KeyEvent.KEYCODE_BUTTON_A ||
            keyCode == KeyEvent.KEYCODE_ENTER ||
            keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
            keyCode == KeyEvent.KEYCODE_SPACE) {
            onAttemptStartGame();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private void onAttemptStartGame() {
        if (isStartingGame) {
            return;
        }

        // Se o jogo ainda não foi verificado, orienta o usuário a selecionar
        if (verifiedGamePath == null) {
            Toast.makeText(this, "Selecione o local da ROM ou pasta do jogo antes de iniciar.", Toast.LENGTH_SHORT).show();
            showPickerSelectionDialog();
            return;
        }

        startGame();
    }

    private synchronized void startGame() {
        if (isStartingGame) {
            return;
        }
        isStartingGame = true;

        if (tvPressStart != null) {
            tvPressStart.setText(R.string.title_starting);
        }

        if (startPromptContainer != null) {
            startPromptContainer.clearAnimation();
            startPromptContainer.setScaleX(1.1f);
            startPromptContainer.setScaleY(1.1f);
        }

        if (fadeOverlay != null) {
            fadeOverlay.setVisibility(View.VISIBLE);
            fadeOverlay.animate()
                    .alpha(1.0f)
                    .setDuration(450)
                    .withEndAction(this::launchNativeGame)
                    .start();
        } else {
            launchNativeGame();
        }
    }

    private void launchNativeGame() {
        try {
            // Garante que as configurações estejam salvas antes de iniciar a NativeActivity
            writeTomlConfiguration(true);
            extractBundledShaders(getExternalFilesDir(null));

            Intent intent = new Intent(this, GameActivity.class);
            if (verifiedGamePath != null) {
                intent.putExtra("selected_game_path", verifiedGamePath);
            }
            intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            startActivity(intent);
            overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out);
            finish();
        } catch (Exception e) {
            e.printStackTrace();
            isStartingGame = false;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Turnip driver helpers
    // ─────────────────────────────────────────────────────────────────────────

    /** Returns the directory where the Turnip driver SO lives. */
    private File getTurnipDir() {
        return new File(getFilesDir(), TURNIP_SUBDIR);
    }

    /** Returns the Turnip driver SO file (may or may not exist). */
    private File getTurnipSo() {
        return new File(getTurnipDir(), TURNIP_FILENAME);
    }

    /** Updates the Turnip status TextView to reflect what is currently installed. */
    private void refreshTurnipStatus(TextView statusView) {
        if (statusView == null) return;
        File so = getTurnipSo();
        if (so.exists() && so.length() > 0) {
            long kb = so.length() / 1024;
            statusView.setText("✅ Driver instalado: " + TURNIP_FILENAME + " (" + kb + " KB)");
            statusView.setTextColor(Color.parseColor("#4CAF50"));
        } else {
            statusView.setText("● Driver: usando sistema (padrão — sem Turnip)");
            statusView.setTextColor(Color.parseColor("#A0A0A0"));
        }
    }

    /**
     * Extracts the first .so entry found inside a ZIP stream and writes it
     * to internalDataPath/turnip/libvulkan_freedreno.so.
     * Returns true on success.
     */
    private boolean extractTurnipFromStream(InputStream inputStream, TextView statusView) {
        File dir = getTurnipDir();
        if (!dir.exists() && !dir.mkdirs()) {
            Toast.makeText(this, "Falha ao criar pasta turnip/", Toast.LENGTH_SHORT).show();
            return false;
        }
        File dest = getTurnipSo();
        try (ZipInputStream zis = new ZipInputStream(inputStream)) {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                String name = entry.getName();
                // Accept any .so inside the ZIP, rename it to our target name
                if (!entry.isDirectory() && name.endsWith(".so")) {
                    android.util.Log.i("NFS-Turnip",
                        "Extracting '" + name + "' -> " + dest.getAbsolutePath());
                    try (FileOutputStream fos = new FileOutputStream(dest)) {
                        byte[] buf = new byte[65536];
                        int read;
                        while ((read = zis.read(buf)) != -1) {
                            fos.write(buf, 0, read);
                        }
                    }
                    zis.closeEntry();
                    refreshTurnipStatus(statusView);
                    Toast.makeText(this,
                        "✅ Driver Turnip instalado! Reinicie o jogo.",
                        Toast.LENGTH_LONG).show();
                    return true;
                }
                zis.closeEntry();
            }
        } catch (Exception e) {
            android.util.Log.e("NFS-Turnip", "ZIP extraction failed", e);
            Toast.makeText(this,
                "Erro ao extrair o ZIP: " + e.getMessage(),
                Toast.LENGTH_LONG).show();
            return false;
        }
        Toast.makeText(this,
            "Nenhum arquivo .so encontrado no ZIP selecionado.",
            Toast.LENGTH_LONG).show();
        return false;
    }

    /** Deletes the currently installed Turnip driver. */
    private void removeTurnipDriver(TextView statusView) {
        File so = getTurnipSo();
        if (!so.exists()) {
            Toast.makeText(this, "Nenhum driver Turnip instalado.", Toast.LENGTH_SHORT).show();
            return;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("Remover Driver Turnip")
            .setMessage("Tem certeza? O driver de sistema (padrão) será usado na próxima execução.")
            .setPositiveButton("Remover", (d, w) -> {
                if (so.delete()) {
                    refreshTurnipStatus(statusView);
                    Toast.makeText(this,
                        "Driver Turnip removido. Reinicie o jogo.",
                        Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(this,
                        "Falha ao remover o driver.",
                        Toast.LENGTH_SHORT).show();
                }
            })
            .setNegativeButton("Cancelar", null)
            .show();
    }

    private void showSettingsDialog() {
        View dialogView = getLayoutInflater().inflate(R.layout.dialog_settings, null);
        AlertDialog dialog = new AlertDialog.Builder(this, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
                .setView(dialogView)
                .create();

        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));
        }

        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);

        // Spinners
        Spinner spResolution = dialogView.findViewById(R.id.sp_resolution);
        Spinner spResolutionScale = dialogView.findViewById(R.id.sp_resolution_scale);
        Spinner spEdramPath = dialogView.findViewById(R.id.sp_edram_path);
        Spinner spPipelineThreads = dialogView.findViewById(R.id.sp_pipeline_threads);
        Spinner spTextureCacheLimit = dialogView.findViewById(R.id.sp_texture_cache_limit);
        Spinner spAnisotropic = dialogView.findViewById(R.id.sp_anisotropic);
        Spinner spAntialiasing = dialogView.findViewById(R.id.sp_antialiasing);
        Spinner spLogLevel = dialogView.findViewById(R.id.sp_log_level);

        // Switches
        Switch swVsync = dialogView.findViewById(R.id.sw_vsync);
        Switch swStretchScreen = dialogView.findViewById(R.id.sw_stretch_screen);
        Switch swAsyncShaders = dialogView.findViewById(R.id.sw_async_shaders);
        Switch swAsyncSkip = dialogView.findViewById(R.id.sw_async_skip);
        Switch swGpu3dTo2d = dialogView.findViewById(R.id.sw_gpu_3d_to_2d);
        Switch swReadbackResolve = dialogView.findViewById(R.id.sw_readback_resolve);
        Switch swNativeMsaa = dialogView.findViewById(R.id.sw_native_msaa);
        Switch swAudioMute = dialogView.findViewById(R.id.sw_audio_mute);
        Switch swMnkMode = dialogView.findViewById(R.id.sw_mnk_mode);
        Switch swBlackEdition = dialogView.findViewById(R.id.sw_black_edition);
        Switch swGrantPrivileges = dialogView.findViewById(R.id.sw_grant_privileges);

        // SeekBars & Text
        SeekBar sbGameSpeed = dialogView.findViewById(R.id.sb_game_speed);
        TextView tvGameSpeed = dialogView.findViewById(R.id.tv_speed_label);
        EditText etGamertag = dialogView.findViewById(R.id.et_gamertag);

        // Options arrays
        final String[] resLabels = {"720p (1280x720 - Nativo Xbox 360)", "540p (960x540 - Modo Performance / +44% FPS)", "480p (854x480 - Mais Leve)", "1080p (1920x1080 - Full HD)"};
        final String[] resValues = {"720p", "540p", "480p", "1080p"};

        final String[] scaleLabels = {"1x - Nativo (Mais rápido)", "2x - 1440p (Alta Nitidez)", "3x - 4K"};
        final int[] scaleValues = {1, 2, 3};

        final String[] edramLabels = {"rtv (FBO Host - Rápido / Recomendado)", "rov (Pixel Shader Interlock - Lento)"};
        final String[] edramValues = {"rtv", "rov"};

        final String[] threadLabels = {"1 Thread (Economia de Bateria)", "2 Threads (Recomendado - Frio & Estável)", "4 Threads (Compilação Rápida)", "Automático (-1)"};
        final int[] threadValues = {1, 2, 4, -1};

        final String[] cacheLabels = {"256 MB (Celulares 4GB RAM - Evita OOM)", "384 MB (Equilibrado)", "512 MB (Recomendado / Padrão)", "768 MB (Celulares 8GB+ RAM)"};
        final int[] cacheValues = {256, 384, 512, 768};

        final String[] anisoLabels = {"Desativado (0x)", "1x", "2x (Recomendado)", "4x", "8x", "16x"};
        final int[] anisoValues = {0, 1, 2, 4, 8, 16};

        final String[] aaLabels = {"Desativado (none)", "FXAA (Recomendado)", "FXAA Extreme"};
        final String[] aaValues = {"none", "fxaa", "fxaa_extreme"};

        final String[] logLabels = {"Erro (Desempenho Máximo / Menos I/O)", "Aviso (Warning)", "Informação (Info - Padrão)", "Depuração (Debug)"};
        final String[] logValues = {"error", "warning", "info", "debug"};

        // Set adapters
        setupSpinner(spResolution, resLabels);
        setupSpinner(spResolutionScale, scaleLabels);
        setupSpinner(spEdramPath, edramLabels);
        setupSpinner(spPipelineThreads, threadLabels);
        setupSpinner(spTextureCacheLimit, cacheLabels);
        setupSpinner(spAnisotropic, anisoLabels);
        setupSpinner(spAntialiasing, aaLabels);
        setupSpinner(spLogLevel, logLabels);

        // Load saved values
        String curRes = prefs.getString(KEY_RESOLUTION, "720p");
        spResolution.setSelection(findStringIndex(resValues, curRes, 0));

        int curScale = prefs.getInt(KEY_RESOLUTION_SCALE, 1);
        spResolutionScale.setSelection(findIntIndex(scaleValues, curScale, 0));

        String curEdram = prefs.getString(KEY_EDRAM_PATH, "rtv");
        spEdramPath.setSelection(findStringIndex(edramValues, curEdram, 0));

        int curThreads = prefs.getInt(KEY_PIPELINE_THREADS, 2);
        spPipelineThreads.setSelection(findIntIndex(threadValues, curThreads, 1));

        int curCache = prefs.getInt(KEY_TEXTURE_CACHE_LIMIT, 256);
        spTextureCacheLimit.setSelection(findIntIndex(cacheValues, curCache, 0));

        int curAniso = prefs.getInt(KEY_ANISOTROPIC, 1);
        spAnisotropic.setSelection(findIntIndex(anisoValues, curAniso, 1));

        String curAa = prefs.getString(KEY_ANTIALIASING, "none");
        spAntialiasing.setSelection(findStringIndex(aaValues, curAa, 0));

        String curLog = prefs.getString(KEY_LOG_LEVEL, "info");
        spLogLevel.setSelection(findStringIndex(logValues, curLog, 2));

        swVsync.setChecked(prefs.getBoolean(KEY_VSYNC, true));
        swStretchScreen.setChecked(prefs.getBoolean(KEY_STRETCH_SCREEN, true));
        swAsyncShaders.setChecked(prefs.getBoolean(KEY_ASYNC_SHADERS, true));
        swAsyncSkip.setChecked(prefs.getBoolean(KEY_ASYNC_SKIP, false));
        swGpu3dTo2d.setChecked(prefs.getBoolean(KEY_GPU_3D_TO_2D, true));
        swReadbackResolve.setChecked(prefs.getBoolean(KEY_READBACK_RESOLVE, false));
        swNativeMsaa.setChecked(prefs.getBoolean(KEY_NATIVE_MSAA, false));
        swAudioMute.setChecked(prefs.getBoolean(KEY_AUDIO_MUTE, false));
        swMnkMode.setChecked(prefs.getBoolean(KEY_MNK_MODE, false));
        swBlackEdition.setChecked(prefs.getBoolean(KEY_BLACK_EDITION, true));
        swGrantPrivileges.setChecked(prefs.getBoolean(KEY_GRANT_PRIVILEGES, false));

        int curSpeed = prefs.getInt(KEY_GAME_SPEED, 100);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            sbGameSpeed.setMin(20);
        }
        sbGameSpeed.setMax(200);
        sbGameSpeed.setProgress(curSpeed);
        tvGameSpeed.setText("Velocidade da Simulação: " + curSpeed + "%");
        sbGameSpeed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int val = Math.max(20, progress);
                tvGameSpeed.setText("Velocidade da Simulação: " + val + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        etGamertag.setText(prefs.getString(KEY_GAMERTAG, "Player"));

        // Turnip driver section
        TextView tvTurnipStatus = dialogView.findViewById(R.id.tv_turnip_status);
        Button btnInstallTurnip = dialogView.findViewById(R.id.btn_install_turnip);
        Button btnRemoveTurnip  = dialogView.findViewById(R.id.btn_remove_turnip);
        refreshTurnipStatus(tvTurnipStatus);
        if (btnInstallTurnip != null) {
            btnInstallTurnip.setOnClickListener(v -> {
                pendingTurnipStatusView = tvTurnipStatus;
                Intent zipIntent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                zipIntent.addCategory(Intent.CATEGORY_OPENABLE);
                zipIntent.setType("*/*");
                zipIntent.putExtra(Intent.EXTRA_MIME_TYPES,
                    new String[]{"application/zip", "application/x-zip-compressed",
                                 "application/octet-stream", "*/*"});
                startActivityForResult(zipIntent, REQ_CODE_TURNIP_ZIP);
            });
        }
        if (btnRemoveTurnip != null) {
            btnRemoveTurnip.setOnClickListener(v -> removeTurnipDriver(tvTurnipStatus));
        }

        // Close button
        Button btnClose = dialogView.findViewById(R.id.btn_close_settings);
        if (btnClose != null) {
            btnClose.setOnClickListener(v -> dialog.dismiss());
        }

        // Reset button
        Button btnReset = dialogView.findViewById(R.id.btn_reset_defaults);
        if (btnReset != null) {
            btnReset.setOnClickListener(v -> {
                spResolution.setSelection(2);
                spResolutionScale.setSelection(0);
                spEdramPath.setSelection(0);
                spPipelineThreads.setSelection(1);
                spTextureCacheLimit.setSelection(2);
                spAnisotropic.setSelection(2);
                spAntialiasing.setSelection(0);
                spLogLevel.setSelection(2);
                swVsync.setChecked(true);
                swStretchScreen.setChecked(true);
                swAsyncShaders.setChecked(true);
                swAsyncSkip.setChecked(false);
                swGpu3dTo2d.setChecked(true);
                swReadbackResolve.setChecked(false);
                swNativeMsaa.setChecked(false);
                swAudioMute.setChecked(false);
                swMnkMode.setChecked(false);
                swBlackEdition.setChecked(true);
                swGrantPrivileges.setChecked(false);
                sbGameSpeed.setProgress(100);
                etGamertag.setText("Player");
                Toast.makeText(TitleActivity.this, "Padrões otimizados para Android restaurados.", Toast.LENGTH_SHORT).show();
            });
        }

        // Save button
        Button btnSave = dialogView.findViewById(R.id.btn_save_settings);
        if (btnSave != null) {
            btnSave.setOnClickListener(v -> {
                String selRes = resValues[spResolution.getSelectedItemPosition()];
                int selScale = scaleValues[spResolutionScale.getSelectedItemPosition()];
                String selEdram = edramValues[spEdramPath.getSelectedItemPosition()];
                int selThreads = threadValues[spPipelineThreads.getSelectedItemPosition()];
                int selCache = cacheValues[spTextureCacheLimit.getSelectedItemPosition()];
                int selAniso = anisoValues[spAnisotropic.getSelectedItemPosition()];
                String selAa = aaValues[spAntialiasing.getSelectedItemPosition()];
                String selLog = logValues[spLogLevel.getSelectedItemPosition()];

                boolean selVsync = swVsync.isChecked();
                boolean selStretchScreen = swStretchScreen.isChecked();
                boolean selAsyncShaders = swAsyncShaders.isChecked();
                boolean selAsyncSkip = swAsyncSkip.isChecked();
                boolean selGpu3dTo2d = swGpu3dTo2d.isChecked();
                boolean selReadback = swReadbackResolve.isChecked();
                boolean selNativeMsaa = swNativeMsaa.isChecked();
                boolean selAudioMute = swAudioMute.isChecked();
                boolean selMnk = swMnkMode.isChecked();
                boolean selBlack = swBlackEdition.isChecked();
                boolean selGrant = swGrantPrivileges.isChecked();
                int selSpeed = Math.max(20, sbGameSpeed.getProgress());
                String selGamertag = etGamertag.getText().toString().trim();
                if (selGamertag.isEmpty()) selGamertag = "Player";

                prefs.edit()
                        .putString(KEY_RESOLUTION, selRes)
                        .putInt(KEY_RESOLUTION_SCALE, selScale)
                        .putString(KEY_EDRAM_PATH, selEdram)
                        .putInt(KEY_PIPELINE_THREADS, selThreads)
                        .putInt(KEY_TEXTURE_CACHE_LIMIT, selCache)
                        .putInt(KEY_ANISOTROPIC, selAniso)
                        .putString(KEY_ANTIALIASING, selAa)
                        .putString(KEY_LOG_LEVEL, selLog)
                        .putBoolean(KEY_VSYNC, selVsync)
                        .putBoolean(KEY_STRETCH_SCREEN, selStretchScreen)
                        .putBoolean(KEY_ASYNC_SHADERS, selAsyncShaders)
                        .putBoolean(KEY_ASYNC_SKIP, selAsyncSkip)
                        .putBoolean(KEY_GPU_3D_TO_2D, selGpu3dTo2d)
                        .putBoolean(KEY_READBACK_RESOLVE, selReadback)
                        .putBoolean(KEY_NATIVE_MSAA, selNativeMsaa)
                        .putBoolean(KEY_AUDIO_MUTE, selAudioMute)
                        .putBoolean(KEY_MNK_MODE, selMnk)
                        .putBoolean(KEY_BLACK_EDITION, selBlack)
                        .putBoolean(KEY_GRANT_PRIVILEGES, selGrant)
                        .putInt(KEY_GAME_SPEED, selSpeed)
                        .putString(KEY_GAMERTAG, selGamertag)
                        .apply();

                writeTomlConfiguration(true);
                Toast.makeText(TitleActivity.this, "Configurações salvas e aplicadas ao jogo!", Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            });
        }

        dialog.show();
    }

    private void writeTomlConfiguration(boolean forceOverwrite) {
        try {
            SharedPreferences prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);

            String res = prefs.getString(KEY_RESOLUTION, "720p");
            int scale = prefs.getInt(KEY_RESOLUTION_SCALE, 1);
            boolean vsync = prefs.getBoolean(KEY_VSYNC, true);
            boolean stretchScreen = prefs.getBoolean(KEY_STRETCH_SCREEN, true);
            String edram = prefs.getString(KEY_EDRAM_PATH, "rtv");
            boolean asyncShaders = prefs.getBoolean(KEY_ASYNC_SHADERS, true);
            boolean asyncSkip = prefs.getBoolean(KEY_ASYNC_SKIP, false);
            int pipelineThreads = prefs.getInt(KEY_PIPELINE_THREADS, 2);
            int textureCacheLimit = prefs.getInt(KEY_TEXTURE_CACHE_LIMIT, 256);
            boolean gpu3dTo2d = prefs.getBoolean(KEY_GPU_3D_TO_2D, true);
            boolean readback = prefs.getBoolean(KEY_READBACK_RESOLVE, false);
            int aniso = prefs.getInt(KEY_ANISOTROPIC, 1);
            String aa = prefs.getString(KEY_ANTIALIASING, "none");
            boolean nativeMsaa = prefs.getBoolean(KEY_NATIVE_MSAA, false);
            boolean mute = prefs.getBoolean(KEY_AUDIO_MUTE, false);
            boolean mnk = prefs.getBoolean(KEY_MNK_MODE, false);
            boolean black = prefs.getBoolean(KEY_BLACK_EDITION, true);
            boolean grant = prefs.getBoolean(KEY_GRANT_PRIVILEGES, false);
            String gamertag = prefs.getString(KEY_GAMERTAG, "Player");
            String logLevel = prefs.getString(KEY_LOG_LEVEL, "info");
            int speed = prefs.getInt(KEY_GAME_SPEED, 100);

            int width = 1280;
            int height = 720;
            if ("480p".equalsIgnoreCase(res)) {
                width = 854; height = 480;
            } else if ("540p".equalsIgnoreCase(res)) {
                width = 960; height = 540;
            } else if ("1080p".equalsIgnoreCase(res)) {
                width = 1920; height = 1080;
            }

            StringBuilder toml = new StringBuilder();
            toml.append("# ============================================================================").append((char) 10);
            toml.append("#  NFSMW Recompiled - Android Configuration (Gerado pelo TitleActivity)").append((char) 10);
            toml.append("# ============================================================================").append((char) 10).append((char) 10);

            String vulkanEdram = "rov".equalsIgnoreCase(edram) || "fsi".equalsIgnoreCase(edram) ? "fsi" : "fbo";
            toml.append("render_target_path_d3d12 = ").append((char) 34).append(edram).append((char) 34).append((char) 10);
            toml.append("render_target_path_vulkan = ").append((char) 34).append(vulkanEdram).append((char) 34).append((char) 10);
            toml.append("vulkan_async_skip_incomplete_frames = ").append(asyncSkip ? "true" : "false").append((char) 10);
            toml.append("vulkan_submit_on_primary_buffer_end = false").append((char) 10);
            toml.append("vulkan_dynamic_rendering = true").append((char) 10);
            toml.append("gpu_backend = ").append((char) 34).append("vulkan").append((char) 34).append((char) 10);
            toml.append("gpu = ").append((char) 34).append("xenos").append((char) 34).append((char) 10).append((char) 10);

            toml.append("video_mode_width = ").append(width).append((char) 10);
            toml.append("video_mode_height = ").append(height).append((char) 10);
            toml.append("resolution = ").append((char) 34).append(res).append((char) 34).append((char) 10);
            toml.append("resolution_scale = ").append(scale).append((char) 10);
            toml.append("vsync = ").append(vsync ? "true" : "false").append((char) 10);
            toml.append("fullscreen = true").append((char) 10);
            toml.append("present_letterbox = ").append(!stretchScreen ? "true" : "false").append((char) 10).append((char) 10);

            toml.append("present_effect = ").append((char) 34).append("bilinear").append((char) 34).append((char) 10);
            toml.append("anisotropic_override = ").append(aniso).append((char) 10);
            toml.append("swap_post_effect = ").append((char) 34).append(aa).append((char) 34).append((char) 10).append((char) 10);

            toml.append("async_shader_compilation = ").append(asyncShaders ? "true" : "false").append((char) 10);
            toml.append("vulkan_pipeline_creation_threads = ").append(pipelineThreads).append((char) 10);
            toml.append("texture_cache_memory_limit_soft = ").append(textureCacheLimit).append((char) 10);
            toml.append("gpu_3d_to_2d_texture = ").append(gpu3dTo2d ? "true" : "false").append((char) 10);
            toml.append("readback_resolve = ").append((char) 34).append(readback ? "fast" : "none").append((char) 34).append((char) 10);
            toml.append("native_2x_msaa = ").append(nativeMsaa ? "true" : "false").append((char) 10);
            toml.append("gamma_render_target_as_unorm16 = false").append((char) 10).append((char) 10);

            toml.append("audio_mute = ").append(mute ? "true" : "false").append((char) 10);
            toml.append("mnk_mode = ").append(mnk ? "true" : "false").append((char) 10).append((char) 10);

            toml.append("black_edition = ").append(black ? "true" : "false").append((char) 10);
            toml.append("grant_user_privileges = ").append(grant ? "true" : "false").append((char) 10);
            toml.append("user_profile_name = ").append((char) 34).append(gamertag).append((char) 34).append((char) 10);
            toml.append("game_speed = ").append(String.format(java.util.Locale.US, "%.1f", (float) speed)).append((char) 10);
            toml.append("log_level = ").append((char) 34).append(logLevel).append((char) 34).append((char) 10);
            if ("error".equalsIgnoreCase(logLevel)) {
                toml.append("log_noisy = false").append((char) 10);
                toml.append("log_verbose = false").append((char) 10);
            }
            toml.append("protect_zero = false").append((char) 10);

            String tomlContent = toml.toString();

            // Salva no armazenamento de arquivos externo do app
            File extDir = getExternalFilesDir(null);
            if (extDir != null) {
                File tomlFile = new File(extDir, "nfsmw.toml");
                if (forceOverwrite || !tomlFile.exists()) {
                    try (OutputStreamWriter writer = new OutputStreamWriter(new FileOutputStream(tomlFile), StandardCharsets.UTF_8)) {
                        writer.write(tomlContent);
                    }
                }
            }

            // Salva também no armazenamento interno
            File intDir = getFilesDir();
            if (intDir != null) {
                File tomlFile = new File(intDir, "nfsmw.toml");
                if (forceOverwrite || !tomlFile.exists()) {
                    try (OutputStreamWriter writer = new OutputStreamWriter(new FileOutputStream(tomlFile), StandardCharsets.UTF_8)) {
                        writer.write(tomlContent);
                    }
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void extractBundledShaders(File extDir) {
        if (extDir == null) return;
        try {
            File shaderDir = new File(extDir, "cache/shaders/shareable");
            if (!shaderDir.exists()) {
                shaderDir.mkdirs();
            }
            String[] shaderFiles = new String[] { "454107D9.xsh", "454107D9.fbo.vk.xpso" };
            for (String fileName : shaderFiles) {
                File target = new File(shaderDir, fileName);
                try (java.io.InputStream in = getAssets().open("shaders/shareable/" + fileName)) {
                    long assetSize = in.available();
                    if (!target.exists() || target.length() < assetSize) {
                        try (java.io.FileOutputStream out = new java.io.FileOutputStream(target)) {
                            byte[] buf = new byte[8192];
                            int read;
                            while ((read = in.read(buf)) != -1) {
                                out.write(buf, 0, read);
                            }
                            android.util.Log.i("NFS-Title", "Extracted bundled shader asset: " + fileName + " (" + target.length() + " bytes)");
                        }
                    }
                } catch (Exception ignored) {
                }
            }
        } catch (Exception e) {
            android.util.Log.w("NFS-Title", "Error extracting shader assets", e);
        }
    }

    private void setupSpinner(Spinner spinner, String[] items) {
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, items) {
            @Override
            public View getView(int position, View convertView, android.view.ViewGroup parent) {
                View v = super.getView(position, convertView, parent);
                if (v instanceof TextView) {
                    ((TextView) v).setTextColor(android.graphics.Color.WHITE);
                    ((TextView) v).setTextSize(12f);
                }
                return v;
            }
            @Override
            public View getDropDownView(int position, View convertView, android.view.ViewGroup parent) {
                View v = super.getDropDownView(position, convertView, parent);
                v.setBackgroundColor(android.graphics.Color.parseColor("#1C212A"));
                if (v instanceof TextView) {
                    ((TextView) v).setTextColor(android.graphics.Color.parseColor("#E8A13C"));
                    ((TextView) v).setPadding(24, 20, 24, 20);
                }
                return v;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
    }

    private int findStringIndex(String[] arr, String val, int defaultIdx) {
        for (int i = 0; i < arr.length; i++) {
            if (arr[i].equalsIgnoreCase(val)) return i;
        }
        return defaultIdx;
    }

    private int findIntIndex(int[] arr, int val, int defaultIdx) {
        for (int i = 0; i < arr.length; i++) {
            if (arr[i] == val) return i;
        }
        return defaultIdx;
    }
}

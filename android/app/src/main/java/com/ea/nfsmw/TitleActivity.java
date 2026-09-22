package com.ea.nfsmw;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
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
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

public class TitleActivity extends Activity {

    private static final String PREFS_NAME = "NFSMW_PREFS";
    private static final String KEY_ROM_PATH = "game_rom_path";
    private static final String KEY_RESOLUTION = "cfg_resolution";
    private static final String KEY_RESOLUTION_SCALE = "cfg_resolution_scale";
    private static final String KEY_VSYNC = "cfg_vsync";
    private static final String KEY_MAX_FPS = "cfg_max_fps";
    private static final String KEY_PRESENT_EFFECT = "cfg_present_effect";
    private static final String KEY_SHARPNESS = "cfg_sharpness";
    private static final String KEY_EDRAM_PATH = "cfg_edram_path";
    private static final String KEY_ASYNC_SHADERS = "cfg_async_shaders";
    private static final String KEY_READBACK_RESOLVE = "cfg_readback_resolve";
    private static final String KEY_ANISOTROPIC = "cfg_anisotropic";
    private static final String KEY_ANTIALIASING = "cfg_antialiasing";
    private static final String KEY_AUDIO_MUTE = "cfg_audio_mute";
    private static final String KEY_MNK_MODE = "cfg_mnk_mode";
    private static final String KEY_BLACK_EDITION = "cfg_black_edition";
    private static final String KEY_GRANT_PRIVILEGES = "cfg_grant_privileges";
    private static final String KEY_GAMERTAG = "cfg_gamertag";
    private static final String KEY_GAME_SPEED = "cfg_game_speed";

    private static final int REQ_CODE_FOLDER = 1001;
    private static final int REQ_CODE_ISO = 1002;
    private static final int REQ_CODE_MANAGE_STORAGE = 1003;

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
        writeTomlConfiguration(false);
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

    private void showSettingsDialog() {
        View dialogView = getLayoutInflater().inflate(R.layout.dialog_settings, null);
        AlertDialog dialog = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_NoActionBar)
                .setView(dialogView)
                .create();

        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));
        }

        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);

        // Spinners
        Spinner spResolution = dialogView.findViewById(R.id.sp_resolution);
        Spinner spResolutionScale = dialogView.findViewById(R.id.sp_resolution_scale);
        Spinner spMaxFps = dialogView.findViewById(R.id.sp_max_fps);
        Spinner spPresentEffect = dialogView.findViewById(R.id.sp_present_effect);
        Spinner spEdramPath = dialogView.findViewById(R.id.sp_edram_path);
        Spinner spAnisotropic = dialogView.findViewById(R.id.sp_anisotropic);
        Spinner spAntialiasing = dialogView.findViewById(R.id.sp_antialiasing);

        // Switches
        Switch swVsync = dialogView.findViewById(R.id.sw_vsync);
        Switch swAsyncShaders = dialogView.findViewById(R.id.sw_async_shaders);
        Switch swReadbackResolve = dialogView.findViewById(R.id.sw_readback_resolve);
        Switch swAudioMute = dialogView.findViewById(R.id.sw_audio_mute);
        Switch swMnkMode = dialogView.findViewById(R.id.sw_mnk_mode);
        Switch swBlackEdition = dialogView.findViewById(R.id.sw_black_edition);
        Switch swGrantPrivileges = dialogView.findViewById(R.id.sw_grant_privileges);

        // SeekBars & Text
        SeekBar sbSharpness = dialogView.findViewById(R.id.sb_sharpness);
        TextView tvSharpness = dialogView.findViewById(R.id.tv_sharpness_label);
        SeekBar sbGameSpeed = dialogView.findViewById(R.id.sb_game_speed);
        TextView tvGameSpeed = dialogView.findViewById(R.id.tv_speed_label);
        EditText etGamertag = dialogView.findViewById(R.id.et_gamertag);

        // Options arrays
        final String[] resLabels = {"480p (854x480 - Mais Leve)", "540p (960x540)", "720p (1280x720 - Padrão)", "1080p (1920x1080 - Full HD)"};
        final String[] resValues = {"480p", "540p", "720p", "1080p"};

        final String[] scaleLabels = {"1x - Nativo (Mais rápido)", "2x - 1440p (Alta Nitidez)", "3x - 4K"};
        final int[] scaleValues = {1, 2, 3};

        final String[] fpsLabels = {"30 FPS", "60 FPS (Recomendado)", "Sem limite (0)"};
        final int[] fpsValues = {30, 60, 0};

        final String[] effectLabels = {"Bilinear (Padrão)", "CAS (AMD Sharpening)", "FSR (AMD FidelityFX)"};
        final String[] effectValues = {"bilinear", "cas", "fsr"};

        final String[] edramLabels = {"rtv (Host Render Targets - Rápido)", "rov (Pixel Shader Interlock - Lento)"};
        final String[] edramValues = {"rtv", "rov"};

        final String[] anisoLabels = {"Desativado (0)", "1x", "2x", "4x", "8x", "16x"};
        final int[] anisoValues = {0, 1, 2, 3, 4, 5};

        final String[] aaLabels = {"Desativado (none)", "FXAA", "FXAA Extreme"};
        final String[] aaValues = {"none", "fxaa", "fxaa_extreme"};

        // Set adapters
        setupSpinner(spResolution, resLabels);
        setupSpinner(spResolutionScale, scaleLabels);
        setupSpinner(spMaxFps, fpsLabels);
        setupSpinner(spPresentEffect, effectLabels);
        setupSpinner(spEdramPath, edramLabels);
        setupSpinner(spAnisotropic, anisoLabels);
        setupSpinner(spAntialiasing, aaLabels);

        // Load saved values
        String curRes = prefs.getString(KEY_RESOLUTION, "720p");
        spResolution.setSelection(findStringIndex(resValues, curRes, 2));

        int curScale = prefs.getInt(KEY_RESOLUTION_SCALE, 1);
        spResolutionScale.setSelection(findIntIndex(scaleValues, curScale, 0));

        int curFps = prefs.getInt(KEY_MAX_FPS, 60);
        spMaxFps.setSelection(findIntIndex(fpsValues, curFps, 1));

        String curEffect = prefs.getString(KEY_PRESENT_EFFECT, "bilinear");
        spPresentEffect.setSelection(findStringIndex(effectValues, curEffect, 0));

        String curEdram = prefs.getString(KEY_EDRAM_PATH, "rtv");
        spEdramPath.setSelection(findStringIndex(edramValues, curEdram, 0));

        int curAniso = prefs.getInt(KEY_ANISOTROPIC, 0);
        spAnisotropic.setSelection(findIntIndex(anisoValues, curAniso, 0));

        String curAa = prefs.getString(KEY_ANTIALIASING, "none");
        spAntialiasing.setSelection(findStringIndex(aaValues, curAa, 0));

        swVsync.setChecked(prefs.getBoolean(KEY_VSYNC, true));
        swAsyncShaders.setChecked(prefs.getBoolean(KEY_ASYNC_SHADERS, true));
        swReadbackResolve.setChecked(prefs.getBoolean(KEY_READBACK_RESOLVE, false));
        swAudioMute.setChecked(prefs.getBoolean(KEY_AUDIO_MUTE, false));
        swMnkMode.setChecked(prefs.getBoolean(KEY_MNK_MODE, false));
        swBlackEdition.setChecked(prefs.getBoolean(KEY_BLACK_EDITION, true));
        swGrantPrivileges.setChecked(prefs.getBoolean(KEY_GRANT_PRIVILEGES, false));

        int curSharpness = prefs.getInt(KEY_SHARPNESS, 0);
        sbSharpness.setProgress(curSharpness);
        tvSharpness.setText("Nitidez CAS / FSR: " + curSharpness + "%");
        sbSharpness.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvSharpness.setText("Nitidez CAS / FSR: " + progress + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

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

        // Close button
        Button btnClose = dialogView.findViewById(R.id.btn_close_settings);
        if (btnClose != null) {
            btnClose.setOnClickListener(v -> dialog.dismiss());
        }

        // Reset button
        Button btnReset = dialogView.findViewById(R.id.btn_reset_defaults);
        if (btnReset != null) {
            btnReset.setOnClickListener(v -> {
                spResolution.setSelection(0); // 480p
                spResolutionScale.setSelection(0); // 1x
                spMaxFps.setSelection(1); // 60 FPS
                spPresentEffect.setSelection(0); // bilinear
                spEdramPath.setSelection(0); // rtv
                spAnisotropic.setSelection(0); // 0
                spAntialiasing.setSelection(0); // none
                swVsync.setChecked(true);
                swAsyncShaders.setChecked(true);
                swReadbackResolve.setChecked(false);
                swAudioMute.setChecked(false);
                swMnkMode.setChecked(false);
                swBlackEdition.setChecked(true);
                swGrantPrivileges.setChecked(false);
                sbSharpness.setProgress(0);
                sbGameSpeed.setProgress(100);
                etGamertag.setText("Player");
                Toast.makeText(TitleActivity.this, "Padrões de alto desempenho para Android restaurados.", Toast.LENGTH_SHORT).show();
            });
        }

        // Save button
        Button btnSave = dialogView.findViewById(R.id.btn_save_settings);
        if (btnSave != null) {
            btnSave.setOnClickListener(v -> {
                String selRes = resValues[spResolution.getSelectedItemPosition()];
                int selScale = scaleValues[spResolutionScale.getSelectedItemPosition()];
                int selFps = fpsValues[spMaxFps.getSelectedItemPosition()];
                String selEffect = effectValues[spPresentEffect.getSelectedItemPosition()];
                String selEdram = edramValues[spEdramPath.getSelectedItemPosition()];
                int selAniso = anisoValues[spAnisotropic.getSelectedItemPosition()];
                String selAa = aaValues[spAntialiasing.getSelectedItemPosition()];

                boolean selVsync = swVsync.isChecked();
                boolean selAsyncShaders = swAsyncShaders.isChecked();
                boolean selReadback = swReadbackResolve.isChecked();
                boolean selAudioMute = swAudioMute.isChecked();
                boolean selMnk = swMnkMode.isChecked();
                boolean selBlack = swBlackEdition.isChecked();
                boolean selGrant = swGrantPrivileges.isChecked();
                int selSharpness = sbSharpness.getProgress();
                int selSpeed = Math.max(20, sbGameSpeed.getProgress());
                String selGamertag = etGamertag.getText().toString().trim();
                if (selGamertag.isEmpty()) selGamertag = "Player";

                prefs.edit()
                        .putString(KEY_RESOLUTION, selRes)
                        .putInt(KEY_RESOLUTION_SCALE, selScale)
                        .putInt(KEY_MAX_FPS, selFps)
                        .putString(KEY_PRESENT_EFFECT, selEffect)
                        .putString(KEY_EDRAM_PATH, selEdram)
                        .putInt(KEY_ANISOTROPIC, selAniso)
                        .putString(KEY_ANTIALIASING, selAa)
                        .putBoolean(KEY_VSYNC, selVsync)
                        .putBoolean(KEY_ASYNC_SHADERS, selAsyncShaders)
                        .putBoolean(KEY_READBACK_RESOLVE, selReadback)
                        .putBoolean(KEY_AUDIO_MUTE, selAudioMute)
                        .putBoolean(KEY_MNK_MODE, selMnk)
                        .putBoolean(KEY_BLACK_EDITION, selBlack)
                        .putBoolean(KEY_GRANT_PRIVILEGES, selGrant)
                        .putInt(KEY_SHARPNESS, selSharpness)
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
            int fps = prefs.getInt(KEY_MAX_FPS, 60);
            boolean vsync = prefs.getBoolean(KEY_VSYNC, true);
            String effect = prefs.getString(KEY_PRESENT_EFFECT, "bilinear");
            int sharpness = prefs.getInt(KEY_SHARPNESS, 0);
            String edram = prefs.getString(KEY_EDRAM_PATH, "rtv");
            boolean asyncShaders = prefs.getBoolean(KEY_ASYNC_SHADERS, true);
            boolean readback = prefs.getBoolean(KEY_READBACK_RESOLVE, false);
            int aniso = prefs.getInt(KEY_ANISOTROPIC, 0);
            String aa = prefs.getString(KEY_ANTIALIASING, "none");
            boolean mute = prefs.getBoolean(KEY_AUDIO_MUTE, false);
            boolean mnk = prefs.getBoolean(KEY_MNK_MODE, false);
            boolean black = prefs.getBoolean(KEY_BLACK_EDITION, true);
            boolean grant = prefs.getBoolean(KEY_GRANT_PRIVILEGES, false);
            String gamertag = prefs.getString(KEY_GAMERTAG, "Player");
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
            toml.append("# ============================================================================\n");
            toml.append("#  NFSMW Recompiled - Android Configuration (Gerado pelo TitleActivity)\n");
            toml.append("# ============================================================================\n\n");

            String vulkanEdram = "rov".equalsIgnoreCase(edram) || "fsi".equalsIgnoreCase(edram) ? "fsi" : "fbo";
            toml.append("render_target_path_d3d12 = \"").append(edram).append("\"\n");
            toml.append("render_target_path_vulkan = \"").append(vulkanEdram).append("\"\n");
            toml.append("vulkan_async_skip_incomplete_frames = false\n");
            toml.append("vulkan_submit_on_primary_buffer_end = false\n");
            toml.append("vulkan_dynamic_rendering = true\n");
            toml.append("gpu_backend = \"vulkan\"\n");
            toml.append("gpu = \"xenos\"\n\n");

            toml.append("video_mode_width = ").append(width).append("\n");
            toml.append("video_mode_height = ").append(height).append("\n");
            toml.append("resolution = \"").append(res).append("\"\n");
            toml.append("resolution_scale = ").append(scale).append("\n");
            toml.append("vsync = ").append(vsync ? "true" : "false").append("\n");
            toml.append("max_fps = ").append(fps).append("\n");
            toml.append("fullscreen = true\n\n");

            toml.append("present_effect = \"").append(effect).append("\"\n");
            toml.append("present_cas_additional_sharpness = ").append(String.format(java.util.Locale.US, "%.2f", sharpness / 100.0f)).append("\n");
            toml.append("anisotropic_override = ").append(aniso).append("\n");
            toml.append("swap_post_effect = \"").append(aa).append("\"\n\n");

            toml.append("async_shader_compilation = ").append(asyncShaders ? "true" : "false").append("\n");
            toml.append("readback_resolve = \"").append(readback ? "fast" : "none").append("\"\n");
            toml.append("gpu_3d_to_2d_texture = true\n");
            toml.append("native_2x_msaa = false\n");
            toml.append("gamma_render_target_as_unorm16 = false\n\n");

            toml.append("audio_mute = ").append(mute ? "true" : "false").append("\n");
            toml.append("mnk_mode = ").append(mnk ? "true" : "false").append("\n\n");

            toml.append("black_edition = ").append(black ? "true" : "false").append("\n");
            toml.append("grant_user_privileges = ").append(grant ? "true" : "false").append("\n");
            toml.append("user_profile_name = \"").append(gamertag).append("\"\n");
            toml.append("game_speed = ").append(String.format(java.util.Locale.US, "%.1f", (float) speed)).append("\n");

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

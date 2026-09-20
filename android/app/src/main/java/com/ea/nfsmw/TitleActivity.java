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
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

public class TitleActivity extends Activity {

    private static final String PREFS_NAME = "NFSMW_PREFS";
    private static final String KEY_ROM_PATH = "game_rom_path";
    private static final int REQ_CODE_FOLDER = 1001;
    private static final int REQ_CODE_ISO = 1002;
    private static final int REQ_CODE_MANAGE_STORAGE = 1003;

    private View rootLayout;
    private View startPromptContainer;
    private TextView tvPressStart;
    private TextView tvRomStatus;
    private Button btnSelectRom;
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
        fadeOverlay = findViewById(R.id.fade_overlay);

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
}

package com.ea.nfsmw;

import android.app.NativeActivity;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.PopupWindow;

import com.windroid.emu.views.VirtualControllerInputView;

public class GameActivity extends NativeActivity {

    private static final String TAG = "NFS-GameActivity";

    // Native XInput bitmask constants
    public static final int BTN_DPAD_UP        = 0x0001;
    public static final int BTN_DPAD_DOWN      = 0x0002;
    public static final int BTN_DPAD_LEFT      = 0x0004;
    public static final int BTN_DPAD_RIGHT     = 0x0008;
    public static final int BTN_START          = 0x0010;
    public static final int BTN_BACK           = 0x0020;
    public static final int BTN_LEFT_THUMB     = 0x0040;
    public static final int BTN_RIGHT_THUMB    = 0x0080;
    public static final int BTN_LEFT_SHOULDER  = 0x0100;
    public static final int BTN_RIGHT_SHOULDER = 0x0200;
    public static final int BTN_A              = 0x1000;
    public static final int BTN_B              = 0x2000;
    public static final int BTN_X              = 0x4000;
    public static final int BTN_Y              = 0x8000;

    public static native void nativeSetVirtualGamepad(
            int buttons, float thumbLx, float thumbLy, float leftTrigger, float rightTrigger);

    private VirtualControllerInputView controlsView;
    private PopupWindow controlsPopup;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "GameActivity onCreate with Windroid-emu virtual controls");

        configureFullscreen();

        // Create VirtualControllerInputView from Windroid-emu
        controlsView = new VirtualControllerInputView(this);
        controlsView.setBackgroundColor(Color.TRANSPARENT);
        controlsView.setAlpha(0.85F);

        // Map Windroid-emu virtual controller state to native XInput gamepad
        controlsView.setControllerListener((lx, ly, rx, ry, lt, rt, buttonsA, buttonsB, dpadStatus) -> {
            int xinputButtons = 0;

            if ((buttonsA & VirtualControllerInputView.MASK_A) != 0)  xinputButtons |= BTN_A;
            if ((buttonsA & VirtualControllerInputView.MASK_B) != 0)  xinputButtons |= BTN_B;
            if ((buttonsA & VirtualControllerInputView.MASK_X) != 0)  xinputButtons |= BTN_X;
            if ((buttonsA & VirtualControllerInputView.MASK_Y) != 0)  xinputButtons |= BTN_Y;
            if ((buttonsA & VirtualControllerInputView.MASK_RB) != 0) xinputButtons |= BTN_RIGHT_SHOULDER;
            if ((buttonsA & VirtualControllerInputView.MASK_LB) != 0) xinputButtons |= BTN_LEFT_SHOULDER;
            if ((buttonsA & VirtualControllerInputView.MASK_LS) != 0) xinputButtons |= BTN_LEFT_THUMB;
            if ((buttonsA & VirtualControllerInputView.MASK_RS) != 0) xinputButtons |= BTN_RIGHT_THUMB;

            if ((buttonsB & VirtualControllerInputView.MASK_START) != 0)  xinputButtons |= BTN_START;
            if ((buttonsB & VirtualControllerInputView.MASK_SELECT) != 0) xinputButtons |= BTN_BACK;

            switch (dpadStatus) {
                case 1: xinputButtons |= BTN_DPAD_UP; break;
                case 2: xinputButtons |= (BTN_DPAD_UP | BTN_DPAD_RIGHT); break;
                case 3: xinputButtons |= BTN_DPAD_RIGHT; break;
                case 4: xinputButtons |= (BTN_DPAD_DOWN | BTN_DPAD_RIGHT); break;
                case 5: xinputButtons |= BTN_DPAD_DOWN; break;
                case 6: xinputButtons |= (BTN_DPAD_DOWN | BTN_DPAD_LEFT); break;
                case 7: xinputButtons |= BTN_DPAD_LEFT; break;
                case 8: xinputButtons |= (BTN_DPAD_UP | BTN_DPAD_LEFT); break;
                default: break;
            }

// Logging disabled for 60fps input performance

            try {
                nativeSetVirtualGamepad(xinputButtons, lx, -ly, lt, rt);
            } catch (UnsatisfiedLinkError err) {
                Log.e(TAG, "nativeSetVirtualGamepad call failed", err);
            }
        });

        // Watch layout changes to update popup size
        View decorView = getWindow().getDecorView();
        if (decorView != null) {
            decorView.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
                int w = right - left;
                int h = bottom - top;
                if (w > 0 && h > 0) {
                    if (controlsPopup != null && controlsPopup.isShowing()) {
                        controlsPopup.update(0, 0, w, h);
                    } else {
                        showControlsOverlay();
                    }
                }
            });
        }
    }

    private void showControlsOverlay() {
        if (isFinishing() || (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1 && isDestroyed())) {
            return;
        }

        View decorView = getWindow().getDecorView();
        if (decorView == null || decorView.getWindowToken() == null) {
            return;
        }

        int width = decorView.getWidth();
        int height = decorView.getHeight();
        if (width <= 0 || height <= 0) {
            decorView.post(this::showControlsOverlay);
            return;
        }

        if (controlsPopup == null) {
            controlsPopup = new PopupWindow(controlsView, width, height, false);
            controlsPopup.setTouchable(true);
            controlsPopup.setFocusable(false);
            controlsPopup.setOutsideTouchable(false);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
                controlsPopup.setSplitTouchEnabled(true);
            }
            controlsPopup.setBackgroundDrawable(null);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                controlsPopup.setIsClippedToScreen(false);
            }
        }

        if (!controlsPopup.isShowing()) {
            try {
                Log.i(TAG, "Displaying Windroid-emu VirtualControllerInputView overlay (" + width + "x" + height + ")");
                controlsPopup.setWidth(width);
                controlsPopup.setHeight(height);
                controlsPopup.showAtLocation(decorView, Gravity.TOP | Gravity.START, 0, 0);
            } catch (Exception e) {
                Log.e(TAG, "Failed to show controls PopupWindow", e);
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveMode();
        View decorView = getWindow().getDecorView();
        if (decorView != null) {
            decorView.post(this::showControlsOverlay);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
            View decorView = getWindow().getDecorView();
            if (decorView != null) {
                decorView.post(this::showControlsOverlay);
            }
        }
    }

    @Override
    protected void onDestroy() {
        if (controlsPopup != null) {
            try {
                controlsPopup.dismiss();
            } catch (Exception ignored) {}
            controlsPopup = null;
        }
        super.onDestroy();
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
        if (decorView != null) {
            int uiOptions = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            decorView.setSystemUiVisibility(uiOptions);
        }
    }
}

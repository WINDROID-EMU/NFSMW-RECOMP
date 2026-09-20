package com.windroid.emu.views;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;

/**
 * Virtual Controller from Windroid-emu adapted for NFSMW Recompiled.
 * Matches Windroid-emu layout, aesthetics, touch physics and button behaviors.
 */
public class VirtualControllerInputView extends View {

    public final static int SHAPE_CIRCLE = 0;
    public final static int SHAPE_SQUARE = 1;
    public final static int SHAPE_RECTANGLE = 2;
    public final static int SHAPE_DPAD = 3;

    public final static int GRID_SIZE = 10;

    public static final int UP = 1;
    public static final int RIGHT_UP = 2;
    public static final int RIGHT = 3;
    public static final int RIGHT_DOWN = 4;
    public static final int DOWN = 5;
    public static final int LEFT_DOWN = 6;
    public static final int LEFT = 7;
    public static final int LEFT_UP = 8;

    public final static int A_BUTTON = 1;
    public final static int B_BUTTON = 2;
    public final static int X_BUTTON = 3;
    public final static int Y_BUTTON = 4;
    public final static int START_BUTTON = 5;
    public final static int SELECT_BUTTON = 6;
    public final static int LB_BUTTON = 7;
    public final static int LT_BUTTON = 8;
    public final static int RB_BUTTON = 9;
    public final static int RT_BUTTON = 10;
    public final static int LEFT_ANALOG = 11;
    public final static int LS_BUTTON = 12;
    public final static int RS_BUTTON = 13;
    public final static int RIGHT_ANALOG = 14;

    // Button state bitmasks matching ControllerUtils
    public final static int MASK_A      = 0x01;
    public final static int MASK_B      = 0x02;
    public final static int MASK_X      = 0x04;
    public final static int MASK_Y      = 0x08;
    public final static int MASK_RB     = 0x10;
    public final static int MASK_LB     = 0x20;
    public final static int MASK_LS     = 0x40;
    public final static int MASK_RS     = 0x80;

    public final static int MASK_START  = 0x01;
    public final static int MASK_SELECT = 0x02;

    public interface ControllerListener {
        void onControllerState(float lx, float ly, float rx, float ry, float lt, float rt,
                               int buttonsA, int buttonsB, int dpadStatus);
    }

    private ControllerListener listener;

    private Paint paint;
    private Paint textPaint;

    private final Path dpadUp = new Path();
    private final Path dpadDown = new Path();
    private final Path dpadLeft = new Path();
    private final Path dpadRight = new Path();
    private final Path startButton = new Path();
    private final Path selectButton = new Path();

    private final ArrayList<VirtualControllerButton> buttonList = new ArrayList<>();
    private VirtualXInputDPad dpad;
    private VirtualXInputAnalog leftAnalog;
    private VirtualXInputAnalog rightAnalog;

    private byte buttonsStateA = 0;
    private byte buttonsStateB = 0;
    private float lt = 0;
    private float rt = 0;
    public boolean isEditing = false;
    public static int virtualXInputControllerId = 0;

    private float baseWidth = 2400F;
    private float baseHeight = 1080F;

    public VirtualControllerInputView(Context context) {
        super(context);
        init();
    }

    public VirtualControllerInputView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public VirtualControllerInputView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    public void setControllerListener(ControllerListener listener) {
        this.listener = listener;
    }

    private void init() {
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);

        paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStrokeWidth(16F);
        paint.setColor(Color.WHITE);
        paint.setStyle(Paint.Style.STROKE);

        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTextSize(54F);
        textPaint.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));

        // Default layout positioned for 2400x1080 base screen
        addButton(A_BUTTON, 2065F, 910F, 180F, SHAPE_CIRCLE);
        addButton(B_BUTTON, 2205F, 735F, 180F, SHAPE_CIRCLE);
        addButton(X_BUTTON, 1925F, 735F, 180F, SHAPE_CIRCLE);
        addButton(Y_BUTTON, 2065F, 560F, 180F, SHAPE_CIRCLE);
        addButton(START_BUTTON, 1330F, 980F, 130F, SHAPE_CIRCLE);
        addButton(SELECT_BUTTON, 1120F, 980F, 130F, SHAPE_CIRCLE);
        addButton(LB_BUTTON, 280F, 300F, 260F, SHAPE_RECTANGLE);
        addButton(LT_BUTTON, 280F, 140F, 260F, SHAPE_RECTANGLE);
        addButton(RB_BUTTON, 2065F, 300F, 260F, SHAPE_RECTANGLE);
        addButton(RT_BUTTON, 2065F, 140F, 260F, SHAPE_RECTANGLE);
        addButton(LS_BUTTON, 880F, 980F, 180F, SHAPE_CIRCLE);
        addButton(RS_BUTTON, 1560F, 980F, 180F, SHAPE_CIRCLE);

        leftAnalog = new VirtualXInputAnalog(LEFT_ANALOG, 280F, 840F, 275F);
        rightAnalog = new VirtualXInputAnalog(RIGHT_ANALOG, 1750F, 480F, 275F);
        dpad = new VirtualXInputDPad(0, 640F, 480F, 200F);

        loadSavedLayout();
    }

    private void loadSavedLayout() {
        try {
            SharedPreferences prefs = getContext().getSharedPreferences("virtual_controller_windroid", Context.MODE_PRIVATE);
            if (prefs != null && prefs.contains("VC_BUTTON_" + A_BUTTON + "_X")) {
                buttonList.forEach((i) -> {
                    i.x = prefs.getFloat("VC_BUTTON_" + i.id + "_X", i.x);
                    i.y = prefs.getFloat("VC_BUTTON_" + i.id + "_Y", i.y);
                });

                leftAnalog.x = prefs.getFloat("VC_BUTTON_" + LEFT_ANALOG + "_X", leftAnalog.x);
                leftAnalog.y = prefs.getFloat("VC_BUTTON_" + LEFT_ANALOG + "_Y", leftAnalog.y);

                rightAnalog.x = prefs.getFloat("VC_BUTTON_" + RIGHT_ANALOG + "_X", rightAnalog.x);
                rightAnalog.y = prefs.getFloat("VC_BUTTON_" + RIGHT_ANALOG + "_Y", rightAnalog.y);

                dpad.x = prefs.getFloat("VC_BUTTON_DPAD_X", dpad.x);
                dpad.y = prefs.getFloat("VC_BUTTON_DPAD_Y", dpad.y);
            }
        } catch (Exception ignored) {}
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (w > 0 && h > 0) {
            adjustButtonsForResolution(w, h);
            invalidate();
        }
    }

    private void adjustButtonsForResolution(int actualWidth, int actualHeight) {
        float scaleX = (float) actualWidth / baseWidth;
        float scaleY = (float) actualHeight / baseHeight;

        if (Math.abs(scaleX - 1.0F) > 0.02F || Math.abs(scaleY - 1.0F) > 0.02F) {
            buttonList.forEach((i) -> {
                i.x = (i.x / baseWidth) * actualWidth;
                i.y = (i.y / baseHeight) * actualHeight;
                i.radius = i.radius * Math.min(scaleX, scaleY);
            });

            leftAnalog.x = (leftAnalog.x / baseWidth) * actualWidth;
            leftAnalog.y = (leftAnalog.y / baseHeight) * actualHeight;
            leftAnalog.radius = leftAnalog.radius * Math.min(scaleX, scaleY);

            rightAnalog.x = (rightAnalog.x / baseWidth) * actualWidth;
            rightAnalog.y = (rightAnalog.y / baseHeight) * actualHeight;
            rightAnalog.radius = rightAnalog.radius * Math.min(scaleX, scaleY);

            dpad.x = (dpad.x / baseWidth) * actualWidth;
            dpad.y = (dpad.y / baseHeight) * actualHeight;
            dpad.radius = dpad.radius * Math.min(scaleX, scaleY);

            textPaint.setTextSize(textPaint.getTextSize() * Math.min(scaleX, scaleY));
            baseWidth = (float) actualWidth;
            baseHeight = (float) actualHeight;
        }
    }

    private void addButton(int id, float x, float y, float radius, int shape) {
        buttonList.add(new VirtualControllerButton(id, x, y, radius, shape));
    }

    private String getButtonName(int id) {
        return switch (id) {
            case A_BUTTON -> "A";
            case B_BUTTON -> "B";
            case X_BUTTON -> "X";
            case Y_BUTTON -> "Y";
            case RB_BUTTON -> "RB";
            case LB_BUTTON -> "LB";
            case RT_BUTTON -> "RT";
            case LT_BUTTON -> "LT";
            case RS_BUTTON -> "RS";
            case LS_BUTTON -> "LS";
            default -> "";
        };
    }

    private void drawDPad(Path path, boolean isPressed, Canvas canvas) {
        paint.setStyle(isPressed ? Paint.Style.FILL_AND_STROKE : Paint.Style.STROKE);
        paint.setAlpha(isEditing ? 200 : (int) (getAlpha() * 200));
        canvas.drawPath(path, paint);
    }

    public static boolean detectClick(MotionEvent event, int index, float x, float y, float radius, int shape) {
        float ex = event.getX(index);
        float ey = event.getY(index);
        float r = radius * 0.65F;
        return switch (shape) {
            case SHAPE_RECTANGLE -> (ex >= x - radius * 0.65F && ex <= x + radius * 0.65F) &&
                    (ey >= y - radius * 0.4F && ey <= y + radius * 0.4F);
            case SHAPE_DPAD -> (ex >= x - radius - 30F && ex <= x + radius + 30F) &&
                    (ey >= y - radius - 30F && ey <= y + radius + 30F);
            default -> (ex >= x - r && ex <= x + r) &&
                    (ey >= y - r && ey <= y + r);
        };
    }

    public static int getAxisStatus(float axisX, float axisY, float deadZone) {
        boolean axisXNeutral = (axisX < deadZone && axisX > -deadZone);
        boolean axisYNeutral = (axisY < deadZone && axisY > -deadZone);

        if (axisX > deadZone && axisY < -deadZone) {
            return RIGHT_UP;
        } else if (axisX > deadZone && axisYNeutral) {
            return RIGHT;
        } else if (axisX > deadZone && axisY > deadZone) {
            return RIGHT_DOWN;
        } else if (axisY > deadZone && axisXNeutral) {
            return DOWN;
        } else if (axisY < -deadZone && axisXNeutral) {
            return UP;
        } else if (axisX < -deadZone && axisY > deadZone) {
            return LEFT_DOWN;
        } else if (axisX < -deadZone && axisYNeutral) {
            return LEFT;
        } else if (axisX < -deadZone && axisY < -deadZone) {
            return LEFT_UP;
        }

        return 0;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        buttonList.forEach((i) -> {
            if (i.isPressed) {
                paint.setStyle(Paint.Style.FILL_AND_STROKE);
                textPaint.setColor(Color.BLACK);
            } else {
                paint.setStyle(Paint.Style.STROKE);
                textPaint.setColor(Color.WHITE);
            }
            paint.setColor(Color.WHITE);
            paint.setAlpha(isEditing ? 255 : (int) (getAlpha() * 255));
            paint.setStrokeWidth(14F);
            textPaint.setAlpha(isEditing ? 255 : (int) (getAlpha() * 255));

            float offset = (textPaint.getFontMetrics().ascent + textPaint.getFontMetrics().descent) / 2F;

            switch (i.shape) {
                case SHAPE_CIRCLE -> canvas.drawCircle(i.x, i.y, i.radius / 2F, paint);
                case SHAPE_RECTANGLE -> canvas.drawRoundRect(
                        i.x - i.radius / 2F,
                        i.y - i.radius / 4F,
                        i.x + i.radius / 2F,
                        i.y + i.radius / 4F,
                        32F,
                        32F,
                        paint);
            }

            switch (i.id) {
                case START_BUTTON -> {
                    paint.setStrokeWidth(10F);

                    startButton.reset();
                    startButton.moveTo(i.x - i.radius / 3, i.y - i.radius / 8);
                    startButton.lineTo(i.x - i.radius / 3 + i.radius - i.radius / 3, i.y - i.radius / 8);
                    startButton.moveTo(i.x - i.radius / 3, i.y);
                    startButton.lineTo(i.x - i.radius / 3 + i.radius - i.radius / 3, i.y);
                    startButton.moveTo(i.x - i.radius / 3, i.y + i.radius / 8);
                    startButton.lineTo(i.x - i.radius / 3 + i.radius - i.radius / 3, i.y + i.radius / 8);

                    paint.setColor(i.isPressed ? Color.BLACK : Color.WHITE);
                    paint.setAlpha(isEditing ? 200 : (int) (getAlpha() * 200));

                    canvas.drawPath(startButton, paint);
                }
                case SELECT_BUTTON -> {
                    paint.setStrokeWidth(10F);

                    selectButton.reset();
                    selectButton.moveTo(i.x - i.radius / 4F + 4F, i.y - i.radius / 4 + 40F);
                    selectButton.lineTo(i.x - i.radius / 4F + 4F, i.y - i.radius / 4);
                    selectButton.lineTo(i.x - i.radius / 4F + 4F + 40F, i.y - i.radius / 4);
                    selectButton.lineTo(i.x - i.radius / 4F + 4F + 40F, i.y - i.radius / 4 + 20F);
                    selectButton.lineTo(i.x - i.radius / 4F + 4F + 40F, i.y - i.radius / 4);
                    selectButton.lineTo(i.x - i.radius / 4F + 4F, i.y - i.radius / 4);
                    selectButton.close();
                    selectButton.moveTo(i.x - i.radius / 4F + 20F, i.y - i.radius / 4 + 30F);
                    selectButton.lineTo(i.x - i.radius / 4F + 60F, i.y - i.radius / 4 + 30F);
                    selectButton.lineTo(i.x - i.radius / 4F + 60F, i.y - i.radius / 4 + 70F);
                    selectButton.lineTo(i.x - i.radius / 4F + 20F, i.y - i.radius / 4 + 70F);
                    selectButton.close();

                    paint.setColor(i.isPressed ? Color.BLACK : Color.WHITE);
                    paint.setAlpha(isEditing ? 200 : (int) (getAlpha() * 200));

                    canvas.drawPath(selectButton, paint);
                }
                default -> canvas.drawText(getButtonName(i.id), i.x, i.y - offset - 4, textPaint);
            }
        });

        // Left Analog Stick
        float analogX = leftAnalog.x + leftAnalog.fingerX;
        float analogY = leftAnalog.y + leftAnalog.fingerY;

        float distSquared = (leftAnalog.fingerX * leftAnalog.fingerX) + (leftAnalog.fingerY * leftAnalog.fingerY);
        float maxDist = (leftAnalog.radius / 4F) * (leftAnalog.radius / 4F);

        if (distSquared > maxDist) {
            float dist = (float) Math.sqrt(distSquared);
            float scale = (leftAnalog.radius / 4F) / dist;
            analogX = leftAnalog.x + (leftAnalog.fingerX * scale);
            analogY = leftAnalog.y + (leftAnalog.fingerY * scale);
        }

        paint.setColor(Color.WHITE);
        paint.setAlpha(isEditing ? 200 : (int) (getAlpha() * 200));
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(12F);
        canvas.drawCircle(leftAnalog.x, leftAnalog.y, leftAnalog.radius / 2F, paint);

        paint.setStyle(Paint.Style.FILL);
        canvas.drawCircle(analogX, analogY, leftAnalog.radius / 4F, paint);

        // Right Analog Stick
        float rightAnalogX = rightAnalog.x + rightAnalog.fingerX;
        float rightAnalogY = rightAnalog.y + rightAnalog.fingerY;

        float rightDistSquared = (rightAnalog.fingerX * rightAnalog.fingerX)
                + (rightAnalog.fingerY * rightAnalog.fingerY);
        float rightMaxDist = (rightAnalog.radius / 4F) * (rightAnalog.radius / 4F);

        if (rightDistSquared > rightMaxDist) {
            float dist = (float) Math.sqrt(rightDistSquared);
            float scale = (rightAnalog.radius / 4F) / dist;
            rightAnalogX = rightAnalog.x + (rightAnalog.fingerX * scale);
            rightAnalogY = rightAnalog.y + (rightAnalog.fingerY * scale);
        }

        paint.setColor(Color.WHITE);
        paint.setAlpha(isEditing ? 200 : (int) (getAlpha() * 200));
        paint.setStyle(Paint.Style.STROKE);
        canvas.drawCircle(rightAnalog.x, rightAnalog.y, rightAnalog.radius / 2F, paint);

        paint.setStyle(Paint.Style.FILL);
        canvas.drawCircle(rightAnalogX, rightAnalogY, rightAnalog.radius / 4F, paint);

        // D-Pad Drawing
        dpadLeft.reset();
        dpadLeft.moveTo(dpad.x - 20F, dpad.y);
        dpadLeft.lineTo(dpad.x - 20F - dpad.radius / 4F, dpad.y - dpad.radius / 4F);
        dpadLeft.lineTo(dpad.x - 20F - dpad.radius / 4F - dpad.radius / 2F, dpad.y - dpad.radius / 4F);
        dpadLeft.lineTo(dpad.x - 20F - dpad.radius / 4F - dpad.radius / 2F, dpad.y - dpad.radius / 4F + dpad.radius / 2F);
        dpadLeft.lineTo(dpad.x - 20F - dpad.radius / 4F, dpad.y - dpad.radius / 4F + dpad.radius / 2F);
        dpadLeft.lineTo(dpad.x - 20F, dpad.y);
        dpadLeft.close();

        dpadUp.reset();
        dpadUp.moveTo(dpad.x, dpad.y - 20F);
        dpadUp.lineTo(dpad.x - dpad.radius / 4F, dpad.y - 20F - dpad.radius / 4F);
        dpadUp.lineTo(dpad.x - dpad.radius / 4F, dpad.y - 20F - dpad.radius / 4F - dpad.radius / 2F);
        dpadUp.lineTo(dpad.x - dpad.radius / 4F + dpad.radius / 2F, dpad.y - 20F - dpad.radius / 4F - dpad.radius / 2F);
        dpadUp.lineTo(dpad.x - dpad.radius / 4F + dpad.radius / 2F, dpad.y - 20F - dpad.radius / 4F);
        dpadUp.lineTo(dpad.x, dpad.y - 20F);
        dpadUp.close();

        dpadRight.reset();
        dpadRight.moveTo(dpad.x + 20F, dpad.y);
        dpadRight.lineTo(dpad.x + 20F + dpad.radius / 4F, dpad.y - dpad.radius / 4F);
        dpadRight.lineTo(dpad.x + 20F + dpad.radius / 4F + dpad.radius / 2F, dpad.y - dpad.radius / 4F);
        dpadRight.lineTo(dpad.x + 20F + dpad.radius / 4F + dpad.radius / 2F, dpad.y - dpad.radius / 4F + dpad.radius / 2F);
        dpadRight.lineTo(dpad.x + 20F + dpad.radius / 4F, dpad.y - dpad.radius / 4F + dpad.radius / 2F);
        dpadRight.lineTo(dpad.x + 20F, dpad.y);
        dpadRight.close();

        dpadDown.reset();
        dpadDown.moveTo(dpad.x, dpad.y + 20F);
        dpadDown.lineTo(dpad.x - dpad.radius / 4F, dpad.y + 20F + dpad.radius / 4F);
        dpadDown.lineTo(dpad.x - dpad.radius / 4F, dpad.y + 20F + dpad.radius / 4F + dpad.radius / 2F);
        dpadDown.lineTo(dpad.x - dpad.radius / 4F + dpad.radius / 2F, dpad.y + 20F + dpad.radius / 4F + dpad.radius / 2F);
        dpadDown.lineTo(dpad.x - dpad.radius / 4F + dpad.radius / 2F, dpad.y + 20F + dpad.radius / 4F);
        dpadDown.lineTo(dpad.x, dpad.y + 20F);
        dpadDown.close();

        drawDPad(dpadUp, dpad.dpadStatus == UP || dpad.dpadStatus == RIGHT_UP || dpad.dpadStatus == LEFT_UP, canvas);
        drawDPad(dpadDown, dpad.dpadStatus == DOWN || dpad.dpadStatus == RIGHT_DOWN || dpad.dpadStatus == LEFT_DOWN, canvas);
        drawDPad(dpadLeft, dpad.dpadStatus == LEFT || dpad.dpadStatus == LEFT_DOWN || dpad.dpadStatus == LEFT_UP, canvas);
        drawDPad(dpadRight, dpad.dpadStatus == RIGHT || dpad.dpadStatus == RIGHT_DOWN || dpad.dpadStatus == RIGHT_UP, canvas);
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int actionIdx = event.getActionIndex();
        if (action != MotionEvent.ACTION_MOVE) {
            android.util.Log.i("NFS-VirtualControls", "onTouchEvent: action=" + action + " (" + event.getX(actionIdx) + ", " + event.getY(actionIdx) + ") ptrs=" + event.getPointerCount());
        }

        float lx = leftAnalog.isPressed ? (leftAnalog.fingerX / (leftAnalog.radius / 4)) : 0F;
        float ly = leftAnalog.isPressed ? (leftAnalog.fingerY / (leftAnalog.radius / 4)) : 0F;
        float rx = rightAnalog.isPressed ? (rightAnalog.fingerX / (rightAnalog.radius / 4)) : 0F;
        float ry = rightAnalog.isPressed ? (rightAnalog.fingerY / (rightAnalog.radius / 4)) : 0F;
        byte dpadStatus = (byte) dpad.dpadStatus;

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_DOWN -> {
                for (VirtualControllerButton i : buttonList) {
                    if (detectClick(event, event.getActionIndex(), i.x, i.y, i.radius, i.shape)) {
                        i.fingerId = event.getPointerId(event.getActionIndex());
                        i.isPressed = true;
                        handleButton(i, true);
                        break;
                    }
                }

                if (detectClick(event, event.getActionIndex(), leftAnalog.x, leftAnalog.y, leftAnalog.radius, SHAPE_CIRCLE)) {
                    float posX = event.getX(event.getActionIndex()) - leftAnalog.x;
                    float posY = event.getY(event.getActionIndex()) - leftAnalog.y;

                    leftAnalog.fingerId = event.getPointerId(event.getActionIndex());

                    float maxDist = leftAnalog.radius / 4F;
                    float dist = (float) Math.sqrt(posX * posX + posY * posY);

                    if (dist > maxDist) {
                        float scale = maxDist / dist;
                        posX *= scale;
                        posY *= scale;
                    }

                    leftAnalog.fingerX = posX;
                    leftAnalog.fingerY = posY;
                    leftAnalog.isPressed = true;

                    lx = (posX / maxDist);
                    ly = (posY / maxDist);
                }

                if (detectClick(event, event.getActionIndex(), rightAnalog.x, rightAnalog.y, rightAnalog.radius, SHAPE_CIRCLE)) {
                    float posX = event.getX(event.getActionIndex()) - rightAnalog.x;
                    float posY = event.getY(event.getActionIndex()) - rightAnalog.y;

                    rightAnalog.fingerId = event.getPointerId(event.getActionIndex());

                    float maxDist = rightAnalog.radius / 4F;
                    float dist = (float) Math.sqrt(posX * posX + posY * posY);

                    if (dist > maxDist) {
                        float scale = maxDist / dist;
                        posX *= scale;
                        posY *= scale;
                    }

                    rightAnalog.fingerX = posX;
                    rightAnalog.fingerY = posY;
                    rightAnalog.isPressed = true;

                    rx = (posX / maxDist);
                    ry = (posY / maxDist);
                }

                if (detectClick(event, event.getActionIndex(), dpad.x, dpad.y, dpad.radius, SHAPE_DPAD)) {
                    float posX = event.getX(event.getActionIndex()) - dpad.x;
                    float posY = event.getY(event.getActionIndex()) - dpad.y;

                    dpad.fingerId = event.getPointerId(event.getActionIndex());
                    dpad.fingerX = posX;
                    dpad.fingerY = posY;
                    dpad.isPressed = true;
                    dpad.dpadStatus = getAxisStatus(posX / dpad.radius, posY / dpad.radius, 0.25F);

                    dpadStatus = (byte) dpad.dpadStatus;
                }

                invalidate();
            }
            case MotionEvent.ACTION_MOVE -> {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    if (leftAnalog.isPressed && leftAnalog.fingerId == event.getPointerId(i)) {
                        float posX = event.getX(i) - leftAnalog.x;
                        float posY = event.getY(i) - leftAnalog.y;

                        float maxDist = leftAnalog.radius / 4F;
                        float dist = (float) Math.sqrt(posX * posX + posY * posY);

                        if (dist > maxDist) {
                            float scale = maxDist / dist;
                            posX *= scale;
                            posY *= scale;
                        }

                        leftAnalog.fingerX = posX;
                        leftAnalog.fingerY = posY;

                        lx = (posX / maxDist);
                        ly = (posY / maxDist);
                    }

                    if (rightAnalog.isPressed && rightAnalog.fingerId == event.getPointerId(i)) {
                        float posX = event.getX(i) - rightAnalog.x;
                        float posY = event.getY(i) - rightAnalog.y;

                        float maxDist = rightAnalog.radius / 4F;
                        float dist = (float) Math.sqrt(posX * posX + posY * posY);

                        if (dist > maxDist) {
                            float scale = maxDist / dist;
                            posX *= scale;
                            posY *= scale;
                        }

                        rightAnalog.fingerX = posX;
                        rightAnalog.fingerY = posY;

                        rx = (posX / maxDist);
                        ry = (posY / maxDist);
                    }

                    if (dpad.isPressed && dpad.fingerId == event.getPointerId(i)) {
                        float posX = event.getX(i) - dpad.x;
                        float posY = event.getY(i) - dpad.y;

                        dpad.fingerX = posX;
                        dpad.fingerY = posY;
                        dpad.dpadStatus = getAxisStatus(posX / dpad.radius, posY / dpad.radius, 0.25F);

                        dpadStatus = (byte) dpad.dpadStatus;
                    }
                }

                invalidate();
            }
            case MotionEvent.ACTION_POINTER_UP -> {
                for (VirtualControllerButton i : buttonList) {
                    if (i.fingerId == event.getPointerId(event.getActionIndex())) {
                        i.fingerId = -1;
                        handleButton(i, false);
                    }
                }

                if (leftAnalog.fingerId == event.getPointerId(event.getActionIndex())) {
                    leftAnalog.fingerId = -1;
                    leftAnalog.fingerX = 0F;
                    leftAnalog.fingerY = 0F;
                    leftAnalog.isPressed = false;

                    lx = 0F;
                    ly = 0F;
                }

                if (rightAnalog.fingerId == event.getPointerId(event.getActionIndex())) {
                    rightAnalog.fingerId = -1;
                    rightAnalog.fingerX = 0F;
                    rightAnalog.fingerY = 0F;
                    rightAnalog.isPressed = false;

                    rx = 0F;
                    ry = 0F;
                }

                if (dpad.fingerId == event.getPointerId(event.getActionIndex())) {
                    dpad.fingerId = -1;
                    dpad.fingerX = 0F;
                    dpad.fingerY = 0F;
                    dpad.isPressed = false;
                    dpad.dpadStatus = 0;

                    dpadStatus = 0;
                }

                invalidate();
            }
            case MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                for (VirtualControllerButton i : buttonList) {
                    if (i.isPressed) {
                        i.fingerId = -1;
                        handleButton(i, false);
                    }
                }

                leftAnalog.fingerId = -1;
                leftAnalog.fingerX = 0F;
                leftAnalog.fingerY = 0F;
                leftAnalog.isPressed = false;
                lx = 0F;
                ly = 0F;

                rightAnalog.fingerId = -1;
                rightAnalog.fingerX = 0F;
                rightAnalog.fingerY = 0F;
                rightAnalog.isPressed = false;
                rx = 0F;
                ry = 0F;

                dpad.fingerId = -1;
                dpad.fingerX = 0F;
                dpad.fingerY = 0F;
                dpad.isPressed = false;
                dpad.dpadStatus = 0;
                dpadStatus = 0;

                invalidate();
            }
        }

        if (listener != null) {
            listener.onControllerState(lx, ly, rx, ry, lt, rt, buttonsStateA, buttonsStateB, dpadStatus);
        }

        return true;
    }

    private void handleButton(VirtualControllerButton button, boolean isPressed) {
        button.isPressed = isPressed;
        android.util.Log.i("NFS-VirtualControls", "handleButton: " + getButtonName(button.id) + " pressed=" + isPressed);

        switch (button.id) {
            case A_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_A;
                else buttonsStateA &= ~MASK_A;
            }
            case B_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_B;
                else buttonsStateA &= ~MASK_B;
            }
            case X_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_X;
                else buttonsStateA &= ~MASK_X;
            }
            case Y_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_Y;
                else buttonsStateA &= ~MASK_Y;
            }
            case START_BUTTON -> {
                if (isPressed) buttonsStateB |= MASK_START;
                else buttonsStateB &= ~MASK_START;
            }
            case SELECT_BUTTON -> {
                if (isPressed) buttonsStateB |= MASK_SELECT;
                else buttonsStateB &= ~MASK_SELECT;
            }
            case LB_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_LB;
                else buttonsStateA &= ~MASK_LB;
            }
            case LT_BUTTON -> lt = isPressed ? 1F : 0F;
            case RB_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_RB;
                else buttonsStateA &= ~MASK_RB;
            }
            case RT_BUTTON -> rt = isPressed ? 1F : 0F;
            case LS_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_LS;
                else buttonsStateA &= ~MASK_LS;
            }
            case RS_BUTTON -> {
                if (isPressed) buttonsStateA |= MASK_RS;
                else buttonsStateA &= ~MASK_RS;
            }
        }
    }

    public static class VirtualControllerButton {
        public int id;
        public float x;
        public float y;
        public float radius;
        public int shape;
        public int fingerId = -1;
        public boolean isPressed = false;

        public VirtualControllerButton(int id, float x, float y, float radius, int shape) {
            this.id = id;
            this.x = x;
            this.y = y;
            this.radius = radius;
            this.shape = shape;
        }
    }

    public static class VirtualXInputDPad {
        public int id;
        public float x;
        public float y;
        public float radius;
        public int fingerId = -1;
        public boolean isPressed = false;
        public float fingerX = 0F;
        public float fingerY = 0F;
        public int dpadStatus = 0;

        public VirtualXInputDPad(int id, float x, float y, float radius) {
            this.id = id;
            this.x = x;
            this.y = y;
            this.radius = radius;
        }
    }

    public static class VirtualXInputAnalog {
        public int id;
        public float x;
        public float y;
        public float radius;
        public int fingerId = -1;
        public boolean isPressed = false;
        public float fingerX = 0F;
        public float fingerY = 0F;

        public VirtualXInputAnalog(int id, float x, float y, float radius) {
            this.id = id;
            this.x = x;
            this.y = y;
            this.radius = radius;
        }
    }
}

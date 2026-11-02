package org.etr.vr;

import android.app.NativeActivity;
import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Handler;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowManager;

/** NativeActivity wrapper for the standalone Google Cardboard build. */
public final class CardboardActivity extends NativeActivity
        implements InputManager.InputDeviceListener {
    private InputManager inputManager;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        enterImmersiveMode();

        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) {
            inputManager.registerInputDeviceListener(this,
                    new Handler(getMainLooper()));
            refreshGamepadConnection();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        enterImmersiveMode();
        refreshGamepadConnection();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) enterImmersiveMode();
    }

    @Override
    protected void onDestroy() {
        if (inputManager != null) {
            inputManager.unregisterInputDeviceListener(this);
            inputManager = null;
        }
        nativeSetGamepadConnected(false);
        super.onDestroy();
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        refreshGamepadConnection();
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        refreshGamepadConnection();
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        refreshGamepadConnection();
    }

    private void refreshGamepadConnection() {
        if (inputManager == null) return;
        boolean found = false;
        for (int deviceId : InputDevice.getDeviceIds()) {
            InputDevice device = inputManager.getInputDevice(deviceId);
            if (device == null) continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK)
                    == InputDevice.SOURCE_JOYSTICK) {
                found = true;
                break;
            }
        }
        nativeSetGamepadConnected(found);
    }

    @SuppressWarnings("deprecation")
    private void enterImmersiveMode() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private static native void nativeSetGamepadConnected(boolean connected);
}

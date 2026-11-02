/* --------------------------------------------------------------------
EXTREME TUXRACER -- platform layer internals

Shared between the sf:: compatibility modules and the Android/VR entry
point. Not included by game code.
---------------------------------------------------------------------*/

#ifndef ETR_PLATFORM_H
#define ETR_PLATFORM_H

#include "SFML/Window.hpp"

#include <string>

struct android_app;

/** Snapshot of the standard Android gamepad axes and buttons. The NDK
 * input callback fills this for a Bluetooth controller; the Cardboard VR
 * backend translates it into the game's existing joystick and key events. */
struct AndroidGamepadState {
	bool connected = false;
	float left_x = 0.f;
	float left_y = 0.f;
	float left_trigger = 0.f;
	float right_trigger = 0.f;
	bool a = false;
	bool b = false;
	bool x = false;
	bool y = false;
	bool left_bumper = false;
	bool right_bumper = false;
	bool menu = false;
	bool view = false;
	bool dpad_up = false;
	bool dpad_down = false;
	bool dpad_left = false;
	bool dpad_right = false;
	bool hat_up = false;
	bool hat_down = false;
	bool hat_left = false;
	bool hat_right = false;
};

namespace etr_platform {

/** Initialises the GL compatibility shim and the input state. The EGL
 *  context itself is created earlier, by android_main(). */
bool Init();
void Shutdown();

/** Size of the virtual 2D canvas the HUD and menus are laid out against.
 *  This is not a physical surface: in VR the result is composited into the
 *  eye buffers, which have a different size and aspect entirely. */
void GetDrawableSize(unsigned int& width, unsigned int& height);
void SetCanvasSize(unsigned int width, unsigned int height);

/** Drains the event queue, pumping the Android looper on the way through.
 *  Returns false when nothing is pending. */
bool PollEvent(sf::Event& event);

// Implemented in android_main.cpp.
::android_app* GetAndroidApp();
void PumpAndroidEvents();
bool IsResumed();
bool IsDestroyRequested();
bool HasDisplaySurface();
void GetDisplaySize(unsigned int& width, unsigned int& height);
bool PresentDisplay();
AndroidGamepadState GetGamepadState();

/** Injects a synthetic event so a VR backend can deliver controller and
 *  menu state through the game's existing input handlers. */
void PushEvent(const sf::Event& event);

/** Current state of the virtual joystick that the VR input layer drives. */
void SetJoystickAxis(unsigned int joystick, sf::Joystick::Axis axis,
                     float position);
void SetJoystickButton(unsigned int joystick, unsigned int button,
                       bool pressed);
float GetJoystickAxis(unsigned int joystick, sf::Joystick::Axis axis);
bool GetJoystickButton(unsigned int joystick, unsigned int button);
void SetJoystickConnected(unsigned int joystick, bool connected);

// --------------------------------------------------------------------
//	Assets
// --------------------------------------------------------------------

/** Writable per-app directory (Android internal storage). Both the
 *  extracted game data and the saved config live under here. */
const std::string& GetInternalDataPath();

/** Unpacks data.zip out of the APK's assets into the internal data path,
 *  the first time the app runs or whenever the stamped version changes.
 *  Reports progress as a 0..1 fraction. */
bool ExtractGameData(void (*progress)(float fraction) = nullptr);

}  // namespace etr_platform

#endif  // ETR_PLATFORM_H

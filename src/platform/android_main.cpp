/* --------------------------------------------------------------------
EXTREME TUXRACER -- Android entry point

The Quest flavor keeps its pbuffer/OpenXR lifecycle. The Cardboard flavor
also creates an EGL window surface, because the Cardboard SDK's distortion
renderer presents directly to the phone display.
---------------------------------------------------------------------*/

#include "platform.h"

#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#ifdef ENABLE_CARDBOARD
#include <cardboard.h>
#endif

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "ETRPlat", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ETRPlat", __VA_ARGS__)

/** The game's entry point, renamed on Android so that android_main() drives
 *  it (see the bottom of src/main.cpp). */
extern int etr_main(int argc, char** argv);

namespace etr_platform {
namespace {

struct AndroidState {
	android_app* app = nullptr;

	EGLDisplay display = EGL_NO_DISPLAY;
	EGLConfig config = nullptr;
	EGLContext context = EGL_NO_CONTEXT;
	EGLSurface surface = EGL_NO_SURFACE;
	EGLSurface pbuffer = EGL_NO_SURFACE;
	EGLSurface window_surface = EGL_NO_SURFACE;

	bool resumed = false;
	bool focused = true;
	bool destroy_requested = false;
	bool egl_ready = false;
	bool using_window_surface = false;

	std::mutex gamepad_mutex;
	AndroidGamepadState gamepad;
};

AndroidState g;

bool createWindowSurface();
void destroyWindowSurface();

float clampAxis(float value) {
	return std::max(-1.f, std::min(1.f, value));
}

float clampTrigger(float value) {
	return std::max(0.f, std::min(1.f, value));
}

bool isGamepadSource(int32_t source) {
	return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
	       (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK ||
	       (source & AINPUT_SOURCE_DPAD) == AINPUT_SOURCE_DPAD;
}

void setGamepadConnected(bool connected) {
	std::lock_guard<std::mutex> lock(g.gamepad_mutex);
	if (g.gamepad.connected == connected) return;
	g.gamepad = AndroidGamepadState();
	g.gamepad.connected = connected;
}

bool handleGamepadKey(int32_t key, bool pressed) {
	std::lock_guard<std::mutex> lock(g.gamepad_mutex);
	g.gamepad.connected = true;
	switch (key) {
		case AKEYCODE_BUTTON_A:      g.gamepad.a = pressed; break;
		case AKEYCODE_BUTTON_B:      g.gamepad.b = pressed; break;
		case AKEYCODE_BUTTON_X:      g.gamepad.x = pressed; break;
		case AKEYCODE_BUTTON_Y:      g.gamepad.y = pressed; break;
		case AKEYCODE_BUTTON_L1:     g.gamepad.left_bumper = pressed; break;
		case AKEYCODE_BUTTON_R1:     g.gamepad.right_bumper = pressed; break;
		case AKEYCODE_BUTTON_START:  g.gamepad.menu = pressed; break;
		case AKEYCODE_BUTTON_SELECT: g.gamepad.view = pressed; break;
		case AKEYCODE_BUTTON_L2:
			g.gamepad.left_trigger = pressed ? 1.f : 0.f;
			break;
		case AKEYCODE_BUTTON_R2:
			g.gamepad.right_trigger = pressed ? 1.f : 0.f;
			break;
		case AKEYCODE_DPAD_UP:    g.gamepad.dpad_up = pressed; break;
		case AKEYCODE_DPAD_DOWN:  g.gamepad.dpad_down = pressed; break;
		case AKEYCODE_DPAD_LEFT:  g.gamepad.dpad_left = pressed; break;
		case AKEYCODE_DPAD_RIGHT: g.gamepad.dpad_right = pressed; break;
		default: return false;
	}
	return true;
}

void handleGamepadMotion(const AInputEvent* event) {
	std::lock_guard<std::mutex> lock(g.gamepad_mutex);
	g.gamepad.connected = true;
	g.gamepad.left_x = clampAxis(
	    AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0));
	g.gamepad.left_y = clampAxis(
	    AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0));
	// Android standardizes the trigger axes to 0..1. Digital L2/R2 key events
	// are also accepted for controllers which expose only button events.
	g.gamepad.left_trigger = clampTrigger(
	    AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0));
	g.gamepad.right_trigger = clampTrigger(
	    AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0));

	const float hat_x = AMotionEvent_getAxisValue(
	    event, AMOTION_EVENT_AXIS_HAT_X, 0);
	const float hat_y = AMotionEvent_getAxisValue(
	    event, AMOTION_EVENT_AXIS_HAT_Y, 0);
	// Keep hat axes separate from D-pad key events: some controllers emit
	// both, and a neutral hat sample must not cancel a still-held key event.
	g.gamepad.hat_left = hat_x < -0.5f;
	g.gamepad.hat_right = hat_x > 0.5f;
	g.gamepad.hat_up = hat_y < -0.5f;
	g.gamepad.hat_down = hat_y > 0.5f;
}

int32_t onInputEvent(android_app*, AInputEvent* event) {
	if (!event || !isGamepadSource(AInputEvent_getSource(event))) return 0;

	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
		const int32_t action = AKeyEvent_getAction(event);
		if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP)
			return 0;
		return handleGamepadKey(AKeyEvent_getKeyCode(event),
		                        action == AKEY_EVENT_ACTION_DOWN) ? 1 : 0;
	}

	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
		if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) !=
		    AMOTION_EVENT_ACTION_MOVE)
			return 0;
		handleGamepadMotion(event);
		return 1;
	}
	return 0;
}

void onAppCmd(android_app* /*app*/, int32_t cmd) {
	switch (cmd) {
		case APP_CMD_RESUME:
			g.resumed = true;
			break;
		case APP_CMD_PAUSE:
			g.resumed = false;
			break;
		case APP_CMD_GAINED_FOCUS:
			g.focused = true;
			break;
		case APP_CMD_LOST_FOCUS:
			g.focused = false;
			break;
		case APP_CMD_INIT_WINDOW:
#ifdef ENABLE_CARDBOARD
			if (g.egl_ready) createWindowSurface();
#endif
			break;
		case APP_CMD_TERM_WINDOW:
#ifdef ENABLE_CARDBOARD
			destroyWindowSurface();
#endif
			break;
		case APP_CMD_DESTROY:
			g.destroy_requested = true;
			break;
		default:
			break;
	}
}

bool createEGL() {
	g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (g.display == EGL_NO_DISPLAY) {
		LOGE("eglGetDisplay failed");
		return false;
	}

	EGLint major = 0, minor = 0;
	if (!eglInitialize(g.display, &major, &minor)) {
		LOGE("eglInitialize failed");
		return false;
	}

#ifdef ENABLE_CARDBOARD
	const EGLint surface_type = EGL_WINDOW_BIT | EGL_PBUFFER_BIT;
#else
	const EGLint surface_type = EGL_PBUFFER_BIT;
#endif
	const EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_SURFACE_TYPE,    surface_type,
		EGL_RED_SIZE,        8,
		EGL_GREEN_SIZE,      8,
		EGL_BLUE_SIZE,       8,
		EGL_ALPHA_SIZE,      8,
		EGL_DEPTH_SIZE,      0,
		EGL_STENCIL_SIZE,    0,
		EGL_NONE
	};
	EGLint num_config = 0;
	if (!eglChooseConfig(g.display, cfg_attr, &g.config, 1, &num_config) ||
	    num_config < 1) {
		LOGE("no GLES3 EGL config available");
		return false;
	}

	const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	g.context = eglCreateContext(g.display, g.config, EGL_NO_CONTEXT, ctx_attr);
	if (g.context == EGL_NO_CONTEXT) {
		LOGE("eglCreateContext failed");
		return false;
	}

	const EGLint surf_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	g.pbuffer = eglCreatePbufferSurface(g.display, g.config, surf_attr);
	if (g.pbuffer == EGL_NO_SURFACE) {
		LOGE("eglCreatePbufferSurface failed");
		return false;
	}
	g.surface = g.pbuffer;
	if (!eglMakeCurrent(g.display, g.surface, g.surface, g.context)) {
		LOGE("eglMakeCurrent failed");
		return false;
	}

	g.egl_ready = true;
#ifdef ENABLE_CARDBOARD
	if (g.app && g.app->window) createWindowSurface();
#endif

	LOGI("EGL %d.%d, %s / %s", major, minor, glGetString(GL_RENDERER),
	     glGetString(GL_VERSION));
	return true;
}

bool createWindowSurface() {
#ifdef ENABLE_CARDBOARD
	if (!g.egl_ready || !g.app || !g.app->window) return false;
	if (g.window_surface != EGL_NO_SURFACE) return true;

	EGLint visual = 0;
	if (eglGetConfigAttrib(g.display, g.config, EGL_NATIVE_VISUAL_ID, &visual))
		ANativeWindow_setBuffersGeometry(g.app->window, 0, 0, visual);

	g.window_surface = eglCreateWindowSurface(
	    g.display, g.config, g.app->window, nullptr);
	if (g.window_surface == EGL_NO_SURFACE) {
		LOGE("eglCreateWindowSurface failed (0x%x)", eglGetError());
		return false;
	}
	if (!eglMakeCurrent(g.display, g.window_surface, g.window_surface,
	                    g.context)) {
		LOGE("eglMakeCurrent(window) failed (0x%x)", eglGetError());
		eglDestroySurface(g.display, g.window_surface);
		g.window_surface = EGL_NO_SURFACE;
		return false;
	}
	g.surface = g.window_surface;
	g.using_window_surface = true;
	eglSwapInterval(g.display, 1);
	LOGI("Cardboard display surface %dx%d",
	     ANativeWindow_getWidth(g.app->window),
	     ANativeWindow_getHeight(g.app->window));
	return true;
#else
	return false;
#endif
}

void destroyWindowSurface() {
#ifdef ENABLE_CARDBOARD
	if (g.window_surface == EGL_NO_SURFACE || g.display == EGL_NO_DISPLAY)
		return;
	if (g.context != EGL_NO_CONTEXT && g.pbuffer != EGL_NO_SURFACE) {
		eglMakeCurrent(g.display, g.pbuffer, g.pbuffer, g.context);
		g.surface = g.pbuffer;
	}
	eglDestroySurface(g.display, g.window_surface);
	g.window_surface = EGL_NO_SURFACE;
	g.using_window_surface = false;
#endif
}

void destroyEGL() {
	if (g.display == EGL_NO_DISPLAY) return;
	destroyWindowSurface();
	eglMakeCurrent(g.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (g.pbuffer != EGL_NO_SURFACE) eglDestroySurface(g.display, g.pbuffer);
	if (g.context != EGL_NO_CONTEXT) eglDestroyContext(g.display, g.context);
	eglTerminate(g.display);
	g.display = EGL_NO_DISPLAY;
	g.surface = EGL_NO_SURFACE;
	g.pbuffer = EGL_NO_SURFACE;
	g.context = EGL_NO_CONTEXT;
	g.egl_ready = false;
}

}  // namespace

// --------------------------------------------------------------------
// Platform interface
// --------------------------------------------------------------------

android_app* GetAndroidApp() { return g.app; }
bool IsResumed() { return g.resumed && g.focused; }
bool IsDestroyRequested() {
	return g.destroy_requested || (g.app && g.app->destroyRequested);
}
bool HasDisplaySurface() { return g.using_window_surface; }

void GetDisplaySize(unsigned int& width, unsigned int& height) {
	width = height = 0;
	if (!g.app || !g.app->window) return;
	width = static_cast<unsigned int>(std::max(0, ANativeWindow_getWidth(g.app->window)));
	height = static_cast<unsigned int>(std::max(0, ANativeWindow_getHeight(g.app->window)));
}

bool PresentDisplay() {
#ifdef ENABLE_CARDBOARD
	return g.using_window_surface && g.display != EGL_NO_DISPLAY &&
	       eglSwapBuffers(g.display, g.window_surface) == EGL_TRUE;
#else
	return false;
#endif
}

AndroidGamepadState GetGamepadState() {
	std::lock_guard<std::mutex> lock(g.gamepad_mutex);
	AndroidGamepadState state = g.gamepad;
	state.dpad_up = state.dpad_up || state.hat_up;
	state.dpad_down = state.dpad_down || state.hat_down;
	state.dpad_left = state.dpad_left || state.hat_left;
	state.dpad_right = state.dpad_right || state.hat_right;
	return state;
}

void SetGamepadConnection(bool connected) { setGamepadConnected(connected); }

/** Drains pending Android events without blocking. Called from the event
 * pump every frame; blocking here would stall the VR frame loop. */
void PumpAndroidEvents() {
	if (!g.app) return;
	for (;;) {
		int events = 0;
		android_poll_source* source = nullptr;
		if (ALooper_pollOnce(0, nullptr, &events,
		                     reinterpret_cast<void**>(&source)) < 0)
			break;
		if (source) source->process(g.app, source);
	}
}

}  // namespace etr_platform

// --------------------------------------------------------------------
// Cardboard Activity callbacks
// --------------------------------------------------------------------

#ifdef ENABLE_CARDBOARD
extern "C" JNIEXPORT void JNICALL
Java_org_etr_vr_CardboardActivity_nativeInitializeCardboard(
    JNIEnv* env, jobject activity) {
	JavaVM* vm = nullptr;
	if (env->GetJavaVM(&vm) == JNI_OK && vm)
		Cardboard_initializeAndroid(vm, activity);
}

extern "C" JNIEXPORT void JNICALL
Java_org_etr_vr_CardboardActivity_nativeSetGamepadConnected(
    JNIEnv*, jclass, jboolean connected) {
	etr_platform::SetGamepadConnection(connected == JNI_TRUE);
}
#endif

// --------------------------------------------------------------------
// NativeActivity entry point
// --------------------------------------------------------------------

void android_main(android_app* app) {
	etr_platform::g.app = app;
	app->onAppCmd = etr_platform::onAppCmd;
	app->onInputEvent = etr_platform::onInputEvent;

	// The Cardboard SDK launches Java activities through JNI; keep this native
	// thread attached for the lifetime of the activity.
	JNIEnv* env = nullptr;
	app->activity->vm->AttachCurrentThread(&env, nullptr);

	while (!app->destroyRequested && !etr_platform::g.resumed) {
		etr_platform::PumpAndroidEvents();
		usleep(10000);
	}

	if (!app->destroyRequested) {
		if (etr_platform::createEGL()) {
			etr_main(0, nullptr);
		} else {
			LOGE("could not create a GLES 3 context -- exiting");
		}
	}

	etr_platform::destroyEGL();
	app->activity->vm->DetachCurrentThread();
	ANativeActivity_finish(app->activity);
}

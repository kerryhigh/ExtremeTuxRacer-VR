/* --------------------------------------------------------------------
EXTREME TUXRACER -- Google Cardboard backend

This backend shares the game's existing GLES compatibility renderer and
one-update/two-eye frame loop with Quest. Google Cardboard supplies the
viewer profile, eye projections, eye separation, sensor pose and final lens
distortion. Phone translation is intentionally ignored.
---------------------------------------------------------------------*/

#include "vr.h"

#include "../platform/platform.h"
#include "../platform/android_gamepad_mapping.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <cardboard.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <unistd.h>

extern "C" void etrgl_InvalidateState(void);

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "ETRCardboard", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "ETRCardboard", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ETRCardboard", __VA_ARGS__)

namespace vr {

double world_scale = 1.0;

namespace {

const unsigned int kCanvasWidth = 1280;
const unsigned int kCanvasHeight = 720;
const double kMenuPlaneWidthMetres = 2.6;
const double kMenuPlaneDistanceMetres = 2.0;
const int64_t kPosePredictionNanos = 50000000;
const float kMenuDeadzone = 0.55f;
const double kMenuRepeatDelay = 0.38;
const double kMenuRepeatInterval = 0.12;
const unsigned int kButtonPaddle = 0;
const unsigned int kButtonTrick = 1;
const unsigned int kButtonBrake = 2;
const unsigned int kButtonCharge = 3;

struct EyeTarget {
	GLuint framebuffer = 0;
	GLuint color_texture = 0;
	GLuint depth_buffer = 0;
	CardboardEyeTextureDescription texture{};
};

struct CardboardState {
	bool initialized = false;
	bool frame_live = false;
	bool tracker_paused = true;
	bool in_screen = false;
	bool screen_ready = false;
	bool panel_presented = false;
	bool eye_rendered[2] = {false, false};
	bool auto_scan_requested = false;
	int current_eye = -1;

	CardboardHeadTracker* tracker = nullptr;
	CardboardLensDistortion* lens = nullptr;
	CardboardDistortionRenderer* distortion = nullptr;
	int device_params_change_count = -1;

	int display_width = 0;
	int display_height = 0;
	int eye_width = 0;
	int eye_height = 0;
	int screen_width = kCanvasWidth;
	int screen_height = kCanvasHeight;
	EyeTarget eyes[2];
	GLuint screen_framebuffer = 0;
	GLuint screen_texture = 0;
	GLuint screen_depth = 0;

	float eye_from_head[2][16] = {};
	float panel_projection[2][16] = {};
	float field_of_view[2][4] = {};
	float head_orientation[4] = {0.f, 0.f, 0.f, 1.f};
	TMatrix<4, 4> head_view;

	GLuint panel_program = 0;
	GLuint panel_vertex_array = 0;
	GLuint panel_vertex_buffer = 0;
	GLint panel_mvp_location = -1;
	GLint panel_texture_location = -1;

	bool ui_navigation = false;
	int held_x = 0;
	int held_y = 0;
	double repeat_at = 0.0;
	bool was_confirm = false;
	bool was_cancel = false;
	bool was_pause = false;
	bool was_restart = false;
	bool was_camera = false;
	bool was_hud = false;
	bool was_recenter = false;
	double viewer_calibration_started = 0.0;
	bool viewer_chord_fired = false;
};

CardboardState g;

int64_t bootTimeNanos() {
	struct timespec value{};
	clock_gettime(CLOCK_BOOTTIME, &value);
	return static_cast<int64_t>(value.tv_sec) * 1000000000LL + value.tv_nsec;
}

double nowSeconds() {
	struct timespec value{};
	clock_gettime(CLOCK_MONOTONIC, &value);
	return static_cast<double>(value.tv_sec) +
	       static_cast<double>(value.tv_nsec) / 1000000000.0;
}

void pushKey(sf::Keyboard::Key key, bool pressed) {
	sf::Event event{};
	event.type = pressed ? sf::Event::KeyPressed : sf::Event::KeyReleased;
	event.key.code = key;
	event.key.alt = event.key.control = event.key.shift = event.key.system = false;
	etr_platform::PushEvent(event);
}

void tapKey(sf::Keyboard::Key key) {
	pushKey(key, true);
	pushKey(key, false);
}

void navigateMenu(int dx, int dy, double now) {
	if (dx != g.held_x || dy != g.held_y) {
		g.held_x = dx;
		g.held_y = dy;
		g.repeat_at = now + kMenuRepeatDelay;
		if (dy > 0)      tapKey(sf::Keyboard::Up);
		else if (dy < 0) tapKey(sf::Keyboard::Down);
		else if (dx < 0) tapKey(sf::Keyboard::Left);
		else if (dx > 0) tapKey(sf::Keyboard::Right);
	} else if ((dx || dy) && now >= g.repeat_at) {
		g.repeat_at = now + kMenuRepeatInterval;
		if (dy > 0)      tapKey(sf::Keyboard::Up);
		else if (dy < 0) tapKey(sf::Keyboard::Down);
		else if (dx < 0) tapKey(sf::Keyboard::Left);
		else if (dx > 0) tapKey(sf::Keyboard::Right);
	}
}

int quantise(float value) {
	return value < -kMenuDeadzone ? -1 : value > kMenuDeadzone ? 1 : 0;
}

GLuint compileShader(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled == GL_TRUE) return shader;
	GLint log_length = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 1) {
		char log[1024] = {};
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		LOGE("panel shader compilation failed: %s", log);
	}
	glDeleteShader(shader);
	return 0;
}

bool createPanelRenderer() {
	static const char* vertex_source =
	    "#version 300 es\n"
	    "layout(location=0) in vec3 aPosition;\n"
	    "layout(location=1) in vec2 aTexCoord;\n"
	    "uniform mat4 uMvp;\n"
	    "out vec2 vTexCoord;\n"
	    "void main() { vTexCoord = aTexCoord; gl_Position = uMvp * vec4(aPosition, 1.0); }\n";
	static const char* fragment_source =
	    "#version 300 es\n"
	    "precision mediump float;\n"
	    "in vec2 vTexCoord;\n"
	    "uniform sampler2D uScreen;\n"
	    "out vec4 fragmentColor;\n"
	    "void main() { fragmentColor = texture(uScreen, vTexCoord); }\n";

	const GLuint vertex_shader = compileShader(GL_VERTEX_SHADER, vertex_source);
	const GLuint fragment_shader = compileShader(GL_FRAGMENT_SHADER, fragment_source);
	if (!vertex_shader || !fragment_shader) {
		if (vertex_shader) glDeleteShader(vertex_shader);
		if (fragment_shader) glDeleteShader(fragment_shader);
		return false;
	}

	g.panel_program = glCreateProgram();
	glAttachShader(g.panel_program, vertex_shader);
	glAttachShader(g.panel_program, fragment_shader);
	glLinkProgram(g.panel_program);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	GLint linked = GL_FALSE;
	glGetProgramiv(g.panel_program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE) {
		GLint log_length = 0;
		glGetProgramiv(g.panel_program, GL_INFO_LOG_LENGTH, &log_length);
		if (log_length > 1) {
			char log[1024] = {};
			glGetProgramInfoLog(g.panel_program, sizeof(log), nullptr, log);
			LOGE("panel shader link failed: %s", log);
		}
		glDeleteProgram(g.panel_program);
		g.panel_program = 0;
		return false;
	}

	static const GLfloat vertices[] = {
		-1.f, -1.f, 0.f, 0.f, 0.f,
		 1.f, -1.f, 0.f, 1.f, 0.f,
		-1.f,  1.f, 0.f, 0.f, 1.f,
		 1.f,  1.f, 0.f, 1.f, 1.f,
	};
	glGenVertexArrays(1, &g.panel_vertex_array);
	glGenBuffers(1, &g.panel_vertex_buffer);
	glBindVertexArray(g.panel_vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, g.panel_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
	                       reinterpret_cast<void*>(3 * sizeof(GLfloat)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	g.panel_mvp_location = glGetUniformLocation(g.panel_program, "uMvp");
	g.panel_texture_location = glGetUniformLocation(g.panel_program, "uScreen");
	return g.panel_mvp_location >= 0 && g.panel_texture_location >= 0;
}

void destroyTarget(EyeTarget& target) {
	if (target.depth_buffer) glDeleteRenderbuffers(1, &target.depth_buffer);
	if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
	if (target.color_texture) glDeleteTextures(1, &target.color_texture);
	target = EyeTarget();
}

void destroyEyeTargets() {
	destroyTarget(g.eyes[0]);
	destroyTarget(g.eyes[1]);
	g.eye_width = 0;
	g.eye_height = 0;
}

bool createTarget(EyeTarget& target, int width, int height) {
	if (width <= 0 || height <= 0) return false;
	glGenTextures(1, &target.color_texture);
	glBindTexture(GL_TEXTURE_2D, target.color_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glGenRenderbuffers(1, &target.depth_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, target.depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);

	glGenFramebuffers(1, &target.framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
	                       target.color_texture, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	                         GL_RENDERBUFFER, target.depth_buffer);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		LOGE("incomplete Cardboard render target (%dx%d)", width, height);
		destroyTarget(target);
		return false;
	}

	target.texture.texture = target.color_texture;
	target.texture.left_u = 0.f;
	target.texture.right_u = 1.f;
	target.texture.top_v = 1.f;
	target.texture.bottom_v = 0.f;
	return true;
}

bool createScreenTarget() {
	EyeTarget target;
	if (!createTarget(target, g.screen_width, g.screen_height)) return false;
	g.screen_framebuffer = target.framebuffer;
	g.screen_texture = target.color_texture;
	g.screen_depth = target.depth_buffer;
	return true;
}

bool refreshEyeTargets(int width, int height) {
	if (width < 2 || height < 2) return false;
	const int half_width = (width + 1) / 2;
	if (g.eye_width == half_width && g.eye_height == height &&
	    g.eyes[0].framebuffer && g.eyes[1].framebuffer)
		return true;

	destroyEyeTargets();
	if (!createTarget(g.eyes[0], half_width, height) ||
	    !createTarget(g.eyes[1], half_width, height)) {
		destroyEyeTargets();
		return false;
	}
	g.eye_width = half_width;
	g.eye_height = height;
	return true;
}

void multiplyMatrix(const float* left, const float* right, float* out) {
	float result[16] = {};
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			for (int k = 0; k < 4; ++k)
				result[column * 4 + row] +=
				    left[k * 4 + row] * right[column * 4 + k];
		}
	}
	std::copy(result, result + 16, out);
}

void makeHeadView() {
	const float x = g.head_orientation[0];
	const float y = g.head_orientation[1];
	const float z = g.head_orientation[2];
	const float w = g.head_orientation[3];
	const float xx = 2.f * x * x;
	const float yy = 2.f * y * y;
	const float zz = 2.f * z * z;
	const float xy = 2.f * x * y;
	const float xz = 2.f * x * z;
	const float yz = 2.f * y * z;
	const float xw = 2.f * x * w;
	const float yw = 2.f * y * w;
	const float zw = 2.f * z * w;

	// This uses the same quaternion-to-matrix convention as the official
	// Cardboard NDK sample. Sensor translation is intentionally ignored.
	g.head_view.SetIdentity();
	g.head_view[0][0] = 1.f - yy - zz;
	g.head_view[0][1] = xy + zw;
	g.head_view[0][2] = xz - yw;
	g.head_view[1][0] = xy - zw;
	g.head_view[1][1] = 1.f - xx - zz;
	g.head_view[1][2] = yz + xw;
	g.head_view[2][0] = xz + yw;
	g.head_view[2][1] = yz - xw;
	g.head_view[2][2] = 1.f - xx - yy;
}

void requestViewerCalibration() {
	if (!g.initialized) return;
	CardboardQrCode_scanQrCodeAndSaveDeviceParams();
	LOGI("opened the Cardboard viewer QR scanner");
}

bool updateDeviceParams() {
	const int changed_count = CardboardQrCode_getDeviceParamsChangedCount();
	if (g.lens && changed_count == g.device_params_change_count) return true;

	uint8_t* encoded = nullptr;
	int size = 0;
	CardboardQrCode_getSavedDeviceParams(&encoded, &size);
	if (size <= 0) {
		CardboardQrCode_destroy(encoded);
		encoded = nullptr;
		size = 0;
		if (!g.auto_scan_requested) {
			g.auto_scan_requested = true;
			requestViewerCalibration();
		}
		// Use the SDK's Cardboard V1 fallback so the game still starts when a
		// viewer QR is not immediately available. Scanning replaces this profile.
		CardboardQrCode_getCardboardV1DeviceParams(&encoded, &size);
	}
	if (!encoded || size <= 0) {
		CardboardQrCode_destroy(encoded);
		LOGE("Cardboard SDK did not return saved or default viewer parameters");
		return false;
	}

	CardboardLensDistortion* new_lens = CardboardLensDistortion_create(
	    encoded, size, g.display_width, g.display_height);
	CardboardQrCode_destroy(encoded);
	if (!new_lens) {
		LOGE("could not create Cardboard lens profile");
		return false;
	}

	CardboardDistortionRenderer_destroy(g.distortion);
	g.distortion = nullptr;
	CardboardLensDistortion_destroy(g.lens);
	g.lens = new_lens;
	const CardboardOpenGlEsDistortionRendererConfig config{kGlTexture2D};
	g.distortion = CardboardOpenGlEs3DistortionRenderer_create(&config);
	if (!g.distortion) {
		LOGE("could not create the Cardboard GLES distortion renderer");
		return false;
	}

	CardboardMesh left_mesh{};
	CardboardMesh right_mesh{};
	CardboardLensDistortion_getDistortionMesh(g.lens, kLeft, &left_mesh);
	CardboardLensDistortion_getDistortionMesh(g.lens, kRight, &right_mesh);
	CardboardDistortionRenderer_setMesh(g.distortion, &left_mesh, kLeft);
	CardboardDistortionRenderer_setMesh(g.distortion, &right_mesh, kRight);

	CardboardLensDistortion_getEyeFromHeadMatrix(g.lens, kLeft,
	                                             g.eye_from_head[0]);
	CardboardLensDistortion_getEyeFromHeadMatrix(g.lens, kRight,
	                                             g.eye_from_head[1]);
	CardboardLensDistortion_getFieldOfView(g.lens, kLeft,
	                                       g.field_of_view[0]);
	CardboardLensDistortion_getFieldOfView(g.lens, kRight,
	                                       g.field_of_view[1]);
	for (int eye = 0; eye < 2; ++eye) {
		CardboardLensDistortion_getProjectionMatrix(
		    g.lens, eye == 0 ? kLeft : kRight, 0.1f, 1000.f,
		    g.panel_projection[eye]);
	}
	g.device_params_change_count = changed_count;
	LOGI("loaded Cardboard viewer parameters for %dx%d display",
	     g.display_width, g.display_height);
	return true;
}

void updateGamepadInput() {
	const AndroidGamepadState pad = etr_platform::GetGamepadState();
	const AndroidGamepadGameplayControls controls =
	    MapAndroidGamepadToGameplay(pad);
	etr_platform::SetJoystickConnected(0, pad.connected);

	const float paddle = controls.paddle;
	const float brake = controls.brake;
	const float steer = controls.steering;
	const bool confirm = pad.connected && pad.a;
	const bool cancel = pad.connected && pad.b;
	const double now = nowSeconds();

	if (g.ui_navigation) {
		// Menus are controller-only: turn the left stick and D-pad into the
		// same arrow-key events used by the original GUI everywhere in the game.
		etr_platform::SetJoystickAxis(0, sf::Joystick::X, 0.f);
		etr_platform::SetJoystickAxis(0, sf::Joystick::Y, 0.f);
		etr_platform::SetJoystickButton(0, kButtonPaddle, false);
		etr_platform::SetJoystickButton(0, kButtonTrick, false);
		etr_platform::SetJoystickButton(0, kButtonBrake, false);
		etr_platform::SetJoystickButton(0, kButtonCharge, false);

		int dx = pad.dpad_left ? -1 : pad.dpad_right ? 1 : quantise(pad.left_x);
		const float menu_y = -pad.left_y;
		int dy = pad.dpad_up ? 1 : pad.dpad_down ? -1 : quantise(menu_y);
		if (dy != 0) dx = 0;
		navigateMenu(dx, dy, now);

		if (confirm && !g.was_confirm) tapKey(sf::Keyboard::Return);
		if (cancel && !g.was_cancel) tapKey(sf::Keyboard::Escape);
		g.was_confirm = confirm;
		g.was_cancel = cancel;
		g.was_pause = g.was_restart = g.was_camera = g.was_hud = false;
		g.was_recenter = pad.connected && pad.right_bumper;

		// In menus, holding Menu + View opens the SDK's QR calibration flow.
		if (pad.connected && pad.menu && pad.view) {
			if (g.viewer_calibration_started == 0.0)
				g.viewer_calibration_started = now;
			if (!g.viewer_chord_fired &&
			    now - g.viewer_calibration_started >= 0.9) {
				requestViewerCalibration();
				g.viewer_chord_fired = true;
			}
		} else {
			g.viewer_calibration_started = 0.0;
			g.viewer_chord_fired = false;
		}
		return;
	}

	g.held_x = g.held_y = 0;
	g.repeat_at = 0.0;
	g.viewer_calibration_started = 0.0;
	g.viewer_chord_fired = false;
	etr_platform::SetJoystickAxis(0, sf::Joystick::X, steer);
	// The game reads negative Y as paddle and positive Y as brake.
	etr_platform::SetJoystickAxis(0, sf::Joystick::Y, brake - paddle);
	etr_platform::SetJoystickButton(0, kButtonPaddle, paddle > 0.25f);
	etr_platform::SetJoystickButton(0, kButtonBrake, brake > 0.25f);
	etr_platform::SetJoystickButton(0, kButtonCharge, controls.jump_charge);
	etr_platform::SetJoystickButton(0, kButtonTrick,
	                                controls.trick_modifier);

	const bool menu_now = pad.connected && pad.menu;
	if (menu_now && !g.was_pause) tapKey(sf::Keyboard::P);
	g.was_pause = menu_now;
	const bool restart_now = pad.connected && pad.view;
	if (restart_now && !g.was_restart) tapKey(sf::Keyboard::R);
	g.was_restart = restart_now;
	const bool camera_now = pad.connected && pad.y;
	if (camera_now && !g.was_camera) tapKey(sf::Keyboard::Y);
	g.was_camera = camera_now;
	const bool hud_now = pad.connected && pad.x;
	if (hud_now && !g.was_hud) tapKey(sf::Keyboard::X);
	g.was_hud = hud_now;
	const bool recenter_now = pad.connected && pad.right_bumper;
	if (recenter_now && !g.was_recenter) {
		CardboardHeadTracker_recenter(g.tracker);
		LOGI("head orientation recentered");
	}
	g.was_recenter = recenter_now;
	g.was_confirm = g.was_cancel = false;
}

void destroyScreenTarget() {
	if (g.screen_depth) glDeleteRenderbuffers(1, &g.screen_depth);
	if (g.screen_framebuffer) glDeleteFramebuffers(1, &g.screen_framebuffer);
	if (g.screen_texture) glDeleteTextures(1, &g.screen_texture);
	g.screen_depth = g.screen_framebuffer = g.screen_texture = 0;
}

bool refreshDisplay() {
	unsigned int width = 0, height = 0;
	etr_platform::GetDisplaySize(width, height);
	if (width < 2 || height < 2) return false;
	if (g.display_width == static_cast<int>(width) &&
	    g.display_height == static_cast<int>(height) && g.eyes[0].framebuffer)
		return true;

	g.display_width = static_cast<int>(width);
	g.display_height = static_cast<int>(height);
	if (!refreshEyeTargets(g.display_width, g.display_height)) return false;
	g.device_params_change_count = -1;
	return true;
}

void drawFloatingScreen() {
	if (!g.screen_ready || !g.lens || !g.panel_program) return;
	const double panel_height = kMenuPlaneWidthMetres *
	                           static_cast<double>(g.screen_height) /
	                           static_cast<double>(g.screen_width);
	const float model[16] = {
		static_cast<float>(kMenuPlaneWidthMetres * 0.5), 0.f, 0.f, 0.f,
		0.f, static_cast<float>(panel_height * 0.5), 0.f, 0.f,
		0.f, 0.f, 1.f, 0.f,
		0.f, 0.f, static_cast<float>(-kMenuPlaneDistanceMetres), 1.f
	};

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(g.panel_program);
	glBindVertexArray(g.panel_vertex_array);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g.screen_texture);
	glUniform1i(g.panel_texture_location, 0);

	for (int eye = 0; eye < 2; ++eye) {
		glBindFramebuffer(GL_FRAMEBUFFER, g.eyes[eye].framebuffer);
		glViewport(0, 0, g.eye_width, g.display_height);
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float eye_head[16];
		float eye_head_model[16];
		float mvp[16];
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				eye_head[c * 4 + r] = static_cast<float>(g.eye_from_head[eye][c * 4 + r]);
		float head[16];
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				head[c * 4 + r] = static_cast<float>(g.head_view[c][r]);
		multiplyMatrix(eye_head, head, eye_head_model);
		multiplyMatrix(eye_head_model, model, eye_head_model);
		multiplyMatrix(g.panel_projection[eye], eye_head_model, mvp);
		glUniformMatrix4fv(g.panel_mvp_location, 1, GL_FALSE, mvp);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	g.panel_presented = true;
}

}  // namespace

bool Init() {
	if (g.initialized) return true;
	android_app* app = etr_platform::GetAndroidApp();
	if (!app || !app->activity || !app->activity->vm) {
		LOGE("Cardboard has no Android activity");
		return false;
	}

	Cardboard_initializeAndroid(app->activity->vm, app->activity->clazz);
	g.tracker = CardboardHeadTracker_create();
	if (!g.tracker) {
		LOGE("Cardboard head tracker could not be created");
		return false;
	}
	if (!createPanelRenderer()) {
		LOGE("Cardboard menu panel renderer could not be created");
		return false;
	}
	etr_platform::SetCanvasSize(kCanvasWidth, kCanvasHeight);
	if (!createScreenTarget()) {
		LOGE("Cardboard menu screen target could not be created");
		return false;
	}
	g.initialized = true;
	LOGI("Cardboard initialized; viewer parameters will come from the SDK");
	return true;
}

void Shutdown() {
	if (g.tracker && !g.tracker_paused)
		CardboardHeadTracker_pause(g.tracker);
	CardboardDistortionRenderer_destroy(g.distortion);
	CardboardLensDistortion_destroy(g.lens);
	CardboardHeadTracker_destroy(g.tracker);
	g.distortion = nullptr;
	g.lens = nullptr;
	g.tracker = nullptr;
	destroyEyeTargets();
	destroyScreenTarget();
	if (g.panel_vertex_buffer) glDeleteBuffers(1, &g.panel_vertex_buffer);
	if (g.panel_vertex_array) glDeleteVertexArrays(1, &g.panel_vertex_array);
	if (g.panel_program) glDeleteProgram(g.panel_program);
	g = CardboardState();
}

bool IsActive() { return g.initialized; }
bool IsSessionRunning() {
	return g.initialized && etr_platform::IsResumed() &&
	       etr_platform::HasDisplaySurface();
}
bool IsExitRequested() { return etr_platform::IsDestroyRequested(); }

bool BeginFrame() {
	// NativeActivity lifecycle and controller callbacks share this looper.
	// Pump before checking the surface/resume state so APP_CMD_RESUME can
	// recover a frame loop that is currently skipping rendering.
	etr_platform::PumpAndroidEvents();
	if (!g.initialized || etr_platform::IsDestroyRequested()) return false;
	if (!etr_platform::IsResumed() || !etr_platform::HasDisplaySurface()) {
		if (!g.tracker_paused && g.tracker) {
			CardboardHeadTracker_pause(g.tracker);
			g.tracker_paused = true;
		}
		usleep(10000);
		return false;
	}

	if (g.tracker_paused && g.tracker) {
		CardboardHeadTracker_resume(g.tracker);
		g.tracker_paused = false;
	}
	if (!refreshDisplay()) return false;
	if (!updateDeviceParams()) return false;
	if (!g.distortion || !g.eyes[0].framebuffer || !g.eyes[1].framebuffer)
		return false;

	float position[3] = {0.f, 0.f, 0.f};
	CardboardHeadTracker_getPose(
	    g.tracker, bootTimeNanos() + kPosePredictionNanos, kLandscapeLeft,
	    position, g.head_orientation);
	makeHeadView();

	g.eye_rendered[0] = g.eye_rendered[1] = false;
	g.current_eye = -1;
	g.screen_ready = false;
	g.panel_presented = false;
	g.frame_live = true;
	updateGamepadInput();
	return true;
}

void EndFrame() {
	if (!g.frame_live) return;
	const bool complete_game_frame = g.eye_rendered[0] && g.eye_rendered[1];
	if (g.distortion && (complete_game_frame || g.panel_presented)) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		CardboardDistortionRenderer_renderEyeToDisplay(
		    g.distortion, 0, 0, 0, g.display_width, g.display_height,
		    &g.eyes[0].texture, &g.eyes[1].texture);
		if (!etr_platform::PresentDisplay())
			LOGW("could not present Cardboard frame (EGL 0x%x)", eglGetError());
	}
	etrgl_InvalidateState();
	g.frame_live = false;
	g.current_eye = -1;
}

int NumEyes() { return 2; }
int CurrentEye() { return g.current_eye; }

void BeginEye(int eye) {
	if (!g.frame_live || eye < 0 || eye > 1 || !g.eyes[eye].framebuffer)
		return;
	g.current_eye = eye;
	glBindFramebuffer(GL_FRAMEBUFFER, g.eyes[eye].framebuffer);
	glViewport(0, 0, g.eye_width, g.display_height);
}

void EndEye(int eye) {
	if (g.current_eye != eye) return;
	g.eye_rendered[eye] = true;
	g.current_eye = -1;
}

bool BeginScreen() {
	if (!g.frame_live || !g.screen_framebuffer) return false;
	g.in_screen = true;
	glBindFramebuffer(GL_FRAMEBUFFER, g.screen_framebuffer);
	glViewport(0, 0, g.screen_width, g.screen_height);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return true;
}

void EndScreen() {
	if (!g.in_screen) return;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	g.in_screen = false;
	g.screen_ready = true;
}

void GetScreenSize(unsigned int& width, unsigned int& height) {
	width = static_cast<unsigned int>(g.screen_width);
	height = static_cast<unsigned int>(g.screen_height);
}

void PresentScreen() { drawFloatingScreen(); }

bool GetEyeProjection(double near_dist, double far_dist,
                      TMatrix<4, 4>& out) {
	if (!g.lens || g.current_eye < 0 || g.current_eye > 1) return false;
	float matrix[16];
	CardboardLensDistortion_getProjectionMatrix(
	    g.lens, g.current_eye == 0 ? kLeft : kRight,
	    static_cast<float>(near_dist), static_cast<float>(far_dist), matrix);
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			out[c][r] = matrix[c * 4 + r];
	return true;
}

bool GetEyeViewOffset(TMatrix<4, 4>& out) {
	if (!g.lens || g.current_eye < 0 || g.current_eye > 1) return false;
	TMatrix<4, 4> eye;
	TMatrix<4, 4> head;
	for (int c = 0; c < 4; ++c) {
		for (int r = 0; r < 4; ++r) {
			eye[c][r] = g.eye_from_head[g.current_eye][c * 4 + r];
			head[c][r] = g.head_view[c][r];
		}
	}
	out = eye * head;
	return true;
}

void GetCullHalfFov(double& half_vertical, double& half_horizontal) {
	half_vertical = half_horizontal = 0.0;
	if (!g.lens) return;
	for (int eye = 0; eye < 2; ++eye) {
		half_horizontal = std::max(half_horizontal,
		    std::max(std::fabs(static_cast<double>(g.field_of_view[eye][0])),
		             std::fabs(static_cast<double>(g.field_of_view[eye][1]))));
		half_vertical = std::max(half_vertical,
		    std::max(std::fabs(static_cast<double>(g.field_of_view[eye][2])),
		             std::fabs(static_cast<double>(g.field_of_view[eye][3]))));
	}
}

void UpdateInput() { updateGamepadInput(); }

void SetUiNavigation(bool enabled) {
	if (g.ui_navigation == enabled) return;
	g.ui_navigation = enabled;
	g.held_x = g.held_y = 0;
	g.repeat_at = 0.0;
	g.was_confirm = g.was_cancel = false;
	g.was_pause = g.was_restart = g.was_camera = g.was_hud = false;
	g.was_recenter = false;
	if (enabled) {
		etr_platform::SetJoystickAxis(0, sf::Joystick::X, 0.f);
		etr_platform::SetJoystickAxis(0, sf::Joystick::Y, 0.f);
	}
}

}  // namespace vr

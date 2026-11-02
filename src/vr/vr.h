/* --------------------------------------------------------------------
EXTREME TUXRACER -- VR façade

The seam between the game and its Android VR backends. Everything the game
needs to know about VR goes through here, and every entry point is safe to
call (and cheap) when VR is unavailable, so the desktop build stays untouched.

The camera model is a "stereo chase cam": the game's existing BEHIND /
FOLLOW orbit camera is the rig, and the tracked head pose plus per-eye
offset are composed on top of it. That keeps a stable external reference
in view, which matters a great deal for comfort in a downhill racer.
---------------------------------------------------------------------*/

#ifndef ETR_VR_H
#define ETR_VR_H

#include "../matrices.h"

namespace vr {

/** ETR world units per metre. Tux and the courses were authored for a
 *  1990s desktop game with no physical scale, so this is the knob that
 *  makes the world feel the right size through a headset. */
extern double world_scale;

/** Brings up the selected VR backend. Returns false harmlessly if it cannot
 *  initialize. Requires a current GLES context. */
bool Init();
void Shutdown();

bool IsActive();

/** Scale for Cardboard's floating 2D canvas. Other backends keep the
 *  game's original font size. */
float UiTextScale();

/** True while the backend has an active foreground VR presentation. */
bool IsSessionRunning();
bool IsExitRequested();

// --------------------------------------------------------------------
//	Frame driver
// --------------------------------------------------------------------

/** Pumps XR events and starts the frame. False means skip rendering
 *  this tick (session idle, or the runtime asked us not to draw). */
bool BeginFrame();
void EndFrame();

int NumEyes();

/** Binds the eye's swapchain framebuffer and makes its matrices current.
 *  Between these two calls the game renders exactly as it always has. */
void BeginEye(int eye);
void EndEye(int eye);

/** The eye currently being rendered, or -1 outside BeginEye/EndEye. */
int CurrentEye();

// --------------------------------------------------------------------
//	Flat 2D path (menus, and anything not yet stereo-aware)
// --------------------------------------------------------------------

/** Renders a normal monoscopic frame into a swapchain that is presented
 *  as a quad floating in front of the viewer. */
bool BeginScreen();
void EndScreen();
void GetScreenSize(unsigned int& width, unsigned int& height);
/** Presents the screen canvas as a floating panel. OpenXR uses a quad
 *  composition layer; Cardboard projects the canvas in stereo. */
void PresentScreen();

// --------------------------------------------------------------------
//	Camera injection
// --------------------------------------------------------------------

/** Projection for the eye being rendered, built from the runtime's
 *  asymmetric field of view. False if unavailable. */
bool GetEyeProjection(double near_dist, double far_dist,
                      TMatrix<4, 4>& out);

/** Pre-multiplies the game's chase-camera view matrix to apply the
 *  tracked head pose and per-eye separation. False if unavailable. */
bool GetEyeViewOffset(TMatrix<4, 4>& out);

/** Widest half-angles across both eyes, in radians. The game's own
 *  frustum culling is built from param.fov and would otherwise clip
 *  geometry that is plainly visible at the edge of a much wider VR view. */
void GetCullHalfFov(double& half_vertical, double& half_horizontal);

// --------------------------------------------------------------------
//	Input
// --------------------------------------------------------------------

/** Syncs the OpenXR action set and pushes the results onto the virtual
 *  joystick the game already reads. */
void UpdateInput();

/** Switches the controller between gameplay and menu behaviour. On a menu
 *  screen the thumbstick and buttons are translated into the arrow /
 *  Return / Escape keys the existing GUI already navigates with, rather
 *  than into steering and paddling. */
void SetUiNavigation(bool enabled);

}  // namespace vr

#endif  // ETR_VR_H

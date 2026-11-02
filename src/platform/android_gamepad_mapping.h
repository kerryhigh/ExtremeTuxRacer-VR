/* --------------------------------------------------------------------
EXTREME TUXRACER -- Android gamepad to gameplay controls

This header intentionally contains only the stable gameplay mapping, so it
can be exercised by a small host-side test without an Android device.
---------------------------------------------------------------------*/

#ifndef ETR_ANDROID_GAMEPAD_MAPPING_H
#define ETR_ANDROID_GAMEPAD_MAPPING_H

#include "platform.h"

struct AndroidGamepadGameplayControls {
	float steering = 0.f;
	float paddle = 0.f;
	float brake = 0.f;
	bool jump_charge = false;
	bool trick_modifier = false;
};

inline AndroidGamepadGameplayControls MapAndroidGamepadToGameplay(
		const AndroidGamepadState& pad) {
	AndroidGamepadGameplayControls controls;
	if (!pad.connected) return controls;
	controls.steering = pad.left_x;
	controls.paddle = pad.right_trigger;
	controls.brake = pad.left_trigger;
	controls.jump_charge = pad.a;
	controls.trick_modifier = pad.left_bumper;
	return controls;
}

#endif  // ETR_ANDROID_GAMEPAD_MAPPING_H

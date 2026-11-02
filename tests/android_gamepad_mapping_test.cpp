#include "platform/android_gamepad_mapping.h"
#include "racing_tricks.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
	if (condition) return;
	std::cerr << "FAIL: " << message << '\n';
	std::exit(1);
}

void expectNoTricks(const TTrickRequests& r, const char* context) {
	expect(!r.roll_left && !r.roll_right && !r.front_flip && !r.back_flip,
	       context);
}

}  // namespace

int main() {
	AndroidGamepadState pad;
	pad.connected = true;
	pad.left_x = -0.75f;
	pad.right_trigger = 0.8f;
	pad.left_trigger = 0.2f;
	pad.a = true;
	pad.left_bumper = true;
	AndroidGamepadGameplayControls controls = MapAndroidGamepadToGameplay(pad);
	expect(controls.steering == -0.75f, "left stick maps to steering");
	expect(controls.paddle == 0.8f, "RT maps to paddle");
	expect(controls.brake == 0.2f, "LT maps to brake");
	expect(controls.jump_charge, "held A charges a jump");
	expect(controls.trick_modifier, "LB enables trick modifier");

	pad.connected = false;
	controls = MapAndroidGamepadToGameplay(pad);
	expect(controls.steering == 0.f && controls.paddle == 0.f &&
	           controls.brake == 0.f && !controls.jump_charge &&
	           !controls.trick_modifier,
	       "disconnected controller clears gameplay controls");

	expectNoTricks(GetTrickRequests(false, true, false, false, true, -1.f,
	                               true, true),
	               "tricks are not requested while grounded");
	expectNoTricks(GetTrickRequests(true, false, false, false, true, -1.f,
	                               true, true),
	               "trick inputs require LB");

	TTrickRequests r = GetTrickRequests(true, true, false, false, true, -1.f,
	                                    false, false);
	expect(r.roll_left && !r.roll_right, "LB plus left stick left rolls left");
	r = GetTrickRequests(true, true, false, false, true, 1.f, false, false);
	expect(r.roll_right && !r.roll_left, "LB plus left stick right rolls right");
	r = GetTrickRequests(true, true, false, false, false, 0.f, true, false);
	expect(r.front_flip && !r.back_flip, "LB plus RT requests a front flip");
	r = GetTrickRequests(true, true, false, false, false, 0.f, false, true);
	expect(r.back_flip && !r.front_flip, "LB plus LT requests a back flip");
	r = GetTrickRequests(true, true, false, false, true, -1.f, true, false);
	expect(r.roll_left && r.front_flip,
	       "a roll and front flip can be requested together");
	r = GetTrickRequests(true, true, false, false, true, 1.f, false, true);
	expect(r.roll_right && r.back_flip,
	       "a roll and back flip can be requested together");

	std::cout << "Android gamepad and trick mapping checks passed.\n";
	return 0;
}

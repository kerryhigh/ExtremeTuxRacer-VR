/* --------------------------------------------------------------------
EXTREME TUXRACER -- trick input translation

Separating the input-to-trick decisions from the physics update keeps the
keyboard and analog-controller paths consistent and testable.
---------------------------------------------------------------------*/

#ifndef ETR_RACING_TRICKS_H
#define ETR_RACING_TRICKS_H

struct TTrickRequests {
	bool roll_left = false;
	bool roll_right = false;
	bool front_flip = false;
	bool back_flip = false;
};

inline TTrickRequests GetTrickRequests(bool airborne, bool modifier,
		bool left_turn, bool right_turn, bool stick_turn, float stick_turnfact,
		bool paddling, bool braking) {
	TTrickRequests requests;
	if (!airborne || !modifier) return requests;
	requests.roll_left = left_turn || (stick_turn && stick_turnfact < -0.2f);
	requests.roll_right = right_turn || (stick_turn && stick_turnfact > 0.2f);
	requests.front_flip = paddling;
	requests.back_flip = braking;
	return requests;
}

#endif  // ETR_RACING_TRICKS_H

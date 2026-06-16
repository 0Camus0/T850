#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <utils/InputManager.h>
#include <cmath>
#include <stdio.h>

InputManager::InputManager() {
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < MAXKEYS; j++) {
			KeyStates[i][j] = false;
		}

		for (int j = 0; j < MAXMOUSEBUTTONS; j++) {
			MouseButtonStates[i][j] = false;
		}
	}
	xDelta = 0;
	yDelta = 0;
	mouseX = 0;
	mouseY = 0;
	scrollDelta = 0.0f;
}

bool InputManager::PressedOnceKey(int key) {

	bool ret = KeyStates[0][key];

	if (!KeyStates[1][key] && KeyStates[0][key]) {
		KeyStates[1][key] = true;
	}else {
		ret = false;
	}

	return ret;

}

bool InputManager::PressedOnceMouseButton(int mb) {
	bool ret = MouseButtonStates[0][mb];

	if (!MouseButtonStates[1][mb] && MouseButtonStates[0][mb]) {
		MouseButtonStates[1][mb] = true;
	}
	else {
		ret = false;
	}

	return ret;
}

bool InputManager::PressedKey(int key) {
	return KeyStates[0][key];
}

bool InputManager::PressedMouseButton(int mb) {
	return MouseButtonStates[0][mb];
}

bool InputManager::HasGamepadInput() const {
	if (!Gamepad.connected || !Gamepad.enabled) {
		return false;
	}
	constexpr float kAxisActivity = 0.08f;
	constexpr float kTriggerActivity = 0.05f;
	return std::fabs(Gamepad.leftX) > kAxisActivity ||
	       std::fabs(Gamepad.leftY) > kAxisActivity ||
	       std::fabs(Gamepad.rightX) > kAxisActivity ||
	       std::fabs(Gamepad.rightY) > kAxisActivity ||
	       Gamepad.leftTrigger > kTriggerActivity ||
	       Gamepad.rightTrigger > kTriggerActivity ||
	       Gamepad.buttonSouth ||
	       Gamepad.buttonEast ||
	       Gamepad.buttonWest ||
	       Gamepad.buttonNorth ||
	       Gamepad.back ||
	       Gamepad.guide ||
	       Gamepad.start ||
	       Gamepad.leftStick ||
	       Gamepad.rightStick ||
	       Gamepad.leftShoulder ||
	       Gamepad.rightShoulder ||
	       Gamepad.dpadUp ||
	       Gamepad.dpadDown ||
	       Gamepad.dpadLeft ||
	       Gamepad.dpadRight;
}

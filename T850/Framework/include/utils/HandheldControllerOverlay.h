#pragma once

#include <utils/InputManager.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace t850 {

inline float HandheldClamp01(float value) {
  return (std::max)(0.0f, (std::min)(1.0f, value));
}

inline float HandheldAnalogAmount(float value, float threshold) {
  const float magnitude = std::fabs(value);
  if (magnitude <= threshold) {
    return 0.0f;
  }
  return HandheldClamp01((magnitude - threshold) / (1.0f - threshold));
}

inline void SubmitGamepadGuiNavigation(const GamepadInputState& gamepad, bool guiVisible) {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  const bool gamepadAvailable = gamepad.connected && gamepad.enabled;
  const bool active = guiVisible && gamepadAvailable;
  if (gamepadAvailable) {
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  } else {
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  }

  const auto key = [&](ImGuiKey imguiKey, bool down) {
    io.AddKeyEvent(imguiKey, active && down);
  };
  const auto analog = [&](ImGuiKey imguiKey, float value) {
    io.AddKeyAnalogEvent(imguiKey, active && value > 0.0f, active ? value : 0.0f);
  };

  key(ImGuiKey_GamepadStart, gamepad.start);
  key(ImGuiKey_GamepadBack, gamepad.back);
  key(ImGuiKey_GamepadFaceDown, gamepad.buttonSouth);
  key(ImGuiKey_GamepadFaceRight, gamepad.buttonEast);
  key(ImGuiKey_GamepadFaceLeft, gamepad.buttonWest);
  key(ImGuiKey_GamepadFaceUp, gamepad.buttonNorth);
  key(ImGuiKey_GamepadL2, gamepad.leftTrigger > 0.18f);
  key(ImGuiKey_GamepadR2, gamepad.rightTrigger > 0.18f);
  key(ImGuiKey_GamepadL3, gamepad.leftStick);
  key(ImGuiKey_GamepadR3, gamepad.rightStick);
  key(ImGuiKey_GamepadDpadLeft, gamepad.dpadLeft);
  key(ImGuiKey_GamepadDpadRight, gamepad.dpadRight);
  key(ImGuiKey_GamepadDpadUp, gamepad.dpadUp);
  key(ImGuiKey_GamepadDpadDown, gamepad.dpadDown);

  constexpr float kStickNavThreshold = 0.22f;
  analog(ImGuiKey_GamepadLStickLeft, HandheldAnalogAmount((std::min)(gamepad.leftX, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadLStickRight, HandheldAnalogAmount((std::max)(gamepad.leftX, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadLStickUp, HandheldAnalogAmount((std::min)(gamepad.leftY, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadLStickDown, HandheldAnalogAmount((std::max)(gamepad.leftY, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadRStickLeft, HandheldAnalogAmount((std::min)(gamepad.rightX, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadRStickRight, HandheldAnalogAmount((std::max)(gamepad.rightX, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadRStickUp, HandheldAnalogAmount((std::min)(gamepad.rightY, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadRStickDown, HandheldAnalogAmount((std::max)(gamepad.rightY, 0.0f), kStickNavThreshold));
}

inline void SubmitGamepadGuiDirectionalNavigation(const GamepadInputState& gamepad, bool guiVisible) {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  const bool gamepadAvailable = gamepad.connected && gamepad.enabled;
  const bool active = guiVisible && gamepadAvailable;
  if (gamepadAvailable) {
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  } else {
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  }

  const auto key = [&](ImGuiKey imguiKey, bool down) {
    io.AddKeyEvent(imguiKey, active && down);
  };
  const auto analog = [&](ImGuiKey imguiKey, float value) {
    io.AddKeyAnalogEvent(imguiKey, active && value > 0.0f, active ? value : 0.0f);
  };

  key(ImGuiKey_GamepadFaceDown, gamepad.buttonSouth);
  key(ImGuiKey_GamepadDpadLeft, gamepad.dpadLeft);
  key(ImGuiKey_GamepadDpadRight, gamepad.dpadRight);
  key(ImGuiKey_GamepadDpadUp, gamepad.dpadUp);
  key(ImGuiKey_GamepadDpadDown, gamepad.dpadDown);

  constexpr float kStickNavThreshold = 0.22f;
  analog(ImGuiKey_GamepadLStickLeft, HandheldAnalogAmount((std::min)(gamepad.leftX, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadLStickRight, HandheldAnalogAmount((std::max)(gamepad.leftX, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadLStickUp, HandheldAnalogAmount((std::min)(gamepad.leftY, 0.0f), kStickNavThreshold));
  analog(ImGuiKey_GamepadLStickDown, HandheldAnalogAmount((std::max)(gamepad.leftY, 0.0f), kStickNavThreshold));
}

inline void DrawHandheldCenteredText(ImDrawList* drawList, ImVec2 center, const char* text, ImU32 color) {
  const ImVec2 size = ImGui::CalcTextSize(text);
  drawList->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), color, text);
}

inline void DrawHandheldControllerPill(ImDrawList* drawList,
                                       ImVec2 center,
                                       float radius,
                                       const char* label,
                                       ImU32 fill,
                                       ImU32 border,
                                       ImU32 text) {
  drawList->AddCircleFilled(center, radius, fill, 24);
  drawList->AddCircle(center, radius, border, 24, 2.0f);
  DrawHandheldCenteredText(drawList, center, label, text);
}

inline void DrawHandheldControllerImage(ImDrawList* drawList, ImVec2 min, ImVec2 size) {
  const ImVec2 imageMax(min.x + size.x, min.y + size.y);
  const ImU32 body = IM_COL32(32, 36, 44, 245);
  const ImU32 grip = IM_COL32(24, 27, 34, 245);
  const ImU32 panel = IM_COL32(9, 13, 18, 255);
  const ImU32 accent = IM_COL32(85, 180, 255, 255);
  const ImU32 button = IM_COL32(62, 70, 84, 255);
  const ImU32 border = IM_COL32(190, 210, 235, 180);
  const ImU32 text = IM_COL32(245, 248, 255, 255);

  drawList->AddRectFilled(min, imageMax, body, 32.0f);
  drawList->AddRect(min, imageMax, border, 32.0f, 0, 2.0f);

  const float w = size.x;
  const float h = size.y;
  const ImVec2 leftGrip(min.x + w * 0.03f, min.y + h * 0.08f);
  const ImVec2 rightGrip(min.x + w * 0.86f, min.y + h * 0.08f);
  drawList->AddRectFilled(leftGrip, ImVec2(min.x + w * 0.17f, min.y + h * 0.92f), grip, 28.0f);
  drawList->AddRectFilled(rightGrip, ImVec2(min.x + w * 0.97f, min.y + h * 0.92f), grip, 28.0f);

  drawList->AddRectFilled(ImVec2(min.x + w * 0.22f, min.y + h * 0.16f),
                          ImVec2(min.x + w * 0.78f, min.y + h * 0.78f),
                          panel,
                          10.0f);
  DrawHandheldCenteredText(drawList, ImVec2(min.x + w * 0.50f, min.y + h * 0.47f), "T850", accent);

  DrawHandheldControllerPill(drawList, ImVec2(min.x + w * 0.21f, min.y + h * 0.54f), h * 0.11f, "LS", button, border, text);
  DrawHandheldControllerPill(drawList, ImVec2(min.x + w * 0.79f, min.y + h * 0.54f), h * 0.11f, "RS", button, border, text);

  const ImVec2 dpad(min.x + w * 0.18f, min.y + h * 0.31f);
  drawList->AddRectFilled(ImVec2(dpad.x - h * 0.035f, dpad.y - h * 0.11f),
                          ImVec2(dpad.x + h * 0.035f, dpad.y + h * 0.11f),
                          button,
                          4.0f);
  drawList->AddRectFilled(ImVec2(dpad.x - h * 0.11f, dpad.y - h * 0.035f),
                          ImVec2(dpad.x + h * 0.11f, dpad.y + h * 0.035f),
                          button,
                          4.0f);
  drawList->AddRect(ImVec2(dpad.x - h * 0.11f, dpad.y - h * 0.11f),
                    ImVec2(dpad.x + h * 0.11f, dpad.y + h * 0.11f),
                    border,
                    4.0f);
  DrawHandheldCenteredText(drawList, ImVec2(dpad.x, dpad.y + h * 0.16f), "GUI NAV", accent);

  const ImVec2 face(min.x + w * 0.84f, min.y + h * 0.30f);
  const float br = h * 0.055f;
  DrawHandheldControllerPill(drawList, ImVec2(face.x, face.y - h * 0.075f), br, "Y", button, border, text);
  DrawHandheldControllerPill(drawList, ImVec2(face.x - h * 0.075f, face.y), br, "X", button, border, text);
  DrawHandheldControllerPill(drawList, ImVec2(face.x + h * 0.075f, face.y), br, "B", button, border, text);
  DrawHandheldControllerPill(drawList, ImVec2(face.x, face.y + h * 0.075f), br, "A", button, border, text);

  drawList->AddRectFilled(ImVec2(min.x + w * 0.15f, min.y - h * 0.04f),
                          ImVec2(min.x + w * 0.38f, min.y + h * 0.04f),
                          button,
                          8.0f);
  drawList->AddRectFilled(ImVec2(min.x + w * 0.62f, min.y - h * 0.04f),
                          ImVec2(min.x + w * 0.85f, min.y + h * 0.04f),
                          button,
                          8.0f);
  DrawHandheldCenteredText(drawList, ImVec2(min.x + w * 0.265f, min.y), "LB", text);
  DrawHandheldCenteredText(drawList, ImVec2(min.x + w * 0.735f, min.y), "RB", text);
  DrawHandheldCenteredText(drawList, ImVec2(min.x + w * 0.50f, min.y + h * 0.89f), "VIEW: EXIT     START: GUI     R3: HELP", accent);
}

inline void DrawHandheldControllerHelpOverlay(const GamepadInputState& gamepad) {
  if (!gamepad.connected || !gamepad.enabled || !gamepad.rightStick) {
    return;
  }

  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 display = io.DisplaySize;
  const float width = (std::min)(820.0f, (std::max)(360.0f, display.x - 48.0f));
  const float height = (std::min)(430.0f, (std::max)(300.0f, display.y - 48.0f));
  const ImVec2 pos((display.x - width) * 0.5f, 24.0f);
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.92f);

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoNav;
  if (!ImGui::Begin("Handheld Controller Mapping##T850", nullptr, flags)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("ROG Ally X / Xbox layout");
  ImGui::SameLine();
  ImGui::TextDisabled("(hold R3 to show)");
  ImGui::Separator();

  const ImVec2 imageMin = ImGui::GetCursorScreenPos();
  const float imageHeight = (std::min)(height * 0.50f, 190.0f);
  const ImVec2 imageSize(width - 32.0f, imageHeight);
  ImGui::InvisibleButton("controller_image", imageSize);
  DrawHandheldControllerImage(ImGui::GetWindowDrawList(), imageMin, imageSize);

  ImGui::Spacing();
  ImGui::Columns(2, "handheld_mapping_columns", false);
  ImGui::TextUnformatted("LS: Move / strafe");
  ImGui::TextUnformatted("LS click: Sprint");
  ImGui::TextUnformatted("RS: Look / aim");
  ImGui::TextUnformatted("A: Jump / select");
  ImGui::NextColumn();
  ImGui::TextUnformatted("B: Crouch / close GUI");
  ImGui::TextUnformatted("Start: Toggle GUI");
  ImGui::TextUnformatted("View/Back: Close app");
  ImGui::TextUnformatted("D-pad/LS: Navigate GUI");
  ImGui::TextUnformatted("LB/RB: Switch GUI panel");
  ImGui::Columns(1);

  ImGui::End();
}

inline void DrawHandheldGuiFooter(const GamepadInputState& gamepad) {
  if (!gamepad.connected || !gamepad.enabled) {
    return;
  }

  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 display = io.DisplaySize;
  const float width = (std::min)(900.0f, (std::max)(420.0f, display.x - 48.0f));
  const float height = 42.0f;
  ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, display.y - height - 18.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.86f);

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("Handheld GUI Footer##T850", nullptr, flags)) {
    ImGui::TextUnformatted("GUI: LS/D-pad focus   A select   B close GUI   LB/RB switch panel   View close app   RT mappings");
  }
  ImGui::End();
}

} // namespace t850

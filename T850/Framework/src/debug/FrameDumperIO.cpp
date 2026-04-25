#include <pch.h>
#include <debug/FrameDumper.h>
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>

// ── Glaze-based JSON I/O for snapshot files ──

namespace t850 {

// ── Legacy .txt format parser ──

static bool ParseLegacyTxt(const std::string& content, SnapshotJson& data) {
  auto getVal = [&](const char* key) -> float {
    std::string k = std::string(key) + "=";
    size_t p = content.find(k);
    if (p == std::string::npos) return 0.0f;
    return std::stof(content.substr(p + k.size()));
  };
  auto getVec = [&](const char* key, std::array<float, 3>& out) {
    std::string k = std::string(key) + "=";
    size_t p = content.find(k);
    if (p == std::string::npos) return;
    sscanf(content.c_str() + p + k.size(), "%f,%f,%f", &out[0], &out[1], &out[2]);
  };

  getVec("Cam.Eye", data.cam.eye);
  data.cam.pitch = getVal("Cam.Pitch");
  data.cam.roll  = getVal("Cam.Roll");
  data.cam.yaw   = getVal("Cam.Yaw");
  getVec("LightCam.Eye", data.light_cam.eye);
  data.light_cam.pitch = getVal("LightCam.Pitch");
  data.light_cam.roll  = getVal("LightCam.Roll");
  data.light_cam.yaw   = getVal("LightCam.Yaw");
  // No matrices, lights, or scene props in legacy format
  data.matrices = std::nullopt;
  data.omni = std::nullopt;
  data.lights.clear();

  printf("[FrameDumper] Loaded legacy .txt format\n");
  return true;
}

// ── Load ──

bool LoadSnapshot(const std::string& path, SnapshotJson& data) {
  std::ifstream f(path);
  if (!f.is_open()) {
    printf("[FrameDumper] ERROR: cannot open '%s'\n", path.c_str());
    return false;
  }

  std::stringstream ss;
  ss << f.rdbuf();
  std::string content = ss.str();

  // Detect legacy .txt format (no JSON braces)
  if (content.find('{') == std::string::npos) {
    return ParseLegacyTxt(content, data);
  }

  // JSON via glaze
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(data, content);
  if (ec) {
    auto err = glz::format_error(ec, content);
    printf("[FrameDumper] ERROR parsing '%s': %s\n", path.c_str(), err.c_str());
    return false;
  }

  printf("[FrameDumper] Loaded '%s': scene=%d, %zu lights, matrices=%s, omni=%s\n",
         path.c_str(), data.scene, data.lights.size(),
         data.matrices.has_value() ? "yes" : "no",
         data.omni.has_value() ? "yes" : "no");
  return true;
}

// ── Save ──

bool SaveSnapshot(const std::string& path, const SnapshotJson& data) {
  auto result = glz::write<glz::opts{.prettify = true}>(data);
  if (!result) {
    printf("[FrameDumper] ERROR: failed to serialize snapshot data\n");
    return false;
  }

  std::ofstream f(path);
  if (!f.is_open()) {
    printf("[FrameDumper] ERROR: cannot write '%s'\n", path.c_str());
    return false;
  }

  f << result.value();
  return true;
}

} // namespace t850

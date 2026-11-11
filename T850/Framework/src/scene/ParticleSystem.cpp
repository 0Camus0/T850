#include <pch.h>

#include <scene/ParticleSystem.h>

#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <algorithm>

namespace t850 {

namespace {
  // A procedural system with no particles is a no-op rather than an error: scenes often
  // author a system and disable it.
  bool IsValid(const ParticleSystemDesc& d) {
    if (d.name.empty()) return false;
    if (d.radius <= 0.0f) return false;
    if (d.opacity < 0.0f || d.opacity > 1.0f) return false;
    if (d.softness < 0.0f || d.softness > 1.0f) return false;
    if (d.size_scale_min <= 0.0f || d.size_scale_max <= 0.0f) return false;
    if (d.size_scale_max < d.size_scale_min) return false;
    if (d.source == ParticleSourceKind::Buffer && d.buffer.empty()) return false;
    if (d.source == ParticleSourceKind::Procedural) {
      if (d.count < 0) return false;
      if (d.lifetime <= 0.0f) return false;
    }
    const ParticleScreenSpaceDesc& ss = d.screen_space;
    if (ss.smoothing_iterations < 0 || ss.smoothing_iterations > 8) return false;
    if (ss.smoothing_strength < 0.0f || ss.smoothing_strength > 1.0f) return false;
    if (ss.smoothing_radius < 1.0f || ss.smoothing_radius > 128.0f) return false;
    if (ss.smoothing_world_radius < 0.0f) return false;
    if (ss.depth_tolerance < 0.0f) return false;
    if (ss.depth_radius_scale <= 0.0f || ss.depth_radius_scale > 1.5f) return false;
    if (ss.packing_factor < 1.0f) return false;
    if (ss.absorption_scale < 0.0f) return false;
    if (ss.thickness_scale < 0.0f) return false;
    if (ss.refraction < 0.0f || ss.refraction > 2.0f) return false;
    if (ss.refraction_strength < 0.0f || ss.refraction_strength > 1.0f) return false;
    if (ss.reflection_distance < 0.0f) return false;
    if (ss.specular < 0.0f) return false;
    if (ss.glint_exponent < 1.0f) return false;
    if (ss.fresnel_power < 0.0f) return false;
    if (ss.normal_scale < 0.0f) return false;
    if (ss.foam < 0.0f || ss.foam > 1.0f) return false;
    if (ss.ripple_slope < 0.0f || ss.ripple_wavelength <= 0.0f || ss.ripple_speed < 0.0f ||
        ss.ripple_distance <= 0.0f) return false;
    if (ss.caustics < 0.0f || ss.caustic_scale <= 0.0f || ss.caustic_speed < 0.0f ||
        ss.caustic_depth <= 0.0f) return false;
    if (ss.env_intensity < 0.0f || ss.sun_attenuation < 0.0f || ss.sun_attenuation > 1.0f ||
        ss.caustic_focus < 0.0f || ss.contact_line < 0.0f || ss.backlight < 0.0f ||
        ss.scatter_sun < 0.0f || ss.scatter_sky < 0.0f || ss.scatter_anisotropy < 0.0f ||
        ss.scatter_anisotropy > 0.95f || ss.ripple_calm < 0.0f || ss.ripple_agitation < 0.0f)
      return false;
    if (ss.foam_strength < 0.0f || ss.foam_texture_scale <= 0.0f || ss.aeration < 0.0f ||
        ss.bubble_strength < 0.0f || ss.spray_strength < 0.0f || ss.wetness < 0.0f)
      return false;
    if (ss.debug_view < 0 || ss.debug_view > 9) return false;
    if (ss.surfel_band < 0.1f || ss.surfel_band > 8.0f) return false;
    if (ss.surfel_sharpness < 0.25f || ss.surfel_sharpness > 8.0f) return false;
    if (ss.surfel_smoothing < 0.0f || ss.surfel_smoothing > 4.0f) return false;
    return true;
  }
}  // namespace

ParticleBufferRegistry& ParticleBufferRegistry::Instance() {
  static ParticleBufferRegistry registry;
  return registry;
}

void ParticleBufferRegistry::Clear() { m_entries.clear(); }

void ParticleBufferRegistry::Set(const std::string& name, ComputeBuffer* buffer, int elementCount) {
  for (Entry& e : m_entries) {
    if (e.name == name) { e.buffer = buffer; e.count = elementCount; return; }
  }
  m_entries.push_back({name, buffer, elementCount});
}

ComputeBuffer* ParticleBufferRegistry::Find(const std::string& name, int* elementCount) const {
  for (const Entry& e : m_entries)
    if (e.name == name) {
      if (elementCount) *elementCount = e.count;
      return e.buffer;
    }
  return nullptr;
}

ParticleSystemRegistry& ParticleSystemRegistry::Instance() {
  static ParticleSystemRegistry registry;
  return registry;
}

void ParticleSystemRegistry::Clear() {
  m_systems.clear();
  m_timeSeconds = 0.0f;
}

bool ParticleSystemRegistry::Add(const ParticleSystemDesc& desc) {
  if (!IsValid(desc)) {
    T8_LOG_ERROR("[ParticleSystem] Invalid particle system '%s' (radius %.3f, opacity %.3f, "
                 "source %d, buffer '%s')",
                 desc.name.c_str(), desc.radius, desc.opacity,
                 static_cast<int>(desc.source), desc.buffer.c_str());
    return false;
  }
  for (ParticleSystemDesc& existing : m_systems) {
    if (existing.name == desc.name) {
      existing = desc;
      T8_LOG_INFO("[ParticleSystem] Replaced particle system '%s'", desc.name.c_str());
      return true;
    }
  }
  m_systems.push_back(desc);
  T8_LOG_INFO("[ParticleSystem] Added particle system '%s' (%s, %s, %s, %d particles%s)",
              desc.name.c_str(),
              desc.source == ParticleSourceKind::Procedural ? "procedural" : "buffer",
              desc.blend == ParticleBlend::Additive ? "additive" : "alpha",
              desc.render_mode == ParticleRenderMode::ScreenSpace ? "screen-space" : "splat",
              desc.source == ParticleSourceKind::Buffer
                  ? (desc.buffer_count > 0 ? desc.buffer_count : desc.count)
                  : desc.count,
              desc.buffer.empty() ? "" : (", buffer " + desc.buffer).c_str());
  return true;
}

const ParticleSystemDesc* ParticleSystemRegistry::Find(const std::string& name) const {
  for (const ParticleSystemDesc& system : m_systems)
    if (system.name == name) return &system;
  return nullptr;
}

void ParticleSystemRegistry::Advance(float deltaSeconds) {
  if (deltaSeconds > 0.0f) m_timeSeconds += deltaSeconds;
}

}  // namespace t850

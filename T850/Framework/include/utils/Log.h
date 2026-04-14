#ifndef T800_LOG_H
#define T800_LOG_H

/*  T850 Logging System
 *  -------------------
 *  Macro-based, multi-backend, thread-safe logging with verbosity levels.
 *
 *  Usage:
 *    T8_LOG_ERROR("Failed to load '%s'", filename);
 *    T8_LOG_INFO("RenderTarget created: %s (%dx%d)", name, w, h);
 *    T8_LOG_DEBUG("Shader key 0x%08X compiled", key);
 *    T8_LOG_VERBOSE("Vertex stride=%u offset=%u", stride, offset);
 *
 *  Backends (combinable via Init flags):
 *    T8_LOG_BACKEND_CONSOLE       - stdout with ANSI colors
 *    T8_LOG_BACKEND_DEBUG_OUTPUT  - OutputDebugStringA (Windows)
 *    T8_LOG_BACKEND_FILE          - append to log file
 *
 *  Each log line includes: [timestamp] [PID:TID] [RAM MB] [LEVEL] message
 */

#include <cstdint>

namespace t800 {
namespace Log {

  enum Level : uint8_t {
    LVL_ERROR   = 0,
    LVL_INFO    = 1,
    LVL_DEBUG   = 2,
    LVL_VERBOSE = 3,
  };

  enum Backend : uint32_t {
    T8_LOG_BACKEND_CONSOLE      = 1u << 0,
    T8_LOG_BACKEND_DEBUG_OUTPUT = 1u << 1,
    T8_LOG_BACKEND_FILE         = 1u << 2,
  };

  // Call once at startup. backends = OR'd Backend flags, logFilePath only for FILE backend.
  void Init(Level maxLevel, uint32_t backends, const char* logFilePath = nullptr);
  void Shutdown();

  // Optional short tag (e.g. "d3d11", "gl") prepended to every log line as [tag].
  // Call after Init. Empty string disables the field.
  void SetSessionTag(const char* tag);

  // Internal – called by macros
  void Write(Level level, const char* file, int line, const char* fmt, ...);

  // Current max level (for macro short-circuit)
  Level GetMaxLevel();

} // namespace Log
} // namespace t800

// ── Macros ──

#define T8_LOG_ERROR(fmt, ...)   do { if (t800::Log::GetMaxLevel() >= t800::Log::LVL_ERROR)   t800::Log::Write(t800::Log::LVL_ERROR,   __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define T8_LOG_INFO(fmt, ...)    do { if (t800::Log::GetMaxLevel() >= t800::Log::LVL_INFO)    t800::Log::Write(t800::Log::LVL_INFO,    __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define T8_LOG_DEBUG(fmt, ...)   do { if (t800::Log::GetMaxLevel() >= t800::Log::LVL_DEBUG)   t800::Log::Write(t800::Log::LVL_DEBUG,   __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define T8_LOG_VERBOSE(fmt, ...) do { if (t800::Log::GetMaxLevel() >= t800::Log::LVL_VERBOSE) t800::Log::Write(t800::Log::LVL_VERBOSE, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#endif // T800_LOG_H

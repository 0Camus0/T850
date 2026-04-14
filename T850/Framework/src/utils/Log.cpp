#include <utils/Log.h>
#include <Config.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <mutex>

#ifdef OS_WINDOWS
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <psapi.h>
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <fstream>
#endif

namespace t800 {
namespace Log {

  static Level     s_maxLevel  = LVL_ERROR;
  static uint32_t  s_backends  = T8_LOG_BACKEND_CONSOLE;
  static FILE*     s_file      = nullptr;
  static std::mutex s_mutex;
  static bool      s_initialized = false;
  static char      s_sessionTag[32] = {0};

#ifdef OS_WINDOWS
  static HANDLE    s_console   = INVALID_HANDLE_VALUE;
  static WORD      s_defaultAttribs = 0;
#endif

  // ── helpers ──

  static const char* LevelTag(Level lvl) {
    switch (lvl) {
      case LVL_ERROR:   return "ERROR";
      case LVL_INFO:    return "INFO ";
      case LVL_DEBUG:   return "DEBUG";
      case LVL_VERBOSE: return "VERB ";
      default:          return "?????";
    }
  }

  static void GetTimestamp(char* buf, size_t len) {
    auto now  = std::chrono::system_clock::now();
    auto tt   = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;
    struct tm lt;
#ifdef OS_WINDOWS
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    int n = snprintf(buf, len, "%02d:%02d:%02d.%03d",
                     lt.tm_hour, lt.tm_min, lt.tm_sec, (int)ms.count());
    (void)n;
  }

  static uint32_t GetPID() {
#ifdef OS_WINDOWS
    return (uint32_t)GetCurrentProcessId();
#else
    return (uint32_t)getpid();
#endif
  }

  static uint32_t GetTID() {
#ifdef OS_WINDOWS
    return (uint32_t)GetCurrentThreadId();
#else
    return (uint32_t)pthread_self();
#endif
  }

  static size_t GetProcessRAM_MB() {
#ifdef OS_WINDOWS
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
      return pmc.WorkingSetSize / (1024 * 1024);
#else
    // Linux: read /proc/self/status VmRSS
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("VmRSS:", 0) == 0) {
        size_t kb = 0;
        sscanf(line.c_str(), "VmRSS: %zu kB", &kb);
        return kb / 1024;
      }
    }
#endif
    return 0;
  }

#ifdef OS_WINDOWS
  static void SetConsoleColor(Level lvl) {
    if (s_console == INVALID_HANDLE_VALUE) return;
    WORD attr;
    switch (lvl) {
      case LVL_ERROR:   attr = FOREGROUND_RED | FOREGROUND_INTENSITY;   break;
      case LVL_INFO:    attr = s_defaultAttribs;                        break;
      case LVL_DEBUG:   attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
      case LVL_VERBOSE: attr = FOREGROUND_INTENSITY;                    break; // dark gray
      default:          attr = s_defaultAttribs;                        break;
    }
    SetConsoleTextAttribute(s_console, attr);
  }
  static void ResetConsoleColor() {
    if (s_console != INVALID_HANDLE_VALUE)
      SetConsoleTextAttribute(s_console, s_defaultAttribs);
  }
#else
  static const char* AnsiColor(Level lvl) {
    switch (lvl) {
      case LVL_ERROR:   return "\033[1;31m"; // bold red
      case LVL_INFO:    return "\033[0m";    // default
      case LVL_DEBUG:   return "\033[32m";   // green
      case LVL_VERBOSE: return "\033[90m";   // dark gray
      default:          return "\033[0m";
    }
  }
#endif

  // ── public API ──

  void Init(Level maxLevel, uint32_t backends, const char* logFilePath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_maxLevel = maxLevel;
    s_backends = backends;

#ifdef OS_WINDOWS
    if (s_backends & T8_LOG_BACKEND_CONSOLE) {
      s_console = GetStdHandle(STD_OUTPUT_HANDLE);
      if (s_console != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(s_console, &info))
          s_defaultAttribs = info.wAttributes;
        else
          s_defaultAttribs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
      }
    }
#endif

    if ((s_backends & T8_LOG_BACKEND_FILE) && logFilePath) {
      s_file = fopen(logFilePath, "a");
    }

    s_initialized = true;
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file) {
      fclose(s_file);
      s_file = nullptr;
    }
#ifdef OS_WINDOWS
    if (s_console != INVALID_HANDLE_VALUE) {
      SetConsoleTextAttribute(s_console, s_defaultAttribs);
      s_console = INVALID_HANDLE_VALUE;
    }
#endif
    s_initialized = false;
  }

  Level GetMaxLevel() {
    return s_maxLevel;
  }

  void SetSessionTag(const char* tag) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (tag && *tag)
      snprintf(s_sessionTag, sizeof(s_sessionTag), "%s", tag);
    else
      s_sessionTag[0] = '\0';
  }

  void Write(Level level, const char* file, int line, const char* fmt, ...) {
    if (level > s_maxLevel) return;

    // Format user message
    char userMsg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(userMsg, sizeof(userMsg), fmt, args);
    va_end(args);

    // Build metadata
    char timestamp[32];
    GetTimestamp(timestamp, sizeof(timestamp));
    uint32_t pid = GetPID();
    uint32_t tid = GetTID();
    size_t   ram = GetProcessRAM_MB();
    const char* tag = LevelTag(level);

    // Strip path to just filename
    const char* fname = file;
    const char* slash = strrchr(file, '\\');
    if (!slash) slash = strrchr(file, '/');
    if (slash) fname = slash + 1;

    // Full line
    char fullLine[2560];
    if (s_sessionTag[0]) {
      snprintf(fullLine, sizeof(fullLine),
               "[%s] [%u:%u] [%zuMB] [%s] [%s] %s  (%s:%d)",
               timestamp, pid, tid, ram, s_sessionTag, tag, userMsg, fname, line);
    } else {
      snprintf(fullLine, sizeof(fullLine),
               "[%s] [%u:%u] [%zuMB] [%s] %s  (%s:%d)",
               timestamp, pid, tid, ram, tag, userMsg, fname, line);
    }

    std::lock_guard<std::mutex> lock(s_mutex);

    // Console backend
    if (s_backends & T8_LOG_BACKEND_CONSOLE) {
#ifdef OS_WINDOWS
      SetConsoleColor(level);
      printf("%s\n", fullLine);
      ResetConsoleColor();
#else
      printf("%s%s\033[0m\n", AnsiColor(level), fullLine);
#endif
    }

    // OutputDebugString backend (Windows only)
#ifdef OS_WINDOWS
    if (s_backends & T8_LOG_BACKEND_DEBUG_OUTPUT) {
      OutputDebugStringA(fullLine);
      OutputDebugStringA("\n");
    }
#endif

    // File backend
    if ((s_backends & T8_LOG_BACKEND_FILE) && s_file) {
      fprintf(s_file, "%s\n", fullLine);
      fflush(s_file);
    }
  }

} // namespace Log
} // namespace t800

#include <utils/ThreadPool.h>
#include <utils/Log.h>
#include <memory>

namespace t800 {

static std::unique_ptr<ThreadPool> s_globalPool;
ThreadPool* g_threadPool = nullptr;

void InitGlobalThreadPool() {
  if (!g_threadPool) {
    s_globalPool = std::make_unique<ThreadPool>();
    g_threadPool = s_globalPool.get();
    T8_LOG_INFO("[Engine] Global ThreadPool created: %u workers",
                g_threadPool->NumWorkers());
  }
}

void ShutdownGlobalThreadPool() {
  g_threadPool = nullptr;
  s_globalPool.reset();
  T8_LOG_INFO("[Engine] Global ThreadPool destroyed");
}

} // namespace t800

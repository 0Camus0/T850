#include <pch.h>
#include <utils/ThreadPool.h>
#include <utils/Log.h>

#include <cassert>
#include <memory>

namespace t850 {

static std::unique_ptr<ThreadPool> s_globalPool;
thread_local ThreadPool* ThreadPool::s_currentWorkerPool = nullptr;
ThreadPool* g_threadPool = nullptr;

bool ThreadPool::IsWorkerThread() const {
  return s_currentWorkerPool == this;
}

bool ThreadPool::CanBlockFromCurrentThread(const char* operation) const {
  if (!IsWorkerThread()) return true;

  T8_LOG_ERROR("[ThreadPool] %s called from a worker thread; this violates the blocking-call contract", operation);
  assert(false && "ThreadPool blocking call from worker thread");
  return false;
}

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

} // namespace t850

/*********************************************************
 * ThreadPool — lightweight fixed-size worker pool
 *
 * Usage:
 *   t850::ThreadPool pool;  // default: hardware_concurrency - 1 workers
 *
 *   // Fire-and-forget or wait on result:
 *   auto future = pool.Submit([](){ return heavyWork(); });
 *   auto result = future.get();
 *
 *   // Data-parallel loop (blocks until done):
 *   pool.ParallelFor(0, N, [&](int i){ process(i); });
 *
 *   // Wait for all submitted tasks:
 *   pool.WaitAll();
 *
 * Thread safety:
 *   - Submit() and ParallelFor() are safe to call from any thread.
 *   - ParallelFor() and WaitAll() must NOT be called from a worker
 *     thread (will deadlock on a fixed-size pool).
 *
 * The pool is destroyed (joined) when it goes out of scope.
 *********************************************************/

#ifndef T800_THREADPOOL_H
#define T800_THREADPOOL_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <vector>
#include <queue>
#include <atomic>
#include <cstdint>
#include <algorithm>

namespace t850 {

class ThreadPool {
public:
  explicit ThreadPool(unsigned int numThreads = 0) {
    if (numThreads == 0) {
      unsigned int hw = std::thread::hardware_concurrency();
      numThreads = (hw > 1) ? (hw - 1) : 1;
    }
    m_stop = false;
    m_inFlight = 0;
    m_workers.reserve(numThreads);
    for (unsigned int i = 0; i < numThreads; i++) {
      m_workers.emplace_back([this]() { WorkerLoop(); });
    }
  }

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stop = true;
    }
    m_cv.notify_all();
    for (auto& w : m_workers) {
      if (w.joinable()) w.join();
    }
  }

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  unsigned int NumWorkers() const { return static_cast<unsigned int>(m_workers.size()); }

  // Submit a callable, returns a future for the result.
  template<typename F, typename... Args>
  auto Submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<ReturnType> result = task->get_future();
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_tasks.emplace([task]() { (*task)(); });
      m_inFlight++;
    }
    m_cv.notify_one();
    return result;
  }

  // Block until all submitted tasks have finished executing.
  void WaitAll() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cvDone.wait(lock, [this]() {
      return m_tasks.empty() && m_inFlight == 0;
    });
  }

  // Data-parallel loop: calls func(i) for i in [begin, end).
  // Blocks the calling thread until all iterations complete.
  // Uses dynamic chunking for load balancing.
  template<typename Func>
  void ParallelFor(int begin, int end, Func&& func) {
    if (begin >= end) return;
    int total = end - begin;
    int numWorkers = static_cast<int>(m_workers.size());

    // For very small ranges, just run inline
    if (total <= numWorkers || numWorkers == 0) {
      for (int i = begin; i < end; i++) func(i);
      return;
    }

    // Dynamic chunking via shared atomic index
    std::atomic<int> nextIndex(begin);
    int chunkSize = (std::max)(1, total / (numWorkers * 8)); // ~8 chunks per worker

    std::atomic<int> remaining(numWorkers);
    std::mutex doneMutex;
    std::condition_variable doneCv;

    auto worker = [&]() {
      for (;;) {
        int myBegin = nextIndex.fetch_add(chunkSize);
        if (myBegin >= end) break;
        int myEnd = (std::min)(myBegin + chunkSize, end);
        for (int i = myBegin; i < myEnd; i++) {
          func(i);
        }
      }
      if (remaining.fetch_sub(1) == 1) {
        std::lock_guard<std::mutex> lock(doneMutex);
        doneCv.notify_one();
      }
    };

    // Submit N-1 tasks to workers, run one chunk on calling thread
    for (int w = 0; w < numWorkers; w++) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_tasks.emplace(worker);
      m_inFlight++;
    }
    m_cv.notify_all();

    // Wait for all workers to finish
    {
      std::unique_lock<std::mutex> lock(doneMutex);
      doneCv.wait(lock, [&]() { return remaining.load() == 0; });
    }
  }

private:
  void WorkerLoop() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_stop || !m_tasks.empty(); });
        if (m_stop && m_tasks.empty()) return;
        task = std::move(m_tasks.front());
        m_tasks.pop();
      }
      task();
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inFlight--;
      }
      m_cvDone.notify_all();
    }
  }

  std::vector<std::thread> m_workers;
  std::queue<std::function<void()>> m_tasks;
  std::mutex m_mutex;
  std::condition_variable m_cv;      // wakes workers
  std::condition_variable m_cvDone;  // wakes WaitAll()
  bool m_stop;
  int m_inFlight;                    // queued + running tasks
};

// ── Global engine thread pool ──────────────────────────────────────
// Created once at startup, lives for the process lifetime.
// Any component can use t850::g_threadPool->Submit() or ParallelFor().
extern ThreadPool* g_threadPool;

// Call once at engine init (before any component uses g_threadPool).
void InitGlobalThreadPool();
// Call once at engine shutdown.
void ShutdownGlobalThreadPool();

} // namespace t850

#endif // T800_THREADPOOL_H

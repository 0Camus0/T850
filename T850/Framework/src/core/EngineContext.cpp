#include <pch.h>

#include <core/EngineContext.h>

#include <core/Config.h>
#include <utils/ThreadPool.h>
#include <video/BaseDriver.h>

namespace t850 {

  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

  EngineContext& GetEngineContext() {
    static EngineContext context;
    return context;
  }

  void SetEngineContext(const EngineContext& context) {
    GetEngineContext() = context;
  }

  void RefreshEngineContextFromGlobals() {
    EngineContext context;
    context.driver = g_pBaseDriver;
    context.device = T8Device;
    context.deviceContext = T8DeviceContext;
    context.threadPool = g_threadPool;
    context.config = &g_config;
    SetEngineContext(context);
  }

  void ClearEngineContext() {
    SetEngineContext(EngineContext{});
  }

} // namespace t850

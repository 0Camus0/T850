#pragma once

namespace t850 {

class BaseDriver;
class Config;
class Device;
class DeviceContext;
class ThreadPool;

struct EngineContext {
  BaseDriver* driver = nullptr;
  Device* device = nullptr;
  DeviceContext* deviceContext = nullptr;
  ThreadPool* threadPool = nullptr;
  Config* config = nullptr;

  bool HasGraphics() const { return driver && device && deviceContext; }
};

EngineContext& GetEngineContext();
void SetEngineContext(const EngineContext& context);
void RefreshEngineContextFromGlobals();
void ClearEngineContext();

} // namespace t850

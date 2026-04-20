# Micros Engine — D3D12 Abstraction Layer Architecture

## DOCUMENT PURPOSE

Machine-readable specification for implementing a D3D12-abstracted rendering engine. Every class, member, relationship, and initialization sequence is documented. Namespace: `micros`. All code under `microengine/`.

---

## 1. LAYER DIAGRAM

```
┌─────────────────────────────────────────────────────┐
│  APPLICATION LAYER (microapp/)                      │
│  Benchmark : AppBase                                │
│  App.cpp (main entry point)                         │
├─────────────────────────────────────────────────────┤
│  FRAMEWORK LAYER (microengine/core/)                │
│  RootFramework (abstract)                           │
│  └── Win32Framework (SDL2 + Win32)                  │
├─────────────────────────────────────────────────────┤
│  SCENE LAYER (microengine/scene/)                   │
│  PrimitiveBase → RenderMesh, RenderQuad,            │
│                  TextRendererMesh, SplineWireframe   │
│  PrimitiveInst, PrimitiveManager, SceneProps        │
├─────────────────────────────────────────────────────┤
│  VIDEO ABSTRACTION LAYER (microengine/video/)       │
│  BaseDriver, Device, DeviceContext, Buffer,         │
│  Texture, RenderTarget, Shader, CommandList,        │
│  CommandQueue, Fence, Heap, PipelineState, Sampler  │
├─────────────────────────────────────────────────────┤
│  D3D12 IMPLEMENTATION (microengine/video/d3d12/)    │
│  D3DX* prefix classes implementing abstract layer   │
├─────────────────────────────────────────────────────┤
│  UTILITIES (microengine/utils/)                     │
│  Camera, Timer, InputManager, ResourceManager,      │
│  xMaths, Spline, cil (image loader), XDataBase      │
└─────────────────────────────────────────────────────┘
```

---

## 2. CONFIGURATION (Config.h)

Compile-time defines controlling API selection and features:

| Define | Purpose |
|--------|---------|
| `OS_WINDOWS` / `OS_LINUX` | Platform detection |
| `FORCE_LOW_RES_TEXTURES` | Force texture downscaling |
| `FORCED_FACTOR` | Downscale factor (default 2) |
| `DEBUG_DRIVER` | Enable driver debug layer |
| `USE_PROFILING` | Enable profiling |
| `MICROS_NO_SIGNATURE` | Value 0, used as default shader sig |
| `USING_SDL` | Window manager selection (always SDL on Windows) |

---

## 3. CORE FRAMEWORK CLASSES

### 3.1 AppBase (abstract)

**File:** `include/core/Core.h`

Base class for all applications. The Benchmark app derives from this.

```
class AppBase {
  // PURE VIRTUALS — app must implement:
  virtual void InitVars() = 0;
  virtual void CreateAssets() = 0;
  virtual void LoadAssets() = 0;
  virtual void DestroyAssets() = 0;
  virtual void OnUpdate() = 0;
  virtual void OnDraw() = 0;
  virtual void OnInput() = 0;
  virtual void OnPause() = 0;
  virtual void OnResume() = 0;
  virtual void OnReset() = 0;
  virtual void LoadScene(int id) = 0;

  // MEMBERS:
  bool applicationInited;
  bool applicationPaused;
  RootFramework* pFramework;       // back-pointer to framework
  InputManager inputManager;       // keyboard/mouse state
  ResourceManager resourceManager; // loads .X model files
};
```

### 3.2 SceneBase (abstract)

**File:** `include/core/Core.h`

Base class for individual scenes (levels). Contains its own SceneProps and framework back-pointer.

```
class SceneBase {
  virtual void OnUpdate(float dtSecs) = 0;
  virtual void OnDraw() = 0;
  virtual void OnInput(InputManager*) = 0;
  virtual void OnLoadScene() = 0;
  virtual void OnDestoryScene() = 0;
  virtual void InitVars() = 0;
  virtual void CreateAssets() = 0;
  virtual void DestroyAssets() = 0;

  SceneProps sceneProperties;
  RootFramework* pFramework;
};
```

### 3.3 RootFramework (abstract)

**File:** `include/core/Core.h`

Owns the video driver and the application.

```
class RootFramework {
  RootFramework(AppBase* pApp);

  virtual void InitGlobalVars() = 0;
  virtual void OnCreateApplication(ApplicationDesc desc) = 0;
  virtual void OnDestroyApplication() = 0;
  virtual void OnInterruptApplication() = 0;
  virtual void OnResumeApplication() = 0;
  virtual void UpdateApplication() = 0;
  virtual void ProcessInput() = 0;
  virtual void ResetApplication() = 0;
  virtual void ChangeAPI(GRAPHICS_API::E api) = 0;

  BaseDriver* pVideoDriver;
  AppBase* pBaseApp;
  bool frameworkInitialized;
  ApplicationDesc aplicationDescriptor;
};
```

### 3.4 Win32Framework : RootFramework

**File:** `include/core/Win32Framework.h`, `src/core/Win32Framework.cpp`

Concrete framework using SDL2 for windowing on Windows.

**Members:**
- `bool appAlive` — main loop sentinel
- `SDL_Window* pWindow` — SDL window handle

**Lifecycle (ChangeAPI method — most important):**
1. If `frameworkInitialized`: calls `pBaseApp->DestroyAssets()`, `pVideoDriver->DestroyDriver()`, `delete pVideoDriver`
2. Creates SDL window with title, resolution, flags from `ApplicationDesc`
3. `MicroBaseDriver = pVideoDriver = new D3DXDriver()` — sets global pointer
4. `MicroBaseDriver->SetDimensions(w, h)`
5. `MicroBaseDriver->SetWindow(GetActiveWindow())` — passes HWND
6. `MicroBaseDriver->InitDriver()` — D3D12 initialization
7. `pBaseApp->CreateAssets()` — app creates shaders/textures/meshes
8. `pVideoDriver->BuildPipelineObjects()` — creates PSOs for all shader×pipeline combos

**UpdateApplication():** `while(appAlive) { ProcessInput(); pBaseApp->OnUpdate(); }`

**ProcessInput():** Pumps SDL events → updates `inputManager.keyStatesMap`, mouse position/delta.

---

## 4. APPLICATION DESCRIPTOR

**File:** `include/video/MicroDescriptors.h`

```
struct ApplicationDesc {
  GRAPHICS_API::E api = D3D12;
  MICROS_VIDEO_MODE::E videoMode = WINDOWED;
  unsigned int width = 1280;
  unsigned int height = 720;
  unsigned int modelQuality = 0;
  bool userBenchmarkMode = false;
  bool useRenderPass = false;
  bool useRenderPassBarriers = false;
  bool invertY = false;
  bool startMinimized = false;
  std::string title;
};
```

---

## 5. VIDEO ABSTRACTION LAYER — ABSTRACT INTERFACES

All abstract base classes live in `include/video/BaseDriver.h`.

### 5.1 IObjectBase (interface)

```
class IObjectBase {
  virtual void* GetAPIObject() const = 0;        // returns raw API pointer (e.g. ID3D12Device*)
  virtual void** GetAPIObjectReference() const = 0; // returns pointer-to-pointer for creation
};
```

Pattern: All D3D12 objects expose their native handle via this interface. Callers cast the `void*` to the native type.

### 5.2 IBindingBase (interface)

```
class IBindingBase {
  std::string GetName();
  void SetName(std::string);
  uint32_t GetBindingSlot(uint64_t sig);    // lookup by shader signature
  void SetBindingSlot(uint64_t sig, uint32_t slot);

  // PROTECTED:
  std::string name;
  uint32_t slot;
  std::unordered_map<uint64_t, uint32_t> bindingsMap;  // sig → slot
};
```

**KEY CONCEPT — Signature-based binding:** Resources (buffers, textures, samplers) store a map of `shaderSignature → rootParameterSlot`. When binding, the system looks up which root parameter slot a resource maps to for the active shader. The slot is resolved via `BaseDriver::GetSlotBinding()` which uses longest-common-substring matching between the resource name and root signature element names.

### 5.3 Device (abstract) : IObjectBase

**File:** `include/video/BaseDriver.h`

Factory for all GPU resources. Single instance, owned by BaseDriver.

```
class Device : public IObjectBase {
  virtual void release() = 0;
  virtual Buffer* CreateBuffer(MICROS_BUFFER_TYPE::E, BufferDesc, void* initialData = nullptr) = 0;
  virtual Shader* CreateShader(std::string src_vs, std::string src_fs, uint64_t sig = 0) = 0;
  virtual Texture* CreateTexture(std::string path) = 0;
  virtual Texture* CreateTextureFromMemory(const unsigned char*, int w, int h, int channels, std::string name) = 0;
  virtual Texture* CreateCubeMap(const unsigned char*, int w, int h) = 0;
  virtual RenderTarget* CreateRT(RenderTargetDesc) = 0;
  virtual CommandList* CreateCommandList(MICROS_COMMAND_LIST::E) = 0;
  virtual CommandQueue* CreateCommandQueue(MICROS_COMMAND_LIST::E) = 0;
  virtual Fence* CreateFence() = 0;
  virtual PipelineState* CreatePipeline(Shader*, PipelineStateDesc) = 0;
  virtual Sampler* CreateSampler(SamplerDesc) = 0;

  void SetContext(DeviceContext*);
  DeviceContext* GetDeviceContext();

  // PROTECTED:
  DeviceContext* pContext;
};
```

### 5.4 DeviceContext (abstract)

**File:** `include/video/BaseDriver.h`

Command recording interface. Wraps a command list + command queue + fence. All draw-time state-setting goes through here.

```
class DeviceContext {
  DeviceContext(BaseDriver* drv, Device* dev);

  // STATE SETTERS:
  virtual void SetCommandQueue(CommandQueue*) = 0;
  virtual void SetCommandList(CommandList*) = 0;
  virtual void SetFence(Fence*) = 0;
  virtual void SetShader(Shader*) = 0;               // also sets root signature + PSO
  virtual void SetViewport(ViewPort) = 0;
  virtual void SetScissorRects(MRect) = 0;
  virtual void SetHeaps(std::vector<Heap*>) = 0;
  virtual void SetRenderTargets(std::vector<RenderTarget*>, RenderTarget* DSV) = 0;
  virtual void BeginRenderPass(std::vector<RenderTarget*>, RenderTarget* DSV) = 0;
  virtual void EndRenderPass() = 0;
  virtual void SetConstantBuffer(uint32_t slot, Buffer*) = 0;
  virtual void SetSampler(uint32_t slot, Sampler*) = 0;
  virtual void SetTexture(uint32_t slot, Texture*) = 0;
  virtual void SetVertexBuffer(uint32_t slot, Buffer*) = 0;
  virtual void SetVertexBuffer(uint32_t slot, Buffer*, uint32_t stride, uint32_t offset) = 0;
  virtual void SetIndexBuffer(Buffer*) = 0;
  virtual void SetPrimitiveTopology(MICROS_TOPOLOGY::E) = 0;
  virtual void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) = 0;

  // CLEAR OPERATIONS (two overloads each — raw handle or RenderTarget):
  virtual void ClearRtv(RenderTarget*, const float[4], uint32_t, MRect*) = 0;
  virtual void ClearDsv(RenderTarget*, MICROS_CLEAR_TYPE::E, float, uint8_t, uint32_t, MRect*) = 0;
  virtual void ClearRtv(void*, const float[4], uint32_t, MRect*) = 0;
  virtual void ClearDsv(void*, MICROS_CLEAR_TYPE::E, float, uint8_t, uint32_t, MRect*) = 0;

  virtual void SetBlendFactor(const float[4]) = 0;

  // COMMAND LIST OPERATIONS:
  virtual void CloseCommandList() = 0;
  virtual void ResetCommandList() = 0;
  virtual void TransisionBarrier(Texture*, MICROS_RESOURCE_STATE_TYPE before, MICROS_RESOURCE_STATE_TYPE after) = 0;
  virtual void TransisionBarrier(RenderTarget*, MICROS_RESOURCE_STATE_TYPE before, MICROS_RESOURCE_STATE_TYPE after) = 0;
  virtual void Execute() = 0;
  virtual void ExecuteAndWait() = 0;     // executes + fence wait

  BaseDriver* GetDriver();
  Device* GetDevice();

  // PROTECTED:
  BaseDriver* pDriver;
  Device* pDevice;
  CommandQueue* pActualQueue;
  CommandList* pActualList;
  Buffer* pIndexBuffer;
  Buffer* pVertexBuffer;
  Shader* pActualShader;
  Fence* pActualFence;
  std::vector<Heap*> pActualHeaps;
  std::vector<RenderTarget*> pRenderTargets;
  RenderTarget* pActualDepthDSV;
};
```

### 5.5 Buffer (abstract) : IObjectBase, IBindingBase

```
class Buffer : public IObjectBase, public IBindingBase {
  virtual void Create(Device*, BufferDesc, void* initialData = nullptr) = 0;
  virtual void UpdateFromSystemCopy(Device*) = 0;    // copies sysMemCpy → GPU
  virtual void UpdateFromBuffer(Device*, const void*) = 0;  // copies arbitrary ptr → GPU
  virtual void release() = 0;

  BufferDesc descriptor;
  std::vector<char> sysMemCpy;   // CPU-side shadow copy
};
```

### 5.6 Texture (abstract) : IObjectBase, IBindingBase

```
class Texture : public IObjectBase, public IBindingBase {
  // API-agnostic loaders (call virtual API methods internally):
  bool LoadTexture(Device*, const char* fn);
  bool LoadFromMemory(Device*, const unsigned char*, int w, int h, int channels);
  bool CreateCubeMap(Device*, const unsigned char*, int w, int h);
  void release();

  // API-SPECIFIC VIRTUALS:
  virtual void LoadAPITexture(Device*, unsigned char* buffer) = 0;
  virtual void LoadAPITextureCompressed(unsigned char*) = 0;
  virtual void DestroyAPITexture() = 0;
  virtual void SetTextureParams() = 0;
  virtual void GetFormatBpp(unsigned int&, unsigned int&, unsigned int&) = 0;

  // MEMBERS:
  std::string filepath;
  unsigned int size, props, params, cil_props;
  unsigned int x, y;                    // dimensions
  unsigned int id, bounded, mipmaps, m_channels;

  // DESCRIPTOR HEAP ADDRESSES (stored as void* for API-agnosticism):
  void* ownSrvCPUAddressRTV;     void* ownSrvGPUAddressRTV;
  void* ownSrvCPUAddressDsv_RW;  void* ownSrvGPUAddressDsv_RW;
  void* ownSrvCPUAddressDsv_RO;  void* ownSrvGPUAddressDsv_RO;
  void* ownSrvCPUAddressSRV;     void* ownSrvGPUAddressSRV;

  std::string m_shaderTextureName;
  static std::vector<Texture*> vTexturesAll;
};
```

**KEY PATTERN:** Textures store their own SRV/RTV/DSV descriptor heap addresses as `void*`. When binding to a shader, the D3D12 layer casts to `D3D12_GPU_DESCRIPTOR_HANDLE`. This allows the abstraction layer to pass descriptor addresses without knowing D3D12 types.

### 5.7 RenderTarget (abstract)

```
class RenderTarget {
  bool LoadRT(Device*, RenderTargetDesc);       // calls LoadAPIRT
  virtual bool LoadAPIRT(Device*) = 0;
  void release();                               // calls DestroyAPIRT
  virtual void DestroyAPIRT() = 0;
  virtual void Set(Device*) = 0;
  virtual void ChangeCubeDepthTexture(int) = 0;

  RenderTargetDesc bufferDesc;
  Texture* pTexture;                 // associated texture (for reading as SRV)
  MICROS_RESOURCE_STATE_TYPE resourceState;
  float clearDepthValue;
  float clearStencilValue;
  bool isDSV;
};
```

### 5.8 Shader (abstract)

```
class Shader {
  bool CreateShader(Device*, std::string src_vs, std::string src_fs, uint64_t sig = 0);
  // CreateShader: if sig != 0, prepends #defines based on signature bitflags
  virtual bool CreateShaderAPI(Device*, std::string, std::string, uint64_t) = 0;
  virtual void Set(Device*) = 0;
  virtual void DestroyAPIShader() = 0;
  void release();

  ConstantBufferDesc cbuffDescVS;
  ConstantBufferDesc cbuffDescPS;
  RootSignature vSignature;          // parsed from shader reflection
  uint32_t pipelineId;
  uint64_t Sig;                      // unique signature (bitmask of ShaderSig flags)
  static std::vector<Shader*> vShadersAll;
};
```

**SHADER SIGNATURE SYSTEM:** `Sig` is a `uint64_t` bitmask composed of `ShaderSig` enum values (HAS_NORMALS, DIFFUSE_MAP, SHADOW_MAP_PASS, TEXT_PASS, etc.). The base `CreateShader()` translates these bits into `#define` preprocessor directives prepended to the HLSL source. This enables uber-shader compilation with feature permutations.

### 5.9 CommandList (abstract)

```
class CommandList {
  virtual void* GetAPIObject() const = 0;
  virtual void** GetAPIObjectReference() const = 0;
  virtual bool CreateCommandListAPI(Device*, MICROS_COMMAND_LIST::E) = 0;
  virtual bool DestroyCommandListAPI() = 0;
  static std::vector<CommandList*> vCommandListsAll;
};
```

### 5.10 CommandQueue (abstract)

```
class CommandQueue {
  virtual void* GetAPIObject() const = 0;
  virtual void** GetAPIObjectReference() const = 0;
  virtual bool CreateCommandQueueAPI(Device*, MICROS_COMMAND_LIST::E) = 0;
  virtual bool DestroyCommandQueueAPI() = 0;
  virtual bool ExecuteCommandList(uint32_t count, CommandList**) = 0;
  virtual bool Signal(Fence*, uint64_t value) = 0;
  virtual bool Wait(Fence*, uint64_t value) = 0;
  static std::vector<CommandQueue*> vCommandQueueAll;
};
```

### 5.11 Fence (abstract)

```
class Fence {
  virtual void* GetAPIObject() const = 0;
  virtual void** GetAPIObjectReference() const = 0;
  virtual bool CreateFence(Device*) = 0;
  virtual uint64_t GetCompletedValue() = 0;
  virtual void SetEventOnCompletion(uint64_t, void* event) = 0;
  virtual void Signal(uint64_t value) = 0;
  virtual void WaitForCompletion(CommandQueue*) = 0;
  static std::vector<Fence*> vFencesAll;
};
```

### 5.12 Heap (abstract) : IObjectBase

```
class Heap : public IObjectBase {
  virtual bool Create(Device*, MICROS_HEAP_TYPE::E, uint32_t numDescriptors, bool shaderVisible) = 0;
  virtual uint64_t GetIncrementSize() = 0;
  virtual void* GetOffsetFromStart(MICROS_HEAP_ADDRESS_SPACE::E) = 0;
  virtual void* GetOffsetFromIndex(MICROS_HEAP_ADDRESS_SPACE::E, uint64_t index) = 0;
  virtual void* GetOffsetFromCurrent(MICROS_HEAP_ADDRESS_SPACE::E) = 0;
  virtual void IncrementUsedDescriptorIndex() = 0;
  virtual uint64_t GetCurrentIndex() = 0;
  virtual MICROS_HEAP_TYPE::E GetType() = 0;
};
```

**PATTERN — Linear allocator:** Heap tracks `currentDescCount`. `GetOffsetFromCurrent()` returns `heapStart + currentDescCount * incrementSize`. After placing a descriptor, call `IncrementUsedDescriptorIndex()`. No free/recycle.

### 5.13 PipelineState (abstract) : IObjectBase

```
class PipelineState : public IObjectBase {
  virtual bool Create(Device*, Shader*, PipelineStateDesc) = 0;

  uint64_t shaderSignature;
  uint64_t pipelineSignature;
  Shader* pShader;
  PipelineStateDesc pipelineDesc;
};
```

### 5.14 PipelineStateObject (container)

```
class PipelineStateObject {
  uint64_t signature;
  std::unordered_map<uint64_t, PipelineState*> vPipelineObjects;  // shaderSig → PSO
};
```

### 5.15 Sampler (abstract) : IBindingBase

```
class Sampler : public IBindingBase {
  virtual void CreateSampler(Device*, SamplerDesc) = 0;
  void* ownSrvCPUAddress;
  void* ownSrvGPUAddress;
  // PROTECTED:
  SamplerDesc samplerDesc;
};
```

---

## 6. BASEDRIVER — THE CENTRAL MANAGER

**File:** `include/video/BaseDriver.h`, `src/video/BaseDriver.cpp`

Concrete orchestrator class. Owns all GPU resources in vectors. Provides factory methods that delegate to Device, then store results.

### 6.1 Heap Enum

```
enum HEAPS {
  H_CBV_SRV_UAV_SHADER_VISIBLE = 0,
  H_CBV_SRV_UAV_SHADER_NOT_VISIBLE,
  H_SAMPLER,
  H_RTV,
  H_DSV,
  H_MAX
};
```

### 6.2 Members

```
Device* pDevice;
DeviceContext* pDeviceContext;

std::vector<Heap*> vHeaps;                // indexed by HEAPS enum
std::vector<Shader*> vShaderSignatures;
std::vector<Texture*> vTextures;
std::vector<RenderTarget*> vRenderTargets;
std::vector<CommandList*> vCommandLists;
std::vector<CommandQueue*> vCommandQueues;
std::vector<Fence*> vFences;
std::vector<PipelineState*> vPipelines;
std::vector<Sampler*> vSamplers;

std::vector<PipelineStateDesc> pipelinesDescs;
std::unordered_map<uint64_t, PipelineStateObject> mapPipelineStateObjects;

uint64_t defaultOpaquePipeline;
uint64_t currentPipeline;
int defaultCommandListId, defaultCommandQueueId, defaultFenceId, defaultSampler;
GRAPHICS_API::E m_currentAPI;
PipelineStateDesc pipelineDesc;
int width, height;
```

### 6.3 Virtual Methods

```
virtual void InitDriver() = 0;
virtual void CreateSurfaces() = 0;
virtual void DestroySurfaces() = 0;
virtual void Update() = 0;
virtual void DestroyDriver() = 0;
virtual void SetWindow(void*) = 0;
virtual void SetDimensions(int, int) = 0;
virtual void Clear() = 0;
virtual void SwapBuffers() = 0;
virtual void PushBackBuffer() = 0;
virtual void SaveScreenshot(std::string) = 0;
virtual void SetViewPortAndScissors() = 0;
virtual void BeginRenderPass(std::vector<RenderTarget*>, RenderTarget*) = 0;
virtual void EndRenderPass() = 0;
virtual void PopRT() = 0;
virtual void* GetDevice() = 0;
```

### 6.4 Factory Methods (non-virtual, in BaseDriver)

All return `uint32_t` index into the corresponding vector:

```
uint32_t CreateTexture(std::string path);       // deduplicates by filepath
uint32_t CreateCubeMap(const unsigned char*, int, int);
uint32_t CreateShader(std::string vs, std::string fs, uint64_t sig);  // deduplicates by sig
uint32_t CreateRT(RenderTargetDesc);
uint32_t CreateCommandList(MICROS_COMMAND_LIST::E);
uint32_t CreateCommandQueue(MICROS_COMMAND_LIST::E);
uint32_t CreateFence();
uint32_t CreatePipeline(Shader*, PipelineStateDesc);
uint32_t CreatePipeline(Shader*);              // uses current pipelineDesc
uint32_t CreateSampler(SamplerDesc);
```

### 6.5 Pipeline Management

```
uint64_t AddPipeline(PipelineStateDesc desc);   // stores desc, returns index
void BuildPipelineObjects();                     // creates PSOs for ALL shaders × ALL pipeline descs
void SetPipeline(uint64_t pipeline);             // sets currentPipeline
PipelineState* GetPipeline(uint64_t shaderSig);  // returns mapPipelineStateObjects[currentPipeline][shaderSig]
```

**BuildPipelineObjects()** is called ONCE after all shaders are created. It iterates `pipelinesDescs × vShaderSignatures` and creates a PSO for each combination. These are stored in `mapPipelineStateObjects[pipelineDescIndex].vPipelineObjects[shaderSig]`.

### 6.6 Resource Barrier Helper

```
Texture* GetRenderTargetTexture(RenderTarget* rtv);
```
Transitions the RT from current state to `STATE_PIXEL_SHADER_RESOURCE` (+ `STATE_DEPTH_READ` if DSV), returns its texture. Used when reading an RT as an SRV.

### 6.7 Slot Binding Resolution

```
uint32_t GetSlotBinding(uint64_t shaderSig, IBindingBase* pResource);
uint32_t GetSlotBinding(uint64_t shaderSig, std::string name);
```
Uses **longest common substring** (case-insensitive) between resource name and root parameter names in the shader's `vSignature.vRootElements[]`. Returns the root parameter index (slot). Returns `MICROS_INVALID_SLOT` (0x40000000) if best match < 5 chars.

### 6.8 RT Push/Pop

```
void PushRT(std::vector<RenderTarget*> RTVs, RenderTarget* pDSV);
```
1. Barriers RTVs to `STATE_RENDER_TARGET`, DSV to `STATE_DEPTH_WRITE`
2. Sets viewport/scissor from RT dimensions
3. Calls `pDeviceContext->SetRenderTargets()`
4. Clears all RTVs + DSV

`PopRT()` → D3D12 impl restores back buffer as render target.

### 6.9 Global Pointer

```
extern BaseDriver* MicroBaseDriver;   // global singleton, set in Win32Framework::ChangeAPI
extern uint32_t gSeedKey;
#define GETDRIVERBASE() MicroBaseDriver
```

---

## 7. D3D12 IMPLEMENTATION CLASSES

### 7.1 D3DXDriver : BaseDriver

**File:** `include/video/d3d12/D3DXDriver.h`, `src/video/d3d12/D3DXDriver.cpp`

```
class D3DXDriver : public BaseDriver {
  static const UINT backbufferCount = 2;

  struct BackBufferNative {
    D3D12_CPU_DESCRIPTOR_HANDLE decriptorHandle;
    ID3D12Resource* pBuffer;
  };

  BackBufferNative BackBuffersRtv[2];
  BackBufferNative DepthBufferDsv;
  HWND hwnd;
  ID3D12PipelineState* pPipelineState;
  ComPtr<IDXGISwapChain3> pSwapChain;
  IDXGIFactory4* pDxgiFactory;
  ID3D12Debug3* pDebug;
  IDXGIAdapter1* pAdapter;
  uint32_t currentBackBuffer;
  ViewPort viewport;
  MRect surfaceSize;
};
```

#### InitDriver() Sequence:
1. `pDevice = new D3DXDevice()`
2. `pDeviceContext = new D3DXDeviceContext(this, pDevice)`
3. `pDevice->SetContext(pDeviceContext)`
4. **Debug layer:** `D3D12GetDebugInterface()` → `EnableDebugLayer()` + `SetEnableGPUBasedValidation(true)` (if `_DEBUG`)
5. `CreateDXGIFactory1()`
6. Enumerate adapters, pick first non-software adapter
7. `D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0)`
8. Create default CommandList (DIRECT), CommandQueue (DIRECT), Fence
9. Create swap chain: `DXGI_FORMAT_R8G8B8A8_UNORM`, 2 buffers, `FLIP_DISCARD`
10. Create 5 descriptor heaps:
    - `H_CBV_SRV_UAV_SHADER_VISIBLE`: 512 descriptors, shader-visible
    - `H_CBV_SRV_UAV_SHADER_NOT_VISIBLE`: 512 descriptors, not shader-visible
    - `H_SAMPLER`: 512 descriptors, shader-visible
    - `H_RTV`: 64 descriptors, not shader-visible
    - `H_DSV`: 32 descriptors, not shader-visible
11. Create RTV descriptors for each back buffer from RTV heap
12. Create depth buffer: `D3D12_RESOURCE_DIMENSION_TEXTURE2D`, `DXGI_FORMAT_D32_FLOAT`, `ALLOW_DEPTH_STENCIL`
13. Create DSV descriptor
14. Set viewport and scissor rect
15. Create default sampler: `MIN_MAG_MIP_LINEAR`, `CLAMP`, `COMPARISON_FUNC_NEVER`, maxLOD = `D3D12_FLOAT32_MAX`
16. Set default command list/queue/fence on DeviceContext
17. Register default pipeline desc: `CULL_CLOCKWISE`, `DEPTH_DEFAULT`, `BLEND_OPAQUE`

#### Clear():
1. Transition back buffer: `PRESENT → RENDER_TARGET`
2. Set back buffer + depth buffer as render targets
3. Set viewport + scissor
4. Clear RTV (white) and DSV (depth=1.0)

#### SwapBuffers():
1. Transition back buffer: `RENDER_TARGET → PRESENT`
2. Close command list
3. Execute + fence wait
4. `pSwapChain->Present(0, 0)`
5. Update `currentBackBuffer`
6. Reset command list (reset allocator + reset cmd list)

#### BeginRenderPass():
1. For each RTV: if not already `STATE_RENDER_TARGET`, transition
2. For DSV: if not already `STATE_DEPTH_WRITE`, transition
3. Call `pDeviceContext->BeginRenderPass()`

### 7.2 D3DXDevice : Device

**File:** `include/video/d3d12/D3DXDevice.h`, `src/video/d3d12/D3DXDevice.cpp`

```
class D3DXDevice : public Device {
  ID3D12Device* pDevice;  // PRIVATE
};
```

Factory method implementations — all `new D3DX*()` + call creation method:

| Method | Creates |
|--------|---------|
| `CreateBuffer(VERTEX, ...)` | `new D3DXVertexBuffer()` |
| `CreateBuffer(INDEX, ...)` | `new D3DXIndexBuffer()` |
| `CreateBuffer(CONSTANT, ...)` | `new D3DXConstantBuffer()` |
| `CreateShader(...)` | `new D3DXShader()` |
| `CreateTexture(path)` | `new D3DXTexture()` → `LoadTexture()` |
| `CreateTextureFromMemory(...)` | `new D3DXTexture()` → `LoadFromMemory()` |
| `CreateCubeMap(...)` | `new D3DXTexture()` → `CreateCubeMap()` |
| `CreateRT(...)` | `new D3DXRenderTarget()` → `LoadRT()` |
| `CreateCommandList(...)` | `new D3DXCommandList()` |
| `CreateCommandQueue(...)` | `new D3DXCommandQueue()` |
| `CreateFence()` | `new D3DXFence()` |
| `CreatePipeline(...)` | `new D3DXPipelineState()` |
| `CreateSampler(...)` | `new D3DXSampler()` |

### 7.3 D3DXDeviceContext : DeviceContext

**File:** `include/video/d3d12/D3DXDeviceContext.h`, `src/video/d3d12/D3DXDeviceContext.cpp`

```
class D3DXDeviceContext : public DeviceContext {
  // PRIVATE — cached native pointers for perf:
  D3DXDriver* pDriver12;
  D3DXDevice* pDevice12;
  D3D12_CPU_DESCRIPTOR_HANDLE* pBBRtv;   // back buffer RTV
  D3D12_CPU_DESCRIPTOR_HANDLE* pBBDsv;   // back buffer DSV
  ID3D12CommandQueue* pActualCommandQueueNative;
  ID3D12GraphicsCommandList4* pActualCommandListNative;
  D3D12_INDEX_BUFFER_VIEW* pActualIndexBufferNative;
  D3D12_VERTEX_BUFFER_VIEW* pActualVertexBufferNative;
  D3D12_CONSTANT_BUFFER_VIEW_DESC* pActualConstantBufferNative;
  ID3D12Fence* pActualFenceNative;
};
```

#### Key Implementation Details:

**SetShader(Shader*):**
1. Cast to `D3DXShader*`
2. Look up PSO: `pDriver12->GetPipeline(shaderSig)->GetAPIObject()`
3. `pCmdList->SetGraphicsRootSignature(pShader12->pRootSignature)`
4. `pCmdList->SetPipelineState(pPipelineState)`

**SetConstantBuffer(slot, Buffer*):**
- `pCmdList->SetGraphicsRootDescriptorTable(slot, pConstantBuffer->gpuHandle)`
- Returns early if `slot == MICROS_INVALID_SLOT`

**SetTexture(slot, Texture*):**
- `pCmdList->SetGraphicsRootDescriptorTable(slot, {pTexture->ownSrvGPUAddressSRV})`
- Returns early if `slot == MICROS_INVALID_SLOT`

**SetSampler(slot, Sampler*):**
- `pCmdList->SetGraphicsRootDescriptorTable(slot, {pSampler->ownSrvGPUAddress})`

**DrawIndexed(vertexCount, startIndex, startVertex):**
- `pCmdList->DrawIndexedInstanced(vertexCount, 1, 0, 0, 0)`

**BeginRenderPass():** Uses `ID3D12GraphicsCommandList4::BeginRenderPass()` with:
- `D3D12_RENDER_PASS_RENDER_TARGET_DESC` per RTV, mapping `bufferDesc.beginOp/endOp` to `D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE/ENDING_ACCESS_TYPE`
- `D3D12_RENDER_PASS_DEPTH_STENCIL_DESC` for DSV
- If DSV is `readOnly`: sets `D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_DEPTH`
- Supports `PRESERVE_LOCAL_RENDER` and `PRESERVE_LOCAL_SRV` types

**TransisionBarrier():** Creates `CD3DX12_RESOURCE_BARRIER::Transition()` and calls `ResourceBarrier()`.

**ExecuteAndWait():**
1. `pCmdQueue->ExecuteCommandLists(1, &pCmdList)`
2. `pActualFence->WaitForCompletion(pActualQueue)`

**ResetCommandList():**
1. Get allocator from `D3DXCommandList::GetAllocatorAPIObject()`
2. `pAlloc->Reset()`
3. `pCmdList->Reset(pAlloc, nullptr)`

**D3D12-specific extra methods (not in base):**
- `SetBackBuffer(D3D12_CPU_DESCRIPTOR_HANDLE* rtv, dsv)` — `OMSetRenderTargets(1, rtv, FALSE, dsv)`
- `TransitionBackBufferToPresent(ID3D12Resource*)` — `RENDER_TARGET → PRESENT`
- `TransitionBackBufferToRTV(ID3D12Resource*)` — `PRESENT → RENDER_TARGET`

### 7.4 D3DXShader : Shader

**File:** `include/video/d3d12/D3DXShader.h`, `src/video/d3d12/D3DXShader.cpp`

```
class D3DXShader : public Shader {
  ComPtr<ID3DBlob> VS_blob;
  ComPtr<ID3DBlob> FS_blob;
  ID3D12RootSignature* pRootSignature;
  std::vector<D3D12_INPUT_ELEMENT_DESC> VertexDecl;
  std::vector<OutputLayout> OutputDecl;

  // PRIVATE:
  bool createRootSignature(Device*, ID3D12ShaderReflection*, MICROS_SHADER_TYPE::E);
  std::vector<std::string*> semanticsVector;  // lifetime management for semantic names
};
```

#### CreateShaderAPI() Sequence:
1. **Compile VS:** `D3DCompile(src_vs, "VS", "vs_5_0", DEBUG|SKIP_OPT|ALL_RESOURCES_BOUND)`
2. **Compile FS:** `D3DCompile(src_fs, "FS", "ps_5_0", ...)`
3. **Reflect VS:** `D3DReflect()` → extract input layout:
   - For each `InputParameter`: determine format from `Mask` (1→R32, ≤3→R32G32, ≤7→R32G32B32, ≤15→R32G32B32A32) and `ComponentType`
   - Build `D3D12_INPUT_ELEMENT_DESC` with auto-calculated byte offsets
4. **createRootSignature(VS):** Iterate `BoundResources` via reflection, create `RootParameter` for each:
   - `D3D_SIT_CBUFFER` → `CONSTANT_BUFFER`
   - `D3D_SIT_TEXTURE/STRUCTURED/etc` → `SHADER_RESOURCE_VIEW`
   - `D3D_SIT_SAMPLER` → `SAMPLER`
   - `D3D_SIT_UAV_*` → `UAV`
5. **Reflect FS:** Extract output parameters (semantic names + component masks) → `OutputDecl`
6. **createRootSignature(FS):** same as VS
7. **Sort** root elements by type (CBV first, then SRV, SAMPLER, UAV)
8. **Deduplicate CBVs:** If 2 constant buffer entries (VS+FS share), remove duplicate
9. **Build D3D12 root signature:**
   - Each root element → `D3D12_DESCRIPTOR_RANGE` + `D3D12_ROOT_PARAMETER` (descriptor table, visibility ALL)
   - Flags: `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`
   - `D3D12SerializeRootSignature()` + `device->CreateRootSignature()`

### 7.5 D3DXPipelineState : PipelineState

**File:** `include/video/d3d12/D3DXDevicePipeline.h`, `src/video/d3d12/D3DXPipelineState.cpp`

```
class D3DXPipelineState : public PipelineState {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
  ID3D12PipelineState* pPipeline;
};
```

#### Create() Logic:

1. **Input layout** from `D3DXShader::VertexDecl`
2. **Root signature** from `D3DXShader::pRootSignature`
3. **VS/PS bytecode** from `D3DXShader::VS_blob/FS_blob`
4. **Sample mask** = `UINT_MAX`, **sample count** = 1
5. **Topology type** = `TRIANGLE`
6. **Rasterizer state** from `PipelineStateDesc::rasterState`:
   - `CULL_NONE` → `FILL_SOLID`, `CULL_MODE_NONE`
   - `CULL_CLOCKWISE` → `FILL_SOLID`, `CULL_MODE_FRONT`
   - `CULL_COUNTER_CLOCK_WISE` → `FILL_SOLID`, `CULL_MODE_BACK`
   - `CULL_WIREFRAME` → `FILL_WIREFRAME`, `CULL_MODE_NONE`
7. **Render targets** from `OutputDecl` (or `overrideRTVs` if `overridePSORTV`):
   - Counts `SV_TARGET` outputs
   - Maps component type to format: `FLOAT4→R8G8B8A8`, `FLOAT3→R8G8B8A8`, `FLOAT2→R16G16`, `FLOAT→R8`
8. **Depth/stencil** from `PipelineStateDesc::depthState`:
   - `DEPTH_NONE` → disabled, zero write mask
   - `DEPTH_DEFAULT` → enabled, `LESS_EQUAL`
   - `DEPTH_READ` → enabled, zero write mask, `LESS_EQUAL`
   - `DEPTH_REVERSE_Z` → enabled, `GREATER_EQUAL`
   - `DEPTH_READ_REVERSE_Z` → enabled, zero write mask, `GREATER_EQUAL`
   - DSV format always `DXGI_FORMAT_D32_FLOAT`
9. **Blend state** from `PipelineStateDesc::blendState`:
   - `BLEND_OPAQUE` → disabled
   - `BLEND_ALPHA` → `SRC=ONE`, `DST=INV_SRC_ALPHA`, `OP=ADD`
   - `BLEND_ADDITIVE` → `SRC=SRC_ALPHA`, `DST=ONE`
   - `BLEND_NON_PREMULTIPLIED` → `SRC=SRC_ALPHA`, `DST=INV_SRC_ALPHA`
10. `CreateGraphicsPipelineState()`

### 7.6 D3DXHeap : Heap

**File:** `include/video/d3d12/D3DXHeap.h`, `src/video/d3d12/D3DXHeap.cpp`

```
class D3DXHeap : public Heap {
  ID3D12DescriptorHeap* pHeap;
  D3D12_DESCRIPTOR_HEAP_TYPE nativeType;
  MICROS_HEAP_TYPE::E type;
  uint64_t maxDescriptors;
  uint64_t currentDescCount;
  uint64_t intementSize;     // note: typo in codebase
  bool visibleToShader;
};
```

#### Create():
1. Map `MICROS_HEAP_TYPE` to `D3D12_DESCRIPTOR_HEAP_TYPE`:
   - `CBV_SRV_UAV` → `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV`
   - `SAMPLER` → `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER`
   - `RTV` → `D3D12_DESCRIPTOR_HEAP_TYPE_RTV`
   - `DSV` → `D3D12_DESCRIPTOR_HEAP_TYPE_DSV`
2. Set `SHADER_VISIBLE` flag if `shaderVisible`
3. `CreateDescriptorHeap()`
4. `intementSize = GetDescriptorHandleIncrementSize(type)`

#### Offset Calculation:
```
GetOffsetFromCurrent(CPU) → pHeap->GetCPUDescriptorHandleForHeapStart().ptr + currentDescCount * intementSize
GetOffsetFromCurrent(GPU) → pHeap->GetGPUDescriptorHandleForHeapStart().ptr + currentDescCount * intementSize
```

### 7.7 D3DXFence : Fence

**File:** `include/video/d3d12/D3DXFence.h`, `src/video/d3d12/D3DXFence.cpp`

```
class D3DXFence : public Fence {
  ID3D12Fence* pFence;
  uint32_t fenceValue;         // monotonically increasing
  HANDLE eventHandle;
};
```

#### WaitForCompletion(CommandQueue*):
```
const UINT64 fence = fenceValue;
cmdQueue->Signal(this, fence);
fenceValue++;
if (pFence->GetCompletedValue() < fence) {
  pFence->SetEventOnCompletion(fence, eventHandle);
  WaitForSingleObject(eventHandle, INFINITE);
}
```

### 7.8 D3DXCommandList : CommandList

**File:** `include/video/d3d12/D3DXCommandList.h`, `src/video/d3d12/D3DXCommandList.cpp`

```
class D3DXCommandList : public CommandList {
  ID3D12CommandAllocator* pCommandAllocator;
  ID3D12GraphicsCommandList4* pCommandList;   // Note: List4 for render pass support
};
```

Creates both allocator and command list. Extra methods: `GetAllocatorAPIObject()`, `GetAllocatorAPIObjectReference()` — needed for reset.

### 7.9 D3DXCommandQueue : CommandQueue

**File:** `include/video/d3d12/D3DXCommandQueue.h`, `src/video/d3d12/D3DXCommandQueue.cpp`

```
class D3DXCommandQueue : public CommandQueue {
  ID3D12CommandQueue* pCommandQueue;
};
```

Maps `MICROS_COMMAND_LIST` types: `DIRECT→DIRECT`, `COMPUTE→COMPUTE`, `COPY→COPY`.

### 7.10 D3DXVertexBuffer : Buffer

**File:** `include/video/d3d12/D3DXVertexBuffer.h`, `src/video/d3d12/D3DXVertexBuffer.cpp`

```
class D3DXVertexBuffer : public Buffer {
  ID3D12Resource* APIBuffer;
  D3D12_VERTEX_BUFFER_VIEW viewDesc;
};
```

#### Create() dispatches by usage:

**DYNAMIC:** Upload heap, `Map/Unmap` initial data, view = `{GPUVirtualAddress, stride, byteWidth}`.

**STATIC:** 
1. Default heap resource (state `COMMON`)
2. Upload heap staging buffer
3. Create temp command queue/list/fence
4. `UpdateSubresources()` from upload → default
5. Barrier: `COPY_DEST → VERTEX_AND_CONSTANT_BUFFER`
6. Execute, fence wait, release temp objects
7. Set view desc

### 7.11 D3DXIndexBuffer : Buffer

**File:** `include/video/d3d12/D3DXIndexBuffer.h`, `src/video/d3d12/D3DXIndexBuffer.cpp`

```
class D3DXIndexBuffer : public Buffer {
  ID3D12Resource* APIBuffer;
  D3D12_INDEX_BUFFER_VIEW viewDesc;  // Format = R32_UINT
};
```

Same STATIC/DYNAMIC pattern as vertex buffer. Static path transitions to `STATE_INDEX_BUFFER`.

### 7.12 D3DXConstantBuffer : Buffer

**File:** `include/video/d3d12/D3DXConstantBuffer.h`, `src/video/d3d12/D3DXConstantBuffer.cpp`

```
class D3DXConstantBuffer : public Buffer {
  ID3D12Resource* APIBuffer;
  D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
};
```

#### Create():
1. Upload heap, size 256-byte aligned: `(byteWidth + 255) & ~255`
2. State `GENERIC_READ`
3. Copy initial data to `sysMemCpy`
4. Allocate CBV descriptor from `H_CBV_SRV_UAV_SHADER_VISIBLE` heap
5. `CreateConstantBufferView()`
6. `Map/memcpy/Unmap` initial data

#### UpdateFromSystemCopy/UpdateFromBuffer:
`Map → memcpy → Unmap` (no barriers needed, upload heap is persistently mapped-compatible).

### 7.13 D3DXTexture : Texture

**File:** `include/video/d3d12/D3DXTexture.h`, `src/video/d3d12/D3DXTexture.cpp`

```
class D3DXTexture : public Texture {
  ComPtr<ID3D12Resource> pTex;
};
```

#### LoadAPITexture():
1. Determine format: `CH_ALPHA → R8_UNORM`, cubemap → `R8G8B8A8_UNORM`, else → `B8G8R8A8_UNORM`
2. If >2048px: enable mipmap downscaling (skip first N mips)
3. Create committed resource: default heap, `COPY_DEST`, `TEXTURE2D`
4. Create upload buffer: `GetRequiredIntermediateSize()`
5. Build `D3D12_SUBRESOURCE_DATA` array for all mips × array slices
6. Create temp queue/allocator/list
7. `UpdateSubresources()` + barrier `COPY_DEST → PIXEL_SHADER_RESOURCE`
8. Allocate SRV descriptor from `H_CBV_SRV_UAV_SHADER_VISIBLE` heap
9. Create SRV: `TEXTURE2D` or `TEXTURECUBE`, `MipLevels = desc.MipLevels`
10. Execute, fence wait, cleanup

### 7.14 D3DXRenderTarget : RenderTarget

**File:** `include/video/d3d12/D3DXRenderTarget.h`, `src/video/d3d12/D3DXRenderTarget.cpp`

```
class D3DXRenderTarget : public RenderTarget {
  D3D12_CPU_DESCRIPTOR_HANDLE heapLocationCPURTV, heapLocationCPUDsv_RO, heapLocationCPUDsv_RW, heapLocationCPUSRV;
  D3D12_GPU_DESCRIPTOR_HANDLE heapLocationGPURTV, heapLocationGPUDsv_RO, heapLocationGPUDsv_RW, heapLocationGPUSRV;
  ID3D12Resource* APIBuffer;
  ID3D12Resource* APIBufferRO;
  DXGI_FORMAT formatNative;
  DXGI_FORMAT formatSrvNative;
};
```

#### LoadAPIRT():
1. Determine format from `RTV_FORMAT` enum:
   - `F32 → D32_FLOAT / R32_FLOAT`
   - `RGBA8 → R8G8B8A8_UNORM`
   - `RGBA16F → R16G16B16A16_FLOAT`
   - etc.
2. Set flags: DSV gets `ALLOW_DEPTH_STENCIL`, color gets `ALLOW_RENDER_TARGET`
3. `CreateCommittedResource()` (default heap)
4. **If DSV:**
   - Allocate 2 DSV descriptors from `H_DSV` heap (read-write + read-only with `D3D12_DSV_FLAG_READ_ONLY_DEPTH`)
   - Store CPU/GPU handles
5. **If Color:**
   - Allocate RTV descriptor from `H_RTV` heap
   - `CreateRenderTargetView()`
6. **Always:** Allocate SRV descriptor from `H_CBV_SRV_UAV_SHADER_VISIBLE` heap
   - `CreateShaderResourceView()` with `TEXTURE2D`, 1 mip
7. Create `D3DXTexture` to hold the resource, copy descriptor addresses to texture's `ownSrv*` members
8. Set `pTexture = pTexlocal`

### 7.15 D3DXSampler : Sampler

**File:** `include/video/d3d12/D3DXSampler.h`, `src/video/d3d12/D3DXSampler.cpp`

```
class D3DXSampler : public Sampler {
  // No additional members
};
```

#### CreateSampler():
1. Map `SamplerDesc` fields directly to `D3D12_SAMPLER_DESC` (enums are aligned 1:1)
2. Allocate from `H_SAMPLER` heap
3. `device->CreateSampler()`
4. Store CPU/GPU handles in `ownSrvCPUAddress`, `ownSrvGPUAddress`

---

## 8. DESCRIPTOR STRUCTS

### 8.1 BufferDesc
```
struct BufferDesc {
  uint64_t virtualAddress = 0;
  uint32_t byteWidth = 0;
  uint32_t stride = 0;
  MICROS_BUFFER_USAGE::E usage;   // DEFAULT, DYNAMIC, STATIC
};
```

### 8.2 RenderTargetDesc
```
struct RenderTargetDesc {
  MICROS_RTV_TYPE::E type = COLOR;
  RENDER_TARGET_BEGIN_TYPE beginOp = BEGIN_DISCARD;
  RENDER_TARGET_END_TYPE endOp = END_DISCARD;
  RTV_FORMAT format = RGBA8;
  uint32_t width = 0;
  uint32_t height = 0;
  bool allowBarriersToRtv = true;
  bool allowBarriersToSrv = true;
  bool generateMipmaps = false;
  bool readOnly = false;
};
```

### 8.3 PipelineStateDesc
```
struct PipelineStateDesc {
  CULL_STATES rasterState = CULL_CLOCKWISE;
  BLEND_STATES blendState = BLEND_OPAQUE;
  DEPTH_STATES depthState = DEPTH_DEFAULT;
  std::string pipelineName;
  bool overridePSORTV = false;
  std::vector<OutputLayout> overrideRTVs;
};
```

### 8.4 SamplerDesc
```
struct SamplerDesc {
  MICROS_FILTER::E filter;
  MICROS_ADDRESS_MODE::E addressU, addressV, addressW;
  MICROS_COMPARISON_MODE::E funcMode;
  float mipLODBias, borderColor[4], minLOD, maxLOD;
  uint32_t maxAnisotropic;
};
```

### 8.5 RootParameter / RootSignature
```
struct RootParameter {
  MICROS_SHADER_TYPE::E shaderAttachment;
  MICROS_DESCRIPTOR_RANGE_TYPE::E type;   // CBV, SRV, SAMPLER, UAV
  uint32_t baseRegister;
  uint32_t numDescriptors;
  uint32_t registerSpace;
  std::string name;
};

struct RootSignature {
  std::vector<RootParameter> vRootElements;
};
```

### 8.6 ViewPort / MRect
```
struct ViewPort { float TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; };
struct MRect { uint32_t left, top, right, bottom; };
```

### 8.7 ConstantBufferDesc / Element
```
struct ConstantBufferElement { std::string name; uint32_t offset; uint32_t size; };
struct ConstantBufferDesc { uint32_t numVars; uint32_t size; std::vector<ConstantBufferElement> cbuff_elements; };
```

### 8.8 OutputLayout
```
struct OutputLayout {
  std::string name;
  MICROS_COMPONENT_TYPE::E type;   // bitmask: 0x1=float, 0x3=float2, 0x7=float3, 0xF=float4
  uint32_t index;
  uint32_t numMemberVar;
};
```

---

## 9. ENUMERATIONS REFERENCE

### Resource States (MICROS_RESOURCE_STATE_TYPE)
Direct 1:1 mapping to `D3D12_RESOURCE_STATES`:
```
STATE_COMMON=0x0, STATE_VERTEX_AND_CONSTANT_BUFFER=0x1, STATE_INDEX_BUFFER=0x2,
STATE_RENDER_TARGET=0x4, STATE_UNORDERED_ACCESS=0x8, STATE_DEPTH_WIRTE=0x10,
STATE_DEPTH_READ=0x20, STATE_NON_PIXEL_SHADER_RESOURCE=0x40,
STATE_PIXEL_SHADER_RESOURCE=0x80, STATE_COPY_DEST=0x400, STATE_COPY_SOURCE=0x800,
STATE_GENERIC_READ=(0x1|0x2|0x40|0x80|0x200|0x800), STATE_PRESENT=0x0
```

### Topology (MICROS_TOPOLOGY::E)
`POINT_LIST, LINE_LIST, LINE_STRIP, TRIANGLE_LIST, TRIANGLE_STRIP`

### Buffer Type (MICROS_BUFFER_TYPE::E)
`VERTEX, INDEX, CONSTANT`

### Command List Type (MICROS_COMMAND_LIST::E)
`DIRECT, BUNDLE, COMPUTE, COPY, VIDEO`

### Heap Types (MICROS_HEAP_TYPE::E)
`CBV_SRV_UAV=0, SAMPLER, RTV, DSV, MAX_HEAP_NUM`

### RTV Format (RTV_FORMAT)
`FD16, F32, F16, RGB8, RGBA8, RGBA16F, RGBA32F, R8, BGR8, BGRA8, BGRA32, CUBE_F32`

### Cull/Blend/Depth States
```
CULL_STATES:   CULL_NONE, CULL_CLOCKWISE, CULL_COUNTER_CLOCK_WISE, CULL_WIREFRAME
BLEND_STATES:  BLEND_OPAQUE, BLEND_ALPHA, BLEND_ADDITIVE, BLEND_NON_PREMULTIPLIED
DEPTH_STATES:  DEPTH_NONE, DEPTH_DEFAULT, DEPTH_READ, DEPTH_REVERSE_Z, DEPTH_READ_REVERSE_Z
```

### Render Pass Begin/End Types
```
RENDER_TARGET_BEGIN_TYPE: BEGIN_DISCARD, BEGIN_PRESERVE, BEGIN_CLEAR, BEGIN_NO_ACCESS,
  BEGIN_PRESERVE_LOCAL_RENDER, BEGIN_PRESERVE_LOCAL_SRV, BEGIN_PRESERVE_LOCAL_UAV
RENDER_TARGET_END_TYPE: END_DISCARD, END_PRESERVE, END_RESOLVE, END_NO_ACCESS,
  END_PRESERVE_LOCAL_RENDER, END_PRESERVE_LOCAL_SRV, END_PRESERVE_LOCAL_UAV
```

### ShaderSig Bitmask (uint64_t)
Used to generate uber-shader permutations:
```
HAS_NORMALS=0x1, HAS_TANGENTS=0x2, HAS_BINORMALS=0x4, HAS_TEXCOORDS0=0x8,
HAS_TEXCOORDS1=0x10, DIFFUSE_MAP=0x20, SPECULAR_MAP=0x40, GLOSS_MAP=0x80,
NORMAL_MAP=0x100, REFLECT_MAP=0x200, HEIGHT_MAP=0x400, SHADOW_MAP0=0x800,
USE_NO_LIGHT=0x8000, FORWARD_PASS=0x40000, GBUFF_PASS=0x80000,
SHADOW_MAP_PASS=0x100000, FSQUAD_1_TEX=0x200000, DEFERRED_PASS=0x2000000,
TEXT_PASS=0x200000000000, ... (see MicroDescriptors.h for full list)
```

---

## 10. SCENE LAYER

### 10.1 PrimitiveBase (abstract)

**File:** `include/scene/PrimitiveBase.h`

```
class PrimitiveBase {
  virtual void Load(Device*, const char*) = 0;
  virtual void Create(Device*) = 0;
  virtual void Transform(float* t) = 0;
  virtual void Draw(Device*, float* t, float* vp) = 0;
  virtual void Destroy() = 0;

  void ResolveBindings(DeviceContext*);   // for each shader × each binding object, resolve slot

  SceneProps* pScProp;
  Texture* slotTextures[10];
  Texture* slotEnvMap;
  uint64_t globalSignature;
  float brightnessScene, parallax*;
  uint32_t currentCBIndex;
  std::vector<IBindingBase*> vbindingObjects;  // resources that need slot resolution
};
```

**ResolveBindings():** Iterates all shaders in `driver->vShaderSignatures`, for each `IBindingBase*` in `vbindingObjects`, calls `driver->GetSlotBinding()` to map resource→slot per shader signature.

### 10.2 PrimitiveInst

**File:** `include/scene/PrimitiveInstance.h`

Wrapper for instanced rendering of a PrimitiveBase. Stores transform matrices (position, scale, rotation XYZ). `Update()` composites final matrix, `Draw()` calls `pBase->Draw()`.

### 10.3 PrimitiveManager

**File:** `include/scene/PrimitiveManager.h`

Owns `std::vector<PrimitiveBase*> primitives`. Factory methods: `CreateQuad()`, `CreateMesh(fname)`, `CreateTriangle()`, `CreateCube()`, `CreateSpline()`.

### 10.4 RenderMesh : PrimitiveBase

**File:** `include/scene/RenderMesh.h`

Loads `.X` model files. Contains:
- `CBuffer` struct: WVP, World, WorldView, Light0Pos/Col, CameraPos, etc.
- `SubSetInfo`: per-material data — textures, index buffer, material colors, shader signature
- `MeshInfo`: vertex/index buffers, constant buffers, vector of SubSetInfo
- `XDataBase* xFile`: loaded model data

### 10.5 RenderQuad : PrimitiveBase

**File:** `include/scene/RenderQuad.h`

Full-screen quad for post-processing. CBuffer contains WVP, inverse matrices, light arrays (128 positions+colors+radii), camera info, brightness.

### 10.6 TextRenderer

**File:** `include/scene/TextRendererMesh.h`

Text rendering using stb_truetype. Bakes font to texture, renders character quads.

### 10.7 Quad (mesh primitive)

**File:** `include/scene/QuadMesh.h`

```
struct Quad {
  struct Vertex { float x,y,z,w,s,t; };
  Buffer* m_IB;
  Buffer* m_VB[32];       // ring buffer of 32 VBs for dynamic updates
  uint32_t currentVB;
  Vertex m_vertex[4];
  unsigned int m_index[6];
  void Init(Device*, MICROS_BUFFER_USAGE::E);
  Buffer* GetCurrentVB();
  void AdvanceVB();
};
```

---

## 11. FRAME LIFECYCLE

### 11.1 Initialization Flow
```
main() → new Benchmark() → new Win32Framework(app)
  → InitGlobalVars()
  → OnCreateApplication(desc)
    → SDL_Init()
    → app->InitVars()
    → ChangeAPI(D3D12)
      → new D3DXDriver()
      → SetDimensions/SetWindow/InitDriver()
        → new D3DXDevice/Context
        → debug layer, factory, adapter, device
        → default cmd list/queue/fence
        → swap chain
        → 5 descriptor heaps
        → back buffer RTVs, depth buffer DSV
        → default sampler
        → default pipeline desc
      → app->CreateAssets()
        → create shaders, textures, meshes, RTs, pipelines
        → resolve bindings
      → driver->BuildPipelineObjects()
        → for each pipelineDesc × shader: CreatePipeline()
  → StartRendering()  // marker function
  → UpdateApplication()
    → while(appAlive): ProcessInput() + app->OnUpdate()
```

### 11.2 Per-Frame Flow
```
app->OnUpdate()
  → update timers, camera, logic
  → app->OnDraw()
    → driver->Clear()
      → TransitionBackBufferToRTV
      → SetBackBuffer (BB RTV + BB DSV)
      → SetViewport/Scissor
      → ClearRtv/ClearDsv
    → set heaps on context
    → for each render pass:
      → driver->PushRT(RTVs, DSV) or driver->BeginRenderPass(RTVs, DSV)
        → barrier RT states
        → set viewport/scissor
        → set render targets
        → clear
      → for each object:
        → context->SetShader(shader)  // sets root sig + PSO
        → context->SetConstantBuffer(slot, cb)
        → context->SetTexture(slot, tex)
        → context->SetSampler(slot, sampler)
        → context->SetVertexBuffer(0, vb)
        → context->SetIndexBuffer(ib)
        → context->SetPrimitiveTopology(TRIANGLE_LIST)
        → context->DrawIndexed(count, 0, 0)
      → driver->EndRenderPass() (if using render passes)
      → driver->PopRT()  // restore back buffer
    → driver->SwapBuffers()
      → TransitionBackBufferToPresent
      → CloseCommandList
      → ExecuteAndWait
      → Present(0, 0)
      → ResetCommandList
```

---

## 12. KEY DESIGN PATTERNS

### 12.1 Descriptor Heap as Linear Allocator
All descriptor heaps use monotonic allocation — `GetOffsetFromCurrent()` + `IncrementUsedDescriptorIndex()`. No deallocation. Descriptors persist for app lifetime.

### 12.2 Void-Pointer Descriptor Handles
Texture/Sampler/RenderTarget store descriptor addresses as `void*` pointers. D3D12 layer casts to `D3D12_CPU_DESCRIPTOR_HANDLE` / `D3D12_GPU_DESCRIPTOR_HANDLE` via `{(UINT64)ptr}`.

### 12.3 Shader Signature Bitmask
Shaders identified by `uint64_t Sig` bitmask. Controls #defines for uber-shader compilation. PSOs are indexed by `[pipelineDescIndex][shaderSig]`.

### 12.4 Name-Based Slot Resolution
Resources bound to shaders via fuzzy string matching (longest common substring ≥ 5 chars). Slot mapping cached in `IBindingBase::bindingsMap[sig] → slot`.

### 12.5 Static Buffer Upload Pattern
For static (immutable) buffers: create default heap resource + upload heap staging → temp cmd queue/list/fence → `UpdateSubresources()` → barrier → execute → fence wait → release temp objects. Each buffer creates its own temp command infrastructure.

### 12.6 Pipeline State Matrix
`BuildPipelineObjects()` creates `|pipelineDescs| × |shaders|` PSOs. Stored in `mapPipelineStateObjects[descIndex].vPipelineObjects[shaderSig]`. Runtime selects via `SetPipeline(descIndex)` then `GetPipeline(shaderSig)`.

### 12.7 Single Command List Per Frame
One default command list/queue/fence triple used for the entire frame. Opened at frame start (after `ResetCommandList()`), closed before present.

### 12.8 Render Pass API (ID3D12GraphicsCommandList4)
Uses `BeginRenderPass()`/`EndRenderPass()` from D3D12 render pass API. Supports `PRESERVE_LOCAL_*` begin/end types for tile-based rendering optimization. Command list type is `ID3D12GraphicsCommandList4`.

---

## 13. FILE MAP

```
microengine/
├── include/
│   ├── Config.h                              # Platform/API defines
│   ├── core/
│   │   ├── Core.h                            # AppBase, SceneBase, RootFramework
│   │   └── Win32Framework.h                  # Win32 + SDL framework
│   ├── video/
│   │   ├── BaseDriver.h                      # ALL abstract classes + BaseDriver
│   │   ├── MicroDescriptors.h                # ALL enums + descriptor structs
│   │   ├── ShaderBase.h                      # hyperspace::shader enums
│   │   └── d3d12/
│   │       ├── D3DXDriver.h                  # D3DXDriver : BaseDriver
│   │       ├── D3DXDevice.h                  # D3DXDevice : Device
│   │       ├── D3DXDeviceContext.h           # D3DXDeviceContext : DeviceContext
│   │       ├── D3DXDevicePipeline.h          # D3DXPipelineState : PipelineState
│   │       ├── D3DXCommandList.h             # D3DXCommandList : CommandList
│   │       ├── D3DXCommandQueue.h            # D3DXCommandQueue : CommandQueue
│   │       ├── D3DXFence.h                   # D3DXFence : Fence
│   │       ├── D3DXHeap.h                    # D3DXHeap : Heap
│   │       ├── D3DXShader.h                  # D3DXShader : Shader
│   │       ├── D3DXTexture.h                 # D3DXTexture : Texture
│   │       ├── D3DXVertexBuffer.h            # D3DXVertexBuffer : Buffer
│   │       ├── D3DXIndexBuffer.h             # D3DXIndexBuffer : Buffer
│   │       ├── D3DXConstantBuffer.h          # D3DXConstantBuffer : Buffer
│   │       ├── D3DXRenderTarget.h            # D3DXRenderTarget : RenderTarget
│   │       └── D3DXSampler.h                 # D3DXSampler : Sampler
│   ├── scene/
│   │   ├── PrimitiveBase.h                   # Abstract renderable
│   │   ├── PrimitiveInstance.h               # Transform wrapper
│   │   ├── PrimitiveManager.h                # Factory/registry
│   │   ├── RenderMesh.h                      # 3D mesh renderable
│   │   ├── RenderQuad.h                      # Post-process quad
│   │   ├── QuadMesh.h                        # Quad geometry (ring-buffered VBs)
│   │   ├── TextRendererMesh.h                # stb_truetype text
│   │   ├── SceneProp.h                       # Lights, cameras, filters
│   │   ├── SplineWireframe.h                 # Spline debug vis
│   │   └── LensFlare.h                       # Lens flare effect
│   └── utils/
│       ├── Camera.h                          # FPS camera
│       ├── Timer.h                           # Delta time
│       ├── InputManager.h                    # Keyboard/mouse state
│       ├── ResourceManager.h                 # .X file loader
│       ├── xMaths.h                          # XVECTOR3, XMATRIX44, math ops
│       ├── xDefs.h                           # Type defines
│       ├── Utils.h                           # String utils (LongestCommonSubString)
│       ├── Spline.h                          # Bezier spline
│       ├── cil.h                             # Custom image loader
│       ├── Checker.h                         # Fallback checker texture
│       └── XDataBase.h                       # .X model file parser
└── src/
    ├── core/
    │   ├── Core.cpp
    │   └── Win32Framework.cpp
    ├── video/
    │   ├── BaseDriver.cpp                    # Global pointer, factory methods, pipeline management
    │   └── d3d12/
    │       ├── D3DXDriver.cpp                # Init, Clear, SwapBuffers, BeginRenderPass
    │       ├── D3DXDevice.cpp                # Factory dispatch
    │       ├── D3DXDeviceContext.cpp          # State setting, draw, barriers, render passes
    │       ├── D3DXPipelineState.cpp          # PSO creation (raster/blend/depth/RT config)
    │       ├── D3DXShader.cpp                 # Compile, reflect, root signature creation
    │       ├── D3DXTexture.cpp                # Texture upload, SRV creation
    │       ├── D3DXRenderTarget.cpp           # RT creation, DSV/RTV/SRV views
    │       ├── D3DXVertexBuffer.cpp           # Static/dynamic VB creation
    │       ├── D3DXIndexBuffer.cpp            # Static/dynamic IB creation
    │       ├── D3DXConstantBuffer.cpp         # CB creation, update via Map
    │       ├── D3DXHeap.cpp                   # Descriptor heap management
    │       ├── D3DXFence.cpp                  # Fence creation, wait
    │       ├── D3DXSampler.cpp                # Sampler creation
    │       ├── D3DXCommandList.cpp            # Allocator + cmd list creation
    │       └── D3DXCommandQueue.cpp           # Queue creation, execute, signal
    ├── scene/
    │   ├── RenderMesh.cpp, RenderQuad.cpp, QuadMesh.cpp,
    │   ├── PrimitiveInstance.cpp, PrimitiveManager.cpp,
    │   ├── TextRendererMesh.cpp, SplineWireframe.cpp,
    │   ├── SceneProp.cpp, LensFlare.cpp
    └── utils/
        ├── Camera.cpp, Timer.cpp, InputManager.cpp,
        ├── ResourceManager.cpp, Utils.cpp, XMaths.cpp,
        ├── Spline.cpp, cil.cpp, XDataBase.cpp

microapp/
├── include/Benchmark.h                       # App class deriving AppBase
└── src/
    ├── App.cpp                               # main(), ApplicationDesc, entry point
    └── Benchmark.cpp                         # Scene setup, draw, input
```

---

## 14. IMPLEMENTING A NEW API BACKEND

To implement Vulkan (or other) backend alongside D3D12:

1. **Create new directory:** `microengine/include/video/vulkan/` and `src/video/vulkan/`
2. **Implement these classes (one per file):**
   - `VkDriver : BaseDriver`
   - `VkDevice : Device`
   - `VkDeviceContext : DeviceContext`
   - `VkVertexBuffer : Buffer`
   - `VkIndexBuffer : Buffer`
   - `VkConstantBuffer : Buffer`
   - `VkTexture : Texture`
   - `VkRenderTarget : RenderTarget`
   - `VkShader : Shader`
   - `VkCommandList : CommandList`
   - `VkCommandQueue : CommandQueue`
   - `VkFence : Fence`
   - `VkHeap : Heap`
   - `VkPipelineState : PipelineState`
   - `VkSampler : Sampler`
3. **In `Win32Framework::ChangeAPI()`:** add case for new API, `new VkDriver()` instead of `new D3DXDriver()`
4. **Critical contracts to maintain:**
   - `IObjectBase::GetAPIObject()` must return native API handle
   - `Heap` must implement linear allocation pattern
   - Texture/Sampler/RenderTarget must store descriptor addresses in `void* ownSrv*` members
   - `DeviceContext::SetShader()` must set both pipeline state AND root signature equivalent
   - Static buffer creation must handle upload→GPU copy with synchronization
   - `BuildPipelineObjects()` creates `|descs| × |shaders|` pipeline permutations
   - Resource state transitions tracked via `RenderTarget::resourceState`

---

## 15. EXTERNAL DEPENDENCIES

| Library | Purpose | Location |
|---------|---------|----------|
| D3D12 (Agility SDK v709) | Graphics API | `external/AgilitySDK/` |
| d3dx12 (helper headers) | `CD3DX12_*` helpers | `external/d3dx12/` |
| SDL2 | Windowing, input | `external/SDL2/` |
| stb_truetype | Font rendering | `external/stb/` |
| fasthash | String hashing | `external/fasthash/` |
| D3DCompiler | HLSL compilation | System |
| cil | Custom image format loader | `src/utils/cil.cpp` |

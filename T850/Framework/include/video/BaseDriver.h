/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#ifndef T800_BASEDRIVER_H
#define T800_BASEDRIVER_H

#include <utils/cil.h>
#include <Config.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <Descriptors.h>
#include <utils/Technique.h>
#include <video/WindowHandle.h>


namespace t850 {
  class Buffer;
  class VertexBuffer;
  class IndexBuffer;
  class ConstantBuffer;
  class Texture;
  class BaseRT;

  class DeviceContext {
  public:
    virtual void* GetAPIObject() const = 0;
    virtual void** GetAPIObjectReference() const = 0;

    virtual void release() = 0;
    virtual void SetPrimitiveTopology(Topology::E topology) = 0;
    virtual void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) = 0;

    ConstantBuffer* actualConstantBuffer;
    IndexBuffer* actualIndexBuffer;
    VertexBuffer* actualVertexBuffer;
    ShaderBase* actualShaderSet;
  };
  class Device {
  public:
    virtual void* GetAPIObject() const = 0;
    virtual void** GetAPIObjectReference() const = 0;

    virtual void release() = 0;
    virtual Buffer* CreateBuffer(BufferType::E bufferType, BufferDesc desc, void* initialData = nullptr) = 0;
    virtual ShaderBase* CreateShader(std::string src_vs, std::string src_fs, ShaderKey key = ShaderKey(), const std::string& vs_name = "", const std::string& fs_name = "") = 0;
    virtual Texture* CreateTexture(std::string path) = 0;
    virtual Texture* CreateTextureFromMemory(const unsigned char *buff, int w, int h, int channels, std::string name) = 0;
    virtual Texture* CreateCubeMap(const unsigned char * buff, int w, int h) = 0;
    // Create an RGBA32F texture for raw float data (e.g., bone matrices).
    // No mips, NEAREST filtering. Can be updated per-frame via Texture::UpdateFloatData.
    virtual Texture* CreateFloatTexture(int w, int h, const float* data = nullptr) = 0;
    virtual BaseRT* CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) = 0;
  };
  /* BUFFERS */
  class Buffer {
  public:
    virtual void* GetAPIObject() const = 0;
    virtual void** GetAPIObjectReference() const = 0;

    virtual void UpdateFromSystemCopy(const DeviceContext& deviceContext) = 0;
    virtual void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) = 0;
    virtual void release() = 0;
    virtual void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) = 0;

    BufferDesc descriptor;
    std::vector<char> sysMemCpy;
  protected:
  };
  class VertexBuffer : public Buffer {
  public:
    virtual void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) = 0;
  };
  class IndexBuffer : public Buffer {
  public:
    virtual void Set(const DeviceContext& deviceContext, const unsigned offset, IndexBufferFormat::E format = IndexBufferFormat::R32) = 0;
  };
  class ConstantBuffer : public Buffer {
  public:
    virtual void Set(const DeviceContext& deviceContext) = 0;
  };


  class Texture {
  public:
    Texture() :
      size(0),
      props(0),
      params(0),
      x(0),
      y(0),
      id(0),
      bounded(0),
      mipmaps(0)
    {

    }

    virtual ~Texture() {}

    bool			LoadTexture(const char *fn);
    bool			LoadFromMemory(const unsigned char *buff, int w, int h, int channels);
    bool      CreateCubeMap(const unsigned char *buff, int w, int h);
    void			release();

    virtual void	LoadAPITexture(DeviceContext* context, unsigned char* buffer) = 0;
    virtual void	LoadAPITextureCompressed(unsigned char* buffer) = 0;
    virtual void	DestroyAPITexture() = 0;

    virtual void	SetTextureParams() = 0;
    virtual void	GetFormatBpp(unsigned int &props, unsigned int &format, unsigned int &bpp) = 0;
    virtual void  Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) = 0;
    virtual void  SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) = 0;
    // Bind this texture to a vertex shader slot (for bone textures).
    // Default implementation calls Set() — backends override to use VS binding.
    virtual void  SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string name) {
      Set(deviceContext, slot, name);
    }
    // Update RGBA32F texture data in-place (for per-frame bone matrix upload).
    // Only valid for textures created via CreateFloatTexture.
    virtual void  UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data) {}


    std::string filepath;
    char			optname[128];
    unsigned int	size;
    unsigned int	props;
    unsigned int	params;
    unsigned int	cil_props;
    unsigned int	x, y;
    unsigned int	id;
    unsigned int	bounded;
    unsigned int	mipmaps;
    unsigned int	m_channels;
    std::string m_shaderTextureName;
  };

  class BaseRT {
  public:
    enum ATTACHMENTS {
      COLOR0_ATTACHMENT = 1,
      COLOR1_ATTACHMENT = 2,
      COLOR2_ATTACHMENT = 4,
      COLOR3_ATTACHMENT = 8,
      DEPTH_ATTACHMENT = 16
    };

    enum FORMAT {
      FD16 = 0,
      F32,
      F16,
      RGB8,
      RGBA8,
      RGBA16F,
      RGBA32F,
      R8,
      BGR8,
      BGRA8,
      BGRA32,
      CUBE_F32,
      NOTHING
    };

    bool			LoadRT(int nrt, int cf, int df, int w, int h, bool GenMips = false);
    // Per-attachment color formats (overrides single cf when non-empty)
    bool			LoadRT(int nrt, const std::vector<int>& perColorFormats, int df, int w, int h, bool GenMips = false);
    virtual bool	LoadAPIRT() = 0;

    void			release();
    virtual void	DestroyAPIRT() = 0;

    virtual void Set(const DeviceContext& context) = 0;
    virtual void ChangeCubeDepthTexture(int i) = 0;

    int w;
    int h;
    int number_RT;
    int color_format;
    int depth_format;
    bool GenMips;
    std::vector<int> perColorFormats;  // per-attachment formats (empty = use color_format for all)

    std::vector<Texture*>							vColorTextures;
    Texture*										pDepthTexture = nullptr;
  };
  class ShaderBase {
  public:
    ShaderBase() {}
    bool CreateShader(std::string src_vs, std::string src_fs, ShaderKey key = ShaderKey(), const std::string& vs_name = "", const std::string& fs_name = "");
    virtual bool    CreateShaderAPI(std::string src_vs, std::string src_fs, const std::string& vs_name = "", const std::string& fs_name = "") = 0;
    virtual void  Set(const t850::DeviceContext& deviceContext) = 0;
    virtual void DestroyAPIShader() = 0;
    void release();

    ShaderKey key;
  };

  class BaseDriver {
  public:
    enum {
      DEPTH_ATTACHMENT = -1,
      COLOR0_ATTACHMENT = 0,
      COLOR1_ATTACHMENT = 1,
      COLOR2_ATTACHMENT = 2,
      COLOR3_ATTACHMENT = 3,
      COLOR4_ATTACHMENT = 4,
      COLOR5_ATTACHMENT = 5,
      COLOR6_ATTACHMENT = 6,
      COLOR7_ATTACHMENT = 7,
    };
    enum BlendStates
    {
      BLEND_DEFAULT,
      BLEND_OPAQUE,
      ADDITIVE,
      ALPHA_BLEND,
      NON_PREMULTIPLIED
    };
    enum RasterizerStates
    {
      RASTER_DEFAULT,
      CULL_NONE,
      CULL_CLOCKWISE,
      CULL_COUNTERCLOCKWISE,
      WIREFRAME
    };
    enum DepthStencilStates
    {
      DEPTH_DEFAULT,
      READ_WRITE,
      NONE,
      READ
    };
	enum FaceCulling {
		FRONT_FACES,
		BACK_FACES,
		FRONT_AND_BACK
	};

    BaseDriver() : CurrentRT(-1) , m_FaceCulling(FRONT_FACES) {  }

    // Returns true if this API uses GLSL shaders (OpenGL only; Vulkan uses HLSL→SPIR-V)
    bool UsesGLSL() const { return m_currentAPI == GraphicsApi::OPENGL; }
    // Returns true if GL-style V-flip is needed (OpenGL only; Vulkan uses top-left like D3D)
    bool NeedsVFlip() const { return m_currentAPI == GraphicsApi::OPENGL; }
    virtual	void	 InitDriver() = 0;
    virtual void	 CreateSurfaces() = 0;
    virtual void	 DestroySurfaces() = 0;
    virtual void	 Update() = 0;
    virtual void	 DestroyDriver() = 0;
    virtual void	 SetWindow(void *window) = 0;
    // Editor-friendly entry point: pass either an SDL_Window* or a native
    // HWND via a tagged WindowHandle. Default impl preserves the legacy
    // SDL path so existing callers (Win32Framework -> SetWindow(SDL_Window*))
    // keep working unchanged. Backends override to honor an explicit HWND
    // from an editor host instead of GetActiveWindow().
    virtual void   SetWindowHandle(const WindowHandle& handle) {
      if (handle.kind == WindowHandle::SDL_WINDOW) {
        SetWindow(handle.sdlWindow);
      } else if (handle.kind == WindowHandle::WIN32_HWND) {
        SetWindow(handle.nativeHandle);
      } else {
        SetWindow(nullptr);
      }
    }
    virtual void	 SetDimensions(int, int) = 0;
    virtual void	 Clear() = 0;
    virtual void	 ClearWithColor(float r, float g, float b, float a) { Clear(); }
    virtual void	 SwapBuffers() = 0;
    virtual void SetBlendState(BlendStates state) = 0;
    virtual void SetDepthStencilState(DepthStencilStates state) = 0;

    virtual void SaveScreenshot(std::string path) = 0;
    virtual void SaveRTToFile(int rtID, int attachment, std::string path) {}
	virtual void SetCullFace(FaceCulling state) = 0;

    // ── D3D12/Vulkan explicit API (no-ops for D3D11/GL) ──
    virtual void BuildPipelineObjects() {}
    virtual void BeginFrame() {}
    virtual void EndFrame() {}
    virtual void WaitForGPU() {}
    virtual void FlushGPUResources() { WaitForGPU(); }  // flush GPU + release cmd buffer/descriptor references
    virtual void SetViewport(float x, float y, float w, float h) {}
    virtual void SetScissorRect(int x, int y, int w, int h) {}

    // Resize the swapchain, back-buffer RTVs, and depth buffer to the new
    // pixel dimensions. Returns true on success. Implementations must flush
    // the GPU before releasing/recreating resources.
    virtual bool ResizeSwapchain(int newW, int newH) { return false; }

    int 	 CreateTexture(std::string);
    int    CreateCubeMap(const unsigned char * buff, int w, int h);
    int	   CreateShader(std::string src_vs, std::string src_fs, ShaderKey key = ShaderKey(), const std::string& vs_name = "", const std::string& fs_name = "");
    int 	 CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false);
    int    CreateRT(int nrt, const std::vector<int>& perColorFormats, int df, int w, int h, bool genMips = false);
    void 	 ModifyRT(int RTID, int nrt, int cf, int df, int w, int h, bool genMips = false);
    int    CreateTechnique(std::string path);

    void	 PushRT(int id);
    virtual void	 PopRT() = 0;


    Texture* GetRTTexture(int id, int index);
    ShaderBase*	GetShader(ShaderKey key);
    ShaderBase*	GetShaderIdx(int id);
    Texture* GetTexture(int id);
    Technique* GetTechnique(int id);

    void DestroyShaders();
    void DestroyShader(int id);

    void DestroyRTs();
    void DestroyRT(int id);

    void DestroyTextures();
    void	DestroyTexture(int id);

    void DestroyTechniques();
    void DestroyTechnique(int id);



    std::vector<Technique*> m_techniques;
    std::vector<ShaderBase*>	m_shaders;
    std::unordered_map<uint32_t, ShaderBase*> m_shaderCache;
    std::vector<BaseRT*>		RTs;
    std::vector<Texture*>		Textures;
    int							CurrentRT;
    GraphicsApi::E m_currentAPI;
	FaceCulling	m_FaceCulling;
    int	width, height;
  };



#ifndef GETDRIVERBASE
  extern BaseDriver *g_pBaseDriver;
#define GETDRIVERBASE() g_pBaseDriver
#endif

}

#endif

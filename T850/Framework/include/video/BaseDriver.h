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
#include "T8_descriptors.h"
#include "utils/T8_Technique.h"


namespace t800 {
#define T8_NO_SIGNATURE -1
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
    virtual void SetPrimitiveTopology(T8_TOPOLOGY::E topology) = 0;
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
    virtual Buffer* CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData = nullptr) = 0;
    virtual ShaderBase* CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig = T8_NO_SIGNATURE) = 0;
    virtual Texture* CreateTexture(std::string path) = 0;
    virtual Texture* CreateTextureFromMemory(const unsigned char *buff, int w, int h, int channels, std::string name) = 0;
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
    virtual void Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format = T8_IB_FORMAR::R32) = 0;
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
    void			release();

    virtual void	LoadAPITexture(DeviceContext* context, unsigned char* buffer) = 0;
    virtual void	LoadAPITextureCompressed(unsigned char* buffer) = 0;
    virtual void	DestroyAPITexture() = 0;

    virtual void	SetTextureParams() = 0;
    virtual void	GetFormatBpp(unsigned int &props, unsigned int &format, unsigned int &bpp) = 0;
    virtual void  Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) = 0;
    virtual void  SetSampler(const DeviceContext& deviceContext) = 0;

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
      NOTHING
    };

    bool			LoadRT(int nrt, int cf, int df, int w, int h, bool GenMips = false);
    virtual bool	LoadAPIRT() = 0;

    void			release();
    virtual void	DestroyAPIRT() = 0;

    virtual void Set(const DeviceContext& context) = 0;

    int w;
    int h;
    int number_RT;
    int color_format;
    int depth_format;
    bool GenMips;

    std::vector<Texture*>							vColorTextures;
    Texture*										pDepthTexture;
  };
  class ShaderBase {
  public:
    ShaderBase() : Sig(T8_NO_SIGNATURE) {	}
    bool CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig = T8_NO_SIGNATURE);
    virtual bool    CreateShaderAPI(std::string src_vs, std::string src_fs, unsigned long long sig) = 0;
    virtual void  Set(const t800::DeviceContext& deviceContext) = 0;
    virtual void DestroyAPIShader() = 0;
    void release();

    unsigned long long	Sig;
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
    enum BLEND_STATES
    {
      BLEND_DEFAULT,
      BLEND_OPAQUE,
      ADDITIVE,
      ALPHA_BLEND,
      NON_PREMULTIPLIED
    };
    enum RASTERIZER_STATES
    {
      RASTER_DEFAULT,
      CULL_NONE,
      CULL_CLOCKWISE,
      CULL_COUNTERCLOCKWISE,
      WIREFRAME
    };
    enum DEPTH_STENCIL_STATES
    {
      DEPTH_DEFAULT,
      READ_WRITE,
      NONE,
      READ
    };
	enum FACE_CULLING {
		FRONT_FACES,
		BACK_FACES,
		FRONT_AND_BACK
	};
   
    BaseDriver() : CurrentRT(-1) , m_FaceCulling(FRONT_FACES) {  }
    virtual	void	 InitDriver() = 0;
    virtual void	 CreateSurfaces() = 0;
    virtual void	 DestroySurfaces() = 0;
    virtual void	 Update() = 0;
    virtual void	 DestroyDriver() = 0;
    virtual void	 SetWindow(void *window) = 0;
    virtual void	 SetDimensions(int, int) = 0;
    virtual void	 Clear() = 0;
    virtual void	 SwapBuffers() = 0;
    virtual void SetBlendState(BLEND_STATES state) = 0;
    virtual void SetDepthStencilState(DEPTH_STENCIL_STATES state) = 0;
	virtual void SetCullFace(FACE_CULLING state) = 0;

    int 	 CreateTexture(std::string);
    int	   CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig = T8_NO_SIGNATURE);
    int 	 CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false);
    void 	 ModifyRT(int RTID, int nrt, int cf, int df, int w, int h, bool genMips = false);
    int    CreateTechnique(std::string path);

    void	 PushRT(int id);
    virtual void	 PopRT() = 0;


    Texture* GetRTTexture(int id, int index);
    ShaderBase*	GetShaderSig(unsigned long long sig);
    ShaderBase*	GetShaderIdx(int id);
    Texture* GetTexture(int id);
    T8Technique* GetTechnique(int id);

    void DestroyShaders();
    void DestroyShader(int id);

    void DestroyRTs();
    void DestroyRT(int id);

    void DestroyTextures();
    void	DestroyTexture(int id);

    void DestroyTechniques();
    void DestroyTechnique(int id);



    std::vector<T8Technique*> m_techniques;
    std::vector<ShaderBase*>	m_signatureShaders;
    std::vector<BaseRT*>		RTs;
    std::vector<Texture*>		Textures;
    int							CurrentRT;
    GRAPHICS_API::E m_currentAPI;
	FACE_CULLING	m_FaceCulling;
    int	width, height;
  };



#ifndef GETDRIVERBASE
  extern BaseDriver *g_pBaseDriver;
#define GETDRIVERBASE() g_pBaseDriver
#endif

}

#endif

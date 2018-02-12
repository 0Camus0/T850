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

#ifndef T800_D3DXDRIVER_H
#define T800_D3DXDRIVER_H

#include <Config.h>

#include <video\BaseDriver.h>

#include <d3d11.h>
#include <dxgi.h>
#include <D3Dcompiler.h>

#include <wrl.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;


namespace t800 {
  /* DEVICES */
  class D3DXDeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;
  private:
    ID3D11DeviceContext* APIContext;
  };
  class D3DXDevice : public Device {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    Buffer* CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData = nullptr) override;
    ShaderBase* CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig = T8_NO_SIGNATURE) override;
    Texture* CreateTexture(std::string path) override;
    Texture* CreateTextureFromMemory(const unsigned char *buff, int w, int h, int channels, std::string name) override;
    Texture* CreateCubeMap(const unsigned char * buff, int w, int h) override;
    BaseRT* CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) override; // TODO RT Desc
  private:
    ID3D11Device* APIDevice;
  };

  /* BUFFERS */
  class D3DXVertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    D3DXVertexBuffer() = default;
    D3DXVertexBuffer(D3DXVertexBuffer const& other) = default;
    D3DXVertexBuffer(D3DXVertexBuffer&& other) = default;

    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ID3D11Buffer* APIBuffer;
  };
  class D3DXIndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format = T8_IB_FORMAR::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ID3D11Buffer* APIBuffer;
  };
  class D3DXConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ID3D11Buffer* APIBuffer;
  };



  class D3DXDriver : public BaseDriver {
  public:
    D3DXDriver() { m_currentAPI = GRAPHICS_API::D3D11; }
    void	InitDriver();
    void	CreateSurfaces();
    void	DestroySurfaces();
    void	Update();
    void	DestroyDriver();
    void	SetWindow(void *window);
    void	SetDimensions(int, int);
    void SetBlendState(BLEND_STATES state) override;
    void SetDepthStencilState(DEPTH_STENCIL_STATES state) override;
	  void SetCullFace(FACE_CULLING state) override;

    void SaveScreenshot(std::string path) override {};

    void	 PopRT();

    void	Clear();
    void	SwapBuffers();

    HWND	hwnd;

    D3D11_VIEWPORT viewport;
    //D3D11_VIEWPORT viewport_RT;

    /*STATES*/
    ID3D11BlendState* m_BlendStateAdditive;
    ID3D11BlendState* m_BlendStateOpaque;
    ID3D11BlendState* m_BlendStateAlphaBlend;
    ID3D11BlendState* m_BlendStateNonPremultiplied;

    ID3D11DepthStencilState* m_depthStateReadWrite;
    ID3D11DepthStencilState* m_depthStateNone;
    ID3D11DepthStencilState* m_depthStateRead;

    ID3D11RasterizerState* m_RasterStateWireframe;
    ID3D11RasterizerState* m_RasterStateCullNone;
    ID3D11RasterizerState* m_RasterStateCullClockWise;
    ID3D11RasterizerState* m_RasterStateCullCounterClockwise;
  };
}

#endif
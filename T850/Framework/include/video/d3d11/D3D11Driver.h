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

#ifndef T800_D3D11DRIVER_H
#define T800_D3D11DRIVER_H

#include <Config.h>

#include <video\BaseDriver.h>
#include <video/d3d11/D3D11DeviceContext.h>
#include <video/d3d11/D3D11Device.h>
#include <video/d3d11/D3D11VertexBuffer.h>
#include <video/d3d11/D3D11IndexBuffer.h>
#include <video/d3d11/D3D11ConstantBuffer.h>

#include <d3d11.h>
#include <dxgi.h>
#include <D3Dcompiler.h>

#include <wrl.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;


namespace t850 {
  class D3DXDriver : public BaseDriver {
  public:
    D3DXDriver() { m_currentAPI = GraphicsApi::D3D11; }
    void	InitDriver();
    void	CreateSurfaces();
    void	DestroySurfaces();
    void	Update();
    void	DestroyDriver();
    void	SetWindow(void *window);
    void  SetWindowHandle(const WindowHandle& handle) override;
    void	SetDimensions(int, int);
    void SetBlendState(BlendStates state) override;
    void SetDepthStencilState(DepthStencilStates state) override;
	  void SetCullFace(FaceCulling state) override;

    void SaveScreenshot(std::string path) override;
    void SaveRTToFile(int rtID, int attachment, std::string path) override;
    bool ReadRTColorFloat(int rtID, int attachment, float outRGBA[4]) override;

    bool ResizeSwapchain(int newW, int newH) override;
    void SetViewport(float x, float y, float w, float h) override;
    void SetScissorRect(int x, int y, int w, int h) override;
#ifdef T850_RENDER_TRACE
    void RefreshTracePendingRenderState() override;
#endif

    void	 PopRT();

    void	Clear();
    void	ClearWithColor(float r, float g, float b, float a) override;
    void  ClearBackbufferWithColor(float r, float g, float b, float a) override;
    void	SwapBuffers();
    void  CompleteFrame(FrameCompletionMode mode = FrameCompletionMode::Present) override;

    HWND	hwnd;

    D3D11_VIEWPORT viewport;
    //D3D11_VIEWPORT viewport_RT;

    /*STATES*/
    ComPtr<ID3D11BlendState> m_BlendStateAdditive;
    ComPtr<ID3D11BlendState> m_BlendStateOpaque;
    ComPtr<ID3D11BlendState> m_BlendStateAlphaBlend;
    ComPtr<ID3D11BlendState> m_BlendStateNonPremultiplied;

    ComPtr<ID3D11DepthStencilState> m_depthStateReadWrite;
    ComPtr<ID3D11DepthStencilState> m_depthStateNone;
    ComPtr<ID3D11DepthStencilState> m_depthStateRead;

    ComPtr<ID3D11RasterizerState> m_RasterStateWireframe;
    ComPtr<ID3D11RasterizerState> m_RasterStateCullNone;
    ComPtr<ID3D11RasterizerState> m_RasterStateCullClockWise;
    ComPtr<ID3D11RasterizerState> m_RasterStateCullCounterClockwise;
  };
}

#endif

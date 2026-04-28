#ifndef T800_D3D12RT_H
#define T800_D3D12RT_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <vector>

namespace t850 {

  class D3D12RT : public BaseRT {
  public:
    bool LoadAPIRT() override;
    void DestroyAPIRT() override;
    void Set(const DeviceContext& context) override;
    void SetLoad(const DeviceContext& context) override;
    void ChangeCubeDepthTexture(int i) override;

    std::vector<ComPtr<ID3D12Resource>>       vColorResources;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>  vRTVHandles;
    ComPtr<ID3D12Resource>                    depthResource;
    D3D12_CPU_DESCRIPTOR_HANDLE               depthDSV = {};
    DXGI_FORMAT                               colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool isCubeDepth = false;
    D3D12_CPU_DESCRIPTOR_HANDLE               cubeFaceDSVs[6] = {};
    // Track resource states for barriers
    std::vector<D3D12_RESOURCE_STATES>        vColorStates;
    D3D12_RESOURCE_STATES                     depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12RT_H

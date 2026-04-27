/*********************************************************
 * T850 Engine — D3D12 Ray Tracing
 *
 * D3D12RTPipeline.h: DXR state object + shader binding table
 *********************************************************/

#ifndef T800_D3D12RTPIPELINE_H
#define T800_D3D12RTPIPELINE_H

#include <Config.h>
#include <video/RTPipeline.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <cstdint>
#include <string>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  D3D12RTPipeline — DXR state object + SBT
  // ══════════════════════════════════════════════════════
  class D3D12RTPipeline : public RTPipeline {
  public:
    // Create the RT state object from HLSL source files (compiled via DXC SM 6.3 lib target).
    // raygenSrc, missSrc, closestHitSrc: paths to .hlsl files relative to Assets/Shaders/.
    bool Create(const char* raygenSrc, const char* missSrc, const char* closestHitSrc);
    void Destroy() override;

    // Shader binding table layout after Create() succeeds.
    // DispatchRays() reads these directly.
    struct SBT {
      ComPtr<ID3D12Resource> buffer;       // GPU buffer: [rayGen][miss][hitGroup]
      void*                  mapped = nullptr;
      D3D12_GPU_VIRTUAL_ADDRESS rayGenVA  = 0;
      D3D12_GPU_VIRTUAL_ADDRESS missVA    = 0;
      D3D12_GPU_VIRTUAL_ADDRESS hitGroupVA= 0;
      UINT stride = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;  // record stride
    } sbt;

    ComPtr<ID3D12StateObject>     stateObject;
    ComPtr<ID3D12RootSignature>   globalRootSig;
    bool valid = false;

  private:
    bool CompileRTLibrary(const char* hlslPath, ComPtr<ID3DBlob>& outBlob);
    bool BuildRootSignature(ID3D12Device5* device);
    bool BuildSBT(ID3D12StateObjectProperties* props);
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12RTPIPELINE_H

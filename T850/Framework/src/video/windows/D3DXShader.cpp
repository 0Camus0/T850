#include <video/windows/D3DXShader.h>


namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  bool D3DXShader::CreateShaderAPI(std::string src_vs, std::string src_fs, unsigned long long sig) {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    HRESULT hr = S_OK;
    {
      VS_blob = nullptr;
      ComPtr<ID3DBlob> errorBlob = nullptr;
      hr = D3DCompile(src_vs.c_str(), src_vs.size(), 0, 0, 0, "VS", "vs_5_0", 0, 0, &VS_blob, &errorBlob);
      if (hr != S_OK) {

        if (errorBlob) {
          printf("ErrorBlob shader VS[%s] [%s]\n\n", src_vs.c_str(), (char*)errorBlob->GetBufferPointer());
          return false;
        }

        if (!VS_blob) {
          return false;
        }
      }

      hr = device->CreateVertexShader(VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), 0, &pVS);
      if (hr != S_OK) {
        printf("Error Creating Vertex Shader\n");
        exit(666);
      }
    }

    {
      FS_blob = nullptr;
      ComPtr<ID3DBlob> errorBlob = nullptr;
      hr = D3DCompile(src_fs.c_str(), src_fs.size(), 0, 0, 0, "FS", "ps_5_0", 0, 0, &FS_blob, &errorBlob);
      if (hr != S_OK) {
        if (errorBlob) {
          printf("ErrorBlob shader FS [%s] [%s]\n\n", src_fs.c_str(), (char*)errorBlob->GetBufferPointer());
          return false;
        }

        if (!FS_blob) {
          return false;
        }
      }

      hr = device->CreatePixelShader(FS_blob->GetBufferPointer(), FS_blob->GetBufferSize(), 0, &pFS);
      if (hr != S_OK) {
        printf("Error Creating Pixel Shader\n");
        return false;
      }
    }
    ID3D11ShaderReflection* reflect;

    hr = D3DReflect(VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflect);
    D3D11_SHADER_DESC lShaderDesc;
    reflect->GetDesc(&lShaderDesc);
    int offset = 0;
    for (unsigned i = 0; i < lShaderDesc.InputParameters; i++)
    {
      D3D11_SIGNATURE_PARAMETER_DESC desc;
      reflect->GetInputParameterDesc(i, &desc);

      D3D11_INPUT_ELEMENT_DESC ie;
      ie.SemanticName = desc.SemanticName;
      ie.SemanticIndex = desc.SemanticIndex;
      ie.InputSlot = 0;
      ie.AlignedByteOffset = offset ;
      ie.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
      ie.InstanceDataStepRate = 0;
      if (desc.Mask == 1)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32_FLOAT;
        offset += 4;
      }
      else if (desc.Mask <= 3)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32G32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32G32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32G32_FLOAT;
        offset += 8;
      }
      else if (desc.Mask <= 7)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32G32B32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32G32B32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        offset += 12;
      }
      else if (desc.Mask <= 15)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32G32B32A32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        offset += 16;
      }

      VertexDecl.push_back(ie);
    }
    reflect->Release();

    hr = device->CreateInputLayout(&VertexDecl[0], VertexDecl.size(), VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), &Layout);
    if (hr != S_OK) {
      printf("Error Creating Input Layout\n");
      return false;
    }

    return true;
  }

  void D3DXShader::Set(const DeviceContext & deviceContext)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->VSSetShader(pVS.Get(), 0, 0);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetShader(pFS.Get(), 0, 0);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetInputLayout(Layout.Get());
  }
  void D3DXShader::DestroyAPIShader()
  {
    pVS.Reset();
    pFS.Reset();
    VS_blob.Reset();
    FS_blob.Reset();
    Layout.Reset();
  }
}
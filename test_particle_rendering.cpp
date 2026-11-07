#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static void Check(HRESULT result) {
  if (FAILED(result)) {
    throw std::runtime_error("Direct3D operation failed");
  }
}

int main(int argc, char **argv) {
  try {
    if (argc != 3) return 1;
    const std::filesystem::path shaders = argv[1];
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                           D3D11_SDK_VERSION, &device, nullptr, &context));
    auto compile = [&](const wchar_t *name, const char *profile) {
      ComPtr<ID3DBlob> code, errors;
      const HRESULT hr = D3DCompileFromFile((shaders / name).c_str(), nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", profile, 0, 0, &code, &errors);
      if (FAILED(hr) && errors) {
        std::cerr << static_cast<const char *>(errors->GetBufferPointer());
      }
      Check(hr);
      return code;
    };
    auto particleCode = compile(L"ParticleVS.hlsl", "vs_5_0");
    auto trailCode = compile(L"TrailVS.hlsl", "vs_5_0");
    auto pixelCode = compile(L"ParticlePS.hlsl", "ps_5_0");
    ComPtr<ID3D11VertexShader> particleVS, trailVS;
    ComPtr<ID3D11PixelShader> ps;
    Check(device->CreateVertexShader(particleCode->GetBufferPointer(), particleCode->GetBufferSize(), nullptr, &particleVS));
    Check(device->CreateVertexShader(trailCode->GetBufferPointer(), trailCode->GetBufferSize(), nullptr, &trailVS));
    Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &ps));
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ComPtr<ID3D11InputLayout> inputLayout;
    Check(device->CreateInputLayout(layout, 4, particleCode->GetBufferPointer(), particleCode->GetBufferSize(), &inputLayout));
    struct Vertex { XMFLOAT3 p, n; XMFLOAT2 uv; XMFLOAT4 color; };
    const Vertex vertices[] = {
        {{-.5f,.5f,0},{0,1,0},{0,0},{1,1,1,1}},
        {{.5f,.5f,0},{0,1,0},{1,0},{1,1,1,1}},
        {{.5f,-.5f,0},{0,1,0},{1,1},{1,1,1,1}},
        {{-.5f,.5f,0},{0,1,0},{0,0},{1,1,1,1}},
        {{.5f,-.5f,0},{0,1,0},{1,1},{1,1,1,1}},
        {{-.5f,-.5f,0},{0,1,0},{0,1},{1,1,1,1}}};
    auto buffer = [&](UINT bytes, UINT bind, const void *data,
                      UINT misc = 0, UINT stride = 0) {
      D3D11_BUFFER_DESC desc{};
      desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = bind; desc.MiscFlags = misc; desc.StructureByteStride = stride;
      D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = data;
      ComPtr<ID3D11Buffer> result;
      Check(device->CreateBuffer(&desc, &initial, &result));
      return result;
    };
    auto vb = buffer(sizeof(vertices), D3D11_BIND_VERTEX_BUFFER, vertices);
    struct Constants { XMMATRIX world, view, projection; XMFLOAT4 color, flags, light; };
    Constants constants{XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity(), {}, {}, {}};
    auto cb = buffer(sizeof(constants), D3D11_BIND_CONSTANT_BUFFER, &constants);
    struct Instance { XMFLOAT4X4 world; XMFLOAT4 color, flags; XMFLOAT4X4 previous; };
    Instance instance{};
    auto instances = buffer(sizeof(instance), D3D11_BIND_SHADER_RESOURCE, &instance,
                             D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(instance));
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; srvDesc.Buffer.NumElements = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    Check(device->CreateShaderResourceView(instances.Get(), &srvDesc, &srv));
    constexpr UINT cell = 128, width = cell * 5, height = cell * 2;
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width; textureDesc.Height = height;
    textureDesc.MipLevels = 1; textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1; textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture, staging;
    Check(device->CreateTexture2D(&textureDesc, nullptr, &texture));
    ComPtr<ID3D11RenderTargetView> target;
    Check(device->CreateRenderTargetView(texture.Get(), nullptr, &target));
    textureDesc.BindFlags = 0; textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&textureDesc, nullptr, &staging));
    D3D11_BLEND_DESC blendDesc{};
    auto &blend = blendDesc.RenderTarget[0];
    blend.BlendEnable = TRUE; blend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; blend.BlendOp = D3D11_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D11_BLEND_ONE; blend.DestBlendAlpha = D3D11_BLEND_ZERO;
    blend.BlendOpAlpha = D3D11_BLEND_OP_ADD; blend.RenderTargetWriteMask = 15;
    ComPtr<ID3D11BlendState> blendState;
    Check(device->CreateBlendState(&blendDesc, &blendState));
    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID; rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    Check(device->CreateRasterizerState(&rasterDesc, &raster));
    const float clear[] = {0,0,0,0};
    context->ClearRenderTargetView(target.Get(), clear);
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    context->OMSetBlendState(blendState.Get(), nullptr, 0xffffffff);
    context->RSSetState(raster.Get());
    context->IASetInputLayout(inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0,1,vb.GetAddressOf(),&stride,&offset);
    context->VSSetConstantBuffers(0,1,cb.GetAddressOf());
    context->PSSetConstantBuffers(0,1,cb.GetAddressOf());
    context->VSSetShaderResources(15,1,srv.GetAddressOf());
    context->PSSetShader(ps.Get(),nullptr,0);
    for (int mode = 0; mode < 10; ++mode) {
      D3D11_VIEWPORT viewport{float(mode % 5 * cell),float(mode / 5 * cell),float(cell),float(cell),0,1};
      context->RSSetViewports(1,&viewport);
      XMMATRIX world = XMMatrixScaling(2,2,2) * XMMatrixTranslation(0,0,0.5f);
      if (mode == 8) {
        world = XMMatrixScaling(0.65f,1,1.9f) * XMMatrixRotationX(XM_PIDIV2) * XMMatrixTranslation(0,0,0.5f);
      }
      XMStoreFloat4x4(&instance.world,XMMatrixTranspose(world));
      instance.color = {0.7f,0.9f,1,1};
      if (mode == 1) instance.color = {1,0.75f,0.4f,0.8f};
      if (mode == 6) instance.color = {0.3f,0.9f,0.2f,1};
      instance.flags = {1,0,float(mode),0};
      if (mode == 9) instance.flags = {1,0,0,1};
      context->UpdateSubresource(instances.Get(),0,nullptr,&instance,0,0);
      context->VSSetShader(particleVS.Get(),nullptr,0);
      if (mode == 8) context->VSSetShader(trailVS.Get(),nullptr,0);
      context->DrawInstanced(6,1,0,0);
    }
    context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
    std::vector<unsigned char> pixels(width * height * 4);
    for (UINT y=0;y<height;++y) {
      const auto *row = static_cast<const unsigned char *>(mapped.pData)+y*mapped.RowPitch;
      std::copy(row,row+width*4,pixels.begin()+y*width*4);
    }
    context->Unmap(staging.Get(),0);
    for (int mode=0;mode<10;++mode) {
      int lit=0;
      for (UINT y=0;y<cell;++y) for (UINT x=0;x<cell;++x) {
        const size_t index=((mode/5*cell+y)*width+mode%5*cell+x)*4;
        const int brightness=pixels[index]+pixels[index+1]+pixels[index+2];
        if (brightness>12) ++lit;
        if ((x==0 || x==cell-1 || y==0 || y==cell-1) && brightness>3)
          throw std::runtime_error("Particle has visible rectangular edges");
      }
      if (lit<20) throw std::runtime_error("Particle or trail was not rendered");
      std::cout << "mode " << mode << ": " << lit << " visible pixels\n";
    }
    for (size_t i=0;i<pixels.size();i+=4) std::swap(pixels[i],pixels[i+2]);
    BITMAPFILEHEADER file{}; file.bfType=0x4d42;
    file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=width;
    info.biHeight=-static_cast<LONG>(height); info.biPlanes=1; info.biBitCount=32;
    std::ofstream output(argv[2],std::ios::binary);
    output.write(reinterpret_cast<const char *>(&file),sizeof(file));
    output.write(reinterpret_cast<const char *>(&info),sizeof(info));
    output.write(reinterpret_cast<const char *>(pixels.data()),pixels.size());
    if (!output) throw std::runtime_error("Could not write particle preview");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

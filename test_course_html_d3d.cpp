#include "graphics/WikiTextureGenerator.h"
#include "graphics/html/CourseHtml.h"
#include <iostream>
#include <stdexcept>

void Require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
int main() {
    try {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        graphics::WikiTextureGenerator generator;
        Require(generator.Initialize(device.Get()),"generator init");
        std::string html="<p>Plain 日本 <a href=\"/wiki/日本\">日本</a></p>";
        for(int i=0;i<20;++i) html+="<p>Article paragraph for tile coverage.</p>";
        auto article=graphics::html::PrepareArticle("Course",html);
        for(int cycle=0;cycle<3;++cycle) {
            auto result=generator.GenerateTexture(L"Course",L"fallback",{},"日本",2048,512,{},article.html);
            Require(result.layoutWidth>0,"HTML path used");
            Require(result.links.size()==1 && result.links[0].isTarget,"anchor region");
            Require(result.tiles.size()>1,"multiple tiles");
            std::uint64_t pixels=0; float offset=0; bool ink=false;
            for(const auto& tile:result.tiles) {
                Require(tile.texture && tile.srv,"tile GPU resources");
                Require(tile.offsetY==offset,"continuous tile coordinates");
                offset+=tile.height; pixels+=std::uint64_t(tile.width)*tile.height;
                D3D11_TEXTURE2D_DESC desc{}; tile.texture->GetDesc(&desc);
                desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> copy;
                Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&copy)),"readback texture");
                context->CopyResource(copy.Get(),tile.texture.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                Require(SUCCEEDED(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback");
                for(UINT y=0;y<desc.Height;++y) {
                    auto* row=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch;
                    for(UINT x=0;x<desc.Width;++x) if(row[x*4]<200 || row[x*4+1]<200 || row[x*4+2]<200) ink=true;
                }
                context->Unmap(copy.Get(),0);
            }
            Require(ink,"rendered nonwhite content");
            Require(offset==result.height && pixels<=graphics::html::kMaxTexturePixels,"texture budget");
        }
        generator.Shutdown();
        std::cout<<"course_html_d3d: all checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

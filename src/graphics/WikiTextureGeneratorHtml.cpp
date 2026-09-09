#include "WikiTextureGenerator.h"
#include "../core/StringUtils.h"
#include "../core/Logger.h"
#ifdef WIKIGOLF_HTML_COURSES
#include "html/CourseHtmlContainer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <future>
#include <atomic>
#include <chrono>
#include <stdexcept>

namespace graphics {
namespace {
D2D1_COLOR_F Color(litehtml::web_color c) { return D2D1::ColorF(c.red/255.f, c.green/255.f, c.blue/255.f, c.alpha/255.f); }
D2D1_RECT_F Rect(const litehtml::position& p) {
    return D2D1::RectF(p.x.value(), p.y.value(), (p.x+p.width).value(), (p.y+p.height).value());
}
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HTML Direct2D/DirectWrite operation failed"); }
}
struct WikiHtmlRenderState final : html::CourseHtmlContainer {
    struct Font { ComPtr<IDWriteTextFormat> format; bool underline = false; };
    ComPtr<IDWriteFactory> write;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::map<litehtml::uint_ptr, Font> fonts;
    std::map<std::string, ComPtr<ID2D1Bitmap>> images;
    std::map<std::string, litehtml::size> imageSizes;
    std::future<bool> layoutFuture;
    std::atomic_bool cancelled{false};
    litehtml::uint_ptr nextFont = 1;
    html::RasterSize raster;
    unsigned clipDepth = 0;
    void ResetClips() { while (clipDepth) { context->PopAxisAlignedClip(); --clipDepth; } }
    ~WikiHtmlRenderState() override {
        cancelled.store(true);
        if (layoutFuture.valid()) layoutFuture.wait();
        ClearDocument();
    }
    ComPtr<IDWriteTextLayout> Text(const char* text, litehtml::uint_ptr font) {
        if (cancelled.load()) throw std::runtime_error("HTML layout cancelled");
        const auto value = core::ToWString(text);
        ComPtr<IDWriteTextLayout> layout;
        Check(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()),
            fonts.at(font).format.Get(), 1000000, 1000000, &layout));
        if (fonts.at(font).underline) layout->SetUnderline(TRUE, {0, static_cast<UINT32>(value.size())});
        return layout;
    }
    litehtml::uint_ptr create_font(const litehtml::font_description& d, const litehtml::document*,
                                   litehtml::font_metrics* metrics) override {
        Font font;
        Check(write->CreateTextFormat(L"Meiryo", nullptr, static_cast<DWRITE_FONT_WEIGHT>(std::clamp(d.weight, 100, 900)),
            d.style == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, d.size.value(), L"ja-JP", &font.format));
        font.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        font.underline = (d.decoration_line & litehtml::text_decoration_line_underline) != 0;
        const auto id = nextFont++;
        fonts.emplace(id, std::move(font));
        auto layout = Text("Mg0", id);
        DWRITE_LINE_METRICS line{}; UINT32 count = 0;
        Check(layout->GetLineMetrics(&line, 1, &count));
        *metrics = {};
        metrics->font_size = d.size;
        metrics->height = line.height;
        metrics->ascent = line.baseline;
        metrics->descent = line.height-line.baseline;
        metrics->x_height = d.size.value()*0.5f;
        metrics->ch_width = text_width("0", id);
        metrics->draw_spaces = true;
        return id;
    }
    void delete_font(litehtml::uint_ptr font) override { fonts.erase(font); }
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr font) override {
        auto layout = Text(text, font);
        DWRITE_TEXT_METRICS metrics{}; Check(layout->GetMetrics(&metrics));
        return metrics.widthIncludingTrailingWhitespace;
    }
    void draw_text(litehtml::uint_ptr, const char* text, litehtml::uint_ptr font,
                   litehtml::web_color color, const litehtml::position& pos) override {
        brush->SetColor(Color(color));
        auto layout = Text(text, font);
        context->DrawTextLayout(D2D1::Point2F(pos.x.value(), pos.y.value()), layout.Get(), brush.Get());
    }
    void get_image_size(const char* src, const char*, litehtml::size& size) override {
        const auto it = imageSizes.find(src);
        size = it == imageSizes.end() ? litehtml::size{320,180} : it->second;
    }
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer& layer,
                    const std::string& url, const std::string&) override {
        context->PushAxisAlignedClip(Rect(layer.clip_box), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        auto it = images.find(url);
        if (it != images.end()) context->DrawBitmap(it->second.Get(), Rect(layer.origin_box));
        else { brush->SetColor(D2D1::ColorF(0.9f,0.9f,0.9f)); context->FillRectangle(Rect(layer.origin_box), brush.Get()); }
        context->PopAxisAlignedClip();
    }
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& layer,
                         const litehtml::web_color& color) override {
        brush->SetColor(Color(color));
        context->PushAxisAlignedClip(Rect(layer.clip_box), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        context->FillRectangle(Rect(layer.border_box), brush.Get());
        context->PopAxisAlignedClip();
    }
    void draw_borders(litehtml::uint_ptr, const litehtml::borders& b,
                      const litehtml::position& p, bool) override {
        const auto r = Rect(p);
        auto edge = [&](const litehtml::border& e, D2D1_RECT_F bounds) {
            if (e.width.value() <= 0 || e.style == litehtml::border_style_none || e.style == litehtml::border_style_hidden) return;
            brush->SetColor(Color(e.color)); context->FillRectangle(bounds, brush.Get());
        };
        edge(b.top, D2D1::RectF(r.left,r.top,r.right,r.top+b.top.width.value()));
        edge(b.bottom,D2D1::RectF(r.left,r.bottom-b.bottom.width.value(),r.right,r.bottom));
        edge(b.left,D2D1::RectF(r.left,r.top,r.left+b.left.width.value(),r.bottom));
        edge(b.right,D2D1::RectF(r.right-b.right.width.value(),r.top,r.right,r.bottom));
    }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker& m) override {
        brush->SetColor(Color(m.color));
        const auto r = Rect(m.pos);
        if (m.marker_type == litehtml::list_style_type_none) return;
        if (m.marker_type == litehtml::list_style_type_decimal) {
            draw_text(0, (std::to_string(m.index)+".").c_str(), m.font, m.color, m.pos); return;
        }
        context->FillEllipse(D2D1::Ellipse(D2D1::Point2F((r.left+r.right)/2,(r.top+r.bottom)/2),3,3),brush.Get());
    }
    void set_clip(const litehtml::position& p, const litehtml::border_radiuses&) override {
        context->PushAxisAlignedClip(Rect(p), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++clipDepth;
    }
    void del_clip() override { if (clipDepth) { context->PopAxisAlignedClip(); --clipDepth; } }
};

bool WikiTextureGenerator::BeginHtmlTexture(WikiTextureGenerationState& state,
    const std::string& articleHtml, const std::string& target,
    const std::vector<PendingWikiImage>& images) {
    try {
        std::uint64_t imageHash = 14695981039346656037ull;
        auto hashByte = [&](unsigned char byte) { imageHash = (imageHash ^ byte) * 1099511628211ull; };
        for (const auto& image : images) {
            for (unsigned char c : image.sourceUrl) hashByte(c);
            hashByte(0);
            for (unsigned shift = 0; shift < 32; shift += 8) {
                hashByte(static_cast<unsigned char>(image.pixelWidth >> shift));
                hashByte(static_cast<unsigned char>(image.pixelHeight >> shift));
            }
            for (auto c : image.pixelsBGRA) hashByte(c);
        }
        const std::string cacheKey = articleHtml + "\n" + target + "\n" + std::to_string(imageHash);
        for (const auto& cached : m_htmlCache) {
            if (cached.key != cacheKey) continue;
            state = WikiTextureGenerationState();
            state.result = cached.result;
            state.actualWidth = state.result.width;
            state.totalHeight = state.currentOffsetY = state.result.height;
            state.started = state.completed = true;
            LOG_INFO("WikiHtml", "Texture cache hit: {}x{}", state.result.width, state.result.height);
            return true;
        }
        auto render = std::make_shared<WikiHtmlRenderState>();
        render->write = m_dwriteFactory; render->context = m_d2dContext;
        Check(m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0,0,0), &render->brush));
        for (const auto& image : images) {
            if (image.sourceUrl.empty() || image.pixelsBGRA.empty()) continue;
            auto bitmap = CreateBitmapFromPixels(image.pixelsBGRA.data(), image.pixelWidth, image.pixelHeight);
            if (bitmap) {
                render->imageSizes.emplace(image.sourceUrl, litehtml::size{
                    static_cast<int>(image.pixelWidth),static_cast<int>(image.pixelHeight)});
                render->images.emplace(image.sourceUrl, std::move(bitmap));
            }
        }
        state = WikiTextureGenerationState();
        state.targetPage = target;
        auto* worker = render.get();
        render->layoutFuture = std::async(std::launch::async, [worker, articleHtml, target] {
            return worker->Layout(articleHtml, target);
        });
        state.htmlCacheKey=cacheKey;
        state.htmlState=std::move(render); state.started=true;
        return true;
    } catch (const std::exception& e) { LOG_WARN("WikiHtml", "Layout failed: {}", e.what()); return false; }
}

bool WikiTextureGenerator::GenerateHtmlTile(WikiTextureGenerationState& state) {
    auto& render = *state.htmlState;
    try {
        if (render.layoutFuture.valid()) {
            if (render.layoutFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return false;
            if (!render.layoutFuture.get()) throw std::runtime_error("HTML layout limit");
            render.raster = html::FitRaster(render.Width(), render.Height());
            const auto raster = render.raster;
            if (!raster.width) throw std::runtime_error("HTML raster limit");
            state.actualWidth = state.result.width = raster.width;
            state.totalHeight = state.remainingHeight = state.result.height = raster.height;
            state.result.layoutWidth = render.Width(); state.result.layoutHeight = render.Height();
            auto convert = [&](html::Rect r) {
                return html::Rect{r.x*raster.scaleX,r.y*raster.scaleY,r.width*raster.scaleX,r.height*raster.scaleY};
            };
            for (const auto& link : render.Links()) {
                const auto r = convert(link.representative);
                LinkRegion region;
                region.x=r.x; region.y=r.y; region.width=r.width; region.height=r.height;
                region.targetPage=link.target; region.isTarget=(link.target==state.targetPage); region.elementId=link.elementId;
                for (const auto& fragment : link.fragments) {
                    const auto f = convert(fragment);
                    region.fragments.push_back({f.x,f.y,f.width,f.height});
                }
                state.result.links.push_back(std::move(region));
            }
            for (const auto& region : render.Regions()) {
                const auto r = convert(region.rect);
                if (region.kind == html::RegionKind::Heading)
                    state.result.headings.push_back({r.x,r.y,r.width,r.height,region.level});
                else if (region.kind == html::RegionKind::Image)
                    state.result.images.push_back({r.x,r.y,r.width,r.height});
                else state.result.tables.push_back({r.x,r.y,r.width,r.height});
            }
            LOG_INFO("WikiHtml", "Layout ready: {}x{} raster={}x{} links={}",
                state.result.layoutWidth,state.result.layoutHeight,state.result.width,state.result.height,state.result.links.size());
            return false;
        }
        const auto height = std::min(512u, state.remainingHeight);
        D3D11_TEXTURE2D_DESC renderDesc{};
        renderDesc.Width=state.actualWidth; renderDesc.Height=height;
        renderDesc.MipLevels=renderDesc.ArraySize=1;
        renderDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        renderDesc.SampleDesc.Count=1;
        renderDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> renderTexture;
        Check(m_d3dDevice->CreateTexture2D(&renderDesc,nullptr,&renderTexture));
        WikiTextureResult::Tile tile{};
        ComPtr<IDXGISurface> surface; Check(renderTexture.As(&surface));
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(renderDesc.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        ComPtr<ID2D1Bitmap1> bitmap;
        Check(m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(),&props,&bitmap));
        m_d2dContext->SetTarget(bitmap.Get());
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(render.raster.scaleX,render.raster.scaleY)*
            D2D1::Matrix3x2F::Translation(0,-static_cast<float>(state.currentOffsetY)));
        m_d2dContext->BeginDraw(); m_d2dContext->Clear(D2D1::ColorF(1,1,1));
        try { render.Draw(state.currentOffsetY/render.raster.scaleY, height/render.raster.scaleY); }
        catch (...) { render.ResetClips(); m_d2dContext->EndDraw(); throw; }
        Check(m_d2dContext->EndDraw());
        m_d2dContext->SetTarget(nullptr);
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        D3D11_TEXTURE2D_DESC textureDesc=renderDesc;
        textureDesc.MipLevels=0;
        textureDesc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        textureDesc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        Check(m_d3dDevice->CreateTexture2D(&textureDesc,nullptr,&tile.texture));
        ComPtr<ID3D11DeviceContext> d3dContext;
        m_d3dDevice->GetImmediateContext(&d3dContext);
        d3dContext->CopySubresourceRegion(
            tile.texture.Get(),0,0,0,0,renderTexture.Get(),0,nullptr);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format=textureDesc.Format;
        srvDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip=0;
        srvDesc.Texture2D.MipLevels=UINT(-1);
        Check(m_d3dDevice->CreateShaderResourceView(
            tile.texture.Get(),&srvDesc,&tile.srv));
        d3dContext->GenerateMips(tile.srv.Get());
        tile.width=state.actualWidth; tile.height=height; tile.offsetY=static_cast<float>(state.currentOffsetY);
        state.result.tiles.push_back(std::move(tile));
        state.currentOffsetY+=height; state.remainingHeight-=height;
        if (state.remainingHeight) return false;
        state.result.texture=state.result.tiles.front().texture;
        state.result.srv=state.result.tiles.front().srv;
        std::uint64_t cachedPixels = std::uint64_t(state.result.width)*state.result.height;
        for (const auto& entry : m_htmlCache) cachedPixels += std::uint64_t(entry.result.width)*entry.result.height;
        while (!m_htmlCache.empty() && (cachedPixels > html::kMaxTexturePixels || m_htmlCache.size() >= 2)) {
            cachedPixels -= std::uint64_t(m_htmlCache.front().result.width)*m_htmlCache.front().result.height;
            m_htmlCache.erase(m_htmlCache.begin());
        }
        m_htmlCache.push_back({state.htmlCacheKey, state.result});
        state.completed=true; state.htmlState.reset();
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("WikiHtml", "Tile failed: {}", e.what());
        m_d2dContext->SetTarget(nullptr);
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        state.failed=true; state.completed=true; state.result={}; state.htmlState.reset();
        return true;
    }
}
}
#else
namespace graphics {
bool WikiTextureGenerator::BeginHtmlTexture(WikiTextureGenerationState&, const std::string&,
    const std::string&, const std::vector<PendingWikiImage>&) { return false; }
bool WikiTextureGenerator::GenerateHtmlTile(WikiTextureGenerationState&) { return true; }
}
#endif

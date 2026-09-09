#pragma once
#include "CourseHtml.h"
#include <litehtml.h>

namespace graphics::html {
/** @brief 固定CSSの記事レイアウト。描画・文字計測は利用側が実装します。 */
class CourseHtmlContainer : public litehtml::document_container {
public:
    ~CourseHtmlContainer() override = default;
    bool Layout(const std::string& preparedHtml, const std::string& target);
    void Draw(float top, float height);
    float Width() const { return m_width; }
    float Height() const { return m_height; }
    const std::vector<Link>& Links() const { return m_links; }
    const std::vector<Region>& Regions() const { return m_regions; }
    void ClearDocument();

    litehtml::pixel_t pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
    litehtml::pixel_t get_default_font_size() const override { return 18; }
    const char* get_default_font_name() const override { return "Meiryo"; }
    void load_image(const char*, const char*, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const litehtml::document::ptr&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(std::string&, litehtml::text_transform) override {}
    void import_css(std::string& text, const std::string&, std::string&) override { text.clear(); }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                          const litehtml::document::ptr&) override { return {}; }
    void get_viewport(litehtml::position& p) const override { p = {0, 0, kLayoutWidth, 1024}; }
    void get_media_features(litehtml::media_features& media) const override;
    void get_language(std::string& language, std::string& culture) const override { language="ja"; culture="JP"; }
    void split_text(const char*, const std::function<void(const char*)>&,
                    const std::function<void(const char*)>&) override;
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
        const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
        const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
        const litehtml::background_layer::conic_gradient&) override {}
private:
    void Collect(const litehtml::element::ptr& el);
    litehtml::document::ptr m_document;
    float m_width = 0, m_height = 0;
    std::vector<Link> m_links;
    std::vector<Region> m_regions;
};
}

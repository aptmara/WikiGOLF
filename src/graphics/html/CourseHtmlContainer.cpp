#include "CourseHtmlContainer.h"
#include <litehtml/render_item.h>
#include <litehtml/utf8_strings.h>
#include <algorithm>
#include <cmath>

namespace graphics::html {
namespace {
Rect ToRect(const litehtml::position& p) {
    return {p.x.value(), p.y.value(), p.width.value(), p.height.value()};
}
bool Visible(const litehtml::element::ptr& el) {
    for (auto p = el; p; p = p->parent())
        if (p->css().get_display() == litehtml::display_none ||
            p->css().get_visibility() != litehtml::visibility_visible) return false;
    return true;
}
void LeafRects(const litehtml::element::ptr& el, std::vector<Rect>& out) {
    if (!Visible(el)) return;
    if (el->is_text() || std::string(el->get_tagName()) == "img") {
        el->run_on_renderers([&](const auto& ri) {
            const auto r = ToRect(ri->get_placement());
            if (r.width > 0 && r.height > 0) out.push_back(r);
            return true;
        });
    } else for (const auto& child : el->children()) LeafRects(child, out);
}
}
void CourseHtmlContainer::ClearDocument() {
    m_document.reset();
    m_links.clear(); m_regions.clear(); m_width = m_height = 0;
}
bool CourseHtmlContainer::Layout(const std::string& html, const std::string& target) {
    ClearDocument();
    if (html.empty()) return false;
    m_document = litehtml::document::createFromString(
        litehtml::estring(html, litehtml::encoding::utf_8), this, litehtml::master_css, CourseCss());
    if (!m_document || !m_document->root()) return false;
    for (const auto& a : m_document->root()->select_all("a[data-course-target]")) {
        if (target == a->get_attr("data-course-target", "")) a->set_attr("data-course-goal", "1");
    }
    m_document->root()->refresh_styles();
    m_document->root()->compute_styles();
    m_document->render(kLayoutWidth);
    m_width = std::max(float(kLayoutWidth), m_document->width().value());
    m_height = m_document->height().value();
    if (!FitRaster(m_width, m_height).width) { ClearDocument(); return false; }
    Collect(m_document->root());
    return true;
}
void CourseHtmlContainer::Collect(const litehtml::element::ptr& el) {
    if (!Visible(el)) return;
    const std::string tag = el->get_tagName();
    const auto bounds = ToRect(el->get_placement());
    if (tag == "a" && el->get_attr("data-course-target")) {
        Link link;
        link.elementId = static_cast<std::uint32_t>(m_links.size() + 1);
        link.target = el->get_attr("data-course-target");
        std::vector<Rect> leaves;
        LeafRects(el, leaves);
        std::stable_sort(leaves.begin(), leaves.end(), [](const Rect& a, const Rect& b) {
            return a.y == b.y ? a.x < b.x : a.y < b.y;
        });
        for (const auto& r : leaves) {
            if (!link.fragments.empty()) {
                auto& back = link.fragments.back();
                if (std::abs(back.y-r.y) < 0.5f && std::abs(back.height-r.height) < 0.5f &&
                    r.x <= back.x + back.width + 1.0f) {
                    back.width = std::max(back.x + back.width, r.x + r.width) - back.x;
                    continue;
                }
            }
            link.fragments.push_back(r);
        }
        if (!link.fragments.empty()) {
            link.representative = *std::max_element(link.fragments.begin(), link.fragments.end(),
                [](const Rect& a, const Rect& b) { return a.width*a.height < b.width*b.height; });
            m_links.push_back(std::move(link));
        }
    }
    if (bounds.width > 0 && bounds.height > 0) {
        if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6')
            m_regions.push_back({bounds, RegionKind::Heading, tag[1]-'0'});
        else if (tag == "img") m_regions.push_back({bounds, RegionKind::Image});
        else if (tag == "table") m_regions.push_back({bounds, RegionKind::Table});
    }
    for (const auto& child : el->children()) Collect(child);
}
void CourseHtmlContainer::Draw(float top, float height) {
    if (!m_document) return;
    litehtml::position clip(0, top, m_width, height);
    m_document->draw(0, 0, 0, &clip);
}
void CourseHtmlContainer::get_media_features(litehtml::media_features& m) const {
    m.type = litehtml::media_type_screen;
    m.width = m.device_width = kLayoutWidth;
    m.height = m.device_height = 1024;
    m.color = 8; m.resolution = 96;
}
void CourseHtmlContainer::split_text(const char* text,
    const std::function<void(const char*)>& word, const std::function<void(const char*)>& space) {
    std::u32string pending;
    auto flush = [&] { if (!pending.empty()) { word(litehtml::utf32_to_utf8(pending)); pending.clear(); } };
    const std::u32string input = static_cast<const char32_t*>(litehtml::utf8_to_utf32(text));
    for (char32_t c : input) {
        if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f') {
            flush(); space(litehtml::utf32_to_utf8(std::u32string(1, c)));
        } else if ((c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
                   (c >= 0xff00 && c <= 0xffef) || (c >= 0x20000 && c <= 0x3134f)) {
            flush(); word(litehtml::utf32_to_utf8(std::u32string(1, c)));
        } else pending += c;
    }
    flush();
}
}

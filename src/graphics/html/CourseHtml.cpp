#include "CourseHtml.h"
#include <gumbo.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace graphics::html {
namespace {
std::string Escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}
std::string Attr(const GumboElement& el, const char* name) {
    const auto* a = gumbo_get_attribute(&el.attributes, name);
    return a ? a->value : "";
}
bool HasClass(const std::string& classes, const std::string& token) {
    std::string padded = " " + classes + " ";
    for (auto& c : padded) if (std::isspace(static_cast<unsigned char>(c))) c = ' ';
    return padded.find(" " + token + " ") != std::string::npos;
}
std::string ImageUrl(std::string src) {
    if (src.rfind("//", 0) == 0) src = "https:" + src;
    if (src.rfind("https://upload.wikimedia.org/", 0) != 0) return {};
    if (src.find_first_of("\r\n\t\\") != std::string::npos) return {};
    return src;
}
struct Serializer {
    PreparedArticle result;
    std::size_t nodes = 0;
    void Visit(const GumboNode* node, int depth) {
        if (++nodes > 40000 || depth > 96) throw std::runtime_error("HTML structure limit");
        if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE) {
            result.html += Escape(node->v.text.text);
            return;
        }
        if (node->type != GUMBO_NODE_ELEMENT) return;
        const auto& e = node->v.element;
        const std::string tag = gumbo_normalized_tagname(e.tag);
        const std::string cls = Attr(e, "class");
        if (tag == "script" || tag == "style" || tag == "head" || tag == "link" ||
            tag == "iframe" || tag == "object" || tag == "svg" || tag == "form" ||
            tag == "nav" || tag == "noscript" || !Attr(e, "hidden").empty() ||
            gumbo_get_attribute(&e.attributes, "hidden")) return;
        for (const char* hidden : {"mw-editsection", "reference", "references", "reflist",
                                  "navbox", "metadata", "noprint", "toc"}) {
            if (HasClass(cls, hidden)) return;
        }
        const std::string allowed = " div span p h1 h2 h3 h4 h5 h6 a img figure figcaption table caption thead tbody tfoot tr th td ul ol li dl dt dd b strong i em small sub sup blockquote pre code br hr section " ;
        const bool keep = !tag.empty() && allowed.find(" " + tag + " ") != std::string::npos;
        if (keep) {
            result.html += "<" + tag;
            if (tag == "a") {
                auto target = ArticleTarget(Attr(e, "href"));
                if (!target.empty() && !HasClass(cls, "new"))
                    result.html += " data-course-target=\"" + Escape(target) + "\" href=\"#\"";
            }
            if (tag == "img") {
                auto url = ImageUrl(Attr(e, "src"));
                if (!url.empty()) {
                    auto it = std::find(result.imageUrls.begin(), result.imageUrls.end(), url);
                    if (it != result.imageUrls.end() || result.imageUrls.size() < kMaxImages) {
                        if (it == result.imageUrls.end()) result.imageUrls.push_back(url);
                        result.html += " src=\"" + Escape(url) + "\"";
                    }
                }
                result.html += " alt=\"" + Escape(Attr(e, "alt")) + "\"";
            }
            if (tag == "td" || tag == "th") {
                for (const char* name : {"colspan", "rowspan"}) {
                    const auto v = Attr(e, name);
                    if (!v.empty() && v.size() <= 2 &&
                        v.find_first_not_of("0123456789") == std::string::npos && std::stoi(v) > 0)
                        result.html += " " + std::string(name) + "=\"" + v + "\"";
                }
            }
            if (HasClass(cls, "infobox") || HasClass(cls, "thumb") || HasClass(cls, "tright"))
                result.html += " class=\"course-float\"";
            result.html += ">";
        }
        for (unsigned i = 0; i < e.children.length; ++i)
            Visit(static_cast<const GumboNode*>(e.children.data[i]), depth + 1);
        if (keep && tag != "img" && tag != "br" && tag != "hr") result.html += "</" + tag + ">";
    }
};
}
std::string ArticleTarget(const std::string& href) {
    std::string value = href;
    for (const char* prefix : {"https://ja.wikipedia.org", "//ja.wikipedia.org"}) {
        if (value.rfind(prefix, 0) == 0) { value.erase(0, std::char_traits<char>::length(prefix)); break; }
    }
    if (value.rfind("/wiki/", 0) == 0) value.erase(0, 6);
    else if (value.rfind("./", 0) == 0) value.erase(0, 2);
    else return {};
    value = value.substr(0, value.find('#'));
    if (value.find('?') != std::string::npos) return {};
    std::string out;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < value.size(); ++i) {
        unsigned char c = value[i];
        if (c == '%') {
            if (i + 2 >= value.size() || hex(value[i + 1]) < 0 || hex(value[i + 2]) < 0) return {};
            c = static_cast<unsigned char>(hex(value[i + 1]) * 16 + hex(value[i + 2]));
            i += 2;
        }
        if (c < 32 || c == 127 || c == '#' || c == '\\') return {};
        out += c == '_' ? ' ' : static_cast<char>(c);
    }
    if (out.empty()) return {};
    const auto colon = out.find(':');
    if (colon != std::string::npos) {
        auto prefix = out.substr(0, colon);
        for (auto& c : prefix) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* ns : {"file", "image", "category", "template", "help", "special", "wikipedia", "wp", "media", "mediawiki", "portal", "project", "user", "talk", "module", "draft", "ファイル", "画像", "カテゴリ", "カテゴリー", "テンプレート", "特別", "利用者", "ノート", "プロジェクト", "モジュール"})
            if (prefix == ns) return {};
        if (prefix.find(" talk") != std::string::npos || prefix.find("‐ノート") != std::string::npos) return {};
    }
    return out;
}
PreparedArticle PrepareArticle(const std::string& title, const std::string& html) {
    if (html.empty() || html.size() > kMaxHtmlBytes) return {{}, {}, "HTML size limit or empty article"};
    auto destroy = [](GumboOutput* p) { gumbo_destroy_output(&kGumboDefaultOptions, p); };
    std::unique_ptr<GumboOutput, decltype(destroy)> parsed(gumbo_parse(html.c_str()), destroy);
    if (!parsed) return {{}, {}, "HTML parse failed"};
    Serializer s;
    s.result.html = "<!doctype html><html lang=\"ja\"><body><h1>" + Escape(title) + "</h1>";
    try { s.Visit(parsed->root, 0); }
    catch (const std::exception& e) { return {{}, {}, e.what()}; }
    s.result.html += "</body></html>";
    return std::move(s.result);
}
RasterSize FitRaster(float width, float height, std::uint64_t maxPixels) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        width > 65536 || height > 500000 || maxPixels == 0) return {};
    const double scale = std::min({1.0, 4096.0 / width, std::sqrt(static_cast<double>(maxPixels) / (static_cast<double>(width) * height))});
    RasterSize out;
    out.width = std::max(1u, static_cast<std::uint32_t>(std::floor(width * scale)));
    out.height = std::max(1u, static_cast<std::uint32_t>(std::floor(height * scale)));
    if (static_cast<std::uint64_t>(out.width) * out.height > maxPixels) return {};
    out.scaleX = out.width / width;
    out.scaleY = out.height / height;
    return out;
}
const std::string& CourseCss() {
    static const std::string css = R"CSS(
html, body { margin:0; padding:0; background:white; color:#202122; }
body { padding:40px; font-family:Meiryo; font-size:40px; line-height:1.55; }
h1 { font-size:80px; margin:0 0 32px; border-bottom:2px solid #a2a9b1; }
h2 { font-size:64px; margin:48px 0 20px; border-bottom:2px solid #a2a9b1; clear:both; }
h3,h4,h5,h6 { font-size:48px; margin:32px 0 16px; }
p { margin:16px 0; }
a[data-course-target] { color:#0645ad; background-color:#e6f2ff; text-decoration:underline; }
a[data-course-goal] { color:#806300; background-color:#fff4bb; }
table { border-collapse:collapse; max-width:100%; margin:24px 0; font-size:32px; }
th,td { border:1px solid #a2a9b1; padding:10px; }
th { background-color:#eaecf0; }
img { max-width:560px; max-height:700px; }
figure,.course-float { float:right; clear:right; width:560px; max-width:40%; margin:16px 0 24px 28px; }
figcaption,caption { font-size:28px; }
pre,code { white-space:normal; }
ul,ol { padding-left:60px; }
)CSS";
    return css;
}
}

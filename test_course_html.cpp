#include "graphics/html/CourseHtmlContainer.h"
#include <cmath>
#include "game/scenes/HtmlHoleSpacing.h"
#include "game/systems/HtmlTerrainRules.h"
#include <iostream>
#include <stdexcept>
#include <map>

using namespace graphics::html;
void Require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
struct TestContainer final : CourseHtmlContainer {
    std::map<litehtml::uint_ptr,float> fonts;
    litehtml::uint_ptr next = 1;
    int drawCount = 0;
    ~TestContainer() override { ClearDocument(); }
    litehtml::uint_ptr create_font(const litehtml::font_description& d, const litehtml::document*, litehtml::font_metrics* m) override {
        const float size=d.size.value(); *m={}; m->font_size=size; m->height=size;
        m->ascent=size*.8f; m->descent=size*.2f; m->x_height=size*.5f; m->ch_width=size*.5f;
        fonts[next]=size; return next++;
    }
    void delete_font(litehtml::uint_ptr id) override { fonts.erase(id); }
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr id) override {
        float n=0;
        for (const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p)
            if ((*p&0xc0)!=0x80) n+= *p < 128 ? .5f : 1.f;
        return n*fonts.at(id);
    }
    void draw_text(litehtml::uint_ptr,const char*,litehtml::uint_ptr,litehtml::web_color,const litehtml::position&) override { ++drawCount; }
    void get_image_size(const char*,const char*,litehtml::size& s) override { s={320,180}; }
    void draw_image(litehtml::uint_ptr,const litehtml::background_layer&,const std::string&,const std::string&) override {}
    void draw_solid_fill(litehtml::uint_ptr,const litehtml::background_layer&,const litehtml::web_color&) override {}
    void draw_borders(litehtml::uint_ptr,const litehtml::borders&,const litehtml::position&,bool) override {}
    void draw_list_marker(litehtml::uint_ptr,const litehtml::list_marker&) override {}
    void set_clip(const litehtml::position&,const litehtml::border_radiuses&) override {}
    void del_clip() override {}
};
int main() {
    try {
        Require(ArticleTarget("/wiki/%E6%97%A5%E6%9C%AC#History")=="日本","decoded title");
        Require(ArticleTarget("https://ja.wikipedia.org/wiki/A_B")=="A B","absolute title");
        Require(ArticleTarget("./日本")=="日本","relative title");
        Require(ArticleTarget("/wiki/Star_Trek:_Voyager")=="Star Trek: Voyager","colon in main title");
        for (const char* s : {"#note","https://other.test/wiki/A","/wiki/File%3ATest.png","/wiki/A%00B","/wiki/%QQ","/wiki/A?action=edit"})
            Require(ArticleTarget(s).empty(),"excluded URL");
        auto p=PrepareArticle("Title <test>",R"HTML(<style>body{font-size:9000px}</style><script>evil()</script><p onclick="evil()">plain Target <a href="/wiki/Target">Target</a><a href="#note">note</a></p><div class="mw-editsection"><a href="/wiki/Edit">Edit</a></div><table><tr><td><a href="/wiki/Table">Table</a></td></tr></table><img src="//upload.wikimedia.org/wikipedia/commons/test.png"><img src="https://other.test/tracker">)HTML");
        Require(CourseCss().find("font-size:18px")!=std::string::npos,"course body font size");
        Require(CourseCss().find("h1 { font-size:36px")!=std::string::npos,"course heading font size");
        Require(p.error.empty(),"prepare");
        Require(p.html.find("evil")==std::string::npos,"script stripped");
        Require(p.html.find("9000")==std::string::npos,"style stripped");
        Require(p.html.find("Edit")==std::string::npos,"edit stripped");
        Require(p.imageUrls.size()==1,"image allowlist");
        TestContainer c;
        Require(c.Layout(p.html,"Target"),"layout");
        Require(c.Links().size()==2,"only actual anchors create holes");
        Require(c.Links()[0].target=="Target" && c.Links()[1].target=="Table","table link preserved");
        Require(c.Regions().size()>=4,"heading image table regions");
        std::string longLabel;
        for(int i=0;i<400;++i) longLabel+="あ";
        auto wrap=PrepareArticle("Wrapped", "<p><a href=\"/wiki/Long\">"+longLabel+"</a></p><p><a href=\"/wiki/Long\">Long</a></p>");
        Require(c.Layout(wrap.html,"Long"),"wrapped layout");
        Require(c.Links().size()==2,"same target separate elements");
        Require(c.Links()[0].fragments.size()>1,"Japanese wrap");
        const auto& link=c.Links()[0];
        bool inside=false;
        for(const auto& f:link.fragments) if(f.x==link.representative.x && f.y==link.representative.y) inside=true;
        Require(inside,"representative inside fragment");
        const float width=c.Width(), height=c.Height();
        c.Draw(0,512); Require(c.drawCount>0,"tile renders");
        for(std::uint64_t budget:{1024ull,4096ull,33554432ull}) {
            auto size=FitRaster(width,height,budget);
            Require(size.width && std::uint64_t(size.width)*size.height<=budget,"pixel budget");
            Require(std::abs(link.representative.x/width-link.representative.x*size.scaleX/size.width)<1e-5,"UV unchanged");
        }
        Require(!FitRaster(2048,INFINITY).width,"nonfinite rejected");
        Require(!FitRaster(2048,500001).width,"height limit");
        Require(!PrepareArticle("x",std::string(kMaxHtmlBytes+1,'x')).error.empty(),"input limit");
        c.ClearDocument(); Require(c.fonts.empty(),"fonts released");
        struct Hole { float x,y,width,height; bool isTarget; };
        std::vector<Hole> dense={{20,20,10,10,false},{22,20,10,10,true},{200,200,10,10,false}};
        auto spaced=game::scenes::SelectSpacedHtmlLinks(dense,1000,1000,100,100);
        Require(spaced.size()==2 && spaced.front().isTarget,"goal priority and spacing");
        std::vector<float> heights(10201,1.0f);
        std::vector<std::uint8_t> materials(10201,0);
        materials[30*101+30]=6;
        game::systems::ApplyHtmlTerrainLayout(
            101,101,100,100,2.0f,
            {{.2f,.2f,.6f,.6f,game::systems::HtmlRegionKind::Body}},
            heights,materials);
        Require(heights.size()==10201,"terrain dimensions");
        Require(heights[50*101+50]>1 && materials[50*101+50]==1,"image relief");
        Require(heights[5*101+5]==1 && materials[5*101+5]==0,"base terrain preserved");
        Require(materials[30*101+30]==6,"biome hazard preserved");
        for(float h:heights) Require(std::isfinite(h) && h>=1 && h<=1.48f,"bounded terrain overlay");
        for(int z=0;z<101;++z) for(int x=1;x<101;++x)
            Require(std::abs(heights[z*101+x]-heights[z*101+x-1])<.2f,"traversable slope");
        std::cout << "course_html: all checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

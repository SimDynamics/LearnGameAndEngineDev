#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/programming_technical_ranges.hpp"
#include "presentation/windows/curricular_text_roles.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/state_store.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Window { HWND value; ~Window() { if (value) DestroyWindow(value); } };
struct FormatVisibilityProbe {
    unsigned hidden=0,redraw_disabled=0;
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value){
        auto& self=*reinterpret_cast<FormatVisibilityProbe*>(value);
        if(message==WM_SETREDRAW&&!wp)++self.redraw_disabled;
        if(message==WM_STYLECHANGED&&wp==static_cast<WPARAM>(GWL_STYLE)&&lp){
            const auto& style=*reinterpret_cast<const STYLESTRUCT*>(lp);
            if((style.styleOld&WS_VISIBLE)&&!(style.styleNew&WS_VISIBLE))++self.hidden;
        }
        return DefSubclassProc(window,message,wp,lp);
    }
};
struct HintGestureProbe {
    std::vector<UINT> messages;
    bool consume=false;
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value) {
        auto& self=*reinterpret_cast<HintGestureProbe*>(value);
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE||message==WM_LBUTTONUP){
            self.messages.push_back(message);
            if(self.consume){
                if(message==WM_LBUTTONDOWN)SetCapture(item);
                if(message==WM_LBUTTONUP&&GetCapture()==item)ReleaseCapture();
                return 73;
            }
        }
        return DefSubclassProc(item,message,wp,lp);
    }
};
CHARFORMAT2W character(HWND edit, LONG position) {
    CHARRANGE before{}; SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{position,position + 1}; SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W result{}; result.cbSize = sizeof(result);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&result));
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
    return result;
}
bool reference_at(HWND target,std::size_t position){
    const auto ranges=lgaed::presentation::rich_text_reference_ranges(target);
    return std::ranges::any_of(ranges,[&](const auto& range){return range.first<=position&&position<range.second;});
}
void neutral_reference_background(HWND target,const CHARFORMAT2W& style){
    using namespace lgaed::presentation;
    check(!(style.dwEffects&CFE_UNDERLINE),"Curricular highlighting introduced a line under the letters");
    if(reader_theme::palette(target).high_contrast)return;
    const auto background=style.crBackColor;
    check(!(style.dwEffects&CFE_AUTOBACKCOLOR)&&background==reader_theme::reference_background(target)
        &&background!=reader_theme::background(target),"A curricular reference lacks its exact static Android backdrop");
}
struct NativeRaster {LONG width=0,height=0;std::vector<DWORD> pixels;};
void save_native_raster(const NativeRaster& raster,const std::string& path){
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=raster.width;info.biHeight=-raster.height;
    info.biPlanes=1;info.biBitCount=32;file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(raster.pixels.size()*sizeof(DWORD));
    std::ofstream output(path,std::ios::binary);check(static_cast<bool>(output),"Open the optional synthetic backdrop artifact");
    output.write(reinterpret_cast<const char*>(&file),sizeof(file));output.write(reinterpret_cast<const char*>(&info),sizeof(info));
    output.write(reinterpret_cast<const char*>(raster.pixels.data()),static_cast<std::streamsize>(raster.pixels.size()*sizeof(DWORD)));
}
NativeRaster native_raster(HWND edit,bool clipped=false,LPARAM flags=PRF_CLIENT|PRF_ERASEBKGND){
    RECT area{};GetClientRect(edit,&area);const auto dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;
    info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* memory=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&memory,nullptr,0);
    check(dc&&bitmap&&memory,"Create an isolated technical-background raster");const auto previous=SelectObject(dc,bitmap);
    FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if(clipped)IntersectClipRect(dc,9,7,area.right-11,area.bottom-13);
    SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),flags);GdiFlush();
    const auto first=static_cast<const DWORD*>(memory);
    NativeRaster result{area.right,area.bottom,std::vector<DWORD>(first,first+static_cast<std::size_t>(area.right)*area.bottom)};
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return result;
}
struct RichStream {std::vector<BYTE> bytes;std::size_t offset=0;};
DWORD CALLBACK write_rich_stream(DWORD_PTR cookie,LPBYTE buffer,LONG size,LONG* copied){
    auto& stream=*reinterpret_cast<RichStream*>(cookie);stream.bytes.insert(stream.bytes.end(),buffer,buffer+size);*copied=size;return 0;
}
DWORD CALLBACK read_rich_stream(DWORD_PTR cookie,LPBYTE buffer,LONG size,LONG* copied){
    auto& stream=*reinterpret_cast<RichStream*>(cookie);const auto count=std::min<std::size_t>(size,stream.bytes.size()-stream.offset);
    std::copy_n(stream.bytes.data()+stream.offset,count,buffer);stream.offset+=count;*copied=static_cast<LONG>(count);return 0;
}
void clone_native_document(HWND source,HWND target){
    RichStream bytes;EDITSTREAM write{reinterpret_cast<DWORD_PTR>(&bytes),0,write_rich_stream};
    SendMessageW(source,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&write));check(!write.dwError&&!bytes.bytes.empty(),"Export a native format reference");
    EDITSTREAM read{reinterpret_cast<DWORD_PTR>(&bytes),0,read_rich_stream};SendMessageW(target,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&read));
    check(!read.dwError,"Import the independent native format reference");
    int numerator=0,denominator=0;SendMessageW(source,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
    SendMessageW(target,EM_SETZOOM,numerator,denominator);
    POINT scroll{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));SendMessageW(target,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
}
void technical_background_geometry(const char* artifact=nullptr){
    using namespace lgaed::presentation;
    if(reader_theme::palette().high_contrast){std::cout<<"Technical background raster: system high contrast retained\n";return;}
    Window parent{CreateWindowExW(0,L"STATIC",L"Hidden background QA",WS_OVERLAPPEDWINDOW,0,0,1200,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    const auto create=[&]{return CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE|ES_NOHIDESEL,0,0,520,240,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr);};
    Window decorated{create()},native{create()};check(parent.value&&decorated.value&&native.value,"Create hidden background-only controls");
    for(const auto target:{decorated.value,native.value})reader_theme::apply(target,reader_theme::Surface::canvas);
    const auto pixel=[](COLORREF color){return (static_cast<DWORD>(GetRValue(color))<<16)|(static_cast<DWORD>(GetGValue(color))<<8)|GetBValue(color);};
    const auto surface=pixel(reader_theme::background(decorated.value));
    const std::array<std::string,4> sources{{
        "𐐀 Las comillas cambian qué dato estamos enviando a `std::cout` (Stroustrup, 2024). C++ sigue como prosa.",
        "Lee `std::cout << \"un mensaje suficientemente largo para ocupar varias líneas del párrafo\"; return 0;` y continúa.",
        "Antes.\r\n```cpp\r\nstd::cout << \"gyp\\n\";\r\nreturn 0;\r\n``` Después C++ y `std::cerr`.",
        "Mira `std::cout << \"gyp\";\n`Después sigue la prosa sin fondo."}};
    const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"std::cout",0,"bold_italic"}}};
    std::size_t scenarios=0,added=0,removed=0;bool selection_visible=false,wrapped=false;NativeRaster example;
    for(const auto& source:sources)for(const LONG width:{280,520})for(const int zoom:{75,100,150,200})for(const bool dim:{false,true}){
        for(const auto target:{decorated.value,native.value})SetWindowPos(target,nullptr,0,0,width,240,SWP_NOZORDER|SWP_NOACTIVATE);
        set_rich_text(decorated.value,source,emphasis);SendMessageW(decorated.value,EM_SETZOOM,zoom,100);
        const auto text=wide(edit_text(decorated.value));
        if(dim){CHARRANGE rest{static_cast<LONG>(text.find(L"std::cout"))+4,static_cast<LONG>(text.size())};
            SendMessageW(decorated.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&rest));
            CHARFORMAT2W shade{};shade.cbSize=sizeof(shade);shade.dwMask=CFM_COLOR;shade.crTextColor=RGB(117,117,117);
            SendMessageW(decorated.value,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&shade));}
        Microsoft::WRL::ComPtr<IRichEditOle> rich;SendMessageW(decorated.value,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;check(rich&&SUCCEEDED(rich.As(&document)),"Read the independent native backdrop geometry");
        const auto point=[&](LONG at,LONG align){Microsoft::WRL::ComPtr<ITextRange> range;POINTL value{};
            check(document->Range(at,at,&range)==S_OK&&range&&range->GetPoint(tomStart|tomClientCoord|tomAllowOffClient|align,&value.x,&value.y)==S_OK,"Measure the native selection cell");return value;};
        std::vector<std::pair<LONG,LONG>> groups;LONG begin=-1;COLORREF background=0;
        std::vector<bool> cells(static_cast<std::size_t>(width)*240,false);
        const auto allowance=std::max(2,MulDiv(2*static_cast<int>(GetDpiForWindow(decorated.value)),zoom,9600));
        for(LONG at=0;at<static_cast<LONG>(text.size());++at){const auto style=character(decorated.value,at);const bool backed=!(style.dwEffects&CFE_AUTOBACKCOLOR);
            if(backed){if(begin<0)begin=at;background=style.crBackColor;
                const auto a=point(at,TA_TOP|TA_LEFT),b=point(at,TA_BOTTOM|TA_RIGHT);
                if(a.y>80)wrapped=true;
                for(auto y=std::max<LONG>(0,a.y-allowance);y<std::min<LONG>(240,b.y+allowance);++y)
                    for(auto x=std::max<LONG>(0,std::min(a.x,b.x));x<std::min<LONG>(width,std::max(a.x,b.x));++x)cells[static_cast<std::size_t>(y)*width+x]=true;
            }else if(begin>=0){groups.emplace_back(begin,at);begin=-1;}}
        if(begin>=0)groups.emplace_back(begin,static_cast<LONG>(text.size()));check(!groups.empty(),"The technical fixture has no backdrop");
        std::array<NativeRaster,2> unselected;
        for(const bool selected:{false,true})for(const bool clipped:{false,true}){
            CHARRANGE selection{selected?groups.front().first:0,selected?groups.front().second:0};
            SendMessageW(decorated.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(decorated.value,EM_HIDESELECTION,FALSE,0);
            POINT scroll{};SendMessageW(decorated.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
            const auto modified=SendMessageW(decorated.value,EM_GETMODIFY,0,0),events=SendMessageW(decorated.value,EM_GETEVENTMASK,0,0);
            const auto undo=SendMessageW(decorated.value,EM_CANUNDO,0,0),redo=SendMessageW(decorated.value,EM_CANREDO,0,0);
            // PRF_CLIENT deliberately bypasses the decorator. This is the
            // exact native glyph/AA/selection raster, not a copied paint rule.
            const auto original=native_raster(decorated.value,clipped,PRF_CLIENT),actual=native_raster(decorated.value,clipped);
            for(std::size_t at=0;at<actual.pixels.size();++at){
                if(actual.pixels[at]==original.pixels[at])continue;
                const auto from=original.pixels[at]&0xffffffu,to=actual.pixels[at]&0xffffffu;
                if(!cells[at]||!((from==surface&&to==pixel(background))||(from==pixel(background)&&to==surface))){
                    std::cerr<<"Background damaged native raster source="<<(&source-sources.data())<<" zoom="<<zoom<<" width="<<width
                        <<" dim="<<dim<<" selected="<<selected<<" clipped="<<clipped<<" pixel="<<at%width<<','<<at/width
                        <<" native="<<original.pixels[at]<<" actual="<<actual.pixels[at]<<'\n';
                    if(artifact){save_native_raster(actual,std::string(artifact)+".actual.bmp");save_native_raster(original,std::string(artifact)+".native.bmp");}
                    throw std::runtime_error("A uniform backdrop changed glyphs/antialiasing, selection, adjacent prose or caller clipping");
                }
                if(from==surface)++added;else ++removed;
            }
            if(!selected){unselected[clipped?1:0]=actual;if(&source==sources.data()&&width==520&&zoom==150&&!dim&&!clipped)example=actual;}
            else if(actual.pixels!=unselected[clipped?1:0].pixels)selection_visible=true;
            CHARRANGE after{};SendMessageW(decorated.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
            POINT after_scroll{};SendMessageW(decorated.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after_scroll));
            check(after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&wide(edit_text(decorated.value))==text&&scroll.x==after_scroll.x&&scroll.y==after_scroll.y
                &&modified==SendMessageW(decorated.value,EM_GETMODIFY,0,0)&&events==SendMessageW(decorated.value,EM_GETEVENTMASK,0,0)
                &&undo==SendMessageW(decorated.value,EM_CANUNDO,0,0)&&redo==SendMessageW(decorated.value,EM_CANREDO,0,0),
                "Backdrop painting changed source, selection, scroll, Modify, events or native Undo/Redo");
            check(character(decorated.value,groups.front().first).crBackColor==background,"Painting changed static background metadata");++scenarios;
        }
    }
    check(added>100&&removed>100&&wrapped&&selection_visible,"The raster matrix missed extension, leading removal, wrapping or native selection");
    // This independent raster oracle compares the actual top/bottom envelopes
    // of different glyphs/fonts on ONE line. It knows no production formula.
    for(const int zoom:{75,100,150,200}){
        for(const auto target:{decorated.value,native.value})SetWindowPos(target,nullptr,0,0,1100,240,SWP_NOZORDER|SWP_NOACTIVATE);
        set_rich_text(decorated.value,"Mira `return` `bool` `std::cout` `gypq` y prosa.",emphasis);SendMessageW(decorated.value,EM_SETZOOM,zoom,100);
        SendMessageW(decorated.value,EM_SETSEL,0,0);clone_native_document(decorated.value,native.value);
        CHARFORMAT2W plain{};plain.cbSize=sizeof(plain);plain.dwMask=CFM_BACKCOLOR;plain.dwEffects=CFE_AUTOBACKCOLOR;
        SendMessageW(native.value,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&plain));
        const auto bare=native_raster(native.value),actual=native_raster(decorated.value);const auto text=wide(edit_text(decorated.value));
        const auto background=pixel(reader_theme::reference_background(decorated.value));std::optional<std::pair<LONG,LONG>> envelope;
        for(const std::wstring_view token:{L"return",L"bool",L"std::cout",L"gypq"}){const auto at=text.find(token);check(at!=text.npos,"The equal-height fixture lost a technical sample");
            POINTL a{},b{};SendMessageW(decorated.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&a),at);
            SendMessageW(decorated.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&b),at+token.size());
            check(a.y==b.y&&b.x>a.x,"The equal-height technical samples must remain on one visual line");
            LONG top=240,bottom=-1,glyph_top=240,glyph_bottom=-1;
            for(LONG y=0;y<240;++y)for(auto x=std::max<LONG>(0,a.x);x<std::min<LONG>(1100,b.x);++x){const auto offset=static_cast<std::size_t>(y)*1100+x;
                if((actual.pixels[offset]&0xffffffu)==background){top=std::min(top,y);bottom=std::max(bottom,y);}
                if((bare.pixels[offset]&0xffffffu)!=surface){glyph_top=std::min(glyph_top,y);glyph_bottom=std::max(glyph_bottom,y);}
            }
            if(!envelope)envelope=std::pair{top,bottom};
            if(*envelope!=std::pair{top,bottom})std::cerr<<"Unequal backdrop zoom="<<zoom<<" token="<<utf8(token)<<" top/bottom="<<top<<','<<bottom<<" expected="<<envelope->first<<','<<envelope->second<<'\n';
            check(*envelope==std::pair{top,bottom},"Technical tokens on one line have unequal background heights");
            check(top<glyph_top&&bottom>glyph_bottom&&bottom-top>0,"The technical backdrop does not surround the whole glyph including descenders with a small margin");
        }
    }
    set_rich_text(decorated.value,"Lee `std::cout` y conserva gyp.");clone_native_document(decorated.value,native.value);
    for(const LPARAM flags:{LPARAM{0},LPARAM{PRF_CLIENT},LPARAM{PRF_CLIENT|PRF_ERASEBKGND|PRF_CHECKVISIBLE}})for(const bool clipped:{false,true})
        check(native_raster(decorated.value,clipped,flags).pixels==native_raster(native.value,clipped,flags).pixels,"An unsupported print mode changed visibility, erase options or clipping");
    {
        std::string text;for(int line=0;line<10;++line)text+="Lee `std::cout << \"gyp\";` y sigue.\n";
        set_rich_text(decorated.value,text);SendMessageW(decorated.value,EM_SETZOOM,150,100);SendMessageW(decorated.value,EM_SETSEL,0,0);
        POINT zero{};SendMessageW(decorated.value,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&zero));const auto before=native_raster(decorated.value);
        POINT scroll{0,13};SendMessageW(decorated.value,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));SendMessageW(decorated.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        check(scroll.y>0&&scroll.y<before.height,"The fixture did not exercise a partial-row scroll");const auto after=native_raster(decorated.value);
        for(LONG y=1;y<before.height-scroll.y-1;++y)for(LONG x=2;x<before.width-2;++x)
            check((before.pixels[static_cast<std::size_t>(y+scroll.y)*before.width+x]&0xffffffu)==(after.pixels[static_cast<std::size_t>(y)*after.width+x]&0xffffffu),"Partial-row scrolling moved glyphs or left a backdrop artifact");
    }
    {
        const std::string source="Mira std::cout junto a (1)/(2) en la misma línea.";const auto first=source.find("(1)/(2)");
        const std::array<std::pair<std::size_t,std::size_t>,1> protected_math{{{first,first+7}}};
        set_rich_text(decorated.value,source,{},false,RichTextRole::body,"es",protected_math);SendMessageW(decorated.value,EM_SETEDITSTYLEEX,0,SES_EX_NOMATH);
        CHARRANGE selected{static_cast<LONG>(first),static_cast<LONG>(first+7)};SendMessageW(decorated.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        CHARFORMAT2W math{};math.cbSize=sizeof(math);math.dwMask=CFM_MATH|CFM_FACE;math.dwEffects=CFE_MATH;wcscpy_s(math.szFaceName,L"Cambria Math");
        SendMessageW(decorated.value,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&math));
        Microsoft::WRL::ComPtr<IRichEditOle> rich;SendMessageW(decorated.value,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument2> document;Microsoft::WRL::ComPtr<ITextRange2> range;
        check(rich&&SUCCEEDED(rich.As(&document))&&document->Range2(selected.cpMin,selected.cpMax,&range)==S_OK&&range&&range->BuildUpMath(0)==S_OK,"Build the independent mixed OfficeMath fixture");
        SendMessageW(decorated.value,EM_SETSEL,0,0);
        check(native_raster(decorated.value).pixels==native_raster(decorated.value,false,PRF_CLIENT).pixels,"The prose decorator changed an OfficeMath row");
    }
    for(const bool code:{false,true}){set_rich_text(decorated.value,code?"std::cout << \"gyp\\n\";":"Prosa corriente, sin tokens técnicos.",{},code);clone_native_document(decorated.value,native.value);
        check(native_raster(decorated.value).pixels==native_raster(native.value).pixels,"Reusing a control retained an obsolete semantic painter");}
    if(artifact&&!example.pixels.empty())save_native_raster(example,artifact);
    std::cout<<"Technical background raster: "<<scenarios<<" hidden comparisons; uniform line heights/full glyph margins, native AA, selection, clipping, scroll, math exclusion and source/state preservation: PASS\n";
}
void technical_range_contract(){
    using namespace lgaed::presentation::technical_prose;
    const auto covered=[](const Analysis& result,std::size_t begin,std::size_t end){for(auto at=begin;at<end;++at)
        if(!std::ranges::any_of(result.fragments,[&](Range range){return range.first<=at&&range.second>at;}))return false;return true;};
    for(const auto locale:{"es","en","fr"}){
        const std::wstring source=L"Before: #include <iostream>\nint count = 2; std::cout << \"ready\"; return 0; After.";const auto result=analyze(source,locale);
        for(const auto fragment:{L"#include <iostream>",L"int count = 2;",L"std::cout << \"ready\";",L"return 0;"}){
            const auto begin=source.find(fragment);check(covered(result,begin,begin+std::wstring_view(fragment).size()),"An Android technical statement lost its exact semantic range");}
        check(!covered(result,0,6)&&!covered(result,source.find(L"After"),source.size()),"A semantic statement swallowed neighboring prose");
        for(const auto text:{L"const int&& value = other;",L"int render() { return 0; }",L"left && right",L"total += entrada;",L"valor = calcular(2);",L"if (listo) { avanzar(); }",L"valor = objeto.calcular(2);",L"std::vector<int> values{1, 2};"})
            check(covered(analyze(text,locale),0,std::wstring_view(text).size()),"A complete Android declaration/expression/control lost part of its underlay");
        const std::wstring protected_text=LR"(Read https://example.org/std::cout?a=2 and \(x < y\), then \[a+b\].)";const auto protected_result=analyze(protected_text,locale);
        for(const auto part:{LR"(https://example.org/std::cout?a=2)",LR"(\(x < y\))",LR"(\[a+b\])"}){
            const auto begin=protected_text.find(part),end=begin+std::wstring_view(part).size();
            check(!overlaps({begin,end},protected_result.fragments),"A URL or explicit mathematical formula acquired a programming backdrop");}
        const std::wstring name=L"std:: indica el espacio de nombres.";const auto names=analyze(name,locale);
        check(covered(names,0,5)&&!overlaps({6,name.size()},names.fragments),"A namespace mention consumed its following explanatory verb");
    }
    for(const auto source:{L"El operador es << y conserva la secuencia.",L"The operator is << and keeps its place.",L"L'opérateur est << et garde sa place."}){
        const auto text=std::wstring_view(source);const auto begin=text.find(L"<<");const auto result=analyze(text,text.starts_with(L"The")?"en":text.starts_with(L"L'")?"fr":"es");
        check(covered(result,begin,begin+2)&&!covered(result,begin-2,begin)&&!covered(result,begin+3,begin+4),"An operator underlay absorbed its adjacent prose copula/conjunction");}
    for(const auto source:{L"La main droite reste ouverte; puis elle se ferme.",L"La fonction sort du domaine; la main est calme.",L"Une double valeur existe dans cette phrase; elle reste visible.",L"Un exemple (ici); puis une explication."})
        check(analyze(source,"fr").fragments.empty(),"An ordinary French homonym or punctuation became C++ presentation");
    for(const auto source:{L"The main point is to return home; read the next section.",L"A long explanation; another ordinary clause.",L"First ; next. This isn't code!",L"See (details); then continue.",L"Calculate the comparisons; do not simply print constant ones.",L"A clog blocks the pipe; clean it."})
        check(analyze(source,"en").fragments.empty(),"An ordinary English homonym or clause punctuation became C++ presentation");
    for(const auto op:{L"+=",L"-=",L"*=",L"/=",L"%=",L"&=",L"|=",L"^=",L"<<=",L">>=",L"<=",L">=",L"##"}){
        check(analyze(op,"en").fragments.empty(),"An English graphical operator spelling became semantic code without its original context");
        const auto named=L"operator "+std::wstring(op);check(covered(analyze(named,"en"),9,named.size()),"An explicitly named English operator lost its exact backdrop");}
    check(analyze(L"!ready","en").fragments.empty()&&covered(analyze(L"!ready","fr"),0,1)&&covered(analyze(L"!ready","es"),0,1),
        "The original-language frontends' different logical-negation ownership was flattened");
    check(covered(analyze(L"--","fr"),0,2)&&covered(analyze(L"--","en"),0,2)&&analyze(L"::","es").fragments.empty(),
        "A graphical French/English dash or ordinary Spanish colon pair acquired the wrong semantic role");
    check(covered(analyze(L"operador ::","es"),9,11)&&covered(analyze(L"custom_name","es"),0,11)&&covered(analyze(LR"("a\n")","es"),0,5),
        "A named sign, observed identifier or complete escaped literal lost its exact source atom");
    check(!overlaps({10,11},analyze(L"carácter q)").fragments),"An explanatory closing parenthesis acquired a technical backdrop");
    check(analyze(LR"(u8R"delimiter_demasiado_largo(include "return")").fragments==std::vector<Range>{{4,29}},
        "An invalid prefixed raw-string spelling bypassed the canonical codeEnd word boundary");
    check(covered(analyze(L"ÉCRIS ; pour terminer.","fr"),6,7)&&analyze(L"ÉCRIS ; pour terminer.","es").fragments.empty()&&
        analyze(L"ÉCRIS ; pour terminer.","en").fragments.empty()&&explicit_semicolon(L"AÑADE ; para terminar.",6),
        "Original-language Unicode cues were flattened instead of matching their canonical frontend");
    for(const auto locale:{"fr-FR","fr-CA","FR-fr","fr_CA"})check(analyze(L"ÉCRIS ; pour terminer.",locale).fragments==std::vector<Range>{{6,7}},
        "An explicit French BCP-47 locale did not retain the canonical Android French range");
    for(const auto locale:{"en-US","en-GB","en-AU","EN-us","en_GB"}){
        check(analyze(L"place ;",locale).fragments==std::vector<Range>{{6,7}}&&analyze(L"The main point is to return home; read the next section.",locale).fragments.empty(),
            "An explicit English BCP-47 locale fell back to Spanish technical contexts");}
    {
        const std::wstring source=L"😀 Avant `std::cout` puis ```cpp\nint count{3};\n``` fin.";const auto spans=inline_code_spans(source);
        check(spans.wrappers.size()==4&&spans.fragments.size()==2&&source.substr(spans.fragments[1].first,spans.fragments[1].second-spans.fragments[1].first)==L"cpp\nint count{3};\n",
            "Android fenced-code projection changed the visible language tag, newline or original UTF-16 owners");
        check(inline_code_spans(L"Des ticks `   ` et ```\n \n``` restent visibles.").wrappers.empty()&&inline_code_spans(L"`sans fermeture").wrappers.empty(),
            "Blank or unfinished code delimiters were hidden even though Android keeps them visible");
        check(inline_code_spans(LR"(La formule `\(x+1\)` reste mathématique.)").wrappers.empty(),
            "A technical delimiter crossing mathematics changed the formula's original presentation");
    }
    for(const auto source:{L"La instrucción no tiene su ;.",L"¿Conviene quitar sólo el ;, o cambiar el texto?",L"Escribe ; para terminar.",L"La operación exige ;.",L"La asignación se rechaza por faltar ;.",L"Cada operación termina en ;.",L"Se acepta una operación terminada en ;.",L"Descarta hasta el primer ;.",L"El búfer contiene ; después del operando.",L"El lector entrega 6 y ;.",L"8 y ; se reconocen.",L"Cada signo +, -, *, /, (, ) o ; es otro token.",L"Prueba también [ 1 ; 3 ].",L"La operación devuelve ; al nivel superior.",L"El token siguiente puede ser ;.",L"La recuperación llega al primer ;.",L"El análisis comprueba ;.",L"El resultado se acepta después de ;.",L"La validación de ; precede al resultado.",L"Los signos ; y = internos pertenecen a la expresión.",L"El operador ; termina la instrucción.",L"El literal y ; termina la instrucción.",L"Si ; ya salió del origen, conserva esa unidad.",L"La regla operación→sentencia ; termina aquí.",L"Signo: ; y Fin.",L"Si encuentra +, -, ; o final, conserva el token.",L"La gramática inicial de sólo sumas y final ; conserva sus reglas.",L"Esta etapa acepta números, +, -, final ; y separadores."}){
        const auto text=std::wstring_view(source);const auto at=text.find(L';');check(explicit_semicolon(text,at)&&covered(analyze(text),at,at+1),"An established Android metalinguistic semicolon lost its source underlay");}
    for(const auto source:{L"La instrucción termina; después sigue.",L"Cambiar el texto; luego probar.",L"El texto ;continuará.",L"Primero ; después sigue.",L"First ; next. This isn't code!",L"Je lis ; puis je continue.",L"Il ajoute ; ensuite, il vérifie.",L"N'ajoute pas ; à chaque ligne ; conserve la prose.",L"6000 + 300 + 8 = 6308 ; no hay decenas.",L"El resultado final ; después sigue."}){
        const auto text=std::wstring_view(source);const auto at=text.find_last_of(L';');check(!explicit_semicolon(text,at),"Ordinary clause punctuation acquired a technical-sign role");}
}
void technical_range_stress(){
    using namespace lgaed::presentation::technical_prose;
    const std::wstring token(20000,L'a');
    const std::array<std::wstring,10> sources{{token,L"Lee https://example.org/"+token+L"?std::cout después.",
        L"$"+token+L" ` sin cierre",L"`"+token+L"`",L"#include <"+token+L">",L"std::"+token+L" después.",
        L"El literal \""+token+L"\\\" queda sin cierre.",L"return "+std::wstring(20000,L'9')+L";",L"return"+std::wstring(20000,L' ')+L"0;",L"El literal \""+token+L"\\n\"."}};
    for(std::size_t index=0;index<sources.size();++index){
        std::cout<<"Technical-range stress case "<<index<<" ("<<sources[index].size()<<" UTF-16 units)\n"<<std::flush;
        for(const auto locale:{"es","en","fr"}){const auto before=sources[index];const auto result=analyze(sources[index],locale);
            check(sources[index]==before,"Technical-range stress changed its original source");
            for(const auto& [first,last]:result.fragments)check(first<last&&last<=sources[index].size(),"Technical-range stress produced an invalid UTF-16 range");}
    }
    const auto header=L"#include <"+token+L">";const auto included=analyze(header);check(included.fragments.size()==1&&included.fragments[0]==Range{0,header.size()},
        "The bounded regex projection truncated a long header instead of preserving its original offsets");
    std::cout<<"Long technical tokens, incomplete delimiters and protected URLs: PASS\n";
}
std::string fixture_text(std::string_view value){std::string result;for(std::size_t at=0;at<value.size();++at){auto next=value[at];if(next=='\\'){
        check(++at<value.size(),"Incomplete range-fixture escape");next=value[at];switch(next){case 'n':next='\n';break;case 'r':next='\r';break;case 't':next='\t';break;case '\\':break;default:throw std::runtime_error("Invalid range-fixture escape");}}
    result+=next;}return result;}
std::vector<lgaed::presentation::technical_prose::Range> fixture_ranges(std::string_view value){
    if(value=="-")return {};
    std::vector<lgaed::presentation::technical_prose::Range> result;for(std::size_t begin=0;begin<value.size();){const auto colon=value.find(':',begin),comma=value.find(',',begin);
        check(colon!=value.npos&&(comma==value.npos||colon<comma),"Invalid range fixture");
        result.emplace_back(std::stoull(std::string(value.substr(begin,colon-begin))),std::stoull(std::string(value.substr(colon+1,comma==value.npos?value.size()-colon-1:comma-colon-1))));
        if(comma==value.npos)break;begin=comma+1;}return result;}
std::string fixture_ranges_text(std::span<const lgaed::presentation::technical_prose::Range> ranges){std::string result;for(const auto& [first,last]:ranges){if(!result.empty())result+=',';result+=std::to_string(first)+':'+std::to_string(last);}return result.empty()?"-":result;}
int technical_range_parity(const char* path,const char* diagnostics){
    using namespace lgaed::presentation;std::ifstream input(path,std::ios::binary);check(static_cast<bool>(input),"Cannot open canonical Android range fixture");
    std::ofstream output;if(diagnostics){output.open(diagnostics,std::ios::binary|std::ios::trunc);check(static_cast<bool>(output),"Cannot open range-parity diagnostics");}
    std::size_t checked=0,failed=0;std::string line;
    while(std::getline(input,line)){if(line.empty()||line.front()=='#')continue;if(line.back()=='\r')line.pop_back();std::array<std::string_view,4> cells;std::size_t begin=0;
        for(int cell=0;cell<3;++cell){const auto end=line.find('\t',begin);check(end!=line.npos,"Incomplete Android range fixture");cells[cell]=std::string_view(line).substr(begin,end-begin);begin=end+1;}cells[3]=std::string_view(line).substr(begin);
        const auto source=wide(fixture_text(cells[1]));const auto expected=fixture_ranges(cells[2]),expected_wrappers=fixture_ranges(cells[3]);
        const auto actual=technical_prose::analyze(source,cells[0]);const auto wrappers=technical_prose::inline_code_spans(source).wrappers;++checked;
        if(expected!=actual.fragments||expected_wrappers!=wrappers){++failed;
            const auto detail=line+"\t"+fixture_ranges_text(actual.fragments)+"\t"+fixture_ranges_text(wrappers)+'\n';if(output)output<<detail;
            if(failed<=12)std::cerr<<"Android parity mismatch "<<checked<<": "<<detail;
        }
    }
    std::cout<<"Canonical Android range parity: "<<checked<<" cases, "<<failed<<" mismatches\n";return failed?1:0;
}
void reference_ranges_follow_effective_text_changes(){
    using namespace lgaed::presentation;
    Window parent{CreateWindowExW(0,L"STATIC",L"Hidden reference ownership QA",WS_OVERLAPPEDWINDOW,
        0,0,600,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    Window target{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE|ES_READONLY,
        0,0,500,200,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
    check(parent.value&&target.value,"Cannot create the native readonly reference ownership fixture");
    reader_theme::apply(target.value,reader_theme::Surface::canvas);
    const std::string source="𐐀 La variable `valor` vale `2`. El resultado es 15.";
    const std::string visible="𐐀 La variable valor vale 2. El resultado es 15.";
    const auto text=wide(visible);
    const std::vector<std::pair<std::size_t,std::size_t>> expected{{text.find(L"valor"),text.find(L"valor")+5},
        {text.find(L"2"),text.find(L"2")+1},{text.find(L"15"),text.find(L"15")+2}};
    const auto publish=[&]{set_rich_text(target.value,source);
        check(edit_text(target.value)==visible&&rich_text_reference_ranges(target.value)==expected,
            "The renderer did not publish the independently expected native UTF16 reference ranges");};
    publish();SendMessageW(target.value,EM_EMPTYUNDOBUFFER,0,0);
    CHARRANGE selected{static_cast<LONG>(expected.front().first),static_cast<LONG>(expected.front().second)};
    SendMessageW(target.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    // Clipboard operations are deliberately unnecessary: native character,
    // clear and empty Undo/Redo attempts exercise rejected readonly input.
    for(const UINT message:{WM_CHAR,WM_CLEAR,WM_UNDO,EM_UNDO,EM_REDO}){
        SendMessageW(target.value,message,message==WM_CHAR?L'X':0,0);
        if(edit_text(target.value)!=visible||rich_text_reference_ranges(target.value)!=expected)
            std::cerr<<"Readonly reference ownership mismatch after message="<<message<<'\n';
        check((GetWindowLongPtrW(target.value,GWL_STYLE)&ES_READONLY)&&edit_text(target.value)==visible
            &&rich_text_reference_ranges(target.value)==expected,"Rejected readonly input invalidated unchanged reference ownership");
    }
    const std::wstring replacement=L"Texto realmente reemplazado 7.";
    SendMessageW(target.value,WM_SETTEXT,0,reinterpret_cast<LPARAM>(replacement.c_str()));
    check(edit_text(target.value)==utf8(replacement)&&rich_text_reference_ranges(target.value).empty(),
        "A real programmatic replacement retained references belonging to the old document");
    publish();const auto same_text=wide(visible);
    SendMessageW(target.value,WM_SETTEXT,0,reinterpret_cast<LPARAM>(same_text.c_str()));
    check(edit_text(target.value)==visible&&rich_text_reference_ranges(target.value).empty(),
        "Replacing document formatting with the same text retained stale reference ownership");
    publish();
    std::cout<<"PASS reference ownership: rejected readonly input preserves exact UTF16 ranges; real and same-text document replacement invalidate them\n";
}
void curricular_role_formatting(){
    using namespace lgaed::presentation;
    Window parent{CreateWindowExW(0,L"STATIC",L"Hidden curricular role QA",WS_OVERLAPPEDWINDOW,
        0,0,900,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    check(parent.value!=nullptr,"Cannot create the curricular role QA host");
    const std::string source="𐐀 La variable `a` vale `2` y vuelve a la sala. `int turnos = 1;` continúa. "
        "`++turnos;` aumenta el valor existente. Es una definición, no una actualización del objeto existente. "
        "El identificador `texto` guarda la cadena `\"Hola\"`. "
        "En C++, `3 * 5` produce 15. El relleno de la página anterior utiliza `10` como inicializador y `-=` para restar. "
        "El código `int y = 2;` y `int para = 3;` usa `y -= para;`.\r\nAntes: `5`\r\nAhora: `8`\r\nLa consola muestra `6`. "
        "Hay 7 y 8 elementos. La consola muestra `1.5 2.5`. `2024` es un literal real. "
        "Según Stroustrup (2024, pp. 139–140), seguimos. El literal `2024` pertenece a `int anno = 2024;`. "
        "(Stroustrup, 2024), Stroustrup (2024), (Köppe et al., 2023, pp. 865–866) "
        "y Stroustrup (2024, sección 4.2.3) conservan sus citas. El resultado es 2024.";
    auto displayed=source;std::erase(displayed,'`');std::erase(displayed,'\r');
    const std::array<lgaed::curriculum::Emphasis,2> emphasis{{{"variable",0,"bold_italic"},{"produce",0,"italic"}}};
    // Role expectations are authored here, independently of the production
    // detector. The code editor supplies the current palette, not a copied RGB.
    const std::string program="int a = 2; int turnos = 1; ++turnos; const char* texto = \"Hola\"; return 2024; 3 * 5; int y = 2; int para = 3; y -= para; int anterior = 3;";
    for(const int surface:{-1,0,1,2}){
        const auto create=[&]{return CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE,
            0,0,700,400,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr);};
        Window target{create()},reference{create()},mathematics{create()};
        check(target.value&&reference.value&&mathematics.value,"Cannot create independent curricular color references");
        if(surface>=0)for(const auto window:{target.value,reference.value,mathematics.value})
            reader_theme::apply(window,surface==0?reader_theme::Surface::canvas:surface==1?reader_theme::Surface::card:reader_theme::Surface::field);
        set_rich_text(reference.value,program,{},true);set_rich_text(mathematics.value,"15");
        const auto neutral=reader_theme::foreground(mathematics.value);
        check(character(mathematics.value,0).crTextColor==neutral,"Mathematics no longer uses its active neutral foreground");
        const auto code_ink=[&](std::wstring_view token){const auto visible=wide(program);const auto at=visible.find(token);
            check(at!=visible.npos,"The independent C++ color reference lost a token");return character(reference.value,static_cast<LONG>(at)).crTextColor;};
        for(const int zoom:{75,100,150,200}){
            set_rich_text(target.value,source,emphasis);SendMessageW(target.value,EM_SETZOOM,zoom,100);
            check(edit_text(target.value)==displayed,"Curricular roles rewrote text, Unicode, CRLF projection or visible delimiters");
            const auto text=wide(displayed);const auto prose=character(target.value,0).crTextColor;
            const auto expect=[&](std::wstring_view context,std::wstring_view token,COLORREF expected,bool highlighted){
                const auto start=text.find(context),local=context.find(token);
                check(start!=text.npos&&local!=context.npos,"The independently authored role fixture lost a passage");
                for(std::size_t at=start+local;at<start+local+token.size();++at){const auto style=character(target.value,static_cast<LONG>(at));
                    if(style.crTextColor!=expected||(style.dwEffects&CFE_UNDERLINE))
                        std::cerr<<"Curricular role mismatch: surface="<<surface<<" zoom="<<zoom<<" context="<<utf8(context)<<" token="<<utf8(token)<<" offset="<<at-start<<'\n';
                    check(style.crTextColor==expected&&!(style.dwEffects&CFE_UNDERLINE),
                        "A curricular reference lost its role ink or acquired an automatic underline");
                    if(highlighted){neutral_reference_background(target.value,style);
                        if(!reader_theme::palette(target.value).high_contrast)check(reference_at(target.value,at),"A real highlighted reference has no native reading range");
                    }else check((style.dwEffects&CFE_AUTOBACKCOLOR)&&!reference_at(target.value,at),
                        "Ordinary prose or a bibliographic citation acquired a reference background/range");
                }
            };
            expect(L"a vale 2",L"a",code_ink(L"a"),true);
            expect(L"vale 2 y",L"2",code_ink(L"2"),true);
            expect(L"vuelve a la sala",L"a",prose,false);
            for(const auto token:{L"int",L"turnos",L"=",L"1"})expect(L"int turnos = 1;",token,code_ink(token),true);
            // The lexer leaves ';' uncategorized. Preserve the historical
            // technical neutral; only categorized glyphs take editor ink.
            expect(L"int turnos = 1;",L";",reader_theme::palette(target.value).high_contrast?reader_theme::foreground(target.value):RGB(216,216,216),true);
            expect(L"++turnos;",L"++",code_ink(L"++"),true);expect(L"++turnos;",L"turnos",code_ink(L"turnos"),true);
            expect(L"aumenta el valor existente",L"aumenta el valor existente",prose,false);
            expect(L"objeto existente",L"objeto existente",prose,false);
            expect(L"identificador texto guarda",L"texto",code_ink(L"texto"),true);
            expect(L"cadena \"Hola\"",L"\"Hola\"",code_ink(L"\"Hola\""),true);
            expect(L"3 * 5 produce 15",L"3",code_ink(L"3"),true);expect(L"3 * 5 produce 15",L"*",code_ink(L"*"),true);
            expect(L"3 * 5 produce 15",L"5",code_ink(L"5"),true);expect(L"produce 15",L"15",neutral,true);
            expect(L"inicializador y -= para restar",L"y",prose,false);
            expect(L"-= para restar",L"para restar",prose,false);expect(L"-= para restar",L"-=",code_ink(L"-="),true);
            expect(L"int y = 2;",L"y",code_ink(L"y"),true);expect(L"int para = 3;",L"para",code_ink(L"para"),true);
            expect(L"y -= para;",L"y",code_ink(L"y"),true);expect(L"y -= para;",L"para",code_ink(L"para"),true);
            for(const auto context:{L"Antes: 5",L"Ahora: 8",L"consola muestra 6",L"Hay 7",L"y 8 elementos",L"resultado es 2024"}){
                const std::wstring_view passage=context;const auto at=passage.find_first_of(L"0123456789");
                expect(passage,passage.substr(at,passage.find_first_not_of(L"0123456789",at)-at),neutral,true);
            }
            expect(L"literal 2024 pertenece",L"2024",code_ink(L"2024"),true);
            expect(L"int anno = 2024;",L"2024",code_ink(L"2024"),true);
            expect(L"consola muestra 1.5 2.5",L"1.5",neutral,true);expect(L"consola muestra 1.5 2.5",L"2.5",neutral,true);
            expect(L"2024 es un literal real",L"2024",code_ink(L"2024"),true);
            for(const auto citation:{L"(Stroustrup, 2024)",L"Stroustrup (2024)",L"(Köppe et al., 2023, pp. 865–866)",L"Stroustrup (2024, sección 4.2.3)",L"Stroustrup (2024, pp. 139–140)"})
                expect(citation,citation,prose,false);
            const auto bold=character(target.value,static_cast<LONG>(text.find(L"variable")));
            check((bold.dwEffects&(CFE_BOLD|CFE_ITALIC))==(CFE_BOLD|CFE_ITALIC)
                &&(character(target.value,static_cast<LONG>(text.find(L"produce"))).dwEffects&CFE_ITALIC),
                "Curricular highlighting replaced authored bold/italic");
            const std::string question="Ordena las acciones para conservar primero el valor 3 en anterior y luego imprimir 8 3.";
            // This is an actual visible fragment of the current ordering
            // exercise, not its hidden correctOrder or success explanation.
            const std::string visible_fragment="    int anterior = actual;";
            for(const bool supplied:{false,true}){
                set_rich_text(target.value,question,{},false,RichTextRole::body,"es",{},RichTextContext{supplied?std::string_view(visible_fragment):std::string_view{}, {}});
                const auto first=wide(question).find(L"anterior");check(first!=std::wstring::npos&&edit_text(target.value)==question,"Visible context changed the actual ordering question");
                for(std::size_t offset=0;offset<std::wstring_view(L"anterior").size();++offset){const auto style=character(target.value,static_cast<LONG>(first+offset));
                    check(style.crTextColor==reader_theme::foreground(target.value)&&!(style.dwEffects&CFE_UNDERLINE)&&
                        (style.dwEffects&CFE_AUTOBACKCOLOR)&&!reference_at(target.value,first+offset),
                        "A name in unmarked ordering prose acquired code ink/background from a neighboring fragment");}
            }
            const auto name_at=wide(question).find(L"anterior");
            const lgaed::curriculum::TextSemantics marked_question{question,{{name_at,name_at+8,"anterior",lgaed::curriculum::SemanticRole::cpp}}};
            set_rich_text(target.value,question,{},false,RichTextRole::body,"es",{},RichTextContext{visible_fragment,{}},&marked_question);
            check(edit_text(target.value)==question,"Explicit ordering semantics changed its exact source");
            for(std::size_t offset=0;offset<8;++offset){const auto style=character(target.value,static_cast<LONG>(name_at+offset));
                check(style.crTextColor==code_ink(L"anterior")&&!(style.dwEffects&CFE_UNDERLINE),
                    "An explicitly authored ordering identifier lost the native code palette");
                neutral_reference_background(target.value,style);}
            // set_rich_text loads a whole document via EM_STREAMIN; it is not
            // an in-place edit transaction. The editor's syntax-only refresh
            // owns the existing selection-preservation contract.
            CHARRANGE selected{3,9};SendMessageW(reference.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
            style_cpp(reference.value);CHARRANGE after{};SendMessageW(reference.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
            check(after.cpMin==selected.cpMin&&after.cpMax==selected.cpMax&&edit_text(reference.value)==program,
                "Refreshing the independent native code palette moved selection or changed source");
        }
        const auto reference_background_before=reader_theme::reference_background(target.value);
        reader_theme::set_accent(parent.value,RGB(228,50,72));set_rich_text(target.value,source,emphasis);
        check(reader_theme::reference_background(target.value)==reference_background_before,"A red route accent recolored the static reference background");
        const auto actual_reference=wide(edit_text(target.value)).find(L"turnos");
        neutral_reference_background(target.value,character(target.value,static_cast<LONG>(actual_reference)));
        reader_theme::set_accent(parent.value,std::nullopt);
        set_rich_text(target.value,"Lección 3 de 6: página 9",{},false,RichTextRole::lesson_title);
        const auto heading=wide(edit_text(target.value));const auto heading_ink=character(target.value,0).crTextColor;
        check(rich_text_reference_ranges(target.value).empty(),"Lesson chrome acquired curricular reference metadata");
        for(std::size_t at=0;at<heading.size();++at){const auto style=character(target.value,static_cast<LONG>(at));
            check(!(style.dwEffects&CFE_UNDERLINE)&&(style.dwEffects&CFE_AUTOBACKCOLOR)&&style.crTextColor==heading_ink,
                "A lesson header/counter acquired curricular background, numeric ink or underline");}
        // Exact curricular prose from fuentes_objetivos_y_fases p4b2. Only
        // the version digits receive a background; C++ remains a language name.
        const std::string version="Este curso utiliza un perfil de C++20: la herramienta elegida debe poder proporcionar al menos las características de ese estándar.";
        const auto version_text=wide(version);const auto name=version_text.find(L"C++20");
        check(name!=version_text.npos,"The current curricular C++ version fixture lost its language name");
        const auto analyzed=technical_prose::analyze(version_text,"es");
        const auto roles=technical_prose::curricular_roles(version_text,analyzed);
        check(std::ranges::find(roles.numbers,std::pair{name+3,name+5})!=roles.numbers.end(),"The existing numeric classifier omitted the real C++20 version digits");
        set_rich_text(target.value,version);const auto expected_ink=reader_theme::foreground(target.value);
        check(edit_text(target.value)==version,"Highlighting a curricular language version rewrote its source");
        for(std::size_t at=0;at<version_text.size();++at){const auto style=character(target.value,static_cast<LONG>(at));
            check(!(style.dwEffects&CFE_UNDERLINE),"A curricular language version acquired a typographic underline");
            if(at>=name&&at<name+5)check(style.crTextColor==expected_ink&&(style.dwEffects&CFE_BOLD),"C++20 lost its complete language-name ink/bold treatment or became an identifier/operator sequence");
            if(at>=name+3&&at<name+5){neutral_reference_background(target.value,style);
                if(!reader_theme::palette(target.value).high_contrast)check(reference_at(target.value,at),"A highlighted version digit has no native reference ownership");
            }else check((style.dwEffects&CFE_AUTOBACKCOLOR)&&!reference_at(target.value,at),"Version highlighting escaped the digits into C++ or adjacent curricular prose");
        }
        const std::array<std::string,3> candidates{{"2cajas","cajas_totales","cajas totales"}};
        const std::string naming_question="¿Cuál es un nombre válido según la convención del curso?";
        const std::array<std::string,3> explanation_choices{{"Sólo se permite en nombres de archivos.","Es una palabra reservada del lenguaje.","Es demasiado corto."}};
        check(rich_text_naming_choice_samples(naming_question,candidates)&&
            !rich_text_naming_choice_samples("¿Qué impide usar int como nombre de una variable?",explanation_choices)&&
            !rich_text_naming_choice_samples("¿Cuál de estas frases explica la situación?",candidates),
            "Technical-sample context must use the public naming exercise, not validity or arbitrary option text");
        for(const auto& option:candidates){
            const auto sample=wide(option);
            set_rich_text(mathematics.value,option,{},true);
            std::vector<CHARFORMAT2W> previous;for(std::size_t at=0;at<sample.size();++at)previous.push_back(character(mathematics.value,static_cast<LONG>(at)));
            const lgaed::curriculum::TextSemantics marked_sample{option,{{0,sample.size(),option,lgaed::curriculum::SemanticRole::cpp}}};
            set_rich_text(target.value,option,{},false,RichTextRole::body,"es",{},RichTextContext{naming_question,{},true},&marked_sample);
            check(edit_text(target.value)==option,"Invalid-name highlighting repaired or rewrote the authored option");
            for(std::size_t at=0;at<sample.size();++at){const auto style=character(target.value,static_cast<LONG>(at));
                check(sample[at]==L' '||style.crTextColor==previous[at].crTextColor,
                    "An explicitly authored name sample differs from the same native code palette");
                neutral_reference_background(target.value,style);
                if(sample[at]!=L' '&&!reader_theme::palette(target.value).high_contrast)check(reference_at(target.value,at),"An incorrect technical sample lacks its own native reading range");
            }
        }
        for(const auto& option:explanation_choices){set_rich_text(target.value,option);
            for(std::size_t at=0;at<wide(option).size();++at){const auto style=character(target.value,static_cast<LONG>(at));
                check((style.dwEffects&CFE_AUTOBACKCOLOR)&&!(style.dwEffects&CFE_UNDERLINE)&&!reference_at(target.value,at),"An ordinary explanatory option acquired sample-wide background or identifier styling");}}
    }
    reference_ranges_follow_effective_text_changes();
    std::cout<<"PASS curricular roles: native C++ palette, active mathematics neutral, neutral background without underlines/red accents, citations/homonyms, Unicode/CRLF, emphasis, lesson chrome and four zooms; actual contrast mode only\n";
}
}
void unmarked_prose_keeps_its_role() {
    using namespace lgaed::presentation;
    Window parent{CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden prose role QA",WS_OVERLAPPEDWINDOW,
        0,0,680,420,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    check(parent.value!=nullptr,"Create the isolated nonactivating prose host");
    Window target{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE|ES_READONLY,
        0,0,640,380,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
    check(target.value!=nullptr,"Create the isolated prose control");
    reader_theme::apply(target.value,reader_theme::Surface::card);
    const RichTextContext public_code{"La variable anterior conserva su valor.","int a, o, y, anterior, izquierda, derecha;"};
    const std::wstring linked_source=L"std::string url = \"https://example.org/12\";";
    const std::array<technical_prose::Range,1> declared_source{{{0,linked_source.size()}}};
    check(technical_prose::authored_analysis(linked_source,declared_source).fragments==std::vector<technical_prose::Range>(declared_source.begin(),declared_source.end()),
        "An explicitly authored source lost its range because a string contains a URL");
    check(technical_prose::authored_analysis(linked_source).fragments.empty(),"A URL in unmarked prose inferred surrounding source code");
    const auto plain=[&](std::string_view text,RichTextRole role,std::string_view locale="es"){
        set_rich_text(target.value,text,{},false,role,locale,{},public_code);
        check(edit_text(target.value)==text,"Plain prose changed its original text");
        const auto expected=role==RichTextRole::body||reader_theme::palette(target.value).high_contrast
            ?reader_theme::foreground(target.value):RGB(255,255,255);
        for(std::size_t at=0;at<wide(text).size();++at){const auto style=character(target.value,static_cast<LONG>(at));
            check(style.crTextColor==expected&&!reference_at(target.value,at)&&(style.dwEffects&CFE_AUTOBACKCOLOR),
                "Unmarked prose borrowed identifier ink or a backdrop from code/context");
        }
    };
    for(const auto role:{RichTextRole::body,RichTextRole::prompt,RichTextRole::heading,RichTextRole::lesson_title}){
        for(const auto text:{"Caracteres y valores lógicos","El carácter y cualquier emoji forman texto.",
            "La letra o también aparece aquí.","La variable anterior conserva su valor.",
            "Supón izquierda y derecha en el dibujo.","La función main y el tipo int son referencias sin marcar."})plain(text,role);
        plain("The variable anterior and the letter a remain ordinary prose.",role,"en");
        plain("La variable anterior et la lettre a restent du texte.",role,"fr");
    }
    set_rich_text(target.value,"Compara `anterior`, pero anterior sigue siendo prosa.",{},false,RichTextRole::body,"es",{},public_code);
    const auto visible=wide(edit_text(target.value));const auto first=visible.find(L"anterior"),second=visible.find(L"anterior",first+1);
    check(first!=visible.npos&&second!=visible.npos,"The two homonymous occurrences disappeared");
    for(std::size_t n=0;n<8;++n){
        neutral_reference_background(target.value,character(target.value,static_cast<LONG>(first+n)));
        check(reference_at(target.value,first+n)&&!reference_at(target.value,second+n),"A delimited reference expanded to an unmarked homonym");
        check(character(target.value,static_cast<LONG>(second+n)).crTextColor==reader_theme::foreground(target.value),"A nearby marked name changed ordinary prose ink");
    }
    const lgaed::curriculum::TextSemantics marked{"La letra y une palabras.",{{9,10,"y",lgaed::curriculum::SemanticRole::cpp}}};
    set_rich_text(target.value,marked.source,{},false,RichTextRole::body,"es",{},public_code,&marked);
    check(reference_at(target.value,9)&&!reference_at(target.value,10),"An explicitly authored letter lost its exact reference limits");
    const lgaed::curriculum::TextSemantics natural{marked.source,{}};
    set_rich_text(target.value,natural.source,{},false,RichTextRole::body,"es",{},public_code,&natural);
    check(rich_text_reference_ranges(target.value).empty(),"An explicit empty semantic field inherited a technical letter");
    const std::string numbers="Hay 12.5 y 0 casos (Autor, 2024, p. 7).";
    set_rich_text(target.value,numbers,{},false,RichTextRole::body,"es",{},public_code);
    const auto numeric_text=wide(numbers);
    for(const auto token:{L"12.5",L"0 casos"}){
        const auto at=numeric_text.find(token);check(reference_at(target.value,at),"An ordinary curricular number lost its neutral backdrop");
        check(character(target.value,static_cast<LONG>(at)).crTextColor==reader_theme::foreground(target.value),"A prose number lost its neutral ink");
    }
    for(const auto token:{L"2024",L"7)."})check(!reference_at(target.value,numeric_text.find(token)),"A bibliographic locator became code or a curricular quantity");
    set_rich_text(target.value,"int anterior = 1;",{},true);
    if(!reader_theme::palette(target.value).high_contrast)
        check(character(target.value,0).crTextColor!=reader_theme::foreground(target.value),"Actual source code lost its syntax ink");
    check(!IsWindowVisible(parent.value)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=parent.value,"The isolated prose check activated the desktop");
    std::cout<<"PASS unmarked prose in four roles/three locales, exact delimited and authored references, numeric/citation boundaries and real C++ source; hidden controls only, no input/audio.\n";
}
int main(int argc,char** argv) {
    if(argc>1){try{
        if((argc==2||argc==3)&&std::string_view(argv[1])=="--technical-background"){
            const auto module=LoadLibraryW(L"Msftedit.dll");check(module!=nullptr,"Load the native synthetic background fixture");
            technical_background_geometry(argc==3?argv[2]:nullptr);FreeLibrary(module);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--unmarked-prose"){
            const auto module=LoadLibraryW(L"Msftedit.dll");check(module!=nullptr,"Load isolated prose role QA");
            unmarked_prose_keeps_its_role();FreeLibrary(module);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--technical-stress"){technical_range_stress();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--curricular-roles"){
            const auto module=LoadLibraryW(L"Msftedit.dll");check(module!=nullptr,"Load native curricular role QA");
            curricular_role_formatting();FreeLibrary(module);return 0;}
        if((argc==3||argc==4)&&std::string_view(argv[1])=="--android-parity")return technical_range_parity(argv[2],argc==4?argv[3]:nullptr);
        return 2;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
    const auto module = LoadLibraryW(L"Msftedit.dll");
    if (!module) return 2;
    try {
        using namespace lgaed::presentation;
        const auto declared_strings=[](std::string_view source,std::initializer_list<std::string_view> literals){
            lgaed::curriculum::TextSemantics result{std::string(source),{}};
            for(const auto literal:literals){const auto at=source.find(literal);
                check(at!=std::string_view::npos,"An explicitly authored string fixture lost its exact literal");
                const auto first=wide(source.substr(0,at)).size();
                result.spans.push_back({first,first+wide(literal).size(),std::string(literal),lgaed::curriculum::SemanticRole::string});}
            lgaed::curriculum::validate_text_semantics(result);return result;
        };
        curricular_role_formatting();
        technical_range_contract();
        std::cout<<"Android technical ranges: contextual names, syntax, symbols, protected math/URLs and prose neighbors: PASS\n";
        Window parent{CreateWindowExW(0, L"STATIC", L"RichEdit QA", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(parent.value != nullptr, "Cannot create QA parent");
        Window editor{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 500, 300, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(editor.value != nullptr, "Cannot create real RichEdit");
        {
            Window live{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,
                0,0,320,120,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(live.value!=nullptr,"Create the live syntax formatting fixture");
            reader_theme::apply(live.value,reader_theme::Surface::card);
            set_rich_text(live.value,"int n = 2;\nstd::cout << \"texto\\n\";",{},true);
            CHARRANGE selected{4,5};SendMessageW(live.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
            const auto original_text=edit_text(live.value);
            const auto original_events=SendMessageW(live.value,EM_GETEVENTMASK,0,0);
            FormatVisibilityProbe probe;
            check(SetWindowSubclass(live.value,FormatVisibilityProbe::proc,9918,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,
                "Observe actual visibility during repeated syntax passes");
            for(int repeat=0;repeat<16;++repeat)style_cpp(live.value);
            CHARRANGE after{};SendMessageW(live.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
            check(probe.hidden==0&&probe.redraw_disabled==0&&(GetWindowLongPtrW(live.value,GWL_STYLE)&WS_VISIBLE),
                "A syntax pass temporarily hid the editor while typing");
            check(edit_text(live.value)==original_text&&after.cpMin==selected.cpMin&&after.cpMax==selected.cpMax&&
                SendMessageW(live.value,EM_GETEVENTMASK,0,0)==original_events,
                "Atomic syntax formatting changed text, selection or native notifications");
            RemoveWindowSubclass(live.value,FormatVisibilityProbe::proc,9918);
        }
        {
            Window hinted{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,
                0,0,360,90,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(hinted.value!=nullptr,"Cannot create the real hinted RichEdit");reader_theme::apply(hinted.value,reader_theme::Surface::card);
            set_rich_text(hinted.value,"",{},true);SendMessageW(hinted.value,EM_EMPTYUNDOBUFFER,0,0);
            HintGestureProbe gesture;
            check(SetWindowSubclass(hinted.value,HintGestureProbe::proc,9917,reinterpret_cast<DWORD_PTR>(&gesture))!=FALSE,
                "Cannot attach the hint's lower gesture probe");
            const auto pixels=[&]{RECT bounds{};GetClientRect(hinted.value,&bounds);const auto dc=CreateCompatibleDC(nullptr);
                BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;
                info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                void* memory=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&memory,nullptr,0);
                check(bitmap&&memory,"Create the isolated hint drawing surface");const auto previous=SelectObject(dc,bitmap);
                FillRect(dc,&bounds,reader_theme::brush(reader_theme::Surface::card));
                SendMessageW(hinted.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);GdiFlush();
                const auto begin=static_cast<const std::uint32_t*>(memory);std::vector<std::uint32_t> result(begin,begin+bounds.right*bounds.bottom);
                SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return result;};
            const auto empty=pixels();const auto modified=SendMessageW(hinted.value,EM_GETMODIFY,0,0);
            const auto query_hint=[&](std::wstring_view expected){
                CHARRANGE selection{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
                const auto focus=GetFocus();const auto mask=SendMessageW(hinted.value,EM_GETEVENTMASK,0,0);
                const auto changed=SendMessageW(hinted.value,EM_GETMODIFY,0,0);
                int numerator=0,denominator=0;
                SendMessageW(hinted.value,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
                // These synchronous read messages must reach RichEdit with an
                // active hint, including the exact query made by showing().
                for(int repeat=0;repeat<16;++repeat){
                    check(GetWindowTextLengthW(hinted.value)==static_cast<int>(expected.size())&&
                        SendMessageW(hinted.value,WM_GETTEXTLENGTH,0,0)==static_cast<LRESULT>(expected.size()),
                        "An active hint recursed on WM_GETTEXTLENGTH or became document text");
                    CHARRANGE after{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
                    check(after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax,
                        "Reading an active hint moved the real text selection");
                }
                int after_numerator=0,after_denominator=0;
                SendMessageW(hinted.value,EM_GETZOOM,reinterpret_cast<WPARAM>(&after_numerator),reinterpret_cast<LPARAM>(&after_denominator));
                check(GetFocus()==focus&&SendMessageW(hinted.value,EM_GETEVENTMASK,0,0)==mask&&
                    SendMessageW(hinted.value,EM_GETMODIFY,0,0)==changed&&
                    numerator==after_numerator&&denominator==after_denominator&&wide(edit_text(hinted.value))==expected,
                    "Reading an active hint changed focus, events, Modify, zoom or entered text");
            };
            set_edit_hint(hinted.value,"Escribe el código faltante");query_hint(L"");const auto hint=pixels();
            check(hint!=empty&&edit_text(hinted.value).empty()&&GetWindowTextLengthW(hinted.value)==0&&
                !SendMessageW(hinted.value,EM_CANUNDO,0,0)&&SendMessageW(hinted.value,EM_GETMODIFY,0,0)==modified,
                "The empty hint was not painted, became false document text, changed Modify or entered Undo");
            set_edit_hint(hinted.value,"Escribe el código faltante",RGB(174,183,198));
            check(pixels()!=hint&&edit_text(hinted.value).empty()&&!SendMessageW(hinted.value,EM_CANUNDO,0,0),
                "The mathematical hint color was ignored or changed the editable document");
            set_edit_hint(hinted.value,"Escribe el código faltante");
            SetFocus(hinted.value);const auto focus=GetFocus();CHARRANGE before{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
            (void)pixels();CHARRANGE after{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
            check(focus==hinted.value&&GetFocus()==focus&&before.cpMin==after.cpMin&&before.cpMax==after.cpMax,
                "Drawing a focused hint moved the native caret, selection or focus");
            SendMessageW(hinted.value,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"x"));const auto typed=pixels();
            set_edit_hint(hinted.value,"");check(pixels()==typed&&edit_text(hinted.value)=="x","A hint remained over the actual typed text");
            set_edit_hint(hinted.value,"Escribe el código faltante");
            check(SendMessageW(hinted.value,EM_UNDO,0,0)&&edit_text(hinted.value).empty()&&pixels()==hint,
                "Undo failed to restore an empty document with a purely visual hint");
            check(SendMessageW(hinted.value,EM_CANREDO,0,0)!=0,"Painting the restored hint discarded Redo");
            SendMessageW(hinted.value,EM_SETREADONLY,TRUE,0);query_hint(L"");const auto readonly=pixels();set_edit_hint(hinted.value,"");
            check(pixels()==readonly&&edit_text(hinted.value).empty(),"A readonly display acquired an editable placeholder");
            SendMessageW(hinted.value,EM_SETREADONLY,FALSE,0);
            set_edit_hint(hinted.value,"Escribe la salida");
            const auto empty_selection=[&]{
                CHARRANGE range{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&range));
                check(range.cpMin==0&&range.cpMax==0&&edit_text(hinted.value).empty(),
                    "The synthetic final paragraph was selected as if it were the placeholder's first letter");
            };
            SendMessageW(hinted.value,EM_SETSEL,0,-1);empty_selection();
            CHARRANGE synthetic{0,1};SendMessageW(hinted.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&synthetic));empty_selection();
            POINTL start{};SendMessageW(hinted.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&start),0);
            const auto down=MAKELPARAM(start.x+1,start.y+1),moved=MAKELPARAM(start.x+90,start.y+40);
            gesture.messages.clear();
            SendMessageW(hinted.value,WM_LBUTTONDOWN,MK_LBUTTON,down);empty_selection();
            SendMessageW(hinted.value,WM_MOUSEMOVE,MK_LBUTTON,moved);empty_selection();
            SendMessageW(hinted.value,WM_LBUTTONUP,0,moved);empty_selection();
            SendMessageW(hinted.value,WM_LBUTTONDBLCLK,MK_LBUTTON,down);empty_selection();
            SendMessageW(hinted.value,WM_LBUTTONUP,0,down);empty_selection();
            check(gesture.messages==std::vector<UINT>{WM_LBUTTONDOWN,WM_MOUSEMOVE,WM_LBUTTONUP,WM_LBUTTONDBLCLK,WM_LBUTTONUP},
                "The empty hint swallowed part of the native pointer sequence");
            gesture.consume=true;gesture.messages.clear();
            check(SendMessageW(hinted.value,WM_LBUTTONDOWN,MK_LBUTTON,down)==73&&GetCapture()==hinted.value&&
                SendMessageW(hinted.value,WM_MOUSEMOVE,MK_LBUTTON,moved)==73&&GetCapture()==hinted.value&&
                SendMessageW(hinted.value,WM_LBUTTONUP,0,moved)==73&&GetCapture()!=hinted.value,
                "The hint blocked a lower panning handler or stole its mouse capture");
            empty_selection();gesture.consume=false;
            SendMessageW(hinted.value,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Escribe"));
            SendMessageW(hinted.value,EM_SETSEL,0,1);CHARRANGE selected{};
            SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
            query_hint(L"Escribe");
            wchar_t copied[16]{};SendMessageW(hinted.value,EM_GETSELTEXT,0,reinterpret_cast<LPARAM>(copied));
            check(selected.cpMin==0&&selected.cpMax==1&&std::wstring_view(copied)==L"E",
                "The hint guard prevented selecting the first letter of real entered text");
            set_edit_text(hinted.value,"");set_edit_hint(hinted.value,"");
            SendMessageW(hinted.value,EM_SETSEL,0,-1);SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
            check(selected.cpMin==0&&selected.cpMax==1,"An inactive hint changed ordinary native selection behavior");
            SendMessageW(hinted.value,EM_SETSEL,0,0);
            // Android's hint and entered text use the same native line. Check
            // actual glyph bounds, not just a nonempty placeholder bitmap.
            const auto hint_dpi=static_cast<int>(GetDpiForWindow(hinted.value));
            SetWindowPos(hinted.value,nullptr,0,0,MulDiv(600,hint_dpi,96),MulDiv(160,hint_dpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(parent.value,SW_SHOWNOACTIVATE);
            const auto ink_bounds=[&](const auto& image,const auto& background){
                RECT client{};GetClientRect(hinted.value,&client);RECT ink{client.right,client.bottom,0,0};
                for(LONG y=0;y<client.bottom;++y)for(LONG x=0;x<client.right;++x){
                    const auto at=static_cast<std::size_t>(y*client.right+x);
                    if(((image[at]^background[at])&0x00ffffffu)==0)continue;
                    ink.left=std::min(ink.left,x);ink.top=std::min(ink.top,y);
                    ink.right=std::max(ink.right,x+1);ink.bottom=std::max(ink.bottom,y+1);
                }
                return ink;
            };
            const auto native_viewport=[&]{
                Microsoft::WRL::ComPtr<IRichEditOle> rich;
                SendMessageW(hinted.value,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
                Microsoft::WRL::ComPtr<ITextDocument2> document;
                check(rich&&SUCCEEDED(rich.As(&document)),"Cannot inspect the real hint text viewport");
                RECT viewport{};
                check(document->GetClientRect(tomClientCoord|tomIncludeInset,&viewport.left,&viewport.top,&viewport.right,&viewport.bottom)==S_OK,
                    "Cannot measure all four real hint text insets");
                return viewport;
            };
            for(const auto zoom:{75,100,125,150,175,200}){
                SetFocus(parent.value);set_edit_hint(hinted.value,"");set_edit_text(hinted.value,"");
                SendMessageW(hinted.value,EM_EMPTYUNDOBUFFER,0,0);SendMessageW(hinted.value,EM_SETZOOM,zoom,100);
                const int padding=MulDiv(16,static_cast<int>(GetDpiForWindow(hinted.value)),96);
                RECT area{};GetClientRect(hinted.value,&area);
                const int inset=MulDiv(padding,100,zoom);
                RECT format{inset,inset,area.right-inset,area.bottom-inset};
                SendMessageW(hinted.value,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
                const auto blank=pixels();set_edit_hint(hinted.value,"Escribe la salida",reader_theme::foreground(hinted.value));
                const auto placeholder_ink=ink_bounds(pixels(),blank);
                check(placeholder_ink.right>placeholder_ink.left&&placeholder_ink.bottom>placeholder_ink.top,
                    "The zoomed native answer hint disappeared");
                SetFocus(hinted.value);RedrawWindow(hinted.value,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
                query_hint(L"");
                GUITHREADINFO caret_before{};caret_before.cbSize=sizeof(caret_before);
                check(GetGUIThreadInfo(GetCurrentThreadId(),&caret_before)&&caret_before.hwndCaret==hinted.value&&
                    caret_before.rcCaret.bottom>caret_before.rcCaret.top&&caret_before.rcCaret.top>=0&&
                    caret_before.rcCaret.bottom<=area.bottom-padding,
                    "Focusing a zoomed empty answer did not retain its complete native editing caret");
                for(int repaint=0;repaint<3;++repaint)RedrawWindow(hinted.value,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
                GUITHREADINFO caret_after{};caret_after.cbSize=sizeof(caret_after);
                check(GetGUIThreadInfo(GetCurrentThreadId(),&caret_after)&&caret_after.hwndCaret==hinted.value&&
                    EqualRect(&caret_before.rcCaret,&caret_after.rcCaret),
                    "Repainting the hint moved or replaced the native caret");
                SendMessageW(hinted.value,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Escribe la salida"));
                SetFocus(parent.value);const auto text_ink=ink_bounds(pixels(),blank);
                check(std::abs(placeholder_ink.left-text_ink.left)<=1&&std::abs(placeholder_ink.top-text_ink.top)<=1&&
                    std::abs(placeholder_ink.right-text_ink.right)<=2&&std::abs(placeholder_ink.bottom-text_ink.bottom)<=1,
                    "Entering the first line moved or clipped text relative to its zoomed placeholder");
                check(SendMessageW(hinted.value,EM_UNDO,0,0)&&edit_text(hinted.value).empty()&&
                    SendMessageW(hinted.value,EM_CANREDO,0,0),"Zoomed hint painting changed the original text Undo/Redo history");
                // A long, wrapped hint must clip exactly like native text at
                // every edge, including zoomed right/bottom asymmetric insets.
                SetFocus(parent.value);set_edit_hint(hinted.value,"");
                RECT asymmetric{MulDiv(MulDiv(11,hint_dpi,96),100,zoom),MulDiv(MulDiv(13,hint_dpi,96),100,zoom),
                    area.right-MulDiv(MulDiv(29,hint_dpi,96),100,zoom),area.bottom-MulDiv(MulDiv(37,hint_dpi,96),100,zoom)};
                SendMessageW(hinted.value,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&asymmetric));
                const auto without_hint=pixels();const auto viewport=native_viewport();
                const std::string long_hint=std::string(120,'W')+"\nEscribe la salida\nEscribe la salida\nEscribe la salida\nEscribe la salida";
                set_edit_hint(hinted.value,long_hint);const auto clipped=pixels();std::size_t ink=0;
                for(LONG y=0;y<area.bottom;++y)for(LONG x=0;x<area.right;++x){
                    const auto at=static_cast<std::size_t>(y*area.right+x);
                    if(((clipped[at]^without_hint[at])&0x00ffffffu)==0)continue;
                    ++ink;
                    check(x>=viewport.left&&x<viewport.right&&y>=viewport.top&&y<viewport.bottom,
                        "Hint pixels escaped the native zoomed text viewport");
                }
                check(ink>20&&edit_text(hinted.value).empty()&&SendMessageW(hinted.value,EM_CANREDO,0,0),
                    "Clipping the hint lost its text, modified the document or discarded Redo");
            }
            RemoveWindowSubclass(hinted.value,HintGestureProbe::proc,9917);
            ShowWindow(parent.value,SW_HIDE);
        }
        {
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"prosa",0,"bold_italic"}}};
            set_rich_text(editor.value,"Una prosa con `std::cout`.",emphasis);
            check(character(editor.value,0).yHeight==270
                && std::wstring(character(editor.value,0).szFaceName)==L"Roboto",
                "Lesson prose does not use the Android-derived native 18sp base");
            const auto emphasized=character(editor.value,4),technical=character(editor.value,14);
            check(emphasized.yHeight==270&&(emphasized.dwEffects&CFE_BOLD)&&(emphasized.dwEffects&CFE_ITALIC)
                && technical.yHeight==270&&std::wstring(technical.szFaceName)==reader_theme::code_font_face(),
                "Readable body typography flattened emphasis or changed the inline code face/size");
            set_rich_text(editor.value,"`return 0;`");
            std::array<CHARFORMAT2W,9> return_with_space;
            for(LONG at=0;at<9;++at){return_with_space[at]=character(editor.value,at);
                check(std::wstring_view(return_with_space[at].szFaceName)==reader_theme::code_font_face(),
                    "An explicitly delimited return statement lost its code font");}
            set_rich_text(editor.value,"`return\t0;`");
            for(LONG at=0;at<9;++at)check(std::wstring_view(character(editor.value,at).szFaceName)==return_with_space[at].szFaceName,
                "A tab-separated delimited return statement changed its code font");
            check(edit_text(editor.value)=="return\t0;","Styling a tab-separated return statement changed its source");
            set_rich_text(editor.value,"Una pregunta",{},false,RichTextRole::heading);
            check(character(editor.value,0).yHeight==345&&(character(editor.value,0).dwEffects&CFE_BOLD),
                "Headings and questions lost the 23sp bold hierarchy");
            set_rich_text(editor.value,"Una pregunta matemática",{},false,RichTextRole::prompt);
            check(character(editor.value,0).yHeight==270&&(character(editor.value,0).dwEffects&CFE_BOLD),
                "Mathematical prompts must be bold without increasing their 18sp body size");
            set_rich_text(editor.value,"Título de lección",{},false,RichTextRole::lesson_title);
            check(character(editor.value,0).yHeight==480&&(character(editor.value,0).dwEffects&CFE_BOLD),
                "The 32sp lesson title was flattened into an ordinary heading");
            set_rich_text(editor.value,"std::cout << 2;",{},true);
            check(character(editor.value,0).yHeight==270&&std::wstring(character(editor.value,0).szFaceName)==reader_theme::code_font_face(),
                "Displayed code is still smaller than the original source editor");
            SendMessageW(editor.value,EM_SETZOOM,150,100);
            const int dpi=static_cast<int>(GetDpiForWindow(editor.value));
            POINTL text_at{},space_at{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&text_at),1);
            SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_at),9);
            CHARRANGE preserved{2,4};SendMessageW(editor.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&preserved));
            check(rich_text_hit_test(editor.value,{text_at.x+1,text_at.y+MulDiv(27,dpi,96)})
                &&!rich_text_hit_test(editor.value,{space_at.x+1,space_at.y+MulDiv(27,dpi,96)}),
                "Actual font/zoom geometry confuses enlarged glyphs or real whitespace with a fixed-height gesture area");
            CHARRANGE after_hit{};SendMessageW(editor.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_hit));
            check(after_hit.cpMin==preserved.cpMin&&after_hit.cpMax==preserved.cpMax,"Pure text hit testing changed the source selection");
            SendMessageW(editor.value,EM_SETZOOM,0,0);
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            for(const auto role:{RichTextRole::body,RichTextRole::prompt,RichTextRole::heading,RichTextRole::lesson_title}){
                set_rich_text(editor.value,"std::cout",{},false,role,"es");const auto style=character(editor.value,0);
                if(reader_theme::palette().high_contrast)check(style.dwEffects&CFE_AUTOBACKCOLOR,"A semantic role replaced the system high-contrast background");
                else{
                    if(role!=RichTextRole::lesson_title)neutral_reference_background(editor.value,style);
                    else check(style.dwEffects&CFE_AUTOBACKCOLOR,"A lesson title acquired a curricular reference background");
                }
                check(!(style.dwEffects&CFE_UNDERLINE),"A body/prompt/heading/title introduced a typographic underline");
            }
            if(!reader_theme::palette().high_contrast){
                reader_theme::set_text_color(editor.value,RGB(235,239,245));set_rich_text(editor.value,"std::cout");
                neutral_reference_background(editor.value,character(editor.value,0));
                check(reader_theme::foreground(editor.value)==RGB(235,239,245),"The neutral background replaced an explicit mathematics-widget foreground reference");
                reader_theme::set_text_color(editor.value,std::nullopt);
            }
            const std::string source="𐐀 Lee std::cout y continúa con C++. El operador es << y conserva. No añadas ; a cualquier línea; luego lee.";
            const std::array<lgaed::curriculum::Emphasis,2> emphasis{{{"std::cout",0,"bold_italic"},{"C++",0,"italic"}}};
            const auto visible=wide(source);
            lgaed::curriculum::TextSemantics declared{source,{}};
            for(const auto token:{L"std::cout",L"<<",L";"}){const auto at=visible.find(token);
                declared.spans.push_back({at,at+std::wstring_view(token).size(),utf8(token),lgaed::curriculum::SemanticRole::cpp});}
            set_rich_text(editor.value,source,emphasis,false,RichTextRole::body,"es",{},{},&declared);
            check(edit_text(editor.value)==source,"The semantic backdrop changed source bytes or Unicode narration offsets");
            std::vector<technical_prose::Range> expected;
            for(const auto token:{L"std::cout",L"<<"}){const auto at=visible.find(token);expected.emplace_back(at,at+std::wstring_view(token).size());}
            expected.emplace_back(visible.find(L';'),visible.find(L';')+1);
            for(std::size_t at=0;at<visible.size();++at){
                const bool decorated=technical_prose::overlaps({at,at+1},expected)&&!reader_theme::palette().high_contrast;
                const auto style=character(editor.value,static_cast<LONG>(at));
                check(((style.dwEffects&CFE_AUTOBACKCOLOR)==0)==decorated,"The Android backdrop missed a technical glyph or spilled into adjacent prose/C++");
                check(!(style.dwEffects&CFE_UNDERLINE),"Technical highlighting added a typographic underline");
                if(decorated)neutral_reference_background(editor.value,style);
            }
            const auto technical=character(editor.value,static_cast<LONG>(visible.find(L"std::cout"))),language=character(editor.value,static_cast<LONG>(visible.find(L"C++")));
            check((technical.dwEffects&(CFE_BOLD|CFE_ITALIC))==(CFE_BOLD|CFE_ITALIC)&&technical.yHeight==270&&std::wstring_view(technical.szFaceName)==reader_theme::code_font_face(),
                "Adding a semantic background changed an existing inline face, size or authored emphasis");
            check((language.dwEffects&(CFE_BOLD|CFE_ITALIC))==(CFE_BOLD|CFE_ITALIC)&&(language.dwEffects&CFE_AUTOBACKCOLOR)&&std::wstring_view(language.szFaceName)==L"Roboto",
                "The C++ exception lost its existing bold/italic prose treatment");
            for(const int zoom:{75,100,150,200}){
                set_rich_text(editor.value,source,emphasis,false,RichTextRole::body,"es",{},{},&declared);SendMessageW(editor.value,EM_SETZOOM,zoom,100);
                std::vector<POINTL> positions;for(std::size_t at=0;at<visible.size();++at){POINTL point{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),at);positions.push_back(point);}
                const auto lines=SendMessageW(editor.value,EM_GETLINECOUNT,0,0);
                // Removing just native background ink supplies an independent
                // geometry reference; the semantic decoration must be metric-free.
                CHARRANGE all{0,-1};SendMessageW(editor.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
                CHARFORMAT2W plain{};plain.cbSize=sizeof(plain);plain.dwMask=CFM_BACKCOLOR;plain.dwEffects=CFE_AUTOBACKCOLOR;
                SendMessageW(editor.value,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&plain));
                for(std::size_t at=0;at<visible.size();++at){POINTL point{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),at);
                    check(point.x==positions[at].x&&point.y==positions[at].y,"A semantic backdrop changed native text measurements at a supported zoom");}
                check(lines==SendMessageW(editor.value,EM_GETLINECOUNT,0,0),"A background-only span changed native wrapping");
            }
            SendMessageW(editor.value,EM_SETZOOM,0,0);
            set_rich_text(editor.value,"Prosa normal sin decoración.");
            check(character(editor.value,0).dwEffects&CFE_AUTOBACKCOLOR,"Reusing a rich-text control retained an obsolete semantic backdrop");
            for(const auto locale:{"en","fr"}){
                const std::string prose=locale==std::string_view("en")?"The main point is to return home; read the next section.":"La main droite reste ouverte; puis elle se ferme.";
                set_rich_text(editor.value,prose,{},false,RichTextRole::body,locale);
                for(std::size_t at=0;at<wide(prose).size();++at)check(character(editor.value,static_cast<LONG>(at)).dwEffects&CFE_AUTOBACKCOLOR,
                    "An explicit English/French locale treated an ordinary homonym as technical prose");
            }
            const std::string protected_text=R"(Consulta https://example.org/std::cout?a=2 y \(std::cout + 2\), después `std::cout`.)";
            set_rich_text(editor.value,protected_text);const auto protected_visible=wide(edit_text(editor.value));
            for(const auto part:{LR"(https://example.org/std::cout?a=2)",LR"(\(std::cout + 2\))"}){const auto begin=protected_visible.find(part);
                for(auto at=begin;at<begin+std::wstring_view(part).size();++at)check(character(editor.value,static_cast<LONG>(at)).dwEffects&CFE_AUTOBACKCOLOR,
                    "Protected mathematics or a URL acquired a technical backdrop");}
            set_rich_text(editor.value,R"(std::cout << "C++";)" ,{},true);
            for(std::size_t at=0;at<wide(edit_text(editor.value)).size();++at)check(character(editor.value,static_cast<LONG>(at)).dwEffects&CFE_AUTOBACKCOLOR,
                "A source-code editor acquired the prose-only semantic background");
            const std::string fenced="😀 Avant ```cpp\r\nstd::cout << \"C++\";\r\n``` après et `return 0;`.";
            const std::string projected="😀 Avant cpp\nstd::cout << \"C++\";\n après et return 0;.";
            const std::array<lgaed::curriculum::Emphasis,1> fence_emphasis{{{"std::cout",0,"bold_italic"}}};
            set_rich_text(editor.value,fenced,fence_emphasis,false,RichTextRole::body,"fr");
            check(edit_text(editor.value)==projected,"Fenced technical prose stripped a language tag/newline or retained an Android-hidden wrapper");
            const auto fence_at=wide(projected).find(L"std::cout");const auto fenced_style=character(editor.value,static_cast<LONG>(fence_at));
            check((fenced_style.dwEffects&(CFE_BOLD|CFE_ITALIC))==(CFE_BOLD|CFE_ITALIC)&&((fenced_style.dwEffects&CFE_AUTOBACKCOLOR)!=0)==reader_theme::palette().high_contrast,
                "Hiding triple delimiters shifted Unicode/CRLF authored emphasis or the technical background");
            for(const auto text:{"Ticks `   ` et ```\n \n``` visibles.","`sans fermeture","La formule `\\(x+1\\)` reste mathématique."}){
                set_rich_text(editor.value,text,{},false,RichTextRole::body,"fr");check(edit_text(editor.value)==text,"Blank, mathematical or incomplete wrappers changed their visible source");}
            set_rich_text(editor.value,fenced,{},true);
            check(edit_text(editor.value)=="😀 Avant ```cpp\nstd::cout << \"C++\";\n``` après et `return 0;`.",
                "An actual source-code block acquired the prose-only delimiter projection");
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            const auto base=reader_theme::foreground(editor.value);
            const auto green=reader_theme::palette(editor.value).high_contrast?base:RGB(131,209,151);
            const std::string prose="El código fuente de C++ se ejecuta. C++20 mantiene el nombre.";
            set_rich_text(editor.value,prose);
            check(edit_text(editor.value)==prose,"Formatting the curricular language name rewrote the original narration text");
            const auto visible=wide(prose);
            for(const auto name:{L"C++",L"C++20"}) {
                const auto position=visible.find(name);
                check(position!=visible.npos,"Missing curricular C++ regression token");
                for(auto i=position;i<position+std::wstring_view(name).size();++i){
                    const auto ink=character(editor.value,static_cast<LONG>(i));
                    check(ink.crTextColor==base&&(ink.dwEffects&CFE_BOLD)&&
                        std::wstring_view(ink.szFaceName)==L"Roboto",
                        "C++ in curricular prose should be bold in the ordinary text color, not syntax ink");
                }
            }
            const auto surrounding=character(editor.value,static_cast<LONG>(visible.find(L"fuente")));
            check(surrounding.crTextColor==base&&!(surrounding.dwEffects&CFE_BOLD),
                "Language name emphasis leaked into surrounding prose");
            set_rich_text(editor.value,"Aprende `C++` sin alterar los bloques.");
            const auto inline_at=wide(edit_text(editor.value)).find(L"C++");
            check(inline_at!=std::wstring::npos&&
                (character(editor.value,static_cast<LONG>(inline_at)).dwEffects&CFE_BOLD)&&
                character(editor.value,static_cast<LONG>(inline_at)).crTextColor==base&&
                std::wstring_view(character(editor.value,static_cast<LONG>(inline_at)).szFaceName)==L"Roboto",
                "A standalone inline language label stayed purple or monospace instead of bold prose");
            const std::array<lgaed::curriculum::Emphasis,1> italic{{{"C++",0,"italic"}}};
            set_rich_text(editor.value,"C++ es un lenguaje.",italic);
            const auto emphasized=character(editor.value,0);
            check((emphasized.dwEffects&(CFE_BOLD|CFE_ITALIC))==(CFE_BOLD|CFE_ITALIC)&&emphasized.crTextColor==base,
                "Curricular C++ emphasis must preserve both automatic bold and authored italics");
            const std::string inline_output=R"code(En std::cout << "C++"; queda el literal de salida.)code";
            const auto output_string=declared_strings(inline_output,{R"("C++")"});
            set_rich_text(editor.value,inline_output,{},false,RichTextRole::body,"es",{},{},&output_string);
            const auto string_at=wide(edit_text(editor.value)).find(L"C++");
            check(string_at!=std::wstring::npos&&
                character(editor.value,static_cast<LONG>(string_at)).crTextColor==green&&
                !(character(editor.value,static_cast<LONG>(string_at)).dwEffects&CFE_BOLD),
                "A real C++ output string was mistaken for a bold language name");
            set_rich_text(editor.value,R"code(std::cout << "C++";)code",{},true);
            const auto code_at=wide(edit_text(editor.value)).find(L"C++");
            check(code_at!=std::wstring::npos&&
                character(editor.value,static_cast<LONG>(code_at)).crTextColor==green&&
                !(character(editor.value,static_cast<LONG>(code_at)).dwEffects&CFE_BOLD),
                "An actual code block lost its original string highlighting");
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            const auto base=reader_theme::foreground(editor.value);
            const auto string_ink=reader_theme::palette(editor.value).high_contrast?base:RGB(131,209,151);
            const auto check_ink=[&](std::wstring_view token,COLORREF ink){
                const auto visible=wide(edit_text(editor.value));const auto begin=visible.find(token);
                check(begin!=visible.npos,"The inline-string regression lost its original text");
                for(auto index=begin;index<begin+token.size();++index)
                    check(character(editor.value,static_cast<LONG>(index)).crTextColor==ink,
                        "An inline literal lost Android's complete string color or leaked into ordinary prose");
            };
            const std::string screenshot=R"text(𐐀 Un espacio dentro de un literal sí es parte del mensaje. En std::cout << "Sala" << " " << "Central";, las inserciones aportan Sala, un espacio y Central.)text";
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"Un espacio dentro de un literal",0,"bold"}}};
            const auto screenshot_strings=declared_strings(screenshot,{R"("Sala")",R"(" ")",R"("Central")"});
            set_rich_text(editor.value,screenshot,emphasis,false,RichTextRole::body,"es",{},{},&screenshot_strings);
            check(edit_text(editor.value)==screenshot,"Coloring inline strings changed source text or narration offsets");
            for(const auto token:{LR"("Sala")",LR"(" ")",LR"("Central")"})check_ink(token,string_ink);
            check_ink(L"las inserciones aportan Sala",base);
            const auto literal_at=static_cast<LONG>(wide(screenshot).find(L"Central\""));
            const auto literal_style=character(editor.value,literal_at);
            check(literal_style.yHeight==270&&std::wstring(literal_style.szFaceName)==L"Roboto"&&
                (character(editor.value,static_cast<LONG>(wide(screenshot).find(L"Un espacio"))).dwEffects&CFE_BOLD),
                "Inline color correction changed the literal's typography or an existing emphasis range");
            const std::string escaped=R"text(En std::cout << "std::cout << \"Hola\"; int // sigue" << '\n';, después "texto normal".)text";
            const auto escaped_strings=declared_strings(escaped,{R"text("std::cout << \"Hola\"; int // sigue")text",R"('\n')"});
            set_rich_text(editor.value,escaped,{},false,RichTextRole::body,"es",{},{},&escaped_strings);
            check(edit_text(editor.value)==escaped,"Inline escaped strings changed their source characters");
            check_ink(LR"text("std::cout << \"Hola\"; int // sigue")text",string_ink);
            check_ink(LR"('\n')",string_ink);check_ink(LR"("texto normal")",base);
            const std::string arguments=R"text(En std::cout << escoger(y, "Hola", '\n');, sigue la explicación.)text";
            const auto argument_strings=declared_strings(arguments,{R"("Hola")",R"('\n')"});
            set_rich_text(editor.value,arguments,{},false,RichTextRole::body,"es",{},{},&argument_strings);
            check_ink(LR"("Hola")",string_ink);check_ink(LR"('\n')",string_ink);
            for(const auto prose:{R"text(La frase "Hola" es prosa; std::cout representa el recurso.)text",
                R"text(Se menciona std::cout << "Hola" y luego otra frase; conserva su presentación.)text",
                "Se menciona std::cout << \"Hola\"\nOtra línea termina;",
                "Se menciona std::cout << \"Hola sin cerrar; continúa la frase."}){
                set_rich_text(editor.value,prose);check_ink(L"Hola",base);
            }
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            const auto base=reader_theme::foreground(editor.value);
            const bool high_contrast=reader_theme::palette(editor.value).high_contrast;
            const auto identifier_ink=high_contrast?base:RGB(184,151,244);
            const auto string_ink=high_contrast?base:RGB(131,209,151);
            const auto check_range=[&](std::wstring_view context,std::size_t offset,std::size_t count,COLORREF ink){
                const auto visible=wide(edit_text(editor.value));const auto start=visible.find(context);
                check(start!=visible.npos&&offset+count<=context.size(),"The technical-character regression lost its source context");
                for(auto index=start+offset;index<start+offset+count;++index){const auto actual=character(editor.value,static_cast<LONG>(index)).crTextColor;
                    if(actual!=ink)std::cerr<<"Technical-character diagnostics: context="<<utf8(context)<<", offset="<<index-start<<", actual="<<actual<<", expected="<<ink<<'\n';
                    check(actual==ink,"A technical character lost its Android ink or colored neighboring prose punctuation");}
            };
            const std::string lesson=R"text(𐐀 Dentro de un literal, \n representa un salto de línea. Su escritura en el código utiliza una barra inversa y la letra n, pero el texto producido contiene un carácter de control. No se muestran la barra inversa ni la letra n: juntas forman el escape.)text";
            const std::array<lgaed::curriculum::Emphasis,2> emphasis{{
                {"\\n representa un salto de línea",0,"bold"},{"letra n",1,"italic"}}};
            set_rich_text(editor.value,lesson,emphasis);
            check(edit_text(editor.value)==lesson,"Character highlighting changed Unicode text or narration offsets");
            check_range(LR"(\n representa)",0,2,string_ink);
            check_range(L"la letra n, pero",0,9,base);check_range(L"la letra n, pero",9,1,base);
            check_range(L"la letra n, pero",10,6,base);
            check_range(L"la letra n: juntas",9,1,base);check_range(L"la letra n: juntas",10,8,base);
            const auto visible=wide(lesson);
            const auto named_style=character(editor.value,static_cast<LONG>(visible.find(L"letra n")+6));
            check(named_style.yHeight==270&&std::wstring(named_style.szFaceName)==L"Roboto"&&(named_style.dwEffects&CFE_ITALIC)
                &&(character(editor.value,static_cast<LONG>(visible.find(L"\\n")+1)).dwEffects&CFE_BOLD),
                "Coloring a named letter changed its typography or authored emphasis");
            for(const auto source:{"letra n","Letra N:","CARÁCTER n.","caracter t,","carácter q)","carácter v?"}){
                set_rich_text(editor.value,source);const auto text=wide(source);const auto letter=text.find(L' ')+1;
                check_range(text,0,letter,base);check_range(text,letter,1,base);
                if(letter+1<text.size())check_range(text,letter+1,text.size()-letter-1,base);
            }
            set_rich_text(editor.value,"La letra n se escribe literalmente; su nombre es independiente.");
            check_range(L"La letra n se",9,1,base);
            const std::string escapes=R"(\n \t \r \0 \a \b \f \v)";
            set_rich_text(editor.value,escapes);
            for(const auto token:{LR"(\n)",LR"(\t)",LR"(\r)",LR"(\a)",LR"(\b)",LR"(\f)",LR"(\v)"}){
                check_range(token,0,2,string_ink);
            }
            check_range(LR"(\0)",0,2,string_ink);
            set_rich_text(editor.value,R"text(Dentro del literal, `\n` produce un salto. No escribas barras invertidas visibles ni letras n en lugar de los saltos. La variable `n` cuenta líneas.)text");
            check_range(LR"(\n produce)",0,2,string_ink);
            check_range(L"letras n en",7,1,base);
            check_range(L"variable n cuenta",9,1,identifier_ink);
            set_rich_text(editor.value,R"text(En el escape, la letra `n` representa el salto; la variable `n` permanece independiente. En `// comentario` el marcador pertenece al comentario, y en el literal `"// texto"` pertenece a la cadena.)text");
            check_range(L"la letra n representa",9,1,string_ink);
            check_range(L"variable n permanece",9,1,identifier_ink);
            const auto comment_ink=high_contrast?base:RGB(172,172,172);
            check_range(L"// comentario",0,std::wstring_view(L"// comentario").size(),comment_ink);
            check_range(LR"("// texto")",0,std::wstring_view(LR"("// texto")").size(),string_ink);
            const std::string isolated=R"text(Los símbolos \" y \\ son escapes técnicos; se leen por separado.)text";
            set_rich_text(editor.value,isolated);
            check(edit_text(editor.value)==isolated,"Isolated escaped symbols changed the original narration text");
            check_range(LR"(\")",0,2,string_ink);
            check_range(LR"(\\)",0,2,string_ink);
            check_range(L" son escapes técnicos",0,std::wstring_view(L" son escapes técnicos").size(),base);
            // This sentence occurs on page 3 of the authored Spanish lesson.
            // The escape pairs are green even when explained outside a code
            // block; all adjacent Spanish prose stays the ordinary text color.
            const std::string authored_escape_prose=R"text(Para incluir una comilla en el mensaje, escribimos \" dentro del literal. De manera semejante, \\ representa una sola barra inversa.)text";
            set_rich_text(editor.value,authored_escape_prose);
            check(edit_text(editor.value)==authored_escape_prose,"String escape highlighting changed authored lesson text");
            check_range(LR"(\")",0,2,string_ink);
            check_range(LR"(\\)",0,2,string_ink);
            check_range(L" dentro del literal.",0,std::wstring_view(L" dentro del literal.").size(),base);
            check_range(L" representa una sola barra inversa.",0,std::wstring_view(L" representa una sola barra inversa.").size(),base);
            // Complete C++ quoted literals retain their existing green ink.
            const std::string quoted_cpp=R"code(std::cout << "Nombre: \"Luna\"" << "Nivel\\Uno";)code";
            set_rich_text(editor.value,quoted_cpp,{},true);
            check_range(LR"("Nombre: \"Luna\"")",0,std::wstring_view(LR"("Nombre: \"Luna\"")").size(),string_ink);
            check_range(LR"("Nivel\\Uno")",0,std::wstring_view(LR"("Nivel\\Uno")").size(),string_ink);
            const std::string ordinary_quotes=R"text(En la ruta C:\\carpeta y en "una cita" no hay símbolos escapados aislados.)text";
            set_rich_text(editor.value,ordinary_quotes);
            check_range(wide(ordinary_quotes),0,wide(ordinary_quotes).size(),base);
            const std::string prose=R"text(La letra número, letra nombre, carácter nñ, carácter n2, carácter n_1; la n y una a. \nueva, \tarea, \z y alfa\n; superletra n y áletra n.)text";
            set_rich_text(editor.value,prose);check(edit_text(editor.value)==prose,"Unmarked homonyms changed their exact source");
            for(const auto token:{L"número",L"nombre",L"nñ",L"n_1;",L"la n y una a",L"superletra n",L"áletra n"})
                check_range(token,0,std::wstring_view(token).size(),base);
            const std::string statement=R"text(En std::cout << "letra n: \n";, la letra n: sigue.)text";
            const auto statement_string=declared_strings(statement,{R"("letra n: \n")"});
            set_rich_text(editor.value,statement,{},false,RichTextRole::body,"es",{},{},&statement_string);
            check_range(LR"("letra n: \n")",0,std::wstring_view(LR"("letra n: \n")").size(),string_ink);
            check_range(L"la letra n: sigue",9,1,base);check_range(L"la letra n: sigue",10,7,base);
            set_rich_text(editor.value,R"text(𐐀 `"letra n: \n"` y la letra n: sigue.)text");
            check_range(LR"("letra n: \n")",0,std::wstring_view(LR"("letra n: \n")").size(),string_ink);
            check_range(L"la letra n: sigue",9,1,base);check_range(L"la letra n: sigue",10,7,base);
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            const auto base=reader_theme::foreground(editor.value);
            const bool high_contrast=reader_theme::palette(editor.value).high_contrast;
            const auto identifier_ink=high_contrast?base:RGB(184,151,244);
            const auto string_ink=high_contrast?base:RGB(131,209,151);
            const auto check_token=[&](std::wstring_view token,COLORREF expected){
                const auto visible=wide(edit_text(editor.value));const auto begin=visible.find(token);
                check(begin!=visible.npos,"The authored contextual-color regression lost its original token");
                for(auto at=begin;at<begin+token.size();++at){
                    const auto actual=character(editor.value,static_cast<LONG>(at)).crTextColor;
                    if(actual!=expected)std::cerr<<"Contextual-color diagnostics: token="<<utf8(token)
                        <<", offset="<<at-begin<<", actual="<<actual<<", expected="<<expected
                        <<", source="<<utf8(visible)<<'\n';
                    check(actual==expected,"An authored C++ type or literal stayed gray in curriculum prose");
                }
            };
            // The unchanged authored question does not declare an inline role.
            const std::string constructor_question="¿Cuál declaración describe un constructor de Punto con dos coordenadas int?";
            set_rich_text(editor.value,constructor_question);
            check(edit_text(editor.value)==constructor_question,"Contextual type coloring rewrote the authored question");
            check_token(L"int",base);
            const auto type_at=wide(constructor_question).find(L"int?");
            const lgaed::curriculum::TextSemantics declared_type{constructor_question,{{type_at,type_at+3,"int",lgaed::curriculum::SemanticRole::cpp}}};
            set_rich_text(editor.value,constructor_question,{},false,RichTextRole::body,"es",{},{},&declared_type);
            check_token(L"int",identifier_ink);
            const std::string question=R"(En un literal válido, ¿qué representa \n?)";
            const std::string option="Los dos caracteres visibles barra invertida y n.";
            const auto check_option=[&](RichTextContext context,COLORREF expected){
                set_rich_text(editor.value,option,{},false,RichTextRole::body,"es",{},context);
                check(edit_text(editor.value)==option,"Question context rewrote the original distractor");
                check_token(L"n.",expected);
            };
            // A visible question cannot give an unmarked distractor's letter
            // a code role. The option keeps its own exact source and styling.
            set_rich_text(editor.value,option,{},false,RichTextRole::body,"es",{},RichTextContext{question,{}});
            const auto option_n=static_cast<LONG>(wide(option).find_last_of(L'n'));
            check(character(editor.value,option_n).crTextColor==base&&
                character(editor.value,option_n+1).crTextColor==base,
                "A visible question gave an unmarked distractor letter syntax ink");
            check(character(editor.value,option_n).yHeight==270&&
                std::wstring(character(editor.value,option_n).szFaceName)==L"Roboto",
                "Contextual escape ink changed the authored typography");
            check_option({},base);
            check_option({"La variable n cuenta elementos.","int n = 1;"},base);
            check_option({{},R"code(int n = 1; // "\n" belongs to a comment)code"},base);
            check_option({{},R"code(std::cout << "\\n";)code"},base);
            check_option({{},R"code(std::cout << R"(\n)";)code"},base);
            set_rich_text(editor.value,option,{},false,RichTextRole::body,"es",{},
                RichTextContext{{},R"code(std::cout << "\n";)code"});
            check(character(editor.value,option_n).crTextColor==base,
                "A visible C++ string supplied a role to an unmarked distractor letter");
            set_rich_text(editor.value,option,{},false,RichTextRole::body,"es",{},
                RichTextContext{{},R"code(std::cout << "Entrada\nSalida";)code"});
            check(character(editor.value,option_n).crTextColor==base,
                "A neighboring literal assigned its escape role to another unmarked field");
            const lgaed::curriculum::TextSemantics declared_letter{option,{{static_cast<std::size_t>(option_n),static_cast<std::size_t>(option_n)+1,"n",lgaed::curriculum::SemanticRole::string}}};
            set_rich_text(editor.value,option,{},false,RichTextRole::body,"es",{},RichTextContext{question,{}},&declared_letter);
            check(character(editor.value,option_n).crTextColor==string_ink&&character(editor.value,option_n+1).crTextColor==base,
                "An explicitly authored string character lost its palette or absorbed adjacent punctuation");
            set_rich_text(editor.value,"La superletra n y áletra n siguen siendo prosa.",{},false,RichTextRole::body,"es",{},RichTextContext{question,{}});
            check_token(L"superletra n",base);check_token(L"áletra n",base);
            const std::string homonyms="La salida normal y a siguen siendo prosa. La variable `n` cuenta elementos.";
            set_rich_text(editor.value,homonyms,{},false,RichTextRole::body,"es",{},
                RichTextContext{question,"int a = 1; bool normal = true; int n = 2;"});
            check_token(L"normal y a",base);
            check(character(editor.value,static_cast<LONG>(wide(edit_text(editor.value)).find(L"n cuenta"))).crTextColor==identifier_ink,
                "A variable inherited the unrelated escape's green ink");
            set_rich_text(editor.value,"La frase \"int y char\" es prosa; intento, intérprete, áint y charlatán son palabras completas.");
            check_token(L"int y char",base);check_token(L"intento",base);check_token(L"intérprete",base);check_token(L"áint",base);check_token(L"charlatán",base);
            set_rich_text(editor.value,R"(La fórmula \(int + char\) y https://ejemplo.test/int/char conservan su representación.)");
            check_token(L"int + char",base);check_token(L"https://ejemplo.test/int/char",base);
            // Existing U11L8 authored literals already use the supported inline delimiters.
            set_rich_text(editor.value,"Las etiquetas `'n'` y `'f'` se construyen dentro del programa.");
            check_token(L"'n'",string_ink);check_token(L"'f'",string_ink);
            set_rich_text(editor.value,"`char` e `int` conservan sus tipos; la categoría `'+'` es un carácter.");
            check_token(L"char",identifier_ink);check_token(L"int",identifier_ink);check_token(L"'+'",string_ink);
            // An unmarked initializer in prose does not identify its nouns.
            const std::string token_question="Con el búfer vacío se intenta devolver Token{'n', 14}. ¿Qué exige este contrato inicial?";
            set_rich_text(editor.value,token_question);
            check(edit_text(editor.value)==token_question,"List-initialization styling rewrote the original question");
            check_token(L"Token",base);
            const auto token_at=wide(token_question).find(L"Token{'n', 14}");
            const lgaed::curriculum::TextSemantics declared_initializer{token_question,{{token_at,token_at+14,"Token{'n', 14}",lgaed::curriculum::SemanticRole::cpp}}};
            set_rich_text(editor.value,token_question,{},false,RichTextRole::body,"es",{},{},&declared_initializer);
            check_token(L"Token",identifier_ink);check_token(L"'n'",string_ink);
            for(const auto prose:{"Un Token conserva normal, a e y como palabras.",
                "Se menciona Token{una frase y otra} en la explicación.",
                "Se menciona Token{valor} sin un argumento literal.",
                "Se menciona Token{'n', 14 sin cierre.",
                "Se menciona 1Token{'n', 14} sin identidad válida.",
                R"(La fórmula \(Token{'n', 14}\) se conserva.)",
                "Consulta https://ejemplo.test/Token{'n',14} y conserva el vínculo."}){
                set_rich_text(editor.value,prose,{},false,RichTextRole::body,"es",{},
                    RichTextContext{{},"struct Token { char clase; int valor; }; int normal; int a; int y;"});
                check_token(L"Token",base);
            }
            // The author declares the whole quoted value as string data;
            // its interior must not be reinterpreted as C++ identifiers.
            const std::string quoted_initializer="La frase \"Token{'n', 14}\" conserva su categoría de cadena.";
            const auto initializer_string=declared_strings(quoted_initializer,{R"("Token{'n', 14}")"});
            set_rich_text(editor.value,quoted_initializer,{},false,RichTextRole::body,"es",{},{},&initializer_string);
            check_token(L"Token",string_ink);
        }
        const std::string original = "#include <iostream>\n\nint main() {\n\tstd::cout << \"área  A\";  \n}\n";
        set_edit_text(editor.value, original);
        check(edit_text(editor.value) == original, "RichEdit did not preserve exact indentation, spaces or final LF");
        set_edit_text(editor.value, "{\\rtf1 This is C++ text, not RTF.}\n");
        check(edit_text(editor.value) == "{\\rtf1 This is C++ text, not RTF.}\n", "Code was interpreted as RTF");
        // Display-only code uses the original monospace face in production.
        // Do not measure a narrow, pixel-rounded space inherited from prose.
        set_rich_text(editor.value, original, {}, true);
        PARAFORMAT2 tabs{}; tabs.cbSize = sizeof(tabs); tabs.dwMask = PFM_TABSTOPS;
        tabs.cTabCount = 1; tabs.rgxTabs[0] = 720;
        SendMessageW(editor.value, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&tabs));
        SendMessageW(editor.value, EM_EMPTYUNDOBUFFER, 0, 0);
        CHARRANGE end{-1,-1}; SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
        SendMessageW(editor.value, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
        CHARRANGE before{3,8}; SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
        const auto modified = SendMessageW(editor.value, EM_GETMODIFY, 0, 0);
        style_cpp(editor.value);
        POINTL space_before{},next_before{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_before),8);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next_before),9);
        POINTL first_before{},second_before{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first_before),0);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second_before),1);
        const auto syntax_before=character(editor.value,0);POINT scroll_before{};SendMessageW(editor.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll_before));
        SendMessageW(editor.value,EM_SETREADONLY,TRUE,0);
        // Styling a readonly code card must preserve the same full-width
        // monospace advance as an editable card. Syntax is paint, not text.
        style_cpp(editor.value);
        check((GetWindowLongPtrW(editor.value,GWL_STYLE)&ES_READONLY)!=0,
            "Restyling a readonly code display left it editable");
        POINTL space_after{},next_after{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_after),8);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next_after),9);
        POINTL first_after{},second_after{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first_after),0);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second_after),1);
        POINT scroll_after{};SendMessageW(editor.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll_after));
        const auto normal_width=next_before.x-space_before.x,restyled_width=next_after.x-space_after.x;
        const auto space_format=character(editor.value,8);
        const bool preserved_geometry=normal_width>0&&restyled_width==normal_width&&
            second_before.x-first_before.x==second_after.x-first_after.x&&
            space_format.yHeight==syntax_before.yHeight&&first_after.y==first_before.y;
        if(!preserved_geometry)std::cerr<<"Monospace geometry: width="<<normal_width<<" -> "<<restyled_width
            <<", first advance="<<second_before.x-first_before.x<<" -> "<<second_after.x-first_after.x
            <<", y="<<first_before.y<<" -> "<<first_after.y<<", height="<<syntax_before.yHeight
            <<"/"<<space_format.yHeight<<", font="<<utf8(syntax_before.szFaceName)<<"/"<<utf8(space_format.szFaceName)
            <<", DPI="<<GetDpiForWindow(editor.value)<<'\n';
        check(preserved_geometry,
            "Restyling code changed actual monospace indentation width or character geometry");
        style_cpp(editor.value);
        POINTL space_again{},next_again{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_again),8);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next_again),9);
        check(next_again.x-space_again.x==normal_width&&character(editor.value,0).crTextColor==syntax_before.crTextColor&&
            character(editor.value,0).yHeight==syntax_before.yHeight&&scroll_after.x==scroll_before.x&&scroll_after.y==scroll_before.y,
            "Repeated code restyling changed geometry, syntax, font or scrolling");
        CHARRANGE after{}; SendMessageW(editor.value, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
        check(before.cpMin == after.cpMin && before.cpMax == after.cpMax, "Syntax formatting moved selection");
        check(SendMessageW(editor.value, EM_GETMODIFY, 0, 0) == modified, "Syntax formatting changed modified state");
        PARAFORMAT2 after_tabs{}; after_tabs.cbSize = sizeof(after_tabs);
        SendMessageW(editor.value, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&after_tabs));
        check(after_tabs.cTabCount == 1 && after_tabs.rgxTabs[0] == 720, "Syntax formatting replaced the editor's measured tab stops");
        check(edit_text(editor.value) == original + 'X', "Syntax formatting changed source text");
        SendMessageW(editor.value,EM_SETREADONLY,FALSE,0);
        check(SendMessageW(editor.value, EM_UNDO, 0, 0) != 0 && edit_text(editor.value) == original, "Syntax formatting entered the text undo history");
        check(SendMessageW(editor.value, EM_REDO, 0, 0) != 0 && edit_text(editor.value) == original + 'X', "Syntax formatting broke redo history");
        set_rich_text(editor.value, "El `std::cout` conserva su texto.", {}, false);
        check(edit_text(editor.value) == "El std::cout conserva su texto.", "Inline code delimiters were displayed or text was lost");
        ShowWindow(editor.value,SW_HIDE);
        style_cpp(editor.value);
        check(!(GetWindowLongPtrW(editor.value,GWL_STYLE) & WS_VISIBLE),"Syntax formatting revealed a hidden editor");
        set_rich_text(editor.value,"Un párrafo oculto.");
        check(!(GetWindowLongPtrW(editor.value,GWL_STYLE) & WS_VISIBLE),"Rich text formatting revealed a hidden paragraph");
        check(utf8(wide("Acentos áéíóú y carácter 𐐀")) == "Acentos áéíóú y carácter 𐐀", "UTF-8/UTF-16 conversion lost Unicode");
        {
            const auto colors = reader_theme::palette();
            HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
            const bool system_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
                && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
            check(colors.high_contrast == system_contrast, "Theme ignored Windows high contrast");
            if (system_contrast) {
                check(colors.canvas == GetSysColor(COLOR_WINDOW) && colors.text == GetSysColor(COLOR_WINDOWTEXT),
                    "High-contrast theme replaced the user's system colors");
            } else {
                check(colors.canvas == RGB(16,16,16) && colors.header == RGB(23,23,23)
                    && colors.card == RGB(28,28,28) && colors.field == RGB(37,37,37)
                    && colors.text == RGB(218,221,228) && colors.accent == RGB(89,240,252),
                    "Reader theme diverged from the Android reference presentation");
            }
            {
                reader_theme::prepare_fonts();const auto dc=GetDC(parent.value);
                std::array<LONG,2> actual_weights{};
                for(std::size_t index=0;index<actual_weights.size();++index){
                    const auto font=CreateFontW(-20,0,0,0,index?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
                    check(font!=nullptr,"Create the actual Android-equivalent font faces");const auto previous=SelectObject(dc,font);
                    TEXTMETRICW measured{};check(GetTextMetricsW(dc,&measured)!=FALSE,"Inspect the selected font's real weight");
                    actual_weights[index]=measured.tmWeight;SelectObject(dc,previous);DeleteObject(font);
                }
                // GetFontData proves which exact private font GDI selected,
                // rather than merely trusting the requested family name.
                for(const bool italic:{false,true}){
                    const auto font=CreateFontW(-24,0,0,0,FW_NORMAL,italic,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,reader_theme::math_font_face());
                    check(font!=nullptr,"Cannot create the original Android mathematical serif face");const auto previous=SelectObject(dc,font);
                    wchar_t face[LF_FACESIZE]{};check(GetTextFaceW(dc,LF_FACESIZE,face)>0&&std::wstring_view(face)==L"Noto Serif",
                        "Mathematical text silently fell back from the original Noto Serif family");
                    TEXTMETRICW metrics{};check(GetTextMetricsW(dc,&metrics)&&metrics.tmHeight-metrics.tmInternalLeading==24
                        &&static_cast<bool>(metrics.tmItalic)==italic,"Mathematical font changed its requested size or italic face");
                    const auto size=GetFontData(dc,0,0,nullptr,0);check(size==(italic?249748u:246740u),"Mathematical font does not have the reference Android font size");
                    std::vector<std::uint8_t> bytes(size);check(GetFontData(dc,0,0,bytes.data(),size)==size,"Cannot inspect actual selected mathematical font data");
                    check(lgaed::state::sha256(bytes)==(italic?"bb2ad30a8b95b367ed9576cea42bc085b7e69a6f81e6fe53ac3db6f10c7ff5f3":"1e65d9bbb6864fd4a32b96a8f0de3739b667c9eac793ec2d6d00bab7adb5a965"),
                        "The selected mathematical font is not byte-identical to the official OFL Android reference");
                    SelectObject(dc,previous);DeleteObject(font);
                }
                {
                    const auto font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,reader_theme::code_font_face());
                    check(font!=nullptr,"Cannot create Android's original monospace face");const auto previous=SelectObject(dc,font);
                    wchar_t face[LF_FACESIZE]{};check(GetTextFaceW(dc,LF_FACESIZE,face)>0&&std::wstring_view(face)==L"Droid Sans Mono",
                        "Technical text silently fell back from the original Droid Sans Mono family");
                    TEXTMETRICW metrics{};check(GetTextMetricsW(dc,&metrics)&&metrics.tmHeight-metrics.tmInternalLeading==24,
                        "The original monospace font changed its requested character size");
                    const auto size=GetFontData(dc,0,0,nullptr,0);check(size==108128u,"The selected code font has a different size from Android's reference");
                    std::vector<std::uint8_t> bytes(size);check(GetFontData(dc,0,0,bytes.data(),size)==size,"Cannot inspect the actual selected code font");
                    check(lgaed::state::sha256(bytes)=="db19a1fdaba41cc4a2fec0330e5c15e71c6dd68a3ef074f4f28268828b45c862",
                        "The selected code font is not byte-identical to the Apache-2.0 AOSP/LDPlayer reference");
                    std::array<SIZE,4> widths{};const std::wstring glyphs=L"iW 0";
                    for(std::size_t i=0;i<widths.size();++i)check(GetTextExtentPoint32W(dc,glyphs.data()+i,1,&widths[i])!=FALSE,"Cannot measure the code font's advances");
                    check(std::ranges::all_of(widths,[&](const auto& value){return value.cx==widths[0].cx;}),
                        "The original code font lost fixed-width glyph/space advances");
                    SelectObject(dc,previous);DeleteObject(font);
                }
                ReleaseDC(parent.value,dc);
                check(actual_weights[0]==FW_NORMAL&&actual_weights[1]>=FW_BOLD,
                    "A Windows Roboto registration resolves ordinary body text to the Bold face");
            }
            set_edit_text(editor.value, original);
            SendMessageW(editor.value, EM_EMPTYUNDOBUFFER, 0, 0);
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
            SendMessageW(editor.value, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
            const auto mask = SendMessageW(editor.value, EM_GETEVENTMASK, 0, 0);
            const auto previous_modify = SendMessageW(editor.value, EM_GETMODIFY, 0, 0);
            reader_theme::apply(editor.value, reader_theme::Surface::field);
            style_cpp(editor.value);
            SendMessageW(editor.value, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
            check(before.cpMin == after.cpMin && before.cpMax == after.cpMax,
                "Local theme/syntax moved the code selection");
            check(mask == SendMessageW(editor.value, EM_GETEVENTMASK, 0, 0)
                && previous_modify == SendMessageW(editor.value, EM_GETMODIFY, 0, 0),
                "Local theme/syntax changed the editor event or modified state");
            check(reader_theme::background(editor.value) == colors.field && edit_text(editor.value) == original + 'X',
                "Local field theme changed the original code");
            check(character(editor.value, 0).crTextColor == (system_contrast ? colors.text : RGB(92,156,231)),
                "Themed syntax lost the Android preprocessor color or system contrast");
            const auto quote = static_cast<LONG>(wide(original).find(L'"'));
            check(character(editor.value, quote).crTextColor == (system_contrast ? colors.text : RGB(131,209,151)),
                "Themed syntax lost the original string color");
            check(character(editor.value, static_cast<LONG>(wide(original).find(L'}'))).crTextColor ==
                (system_contrast ? colors.text : RGB(245,245,245)),
                "Themed syntax left uncolored punctuation unreadable");
            // These are the reference code-block inks reported by the user.
            // Verify the actual RichEdit colors without altering its theme fill.
            Window syntax_sample{CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                WS_CHILD | ES_MULTILINE | ES_READONLY,
                0,0,700,180,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(syntax_sample.value!=nullptr,"Create native C++ syntax reference fixture");
            reader_theme::apply(syntax_sample.value,reader_theme::Surface::field);
            const auto source_ink=std::string("int equipos{3};\nfor (int equipo{1}; equipo<=equipos; ++equipo) {\n")
                +"    std::cout << \"Revision=\" << equipo << '\\n';\n}\n";
            set_edit_text(syntax_sample.value,source_ink);
            style_cpp(syntax_sample.value);
            const auto sample=wide(edit_text(syntax_sample.value));
            const auto validate_ink=[&](std::wstring_view token,COLORREF intended) {
                const auto at=sample.find(token);
                check(at!=sample.npos,"C++ reference fixture lost its sampled token");
                check(character(syntax_sample.value,static_cast<LONG>(at)).crTextColor==
                    (system_contrast?colors.text:intended),
                    "The editor C++ token differs from the reference color");
            };
            validate_ink(L"equipos",RGB(184,151,244));
            validate_ink(L"for",RGB(248,166,200));
            validate_ink(L"3",RGB(241,162,117));
            validate_ink(L"\"Revision=\"",RGB(131,209,151));
            validate_ink(L"'\\n'",RGB(131,209,151));
            validate_ink(L"{",RGB(245,245,245));
            check(reader_theme::background(syntax_sample.value)==colors.field,
                "Syntax recoloring changed the existing editor background");
            check(SendMessageW(editor.value, EM_UNDO, 0, 0) != 0 && edit_text(editor.value) == original,
                "Theming entered the native Undo history");
            check(SendMessageW(editor.value, EM_REDO, 0, 0) != 0 && edit_text(editor.value) == original + 'X',
                "Theming broke native Redo");
            const auto technical = static_cast<LONG>(wide(original).find(L"std"));
            CHARRANGE underlined{technical,technical + 3};
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&underlined));
            CHARFORMAT2W underline{}; underline.cbSize = sizeof(underline);
            underline.dwMask = CFM_UNDERLINE; underline.dwEffects = CFE_UNDERLINE;
            SendMessageW(editor.value, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&underline));
            style_cpp(editor.value);
            check((character(editor.value,technical).dwEffects & CFE_UNDERLINE) != 0,
                "Themed syntax removed an existing underline span");

            Window text{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | ES_READONLY,
                0, 0, 500, 120, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(text.value != nullptr, "Cannot create themed prose control");
            reader_theme::apply(text.value, reader_theme::Surface::canvas);
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"valor",0,"bold_italic"}}};
            set_rich_text(text.value, "Álgebra con `std::cout` y valor.", emphasis);
            check(edit_text(text.value) == "Álgebra con std::cout y valor.", "Theming altered Unicode or displayed inline delimiters");
            check(character(text.value, 0).crTextColor == colors.text, "Themed prose kept an unreadable system foreground");
            const auto prose = wide(edit_text(text.value));
            check(std::wstring(character(text.value, static_cast<LONG>(prose.find(L"std"))).szFaceName) == reader_theme::code_font_face(),
                "Theming removed the technical fragment's monospace span");
            const auto value_style = character(text.value, static_cast<LONG>(prose.find(L"valor")));
            check((value_style.dwEffects & (CFE_BOLD | CFE_ITALIC)) == (CFE_BOLD | CFE_ITALIC),
                "Theming erased authored bold/italic emphasis");

            Window untouched{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE,
                0, 0, 200, 80, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(untouched.value != nullptr, "Cannot create original palette control");
            set_rich_text(untouched.value, "Original system presentation");
            check(!reader_theme::applied(untouched.value)
                && character(untouched.value, 0).crTextColor == GetSysColor(COLOR_WINDOWTEXT),
                "A themed HWND changed unrelated controls");
            const auto dc = GetDC(parent.value); LRESULT result = 0;
            check(!reader_theme::control_color(WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(untouched.value), result), "Theme intercepted an unrelated control");
            check(reader_theme::control_color(WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(editor.value), result)
                && result == reinterpret_cast<LRESULT>(reader_theme::brush(reader_theme::Surface::field)),
                "Themed field did not use its local brush");
            ReleaseDC(parent.value, dc);

            for(const auto& [surface,expected]:std::array<std::pair<reader_theme::Surface,COLORREF>,3>{{
                {reader_theme::Surface::compiler_header,colors.high_contrast?colors.header:RGB(37,40,46)},
                {reader_theme::Surface::compiler_editor,colors.high_contrast?colors.field:RGB(28,31,36)},
                {reader_theme::Surface::compiler_output,colors.high_contrast?colors.field:RGB(5,6,7)}}}){
                reader_theme::apply(untouched.value,surface);
                check(reader_theme::background(untouched.value)==expected,"Compiler surface lost its exact Android fill or high-contrast fallback");
                LOGBRUSH brush{};check(GetObjectW(reader_theme::brush(surface),sizeof(brush),&brush)==static_cast<int>(sizeof(brush))
                    &&brush.lbColor==expected,"Compiler surface brush uses a stale surface index");
            }

            Window button{CreateWindowExW(0, L"BUTTON", L"&Comprobar", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 130, 36, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            Window radio{CreateWindowExW(0, L"BUTTON", L"Una opción", WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON,
                0, 40, 130, 36, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(button.value && radio.value, "Cannot create real themed buttons");
            const auto focus = GetFocus();
            const auto radio_dialog_code = SendMessageW(radio.value,WM_GETDLGCODE,0,0);
            reader_theme::apply(button.value, reader_theme::Surface::card);
            reader_theme::apply(radio.value, reader_theme::Surface::card);
            check((GetWindowLongPtrW(button.value,GWL_STYLE) & (BS_TYPEMASK | WS_TABSTOP)) == (BS_OWNERDRAW | WS_TABSTOP),
                "Theming lost button Tab navigation or failed to draw the push button");
            check((GetWindowLongPtrW(radio.value,GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON,
                "Theming converted a radio button into a push button");
            check(SendMessageW(radio.value,WM_GETDLGCODE,0,0) == radio_dialog_code
                && (radio_dialog_code & DLGC_RADIOBUTTON) != 0,
                "Radio theming changed native dialog-key handling or its role");
            wchar_t caption[64]{}; GetWindowTextW(button.value,caption,64);
            check(std::wstring(caption) == L"&Comprobar" && GetFocus() == focus, "Theming changed button text or moved keyboard focus");
            const auto screen = GetDC(parent.value); const auto memory = CreateCompatibleDC(screen);
            const auto bitmap = CreateCompatibleBitmap(screen,130,36); const auto previous_bitmap = SelectObject(memory,bitmap);
            DRAWITEMSTRUCT item{}; item.CtlType = ODT_BUTTON; item.hwndItem = button.value;
            item.hDC = memory; item.rcItem = {0,0,130,36};
            for (const bool primary : {false,true}) {
                reader_theme::primary_button(button.value,primary);
                for (const UINT state : std::array<UINT,5>{0,ODS_SELECTED,ODS_DISABLED,ODS_FOCUS,ODS_FOCUS | ODS_NOFOCUSRECT}) {
                    item.itemState = state;
                    check(reader_theme::draw_button(item), "Themed button could not draw a normal, pressed, disabled or focused state");
                }
            }
            if(!colors.high_contrast){
                reader_theme::icon_button(button.value);reader_theme::apply(button.value,reader_theme::Surface::header);item.itemState=ODS_FOCUS;
                check(reader_theme::draw_button(item)&&GetPixel(memory,1,18)==colors.header&&GetPixel(memory,1,1)==colors.header,
                    "A header icon still paints a canvas-colored rectangular tile over its own surface");
                reader_theme::apply(button.value,reader_theme::Surface::card);
                SetWindowTextW(button.value,L"");reader_theme::icon_button(button.value,false);
                reader_theme::premium_button(button.value);item.itemState=0;
                check(reader_theme::draw_button(item),"The Premium action could not be rendered");
                const auto first=GetPixel(memory,16,18),middle=GetPixel(memory,65,18),last=GetPixel(memory,114,18);
                check(first!=middle&&middle!=last&&first!=last&&GetRValue(middle)>GetRValue(first)&&GetRValue(middle)>GetRValue(last),
                    "Premium lost Android's true three-stop gold gradient");
                item.itemState=ODS_DISABLED;check(reader_theme::draw_button(item)&&GetPixel(memory,16,18)==first
                    &&GetPixel(memory,65,18)==middle&&GetPixel(memory,114,18)==last,
                    "The non-clickable Premium activated status lost its Android gold drawable");
                reader_theme::premium_button(button.value,false);SetWindowTextW(button.value,L"&Comprobar");
            }
            item.hwndItem = radio.value;
            check(!reader_theme::draw_button(item), "Theme intercepted native radio-button drawing");
            const auto radio_dpi = GetDpiForWindow(radio.value);
            const auto glyph_left = MulDiv(3,static_cast<int>(radio_dpi ? radio_dpi : 96),96);
            const auto glyph_diameter = std::min(MulDiv(20,static_cast<int>(radio_dpi ? radio_dpi : 96),96),36-glyph_left*2);
            const auto glyph_center_x = glyph_left + glyph_diameter / 2;
            const auto glyph_center_y = 36 / 2;
            const auto raster = [&] {
                SendMessageW(radio.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            };
            const auto glyph_pixels = [&](COLORREF ink) {
                unsigned count = 0;
                for (int y = 0; y < 36; ++y)
                    for (int x = 0; x < std::min(130,glyph_left + glyph_diameter + 2); ++x)
                        if (GetPixel(memory,x,y) == ink) ++count;
                return count;
            };
            SendMessageW(radio.value,BM_SETCHECK,BST_UNCHECKED,0); raster();
            const auto unchecked_ink=colors.high_contrast?colors.muted:RGB(120,124,134);
            if(glyph_pixels(unchecked_ink)<24||GetPixel(memory,glyph_center_x,glyph_center_y)!=colors.card)
                std::cerr<<"Radio paint diagnostic: dpi="<<radio_dpi<<" diameter="<<glyph_diameter<<" left="<<glyph_left
                    <<" center=("<<glyph_center_x<<','<<glyph_center_y<<") actual="<<GetPixel(memory,glyph_center_x,glyph_center_y)
                    <<" background="<<colors.card<<" exact-edge="<<glyph_pixels(unchecked_ink)<<'\n';
            check(glyph_pixels(unchecked_ink) >= 24 && GetPixel(memory,glyph_center_x,glyph_center_y) == colors.card,
                "Unchecked radio did not draw the Android 20-DIP outline and empty center");
            if(!colors.high_contrast){unsigned blended=0;
                for(int y=0;y<36;++y)for(int x=0;x<glyph_left+glyph_diameter;++x){const auto value=GetPixel(memory,x,y);
                    if(value!=colors.card&&value!=unchecked_ink)++blended;}
                check(blended>=12,"The radio outline is still a jagged, unfiltered pixel circle");}
            unsigned caption_pixels = 0;
            for (int y = 0; y < 36; ++y) for (int x = glyph_left + glyph_diameter + 4; x < 130; ++x)
                if (GetPixel(memory,x,y) != colors.card) ++caption_pixels;
            check(caption_pixels >= 12,"Radio theming hid the original native caption");
            SendMessageW(radio.value,BM_SETCHECK,BST_CHECKED,0); raster();
            check(SendMessageW(radio.value,BM_GETCHECK,0,0) == BST_CHECKED
                && GetPixel(memory,glyph_center_x,glyph_center_y) == colors.accent
                && glyph_pixels(colors.accent) >= 32,"Selected radio did not preserve native state and draw its accent center");
            EnableWindow(radio.value,FALSE); raster();
            const auto disabled_ink = colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.accent;
            check(GetPixel(memory,glyph_center_x,glyph_center_y) == disabled_ink,
                "A correct/disabled selected radio lost its route accent instead of matching Android");
            EnableWindow(radio.value,TRUE);
            SendMessageW(radio.value,BM_SETCHECK,BST_CHECKED,0);
            check(SendMessageW(radio.value,BM_GETCHECK,0,0) == BST_CHECKED,
                "Radio theming broke native checked-state persistence");
            GetWindowTextW(radio.value,caption,64);
            check(std::wstring(caption) == L"Una opción"
                && (GetWindowLongPtrW(radio.value,GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON,
                "Radio drawing changed the original caption or native control type");
            Window checkbox{CreateWindowExW(0,L"BUTTON",L"Activar lectura",WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
                0,80,130,36,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(checkbox.value != nullptr,"Cannot create a real automatic checkbox");
            const auto checkbox_dialog=SendMessageW(checkbox.value,WM_GETDLGCODE,0,0);reader_theme::apply(checkbox.value,reader_theme::Surface::card);
            SendMessageW(checkbox.value,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(checkbox.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            check(GetPixel(memory,glyph_center_x,glyph_center_y)==colors.card&&glyph_pixels(colors.high_contrast?colors.muted:colors.accent)>=24,
                "The native checkbox has no empty, clearly drawn square glyph");
            SendMessageW(checkbox.value,BM_CLICK,0,0);SendMessageW(checkbox.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            check(SendMessageW(checkbox.value,BM_GETCHECK,0,0)==BST_CHECKED&&glyph_pixels(colors.accent)>=32,
                "The checkbox lost native automatic state or its visible checked square");
            check((GetWindowLongPtrW(checkbox.value,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX
                && SendMessageW(checkbox.value,WM_GETDLGCODE,0,0)==checkbox_dialog,"Checkbox painting changed its native type or keyboard dialog behavior");
            GetWindowTextW(checkbox.value,caption,64);check(std::wstring(caption)==L"Activar lectura","Checkbox painting replaced its accessible native caption");
            SelectObject(memory,previous_bitmap); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(parent.value,screen);
        }
        {
            lgaed::state::StateData preferences;
            check(reader_theme::appearance_accent(preferences) == RGB(89,240,252)
                && reader_theme::appearance_accent(preferences,"programming") == RGB(228,50,72)
                && reader_theme::appearance_accent(preferences,"math_physics") == RGB(108,209,92),
                "Appearance defaults differ from the original Android repository");
            auto& appearance = preferences.preferences["appearance_settings"];
            appearance["accent_red"] = std::int64_t{10}; appearance["accent_green"] = std::int64_t{20}; appearance["accent_blue"] = std::int64_t{30};
            appearance["route_accent_green_programming"] = std::int64_t{88};
            const auto unchanged = preferences;
            check(reader_theme::appearance_accent(preferences) == RGB(10,20,30)
                && reader_theme::appearance_accent(preferences,"programming") == RGB(228,88,72)
                && reader_theme::appearance_accent(preferences,"math_physics") == RGB(108,209,92)
                && reader_theme::appearance_accent(preferences,"other") == RGB(10,20,30) && preferences == unchanged,
                "Reading local appearance lost original per-component overrides, changed another route or mutated preferences");
            Window branch{CreateWindowExW(0,L"STATIC",L"Scoped theme QA",WS_CHILD,0,0,500,200,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            Window formula{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | ES_MULTILINE,0,0,450,160,branch.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            Window other{CreateWindowExW(0,L"STATIC",L"Unrelated theme QA",WS_OVERLAPPEDWINDOW,0,0,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(branch.value && formula.value && other.value,"Cannot create isolated per-window theme controls");
            reader_theme::apply(formula.value,reader_theme::Surface::card);
            const std::string source="Negrita con `std::cout` y 6 × 100 = 600.";
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"Negrita",0,"bold_italic"}}};
            set_rich_text(formula.value,source,emphasis); const auto displayed = edit_text(formula.value);
            std::vector<CHARFORMAT2W> original_ink;
            for (std::size_t index=0;index<wide(displayed).size();++index) original_ink.push_back(character(formula.value,static_cast<LONG>(index)));
            const auto events=SendMessageW(formula.value,EM_GETEVENTMASK,0,0),original_modified=SendMessageW(formula.value,EM_GETMODIFY,0,0);
            reader_theme::set_accent(parent.value,reader_theme::appearance_accent(preferences,"programming"));
            reader_theme::set_accent(branch.value,reader_theme::appearance_accent(preferences,"math_physics"));
            const auto system = reader_theme::palette();
            check(reader_theme::palette(editor.value).accent == (system.high_contrast ? system.accent : RGB(228,88,72))
                && reader_theme::palette(formula.value).accent == (system.high_contrast ? system.accent : RGB(108,209,92))
                && reader_theme::palette(other.value).accent == system.accent && reader_theme::palette().accent == system.accent,
                "A local route accent leaked globally, ignored its nearest parent or replaced high-contrast system colors");
            reader_theme::set_accent(branch.value,RGB(0,0,0));
            check(reader_theme::palette(formula.value).accent == (system.high_contrast ? system.accent : RGB(0,0,0)),"A valid black accent was confused with an absent property");
            check(reader_theme::accent_foreground(formula.value)==(system.high_contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255)),"A black primary accent lost Android's white enabled Button foreground");
            for(const auto surface:{system.canvas,system.header,system.card,system.field})
                check(reader_theme::accent_ink(formula.value,surface)==system.text,"A black accent disappeared as icon/text ink on a real themed surface");
            check(reader_theme::palette(formula.value).accent==(system.high_contrast?system.accent:RGB(0,0,0))&&preferences==unchanged,
                "Contrast-safe ink rewrote a valid configured RGB or stored preference");
            for(const auto normal:{RGB(89,240,252),RGB(228,50,72),RGB(108,209,92)}){
                reader_theme::set_accent(branch.value,normal);
                for(const auto surface:{system.canvas,system.header,system.card,system.field})
                    check(reader_theme::accent_ink(formula.value,surface)==(system.high_contrast?GetSysColor(COLOR_WINDOWTEXT):normal),
                        "Contrast-safe accent ink changed an original Android accent or ignored actual system high contrast");
            }
            reader_theme::set_accent(branch.value,RGB(255,255,255));
            check(reader_theme::accent_foreground(formula.value)==(system.high_contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(0,0,0)),"A white primary accent lost Android's black enabled Button foreground");
            reader_theme::set_accent(branch.value,std::nullopt);
            check(reader_theme::palette(formula.value).accent == (system.high_contrast ? system.accent : RGB(228,88,72)),"Removing a local accent did not restore parent inheritance");
            for (std::size_t index=0;index<original_ink.size();++index) {
                const auto current_format=character(formula.value,static_cast<LONG>(index));const auto& original_format=original_ink[index];
                check(original_format.crTextColor == current_format.crTextColor && original_format.dwEffects == current_format.dwEffects && original_format.yHeight == current_format.yHeight
                    && std::wstring_view(original_format.szFaceName) == current_format.szFaceName,"Changing a route accent recolored syntax/formulas or altered an original emphasis span");
            }
            check(edit_text(formula.value) == displayed && events == SendMessageW(formula.value,EM_GETEVENTMASK,0,0)
                && original_modified == SendMessageW(formula.value,EM_GETMODIFY,0,0),"Local appearance changed original text, edit events or modified state");
            reader_theme::set_accent(parent.value,std::nullopt);
        }
        std::cout << "Real RichEdit: exact source/LF, RTF safety, selection/Undo/Redo, inline spans, local Android theme and native contrast/buttons: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; FreeLibrary(module); return 1;
    }
    FreeLibrary(module); return 0;
}

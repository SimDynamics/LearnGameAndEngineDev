#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::string source(std::size_t lines){std::string result;for(std::size_t i=1;i<=lines;++i){if(i>1)result+='\n';result+="int line_"+std::to_string(i)+" = "+std::to_string(i)+";";}return result;}
struct Fixture{
    HWND host=nullptr,edit=nullptr;HFONT font=nullptr;std::unique_ptr<CodeEditor> editor;
    Fixture(std::string text,bool multiline=true,bool assistance=true,int width=500,bool wrap=false){
        host=CreateWindowExW(0,L"STATIC",L"Native code gutter QA",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create gutter QA host");
        const DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|(multiline?ES_MULTILINE|WS_VSCROLL|ES_AUTOVSCROLL|ES_WANTRETURN:0)|(wrap?0:ES_AUTOHSCROLL);
        edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",style,0,0,width,220,host,nullptr,GetModuleHandleW(nullptr),nullptr);check(edit!=nullptr,"Cannot create native RichEdit");
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        presentation::reader_theme::apply(edit,presentation::reader_theme::Surface::field);
        editor=std::make_unique<CodeEditor>(edit,predefined_code_document(text),multiline,std::function<void()>{},assistance);
        if(wrap)SendMessageW(edit,EM_SETTARGETDEVICE,0,0);
    }
    ~Fixture(){editor.reset();if(host)DestroyWindow(host);if(font)DeleteObject(font);}
};
struct Raster{
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;int width=0,height=0;
    explicit Raster(HWND target){RECT rect{};GetClientRect(target,&rect);width=rect.right;height=rect.bottom;const auto screen=GetDC(target);
        dc=CreateCompatibleDC(screen);bitmap=CreateCompatibleBitmap(screen,width,height);ReleaseDC(target,screen);check(dc&&bitmap,"Cannot create gutter raster");
        previous=SelectObject(dc,bitmap);SendMessageW(target,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);}
    ~Raster(){if(dc&&previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
};
void aligned(HWND target,const CodeGutterSnapshot& gutter){
    check(gutter.active&&!gutter.visible.empty(),"Native gutter has no visible paragraph numbers");
    std::size_t previous=0;
    for(const auto& line:gutter.visible){POINTL point{};SendMessageW(target,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),line.character);
        check(point.y==line.text_position.y&&point.x==line.text_position.x,"Number coordinates diverged from their actual text start");
        check(line.number>previous,"Wrapped display lines created duplicate paragraph numbers");previous=line.number;}
}
}
int main(){std::cout<<std::unitbuf;const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);int passed=0,failed=0;
    const auto test=[&](const char* name,const std::function<void()>& action){try{action();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("Gutter applies only to assisted multiline code",[]{Fixture code("int main() {}"),single("std::cout",false),input("A\n B",true,false);
        check(code_gutter_snapshot(code.edit).active,"C++ multiline editor omitted its gutter");
        check(!code_gutter_snapshot(single.edit).active&&!code_gutter_snapshot(input.edit).active,"Single-line response or stdin acquired line numbers");});
    test("Width grows for 100+ lines without wrapping or changing source",[]{Fixture field(source(9));const auto initial=code_gutter_snapshot(field.edit);
        const auto text=source(130);field.editor->reset(predefined_code_document(text));const auto gutter=code_gutter_snapshot(field.edit);
        check(gutter.logical_lines==130&&gutter.width>initial.width,"Three-digit numbers did not receive a wider native margin");
        check(field.editor->document().text==text,"Margin update changed source characters");aligned(field.edit,gutter);
        RECT format{};SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));check(format.left>=gutter.width,"Native text was not kept outside the gutter");});
    test("Actual paragraph starts remain aligned through vertical scrolling",[]{Fixture field(source(130));SendMessageW(field.edit,EM_LINESCROLL,0,95);
        aligned(field.edit,code_gutter_snapshot(field.edit));
        check(SendMessageW(field.edit,EM_GETFIRSTVISIBLELINE,0,0)>0,"Native relative scrolling did not move the editor");
        // RichEdit's relative line-scroll step can differ from the actual formatted
        // paragraph height. Locate paragraph 100 through its real native position,
        // rather than assuming that requesting 95 steps makes it visible.
        const auto document=field.editor->document();POINT scroll{},position{};RECT area{};
        SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));GetClientRect(field.edit,&area);
        const auto character=SendMessageW(field.edit,EM_LINEINDEX,99,0);check(character>=0,"Native paragraph 100 is missing");
        SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&position),character);
        POINT destination{scroll.x,std::max(0L,scroll.y+position.y-area.bottom/4)};
        SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&destination));
        const auto gutter=code_gutter_snapshot(field.edit);aligned(field.edit,gutter);
        const auto hundred=std::ranges::find(gutter.visible,std::size_t{100},&CodeGutterLine::number);
        check(hundred!=gutter.visible.end(),"Real scroll did not expose paragraph 100");
        const auto after=field.editor->document();check(after.text==document.text&&after.selection_start==document.selection_start&&after.selection_end==document.selection_end,
            "Scrolling the real gutter changed source or cursor selection");
        Raster pixels(field.edit);const auto background=presentation::reader_theme::background(field.edit);unsigned ink=0;
        for(int y=std::max(0L,hundred->text_position.y);y<std::min(pixels.height,static_cast<int>(hundred->text_position.y)+17);++y)
            for(int x=0;x<gutter.width;++x)if(GetPixel(pixels.dc,x,y)!=background)++ink;
        check(ink>10,"Three-digit line number was not drawn in its real text row");
        const auto old=SelectObject(pixels.dc,field.font);RECT measured{0,0,0,0};DrawTextW(pixels.dc,L"130",3,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);SelectObject(pixels.dc,old);
        check(measured.right+12<=gutter.width,"Three-digit number can be clipped or split in the reserved margin");});
    test("Soft wrapping numbers each source paragraph exactly once",[]{Fixture field("// "+std::string(72,'x')+"\nint value = 2;",true,true,180,true);
        check(SendMessageW(field.edit,EM_GETLINECOUNT,0,0)>2,"Native fixture did not actually soft-wrap");
        auto gutter=code_gutter_snapshot(field.edit);check(gutter.logical_lines==2,"Soft wrap was mistaken for source lines");aligned(field.edit,gutter);
        SendMessageW(field.edit,EM_LINESCROLL,0,2);gutter=code_gutter_snapshot(field.edit);aligned(field.edit,gutter);
        check(gutter.logical_lines==2,"Scrolling changed the number of source paragraphs");});
    test("Painting preserves exact CRLF tabs selection indentation and undo",[]{const std::string original="int main() {\r\n\t    return 0;\r\n}\r\n";Fixture field(original);
        CHARRANGE selection{2,6};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));const auto before=field.editor->document();
        set_code_gutter_appearance(field.edit,RGB(24,26,31),RGB(111,119,130),3);
        {Raster pixels(field.edit);const auto gutter=code_gutter_snapshot(field.edit);const auto colors=presentation::reader_theme::palette();
            const auto expected_background=colors.high_contrast?presentation::reader_theme::background(field.edit):RGB(24,26,31);
            check(GetPixel(pixels.dc,0,1)==expected_background,"The explicit laboratory gutter color was not painted, or replaced system high contrast");
            const auto expected_foreground=colors.high_contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(111,119,130);unsigned ink=0;
            for(int y=0;y<pixels.height;++y)for(int x=0;x<gutter.width;++x)if(GetPixel(pixels.dc,x,y)==expected_foreground)++ink;
            check(ink>0,"The explicit laboratory line-number ink was not rendered");}
        const auto after=field.editor->document();check(after.text==before.text&&after.indentation_starts==before.indentation_starts&&after.selection_start==before.selection_start&&after.selection_end==before.selection_end,
            "Gutter painting changed source, indentation identity or selection");
        const LONG length=GetWindowTextLengthW(field.edit);CHARRANGE end{length,length};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));SendMessageW(field.edit,WM_CHAR,L'x',1);
        const auto edited=field.editor->document();check(edited.text!=before.text,"Real editor insertion did not execute");
        {Raster pixels(field.edit);}field.editor->undo();check(field.editor->document().text==original,"Gutter formatting polluted undo or normalized the original source");
        field.editor->redo();check(field.editor->document().text==edited.text,"Gutter painting damaged redo");});
    test("Native text zoom scales gutter metrics without changing paragraph identities",[]{Fixture field(source(130));
        const auto before=field.editor->document();SendMessageW(field.edit,EM_SETZOOM,75,100);const auto reduced=code_gutter_snapshot(field.edit);
        SendMessageW(field.edit,EM_SETZOOM,150,100);const auto large=code_gutter_snapshot(field.edit);
        check(large.width>reduced.width&&large.logical_lines==reduced.logical_lines&&large.logical_lines==130,"Gutter did not measure actual RichEdit zoom");
        aligned(field.edit,large);const auto after=field.editor->document();
        check(before.text==after.text&&before.indentation_starts==after.indentation_starts&&before.selection_start==after.selection_start&&before.selection_end==after.selection_end,
            "Text zoom changed source, tab origin or selection");});
    FreeLibrary(rich);std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

#include "presentation/windows/curricular_image_view.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
struct Window {HWND value{};~Window(){if(IsWindow(value))DestroyWindow(value);}};
HWND descendant(HWND parent,int id){struct Search {int id;HWND found{};} search{id};EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{
    auto& found=*reinterpret_cast<Search*>(raw);if(GetDlgCtrlID(item)==found.id){found.found=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.found;}
CHARFORMAT2W character(HWND item,std::size_t at){CHARRANGE range{static_cast<LONG>(at),static_cast<LONG>(at+1)};SendMessageW(item,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W value{};value.cbSize=sizeof(value);SendMessageW(item,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&value));return value;}
void write(const std::filesystem::path& path,std::string_view text){std::ofstream file(path,std::ios::binary);file.write(text.data(),static_cast<std::streamsize>(text.size()));check(bool(file),"Cannot write the isolated image fixture");}
void encode(const std::filesystem::path& path,const WCHAR* mime){UINT count{},size{};check(Gdiplus::GetImageEncodersSize(&count,&size)==Gdiplus::Ok,"Cannot enumerate QA image encoders");
    std::vector<BYTE> data(size);auto* encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(data.data());check(Gdiplus::GetImageEncoders(count,size,encoders)==Gdiplus::Ok,"Cannot inspect QA image encoders");
    const auto codec=std::find_if(encoders,encoders+count,[&](const auto& value){return wcscmp(value.MimeType,mime)==0;});check(codec!=encoders+count,"Missing the standard QA image encoder");
    Gdiplus::Bitmap bitmap(320,160,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color(255,20,140,210));
    Gdiplus::SolidBrush marker(Gdiplus::Color(255,240,90,30));graphics.FillRectangle(&marker,80,40,80,40);
    check(bitmap.Save(path.c_str(),&codec->Clsid,nullptr)==Gdiplus::Ok,"Cannot encode the synthetic QA pattern");
}
void contracts(){using curriculum::ImageResource;
    curriculum::validate_image_resource({"qa_pattern","images/qa_pattern.PNG","Descripción",{}});
    for(const auto path:{"../images/qa_pattern.png","images/../qa_pattern.png","images//qa_pattern.png","C:/qa_pattern.png", "images/aux/qa_pattern.png",
        "images/COM¹/qa_pattern.png","images/conin$/qa_pattern.png","images/x./qa_pattern.png","images/x /qa_pattern.png","images/qa_pattern.png:stream","images/other.png","images/qa_pattern.svg"}){
        bool rejected=false;try{curriculum::validate_image_resource({"qa_pattern",path,"Descripción",{}});}catch(...){rejected=true;}check(rejected,"An invalid image contract was accepted");}
    bool rejected=false;try{curriculum::validate_image_resource({"qa_pattern","images/qa_pattern.png","\xc2\xa0\xe3\x80\x80",{}});}catch(...){rejected=true;}
    check(rejected,"Unicode whitespace was accepted as image alternative text");
    ImageViewport view;view.layout(3840,2160,960,600);check(std::abs(view.scale-.25)<1e-9,"Image fitting changed its aspect ratio");
    view.zoom(1,3840,2160,960,600);check(!view.fit&&view.scale==1&&view.x>=0&&view.y>=0,"Actual-size detail did not preserve bounded offsets");
    view.x=999999;view.y=-999;view.layout(3840,2160,960,600);check(view.x==2880&&view.y==0,"Panning escaped decoded image bounds");
    view.fit=true;view.layout(320,160,500,500);check(std::abs(view.scale-1.5625)<1e-9&&view.x==0&&view.y==0,"Returning to fit retained a stale crop");
}
}
int main(int argc,char** argv)try{
    const auto ole=OleInitialize(nullptr);check(SUCCEEDED(ole),"Cannot initialize hidden image QA");const auto edit_module=LoadLibraryW(L"Msftedit.dll");check(edit_module,"Cannot load hidden text controls");
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);reader_theme::prepare_fonts();
    ULONG_PTR graphics_token{};Gdiplus::GdiplusStartupInput graphics_input;check(Gdiplus::GdiplusStartup(&graphics_token,&graphics_input,nullptr)==Gdiplus::Ok,"Cannot initialize QA pattern encoder");
    if(argc==3&&std::string_view(argv[1])=="--physical-root"){
        Window host{CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden physical resource QA",WS_OVERLAPPEDWINDOW,0,0,500,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
        check(host.value,"Cannot create hidden physical resource host");
        CurricularImageView rejected(host.value,700,std::filesystem::path(wide(argv[2])),{"qa_pattern","images/escape/qa_pattern.png","QA outside-content probe",{}});
        check(!rejected.loaded()&&rejected.error().find("fuera de la raíz")!=std::string::npos,"A junction escaped the actual content root before decoder validation");
        check(!IsWindowVisible(host.value)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=host.value,"Physical resource QA activated the desktop");
        std::cout<<"PASS real Windows junction rejected by final handle locations before decoding; no UI/input/audio.\n";return 0;
    }
    check(argc==1,"Unknown curricular image QA selector");
    contracts();
    const auto qa=std::filesystem::path(build::source_root)/"out/qa"/("curricular-images-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    const auto content=qa/"content",images=content/"images",lesson_path=content/"es/programming/qa/lesson.json";
    std::filesystem::create_directories(images);std::filesystem::create_directories(lesson_path.parent_path());
    encode(images/"qa_pattern.png",L"image/png");encode(images/"qa_photo.jpg",L"image/jpeg");write(images/"qa_bad.png","This is an explicitly invalid QA resource.");
    std::filesystem::copy_file(images/"qa_photo.jpg",images/"qa_mismatch.png");
    Window host{CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden synthetic curricular image QA",WS_OVERLAPPEDWINDOW,0,0,900,760,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    check(host.value,"Cannot create the hidden image host");
    const curriculum::ImageResource pattern{"qa_pattern","images/qa_pattern.png","Patrón QA sintético.","Pie: dato y."};
    {
        CurricularImageView preview(host.value,700,lesson_path,pattern);check(preview.loaded()&&preview.image_width()==320&&preview.image_height()==160,"Valid PNG did not decode to its real dimensions");
        check(preview.height_for_width(640)>preview.height_for_width(320),"Preview ignored the available width");
        const auto focus_before=GetFocus();preview.show_detail(host.value);
        const auto detail=FindWindowExW(host.value,nullptr,L"LGAED.CurricularImageDetail",nullptr);
        check(detail&&(GetWindowLongPtrW(detail,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(detail,GWL_STYLE)&WS_POPUP)&&
            GetAncestor(detail,GA_ROOT)==host.value&&!IsWindowVisible(detail),"Detail escaped the hidden application's single root window");
        MoveWindow(detail,0,0,600,360,TRUE);SendMessageW(detail,WM_COMMAND,102,reinterpret_cast<LPARAM>(GetDlgItem(detail,102)));
        for(int n=0;n<8;++n)SendMessageW(detail,WM_COMMAND,104,reinterpret_cast<LPARAM>(GetDlgItem(detail,104)));
        const auto canvas=GetDlgItem(detail,106);RECT canvas_area{};GetClientRect(canvas,&canvas_area);
        check(canvas&&canvas_area.right>0&&canvas_area.bottom>0,"Internal detail lost its own image viewport");
        POINT client{20,20};ClientToScreen(canvas,&client);
        check(SendMessageW(canvas,WM_NCHITTEST,0,MAKELPARAM(client.x,client.y))==HTCLIENT,"The image canvas passes click/drag hits through to its parent");
        POINT vertical{canvas_area.right+GetSystemMetrics(SM_CXVSCROLL)/2,20};ClientToScreen(canvas,&vertical);
        check(SendMessageW(canvas,WM_NCHITTEST,0,MAKELPARAM(vertical.x,vertical.y))==HTVSCROLL,"The detail vertical scrollbar lost its native hit region");
        SCROLLINFO scroll{sizeof(scroll),SIF_ALL};GetScrollInfo(canvas,SB_VERT,&scroll);
        check(scroll.nMax>static_cast<int>(scroll.nPage),"Zoomed detail did not expose native vertical scrolling");
        SendMessageW(canvas,WM_VSCROLL,SB_LINEDOWN,0);GetScrollInfo(canvas,SB_VERT,&scroll);check(scroll.nPos>0,"Native detail scrolling ignored an explicit line move");
        SendMessageW(detail,WM_COMMAND,IDCANCEL,0);check(!IsWindow(detail)&&GetFocus()==focus_before&&preview.loaded(),"Closing hidden detail changed focus or destroyed its owner's image");
        CurricularImageView jpeg(host.value,701,lesson_path,{"qa_photo","images/qa_photo.jpg","JPEG QA",{}});check(jpeg.loaded()&&jpeg.image_width()==320&&jpeg.image_height()==160,"Valid JPEG did not decode");
        for(const auto name:{"qa_missing","qa_bad","qa_mismatch"}){
            CurricularImageView failure(host.value,702,lesson_path,{name,std::string("images/")+name+".png","Descripción conservada",{}});
            check(!failure.loaded()&&!failure.error().empty(),"Missing, invalid or mismatched bytes simulated a loaded image");}
        // The preview owns pixels, not the source handle. Renaming the QA
        // original must work while a valid decoded preview remains alive.
        std::filesystem::rename(images/"qa_pattern.png",images/"qa_pattern_saved.png");
        check(preview.loaded(),"A detached preview depended on its source file remaining open");
        std::filesystem::rename(images/"qa_pattern_saved.png",images/"qa_pattern.png");
    }
    const std::string source=R"({"id":"qa_curricular_images","title":"Caracteres y main","pages":[{"blocks":[{"type":"image","imageId":"qa_pattern","src":"images/qa_pattern.png","alt":"Patrón QA sintético.","caption":"Pie: dato y."},{"type":"image","imageId":"qa_missing","src":"images/qa_missing.png","alt":"Descripción del recurso ausente."}]}]})";
    write(lesson_path,source);auto lesson=windows::load_lesson(lesson_path);
    check(lesson.pages[0].blocks[0].type==curriculum::BlockType::image&&lesson.pages[0].blocks[0].image==pattern,"Native loader lost the exact image fields");
    const auto title_at=wide(lesson.title).find(L"main");lesson.text_semantics["/title"]={lesson.title,{{title_at,title_at+4,"main",curriculum::SemanticRole::cpp}}};
    auto& block=lesson.pages[0].blocks[0];block.text_semantics["/caption"]={pattern.caption,{{5,9,"dato",curriculum::SemanticRole::output}}};
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"curricular-images-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    {state::StateStore store(environment.paths.root);auto data=store.load();
        data.preferences["qa_image_opaque"]["keep"]=std::string("\t Conserva progreso y ajustes.\r\n保存");
        state::Draft draft;draft.source="int conservar = 7;\r\n";draft.input="dato\t7\n";
        state::put_draft(data,"qa_image_unrelated_cpp",draft);store.save(data);}
    HWND view{};const auto open=[&]{view=create_lesson_view(host.value,environment,lesson);check(view,"Cannot create the image lesson");MoveWindow(view,0,0,900,760,TRUE);pump();};
    open();
    const auto verify=[&]{const auto regions=lesson_reading_regions(view);
        const auto title=std::ranges::find_if(regions,[&](const auto& region){return region.target==descendant(view,990);});
        check(title!=regions.end()&&title->source_text==lesson.title&&title->text_semantics==lesson.text_semantics.at("/title"),"The native lesson title lost its exact root descriptor");
        const auto main_style=character(title->target,title_at),prose_style=character(title->target,11);
        check((main_style.dwEffects&CFE_BOLD)&&(prose_style.dwEffects&CFE_BOLD),"Root semantic title lost its title typography");
        if(!reader_theme::palette().high_contrast)check(main_style.crTextColor!=prose_style.crTextColor&&!(main_style.dwEffects&CFE_AUTOBACKCOLOR)&&(prose_style.dwEffects&CFE_AUTOBACKCOLOR),"Root title roles did not stay within their exact span");
        auto alt=std::ranges::find_if(regions,[&](const auto& region){return region.source_text==pattern.alt;});
        auto caption=std::ranges::find_if(regions,[&](const auto& region){return region.source_text==pattern.caption;});
        check(alt!=regions.end()&&caption!=regions.end()&&alt<caption&&!alt->is_exercise&&!caption->is_exercise&&
            caption->text_semantics==block.text_semantics.at("/caption"),"Image description/caption lost their order, source or semantic ownership");
        check(std::ranges::any_of(regions,[](const auto& region){return region.source_text.starts_with("Imagen no disponible.");}),"A missing image error was hidden from the reader");
        check(edit_text(descendant(view,1000))==pattern.alt&&edit_text(descendant(view,1019))==pattern.caption,"Image descriptions were rewritten or merged");
    };
    verify();check(flush_lesson_view(view),"Cannot persist isolated image QA");const auto before=read_lesson_state(view);
    DestroyWindow(view);open();verify();check(flush_lesson_view(view),"Cannot flush reopened image QA");const auto after=read_lesson_state(view);
    check(before.preferences==after.preferences,"Image/title presentation changed stored responses, drafts or preferences");
    DestroyWindow(view);check(state::read_bytes(lesson_path)==std::vector<std::uint8_t>(source.begin(),source.end()),"Image QA changed its source lesson");
    check(!IsWindowVisible(host.value)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=host.value,"Hidden image QA activated the desktop");
    Gdiplus::GdiplusShutdown(graphics_token);FreeLibrary(edit_module);OleUninitialize();
    std::cout<<"PASS PNG/JPEG decoding, detached handles, missing/corrupt/mismatched failure, aspect/fit/100%/pan bounds, internal hidden detail/native hit regions/scroll, native root title/image regions and exact reopen state. Synthetic QA only; no screenshots, desktop activation, input, playback or personal state.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL curricular images: "<<error.what()<<'\n';return 1;}

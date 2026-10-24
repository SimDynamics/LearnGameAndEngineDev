#include "presentation/windows/graphics_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace lgaed::presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)continue;TranslateMessage(&message);DispatchMessageW(&message);}}
int px(HWND window,int value){return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
struct Window{HWND value=nullptr;~Window(){if(IsWindow(value))DestroyWindow(value);}};
template<class Predicate>GraphicsLabSnapshot until(HWND view,Predicate predicate,const char* reason){const auto deadline=GetTickCount64()+15000;GraphicsLabSnapshot value;
    do{pump();value=inspect_graphics_lab(view,false);if(!value.error.empty())throw std::runtime_error(value.error);if(predicate(value))return value;Sleep(5);}while(GetTickCount64()<deadline);throw std::runtime_error(reason);}
void frames(HWND view,unsigned count){const auto initial=inspect_graphics_lab(view,false).presented_frames;(void)until(view,[&](const auto& frame){return frame.presented_frames>=initial+count;},"Actual GPU presentation did not advance");}
void click(HWND view,int id){const auto control=GetDlgItem(view,id);check(control&&IsWindowVisible(control)&&IsWindowEnabled(control),"The original graphics control is missing or disabled");SendMessageW(control,BM_CLICK,0,0);pump();}
void exercise(HWND host,GraphicsBackend backend){unsigned returned=0;Window view{create_physics_graphics_view(host,{},[&]{++returned;},backend)};
    MoveWindow(view.value,0,0,px(host,800),px(host,700),TRUE);pump();
    auto initial=until(view.value,[](const auto& frame){return frame.presented_frames>=2&&frame.simulation_time>0.15f;},"The real14-body physics scene did not start");
    check(initial.objects==14&&!initial.benchmark&&initial.selected==-1&&initial.width>0&&initial.height>0,"The original interactive physics scene was replaced with another demo");
    check(backend!=GraphicsBackend::warp||initial.software,"The explicit WARP request was mislabeled as software without using WARP");
    const auto pixels=inspect_graphics_lab(view.value);check(pixels.colored_pixels>100&&pixels.backend.starts_with("Direct3D 11"),"The actual sphere/floor GPU frame is blank");
    check(!GetDlgItem(view.value,5210)&&!GetDlgItem(view.value,5211),"Invented triangle/wireframe/rotation controls remained in the original laboratory");
    for(const auto id:{5260,5261,5262,5263,5264,5212}){const auto control=GetDlgItem(view.value,id);check(control!=nullptr,"An original graphics action is missing");
        if(!reader_theme::palette(control).high_contrast)check(reader_theme::palette(control).accent==RGB(214,215,215)&&reader_theme::accent_foreground(control)==RGB(0,0,0),"Graphics framework actions incorrectly inherited the learning-route accent fill");}
    const auto canvas=GetDlgItem(view.value,5221);check(canvas!=nullptr,"The interactive graphics surface is missing");RECT area{};GetClientRect(canvas,&area);
    SendMessageW(canvas,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),MAKELPARAM(20,20));frames(view.value,1);check(inspect_graphics_lab(view.value,false).selected==-1,"Mouse scrolling started a sphere drag");
    initial=inspect_graphics_lab(view.value,false);const auto picked=std::ranges::min_element(initial.bodies,[](const auto& left,const auto& right){
        const auto visible=[](const auto& body){return body.u>0.03f&&body.u<0.97f&&body.v>0.03f&&body.v<0.97f;};if(visible(left)!=visible(right))return visible(left);return left.z>right.z;});
    check(picked!=initial.bodies.end()&&picked->u>0&&picked->u<1&&picked->v>0&&picked->v<1,"There is no visible real sphere for the pointer regression");
    const auto x=static_cast<int>(picked->u*area.right),y=static_cast<int>(picked->v*area.bottom);
    SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));const auto held=until(view.value,[](const auto& frame){return frame.selected>=0;},"Clicking a real projected sphere did not pick it");
    const auto selected=held.selected;const auto old_x=held.bodies[static_cast<std::size_t>(selected)].x;
    Sleep(20);const auto moved_x=x+(x>area.right/2?-px(view.value,30):px(view.value,30));
    SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(moved_x,y-px(view.value,12)));
    const auto moved=until(view.value,[&](const auto& frame){return frame.selected==selected&&std::abs(frame.bodies[static_cast<std::size_t>(selected)].x-old_x)>0.01f;},"Dragging did not move the selected original physics body");
    // Waiting for the asynchronous renderer above can exceed the real
    // 120-ms velocity history. A release at the same old position then
    // correctly represents a stopped hand. Model a fresh final stroke,
    // rather than requiring momentum from an arbitrarily old movement.
    (void)moved;Sleep(160);SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(moved_x,y-px(view.value,12)));Sleep(20);
    const auto release_x=moved_x+(x>area.right/2?-px(view.value,10):px(view.value,10));
    SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(release_x,y-px(view.value,12)));
    SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(release_x,y-px(view.value,12)));
    const auto released=until(view.value,[](const auto& frame){return frame.selected==-1;},"Mouse release retained sphere capture");
    check(std::abs(released.bodies[static_cast<std::size_t>(selected)].velocity_x)>0.01f&&GetCapture()!=canvas,"Measured mouse velocity did not launch the body or release capture");
    click(view.value,5263);auto benchmark=until(view.value,[](const auto& frame){return frame.benchmark&&frame.objects==64;},"The original GPU benchmark did not replace physics");
    check(IsWindowVisible(GetDlgItem(view.value,5264))&&!IsWindowVisible(GetDlgItem(view.value,5212)),"Benchmark load/reset controls did not follow the original visibility contract");
    const auto frozen=benchmark.bodies;frames(view.value,2);benchmark=inspect_graphics_lab(view.value);
    check(benchmark.colored_pixels>100,"The64-sphere instanced GPU benchmark is blank");
    for(std::size_t i=0;i<frozen.size();++i)check(benchmark.bodies[i].x==frozen[i].x&&benchmark.bodies[i].y==frozen[i].y&&benchmark.bodies[i].z==frozen[i].z,"The CPU physics scene continued advancing in GPU-only benchmark mode");
    click(view.value,5264);(void)until(view.value,[](const auto& frame){return frame.objects==256;},"GPU load did not advance to256 original sphere instances");
    click(view.value,5264);(void)until(view.value,[](const auto& frame){return frame.objects==1024;},"GPU load did not advance to1024 original sphere instances");
    check(inspect_graphics_lab(view.value).colored_pixels>100,"The1024-sphere instanced GPU frame is blank");click(view.value,5264);(void)until(view.value,[](const auto& frame){return frame.objects==64;},"GPU load did not cycle to64");
    const auto measured=until(view.value,[](const auto& frame){return frame.frames_per_second>0&&frame.milliseconds_per_frame>0;},"Measured presentation statistics did not update");
    check(std::abs(measured.frames_per_second*measured.milliseconds_per_frame-1000)<0.001,"FPS and frame time do not describe the same real elapsed interval");
    click(view.value,5263);const auto resumed=until(view.value,[](const auto& frame){return !frame.benchmark&&frame.objects==14&&frame.simulation_time>0.2f;},"Returning from benchmark lost the interactive physics scene");
    (void)resumed;click(view.value,5212);const auto reset=until(view.value,[](const auto& frame){return frame.simulation_time<0.15f&&frame.selected==-1;},"Reset did not reset original physics time and selection");(void)reset;
    check(!IsWindowVisible(GetDlgItem(view.value,5264))&&IsWindowVisible(GetDlgItem(view.value,5212)),"Returning to physics retained benchmark-only controls");
    const auto identity=GetDlgItem(view.value,5263);MoveWindow(view.value,0,0,px(host,640),px(host,480),TRUE);SendMessageW(view.value,WM_DPICHANGED_AFTERPARENT,0,0);pump();GetClientRect(canvas,&area);
    const auto resized=until(view.value,[&](const auto& frame){return frame.width==static_cast<unsigned>(area.right)&&frame.height==static_cast<unsigned>(area.bottom);},"The real GPU surface did not follow resize/DPI layout");
    LOGFONTW font{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(view.value,5213),WM_GETFONT,0,0)),sizeof(font),&font);
    check(identity==GetDlgItem(view.value,5263)&&-font.lfHeight==px(view.value,13)&&std::wstring_view(font.lfFaceName)==L"Roboto"&&resized.objects==14,"DPI refresh replaced controls, lost physics, or kept stale font metrics");
    std::cout<<"PASS actual graphics adapter: "<<initial.backend<<"; original14-body input, GPU instances and measured statistics (not a hardware throughput benchmark)\n";
    click(view.value,5201);check(returned==1,"The native laboratory Back action is decorative");
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);reader_theme::prepare_fonts();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    Window host{CreateWindowExW(0,L"STATIC",L"Original physics and GPU integration QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1500,1200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};check(host.value!=nullptr,"Create isolated graphics QA host");
    exercise(host.value,GraphicsBackend::automatic);exercise(host.value,GraphicsBackend::warp);return 0;
}catch(const std::exception& error){std::cerr<<"Graphics laboratory failure: "<<error.what()<<'\n';return 1;}}

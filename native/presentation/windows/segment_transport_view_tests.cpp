#include "core/attempts.hpp"
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <gdiplus.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string utf8(winrt::hstring value) { return winrt::to_string(value); }
void pump() {
    MSG event{};
    while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE)) if (event.message != WM_QUIT) {
        TranslateMessage(&event); DispatchMessageW(&event);
    }
}
template<class Predicate> void wait_for(Predicate predicate, const char* message) {
    const auto end = GetTickCount64() + 30000;
    do { pump(); if (predicate()) return; MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT); }
    while (GetTickCount64() < end);
    throw std::runtime_error(message);
}
std::vector<HWND> descendants(HWND parent) {
    std::vector<HWND> result;
    EnumChildWindows(parent, [](HWND item, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(item); return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::wstring caption(HWND item) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item)) + 1, L'\0');
    text.resize(GetWindowTextW(item, text.data(), static_cast<int>(text.size()))); return text;
}
HWND named(HWND parent, std::wstring_view text, bool prefix = false) {
    for (const auto item : descendants(parent)) {
        const auto value = caption(item);
        if (prefix ? value.starts_with(text) : value == text) return item;
    }
    return nullptr;
}
HWND by_id(HWND parent, int id) {
    for (const auto item : descendants(parent)) if (GetDlgCtrlID(item) == id) return item;
    return nullptr;
}
HWND canvas(HWND parent) {
    for (const auto item : descendants(parent)) {
        wchar_t kind[80]{}; GetClassNameW(item, kind, 80);
        if (std::wstring_view(kind) == L"LearnGameAndEngineDev.MathCanvas") return item;
    }
    throw std::runtime_error("The free construction has no native canvas");
}
void click(HWND parent, std::wstring_view text) {
    const auto item = named(parent, text); require(item && IsWindowEnabled(item), "A required segment action is missing or disabled");
    SendMessageW(item, BM_CLICK, 0, 0); pump();
}
POINT pixel(const JsonObject& value) {
    return {static_cast<LONG>(std::lround(value.GetNamedNumber(L"x"))), static_cast<LONG>(std::lround(value.GetNamedNumber(L"y")))};
}
POINT marker(const JsonObject& drawing, std::string_view id) {
    for (const auto& value : drawing.GetNamedArray(L"points")) {
        const auto point = value.GetObject();
        if (point.GetNamedString(L"id") == wide(id)) return pixel(point.GetNamedObject(L"center"));
    }
    throw std::runtime_error("A given segment marker disappeared: " + std::string(id));
}
void place(HWND target, POINT at) {
    SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(at.x, at.y));
    SendMessageW(target, WM_LBUTTONUP, 0, MAKELPARAM(at.x, at.y)); pump();
}
void drag(HWND target, POINT from, POINT to) {
    SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
    SendMessageW(target, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(to.x, to.y));
    SendMessageW(target, WM_LBUTTONUP, 0, MAKELPARAM(to.x, to.y)); pump();
}
void select_point(HWND parent, std::wstring_view label) {
    click(parent, L"Colocar puntos");
    for (int index = 0; index < 34; ++index) {
        const auto picker = named(parent, L"Punto: ", true); require(picker, "No free endpoint selector");
        if (caption(picker) == L"Punto: " + std::wstring(label)) return;
        SendMessageW(picker, BM_CLICK, 0, 0);
    }
    throw std::runtime_error("The endpoint selector offered no requested construction point");
}
JsonObject config(bool partition = false, int copies = 2) {
    if (partition) return JsonObject::Parse(LR"({"type":"segment_transport","mode":"partition","target":{"origin":{"x":0,"y":0,"label":"M"},"end":{"x":6,"y":0,"label":"T"}},"partitionCount":3,"pointLabels":["N","P"],"positionTolerance":0.08,"lengthTolerance":0.08})");
    auto result = JsonObject::Parse(LR"({"type":"segment_transport","mode":"chain","sources":[{"id":"ab","from":{"x":0,"y":3,"label":"A"},"to":{"x":4,"y":3,"label":"B"}}],"target":{"origin":{"x":0,"y":0,"label":"M"},"direction":{"x":1,"y":0}},"positionTolerance":0.08,"lengthTolerance":0.08})");
    JsonArray steps, labels;
    for (int at = 0; at < copies; ++at) { steps.Append(JsonValue::CreateStringValue(L"ab")); labels.Append(JsonValue::CreateStringValue(at ? L"P" : L"N")); }
    result.SetNamedValue(L"steps", steps); result.SetNamedValue(L"pointLabels", labels); return result;
}
JsonObject puzzle(bool partition = false, int copies = 2) {
    auto result = JsonObject::Parse(LR"({"id":"qa_segment_construction","type":"math_puzzle","prompt":"Construye los tramos con los datos dados.","responses":[{"id":"construction","type":"math_segment_transport"}],"feedback":{"correct":"La construcción conserva las condiciones pedidas.","incorrect":"Revisa las condiciones geométricas."}})");
    result.GetNamedArray(L"responses").GetObjectAt(0).SetNamedValue(L"validator", config(partition, copies)); return result;
}
JsonObject response(const MathPuzzleView& view) { return JsonObject::Parse(wide(view.answers_json())).GetNamedObject(L"construction"); }
struct Bridge {
    windows::NarrationWorker worker{std::filesystem::path(build::source_root) / "out/narration/runtime.json"};
    JsonObject drawing;
    bool fail_next_layout=false;
    std::string request(std::string_view raw) {
        const auto action=JsonObject::Parse(wide(raw)).GetNamedString(L"action",L"");
        if(action==L"segment_layout"&&fail_next_layout){fail_next_layout=false;throw std::runtime_error("simulated worker failure with a private path");}
        const auto result = worker.math_editor(raw);
        if (action == L"segment_layout") drawing = JsonObject::Parse(wide(result));
        return result;
    }
    MathPuzzleCallbacks callbacks(int& changes, int& drafts) {
        MathPuzzleCallbacks value;
        value.changed = [&changes] { ++changes; }; value.draft_changed = [&drafts] { ++drafts; };
        value.expression_request = [this](std::string_view raw) { return request(raw); }; return value;
    }
    bool correct(const JsonObject& block, const MathPuzzleView& view) {
        const auto reply = JsonObject::Parse(wide(worker.validate_math(utf8(block.Stringify()), view.answers_json(), "es")));
        return reply.GetNamedBoolean(L"evaluable") && reply.GetNamedBoolean(L"correct");
    }
};
struct Host {
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden segment construction QA", WS_OVERLAPPEDWINDOW,
        0, 0, 1100, 950, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Host() { if (window) DestroyWindow(window); }
};
POINT along(const JsonObject& drawing, double fraction, bool partition = false) {
    const auto origin = marker(drawing, "target:origin");
    const auto first = marker(drawing, partition ? "target:origin" : "source:ab:from");
    const auto last = marker(drawing, partition ? "target:end" : "source:ab:to");
    const auto length = std::hypot(last.x - first.x, last.y - first.y);
    return {static_cast<LONG>(std::lround(origin.x + fraction * length)), origin.y};
}
void fit_given_distance(HWND parent, HWND plot, Bridge& bridge, double multiple) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        RECT client{}; GetClientRect(plot, &client); const auto at = along(bridge.drawing, multiple);
        if (at.x >= 12 && at.x <= client.right - 12) return;
        click(parent, L"Alejar");
    }
    throw std::runtime_error("The independent camera cannot reach the required part of the ray");
}
int font_height(HWND item) {
    const auto dc=GetDC(item);const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(item,WM_GETFONT,0,0)));
    TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(item,dc);return metrics.tmHeight;
}
void toolbar_text_fits(HWND parent) {
    const std::vector<std::wstring> actions{L"Colocar puntos",L"Mover herramienta",L"Girar herramienta",L"Vista",L"Restablecer construcción",L"Restablecer herramienta",L"Restablecer vista",L"Acercar",L"Alejar"};
    int checked=0;
    for(const auto item:descendants(parent)){
        const auto text=caption(item);
        if(std::ranges::find(actions,text)==actions.end()&&!text.starts_with(L"Punto: ")&&!text.starts_with(L"Longitud: "))continue;
        RECT area{};GetClientRect(item,&area);const auto dpi=static_cast<int>(GetDpiForWindow(item));
        const auto dc=GetDC(item);const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(item,WM_GETFONT,0,0)));
        RECT needed{0,0,std::max(1L,area.right-MulDiv(24,dpi,96)),0};
        DrawTextW(dc,text.data(),static_cast<int>(text.size()),&needed,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,old);ReleaseDC(item,dc);
        require(area.bottom>=needed.bottom+MulDiv(8,dpi,96)&&area.right>=needed.right+MulDiv(24,dpi,96),
            "A segment toolbar caption is clipped at the actual font, scale or column width");
        ++checked;
    }
    require(checked==11,"Responsive layout lost a segment action or duplicated controls");
}
void capture_png(HWND target,const wchar_t* filename) {
    RECT area{};GetClientRect(target,&area);require(area.right>0&&area.bottom>0&&area.bottom<20000,"Invalid native QA snapshot bounds");
    const auto window_dc=GetDC(target),buffer=CreateCompatibleDC(window_dc);const auto bitmap=CreateCompatibleBitmap(window_dc,area.right,area.bottom);
    ReleaseDC(target,window_dc);require(buffer&&bitmap,"Cannot capture native construction controls");
    const auto old=SelectObject(buffer,bitmap);
    SendMessageW(target,WM_PRINT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);GdiFlush();
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;require(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"Cannot encode native QA snapshot");
    {
        UINT count=0,size=0;Gdiplus::GetImageEncodersSize(&count,&size);auto bytes=std::make_unique<BYTE[]>(size);
        auto* encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.get());Gdiplus::GetImageEncoders(count,size,encoders);
        const Gdiplus::ImageCodecInfo* png=nullptr;for(UINT at=0;at<count;++at)if(std::wstring_view(encoders[at].MimeType)==L"image/png")png=&encoders[at];
        require(png,"No PNG encoder for the actual native frame");
        const auto directory=std::filesystem::path(build::source_root)/"out/qa/segment-transport";std::filesystem::create_directories(directory);
        Gdiplus::Bitmap image(bitmap,nullptr);require(image.Save((directory/filename).c_str(),&png->Clsid,nullptr)==Gdiplus::Ok,"Cannot save native QA snapshot");
    }
    Gdiplus::GdiplusShutdown(token);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);
}
void native_control_tests(HWND host, Bridge& bridge) {
    int changes = 0, drafts = 0; const auto block = puzzle();
    MathPuzzleView view(host, utf8(block.Stringify()), "es", "", bridge.callbacks(changes, drafts)); view.layout(0, 0, 850);
    const auto plot = canvas(view.window());
    require(response(view).GetNamedArray(L"points").Size() == 0, "The constructor offered precomputed answer points");
    for (const auto item : descendants(view.window())) {
        wchar_t kind[80]{}; GetClassNameW(item, kind, 80);
        if (_wcsnicmp(kind, L"RichEdit", 8) == 0) require((GetWindowLongPtrW(item, GWL_STYLE) & ES_READONLY) != 0, "The construction exposed an x/y editor");
    }
    fit_given_distance(view.window(), plot, bridge, 2);
    select_point(view.window(), L"N"); place(plot, along(bridge.drawing, .5));
    select_point(view.window(), L"P"); place(plot, along(bridge.drawing, 2));
    require(!bridge.correct(block, view), "A correct total accepted two incorrect component lengths");
    require(changes == 2, "Placing endpoints did not publish the two independent answers");
    const auto identity = view.answer_identity_json();
    const auto opening = response(view).GetNamedObject(L"interactionDraft").GetNamedObject(L"tool").GetNamedNumber(L"length");
    click(view.window(), L"Mover herramienta"); drag(plot, {200, 180}, {220, 190});
    click(view.window(), L"Girar herramienta"); const auto pivot = pixel(bridge.drawing.GetNamedObject(L"tool").GetNamedObject(L"center"));
    drag(plot, {pivot.x + 70, pivot.y}, {pivot.x + 50, pivot.y + 45});
    require(view.answer_identity_json() == identity && changes == 2 && drafts > 0, "Tool movement changed a mathematical answer");
    require(response(view).GetNamedObject(L"interactionDraft").GetNamedObject(L"tool").GetNamedNumber(L"length") == opening, "Rigid movement stretched the captured opening");
    select_point(view.window(), L"N");
    const auto old_point = pixel(bridge.drawing.GetNamedArray(L"constructedPoints").GetObjectAt(0).GetNamedObject(L"center"));
    drag(plot, old_point, along(bridge.drawing, 1));
    require(bridge.correct(block, view), "Freely correcting the first endpoint did not preserve both source lengths");
    const auto semantic = view.answer_identity_json();
    click(view.window(),L"Mover herramienta");
    const auto before_failure=response(view).Stringify();const auto original_caption=caption(plot);const auto before_drafts=drafts;
    bridge.fail_next_layout=true;drag(plot,{200,150},{230,170});
    require(!bridge.fail_next_layout&&response(view).Stringify()==before_failure&&drafts==before_drafts,
        "A failed drawing RPC mutated the last valid construction or published a draft");
    require(caption(plot)!=original_caption&&caption(plot).find(L"simulated")==std::wstring::npos&&caption(plot).find(L"private")==std::wstring::npos,
        "A temporary drawing failure lacked a neutral accessible status or leaked its exception");
    drag(plot,{200,150},{230,170});
    require(view.answer_identity_json()==semantic&&caption(plot)==original_caption&&drafts>before_drafts,
        "A valid retry did not recover drawing without changing the answer");
    click(view.window(), L"Vista"); drag(plot, {300, 240}, {330, 250}); click(view.window(), L"Acercar");
    require(view.answer_identity_json() == semantic && bridge.correct(block, view), "Camera navigation altered the geometry or its tolerance");
    const auto saved = view.answers_json(); const auto old_draft = response(view).GetNamedObject(L"interactionDraft").Stringify();
    MathPuzzleView reopened(host, utf8(block.Stringify()), "es", saved, bridge.callbacks(changes, drafts)); reopened.layout(0, 0, 850);
    require(reopened.answer_identity_json() == semantic && response(reopened).GetNamedObject(L"interactionDraft").Stringify() == old_draft, "Recreating the native view changed a construction or camera/tool draft");
    capture_png(reopened.window(),L"native-wide.png");
    const auto old_height=font_height(named(reopened.window(),L"Restablecer construcción"));
    reopened.set_text_scale(150);reopened.layout(0,0,400);
    require(font_height(named(reopened.window(),L"Restablecer construcción"))>=old_height*1.35,"The construction toolbar ignored the text scale");
    toolbar_text_fits(reopened.window());capture_png(reopened.window(),L"native-narrow-150.png");
    const auto expanded_plot = canvas(reopened.window());
    click(reopened.window(), L"Ampliar representación"); const auto expanded = GetParent(expanded_plot);
    require(expanded != reopened.window() && IsChild(expanded,named(expanded,L"Vista")), "Fullscreen lost the existing construction controls");
    const auto first_action=named(expanded,L"Colocar puntos");SetFocus(first_action);
    MSG tab{};tab.hwnd=first_action;tab.message=WM_KEYDOWN;tab.wParam=VK_TAB;
    require(IsDialogMessageW(expanded,&tab)&&GetFocus()!=first_action&&IsChild(expanded,GetFocus()),
        "Expanded construction controls cannot be traversed by native Tab dispatch");
    ShowWindow(expanded,SW_HIDE);MoveWindow(expanded,0,0,400,600,TRUE);pump();toolbar_text_fits(expanded);
    RECT plot_area{};GetClientRect(expanded_plot,&plot_area);require(plot_area.bottom>=100,"Fullscreen toolbar consumed the construction canvas");
    capture_png(expanded,L"native-fullscreen-narrow-150.png");
    const auto controls_host=GetParent(named(expanded,L"Vista"));
    const auto expanded_state=reopened.answers_json();RECT plot_before{};GetWindowRect(expanded_plot,&plot_before);
    SendMessageW(controls_host,WM_VSCROLL,SB_BOTTOM,0);pump();
    RECT plot_after{};GetWindowRect(expanded_plot,&plot_after);
    require(GetScrollPos(controls_host,SB_VERT)>0&&EqualRect(&plot_before,&plot_after)&&reopened.answers_json()==expanded_state,
        "Scrolling expanded actions changed the camera, answer or canvas bounds");
    capture_png(expanded,L"native-fullscreen-bottom-150.png");
    SendMessageW(expanded, WM_COMMAND, IDCANCEL, 0);
    require(GetParent(expanded_plot) == reopened.window() && reopened.answer_identity_json() == semantic, "Closing fullscreen changed a constructed answer");
    reopened.set_enabled(false);
    require(!IsWindowEnabled(named(reopened.window(), L"Colocar puntos")) && IsWindowEnabled(named(reopened.window(), L"Vista")), "Solved geometry was editable or its independent camera was frozen");
    click(reopened.window(), L"Vista"); drag(expanded_plot, {200, 150}, {225, 170});
    require(reopened.answer_identity_json() == semantic, "Reviewing a solved figure changed its answer");
    reopened.set_enabled(true); reopened.reset();
    require(response(reopened).GetNamedArray(L"points").Size() == 0, "Reset retained constructed endpoints");
    std::cout << "PASS free native endpoints, component-length rejection, rigid tool, camera, serialized draft, fullscreen and solved-state review\n";
}
void partition_control_test(HWND host, Bridge& bridge) {
    int changes = 0, drafts = 0; const auto block = puzzle(true);
    MathPuzzleView view(host, utf8(block.Stringify()), "es", "", bridge.callbacks(changes, drafts)); view.layout(0, 0, 700);
    const auto plot = canvas(view.window());
    select_point(view.window(), L"N"); place(plot, along(bridge.drawing, 1. / 3, true));
    select_point(view.window(), L"P"); place(plot, along(bridge.drawing, 2. / 3, true));
    require(bridge.correct(block, view), "Native free partition did not cover the given total with equal parts");
    require(changes == 2, "Partition replaced construction with an automatic arrangement");
    std::cout << "PASS native partition: freely placed interiors, fixed total endpoints and equal consecutive parts\n";
}
void lesson_state_test(Bridge& bridge, HWND host) {
    std::cout << "Checking real lesson persistence and review isolation\n";
    const auto block = puzzle(false, 1);
    auto root = JsonObject::Parse(LR"({"id":"qa_segments__unit__lesson","title":"Geometría QA","pages":[{"blocks":[]}]})");
    root.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks").Append(block);
    auto lesson = windows::parse_lesson_json(utf8(root.Stringify()));
    lesson.source_path = std::filesystem::path(build::source_root) / "out/qa/math_physics/segments.json";
    const auto& activity = lesson.pages[0].blocks[0];
    const auto environment = resolve_windows_environment({ProfileId::Codex, L"segment-state-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false});
    require(!std::filesystem::exists(environment.paths.root), "Segment QA requires its own new isolated profile");
    initialize_profile_directories(environment.paths);
    { state::StateStore store(environment.paths.root); auto data = store.load(); data.preferences["unrelated_segment_qa"]["keep"] = std::string("unchanged"); store.save(data); }
    HWND view = nullptr; std::atomic<int> evaluations{0};
    std::atomic<bool> delay_next{false}, delayed_started{false}, delayed_cancelled{false}, release_delayed{false};
    const auto open = [&] {
        LessonViewHooks hooks;
        hooks.math_editor_request = [&bridge](std::string_view raw) { return bridge.request(raw); };
        hooks.validate_math = [&](std::string_view raw, std::string_view answers, std::string_view language, std::stop_token stop) {
            if(delay_next.exchange(false)){
                delayed_started=true;
                const auto until=GetTickCount64()+5000;
                while(!release_delayed&&GetTickCount64()<until&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if(stop.stop_requested()){delayed_cancelled=true;throw std::runtime_error("Cancelled delayed QA evaluation");}
            }
            auto result = bridge.worker.validate_math(raw, answers, language, stop); ++evaluations; return result;
        };
        view = create_lesson_view(host, environment, lesson, std::move(hooks)); require(view, "Cannot mount the segment lesson");
        MoveWindow(view, 0, 0, 1100, 900, TRUE); pump();
    };
    const auto saved = [&] { require(flush_lesson_view(view), "Cannot persist segment QA"); return read_lesson_state(view); };
    const auto review = [&] { const auto value = state::get_response(saved(), lesson, activity); require(value.has_value(), "The segment answer was not saved"); return *value; };
    const auto reserve = [&](const state::StateData& data) { return attempts::number(data, "reserve_math_physics", attempts::maximum("math_physics")); };
    const char* phase="mount";
    try {
        open(); phase="place unreviewed point"; auto plot = canvas(view); select_point(view, L"N"); place(plot, along(bridge.drawing, .5));
        phase="read unreviewed payload";
        const auto wrong_points = JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify();
        require(!review().correct && review().reviewed_answer_identity.empty(), "A point placement was already graded");
        require(flush_lesson_view(view), "Cannot flush the unreviewed segment draft"); DestroyWindow(view); view = nullptr; phase="reopen unreviewed draft"; open(); plot = canvas(view);
        require(JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify() == wrong_points && review().reviewed_answer_identity.empty(), "Reopening the unreviewed draft changed or graded it");
        phase="evaluate incorrect construction"; auto checks = evaluations.load(); SendMessageW(by_id(view, 1003), BM_CLICK, 0, 0);
        wait_for([&] { return evaluations.load() > checks && !review().reviewed_answer_identity.empty(); }, "The real wrong construction did not produce a reviewed response");
        const auto rejected = review(); const auto rejected_feedback = edit_text(by_id(view, 1004)); const auto rejected_reserve = reserve(saved());
        require(!rejected.correct && rejected_reserve == attempts::maximum("math_physics") - 1, "An incorrect construction did not consume exactly one ordinary attempt");
        phase="navigate reviewed construction"; click(view, L"Vista"); drag(plot, {250, 210}, {280, 220}); click(view, L"Mover herramienta"); drag(plot, {210, 180}, {230, 195});
        const auto after_navigation = review();
        require(state::response_reviewed(after_navigation,activity) && edit_text(by_id(view, 1004)) == rejected_feedback && reserve(saved()) == rejected_reserve, "Camera/tool draft erased feedback, review or attempts");
        checks = evaluations.load(); SendMessageW(by_id(view, 1003), BM_CLICK, 0, 0);
        wait_for([&] { return evaluations.load() > checks && IsWindowEnabled(by_id(view, 1003)); }, "Repeated checking did not finish");
        require(reserve(saved()) == rejected_reserve, "An unchanged answer was charged again after moving the camera/tool");
        phase="navigate during rejected-answer restoration";
        DestroyWindow(view);view=nullptr;delay_next=true;delayed_started=false;release_delayed=false;open();plot=canvas(view);
        wait_for([&]{return delayed_started.load();},"Rejected restoration never started");
        click(view,L"Vista");drag(plot,{250,210},{290,230});
        const auto rejection_draft=JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify();
        release_delayed=true;
        wait_for([&]{return edit_text(by_id(view,1004))==rejected_feedback;},"Draft-only navigation discarded the restored rejection feedback");
        require(!review().correct&&state::response_reviewed(review(),activity)&&reserve(saved())==rejected_reserve
            &&JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify()==rejection_draft,
            "Restoring a rejection overwrote the latest presentation draft or charged another attempt");
        phase="correct construction"; select_point(view, L"N"); place(plot, along(bridge.drawing, 1));
        require(review().reviewed_answer_identity.empty(), "Correcting a point retained an obsolete review");
        SendMessageW(by_id(view, 1003), BM_CLICK, 0, 0);
        wait_for([&] { return review().correct && caption(by_id(view, 1003)).starts_with(L"Correcto"); }, "The corrected real construction was not accepted");
        const auto solved = review(); const auto solved_feedback = edit_text(by_id(view, 1004));
        phase="navigate solved construction"; click(view, L"Vista"); drag(plot, {250, 210}, {270, 230}); click(view, L"Acercar");
        require(review().correct && review().reviewed_answer_identity == solved.reviewed_answer_identity && edit_text(by_id(view, 1004)) == solved_feedback, "Solved-state view changes revoked the result or feedback");
        const auto solved_answers = JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify();
        require(flush_lesson_view(view), "Cannot save the solved segment draft"); DestroyWindow(view); view = nullptr; phase="reopen solved construction"; open();
        wait_for([&] { return caption(by_id(view, 1003)).starts_with(L"Correcto"); }, "Reopening did not revalidate the solved construction");
        require(review().correct && JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify() == solved_answers && reserve(saved()) == rejected_reserve, "Solved answer, draft or attempt reserve changed on reopening");
        require(state::string_value(saved(), "unrelated_segment_qa", "keep") == "unchanged", "Segment QA modified unrelated preferences");
        phase="navigate during solved-answer restoration";
        DestroyWindow(view);view=nullptr;delay_next=true;delayed_started=false;release_delayed=false;open();plot=canvas(view);
        wait_for([&]{return delayed_started.load();},"Solved restoration never started");
        click(view,L"Vista");drag(plot,{250,210},{290,230});click(view,L"Mover herramienta");drag(plot,{200,160},{220,180});
        const auto restoration_draft=JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify();
        require(review().correct,"Moving only the view during restoration revoked the persisted success");
        release_delayed=true;
        wait_for([&]{return caption(by_id(view,1003)).starts_with(L"Correcto");},"Draft-only navigation discarded the restored success");
        require(review().correct&&review().reviewed_answer_identity.empty(),"Restoring success lost its saved verdict");
        require(reserve(saved())==rejected_reserve,"Restoring success charged a new attempt");
        require(JsonObject::Parse(wide(review().raw_json)).GetNamedObject(L"responses").Stringify()==restoration_draft,
            "Restoring success overwrote the latest camera/tool draft");
        require(edit_text(by_id(view,1004))==solved_feedback,"Restoring success changed the feedback for the same correct answer");
        phase="reset during solved-answer restoration";
        DestroyWindow(view);view=nullptr;delay_next=true;delayed_started=false;release_delayed=false;open();
        wait_for([&]{return delayed_started.load();},"Reset restoration never started");
        click(view,L"Reiniciar");checks=evaluations.load();release_delayed=true;
        wait_for([&]{return evaluations.load()>checks;},"The reset restoration never returned");
        pump();require(!review().correct&&!JsonObject::Parse(wide(review().raw_json)).HasKey(L"responses"),"Restoration resurrected a construction after explicit reset");
        phase="explicit reset"; click(view, L"Reiniciar"); require(!review().correct, "Explicit reset did not revoke the construction result");
        // The established reset representation is an empty saved response;
        // the initial geometry is rebuilt from the unchanged content contract.
        require(!JsonObject::Parse(wide(review().raw_json)).HasKey(L"responses") && bridge.drawing.GetNamedObject(L"state").GetNamedArray(L"points").Size() == 0, "Explicit reset retained constructed points");
        phase="cancel pending evaluation on reset"; plot=canvas(view); select_point(view,L"N"); place(plot,along(bridge.drawing,1));
        delay_next=true;delayed_started=false;release_delayed=false;SendMessageW(by_id(view,1003),BM_CLICK,0,0);
        wait_for([&]{return delayed_started.load();},"The delayed real evaluation never started");
        click(view,L"Reiniciar");pump();
        require(delayed_cancelled && !review().correct && bridge.drawing.GetNamedObject(L"state").GetNamedArray(L"points").Size()==0,
            "Reset left an old evaluation able to restore a solved answer");
        require(flush_lesson_view(view), "Cannot flush the reset QA"); DestroyWindow(view); view = nullptr;
    } catch (const winrt::hresult_error& error) {
        if(view)DestroyWindow(view); throw std::runtime_error(std::string(phase)+": "+winrt::to_string(error.message()));
    } catch (...) { if (view) DestroyWindow(view); throw; }
    std::cout << "PASS real lesson: draft/rejection/retry/success/reopen/reset, stable feedback and finite attempts during camera/tool changes\n";
}
}
int main() {
    std::cout << std::unitbuf;
    try {
        windows::ensure_winrt_apartment(); INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES}; InitCommonControlsEx(&common);
        Host host; require(host.window, "Cannot create isolated segment QA host"); Bridge bridge;
        native_control_tests(host.window, bridge); partition_control_test(host.window, bridge); lesson_state_test(bridge, host.window);
        std::cout << "Native segment construction tests PASS; curricular integration and human visual/listening acceptance remain separate.\n"; return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << "FAIL WinRT: " << winrt::to_string(error.message()) << '\n'; }
      catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; }
    return 1;
}

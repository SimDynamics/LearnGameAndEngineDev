#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "core/profile.hpp"

#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <ole2.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;

void require(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}
HWND child(HWND parent, int id) {
    struct Search { int id; HWND window = nullptr; } found{id};
    EnumChildWindows(parent, [](HWND item, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        if (GetDlgCtrlID(item) == search.id) {
            search.window = item; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found.window;
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        if (!dispatch_lesson_enter(message)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
}
void wait_until(const std::function<bool()>& condition, std::string_view reason) {
    const auto deadline = GetTickCount64() + 90000;
    do {
        pump();
        if (condition()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(std::string(reason));
}
std::string original_json() {
    return R"json({
      "id":"qa_windows_first_case_thirteen_inputs",
      "title":"Solo el primer caso es obligatorio",
      "compilerPuzzlesRequired":true,
      "pages":[{"blocks":[
        {"type":"cpp_compiler","question":"Comprueba la primera entrada.",
         "initialCode":"#include <iostream>\nint main(){int n=0;std::cin >> n;std::cout << n*2;}\n",
         "initialStandardInput":"1",
         "outputCheck":{"cases":[
           {"standardInput":"1","outputs":["2"]},
           {"standardInput":"2","outputs":["4"]},
           {"standardInput":"3","outputs":["6"]},
           {"standardInput":"4","outputs":["8"]},
           {"standardInput":"5","outputs":["10"]},
           {"standardInput":"6","outputs":["12"]},
           {"standardInput":"7","outputs":["14"]},
           {"standardInput":"8","outputs":["16"]},
           {"standardInput":"9","outputs":["18"]},
           {"standardInput":"10","outputs":["20"]},
           {"standardInput":"11","outputs":["22"]},
           {"standardInput":"12","outputs":["24"]},
           {"standardInput":"13","outputs":["26"]}
         ]}},
        {"type":"paragraph","text":"Los doce casos restantes son opcionales."}
      ]},{"blocks":[{"type":"paragraph","text":"Página que se habilita tras un único acierto."}]}]
    })json";
}
struct Scenario {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host = nullptr, view = nullptr;
    explicit Scenario(std::wstring suffix)
        :environment(resolve_windows_environment({ProfileId::Codex,
            L"ci-first-case-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(GetTickCount64()) + L"-" + suffix,false})),
         lesson(windows::parse_lesson_json(original_json(),"es")) {
        require(lesson.pages.size() == 2 && lesson.pages[0].blocks[0].output_check &&
            lesson.pages[0].blocks[0].output_check->cases.size() == 13 &&
            lesson.pages[0].blocks[0].initial_code.find('\n') != std::string::npos &&
            lesson.pages[0].blocks[0].initial_code.find("\\n") == std::string::npos,
            "The thirteen-case test contains literal backslash-n instead of C++ source lines");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root);
        auto data = store.load();
        data.preferences["native_windows"]["ui_language"] = std::string("es");
        store.save(data);
        host = CreateWindowExW(WS_EX_CONTROLPARENT,
            L"LgaedCompilerFirstCaseTest",L"QA primer caso C++",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,30,30,1080,800,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(host != nullptr,"Cannot create first-case Win32 test host");
        open();
    }
    ~Scenario() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        view = create_lesson_view(host,environment,lesson);
        require(view != nullptr,"Cannot create real Windows lesson view");
        RECT bounds{};GetClientRect(host,&bounds);
        MoveWindow(view,0,0,bounds.right,bounds.bottom,TRUE);
        pump();
    }
    void reopen() {
        require(flush_lesson_view(view),"Cannot persist first-case achievement");
        DestroyWindow(view);
        view=nullptr;
        open();
    }
    std::string feedback() const {
        const auto field=child(view,1004);
        require(field != nullptr,"Missing native compiler feedback");
        return edit_text(field);
    }
    std::string case_label() const {
        const auto field=child(view,1034);
        require(field != nullptr,"Missing native first/optional-case label");
        return edit_text(field);
    }
    void check_input(std::string_view input) {
        const auto input_field=child(view,1015), run=child(view,1011);
        require(input_field&&run&&IsWindowEnabled(run),
            "The compiler or standard-input field is unavailable");
        const auto value=wide(input);
        CHARRANGE selection{0,-1};
        SendMessageW(input_field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        SendMessageW(input_field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));
        pump();
        require(edit_text(input_field)==input,"The native input editor rewrote the case");
        SendMessageW(run,BM_CLICK,0,0);
        pump();
        wait_until([&]{
            return IsWindowEnabled(run) && case_label()!="Compilando…";
        },"Native MSVC compiler first-case run did not finish");
    }
};
LRESULT CALLBACK host_procedure(HWND item,UINT message,WPARAM wp,LPARAM lp) {
    return DefWindowProcW(item,message,wp,lp);
}
}

int main() try {
    require(SUCCEEDED(OleInitialize(nullptr)),"Cannot initialize Win32 OLE apartment");
    struct OleScope { ~OleScope() { OleUninitialize(); } } ole;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};
    require(InitCommonControlsEx(&controls)!=FALSE,"Cannot initialize Win32 controls");
    WNDCLASSW host{};host.lpfnWndProc=host_procedure;
    host.hInstance=GetModuleHandleW(nullptr);
    host.lpszClassName=L"LgaedCompilerFirstCaseTest";
    require(RegisterClassW(&host)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,
        "Cannot register first-case Windows host");

    { // A correct second case alone is not allowed to unlock the lesson.
        Scenario before(L"optional-first");
        require(!lesson_page_state(before.view).can_next,
            "An untouched compiler already unlocked the next page");
        before.check_input("2");
        require(!lesson_page_state(before.view).can_next &&
            !navigate_lesson_view(before.view,1),
            "An optional case granted an unearned first-case completion");
    }
    { // Exactly one valid first case unlocks immediately and survives reopen.
        Scenario f(L"required-first");
        f.check_input("1");
        require(f.feedback()=="✓ Correcto" && lesson_page_state(f.view).can_next &&
            f.case_label().find("Comprobación obligatoria: 1 de 1.")!=std::string::npos &&
            f.case_label().find("Pruebas opcionales comprobadas: 0 de 12.")!=std::string::npos,
            "A correct first case failed to unlock with twelve optional tests");
        const auto state_before=read_lesson_state(f.view);
        f.reopen();
        require(f.feedback()=="✓ Correcto" && lesson_page_state(f.view).can_next &&
            read_lesson_state(f.view)==state_before,
            "Reopening changed the earned first-case credit or saved student data");
        f.check_input("2");
        require(lesson_page_state(f.view).can_next,
            "Testing an optional second input revoked the first-case completion");
        f.check_input("un caso no declarado");
        require(lesson_page_state(f.view).can_next,
            "Testing an undeclared input revoked a previously earned completion");
        require(navigate_lesson_view(f.view,1)&&lesson_page_state(f.view).index==1,
            "A first-case achievement did not unlock actual Win32 page navigation");
    }
    std::cout<<"PASS native.compiler_puzzles: 13 inputs, first required only, optional-first blocked, reopen and 12 optional cases preserved\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL native.compiler_puzzles first case: "<<error.what()<<'\n';
    return 1;
}

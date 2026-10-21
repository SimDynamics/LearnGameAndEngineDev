#include "core/profile.hpp"
#include "platform/windows/environment.hpp"
#include "native_build_config.hpp"
#include "presentation/windows/lesson_view.hpp"

#include <windows.h>
#include <shellapi.h>
#include <ole2.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {

constexpr wchar_t window_class[] = L"LearnGameAndEngineDev.NativeShell";
constexpr UINT_PTR smoke_timer = 1;
constexpr DWORD window_style = WS_OVERLAPPEDWINDOW;

struct WindowState {
    lgaed::WindowsEnvironment environment;
    std::array<HWND, 5> controls{};
    HFONT font = nullptr;
    bool painted = false;
    int exit_code = 0;
    HWND lesson = nullptr;
    std::string startup_error;
};

std::wstring widen_error(const std::exception& error) {
    const std::string message(error.what());
    const int size = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return L"No se pudo iniciar la base nativa Windows.";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, result.data(), size);
    result.pop_back();
    return result;
}

void layout_controls(HWND window, WindowState& state) {
    if (state.lesson) {
        RECT area{}; GetClientRect(window, &area);
        MoveWindow(state.lesson, 0, 0, area.right, area.bottom, TRUE);
        return;
    }
    const auto dpi = GetDpiForWindow(window);
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    RECT client{};
    GetClientRect(window, &client);
    const int margin = scale(24);
    const int width = (std::max)(scale(100), static_cast<int>(client.right) - margin * 2);
    const std::array<int, 5> heights{scale(34), scale(42), scale(30), scale(112), scale(56)};
    int top = margin;
    for (std::size_t i = 0; i < state.controls.size(); ++i) {
        MoveWindow(state.controls[i], margin, top, width, heights[i], TRUE);
        top += heights[i] + scale(12);
    }
    HFONT next_font = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (next_font) {
        for (const auto control : state.controls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
        }
        if (state.font) {
            DeleteObject(state.font);
        }
        state.font = next_font;
    }
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    switch (message) {
    case WM_GETMINMAXINFO: {
        const auto dpi = GetDpiForWindow(window);
        RECT minimum{0, 0, MulDiv(640, static_cast<int>(dpi), 96),
            MulDiv(390, static_cast<int>(dpi), 96)};
        if (AdjustWindowRectExForDpi(&minimum, window_style, FALSE, 0, dpi)) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        }
        return 0;
    }
    case WM_CREATE: {
        if (!state->environment.options.smoke_test) {
            try {
                state->lesson = lgaed::presentation::create_lesson_view(window, state->environment,
                    std::filesystem::path(lgaed::build::source_root) / L"app/src/main/assets/content");
                layout_controls(window, *state);
                return 0;
            } catch (const std::exception& error) {
                state->startup_error = error.what();
                return -1;
            }
        }
        const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        const auto label = [window, instance](const wchar_t* text) {
            return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, window, nullptr, instance, nullptr);
        };
        state->controls[0] = label(L"LearnGameAndEngineDev");
        state->controls[1] = label(L"Base Windows nativa · C++20 · Win32 x64");
        const auto profile = L"Perfil: " + std::wstring(lgaed::profile_name(state->environment.options.profile));
        state->controls[2] = label(profile.c_str());
        state->controls[3] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            state->environment.paths.root.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP
                | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, window, nullptr, instance, nullptr);
        state->controls[4] = label(L"Recuperación curricular e importación del progreso: pendientes.\r\n"
            L"El contenido educativo importado permanece intacto.");
        for (const auto control : state->controls) {
            if (!control) {
                return -1;
            }
        }
        layout_controls(window, *state);
        return 0;
    }
    case WM_SIZE:
        layout_controls(window, *state);
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        layout_controls(window, *state);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (!state->painted) {
            state->painted = true;
            try {
                lgaed::write_qa_window_record(state->environment, GetCurrentProcessId(),
                    GetDpiForWindow(window), state->environment.options.smoke_test);
                if (state->environment.options.smoke_test
                    && !SetTimer(window, smoke_timer, 3000, nullptr)) {
                    throw std::runtime_error("Cannot schedule smoke-test window closure");
                }
            } catch (const std::exception&) {
                state->exit_code = 3;
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == smoke_timer) {
            KillTimer(window, smoke_timer);
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state->lesson && !lgaed::presentation::flush_lesson_view(state->lesson)) return 0;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state->font) {
            DeleteObject(state->font);
            state->font = nullptr;
        }
        PostQuitMessage(state->exit_code);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    bool qa_requested = false;
    try {
        struct OleApartment {
            OleApartment() { if (FAILED(OleInitialize(nullptr))) throw std::runtime_error("Cannot initialize native Windows controls"); }
            ~OleApartment() { OleUninitialize(); }
        } apartment;
        static_assert(sizeof(void*) == 8, "The native application requires a 64-bit target");
        const auto default_profile = lgaed::parse_profile(lgaed::build::default_profile);
        if (!default_profile) {
            throw std::runtime_error("The configured default profile is invalid");
        }
        int count = 0;
        auto** values = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!values) {
            throw std::runtime_error("Cannot parse the Unicode command line");
        }
        const auto free_arguments = [](wchar_t** value) { LocalFree(value); };
        const std::unique_ptr<wchar_t*, decltype(free_arguments)> arguments(values, free_arguments);
        std::vector<std::wstring_view> options;
        for (int i = 1; i < count; ++i) {
            options.emplace_back(values[i]);
            if (options.back() == L"--smoke-test" || options.back() == L"--qa-instance") {
                qa_requested = true;
            }
        }
        auto environment = lgaed::resolve_windows_environment(
            lgaed::parse_launch_options(options, *default_profile));
        lgaed::initialize_profile_directories(environment.paths);
        WindowState state{std::move(environment)};

        WNDCLASSEXW definition{};
        definition.cbSize = sizeof(definition);
        definition.lpfnWndProc = window_procedure;
        definition.hInstance = instance;
        definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        definition.lpszClassName = window_class;
        if (!RegisterClassExW(&definition)) {
            throw std::runtime_error("Cannot register the native application window");
        }
        auto title = L"LearnGameAndEngineDev — "
            + std::wstring(lgaed::profile_name(state.environment.options.profile));
        if (state.environment.options.qa_instance) {
            title += L" (QA " + *state.environment.options.qa_instance + L")";
        }
        const auto initial_dpi = GetDpiForSystem();
        RECT initial_size{0, 0, MulDiv(900, static_cast<int>(initial_dpi), 96),
            MulDiv(state.environment.options.smoke_test ? 440 : 720, static_cast<int>(initial_dpi), 96)};
        if (!AdjustWindowRectExForDpi(&initial_size, window_style, FALSE, 0, initial_dpi)) {
            throw std::runtime_error("Cannot calculate the initial DPI-aware window size");
        }
        const auto window = CreateWindowExW(0, window_class, title.c_str(), window_style,
            CW_USEDEFAULT, CW_USEDEFAULT, initial_size.right - initial_size.left,
            initial_size.bottom - initial_size.top, nullptr, nullptr, instance, &state);
        if (!window) {
            throw std::runtime_error(state.startup_error.empty() ? "Cannot create the native application window" : state.startup_error);
        }
        ShowWindow(window, show_command);
        UpdateWindow(window);
        MSG message{};
        BOOL result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return result == -1 ? 4 : static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        const auto text = widen_error(error);
        if (qa_requested) {
            OutputDebugStringW(text.c_str());
        } else {
            MessageBoxW(nullptr, text.c_str(), L"LearnGameAndEngineDev", MB_OK | MB_ICONERROR);
        }
        return 2;
    }
}

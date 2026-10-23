#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "platform/windows/narration_worker.hpp"
#include "native_build_config.hpp"
#include "core/scientific_calculator.hpp"
#include "platform/windows/state_store.hpp"

#include <commctrl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::presentation {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t tool_class[] = L"LearnGameAndEngineDev.NativeTool";
constexpr UINT calculate_message = WM_APP + 152;
constexpr UINT_PTR frame_timer = 1;
constexpr int back_id = 5201, expression_id = 5202, result_id = 5203, angle_id = 5204, history_id = 5205, evaluate_id = 5206;
constexpr int mode_id = 5210, rotation_id = 5211, reset_id = 5212, backend_id = 5213;
constexpr int first_key = 5300;
struct Key { const wchar_t* label; const wchar_t* text; bool function = false; };
constexpr std::array<Key,36> keys{{
    {L"sin",L"sin",true},{L"cos",L"cos",true},{L"tan",L"tan",true},{L"(",L"("},{L")",L")"},{L"CE",L""},
    {L"asin",L"asin",true},{L"acos",L"acos",true},{L"atan",L"atan",true},{L"7",L"7"},{L"8",L"8"},{L"9",L"9"},
    {L"ln",L"ln",true},{L"log",L"log",true},{L"exp",L"exp",true},{L"4",L"4"},{L"5",L"5"},{L"6",L"6"},
    {L"√",L"sqrt",true},{L"abs",L"abs",true},{L"!",L"!"},{L"1",L"1"},{L"2",L"2"},{L"3",L"3"},
    {L"π",L"pi"},{L"e",L"e"},{L"^",L"^"},{L"0",L"0"},{L".",L"."},{L"⌫",L"\b"},
    {L"+",L"+"},{L"−",L"-"},{L"×",L"*"},{L"÷",L"/"},{L"%",L"%"},{L"=",L"="}
}};
std::wstring text(HWND window) {
    const auto length = GetWindowTextLengthW(window); std::wstring value(static_cast<std::size_t>(length) + 1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size())))); return value;
}
std::string system_language() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{}; GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH);
    const auto value = utf8(name).substr(0,2); return value == "es" || value == "fr" ? value : "en";
}
void succeeded(HRESULT result,const char* message) { if (FAILED(result)) throw std::runtime_error(message); }
struct Tool;
LRESULT CALLBACK tool_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK calculator_body_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR) {
    if (message == calculate_message || message == WM_COMMAND || message == WM_VSCROLL || message == WM_MOUSEWHEEL || message == WM_DRAWITEM ||
        (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)) return SendMessageW(GetParent(window),message,wparam,lparam);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window,calculator_body_proc,1);
    return DefSubclassProc(window,message,wparam,lparam);
}
LRESULT CALLBACK expression_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR) {
    if (message == WM_GETDLGCODE) {
        auto value = DefSubclassProc(window,message,wparam,lparam);
        const auto key = lparam ? reinterpret_cast<MSG*>(lparam)->wParam : wparam;
        if (key == VK_RETURN) value |= DLGC_WANTALLKEYS; return value;
    }
    if (message == WM_KEYDOWN && wparam == VK_RETURN) { PostMessageW(GetParent(window),calculate_message,0,0); return 0; }
    if (message == WM_CHAR && (wparam == L'\r' || wparam == L'\n')) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window,expression_proc,1);
    return DefSubclassProc(window,message,wparam,lparam);
}
struct History { std::string expression, result; bool degrees = false; };
struct Vertex { float x,y,z,r,g,b; };
struct Tool {
    HWND window = nullptr, back = nullptr, heading = nullptr, expression = nullptr, result = nullptr, angle = nullptr, history_list = nullptr;
    HWND mode = nullptr, rotating = nullptr, reset = nullptr, backend = nullptr, canvas = nullptr, evaluate_button = nullptr, body = nullptr;
    std::array<HWND,keys.size()> keyboard{};
    HFONT font = nullptr, title_font = nullptr;
    std::optional<WindowsEnvironment> environment;
    std::function<void()> on_back;
    std::string locale = "en";
    std::vector<History> history;
    bool graphics = false, owned_by_window = false;
    std::uint64_t presented_frames = 0;
    int calculator_scroll = 0, calculator_extent = 0;
    double rotation = 0;
    std::chrono::steady_clock::time_point last_frame = std::chrono::steady_clock::now();
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapchain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11InputLayout> input_layout;
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11RasterizerState> solid, wire;

    Tool(std::optional<WindowsEnvironment> env,std::function<void()> callback,bool rendering)
        : environment(std::move(env)),on_back(std::move(callback)),graphics(rendering) {
        locale = system_language();
        if (environment) { state::StateStore store(environment->paths.root); const auto data = store.load(); locale = interface_language(data); }
    }
    ~Tool() {
        if (window) KillTimer(window,frame_timer);
        if (context) { context->ClearState(); context->Flush(); }
        if (font) DeleteObject(font); if (title_font) DeleteObject(title_font);
    }
    std::string ui(std::string_view es,std::string_view en,std::string_view fr) const { return std::string(locale == "es" ? es : locale == "fr" ? fr : en); }
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96); }
    HWND control(const wchar_t* type,std::string_view label,DWORD style,int id) {
        const auto caption = wide(label);
        auto control = CreateWindowExW(0,type,caption.c_str(),WS_CHILD | WS_VISIBLE | style,0,0,1,1,body ? body : window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if (!control) throw std::runtime_error("No se pudo crear una herramienta nativa.");
        reader_theme::apply(control,reader_theme::Surface::canvas); return control;
    }
    void initialize(HWND owner) {
        window = owner; reader_theme::apply(window,reader_theme::Surface::canvas);
        back = control(L"BUTTON",ui("Volver","Back","Retour"),WS_TABSTOP | BS_PUSHBUTTON,back_id);
        heading = control(L"STATIC",graphics ? ui("Motor gráfico · Direct3D 11","Graphics engine · Direct3D 11","Moteur graphique · Direct3D 11") :
            ui("Calculadora científica","Scientific calculator","Calculatrice scientifique"),SS_LEFT,5220);
        if (graphics) initialize_graphics(); else initialize_calculator();
        font = CreateFontW(-px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        EnumChildWindows(window,[](HWND item,LPARAM value) -> BOOL { SendMessageW(item,WM_SETFONT,static_cast<WPARAM>(value),TRUE); return TRUE; },reinterpret_cast<LPARAM>(font));
        title_font = CreateFontW(-px(27),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE); resize();
    }
    void initialize_calculator() {
        body = CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Calculadora",WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            0,0,1,1,window,reinterpret_cast<HMENU>(5222),GetModuleHandleW(nullptr),nullptr);
        if (!body) throw std::runtime_error("No se pudo crear el área de la calculadora.");
        reader_theme::apply(body,reader_theme::Surface::canvas); SetWindowSubclass(body,calculator_body_proc,1,0);
        expression = control(L"EDIT","",WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,expression_id);
        reader_theme::apply(expression,reader_theme::Surface::field); SendMessageW(expression,EM_LIMITTEXT,8192,0);
        SetWindowSubclass(expression,expression_proc,1,0);
        result = control(L"EDIT","",WS_TABSTOP | ES_READONLY | ES_MULTILINE | WS_VSCROLL,result_id);
        angle = control(L"COMBOBOX","",WS_TABSTOP | CBS_DROPDOWNLIST,angle_id);
        for (const auto& label : {ui("Radianes","Radians","Radians"),ui("Grados","Degrees","Degrés")}) SendMessageW(angle,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(label).c_str()));
        evaluate_button = control(L"BUTTON",ui("Calcular","Calculate","Calculer"),WS_TABSTOP | BS_PUSHBUTTON,evaluate_id); reader_theme::primary_button(evaluate_button);
        history_list = control(L"LISTBOX","",WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,history_id);
        for (std::size_t index = 0; index < keys.size(); ++index) keyboard[index] = control(L"BUTTON",utf8(keys[index].label),WS_TABSTOP | BS_PUSHBUTTON,first_key + static_cast<int>(index));
        state::StateStore store(environment->paths.root); const auto data = store.load();
        const auto angle_unit = state::string_value(data,"native_windows","calculator/angle","rad"); SendMessageW(angle,CB_SETCURSEL,angle_unit == "deg" ? 1 : 0,0);
        const auto value = state::find(data,"native_windows","calculator/history/v1");
        if (value) {
            const auto rows = std::get_if<std::vector<std::string>>(value);
            if (!rows || rows->size() > 30) throw std::runtime_error("Historial de cálculo no compatible; se conserva sin reemplazar.");
            for (const auto& record : *rows) {
                const auto delimiter = record.find(':'); std::size_t length = 0;
                if (delimiter == record.npos) throw std::runtime_error("Historial de cálculo incompleto.");
                const auto parsed = std::from_chars(record.data(),record.data() + delimiter,length);
                if (parsed.ec != std::errc{} || parsed.ptr != record.data() + delimiter || length > 8192 || length + delimiter + 3 > record.size()) throw std::runtime_error("Historial de cálculo incompleto.");
                const auto start = delimiter + 1; const auto unit = record[start + length];
                if ((unit != 'D' && unit != 'R') || record[start + length + 1] != ':') throw std::runtime_error("Historial de cálculo no compatible.");
                history.push_back({record.substr(start,length),record.substr(start + length + 2),unit == 'D'});
            }
        }
        refresh_history();
    }
    void save_calculator() {
        state::StateStore store(environment->paths.root); auto data = store.load(); std::vector<std::string> records;
        for (const auto& item : history) records.push_back(std::to_string(item.expression.size()) + ':' + item.expression + (item.degrees ? 'D' : 'R') + ':' + item.result);
        data.preferences["native_windows"]["calculator/history/v1"] = std::move(records);
        data.preferences["native_windows"]["calculator/angle"] = std::string(SendMessageW(angle,CB_GETCURSEL,0,0) == 1 ? "deg" : "rad"); store.save(data);
    }
    void refresh_history() {
        SendMessageW(history_list,LB_RESETCONTENT,0,0);
        for (const auto& item : history) {
            const auto caption = wide(item.expression + " = " + item.result + (item.degrees ? " [deg]" : " [rad]"));
            SendMessageW(history_list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(caption.c_str()));
        }
    }
    void calculate() {
        const auto source = utf8(text(expression)); const bool degrees = SendMessageW(angle,CB_GETCURSEL,0,0) == 1;
        try {
            const auto value = math::calculate(source,degrees ? math::AngleUnit::degrees : math::AngleUnit::radians);
            const auto output = math::format_calculation(value); SetWindowTextW(result,wide(output).c_str());
            history.insert(history.begin(),{source,output,degrees}); if (history.size() > 30) history.resize(30);
            save_calculator(); refresh_history();
        } catch (const std::exception& error) {
            SetWindowTextW(result,wide(ui("Error: ","Error: ","Erreur : ") + error.what()).c_str());
        }
        SetFocus(expression);
    }
    void insert(std::size_t index) {
        if (index >= keys.size()) return;
        if (index == 5) { SetWindowTextW(expression,L""); SetWindowTextW(result,L""); SetFocus(expression); return; }
        if (index + 1 == keys.size()) { calculate(); return; }
        SetFocus(expression);
        if (index == 29) { SendMessageW(expression,WM_CHAR,L'\b',1); return; }
        DWORD first = 0, last = 0; SendMessageW(expression,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        auto replacement = std::wstring(keys[index].text);
        if (keys[index].function) {
            const auto original = text(expression); replacement += L'(' + original.substr(first,last - first) + L')';
        }
        SendMessageW(expression,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(replacement.c_str()));
        if (keys[index].function) SendMessageW(expression,EM_SETSEL,first + wcslen(keys[index].text) + 1,last + wcslen(keys[index].text) + 1);
    }
    void initialize_graphics() {
        mode = control(L"COMBOBOX","",WS_TABSTOP | CBS_DROPDOWNLIST,mode_id);
        for (const auto& label : {ui("Triángulo sólido","Solid triangle","Triangle plein"),ui("Estructura de alambre","Wireframe","Fil de fer")}) SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(label).c_str()));
        SendMessageW(mode,CB_SETCURSEL,0,0);
        rotating = control(L"BUTTON",ui("Rotación","Rotation","Rotation"),WS_TABSTOP | BS_AUTOCHECKBOX,rotation_id); SendMessageW(rotating,BM_SETCHECK,BST_CHECKED,0);
        reset = control(L"BUTTON",ui("Reiniciar","Reset","Réinitialiser"),WS_TABSTOP | BS_PUSHBUTTON,reset_id);
        backend = control(L"STATIC","",SS_LEFT,backend_id);
        canvas = control(L"STATIC","",SS_BLACKRECT,5221);
        DXGI_SWAP_CHAIN_DESC description{}; description.BufferDesc.Width = 1; description.BufferDesc.Height = 1;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; description.BufferCount = 2; description.OutputWindow = canvas;
        description.Windowed = TRUE; description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        constexpr D3D_FEATURE_LEVEL supported[]{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL level{}; bool software = false;
        auto created = D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,supported,3,D3D11_SDK_VERSION,
            &description,swapchain.GetAddressOf(),device.GetAddressOf(),&level,context.GetAddressOf());
        if (FAILED(created)) { software = true; created = D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,supported,3,D3D11_SDK_VERSION,
            &description,swapchain.ReleaseAndGetAddressOf(),device.ReleaseAndGetAddressOf(),&level,context.ReleaseAndGetAddressOf()); }
        succeeded(created,"No se pudo iniciar Direct3D 11 con hardware ni WARP.");
        ComPtr<IDXGIDevice> dxgi; succeeded(device.As(&dxgi),"No se pudo consultar el dispositivo gráfico.");
        ComPtr<IDXGIAdapter> adapter; succeeded(dxgi->GetAdapter(adapter.GetAddressOf()),"No se pudo consultar el adaptador.");
        DXGI_ADAPTER_DESC details{}; succeeded(adapter->GetDesc(&details),"No se pudo consultar el nombre del adaptador.");
        const auto feature = std::to_string((static_cast<unsigned>(level) >> 12) & 0xf) + "." + std::to_string((static_cast<unsigned>(level) >> 8) & 0xf);
        const auto description_text = "Direct3D 11 · " + utf8(details.Description) + " · Feature Level " + feature + (software ? " · WARP (software)" : " · hardware");
        SetWindowTextW(backend,wide(description_text).c_str());
        constexpr char shader[] = R"(
            struct Vertex { float3 position : POSITION; float3 color : COLOR; };
            struct Pixel { float4 position : SV_POSITION; float3 color : COLOR; };
            Pixel VSMain(Vertex input) { Pixel result; result.position=float4(input.position,1); result.color=input.color; return result; }
            float4 PSMain(Pixel input) : SV_TARGET { return float4(input.color,1); }
        )";
        ComPtr<ID3DBlob> vs, ps, error;
        succeeded(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"VSMain","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,vs.GetAddressOf(),error.GetAddressOf()),"No se pudo compilar el shader de vértices.");
        succeeded(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"PSMain","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,ps.GetAddressOf(),error.ReleaseAndGetAddressOf()),"No se pudo compilar el shader de píxeles.");
        succeeded(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex_shader.GetAddressOf()),"No se pudo crear el shader de vértices.");
        succeeded(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel_shader.GetAddressOf()),"No se pudo crear el shader de píxeles.");
        constexpr D3D11_INPUT_ELEMENT_DESC input[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
        succeeded(device->CreateInputLayout(input,2,vs->GetBufferPointer(),vs->GetBufferSize(),input_layout.GetAddressOf()),"No se pudo crear el formato de vértice.");
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = sizeof(Vertex) * 3; buffer.Usage = D3D11_USAGE_DYNAMIC; buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        succeeded(device->CreateBuffer(&buffer,nullptr,vertices.GetAddressOf()),"No se pudo crear el buffer gráfico.");
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
        succeeded(device->CreateRasterizerState(&raster,solid.GetAddressOf()),"No se pudo crear el rasterizador sólido."); raster.FillMode = D3D11_FILL_WIREFRAME;
        succeeded(device->CreateRasterizerState(&raster,wire.GetAddressOf()),"No se pudo crear el rasterizador de alambre.");
        if (!SetTimer(window,frame_timer,16,nullptr)) throw std::runtime_error("No se pudo iniciar el renderizado.");
    }
    void resize() {
        RECT area{}; GetClientRect(window,&area); const int width = std::max(px(160),static_cast<int>(area.right) - px(48));
        MoveWindow(back,px(24),px(20),px(72),px(40),TRUE); MoveWindow(heading,px(24),px(80),width,px(45),TRUE);
        if (graphics) {
            MoveWindow(mode,px(24),px(138),px(220),px(180),TRUE); MoveWindow(rotating,px(260),px(138),px(140),px(36),TRUE);
            MoveWindow(reset,px(420),px(138),px(130),px(36),TRUE); MoveWindow(backend,px(24),px(184),width,px(46),TRUE);
            MoveWindow(canvas,px(24),px(240),width,std::max(px(80),static_cast<int>(area.bottom) - px(264)),TRUE);
            if (swapchain) {
                context->OMSetRenderTargets(0,nullptr,nullptr); target.Reset();
                succeeded(swapchain->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0),"No se pudo redimensionar la superficie Direct3D.");
                ComPtr<ID3D11Texture2D> surface; succeeded(swapchain->GetBuffer(0,IID_PPV_ARGS(surface.GetAddressOf())),"No se pudo consultar el back buffer.");
                succeeded(device->CreateRenderTargetView(surface.Get(),nullptr,target.GetAddressOf()),"No se pudo crear la superficie de renderizado.");
            }
            render(); return;
        }
        const int body_height = std::max(px(50),static_cast<int>(area.bottom) - px(130));
        MoveWindow(body,0,px(130),static_cast<int>(area.right),body_height,TRUE);
        const int columns = 6, gap = px(6), cell_width = (width - gap * (columns - 1)) / columns;
        const int cell_height = std::clamp((static_cast<int>(area.bottom) - px(420)) / 6,px(26),px(44));
        const int history_top = px(182) + 6 * (cell_height + gap) + px(12);
        const int history_height = std::max(px(90),body_height - history_top - px(24));
        calculator_extent = history_top + history_height + px(24);
        calculator_scroll = std::clamp(calculator_scroll,0,std::max(0,calculator_extent - body_height));
        MoveWindow(expression,px(24),px(10) - calculator_scroll,width,px(42),TRUE); MoveWindow(result,px(24),px(62) - calculator_scroll,width,px(60),TRUE);
        MoveWindow(angle,px(24),px(134) - calculator_scroll,px(160),px(180),TRUE); MoveWindow(evaluate_button,px(198),px(134) - calculator_scroll,px(160),px(36),TRUE);
        for (std::size_t index = 0; index < keys.size(); ++index) MoveWindow(keyboard[index],px(24) + static_cast<int>(index % columns) * (cell_width + gap),
            px(182) + static_cast<int>(index / columns) * (cell_height + gap) - calculator_scroll,cell_width,cell_height,TRUE);
        MoveWindow(history_list,px(24),history_top - calculator_scroll,width,history_height,TRUE);
        SCROLLINFO info{sizeof(info),SIF_RANGE | SIF_PAGE | SIF_POS}; info.nMax = calculator_extent - 1; info.nPage = static_cast<UINT>(body_height); info.nPos = calculator_scroll;
        SetScrollInfo(body,SB_VERT,&info,TRUE);
        reader_theme::refresh_scrollbars(body);
    }
    void render(bool present = true) {
        if (!graphics || !target || !IsWindowVisible(window)) return;
        const auto now = std::chrono::steady_clock::now();
        if (present) {
            if (SendMessageW(rotating,BM_GETCHECK,0,0) == BST_CHECKED) rotation += std::min(.1,std::chrono::duration<double>(now - last_frame).count()) * .7;
            last_frame = now;
        }
        constexpr Vertex original[]{{0,.65f,.5f,1,.2f,.2f},{.65f,-.6f,.5f,.2f,1,.3f},{-.65f,-.6f,.5f,.2f,.4f,1}};
        D3D11_MAPPED_SUBRESOURCE mapped{}; succeeded(context->Map(vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"No se pudo actualizar la escena.");
        RECT area{}; GetClientRect(canvas,&area); const auto aspect = static_cast<float>(std::max<LONG>(1,area.bottom)) / std::max<LONG>(1,area.right);
        auto* output = static_cast<Vertex*>(mapped.pData);
        for (std::size_t index = 0; index < 3; ++index) {
            output[index] = original[index]; const auto x = original[index].x, y = original[index].y;
            output[index].x = static_cast<float>((x * std::cos(rotation) - y * std::sin(rotation)) * aspect);
            output[index].y = static_cast<float>(x * std::sin(rotation) + y * std::cos(rotation));
        }
        context->Unmap(vertices.Get(),0);
        constexpr float background[]{.075f,.085f,.10f,1}; context->ClearRenderTargetView(target.Get(),background);
        auto* surface = target.Get(); context->OMSetRenderTargets(1,&surface,nullptr);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(std::max<LONG>(1,area.right)),static_cast<float>(std::max<LONG>(1,area.bottom)),0,1}; context->RSSetViewports(1,&viewport);
        context->RSSetState(SendMessageW(mode,CB_GETCURSEL,0,0) == 1 ? wire.Get() : solid.Get());
        auto* buffer = vertices.Get(); const UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset); context->IASetInputLayout(input_layout.Get()); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex_shader.Get(),nullptr,0); context->PSSetShader(pixel_shader.Get(),nullptr,0); context->Draw(3,0);
        if (present) { succeeded(swapchain->Present(1,0),"El dispositivo gráfico dejó de presentar imágenes."); ++presented_frames; }
    }
    GraphicsFrame inspect() {
        if (!graphics || !swapchain || !device || !target) throw std::invalid_argument("No hay una escena Direct3D activa.");
        // DISCARD swap-chain contents after Present are unspecified. Redraw
        // the unchanged scene before readback without advancing or presenting.
        render(false);
        ComPtr<ID3D11Texture2D> buffer; succeeded(swapchain->GetBuffer(0,IID_PPV_ARGS(buffer.GetAddressOf())),"No se pudo consultar el frame real.");
        D3D11_TEXTURE2D_DESC description{}; buffer->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging; succeeded(device->CreateTexture2D(&description,nullptr,staging.GetAddressOf()),"No se pudo leer la escena gráfica.");
        context->CopyResource(staging.Get(),buffer.Get()); D3D11_MAPPED_SUBRESOURCE data{};
        succeeded(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&data),"No se pudo inspeccionar el frame gráfico.");
        GraphicsFrame frame{presented_frames,0,rotation,SendMessageW(mode,CB_GETCURSEL,0,0) == 1,utf8(text(backend))};
        for (UINT y = 0; y < description.Height; ++y) {
            const auto pixels = static_cast<const unsigned char*>(data.pData) + y * data.RowPitch;
            for (UINT x = 0; x < description.Width; ++x) {
                const auto pixel = pixels + x * 4;
                // Background is (19,22,26), while the actual interpolated
                // triangle contains at least one channel well above 64.
                if (pixel[0] > 64 || pixel[1] > 64 || pixel[2] > 64) ++frame.colored_pixels;
            }
        }
        context->Unmap(staging.Get(),0); return frame;
    }
    void command(int id,int notification) {
        if (id == back_id && notification == BN_CLICKED) { const auto callback = on_back; if (callback) callback(); return; }
        if (graphics) {
            if (id == reset_id && notification == BN_CLICKED) { rotation = 0; SendMessageW(mode,CB_SETCURSEL,0,0); last_frame = std::chrono::steady_clock::now(); }
            render(); return;
        }
        if (id == evaluate_id && notification == BN_CLICKED) calculate();
        if (id >= first_key && id < first_key + static_cast<int>(keys.size()) && notification == BN_CLICKED) insert(static_cast<std::size_t>(id - first_key));
        if (id == angle_id && notification == CBN_SELCHANGE) save_calculator();
        if (id == history_id && notification == LBN_DBLCLK) {
            const auto selected = SendMessageW(history_list,LB_GETCURSEL,0,0);
            if (selected >= 0 && static_cast<std::size_t>(selected) < history.size()) {
                const auto& item = history[static_cast<std::size_t>(selected)]; SetWindowTextW(expression,wide(item.expression).c_str()); SendMessageW(angle,CB_SETCURSEL,item.degrees ? 1 : 0,0); SetFocus(expression);
            }
        }
    }
};
LRESULT CALLBACK tool_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* self = reinterpret_cast<Tool*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<Tool*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window,message,wparam,lparam);
    try {
        LRESULT color = 0; if (reader_theme::control_color(message,wparam,lparam,color)) return color;
        switch (message) {
        case WM_CREATE: self->initialize(window); return 0;
        case WM_SIZE: self->resize(); return 0;
        case WM_VSCROLL: {
            if (self->graphics) break;
            SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(self->body,SB_VERT,&info);
            switch (LOWORD(wparam)) {
            case SB_LINEUP: self->calculator_scroll -= self->px(35); break;
            case SB_LINEDOWN: self->calculator_scroll += self->px(35); break;
            case SB_PAGEUP: self->calculator_scroll -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: self->calculator_scroll += static_cast<int>(info.nPage); break;
            case SB_THUMBPOSITION: case SB_THUMBTRACK: self->calculator_scroll = info.nTrackPos; break;
            case SB_TOP: self->calculator_scroll = 0; break;
            case SB_BOTTOM: self->calculator_scroll = self->calculator_extent; break;
            }
            self->resize(); return 0;
        }
        case WM_MOUSEWHEEL:
            if (!self->graphics) { self->calculator_scroll -= MulDiv(GET_WHEEL_DELTA_WPARAM(wparam),self->px(90),WHEEL_DELTA); self->resize(); return 0; } break;
        case WM_COMMAND: self->command(LOWORD(wparam),HIWORD(wparam)); return 0;
        case calculate_message: self->calculate(); return 0;
        case WM_TIMER: if (wparam == frame_timer) self->render(); return 0;
        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (item.CtlID == back_id) {
                const auto palette = reader_theme::palette(); const auto fill = CreateSolidBrush(palette.accent);
                FillRect(item.hDC,&item.rcItem,fill); DeleteObject(fill);
                auto area = item.rcItem; InflateRect(&area,-self->px(22),-self->px(10));
                draw_icon(item.hDC,area,Icon::back,palette.high_contrast ? palette.text : RGB(8,12,14));
                if (item.itemState & ODS_FOCUS) DrawFocusRect(item.hDC,&item.rcItem); return TRUE;
            }
            if (reader_theme::draw_button(item)) return TRUE; break;
        }
        case WM_ERASEBKGND: { RECT area{}; GetClientRect(window,&area); FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(reader_theme::Surface::canvas)); return 1; }
        case WM_DESTROY: KillTimer(window,frame_timer); return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0); if (self->owned_by_window) delete self; break;
        }
    } catch (const std::exception& error) {
        if (message == WM_CREATE) { OutputDebugStringW(wide(error.what()).c_str()); return -1; }
        if (self->graphics) { KillTimer(window,frame_timer); SetWindowTextW(self->backend,wide(error.what()).c_str()); }
        else SetWindowTextW(self->result,wide(error.what()).c_str());
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
HWND create(HWND parent,std::optional<WindowsEnvironment> environment,std::function<void()> on_back,bool graphics) {
    static const bool registered = [] { WNDCLASSW type{}; type.lpfnWndProc = tool_proc; type.hInstance = GetModuleHandleW(nullptr); type.hCursor = LoadCursorW(nullptr,IDC_ARROW); type.lpszClassName = tool_class;
        return RegisterClassW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS; }();
    if (!registered) throw std::runtime_error("No se pudo registrar la herramienta Windows.");
    auto state = std::make_unique<Tool>(std::move(environment),std::move(on_back),graphics);
    const auto window = CreateWindowExW(WS_EX_CONTROLPARENT,tool_class,graphics ? L"Direct3D 11" : L"Calculadora científica",WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),state.get());
    if (!window) throw std::runtime_error("No se pudo iniciar la herramienta nativa.");
    state->owned_by_window = true; state.release(); return window;
}
}
namespace {
using namespace winrt::Windows::Data::Json;
constexpr wchar_t calculator_class[]=L"LearnGameAndEngineDev.CanonicalCalculator";
constexpr UINT calculator_result_message=WM_APP+153;
constexpr int advanced_id=5240,edit_keys_id=5241,expression_panel_id=5242,input_label_id=5243,result_label_id=5244,error_id=5245,basic_label_id=5246,advanced_label_id=5247,paste_result_id=5248,scroll_up_id=5249,scroll_down_id=5250;
struct CalculatorKey {std::string id,label,action,name;};
const std::vector<CalculatorKey>& calculator_keys(){static const std::vector<CalculatorKey> value{
    {"calculator_key_sin","sin","Function","sin"},{"calculator_key_cos","cos","Function","cos"},{"calculator_key_tan","tan","Function","tan"},{"calculator_key_log","log","Function","log"},{"calculator_key_ln","ln","Function","ln"},{"calculator_key_subscript","xᵢ","Subscript",""},
    {"calculator_key_parentheses","( )","Parentheses",""},{"calculator_key_power","xʸ","Power",""},{"calculator_key_root","√","Root",""},{"calculator_key_fraction","a/b","Fraction",""},{"calculator_key_pi","π","Symbol","π"},{"calculator_key_e","e","Symbol","e"},
    {"calculator_key_7","7","text","7"},{"calculator_key_8","8","text","8"},{"calculator_key_9","9","text","9"},{"calculator_key_divide","÷","Symbol","÷"},{"calculator_key_percent","%","Symbol","%"},
    {"calculator_key_4","4","text","4"},{"calculator_key_5","5","text","5"},{"calculator_key_6","6","text","6"},{"calculator_key_multiply","×","Symbol","×"},{"calculator_clear","C","clear",""},
    {"calculator_key_1","1","text","1"},{"calculator_key_2","2","text","2"},{"calculator_key_3","3","text","3"},{"calculator_key_subtract","−","Symbol","−"},{"calculator_key_backspace","⌫","backspace",""},
    {"calculator_key_0","0","text","0"},{"calculator_key_decimal",".","text","."},{"calculator_key_left","←","left",""},{"calculator_key_right","→","right",""},{"calculator_key_add","+","Symbol","+"},
    {"calculator_key_asin","sin⁻¹","Function","arcsin"},{"calculator_key_acos","cos⁻¹","Function","arccos"},{"calculator_key_atan","tan⁻¹","Function","arctan"},{"calculator_key_nth_root","ⁿ√","NthRoot",""},{"calculator_key_absolute","|x|","AbsoluteValue",""},{"calculator_key_factorial","!","Symbol","!"},{"calculator_key_combinations","nCr","Symbol","nCr"},{"calculator_key_permutations","nPr","Symbol","nPr"}};return value;}
std::vector<std::string> default_calculator_order(bool advanced){const auto& all=calculator_keys();std::vector<std::string> result;for(std::size_t index=0;index<12;++index)result.push_back(all[index].id);for(std::size_t index=12;index<32;++index){result.push_back(all[index].id);if((index-12)%5==4)result.emplace_back();}if(advanced){for(std::size_t index=32;index<40;++index)result.push_back(all[index].id);while(result.size()%6)result.emplace_back();}return result;}
std::vector<std::string> restore_calculator_order(std::string_view saved,bool advanced){const auto defaults=default_calculator_order(advanced);std::vector<std::string> result;std::vector<std::string> used;try{const auto array=JsonArray::Parse(wide(saved));if(array.Size()>256||array.Size()==0)return defaults;for(const auto& item:array){const auto key=item.ValueType()==JsonValueType::String?utf8(item.GetString()):std::string{};if(!key.empty()&&(std::ranges::find(defaults,key)==defaults.end()||std::ranges::find(used,key)!=used.end()))result.emplace_back();else{result.push_back(key);if(!key.empty())used.push_back(key);}}}catch(...){return defaults;}while(result.size()<defaults.size())result.emplace_back();for(const auto& key:defaults)if(!key.empty()&&std::ranges::find(used,key)==used.end()){const auto empty=std::ranges::find(result,std::string{});if(empty!=result.end())*empty=key;else result.push_back(key);used.push_back(key);}while(result.size()%6)result.emplace_back();return result;}
std::string order_json(const std::vector<std::string>& order){JsonArray result;for(const auto& id:order)result.Append(id.empty()?JsonValue::CreateNullValue():JsonValue::CreateStringValue(wide(id)));return utf8(result.Stringify());}
bool calculator_boolean(const state::StateData& data,std::string_view key){const auto* value=state::find(data,"scientific_calculator",key);const auto* boolean=value?std::get_if<bool>(value):nullptr;return boolean&&*boolean;}
struct Calculator;
LRESULT CALLBACK canonical_calculator_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK calculator_keypad_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){if(message==WM_COMMAND||message==WM_DRAWITEM||message==WM_MOUSEWHEEL||message==WM_VSCROLL||(message>=WM_CTLCOLORMSGBOX&&message<=WM_CTLCOLORSTATIC))return SendMessageW(GetParent(window),message,wp,lp);if(message==WM_NCDESTROY)RemoveWindowSubclass(window,calculator_keypad_proc,2);return DefSubclassProc(window,message,wp,lp);}
struct Calculator {
    HWND window=nullptr,close=nullptr,expression=nullptr,result=nullptr,angle=nullptr,evaluate=nullptr,keypad=nullptr,expression_panel=nullptr,basic_label=nullptr,advanced_label=nullptr,advanced_switch=nullptr,edit_keys=nullptr,input_label=nullptr,result_label=nullptr,error_label=nullptr,paste=nullptr,scroll_up=nullptr,scroll_down=nullptr;
    WindowsEnvironment environment;CalculatorPreferences preference_access;std::shared_ptr<windows::NarrationWorker> backend;std::function<void()> on_back;std::function<void(std::string_view)> paste_result;
    std::string locale="es",result_latex,error;bool degrees=false,advanced=false,editing=false,initializing=true,owned=false,evaluating=false;int selected=-1,scroll=0,basic_scroll=0,advanced_scroll=0,keypad_extent=0;HFONT font=nullptr,label_font=nullptr;HBRUSH calculator_background=nullptr;std::vector<HWND> buttons;std::vector<std::string> basic_order,advanced_order;std::uint64_t evaluation_serial=0;std::jthread calculation;
    struct Reply {std::mutex mutex;bool closed=false;std::string value,error;std::uint64_t serial=0;};std::shared_ptr<Reply> reply=std::make_shared<Reply>();
    Calculator(const WindowsEnvironment& env,std::shared_ptr<windows::NarrationWorker> worker,std::function<void()> back,std::function<void(std::string_view)> paste_callback,CalculatorPreferences preferences):environment(env),preference_access(std::move(preferences)),backend(std::move(worker)),on_back(std::move(back)),paste_result(std::move(paste_callback)){
        if(static_cast<bool>(preference_access.read)!=static_cast<bool>(preference_access.update))throw std::invalid_argument("Calculator preferences need a matching read/update owner");const auto data=read_preferences();locale=interface_language(data);if(!backend)backend=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
    }
    state::StateData read_preferences()const{if(preference_access.read)return preference_access.read();return state::StateStore(environment.paths.root).load();}
    ~Calculator(){cancel();{std::lock_guard lock(reply->mutex);reply->closed=true;}if(font)DeleteObject(font);if(label_font)DeleteObject(label_font);if(calculator_background)DeleteObject(calculator_background);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    std::string ui(std::string_view es,std::string_view en,std::string_view fr)const{return std::string(locale=="es"?es:locale=="fr"?fr:en);}
    HWND control(HWND parent,const wchar_t* type,std::string_view value,int id,DWORD style=0){const auto child=CreateWindowExW(0,type,wide(value).c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);if(!child)throw std::runtime_error("Cannot create calculator control");reader_theme::apply(child,reader_theme::Surface::canvas);if(font)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;}
    void cancel(){++evaluation_serial;if(calculation.joinable()){calculation.request_stop();calculation.join();}evaluating=false;if(evaluate)EnableWindow(evaluate,TRUE);}
    std::vector<std::string>& order(){return advanced?advanced_order:basic_order;}
    void initialize(HWND owner){window=owner;{reader_theme::set_accent(window,reader_theme::appearance_accent(read_preferences()));}reader_theme::apply(window,reader_theme::Surface::canvas);calculator_background=CreateSolidBrush(reader_theme::palette(window).high_contrast?reader_theme::palette(window).canvas:RGB(16,17,20));font=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");label_font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        close=control(window,L"BUTTON",ui("Cerrar","Close","Fermer"),back_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(close);basic_label=control(window,L"STATIC",ui("Básico","Basic","Basique"),basic_label_id,SS_CENTER|SS_CENTERIMAGE);advanced_label=control(window,L"STATIC",ui("Avanzado","Advanced","Avancé"),advanced_label_id,SS_CENTER|SS_CENTERIMAGE);advanced_switch=control(window,L"BUTTON",ui("Modo avanzado","Advanced mode","Mode avancé"),advanced_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(advanced_switch);edit_keys=control(window,L"BUTTON",ui("Editar","Edit","Modifier"),edit_keys_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(edit_keys);angle=control(window,L"BUTTON","RAD",angle_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(angle);
        expression_panel=control(window,L"STATIC","",expression_panel_id,WS_CLIPCHILDREN);keypad=control(window,L"STATIC","",5222,WS_CLIPCHILDREN|WS_VSCROLL);SetWindowSubclass(expression_panel,calculator_keypad_proc,2,0);SetWindowSubclass(keypad,calculator_keypad_proc,2,0);input_label=control(expression_panel,L"STATIC",ui("Expresión","Expression","Expression"),input_label_id,SS_LEFT|SS_CENTERIMAGE);evaluate=control(expression_panel,L"BUTTON","=",evaluate_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(evaluate);paste=control(expression_panel,L"BUTTON",ui("Pegar","Paste","Coller"),paste_result_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(paste);ShowWindow(paste,paste_result?SW_SHOW:SW_HIDE);
        MathExpressionEditorAccess access;access.request=[worker=backend](std::string_view request){return worker->math_editor(request);};access.changed=[this]{if(!initializing){invalidate();save();resize();}};access.submit=[this]{calculate();};access.cancel=[this]{command(back_id,BN_CLICKED);};access.expected_template="\\log(1)";access.language=locale;expression=create_math_expression_editor(expression_panel,expression_id,std::move(access));SendMessageW(expression,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        MathExpressionEditorAccess output;output.request=[worker=backend](std::string_view request){return worker->math_editor(request);};output.language=locale;result=create_math_expression_editor(expression_panel,result_id,std::move(output));set_math_expression_read_only(result,true);SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);result_label=control(expression_panel,L"STATIC",ui("Resultado","Result","Résultat"),result_label_id,SS_LEFT|SS_CENTERIMAGE);error_label=control(expression_panel,L"STATIC","",error_id,SS_LEFT);reader_theme::set_text_color(error_label,RGB(255,166,166));scroll_up=control(keypad,L"BUTTON","↑",scroll_up_id,WS_TABSTOP|BS_PUSHBUTTON);scroll_down=control(keypad,L"BUTTON","↓",scroll_down_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(scroll_up);reader_theme::icon_button(scroll_down);
        for(auto item:{basic_label,advanced_label,edit_keys})SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(label_font),TRUE);restore();initializing=false;rebuild_keys();resize();
    }
    void restore(){const auto data=read_preferences();degrees=state::string_value(data,"scientific_calculator","calculator_mode","RAD")=="DEG";advanced=calculator_boolean(data,"calculator_advanced");basic_order=restore_calculator_order(state::string_value(data,"scientific_calculator","calculator_order_basic",""),false);advanced_order=restore_calculator_order(state::string_value(data,"scientific_calculator","calculator_order_advanced",""),true);const auto draft=state::string_value(data,"scientific_calculator","calculator_draft","");if(!draft.empty()){try{const auto wrapper=JsonObject::Parse(wide(draft));const auto canonical=wrapper.GetNamedString(L"structured",L"");if(!canonical.empty())(void)restore_math_expression_draft(expression,utf8(canonical));}catch(...){/* Invalid older draft is retained until an actual edit replaces it. */}}
        const auto current=inspect_math_expression_editor(expression);const auto saved=state::string_value(data,"scientific_calculator","calculator_result","");if(!saved.empty()&&state::string_value(data,"scientific_calculator","calculator_result_input","")==current.latex&&state::string_value(data,"scientific_calculator","calculator_result_mode","")==(degrees?"DEG":"RAD")){if(set_math_expression_latex(result,saved))result_latex=saved;}SetWindowTextW(angle,degrees?L"DEG":L"RAD");
    }
    void save(){if(initializing)return;const auto mutation=[&](state::StateData& data){auto& prefs=data.preferences["scientific_calculator"];JsonObject wrapper;wrapper.Insert(L"structured",JsonValue::CreateStringValue(wide(inspect_math_expression_editor(expression).draft_json)));prefs["calculator_draft"]=utf8(wrapper.Stringify());prefs["calculator_mode"]=std::string(degrees?"DEG":"RAD");prefs["calculator_advanced"]=advanced;prefs["calculator_order_basic"]=order_json(basic_order);prefs["calculator_order_advanced"]=order_json(advanced_order);prefs["calculator_result"]=result_latex;prefs["calculator_result_input"]=result_latex.empty()?std::string{}:inspect_math_expression_editor(expression).latex;prefs["calculator_result_mode"]=result_latex.empty()?std::string{}:std::string(degrees?"DEG":"RAD");};if(preference_access.update)preference_access.update(mutation);else{state::StateStore store(environment.paths.root);auto data=store.load();mutation(data);store.save(data);}}
    void invalidate(){cancel();result_latex.clear();error.clear();if(result)(void)set_math_expression_latex(result,"");if(error_label)SetWindowTextW(error_label,L"");}
    void rebuild_keys(){for(auto child:buttons)DestroyWindow(child);buttons.clear();for(std::size_t index=0;index<order().size();++index){const auto found=std::ranges::find(calculator_keys(),order()[index],&CalculatorKey::id);if(found==calculator_keys().end()){buttons.push_back(nullptr);continue;}auto button=control(keypad,L"BUTTON",found->label,first_key+static_cast<int>(index),WS_TABSTOP|BS_PUSHBUTTON);reader_theme::primary_button(button);buttons.push_back(button);}ShowWindow(scroll_up,editing?SW_SHOW:SW_HIDE);ShowWindow(scroll_down,editing?SW_SHOW:SW_HIDE);InvalidateRect(edit_keys,nullptr,FALSE);InvalidateRect(advanced_switch,nullptr,FALSE);}
    void insert(std::size_t index){if(index>=order().size()||order()[index].empty())return;if(editing){if(selected==static_cast<int>(index)){selected=-1;InvalidateRect(buttons[index],nullptr,FALSE);return;}if(selected<0){selected=static_cast<int>(index);InvalidateRect(buttons[index],nullptr,FALSE);return;}std::swap(order()[selected],order()[index]);selected=-1;save();rebuild_keys();resize();return;}const auto found=std::ranges::find(calculator_keys(),order()[index],&CalculatorKey::id);if(found==calculator_keys().end())return;SetFocus(expression);if(found->action=="text")(void)insert_math_expression_text(expression,found->name);else(void)perform_math_expression_key(expression,found->action,found->name);resize();}
    void calculate(){const auto input=inspect_math_expression_editor(expression);cancel();const auto serial=evaluation_serial;error.clear();result_latex.clear();(void)set_math_expression_latex(result,"");SetWindowTextW(error_label,L"");evaluating=true;EnableWindow(evaluate,FALSE);JsonObject request;request.Insert(L"draft",JsonValue::CreateStringValue(wide(input.draft_json)));request.Insert(L"angle",JsonValue::CreateStringValue(degrees?L"DEG":L"RAD"));request.Insert(L"language",JsonValue::CreateStringValue(wide(locale)));const auto encoded=utf8(request.Stringify());const auto mailbox=reply;const auto target=window;calculation=std::jthread([worker=backend,mailbox,target,serial,encoded](std::stop_token stop){std::string value,error;try{value=worker->math_calculate(encoded,stop);}catch(const std::exception& failure){error=failure.what();}catch(const winrt::hresult_error& failure){error=utf8(failure.message());}if(stop.stop_requested())return;std::lock_guard lock(mailbox->mutex);if(mailbox->closed)return;mailbox->serial=serial;mailbox->value=std::move(value);mailbox->error=std::move(error);PostMessageW(target,calculator_result_message,static_cast<WPARAM>(serial),0);});resize();}
    void accept_result(std::uint64_t serial){if(serial!=evaluation_serial)return;std::string raw,failure;{std::lock_guard lock(reply->mutex);if(reply->serial!=serial)return;raw=reply->value;failure=reply->error;}evaluating=false;EnableWindow(evaluate,TRUE);if(!failure.empty()){error=failure;result_latex.clear();}else{const auto response=JsonObject::Parse(wide(raw));if(response.GetNamedBoolean(L"success",false)){const auto draft=utf8(response.GetNamedString(L"draft"));if(!restore_math_expression_draft(result,draft))throw std::runtime_error("Cannot render the calculated expression");result_latex=utf8(response.GetNamedString(L"latex"));error.clear();}else{error=utf8(response.GetNamedString(L"message"));result_latex.clear();}}SetWindowTextW(error_label,wide(error).c_str());save();resize();}
    void resize(){if(!window||!expression)return;RECT area{};GetClientRect(window,&area);const auto width=std::max(1L,area.right),height=std::max(1L,area.bottom);const bool landscape=width>height;MoveWindow(close,0,0,px(48),px(48),TRUE);MoveWindow(angle,width-px(52),0,px(52),px(48),TRUE);MoveWindow(edit_keys,width-px(100),0,px(48),px(48),TRUE);const auto center=width/2;MoveWindow(basic_label,center-px(108),0,px(84),px(48),TRUE);MoveWindow(advanced_switch,center-px(24),0,px(48),px(48),TRUE);MoveWindow(advanced_label,center+px(24),0,px(84),px(48),TRUE);
        int expression_width=width,keypad_width=width,expression_height=0,keypad_height=0;const auto top=px(48);if(landscape){expression_width=static_cast<int>(width/2.4);keypad_width=width-expression_width-px(2);expression_height=height-top;keypad_height=expression_height;MoveWindow(expression_panel,0,top,expression_width,expression_height,TRUE);MoveWindow(keypad,expression_width+px(2),top,keypad_width,keypad_height,TRUE);}else{const auto input_height=math_expression_editor_height(expression),output_height=result_latex.empty()?0:px(48)+math_expression_editor_height(result);expression_height=px(48)+input_height+output_height+(error.empty()?0:px(48));expression_height=std::min(expression_height,std::max(px(100),static_cast<int>(height)-top-px(104)));keypad_height=std::max(1,static_cast<int>(height)-top-expression_height-px(2));MoveWindow(expression_panel,0,top,width,expression_height,TRUE);MoveWindow(keypad,0,top+expression_height+px(2),width,keypad_height,TRUE);}
        MoveWindow(input_label,px(8),0,std::max(px(1),expression_width-px(paste_result?136:64)),px(48),TRUE);MoveWindow(evaluate,expression_width-px(48),0,px(48),px(48),TRUE);MoveWindow(paste,expression_width-px(120),0,px(72),px(48),TRUE);EnableWindow(paste,!result_latex.empty());const auto expression_h=std::min(math_expression_editor_height(expression),std::max(px(44),expression_height-px(48)));MoveWindow(expression,0,px(48),expression_width,expression_h,TRUE);int y=px(48)+expression_h;const bool output=!result_latex.empty();ShowWindow(result_label,output?SW_SHOW:SW_HIDE);ShowWindow(result,output?SW_SHOW:SW_HIDE);if(output){MoveWindow(result_label,px(8),y,expression_width-px(16),px(48),TRUE);y+=px(48);MoveWindow(result,0,y,expression_width,std::max(px(32),math_expression_editor_height(result)),TRUE);y+=math_expression_editor_height(result);}ShowWindow(error_label,error.empty()?SW_HIDE:SW_SHOW);MoveWindow(error_label,px(8),y,expression_width-px(16),std::max(px(40),expression_height-y),TRUE);
        const auto cell_height=px(landscape?36:52),gap=px(1),scroll_controls=editing?px(40):0;keypad_extent=static_cast<int>((buttons.size()+5)/6)*(cell_height+gap)+scroll_controls;scroll=std::clamp(scroll,0,std::max(0,keypad_extent-keypad_height));MoveWindow(scroll_up,keypad_width/2-px(48),0,px(48),px(40),TRUE);MoveWindow(scroll_down,keypad_width/2,0,px(48),px(40),TRUE);const auto cell_width=std::max(1,(keypad_width-gap*5)/6);for(std::size_t index=0;index<buttons.size();++index)if(buttons[index])MoveWindow(buttons[index],static_cast<int>(index%6)*(cell_width+gap),scroll_controls+static_cast<int>(index/6)*(cell_height+gap)-scroll,cell_width,cell_height,TRUE);if(editing){SetWindowPos(scroll_up,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowPos(scroll_down,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=keypad_extent-1;info.nPage=static_cast<UINT>(keypad_height);info.nPos=scroll;SetScrollInfo(keypad,SB_VERT,&info,TRUE);reader_theme::refresh_scrollbars(keypad);
    }
    void command(int id,int notification){if(notification!=BN_CLICKED)return;if(id==back_id){cancel();save();const auto callback=on_back;if(callback)callback();return;}if(id==evaluate_id){calculate();return;}if(id==paste_result_id){if(paste_result&&!result_latex.empty()){const auto callback=paste_result;const auto value=result_latex;callback(value);}return;}if(id==angle_id){degrees=!degrees;SetWindowTextW(angle,degrees?L"DEG":L"RAD");invalidate();save();resize();return;}if(id==advanced_id){if(advanced)advanced_scroll=scroll;else basic_scroll=scroll;advanced=!advanced;scroll=advanced?advanced_scroll:basic_scroll;selected=-1;save();rebuild_keys();resize();return;}if(id==edit_keys_id){editing=!editing;selected=-1;rebuild_keys();resize();return;}if(id==scroll_up_id||id==scroll_down_id){scroll+=(id==scroll_up_id?-1:1)*px(104);resize();return;}if(id>=first_key&&id<first_key+static_cast<int>(buttons.size()))insert(static_cast<std::size_t>(id-first_key));}
    bool draw(const DRAWITEMSTRUCT& item){
        const auto color=reader_theme::palette(window);
        if(item.CtlID==back_id||item.CtlID==edit_keys_id||item.CtlID==angle_id||item.CtlID==evaluate_id||item.CtlID==paste_result_id||item.CtlID==scroll_up_id||item.CtlID==scroll_down_id){
            const auto fill=CreateSolidBrush(color.high_contrast?color.canvas:RGB(16,17,20));FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);const auto disabled=(item.itemState&ODS_DISABLED)!=0;const auto selected_edit=item.CtlID==edit_keys_id&&editing;const auto background=disabled?RGB(72,74,82):selected_edit?RGB(112,117,125):color.accent;const auto surface=CreateSolidBrush(background);const auto old_surface=SelectObject(item.hDC,surface);const auto old_pen=SelectObject(item.hDC,GetStockObject(NULL_PEN));RoundRect(item.hDC,item.rcItem.left,item.rcItem.top+px(6),item.rcItem.right,item.rcItem.bottom-px(6),px(8),px(8));SelectObject(item.hDC,old_pen);SelectObject(item.hDC,old_surface);DeleteObject(surface);const auto ink=disabled?RGB(170,174,183):selected_edit?RGB(255,255,255):reader_theme::accent_foreground(window);
            if(item.CtlID==back_id){auto bounds=item.rcItem;InflateRect(&bounds,-px(12),-px(12));draw_icon(item.hDC,bounds,Icon::close,ink);}
            else if(item.CtlID==edit_keys_id){auto bounds=item.rcItem;bounds.left+=(bounds.right-bounds.left-px(18))/2;bounds.right=bounds.left+px(18);bounds.top=px(5);bounds.bottom=bounds.top+px(18);draw_icon(item.hDC,bounds,Icon::calculator_edit,ink);bounds=item.rcItem;bounds.top=px(24);const auto edit_caption_font=CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");const auto old=SelectObject(item.hDC,edit_caption_font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);const auto label=text(item.hwndItem);DrawTextW(item.hDC,label.c_str(),-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(item.hDC,old);DeleteObject(edit_caption_font);}
            else{const auto face=CreateFontW(-px(item.CtlID==evaluate_id?23:16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");const auto old=SelectObject(item.hDC,face);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);auto bounds=item.rcItem;const auto label=text(item.hwndItem);DrawTextW(item.hDC,label.c_str(),-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(item.hDC,old);DeleteObject(face);}if(item.itemState&ODS_FOCUS)reader_theme::paint_keyboard_focus(item.hwndItem,item.hDC,item.rcItem,ink);return true;
        }
        if(item.CtlID==advanced_id){const auto fill=CreateSolidBrush(color.high_contrast?color.canvas:RGB(16,17,20));FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);auto rect=item.rcItem;const auto blend=[&](COLORREF ink,unsigned alpha){const auto base=color.high_contrast?color.canvas:RGB(16,17,20);return RGB((GetRValue(ink)*alpha+GetRValue(base)*(255-alpha))/255,(GetGValue(ink)*alpha+GetGValue(base)*(255-alpha))/255,(GetBValue(ink)*alpha+GetBValue(base)*(255-alpha))/255);};const auto track=CreateSolidBrush(advanced?blend(color.accent,135):blend(RGB(120,124,134),90));auto old=SelectObject(item.hDC,track);auto pen=SelectObject(item.hDC,GetStockObject(NULL_PEN));RoundRect(item.hDC,rect.left+px(4),rect.top+px(17),rect.right-px(4),rect.bottom-px(17),px(14),px(14));SelectObject(item.hDC,old);DeleteObject(track);const auto knob=CreateSolidBrush(advanced?color.accent:RGB(145,148,158));old=SelectObject(item.hDC,knob);const auto x=advanced?rect.right-px(22):rect.left+px(2);Ellipse(item.hDC,x,rect.top+px(13),x+px(20),rect.top+px(33));SelectObject(item.hDC,old);SelectObject(item.hDC,pen);DeleteObject(knob);return true;}
        if(item.CtlID>=first_key&&item.CtlID<static_cast<UINT>(first_key)+static_cast<UINT>(buttons.size())&&editing&&selected==static_cast<int>(item.CtlID-first_key)){const auto fill=CreateSolidBrush(RGB(112,117,125));FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);SetTextColor(item.hDC,RGB(255,255,255));SetBkMode(item.hDC,TRANSPARENT);const auto old=SelectObject(item.hDC,font);auto bounds=item.rcItem;const auto label=text(item.hwndItem);DrawTextW(item.hDC,label.c_str(),-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item.hDC,old);return true;}return reader_theme::draw_button(item);
    }
};
LRESULT CALLBACK canonical_calculator_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Calculator*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Calculator*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,wp,lp);try{if(message==WM_CTLCOLORSTATIC&&self->calculator_background){const auto colors=reader_theme::palette(window);const auto dc=reinterpret_cast<HDC>(wp);const auto id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));SetBkColor(dc,colors.high_contrast?colors.canvas:RGB(16,17,20));SetTextColor(dc,colors.high_contrast?colors.text:id==error_id?RGB(255,166,166):(id==input_label_id||id==result_label_id)?RGB(174,183,198):RGB(235,239,245));return reinterpret_cast<LRESULT>(self->calculator_background);}LRESULT color=0;if(reader_theme::control_color(message,wp,lp,color))return color;switch(message){case WM_CREATE:self->initialize(window);return 0;case WM_SIZE:self->resize();return 0;case WM_COMMAND:self->command(LOWORD(wp),HIWORD(wp));return 0;case calculator_result_message:self->accept_result(static_cast<std::uint64_t>(wp));return 0;case WM_MOUSEWHEEL:self->scroll-=MulDiv(GET_WHEEL_DELTA_WPARAM(wp),self->px(90),WHEEL_DELTA);self->resize();return 0;case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(self->keypad,SB_VERT,&info);switch(LOWORD(wp)){case SB_LINEUP:self->scroll-=self->px(36);break;case SB_LINEDOWN:self->scroll+=self->px(36);break;case SB_PAGEUP:self->scroll-=static_cast<int>(info.nPage);break;case SB_PAGEDOWN:self->scroll+=static_cast<int>(info.nPage);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:self->scroll=info.nTrackPos;break;case SB_TOP:self->scroll=0;break;case SB_BOTTOM:self->scroll=self->keypad_extent;break;}self->resize();return 0;}case WM_DRAWITEM:if(self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;break;case WM_KEYDOWN:if(wp==VK_ESCAPE){self->command(back_id,BN_CLICKED);return 0;}break;case WM_ERASEBKGND:{RECT area{};GetClientRect(window,&area);const auto fill=CreateSolidBrush(reader_theme::palette(window).high_contrast?reader_theme::palette(window).canvas:RGB(16,17,20));FillRect(reinterpret_cast<HDC>(wp),&area,fill);DeleteObject(fill);return 1;}case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;return DefWindowProcW(window,message,wp,lp);}}catch(const std::exception& error){if(message==WM_CREATE){OutputDebugStringW(wide(error.what()).c_str());return -1;}self->error=error.what();if(self->error_label)SetWindowTextW(self->error_label,wide(self->error).c_str());}catch(const winrt::hresult_error& error){if(message==WM_CREATE){OutputDebugStringW(error.message().c_str());return -1;}self->error=utf8(error.message());if(self->error_label)SetWindowTextW(self->error_label,error.message().c_str());}return DefWindowProcW(window,message,wp,lp);}
}
HWND create_calculator_view(HWND parent,const WindowsEnvironment& environment,std::function<void()> on_back,std::shared_ptr<windows::NarrationWorker> backend,std::function<void(std::string_view)> paste_result,CalculatorPreferences preferences){static const bool registered=[](){WNDCLASSW value{};value.lpfnWndProc=canonical_calculator_proc;value.hInstance=GetModuleHandleW(nullptr);value.hCursor=LoadCursorW(nullptr,IDC_ARROW);value.lpszClassName=calculator_class;return RegisterClassW(&value)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}();if(!registered)throw std::runtime_error("Cannot register native calculator");auto state=std::make_unique<Calculator>(environment,std::move(backend),std::move(on_back),std::move(paste_result),std::move(preferences));const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,calculator_class,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),state.get());if(!window)throw std::runtime_error("Cannot initialize canonical scientific calculator");state->owned=true;state.release();return window;}
CalculatorSnapshot inspect_calculator(HWND window){wchar_t name[128]{};GetClassNameW(window,name,128);if(std::wstring_view(name)!=calculator_class)throw std::invalid_argument("Not a canonical calculator");auto* self=reinterpret_cast<Calculator*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(!self)throw std::invalid_argument("Calculator already closed");const auto input=inspect_math_expression_editor(self->expression);return {input.latex,input.draft_json,self->result_latex,self->error,self->degrees,self->advanced,self->editing,self->evaluating,self->order()};}
namespace {
constexpr wchar_t calculator_frame_class[]=L"LearnGameAndEngineDev.CalculatorWindow";
constexpr wchar_t calculator_frame_property[]=L"LGAED.CalculatorWindow";
struct CalculatorFrameLife {HWND frame=nullptr,owner=nullptr;bool alive=true;std::function<void(std::string_view)> paste;};
struct CalculatorFrame {
    HWND window=nullptr,view=nullptr,owner=nullptr;bool owned=false,disabled_owner=false;WindowsEnvironment environment;std::shared_ptr<windows::NarrationWorker> worker;CalculatorPreferences preferences;std::shared_ptr<CalculatorFrameLife> life=std::make_shared<CalculatorFrameLife>();
    CalculatorFrame(HWND parent,const WindowsEnvironment& env,std::shared_ptr<windows::NarrationWorker> backend,std::function<void(std::string_view)> result,CalculatorPreferences access):owner(parent),environment(env),worker(std::move(backend)),preferences(std::move(access)){life->owner=owner;life->paste=std::move(result);}
    ~CalculatorFrame(){life->alive=false;if(IsWindow(owner)){if(reinterpret_cast<HWND>(GetPropW(owner,calculator_frame_property))==window)RemovePropW(owner,calculator_frame_property);if(disabled_owner)EnableWindow(owner,TRUE);}}
    void initialize(HWND frame){window=frame;life->frame=frame;const auto weak=std::weak_ptr<CalculatorFrameLife>(life);const auto close=[weak]{if(const auto state=weak.lock();state&&state->alive&&IsWindow(state->frame))PostMessageW(state->frame,WM_CLOSE,0,0);};
        view=create_calculator_view(frame,environment,close,worker,[weak](std::string_view value){const auto state=weak.lock();if(!state||!state->alive||!IsWindow(state->frame))return;const auto result=std::string(value);const auto callback=state->paste;const auto parent=state->owner;const auto frame=state->frame;if(IsWindow(parent)&&callback)callback(result);if(IsWindow(frame))DestroyWindow(frame);if(IsWindow(parent))SetForegroundWindow(parent);},preferences);resize();
    }
    void resize(){if(view&&IsWindow(view)){RECT area{};GetClientRect(window,&area);MoveWindow(view,0,0,std::max<LONG>(1,area.right),std::max<LONG>(1,area.bottom),TRUE);}}
};
LRESULT CALLBACK calculator_frame_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<CalculatorFrame*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<CalculatorFrame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,wp,lp);try{switch(message){case WM_CREATE:self->initialize(window);return 0;case WM_SIZE:self->resize();return 0;case WM_GETMINMAXINFO:{auto* bounds=reinterpret_cast<MINMAXINFO*>(lp);bounds->ptMinTrackSize={MulDiv(320,static_cast<int>(GetDpiForWindow(window)),96),MulDiv(240,static_cast<int>(GetDpiForWindow(window)),96)};return 0;}case WM_CLOSE:{const auto owner=self->owner;DestroyWindow(window);if(IsWindow(owner))SetForegroundWindow(owner);return 0;}case WM_COMMAND:if(LOWORD(wp)==IDCANCEL&&HIWORD(wp)==0&&lp==0){PostMessageW(window,WM_CLOSE,0,0);return 0;}break;case WM_KEYDOWN:if(wp==VK_ESCAPE){PostMessageW(window,WM_CLOSE,0,0);return 0;}break;case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;return DefWindowProcW(window,message,wp,lp);}}catch(const std::exception& error){OutputDebugStringW(wide(error.what()).c_str());if(message==WM_CREATE)return -1;}catch(const winrt::hresult_error& error){OutputDebugStringW(error.message().c_str());if(message==WM_CREATE)return -1;}return DefWindowProcW(window,message,wp,lp);}
}
HWND open_calculator_window(HWND owner,const WindowsEnvironment& environment,std::shared_ptr<windows::NarrationWorker> worker,std::function<void(std::string_view)> onpaste,CalculatorPreferences preferences){
    owner=GetAncestor(owner,GA_ROOT);if(!IsWindow(owner))throw std::invalid_argument("Calculator needs a live owner");if(const auto existing=reinterpret_cast<HWND>(GetPropW(owner,calculator_frame_property));IsWindow(existing)){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);return existing;}
    static const bool registered=[](){WNDCLASSW value{};value.lpfnWndProc=calculator_frame_proc;value.hInstance=GetModuleHandleW(nullptr);value.hCursor=LoadCursorW(nullptr,IDC_ARROW);value.lpszClassName=calculator_frame_class;return RegisterClassW(&value)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}();if(!registered)throw std::runtime_error("Cannot register calculator window");
    auto data=std::make_unique<CalculatorFrame>(owner,environment,std::move(worker),std::move(onpaste),std::move(preferences));const auto profile=data->preferences.read?data->preferences.read():state::StateStore(environment.paths.root).load();const auto locale=interface_language(profile);const auto title=locale=="es"?L"Calculadora científica":locale=="fr"?L"Calculatrice scientifique":L"Scientific calculator";RECT owner_area{};GetWindowRect(owner,&owner_area);const auto width=MulDiv(900,static_cast<int>(GetDpiForWindow(owner)),96),height=MulDiv(600,static_cast<int>(GetDpiForWindow(owner)),96);const auto x=owner_area.left+std::max<LONG>(0,(owner_area.right-owner_area.left-width)/2),y=owner_area.top+std::max<LONG>(0,(owner_area.bottom-owner_area.top-height)/2);
    const auto frame=CreateWindowExW(WS_EX_CONTROLPARENT,calculator_frame_class,title,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,GetModuleHandleW(nullptr),data.get());if(!frame)throw std::runtime_error("Cannot create calculator window");data->owned=true;data->disabled_owner=IsWindowEnabled(owner)!=FALSE;if(data->disabled_owner)EnableWindow(owner,FALSE);SetPropW(owner,calculator_frame_property,reinterpret_cast<HANDLE>(frame));data.release();ShowWindow(frame,SW_SHOW);SetForegroundWindow(frame);return frame;
}
HWND create_graphics_view(HWND parent,std::function<void()> on_back) { return create(parent,{},std::move(on_back),true); }
HWND create_graphics_view(HWND parent,const WindowsEnvironment& environment,std::function<void()> on_back) { return create(parent,environment,std::move(on_back),true); }
GraphicsFrame inspect_graphics_frame(HWND window) {
    wchar_t name[128]{}; GetClassNameW(window,name,128);
    if (std::wstring_view(name) != tool_class) throw std::invalid_argument("La ventana no pertenece a una herramienta gráfica.");
    auto* tool = reinterpret_cast<Tool*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (!tool) throw std::invalid_argument("La herramienta gráfica ya está cerrada.");
    return tool->inspect();
}
} // namespace lgaed::presentation

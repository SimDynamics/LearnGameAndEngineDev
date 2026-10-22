#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/icons.hpp"
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
HWND create_calculator_view(HWND parent,const WindowsEnvironment& environment,std::function<void()> on_back) { return create(parent,environment,std::move(on_back),false); }
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

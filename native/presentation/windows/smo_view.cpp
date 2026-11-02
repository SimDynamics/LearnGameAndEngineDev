#include "presentation/windows/smo_view.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/smo_benchmark.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "presentation/windows/smo_rule_gpu.hpp"
#include "presentation/windows/smo_duodecimal_gpu.hpp"
#include "presentation/windows/smo_program_session.hpp"
#include "presentation/windows/smo_lazy_task.hpp"
#include "presentation/windows/smo_subdivision_task.hpp"
#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/ui_language.hpp"
#include "platform/windows/state_store.hpp"
#include "core/smo.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <iomanip>
#include <cmath>

namespace lgaed::presentation {
namespace {
constexpr wchar_t lab_class[]=L"LearnGameAndEngineDev.SmoLab";
constexpr int back_id=7100,run_id=7101,pause_id=7102,triangle_id=7103,cube_id=7104,
    rules_id=7105,benchmark_id=7106,particles_id=7107,sustained_id=7108,source_id=7110,status_id=7111,canvas_id=7112,title_id=7113,preview_id=7114,api_first_id=7120,
    gpu_rules_id=7140,d12_id=7141,tasks_id=7142,rules_api_id=7150,d12_api_id=7160,
    lazy_first_id=7200,subdivision_first_id=7240,lazy_change_first_id=7300,lazy_slow_change_first_id=7400;
constexpr UINT_PTR frame_timer=1,save_timer=2;
constexpr UINT benchmark_complete=WM_APP+71;
constexpr UINT scene_progress=WM_APP+72;
constexpr UINT program_progress=WM_APP+73;
constexpr UINT task_progress=WM_APP+74;
constexpr std::uint32_t blank_bgra=0xff0e1b30u;
using Clock=std::chrono::steady_clock;
LRESULT CALLBACK rules_text_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY)RemoveWindowSubclass(item,rules_text_proc,1);
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){DestroyWindow(GetParent(item));return 0;}
    return DefSubclassProc(item,message,wp,lp);
}

std::string sample_source(std::string_view sample){
    const auto path=std::filesystem::path(build::source_root)/"native"/(sample=="cube"?"smo_cube.smo":"smo_triangle.smo");
    const auto bytes=state::read_bytes(path,64*1024);return std::string(bytes.begin(),bytes.end());
}
std::string rules_text(){return
    "SMO · Sistema Matemático Objetivo · prototipo experimental\n\n"
    "El editor usa por defecto el compilador nativo y aritmética ordinaria de la GPU. Los números se escriben en base 12. Las reglas especiales descritas a continuación pertenecen a los experimentos independientes y todavía no se activan en ese perfil nativo.\n\n"
    "Numeración posicional de base 12: 0 sigue siendo cero; # = diez; ~ = once; 10 = doce. @ es una abreviatura independiente de doce, no una cifra adicional: 1@ y @1 se rechazan. 0.6 en SMO equivale a 0.5 decimal. Los tres símbolos son una elección provisional centralizada.\n\n"
    "Multiplicar por cero ejecuta cero: 2*0=0 y alternativa(2*0)=2. Se conserva la cantidad como segunda interpretación, sin crear contribuciones en componentes gráficas nulas. Dividir una cantidad por cero conserva el numerador por defecto (ninguna partición); alternativa(2/0)=0. Son decisiones deterministas y distintas para producto y subdivisión.\n\n"
    "pdir(2, 3) = 8; pdir(2, 2) = 4; pdir(2, 1) = 2. Los exponentes negativos dan -8, -4 y -2, respectivamente, nunca sus recíprocos. pinv(2, 3) = 0.6; pinv(2, 2) = 1; pinv(2, 1) = 2 (provisional). Para exponentes negativos, el resultado invertido cambia de signo.\n\n"
    "Ambas potencias con exponente cero conservan DOS interpretaciones: se ejecuta 0 (ausencia de bloques de ese orden) y alternativa(pdir(2, 0)) o alternativa(pinv(2, 0)) devuelve 2. Esta regla explícita no se deriva del producto repetido convencional ni produce 1; por ello la ley de suma de exponentes falla cuando uno es cero.\n\n"
    "La extensión a otras bases positivas es provisional: pdir(b,n) replica la magnitud b para n positivo y la niega para n negativo; pinv(b,n) usa b para |n|=1 y subdivide como 1/(b elevado a |n|-2) para |n|>=2. El caso cero es especial. Bases no positivas y exponentes no enteros aún no están definidos en este prototipo y producen un diagnóstico.\n\n"
    "Signos experimentales: si algún factor o divisor no nulo es negativo, el producto o cociente es negativo; también negativo por negativo. Suma y resta se interpretan como balance firmado de bloques. La regla de signos puede revisarse, no es una ley definitiva del usuario.\n\n"
    "Límites comprobados de la regla provisional de signos: no hay distributividad general: (-2)*(1-1)=0, pero (-2)*1+(-2)*(-1)=-4. Tampoco cancelación general: (-2)*1=(-2)*(-1), aunque 1 y -1 son distintos. En las potencias, pdir(2,1+0)=2 pero pdir(2,1)*pdir(2,0)=0. Con exponentes negativos: pdir(2,1)*pdir(2,-1)=-4 y pdir(2,0)=0. La recurrencia invertida tampoco cruza cero: pinv(2,0)/2=0, mientras pinv(2,1)=2. Estas pruebas no demuestran coherencia de todo SMO.\n\n"
    "Identidad de composición experimental: en el perfil con cero como identidad del producto y potencia cero primaria 0, sea P_b(0)=0 y P_b(n)=b^n para b>0 y n entero positivo. Para n,m>=0 se cumple P_b(n+m)=P_b(n) ⊙ P_b(m): si algún índice es cero, actúa la identidad 0; si ambos son positivos, se aplica el producto positivo habitual. Es una demostración por casos del valor primario ideal. Las pruebas acotadas usan b=2 y n,m de 0 a 8 con resultados exactos. Para ejecutarla, los índices y sus sumas deben caber en el intervalo admitido [-64,64] y los valores deben ser representables sin redondeo. No incluye alternativas observables, redondeos, exponentes negativos ni la variante que cambia el exponente uno. Para Q_b(n)=b^(2-n), n,m>=2, la identidad interior es Q_b(n) ⊙ Q_b(m)=Q_b(n+m-2); no se extiende automáticamente por cero o ±1.\n\n"
    "Programa mínimo: vertice(x,y,z), cara(a,b,c), estado nombre=valor, al_actualizar { ... } y al_dibujar { rotacion(x,y,z); color(r,g,b); dibujar(); }. Las caras usan índices desde cero. // inicia comentarios; # y ~ siguen siendo cifras. Se admiten + - * /, pdir, pinv y alternativa. No hay bucles, archivos ni red.\n\n"
    "El host entrega dt como segundos reales transcurridos y ejecuta el bloque de actualización; el programa decide el nuevo estado y solicita el dibujo. Pausar omite por completo ese bloque, sin suponer que todos los programas se detienen al recibir dt=0. En los ejemplos, velocidad=0 sí mantiene el giro porque el producto predeterminado por cero es cero.\n\n"
    "Frontera gráfica del ensayo: los vértices, caras, colores y ángulos proceden del programa SMO. Rotación, seno/coseno aproximados, perspectiva, intersecciones y profundidad se calculan en la GPU con el formato base 12. Son extensiones geométricas explícitas, no una demostración de derivar la geometría de las reglas experimentales. Los ángulos son radianes y el envolvente gráfico permanece acotado; excederlo produce un diagnóstico. La ventana sólo presenta los píxeles ya calculados.\n\n"
    "El programa completo de este editor aislado se valida y traduce a instrucciones sin evaluar sus expresiones en FP64. La GPU ejecuta inicialización, estados, actualización, dibujo, signos, operaciones y alternativas mediante enteros en el formato duodecimal adaptable de doce cifras significativas. El reloj entrega nanosegundos y su conversión a dt también se hace en la GPU. Pausar conserva los estados y omite la actualización. Hay redondeo declarado y diagnósticos de rango; no se afirma exactitud real ilimitada ni mayor velocidad por usar base 12. El intérprete CPU anterior se conserva sólo como referencia para pruebas. Esta integración no cambia el compilador C++, la calculadora ni otras partes de la aplicación. Ejecutar aplica el código actual; editar cancela el programa anterior y evita mostrar resultados obsoletos.\n";
}
struct DetailsContent{const std::string* body;std::string back;};
LRESULT CALLBACK rules_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_CREATE){const auto text=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL,
        0,0,1,1,window,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);
        if(!text)return -1;reader_theme::apply(text,reader_theme::Surface::canvas);
        const auto* content=static_cast<const DetailsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);set_rich_text(text,content&&content->body?*content->body:rules_text());SetWindowSubclass(text,rules_text_proc,1,0);
        const auto back=CreateWindowExW(0,L"BUTTON",wide(content?content->back:"Volver").c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            0,0,1,1,window,reinterpret_cast<HMENU>(IDCANCEL),GetModuleHandleW(nullptr),nullptr);
        if(!back)return -1;reader_theme::apply(back,reader_theme::Surface::canvas);SendMessageW(back,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return 0;}
    if(message==WM_SIZE||message==WM_DPICHANGED_AFTERPARENT){RECT area{};GetClientRect(window,&area);const auto px=[&](int value){return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);};
        MoveWindow(GetDlgItem(window,IDCANCEL),px(12),px(8),std::max(1L,std::min<LONG>(px(112),area.right-px(24))),px(40),TRUE);
        MoveWindow(GetDlgItem(window,1),px(12),px(56),std::max(1L,area.right-px(24)),std::max(1L,area.bottom-px(68)),TRUE);return 0;}
    if(message==WM_COMMAND&&LOWORD(wp)==IDCANCEL){DestroyWindow(window);return 0;}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){DestroyWindow(window);return 0;}
    LRESULT result{};if(reader_theme::control_color(message,wp,lp,result))return result;
    return DefWindowProcW(window,message,wp,lp);
}
struct Lab {
    WindowsEnvironment environment;std::function<void()> on_back;GraphicsBackend backend;SmoProgramBackend program_backend;
    HWND window=nullptr,source=nullptr,canvas=nullptr,status=nullptr,title=nullptr,rules=nullptr,results=nullptr,preview=nullptr;
    std::array<HWND,12> buttons{};HFONT font=nullptr,code_font=nullptr;HMODULE rich_edit_module=nullptr;std::unique_ptr<windows::CodeEditor> editor;
    std::unique_ptr<SmoProgramSession> program_session;bool program=false;
    std::optional<SmoProgramUpdate> program_image;std::uint32_t latest_vertices=0,latest_faces=0;std::string sample="triangle",locale="es",error,status_text;
    std::jthread benchmark;std::mutex benchmark_mutex;std::optional<SmoBenchmarkReport> benchmark_report;std::optional<SmoParticleReport> particle_report;std::optional<SmoSustainedReport> scene_report;std::optional<SmoSceneProgress> pending_progress;SmoSceneProgress displayed_progress;std::string benchmark_error;
    std::optional<SmoRuleGpuReport> rule_report;std::optional<SmoD12SceneReport> d12_report,d12_control_report;std::optional<SmoD12Validation> d12_validation;
    std::optional<SmoLazyTaskReport> lazy_report;std::optional<SmoSubdivisionReport> subdivision_report;
    std::string pending_task_status;
    std::function<void()> deferred_benchmark;
    enum class PreviewMode{none,program,benchmark};PreviewMode preview_mode=PreviewMode::none;
    bool benchmarking=false;
    bool owned=false,running=false,dirty=false,loading=false;double interpreter_ms=0;
    std::int64_t saved_selection_start=0,saved_selection_end=0;
    Lab(const WindowsEnvironment& env,std::function<void()> back,GraphicsBackend api,SmoProgramBackend execution_backend):environment(env),on_back(std::move(back)),backend(api),program_backend(execution_backend){
        state::StateStore store(environment.paths.root);const auto data=store.load();locale=interface_language(data);
        sample=state::string_value(data,"native_smo_laboratory","selected","triangle");if(sample!="cube")sample="triangle";
    }
    ~Lab(){stop_benchmark();running=false;program_session.reset();editor.reset();if(rules&&IsWindow(rules))DestroyWindow(rules);if(results&&IsWindow(results))DestroyWindow(results);if(font)DeleteObject(font);if(code_font)DeleteObject(code_font);if(rich_edit_module)FreeLibrary(rich_edit_module);}
    std::string tr(std::string_view es,std::string_view en,std::string_view fr)const{return std::string(locale=="fr"?fr:locale=="en"?en:es);}
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(window)),96);}
    HWND control(const wchar_t* type,std::string_view text,int id,DWORD style=0){const auto item=CreateWindowExW(0,type,wide(text).c_str(),WS_CHILD|WS_VISIBLE|style,
        0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!item)throw std::runtime_error("Cannot create SMO laboratory control");reader_theme::apply(item,reader_theme::Surface::canvas);return item;}
    void label(std::string_view text,bool failed=false){status_text=text;SetWindowTextW(status,wide(text).c_str());const auto colors=reader_theme::palette(status);reader_theme::set_text_color(status,failed&&!colors.high_contrast?RGB(242,135,135):colors.text);}
    void stop_benchmark(){deferred_benchmark={};if(benchmark.joinable()){benchmark.request_stop();benchmark.join();}}
    void hide_preview(){preview_mode=PreviewMode::none;if(preview)ShowWindow(preview,SW_HIDE);if(canvas)ShowWindow(canvas,SW_SHOW);displayed_progress={};}
    void pause_program(){running=false;if(program_session)program_session->running(false,false);}
    void clear_surface(){latest_vertices=0;latest_faces=0;if(canvas)InvalidateRect(canvas,nullptr,FALSE);}
    void clear_program(){running=false;program=false;if(program_session)program_session->clear();program_image.reset();clear_surface();}
    void update_program(bool paused=false){
        if(!program_session||benchmarking)return;auto update=program_session->take();if(!update)return;
        if(!update->error.empty()) {running=false;program=update->ready;program_session->running(false,false);
            if(!program){clear_surface();program_image.reset();hide_preview();}
            SetWindowTextW(buttons[2],wide(tr("Reanudar","Resume","Reprendre")).c_str());error=std::move(update->error);label(error,true);return;}
        if(!update->ready||(!paused&&!running&&program_image))return;
        error.clear();program=true;interpreter_ms=update->elapsed_ms;latest_vertices=update->vertices;latest_faces=update->faces;
        displayed_progress.width=update->image.width;displayed_progress.height=update->image.height;displayed_progress.frames=update->frames;
        displayed_progress.preview_bgra=update->image.pixels;program_image=std::move(*update);
        preview_mode=PreviewMode::program;ShowWindow(canvas,SW_HIDE);ShowWindow(preview,SW_SHOW);InvalidateRect(preview,nullptr,FALSE);
        if(running)update_status();
    }
    void draw_preview(const DRAWITEMSTRUCT& item){FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::compiler_editor));if(displayed_progress.preview_bgra.empty())return;
        BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=static_cast<LONG>(displayed_progress.width);bitmap.bmiHeader.biHeight=-static_cast<LONG>(displayed_progress.height);bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
        const auto width=item.rcItem.right-item.rcItem.left,height=item.rcItem.bottom-item.rcItem.top;const auto scale=std::min(static_cast<double>(width)/displayed_progress.width,static_cast<double>(height)/displayed_progress.height);const auto w=std::max(1,static_cast<int>(displayed_progress.width*scale)),h=std::max(1,static_cast<int>(displayed_progress.height*scale));SetStretchBltMode(item.hDC,HALFTONE);
        StretchDIBits(item.hDC,item.rcItem.left+(width-w)/2,item.rcItem.top+(height-h)/2,w,h,0,0,displayed_progress.width,displayed_progress.height,displayed_progress.preview_bgra.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
    }
    void update_scene_progress(){std::optional<SmoSceneProgress> update;{std::lock_guard lock(benchmark_mutex);update=std::move(pending_progress);pending_progress.reset();}if(!benchmarking||!update)return;displayed_progress=std::move(*update);
        preview_mode=PreviewMode::benchmark;ShowWindow(canvas,SW_HIDE);ShowWindow(preview,SW_SHOW);InvalidateRect(preview,nullptr,FALSE);
        label(displayed_progress.api+" · "+displayed_progress.phase+" · "+std::to_string(displayed_progress.elapsed)+tr(" s · fotogramas verificados: "," s · verified frames: "," s · images vérifiées : ")+std::to_string(displayed_progress.frames));
    }
    std::string scene_text(const SmoSustainedReport& report)const{std::ostringstream out;out<<std::fixed<<std::setprecision(4);
        out<<tr("Prueba sostenida · ray tracing, gravedad y colisiones\n\n","Sustained test · ray tracing, gravity and collisions\n\n","Test soutenu · ray tracing, gravité et collisions\n\n")
            <<report.device.api<<" · "<<report.device.adapter<<"\n"<<report.device.driver<<"\n"
            <<tr("Duración medida (s): ","Measured duration (s): ","Durée mesurée (s) : ")<<report.elapsed_seconds<<tr(" · fotogramas: "," · frames: "," · images : ")<<report.frames<<"\n"
            <<report.workload.width<<" × "<<report.workload.height<<tr(" · esferas: "," · spheres: "," · sphères : ")<<report.workload.bodies<<tr(" · grupo: "," · group: "," · groupe : ")<<report.workload.group_size<<"\n"
            <<tr("Contactos: ","Contacts: ","Contacts : ")<<report.contacts<<tr(" · cambios de imagen: "," · image changes: "," · changements d’image : ")<<report.dynamic_frames<<"\n"
            <<tr("Diferencias de estado / imagen: ","State / image mismatches: ","Différences d’état / image : ")<<report.state_mismatches<<" / "<<report.pixel_mismatches<<tr(" · error máximo: "," · maximum error: "," · erreur maximale : ")<<report.max_error<<"\n\n";
        const std::array<std::string,3> names{tr("A · convencional, todos los candidatos","A · ordinary, all candidates","A · ordinaire, tous les candidats"),tr("B · convencional agrupado","B · grouped ordinary","B · ordinaire groupé"),tr("C · SMO agrupado","C · grouped SMO","C · SMO groupé")};
        for(unsigned i=0;i<3;++i){const auto& metric=report.metrics[i];out<<names[i]<<"\n";
            if(metric.valid)out<<tr("GPU ms mediana / P95 / P99: ","GPU ms median / P95 / P99: ","GPU ms médiane / P95 / P99 : ")<<metric.median_ms<<" / "<<metric.p95_ms<<" / "<<metric.p99_ms<<"\n";else out<<tr("Reloj GPU inválido; no hay comparación temporal.\n","Invalid GPU clock; no timing comparison.\n","Horloge GPU non valide ; pas de comparaison temporelle.\n");
            out<<tr("Intersecciones rayo/esfera realmente probadas: ","Actual ray/sphere tests: ","Tests rayon/sphère effectués : ")<<report.ray_sphere_tests[i]<<"\n\n";}
        if(report.device.software)out<<tr("WARP: prueba breve de corrección, no rendimiento físico ni un minuto de estrés.\n","WARP: short correctness test, not physical performance or a minute of stress.\n","WARP : test bref de correction, pas de performance physique ni une minute de charge.\n");
        out<<(report.equivalent?tr("A/B/C coinciden dentro de la tolerancia declarada.\n","A/B/C match within the stated tolerance.\n","A/B/C concordent dans la tolérance indiquée.\n"):tr("HAY DISCREPANCIAS: no se acredita aceleración.\n","MISMATCHES: no speedup is credited.\n","DIFFÉRENCES : aucune accélération établie.\n"));
        if(!report.target_reached)out<<tr("La referencia no alcanzó 33,33 ms GPU por fotograma.\n","The reference did not reach 33.33 GPU ms per frame.\n","La référence n’a pas atteint 33,33 ms GPU par image.\n");
        out<<"\n"<<report.conclusion<<"\n\n"<<tr("Los tiempos de GPU no son FPS de la aplicación. B permite comprobar si el ahorro pertenece al algoritmo, también disponible fuera de SMO.","GPU timings are not application FPS. B checks whether the saving belongs to the algorithm and is available outside SMO too.","Les temps GPU ne sont pas les FPS de l’application. B vérifie si le gain vient d’un algorithme également disponible hors SMO.")<<"\n"<<report.identity;return out.str();
    }
    void restore_execution_controls(){
        for(int i=1;i<=4;++i)EnableWindow(buttons[static_cast<std::size_t>(i)],TRUE);
        const std::array<std::string,5> captions{tr("Prueba GPU","GPU test","Test GPU"),tr("Partículas","Particles","Particules"),tr("1 min · APIs","1 min · APIs","1 min · API"),tr("Reglas GPU","GPU rules","Règles GPU"),tr("Motor base 12","Base-12 engine","Moteur base 12")};
        for(std::size_t i=0;i<captions.size();++i)SetWindowTextW(buttons[i+6],wide(captions[i]).c_str());
        SetWindowTextW(buttons[11],wide(tr("Aplicaciones GPU","GPU applications","Applications GPU")).c_str());
    }
    void choose_tasks(){if(benchmarking){cancel_benchmark();return;}
        const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create SMO task menu");
        const auto lazy=CreatePopupMenu(),changing=CreatePopupMenu(),slow_changing=CreatePopupMenu(),subdivision=CreatePopupMenu();
        const std::array<const wchar_t*,4> names{L"DirectX 11",L"DirectX 12",L"Vulkan",L"OpenGL"};
        for(unsigned cadence=0;cadence<3;++cadence)for(unsigned api=0;api<4;++api){const auto arrangements=CreatePopupMenu();
            for(unsigned layout=0;layout<2;++layout){const auto ratios=CreatePopupMenu();
                for(unsigned ratio=0;ratio<5;++ratio){const auto caption=std::to_wstring(ratio*25)+L" % "+wide(tr("activos","active","actifs"));
                    AppendMenuW(ratios,MF_STRING,(cadence==0?lazy_first_id:cadence==1?lazy_change_first_id:lazy_slow_change_first_id)+api*10+layout*5+ratio,caption.c_str());}
                AppendMenuW(arrangements,MF_POPUP,reinterpret_cast<UINT_PTR>(ratios),wide(layout==0?tr("Agrupados","Grouped","Groupés"):tr("Mezclados","Mixed","Mélangés")).c_str());}
            AppendMenuW(cadence==0?lazy:cadence==1?changing:slow_changing,MF_POPUP,reinterpret_cast<UINT_PTR>(arrangements),names[api]);
            if(cadence==0)AppendMenuW(subdivision,MF_STRING,subdivision_first_id+api,names[api]);}
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(lazy),wide(tr("Canales estables · cinco variantes","Stable channels · five variants","Canaux stables · cinq variantes")).c_str());
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(changing),wide(tr("Cambiar canales en cada fotograma","Change channels every frame","Changer les canaux à chaque image")).c_str());
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(slow_changing),wide(tr("Cambiar canales cada cuatro fotogramas","Change channels every four frames","Changer les canaux toutes les quatre images")).c_str());
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(subdivision),wide(tr("Subdivisiones exactas F12 · cuatro variantes","Exact F12 subdivisions · four variants","Subdivisions exactes F12 · quatre variantes")).c_str());
        RECT area{};GetWindowRect(buttons[11],&area);UINT choice=0;{auto style=prepare_native_menu(menu,window);choice=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,area.left,area.bottom,window,nullptr);DestroyMenu(menu);}
        if(choice>=lazy_first_id&&choice<lazy_first_id+40){const auto key=choice-lazy_first_id;start_task(static_cast<SmoComputeApi>(key/10),false,(key%5)*25,(key%10)<5);}
        else if(choice>=subdivision_first_id&&choice<subdivision_first_id+4)start_task(static_cast<SmoComputeApi>(choice-subdivision_first_id),true,0,true);
        else if((choice>=lazy_change_first_id&&choice<lazy_change_first_id+40)||(choice>=lazy_slow_change_first_id&&choice<lazy_slow_change_first_id+40)){const bool slow=choice>=lazy_slow_change_first_id;const auto key=choice-(slow?lazy_slow_change_first_id:lazy_change_first_id);start_task(static_cast<SmoComputeApi>(key/10),false,(key%5)*25,(key%10)<5,slow?4u:1u);}
    }
    void start_task(SmoComputeApi api,bool subdivision,unsigned active,bool grouped,unsigned change_every=0){
        if(benchmarking){cancel_benchmark();return;}if(!save()||!prepare_benchmark([this,api,subdivision,active,grouped,change_every]{start_task(api,subdivision,active,grouped,change_every);}))return;
        benchmarking=true;error.clear();hide_preview();for(unsigned i=1;i<=4;++i)EnableWindow(buttons[i],FALSE);
        for(unsigned i=6;i<buttons.size();++i)SetWindowTextW(buttons[i],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
        {std::lock_guard lock(benchmark_mutex);benchmark_report.reset();particle_report.reset();scene_report.reset();rule_report.reset();d12_report.reset();d12_control_report.reset();d12_validation.reset();lazy_report.reset();subdivision_report.reset();pending_progress.reset();benchmark_error.clear();}
        label(tr("Preparando variantes sobre el mismo estado inicial…","Preparing variants from the same initial state…","Préparation des variantes depuis le même état initial…"));
        benchmark=std::jthread([this,target=window,api,subdivision,active,grouped,change_every](std::stop_token stop){try{
            SmoComputeOptions device;device.software=backend==GraphicsBackend::warp&&api==SmoComputeApi::direct3d11;
            const auto progress=[this,target](std::string_view value){{std::lock_guard lock(benchmark_mutex);pending_task_status=std::string(value);}PostMessageW(target,task_progress,0,0);};
            if(subdivision){SmoSubdivisionOptions options;options.api=api;options.device=device;options.items=device.software?65:4096;options.steps=device.software?2:4;options.rounds=device.software?3:21;options.warmups=device.software?1:3;
                auto report=run_smo_subdivision_task(options,stop,progress);std::lock_guard lock(benchmark_mutex);subdivision_report=std::move(report);}
            else{SmoLazyTaskOptions options;options.api=api;options.device=device;options.items=device.software?129:16384;options.active_percent=active;options.grouped=grouped;options.change_every=change_every;options.frames=device.software?5:8;options.rounds=device.software?3:21;options.warmups=device.software?1:3;
                auto report=run_smo_lazy_task(options,stop,[&](unsigned percent){progress("Omisión de canales · "+std::to_string(percent)+" %");});std::lock_guard lock(benchmark_mutex);lazy_report=std::move(report);}
        }catch(const std::exception& e){std::lock_guard lock(benchmark_mutex);benchmark_error=e.what();}PostMessageW(target,benchmark_complete,0,0);});
    }
    std::string task_text()const{std::ostringstream out;out<<std::fixed<<std::setprecision(5);
        out<<"Aplicaciones SMO · comparación sobre el mismo estado inicial\n\n";
        const auto spread=[&](const std::vector<double>& samples){if(samples.empty())return;auto ordered=samples;std::sort(ordered.begin(),ordered.end());
            out<<"Muestras GPU: "<<samples.size()<<" · mínimo / P95 / máximo ms: "<<ordered.front()<<" / "<<ordered[static_cast<std::size_t>((ordered.size()-1)*.95)]<<" / "<<ordered.back()<<"\n";};
        if(lazy_report){const auto& r=*lazy_report;out<<r.device.api<<" · "<<r.device.adapter<<"\n"<<r.contract<<"\n\nObjetos: "<<r.items<<" · activos: "<<r.active_items<<" · "<<(r.grouped?"agrupados":"mezclados")<<" · pasos por muestra: "<<r.frames<<"\n";
            out<<"Cambio de máscara cada N fotogramas (0=estable): "<<r.change_every<<"\nReinicio de ensayos independientes, fuera del fotograma (ms acumulados): "<<r.trial_reset_ms<<"\n";
            out<<"Memoria adicional de agrupación por variante, índices y prefijos (bytes): "<<r.group_extra_buffer_bytes<<"\n";
            out<<"Preparación total / compilación SMO / shaders / subida ms: "<<r.preparation_ms<<" / "<<r.source_compile_ms<<" / "<<r.shader_prepare_ms<<" / "<<r.upload_ms<<"\nBytes solicitados para buffers GPU: "<<r.buffer_bytes<<" · pico RAM del proceso bytes: "<<r.process_peak_working_set<<" (no es VRAM)\nPalabras verificadas: "<<r.verified_words<<"\n\n";
            out<<"Validación previa / calentamiento ms: "<<r.validation_ms<<" / "<<r.warmup_ms<<"\n\n";
            out<<"Las sondas aisladas de reagrupar se toman en caliente después de validar. El coste necesario ya está incluido en el fotograma: no se suma otra vez. La asignación por variante incluye el scratch compartido; el total de memoria lo cuenta una sola vez.\n\n";
            for(const auto& v:r.variants){out<<v.name<<"\nGPU tarea / fotograma GPU / fotograma con lectura / lectura de píxeles / conversión CPU ms: "<<v.gpu_ms<<" / "<<v.frame_gpu_ms<<" / "<<v.frame_ms<<" / "<<v.readback_ms<<" / "<<v.conversion_ms<<"\nOperaciones IR lógicas: "<<v.executed_instructions<<" · escrituras de estado: "<<v.writes<<" · hash: "<<v.output_hash<<"\nAgrupación inicial GPU ms / sonda aislada de reagrupar ms / reconstrucciones medidas: "<<v.initial_group_gpu_ms<<" / "<<v.group_gpu_ms<<" / "<<v.group_rebuilds<<"\nReloj GPU válido: "<<(v.timing_valid?"sí":"NO; no acredita rendimiento")<<"\n";spread(v.gpu_samples);out<<"\n";}
            for(unsigned i:{3u,4u}){const auto& grouped=r.variants[i];const auto& baseline=r.variants[i==3?1:2];const double saving=baseline.frame_gpu_ms-grouped.frame_gpu_ms;
                out<<"Coste inicial de agrupar GPU / vuelta host / asignación adicional (ms): "<<grouped.initial_group_gpu_ms<<" / "<<grouped.initial_group_wall_ms<<" / "<<grouped.group_allocation_ms<<"\n";
                out<<"Amortización GPU de "<<grouped.name<<": ";if(!r.change_every&&saving>0&&std::isfinite(grouped.initial_group_gpu_ms)&&grouped.initial_group_gpu_ms>=0&&grouped.timing_valid&&baseline.timing_valid)out<<std::ceil(grouped.initial_group_gpu_ms/saving)<<" fotogramas estimados, sólo GPU; no es amortización de toda la app.\n";else out<<"sin amortización estática acreditada; comparar el coste completo de cada cambio.\n";}
            out<<"SMO fuente → parser compartido → tablas de instrucciones por sentencia → GPU. El control convencional está especializado para este mismo programa. La omisión es un contrato experimental explícito de canal; no sustituye globalmente multiplicar por cero.\n\nCódigo SMO ejecutado:\n"<<r.source<<"\nIdentidad: "<<r.source_hash;
        }else if(subdivision_report){const auto& r=*subdivision_report;out<<r.device.api<<" · "<<r.device.adapter<<"\n"<<r.contract<<"\n\nPuntos: "<<r.items<<" · pasos: "<<r.steps<<" · retícula exacta: "<<r.grid_scale<<"\nPreparación / compilación SMO / subida ms: "<<r.preparation_ms<<" / "<<r.source_compile_ms<<" / "<<r.upload_ms<<"\nLectura / validación ms: "<<r.readback_ms<<" / "<<r.validation_ms<<"\nBytes solicitados para buffers GPU: "<<r.gpu_buffer_bytes<<" · pico RAM proceso bytes: "<<r.process_peak_working_set<<" (no es VRAM)\nValores verificados: "<<r.verified_values<<"\n\n";
            const std::array<const char*,4> names{"F12 general","F12 con escalado por exponente","Control convencional de retícula exacta",r.direct_f12?"F12 directo sin máquina de instrucciones":"F12 por instrucciones: fuente no canónica"};
            out<<"Shaders / comprobación previa / calentamiento ms: "<<r.shader_prepare_ms<<" / "<<r.preflight_ms<<" / "<<r.warmup_ms<<"\nLa conversión medida incluye el dibujo de puntos en la GPU.\n\n";
            for(unsigned i=0;i<r.variants.size();++i){const auto& v=r.variants[i];out<<names[i]<<"\nGPU tarea / conversión / fotograma GPU / fotograma con lectura ms: "<<v.task_median_ms<<" / "<<v.conversion_median_ms<<" / "<<v.frame_median_ms<<" / "<<v.wall_median_ms<<"\nHash puntos / imagen: "<<v.point_hash<<" / "<<v.image_hash<<"\nReloj GPU válido: "<<(v.timing_valid?"sí":"NO; no acredita rendimiento")<<"\n";spread(v.frame_gpu_ms);out<<"\n";}
            out<<"Preparación por etapa: recursos/verificación / creación del kernel (ms)\n";for(unsigned i=0;i<smo_subdivision_stages;++i)out<<i<<": "<<r.shader_asset_ms[i]<<" / "<<r.shader_creation_ms[i]<<"\n";
            const std::array<const char*,5> phase_names{"Contexto","Configuración","Compilación GLSL","Enlace GL","Subrutinas"};for(const auto& event:r.preparation_events)out<<phase_names[static_cast<unsigned>(event.stage)]<<" · "<<event.kernel_name<<" · "<<event.wall_ms<<" ms\n";
            out<<"SMO fuente → parser compartido → instrucciones F12 y prueba afín → GPU. Las dos primeras variantes conservan F12 mediante una máquina de instrucciones. La cuarta elimina esa máquina sólo para el flujo completo validado, conservando F12, el orden de operaciones y sus redondeos; una fuente distinta utiliza el retorno explícito por instrucciones. El control convencional simplifica toda la actualización afín y usa la misma retícula y precisión exacta en el dominio aceptado. Por tanto, la diferencia de coste incluye representación, conversión y simplificación, además de la máquina de instrucciones; no demuestra un efecto exclusivo de la base numérica.\n\nCódigo SMO ejecutado:\n"<<r.source;
        }
        out<<"\n\nLas medianas pequeñas o cercanas al 1 % no bastan para atribuir una ventaja. Se muestran las muestras y su dispersión; no son FPS de toda la aplicación. El fotograma medido incluye la tarea y su frontera de presentación declarada; el repintado de esta ventana ocurre después. Las variantes tienen la misma salida verificada: la vista muestra ese resultado común.\n";
        if((lazy_report&&lazy_report->device.software)||(subdivision_report&&subdivision_report->device.software))out<<"WARP: verificación funcional; no es rendimiento físico.\n";
        return out.str();
    }
    void cancel_benchmark(){if(deferred_benchmark){deferred_benchmark={};benchmarking=false;restore_execution_controls();label(tr("Prueba cancelada; código y borradores conservados.","Test cancelled; source and drafts preserved.","Test annulé ; code et brouillons conservés."));}
        else if(benchmark.joinable())benchmark.request_stop();}
    bool prepare_benchmark(std::function<void()> retry){pause_program();update_program(true);
        if(program_session&&!program_session->idle()){
            deferred_benchmark=std::move(retry);benchmarking=true;
            for(int i=1;i<=4;++i)EnableWindow(buttons[static_cast<std::size_t>(i)],FALSE);
            for(std::size_t i=6;i<buttons.size();++i)SetWindowTextW(buttons[i],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
            label(tr("Esperando a que termine la preparación GPU antes de medir…","Waiting for GPU preparation to finish before measuring…","Attente de la fin de préparation GPU avant la mesure…"));return false;}
        return true;}
    void start_sustained(SmoComputeApi api){if(benchmarking){cancel_benchmark();return;}if(!save()||!prepare_benchmark([this,api]{start_sustained(api);}))return;benchmarking=true;error.clear();hide_preview();for(int i=1;i<=4;++i)EnableWindow(buttons[static_cast<std::size_t>(i)],FALSE);
        for(int i=6;i<=8;++i)SetWindowTextW(buttons[static_cast<std::size_t>(i)],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
        if(program)SetWindowTextW(buttons[2],wide(tr("Reanudar","Resume","Reprendre")).c_str());
        label(tr("Preparando un minuto de escena dinámica…","Preparing a minute of dynamic scene work…","Préparation d’une minute de scène dynamique…"));
        {std::lock_guard lock(benchmark_mutex);benchmark_report.reset();particle_report.reset();scene_report.reset();rule_report.reset();d12_report.reset();d12_control_report.reset();d12_validation.reset();lazy_report.reset();subdivision_report.reset();pending_progress.reset();benchmark_error.clear();}
        benchmark=std::jthread([this,target=window,api](std::stop_token stop){try{SmoSustainedOptions options;options.api=api;
            if(backend==GraphicsBackend::warp&&api==SmoComputeApi::direct3d11){options.device.software=true;options.seconds=.08;options.warmup_seconds=.02;options.calibrate=false;options.workload={64,36,16,4};}
            auto report=run_smo_sustained(options,[this,target](SmoSceneProgress update){{std::lock_guard lock(benchmark_mutex);pending_progress=std::move(update);}PostMessageW(target,scene_progress,0,0);},stop);
            std::lock_guard lock(benchmark_mutex);scene_report=std::move(report);
        }catch(const std::exception& e){std::lock_guard lock(benchmark_mutex);benchmark_error=e.what();}PostMessageW(target,benchmark_complete,0,0);});
    }
    void choose_sustained(){if(benchmarking){cancel_benchmark();return;}const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create graphics API menu");
        AppendMenuW(menu,MF_STRING,api_first_id,L"DirectX 11 · 1 min");AppendMenuW(menu,MF_STRING,api_first_id+1,L"DirectX 12 · 1 min");AppendMenuW(menu,MF_STRING,api_first_id+2,L"Vulkan · 1 min");AppendMenuW(menu,MF_STRING,api_first_id+3,L"OpenGL · 1 min");RECT area{};GetWindowRect(buttons[8],&area);UINT chosen=0;{auto style=prepare_native_menu(menu,window);chosen=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,area.left,area.bottom,window,nullptr);DestroyMenu(menu);}if(chosen>=api_first_id&&chosen<api_first_id+4)start_sustained(static_cast<SmoComputeApi>(chosen-api_first_id));
    }
    void choose_math(bool duodecimal){if(benchmarking){cancel_benchmark();return;}const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create arithmetic API menu");
        const auto first=duodecimal?d12_api_id:rules_api_id;const auto suffix=duodecimal?L" · radix12 / FP32":L"";
        for(unsigned i=0;i<4;++i){const std::array<std::wstring_view,4> names{L"DirectX 11",L"DirectX 12",L"Vulkan",L"OpenGL"};const auto caption=std::wstring(names[i])+suffix;AppendMenuW(menu,MF_STRING,first+i,caption.c_str());}
        RECT area{};GetWindowRect(buttons[duodecimal?10:9],&area);UINT choice=0;{auto style=prepare_native_menu(menu,window);choice=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,area.left,area.bottom,window,nullptr);DestroyMenu(menu);}if(choice>=static_cast<UINT>(first)&&choice<static_cast<UINT>(first+4))start_math(static_cast<SmoComputeApi>(choice-first),duodecimal);
    }
    void start_math(SmoComputeApi api,bool duodecimal){if(benchmarking){cancel_benchmark();return;}if(!save()||!prepare_benchmark([this,api,duodecimal]{start_math(api,duodecimal);}))return;benchmarking=true;error.clear();hide_preview();
        for(int i=1;i<=4;++i)EnableWindow(buttons[static_cast<std::size_t>(i)],FALSE);for(std::size_t i=6;i<buttons.size();++i)SetWindowTextW(buttons[i],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
        label(duodecimal?tr("Motor experimental: formato duodecimal adaptable en la GPU…","Experimental engine: adaptive radix-12 format on the GPU…","Moteur expérimental : format duodécimal adaptatif sur le GPU…"):tr("Comprobando todas las reglas y variantes en la GPU…","Checking all rules and variants on the GPU…","Vérification de toutes les règles et variantes sur le GPU…"));
        {std::lock_guard lock(benchmark_mutex);benchmark_report.reset();particle_report.reset();scene_report.reset();rule_report.reset();d12_report.reset();d12_control_report.reset();d12_validation.reset();lazy_report.reset();subdivision_report.reset();pending_progress.reset();benchmark_error.clear();}
        benchmark=std::jthread([this,target=window,api,duodecimal](std::stop_token stop){try{
            SmoComputeOptions device;device.software=backend==GraphicsBackend::warp&&api==SmoComputeApi::direct3d11;
            if(duodecimal){auto validation=validate_smo_duodecimal(api,device,!device.software,stop,{},true);if(!validation.failures.empty()){
                    std::lock_guard lock(benchmark_mutex);d12_validation=std::move(validation);
                }else{SmoD12SceneOptions options;options.api=api;options.device=device;options.floating12=true;options.seconds=device.software?.02:60;options.warmup_seconds=device.software?0:1;options.minimum_frames=device.software?2:16;
                    if(device.software){options.bodies=2;options.width=12;options.height=8;}
                    const auto progress=[this,target](SmoSceneProgress update){{std::lock_guard lock(benchmark_mutex);pending_progress=std::move(update);}PostMessageW(target,scene_progress,0,0);};
                    auto report=run_smo_duodecimal_scene(options,progress,stop);std::optional<SmoD12SceneReport> comparison;
                    if(report.failures.empty()){options.fp32_control=true;comparison=run_smo_duodecimal_scene(options,progress,stop);}
                    std::lock_guard lock(benchmark_mutex);d12_validation=std::move(validation);d12_report=std::move(report);d12_control_report=std::move(comparison);}}
            else{SmoRuleGpuOptions options;options.api=api;options.device=device;auto report=run_smo_rule_gpu(options,stop);std::lock_guard lock(benchmark_mutex);rule_report=std::move(report);}
        }catch(const std::exception& e){std::lock_guard lock(benchmark_mutex);benchmark_error=e.what();}PostMessageW(target,benchmark_complete,0,0);});
    }
    std::string math_text()const{std::ostringstream text;text<<std::fixed<<std::setprecision(6);
        if(rule_report){const auto& report=*rule_report;text<<"Reglas GPU · control de conformidad FP32 y representación duodecimal\n\n"<<report.device.api<<" · "<<report.device.adapter<<"\nCasos: "<<report.cases<<" · perfiles: "<<report.profiles<<"\nDiferencias A/B/C: "<<report.mismatches[0]<<" / "<<report.mismatches[1]<<" / "<<report.mismatches[2]<<"\n";
            text<<"Los valores flotantes son soporte binario acotado; esta prueba NO se presenta como el motor duodecimal completo. Las cifras empaquetadas y los racionales se comparan exactamente.\n\n";
            for(const auto& row:report.benchmarks){text<<row.name<<"\nPerfil: "<<smo_rule_profile_name(row.profile)<<"\nMedianas GPU A/B/C (ms): "<<row.median_ms[0]<<" / "<<row.median_ms[1]<<" / "<<row.median_ms[2]<<"\nEquivalentes: "<<(row.equivalent?"sí":"NO")<<"\n\n";}
            text<<report.conclusion<<"\n"<<report.source_identity;
        }else{if(d12_validation){text<<"Motor duodecimal GPU · experimental\n\n"<<d12_validation->device.api<<" · "<<d12_validation->device.adapter<<"\nReglas comprobadas: "<<d12_validation->cases<<" · diferencias EXACTAS: "<<d12_validation->failures.size()<<"\n";
                for(std::size_t i=0;i<std::min<std::size_t>(16,d12_validation->failures.size());++i)text<<"FALLO: "<<d12_validation->failures[i].name<<"\n";}
            if(d12_report){const auto& report=*d12_report;text<<(report.options.floating12?"\nFormato flotante radix12: 12 cifras significativas y escala adaptable, ejecutado mediante enteros. Sin instrucciones FP32/FP64 en su aritmética GPU. Las composiciones geométricas usan el árbol de redondeo declarado de este perfil.\n":"\nReferencia fija: cuatro grupos de cuatro cifras en base doce; ocho cifras fraccionarias. Aritmética GPU entera.\n")<<"Duración medida(s): "<<report.seconds<<" · fotogramas: "<<report.frames<<"\nGPU ms mediana/P95/P99: "<<report.median_ms<<" / "<<report.p95_ms<<" / "<<report.p99_ms<<"\nContactos: "<<report.contacts<<" · pruebas de rayos: "<<report.ray_tests<<"\nErrores de dominio/rango: "<<report.fatal_values<<" · avisos de cuantización: "<<report.inexact_values<<"\n";
                text<<"Transiciones físicas verificadas: "<<report.validated_body_steps<<" · rayos verificados: "<<report.validated_rays<<" · puntos de control: "<<report.validated_checkpoints<<"\nTiempos GPU válidos: "<<(report.timing_valid?"sí":"NO")<<"\n";
                for(const auto& failure:report.failures)text<<"FALLO: "<<failure<<"\n";
                if(d12_control_report){const auto& control=*d12_control_report;text<<"\nControl FP32 nativo con la misma geometría, fórmulas y ABI; no es el mejor control FP32 compacto. Se contrasta con tolerancia 0.0002 * max(1, magnitud). La conformidad CPU/GPU duodecimal exige palabras y diagnósticos idénticos, sin tolerancia.\nDuración(s): "<<control.seconds<<" · GPU ms mediana/P95/P99: "<<control.median_ms<<" / "<<control.p95_ms<<" / "<<control.p99_ms<<"\n";
                    for(const auto& failure:control.failures)text<<"CONTROL: "<<failure<<"\n";
                    if(report.failures.empty()&&control.failures.empty()&&report.timing_valid&&control.timing_valid&&control.scene_identity==report.scene_identity&&control.median_ms>0)
                        text<<"Relación de tiempo radix12/FP32: "<<report.median_ms/control.median_ms<<" (mayor que uno significa que radix12 tarda más).\n";
                    else text<<"La comparación no acredita una mejora válida.\n";}
                text<<"\nLas potencias se aplican a cuadrados de magnitudes e inversos/distancias de ray tracing. Las variantes se prueban por separado y no cambian tus reglas por defecto. Esta escena es un prototipo de aritmética gráfica, no un compilador GPU completo del programa del editor. No se afirma mayor velocidad que FP32 por usar base 12. Los tiempos cubren los cuatro pases de cómputo GPU; los fotogramas incluyen inspección CPU y no son FPS de la aplicación.\n"<<report.identity;}}
        if((rule_report&&rule_report->device.software)||(d12_validation&&d12_validation->device.software))text<<"\nWARP: corrección breve; no acredita un minuto físico ni rendimiento de hardware.";return text.str();
    }
    HWND details(std::string_view heading,const std::string& body){
        WNDCLASSW cls{};cls.lpfnWndProc=rules_proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LearnGameAndEngineDev.SmoRules";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        WNDCLASSW existing{};if(!GetClassInfoW(cls.hInstance,cls.lpszClassName,&existing))RegisterClassW(&cls);
        const DetailsContent content{&body,tr("Volver","Back","Retour")};
        const auto child=create_internal_window(window,cls.lpszClassName,wide(heading).c_str(),const_cast<DetailsContent*>(&content));
        ShowWindow(child,SW_SHOW);SetFocus(GetDlgItem(child,1));return child;
    }
    std::string benchmark_text(const SmoBenchmarkReport& report)const{
        std::ostringstream out;out<<tr("Prueba de estrés GPU · resultados experimentales\n\n","GPU stress test · experimental results\n\n","Test de charge GPU · résultats expérimentaux\n\n")
            <<report.backend<<(report.software?" · WARP (software)":" · GPU hardware")<<"\n"
            <<tr("FP32; misma entrada y carga; 2 calentamientos y 9 rondas alternadas. Tiempos decimales de GPU, sólo Dispatch, sin lectura de resultados ni compilación.\nNo es un compilador GPU del lenguaje SMO: compara la traducción experimental de sus operaciones.\n\n",
                "FP32; same inputs/work; 2 warmups and 9 alternating rounds. Decimal GPU timings of Dispatch only, excluding readback and compilation.\nThis is not a GPU compiler for SMO: it compares experimental lowering of its operations.\n\n",
                "FP32 ; mêmes entrées et charge ; 2 échauffements et 9 tours alternés. Temps GPU décimaux de Dispatch, hors lecture et compilation.\nCe n’est pas un compilateur GPU SMO : il compare une traduction expérimentale de ses opérations.\n\n");
        out<<tr("Dispatch por muestra medida: ","Dispatches per timed sample: ","Dispatch par échantillon mesuré : ")<<report.dispatches_per_sample
            <<tr(". Entradas calientes; ms por lote, no por frame.\n\n",". Warm inputs; ms per batch, not per frame.\n\n",". Entrées chaudes ; ms par lot, pas par image.\n\n");
        out<<std::fixed<<std::setprecision(5);
        for(const auto& row:report.rows){out<<row.name<<"\n"<<tr("Valores: ","Values: ","Valeurs : ")<<row.values
            <<tr(" · diferencias: "," · mismatches: "," · différences : ")<<row.mismatches
            <<tr(" · error máximo: "," · maximum error: "," · erreur maximale : ")<<row.max_error<<"\n";
            out<<(row.equivalent?tr("Equivalentes en esta muestra.","Equivalent in this sample.","Équivalents dans cet échantillon."):
                tr("RESULTADOS DISTINTOS: no se acredita una optimización.","DIFFERENT OUTPUTS: no optimization is credited.","RÉSULTATS DIFFÉRENTS : aucune optimisation n’est établie."))<<"\n";
            if(row.timing_valid){out<<tr("Mediana convencional / SMO (ms): ","Ordinary / SMO median (ms): ","Médiane ordinaire / SMO (ms) : ")<<row.ordinary_median_ms<<" / "<<row.smo_median_ms<<"\n";
                if(row.equivalent&&row.smo_median_ms>0)out<<tr("Relación convencional/SMO: ","Ordinary/SMO ratio: ","Rapport ordinaire/SMO : ")<<row.ordinary_median_ms/row.smo_median_ms<<"\n";
                out<<"ms A:";for(const auto value:row.ordinary_ms)out<<' '<<value;out<<"\nms B:";for(const auto value:row.smo_ms)out<<' '<<value;out<<"\n";
            }else out<<tr("Tiempos de GPU no válidos; no se sustituyen por un reloj de CPU.\n","GPU timings invalid; no CPU-clock substitution.\n","Temps GPU non valides ; pas de substitution par une horloge CPU.\n");
            out<<row.note<<"\n\n";
        }
        out<<report.conclusion<<"\n\n"<<tr("Una muestra finita no prueba superioridad universal. Los casos no equivalentes sirven para estudiar la semántica, no para afirmar que SMO acelera el mismo cálculo.",
            "A finite sample does not prove universal superiority. Non-equivalent cases study semantics and cannot establish a speedup of the same computation.",
            "Un échantillon fini ne prouve pas une supériorité universelle. Les cas non équivalents étudient la sémantique et ne prouvent pas une accélération du même calcul.");return out.str();
    }
    std::string particle_text(const SmoParticleReport& report)const{
        std::ostringstream out;out<<std::fixed<<std::setprecision(5);
        out<<tr("Partículas · reducción de cálculos compartidos\n\n","Particles · reducing shared computations\n\n","Particules · réduction des calculs partagés\n\n")<<report.backend
            <<(report.software?" · WARP (software)":" · GPU hardware")<<"\n"
            <<tr("A: convencional por partícula. B: convencional agrupado. C: SMO agrupado.\nSe comprueban todas las posiciones; A/B/C reciben el mismo tiempo y datos por fotograma. El prepaso de grupos está incluido.\nSon tiempos GPU de cálculo por fotograma, NO los FPS completos de la aplicación.\n\n",
                 "A: ordinary per particle. B: grouped ordinary. C: grouped SMO.\nEvery position is checked; A/B/C receive the same time and data per frame. The group prepass is included.\nThese are GPU computation times per frame, NOT full application FPS.\n\n",
                 "A : ordinaire par particule. B : ordinaire groupé. C : SMO groupé.\nToutes les positions sont vérifiées ; A/B/C reçoivent le même temps et les mêmes données par image. La prépasse est incluse.\nTemps de calcul GPU par image, PAS les FPS complets de l’application.\n\n");
        out<<tr("Calibración real · objetivo GPU (ms): ","Real calibration · GPU target (ms): ","Étalonnage réel · cible GPU (ms) : ")<<report.target_ms<<"\n";
        for(const auto& value:report.calibration)out<<value.particles<<" · "<<value.terms<<tr(" términos · A ms: "," terms · A ms: "," termes · A ms : ")
            <<(value.timing_valid?std::to_string(value.naive_ms):tr("no válido","invalid","non valide"))<<"\n";
        for(const auto& row:report.rows){out<<"\n"<<tr("Partículas: ","Particles: ","Particules : ")<<row.particles
                <<tr(" · por grupo: "," · per group: "," · par groupe : ")<<row.group_size<<tr(" · grupos: "," · groups: "," · groupes : ")<<row.groups
                <<tr(" · términos: "," · terms: "," · termes : ")<<row.terms<<"\n"
                <<tr("Evaluaciones de términos A / B y C por fotograma: ","Term evaluations A / B and C per frame: ","Évaluations de termes A / B et C par image : ")<<row.naive_term_evaluations<<" / "<<row.grouped_term_evaluations<<"\n"
                <<tr("Valores: ","Values: ","Valeurs : ")<<row.values<<tr(" · diferencias: "," · mismatches: "," · différences : ")<<row.mismatches<<tr(" · error máximo: "," · maximum error: "," · erreur maximale : ")<<row.max_error<<"\n";
            out<<(row.target_reached?tr("Carga objetivo alcanzada.\n","Target load reached.\n","Charge cible atteinte.\n"):tr("No se alcanzó la carga objetivo dentro del límite de seguridad.\n","Target load not reached within the safety limit.\n","Charge cible non atteinte dans la limite de sécurité.\n"));
            if(row.timing_valid){out<<tr("Medianas GPU A / B / C (ms): ","GPU medians A / B / C (ms): ","Médianes GPU A / B / C (ms) : ")<<row.naive_median_ms<<" / "<<row.grouped_ordinary_median_ms<<" / "<<row.grouped_smo_median_ms<<"\n";
                if(row.equivalent&&row.grouped_smo_median_ms>0)out<<tr("A/C: ","A/C: ","A/C : ")<<row.naive_median_ms/row.grouped_smo_median_ms<<"\n";
                const auto samples=[&](std::string_view name,const auto& values){out<<name;for(const auto value:values)out<<' '<<value;out<<"\n";};
                samples("ms A:",row.naive_ms);samples("ms B:",row.grouped_ordinary_ms);samples("ms C:",row.grouped_smo_ms);
            }else out<<tr("Consultas GPU no válidas; no hay comparación temporal.\n","GPU queries invalid; no timing comparison.\n","Requêtes GPU non valides ; pas de comparaison temporelle.\n");
            out<<(row.equivalent?tr("Resultados equivalentes dentro de la tolerancia declarada.\n","Outputs equivalent within the stated tolerance.\n","Résultats équivalents dans la tolérance indiquée.\n"):
                 tr("Resultados diferentes: no se acredita una optimización.\n","Different outputs: no optimization credited.\n","Résultats différents : aucune optimisation établie.\n"))<<row.note<<"\n";
        }
        out<<"\n"<<report.conclusion;return out.str();
    }
    void start_benchmark(bool particles=false){if(benchmarking){cancel_benchmark();return;}
        if(!prepare_benchmark([this,particles]{start_benchmark(particles);}))return;
        hide_preview();
        benchmarking=true;error.clear();for(int i=1;i<=4;++i)EnableWindow(buttons[static_cast<std::size_t>(i)],FALSE);
        SetWindowTextW(buttons[6],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
        SetWindowTextW(buttons[7],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
        SetWindowTextW(buttons[8],wide(tr("Cancelar prueba","Cancel test","Annuler le test")).c_str());
        if(program)SetWindowTextW(buttons[2],wide(tr("Reanudar","Resume","Reprendre")).c_str());
        label(tr("Prueba GPU real en curso; el editor sigue disponible. No se mide el intérprete ni la animación.","Real GPU test running; the editor remains available. The interpreter/animation is not being timed.","Test GPU réel en cours ; l’éditeur reste disponible. L’interpréteur et l’animation ne sont pas mesurés."));
        {std::lock_guard lock(benchmark_mutex);benchmark_report.reset();particle_report.reset();scene_report.reset();rule_report.reset();d12_report.reset();d12_control_report.reset();d12_validation.reset();lazy_report.reset();subdivision_report.reset();pending_progress.reset();benchmark_error.clear();}
        benchmark=std::jthread([this,target=window,particles](std::stop_token stop){try{
            if(particles){auto report=run_smo_particle_benchmark(backend,true,stop);std::lock_guard lock(benchmark_mutex);particle_report=std::move(report);}
            else{auto report=run_smo_benchmark(backend,65536,9,stop);std::lock_guard lock(benchmark_mutex);benchmark_report=std::move(report);}}
            catch(const std::exception& e){std::lock_guard lock(benchmark_mutex);benchmark_error=e.what();}PostMessageW(target,benchmark_complete,0,0);});
    }
    void benchmark_finished(){const bool cancelled=benchmark.get_stop_token().stop_requested();if(benchmark.joinable())benchmark.join();benchmarking=false;
        restore_execution_controls();
        for(int i=1;i<=4;++i)EnableWindow(buttons[static_cast<std::size_t>(i)],TRUE);SetWindowTextW(buttons[6],wide(tr("Prueba GPU","GPU test","Test GPU")).c_str());SetWindowTextW(buttons[7],wide(tr("Partículas","Particles","Particules")).c_str());
        SetWindowTextW(buttons[8],wide(tr("1 min · APIs","1 min · APIs","1 min · API")).c_str());
        SetWindowTextW(buttons[9],wide(tr("Reglas GPU","GPU rules","Règles GPU")).c_str());SetWindowTextW(buttons[10],wide(tr("Motor base 12","Base-12 engine","Moteur base 12")).c_str());
        std::lock_guard lock(benchmark_mutex);if(cancelled){label(tr("Prueba cancelada; código y borradores conservados.","Test cancelled; source and drafts preserved.","Test annulé ; code et brouillons conservés."));return;}
        if(!benchmark_report&&!particle_report&&!scene_report&&!rule_report&&!d12_validation&&!lazy_report&&!subdivision_report){error=benchmark_error;label(error,true);return;}
        const bool math_failed=(d12_validation&&!d12_validation->failures.empty())||(d12_report&&!d12_report->failures.empty())||(d12_control_report&&!d12_control_report->failures.empty())||
            (rule_report&&!rule_report->equivalent)||(lazy_report&&!lazy_report->equivalent)||(subdivision_report&&!subdivision_report->equivalent);
        label(math_failed?tr("La comprobación detectó fallos. Consulta sus diagnósticos; los tiempos no acreditan una mejora válida.","Validation found failures. See diagnostics; timings do not establish a valid improvement.","La vérification a détecté des erreurs. Consultez les diagnostics ; les temps ne prouvent pas une amélioration valide."):tr("Prueba terminada. Consulta equivalencia y tiempos en la ventana de resultados.","Test finished. See equivalence and timings in the results window.","Test terminé. Consultez l’équivalence et les temps dans la fenêtre de résultats."),math_failed);
        if(lazy_report||subdivision_report){displayed_progress={};
            if(subdivision_report){displayed_progress.width=subdivision_report->width;displayed_progress.height=subdivision_report->height;displayed_progress.preview_bgra=subdivision_report->preview_bgra;}
            else{displayed_progress.width=lazy_report->width;displayed_progress.height=lazy_report->height;displayed_progress.preview_bgra=lazy_report->preview_bgra;}
            preview_mode=PreviewMode::benchmark;ShowWindow(canvas,SW_HIDE);ShowWindow(preview,SW_SHOW);InvalidateRect(preview,nullptr,FALSE);}
        if(results&&IsWindow(results))DestroyWindow(results);const auto heading=(lazy_report||subdivision_report)?tr("SMO · Aplicaciones GPU","SMO · GPU applications","SMO · Applications GPU"):tr("SMO · Prueba de estrés GPU","SMO · GPU stress test","SMO · Test de charge GPU");results=details(heading,(lazy_report||subdivision_report)?task_text():(rule_report||d12_validation)?math_text():scene_report?scene_text(*scene_report):particle_report?particle_text(*particle_report):benchmark_text(*benchmark_report));
    }
    bool save(){if(!editor)return true;try{const auto document=editor->document();
        if(!dirty&&document.selection_start==saved_selection_start&&document.selection_end==saved_selection_end)return true;
        state::StateStore store(environment.paths.root);auto data=store.load();
        state::Draft draft;draft.source=document.text;draft.indentation_json=state::make_indentation_metadata(document.text,document.indentation_starts);
        draft.selection_start=document.selection_start;draft.selection_end=document.selection_end;state::put_draft(data,"native_smo_laboratory/"+sample,draft);
        data.preferences["native_smo_laboratory"]["selected"]=sample;store.save(data);dirty=false;saved_selection_start=document.selection_start;saved_selection_end=document.selection_end;return true;
        }catch(const std::exception& e){error=e.what();label(error,true);return false;}}
    void select(std::string_view which){if(!save())return;
        if(sample!=which){state::StateStore store(environment.paths.root);auto data=store.load();data.preferences["native_smo_laboratory"]["selected"]=std::string(which);store.save(data);}
        hide_preview();loading=true;sample=which;clear_program();
        const auto initial=sample_source(sample);state::StateStore store(environment.paths.root);const auto data=store.load();const auto draft=state::get_draft(data,"native_smo_laboratory/"+sample,initial);
        auto document=draft.has_saved?windows::EditorDocument{draft.source,state::indentation_units(draft.source,draft.indentation_json),draft.selection_start,draft.selection_end}:windows::predefined_code_document(initial);
        if(editor)editor->reset(std::move(document));else editor=std::make_unique<windows::CodeEditor>(source,std::move(document),true);
        const auto loaded=editor->document();saved_selection_start=loaded.selection_start;saved_selection_end=loaded.selection_end;
        loading=false;dirty=false;error.clear();SetWindowTextW(buttons[2],wide(tr("Pausar","Pause","Pause")).c_str());
        label(tr("Código SMO listo. Pulsa Ejecutar para procesarlo.","SMO source ready. Run it to process the program.","Code SMO prêt. Exécutez-le pour traiter le programme."));}
    void execute(){if(!save())return;hide_preview();clear_program();error.clear();
        if(!program_session){error="No se inició la sesión GPU SMO.";label(error,true);return;}
        program=true;running=true;program_session->execute(edit_text(source));
        SetWindowTextW(buttons[2],wide(tr("Pausar","Pause","Pause")).c_str());
        label(program_backend==SmoProgramBackend::compiled_binary32?
            tr("Compilando el texto SMO en base 12 para la GPU nativa…","Compiling radix-12 SMO source for the native GPU…","Compilation du texte SMO en base 12 pour le GPU natif…"):
            tr("Preparando la referencia experimental F12…","Preparing the experimental F12 reference…","Préparation de la référence expérimentale F12…"));}
    void update_status(){if(!program_image)return;const auto& info=program_image->device;
        const auto count12=[](std::uint32_t value){std::string text;do{const auto digit=value%12u;text.push_back(digit<10u?static_cast<char>('0'+digit):digit==10u?'#':'~');value/=12u;}while(value);std::ranges::reverse(text);return text;};
        label((program_backend==SmoProgramBackend::compiled_binary32?tr("SMO compilado · ","Compiled SMO · ","SMO compilé · "):tr("Referencia F12 · ","F12 reference · ","Référence F12 · "))+info.api+" · "+info.adapter+
            tr("\nVértices ","\nVertices ","\nSommets ")+count12(program_image->vertices)+
            tr(" · caras "," · faces "," · faces ")+count12(program_image->faces)+
            (program_backend==SmoProgramBackend::compiled_binary32?tr(" · GPU nativa; aritmética ordinaria."," · native GPU; ordinary arithmetic."," · GPU natif ; arithmétique ordinaire."):tr(" · aritmética experimental F12."," · experimental F12 arithmetic."," · arithmétique expérimentale F12.")));}
    void tick(){if(!program_session)return;
        if(deferred_benchmark&&program_session->idle()){auto retry=std::move(deferred_benchmark);deferred_benchmark={};benchmarking=false;restore_execution_controls();retry();}
        program_session->visible(!benchmarking&&IsWindowVisible(window)&&!IsIconic(GetAncestor(window,GA_ROOT)));update_program();}
    void initialize(HWND target){window=target;reader_theme::apply(window,reader_theme::Surface::canvas);rich_edit_module=LoadLibraryW(L"Msftedit.dll");if(!rich_edit_module)throw std::runtime_error("Cannot load the native text editor");
        const auto data=state::StateStore(environment.paths.root).load();reader_theme::set_accent(window,reader_theme::appearance_accent(data));
        buttons[0]=control(L"BUTTON",tr("Volver","Back","Retour"),back_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(buttons[0]);
        buttons[1]=control(L"BUTTON",tr("Ejecutar SMO","Run SMO","Exécuter SMO"),run_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[2]=control(L"BUTTON",tr("Pausar","Pause","Pause"),pause_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[3]=control(L"BUTTON",tr("Triángulo","Triangle","Triangle"),triangle_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[4]=control(L"BUTTON",tr("Cubo","Cube","Cube"),cube_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[5]=control(L"BUTTON",tr("Reglas SMO","SMO rules","Règles SMO"),rules_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[6]=control(L"BUTTON",tr("Prueba GPU","GPU test","Test GPU"),benchmark_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[7]=control(L"BUTTON",tr("Partículas","Particles","Particules"),particles_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[8]=control(L"BUTTON",tr("1 min · APIs","1 min · APIs","1 min · API"),sustained_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[9]=control(L"BUTTON",tr("Reglas GPU","GPU rules","Règles GPU"),gpu_rules_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[10]=control(L"BUTTON",tr("Motor base 12","Base-12 engine","Moteur base 12"),d12_id,WS_TABSTOP|BS_PUSHBUTTON);
        buttons[11]=control(L"BUTTON",tr("Aplicaciones GPU","GPU applications","Applications GPU"),tasks_id,WS_TABSTOP|BS_PUSHBUTTON);
        for(std::size_t i=1;i<buttons.size();++i)reader_theme::primary_button(buttons[i]);
        title=control(L"STATIC",tr("Laboratorio SMO · Sistema Matemático Objetivo","SMO laboratory · Objective Mathematical System","Laboratoire SMO · Système mathématique objectif"),title_id);
        source=control(MSFTEDIT_CLASS,"",source_id,WS_TABSTOP|ES_MULTILINE|ES_WANTRETURN|ES_AUTOHSCROLL|WS_HSCROLL|WS_VSCROLL);
        reader_theme::apply(source,reader_theme::Surface::compiler_editor);SendMessageW(source,EM_SETTARGETDEVICE,0,1);
        font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        code_font=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,reader_theme::code_font_face());
        for(const auto item:buttons)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(source,WM_SETFONT,reinterpret_cast<WPARAM>(code_font),FALSE);SendMessageW(source,EM_SETLIMITTEXT,64*1024,0);
        CHARFORMAT2W ink{};ink.cbSize=sizeof(ink);ink.dwMask=CFM_COLOR;ink.crTextColor=reader_theme::foreground(source);SendMessageW(source,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&ink));
        status=control(L"STATIC","",status_id);SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        // Programs are rendered by their numerical GPU backend. This blank
        // GDI canvas never computes or projects the authored geometry.
        canvas=control(L"STATIC","",canvas_id,SS_OWNERDRAW);preview=control(L"STATIC","",preview_id,SS_OWNERDRAW);ShowWindow(preview,SW_HIDE);
        SmoComputeOptions compute_options;compute_options.software=backend==GraphicsBackend::warp;
        const bool native_profile=program_backend==SmoProgramBackend::compiled_binary32;
        const auto render_width=native_profile?(compute_options.software?128u:512u):(compute_options.software?64u:128u);
        const auto render_height=native_profile?(compute_options.software?72u:288u):(compute_options.software?36u:72u);
        program_session=std::make_unique<SmoProgramSession>(SmoComputeApi::direct3d11,compute_options,render_width,render_height,
            [target]{PostMessageW(target,program_progress,0,0);},program_backend);
        select(sample);layout();SetTimer(window,frame_timer,16,nullptr);}
    void layout(){if(!canvas)return;RECT area{};GetClientRect(window,&area);const auto width=static_cast<int>(area.right),height=static_cast<int>(area.bottom);const auto margin=px(12),gap=px(8),row=px(42);
        MoveWindow(buttons[0],margin,margin,row,row,TRUE);MoveWindow(title,margin+row+gap,margin,std::max(1,width-margin*2-row-gap),row,TRUE);
        const int y=margin+row+gap,columns=width>=px(1400)?11:width>=px(900)?6:4,rows=(11+columns-1)/columns;for(int i=1;i<12;++i){const auto column=(i-1)%columns,level=(i-1)/columns;
            const auto x=margin+(width-margin*2)*column/columns;MoveWindow(buttons[static_cast<std::size_t>(i)],x,y+level*(row+gap),std::max(1,(width-margin*2)/columns-gap),row,TRUE);}
        const int top=y+rows*(row+gap),bottom=std::max(top+1,height-px(66)-margin),usable=std::max(1,bottom-top);
        if(width>=px(760)){const auto left=(width-margin*2-gap)/2;MoveWindow(source,margin,top,left,usable,TRUE);MoveWindow(canvas,margin+left+gap,top,std::max(1,width-margin*2-left-gap),usable,TRUE);}
        else{const auto first=std::max(1,(usable-gap)/2);MoveWindow(source,margin,top,std::max(1,width-margin*2),first,TRUE);MoveWindow(canvas,margin,top+first+gap,std::max(1,width-margin*2),std::max(1,usable-first-gap),TRUE);}
        RECT canvas_area{};GetWindowRect(canvas,&canvas_area);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&canvas_area),2);MoveWindow(preview,canvas_area.left,canvas_area.top,canvas_area.right-canvas_area.left,canvas_area.bottom-canvas_area.top,TRUE);
        MoveWindow(status,margin,bottom+gap,std::max(1,width-margin*2),px(60),TRUE);reader_theme::refresh_scrollbars(source);}
    void changed(){if(loading||!editor)return;editor->capture_change();dirty=true;clear_program();hide_preview();
        error.clear();label(tr("Código modificado. Ejecuta de nuevo para aplicarlo.","Source changed. Run again to apply it.","Code modifié. Exécutez-le de nouveau pour l’appliquer."));SetTimer(window,save_timer,400,nullptr);}
    void command(int id){if(id==back_id){if(save()){const auto callback=on_back;if(callback)callback();}return;}
        if(id==tasks_id){choose_tasks();return;}
        if(id>=lazy_first_id&&id<lazy_first_id+40){const auto key=static_cast<unsigned>(id-lazy_first_id);start_task(static_cast<SmoComputeApi>(key/10),false,(key%5)*25,(key%10)<5);return;}
        if(id>=subdivision_first_id&&id<subdivision_first_id+4){start_task(static_cast<SmoComputeApi>(id-subdivision_first_id),true,0,true);return;}
        if((id>=lazy_change_first_id&&id<lazy_change_first_id+40)||(id>=lazy_slow_change_first_id&&id<lazy_slow_change_first_id+40)){const bool slow=id>=lazy_slow_change_first_id;const auto key=static_cast<unsigned>(id-(slow?lazy_slow_change_first_id:lazy_change_first_id));start_task(static_cast<SmoComputeApi>(key/10),false,(key%5)*25,(key%10)<5,slow?4u:1u);return;}
        if(id==gpu_rules_id||id==d12_id){choose_math(id==d12_id);return;}
        if(id>=rules_api_id&&id<rules_api_id+4){start_math(static_cast<SmoComputeApi>(id-rules_api_id),false);return;}
        if(id>=d12_api_id&&id<d12_api_id+4){start_math(static_cast<SmoComputeApi>(id-d12_api_id),true);return;}
        if(id==sustained_id){choose_sustained();return;}if(id>=api_first_id&&id<api_first_id+4){start_sustained(static_cast<SmoComputeApi>(id-api_first_id));return;}
        if(id==benchmark_id||id==particles_id){start_benchmark(id==particles_id);return;}if(benchmarking&&id!=rules_id)return;
        if(id==run_id){execute();return;}if(id==triangle_id||id==cube_id){select(id==triangle_id?"triangle":"cube");return;}
        if(id==pause_id&&program){running=!running;if(program_session)program_session->running(running);update_program(true);
            SetWindowTextW(buttons[2],wide(running?tr("Pausar","Pause","Pause"):tr("Reanudar","Resume","Reprendre")).c_str());
            if(running)update_status();else label(tr("SMO pausado; no se ejecuta la actualización.","SMO paused; the update block is not executed.","SMO en pause ; le bloc de mise à jour n’est pas exécuté."));return;}
        if(id==rules_id){if(rules&&IsWindow(rules)){ShowWindow(rules,SW_SHOW);SetWindowPos(rules,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetFocus(GetDlgItem(rules,1));return;}rules=details(tr("Reglas SMO · provisionales","SMO rules · provisional","Règles SMO · provisoires"),rules_text());}}
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Lab*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Lab*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    try{switch(message){case WM_CREATE:self->initialize(window);return 0;case WM_SIZE:self->layout();return 0;
        case WM_COMMAND:if(LOWORD(wp)==source_id&&HIWORD(wp)==EN_CHANGE)self->changed();else if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
        case WM_TIMER:if(wp==frame_timer)self->tick();else if(wp==save_timer){KillTimer(window,save_timer);self->save();}return 0;
        case benchmark_complete:self->benchmark_finished();return 0;
        case scene_progress:self->update_scene_progress();return 0;
        case program_progress:self->update_program();return 0;
        case task_progress:{std::string text;{std::lock_guard lock(self->benchmark_mutex);text=self->pending_task_status;}if(self->benchmarking)self->label(text);return 0;}
        case WM_MEASUREITEM:if(measure_native_menu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lp)))return TRUE;break;
        case WM_MENUCHAR:{LRESULT result=0;if(native_menu_character(reinterpret_cast<HMENU>(lp),LOWORD(wp),result))return result;break;}
        case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(draw_native_menu(item))return TRUE;if(item.CtlID==preview_id){self->draw_preview(item);return TRUE;}
            if(item.CtlID==canvas_id){const auto fill=CreateSolidBrush(RGB(14,27,48));FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);return TRUE;}
            if(item.CtlID==back_id){draw_icon(item.hDC,item.rcItem,Icon::back,reader_theme::foreground(item.hwndItem));return TRUE;}if(reader_theme::draw_button(item))return TRUE;break;}
        case WM_ERASEBKGND:{RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::canvas));return 1;}
        case WM_DESTROY:KillTimer(window,frame_timer);KillTimer(window,save_timer);self->stop_benchmark();self->running=false;self->program_session.reset();self->save();self->editor.reset();if(self->rules&&IsWindow(self->rules))DestroyWindow(self->rules);if(self->results&&IsWindow(self->results))DestroyWindow(self->results);return 0;
        case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;return DefWindowProcW(window,message,wp,lp);}
        LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    }catch(const std::exception& e){self->error=e.what();if(self->status)self->label(self->error,true);if(message==WM_CREATE)return -1;}
    return DefWindowProcW(window,message,wp,lp);
}
}
HWND create_smo_view(HWND parent,const WindowsEnvironment& environment,std::function<void()> on_back,GraphicsBackend backend,SmoProgramBackend execution_backend){
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=lab_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    WNDCLASSW existing{};if(!GetClassInfoW(cls.hInstance,lab_class,&existing)&&!RegisterClassW(&cls))throw std::runtime_error("Cannot register SMO laboratory");
    auto owner=std::make_unique<Lab>(environment,std::move(on_back),backend,execution_backend);const auto view=CreateWindowExW(WS_EX_CONTROLPARENT,lab_class,L"Laboratorio SMO",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,1,1,parent,nullptr,cls.hInstance,owner.get());if(!view)throw std::runtime_error("Cannot create SMO laboratory");owner->owned=true;owner.release();return view;
}
SmoLabSnapshot inspect_smo_lab(HWND window,bool read_pixels){const auto* self=reinterpret_cast<Lab*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(!self)throw std::invalid_argument("Invalid SMO laboratory");
    SmoLabSnapshot result;result.backend=self->program_image?self->program_image->backend:self->program_backend;result.source=edit_text(self->source);result.sample=self->sample;result.status=self->status_text;result.error=self->error;result.running=self->running;
    result.benchmarking=self->benchmarking;result.vertices=self->latest_vertices;result.faces=self->latest_faces;result.interpreter_ms=self->interpreter_ms;
    if(self->program_image&&self->preview_mode==Lab::PreviewMode::program){const auto& frame=*self->program_image;result.surface.frames=frame.frames;result.surface.pixel_hash=frame.image.pixel_hash;
        result.surface.colored_pixels=static_cast<std::size_t>(frame.image.colored_pixels);result.surface.width=frame.image.width;result.surface.height=frame.image.height;
        result.surface.software=frame.device.software;result.surface.backend=frame.device.api+" · "+frame.device.adapter+(frame.device.software?" · WARP (software)":" · hardware");
    }else if(self->preview_mode==Lab::PreviewMode::benchmark&&!self->displayed_progress.preview_bgra.empty()){
        result.surface.width=self->displayed_progress.width;result.surface.height=self->displayed_progress.height;result.surface.frames=self->displayed_progress.frames;
        result.surface.backend=self->displayed_progress.api;result.surface.software=self->backend==GraphicsBackend::warp;
        std::uint64_t hash=14695981039346656037ull;for(const auto pixel:self->displayed_progress.preview_bgra){hash=(hash^pixel)*1099511628211ull;}
        result.surface.pixel_hash=hash;
    }else {RECT size{};GetClientRect(self->canvas,&size);result.surface.width=static_cast<unsigned>(std::max<LONG>(1,size.right));result.surface.height=static_cast<unsigned>(std::max<LONG>(1,size.bottom));
        result.surface.backend="Sin programa GPU ejecutado";
        if(read_pixels){std::uint64_t hash=14695981039346656037ull;const auto count=static_cast<std::uint64_t>(result.surface.width)*result.surface.height;
            for(std::uint64_t i=0;i<count;++i)for(unsigned byte=0;byte<4;++byte){hash^=(blank_bgra>>(byte*8u))&255u;hash*=1099511628211ull;}result.surface.pixel_hash=hash;}}
    return result;}
}

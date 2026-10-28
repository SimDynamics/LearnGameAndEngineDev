#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
struct Host {
    HWND window=CreateWindowExW(0,L"STATIC",L"Isolated current curricular fractions",WS_OVERLAPPEDWINDOW,
        0,0,1100,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ~Host(){if(window)DestroyWindow(window);}
};
struct Edit {
    HWND window;
    explicit Edit(HWND parent):window(CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY,
        0,0,800,1000,parent,nullptr,GetModuleHandleW(nullptr),nullptr)){
        require(window!=nullptr,"Cannot create the isolated current-feedback RichEdit");
        reader_theme::apply(window,reader_theme::Surface::card);
    }
    ~Edit(){DestroyWindow(window);}
};
struct Asset {
    std::filesystem::path path;
    std::string hash;
    JsonObject lesson;
    explicit Asset(std::string_view relative):path(std::filesystem::path(build::source_root)/
            "app/src/main/assets/content/es/math_physics"/std::filesystem::path(relative)){
        const auto bytes=state::read_bytes(path);hash=state::sha256(bytes);
        lesson=JsonObject::Parse(wide({reinterpret_cast<const char*>(bytes.data()),bytes.size()}));
    }
    JsonObject block(std::string_view id,std::size_t page,std::size_t position) const {
        const auto pages=lesson.GetNamedArray(L"pages");
        require(page<pages.Size(),"A registered fraction page is no longer current");
        const auto blocks=pages.GetAt(static_cast<std::uint32_t>(page)).GetObject().GetNamedArray(L"blocks");
        require(position<blocks.Size(),"A registered fraction block is no longer current");
        const auto result=blocks.GetAt(static_cast<std::uint32_t>(position)).GetObject();
        require(result.GetNamedString(L"id")==wide(id)&&result.GetNamedString(L"type")==L"math_puzzle",
            "The fraction regression no longer addresses the registered current puzzle");
        return result;
    }
    void unchanged()const{
        require(state::sha256(state::read_bytes(path))==hash,"Fraction QA changed an authored curricular asset");
    }
};
struct OriginalTypesetter {
    windows::NarrationWorker worker{std::filesystem::path(build::source_root)/"out/narration/runtime.json"};
    std::map<std::string,std::vector<NativeMathFragment>> fragments;
    std::string request(std::string_view raw){
        const auto reply=worker.math_editor(raw);
        const auto query=JsonObject::Parse(wide(raw));
        if(query.GetNamedString(L"action",L"")==L"text_layout"){
            const auto source=winrt::to_string(query.GetNamedString(L"text"));
            const auto result=JsonObject::Parse(wide(reply));
            require(result.HasKey(L"fragments"),"The canonical worker returned no original formula ranges");
            std::vector<NativeMathFragment> values;
            for(const auto& value:result.GetNamedArray(L"fragments")){
                const auto item=value.GetObject();
                const auto first=item.GetNamedNumber(L"start"),last=item.GetNamedNumber(L"end");
                require(first>=0&&last>first&&last<=wide(source).size(),"The canonical worker returned invalid UTF-16 ranges");
                values.push_back({static_cast<std::size_t>(first),static_cast<std::size_t>(last),
                    winrt::to_string(item.GetNamedString(L"formula")),item.GetNamedBoolean(L"literal",false)});
            }
            fragments[source]=std::move(values);
        }
        return reply;
    }
};
struct Assertions {
    unsigned failures=0;
    void expect(bool condition,const std::string& message){
        if(!condition){++failures;std::cout<<"FAIL "<<message<<'\n';}
    }
};
struct Fraction {
    std::string source;
    std::size_t begin=0,end=0,numerator=0,denominator=0;
};
Fraction fraction(std::string_view source,std::string_view spelling){
    const auto at=source.find(spelling);
    // The geometry feedback deliberately repeats 9/4 later in ordinary prose;
    // its first occurrence belongs to the authored explicit-delimiter equality.
    require(at!=source.npos,"A selected current fraction is absent from its authored text");
    const auto slash=spelling.find('/');
    require(slash!=spelling.npos&&slash+1<spelling.size(),"The regression fraction has no denominator");
    const auto first=spelling.find_first_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    require(first<slash,"The regression fraction has no numerator");
    return {std::string(spelling),wide(source.substr(0,at)).size(),wide(source.substr(0,at+spelling.size())).size(),
        wide(source.substr(0,at+first)).size(),wide(source.substr(0,at+slash+1)).size()};
}
struct MathArgument {LONG type=0,argument=-1;};
MathArgument inline_argument(HWND window,std::size_t position){
    Microsoft::WRL::ComPtr<IRichEditOle> rich;
    SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument2> document;
    require(rich&&SUCCEEDED(rich.As(&document)),"The actual curricular RichEdit has no TOM2 document");
    Microsoft::WRL::ComPtr<ITextRange2> range;
    require(SUCCEEDED(document->Range2(static_cast<LONG>(position),static_cast<LONG>(position),range.GetAddressOf())),
        "Cannot inspect an actual mapped curricular character");
    LONG type=0,alignment=0,character=0,first=0,second=0,count=-1,style=0,columns=0,level=0;
    if(range->GetInlineObject(&type,&alignment,&character,&first,&second,&count,&style,&columns,&level)!=S_OK)return {};
    return {type,count};
}
LONG visual_line(HWND target,std::size_t at){return static_cast<LONG>(SendMessageW(target,EM_LINEFROMCHAR,at,0));}
std::wstring native_story(HWND target){
    GETTEXTLENGTHEX length{GTL_PRECISE|GTL_NUMCHARS,1200};
    const auto count=SendMessageW(target,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    require(count>=0,"Cannot measure the actual curricular native story");
    std::wstring text(static_cast<std::size_t>(count)+1,L'\0');
    GETTEXTEX request{static_cast<DWORD>(text.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    const auto copied=SendMessageW(target,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&request),reinterpret_cast<LPARAM>(text.data()));
    require(copied>=0,"Cannot read the actual curricular native story");text.resize(static_cast<std::size_t>(copied));return text;
}
void inspect_current_text(Assertions& checks,OriginalTypesetter& typesetter,std::string_view label,HWND target,
        const std::string& source,const std::vector<std::string_view>& spellings,const std::function<void(int)>& resize){
    const auto emitted=typesetter.fragments.find(source);
    require(emitted!=typesetter.fragments.end(),"The current text bypassed the real original worker");
    const auto original=wide(source);
    std::cout<<"CURRENT "<<label<<" utf16="<<original.size()<<" fragments="<<emitted->second.size()<<'\n';
    for(const auto& item:emitted->second)std::cout<<"  RANGE "<<item.start<<':'<<item.end
        <<" source="<<utf8(std::wstring_view(original).substr(item.start,item.end-item.start))
        <<" formula="<<item.formula<<" literal="<<item.literal<<'\n';
    auto projection=native_math_text_mapping(target,source);
    if(!projection){
        // A genuine empty auto layout can leave the original story untouched.
        // Verify that fact before using its actual identity positions; do not
        // invent mathematical ranges when the original detector emitted none.
        const auto story=native_story(target);
        require(story==original,"An unmapped auto layout changed the current source characters");
        projection=NativeMathTextMapping{source,story,std::vector<std::size_t>(original.size()+1),false};
        std::iota(projection->source_to_native.begin(),projection->source_to_native.end(),0);
    }
    require(projection->source_to_native.size()==original.size()+1&&
        std::ranges::is_sorted(projection->source_to_native),"The original UTF-16 source-to-native mapping is invalid");
    const auto story=projection->native_text;
    std::vector<Fraction> fractions;for(const auto spelling:spellings)fractions.push_back(fraction(source,spelling));
    std::vector<int> split_width(fractions.size());std::vector<LONG> top_line(fractions.size()),bottom_line(fractions.size());
    // Real paragraph reflow, including the width where a slash at the end of
    // one line leaves its denominator on the next. No synthetic word breaker.
    for(int width=180;width<=920;++width){
        resize(width);
        for(std::size_t index=0;index<fractions.size();++index){
            if(split_width[index])continue;
            const auto& value=fractions[index];
            const auto top=visual_line(target,projection->source_to_native.at(value.numerator));
            const auto bottom=visual_line(target,projection->source_to_native.at(value.denominator));
            require(top>=0&&bottom>=0,"The mapped fraction characters have no real native lines");
            if(top!=bottom){split_width[index]=width;top_line[index]=top;bottom_line[index]=bottom;}
        }
    }
    for(std::size_t index=0;index<fractions.size();++index){
        const auto& value=fractions[index];
        const auto owner=std::ranges::find_if(emitted->second,[&](const NativeMathFragment& item){
            return !item.literal&&item.start<=value.begin&&item.end>=value.end;
        });
        const auto top_at=projection->source_to_native.at(value.numerator),bottom_at=projection->source_to_native.at(value.denominator);
        const auto top=inline_argument(target,top_at),bottom=inline_argument(target,bottom_at);
        const auto prefix=std::string(label)+" fraction="+value.source;
        std::cout<<"  NATIVE "<<value.source<<" source="<<value.begin<<':'<<value.end<<" numerator="<<top_at
            <<" denominator="<<bottom_at<<" object="<<top.type<<':'<<top.argument<<','<<bottom.type<<':'<<bottom.argument
            <<" owner="<<(owner!=emitted->second.end())<<" split_width="<<split_width[index]
            <<" lines="<<top_line[index]<<','<<bottom_line[index]<<'\n';
        checks.expect(owner!=emitted->second.end(),prefix+" is not wholly owned by an original emitted math unit");
        checks.expect(top.type==tomFraction&&bottom.type==tomFraction&&top.argument==0&&bottom.argument==1,
            prefix+" numerator/denominator are not the two arguments of a real native fraction");
        checks.expect(split_width[index]==0,prefix+" split across real native lines at width "+std::to_string(split_width[index]));
    }
    resize(640);
    checks.expect(native_story(target)==story,std::string(label)+" reflow changed the actual mathematical story");
    const auto mapping=native_math_text_mapping(target,source);
    checks.expect(mapping.has_value(),std::string(label)+" reflow lost the original-source mapping");
    if(mapping){
        checks.expect(mapping->source_to_native==projection->source_to_native,std::string(label)+" reflow changed original-source ownership");
        CHARRANGE selected{1,5};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        ReadingShade shade(target,source);checks.expect(shade.matches(target,source),std::string(label)+" lost its original narration source");
        shade.read_through(original.size());shade.read_through(0);shade.restore();
        CHARRANGE after{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        checks.expect(after.cpMin==selected.cpMin&&after.cpMax==selected.cpMax&&native_story(target)==story&&
            native_math_text_mapping(target,source).has_value(),std::string(label)+" shading changed selection, story or mapping");
    }
}
void current_prompt(Assertions& checks,OriginalTypesetter& typesetter,HWND host,const JsonObject& block,
        std::string_view label,const std::vector<std::string_view>& fractions){
    unsigned changes=0;MathPuzzleCallbacks callbacks;
    callbacks.expression_request=[&](std::string_view raw){return typesetter.request(raw);};callbacks.changed=[&]{++changes;};
    MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","",callbacks);view.layout(0,0,640);
    const auto source=winrt::to_string(block.GetNamedString(L"prompt"));HWND prompt=nullptr;
    for(const auto& region:view.reading_regions())if(region.source_text==source){require(!prompt,"Current prompt is ambiguous");prompt=region.target;}
    require(prompt!=nullptr,"The current curricular prompt has no real readable control");
    const auto answer=view.answers_json(),identity=view.answer_identity_json();
    inspect_current_text(checks,typesetter,label,prompt,source,fractions,[&](int width){view.layout(0,0,width);});
    checks.expect(changes==0&&view.answers_json()==answer&&view.answer_identity_json()==identity,
        std::string(label)+" presentation changed an untouched answer or its identity");
}
void current_feedback(Assertions& checks,OriginalTypesetter& typesetter,HWND host,const JsonObject& block,
        std::string_view label,const std::vector<std::string_view>& fractions){
    // This text is authored feedback today, not the historic prompt. Mount it
    // independently through the real lesson prose adapter without answering a
    // puzzle or granting a fabricated achievement merely to expose a label.
    const auto source=winrt::to_string(block.GetNamedObject(L"feedback").GetNamedString(L"correct"));
    Edit edit(host);(void)set_original_math_text(edit.window,source,"auto",[&](std::string_view raw){return typesetter.request(raw);});
    inspect_current_text(checks,typesetter,label,edit.window,source,fractions,[&](int width){
        SetWindowPos(edit.window,nullptr,0,0,width,1000,SWP_NOZORDER|SWP_NOACTIVATE);
    });
}
}

int main(){
    std::cout<<std::unitbuf;
    try{
        windows::ensure_winrt_apartment();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        const auto rich=LoadLibraryW(L"Msftedit.dll");require(rich!=nullptr,"Cannot load the Windows system RichEdit");
        Host host;require(host.window!=nullptr,"Cannot create the hidden curricular QA host");
        OriginalTypesetter typesetter;Assertions checks;
        Asset recovery("math_algebra_elemental/recuperacion_y_transferencia/lesson_elegir_relaciones_y_recuperar_el_razonamiento_algebraico.json");
        Asset systems("math_algebra_elemental/sistemas_lineales/lesson_formulas_con_datos_y_una_condicion_no_nula.json");
        Asset segments("math_geometria_plana/fundamentos_geometricos/lesson_comparar_segmentos_y_elegir_una_unidad.json");
        current_prompt(checks,typesetter,host.window,recovery.block("dos_exigencias_y_dos_dominios",4,2),
            "dos_exigencias_y_dos_dominios/current-prompt-p5b3",{"5/2"});
        current_prompt(checks,typesetter,host.window,systems.block("resolver_con_datos_fraccionarios",4,4),
            "resolver_con_datos_fraccionarios/current-prompt-p5b5",{"x/2","2y/3","−3x/4","y/5","21/10"});
        current_feedback(checks,typesetter,host.window,systems.block("construir_el_criterio_con_datos",1,3),
            "construir_el_criterio_con_datos/current-correct-feedback-p2b4",{"(du − bv)/D","(av − cu)/D"});
        current_feedback(checks,typesetter,host.window,segments.block("comparar_longitudes_con_dos_referencias",6,2),
            "comparar_longitudes_con_dos_referencias/current-explicit-correct-feedback-p7b3",{"3/4","9/4"});
        recovery.unchanged();systems.unchanged();segments.unchanged();
        require(checks.failures==0,"Current curricular fractions failed original-worker ownership, actual TOM fraction arguments or native line integrity; see diagnostics above");
        std::cout<<"PASS current curricular fraction prompts and feedback: original worker ranges, real TOM arguments, UTF-16 projection, native reflow, narration, selection, untouched answers and asset hashes\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

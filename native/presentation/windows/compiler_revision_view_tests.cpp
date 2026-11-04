#include "core/compiler_revision_test_fixture.hpp"
#include "core/attempts.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& search=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=search.id)return TRUE;search.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));return search.found;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,const state::StateData& initial):environment(resolve_windows_environment({ProfileId::Codex,
        L"compiler-revision-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"Compiler revision QA must own a new profile");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Hidden compiler revision QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the hidden compiler revision host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create a compiler revision lesson");
        MoveWindow(view,0,0,1100,850,TRUE);pump();check(!IsWindowVisible(host),"Compiler revision QA exposed the desktop");}
    void reopen(){check(flush_lesson_view(view),"Cannot save compiler revision QA");DestroyWindow(view);view=nullptr;open();}
    void restore(const state::StateData& next){
        check(flush_lesson_view(view),"Cannot close the prior QA state");DestroyWindow(view);view=nullptr;
        state::StateStore store(environment.paths.root);(void)store.load();store.save(next);open();
    }
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush compiler revision QA");return read_lesson_state(view);}
    HWND control(std::size_t block,int part){const auto item=descendant(view,control_id(block,part));check(item!=nullptr,"Missing compiler revision control");return item;}
    std::string text(std::size_t block,int part){return edit_text(control(block,part));}
    void reset(std::size_t block){SendMessageW(control(block,12),BM_CLICK,0,0);pump();}
};
windows::CppRunRequest program(const curriculum::Block& block,std::string source){
    windows::CppRunRequest request;request.revision=1;request.source_utf8=std::move(source);
    request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;return request;
}
void seed(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
          const windows::CppRunRequest& request,std::size_t count,std::string_view feedback){
    // These synthetic receipts isolate UI/state adoption. Real reference and
    // counterexample programs are compiled separately; no execution is claimed here.
    for(std::size_t index=0;index<std::min(count,block.output_check->cases.size());++index){const auto& item=block.output_check->cases[index];
        curriculum::FreshExecution execution{request.revision,true,true,0,item.standard_input,item.outputs.front()};
        execution.executable=item.executable;
        if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA runtime diagnostic";
        (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,feedback);
    }
}
struct ClipboardGuard {
    winrt::com_ptr<IDataObject> previous;
    struct Saved {FORMATETC format{};std::vector<std::uint8_t> bytes;};
    std::vector<Saved> formats;
    bool restored=false;
    ClipboardGuard(){
        // OleGetClipboard returns a live proxy, not an independent snapshot.
        // Materialize every advertised format before any copy action. Refuse
        // unsupported storage before mutation rather than dropping user data.
        winrt::com_ptr<IDataObject> source;
        check(SUCCEEDED(OleGetClipboard(source.put())),"Cannot inspect the clipboard before copy-control QA");
        check(SUCCEEDED(SHCreateDataObject(nullptr,0,nullptr,nullptr,__uuidof(IDataObject),previous.put_void())),"Cannot allocate an independent clipboard snapshot");
        winrt::com_ptr<IEnumFORMATETC> enumerator;check(SUCCEEDED(source->EnumFormatEtc(DATADIR_GET,enumerator.put())),"Cannot enumerate the original clipboard");
        FORMATETC format{};ULONG fetched=0;HRESULT next{};
        while((next=enumerator->Next(1,&format,&fetched))==S_OK){
            struct FormatCleanup{FORMATETC& value;~FormatCleanup(){CoTaskMemFree(value.ptd);}} cleanup{format};
            check(!format.ptd,"Copy-control QA cannot snapshot a device-specific clipboard without changing it");
            struct Medium{STGMEDIUM value{};~Medium(){if(value.tymed)ReleaseStgMedium(&value);}} original,copy;
            check(SUCCEEDED(source->GetData(&format,&original.value)),"Cannot materialize an original clipboard format before QA");
            Saved saved;saved.format=format;saved.format.tymed=original.value.tymed;
            copy.value.tymed=original.value.tymed;
            if(original.value.tymed==TYMED_HGLOBAL){
                const auto size=GlobalSize(original.value.hGlobal);
                {const auto from=GlobalLock(original.value.hGlobal);
                    struct Unlock{HGLOBAL memory;bool locked;~Unlock(){if(locked)GlobalUnlock(memory);}} unlock{original.value.hGlobal,from!=nullptr};
                    check(from||!size,"Cannot read a clipboard snapshot without modifying the clipboard");
                    if(size)saved.bytes.assign(static_cast<const std::uint8_t*>(from),static_cast<const std::uint8_t*>(from)+size);}
                copy.value.hGlobal=GlobalAlloc(GMEM_MOVEABLE,size);check(copy.value.hGlobal!=nullptr,"Cannot allocate clipboard snapshot data");
                if(size){const auto to=GlobalLock(copy.value.hGlobal);check(to!=nullptr,"Cannot lock clipboard snapshot data");
                    std::memcpy(to,saved.bytes.data(),size);GlobalUnlock(copy.value.hGlobal);}
            }else if(original.value.tymed==TYMED_GDI||original.value.tymed==TYMED_MFPICT){
                copy.value.hGlobal=static_cast<HGLOBAL>(OleDuplicateData(original.value.hGlobal,format.cfFormat,0));
                check(copy.value.hGlobal!=nullptr,"Cannot duplicate a graphical clipboard format before QA");
            }else if(original.value.tymed==TYMED_ENHMF){
                copy.value.hEnhMetaFile=CopyEnhMetaFileW(original.value.hEnhMetaFile,nullptr);
                check(copy.value.hEnhMetaFile!=nullptr,"Cannot duplicate a clipboard metafile before QA");
            }else {copy.value.tymed=TYMED_NULL;throw std::runtime_error("Copy-control QA leaves stream/file/storage clipboard formats untouched; no copy action was performed");}
            check(SUCCEEDED(previous->SetData(&saved.format,&copy.value,TRUE)),"Cannot retain an independent clipboard format before QA");
            copy.value={};formats.push_back(std::move(saved));format={};
        }
        check(next==S_FALSE,"Cannot finish snapshotting the original clipboard before QA");
        if(formats.empty())previous=nullptr;
        else {
            winrt::com_ptr<IEnumFORMATETC> copied;check(SUCCEEDED(previous->EnumFormatEtc(DATADIR_GET,copied.put())),"Cannot verify the independent clipboard snapshot before QA");
            std::vector<bool> seen(formats.size());FORMATETC candidate{};ULONG count=0;HRESULT status{};
            while((status=copied->Next(1,&candidate,&count))==S_OK){
                const bool device=candidate.ptd!=nullptr;CoTaskMemFree(candidate.ptd);candidate.ptd=nullptr;
                std::size_t match=0;for(;match<formats.size();++match){const auto& expected=formats[match].format;
                    if(!seen[match]&&!device&&candidate.cfFormat==expected.cfFormat&&candidate.dwAspect==expected.dwAspect
                        &&candidate.lindex==expected.lindex&&candidate.tymed==expected.tymed)break;}
                check(match<formats.size(),"Snapshot introduced or changed a clipboard format before QA");seen[match]=true;
            }
            check(status==S_FALSE&&std::ranges::all_of(seen,[](bool value){return value;}),"Snapshot omitted an original clipboard format before QA");
        }
    }
    ~ClipboardGuard(){if(!restored){(void)OleSetClipboard(previous.get());if(previous)(void)OleFlushClipboard();}}
    void restore(){
        check(SUCCEEDED(OleSetClipboard(previous.get())),"Cannot restore the original clipboard after copy-control QA");
        if(previous)check(SUCCEEDED(OleFlushClipboard()),"Cannot persist the restored clipboard after copy-control QA");
        restored=true;
        if(formats.empty()){check(CountClipboardFormats()==0,"An originally empty clipboard acquired QA text");return;}
        winrt::com_ptr<IDataObject> actual;check(SUCCEEDED(OleGetClipboard(actual.put())),"Cannot verify the restored clipboard");
        for(auto& saved:formats){
            STGMEDIUM medium{};check(SUCCEEDED(actual->GetData(&saved.format,&medium)),"A restored original clipboard format is missing");
            bool same=true;if(saved.format.tymed==TYMED_HGLOBAL){const auto size=GlobalSize(medium.hGlobal);const auto from=GlobalLock(medium.hGlobal);
                same=size==saved.bytes.size()&&(!size||(from&&std::memcmp(from,saved.bytes.data(),size)==0));if(from)GlobalUnlock(medium.hGlobal);}
            ReleaseStgMedium(&medium);check(same,"Restoring the clipboard changed original format bytes");
        }
    }
};
std::string clipboard_text(HWND owner){
    check(OpenClipboard(owner)!=FALSE,"Cannot inspect the additional-case copy result");
    const auto memory=GetClipboardData(CF_UNICODETEXT);
    const auto locked=memory?static_cast<const wchar_t*>(GlobalLock(memory)):nullptr;
    if(!locked){CloseClipboard();throw std::runtime_error("The additional-case copy control did not provide Unicode text");}
    const auto result=winrt::to_string(winrt::hstring(locked));GlobalUnlock(memory);CloseClipboard();return result;
}
void starter_revision_keeps_existing_program(){
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json";
    const auto bytes=state::read_bytes(path);const auto active=windows::load_lesson(path);
    // Both compiler revisions coexist; the first editor already has its own
    // expanded contract and must survive adoption/Reset of the second.
    const auto original=test::revise_compiler_lesson(test::compiler_revision_before(
        test::compiler_revision_before(active,"block_0006_0003"),"block_0002_0002"),"block_0002_0002");
    const auto revised=test::revise_compiler_lesson(original,"block_0006_0003");
    const auto at=curriculum::stable_block_position(revised,"block_0006_0003"),first_at=curriculum::stable_block_position(revised,"block_0002_0002");
    check(at&&first_at,"The starter revision lost one of its two independent editors");
    const auto& old=original.pages[at->page].blocks[at->block];const auto& current=revised.pages[at->page].blocks[at->block];
    const auto& first=revised.pages[first_at->page].blocks[first_at->block];
    const std::string insertion="    std::cout << \"Caso 5\\n\";\n    probar(\"2;2;\");\n";
    check(current.compiler_revision&&current.compiler_revision->previous_initial_code==old.initial_code
        &&current.compiler_revision->starter_append==insertion&&current.initial_code!=old.initial_code,
        "The native starter proposal lost its exact previous initial code or reviewed added test call");
    ClipboardGuard clipboard;
    for(const std::string mode:{"empty","blank","draft","complete"}){
        state::StateData data;state::prepare_lesson_state(data,original);
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["starter_revision_opaque"]["untouched"]=std::string(" \t untouched metadata\r\n");
        const auto first_source=first.initial_code+"\n// First editor personal draft retained\n";
        seed(data,original,first,program(first,first_source),first.output_check->cases.size(),"Synthetic prerequisite, no execution");
        const auto first_key=curriculum::draft_key("programming",original,first);
        state::Draft first_draft;first_draft.source=first_source;first_draft.input="first input\t";state::put_draft(data,first_key,first_draft);
        const bool empty=mode=="empty",complete=mode=="complete";
        const auto source=empty?current.initial_code:mode=="blank"?std::string{}:old.initial_code+"\n// Personal query under construction\n";
        const auto key=curriculum::draft_key("programming",original,old);
        state::Draft draft;draft.source=source;draft.input=" \t personal prepared input\n";
        if(!empty)state::put_draft(data,key,draft);
        if(complete)seed(data,original,old,program(old,source),1,"Synthetic old four-report aggregate, no execution");
        const auto history=narration::begin({},"starter-storage-only","starter-storage-only",{0,137,0,false},"opaque source","other-page");
        narration::write_history(data,history);state::save_current_page(data,original,at->page);
        const auto available=attempts::balance(data,"programming",now_ms()).available;
        View fixture(revised,data);
        const auto verify=[&]{const auto saved=fixture.saved();
            check(lesson_page_state(fixture.view).index==at->page,"Starter adoption changed its saved page");
            check(fixture.text(at->block,10)==source,"The starter revision replaced an empty or personal saved document");
            const auto retained=state::get_draft(saved,key,current.initial_code,current.standard_input.value_or(""));
            check(retained.source==source&&retained.input==(empty?current.standard_input.value_or(""):draft.input),"The starter revision rewrote saved source/stdin");
            check(windows::compiler_puzzle_completed(saved,original,old)==complete
                &&windows::compiler_puzzle_historical_completed(saved,revised,current)==complete
                &&windows::compiler_puzzle_historical_case_count(saved,revised,current)==(complete?1:0)
                &&!windows::compiler_puzzle_completed(saved,revised,current)&&windows::compiler_puzzle_case_count(saved,revised,current)==0,
                "The starter revision lost the old aggregate or converted four printed reports into current credit");
            check(windows::compiler_puzzle_completed(saved,revised,first)&&state::get_draft(saved,first_key,"").source==first_source,
                "The second editor changed the first editor's draft or independent objective");
            check(attempts::balance(saved,"programming",now_ms()).available==available&&narration::read_history(saved)==history
                &&saved.preferences.at("starter_revision_opaque")==data.preferences.at("starter_revision_opaque"),
                "Starter adoption changed attempts, stored listening history or unrelated settings");
            check(fixture.text(at->block,42)==insertion&&(GetWindowLongPtrW(fixture.control(at->block,42),GWL_STYLE)&ES_READONLY),
                "The exact additional test case was hidden, editable or replaced by a solution");
            bool compiler_gate=false;
            for(const auto& region:lesson_reading_regions(fixture.view)){
                check(region.target!=fixture.control(at->block,42),"The lesson reader began narrating the additional code body");
                if(region.block_index==at->block&&region.required_compiler){compiler_gate=true;
                    check(!region.solved,"The reader treated the historical four-report success as the current five-report objective");}
            }
            check(compiler_gate,"The starter expansion removed the required compiler's reading gate");
        };
        verify();fixture.reopen();verify();
        const auto editor=fixture.control(at->block,10);CHARRANGE selection{0,static_cast<LONG>(std::min<std::size_t>(source.size(),8))};
        SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        const auto before_copy=fixture.saved();
        SendMessageW(fixture.control(at->block,43),BM_CLICK,0,0);pump();
        check(clipboard_text(fixture.host)==insertion,"Copy additional case did not copy exactly the reviewed call and header");
        CHARRANGE after{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        check(after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&fixture.saved()==before_copy,
            "Copy additional case changed the editor selection, document, attempts or progress");
        if(empty){
            // Also prove the preservation path with a populated clipboard:
            // copying the whole starter must not invalidate the saved snippet.
            ClipboardGuard copied_case;SendMessageW(fixture.control(at->block,18),BM_CLICK,0,0);pump();
            check(clipboard_text(fixture.host)==source,"The existing source-copy control did not copy its own starter");
            copied_case.restore();check(clipboard_text(fixture.host)==insertion,"Restoring a populated clipboard lost the independent copied-case snapshot");
        }
        fixture.reopen();verify();
        fixture.reset(at->block);fixture.reopen();const auto reset=fixture.saved();
        check(fixture.text(at->block,10)==current.initial_code&&fixture.text(at->block,42)==insertion
            &&!windows::compiler_puzzle_completed(reset,revised,current)&&!windows::compiler_puzzle_historical_case_count(reset,revised,current),
            "Explicit Reset failed to restore the five-call starter or resurrected its old achievement");
        check(windows::compiler_puzzle_completed(reset,revised,first)&&state::get_draft(reset,first_key,"").source==first_source
            &&narration::read_history(reset)==history&&attempts::balance(reset,"programming",now_ms()).available==available,
            "Reset of the second editor changed the first editor, stored history or available attempts");
    }
    clipboard.restore();check(state::read_bytes(path)==bytes,"Starter revision UI QA modified the authored curriculum");
    std::cout<<"PASS exact second U11L15 starter: fresh five-call code, saved blank/personal/old-success code, one historical aggregate, copy without document/selection/state changes, independent first editor, reader history, Reset/reopen; synthetic receipts, no execution/listening claim\n";
}
void compiler_revisions_keep_prior_progress(bool late){
    struct Plan{const char* filename;const char* identity;bool append;bool late=false;};
    const Plan plans[]{
        {"lesson_construir_un_lector_de_entrada_preparada.json","block_0003_0005",true},
        {"lesson_tokens_con_categoria_y_valor.json","block_0004_0004",false},
        {"lesson_numeros_de_varios_digitos_y_limites.json","block_0004_0003",false},
        {"lesson_una_gramatica_para_sumar_y_restar.json","block_0004_0003",false},
        {"lesson_terminos_prioridad_y_division_segura.json","block_0004_0003",false},
        {"lesson_signos_unarios_parentesis_y_progreso_recursivo.json","block_0004_0004",false},
        {"lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json","block_0004_0004",false},
        {"lesson_construir_un_interprete_con_historial_y_pruebas.json","block_0002_0002",false,true},
        {"lesson_resto_y_asociacion_en_el_interprete.json","block_0003_0004",false,true}};
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    std::size_t scenarios=0;
    for(const auto& plan:plans){if(plan.late!=late)continue;
        const auto path=folder/plan.filename;const auto original_hash=state::sha256(state::read_bytes(path));
        const auto integrated=windows::load_lesson(path);
        const auto original=test::compiler_revision_before(integrated,plan.identity);
        const auto revised=test::revise_compiler_lesson(original,plan.identity);
        const auto position=curriculum::stable_block_position(original,plan.identity);check(position.has_value(),"The reviewed compiler identity is missing");
        const auto& old=original.pages[position->page].blocks[position->block];const auto& current=revised.pages[position->page].blocks[position->block];
        for(const std::string mode:{"empty","draft","partial","complete"}){
            if(mode=="draft"&&!late)continue;
            if(mode=="partial"&&old.output_check->cases.size()==1)continue;
            state::StateData data;state::prepare_lesson_state(data,original);
            data.preferences["native_windows"]["ui_language"]=std::string("es");
            data.preferences["independent_revision_preferences"]["untouched"]=std::string(" \t original metadata\r\n");
            for(const auto& page:original.pages)for(const auto& block:page.blocks)
                if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check&&block.stable_id!=old.stable_id)
                    seed(data,original,block,program(block,block.initial_code),block.output_check->cases.size(),"QA prerequisite receipt");
            const auto pristine=late&&mode=="empty";
            const auto saved_source=old.initial_code+(pristine?"":"\n// Programa personal QA conservado\n");
            const auto captured=program(old,saved_source);
            const auto old_count=(mode=="empty"||mode=="draft")?0:mode=="partial"?std::max<std::size_t>(1,old.output_check->cases.size()/2):old.output_check->cases.size();
            seed(data,original,old,captured,old_count,"Correcto\nComprobación anterior QA conservada.");
            const auto old_complete=old_count==old.output_check->cases.size();
            state::Draft draft;draft.source=saved_source;draft.input=" \t dato personal\n";draft.selection_start=4;draft.selection_end=7;
            const auto draft_key=curriculum::draft_key("programming",original,old);
            if(!pristine)state::put_draft(data,draft_key,draft);
            // Storage-only bookmarks: adopting a contract cannot manufacture
            // heard time. No synthesis or human listening is claimed here.
            const auto history=narration::begin({},"compiler-revision-storage-only","compiler-revision-storage-only",
                {0,137,0,false},"opaque source","other-page");
            narration::write_history(data,history);
            state::save_current_page(data,original,position->page);
            const auto original_preferences=data.preferences["independent_revision_preferences"];
            const auto available=attempts::balance(data,"programming",now_ms()).available;
            View fixture(revised,data);check(lesson_page_state(fixture.view).index==position->page,"Adoption moved the requested compiler bookmark");
            const auto verify=[&]{const auto saved=fixture.saved();
                check(!windows::compiler_puzzle_completed(saved,revised,current),"An old achievement silently completed the expanded objective");
                check(windows::compiler_puzzle_completed(saved,original,old)==old_complete,"Adoption rewrote the historical achievement");
                check(windows::compiler_puzzle_historical_case_count(saved,revised,current)==old_count,"The previous checks lost their provenance");
                check(windows::compiler_puzzle_case_count(saved,revised,current)==(plan.append?old_count:0),"The wrong checks were credited to the expanded objective");
                const auto retained=state::get_draft(saved,draft_key,current.initial_code,current.standard_input.value_or(""));
                check(retained.source==saved_source&&retained.input==(pristine?current.standard_input.value_or(""):draft.input),"Adoption lost personal source or prepared input");
                check(fixture.text(position->block,10)==saved_source,"The actual editor replaced the original personal program");
                check(narration::read_history(saved)==history,"Compiler adoption rewrote a stored narration bookmark");
                check(saved.preferences.at("independent_revision_preferences")==original_preferences,"Compiler adoption changed an unrelated preference");
                check(attempts::balance(saved,"programming",now_ms()).available==available,"Compiler adoption consumed an attempt");
                const auto summary=fixture.text(position->block,40);
                check(summary.empty()==(old_count==0),"The view lost or invented historical checks");
                check(fixture.text(position->block,4).empty(),"An old verdict was presented as the current objective verdict");
                if(old_count){check(summary.find("ampliación")!=summary.npos,"Historical credit lacks its pending expansion label");
                    bool found=false;for(const auto& region:lesson_reading_regions(fixture.view))if(region.source_text==summary){
                        found=true;check(!region.solved&&region.required_compiler,"Historical narration unlocked the current compiler");}
                    check(found,"The reader omitted the retained historical compiler achievement");}
            };
            verify();fixture.reopen();verify();
            auto fresh=fixture.saved();
            seed(fresh,revised,current,program(current,saved_source+"// Solución ampliada QA\n"),current.output_check->cases.size(),"Correcto\nNueva batería QA completa.");
            fixture.restore(fresh);const auto completed=fixture.saved();
            check(windows::compiler_puzzle_completed(completed,revised,current),"New checks did not complete their own expanded objective");
            check(fixture.text(position->block,40).empty()&&fixture.text(position->block,4).starts_with("✓ Correcto"),"The completed expansion kept a pending label or lost its own verdict");
            check(windows::compiler_puzzle_completed(completed,original,old)==old_complete,"Completing the expansion rewrote old achievement data");
            if(late){
                const auto& revision=test::compiler_revision_case(revised,plan.identity);
                const auto reference=curriculum::stable_block_position(revised,revision.reference_identity);
                check(reference.has_value(),"The late expansion lost its paired reference");
                // Mount the paired page from an isolated saved bookmark. Other
                // curricular puzzles remain unanswered; this is not a claim of
                // having completed the intervening lesson navigation.
                auto reference_state=completed;state::save_current_page(reference_state,revised,reference->page);
                fixture.restore(reference_state);
                check(fixture.text(reference->block,1)==revised.pages[reference->page].blocks[reference->block].code,
                    "The native reference control mixed compiler revisions");
                bool found=false;for(const auto& region:lesson_reading_regions(fixture.view))if(region.block_index==reference->block){
                    check(region.target!=fixture.control(reference->block,1),"The lesson reader started narrating the reference program body");
                    if(region.passive_label&&region.source_text=="Ejemplo de código C más más.")found=true;
                }
                check(found,"The reader lost the paired reference's passive language caption");
                fixture.reopen();check(fixture.text(reference->block,1)==revised.pages[reference->page].blocks[reference->block].code,
                    "Reopening lost the current expanded reference");
                check(navigate_lesson_view(fixture.view,position->page),"Cannot return to the expanded compiler for Reset");pump();
            }
            fixture.reset(position->block);fixture.reopen();const auto reset=fixture.saved();
            check(!windows::compiler_puzzle_completed(reset,revised,current)&&!windows::compiler_puzzle_completed(reset,original,old)
                &&windows::compiler_puzzle_case_count(reset,revised,current)==0&&fixture.text(position->block,40).empty(),
                "Explicit reset resurrected historical checks on reopening");
            check(state::get_draft(reset,draft_key,"").source==current.initial_code,"Reset did not restore the original exercise source");
            check(reset.preferences.at("independent_revision_preferences")==original_preferences,"Reset changed unrelated preferences");++scenarios;
        }
        check(state::sha256(state::read_bytes(path))==original_hash,"A compiler revision UI regression changed an authored asset");
    }
    std::cout<<"PASS "<<scenarios<<" hidden real compiler views: "<<(late?"U11L15/U11L16 exact proposals":"seven earlier exact expansions")
        <<", prior empty/draft/partial/complete states, same-program projection, current gate, original drafts/preferences, stored reader history, completion, Reset and reopen (synthetic QA receipts, no compilation or listening claimed)\n";
}
}
int main(int argc,char** argv){const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{check(argc==1||(argc==2&&(std::string_view(argv[1])=="--u11-late"||std::string_view(argv[1])=="--u11-starter")),"Unknown compiler revision view selector");
        if(argc==2&&std::string_view(argv[1])=="--u11-starter")starter_revision_keeps_existing_program();
        else compiler_revisions_keep_prior_progress(argc==2);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}

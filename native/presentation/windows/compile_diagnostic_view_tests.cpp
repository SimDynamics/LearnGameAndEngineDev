#include "core/compile_check_test_fixture.hpp"
#include "core/attempts.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/compiler_diagnostic_display.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,std::string_view text){if(!value)throw std::runtime_error(std::string(text));}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
std::string window_text(HWND window){const auto size=GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(size)+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));
    return utf8(value);
}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& search=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=search.id)return TRUE;search.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));return search.found;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::wstring wide(std::string_view value){return winrt::to_hstring(value).c_str();}
struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,const state::StateData& initial):environment(resolve_windows_environment({ProfileId::Codex,
        L"compile-diagnostic-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"Diagnostic UI QA requires a new private profile");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden diagnostic objective QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create hidden diagnostic host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create diagnostic lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);pump();check(!IsWindowVisible(host),"Diagnostic UI QA exposed a desktop window");}
    void reopen(){check(flush_lesson_view(view),"Cannot save diagnostic QA");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush diagnostic QA");return read_lesson_state(view);}
    HWND control(std::size_t block,int part){const auto item=descendant(view,control_id(block,part));check(item!=nullptr,"Missing diagnostic editor control");return item;}
    std::string text(std::size_t block,int part){return edit_text(control(block,part));}
    void select(std::size_t block,LONG start,LONG end){CHARRANGE selection{start,end};SendMessageW(control(block,10),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump();}
    void replace(std::size_t block,std::string_view source){const auto edit=control(block,10);check(IsWindowEnabled(edit),"The diagnostic source is unexpectedly locked");
        CHARRANGE range{0,-1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));const auto value=wide(source);
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));pump();check(text(block,10)==source,"Editing changed supplied diagnostic source bytes");}
    void run(std::size_t block){const auto button=control(block,11);check(IsWindowEnabled(button),"Diagnostic compilation was already busy");
        check(window_text(button)=="Compilar y comprobar diagnóstico","Diagnostic goal uses an execution action label");
        SendMessageW(button,BM_CLICK,0,0);pump();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(125);
        while(!IsWindowEnabled(button)&&std::chrono::steady_clock::now()<deadline){MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);pump();}
        check(IsWindowEnabled(button),"Fresh diagnostic compilation did not finish");
        check(text(block,34).find("El programa no se ejecutó.")!=std::string::npos,"Diagnostic status lost its compilation-only outcome");
        check(!IsWindowVisible(host),"A compile operation activated the isolated QA host");
    }
    void reset(std::size_t block){SendMessageW(control(block,12),BM_CLICK,0,0);pump();}
};
void seed_other_objectives(state::StateData& data,const curriculum::Lesson& lesson){
    // These synthetic stdout receipts and prior quiz answers ONLY isolate the
    // diagnostic view. Every target diagnostic below must use real BM_CLICK
    // and the production compiler; no synthetic diagnostic proof is recorded.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            for(const auto& item:block.output_check->cases){curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
                execution.executable=item.executable;if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA runtime diagnostic";
                (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"QA prerequisite stdout proof");}
        }
        if(block.type==curriculum::BlockType::multiple_choice||block.type==curriculum::BlockType::fill_code||block.type==curriculum::BlockType::output_prediction){
            state::Response answer;answer.correct=true;
            if(block.correct_index)answer.selected=static_cast<std::int64_t>(*block.correct_index);
            if(!block.answers.empty())answer.answer=block.answers.front();
            state::put_response(data,lesson,block,answer);
        }
    }
}
void reader_goal(HWND view,std::size_t index,bool solved){bool found=false;
    for(const auto& region:lesson_reading_regions(view))if(region.block_index==index&&!region.feedback&&region.is_exercise){
        found=true;check(region.required_compiler&&region.solved==solved,"Diagnostic reader gate has the wrong required/solved state");}
    check(found,"The reader omitted the diagnostic objective");
}
void explanation_region(HWND view,std::size_t index,std::string_view explanation,bool expected){bool found=false;
    for(const auto& region:lesson_reading_regions(view))if(region.source_text.find(explanation)!=std::string::npos){
        found=true;check(region.block_index==index&&region.feedback&&region.correct&&region.solved&&region.required_compiler,
            "A diagnostic success explanation entered a non-success reading region");
    }
    check(found==expected,expected?"The reader omitted the correct diagnostic feedback and explanation"
        :"The reader exposed a diagnostic success explanation before the actual rejection was proved");
}
std::string candidate(const curriculum::Block& block,std::string_view statement){auto source=block.initial_code;
    const auto position=source.rfind("    return 0;");check(position!=std::string::npos,"Reviewed diagnostic has no final main return");
    source.insert(position,"    "+std::string(statement)+"\n");return source;
}
void compact_diagnostic_path_cases(){
    const std::array roots{
        std::filesystem::path(L"C:\\QA\\Código & (espacio)\\run-1\\candidate\\project"),
        std::filesystem::path(L"C:\\QA\\Código & (espacio)\\run-1\\control\\project"),
        std::filesystem::path(L"C:\\QA\\Código & (espacio)\\run-1\\candidate"),
        std::filesystem::path(L"C:\\QA\\Código & (espacio)\\run-1\\control"),
        std::filesystem::path(L"C:\\QA\\Código & (espacio)\\run-1"),
        std::filesystem::path(L"C:\\QA\\cmake\\p"),
        std::filesystem::path(L"C:\\QA\\cmake"),
        std::filesystem::path(L"\\\\servidor\\curso\\proyecto"),
        std::filesystem::path(L"\\\\?\\D:\\curso\\proyecto"),
        std::filesystem::path(L"\\\\?\\UNC\\otro_servidor\\curso\\proyecto"),
        std::filesystem::path{}};
    const auto expect=[&](std::wstring_view original,std::wstring_view expected,std::string_view label){
        const auto actual=compact_compiler_diagnostic_paths(original,roots);
        check(actual==expected,label);
    };
    expect(L"C:\\QA\\Código & (espacio)\\run-1\\candidate\\main.cpp(7,12): error C2065: nombre desconocido.\r\n",
        L"main.cpp(7,12): error C2065: nombre desconocido.\r\n","MSVC single-file source/coordinates/message changed");
    expect(L"  C:/QA/CÓDIGO & (ESPACIO)/RUN-1/candidate/project/src/main.cpp(18,5) : error C2143: falta ';'\n",
        L"  src/main.cpp(18,5) : error C2143: falta ';'\n","Unicode/case/slash mapping or project-relative path failed");
    expect(L"C:\\QA\\Código & (espacio)\\run-1\\control\\project\\tests\\main.cpp(9): note: aquí\n",
        L"tests\\main.cpp(9): note: aquí\n","The control project or a same-basename file lost its relative folder");
    expect(L"C:\\QA\\Código & (espacio)\\run-1\\control\\main.cpp(1,1): error C1189: control\n",
        L"main.cpp(1,1): error C1189: control\n","The single-file control root was not shortened");
    // A failed native build can put the same source location in the
    // driver's error_utf8 field instead of MSVC stdout/stderr. The caller
    // now uses this exact display transformation for both channels.
    const std::wstring driver_error=
        L"C:\\QA\\Código & (espacio)\\run-1\\candidate\\main.cpp(16,31): error C2059: ')'\r\n"
        L"  El proceso de compilación no pudo continuar.\r\n";
    expect(driver_error,
        L"main.cpp(16,31): error C2059: ')'\r\n"
        L"  El proceso de compilación no pudo continuar.\r\n",
        "The driver exception left a long owned source path or changed its failure reason");

    // cpp_runner::write_exact throws these messages BEFORE it can produce
    // source coordinates. Only the internal-driver mode recognizes them.
    const std::wstring driver_io=
        L"Cannot create run file: C:\\QA\\Código & (espacio)\\run-1\\stdin.bin\r\n"
        L"Cannot write run file: C:/QA/Código & (espacio)/run-1/candidate/project/tmp/diagnostics.log\n";
    const std::wstring compact_io=
        L"Cannot create run file: stdin.bin\r\n"
        L"Cannot write run file: tmp/diagnostics.log\n";
    check(compact_compiler_diagnostic_paths(driver_io,roots)==driver_io,
        "Normal compiler diagnostics must not rewrite driver-only run-file messages");
    check(compact_compiler_diagnostic_paths(driver_io,roots,true)==compact_io,
        "The internal driver kept absolute paths in run-file creation or write errors");
    const std::wstring untouched_driver=
        L"Cannot create run file: D:\\External\\private.txt\r\n"
        L"Cannot write run file: C:\\QA\\Código & (espacio)\\run-1_extra\\stdin.bin\n"
        L"  Cannot create run file: C:\\QA\\Código & (espacio)\\run-1\\source-example.txt\n"
        L"Program output: Cannot create run file: C:\\QA\\Código & (espacio)\\run-1\\text.txt\n"
        L"Cannot open another file: C:\\QA\\Código & (espacio)\\run-1\\unknown.bin";
    check(compact_compiler_diagnostic_paths(untouched_driver,roots,true)==untouched_driver,
        "An external path, sibling directory, source excerpt or unknown driver message changed");
    const std::wstring unc_driver=
        L"Cannot write run file: \\\\servidor\\curso\\proyecto\\logs\\salida.bin\n";
    check(compact_compiler_diagnostic_paths(unc_driver,roots,true)==
        L"Cannot write run file: logs\\salida.bin\n",
        "A recognized UNC run-file exception retained its absolute private path");
    check(driver_io.find(L"C:\\QA\\Código & (espacio)")!=std::wstring::npos,
        "Display-only path compaction mutated the original driver's error text");

    // A location without source coordinates may be followed by CRLF, LF,
    // standalone CR, or no delimiter. Compact only its owned path.
    const std::wstring no_coordinate_endings =
        L"Note: including file: C:\\QA\\Código & (espacio)\\run-1\\candidate\\project\\include\\uno.hpp\r\n"
        L"Nota: inclusión del archivo: C:/QA/Código & (espacio)/run-1/control/project/include/dos.hpp\n"
        L"Cannot create run file: C:\\QA\\Código & (espacio)\\run-1\\stdin.bin\r\n"
        L"Cannot write run file: C:\\QA\\Código & (espacio)\\run-1\\control\\error.log\r";
    const std::wstring compact_no_coordinate_endings =
        L"Note: including file: include\\uno.hpp\r\n"
        L"Nota: inclusión del archivo: include/dos.hpp\n"
        L"Cannot create run file: stdin.bin\r\n"
        L"Cannot write run file: error.log\r";
    check(compact_compiler_diagnostic_paths(no_coordinate_endings,roots,true) ==
            compact_no_coordinate_endings,
        "Compacting no-coordinate paths changed CRLF, LF or a final bare CR");
    check(compact_compiler_diagnostic_paths(no_coordinate_endings,roots) ==
        std::wstring(
            L"Note: including file: include\\uno.hpp\r\n"
            L"Nota: inclusión del archivo: include/dos.hpp\n"
            L"Cannot create run file: C:\\QA\\Código & (espacio)\\run-1\\stdin.bin\r\n"
            L"Cannot write run file: C:\\QA\\Código & (espacio)\\run-1\\control\\error.log\r"),
        "Normal diagnostics compacted a driver-only path or lost a line ending");
    expect(L"Note: including file: C:\\QA\\Código & (espacio)\\run-1\\candidate\\main.hpp\r",
        L"Note: including file: main.hpp\r",
        "A final standalone CR was consumed along with an include path");

    expect(L"C:/QA/Código & (espacio)/run-1/main.cpp:4:9: error: expected ';'\r\n",
        L"main.cpp:4:9: error: expected ';'\r\n","Clang coordinates or CRLF changed");
    expect(L"In file included from C:/QA/Código & (espacio)/run-1/candidate/project/include/uno.hpp:3:\n"
        L"                 from C:/QA/Código & (espacio)/run-1/candidate/project/include/dos.hpp:8,\n",
        L"In file included from include/uno.hpp:3:\n"
        L"                 from include/dos.hpp:8,\n","The include stack lost its message or relative headers");
    // /showIncludes does NOT include a line:column suffix. Preserve all
    // nesting whitespace and line endings while shortening only known roots.
    expect(L"Note: including file: C:\\QA\\Código & (espacio)\\run-1\\candidate\\project\\include\\uno.hpp\r\n"
           L"  Note: including file:  C:/QA/Código & (espacio)/run-1/candidate/project/include/dos.hpp\n",
        L"Note: including file: include\\uno.hpp\r\n"
        L"  Note: including file:  include/dos.hpp\n",
        "MSVC /showIncludes did not preserve nested indentation and known relative include paths");

    expect(L"Nota: inclusión del archivo: C:\\QA\\Código & (espacio)\\run-1\\candidate\\project\\include\\primero.hpp\r\n"
           L"    Nota: inclusión del archivo:  \\\\?\\C:\\QA\\Código & (espacio)\\run-1\\control\\project\\include\\segundo.hpp\n",
        L"Nota: inclusión del archivo: include\\primero.hpp\r\n"
        L"    Nota: inclusión del archivo:  include\\segundo.hpp\n",
        "Localized Spanish MSVC include traces lost exact prefix, nesting or extended-path ownership");
    expect(L"Note: including file: D:\\SDK\\externo\\stdio.h\r\n"
           L"Note: including file: C:\\QA\\Código & (espacio)\\run-1_extra\\candidate\\project\\x.h\n"
           L"  const char* msg = \"Note: including file: C:\\QA\\Código & (espacio)\\run-1\\candidate\\project\\x.h\";\n",
        L"Note: including file: D:\\SDK\\externo\\stdio.h\r\n"
        L"Note: including file: C:\\QA\\Código & (espacio)\\run-1_extra\\candidate\\project\\x.h\n"
        L"  const char* msg = \"Note: including file: C:\\QA\\Código & (espacio)\\run-1\\candidate\\project\\x.h\";\n",
        "MSVC include trace changed an external directory, sibling folder or source literal");
    expect(L"CMake Error at C:/QA/cmake/p/CMakeLists.txt:14 (add_executable):\r\n  No SOURCES given to target.\r\n",
        L"CMake Error at CMakeLists.txt:14 (add_executable):\r\n  No SOURCES given to target.\r\n",
        "The CMake location or following explanation changed");
    expect(L"CMake Warning (dev) at C:/QA/cmake/p/cmake/opciones.cmake:4 (message):\n"
        L"Call Stack (most recent call first):\n  C:/QA/cmake/p/CMakeLists.txt:9 (include)\n",
        L"CMake Warning (dev) at cmake/opciones.cmake:4 (message):\n"
        L"Call Stack (most recent call first):\n  CMakeLists.txt:9 (include)\n","CMake warning/call-stack locations failed");
    expect(L"C:\\QA\\Código & (espacio)\\run-1\\program.exe : fatal error LNK1120: 1 unresolved externals\r\n",
        L"program.exe : fatal error LNK1120: 1 unresolved externals\r\n","MSVC linker artifact location was not shortened");
    expect(L"C:\\QA\\cmake\\CMakeFiles\\app.dir\\src\\main.cpp.obj : error LNK2019: símbolo ausente\n",
        L"CMakeFiles\\app.dir\\src\\main.cpp.obj : error LNK2019: símbolo ausente\n","CMake build-relative linker artifact lost its folders");
    expect(L"\\\\servidor\\curso\\proyecto\\src\\entrada.cpp(2,3): error C2065: dato\n",
        L"src\\entrada.cpp(2,3): error C2065: dato\n","UNC source roots failed");
    expect(L"\\\\?\\C:\\QA\\Código & (espacio)\\run-1\\candidate\\project\\src\\main.cpp(7,12): error C2065: dato\r\n",
        L"src\\main.cpp(7,12): error C2065: dato\r\n","An extended drive path did not use the most specific owned root");
    expect(L"\\\\?\\uNc\\servidor\\curso\\proyecto\\src\\entrada.cpp:2:3: error: dato\n",
        L"src\\entrada.cpp:2:3: error: dato\n","An extended UNC location was not recognized");
    expect(L"D:/curso/proyecto/src/main.cpp(2): error C2065: dato\n",
        L"src/main.cpp(2): error C2065: dato\n","An extended owned drive root did not match an ordinary diagnostic location");
    expect(L"\\\\otro_servidor\\curso\\proyecto\\main.cpp(2): error C2065: dato\n",
        L"main.cpp(2): error C2065: dato\n","An extended owned UNC root did not match an ordinary diagnostic location");
    std::wstring long_root=L"C:\\QA";
    for(int i=0;i<12;++i)long_root+=L"\\carpeta_de_diagnosticos_largos";
    const std::array long_roots{std::filesystem::path(long_root)};
    const auto long_diagnostic=L"\\\\?\\"+long_root+L"\\src\\main.cpp(3,8): error C2065: dato\r\n";
    const auto long_original=long_diagnostic;
    check(long_root.size()>MAX_PATH&&
        compact_compiler_diagnostic_paths(long_diagnostic,long_roots)==L"src\\main.cpp(3,8): error C2065: dato\r\n"&&
        long_diagnostic==long_original,"A location longer than MAX_PATH was not compacted without changing raw evidence");
    const std::wstring unchanged=
        L"C:/QA/cmake_extra/p/main.cpp:1:2: error: different folder\r\n"
        L"D:/externo/include/main.hpp(5,7): note: archivo externo\r\n"
        L"src/main.cpp:6:2: error: ya era relativo\r\n"
        L"  4 | const char* p = \"C:/QA/cmake/p/main.cpp:1:2: error: literal\";\r\n"
        L"    |                 ^~~~~~~~~~~~~~~~~~~~~\r\n"
        L"  const char* p = \"C:/QA/cmake/p/main.cpp(1,2): texto\";\r\n"
        L"-- Build files have been written to: C:/QA/cmake\r\n"
        L"\\\\?\\C:\\QA\\cmake_extra\\main.cpp(1): error C2065: different folder\r\n"
        L"\\\\.\\C:\\QA\\cmake\\main.cpp(1): error C2065: different namespace\r\n"
        L"  const char* p = \"\\\\?\\C:\\QA\\cmake\\main.cpp(1): literal\";\r\n"
        L"C:/QA/cmake/p/main.cpp: no son coordenadas\r\n";
    expect(unchanged,unchanged,"Unknown roots, source excerpts, carets, literal messages or directory boundaries changed");
    expect(L"C:/QA/cmake/p/carpeta(2,3)/main.cpp(4,5): error C2065: dato",
        L"carpeta(2,3)/main.cpp(4,5): error C2065: dato","Parentheses in a directory were mistaken for source coordinates");
    expect(L"",L"","An empty compiler diagnostic changed");
    const std::wstring raw=L"C:/QA/cmake/p/src/main.cpp:2:3: error: original\n";
    const auto raw_copy=raw;
    (void)compact_compiler_diagnostic_paths(raw,roots);
    check(raw==raw_copy,"The display transformation changed the retained raw diagnostic");
    std::cout<<"PASS compiler diagnostic display: owned relative paths, MSVC/Clang/CMake/linker, control/candidate, Unicode/UNC, exact messages/carets and retained raw text\n";
}
void five_real_diagnostic_views(){
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido";
    std::size_t completed=0;
    for(const auto& plan:test::compile_check_cases){const auto path=folder/plan.filename;const auto asset_hash=state::sha256(state::read_bytes(path));
        // Integration and adoption have separate sources. Today's complete
        // authored explanation is checked independently; the transition still
        // begins with the exact immutable pre-contract source, never a no-op.
        const auto integrated=windows::load_lesson(path);
        const auto integrated_position=curriculum::stable_block_position(integrated,plan.identity);
        check(integrated_position.has_value(),"An integrated diagnostic lost its current owner");
        const auto& authored=integrated.pages[integrated_position->page].blocks[integrated_position->block];
        check(authored.compile_check&&authored.compile_check->diagnostic==plan.diagnostic
            &&!authored.compile_check->explanation.empty()&&!authored.output_check,
            "The current diagnostic contract or its complete authored explanation is missing");
        const auto original=test::load_pre_compile_check_lesson(plan.filename);
        const auto explanation=authored.compile_check->explanation;
        const auto lesson=test::with_compile_check(original,plan.identity,plan.diagnostic,explanation);
        const auto position=curriculum::stable_block_position(lesson,plan.identity);check(position.has_value(),"Reviewed diagnostic owner is missing");
        const auto& target=lesson.pages[position->page].blocks[position->block];
        check(*position==*integrated_position&&target.compile_check==authored.compile_check
            &&test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(target.raw_json)))
                ==test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(authored.raw_json)))
            &&curriculum::exercise_key(lesson,target)==curriculum::exercise_key(integrated,authored)
            &&curriculum::draft_key("programming",lesson,target)==curriculum::draft_key("programming",integrated,authored),
            "Historical adoption does not exactly reconstruct the current diagnostic block, explanation or storage identity");
        state::StateData data;state::prepare_lesson_state(data,original);seed_other_objectives(data,lesson);
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["diagnostic_qa_unrelated"]["keep"]=std::string(" \t preserved preference\r\n");
        state::Response legacy;legacy.correct=true;legacy.answer="Older opaque correctness is not diagnostic evidence";
        state::put_response(data,lesson,target,legacy);
        state::Draft draft;draft.source=target.initial_code;draft.input=" \t previous input draft\n";draft.selection_start=4;draft.selection_end=7;
        const auto draft_key=curriculum::draft_key("programming",lesson,target);state::put_draft(data,draft_key,draft);
        state::save_current_page(data,lesson,position->page);
        const auto untouched=data.preferences.at("diagnostic_qa_unrelated");const auto hearts=attempts::balance(data,"programming",now_ms()).available;
        View fixture(lesson,data);check(lesson_page_state(fixture.view).index==position->page,"Diagnostic restoration moved to another objective");
        const auto retained=[&]{const auto saved=fixture.saved();const auto restored=state::get_draft(saved,draft_key,"");
            check(saved.preferences.at("diagnostic_qa_unrelated")==untouched,"Diagnostic action changed an unrelated preference");
            check(restored.input==draft.input,"Compilation-only operation rewrote the hidden historical input draft");
            check(attempts::balance(saved,"programming",now_ms()).available==hearts,"A diagnostic attempt consumed a heart");
            for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check)
                check(windows::compiler_puzzle_completed(saved,lesson,block),"Diagnostic action reset an independent stdout objective");
        };
        check(fixture.text(position->block,10)==target.initial_code,"Diagnostic adoption changed initial source");
        const auto initial_draft=state::get_draft(fixture.saved(),draft_key,"");
        check(initial_draft.selection_start==4&&initial_draft.selection_end==7,"Diagnostic adoption lost the saved source selection");
        check(!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&!lesson_page_state(fixture.view).can_next,"An old opaque correctness flag approved the diagnostic objective");
        check(descendant(fixture.view,control_id(position->block,15))==nullptr,"Compilation-only activity exposed a standard-input editor");
        check(!navigate_lesson_view(fixture.view,position->page+1),"Forward navigation bypassed a pending diagnostic objective");
        reader_goal(fixture.view,position->block,false);explanation_region(fixture.view,position->block,explanation,false);fixture.run(position->block);
        check(!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&fixture.text(position->block,4).starts_with("✗ Incorrecto"),"A valid unmodified program accredited a requested rejection or lost its retry guidance");
        check(fixture.text(position->block,4).find("aún falta obtener el rechazo solicitado.")!=std::string::npos,"The valid initial compilation lacks its pending-rejection guidance");
        check(fixture.text(position->block,34).starts_with("Compilación aceptada."),"The valid initial diagnostic program did not compile");
        explanation_region(fixture.view,position->block,explanation,false);retained();
        fixture.replace(position->block,"#error QA_UNRELATED_DIAGNOSTIC\n"+target.initial_code);fixture.run(position->block);
        const auto diagnostic=fixture.text(position->block,14);
        check(diagnostic.find("QA_UNRELATED_DIAGNOSTIC")!=std::string::npos,"An unrelated build error lost its real diagnostic");
        check(diagnostic.starts_with("main.cpp(")||diagnostic.find("\nmain.cpp(")!=std::string::npos,
            "The real diagnostic console did not display its source-relative location");
        check(diagnostic.find(utf8(fixture.environment.paths.projects.wstring()))==std::string::npos&&
            diagnostic.find(utf8(fixture.environment.paths.projects.generic_wstring()))==std::string::npos,
            "The real diagnostic console exposed the private absolute run path");
        check(!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&fixture.text(position->block,4).empty()&&!lesson_page_state(fixture.view).can_next,"An unrelated syntax/build error accredited or unlocked the diagnostic objective");
        reader_goal(fixture.view,position->block,false);explanation_region(fixture.view,position->block,explanation,false);retained();
        const auto solved_source=candidate(target,plan.statement);fixture.replace(position->block,solved_source);fixture.select(position->block,5,8);fixture.run(position->block);
        const auto verdict=fixture.text(position->block,4);
        check(verdict.starts_with("✓ Correcto")&&verdict.find(explanation)!=std::string::npos&&windows::compiler_puzzle_completed(fixture.saved(),lesson,target),"The fresh expected semantic rejection did not accredit its own objective");
        check(fixture.text(position->block,34).starts_with("Compilación rechazada.")&&fixture.text(position->block,14).find(plan.error)!=std::string::npos,"Accepted rejection lost its compiler-only status or original diagnostic");
        check(lesson_page_state(fixture.view).can_next,"A completed diagnostic objective still blocks this otherwise solved page");
        reader_goal(fixture.view,position->block,true);explanation_region(fixture.view,position->block,explanation,true);retained();
        check(navigate_lesson_view(fixture.view,position->page+1),"A completed diagnostic failed to release forward navigation");pump();
        check(navigate_lesson_view(fixture.view,position->page),"Cannot return to the completed diagnostic page");pump();fixture.reopen();
        check(fixture.text(position->block,10)==solved_source&&fixture.text(position->block,4)==verdict,"Reopening lost the diagnostic achievement or its actual draft");
        explanation_region(fixture.view,position->block,explanation,true);
        const auto solved_draft=state::get_draft(fixture.saved(),draft_key,"");
        check(solved_draft.selection_start==5&&solved_draft.selection_end==8,"Compilation or reopening lost the source selection");
        fixture.replace(position->block,target.initial_code);fixture.run(position->block);
        check(fixture.text(position->block,4)==verdict&&windows::compiler_puzzle_completed(fixture.saved(),lesson,target),"Editing and compiling revoked completed diagnostic credit");
        fixture.reopen();check(fixture.text(position->block,10)==target.initial_code&&fixture.text(position->block,4)==verdict,"A changed draft and sticky diagnostic credit were not preserved independently");
        fixture.reset(position->block);fixture.reopen();
        check(fixture.text(position->block,10)==target.initial_code&&fixture.text(position->block,4).empty()
            &&!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&!lesson_page_state(fixture.view).can_next,"Explicit Reset did not restore this diagnostic's pending objective");
        reader_goal(fixture.view,position->block,false);explanation_region(fixture.view,position->block,explanation,false);retained();
        check(state::sha256(state::read_bytes(path))==asset_hash,"A diagnostic UI test changed authored lesson bytes");++completed;
    }
    std::cout<<"PASS "<<completed<<" hidden real U9 diagnostic editors: immutable pre-contract adoption exactly reconstructs each current authored block/explanation; fresh initial/error/expected rejection, no runtime, sticky credit/draft/selection, gates, scoped Reset, original preferences/hearts/assets; other stdout receipts explicitly synthetic\n";
}
}
int main(int argc,char** argv){
    const bool paths_only=argc==2&&std::string_view(argv[1])=="--diagnostic-paths";
    if(argc>1&&!paths_only)return 2;
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{compact_diagnostic_path_cases();if(!paths_only)five_real_diagnostic_views();}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}

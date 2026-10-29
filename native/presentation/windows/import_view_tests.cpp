#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/retirement_test_fixture.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using curriculum::BlockType;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

int block_id(std::size_t block, int part) {
    return 1000 + static_cast<int>(block) * 100 + part;
}

HWND descendant(HWND parent, int id) {
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND child, LPARAM parameter) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(parameter);
        if (GetDlgCtrlID(child) == value.id) { value.result = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

std::string window_text(HWND control) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return utf8(text);
}

void pump(HWND host) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        if (!IsDialogMessageW(host, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}

void wait_for(HWND host, const std::function<bool()>& ready, std::string_view message) {
    const auto deadline = GetTickCount64() + 90000;
    do {
        pump(host);
        if (ready()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(std::string(message));
}
thread_local bool menu_observed = false;
void CALLBACK cancel_menu_timer(HWND,UINT,UINT_PTR timer,DWORD) {
    GUITHREADINFO info{sizeof(info)};
    menu_observed = GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags & GUI_INMENUMODE);
    KillTimer(nullptr,timer); EndMenu();
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

void write_file(const std::filesystem::path& file, std::string_view value) {
    std::filesystem::create_directories(state::native_io_path(file.parent_path()));
    std::ofstream output(state::native_io_path(file), std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
    require(static_cast<bool>(output), "Cannot write isolated Android export fixture");
}

// Numeric character references preserve CR, LF and TAB exactly through XML
// parsing, including raw legacy strings; escaping is not limited to JSON-v1.
std::string xml_text(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        case '\r': result += "&#13;"; break;
        case '\n': result += "&#10;"; break;
        case '\t': result += "&#9;"; break;
        default: result += character; break;
        }
    }
    return result;
}

void export_preferences(const std::filesystem::path& folder, const state::StateData& data) {
    for (const auto& [domain, values] : data.preferences) {
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<map>";
        for (const auto& [key, value] : values) std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            const auto attribute = " name=\"" + xml_text(key) + "\"";
            if constexpr (std::is_same_v<T, std::string>)
                xml += "<string" + attribute + ">" + xml_text(item) + "</string>";
            else if constexpr (std::is_same_v<T, bool>)
                xml += "<boolean" + attribute + " value=\"" + (item ? "true" : "false") + "\"/>";
            else if constexpr (std::is_same_v<T, std::int64_t>)
                xml += "<long" + attribute + " value=\"" + std::to_string(item) + "\"/>";
            else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream number; number.imbue(std::locale::classic());
                number << std::setprecision(17) << item;
                xml += "<float" + attribute + " value=\"" + number.str() + "\"/>";
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                xml += "<set" + attribute + ">";
                for (const auto& entry : item) xml += "<string>" + xml_text(entry) + "</string>";
                xml += "</set>";
            }
        }, value);
        xml += "</map>";
        write_file(folder / L"shared_prefs" / (domain + ".xml"), xml);
    }
}

using FileHashes = std::map<std::filesystem::path, std::string>;
FileHashes hashes(const std::filesystem::path& folder) {
    FileHashes result;
    for (const auto& file : std::filesystem::recursive_directory_iterator(state::native_io_path(folder))) {
        if (!file.is_regular_file()) continue;
        result[file.path().lexically_relative(state::native_io_path(folder))] =
            state::sha256(state::read_bytes(file.path()));
    }
    return result;
}

std::string canonical_payload(std::string_view json) {
    // Reuse the existing complete v2 QA framing, which sorts object keys but
    // retains exact string bytes, array order, every field and each JSON type.
    // The lesson loader already validates these payloads' integral numbers.
    std::string result;
    test::compile_check_test_canonical(winrt::Windows::Data::Json::JsonValue::Parse(winrt::to_hstring(json)),result);
    return result;
}

void archived_json_comparison_preserves_every_value() {
    constexpr std::string_view original=R"({"type":"code","code":" int n = 2;\n\t","extra":{"enabled":true,"values":[1,2],"missing":null}})";
    constexpr std::string_view reordered=R"( { "extra": { "missing": null, "values": [1, 2], "enabled": true }, "code": " int n = 2;\n\t", "type": "code" } )";
    const auto expected=canonical_payload(original);
    require(expected==canonical_payload(reordered),"Archived JSON comparison depends on pretty-printing or object property order");
    for(const auto changed:{
        R"({"type":"code","code":"int n = 2;\n\t","extra":{"enabled":true,"values":[1,2],"missing":null}})",
        R"({"type":"code","code":" int n = 2;\n\t","extra":{"enabled":false,"values":[1,2],"missing":null}})",
        R"({"type":"code","code":" int n = 2;\n\t","extra":{"enabled":true,"values":[2,1],"missing":null}})",
        R"({"type":"code","code":" int n = 2;\n\t","extra":{"enabled":true,"values":[1,2]}})"})
        require(expected!=canonical_payload(changed),"Archived JSON comparison ignored code whitespace, nested values, array order or a removed field");
    std::cout<<"PASS archived JSON: property order/formatting equivalent, exact code strings and all nested values protected\n";
}

std::string crlf(std::string_view source) {
    std::string result;
    for (const auto character : source) { if (character == '\n') result += '\r'; result += character; }
    return result;
}
std::string visual_lf(std::string_view source) {
    std::string result;
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\r') {
            result += '\n';
            if (index + 1 < source.size() && source[index + 1] == '\n') ++index;
        } else result += source[index];
    }
    return result;
}

std::string byte_difference(std::string_view expected, std::string_view actual) {
    std::size_t first = 0;
    while (first < expected.size() && first < actual.size() && expected[first] == actual[first]) ++first;
    return " expected bytes=" + std::to_string(expected.size()) + " actual bytes=" + std::to_string(actual.size()) +
        " first difference=" + std::to_string(first) + " CR=" +
        std::to_string(std::count(expected.begin(), expected.end(), '\r')) + "->" +
        std::to_string(std::count(actual.begin(), actual.end(), '\r')) + " LF=" +
        std::to_string(std::count(expected.begin(), expected.end(), '\n')) + "->" +
        std::to_string(std::count(actual.begin(), actual.end(), '\n'));
}

struct StderrFixturePlan {
    std::string_view folder,filename,lesson_id,owner,owner_reference,target,target_reference;
    std::size_t page;
};

StderrFixturePlan stderr_fixture_plan(std::string_view name) {
    if(name=="u8")return {"excepciones_y_recuperacion","lesson_limites_comprobados_y_tipos_de_captura.json",
        "cpp_fundamentals__excepciones_y_recuperacion__limites_comprobados_y_tipos_de_captura",
        "block_0002_0005","block_0003_0005","block_0002_0008","",2};
    if(name=="u10")return {"aleatoriedad_y_reproducibilidad","lesson_combinar_ensayos_generados_y_casos_elegidos.json",
        "cpp_fundamentals__aleatoriedad_y_reproducibilidad__combinar_ensayos_generados_y_casos_elegidos",
        "block_0004_0004","block_0005_0001","block_0004_0007","block_0005_0008",4};
    throw std::invalid_argument("The isolated stderr import fixture must select u8 or u10");
}

std::filesystem::path stderr_fixture_asset(const StderrFixturePlan& plan) {
    return std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/plan.folder/plan.filename;
}

void prepare_stderr_export(std::string_view name,std::wstring_view token) {
    require(is_valid_qa_instance(token),"Unsafe new Android export token");
    const auto plan=stderr_fixture_plan(name);const auto source=stderr_fixture_asset(plan);
    const auto asset_hash=state::sha256(state::read_bytes(source));const auto lesson=windows::load_lesson(source);
    require(lesson.id==plan.lesson_id&&lesson.compiler_puzzles_required,"The stderr export asset identity or required policy changed");
    const auto locate=[&](std::string_view identity){const auto at=curriculum::stable_block_position(lesson,identity);
        require(at.has_value(),"The stderr export lost an authored stable owner, target or reference");return *at;};
    const auto owner_at=locate(plan.owner),target_at=locate(plan.target),reference_at=locate(plan.owner_reference);
    const auto& owner=lesson.pages[owner_at.page].blocks[owner_at.block];
    const auto& target=lesson.pages[target_at.page].blocks[target_at.block];
    const auto& reference=lesson.pages[reference_at.page].blocks[reference_at.block];
    require(owner_at.page==plan.page&&target_at.page==plan.page&&owner.type==BlockType::cpp_compiler&&
        target.type==BlockType::cpp_compiler&&reference.type==BlockType::code&&!reference.code.empty()&&target.output_check&&
        target.output_check->cases.size()==1&&target.output_check->cases[0].standard_error==curriculum::StandardErrorExpectation::non_blank,
        "The stderr export no longer describes the current independent compiler objectives");
    const auto prerequisites=curriculum::activity_prerequisites(lesson,target);
    require(prerequisites.size()==1&&prerequisites.front()==owner_at,"The stderr target no longer has exactly its original constructor");
    auto target_source=target.initial_code;
    if(name=="u8")require(target_source.find("mostrarDiagnostico{false}")!=target_source.npos&&
        target.output_check->cases[0].outputs==std::vector<std::string>{"Indice invalido:Fin"},"The actual U8 false flag or stable stdout changed");
    else {
        const auto report_at=locate(plan.target_reference);const auto& report=lesson.pages[report_at.page].blocks[report_at.block];
        require(report.type==BlockType::code&&!report.code.empty()&&target.output_check->cases[0].outputs==std::vector<std::string>{"Fallos elegidos:2\n"},
            "The actual U10 defective report reference or stable stdout changed");
        target_source=report.code;constexpr std::string_view main_start="int main() {";const auto start=target_source.find(main_start);
        require(start!=target_source.npos&&target_source.find("std::cerr")!=target_source.npos,"Cannot isolate the current authored U10 stderr report");
        target_source.insert(start+main_start.size(),"\n    std::cerr.setstate(std::ios_base::failbit);");
    }
    state::StateData original;state::prepare_lesson_state(original,lesson);state::save_current_page(original,lesson,plan.page);
    const auto save_draft=[&](const curriculum::Block& block,const std::string& code){state::Draft draft;draft.source=code;draft.input=" \t\r\n    ";
        const auto document=windows::predefined_code_document(code);draft.indentation_json=state::make_indentation_metadata(code,document.indentation_starts);
        const auto caret=code.find("std::cout");require(caret!=code.npos,"The actual imported source lost its exact selection token");
        draft.selection_start=static_cast<std::int64_t>(wide(std::string_view(code).substr(0,caret)).size());draft.selection_end=draft.selection_start+9;
        state::put_draft(original,curriculum::draft_key("programming",lesson,block),draft);return draft;};
    const auto owner_draft=save_draft(owner,reference.code),target_draft=save_draft(target,target_source);
    state::Response old;old.answer=" Correcto antiguo de stdout (QA sintética)\t\r\n ";old.correct=true;
    state::put_response(original,lesson,target,old);
    const auto opaque_key=state::response_storage_key(lesson,target);
    const auto opaque_json=state::string_value(original,"learning_progress",opaque_key);
    state::validate_lesson_state(original,lesson);
    require(!original.preferences.contains("native_windows"),"The Android fixture must not export any Windows metadata or receipts");
    const auto parent=(std::filesystem::path(build::source_root)/"out/qa").lexically_normal();
    const auto folder=parent/(L"stderr-export-"+std::wstring(token));
    require(folder.is_absolute()&&folder.parent_path()==parent&&folder.filename().wstring()==L"stderr-export-"+std::wstring(token),
        "The synthetic Android export must stay inside its exact new out/qa child");
    state::require_safe_path(folder);require(!std::filesystem::exists(folder),"The synthetic export refuses to overwrite an existing fixture");
    std::filesystem::create_directories(parent);require(std::filesystem::create_directory(folder),"Another process created the export fixture first");
    export_preferences(folder,original);const auto untouched=hashes(folder);const auto preview=state::inspect_android_export(folder);
    state::validate_lesson_state(preview.data,lesson);
    require(preview.data==original&&state::current_page(preview.data,lesson)==plan.page&&hashes(folder)==untouched&&
        !preview.data.preferences.contains("native_windows")&&!std::filesystem::exists(folder/L"shared_prefs/native_windows.xml"),
        "The Android preview changed source preferences, the historical page or provenance");
    for(const auto& item:preview.files)require(item.relative_path.parent_path()==L"shared_prefs"&&item.relative_path.extension()==L".xml"&&
        untouched.at(item.relative_path)==item.sha256,"The prepared export contains unexpected files or changed source hashes");
    std::string export_frame;for(const auto& [path,hash]:untouched)export_frame+=path.generic_string()+'\n'+hash+'\n';
    const auto preserved=[&](const curriculum::Block& block,const state::Draft& expected){
        const auto draft=state::get_draft(preview.data,curriculum::draft_key("programming",lesson,block),"");
        require(draft.source==expected.source&&draft.input==expected.input&&draft.indentation_json==expected.indentation_json&&
            draft.selection_start==expected.selection_start&&draft.selection_end==expected.selection_end,
            "XML inspection rewrote exact source/input whitespace, indentation or UTF16 selection");};
    preserved(owner,owner_draft);preserved(target,target_draft);
    const auto imported_old=state::get_response(preview.data,lesson,target);
    require(imported_old&&imported_old->correct&&imported_old->answer==old.answer&&
        state::string_value(preview.data,"learning_progress",opaque_key)==opaque_json,"The opaque historical stdout response changed");
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(curriculum::has_compiler_objective(block))
        require(!windows::compiler_puzzle_completed(preview.data,lesson,block)&&windows::compiler_puzzle_case_count(preview.data,lesson,block)==0,
            "An opaque Android result manufactured a Windows execution receipt");
    require(state::sha256(state::read_bytes(source))==asset_hash,"Preparing an export changed the actual curricular asset");
    std::cout<<"export="<<utf8(folder.native())<<"\nfixture=synthetic-android-stderr\nlesson="<<lesson.id<<"\nasset_sha256="<<asset_hash
        <<"\npage="<<plan.page+1<<"\npage_index="<<plan.page<<"\nowner="<<plan.owner<<"\nowner_reference="<<plan.owner_reference
        <<"\nowner_draft_sha256="<<state::sha256(owner_draft.source)<<"\ntarget="<<plan.target<<"\ntarget_variant="<<(name=="u8"?"false":"silent-authored-reference")
        <<"\ntarget_draft_sha256="<<state::sha256(target_draft.source)<<"\nopaque_response_key="<<opaque_key
        <<"\nopaque_response_sha256="<<state::sha256(opaque_json)<<"\nexport_sha256="<<state::sha256(export_frame)<<"\nxml_files="<<preview.files.size()
        <<"\nwindows_receipts=0\nnative_windows_xml=0\ncompiler_executions=none\nwindows_created=0\n";
}

void prepare_import_catalog(std::string_view name,std::wstring_view token) {
    require(is_valid_qa_instance(token),"Unsafe new import catalog QA token");
    const auto plan=stderr_fixture_plan(name);const auto source=stderr_fixture_asset(plan);
    const auto asset_hash=state::sha256(state::read_bytes(source));const auto lesson=windows::load_lesson(source);
    require(lesson.id==plan.lesson_id,"The isolated import catalog asset identity changed");
    const auto environment=resolve_windows_environment({ProfileId::Codex,std::wstring(token),false});
    require(environment.paths.root.parent_path().filename()==L"qa"&&environment.paths.root.parent_path().parent_path().filename()==L"Codex"&&
        environment.paths.root.filename()==token,"The import catalog must be a new isolated Codex QA profile");
    state::require_safe_path(environment.paths.root);require(!std::filesystem::exists(environment.paths.root),"The catalog helper refuses to reuse an existing QA profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    require(std::filesystem::create_directory(environment.paths.root),"Another process reserved the catalog QA profile first");
    initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);
    require(store.load().preferences.empty(),"The new import catalog unexpectedly contains earlier data");
    state::StateData data;auto& preferences=data.preferences["native_windows"];
    preferences["ui_language"]=std::string("es");preferences["content_language"]=std::string("es");preferences["catalog/locale"]=std::string("es");
    preferences["catalog/route"]=std::string("programming");preferences["catalog/course"]=std::string("cpp_fundamentals");
    const auto unit=lesson.id.rfind("__");require(unit!=lesson.id.npos,"The current lesson has no catalog unit identity");
    preferences["catalog/unit"]=lesson.id.substr(0,unit);
    require(data.preferences.size()==1&&preferences.size()==6&&state::can_import_android_profile(data),"Catalog-only QA must remain eligible for Android import");
    store.save(data);require(store.load()==data&&state::can_import_android_profile(store.load()),"Saving the catalog invented progress or invalidated import");
    require(state::sha256(state::read_bytes(source))==asset_hash,"Preparing the catalog modified its curricular asset");
    std::cout<<"profile=Codex\nqa_instance="<<utf8(token)<<"\ndata_root="<<utf8(environment.paths.root.native())
        <<"\ncatalog_unit="<<lesson.id.substr(0,unit)<<"\nchoose_lesson="<<(name=="u8"?3:4)
        <<"\npreferences=native_windows-only\nnavigation_lesson=absent\nprogress_drafts_responses_receipts=none\nimport_eligible=1\nwindows_created=0\n";
}

void verify_stderr_import(std::string_view name,std::wstring_view qa_token,std::wstring_view export_token) {
    require(is_valid_qa_instance(qa_token)&&is_valid_qa_instance(export_token),"The read-only verifier accepts only safe explicit QA tokens");
    const auto plan=stderr_fixture_plan(name);const auto source=stderr_fixture_asset(plan);
    const auto asset_hash=state::sha256(state::read_bytes(source));const auto lesson=windows::load_lesson(source);
    require(lesson.id==plan.lesson_id&&lesson.compiler_puzzles_required,"The read-only verifier lost its actual curricular identity");
    const auto environment=resolve_windows_environment({ProfileId::Codex,std::wstring(qa_token),false});
    const auto qa_root=environment.paths.root;
    require(qa_root.parent_path().filename()==L"qa"&&qa_root.parent_path().parent_path().filename()==L"Codex"&&qa_root.filename()==qa_token,
        "The verifier must never inspect an original student profile");
    const auto export_parent=(std::filesystem::path(build::source_root)/"out/qa").lexically_normal();
    const auto exported=export_parent/(L"stderr-export-"+std::wstring(export_token));
    require(exported.is_absolute()&&exported.parent_path()==export_parent&&std::filesystem::is_directory(exported)&&
        std::filesystem::is_directory(qa_root),"The verifier requires both exact existing isolated QA roots");
    const auto safe_hashes=[](const std::filesystem::path& folder){state::require_safe_path(folder);
        for(const auto& entry:std::filesystem::recursive_directory_iterator(state::native_io_path(folder)))state::require_safe_path(entry.path());
        return hashes(folder);};
    const auto active=qa_root/L"state/active";state::require_safe_path(active);
    require(std::filesystem::is_regular_file(active/L"state.bin")&&std::filesystem::is_directory(active/L"android"),
        "The QA profile has no completed Android import to verify");
    const auto original_export=safe_hashes(exported),original_active=safe_hashes(active);
    const auto preview=state::inspect_android_export(exported);
    std::string import_manifest="LearnGameAndEngineDev.android-import.v1\n";
    for(const auto& [path,hash]:original_export)import_manifest+=hash+' '+path.generic_string()+'\n';
    auto expected_archive=original_export;expected_archive.emplace(".import-manifest.txt",state::sha256(import_manifest));
    require(!preview.data.preferences.contains("native_windows")&&state::current_page(preview.data,lesson)==plan.page&&
        safe_hashes(active/L"android")==expected_archive,"The archived Android metadata/files or exact import manifest differ from the source export");
    const auto manifest_bytes=state::read_bytes(active/L"android/.import-manifest.txt");
    require(std::string(manifest_bytes.begin(),manifest_bytes.end())==import_manifest,
        "The imported QA archive lost its exact generated provenance manifest");
    state::StateStore store(qa_root);const auto data=store.load();
    const auto locate=[&](std::string_view identity){const auto at=curriculum::stable_block_position(lesson,identity);
        require(at.has_value(),"The verifier lost an actual compiler owner, target or reference");return *at;};
    const auto owner_at=locate(plan.owner),target_at=locate(plan.target),reference_at=locate(plan.owner_reference);
    const auto& owner=lesson.pages[owner_at.page].blocks[owner_at.block];const auto& target=lesson.pages[target_at.page].blocks[target_at.block];
    const auto& reference=lesson.pages[reference_at.page].blocks[reference_at.block];
    require(owner_at.page==plan.page&&target_at.page==plan.page&&owner.type==BlockType::cpp_compiler&&target.type==BlockType::cpp_compiler&&
        reference.type==BlockType::code&&!owner.standard_input&&!target.standard_input,
        "The imported stderr activities no longer have their original unrequested stdin contract");
    const auto owner_key=curriculum::draft_key("programming",lesson,owner),target_key=curriculum::draft_key("programming",lesson,target);
    const auto original_owner=state::get_draft(preview.data,owner_key,""),original_target=state::get_draft(preview.data,target_key,"");
    const auto saved_owner=state::get_draft(data,owner_key,""),saved_target=state::get_draft(data,target_key,"");
    const std::string hidden_input=" \t\r\n    ";
    const auto verify_input=[&](const std::string& key,const state::Draft& original,const state::Draft& saved){
        require(original.has_saved&&saved.has_saved&&original.input==hidden_input&&saved.input==hidden_input&&
            state::string_value(preview.data,"cpp_lesson_drafts",key+"/inputSha256")==state::sha256(hidden_input)&&
            state::string_value(data,"cpp_lesson_drafts",key+"/inputSha256")==state::sha256(hidden_input),
            "An unrequested imported draft input lost its exact eight bytes or original digest");};
    verify_input(owner_key,original_owner,saved_owner);verify_input(target_key,original_target,saved_target);
    require(original_owner.source==reference.code&&saved_owner.source==reference.code&&
        saved_owner.selection_start==original_owner.selection_start&&saved_owner.selection_end==original_owner.selection_end&&
        saved_owner.indentation_json==original_owner.indentation_json,
        "The imported constructor source, original selection or indentation metadata changed");
    std::string initial,enabled;
    if(name=="u8") {
        initial=target.initial_code;enabled=initial;constexpr std::string_view flag="mostrarDiagnostico{false}";const auto at=enabled.find(flag);
        require(at!=enabled.npos,"The U8 import verifier lost its authored false flag");enabled.replace(at,flag.size(),"mostrarDiagnostico{true}");
    } else {
        const auto report_at=locate(plan.target_reference);const auto& report=lesson.pages[report_at.page].blocks[report_at.block];
        require(report.type==BlockType::code,"The U10 import verifier lost its authored report source");enabled=report.code;initial=enabled;
        constexpr std::string_view main_start="int main() {";const auto at=initial.find(main_start);
        require(at!=initial.npos,"The U10 report no longer contains its exact authored main");
        initial.insert(at+main_start.size(),"\n    std::cerr.setstate(std::ios_base::failbit);");
    }
    require(original_target.source==initial&&(saved_target.source==initial||saved_target.source==enabled),
        "The target source differs from the exact imported or explicitly edited GUI variant");
    const auto response_key=state::response_storage_key(lesson,target);
    const auto old=state::get_response(preview.data,lesson,target),saved=state::get_response(data,lesson,target);
    require(old&&old->correct&&saved&&saved->correct&&saved->answer==old->answer&&
        state::string_value(data,"learning_progress",response_key)==state::string_value(preview.data,"learning_progress",response_key),
        "Fresh Windows completion rewrote the opaque historical stdout response");
    unsigned completed=0;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(curriculum::has_compiler_objective(block)) {
        require(!windows::compiler_puzzle_completed(preview.data,lesson,block)&&windows::compiler_puzzle_case_count(preview.data,lesson,block)==0,
            "The Android export contained an invented Windows execution receipt");
        const bool expected=block.stable_id==plan.owner||block.stable_id==plan.target;
        require(windows::compiler_puzzle_completed(data,lesson,block)==expected&&windows::compiler_puzzle_case_count(data,lesson,block)==(expected?1u:0u),
            "The imported QA lacks either current real completion or credited another compiler objective");
        if(expected)++completed;
    }
    require(completed==2&&store.load()==data&&safe_hashes(exported)==original_export&&safe_hashes(active)==original_active&&
        state::sha256(state::read_bytes(source))==asset_hash,"Read-only import verification changed state, original export, archive files or asset");
    std::string export_frame;for(const auto& [path,hash]:original_export)export_frame+=path.generic_string()+'\n'+hash+'\n';
    std::cout<<"PASS isolated stderr import conservation\nqa_instance="<<utf8(qa_token)<<"\nexport="<<utf8(exported.native())
        <<"\nlesson="<<lesson.id<<"\nasset_sha256="<<asset_hash<<"\nexport_sha256="<<state::sha256(export_frame)
        <<"\nstate_sha256="<<original_active.at("state.bin")<<"\nowner="<<plan.owner<<"\ntarget="<<plan.target
        <<"\ntarget_variant="<<(saved_target.source==initial?"imported-stdout-only":"explicit-GUI-two-channel")
        <<"\nhidden_input_hex=20090d0a20202020\nowner_selection="<<saved_owner.selection_start<<','<<saved_owner.selection_end
        <<"\nopaque_response=unchanged\narchive=byte-exact\ncompleted_windows_objectives=2\nother_objective_receipts=0\nreadonly=1\nwindows_created=0\n";
}

struct ExportFixture {
    std::filesystem::path folder;
    state::StateData data;
    std::size_t prediction = 0, conceptual = 0;
    std::vector<std::size_t> editors;
    std::vector<state::Draft> drafts;

    ExportFixture(const std::filesystem::path& path, const curriculum::Lesson& lesson) : folder(path) {
        require(lesson.state_layout.version == 2 && lesson.pages.size() == 6, "Expected original U1L2 stateLayout v2");
        state::save_current_page(data, lesson, 4);
        // These are already-submitted synthetic previous-page responses. The
        // page-five prediction below deliberately remains unsubmitted.
        for (std::size_t page = 0; page != 4; ++page) for (const auto& block : lesson.pages[page].blocks) {
            state::Response answer;
            if (block.type == BlockType::multiple_choice) answer.selected = static_cast<std::int64_t>(*block.correct_index);
            else if (block.type == BlockType::order_code) answer.order.assign(block.correct_order.begin(), block.correct_order.end());
            else if (block.type == BlockType::output_prediction || block.type == BlockType::fill_code) answer.answer = block.answers.at(0);
            else continue;
            answer.correct = true;
            state::put_response(data, curriculum::exercise_key(lesson, block), answer);
        }
        for (std::size_t index = 0; index < lesson.pages[4].blocks.size(); ++index) {
            const auto& block = lesson.pages[4].blocks[index];
            if (block.type == BlockType::output_prediction) prediction = index;
            else if (block.type == BlockType::multiple_choice) conceptual = index;
            else if (block.type == BlockType::cpp_compiler) editors.push_back(index);
        }
        require(editors.size() == 2, "Expected two independent page-five editors");
        state::Response partial;
        partial.answer = lesson.pages[4].blocks[prediction].answers.at(0);
        state::put_response(data, curriculum::exercise_key(lesson, lesson.pages[4].blocks[prediction]), partial);
        for (std::size_t position = 0; position != editors.size(); ++position) {
            const auto& block = lesson.pages[4].blocks[editors[position]];
            auto source = block.initial_code;
            if (position) {
                const auto literal = source.find("Zona inicial");
                require(literal != source.npos, "Missing original U1L2 literal");
                source.replace(literal, std::string("Zona inicial").size(), "Caja recibida");
            }
            state::Draft draft;
            draft.source = position ? "// versión JSON-v1 😀\r" + crlf(source) + "\t// segundo borrador\r\n"
                                    : "// versión original 😀\r\n" + crlf(source) + "// conserva espacios \t\r\n    ";
            draft.input = position ? "\tLeo\r2\r\n\t \n" : " \tRita 4\r\n \n    ";
            const auto document = windows::predefined_code_document(draft.source);
            draft.indentation_json = state::make_indentation_metadata(draft.source, document.indentation_starts);
            const auto token = draft.source.find("std::cout");
            require(token != draft.source.npos, "Missing known source token for UTF16 caret");
            draft.selection_start = static_cast<std::int64_t>(wide(std::string_view(draft.source).substr(0, token)).size());
            draft.selection_end = draft.selection_start + 9;
            const auto key = curriculum::draft_key("programming", lesson, block);
            state::put_draft(data, key, draft);
            if (!position) {
                // Genuine historical non-JSON storage, with independent SHA
                // and indentation sidecar. No invented positional keys.
                auto& preferences = data.preferences["cpp_lesson_drafts"];
                preferences[key + "/source"] = draft.source;
                preferences[key + "/input"] = draft.input;
                preferences.erase(key + "/sourceEncoding");
                preferences.erase(key + "/inputEncoding");
            }
            drafts.push_back(std::move(draft));
        }
        const auto first_key = curriculum::draft_key("programming", lesson, lesson.pages[4].blocks[editors[0]]);
        const auto second_key = curriculum::draft_key("programming", lesson, lesson.pages[4].blocks[editors[1]]);
        require(first_key != second_key && first_key.find("state-layout-v2/") != first_key.npos,
            "Added editor must not steal the historical modification slot");
        // Old cached compiler correctness is retained as opaque source state,
        // but the UI must never turn it into a fresh execution verdict.
        state::Response stale; stale.answer = "Resultado antiguo"; stale.correct = true;
        state::put_response(data, curriculum::exercise_key(lesson, lesson.pages[4].blocks[editors[1]]), stale);
        state::validate_lesson_state(data, lesson);
        publish();
        write_file(folder / L"files/cpp_projects/main.cpp", "\tint dato = 4;\r\n// archivo del estudiante 😀\r\n");
        write_file(folder / L"files/cpp_projects/entrada.txt", " \tRita 4\r\n    ");
    }

    void publish() const { export_preferences(folder, data); }
};

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::filesystem::path content;
    HWND host = nullptr, view = nullptr;
    bool supplied_model = false;

    explicit View(std::optional<curriculum::Lesson> supplied = {}) : environment(resolve_windows_environment({ProfileId::Codex,
            L"native-import-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false})),
        content(std::filesystem::path(build::source_root) / L"app/src/main/assets/content") {
        require(!std::filesystem::exists(environment.paths.root), "An import UI test cannot reuse a real profile");
        initialize_profile_directories(environment.paths);
        // The legacy export contracts retain the exact published two-editor
        // presentation; current retirement import is verified separately.
        lesson = test::load_pre_retirement_lesson("lesson_primer_programa_y_salida.json");supplied_model=true;
        if(supplied){lesson=std::move(*supplied);supplied_model=true;}
        host = CreateWindowExW(WS_EX_CONTROLPARENT, L"LgaedImportViewTests", L"Native Android import QA",
            WS_OVERLAPPEDWINDOW, 30, 30, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create native import host");
        open();
    }
    ~View() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        view = supplied_model?create_lesson_view(host,environment,lesson):create_lesson_view(host, environment, content);
        require(view != nullptr, "Cannot create the real lesson/import view");
        RECT bounds{}; GetClientRect(host, &bounds);
        MoveWindow(view, 0, 0, bounds.right, bounds.bottom, TRUE);
        pump(host);
    }
    void reopen() {
        require(flush_lesson_view(view), "Cannot flush before reopening");
        DestroyWindow(view); view = nullptr; pump(host); open();
    }
    HWND control(int id) const {
        const auto result = descendant(view, id);
        require(result != nullptr, "Missing native control " + std::to_string(id));
        return result;
    }
    HWND control(std::size_t block, int part) const { return control(block_id(block, part)); }
    void click(int id) {
        if (id == 104) {
            require(IsWindowEnabled(control(107)),"A disabled lesson menu cannot open import");
            // Import is now a menu command, rather than an invisible button.
            SendMessageW(view,WM_COMMAND,MAKEWPARAM(104,0),0); pump(host); return;
        }
        const auto button = control(id);
        require(IsWindowEnabled(button), "A disabled import control cannot be activated");
        SendMessageW(button, BM_CLICK, 0, 0);
        if (id == 206)
            require(IsWindow(button), "Import navigation destroyed its button during the native BM_CLICK callback");
        pump(host);
    }
    void cancel_menu() {
        const bool shown = IsWindowVisible(host); if (!shown) { ShowWindow(host,SW_SHOWNOACTIVATE); pump(host); }
        menu_observed = false;
        const auto timer = SetTimer(nullptr,0,50,cancel_menu_timer);
        require(timer != 0,"Cannot schedule native menu cancellation");
        click(107); KillTimer(nullptr,timer);
        require(menu_observed && !descendant(view,205),"The actual native menu did not open/cancel without choosing import");
        if (!shown) { ShowWindow(host,SW_HIDE); pump(host); }
    }
    void path(const std::filesystem::path& folder) {
        require(SetWindowTextW(control(201), folder.c_str()) != FALSE, "Cannot enter the exported folder");
        pump(host);
        require(window_text(control(201)) == utf8(folder.wstring()), "The real path EDIT changed Unicode or symbols");
    }
    std::string summary() const { return edit_text(control(204)); }
    bool empty() const { return state::StateStore(environment.paths.root).empty(); }
    std::size_t page() const { return lesson_page_state(view).index; }
    state::StateData saved() const {
        require(flush_lesson_view(view), "Cannot save the restored lesson");
        state::StateStore reader(environment.paths.root); return reader.load();
    }
};

void no_pending_transaction(const View& view) {
    const auto root = view.environment.paths.root / L"state";
    if (!std::filesystem::exists(root)) return;
    for (const auto& file : std::filesystem::directory_iterator(root))
        require(!file.path().filename().wstring().ends_with(L".pending"), "Failed import left an unpublished transaction");
}

void assert_restored(View& view, const ExportFixture& exported, bool original_executed = false) {
    require(view.page() == 4, "The lesson did not restore the historical page-five bookmark");
    const auto data = view.saved();
    const auto& prediction = view.lesson.pages[4].blocks[exported.prediction];
    const auto answer = state::get_response(data, curriculum::exercise_key(view.lesson, prediction));
    require(answer && answer->answer == prediction.answers.at(0) && !answer->correct &&
        !state::response_reviewed(*answer, prediction), "A partial correct-looking answer was silently submitted/evaluated");
    require(edit_text(view.control(exported.prediction, 2)) == prediction.answers.at(0) &&
        edit_text(view.control(exported.prediction, 4)).empty() && IsWindowEnabled(view.control(exported.prediction, 2)),
        "Partial prediction did not remain visible, editable and unreviewed");
    for (std::size_t position = 0; position != exported.editors.size(); ++position) {
        const auto index = exported.editors[position];
        const auto& block = view.lesson.pages[4].blocks[index];
        const auto restored = state::get_draft(data, curriculum::draft_key("programming", view.lesson, block), block.initial_code);
        const auto& expected = exported.drafts[position];
        require(restored.source == expected.source,
            "Opening/restoring changed editor " + std::to_string(position) + " source:" +
                byte_difference(expected.source, restored.source));
        require(restored.input == expected.input,
            "Opening/restoring changed editor " + std::to_string(position) + " hidden stdin:" +
                byte_difference(expected.input, restored.input));
        require(state::indentation_units(restored.source, restored.indentation_json) ==
            state::indentation_units(expected.source, expected.indentation_json), "Imported indentation origin was not retained");
        require(restored.selection_start == expected.selection_start && restored.selection_end == expected.selection_end,
            "Imported UTF16 source selection changed during display/save");
        require(!descendant(view.view, block_id(index, 15)), "An imported hidden input invented a curricular stdin field");
        if (position == 1 && !original_executed) {
            require(!descendant(view.view,block_id(index,10)) && !descendant(view.view,block_id(index,11))
                    && !descendant(view.view,block_id(index,4)) && !descendant(view.view,block_id(index,14))
                    && descendant(view.view,block_id(index,18)),
                "An imported bookmark or opaque compiler verdict exposed a dependent editor before its original execution");
            require(std::ranges::none_of(lesson_reading_regions(view.view),[&](const auto& region) {
                return region.block_index == index;
            }),"The reader exposed the blocked imported compiler draft before its prerequisite");
            continue;
        }
        require(edit_text(view.control(index, 10)) == visual_lf(expected.source), "Source displayed the wrong independent editor draft");
        const auto feedback=edit_text(view.control(index,4));
        require((original_executed&&position==0?feedback.starts_with("✓ Correcto"):feedback.empty())
                &&edit_text(view.control(index,14)).empty(),
            "Import/reopen confused an opaque cached response with a freshly completed compiler objective or restored stale console output");
    }
}

void imported_stdout_success_does_not_accredit_a_new_runtime_stderr_goal() {
    auto lesson=windows::parse_lesson_json(R"json({"id":"qa_import_stderr","title":"Importar ambos canales","compilerPuzzlesRequired":true,
      "pages":[{"blocks":[{"type":"paragraph","text":"Introducción para importar antes de editar."}]},{"blocks":[{"id":"runtime","type":"cpp_compiler","initialCode":"#include <iostream>\nint main(){std::cout << \"Mensaje estable\";}\n",
        "outputCheck":{"cases":[{"standardInput":"","outputs":["Mensaje estable"]}]}},
        {"type":"paragraph","text":"Contenido posterior."}]},{"blocks":[{"type":"paragraph","text":"Final."}]}]})json");
    state::StateData old;state::prepare_lesson_state(old,lesson);state::save_current_page(old,lesson,1);
    const auto exercise=curriculum::exercise_key(lesson,lesson.pages[1].blocks[0]);
    state::put_response(old,exercise,state::Response{"Correcto antiguo de stdout",true});
    state::Draft draft;draft.source=lesson.pages[1].blocks[0].initial_code+"// borrador importado\n";
    const auto draft_key=curriculum::draft_key("programming",lesson,lesson.pages[1].blocks[0]);state::put_draft(old,draft_key,draft);
    lesson.pages[1].blocks[0].output_check->cases[0].standard_error=curriculum::StandardErrorExpectation::non_blank;
    View view(std::move(lesson));const auto folder=view.environment.paths.projects/L"stderr-export";export_preferences(folder,old);
    const auto original=hashes(folder);view.click(104);view.path(folder);view.click(203);
    require(IsWindowEnabled(view.control(205)),"The compatible added stderr goal rejected historical Android state");
    view.click(205);require(view.summary().starts_with("Importación completada"),"The isolated stderr-history import did not complete");view.click(206);
    const auto verify=[&]{const auto current=view.saved();const auto response=state::get_response(current,exercise);
        require(response&&response->correct&&response->answer=="Correcto antiguo de stdout",
            "The new runtime-channel goal deleted or changed the imported opaque stdout response");
        require(view.page()==1&&edit_text(view.control(0,10))==draft.source&&!lesson_page_state(view.view).can_next
            &&!windows::compiler_puzzle_completed(current,view.lesson,view.lesson.pages[1].blocks[0])
            &&!descendant(view.view,block_id(1,0)),"Imported stdout correctness accredited the new diagnostic, exposed following content or lost the draft");
        require(hashes(folder)==original,"Runtime-channel import/reopen altered its source export");};
    verify();require(edit_text(view.control(0,4)).empty(),"Import restored an old cached output verdict as current");
    view.reopen();verify();view.click(block_id(0,11));wait_for(view.host,[&]{return IsWindowEnabled(view.control(0,11));},"The imported compiler did not execute");
    require(edit_text(view.control(0,14))=="Mensaje estable"&&edit_text(view.control(0,4)).find("falta el diagnóstico solicitado en stderr")!=std::string::npos,
        "A fresh imported stdout-only program failed to expose the missing runtime diagnostic");
    verify();view.reopen();verify();
    std::cout<<"PASS actual Android import of old stdout correctness: added stderr goal remains pending, draft/opaque response/export preserved, fresh partial evaluation and reopen\n";
}

void imported_pre_retirement_state_opens_current_asset() {
    const auto published=test::load_pre_retirement_lesson("lesson_primer_programa_y_salida.json");
    const auto current=windows::load_lesson(published.source_path);
    const auto asset_hash=state::sha256(state::read_bytes(published.source_path));
    // Work subsequently archived the two comparison reference programs as a
    // separate batch. Preserve all four exact historical payloads; counting
    // only the original compiler/consigna retirement no longer describes the
    // active lesson, and must not turn valid import into a failing fixture.
    const std::set<std::string> expected_retired{
        "ejecucion_original_consigna","ejecucion_original_editor","block_0005_0002","block_0005_0008"};
    std::set<std::string> actual_retired,layout_retired;
    for(const auto& archived:current.retired_blocks){
        actual_retired.insert(archived.stable_id);
        const auto original=curriculum::stable_block_position(published,archived.stable_id);
        require(original.has_value(),"The current import fixture archived an identity absent from its immutable historical lesson: "+archived.stable_id);
        const auto& historical=published.pages[original->page].blocks[original->block].raw_json;
        const bool equivalent=canonical_payload(archived.raw_json)==canonical_payload(historical);
        std::cout<<"Archived import payload "<<archived.stable_id<<": historical-UTF8-bytes="<<historical.size()
            <<" current-UTF8-bytes="<<archived.raw_json.size()<<" raw-equal="<<(archived.raw_json==historical)
            <<" canonical-equal="<<equivalent<<'\n';
        require(equivalent,"The current import fixture changed the complete historical payload: "+archived.stable_id);
        require(!curriculum::stable_block_position(current,archived.stable_id),
            "The current import fixture reactivated an archived historical identity");
    }
    for(const auto& archived:current.state_layout.retired)layout_retired.insert(archived.id);
    require(current.compiler_puzzles_required&&actual_retired==expected_retired&&layout_retired==expected_retired
        &&current.retired_blocks.size()==expected_retired.size()
        &&current.state_layout.retired.size()==expected_retired.size(),
        "The current import fixture lost one of the two original execution blocks or two preserved reference programs");
    View view(current);ExportFixture exported(view.environment.paths.projects/L"exportacion anterior al retiro",published);
    const auto untouched=hashes(exported.folder);
    view.click(104);view.path(exported.folder);view.click(203);
    require(IsWindowEnabled(view.control(205)),"The current asset rejected a compatible published pre-retirement export");
    view.click(205);require(view.summary().starts_with("Importación completada"),"The current asset did not import the historical U1 data");view.click(206);
    const auto goal=curriculum::stable_block_position(current,"block_0004_0002");
    require(goal.has_value(),"The integrated useful modification lost its stable identity");
    const auto& active=current.pages[goal->page].blocks[goal->block];
    const auto archived=std::ranges::find(current.retired_blocks,std::string("ejecucion_original_editor"),&curriculum::Block::stable_id);
    require(archived!=current.retired_blocks.end(),"The current asset lost the historical compiler archive");
    const auto archived_key=curriculum::draft_key("programming",current,*archived);
    const auto active_key=curriculum::draft_key("programming",current,active);
    std::optional<std::string> edited_source;
    const auto preserved=[&]{const auto data=view.saved();
        const auto active_draft=state::get_draft(data,active_key,"");
        require(view.page()==goal->page&&!curriculum::stable_block_position(current,archived->stable_id)
            &&state::get_draft(data,archived_key,"").source==exported.drafts[0].source
            &&state::get_draft(data,archived_key,"").input==exported.drafts[0].input
            &&(edited_source?visual_lf(active_draft.source)==*edited_source:active_draft.source==exported.drafts[1].source)
            &&active_draft.input==exported.drafts[1].input,
            "Importing into the integrated asset lost independent archived/active sources or inputs");
        require(hashes(exported.folder)==untouched,"The current-asset import rewrote its historical source export");
        require(state::sha256(state::read_bytes(published.source_path))==asset_hash,
            "The current-asset import modified the authored lesson");
        for(const auto& retired:current.retired_blocks){
            require(!curriculum::stable_block_position(view.lesson,retired.stable_id),
                "Importing or reopening reactivated an archived compiler, consigna or reference");
            const auto retained=std::ranges::find(view.lesson.retired_blocks,retired.stable_id,&curriculum::Block::stable_id);
            require(retained!=view.lesson.retired_blocks.end()&&canonical_payload(retained->raw_json)==canonical_payload(retired.raw_json),
                "Importing or reopening discarded or rewrote a historical archive payload");
        }
        struct Probe{bool leaked=false;} probe;
        EnumChildWindows(view.view,[](HWND window,LPARAM context)->BOOL{auto& probe=*reinterpret_cast<Probe*>(context);
            if(window_text(window).find("versión original")!=std::string::npos)probe.leaked=true;return !probe.leaked;
        },reinterpret_cast<LPARAM>(&probe));
        require(!probe.leaked,"The archived original draft leaked into a current native/accessibility control");};
    preserved();
    require(edit_text(view.control(goal->block,10))==visual_lf(exported.drafts[1].source)
        &&edit_text(view.control(goal->block,4)).empty()
        &&!windows::compiler_puzzle_completed(view.saved(),current,active),
        "An old opaque compiler verdict accredited the current mandatory modification");
    view.reopen();preserved();
    // The export deliberately contains a lone CR immediately after a //
    // comment to test exact historical preservation. MSVC treats that Mac
    // newline as part of the comment and loses the following #include. Make
    // an explicit source edit before asking the real compiler for new proof;
    // importing must never silently rewrite that historical draft.
    auto solution=active.initial_code;const auto literal=solution.find("Zona inicial");
    require(literal!=solution.npos,"The imported current compiler lost its authored initial literal");
    solution.replace(literal,std::string("Zona inicial").size(),"Caja recibida");edited_source=solution;
    const auto replacement=wide(solution);const auto source=view.control(goal->block,10);
    SendMessageW(source,EM_SETSEL,0,-1);SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(replacement.c_str()));pump(view.host);
    preserved();view.click(block_id(goal->block,11));
    wait_for(view.host,[&]{return IsWindowEnabled(view.control(goal->block,11));},"The integrated imported modification did not execute");
    require(edit_text(view.control(goal->block,14))=="Caja recibida"&&edit_text(view.control(goal->block,4)).starts_with("✓ Correcto")
        &&windows::compiler_puzzle_completed(view.saved(),current,active),
        "A fresh real execution failed to accredit the useful current compiler after import: stdout="
        +edit_text(view.control(goal->block,14))+"; feedback="+edit_text(view.control(goal->block,4))
        +"; status="+edit_text(view.control(goal->block,34)));
    view.reopen();preserved();require(edit_text(view.control(goal->block,4)).starts_with("✓ Correcto"),
        "The imported current objective lost its persistent fresh success");
    std::cout<<"PASS published export to integrated U1: all four archived compiler/consigna/reference payloads conserved, independent drafts preserved, retired HWND absent, opaque success uncredited and fresh current execution retained\n";
}

} // namespace

int main(int argc,char** argv) {
    if(argc>1){
        const bool prepare_export=argc==4&&std::string_view(argv[1])=="--prepare-stderr-export";
        const bool prepare_catalog=argc==4&&std::string_view(argv[1])=="--prepare-import-catalog";
        const bool verify_import=argc==5&&std::string_view(argv[1])=="--verify-stderr-import";
        if(!prepare_export&&!prepare_catalog&&!verify_import){std::cerr<<"Usage: import_view_tests [--prepare-stderr-export|--prepare-import-catalog <u8|u10> <safe-new-token>] or [--verify-stderr-import <u8|u10> <QA-token> <export-token>]\n";return 2;}
        try{if(verify_import)verify_stderr_import(argv[2],wide(argv[3]),wide(argv[4]));else if(prepare_export)prepare_stderr_export(argv[2],wide(argv[3]));else prepare_import_catalog(argv[2],wide(argv[3]));return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL isolated stderr import preparation: "<<error.what()<<'\n';return 1;}
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr);
    host.lpszClassName = L"LgaedImportViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        archived_json_comparison_preserves_every_value();
        imported_stdout_success_does_not_accredit_a_new_runtime_stderr_goal();
        imported_pre_retirement_state_opens_current_asset();
        View view;
        ExportFixture exported(view.environment.paths.projects / L"exportación & revisión", view.lesson);
        const auto initial_hashes = hashes(exported.folder);
        view.cancel_menu();
        view.click(104);
        require(!IsWindowEnabled(view.control(205)) && (GetWindowLongPtrW(view.control(204), GWL_STYLE) & ES_READONLY),
            "Import must start with a readonly summary and disabled confirmation");
        require(view.empty(), "Opening the import panel published active state");
        view.click(206); require(!descendant(view.view, 201) && view.empty(), "Cancel did not leave an untouched profile");
        view.reopen(); require(view.empty(), "Closing/reopening after Cancel made the empty profile ineligible");
        view.click(104); view.path(exported.folder / L"inexistente"); view.click(203);
        require(view.summary().starts_with("No se puede importar") && !IsWindowEnabled(view.control(205)) && view.empty(),
            "An invalid folder enabled confirmation or published state");
        require(hashes(exported.folder) == initial_hashes, "Opening/canceling/invalid review changed exported files");
        std::cout << "PASS Real import panel opening, cancel, invalid path and untouched-profile reopen\n" << std::flush;

        auto incompatible = exported.data;
        const auto& conceptual = view.lesson.pages[4].blocks[exported.conceptual];
        state::Response wrong; wrong.selected = 999;
        state::mark_response_reviewed(wrong, conceptual);
        state::put_response(incompatible, curriculum::exercise_key(view.lesson, conceptual), wrong);
        const auto incompatible_folder = view.environment.paths.projects / L"export-incompatible";
        export_preferences(incompatible_folder, incompatible);
        const auto incompatible_hashes = hashes(incompatible_folder);
        view.path(incompatible_folder); view.click(203);
        require(view.summary().starts_with("No se puede importar") && !IsWindowEnabled(view.control(205)) && view.empty(),
            "An incompatible reviewed choice was accepted or silently repaired");
        require(hashes(incompatible_folder) == incompatible_hashes, "Rejected reviewed state changed its source XML");
        no_pending_transaction(view);
        std::cout << "PASS Actual U1L2 stateLayout validation rejects reviewed out-of-range choices without publishing\n" << std::flush;

        view.path(exported.folder); view.click(203);
        require(IsWindowEnabled(view.control(205)) && view.summary().starts_with("Se verificaron") && view.empty(),
            "Valid preview failed, or review imported before confirmation");
        require(hashes(exported.folder) == initial_hashes, "Preview changed original export bytes");
        view.path(exported.folder / L"otra ruta");
        require(!IsWindowEnabled(view.control(205)) && view.empty(), "Changing the path retained a stale confirmed preview");
        view.path(exported.folder); view.click(203);
        require(IsWindowEnabled(view.control(205)) && view.empty(), "A new review did not restore an actionable preview");
        std::cout << "PASS Valid review is read-only and changing the path invalidates confirmation\n" << std::flush;

        write_file(exported.folder / L"files/cpp_projects/entrada.txt", " \tLeo\r\n2\r\n    ");
        const auto changed_hashes = hashes(exported.folder);
        view.click(205);
        require(view.summary().starts_with("No se ha completado") && !IsWindowEnabled(view.control(205)) && view.empty(),
            "A changed source was imported using a stale preview");
        require(hashes(exported.folder) == changed_hashes, "Refused confirmation changed source files");
        no_pending_transaction(view);
        view.click(203); require(IsWindowEnabled(view.control(205)), "Retry review did not recover after a source change");
        view.click(205);
        require(!view.empty() && view.summary().starts_with("Importación completada") &&
            !IsWindowEnabled(view.control(205)) && IsWindowEnabled(view.control(201)) &&
            (GetWindowLongPtrW(view.control(201), GWL_STYLE) & ES_READONLY) &&
            window_text(view.control(206)) == "Volver a la lección", "Successful confirmation did not complete the actual import flow");
        require(hashes(exported.folder) == changed_hashes, "Successful import changed the original export");
        for (const auto& [relative, hash] : changed_hashes)
            require(state::sha256(state::read_bytes(view.environment.paths.root / L"state/active/android" / relative)) == hash,
                "The transactional archive did not preserve an exported file exactly");
        no_pending_transaction(view);
        std::cout << "PASS Source mutation is refused, then a new review and explicit confirmation import atomically\n" << std::flush;

        view.click(206);
        require(!descendant(view.view, 201), "Returning to the lesson left the import controls active");
        assert_restored(view, exported);
        require(navigate_lesson_view(view.view,3),"Backward page navigation after import was blocked");
        pump(view.host); require(view.page() == 3, "Backward page navigation after import failed");
        require(navigate_lesson_view(view.view,4),"Forward page navigation after import was blocked");
        pump(view.host); assert_restored(view, exported);
        view.reopen(); assert_restored(view, exported);
        require(hashes(exported.folder) == changed_hashes, "Native lesson reopen changed the Android source export");
        std::cout << "PASS Real page-five restoration, independent legacy/JSON-v1 drafts, exact whitespace and unsubmitted answers\n" << std::flush;

        // Imported cached correctness remains opaque. Only the real native
        // execution below may create the persistent completed objective.
        const auto before_run = view.saved();
        const auto cached = state::get_response(before_run, curriculum::exercise_key(view.lesson,
            view.lesson.pages[4].blocks[exported.editors[1]]));
        require(cached && cached->correct && cached->answer == "Resultado antiguo", "Opaque historical compiler state was discarded");
        const auto original = exported.editors[0];
        const auto original_source = view.control(original,10);
        const auto compile = view.control(original, 11);
        view.click(block_id(original, 11));
        wait_for(view.host, [&] { return IsWindowEnabled(compile) && edit_text(view.control(original, 14)) != "Compilando…"; },
            "A freshly imported real source could not compile and execute");
        require(edit_text(view.control(original, 14)) == "Zona inicial" && edit_text(view.control(original, 4)).starts_with("✓ Correcto"),
            "A fresh imported source did not produce/check its real output");
        require(view.control(original,10) == original_source && descendant(view.view,block_id(exported.editors[1],10))
                && edit_text(view.control(exported.editors[1],10)) == visual_lf(exported.drafts[1].source),
            "Fresh execution failed to unlock the exact imported dependent draft or recreated its original editor");
        view.reopen(); assert_restored(view, exported,true);
        require(hashes(exported.folder) == changed_hashes, "Compiling the imported source changed the original export");
        std::cout << "PASS Imported C++ compiles freshly; native completion survives reopen while opaque cached correctness stays unverified\n"
            << "Native import UI integration: 6 groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Native import UI integration: " << error.what() << '\n';
        return 1;
    }
}

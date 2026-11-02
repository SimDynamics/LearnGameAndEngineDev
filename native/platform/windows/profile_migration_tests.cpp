#include "platform/windows/profile_migration.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using Path=std::filesystem::path;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function>void rejects(Function&& action){
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    check(rejected,"An unsafe/incompatible migration was accepted");
}
void write(const Path& file,std::string_view bytes){
    state::require_safe_path(file);std::filesystem::create_directories(state::native_io_path(file.parent_path()));
    std::ofstream output(state::native_io_path(file),std::ios::binary|std::ios::trunc);
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output),"Cannot prepare the isolated migration fixture");
}
void write(const Path& file,const std::vector<std::uint8_t>& bytes){
    write(file,std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
}
struct LegacyLease {
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit LegacyLease(const Path& profile){
        std::filesystem::create_directories(state::native_io_path(profile/L"state"));
        handle=CreateFileW(state::native_io_path(profile/L"state/writer.lock").c_str(),
            GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(handle!=INVALID_HANDLE_VALUE,"Cannot hold the caller's legacy writer lease");
        state::require_unredirected_handle(handle,profile/L"state/writer.lock");
    }
    ~LegacyLease(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
ProfileMigrationResult migrate(const Path& legacy,const Path& destination){
    const LegacyLease lease(legacy);return migrate_profile_state(legacy,destination);
}
state::StateData sample(){
    state::StateData data;
    auto& progress=data.preferences["learning_progress"];
    progress["lesson_original__id_page"]=std::int64_t{7};
    progress["opaque_completed_response"]=std::string(R"({"correct":true,"answer":"literal  ","reviewedAnswerIdentity":"original hash","contentFingerprint":"unchanged","attempts":[false,true],"unknown":{"retained":1234567890123456789},"responses":{"point":{"type":"graph","points":[{"x":3,"y":0}],"interactionDraft":{"camera":{"x":17,"y":-12,"zoom":2}}}}})");
    progress["opaque_partial_response"]=std::string(R"({"correct":false,"answer":"  parcial\n","unknown":true})");
    progress["lesson_original__id_state_lineage_v2_es"]=std::string(64,'a');
    data.preferences["native_windows"]["hearts"]=std::int64_t{3};
    data.preferences["appearance_settings"]["scale"]=std::int64_t{150};
    data.preferences["lesson_narration"]["kokoro_voice:es"]=std::string("em_santa");
    data.preferences["unknown_imported_domain"]["opaque_names"]=std::vector<std::string>{"original/key", "é漢字", "trailing  "};
    state::Draft draft;draft.source="    int main() {\n\treturn 0;\n}\n    ";
    draft.input=" \t\r\n";draft.selection_start=4;draft.selection_end=12;
    state::put_draft(data,"original-id/page/slot",draft);return data;
}
void save(const Path& profile,const state::StateData& data){
    state::StateStore store(profile);(void)store.load();store.save(data);
}
struct Fixture {
    Path legacy,destination;
    std::vector<std::uint8_t> original;
    std::vector<std::pair<Path,std::vector<std::uint8_t>>> files;
    explicit Fixture(const Path& root,std::wstring_view name):legacy(root/name/L"local"),destination(root/name/L"roaming"){
        save(legacy,sample());original=state::read_bytes(legacy/L"state/active/state.bin");
        for(const auto& [relative,text]:std::vector<std::pair<Path,std::string>>{
            {L"history/first.revision","immutable historical revision\n"},
            {L"android/shared_prefs/original.xml","<map><string name=\"opaque\">literal</string></map>"},
            {L"android/files/revisions/é漢字.cpp","// retained source\nint main(){return 0;}\n"},
            {L"import-manifest.txt","existing import manifest must remain exact\n"}}){
            write(legacy/L"state/active"/relative,text);
            files.emplace_back(relative,state::read_bytes(legacy/L"state/active"/relative));
        }
        std::filesystem::create_directories(legacy/L"state/active/empty-history");
        for(const auto relative:{L"cache/kokoro/existing.wav",L"models/existing.onnx",L"logs/old.log",L"projects/generated.obj"})
            write(legacy/relative,"outside state/active; never migrate or delete");
    }
    void source_unchanged()const{
        check(state::read_bytes(legacy/L"state/active/state.bin")==original,"Migration changed the source state bytes");
        for(const auto& [relative,bytes]:files)
            check(state::read_bytes(legacy/L"state/active"/relative)==bytes,"Migration changed a source archive/history file");
    }
    void copied()const{
        check(state::read_bytes(destination/L"state/active/state.bin")==original,"Migration re-encoded the original state");
        check(state::StateStore(destination).load()==sample(),"Migration changed responses, drafts, preferences or unknown domains");
        for(const auto& [relative,bytes]:files)
            check(state::read_bytes(destination/L"state/active"/relative)==bytes,"Migration omitted or changed an opaque archive/history file");
        check(std::filesystem::is_directory(destination/L"state/active/empty-history"),"Migration omitted an empty historical directory");
        for(const auto relative:{L"cache",L"models",L"logs",L"projects"})
            check(!std::filesystem::exists(destination/relative),"Migration copied data outside state/active");
        source_unchanged();
    }
};
void exact_copy_and_rerun(const Path& base){
    Fixture fixture(base,L"exact");
    const auto source_time=std::filesystem::last_write_time(fixture.legacy/L"state/active/state.bin");
    check(migrate(fixture.legacy,fixture.destination)==ProfileMigrationResult::migrated,"The first migration did not publish state");
    fixture.copied();
    const auto marker=fixture.destination/L"state/active"/Path(profile_migration_marker_name);
    const auto marker_before=state::read_bytes(marker);
    check(migrate(fixture.legacy,fixture.destination)==ProfileMigrationResult::destination_preserved,"Rerun did not preserve the published destination");
    fixture.copied();check(state::read_bytes(marker)==marker_before,"Rerun rewrote the migration marker");
    check(std::filesystem::last_write_time(fixture.legacy/L"state/active/state.bin")==source_time,"Migration touched the source state timestamp");
    auto newer=sample();newer.preferences["learning_progress"]["new_progress"]=std::string("keep this later answer");
    save(fixture.destination,newer);write(fixture.destination/L"state/active/history/later.revision","new destination history");
    write(fixture.destination/L"state/active/android/files/revisions/é漢字.cpp","legitimate later destination edit");
    const auto newer_bytes=state::read_bytes(fixture.destination/L"state/active/state.bin");
    check(migrate(fixture.legacy,fixture.destination)==ProfileMigrationResult::destination_preserved,"Rerun rejected later legitimate destination progress");
    check(state::read_bytes(fixture.destination/L"state/active/state.bin")==newer_bytes&&state::StateStore(fixture.destination).load()==newer,
        "Migration replaced or reset later destination progress");
    check(state::read_bytes(marker)==marker_before,"Later destination progress changed the source provenance marker");
    fixture.source_unchanged();
    std::cout<<"PASS exact state/drafts/opaque archives, empty history, excluded caches, source preservation, idempotence and later destination progress\n";
}
void source_changes(const Path& base){
    for(const auto mode:{L"state",L"history",L"added",L"deleted",L"directory"}){
        Fixture fixture(base,std::wstring(L"changed-")+mode);(void)migrate(fixture.legacy,fixture.destination);
        const auto destination_before=state::read_bytes(fixture.destination/L"state/active/state.bin");
        if(std::wstring_view(mode)==L"state"){auto changed=sample();changed.preferences["learning_progress"]["legacy_later"]=true;save(fixture.legacy,changed);}
        else if(std::wstring_view(mode)==L"history")write(fixture.legacy/L"state/active/history/first.revision","changed legacy bytes");
        else if(std::wstring_view(mode)==L"added")write(fixture.legacy/L"state/active/new-file.bin","added legacy bytes");
        else if(std::wstring_view(mode)==L"deleted")std::filesystem::remove(fixture.legacy/L"state/active/history/first.revision");
        else std::filesystem::create_directory(fixture.legacy/L"state/active/new-empty-directory");
        const auto changed_source=state::read_bytes(fixture.legacy/L"state/active/state.bin");
        rejects([&]{(void)migrate(fixture.legacy,fixture.destination);});
        check(state::read_bytes(fixture.destination/L"state/active/state.bin")==destination_before&&
            state::read_bytes(fixture.legacy/L"state/active/state.bin")==changed_source,"Rejecting source divergence changed one of the profiles");
    }
    std::cout<<"PASS whole-source manifest detects changes, additions, deletions and empty-directory divergence without replacing either profile\n";
}
void existing_profiles_and_empty_source(const Path& base){
    Fixture conflict(base,L"conflict");auto distinct=sample();distinct.preferences["native_windows"]["different"]=true;
    save(conflict.destination,distinct);const auto before=state::read_bytes(conflict.destination/L"state/active/state.bin");
    rejects([&]{(void)migrate(conflict.legacy,conflict.destination);});
    conflict.source_unchanged();check(state::read_bytes(conflict.destination/L"state/active/state.bin")==before,"Unmarked conflict overwrote a destination");
    const auto legacy=base/L"empty/local",destination=base/L"empty/roaming";
    check(migrate(legacy,destination)==ProfileMigrationResult::no_legacy_state&&!std::filesystem::exists(destination),"An absent source created new saved state");
    save(destination,distinct);const auto existing=state::read_bytes(destination/L"state/active/state.bin");
    check(migrate(legacy,destination)==ProfileMigrationResult::destination_preserved&&state::read_bytes(destination/L"state/active/state.bin")==existing,
        "An absent source did not preserve a valid existing destination");
    Fixture restored(base,L"source-gone");(void)migrate(restored.legacy,restored.destination);
    const auto relocated=restored.legacy.parent_path()/L"archived-local";std::filesystem::rename(restored.legacy,relocated);
    check(migrate(restored.legacy,restored.destination)==ProfileMigrationResult::destination_preserved,
        "A valid migrated destination required the old Windows user's source folder to survive");
    std::cout<<"PASS unmarked conflicts, absent source, existing destination and migration after the old source is no longer present\n";
}
void corruption_and_reserved_marker(const Path& base){
    for(const auto mode:{L"source",L"destination",L"marker",L"incomplete",L"reserved"}){
        Fixture fixture(base,std::wstring(L"corrupt-")+mode);
        const auto which=std::wstring_view(mode);
        if(which==L"destination"||which==L"marker"){
            (void)migrate(fixture.legacy,fixture.destination);
            const auto file=which==L"marker"?fixture.destination/L"state/active"/Path(profile_migration_marker_name):fixture.destination/L"state/active/state.bin";
            auto damaged=state::read_bytes(file);damaged.at(0)^=1;write(file,damaged);
            rejects([&]{(void)migrate(fixture.legacy,fixture.destination);});
            check(state::read_bytes(file)==damaged,"Rejecting corrupt destination/marker rewrote the corrupt evidence");fixture.source_unchanged();
        }else{
            if(which==L"source"){auto damaged=fixture.original;damaged.pop_back();write(fixture.legacy/L"state/active/state.bin",damaged);}
            else if(which==L"incomplete")std::filesystem::remove(fixture.legacy/L"state/active/state.bin");
            else write(fixture.legacy/L"state/active"/Path(profile_migration_marker_name),"reserved collision");
            rejects([&]{(void)migrate(fixture.legacy,fixture.destination);});
            check(!std::filesystem::exists(fixture.destination/L"state/active"),"A corrupt/incomplete source was published");
        }
    }
    std::cout<<"PASS corrupt source/destination/checksum marker, incomplete state and reserved source-name collision remain untouched\n";
}
void path_and_lock_guards(const Path& base){
    Fixture fixture(base,L"paths");
    rejects([&]{(void)migrate(fixture.legacy,fixture.legacy);});
    rejects([&]{(void)migrate(fixture.legacy,fixture.legacy/L"nested");});
    rejects([&]{(void)migrate_profile_state(Path(L"relative"),fixture.destination);});
    {
        const LegacyLease lease(fixture.legacy);
        state::StateStore old_writer(fixture.legacy);const auto data=old_writer.load();
        rejects([&]{old_writer.save(data);});
        check(migrate_profile_state(fixture.legacy,fixture.destination)==ProfileMigrationResult::migrated,
            "Migration could not read the source while its writer lock was held");
    }
    fixture.copied();
    Fixture normalized(base,L"case-and-separator");
    std::filesystem::rename(normalized.legacy/L"state/active/state.bin",normalized.legacy/L"state/active/state-temporary.bin");
    std::filesystem::rename(normalized.legacy/L"state/active/state-temporary.bin",normalized.legacy/L"state/active/STATE.BIN");
    const Path trailing_source(normalized.legacy.native()+L"\\"),trailing_destination(normalized.destination.native()+L"\\");
    rejects([&]{(void)migrate(trailing_source,normalized.legacy/L"nested");});
    check(migrate(trailing_source,trailing_destination)==ProfileMigrationResult::migrated,
        "Valid Windows casing or a final directory separator prevented migration");
    normalized.copied();
    check(migrate(trailing_source,trailing_destination)==ProfileMigrationResult::destination_preserved,
        "A case-preserved state filename prevented verified startup after migration");
    Fixture linked(base,L"reparse");const auto outside=base/L"reparse/outside";
    write(outside/L"preserve.bin","unrelated test-owned bytes");
    const auto link=linked.legacy/L"state/active/redirected";
    if(CreateSymbolicLinkW(link.c_str(),outside.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)){
        rejects([&]{(void)migrate(linked.legacy,linked.destination);});
        check(!std::filesystem::exists(linked.destination/L"state/active")&&
            state::read_bytes(outside/L"preserve.bin")==std::vector<std::uint8_t>({'u','n','r','e','l','a','t','e','d',' ','t','e','s','t','-','o','w','n','e','d',' ','b','y','t','e','s'}),
            "Rejecting a reparse point modified the outside target or published state");
    }else{
        const auto error=GetLastError();check(error==ERROR_PRIVILEGE_NOT_HELD||error==ERROR_INVALID_PARAMETER,
            "The isolated reparse fixture failed for an unexpected reason");
        std::cout<<"SKIP symlink construction unavailable for this user; production rejects reparse points\n";
    }
    fixture.source_unchanged();
    std::cout<<"PASS separated absolute paths, legacy writer exclusion and reparse safety where available\n";
}
}
int wmain(int argc,wchar_t** argv){
    std::cout<<std::unitbuf;
    try{
        ensure_winrt_apartment();check(argc==2,"Migration QA requires one existing isolated build/output directory");
        const Path parent=argv[1];check(parent.is_absolute()&&std::filesystem::is_directory(parent),"Migration QA output directory is missing");
        state::require_safe_path(parent);
        const auto base=parent/(L"profile-migration-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(base),"Migration QA requires a new private directory");
        exact_copy_and_rerun(base);source_changes(base);existing_profiles_and_empty_source(base);
        corruption_and_reserved_marker(base);path_and_lock_guards(base);return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL profile migration: "<<error.what()<<'\n';return 1;}
}

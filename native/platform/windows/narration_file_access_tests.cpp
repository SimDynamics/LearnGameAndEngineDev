#include "platform/windows/narration_file_access.hpp"
#include "native_build_config.hpp"

#include <winioctl.h>
#include <array>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
using namespace lgaed;using namespace lgaed::windows;
void check(bool value,std::string_view text){if(!value)throw std::runtime_error(std::string(text));}
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
void write_new(const std::filesystem::path& path,std::string_view text){
    state::require_safe_path(path);Handle file;file.value=CreateFileW(state::native_io_path(path).c_str(),GENERIC_WRITE,
        FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(file.value!=INVALID_HANDLE_VALUE,"Cannot create isolated recording-path fixture");
    DWORD written=0;check(WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size(),"Cannot write owned path fixture");
}
template<class Action>std::string error(Action&& action){try{action();}catch(const std::exception& failure){return failure.what();}return {};}
void owned_junction(const std::filesystem::path& link,const std::filesystem::path& target){
    // An NTFS mount-point junction needs no symbolic-link elevation. Both ends
    // are newly created test fixtures; no personal directory is involved.
    check(CreateDirectoryW(state::native_io_path(link).c_str(),nullptr)!=FALSE,"Cannot create owned junction directory");
    struct MountPoint {DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;WORD length=0,reserved=0;
        WORD substitute_offset=0,substitute_length=0,print_offset=0,print_length=0;std::array<wchar_t,8192> paths{};} data;
    const auto print=std::filesystem::absolute(target).lexically_normal().native(),substitute=L"\\??\\"+print;
    check(substitute.size()+print.size()+2<data.paths.size(),"Owned junction target is too long");
    data.substitute_length=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
    data.print_offset=static_cast<WORD>(data.substitute_length+sizeof(wchar_t));data.print_length=static_cast<WORD>(print.size()*sizeof(wchar_t));
    data.length=static_cast<WORD>(8+data.print_offset+data.print_length+sizeof(wchar_t));
    std::copy(substitute.begin(),substitute.end(),data.paths.begin());std::copy(print.begin(),print.end(),data.paths.begin()+substitute.size()+1);
    Handle directory;directory.value=CreateFileW(state::native_io_path(link).c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(directory.value!=INVALID_HANDLE_VALUE,"Cannot open owned junction metadata");DWORD returned=0;
    check(DeviceIoControl(directory.value,FSCTL_SET_REPARSE_POINT,&data,8+data.length,nullptr,0,&returned,nullptr)!=FALSE,"Cannot set owned NTFS junction");
}
}
int main()try{
    // Only these new files below out/qa are accessed. The redirected-path
    // scenarios are models, not a claim of Windows audio playback or MSIX QA.
    const auto root=std::filesystem::path(build::source_root)/L"out/qa"/
        (L"narration-file-access-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    state::require_safe_path(root);
    const std::wstring id=L"recording-path-owned";
    WindowsEnvironment qa{{ProfileId::Codex,id,false},make_profile_paths(root/L"logical",ProfileId::Codex,id),false};
    const auto physical=make_profile_paths(root/L"physical",ProfileId::Codex,id);
    std::filesystem::create_directories(qa.paths.cache/L"kokoro");std::filesystem::create_directories(physical.cache/L"kokoro");
    std::filesystem::create_directories(qa.paths.progress);
    const auto requested=qa.paths.cache/L"kokoro/owned.wav",actual=physical.cache/L"kokoro/owned.wav";
    const auto sentinel=qa.paths.progress/L"untouched-model-progress.bin";
    write_new(requested,"RIFF owned model recording, never decoded or played");write_new(actual,"RIFF physical model recording, never decoded or played");
    write_new(sentinel,"QA-only progress sentinel\r\n");
    const auto sentinel_hash=state::sha256(state::read_bytes(sentinel));const auto requested_hash=state::sha256(state::read_bytes(requested));
    const auto actual_hash=state::sha256(state::read_bytes(actual));
    Handle recording;recording.value=CreateFileW(state::native_io_path(actual).c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);check(recording.value!=INVALID_HANDLE_VALUE,"Cannot open model physical recording");
    check(!error([&]{state::require_unredirected_handle(recording.value,requested);}).empty(),
        "The old unconditional physical guard did not reproduce redirected WAV rejection");
    check(narration_recording_location_allowed(qa,requested,actual,physical.cache),
        "A valid isolated QA did not permit the exact relative WAV in its own resolved cache");
    auto personal=qa;personal.options.qa_instance.reset();
    check(!narration_recording_location_allowed(personal,requested,actual,physical.cache),"A personal profile borrowed the QA redirection exception");
    const auto personal_error=error([&]{(void)require_narration_recording_handle(recording.value,requested,personal);});
    check(personal_error.find("WAV")!=std::string::npos&&personal_error.find("progreso")==std::string::npos,
        "The resource diagnostic still misidentified a recording as the learner's progress");
    auto unscoped=qa;unscoped.options.qa_instance=L"../escape";
    check(!narration_recording_location_allowed(unscoped,requested,actual,physical.cache),"An invalid QA identifier enabled redirected access");
    unscoped=qa;unscoped.paths.cache=qa.paths.root/L"progress";
    check(!narration_recording_location_allowed(unscoped,unscoped.paths.cache/L"kokoro/owned.wav",actual,physical.cache),
        "A non-cache profile directory acquired the QA audio exception");
    const auto other=make_profile_paths(root/L"physical",ProfileId::Codex,L"another-qa");
    check(!narration_recording_location_allowed(qa,requested,other.cache/L"kokoro/owned.wav",physical.cache),"Another QA borrowed the owned cache's physical root");
    check(!narration_recording_location_allowed(qa,requested,physical.root/L"other-cache/kokoro/owned.wav",physical.cache),"Another physical cache borrowed the QA exception");
    check(!narration_recording_location_allowed(qa,requested,physical.cache/L"kokoro/different.wav",physical.cache),"A different physical filename borrowed a logical WAV handle");
    check(!narration_recording_location_allowed(qa,requested,physical.cache,physical.cache),"A missing physical file suffix was accepted");
    check(!narration_recording_location_allowed(qa,qa.paths.cache.parent_path()/L"cache-sibling/owned.wav",actual,physical.cache),
        "A lexical directory-prefix collision was accepted as the cache");
    check(!narration_recording_location_allowed(qa,qa.paths.cache/L"../progress/owned.wav",actual,physical.cache),
        "Traversal outside the QA cache retained its recording exception");
    check(!narration_recording_location_allowed(qa,qa.paths.cache/L"kokoro/owned.wav:other",physical.cache/L"kokoro/owned.wav:other",physical.cache),
        "An alternate data stream acquired the redirected QA cache exception");
    check(!narration_recording_location_allowed(qa,requested,actual,{}),"An unresolved physical cache was guessed");
    check(!narration_recording_location_allowed(qa,L"relative.wav",actual,physical.cache),"A relative recording acquired an absolute-cache exception");
    // The actual cache handle determines its physical root; a supplied logical
    // name alone cannot smuggle an unrelated open handle past the real guard.
    check(!error([&]{(void)require_narration_recording_handle(recording.value,requested,qa);}).empty(),
        "The real guard trusted a model physical directory instead of resolving its own cache handle");
    Handle local;local.value=CreateFileW(state::native_io_path(requested).c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);check(local.value!=INVALID_HANDLE_VALUE,"Cannot open unredirected model WAV");
    auto lease=require_narration_recording_handle(local.value,requested,qa);
    auto strict=require_narration_recording_handle(local.value,requested,personal);
    check(!lease&&!strict,"Unredirected WAV unexpectedly required a QA-only cache lease");
    check(!MoveFileExW(state::native_io_path(requested).c_str(),state::native_io_path(qa.paths.cache/L"kokoro/renamed.wav").c_str(),0),
        "A recording could be replaced between validation and playback while its read handle was held");
    Handle directory;directory.value=CreateFileW(state::native_io_path(qa.paths.cache).c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(directory.value!=INVALID_HANDLE_VALUE,"Cannot open isolated cache directory");
    check(!error([&]{(void)require_narration_recording_handle(directory.value,qa.paths.cache,qa);}).empty(),"A directory was accepted as a WAV recording");
    check(!error([&]{(void)require_narration_recording_handle(INVALID_HANDLE_VALUE,requested,qa);}).empty(),"An invalid recording handle was accepted");
    Handle pipe_read,pipe_write;check(CreatePipe(&pipe_read.value,&pipe_write.value,nullptr,0)!=FALSE,"Cannot create isolated nondisk handle fixture");
    check(!error([&]{(void)require_narration_recording_handle(pipe_read.value,requested,qa);}).empty(),"A pipe was accepted as a disk WAV recording");
    const auto junction=qa.paths.cache/L"redirected";owned_junction(junction,physical.cache/L"kokoro");
    Handle through_junction;through_junction.value=CreateFileW(state::native_io_path(junction/L"owned.wav").c_str(),GENERIC_READ,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    check(through_junction.value!=INVALID_HANDLE_VALUE,"Cannot open the owned junction's existing test recording");
    check(!error([&]{(void)require_narration_recording_handle(through_junction.value,junction/L"owned.wav",qa);}).empty(),
        "An isolated QA flag bypassed the existing reparse-point prohibition");
    check(state::sha256(state::read_bytes(sentinel))==sentinel_hash&&state::sha256(state::read_bytes(requested))==requested_hash&&
        state::sha256(state::read_bytes(actual))==actual_hash,"Read-only recording validation changed progress fixtures or recording bytes");
    {
        // The same native environment API as the reader, with a NEW QA only.
        // No personal profile is prepared, imported, loaded or reset here.
        const auto instance=L"recording-handle-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        const auto live=resolve_windows_environment({ProfileId::Codex,instance,false});
        check(live.options.qa_instance==instance,"The native environment lost its unique QA owner");
        const auto folder=live.paths.cache/L"recording-path-probe";state::require_safe_path(folder);
        std::filesystem::create_directories(folder);const auto file=folder/L"owned-empty-model.wav";
        write_new(file,"RIFF QA-only path probe; no audio samples, decoding or playback");
        const auto before=state::sha256(state::read_bytes(file));
        Handle live_recording;live_recording.value=CreateFileW(state::native_io_path(file).c_str(),GENERIC_READ,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        check(live_recording.value!=INVALID_HANDLE_VALUE,"Cannot open the new native QA's owned model WAV");
        const auto resolved=narration_file_detail::final_path(live_recording.value);
        const bool redirected=!narration_file_detail::same(narration_file_detail::normalized(file),narration_file_detail::normalized(resolved));
        const auto pinned_cache=require_narration_recording_handle(live_recording.value,file,live);
        check(bool(pinned_cache)==redirected,"The real QA file guard did not retain precisely its required physical-cache lease");
        check(state::sha256(state::read_bytes(file))==before,"The real QA file guard changed its model recording");
        std::cout<<"PASS new resolve_windows_environment QA: "<<(redirected
            ?"actual WAV redirection accepted under its handle-resolved own cache"
            :"strict same-path WAV accepted; this native process did not exercise MSIX redirection")
            <<"; no samples decoded or played, no personal state accessed\n";
    }
    std::cout<<"PASS isolated WAV guards: redirected-QA location model, strict personal handles, exact own cache/suffix, traversal/foreign handles/file types, held-file replacement rejection and unchanged fixtures. No audio/UI/MSIX-playback claim.\n";
    return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}

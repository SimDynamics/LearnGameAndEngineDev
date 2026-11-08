#include "platform/windows/narration_player.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_file_access.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "core/narration_speed.hpp"

#include <windows.h>
#include <mmdeviceapi.h>
#include <winrt/base.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string hresult_hex(std::int32_t value){
    std::ostringstream text;text<<"0x"<<std::hex<<std::uppercase<<std::setw(8)<<std::setfill('0')<<static_cast<std::uint32_t>(value);return text.str();
}
void print_failure(std::ostream& output,const lgaed::windows::NarrationPlayerFailure& diagnostic){
    output<<"MediaFailed.Error="<<(diagnostic.error?std::to_string(*diagnostic.error):"<unavailable>")
        <<"\nMediaFailed.ExtendedErrorCode="<<(diagnostic.extended_error_code?hresult_hex(*diagnostic.extended_error_code):"<unavailable>")
        <<"\nMediaFailed.ErrorMessage=";
    if(diagnostic.error_message)output<<std::quoted(*diagnostic.error_message);else output<<"<unavailable>";
    output<<'\n';
}
void print_endpoint(IMMDevice* device){
    DWORD state=0;const auto state_result=device->GetState(&state);
    std::cout<<" GetState="<<hresult_hex(state_result);if(SUCCEEDED(state_result))std::cout<<" state="<<state;
    struct EndpointId{LPWSTR value=nullptr;~EndpointId(){CoTaskMemFree(value);}} id;
    const auto id_result=device->GetId(&id.value);std::cout<<" GetId="<<hresult_hex(id_result);
    if(SUCCEEDED(id_result)&&id.value)std::cout<<" id="<<std::quoted(winrt::to_string(std::wstring_view(id.value)));
    std::cout<<'\n';
}
void print_render_endpoints(){
    // Query the real render endpoints in this process. An inventory failure is
    // reported as such; it must neither become a zero count nor skip playback.
    try{
        lgaed::windows::ensure_winrt_apartment();winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        const auto created=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(enumerator.put()));
        std::cout<<"INFO MMDeviceEnumerator.CoCreateInstance="<<hresult_hex(created)<<'\n';if(FAILED(created))return;
        winrt::com_ptr<IMMDeviceCollection> active;
        const auto listed=enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,active.put());
        std::cout<<"INFO MMDevice.EnumAudioEndpoints(eRender,ACTIVE)="<<hresult_hex(listed)<<'\n';
        if(SUCCEEDED(listed)){
            UINT count=0;const auto counted=active->GetCount(&count);
            std::cout<<"INFO MMDevice.active.GetCount="<<hresult_hex(counted);if(SUCCEEDED(counted))std::cout<<" count="<<count;std::cout<<'\n';
            if(SUCCEEDED(counted))for(UINT i=0;i<count;++i){
                winrt::com_ptr<IMMDevice> device;const auto item=active->Item(i,device.put());
                std::cout<<"INFO MMDevice.active["<<i<<"].Item="<<hresult_hex(item);if(SUCCEEDED(item))print_endpoint(device.get());else std::cout<<'\n';
            }
        }
        struct Role{ERole value;const char* name;};
        for(const auto role:{Role{eConsole,"eConsole"},Role{eMultimedia,"eMultimedia"},Role{eCommunications,"eCommunications"}}){
            winrt::com_ptr<IMMDevice> device;const auto result=enumerator->GetDefaultAudioEndpoint(eRender,role.value,device.put());
            std::cout<<"INFO MMDevice.GetDefaultAudioEndpoint(eRender,"<<role.name<<")="<<hresult_hex(result);
            if(SUCCEEDED(result))print_endpoint(device.get());else std::cout<<'\n';
        }
    }catch(const winrt::hresult_error& error){std::cout<<"INFO MMDevice inventory unavailable: "<<hresult_hex(error.code().value)<<" "<<winrt::to_string(error.message())<<'\n';}
    catch(const std::exception& error){std::cout<<"INFO MMDevice inventory unavailable: "<<error.what()<<'\n';}
}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void wait(lgaed::windows::NarrationPlayer& player,const std::function<bool()>& done,
    const char* timeout_message="The real Windows media player did not start the isolated silent recording"){
    const auto until=GetTickCount64()+10000;
    do{pump();player.tick();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<until);
    throw std::runtime_error(timeout_message);
}
std::vector<std::uint8_t> silent_wav(unsigned seconds=12){
    const std::uint32_t samples=24000*seconds,data_bytes=samples*2;
    std::vector<std::uint8_t> bytes(44+data_bytes);
    const auto word=[&](std::size_t at,std::uint32_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(8*i));};
    std::memcpy(bytes.data(),"RIFF",4);word(4,data_bytes+36,4);std::memcpy(bytes.data()+8,"WAVEfmt ",8);
    word(16,16,4);word(20,1,2);word(22,1,2);word(24,24000,4);word(28,48000,4);word(32,2,2);word(34,16,2);
    std::memcpy(bytes.data()+36,"data",4);word(40,data_bytes,4);return bytes;
}
void observe_clock(lgaed::windows::NarrationPlayer& player,double rate){
    require(player.ready()&&std::abs(player.playback_rate()-rate)<1e-6,"The actual MediaPlaybackSession did not accept the selected playback rate");
    const auto media=player.position();const auto wall=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now()-wall<std::chrono::milliseconds(500)){
        pump();player.tick();MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall).count();
    const auto measured=(player.position()-media)/elapsed;
    std::cout<<"INFO real MediaPlaybackSession rate="<<player.playback_rate()<<" media/wall="<<measured<<'\n';
    require(!player.ended()&&std::abs(measured-rate)<.30,"The real media clock still advanced at another playback speed");
}
void qa_path_playback(){
    using namespace lgaed;using namespace lgaed::windows;
    // Only a newly resolved QA and a valid zero-filled WAV. No voices, models,
    // learner state, windows or global audio settings are used by this test.
    const auto instance=L"silent-uri-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto environment=resolve_windows_environment({ProfileId::Codex,instance,false});
    const auto folder=environment.paths.cache/L"silent-uri-test";state::require_safe_path(folder);std::filesystem::create_directories(folder);
    const auto file=folder/L"six-seconds-zero-pcm.wav";const auto bytes=silent_wav(6);
    require(std::all_of(bytes.begin()+44,bytes.end(),[](std::uint8_t value){return value==0;}),"The silent path fixture contains a nonzero sample");
    struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    {
        Handle writer;writer.value=CreateFileW(state::native_io_path(file).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(writer.value!=INVALID_HANDLE_VALUE,"Cannot create the new QA's silent PCM fixture");
        DWORD written=0;require(WriteFile(writer.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(writer.value),
            "Cannot write the new QA's complete silent WAV");
    }
    const auto original_hash=state::sha256(state::read_bytes(file));
    Handle recording;recording.value=CreateFileW(state::native_io_path(file).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);require(recording.value!=INVALID_HANDLE_VALUE,"Cannot open the new QA's silent WAV for verification");
    const auto actual=narration_file_detail::final_path(recording.value);
    const bool redirected=!narration_file_detail::same(narration_file_detail::normalized(file),narration_file_detail::normalized(actual));
    const auto cache_lease=require_narration_recording_handle(recording.value,file,environment);
    require(bool(cache_lease)==redirected,"The actual recording/cache handle lease disagrees with the observed QA redirection");
    const auto utf8=[](const std::filesystem::path& path){const auto text=path.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());};
    std::cout<<"INFO silent QA URI requested="<<utf8(file)<<"\nINFO verified WAV physical="<<utf8(actual)
        <<"\nINFO redirected="<<redirected<<" pinned_cache="<<bool(cache_lease)<<" wav_sha256="<<original_hash<<'\n';
    NarrationPlayer first;first.play(file,300,1.);
    wait(first,[&]{return first.ready()&&first.position()>450;});
    require(!first.ended(),"The silent logical URI ended before a real playback position was observed");
    const auto pause=first.release();require(!first.allocated()&&pause>450&&pause<3000,"Closing the initial decoder lost its real pause position");
    const auto paused_position=first.position();first.tick();require(first.position()==paused_position,"A released decoder kept advancing its clock");
    NarrationPlayer reopened;reopened.play(file,pause,1.);
    wait(reopened,[&]{return reopened.ready()&&reopened.position()>pause+100;});
    reopened.seek(3000);wait(reopened,[&]{return reopened.ready()&&reopened.position()>3150;});
    const auto released=reopened.release();require(!reopened.allocated()&&released>=3150&&released<6000,
        "Seeking or reopening the silent logical URI lost its real recording position");
    require(state::read_bytes(file)==bytes&&state::sha256(state::read_bytes(file))==original_hash,
        "Logical-URI playback changed or regenerated the owned silent recording");
    require(!std::filesystem::exists(environment.paths.root/L"state"),"The silent playback test created a learner state store");
    std::cout<<"PASS "<<(redirected?"actually redirected":"unredirected only (MSIX redirection not exercised)")
        <<" QA WAV: verified handles -> logical file URI -> real clock -> release -> reopened offset -> seek -> release; same zero PCM/hash, no voice/UI/personal state\n";
}
void failure_diagnostic(){
    using namespace lgaed;using namespace lgaed::windows;
    const auto instance=L"invalid-media-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto environment=resolve_windows_environment({ProfileId::Codex,instance,false});
    const auto folder=environment.paths.cache/L"failure-diagnostic-test";state::require_safe_path(folder);std::filesystem::create_directories(folder);
    const auto file=folder/L"deliberately-invalid.wav";
    constexpr std::string_view contents="LGAED isolated invalid media. These bytes are not a WAV, PCM or a voice recording.\n";
    const std::vector<std::uint8_t> bytes(contents.begin(),contents.end());
    {
        struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} writer;
        writer.value=CreateFileW(state::native_io_path(file).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(writer.value!=INVALID_HANDLE_VALUE,"Cannot create the new QA's invalid-media fixture");
        DWORD written=0;require(WriteFile(writer.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(writer.value),
            "Cannot write the new QA's complete invalid-media fixture");
    }
    const auto original_hash=state::sha256(state::read_bytes(file));
    NarrationPlayer player;player.play(file,750,2.5);
    std::optional<NarrationPlayerFailure> observed;
    try{wait(player,[]{return false;},"The real invalid-media attempt did not deliver a MediaFailed event before the timeout");}
    catch(const NarrationPlayerError& error){
        require(std::string_view(error.what())=="Windows no pudo reproducir la grabación de voz.","A diagnostic changed the public playback-failure message");
        observed=error.diagnostic();print_failure(std::cout,*observed);
    }
    require(observed.has_value(),"The real invalid-media attempt did not deliver a captured MediaFailed event");
    require(observed->error.has_value()&&observed->extended_error_code.has_value()&&observed->error_message.has_value(),
        "The real MediaFailed event lost an Error, ExtendedErrorCode or ErrorMessage property");
    // Values come from Windows. A particular error enum/HRESULT/message is not
    // prescribed here: lack of a render endpoint can fail this attempt too.
    bool repeated=false;
    try{player.tick();}catch(const NarrationPlayerError& error){repeated=true;require(error.diagnostic()==*observed,"A later tick changed the captured MediaFailed snapshot");}
    require(repeated,"A second tick lost the outstanding real MediaFailed event");
    const auto released=player.release();require(!player.allocated(),"A failed player retained its decoder after release");
    player.tick();require(player.position()==released,"The released failed decoder changed its saved offset");
    require(state::read_bytes(file)==bytes&&state::sha256(state::read_bytes(file))==original_hash,
        "The failed media attempt changed or regenerated the isolated input bytes");
    require(!std::filesystem::exists(environment.paths.root/L"state"),"The failure diagnostic created a learner state store");
    std::cout<<"PASS real MediaFailed properties retained across ticks; released decoder, unchanged invalid bytes/hash, no learner state; no playback or narration credit\n";
}
}
int main(int argc,char** argv){
    try{
        std::cout<<std::unitbuf;require(argc==2,"Supply an isolated QA output directory");
        print_render_endpoints();
        if(std::string_view(argv[1])=="--qa-path"){qa_path_playback();return 0;}
        if(std::string_view(argv[1])=="--failure-diagnostic"){failure_diagnostic();return 0;}
        using namespace lgaed;
        require(narration::playback_rates==std::array{.5,1.,1.5,2.,2.5,3.},"Playback options lost an existing rate or either requested fast rate");
        for(const auto value:narration::playback_rates)require(narration::clamp_playback_rate(value)==value,"A valid playback rate was changed by restoration");
        require(narration::clamp_playback_rate(-1)==.5&&narration::clamp_playback_rate(8)==3.&&
            narration::clamp_playback_rate(std::numeric_limits<double>::quiet_NaN())==1.&&
            narration::clamp_playback_rate(std::numeric_limits<double>::infinity())==1.,"Invalid playback rates escaped finite native limits");
        const auto root=std::filesystem::absolute(argv[1])/("rates-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        require(!std::filesystem::exists(root),"Playback QA may not replace an existing directory");std::filesystem::create_directories(root);
        const auto file=root/"silent-playback.wav";const auto bytes=silent_wav();
        {std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));require(static_cast<bool>(output),"Cannot save the private silent PCM fixture");}
        windows::NarrationPlayer player;player.play(file,1000,2.5);
        wait(player,[&]{return player.ready()&&player.position()>1250;});observe_clock(player,2.5);
        player.set_rate(3.);const auto changed=player.position();wait(player,[&]{return player.position()>changed+250;});observe_clock(player,3.);
        for(const auto value:narration::playback_rates){player.set_rate(value);require(std::abs(player.playback_rate()-value)<1e-6,"Changing a live rate did not reach MediaPlaybackSession");}
        const auto offset=player.release();require(!player.allocated()&&offset>1000,"Rate changes lost the actual released playback offset");
        player.play(file,offset,3.);wait(player,[&]{return player.ready()&&player.position()>offset+150;});
        require(player.playback_rate()==3.&&player.release()>=offset,"A resumed recording lost its selected3X rate or saved media position");
        require(state::read_bytes(file)==bytes,"Playback speed changed or regenerated the original silent recording");
        std::cout<<"PASS six playback rates: real2.5X/3X media clocks, live native rates, exact released/resumed recording and unchanged PCM; no Kokoro synthesis\n";return 0;
    }catch(const lgaed::windows::NarrationPlayerError& error){std::cerr<<"Playback rate QA failed: "<<error.what()<<'\n';print_failure(std::cerr,error.diagnostic());return 1;}
    catch(const winrt::hresult_error& error){std::cerr<<"Playback rate QA failed: "<<hresult_hex(error.code().value)<<" "<<winrt::to_string(error.message())<<'\n';return 1;}
    catch(const std::exception& error){std::cerr<<"Playback rate QA failed: "<<error.what()<<'\n';return 1;}
}

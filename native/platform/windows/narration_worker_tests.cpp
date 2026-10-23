#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <future>
#include <fstream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
using winrt::Windows::Data::Json::JsonObject;
JsonObject json(std::string_view value) { return JsonObject::Parse(winrt::to_hstring(value)); }
void cancelled_inflight_protocol(const std::filesystem::path& root,const std::filesystem::path& python,const std::filesystem::path& build_directory){
    using namespace lgaed;
    check(std::filesystem::is_regular_file(python)&&std::filesystem::is_directory(build_directory),"Protocol test requires its configured Python and existing build directory");
    const auto runtime=build_directory/L"narration-protocol-fixture-runtime.json";
    JsonObject config;config.Insert(L"java",winrt::Windows::Data::Json::JsonValue::CreateStringValue(python.native()));
    winrt::Windows::Data::Json::JsonArray arguments;
    arguments.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"-u"));
    arguments.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue((root/L"native/platform/windows/narration_worker_protocol_fixture.py").native()));
    config.Insert(L"arguments",arguments);
    {std::ofstream output(runtime,std::ios::binary|std::ios::trunc);check(static_cast<bool>(output),"Cannot write controlled protocol configuration");output<<winrt::to_string(config.Stringify());check(static_cast<bool>(output),"Cannot save controlled protocol configuration");}
    windows::NarrationWorker worker(runtime);std::stop_source stopped;
    auto calculation=std::async(std::launch::async,[&]{
        try{(void)worker.math_calculate(R"({"latex":"1+1"})",stopped.get_token());return false;}
        catch(const std::exception& error){return std::string_view(error.what())=="Lectura cancelada";}
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool observed=false;
    do{observed=json(worker.math_editor(R"({"action":"probe"})")).GetNamedBoolean(L"observed");}
    while(!observed&&std::chrono::steady_clock::now()<deadline);
    check(observed,"Controlled fixture never observed the in-flight request");
    stopped.request_stop();
    const bool returned_before_release=calculation.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;
    const auto cancellation=json(worker.math_editor(R"({"action":"probe"})"));
    const auto release=json(worker.math_editor(R"({"action":"release"})"));
    const bool rejected=calculation.get();
    check(returned_before_release,"An in-flight mathematical cancellation waited for its explicitly withheld response");
    check(rejected&&cancellation.GetNamedBoolean(L"cancelled"),"Mathematical cancellation did not reach the intended in-flight request");
    check(release.GetNamedBoolean(L"released"),"A late cancelled reply was routed to the release request");
    for(const auto* marker:{L"next-request-one",L"next-request-two"}){
        JsonObject request;request.Insert(L"action",winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"echo"));request.Insert(L"marker",winrt::Windows::Data::Json::JsonValue::CreateStringValue(marker));
        check(json(worker.math_editor(winrt::to_string(request.Stringify()))).GetNamedString(L"marker")==marker,"A late cancelled reply corrupted subsequent request identities");
    }
    std::cout<<"PASS controlled IPC in-flight mathematical cancellation, withheld late reply and subsequent request identities (not an audio benchmark)\n";
}
}

int wmain(int argc,wchar_t** argv) {
    using namespace lgaed;
    try {
        windows::ensure_winrt_apartment();
        const auto root = std::filesystem::path(build::source_root);
        check(argc==3,"The narration test requires CMake's Python executable and existing build directory arguments");
        cancelled_inflight_protocol(root,argv[1],argv[2]);
        windows::NarrationWorker worker(root / "out/narration/runtime.json");
        const std::string source = "La línea #include y std::cout muestran una salida.";
        const auto segments = worker.plan(source,"es");
        check(segments.size() == 1 && segments.front().text == source,"Worker lost the original source sentence");
        const auto& segment = segments.front();
        check(!segment.spoken.empty() && segment.spoken_ranges.size() == segment.displayed_ranges.size() &&
              segment.spoken_ranges.size() > 5,"Worker omitted real UTF16 source projections");
        const auto identity = worker.identity("native-real-kokoro-page","es","ef_dora",segments);
        check(identity.size() == 64 && worker.identity("native-real-kokoro-page","es","ef_dora",segments) == identity,
              "Native bridge changed the original resume identity");
        check(worker.identity("another-native-page","es","ef_dora",segments) != identity,
              "Different pages share their original resume identity");
        check(worker.identity("native-real-kokoro-page","es","ef_dora",segments,{},"ANDROID") != identity,
              "The original system-voice identity collided with Kokoro");
        const auto cache = root / "out/qa/narration-native-tests";
        const auto audio = worker.prepare(segment,"es","ef_dora",cache,2);
        check(audio.source == source && audio.duration_ms > 200 && audio.key.size() == 64,
              "Real Kokoro audio lost its original source or cache key");
        check(audio.timings.size() > 5,"Real Kokoro did not return measured word timings");
        double previous = -1; std::size_t last_source = 0;
        for (const auto& row : audio.timings) {
            check(row.start < row.end && row.end <= winrt::to_hstring(source).size(),"Word timing is outside the original source");
            check(std::isfinite(row.start_ms) && std::isfinite(row.end_ms) && row.start_ms >= previous &&
                  row.start_ms < row.end_ms && row.end_ms <= audio.duration_ms,"Word timing is outside real playback");
            previous = row.start_ms; last_source = std::max(last_source,row.end);
        }
        check(last_source == winrt::to_hstring(source).size(),"Technical symbols left the sentence unfinished");
        const auto before = state::read_bytes(audio.wav);
        const auto timestamp = std::filesystem::last_write_time(audio.wav);
        const auto repeated = worker.prepare(segment,"es","ef_dora",cache,2);
        check(repeated.cache_hit && repeated.key == audio.key && repeated.wav == audio.wav,
              "The native client resynthesized a valid installed recording");
        check(state::read_bytes(repeated.wav) == before && std::filesystem::last_write_time(repeated.wav) == timestamp,
              "A cache hit modified the existing waveform");
        check(repeated.timings.size() == audio.timings.size(),"A cache hit lost its exact word timings");

        // Exact source JSON is deliberately not parsed by WinRT on its way to
        // the original rational evaluator: these adjacent integers exceed 2^53.
        constexpr std::string_view block = R"json({"type":"math_puzzle","schemaVersion":1,"prompt":"Conserva el entero exacto.","responses":[{"id":"number","type":"math_numeric_input","label":"Número","initial":9007199254740993,"validator":{"type":"numeric","expected":9007199254740993}}],"feedback":{"correct":"El entero coincide.","incorrect":"Comprueba la unidad final."}})json";
        const auto description_raw = worker.describe_math(block,"es");
        check(description_raw.find("9007199254740993") != std::string::npos,"UI shape rounded the original exact integer");
        check(description_raw.find("validator") == std::string::npos && description_raw.find("expected") == std::string::npos,
              "The visible mathematical description disclosed a hidden answer");
        const auto correct = json(worker.validate_math(block,R"json({"number":{"type":"text","value":"9007199254740993"}})json","es"));
        check(correct.GetNamedBoolean(L"evaluable") && correct.GetNamedBoolean(L"correct"),"Exact original mathematical answer failed through the native bridge");
        const auto wrong = json(worker.validate_math(block,R"json({"number":{"type":"text","value":"9007199254740992"}})json","es"));
        check(wrong.GetNamedBoolean(L"evaluable") && !wrong.GetNamedBoolean(L"correct"),"Adjacent exact integers were rounded into the same answer");
        const auto incomplete = json(worker.validate_math(block,"{}","fr"));
        check(!incomplete.GetNamedBoolean(L"evaluable") && !incomplete.GetNamedBoolean(L"correct"),"An incomplete answer was charged as a wrong evaluated answer");
        std::stop_source cancelled; cancelled.request_stop(); bool rejected = false;
        try { (void)worker.plan("Lectura cancelada.","es",cancelled.get_token()); }
        catch (const std::exception&) { rejected = true; }
        check(rejected,"A cancelled native request was executed");
        check(!worker.plan("La lectura continúa.","es").empty(),"Cancellation broke subsequent planning");
        {
            // One worker process must route out-of-order mathematical replies
            // without consuming an ONNX reply or rebuilding its shared engine.
            auto voice=std::async(std::launch::async,[&]{return worker.prepare(segment,"es","ef_dora",cache,2);});
            std::vector<std::future<std::string>> editors;
            for(int index=0;index<12;++index)editors.push_back(std::async(std::launch::async,[&,index]{
                return worker.math_editor("{\"editorId\":\"native-concurrent-"+std::to_string(index)+"\",\"action\":\"create\",\"latex\":\""+std::to_string(100+index)+"\"}");
            }));
            for(int index=0;index<12;++index){const auto editor=json(editors[index].get());
                check(editor.GetNamedString(L"latex")==winrt::to_hstring(std::to_string(100+index)),
                    "Concurrent mathematical replies were routed to another request");
                (void)worker.math_editor("{\"editorId\":\"native-concurrent-"+std::to_string(index)+"\",\"action\":\"close\"}");
            }
            const auto retained=voice.get();check(retained.cache_hit&&retained.key==audio.key&&retained.wav==audio.wav,
                "Editing mathematics changed or regenerated a compatible Kokoro recording");
            std::stop_source stopped;stopped.request_stop();bool cancelled_editor=false;
            try{(void)worker.math_editor(R"({"editorId":"cancelled","action":"create"})",stopped.get_token());}
            catch(const std::exception&){cancelled_editor=true;}
            check(cancelled_editor,"A cancelled mathematical request was executed");
            check(!worker.plan("La lectura continúa.","es").empty(),"Mathematical cancellation interfered with narration");
            std::cout<<"PASS one-process concurrent mathematical/audio replies, cache reuse and independent cancellation\n";
        }
        std::cout << "PASS native real Kokoro plan/prepare/identity/cache/word timings and exact mathematical bridge\n";
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::cerr << "FAIL native narration worker: " << winrt::to_string(error.message()) << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL native narration worker: " << error.what() << '\n';
    }
    return 1;
}

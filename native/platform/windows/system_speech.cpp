#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>

namespace lgaed::windows {
namespace {
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Data::Json;
void require(bool value,std::string_view message) { if (!value) throw std::runtime_error(std::string(message)); }
void checked(HRESULT value,std::string_view message) { require(SUCCEEDED(value),message); }
void cancelled(std::stop_token stop) { if (stop.stop_requested()) throw std::runtime_error("Lectura cancelada."); }
std::string normalized(std::string_view value) {
    std::string result(value); for (auto& character : result) { if (character == '_') character = '-'; if (character >= 'A' && character <= 'Z') character += 'a' - 'A'; } return result;
}
std::string allocated_text(LPWSTR value) { if (!value) return {}; const auto result = winrt::to_string(std::wstring_view(value)); CoTaskMemFree(value); return result; }
std::vector<SystemVoice> voices() {
    ComPtr<IEnumSpObjectTokens> tokens;
    checked(SpEnumTokens(SPCAT_VOICES,nullptr,nullptr,tokens.GetAddressOf()),"No se pudieron consultar las voces SAPI instaladas.");
    std::vector<SystemVoice> result; ULONG count = 0; checked(tokens->GetCount(&count),"No se pudo consultar el número de voces.");
    for (ULONG index = 0; index < count; ++index) {
        ComPtr<ISpObjectToken> token; checked(tokens->Item(index,token.GetAddressOf()),"No se pudo leer una voz instalada.");
        LPWSTR identifier = nullptr, description = nullptr;
        checked(token->GetId(&identifier),"No se pudo identificar una voz instalada."); const auto id = allocated_text(identifier);
        checked(SpGetDescription(token.Get(),&description),"No se pudo consultar el nombre de la voz."); const auto name = allocated_text(description);
        ComPtr<ISpDataKey> attributes;
        if (FAILED(token->OpenKey(L"Attributes",attributes.GetAddressOf()))) continue;
        LPWSTR value = nullptr; if (FAILED(attributes->GetStringValue(L"Language",&value))) continue;
        const auto languages = allocated_text(value); std::size_t cursor = 0;
        while (cursor < languages.size()) {
            const auto end = languages.find(';',cursor); const auto item = std::string_view(languages).substr(cursor,end == languages.npos ? languages.size() - cursor : end - cursor);
            unsigned language = 0; const auto parsed = std::from_chars(item.data(),item.data() + item.size(),language,16);
            wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
            if (parsed.ec == std::errc{} && parsed.ptr == item.data() + item.size() &&
                LCIDToLocaleName(static_cast<LCID>(language),locale,LOCALE_NAME_MAX_LENGTH,0)) result.push_back({id,name,winrt::to_string(std::wstring_view(locale))});
            if (end == languages.npos) break; cursor = end + 1;
        }
    }
    return result;
}
std::size_t utf16_size(std::string_view value) { return winrt::to_hstring(value).size(); }
std::string framing(const NarrationSegment& segment,std::string_view language,const SystemVoice& voice,std::string_view spoken) {
    std::string result = "windows-sapi5-pcm24k-v1/";
    const auto token = [&](std::string_view text) { result += std::to_string(text.size()) + ':'; result += text; };
    token(language); token(voice.id); token(voice.language); token(segment.text); token(spoken);
    for (std::size_t index = 0; index < segment.spoken_ranges.size(); ++index) {
        const auto spoken_range = segment.spoken_ranges[index], displayed = segment.displayed_ranges[index];
        token(std::to_string(spoken_range.start)); token(std::to_string(spoken_range.end)); token(std::to_string(displayed.start)); token(std::to_string(displayed.end));
    }
    return result;
}
struct Boundary { std::size_t start = 0,end = 0; double time = 0; };
std::vector<NarrationTiming> project(const NarrationSegment&,std::string_view,const std::vector<Boundary>&,double);
struct Wave { double duration = 0; std::string hash; std::vector<Boundary> words; };
Wave wave(const std::filesystem::path& file) {
    const auto bytes = state::read_bytes(file,128 * 1024 * 1024);
    const auto word = [&](std::size_t start) -> std::uint32_t { require(start + 4 <= bytes.size(),"WAV SAPI incompleto."); return
        static_cast<std::uint32_t>(bytes[start]) | (static_cast<std::uint32_t>(bytes[start + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[start + 2]) << 16) | (static_cast<std::uint32_t>(bytes[start + 3]) << 24); };
    const auto marker = [&](std::size_t start,std::string_view name) { return start + name.size() <= bytes.size() && std::equal(name.begin(),name.end(),bytes.begin() + static_cast<std::ptrdiff_t>(start)); };
    require(bytes.size() >= 44 && marker(0,"RIFF") && marker(8,"WAVE") && word(4) + 8ULL == bytes.size(),"SAPI no produjo un archivo WAV íntegro.");
    bool format = false; std::size_t samples = 0; std::vector<Boundary> words;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const auto length = word(offset + 4); require(length <= bytes.size() - offset - 8,"El WAV contiene un bloque incompleto.");
        if (marker(offset,"fmt ")) {
            require(length >= 16 && bytes[offset + 8] == 1 && bytes[offset + 9] == 0 && bytes[offset + 10] == 1 && bytes[offset + 11] == 0 &&
                word(offset + 12) == 24000 && word(offset + 16) == 48000 && bytes[offset + 20] == 2 && bytes[offset + 21] == 0 &&
                bytes[offset + 22] == 16 && bytes[offset + 23] == 0,"La voz no respetó PCM de 24 kHz, mono y 16 bits."); format = true;
        } else if (marker(offset,"data")) { require(length % 2 == 0,"Datos WAV incompletos."); samples += length / 2; }
        else if (marker(offset,"EVNT")) {
            // SAPI file output serializes the requested events instead of
            // notifying the voice. The WAV chunk uses the SDK's fixed-width
            // SPSERIALIZEDEVENT, including on a 64-bit host.
            static_assert(sizeof(SPSERIALIZEDEVENT) == 24);
            require(length % sizeof(SPSERIALIZEDEVENT) == 0,"Eventos WAV SAPI incompletos.");
            for (std::size_t position = 0; position < length; position += sizeof(SPSERIALIZEDEVENT)) {
                SPSERIALIZEDEVENT serialized{}; std::memcpy(&serialized,bytes.data() + offset + 8 + position,sizeof(serialized));
                require(serialized.elParamType == SPET_LPARAM_IS_UNDEFINED,"El WAV contiene eventos no solicitados.");
                CSpEvent event; checked(event.Deserialize(&serialized,nullptr,sizeof(serialized)),"No se pudo recuperar un límite de palabra SAPI.");
                if (event.eEventId == SPEI_WORD_BOUNDARY) {
                    const auto start = static_cast<std::size_t>(event.InputWordPos()), count = static_cast<std::size_t>(event.InputWordLen());
                    words.push_back({start,start + count,static_cast<double>(event.ullAudioStreamOffset) / 48});
                }
            }
        }
        offset += 8 + static_cast<std::size_t>(length) + length % 2;
    }
    require(format && samples > 0,"La voz no produjo muestras de audio."); return {static_cast<double>(samples) / 24,state::sha256(bytes),std::move(words)};
}
void write_metadata(const std::filesystem::path& path,const std::string& text) {
    const auto handle = CreateFileW(state::native_io_path(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,nullptr);
    require(handle != INVALID_HANDLE_VALUE,"No se pudo guardar la alineación SAPI.");
    DWORD written = 0; const bool ok = WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written == text.size() && FlushFileBuffers(handle);
    CloseHandle(handle); require(ok,"No se pudo confirmar la caché de voz del sistema.");
}
NarrationAudio reuse(const std::filesystem::path& directory,std::string_view key,const NarrationSegment& segment,std::string_view spoken) {
    const auto bytes = state::read_bytes(directory / "timings.json",4 * 1024 * 1024);
    const auto metadata = JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
    require(winrt::to_string(metadata.GetNamedString(L"key")) == key && winrt::to_string(metadata.GetNamedString(L"source")) == segment.text &&
        winrt::to_string(metadata.GetNamedString(L"spoken")) == spoken && metadata.GetNamedString(L"provider") == L"windows-sapi5", "La caché del sistema no coincide; se conserva sin regenerar.");
    const auto actual = wave(directory / "audio.wav");
    require(actual.hash == winrt::to_string(metadata.GetNamedString(L"sha256")),"La integridad de la voz del sistema no coincide; se conserva el archivo.");
    NarrationAudio result{std::string(key),segment.text,std::string(spoken),"windows-sapi5",directory / "audio.wav",{},actual.duration,true};
    double previous_time = -1; std::size_t previous_start = 0; const auto length = utf16_size(segment.text);
    for (const auto& item : metadata.GetNamedArray(L"timings")) {
        const auto value = item.GetObject(); const auto from = value.GetNamedNumber(L"start"), to = value.GetNamedNumber(L"end");
        const auto start = value.GetNamedNumber(L"startMs"), end = value.GetNamedNumber(L"endMs");
        require(std::isfinite(from) && std::isfinite(to) && from >= 0 && to > from && to <= static_cast<double>(length) &&
            std::trunc(from) == from && std::trunc(to) == to && from >= static_cast<double>(previous_start) &&
            std::isfinite(start) && std::isfinite(end) && start >= 0 && start >= previous_time && end > start && end <= actual.duration + .1,
            "La alineación SAPI guardada no es válida; se conserva sin reemplazar.");
        result.timings.push_back({static_cast<std::size_t>(from),static_cast<std::size_t>(to),start,end}); previous_time = start; previous_start = static_cast<std::size_t>(from);
    }
    // An earlier compatible WAV may already contain the real event chunk but
    // have an empty sidecar. Recover its alignment without touching the audio.
    if (result.timings.empty() && !actual.words.empty()) result.timings = project(segment,spoken,actual.words,actual.duration);
    return result;
}
std::vector<NarrationTiming> project(const NarrationSegment& segment,std::string_view spoken,const std::vector<Boundary>& boundaries,double duration) {
    std::vector<NarrationTiming> result; const bool identity = spoken == segment.text && segment.spoken_ranges.empty();
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const auto& word = boundaries[index]; const auto end_time = index + 1 < boundaries.size() ? boundaries[index + 1].time : duration;
        if (word.start >= word.end || word.end > utf16_size(spoken) || end_time <= word.time || word.time < 0 || end_time > duration + .1) continue;
        std::optional<SpeechRange> owner;
        if (identity) owner = SpeechRange{word.start,word.end};
        else for (std::size_t range = 0; range < segment.spoken_ranges.size(); ++range) {
            const auto source = segment.spoken_ranges[range];
            if (source.start >= word.end || source.end <= word.start) continue;
            const auto visible = segment.displayed_ranges[range];
            if (!owner) owner = visible; else { owner->start = std::min(owner->start,visible.start); owner->end = std::max(owner->end,visible.end); }
        }
        if (!owner || (!result.empty() && owner->start < result.back().start)) continue;
        result.push_back({owner->start,owner->end,word.time,end_time});
    }
    return result;
}
}
bool system_voice_matches(std::string_view voice,std::string_view request) {
    const auto actual = normalized(voice), wanted = normalized(request);
    return !wanted.empty() && (actual == wanted || (wanted.find('-') == wanted.npos && actual.starts_with(wanted + '-')));
}
std::vector<SystemVoice> system_voices() try { ensure_winrt_apartment(); return voices(); }
catch (const winrt::hresult_error& error) { throw std::runtime_error("Voces del sistema: " + winrt::to_string(error.message())); }
NarrationAudio synthesize_system_speech(const NarrationSegment& segment,std::string_view language,std::string_view voice_id,
    const std::filesystem::path& output_directory,std::stop_token stop) try {
    ensure_winrt_apartment(); cancelled(stop);
    const auto spoken = segment.spoken.empty() ? segment.text : segment.spoken;
    const auto speech = winrt::to_hstring(spoken); const auto source_length = utf16_size(segment.text);
    require(!speech.empty() && speech.size() <= 32768 && segment.spoken_ranges.size() == segment.displayed_ranges.size() && segment.spoken_ranges.size() <= 4096,"Proyección de voz del sistema no compatible.");
    for (std::size_t index = 0; index < segment.spoken_ranges.size(); ++index) {
        const auto a = segment.spoken_ranges[index], b = segment.displayed_ranges[index];
        require(a.start < a.end && a.end <= speech.size() && b.start < b.end && b.end <= source_length,"Proyección de palabras fuera del texto original.");
    }
    const auto installed = voices(); const auto found = std::ranges::find_if(installed,[&](const auto& voice) {
        return (voice_id.empty() || voice.id == voice_id) && system_voice_matches(voice.language,language);
    });
    require(found != installed.end(),"No hay una voz SAPI instalada para el idioma y la voz seleccionados. No se sustituye por otro idioma.");
    cancelled(stop); const auto key = state::sha256(framing(segment,normalized(language),*found,spoken));
    require(output_directory.is_absolute(),"La caché de voz debe pertenecer a una ruta privada absoluta.");
    const auto root = output_directory / "windows_tts"; const auto final = root / key; state::require_safe_path(final);
    if (std::filesystem::exists(state::native_io_path(final))) return reuse(final,key,segment,spoken);
    std::filesystem::create_directories(state::native_io_path(root));
    static std::atomic<std::uint64_t> serial = 0;
    const auto pending = root / (L"." + std::wstring(winrt::to_hstring(key).c_str()) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial) + L".pending");
    require(std::filesystem::create_directory(state::native_io_path(pending)),"No se pudo preparar la grabación del sistema.");
    try {
        ComPtr<ISpObjectToken> token; checked(SpGetTokenFromId(winrt::to_hstring(found->id).c_str(),token.GetAddressOf()),"La voz seleccionada dejó de estar disponible.");
        ComPtr<ISpVoice> voice; checked(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(voice.GetAddressOf())),"No se pudo iniciar SAPI.");
        checked(voice->SetVoice(token.Get()),"No se pudo seleccionar la voz del sistema.");
        ComPtr<ISpStream> stream; checked(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(stream.GetAddressOf())),"No se pudo preparar el WAV del sistema.");
        struct ReleaseSpeech {
            ComPtr<ISpVoice>& voice; ComPtr<ISpStream>& stream;
            ~ReleaseSpeech() {
                if (voice) (void)voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);
                // The output stream is still owned by SpVoice until release.
                // Closing it first can wait on that same voice during cancel.
                voice.Reset(); if (stream) (void)stream->Close(); stream.Reset();
            }
        } release{voice,stream};
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,24000,48000,2,16,0};
        const auto interest = SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM);
        checked(stream->BindToFile(state::native_io_path(pending / "audio.wav").c_str(),SPFM_CREATE_ALWAYS,&SPDFID_WaveFormatEx,&format,interest),"No se pudo abrir la grabación del sistema.");
        checked(voice->SetOutput(stream.Get(),FALSE),"La voz no admite el formato de audio requerido.");
        checked(voice->SetNotifyWin32Event(),"No se pudo preparar la notificación de palabras SAPI.");
        checked(voice->SetInterest(interest,interest),"No se pudieron activar los límites de palabras SAPI.");
        checked(voice->Speak(speech.c_str(),SPF_ASYNC | SPF_IS_NOT_XML,nullptr),"SAPI no pudo sintetizar el texto.");
        std::vector<Boundary> boundaries; const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
        for (;;) {
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                cancelled(stop);
                throw std::runtime_error("La voz del sistema superó el tiempo de síntesis.");
            }
            const auto done = voice->WaitUntilDone(20);
            CSpEvent event;
            while (event.GetFrom(voice.Get()) == S_OK) {
                if (event.eEventId == SPEI_WORD_BOUNDARY) {
                    // The installed SDK's CSpEvent helpers are authoritative:
                    // position=lParam, length=wParam (some old web tables invert them).
                    const auto start = static_cast<std::size_t>(event.InputWordPos()), length = static_cast<std::size_t>(event.InputWordLen());
                    const auto milliseconds = static_cast<double>(event.ullAudioStreamOffset) / 48;
                    if (length && start < speech.size() && length <= speech.size() - start &&
                        (boundaries.empty() || (start >= boundaries.back().start && milliseconds >= boundaries.back().time)))
                        boundaries.push_back({start,start + length,milliseconds});
                }
            }
            if (done == S_OK) break; checked(done,"SAPI interrumpió la síntesis.");
        }
        // Release the engine, not SetOutput(nullptr), which would select the
        // machine's default speaker and needlessly require an audio device.
        voice.Reset(); checked(stream->Close(),"No se pudo cerrar la grabación del sistema.");
        stream.Reset(); token.Reset(); cancelled(stop);
        const auto actual = wave(pending / "audio.wav");
        const auto timings = project(segment,spoken,actual.words.empty() ? boundaries : actual.words,actual.duration);
        JsonObject metadata; metadata.Insert(L"key",JsonValue::CreateStringValue(winrt::to_hstring(key)));
        metadata.Insert(L"source",JsonValue::CreateStringValue(winrt::to_hstring(segment.text))); metadata.Insert(L"spoken",JsonValue::CreateStringValue(speech));
        metadata.Insert(L"provider",JsonValue::CreateStringValue(L"windows-sapi5")); metadata.Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(actual.hash)));
        JsonArray rows;
        for (const auto& timing : timings) {
            JsonObject row; row.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(timing.start))); row.Insert(L"end",JsonValue::CreateNumberValue(static_cast<double>(timing.end)));
            row.Insert(L"startMs",JsonValue::CreateNumberValue(timing.start_ms)); row.Insert(L"endMs",JsonValue::CreateNumberValue(timing.end_ms)); rows.Append(row);
        }
        metadata.Insert(L"timings",rows); write_metadata(pending / "timings.json",winrt::to_string(metadata.Stringify())); cancelled(stop);
        if (!MoveFileExW(state::native_io_path(pending).c_str(),state::native_io_path(final).c_str(),MOVEFILE_WRITE_THROUGH)) {
            require(std::filesystem::exists(state::native_io_path(final)),"No se pudo publicar la caché del sistema.");
            auto result = reuse(final,key,segment,spoken); std::filesystem::remove_all(state::native_io_path(pending)); return result;
        }
        auto result = reuse(final,key,segment,spoken); result.cache_hit = false; return result;
    } catch (...) { std::error_code error; std::filesystem::remove_all(state::native_io_path(pending),error); throw; }
} catch (const winrt::hresult_error& error) {
    // C++/WinRT errors do not derive from std::exception. Keep the native
    // provider's error contract so a corrupt sidecar cannot terminate the host.
    throw std::runtime_error("Voz del sistema: " + winrt::to_string(error.message()));
}
} // namespace lgaed::windows

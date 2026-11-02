#pragma once
#include "platform/windows/narration_cpu_policy.hpp"
#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <vector>

namespace lgaed::windows {
class NarrationPreempted:public std::runtime_error {
public:NarrationPreempted():std::runtime_error("Background narration yielded to interactive work"){}
};
struct SpeechRange {std::size_t start=0,end=0;};
struct NarrationSegment {
    std::string text,prepared;std::size_t start=0,end=0;
    // Paired offsets use UTF-16, as on Android. Displayed ranges address text;
    // spoken ranges address the annotation-free spoken text actually supplied
    // to a native system voice. A source token can own several spoken words.
    std::string spoken;
    std::vector<SpeechRange> spoken_ranges,displayed_ranges;
};
struct NarrationTiming {std::size_t start=0,end=0;double start_ms=0,end_ms=0;};
struct NarrationAudio {
    std::string key,source,spoken,provider;
    std::filesystem::path wav;
    std::vector<NarrationTiming> timings;
    double duration_ms=0;bool cache_hit=false;
};
class NarrationPriorityLease {
public:
    NarrationPriorityLease()=default;
    ~NarrationPriorityLease();
    NarrationPriorityLease(NarrationPriorityLease&&) noexcept;
    NarrationPriorityLease& operator=(NarrationPriorityLease&&) noexcept;
    NarrationPriorityLease(const NarrationPriorityLease&)=delete;
    NarrationPriorityLease& operator=(const NarrationPriorityLease&)=delete;
private:
    explicit NarrationPriorityLease(std::function<void()> release):release_(std::move(release)){}
    std::function<void()> release_;
    friend class NarrationWorker;
};
// Only inference and the shared linguistic pipeline live in the host worker.
// Playback, state ownership and user interaction remain native Win32.
class NarrationWorker {
public:
    explicit NarrationWorker(std::filesystem::path runtime);
    ~NarrationWorker();
    NarrationWorker(const NarrationWorker&)=delete;NarrationWorker& operator=(const NarrationWorker&)=delete;
    // Terminal app shutdown, before joining any producer/reader that uses us.
    // Interrupts only this worker's job, wakes callers and never waits for RPC
    // cancellation acknowledgement. Repeated calls do not restart the service.
    void shutdown() noexcept;
    // Reserve before planning a current page; retain until all needed audio is
    // ready, never until playback finishes. Background work is cancelled and
    // must acknowledge completion before the lease is granted. Cancellable wait.
    [[nodiscard]] NarrationPriorityLease current_page_lease(std::stop_token={});
    // Compilation/heavy rendering excludes synthesis even when a page keeps
    // its preparation lease. In-flight inference acknowledges cancellation;
    // foreground preparation resumes automatically when this lease is released.
    [[nodiscard]] NarrationPriorityLease heavy_work_lease(std::stop_token={});
    // Explicit cache maintenance can reserve an already idle worker. Unlike
    // heavy work this never cancels or waits for an active reading operation.
    [[nodiscard]] std::optional<NarrationPriorityLease> try_idle_maintenance_lease();
    // Only cache misses are limited. Existing recordings remain reusable.
    void set_storage_budget(std::uint64_t bytes);
    [[nodiscard]] std::vector<NarrationSegment> plan(std::string_view text,std::string_view language,std::stop_token={});
    [[nodiscard]] std::string curriculum_plan(std::string_view raw_lesson,std::size_t page,
        std::string_view language,std::string_view voice,std::string_view ui_language,std::stop_token={});
    [[nodiscard]] NarrationAudio prepare(const NarrationSegment&,std::string_view language,std::string_view voice,
        const std::filesystem::path& output,unsigned threads,std::stop_token={},
        NarrationCpuClass work=NarrationCpuClass::current_page);
    // System speech advance generation shares the same cancellable background
    // reservation as Kokoro, and runs in its own hard-affinity native child.
    [[nodiscard]] NarrationAudio prepare_system(const NarrationSegment&,std::string_view language,std::string_view voice,
        const std::filesystem::path& output,std::stop_token={});
    [[nodiscard]] std::string identity(std::string_view page,std::string_view language,std::string_view voice,
        const std::vector<NarrationSegment>&,std::stop_token={},std::string_view engine="KOKORO");
    // Raw curricular JSON travels as a string: WinRT's floating-point JSON
    // representation must not round exact decimal/integer answer contracts.
    [[nodiscard]] std::string describe_math(std::string_view raw_block,std::string_view language,std::stop_token={});
    [[nodiscard]] std::string validate_math(std::string_view raw_block,std::string_view raw_responses,
        std::string_view language,std::stop_token={});
    [[nodiscard]] std::string math_editor(std::string_view request_json,std::stop_token={});
    [[nodiscard]] std::string math_keyboard(std::string_view request_json,std::stop_token={});
    [[nodiscard]] std::string math_calculate(std::string_view request_json,std::stop_token={});
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}

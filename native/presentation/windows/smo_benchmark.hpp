#pragma once

#include "presentation/windows/graphics_view.hpp"
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

namespace lgaed::presentation {
struct SmoBenchmarkRow {
    std::string name;
    bool equivalent = false;
    std::size_t values = 0, mismatches = 0;
    double max_error = 0;
    double ordinary_median_ms = 0, smo_median_ms = 0;
    std::vector<double> ordinary_ms, smo_ms;
    bool timing_valid = false;
    std::string note;
};
struct SmoBenchmarkReport {
    std::string backend;
    bool software = false, gpu_timestamps = false;
    unsigned dispatches_per_sample = 0;
    std::vector<SmoBenchmarkRow> rows;
    std::string conclusion;
};
// Experimental arithmetic lowering to paired HLSL kernels. This is not a GPU
// compiler for the SMO language and never times a fixed native animation.
// GPU queries time only Dispatch; CPU oracles/readback remain outside timing.
[[nodiscard]] SmoBenchmarkReport run_smo_benchmark(
    GraphicsBackend = GraphicsBackend::automatic, std::uint32_t work_items = 65536,
    unsigned rounds = 9, std::stop_token = {});

struct SmoParticleCalibration {
    std::uint32_t particles = 0, terms = 0;
    double naive_ms = 0;
    bool timing_valid = false;
};
struct SmoParticleRow {
    std::uint32_t particles = 0, group_size = 0, groups = 0, terms = 0;
    std::uint64_t naive_term_evaluations = 0, grouped_term_evaluations = 0;
    double naive_median_ms = 0, grouped_ordinary_median_ms = 0, grouped_smo_median_ms = 0;
    std::vector<double> naive_ms, grouped_ordinary_ms, grouped_smo_ms;
    std::size_t values = 0, mismatches = 0;
    double max_error = 0;
    bool equivalent = false, timing_valid = false, target_reached = false;
    std::string note;
};
struct SmoParticleReport {
    std::string backend;
    bool software = false, stress = false, gpu_timestamps = false;
    double target_ms = 1000. / 30.;
    std::vector<SmoParticleCalibration> calibration;
    std::vector<SmoParticleRow> rows;
    std::string conclusion;
};
// One measured interval is one frame, including the emitter prepass and all
// particle writes. B (ordinary grouping) is the essential algorithmic control:
// reducing repeated emitter work must not be attributed exclusively to SMO.
// All measured frame readbacks stay in temporary RAM; independent CPU oracles
// run after the complete sequence, avoiding validation gaps between frames.
// Hardware stress is bounded to 2097152 particles and 8192 terms per emitter.
// stress=false is a small real WARP-compatible correctness workload.
[[nodiscard]] SmoParticleReport run_smo_particle_benchmark(
    GraphicsBackend = GraphicsBackend::automatic, bool stress = true, std::stop_token = {});
} // namespace lgaed::presentation

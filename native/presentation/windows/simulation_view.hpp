#pragma once

#include "core/simulation.hpp"
#include "core/text_semantics.hpp"
#include <windows.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lgaed::presentation {

struct SimulationCallbacks {
    std::function<void()> changed;
    std::function<void()> layout_changed;
    std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> passive_gesture;
};
struct SimulationReadingRegion {
    std::string key,source_text;
    HWND target = nullptr;
    bool passive_label = false;
};

// The native view selects immutable states calculated by the core model.
// Exploration changes neither exercise responses nor lesson achievements.
class SimulationView {
public:
    SimulationView(HWND parent,const curriculum::SimulationSpec& spec,std::string locale,
        SimulationCallbacks callbacks = {},
        const std::map<std::string,curriculum::TextSemantics>& semantics = {});
    ~SimulationView();
    SimulationView(const SimulationView&) = delete;
    SimulationView& operator=(const SimulationView&) = delete;
    [[nodiscard]] HWND window() const;
    [[nodiscard]] int height_for_width(int width);
    void set_text_scale(int percentage);
    [[nodiscard]] std::size_t step_index() const;
    [[nodiscard]] std::size_t step_count() const;
    [[nodiscard]] bool go_to_step(std::size_t index);
    void reset();
    [[nodiscard]] std::vector<SimulationReadingRegion> reading_regions() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lgaed::presentation

#pragma once

#include <cstddef>
#include <string_view>

namespace lgaed::presentation::narration_legacy {
inline constexpr std::string_view previous_output_prefix="previous_output_data/";
struct Passage {
    std::string_view owner,source,context,math_mode,prepared;
    std::size_t start=0,end=0;
};
// Private historical plans can authenticate the same visible passage, never
// listening to its new pronunciation. Range/source/owner equality is mandatory.
inline bool same_previous_passage(const Passage& before,const Passage& after) {
    auto context=before.context;
    const bool previous_output=context.starts_with(previous_output_prefix);
    if(previous_output)context.remove_prefix(previous_output_prefix.size());
    const bool previous_cpp=(context=="auto"||context=="cpp_previous_parentheses")
        &&after.context=="cpp"&&before.math_mode=="auto"&&after.math_mode=="auto";
    const bool same_context=previous_output&&context==after.context;
    return (same_context||previous_cpp)&&before.math_mode==after.math_mode
        &&before.owner==after.owner&&before.source==after.source
        &&before.start==after.start&&before.end==after.end&&before.prepared!=after.prepared;
}
} // namespace lgaed::presentation::narration_legacy

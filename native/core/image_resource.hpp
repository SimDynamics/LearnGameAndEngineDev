#pragma once
#include <cstdint>
#include <string>

namespace lgaed::curriculum {
struct ImageResource {
    std::string image_id,src,alt,caption;
    bool operator==(const ImageResource&) const=default;
};
inline constexpr std::uint64_t image_max_bytes=64ull*1024*1024;
inline constexpr unsigned image_max_dimension=32768;
inline constexpr std::uint64_t image_max_pixels=40000000;
// Resource names are relative to content/, independent of a user profile or
// the process directory. Actual handles and decoded pixels are checked too.
void validate_image_resource(const ImageResource&);
}

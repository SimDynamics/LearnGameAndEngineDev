#include "core/attempts.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void verify_exhausted_premium(std::string route, int maximum) {
    using namespace lgaed;
    constexpr std::int64_t now = 1800000000000LL;
    state::StateData data;
    // This is the state that previously blocked the student. It is not a
    // Play entitlement and must not require network or payment.
    data.preferences["premium_state"]["state"] = std::string("FREE");
    data.preferences["attempt_reserve_seconds_test"]["reserve_" + route] = 0.;
    data.preferences["attempt_reserve_seconds_test"]["updated_at_" + route] = now;
    data.preferences["attempt_reserve_seconds_test"]["recovery_interval_" + route] =
        attempts::recovery_millis;
    data.preferences["learning_progress"]["saved_answer"] = std::string("preserved");
    data.preferences["cpp_lesson_drafts"]["source"] = std::string("int x = 4;\n");
    const auto initial = data;

    const auto balance = attempts::balance(data, route, now);
    require(balance.maximum == maximum && balance.available == 0 &&
            balance.reserve == 0. && balance.unlimited && balance.can_evaluate(),
        "An exhausted Windows profile cannot check a puzzle with personal Premium");
    require(attempts::recovery_label(maximum, balance.reserve, balance.unlimited).empty(),
        "Premium unexpectedly shows a free-life recovery countdown");
    require(data == initial, "Reading Premium rewrote student data");

    for (int mistake = 0; mistake < 3; ++mistake) {
        const auto after = attempts::consume_incorrect(data, route, now);
        require(after.unlimited && after.can_evaluate() && after.available == 0,
            "A wrong answer disabled personal Premium");
        require(data == initial, "An incorrect Premium answer consumed lives or changed student data");
    }
}

} // namespace

int main() {
    try {
        using namespace lgaed;
        require(attempts::personal_premium_enabled,
            "Personal Windows development build must activate Premium in source");
        state::StateData empty;
        require(attempts::premium(empty), "Personal Premium unexpectedly depends on a saved purchase");
        verify_exhausted_premium("programming", 8);
        verify_exhausted_premium("math_physics", 12);
        std::cout << "PASS Windows personal Premium: zero lives, both courses, unlimited checks, no purchase and unchanged progress\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Windows personal Premium: " << error.what() << '\n';
        return 1;
    }
}

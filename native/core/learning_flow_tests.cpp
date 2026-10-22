#include "core/learning_flow.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using lgaed::curriculum::LearningFlow;
using lgaed::state::StateData;
constexpr const char* domain = "learning_flow_settings";
constexpr std::array<const char*, 4> canonical{
    "global_between_lessons", "global_between_units", "global_between_courses", "global_swipe_finish"};
constexpr std::array<const char*, 4> legacy{
    "automatic_between_lessons", "automatic_between_units", "automatic_between_courses", "swipe_finish"};
constexpr std::array<const char*, 4> suffix{
    "between_lessons", "between_units", "between_courses", "swipe_finish"};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
LearningFlow combination(unsigned mask) {
    return {(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0};
}
std::array<bool, 4> fields(LearningFlow setting) {
    return {setting.between_lessons, setting.between_units, setting.between_courses, setting.swipe_finish};
}
std::string course_key(std::string_view course, std::string_view setting) {
    return "course_override_" + std::string(course) + '_' + std::string(setting);
}

StateData unrelated_state() {
    StateData data;
    data.preferences["learning_progress"]["lesson_cpp_fundamentals__unit__lesson_page"] = std::int64_t{5};
    data.preferences["learning_progress"]["exercise_cpp_fundamentals__unit__lesson_page_4_block_2"] =
        std::string(R"({"answer":"<<","correct":true,"extension":{"keep":1}})");
    data.preferences["cpp_lesson_drafts"]["programming/lesson/4/2/source"] =
        std::string("int main() {\n\treturn 0;\n}\n");
    data.preferences["cpp_lesson_drafts"]["programming/lesson/4/2/input"] = std::string(" \t \n    ");
    data.preferences["narration_settings"]["voice"] = std::string("ff_siwis");
    data.preferences["narration_resume_history"]["positions"] = std::string("three retained positions");
    data.preferences["native_windows"]["catalog/es/selection"] = std::int64_t{3};
    data.preferences["native_windows"]["ui_language"] = std::string("fr");
    data.preferences["native_windows"]["course_override_cpp_fundamentals_enabled"] = std::string("other domain");
    data.preferences["future_settings"]["global_between_lessons"] = std::string("opaque");
    data.preferences["future_settings"]["course_override_other_enabled"] = true;
    data.preferences[domain]["future_option"] = std::vector<std::string>{"first", "second"};
    return data;
}

void defaults_match_android() {
    const StateData data;
    const auto original = data;
    check(lgaed::curriculum::learning_flow(data) == LearningFlow{true, false, false, false},
        "Android defaults must continue between lessons only");
    check(lgaed::curriculum::learning_flow(data, "cpp_fundamentals") == LearningFlow{},
        "An unconfigured course does not inherit Android defaults");
    check(!lgaed::curriculum::has_course_flow(data, "cpp_fundamentals") &&
        !lgaed::curriculum::has_course_flow(data, ""), "A read manufactured a course override");
    check(data == original, "Reading default navigation settings modified the profile");
}

void imported_android_preferences_are_authoritative() {
    for (unsigned mask = 0; mask < 16; ++mask) {
        auto data = unrelated_state();
        const auto setting = combination(mask);
        const auto values = fields(setting);
        for (std::size_t i = 0; i < values.size(); ++i) {
            data.preferences[domain][canonical[i]] = values[i];
            data.preferences["native_windows"][legacy[i]] = !values[i];
        }
        // Imported Android booleans retain their original domain/key/type in the
        // native state codec; importing does not need a second settings schema.
        const auto restored = lgaed::state::decode(lgaed::state::encode(data));
        check(restored == data, "Imported Android settings changed in the native state codec");
        check(lgaed::curriculum::learning_flow(restored) == setting &&
            lgaed::curriculum::learning_flow(restored, "math_course_01") == setting,
            "A native prototype fallback overrode canonical Android preferences");
        check(!lgaed::curriculum::has_course_flow(restored, "math_course_01"),
            "Imported global preferences created a course override");
    }
}

void fallback_is_per_field_and_read_only() {
    for (unsigned mask = 0; mask < 16; ++mask) {
        StateData data;
        const auto setting = combination(mask);
        const auto values = fields(setting);
        for (std::size_t i = 0; i < values.size(); ++i)
            data.preferences["native_windows"][legacy[i]] = values[i];
        const auto original = data;
        check(lgaed::curriculum::learning_flow(data) == setting,
            "An explicit early Windows navigation preference was ignored");
        check(data == original, "Reading legacy settings migrated or deleted data implicitly");
    }
    StateData partial;
    partial.preferences[domain]["global_between_lessons"] = false;
    partial.preferences[domain]["global_between_courses"] = true;
    partial.preferences["native_windows"]["automatic_between_lessons"] = true;
    partial.preferences["native_windows"]["automatic_between_units"] = true;
    check(lgaed::curriculum::learning_flow(partial) == LearningFlow{false, true, true, false},
        "Missing canonical fields did not fall back independently to legacy then Android defaults");
}

void inactive_legacy_values_cannot_block_canonical_settings() {
    StateData data;
    const auto expected = LearningFlow{false, true, false, true};
    const auto values = fields(expected);
    for (std::size_t i = 0; i < values.size(); ++i) {
        data.preferences[domain][canonical[i]] = values[i];
        data.preferences["native_windows"][legacy[i]] = std::string("obsolete malformed value");
    }
    const auto original = data;
    check(lgaed::curriculum::learning_flow(data) == expected,
        "An inactive malformed legacy fallback blocked valid canonical settings");
    check(data == original, "Reading canonical settings rewrote inactive legacy values");
}

void enabled_overrides_and_inheritance_are_course_specific() {
    auto data = unrelated_state();
    const LearningFlow global{false, true, false, true};
    lgaed::curriculum::save_global_flow(data, global);
    const auto before = data;
    data.preferences[domain][course_key("cpp_fundamentals", "between_lessons")] = true;
    check(!lgaed::curriculum::has_course_flow(data, "cpp_fundamentals") &&
        lgaed::curriculum::learning_flow(data, "cpp_fundamentals") == global,
        "An orphan field without enabled=true activated a course override");
    data.preferences[domain][course_key("cpp_fundamentals", "enabled")] = true;
    check(lgaed::curriculum::learning_flow(data, "cpp_fundamentals") == LearningFlow{true, true, false, true},
        "A partial imported course override did not inherit its missing global fields");
    check(lgaed::curriculum::learning_flow(data, "cpp_fundamentals_extra") == global &&
        lgaed::curriculum::learning_flow(data, "math_course_01") == global,
        "Course override matched a prefix or another route instead of its exact identifier");
    for (unsigned mask = 0; mask < 16; ++mask) {
        auto snapshot = before;
        auto expected = before;
        const auto setting = combination(mask);
        const auto values = fields(setting);
        expected.preferences[domain][course_key("cpp_fundamentals", "enabled")] = true;
        for (std::size_t i = 0; i < values.size(); ++i)
            expected.preferences[domain][course_key("cpp_fundamentals", suffix[i])] = values[i];
        lgaed::curriculum::save_course_flow(snapshot, "cpp_fundamentals", setting);
        check(snapshot == expected, "Saving a course override changed unrelated profile data");
        check(lgaed::curriculum::has_course_flow(snapshot, "cpp_fundamentals") &&
            lgaed::curriculum::learning_flow(snapshot, "cpp_fundamentals") == setting &&
            lgaed::curriculum::learning_flow(snapshot) == global,
            "A saved course override changed the global configuration or swapped fields");
    }
}

void clear_course_preserves_unknown_and_other_courses() {
    auto data = unrelated_state();
    lgaed::curriculum::save_global_flow(data, {false, false, true, false});
    lgaed::curriculum::save_course_flow(data, "cpp_fundamentals", {true, true, false, true});
    lgaed::curriculum::save_course_flow(data, "cpp_fundamentals_extra", {false, true, false, true});
    lgaed::curriculum::save_course_flow(data, "math_course_01", {true, false, true, false});
    data.preferences[domain][course_key("cpp_fundamentals", "future_extension")] = std::string("retain");
    auto expected = data;
    expected.preferences[domain].erase(course_key("cpp_fundamentals", "enabled"));
    for (const auto name : suffix) expected.preferences[domain].erase(course_key("cpp_fundamentals", name));
    lgaed::curriculum::clear_course_flow(data, "cpp_fundamentals");
    check(data == expected, "Clearing one course removed opaque fields, another course, or student data");
    check(!lgaed::curriculum::has_course_flow(data, "cpp_fundamentals") &&
        lgaed::curriculum::learning_flow(data, "cpp_fundamentals") == LearningFlow{false, false, true, false},
        "Clearing a course override did not restore global inheritance");
    lgaed::curriculum::clear_course_flow(data, "cpp_fundamentals");
    lgaed::curriculum::clear_course_flow(data, "unconfigured");
    check(data == expected, "Clearing a missing or already-cleared override modified data");
}

void global_save_resets_only_its_override_domain() {
    auto data = unrelated_state();
    const auto untouched = data;
    for (unsigned i = 0; i < 44; ++i)
        lgaed::curriculum::save_course_flow(data, "course_" + std::to_string(i), combination(i % 16));
    data.preferences[domain]["course_override_future_extension"] = std::string("also an override");
    data.preferences[domain]["course_override"] = std::string("not the exact override prefix");
    data.preferences[domain]["other_course_override_enabled"] = true;
    auto expected = untouched;
    expected.preferences[domain]["course_override"] = std::string("not the exact override prefix");
    expected.preferences[domain]["other_course_override_enabled"] = true;
    const LearningFlow setting{false, true, true, true};
    const auto values = fields(setting);
    for (std::size_t i = 0; i < values.size(); ++i) expected.preferences[domain][canonical[i]] = values[i];
    lgaed::curriculum::save_global_flow(data, setting);
    check(data == expected,
        "Saving global settings did not clear exactly its course_override_* domain while retaining other data");
    check(lgaed::curriculum::learning_flow(data) == setting,
        "Saving global settings changed their effective values");
    for (unsigned i = 0; i < 44; ++i) {
        const auto course = "course_" + std::to_string(i);
        check(!lgaed::curriculum::has_course_flow(data, course) &&
            lgaed::curriculum::learning_flow(data, course) == setting,
            "An existing course did not inherit a new global configuration");
    }
    const auto persisted = lgaed::state::decode(lgaed::state::encode(data));
    check(persisted == expected, "Saving/reopening navigation settings changed unrelated persisted state");
}

void malformed_active_preferences_are_not_coerced() {
    for (const auto& invalid : {lgaed::state::Value{std::string("true")},
             lgaed::state::Value{std::int64_t{1}}, lgaed::state::Value{1.0}}) {
        StateData data;
        data.preferences[domain]["global_between_lessons"] = invalid;
        const auto original = data;
        rejects([&] { (void)lgaed::curriculum::learning_flow(data); },
            "An active canonical setting with a wrong type was silently coerced");
        check(data == original, "Rejecting malformed canonical settings discarded their original data");
        data = {};
        data.preferences["native_windows"]["automatic_between_units"] = invalid;
        rejects([&] { (void)lgaed::curriculum::learning_flow(data); },
            "An active legacy fallback with a wrong type was silently coerced");
        data = {};
        data.preferences[domain][course_key("cpp_fundamentals", "enabled")] = false;
        data.preferences[domain][course_key("cpp_fundamentals", "between_lessons")] = invalid;
        check(lgaed::curriculum::learning_flow(data, "cpp_fundamentals") == LearningFlow{},
            "Disabled course fields blocked valid inherited settings");
        data.preferences[domain][course_key("cpp_fundamentals", "enabled")] = true;
        rejects([&] { (void)lgaed::curriculum::learning_flow(data, "cpp_fundamentals"); },
            "An active course override with a wrong type was silently coerced");
    }
}
}

int main() {
    unsigned passed = 0, failed = 0;
    const auto run = [&](const char* name, auto action) {
        try {
            action();
            ++passed;
            std::cout << name << ": PASS\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << name << ": FAIL: " << error.what() << '\n';
        }
    };
    run("Android navigation defaults", defaults_match_android);
    run("Canonical imported Android preferences", imported_android_preferences_are_authoritative);
    run("Legacy per-field fallback without mutation", fallback_is_per_field_and_read_only);
    run("Canonical settings ignore inactive malformed legacy", inactive_legacy_values_cannot_block_canonical_settings);
    run("Exact course overrides and global inheritance", enabled_overrides_and_inheritance_are_course_specific);
    run("Clearing one course preserves opaque and unrelated data", clear_course_preserves_unknown_and_other_courses);
    run("Global save clears only its override domain", global_save_resets_only_its_override_domain);
    run("Malformed active values are rejected without coercion", malformed_active_preferences_are_not_coerced);
    std::cout << passed << " learning-flow groups passed; " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

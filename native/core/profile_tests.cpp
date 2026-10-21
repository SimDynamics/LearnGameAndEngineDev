#include "core/profile.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using lgaed::ProfileId;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Action>
void rejects(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected invalid_argument");
}

lgaed::LaunchOptions options(std::initializer_list<std::wstring_view> arguments,
                             ProfileId fallback = ProfileId::Codex) {
    return lgaed::parse_launch_options(
        std::span<const std::wstring_view>(arguments.begin(), arguments.size()), fallback);
}

}  // namespace

int main() {
    unsigned passed = 0;
    unsigned failed = 0;
    const auto test = [&](std::string_view name, const std::function<void()>& action) {
        try {
            action();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << exception.what() << '\n';
        }
    };

    test("Only the two explicit profiles are accepted", [] {
        require(lgaed::parse_profile(L"Work") == ProfileId::Work, "Work profile");
        require(lgaed::parse_profile(L"Codex") == ProfileId::Codex, "Codex profile");
        for (const auto invalid : {L"", L"work", L"CODEX", L"main", L"../Work", L"Work/Codex"}) {
            require(!lgaed::parse_profile(invalid), "Unknown profile must not become a directory");
        }
        require(lgaed::profile_name(ProfileId::Work) == L"Work", "Stable Work name");
        require(lgaed::profile_name(ProfileId::Codex) == L"Codex", "Stable Codex name");
    });

    test("An explicit launch does not depend on the configured default", [] {
        const auto selected = options({L"--profile", L"Work"});
        require(selected.profile == ProfileId::Work, "Explicit profile");
        require(!selected.qa_instance && !selected.smoke_test, "Ordinary launch stays ordinary");
        require(options({}, ProfileId::Work).profile == ProfileId::Work, "Work checkout default");
        require(options({}, ProfileId::Codex).profile == ProfileId::Codex, "Codex checkout default");
    });

    test("QA smoke launch preserves its explicit instance and profile", [] {
        const auto selected = options(
            {L"--smoke-test", L"--qa-instance", L"qa_123-Valid", L"--profile", L"Work"});
        require(selected.profile == ProfileId::Work, "Selected smoke profile");
        require(selected.qa_instance == L"qa_123-Valid", "Instance token");
        require(selected.smoke_test, "Smoke launch");
        const auto ordinary_qa = options({L"--qa-instance", L"qa_123-Valid"});
        require(!ordinary_qa.smoke_test, "QA identity need not auto-close the window");
    });

    test("An unattended smoke test cannot write into the personal profile", [] {
        rejects([] { options({L"--smoke-test"}); });
    });

    test("Malformed command lines fail instead of selecting another profile", [] {
        for (const auto argument : {L"--profile", L"--qa-instance"}) {
            rejects([argument] { options({argument}); });
        }
        rejects([] { options({L"--profile", L"../Work"}); });
        rejects([] { options({L"--unknown"}); });
        rejects([] { options({L"Codex"}); });
        rejects([] { options({L"--profile", L"Work", L"--profile", L"Codex"}); });
        rejects([] { options({L"--qa-instance", L"first", L"--qa-instance", L"second"}); });
        rejects([] { options({L"--smoke-test", L"--qa-instance", L"valid", L"--smoke-test"}); });
    });

    test("QA instance validation prevents traversal and Windows special paths", [] {
        require(lgaed::is_valid_qa_instance(L"A"), "Single-character token");
        require(lgaed::is_valid_qa_instance(L"qa-42_"), "Safe token characters");
        require(lgaed::is_valid_qa_instance(std::wstring(64, L'a')), "Maximum-length token");
        for (const auto invalid : {L"", L".", L"..", L"../Work", L"a/b", L"a\\b", L"C:",
                                   L"a:b", L"a b", L"a\tb", L"a\nb", L"é", L"CON", L"con",
                                   L"PRN", L"AUX", L"NUL", L"COM1", L"LPT9"}) {
            require(!lgaed::is_valid_qa_instance(invalid), "Unsafe token accepted");
        }
        require(!lgaed::is_valid_qa_instance(std::wstring(65, L'a')), "Overlong token");
        rejects([] { options({L"--qa-instance", L"../Work"}); });
    });

    test("Every mutable data category belongs to the selected profile", [] {
        const std::filesystem::path base{LR"(C:\Users\Prueba\AppData\Local)"};
        const auto work = lgaed::make_profile_paths(base, ProfileId::Work);
        const auto codex = lgaed::make_profile_paths(base, ProfileId::Codex);
        require(work.root == base / L"LearnGameAndEngineDev" / L"Work", "Work root");
        require(codex.root == base / L"LearnGameAndEngineDev" / L"Codex", "Codex root");
        require(work.root != codex.root, "Separate profile roots");
        using Member = std::filesystem::path lgaed::ProfilePaths::*;
        const std::array<std::pair<Member, std::wstring_view>, 6> categories{{
            {&lgaed::ProfilePaths::progress, L"progress"},
            {&lgaed::ProfilePaths::drafts, L"drafts"},
            {&lgaed::ProfilePaths::preferences, L"preferences"},
            {&lgaed::ProfilePaths::logs, L"logs"},
            {&lgaed::ProfilePaths::projects, L"projects"},
            {&lgaed::ProfilePaths::cache, L"cache"},
        }};
        for (const auto& [member, directory] : categories) {
            require(work.*member == work.root / directory, "Work category must stay in Work");
            require(codex.*member == codex.root / directory, "Codex category must stay in Codex");
            require(work.*member != codex.*member, "No mutable category may be shared");
        }
    });

    test("QA instances cannot alias one another or the personal profile", [] {
        const std::filesystem::path base{LR"(C:\Users\Prueba\AppData\Local)"};
        const auto personal = lgaed::make_profile_paths(base, ProfileId::Work);
        const auto first = lgaed::make_profile_paths(base, ProfileId::Work, L"first");
        const auto second = lgaed::make_profile_paths(base, ProfileId::Work, L"second");
        const auto other_profile = lgaed::make_profile_paths(base, ProfileId::Codex, L"first");
        require(first.root == personal.root / L"qa" / L"first", "QA stays inside its owner profile");
        require(first.root != personal.root && first.root != second.root &&
                    first.root != other_profile.root,
                "All mutable QA roots are independent");
        require(first.progress != personal.progress && first.drafts != second.drafts &&
                    first.cache != other_profile.cache,
                "QA categories remain isolated");
        rejects([&] { (void)lgaed::make_profile_paths(base, ProfileId::Work, L"../Codex"); });
    });

    test("A missing Windows local-data root cannot fall back to the working directory", [] {
        rejects([] { (void)lgaed::make_profile_paths({}, ProfileId::Codex); });
        rejects([] { (void)lgaed::make_profile_paths(L"relative-data", ProfileId::Work); });
        rejects([] { (void)lgaed::make_profile_paths(L"C:relative-data", ProfileId::Work); });
    });

    test("Unicode local data paths do not depend on the process working directory", [] {
        const std::filesystem::path base{LR"(C:\Usuarios\Ángel\Datos locales)"};
        const auto paths = lgaed::make_profile_paths(base, ProfileId::Codex, L"unicode");
        require(paths.root == base / L"LearnGameAndEngineDev" / L"Codex" / L"qa" / L"unicode",
                "Keep the supplied Windows local-data root intact");
        require(paths.root.is_absolute(), "Never resolve personal data relative to the executable");
    });

    std::cout << "Profile tests: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

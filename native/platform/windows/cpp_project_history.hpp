#pragma once
#include "core/cpp_project.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace lgaed::windows {
struct CppHistoryRevision {
    std::string id,project_key,uuid,label;
    std::int64_t created_at=0;
    curriculum::CppProjectFiles files;
    std::vector<std::string> sources;
    std::map<std::string,std::string> indentation;
};
struct CppHistorySummary {std::string id,label;std::int64_t created_at=0;};
struct CppHistoryRecordEvidence {
    std::string revision_id;
    std::map<std::string,std::string> file_sha256;
    std::size_t recorded_file_count=0;
};

enum class CppHistoryChange {unchanged,added,modified,removed};
struct CppHistoryStatus {std::string path;CppHistoryChange change;};
enum class CppHistoryLineType {context,added,removed};
struct CppHistoryDiffLine {CppHistoryLineType type;std::string text,ending;};
// An observation of an actual selected-file restoration. It is not, by
// itself, proof of a curricular test run or of the preceding diff review.
struct CppHistoryRecoveryEvidence {
    std::string revision_id, path;
    std::optional<std::string> before_sha256, after_sha256;
    std::size_t preserved_other_files=0;
    bool changed=false;
};
struct CppHistoryVerifiedRestoration {
    curriculum::CppProjectFiles files;
    CppHistoryRecoveryEvidence evidence;
};
struct CppHistoryComparisonEvidence {
    std::string revision_id, path;
    std::optional<std::string> revision_sha256, draft_sha256;
    std::size_t added_lines=0, removed_lines=0;
    bool changed=false;
};
struct CppHistoryVerifiedComparison {
    std::vector<CppHistoryDiffLine> lines;
    CppHistoryComparisonEvidence evidence;
};
// The original Android binary format and SHA-256, also used by its importer.
[[nodiscard]] CppHistoryRevision decode_cpp_revision(std::span<const std::uint8_t>,
    std::string_view id,std::string_view project_key);
class CppProjectHistory {
public:
    CppProjectHistory(std::filesystem::path root,std::string_view project_identity);
    [[nodiscard]] std::vector<CppHistorySummary> summaries(std::stop_token stop={})const;
    [[nodiscard]] CppHistoryRevision load(std::string_view id)const;
    // Authenticate an actually saved revision, not a provisional UI label.
    [[nodiscard]] CppHistoryRecordEvidence recorded_evidence(const CppHistoryRevision&)const;
    [[nodiscard]] CppHistoryRevision record(const curriculum::CppProjectFiles&,
        const std::vector<std::string>&,std::string_view label,
        const std::map<std::string,std::string>& indentation={})const;
    [[nodiscard]] std::vector<CppHistoryStatus> statuses(const curriculum::CppProjectFiles&,
        const CppHistoryRevision&)const;
    [[nodiscard]] std::vector<CppHistoryDiffLine> diff(std::string_view path,
        const curriculum::CppProjectFiles&,const CppHistoryRevision&)const;
    // A comparison is linked to the actual immutable revision on disk and
    // the captured draft, not merely to two labels shown by the UI.
    [[nodiscard]] CppHistoryVerifiedComparison diff_verified(std::string_view path,
        const curriculum::CppProjectFiles&,const CppHistoryRevision&)const;
    [[nodiscard]] curriculum::CppProjectFiles restore_file(std::string_view path,
        const curriculum::CppProjectFiles&,const CppHistoryRevision&)const;
    // Authenticate the selected revision and compare EVERY sibling with the
    // original draft. Only the requested path may change or disappear.
    [[nodiscard]] CppHistoryVerifiedRestoration restore_file_verified(std::string_view path,
        const curriculum::CppProjectFiles&,const CppHistoryRevision&)const;
private:
    void authenticate_revision(const CppHistoryRevision&)const;
    std::filesystem::path root_;
    std::string key_;
};
}

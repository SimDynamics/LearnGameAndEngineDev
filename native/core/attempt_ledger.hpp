#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lgaed::attempts {

// Identity is session-local, so retain the complete framed value rather than a
// platform-dependent hash. Callers canonicalize JSON objects before passing
// them here; insertion order must never manufacture another evaluated answer.
class ExerciseAnswerIdentity {
public:
    [[nodiscard]] static std::string of_canonical(std::string_view answer) {
        return frame('c', answer);
    }
    [[nodiscard]] static std::string of(std::string_view answer) {
        return frame('s', answer);
    }
    [[nodiscard]] static std::string of(const std::string& answer) {
        return of(std::string_view(answer));
    }
    [[nodiscard]] static std::string of(const char* answer) {
        return of(std::string_view(answer));
    }
    [[nodiscard]] static std::string of(bool answer) {
        return answer ? "b1" : "b0";
    }
    template<class Integer> requires (std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>)
    [[nodiscard]] static std::string of(Integer answer) {
        return frame('n', std::to_string(answer));
    }
    // Elements are their own typed identities; array order is significant.
    [[nodiscard]] static std::string array(std::span<const std::string> elements) {
        std::string result;
        for (const auto& element : elements) result += frame('e', element);
        return frame('a', result);
    }
    // String keys sort independently of map/JSON insertion order. Values are
    // typed identities produced by these helpers, not serialized visible text.
    [[nodiscard]] static std::string object(const std::map<std::string, std::string>& members) {
        std::string result;
        for (const auto& [key, value] : members) result += frame('k', key) + frame('v', value);
        return frame('o', result);
    }
private:
    static std::string frame(char type, std::string_view value) {
        return std::string(1, type) + std::to_string(value.size()) + ':' + std::string(value);
    }
};

// This ledger owns evaluated-response history only. It neither owns an attempt
// balance nor writes course progress, preferences, audio or persistent files.
class ExerciseAttemptLedger {
public:
    using Snapshot = std::map<std::string, std::set<std::string>>;

    template<class CanEvaluate, class Evaluation, class Verdict, class ConsumeIncorrect>
    [[nodiscard]] auto evaluate(std::string_view key, std::string_view identity,
        CanEvaluate&& can_evaluate, Evaluation&& evaluation, Verdict&& verdict,
        ConsumeIncorrect&& consume_incorrect)
        -> std::optional<std::remove_cvref_t<std::invoke_result_t<Evaluation>>> {
        using Result = std::remove_cvref_t<std::invoke_result_t<Evaluation>>;
        const std::string ledger_key(key);
        {
            const std::lock_guard lock(mutex_);
            if (!in_flight_.insert(ledger_key).second) return std::nullopt;
        }
        struct Finish {
            ExerciseAttemptLedger& ledger;
            const std::string& ledger_key;
            ~Finish() {
                const std::lock_guard lock(ledger.mutex_);
                ledger.in_flight_.erase(ledger_key);
            }
        } finish{*this, ledger_key};
        if (!std::invoke(std::forward<CanEvaluate>(can_evaluate))) return std::nullopt;
        Result result = std::invoke(std::forward<Evaluation>(evaluation));
        // nullopt is incomplete, unavailable or technical feedback, never an
        // evaluated wrong response. Correct responses also consume nothing.
        const std::optional<bool> evaluated = std::invoke(std::forward<Verdict>(verdict), result);
        if (evaluated && !*evaluated)
            record_incorrect(ledger_key, identity, std::forward<ConsumeIncorrect>(consume_incorrect));
        return result;
    }

    template<class ConsumeIncorrect>
    bool record_incorrect(std::string_view key, std::string_view identity,
                          ConsumeIncorrect&& consume_incorrect) {
        {
            const std::lock_guard lock(mutex_);
            // Reserve first, before calling out: a nested callback cannot
            // consume this same evaluated response a second time.
            if (!penalized_[std::string(key)].insert(std::string(identity)).second) return false;
        }
        std::invoke(std::forward<ConsumeIncorrect>(consume_incorrect));
        return true;
    }

    [[nodiscard]] bool has_penalized(std::string_view key, std::string_view identity) const {
        const std::lock_guard lock(mutex_);
        const auto found = penalized_.find(std::string(key));
        return found != penalized_.end() && found->second.contains(std::string(identity));
    }
    // Clearing response history must not unlock an evaluation still executing
    // callbacks. In-flight ownership ends only when that evaluation returns.
    void clear() {
        const std::lock_guard lock(mutex_);
        penalized_.clear();
    }
    // Typed, in-memory saved-instance state. It intentionally has no StateData
    // codec; application restarts do not persist session attempt history.
    [[nodiscard]] Snapshot save() const {
        const std::lock_guard lock(mutex_);
        return penalized_;
    }
    void restore(Snapshot snapshot) {
        const std::lock_guard lock(mutex_);
        penalized_ = std::move(snapshot);
    }
private:
    mutable std::mutex mutex_;
    std::set<std::string> in_flight_;
    Snapshot penalized_;
};

} // namespace lgaed::attempts

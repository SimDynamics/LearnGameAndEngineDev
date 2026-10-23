#include "core/attempt_ledger.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

namespace {
using lgaed::attempts::ExerciseAnswerIdentity;
using lgaed::attempts::ExerciseAttemptLedger;
constexpr const char* test_exercise_key = "programming/lesson/stable-page/stable-block";

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void throws(Action&& action, const char* message) {
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    check(failed, message);
}
auto permitted = [] { return true; };
auto wrong = [] { return false; };
auto verdict = [](bool correct) -> std::optional<bool> { return correct; };

void repeated_answers_are_evaluated_but_charged_once() {
    ExerciseAttemptLedger ledger;
    int evaluations = 0, charges = 0;
    const auto identity = ExerciseAnswerIdentity::of("<<");
    for (int index = 0; index < 4; ++index) {
        const auto result = ledger.evaluate(test_exercise_key, identity, permitted,
            [&] { ++evaluations; return false; }, verdict, [&] { ++charges; });
        check(result && !*result, "A repeated answer stopped being evaluated");
    }
    check(evaluations == 4 && charges == 1, "One wrong answer consumed multiple attempts");
    check(ledger.has_penalized(test_exercise_key, identity), "Wrong identity was not retained in session history");
    (void)ledger.evaluate(test_exercise_key, ExerciseAnswerIdentity::of(">>"), permitted, wrong, verdict, [&] { ++charges; });
    (void)ledger.evaluate("math_physics/lesson/page/block", identity, permitted, wrong, verdict, [&] { ++charges; });
    (void)ledger.evaluate("programming/other-lesson/page/block", identity, permitted, wrong, verdict, [&] { ++charges; });
    check(charges == 4, "Different answers, lessons or routes were deduplicated together");
}

void incomplete_technical_correct_and_unavailable_results_consume_nothing() {
    ExerciseAttemptLedger ledger;
    int charges = 0, evaluations = 0;
    const auto consume = [&] { ++charges; };
    const auto incomplete = ledger.evaluate(test_exercise_key, "incomplete", permitted, [] { return 17; },
        [](int) -> std::optional<bool> { return std::nullopt; }, consume);
    const auto technical = ledger.evaluate(test_exercise_key, "compile-failure", permitted, [] { return std::string("technical"); },
        [](const std::string&) -> std::optional<bool> { return std::nullopt; }, consume);
    const auto correct = ledger.evaluate(test_exercise_key, "correct", permitted, [] { return true; }, verdict, consume);
    const auto unavailable = ledger.evaluate(test_exercise_key, "no-attempts", [] { return false; },
        [&] { ++evaluations; return false; }, verdict, consume);
    check(incomplete == 17 && technical == "technical" && correct == true && !unavailable,
        "Technical or unavailable evaluation returned the wrong result");
    check(charges == 0 && evaluations == 0 && ledger.save().empty(),
        "An incomplete, technical, correct or unavailable answer consumed an attempt");
}

void callbacks_cannot_reenter_the_same_exercise() {
    ExerciseAttemptLedger ledger;
    int charges = 0, nested_evaluations = 0;
    const auto nested = [&] {
        return ledger.evaluate(test_exercise_key, "different-nested-answer", permitted,
            [&] { ++nested_evaluations; return false; }, verdict, [&] { ++charges; });
    };
    const auto result = ledger.evaluate(test_exercise_key, "outer-answer", permitted,
        [&] { check(!nested(), "Evaluation callback reentered its test_exercise_key"); return false; },
        [&](bool correct) -> std::optional<bool> {
            check(!nested(), "Verdict callback reentered its test_exercise_key");
            return correct;
        }, [&] {
            ++charges;
            check(!nested(), "Consumption callback reentered its test_exercise_key");
        });
    check(result == false && charges == 1 && nested_evaluations == 0, "Reentrancy protection changed the result");
    check(ledger.evaluate(test_exercise_key, "after-callbacks", permitted, wrong, verdict, [&] { ++charges; }).has_value(),
        "The finished callback left the test_exercise_key locked");
    check(charges == 2, "A later independent evaluation was not permitted");
}

void identity_is_reserved_before_consumption_callback() {
    ExerciseAttemptLedger ledger;
    int charges = 0;
    const auto first = ledger.record_incorrect(test_exercise_key, "answer", [&] {
        ++charges;
        check(ledger.has_penalized(test_exercise_key, "answer"), "Identity was reserved after calling consumption");
        check(!ledger.record_incorrect(test_exercise_key, "answer", [&] { ++charges; }),
            "Nested consumption charged the same response");
    });
    check(first && charges == 1, "A new wrong identity did not consume exactly once");
    throws([&] {
        ledger.record_incorrect(test_exercise_key, "throwing-consume", [] { throw std::runtime_error("failure"); });
    }, "Consumption failure was hidden");
    check(!ledger.record_incorrect(test_exercise_key, "throwing-consume", [&] { ++charges; }),
        "Consumption failure lost an already reserved identity");
    check(charges == 1, "A failed callback allowed a duplicate charge");
}

void clearing_history_does_not_unlock_an_active_evaluation() {
    ExerciseAttemptLedger ledger;
    int charges = 0;
    const auto result = ledger.evaluate(test_exercise_key, "outer", permitted, [&] {
        ledger.clear();
        check(!ledger.evaluate(test_exercise_key, "nested", permitted, wrong, verdict, [&] { ++charges; }),
            "Clearing history unlocked an active evaluation");
        return false;
    }, verdict, [&] { ++charges; });
    check(result == false && charges == 1, "Clearing session history changed the active result");
    ledger.clear();
    check(ledger.save().empty(), "Clearing history retained an old wrong answer");
    (void)ledger.evaluate(test_exercise_key, "outer", permitted, wrong, verdict, [&] { ++charges; });
    check(charges == 2, "A cleared session retained deduplication from the previous session");
}

void callback_exceptions_always_release_in_flight_ownership() {
    ExerciseAttemptLedger ledger;
    int charges = 0;
    throws([&] {
        (void)ledger.evaluate(test_exercise_key, "can-evaluate", []() -> bool { throw std::runtime_error("gate"); },
            wrong, verdict, [&] { ++charges; });
    }, "Eligibility error was hidden");
    throws([&] {
        (void)ledger.evaluate(test_exercise_key, "evaluate", permitted, []() -> bool { throw std::runtime_error("evaluation"); },
            verdict, [&] { ++charges; });
    }, "Evaluation error was hidden");
    throws([&] {
        (void)ledger.evaluate(test_exercise_key, "verdict", permitted, wrong,
            [](bool) -> std::optional<bool> { throw std::runtime_error("verdict"); }, [&] { ++charges; });
    }, "Verdict error was hidden");
    throws([&] {
        (void)ledger.evaluate(test_exercise_key, "consume", permitted, wrong, verdict,
            [] { throw std::runtime_error("consumption"); });
    }, "Consumption error was hidden");
    check(ledger.save().size() == 1 && ledger.has_penalized(test_exercise_key, "consume"),
        "An unevaluated exception was recorded as an incorrect answer");
    check(ledger.evaluate(test_exercise_key, "after-errors", permitted, wrong, verdict, [&] { ++charges; }).has_value(),
        "An exception left evaluation ownership locked");
    check(charges == 1, "Exceptions consumed an additional attempt");
}

void saved_instance_history_is_independent_of_progress_and_in_flight_work() {
    ExerciseAttemptLedger original;
    int charges = 0;
    original.record_incorrect(test_exercise_key, "first", [&] { ++charges; });
    const auto saved = original.save();
    ExerciseAttemptLedger recreated;
    recreated.restore(saved);
    check(!recreated.record_incorrect(test_exercise_key, "first", [&] { ++charges; }),
        "Recreation forgot an evaluated wrong answer");
    check(recreated.evaluate(test_exercise_key, "new", permitted, wrong, verdict, [&] { ++charges; }).has_value(),
        "Saved-instance history restored a fictitious in-flight evaluation");
    check(charges == 2 && original.save() == saved, "A recreated ledger modified the original snapshot");
    ExerciseAttemptLedger new_session;
    check(new_session.save().empty(), "A new application session inherited persistent attempt history");
}

void simultaneous_callbacks_cannot_charge_the_same_response_twice() {
    ExerciseAttemptLedger ledger;
    std::latch entered(1), finish(1);
    std::atomic<int> charges = 0, evaluations = 0;
    std::atomic<bool> first_finished = false;
    std::thread first([&] {
        const auto result = ledger.evaluate(test_exercise_key, "answer", permitted, [&] {
            ++evaluations;
            entered.count_down();
            finish.wait();
            return false;
        }, verdict, [&] { ++charges; });
        first_finished = result.has_value() && !*result;
    });
    entered.wait();
    const auto repeated = ledger.evaluate(test_exercise_key, "answer", permitted, [&] { ++evaluations; return false; },
        verdict, [&] { ++charges; });
    const auto independent = ledger.evaluate("other-test_exercise_key", "answer", permitted, wrong, verdict, [&] { ++charges; });
    finish.count_down();
    first.join();
    check(!repeated && independent == false && first_finished && evaluations == 1 && charges == 2,
        "Concurrent callbacks bypassed per-test_exercise_key ownership or blocked an independent test_exercise_key");
}

void identities_preserve_answer_types_bytes_array_order_and_object_key_order() {
    using Id = ExerciseAnswerIdentity;
    check(Id::of("1") != Id::of(1) && Id::of(true) != Id::of("true"), "Different answer types collided");
    check(Id::of(" ") != Id::of("") && Id::of("a\tb") != Id::of("a b") &&
        Id::of("a\nb") != Id::of("a b") && Id::of("a  b") != Id::of("a b"),
        "Identity discarded significant answer whitespace");
    check(Id::of(std::string_view("a\0b", 3)) != Id::of("a"), "Identity truncated a counted answer");
    const std::array first{Id::of("ab"), Id::of("c")};
    const std::array second{Id::of("a"), Id::of("bc")};
    const std::array reversed{Id::of("c"), Id::of("ab")};
    check(Id::array(first) != Id::array(second) && Id::array(first) != Id::array(reversed),
        "Array framing lost element boundaries or answer order");
    std::map<std::string, std::string> from_first_order, from_reverse_order;
    from_first_order.emplace("first", Id::of(1)); from_first_order.emplace("second", Id::of("2"));
    from_reverse_order.emplace("second", Id::of("2")); from_reverse_order.emplace("first", Id::of(1));
    check(Id::object(from_first_order) == Id::object(from_reverse_order), "Object insertion order changed response identity");
    check(Id::of_canonical("[]") != Id::of("[]") && Id::array({}) != Id::of(""),
        "Canonical JSON, visible text and empty arrays were confused");
}
} // namespace

int main() try {
    repeated_answers_are_evaluated_but_charged_once();
    incomplete_technical_correct_and_unavailable_results_consume_nothing();
    callbacks_cannot_reenter_the_same_exercise();
    identity_is_reserved_before_consumption_callback();
    clearing_history_does_not_unlock_an_active_evaluation();
    callback_exceptions_always_release_in_flight_ownership();
    saved_instance_history_is_independent_of_progress_and_in_flight_work();
    simultaneous_callbacks_cannot_charge_the_same_response_twice();
    identities_preserve_answer_types_bytes_array_order_and_object_key_order();
    std::cout << "Attempt ledger: 9 regression groups passed.\n";
    return 0;
} catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
}

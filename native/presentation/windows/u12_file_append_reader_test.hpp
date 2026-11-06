#pragma once
// Included after the real reader Fixture. The complete current lesson opens at
// its isolated p4 address; no earlier answer, execution receipt or hearing is
// injected. The posterior author's reference is test input, not student work.
void current_u12_file_append_reader(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto asset = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json";
    const auto asset_bytes = state::read_bytes(asset);
    std::string normalized(reinterpret_cast<const char*>(asset_bytes.data()), asset_bytes.size()); std::erase(normalized, '\r');
    check(state::sha256(normalized) == "e08054fbdeb93756cb45ae6515279742ee273f55c4ddf6a1b6962b447ed50a4e",
        "The focal U12L3 reader asset differs from its audited current LF document");
    const auto lesson = windows::load_lesson(asset);
    constexpr std::size_t page = 3, choice = 2, compiler = 3, note = 4;
    const auto& question = lesson.pages.at(page).blocks.at(choice);
    const auto& goal = lesson.pages.at(page).blocks.at(compiler);
    const auto& tail = lesson.pages.at(page).blocks.at(note);
    const auto& reference = lesson.pages.at(4).blocks.at(1).code;
    const std::string guide = "Distingue una apertura que conserva el texto anterior de otra que lo reemplaza.";
    const std::string explanation = "app conserva lo anterior y añade al final. El espacio escrito separa 4 de 5, de modo que el lector puede obtener tres enteros.";
    check(lesson.id == "cpp_fundamentals__archivos_y_programa_completo__cerrar_reemplazar_y_anadir_datos"
        && lesson.pages.size() == 9 && !lesson.compiler_puzzles_required
        && lesson.reference_after.size() == 1 && lesson.reference_after.front().activity == "block_0003_0003"
        && lesson.reference_after.front().blocks == std::vector<std::string>{"block_0003_0004", "block_0004_0000", "block_0004_0001", "block_0004_0002"}
        && lesson.activity_after.size() == 1 && lesson.activity_after.front().activity == "block_0004_0003"
        && lesson.activity_after.front().prerequisites == std::vector<std::string>{"block_0003_0003"}
        && question.stable_id == "block_0003_0002" && question.correct_index == 2 && question.review_feedback_options.at(1) == guide
        && question.explanation == explanation && goal.stable_id == "block_0003_0003"
        && goal.type == curriculum::BlockType::cpp_compiler && goal.question.empty() && goal.initial_code.size() == 130
        && goal.output_check && goal.output_check->cases.size() == 1
        && goal.output_check->cases.front().outputs == std::vector<std::string>{"Total=12", "Total=12\n"}
        && goal.output_check->cases.front().standard_input.empty() && goal.output_check->cases.front().explanation.empty()
        && goal.output_check->cases.front().review_feedback.empty() && tail.type == curriculum::BlockType::note
        && reference.size() == 1049 && reference.find("std::ios::app") != reference.npos
        && reference.find("salida << \"3 4\";") != reference.npos && reference.find("salida << \" 5\";") != reference.npos,
        "The actual U12 append reader owner, authored feedback, derived gates or exact output oracle changed");
    const auto cache = std::filesystem::path(build::source_root) / "out/qa/u12-file-append-reader" /
        ("pcm-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    Fixture f(backend, false, true, true, "es", lesson, true, page, true, false, [](HWND view) {
        update_lesson_preferences(view, [](state::StateData& data) { data.preferences["lesson_narration_settings"]["rate"] = 3.; });
    }, [](state::StateData& data) { data.preferences["u12_file_append_reader_qa"]["opaque"] = std::string(" \t conservar\r\n保存"); }, cache);
    struct FailureShutdown { std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown() { if (std::uncaught_exceptions()) worker->shutdown(); } } shutdown{backend};
    const auto control = [&](std::size_t block, int part) { return child(f.view, 1000 + static_cast<int>(block) * 100 + part); };
    const auto saved = [&] { return read_lesson_state(f.view); };
    const auto ready = [&] {
        wait([&] { const auto value = f.snapshot(); return value.planned && !value.segments.empty() && value.index < value.segments.size()
            && std::ranges::all_of(value.segments, [](const auto& part) { return part.available; });
        }, "complete focal U12 file-append synthesis and alignment", 120000);
    };
    std::cout << "PHASE U12L3 p4 preparing authentic page with its required compiler\n";
    ready(); f.reader->pause();
    const auto unrelated = saved().preferences.at("u12_file_append_reader_qa");
    const auto draft_key = curriculum::draft_key("programming", lesson, goal);
    std::optional<state::Response> accepted_choice;
    const auto root = f.environment.paths.projects / "cpp-runs";
    const auto directories = [&] {
        std::vector<std::filesystem::path> result;
        if (std::filesystem::exists(root)) for (const auto& item : std::filesystem::directory_iterator(root)) if (item.is_directory()) result.push_back(item.path());
        std::ranges::sort(result); return result;
    };
    const auto bytes = [](const std::filesystem::path& path) {
        const auto value = state::read_bytes(path); return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    };
    struct Retained { std::filesystem::path path; std::string hash; std::filesystem::file_time_type stamp; };
    std::vector<Retained> retained;
    const auto retain = [&](const std::filesystem::path& path) {
        retained.push_back({path, state::sha256(state::read_bytes(path)), std::filesystem::last_write_time(path)});
    };
    const auto preserve = [&] {
        check(state::read_bytes(asset) == asset_bytes && !IsWindowVisible(f.host)
            && saved().preferences.at("u12_file_append_reader_qa") == unrelated,
            "The focal file-append reader changed source, unrelated QA data or desktop visibility");
        for (const auto& file : retained) check(state::sha256(state::read_bytes(file.path)) == file.hash
            && std::filesystem::last_write_time(file.path) == file.stamp,
            "The file-append reader rewrote or removed earlier PCM or execution evidence");
        for (const auto& current_page : lesson.pages) for (const auto& block : current_page.blocks)
            if (block.stable_id != question.stable_id) check(!state::get_response(saved(), lesson, block),
                "The focal file-append reader fabricated an independent curricular answer");
        if(accepted_choice){const auto response=state::get_response(saved(),lesson,question);
            check(response&&response->raw_json==accepted_choice->raw_json&&response->correct&&response->selected==2,
                "File-append editing, execution or playback changed the independent accepted choice");}
    };
    const auto privacy = [&] {
        const auto regions = lesson_reading_regions(f.view);
        for (const auto& region : regions) check(region.target != control(compiler, 10) && region.target != control(compiler, 14),
            "The file-append reader exposed editor source or console text as spoken curriculum");
        if (!windows::compiler_puzzle_completed(saved(), lesson, goal)) {
            check(std::ranges::any_of(regions,[](const auto& region){return region.block_index==compiler
                &&region.required_compiler&&region.is_exercise&&!region.solved;}),
                "The reference dependency did not make this exact file-append editor a pending reader gate");
            check(std::ranges::none_of(regions, [](const auto& region) { return region.block_index != std::numeric_limits<std::size_t>::max()
                && region.block_index > compiler; }), "The pending file-append compiler exposed its following note to narration");
            check(std::ranges::none_of(f.snapshot().segments, [](const auto& part) { return part.block != std::numeric_limits<std::size_t>::max()
                && part.block > compiler; }), "The pending file-append compiler prepared its following note");
            const auto options = lesson_options_snapshot(f.view);
            check(!lesson_page_state(f.view).can_next && std::ranges::find(options.available_pages, 4) == options.available_pages.end()
                && !navigate_lesson_view(f.view, 4),
                "An unchecked file-append compiler opened its reference pages");
        }
    };
    const auto parts = [&](std::size_t block, bool feedback, bool correct, const std::string& expected) {
        const auto regions = lesson_reading_regions(f.view);
        const auto region = std::ranges::find_if(regions, [&](const auto& item) { return item.block_index == block && item.feedback == feedback
            && item.source_text == expected; });
        check(region != regions.end() && (!feedback || (region->correct == correct && region->solved == correct)),
            "The actual file-append feedback/note lost its full source or verdict");
        auto visible = expected; std::erase(visible, '`');
        check(edit_text(region->target) == visible && (!feedback || region->target == control(block, 4)),
            "The file-append displayed feedback/note differs from its complete authored source");
        const auto source = wide(expected); const auto plan = backend->plan(expected, "es", {}, region->reading_context, region->math_mode);
        const auto value = f.snapshot(); std::vector<std::size_t> indices; std::vector<bool> covered(source.size());
        for (std::size_t at = 0; at < value.segments.size(); ++at) {
            const auto& part = value.segments[at]; if (part.region != region->key) continue;
            const auto index = indices.size();
            check(index < plan.size() && part.block == block && part.feedback == feedback && (!feedback || part.correct == correct)
                && part.target == region->target && part.source == plan[index].text && part.spoken == plan[index].spoken
                && part.start == plan[index].start && part.end == plan[index].end && part.start < part.end && part.end <= source.size()
                && part.source == utf8(source.substr(part.start, part.end - part.start)),
                "The file-append audio omitted, reordered or remapped a complete UTF-16 passage");
            for (auto offset = part.start; offset < part.end; ++offset) covered[offset] = true;
            const auto hash = verify_feedback_recording(part); retain(part.recording); indices.push_back(at);
            std::cout << "AUDIO U12L3 block=" << block << " feedback=" << feedback << " correct=" << correct << " part=" << index + 1
                << " sha256=" << hash << " path=" << part.recording.generic_string() << '\n';
        }
        for (std::size_t at = 0; at < source.size(); ++at) check(iswspace(source[at]) || covered[at], "The file-append audio source was truncated");
        check(!indices.empty() && indices.size() == plan.size(), "The file-append passage has no complete real audio plan");
        if (feedback) check(value.index == indices.front() && value.offset == 0 && !value.playing && !value.allocated,
            "Fresh file-append feedback did not retain its zero start and explicit manual pause");
        return indices;
    };
    const auto reopen = [&](const LessonReaderSnapshot& paused) {
        f.reader.reset(); check(flush_lesson_view(f.view), "Cannot checkpoint the isolated file-append lesson");
        const auto data = saved(); const auto history = narration::read_history(data);
        const auto draft = state::get_draft(data, draft_key, goal.initial_code, goal.standard_input.value_or(""));
        const auto answer = state::get_response(data, lesson, question);
        DestroyWindow(f.view); f.view = nullptr;
        LessonViewHooks hooks; hooks.before_page_change = [&](HWND) { if (f.reader) f.reader->pause(); };
        hooks.content_changed = [&](HWND) { if (f.reader) f.reader->content_changed(); };
        hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (f.reader) f.reader->feedback(block, correct); };
        f.view = create_lesson_view(f.host, f.environment, lesson, std::move(hooks)); check(f.view != nullptr, "Cannot reopen the authentic file-append lesson");
        MoveWindow(f.view, 0, 0, 980, 720, TRUE); f.reader = std::make_unique<LessonReader>(f.view, f.environment, backend, [] {}); ready();
        const auto restored = f.snapshot(); const auto restored_data = saved();
        const auto restored_draft = state::get_draft(restored_data, draft_key, goal.initial_code, goal.standard_input.value_or(""));
        const auto restored_answer = state::get_response(restored_data, lesson, question);
        const auto restored_history = narration::read_history(restored_data);
        const auto& expected_part = paused.segments.at(paused.index);
        const auto& actual_part = restored.segments.at(restored.index);
        const auto regions = lesson_reading_regions(f.view);
        std::cout << "RESTORE U12L3 page expected/actual=" << page << '/' << lesson_page_state(f.view).index
            << " key expected=" << paused.key << " actual=" << restored.key
            << " index=" << paused.index << '/' << restored.index << " offset=" << paused.offset << '/' << restored.offset
            << " heard=" << paused.heard_through << '/' << restored.heard_through
            << " finished=" << paused.finished << '/' << restored.finished << " playing=" << restored.playing << " allocated=" << restored.allocated
            << " parts=" << paused.segments.size() << '/' << restored.segments.size()
            << " sourceSha=" << state::sha256(expected_part.source) << '/' << state::sha256(actual_part.source)
            << " currentFeedback=" << expected_part.feedback << '/' << actual_part.feedback
            << " compilerFeedbackRegions=" << std::ranges::count_if(regions, [](const auto& item) { return item.block_index == compiler && item.feedback; }) << '\n';
        std::cout << "RESTORE U12L3 history equal=" << (history == restored_history)
            << " entries=" << history.entries.size() << '/' << restored_history.entries.size()
            << " activeKey=" << history.active_key << '/' << restored_history.active_key
            << " activeOwner=" << history.active_owner << '/' << restored_history.active_owner
            << " draftSaved=" << draft.has_saved << '/' << restored_draft.has_saved
            << " sourceSha=" << state::sha256(draft.source) << '/' << state::sha256(restored_draft.source)
            << " inputSha=" << state::sha256(draft.input) << '/' << state::sha256(restored_draft.input)
            << " indentationSha=" << state::sha256(draft.indentation_json) << '/' << state::sha256(restored_draft.indentation_json)
            << " selection=" << draft.selection_start << ',' << draft.selection_end << '/'
            << restored_draft.selection_start << ',' << restored_draft.selection_end
            << " answerPresent=" << answer.has_value() << '/' << restored_answer.has_value()
            << " answerSha=" << (answer ? state::sha256(answer->raw_json) : "absent") << '/'
            << (restored_answer ? state::sha256(restored_answer->raw_json) : "absent") << '\n';
        check(lesson_page_state(f.view).index == page, "File-append reopen changed its focal page address");
        check(restored.key == paused.key, "File-append reopen changed its exact current narration script key");
        check(restored.index == paused.index, "File-append reopen changed its exact paused segment");
        check(restored.offset == paused.offset, "File-append reopen changed its exact paused audio offset");
        check(restored.heard_through == paused.heard_through, "File-append reopen changed its actual heard boundary");
        check(restored.finished == paused.finished, "File-append reopen changed its finished checkpoint");
        check(!restored.playing && !restored.allocated, "File-append reopen started playback without explicit Play");
        check(restored_history == history, "File-append reopen changed its durable listening history or owner");
        check(restored_draft.has_saved == draft.has_saved, "File-append reopen changed its saved-draft presence");
        check(restored_draft.source == draft.source && edit_text(control(compiler, 10)) == draft.source,
            "File-append reopen changed its stored or displayed exact program source");
        check(restored_draft.input == draft.input, "File-append reopen changed its exact standard-input draft");
        check(restored_draft.indentation_json == draft.indentation_json, "File-append reopen changed its indentation metadata");
        check(restored_draft.selection_start == draft.selection_start && restored_draft.selection_end == draft.selection_end,
            "File-append reopen changed its editor selection");
        check(answer && restored_answer && answer->raw_json == restored_answer->raw_json && answer->correct == restored_answer->correct,
            "File-append reopen changed its independent reviewed choice response");
        const auto until = GetTickCount64() + 200;
        wait([&] { const auto value = f.snapshot(); check(!value.playing && !value.allocated && value.offset == paused.offset,
            "A restored file-append pause started without explicit Play"); return GetTickCount64() >= until; }, "file-append restored pause remains silent", 2000);
        privacy(); preserve();
    };
    const auto play_feedback = [&](const std::vector<std::size_t>& indices, bool correct, bool partial_reopen) {
        std::set<std::size_t> observed;
        const auto observe = [&](const LessonReaderSnapshot& value) { if (value.playing && std::ranges::find(indices, value.index) != indices.end()) observed.insert(value.index); };
        f.reader->toggle();
        if (partial_reopen) {
            wait([&] { const auto value = f.snapshot(); observe(value); return value.playing && value.index == indices.front()
                && value.offset > 30 && value.offset + 30 < value.segments[value.index].duration_ms; }, "real file-append verdict partial playback");
            f.reader->pause(); const auto paused = f.snapshot(); const auto history = narration::read_history(saved());
            const auto slot = std::ranges::find(history.entries, paused.key, &narration::Bookmark::key);
            check(slot != history.entries.end() && slot->position.milliseconds == paused.offset && slot->position.heard_prefix
                && slot->position.heard_prefix->microseconds > 0, "The actual file-append verdict pause has no source-bound playback certificate");
            reopen(paused); f.reader->toggle(); await_resumed_player([&] { return f.snapshot(); }, paused, "Explicit file-append verdict resume");
        }
        wait([&] { const auto value = f.snapshot(); observe(value);
            return !value.playing && !value.allocated && !value.preparing && (correct ? value.waiting_gate || value.finished
                : value.index == indices.back() && value.offset + 1 >= value.segments[value.index].duration_ms);
        }, correct ? "complete file-append success feedback and following reader boundary" : "complete file-append rejected feedback and retry stop", 120000);
        check(std::ranges::all_of(indices, [&](auto at) { return observed.contains(at); }), "The file-append reader skipped complete feedback playback");
        if (!correct) {
            const auto stopped = f.snapshot(); check(!stopped.finished, "Rejected file-append feedback falsely finished the page");
            f.reader->seek_seconds(600); const auto sought = f.snapshot();
            check(sought.segments[sought.index].feedback && sought.segments[sought.index].block == stopped.segments[stopped.index].block
                && !sought.finished && sought.heard_through == stopped.heard_through, "Forward seek escaped a rejected file-append guide or granted hearing");
        }
        preserve();
    };
    check(directories().empty() && narration::read_history(saved()).entries.empty(), "A fresh file-append reader inherited executions or hearing");
    for (const auto& current_page : lesson.pages) for (const auto& block : current_page.blocks)
        check(!state::get_response(saved(), lesson, block), "A fresh file-append reader inherited a response");
    privacy();
    // Preview positioning deliberately earns no hearing of the long task prose.
    f.reader->seek_seconds(600); const auto pending = f.snapshot();
    check(pending.segments[pending.index].block == choice && !pending.finished && pending.heard_through == 0,
        "A forward preview bypassed the untouched actual file-append choice or invented hearing");
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); return value.waiting_gate && !value.playing && !value.allocated; }, "file-append pending choice gate");
    const auto submit = [&](int option) {
        f.reader->pause(); SendMessageW(control(choice, 20 + option * 2), BM_CLICK, 0, 0);
        check(edit_text(control(choice, 4)).empty(), "An unchecked file-append choice retained earlier feedback");
        SendMessageW(control(choice, 3), BM_CLICK, 0, 0); f.reader->pause(); ready(); privacy();
        const auto response = state::get_response(saved(), lesson, question);
        check(response && response->selected == option && (response->correct || state::response_reviewed(*response, question)),
            "The actual file-append choice lost its reviewed native answer");
    };
    submit(1); const auto rejected_choice = parts(choice, true, false, "✗ Incorrecto\n\n" + guide);
    check(edit_text(control(choice, 4)).find(explanation) == std::string::npos, "A rejected file-append choice revealed success feedback");
    play_feedback(rejected_choice, false, true); privacy();
    submit(2); const auto correct_choice = parts(choice, true, true, "✓ Correcto\n\n" + explanation);
    accepted_choice=state::get_response(saved(),lesson,question);
    play_feedback(correct_choice, true, false); const auto compiler_gate = f.snapshot();
    check(compiler_gate.waiting_gate && !compiler_gate.finished && compiler_gate.segments[compiler_gate.index].block == compiler
        && std::ranges::all_of(correct_choice,[&](auto at){return compiler_gate.segments[at].feedback_fully_played;}),
        "The complete correct choice explanation did not stop at the actual required compiler");
    f.reader->seek_seconds(600); check(f.snapshot().segments[f.snapshot().index].block == compiler && !f.snapshot().finished,
        "A forward seek bypassed the untouched file-append compiler"); privacy();
    const auto compile = [&](const std::string& source, const std::string& output, bool correct) {
        f.reader->pause(); if (edit_text(control(compiler, 10)) != source) f.edit_program(source, compiler);
        const auto before = directories(); SendMessageW(control(compiler, 11), BM_CLICK, 0, 0);
        wait([&] { return IsWindowEnabled(control(compiler, 11)) != FALSE; }, "actual file-append compilation and execution");
        f.reader->pause(); ready(); const auto after = directories(); std::vector<std::filesystem::path> added;
        for (const auto& directory : after) if (std::ranges::find(before, directory) == before.end()) added.push_back(directory);
        check(after.size() == before.size() + 1 && added.size() == 1 && added.front().parent_path() == root,
            "One actual file-append action did not create exactly one private execution directory");
        const auto run = added.front();
        check(bytes(run / "main.cpp") == source && bytes(run / "stdin.bin").empty()
            && bytes(run / "execution-stdout.bin") == output && bytes(run / "execution-stderr.bin").empty()
            && std::filesystem::is_regular_file(run / "program.exe") && edit_text(control(compiler, 34)) == "Ejecución terminada (código 0)."
            && windows::compiler_puzzle_completed(saved(), lesson, goal) == correct,
            "The file-append reader verdict differs from its actual source, empty stdin/stderr, exit zero or exact stdout");
        if (correct) check(bytes(run / "serie_lgp.txt") == "3 4 5", "The real accepted program did not retain the exact appended sample");
        else check(!std::filesystem::exists(run / "serie_lgp.txt"), "The empty initial program unexpectedly created a data sample");
        for (const auto& item : std::filesystem::directory_iterator(run)) if (item.is_regular_file()) retain(item.path());
        std::cout << "RUN U12L3 reader directory=" << run.generic_string() << " sourceSha256=" << state::sha256(source)
            << " stdoutSha256=" << state::sha256(output) << " stdinBytes=0 stderrBytes=0 exit=0 correct=" << correct << '\n';
        privacy(); preserve();
    };
    std::cout << "PHASE U12L3 reader initial actual empty program and bare Incorrecto\n";
    compile(goal.initial_code, "", false); const auto wrong_verdict = parts(compiler, true, false, "✗ Incorrecto");
    play_feedback(wrong_verdict, false, true); privacy();
    check(!windows::compiler_puzzle_completed(saved(), lesson, goal), "A heard incorrect verdict granted an execution receipt");
    std::cout << "PHASE U12L3 reader actual author's append reference and bare Correcto before note\n";
    compile(reference, "Total=12", true); const auto correct_verdict = parts(compiler, true, true, "✓ Correcto");
    const auto note_parts = parts(note, false, false, tail.text);
    check(windows::compiler_puzzle_case_count(saved(), lesson, goal) == 1 && directories().size() == 2,
        "The accepted real three-integer append program did not earn exactly its own output receipt in two executions");
    const auto options = lesson_options_snapshot(f.view);
    check(std::ranges::find(options.available_pages, 4) != options.available_pages.end(),
        "The real accepted file-append receipt failed to unlock the posterior reference page");
    check(std::ranges::none_of(correct_verdict,[&](auto at){return f.snapshot().segments[at].feedback_fully_played;}),
        "Fresh file-append success reused playback credit from an earlier verdict");
    std::set<std::size_t> heard; f.reader->toggle();
    wait([&] { const auto value = f.snapshot();
        if (value.playing && (std::ranges::find(correct_verdict, value.index) != correct_verdict.end()
            || std::ranges::find(note_parts, value.index) != note_parts.end())) heard.insert(value.index);
        if (value.playing && value.segments[value.index].block == note)
            check(std::ranges::all_of(correct_verdict,[&](auto at){return value.segments[at].feedback_fully_played;}),
                "The note played before complete source-bound correct-verdict playback");
        return value.finished && !value.playing && !value.allocated && !value.preparing;
    }, "actual complete accepted file-append verdict followed by its complete note", 120000);
    check(std::ranges::all_of(correct_verdict, [&](auto at) { return heard.contains(at); })
        && std::ranges::all_of(correct_verdict,[&](auto at){return f.snapshot().segments[at].feedback_fully_played;})
        && std::ranges::all_of(note_parts, [&](auto at) { return heard.contains(at); }),
        "The accepted file-append reader skipped its bare verdict or part of the full following note");
    const auto completed = f.snapshot(); reopen(completed); preserve();
    check(directories().size()==2&&windows::compiler_puzzle_completed(saved(),lesson,goal)
        &&windows::compiler_puzzle_case_count(saved(),lesson,goal)==1,
        "The final reader reopen changed its two actual executions or its earned aggregate receipt");
    std::cout << "PASS current U12L3 p4 reader: complete authentic lesson isolated at p4, actual choice error/retry and complete success explanation, pending required compiler and reference gates, exactly two real executions (empty and author's append reference), bare Incorrecto/Correcto PCM and word timings, full subsequent note played at 3x, exact real verdict pause/certificate/reopen/resume and finished checkpoint, unchanged editor/input draft, prior WAV/run evidence and source. No earlier-page completion, student authorship, full lesson traversal or human listening acceptance claimed.\n";
}

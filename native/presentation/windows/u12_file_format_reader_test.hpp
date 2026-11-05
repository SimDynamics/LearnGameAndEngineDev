#pragma once
// Focal tests of the current complete U12L1 asset. initial_page selects an
// isolated QA address; it does not answer or bypass earlier curricular gates.
void u12_file_format_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        std::size_t selected_page) {
    check(selected_page < 3, "Invalid U12 file-format reader page");
    const auto asset = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_archivo_formato_y_objetos.json";
    const auto original_bytes = state::read_bytes(asset);
    std::string normalized(reinterpret_cast<const char*>(original_bytes.data()), original_bytes.size());
    std::erase(normalized, '\r');
    check(state::sha256(normalized) == "12b215b2346e9796a1f97a18c0cc1428eb8dd9c9ac641c219ec56fe4ce4369b4",
        "The focal U12L1 reader asset differs from the audited current LF document");
    const auto lesson = windows::load_lesson(asset);
    struct Case {
        std::size_t page, block;
        const char* id;
        int correct; // -1 means output prediction.
        const char* answer;
        const char* wrong;
        std::vector<std::string> guides;
        const char* explanation;
    };
    const std::array<Case, 5> cases{{
        {0, 3, "nombre_no_certifica_datos", 2, "", "", {
            "Distingue lo que indica el nombre de lo que sólo podrías comprobar leyendo los datos.",
            "Separa la representación almacenada de los objetos que el programa utiliza al interpretar sus datos.",
            "Relaciona el nombre, el acuerdo de formato y las comprobaciones del contenido; son datos distintos."},
            "El contenido se valida leyendo e interpretando según un formato definido."},
        {1, 2, "interpretar_ceros_de_presentacion", -1, "7:3", "0007:03", {
            "Sigue cada extracción desde los caracteres de la cadena hasta su destino entero. Después revisa qué insertan las dos variables y el separador en la salida; no copies sin interpretar el texto de entrada."},
            "La lectura decimal produce los enteros 7 y 3. Los ceros iniciales del texto no forman parte de esos valores."},
        {1, 4, "identificador_con_longitud", 1, "", "", {
            "Revisa qué información conserva un valor entero y si incluye cuántos caracteres se usaron para escribirlo.",
            "Distingue conservar los caracteres de comprobar que cumplen la longitud y las reglas exigidas.",
            "Distingue un valor numérico de la secuencia exacta de caracteres; cambiar a otro tipo numérico no añade información sobre cómo estaba escrito."},
            "La cadena conserva los caracteres. Todavía hay que comprobar longitud y caracteres si el formato lo exige."},
        {2, 1, "texto_no_es_entero", -1, "Formato invalido", "7:0", {
            "Comprueba por separado si cada extracción obtiene el entero solicitado. Luego decide qué rama de la condición encadenada se ejecuta y localiza su instrucción de salida."},
            "La extracción compuesta no completó ambos enteros; se toma la rama de fallo sin usar cajas como un dato válido."},
        {2, 2, "campos_intercambiados", 2, "", "", {
            "Compara el orden de las extracciones con el destino escrito a la derecha de cada >>. Revisa si los nombres pueden cambiar ese recorrido.",
            "Ambos destinos tienen el mismo tipo. Separa una incompatibilidad de tipos de una interpretación incorrecta de las posiciones.",
            "Sigue qué parte del texto se asigna a cada destino y quién decide su significado."},
            "La biblioteca sigue el orden de las operaciones; los nombres no corrigen un acuerdo de formato equivocado."}
    }};
    check(lesson.id == "cpp_fundamentals__archivos_y_programa_completo__archivo_formato_y_objetos"
        && lesson.pages.size() == 3, "The current file-format reader owner changed");
    std::vector<const Case*> focal;
    for (const auto& test : cases) {
        const auto& goal = lesson.pages.at(test.page).blocks.at(test.block);
        check(goal.id == test.id && goal.explanation == test.explanation
            && (test.correct >= 0
                ? goal.type == curriculum::BlockType::multiple_choice && goal.correct_index == static_cast<std::size_t>(test.correct)
                    && goal.options.size() == 3 && goal.review_feedback_options == test.guides
                : goal.type == curriculum::BlockType::output_prediction && goal.answers == std::vector<std::string>{test.answer}
                    && goal.review_feedback == test.guides.front()),
            "An audited U12L1 activity, verdict or full authored feedback changed");
        if (test.page == selected_page) focal.push_back(&test);
    }
    const auto cache = std::filesystem::path(build::source_root) / "out/qa/u12-file-format-reader" /
        ("pcm-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(selected_page));
    Fixture f(backend, false, true, true, "es", lesson, true, selected_page, true, false, [](HWND view) {
        update_lesson_preferences(view, [](state::StateData& data) { data.preferences["lesson_narration_settings"]["rate"] = 3.; });
    }, [](state::StateData& data) { data.preferences["u12_file_format_reader_qa"]["opaque"] = std::string("\t conservar\r\n保存"); }, cache);
    struct FailureShutdown { std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown() { if (std::uncaught_exceptions()) worker->shutdown(); } } shutdown{backend};
    const auto ready = [&] {
        wait([&] { const auto value = f.snapshot(); return value.planned && !value.segments.empty()
            && value.index < value.segments.size() && std::ranges::all_of(value.segments, [](const auto& part) { return part.available; });
        }, "complete focal U12 file-format synthesis and alignment", 120000);
    };
    std::cout << "PHASE U12L1 p" << selected_page + 1 << " preparing complete authentic page; no seeded responses/hearing\n";
    ready(); f.reader->pause();
    const auto unrelated = read_lesson_state(f.view).preferences.at("u12_file_format_reader_qa");
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks)
        check(!state::get_response(read_lesson_state(f.view), lesson, block), "The fresh U12 reader profile contains a seeded response");
    check(narration::read_history(read_lesson_state(f.view)).entries.empty() && !IsWindowVisible(f.host),
        "The focal U12 reader inherited hearing or owns a visible desktop window");
    const auto control = [&](std::size_t block, int part) { return child(f.view, 1000 + static_cast<int>(block) * 100 + part); };
    const auto visible = [](std::string text) { std::erase(text, '`'); return text; };
    struct Recording { std::filesystem::path path; std::string hash; std::filesystem::file_time_type stamp; };
    std::vector<Recording> recordings;
    const auto preserve = [&] {
        check(state::read_bytes(asset) == original_bytes && !IsWindowVisible(f.host)
            && read_lesson_state(f.view).preferences.at("u12_file_format_reader_qa") == unrelated,
            "The focal U12 reader changed its source, unrelated QA data or desktop visibility");
        for (const auto& recording : recordings)
            check(state::sha256(state::read_bytes(recording.path)) == recording.hash
                && std::filesystem::last_write_time(recording.path) == recording.stamp,
                "U12 reader playback or restoration rewrote an existing real WAV");
        for (std::size_t page = 0; page < lesson.pages.size(); ++page) if (page != selected_page)
            for (const auto& block : lesson.pages[page].blocks)
                check(!state::get_response(read_lesson_state(f.view), lesson, block), "Focal U12 reader fabricated an earlier/later-page answer");
    };
    const auto privacy = [&] {
        const auto regions = lesson_reading_regions(f.view);
        for (const auto* test : focal) {
            const auto& goal = lesson.pages[test->page].blocks[test->block];
            const auto response = state::get_response(read_lesson_state(f.view), lesson, goal);
            if (test->correct < 0) {
                const auto body = control(test->block, 1), answer = control(test->block, 2);
                check(edit_text(body) == goal.code, "The focal U12 reader changed the complete displayed source code");
                for (const auto& region : regions) check(region.target != body && region.target != answer,
                    "The U12 reader exposed a code body or typed answer as spoken curriculum");
            }
            if (!response || (!response->correct && !state::response_reviewed(*response, goal))) {
                check(edit_text(control(test->block, 4)).empty(), "An unchecked U12 answer exposed visible feedback");
                check(std::ranges::none_of(regions, [&](const auto& region) { return region.block_index == test->block && region.feedback; }),
                    "An unchecked U12 answer exposed spoken feedback");
            }
        }
    };
    const auto feedback = [&](const Case& test, bool correct, const std::string& body) {
        const auto text = std::string(correct ? "✓ Correcto\n\n" : "✗ Incorrecto\n\n") + body;
        const auto source = wide(text); const auto regions = lesson_reading_regions(f.view);
        const auto region = std::ranges::find_if(regions, [&](const auto& item) { return item.block_index == test.block && item.feedback; });
        check(region != regions.end() && region->source_text == text && region->correct == correct && region->solved == correct
            && region->target == control(test.block, 4) && edit_text(region->target) == visible(text),
            "The current U12 feedback omitted or mixed its complete authored source/verdict");
        if (!correct) check(edit_text(region->target).find(visible(test.explanation)) == std::string::npos,
            "A rejected U12 attempt exposed its success explanation");
        const auto plan = backend->plan(text, "es", {}, region->reading_context, region->math_mode);
        const auto snapshot = f.snapshot(); std::vector<std::size_t> indices; std::vector<bool> covered(source.size());
        for (std::size_t at = 0; at < snapshot.segments.size(); ++at) {
            const auto& part = snapshot.segments[at]; if (part.block != test.block || !part.feedback) continue;
            const auto n = indices.size();
            check(n < plan.size() && part.correct == correct && part.region == region->key && part.target == region->target
                && part.source == plan[n].text && part.spoken == plan[n].spoken && part.start == plan[n].start && part.end == plan[n].end
                && part.start < part.end && part.end <= source.size() && part.source == utf8(source.substr(part.start, part.end - part.start)),
                "The U12 feedback audio omitted, reordered or remapped an authored UTF-16 passage");
            for (auto offset = part.start; offset < part.end; ++offset) covered[offset] = true;
            recordings.push_back({part.recording, verify_feedback_recording(part), std::filesystem::last_write_time(part.recording)});
            std::cout << "AUDIO U12L1 p" << selected_page + 1 << ' ' << test.id << " correct=" << correct << " part=" << n + 1
                << " sha256=" << recordings.back().hash << " path=" << part.recording.generic_string() << '\n';
            indices.push_back(at);
        }
        for (std::size_t at = 0; at < source.size(); ++at) check(iswspace(source[at]) || covered[at], "The U12 feedback plan truncated its complete source");
        check(!indices.empty() && indices.size() == plan.size() && snapshot.index == indices.front() && snapshot.offset == 0
            && !snapshot.playing && !snapshot.allocated, "New U12 feedback lost its zero start or overrode manual pause");
        return indices;
    };
    const auto reopen = [&](const LessonReaderSnapshot& paused) {
        const auto protected_data = read_lesson_state(f.view);
        f.reader.reset(); check(flush_lesson_view(f.view), "Cannot flush the isolated U12 reader");
        const auto durable = narration::read_history(read_lesson_state(f.view)); DestroyWindow(f.view); f.view = nullptr;
        LessonViewHooks hooks; hooks.before_page_change = [&](HWND) { if (f.reader) f.reader->pause(); };
        hooks.content_changed = [&](HWND) { if (f.reader) f.reader->content_changed(); };
        hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (f.reader) f.reader->feedback(block, correct); };
        f.view = create_lesson_view(f.host, f.environment, lesson, std::move(hooks)); check(f.view != nullptr, "Cannot reopen the complete U12 lesson view");
        MoveWindow(f.view, 0, 0, 980, 720, TRUE); f.reader = std::make_unique<LessonReader>(f.view, f.environment, backend, [] {}); ready();
        const auto restored = f.snapshot();
        check(lesson_page_state(f.view).index == selected_page && restored.key == paused.key && restored.index == paused.index
            && restored.offset == paused.offset && restored.heard_through == paused.heard_through && restored.finished == paused.finished
            && !restored.playing && !restored.allocated && narration::read_history(read_lesson_state(f.view)) == durable,
            "The U12 whole-view reopen changed its exact real pause/history or autostarted");
        for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
            const auto before = state::get_response(protected_data, lesson, block), after = state::get_response(read_lesson_state(f.view), lesson, block);
            check(before.has_value() == after.has_value() && (!before || (before->raw_json == after->raw_json
                && before->correct == after->correct && before->answer == after->answer && before->selected == after->selected
                && before->reviewed_answer_identity == after->reviewed_answer_identity)), "Reopening U12 changed an independently owned response");
        }
        const auto until = GetTickCount64() + 200;
        wait([&] { const auto value = f.snapshot(); check(!value.playing && !value.allocated && value.offset == paused.offset,
            "A restored U12 pause started without explicit Play"); return GetTickCount64() >= until; }, "U12 restored pause stays silent", 2000);
        privacy(); preserve();
    };
    privacy();
    // A preview can reach the last prompt/option of the first pending block,
    // but cannot grant hearing or cross that ordinary puzzle's reader gate.
    f.reader->seek_seconds(600); const auto preview = f.snapshot();
    check(preview.segments[preview.index].block == focal.front()->block && !preview.finished && preview.heard_through == 0
        && !lesson_page_state(f.view).can_next, "A forward preview bypassed the first actual U12 puzzle or granted hearing");
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); return value.waiting_gate && !value.playing && !value.allocated; }, "actual pending U12 puzzle reader gate");
    check(f.snapshot().segments[f.snapshot().index].block == focal.front()->block, "The pending U12 reader crossed into a later block");
    bool reopened = false;
    for (std::size_t number = 0; number < focal.size(); ++number) {
        const auto& test = *focal[number]; const auto& goal = lesson.pages[test.page].blocks[test.block];
        const auto submit = [&](int option, std::string_view answer) {
            f.reader->pause();
            if (test.correct >= 0) SendMessageW(control(test.block, 20 + option * 2), BM_CLICK, 0, 0);
            else { const auto value = wide(answer); const auto edit = control(test.block, 2);
                SendMessageW(edit, EM_SETSEL, 0, -1); SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value.c_str())); }
            privacy(); SendMessageW(control(test.block, 3), BM_CLICK, 0, 0); f.reader->pause(); ready(); privacy();
            const auto response = state::get_response(read_lesson_state(f.view), lesson, goal);
            check(response && (response->correct || state::response_reviewed(*response, goal))
                && (test.correct >= 0 ? response->selected == option : response->answer == answer), "The actual U12 native controls lost the submitted answer");
        };
        const auto play = [&](const std::vector<std::size_t>& indices, bool correct, bool pause_restore) {
            std::set<std::size_t> observed;
            const auto observe = [&](const LessonReaderSnapshot& value) { if (value.playing && std::ranges::find(indices, value.index) != indices.end()) observed.insert(value.index); };
            f.reader->toggle();
            if (pause_restore) {
                wait([&] { const auto value = f.snapshot(); observe(value); return value.playing && value.index == indices.front()
                    && value.offset > 120 && value.offset + 150 < value.segments[value.index].duration_ms; }, "real U12 guide partial playback");
                f.reader->pause(); const auto paused = f.snapshot();
                const auto history = narration::read_history(read_lesson_state(f.view));
                const auto slot = std::ranges::find(history.entries, paused.key, &narration::Bookmark::key);
                check(slot != history.entries.end() && slot->position.milliseconds == paused.offset && slot->position.heard_prefix
                    && slot->position.heard_prefix->microseconds > 0, "The U12 pause has no actual source-bound playback certificate");
                reopen(paused); f.reader->toggle(); await_resumed_player([&] { return f.snapshot(); }, paused, "Explicit U12 guide resume");
            }
            wait([&] { const auto value = f.snapshot(); observe(value);
                if (!correct) return !value.playing && !value.allocated && !value.preparing && value.index == indices.back()
                    && value.offset + 1 >= value.segments[value.index].duration_ms;
                return !value.playing && !value.allocated && !value.preparing && (value.waiting_gate || value.finished);
            }, correct ? "complete U12 success explanation and real following boundary" : "complete U12 guide playback and retry stop", 120000);
            const auto ended = f.snapshot();
            check(std::ranges::all_of(indices, [&](auto at) { return observed.contains(at); }), "Actual U12 playback skipped part of its full authored feedback");
            if (!correct) {
                check(!ended.finished && !ended.segments[ended.index].correct && !lesson_page_state(f.view).can_next,
                    "A rejected U12 guide completed the page or escaped the retry boundary");
                f.reader->seek_seconds(600); const auto sought = f.snapshot();
                check(sought.segments[sought.index].feedback && sought.segments[sought.index].block == test.block
                    && !sought.finished && sought.heard_through == ended.heard_through,
                    "Seeking escaped the unresolved U12 guide or invented hearing");
            } else {
                check(ended.heard_through > static_cast<std::int64_t>(indices.back()), "U12 success continued without complete feedback hearing");
                if (number + 1 < focal.size()) check(ended.waiting_gate && !ended.finished
                    && ended.segments[ended.index].block == focal[number + 1]->block && !lesson_page_state(f.view).can_next,
                    "The first U12 success bypassed the second pending puzzle");
                else check(ended.finished && (selected_page == 2 || lesson_page_state(f.view).can_next),
                    "The complete focal U12 page failed its real end/navigation boundary");
            }
            preserve();
        };
        for (int option = 0; option < (test.correct >= 0 ? 3 : 1); ++option) {
            if (option == test.correct) continue;
            std::cout << "PHASE U12L1 p" << selected_page + 1 << ' ' << test.id << " rejected option=" << option << '\n';
            submit(option, test.wrong); const auto parts = feedback(test, false, test.guides[test.correct >= 0 ? option : 0]);
            check(!state::get_response(read_lesson_state(f.view), lesson, goal)->correct, "The deliberately wrong U12 attempt acquired success");
            play(parts, false, !reopened); reopened = true;
        }
        submit(test.correct, test.answer); const auto parts = feedback(test, true, test.explanation);
        check(state::get_response(read_lesson_state(f.view), lesson, goal)->correct, "The actual corrected U12 answer failed its independent oracle");
        play(parts, true, false);
    }
    const auto completed = f.snapshot(); reopen(completed);
    std::cout << "PASS current U12L1 p" << selected_page + 1 << ": complete original lesson in isolated page profile; all local distractors, actual pending/retry/next-puzzle gates, full authored guide/success PCM and UTF-16 word timing, real 3x playback, exact silent pause/reopen/resume and finished checkpoint; code/input targets excluded, other-page responses absent and JSON/WAV unchanged. No C++ execution, earlier-page completion or human listening acceptance claimed.\n";
}

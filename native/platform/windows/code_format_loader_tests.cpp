#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "core/narration_state.hpp"
#include "core/lesson.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace lgaed;

void check(bool allowed,std::string_view description) {
    if(!allowed)throw std::runtime_error(std::string(description));
}

void verify_locale(const std::filesystem::path& path,std::string_view locale) {
    const auto original=windows::load_lesson(path/(std::string(locale)+".before.json"),locale);
    const auto formatted=windows::load_lesson(path/(std::string(locale)+".after.json"),locale);
    check(original.id==formatted.id&&original.locale==formatted.locale&&
        original.pages.size()==formatted.pages.size()&&original.pages.size()==1,
        "The reformatted lesson changed its logical owner or number of pages");
    check(original.state_layout.signatures==formatted.state_layout.signatures&&
        original.state_layout.lineage==formatted.state_layout.lineage&&
        original.state_layout.legacy_lineage==formatted.state_layout.legacy_lineage,
        "A source-format revision changed the immutable state signatures or lineage");
    check(formatted.state_layout.source_format_revisions.size()==5,
        "One of the five authenticated source-format archives was not loaded");

    const auto& initial=original.pages.front().blocks;
    const auto& current=formatted.pages.front().blocks;
    check(initial.size()==current.size()&&initial.size()==6,
        "The formatted lesson lost a presentation, activity or reference");
    for(std::size_t i=0;i<initial.size();++i) {
        const auto& before=initial[i],after=current[i];
        check(before.stable_id==after.stable_id&&before.type==after.type&&
            before.address.page==after.address.page&&before.address.block==after.address.block&&
            curriculum::exercise_key(original,before)==curriculum::exercise_key(formatted,after)&&
            curriculum::draft_key("programming",original,before)==
                curriculum::draft_key("programming",formatted,after),
            "Formatting changed a historical exercise owner or draft storage key");
    }
    check(initial[0].code!=current[0].code&&current[0].code.find("\n\n")!=std::string::npos,
        "Formatted example does not expose new physical source line breaks");
    check(initial[1].code!=current[1].code&&initial[1].answers==current[1].answers,
        "Reformatted prediction changed the expected student answer");
    check(initial[2].fragments!=current[2].fragments&&
        initial[2].correct_order==current[2].correct_order&&
        curriculum::order_representation_frame(initial[2])==
        curriculum::order_representation_frame(current[2]),
        "The graded order puzzle lost its canonical original representation");
    check(initial[3].code_template!=current[3].code_template&&
        initial[3].correct_order==current[3].correct_order,
        "The drag-fill template changed its holes or canonical choices");
    check(initial[4].initial_code!=current[4].initial_code&&
        initial[4].project_files!=current[4].project_files&&
        initial[4].output_check.has_value()&&current[4].output_check.has_value()&&
        curriculum::reference_objective_frame(original,initial[4])==
        curriculum::reference_objective_frame(formatted,current[4]),
        "A compiler formatting revision changed the stored execution objective");
    check(current[4].source_format_original_initial_code==initial[4].initial_code&&
        current[4].source_format_original_project_files==initial[4].project_files,
        "The compiler lost its exact pre-format source or project file snapshot");

    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    state::Response prior;
    prior.correct=true;prior.order={0,1,2,3};
    state::put_response(saved,original,initial[2],prior);
    state::Draft draft;
    draft.source="student's exact unformatted C++ source\nreturn 0;";
    draft.input="7\n";
    draft.selection_start=3;draft.selection_end=9;draft.has_saved=true;
    const auto key=curriculum::draft_key("programming",original,initial[4]);
    state::put_draft(saved,key,draft);
    state::save_current_page(saved,original,0);
    const auto bytes=state::encode(saved);
    const auto reopened=state::decode(bytes);
    check(reopened==saved,"State encoding changed student progress or preferences");
    state::validate_lesson_state(reopened,formatted);
    const auto restored=state::get_response(reopened,formatted,current[2]);
    check(restored.has_value()&&restored->correct&&restored->order==prior.order,
        "A previously graded ordering answer was lost after source formatting");
    const auto loaded=state::get_draft(reopened,
        curriculum::draft_key("programming",formatted,current[4]),current[4].initial_code,
        current[4].standard_input.value_or(""));
    check(loaded.has_saved&&loaded.source==draft.source&&loaded.input==draft.input&&
        loaded.selection_start==draft.selection_start&&loaded.selection_end==draft.selection_end,
        "Reopening a revised lesson replaced the student's saved source or cursor");
    check(state::current_page(reopened,formatted)==0,
        "The historic page position moved after source formatting");

    const auto metadata=windows::load_lesson_progress_metadata(
        path/(std::string(locale)+".after.json"),locale);
    check(metadata.state_layout.lineage==formatted.state_layout.lineage&&
        metadata.pages.size()==formatted.pages.size()&&
        metadata.pages.front().blocks.size()==current.size(),
        "Read-only course progress uses a different historical source mapping");
}

void verify_real_curricular_pilot(const std::filesystem::path& fixtures) {
    // Genuine authored U4 lesson, copied only to CI's temporary fixture area.
    // Work owns the production lesson, and its source bytes remain untouched.
    const auto before=windows::load_lesson(fixtures/"real.before.json","es");
    const auto after=windows::load_lesson(fixtures/"real.after.json","es");
    check(before.id=="cpp_fundamentals__expresiones_y_decisiones__construir_decisiones_desde_un_problema"&&
        before.id==after.id&&before.pages.size()==6&&after.pages.size()==6,
        "The real U4 pilot did not load its published original lesson");
    check(before.state_layout.signatures==after.state_layout.signatures&&
        before.state_layout.lineage==after.state_layout.lineage&&
        before.state_layout.legacy_lineage==after.state_layout.legacy_lineage&&
        after.state_layout.source_format_revisions.size()==2,
        "Real lesson formatting changed stable historical ownership");

    // A later unrelated editorial change makes it impossible to create an
    // authentic complete pre-format lesson from the current publication.
    // Both fixture files must remain loadable; the previous source comes
    // from the same authenticated format archive that protects progress.
    const bool archived_before=before.state_layout.source_format_revisions.size()==2;
    check(archived_before||before.state_layout.source_format_revisions.empty(),
        "The pilot's prior document has an unexpected format history");
    const std::array<std::pair<std::size_t,std::size_t>,2> samples{{{3,2},{5,2}}};
    for(const auto [page,index]:samples) {
        const auto& old=before.pages[page].blocks[index];
        const auto& changed=after.pages[page].blocks[index];
        std::string original_code=old.code;
        if(archived_before) {
            const auto& revisions=after.state_layout.source_format_revisions;
            const auto found=std::find_if(revisions.begin(),revisions.end(),
                [&](const auto& revision){return revision.id==changed.stable_id;});
            check(found!=revisions.end(),"The U4 code archive lost its original owner");
            const auto archived=winrt::Windows::Data::Json::JsonObject::Parse(
                winrt::to_hstring(found->raw_json));
            original_code=winrt::to_string(archived.GetNamedString(L"code"));
            check(old.code==changed.code,
                "A comparison-only historic reading was passed off as loadable JSON");
        }
        check(old.type==curriculum::BlockType::code&&changed.type==old.type&&
            original_code!=changed.code&&
            changed.code.find("\n\n    if (")!=std::string::npos&&
            changed.code.find("\n\n    return 0;")!=std::string::npos&&
            old.stable_id==changed.stable_id&&
            curriculum::exercise_key(before,old)==curriculum::exercise_key(after,changed),
            "Actual protected U4 code block did not gain safe physical line breaks");
    }
    for(std::size_t page=0;page<before.pages.size();++page) {
        const auto& old_page=before.pages[page],new_page=after.pages[page];
        check(old_page.blocks.size()==new_page.blocks.size(),"Real formatting lost curricular blocks");
        for(std::size_t index=0;index<old_page.blocks.size();++index) {
            const auto& old=old_page.blocks[index],changed=new_page.blocks[index];
            check(old.stable_id==changed.stable_id&&
                curriculum::exercise_key(before,old)==curriculum::exercise_key(after,changed)&&
                curriculum::draft_key("programming",before,old)==
                    curriculum::draft_key("programming",after,changed),
                "Real formatting changed the student's exercise or draft key");
            if(curriculum::has_compiler_objective(old)) {
                check(curriculum::has_compiler_objective(changed)&&
                    curriculum::reference_objective_frame(before,old)==
                        curriculum::reference_objective_frame(after,changed),
                    "Real formatting changed a different editor's authenticated compiler goal");
            }
        }
    }
    const auto& prediction_before=before.pages[3].blocks[3];
    const auto& prediction_after=after.pages[3].blocks[3];
    check(prediction_before.type==curriculum::BlockType::output_prediction&&
        prediction_before.answers==prediction_after.answers&&
        prediction_before.code==prediction_after.code,
        "Real format pilot altered an evaluated prediction or its original code");

    state::StateData saved;
    state::prepare_lesson_state(saved,before);
    state::Response response;response.correct=true;response.answer=prediction_before.answers.front();
    state::put_response(saved,before,prediction_before,response);
    const auto& editor_before=before.pages[3].blocks[6];
    const auto& editor_after=after.pages[3].blocks[6];
    check(editor_before.type==curriculum::BlockType::cpp_compiler&&
        editor_after.type==editor_before.type,
        "The authentic U4 compiler editor moved during formatting");
    state::Draft draft;
    draft.source="int main() {\n // estudiante: original intacto\n return 0;\n}\n";
    draft.input="18 5 1\n";draft.selection_start=15;draft.selection_end=15;draft.has_saved=true;
    state::put_draft(saved,curriculum::draft_key("programming",before,editor_before),draft);
    state::save_current_page(saved,before,3);
    const auto archived=state::decode(state::encode(saved));
    check(saved==archived,"Actual pilot rewrote the student's historical preferences");
    state::validate_lesson_state(archived,after);
    const auto answer=state::get_response(archived,after,prediction_after);
    check(answer&&answer->correct&&answer->answer==response.answer,
        "Correct historical prediction did not survive the real source edit");
    const auto retrieved=state::get_draft(archived,
        curriculum::draft_key("programming",after,editor_after),
        editor_after.initial_code,editor_after.standard_input.value_or(""));
    check(retrieved.has_saved&&retrieved.source==draft.source&&
        retrieved.input==draft.input&&
        retrieved.selection_start==draft.selection_start&&
        retrieved.selection_end==draft.selection_end&&
        state::current_page(archived,after)==3,
        "The real U4 lesson reopened with a different draft, cursor or page");
    const auto catalog=windows::load_lesson_progress_metadata(fixtures/"real.after.json","es");
    check(catalog.state_layout.lineage==after.state_layout.lineage&&
        catalog.pages.size()==after.pages.size(),
        "The real U4 read-only curriculum catalog lost its historical identity");
}

void verify_real_u6_compiler_diagnostics(const std::filesystem::path& fixtures) {
    // Three already-published multi-file editors have exact, reviewed
    // diagnostics. The archive must authenticate every old source byte while
    // current text may acquire harmless physical blank lines.
    const auto before=windows::load_lesson(fixtures/"u6.before.json","es");
    const auto after=windows::load_lesson(fixtures/"u6.after.json","es");
    check(before.id=="cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas"&&
        after.id==before.id && after.state_layout.signatures==before.state_layout.signatures &&
        after.state_layout.lineage==before.state_layout.lineage &&
        after.state_layout.legacy_lineage==before.state_layout.legacy_lineage &&
        after.state_layout.source_format_revisions.size()==
            before.state_layout.source_format_revisions.size()+3,
        "U6L2 diagnostic formatting changed published lesson lineage or omitted source archives");

    const std::array<std::pair<std::size_t,std::size_t>,3> editors{{{14,2},{15,2},{17,2}}};
    const std::array<std::string_view,3> diagnostic_names{{
        "u6-difference-unselected-definition",
        "u6-difference-conflicting-return",
        "u6-difference-missing-header"}};
    state::StateData saved;
    state::prepare_lesson_state(saved,before);
    for(std::size_t n=0;n<editors.size();++n) {
        const auto [page,index]=editors[n];
        const auto& historical=before.pages[page].blocks[index];
        const auto& current=after.pages[page].blocks[index];
        check(historical.type==curriculum::BlockType::cpp_compiler &&
            current.type==historical.type && historical.compile_check &&
            current.compile_check &&
            current.compile_check->diagnostic==diagnostic_names[n] &&
            current.compile_check->diagnostic==historical.compile_check->diagnostic &&
            current.source_files==historical.source_files &&
            current.stable_id==historical.stable_id &&
            current.address.page==historical.address.page &&
            current.address.block==historical.address.block,
            "A U6L2 compiler diagnostic changed its source selection or historical owner");
        check(historical.initial_code!=current.initial_code &&
            historical.project_files!=current.project_files &&
            current.source_format_original_initial_code==historical.initial_code &&
            current.source_format_original_project_files==historical.project_files &&
            curriculum::exercise_key(before,historical)==
                curriculum::exercise_key(after,current) &&
            curriculum::draft_key("programming",before,historical)==
                curriculum::draft_key("programming",after,current),
            "The U6L2 format archive did not retain the exact old initial project and draft keys");
        state::Draft draft;
        draft.source="int main() {\n    // saved student draft\n    return 0;\n}\n";
        draft.input="unmodified\n";
        draft.selection_start=3;draft.selection_end=13;draft.has_saved=true;
        state::put_draft(saved,curriculum::draft_key("programming",before,historical),draft);
    }
    const auto reopened=state::decode(state::encode(saved));
    check(reopened==saved,"U6L2 formatting mutated the saved state codec");
    state::validate_lesson_state(reopened,after);
    for(const auto [page,index]:editors) {
        const auto& current=after.pages[page].blocks[index];
        const auto restored=state::get_draft(reopened,
            curriculum::draft_key("programming",after,current),
            current.initial_code,current.standard_input.value_or(""));
        check(restored.has_saved&&restored.source=="int main() {\n    // saved student draft\n    return 0;\n}\n"&&
            restored.input=="unmodified\n" &&
            restored.selection_start==3&&restored.selection_end==13,
            "U6L2 formatting lost a student draft or cursor after reopening");
    }
    const auto index=windows::load_lesson_progress_metadata(fixtures/"u6.after.json","es");
    check(index.state_layout.signatures==after.state_layout.signatures &&
        index.state_layout.lineage==after.state_layout.lineage &&
        index.pages.size()==after.pages.size(),
        "The Windows read-only curriculum inventory rejected a legitimate formatted U6L2 diagnostic");
}

void verify_visual_separators_keep_order_and_history(const std::filesystem::path& fixtures) {
    using namespace winrt::Windows::Data::Json;
    const auto file=fixtures/"es.after.json";
    const auto original=windows::load_lesson(file,"es");
    check(original.pages.size()==1&&original.pages.front().blocks.size()==6&&
        original.pages.front().blocks[2].type==curriculum::BlockType::order_code&&
        !original.pages.front().blocks[2].stable_id.empty(),
        "Visual separator QA lost its real historical order-code owner");
    std::ifstream stream(file,std::ios::binary);
    check(stream.good(),"Missing exact Spanish format fixture for separator verification");
    const std::string bytes{std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>()};
    const auto owner=original.pages.front().blocks[2].stable_id;
    const auto expanded=[&](int count,std::string_view identity) {
        const auto root=JsonObject::Parse(winrt::to_hstring(bytes));
        JsonObject record;
        record.Insert(L"activity",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
        record.Insert(L"count",JsonValue::CreateNumberValue(count));
        JsonArray declarations;declarations.Append(record);
        root.Insert(L"orderVisualSeparators",declarations);
        return winrt::to_string(root.Stringify());
    };
    for(const int count:{1,4}) {
        const auto raw=expanded(count,owner);
        const auto full=windows::parse_lesson_json(raw,"es");
        const auto catalog=windows::parse_lesson_progress_metadata(raw,"es");
        const auto& source=original.pages.front().blocks[2];
        const auto& visual=full.pages.front().blocks[2];
        const auto& projected=catalog.pages.front().blocks[2];
        check(visual.optional_order_separators==count&&
            projected.optional_order_separators==count&&
            visual.stable_id==source.stable_id&&
            visual.correct_order==source.correct_order&&
            visual.fragments==source.fragments&&
            full.state_layout.signatures==original.state_layout.signatures&&
            full.state_layout.lineage==original.state_layout.lineage&&
            curriculum::exercise_key(full,visual)==curriculum::exercise_key(original,source),
            "A purely visual order separator altered grading or historic storage");
    }
    for(const int bad:{0,5,8,1000}) {
        const auto raw=expanded(bad,owner);
        for(const bool catalog:{false,true}) {
            bool rejected=false;
            try {
                if(catalog)(void)windows::parse_lesson_progress_metadata(raw,"es");
                else(void)windows::parse_lesson_json(raw,"es");
            }catch(const std::exception&){rejected=true;}
            check(rejected,"Windows accepted an out-of-range optional order-card count");
        }
    }
    // Root declarations are not an escape hatch around a stored answer:
    // reject duplicate owners, a non-order owner, foreign fields and a
    // missing/ill-typed count in both full and read-only catalog loaders.
    const auto must_reject_declaration=[&](const JsonObject& payload,
            std::string_view description){
        const auto raw=winrt::to_string(payload.Stringify());
        for(const bool catalog:{false,true}){
            bool rejected=false;
            try{
                if(catalog)(void)windows::parse_lesson_progress_metadata(raw,"es");
                else(void)windows::parse_lesson_json(raw,"es");
            }catch(const std::exception&){rejected=true;}
            check(rejected,description);
        }
    };
    const auto prepare=[&]{
        return JsonObject::Parse(winrt::to_hstring(expanded(2,owner)));
    };
    {
        const auto root=prepare();
        auto entries=root.GetNamedArray(L"orderVisualSeparators");
        entries.Append(JsonObject::Parse(entries.GetObjectAt(0).Stringify()));
        must_reject_declaration(root,"A duplicate visual card declaration reused the same order puzzle");
    }
    {
        const auto root=prepare();
        const auto foreign=original.pages.front().blocks[0].stable_id;
        check(foreign!=owner,"The invalid-owner test has no independent code block");
        root.GetNamedArray(L"orderVisualSeparators").GetObjectAt(0).Insert(
            L"activity",JsonValue::CreateStringValue(winrt::to_hstring(foreign)));
        must_reject_declaration(root,"A non-order block received drag-order visual cards");
    }
    {
        const auto root=prepare();
        root.GetNamedArray(L"orderVisualSeparators").GetObjectAt(0).Insert(
            L"extra",JsonValue::CreateBooleanValue(true));
        must_reject_declaration(root,"Unknown visual-separator metadata was admitted");
    }
    {
        const auto root=prepare();
        root.GetNamedArray(L"orderVisualSeparators").GetObjectAt(0).Remove(L"count");
        must_reject_declaration(root,"A missing visual-separator count was admitted");
    }
    for(const auto invalid:{JsonValue::CreateStringValue(L"2"),
                            JsonValue::CreateBooleanValue(true),
                            JsonValue::CreateNumberValue(1.5),
                            JsonValue::CreateNullValue()}){
        const auto root=prepare();
        root.GetNamedArray(L"orderVisualSeparators").GetObjectAt(0).Insert(
            L"count",invalid);
        must_reject_declaration(root,"An invalid visual-separator count type was admitted");
    }
    must_reject_declaration(JsonObject::Parse(winrt::to_hstring(
        expanded(1,"unknown_order_owner"))),
        "Visual order cards were assigned to an unknown historical owner");
}

void must_reject(const std::filesystem::path& file) {
    for(const bool catalog:{false,true}) {
        bool rejected=false;
        try {
            if(catalog)(void)windows::load_lesson_progress_metadata(file,"es");
            else (void)windows::load_lesson(file,"es");
        }catch(const std::exception&) {rejected=true;}
        check(rejected,"A forged contract or original archive was admitted by Windows");
    }
}

void verify_additional_real_compile_diagnostics(const std::filesystem::path& fixtures) {
    struct Sample {std::string_view label,lesson,diagnostic;std::size_t page,index;bool project;};
    const std::array<Sample,2> samples{{
        {"u6more",
         "cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos",
         "u6-envelopes-missing-header",16,2,true},
        {"u9",
         "cpp_fundamentals__datos_propios_y_estado_valido__consultas_const_y_objetos_constantes",
         "const-member-call",6,6,false}
    }};
    for(const auto& sample:samples) {
        const auto before_file=fixtures/(std::string(sample.label)+".before.json");
        const auto after_file=fixtures/(std::string(sample.label)+".after.json");
        const auto before=windows::load_lesson(before_file,"es");
        const auto after=windows::load_lesson(after_file,"es");
        check(before.id==sample.lesson&&after.id==before.id&&
            before.state_layout.signatures==after.state_layout.signatures&&
            before.state_layout.lineage==after.state_layout.lineage&&
            before.state_layout.legacy_lineage==after.state_layout.legacy_lineage&&
            after.state_layout.source_format_revisions.size()==
                before.state_layout.source_format_revisions.size()+1,
            "A U6/U9 compiler format changed historical state or the original lesson");
        const auto& original=before.pages.at(sample.page).blocks.at(sample.index);
        const auto& formatted=after.pages.at(sample.page).blocks.at(sample.index);
        check(original.type==curriculum::BlockType::cpp_compiler&&
            formatted.type==original.type&&original.compile_check&&formatted.compile_check&&
            original.compile_check->diagnostic==sample.diagnostic&&
            formatted.compile_check->diagnostic==original.compile_check->diagnostic&&
            original.stable_id==formatted.stable_id&&
            original.address.page==formatted.address.page&&
            original.address.block==formatted.address.block&&
            original.source_files==formatted.source_files&&
            original.initial_code!=formatted.initial_code&&
            formatted.source_format_original_initial_code==original.initial_code&&
            (sample.project?
                (original.project_files!=formatted.project_files&&
                 formatted.source_format_original_project_files==original.project_files):
                (original.project_files.empty()&&formatted.project_files.empty()))&&
            curriculum::exercise_key(before,original)==curriculum::exercise_key(after,formatted)&&
            curriculum::draft_key("programming",before,original)==
                curriculum::draft_key("programming",after,formatted)&&
            curriculum::reference_objective_frame(before,original)==
                curriculum::reference_objective_frame(after,formatted),
            "The reviewed compiler changed source selection, diagnosis, result or draft key");
        state::StateData saved;
        state::prepare_lesson_state(saved,before);
        state::Draft draft;
        draft.source="int main() {\n    // student source\n    return 0;\n}\n";
        draft.input="unchanged input\n";draft.selection_start=5;draft.selection_end=18;
        draft.has_saved=true;
        state::put_draft(saved,curriculum::draft_key("programming",before,original),draft);
        const auto reopened=state::decode(state::encode(saved));
        check(reopened==saved,"A diagnostic format changed the persisted state");
        state::validate_lesson_state(reopened,after);
        const auto restored=state::get_draft(reopened,
            curriculum::draft_key("programming",after,formatted),
            formatted.initial_code,formatted.standard_input.value_or(""));
        check(restored.has_saved&&restored.source==draft.source&&
            restored.input==draft.input&&
            restored.selection_start==draft.selection_start&&
            restored.selection_end==draft.selection_end,
            "A formatted diagnostic lost its saved C++ code, input or cursor");
        const auto catalog=windows::load_lesson_progress_metadata(after_file,"es");
        check(catalog.state_layout.lineage==after.state_layout.lineage&&
            catalog.pages.size()==after.pages.size()&&
            catalog.pages.at(sample.page).blocks.size()==after.pages.at(sample.page).blocks.size(),
            "The progress-only loader rejected an authentic compiled diagnostic");
        for(const auto variant:{"forged-source","forged-goal","forged-archive"})
            must_reject(fixtures/(std::string(sample.label)+"."+variant+".json"));
    }
}
void verify_real_u7_question_and_spacing(const std::filesystem::path& fixtures) {
    // Authenticated U7L3 source/prompt copies, never edits to actual lessons.
    const auto before_file=fixtures/"u7-question.before.json";
    const auto after_file=fixtures/"u7-question.after.json";
    const auto before=windows::load_lesson(before_file,"es");
    const auto after=windows::load_lesson(after_file,"es");
    check(before.id=="cpp_fundamentals__errores_y_construccion_gradual__validar_antes_y_elegir_reporte"&&
          after.id==before.id&&before.state_layout.lineage==after.state_layout.lineage&&
          before.state_layout.signatures==after.state_layout.signatures&&
          before.state_layout.legacy_lineage==after.state_layout.legacy_lineage&&
          after.state_layout.source_format_revisions.size()==
              before.state_layout.source_format_revisions.size()+1,
          "Combined U7L3 source/question edit changed the published history");
    const auto& old=before.pages.at(0).blocks.at(3);
    const auto& revised=after.pages.at(0).blocks.at(3);
    check(old.type==curriculum::BlockType::output_prediction&&
          revised.type==old.type&&old.stable_id=="block_0000_0003"&&
          old.stable_id==revised.stable_id&&old.address.page==revised.address.page&&
          old.address.block==revised.address.block&&old.code!=revised.code&&
          old.question!=revised.question&&old.explanation!=revised.explanation&&
          old.answers==revised.answers&&
          curriculum::exercise_key(before,old)==curriculum::exercise_key(after,revised)&&
          curriculum::draft_key("programming",before,old)==
              curriculum::draft_key("programming",after,revised),
          "Question/source spacing changed U7L3 answer, identity or archived student state");
    const auto archive=std::find_if(after.state_layout.source_format_revisions.begin(),
                                   after.state_layout.source_format_revisions.end(),
        [&](const auto& item){return item.id==revised.stable_id;});
    check(archive!=after.state_layout.source_format_revisions.end(),
          "The real U7L3 text/source change has no complete source archive");
    const auto original=winrt::Windows::Data::Json::JsonObject::Parse(
        winrt::to_hstring(archive->raw_json));
    check(winrt::to_string(original.GetNamedString(L"code"))==old.code&&
          winrt::to_string(original.GetNamedString(L"question"))==old.question,
          "Real U7L3 archive replaced its original question or code");

    state::StateData saved;
    state::prepare_lesson_state(saved,before);
    state::Response response;
    response.correct=true;
    response.answer=old.answers.front();
    state::put_response(saved,before,old,response);
    state::save_current_page(saved,before,0);
    const auto reopened=state::decode(state::encode(saved));
    state::validate_lesson_state(reopened,after);
    const auto retained=state::get_response(reopened,after,revised);
    check(retained&&retained->correct&&retained->answer==response.answer&&
          state::current_page(reopened,after)==0,
          "Win32 import lost the previously correct U7L3 prediction");
    const auto catalog=windows::load_lesson_progress_metadata(after_file,"es");
    check(catalog.state_layout.signatures==after.state_layout.signatures&&
          catalog.state_layout.lineage==after.state_layout.lineage&&
          catalog.pages.at(0).blocks.at(3).stable_id==revised.stable_id,
          "The progress-only Windows loader lost the reviewed U7L3 owner");
    for(const auto forged:{"changed-answer","stale-question","unmarked-question","forged-source"})
        must_reject(fixtures/(std::string("u7-question.")+forged+".json"));
}

void verify_real_u12_editorials(const std::filesystem::path& fixtures) {
    struct Sample {std::string_view tag,lesson,id;std::size_t p,b;bool choice;};
    constexpr std::array<Sample,2> samples{{
        {"formatting","cpp_fundamentals__archivos_y_programa_completo__estado_del_formato_y_flujos_independientes",
         "block_0002_0002",2,3,true},
        {"group","cpp_fundamentals__archivos_y_programa_completo__leer_un_grupo_con_cierre_propio",
         "block_0001_0002",1,2,false}
    }};
    for(const auto& sample:samples) {
        const auto prefix="u12-"+std::string(sample.tag);
        const auto before_file=fixtures/(prefix+".before.json");
        const auto after_file=fixtures/(prefix+".after.json");
        const auto before=windows::load_lesson(before_file,"es");
        const auto after=windows::load_lesson(after_file,"es");
        check(before.id==sample.lesson&&before.id==after.id&&
              before.state_layout.signatures==after.state_layout.signatures&&
              before.state_layout.lineage==after.state_layout.lineage&&
              before.state_layout.legacy_lineage==after.state_layout.legacy_lineage,
              "U12 editorial authoring changed the historical family, origin or response signature");
        const auto& old=before.pages.at(sample.p).blocks.at(sample.b);
        const auto& now=after.pages.at(sample.p).blocks.at(sample.b);
        check(old.stable_id==sample.id&&old.stable_id==now.stable_id&&
              old.address.page==now.address.page&&old.address.block==now.address.block&&
              old.type==now.type&&old.question!=now.question&&
              old.options==now.options&&old.correct_index==now.correct_index&&
              old.answers==now.answers&&old.code==now.code&&
              old.review_feedback==now.review_feedback&&
              old.review_feedback_options==now.review_feedback_options&&
              curriculum::exercise_key(before,old)==curriculum::exercise_key(after,now)&&
              curriculum::draft_key("programming",before,old)==
                  curriculum::draft_key("programming",after,now),
              "A U12 editorial correction changed the saved answer, source or historical owner");
        const auto q=now.text_semantics.find("/question");
        check(q!=now.text_semantics.end()&&q->second.source==now.question,
              "The edited U12 question lost its exact authored reading descriptor");
        if(sample.choice) {
            check(old.type==curriculum::BlockType::multiple_choice&&
                  now.correct_index&&*now.correct_index==2&&
                  old.explanation!=now.explanation&&q->second.spans.size()==2&&
                  now.text_semantics.contains("/explanation")&&
                  now.text_semantics.at("/explanation").spans.size()==4,
                  "U12L6 lost question/guide semantic spans or the original solution");
            for(const auto& span:q->second.spans)
                check(span.role==curriculum::SemanticRole::cpp&&
                      span.reading==curriculum::SemanticReading::technical&&
                      (span.text=="texto << std::hex << 31"||
                       span.text=="texto << std::dec"),
                      "U12L6 introduced inferred or incorrect technical narration");
            const auto& old_returns=before.state_layout.editorial_returns;
            const auto& current_returns=after.state_layout.editorial_returns;
            check(current_returns.size()==old_returns.size()+1,
                  "U12L6 must explicitly return to its earlier full editorial payload");
            // This owner already has an earlier explicit return. Inspect
            // the most recent one, not the first historical predecessor.
            const auto entry=std::find_if(current_returns.rbegin(),current_returns.rend(),
                [&](const auto& value){return value.id==sample.id;});
            check(entry!=current_returns.rend()&&
                  entry->previous_raw_json.find("texto << hex << 31")!=std::string::npos,
                  "The native loader lost the original abbreviated U12L6 reading");
        }else {
            check(old.type==curriculum::BlockType::output_prediction&&
                  old.answers.size()==1&&old.answers.front()=="1:}:fin"&&
                  old.explanation==now.explanation&&q->second.spans.empty()&&
                  before.state_layout.editorial_returns.size()==
                      after.state_layout.editorial_returns.size(),
                  "U12L22 gained a new result or silently changed its older guidance");
        }
        state::StateData saved;state::prepare_lesson_state(saved,before);
        state::Response response;response.correct=true;
        if(sample.choice) {
            response.selected=static_cast<std::int64_t>(*old.correct_index);
            response.answer=old.options.at(*old.correct_index);
        }else response.answer=old.answers.front();
        state::put_response(saved,before,old,response);
        state::save_current_page(saved,before,sample.p);
        const auto archived=state::decode(state::encode(saved));
        check(archived==saved,"A historic U12 response changed during serialization");
        state::validate_lesson_state(archived,after);
        const auto restored=state::get_response(archived,after,now);
        check(restored&&restored->correct&&restored->answer==response.answer&&
              restored->selected==response.selected&&
              state::current_page(archived,after)==sample.p,
              "A saved original U12 answer disappeared when reopening the revised lesson");
        const auto catalog=windows::load_lesson_progress_metadata(after_file,"es");
        check(catalog.state_layout.signatures==after.state_layout.signatures&&
              catalog.state_layout.lineage==after.state_layout.lineage&&
              catalog.pages.at(sample.p).blocks.at(sample.b).stable_id==sample.id,
              "The progress-only native reader lost the exact U12 historic identity");
        for(const auto variant:{"changed-answer","stale-semantic","forged-proof"})
            must_reject(fixtures/(prefix+"."+variant+".json"));
    }
}


std::string format_retirement_text(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path);
    return {bytes.begin(),bytes.end()};
}

std::filesystem::path format_retirement_fixture(const std::filesystem::path& root,
        const winrt::hstring& filename) {
    const std::filesystem::path relative(winrt::to_string(filename));
    check(!relative.empty()&&!relative.is_absolute()&&relative.filename()==relative,
        "A source-format retirement fixture must be a local JSON filename");
    const auto file=root/relative;
    check(std::filesystem::is_regular_file(file),
        "The source-format retirement fixture was not generated");
    return file;
}

bool same_format_address(const curriculum::StateAddress& a,
        const curriculum::StateAddress& b) {
    return a.page==b.page&&a.block==b.block&&
        a.addition_namespace==b.addition_namespace&&a.progress_page==b.progress_page;
}

void compare_format_history_block(const curriculum::Lesson& source,
        const curriculum::Block& original,const curriculum::Lesson& current,
        const curriculum::Block& archived,bool public_text=true) {
    using namespace winrt::Windows::Data::Json;
    // Compare the complete public payload, then the byte-exact decoded source.
    // JSON presentation whitespace is not a change to a C++ source string.
    check(JsonObject::Parse(winrt::to_hstring(original.raw_json)).Stringify()==
              JsonObject::Parse(winrt::to_hstring(archived.raw_json)).Stringify()&&
        original.type==archived.type&&original.stable_id==archived.stable_id&&
        same_format_address(original.address,archived.address)&&
        original.code==archived.code&&original.initial_code==archived.initial_code&&
        original.code_template==archived.code_template&&
        original.fragments==archived.fragments&&
        original.project_files==archived.project_files&&
        original.source_files==archived.source_files&&
        original.answers==archived.answers&&original.correct_order==archived.correct_order&&
        original.correct_index==archived.correct_index&&
        curriculum::exercise_key(source,original)==
            curriculum::exercise_key(current,archived)&&
        curriculum::draft_key("programming",source,original)==
            curriculum::draft_key("programming",current,archived),
        "A retirement reading changed its published payload, source bytes or historical address");
    if(public_text)
        check(original.text==archived.text&&original.question==archived.question&&
              original.explanation==archived.explanation&&
              original.review_feedback==archived.review_feedback&&
              original.prompt==archived.prompt&&original.options==archived.options,
            "Historical reading text was replaced by another editorial presentation");
    if(original.type==curriculum::BlockType::order_code) {
        check(curriculum::order_representation_frame(original)==
                  curriculum::order_representation_frame(archived)&&
            original.source_format_original_fragments.value_or(original.fragments)==
                archived.source_format_original_fragments.value_or(archived.fragments),
            "A historical formatted order puzzle lost its canonical source or answer representation");
        check(curriculum::evaluate_order(original,original.correct_order).correct&&
              curriculum::evaluate_order(archived,original.correct_order).correct,
            "The same original ordering answer stopped being correct in its reading archive");
    }
    if(curriculum::has_compiler_objective(original)) {
        check(curriculum::has_compiler_objective(archived)&&
            curriculum::reference_objective_frame(source,original)==
                curriculum::reference_objective_frame(current,archived)&&
            original.source_format_original_initial_code.value_or(original.initial_code)==
                archived.source_format_original_initial_code.value_or(archived.initial_code)&&
            original.source_format_original_project_files.value_or(original.project_files)==
                archived.source_format_original_project_files.value_or(archived.project_files),
            "A historical formatted compiler lost its source archive or original objective");
    }
}

void check_format_retirement_ownership(const curriculum::Lesson& before,
        const curriculum::Lesson& after) {
    using namespace winrt::Windows::Data::Json;
    check(before.id==after.id&&before.locale==after.locale&&
        before.state_layout.version==2&&after.state_layout.version==2&&
        before.state_layout.signatures==after.state_layout.signatures&&
        before.state_layout.lineage==after.state_layout.lineage&&
        before.state_layout.legacy_lineage==after.state_layout.legacy_lineage,
        "Formatting and retirement changed immutable ownership");
    const auto old_meta=JsonObject::Parse(winrt::to_hstring(before.state_layout.raw_json));
    const auto new_meta=JsonObject::Parse(winrt::to_hstring(after.state_layout.raw_json));
    for(const auto field:{L"origin",L"family",L"additions"})
        check(old_meta.GetNamedValue(field).Stringify()==new_meta.GetNamedValue(field).Stringify(),
            "Formatting and retirement rewrote an origin, family or addition");
    for(const auto& page:before.pages)for(const auto& block:page.blocks) {
        const auto position=curriculum::stable_block_position(after,block.stable_id);
        if(!position) {
            const auto retired=std::find_if(after.retired_blocks.begin(),after.retired_blocks.end(),
                [&](const auto& item){return item.stable_id==block.stable_id;});
            check(retired!=after.retired_blocks.end()&&
                  same_format_address(block.address,retired->address),
                "An original block disappeared instead of retaining its retired identity");
            continue;
        }
        const auto& now=after.pages.at(position->page).blocks.at(position->block);
        check(block.type==now.type&&same_format_address(block.address,now.address)&&
              curriculum::exercise_key(before,block)==curriculum::exercise_key(after,now)&&
              curriculum::draft_key("programming",before,block)==
                  curriculum::draft_key("programming",after,now),
            "An unrelated retirement moved a response or compiler draft to a visible slot");
        if(block.type==curriculum::BlockType::order_code)
            check(curriculum::order_representation_frame(block)==
                      curriculum::order_representation_frame(now),
                "Formatting with retirement changed the active ordering representation");
        if(curriculum::has_compiler_objective(block))
            check(curriculum::has_compiler_objective(now)&&
                  curriculum::reference_objective_frame(before,block)==
                      curriculum::reference_objective_frame(after,now),
                "Formatting with retirement changed an existing compiler objective");
    }
}

std::string format_retirement_xml(std::string_view source) {
    std::string result;
    for(const char c:source) {
        if(c=='\r')result+="&#13;";
        else if(c=='\n')result+="&#10;";
        else if(c=='\t')result+="&#9;";
        else if(c=='&')result+="&amp;";
        else if(c=='<')result+="&lt;";
        else if(c=='>')result+="&gt;";
        else if(c=='"')result+="&quot;";
        else result+=c;
    }
    return result;
}

void export_format_retirement_preferences(const std::filesystem::path& root,
        const state::StateData& data) {
    // Real Android XML transport using synthetic QA preferences only.
    // Character references preserve CR/LF/tab bytes through XML parsing.
    std::filesystem::create_directories(state::native_io_path(root/"shared_prefs"));
    for(const auto& [domain,values]:data.preferences) {
        std::string xml="<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for(const auto& [key,value]:values)std::visit([&](const auto& item) {
            using T=std::decay_t<decltype(item)>;
            const auto name=" name=\""+format_retirement_xml(key)+"\"";
            if constexpr(std::is_same_v<T,std::string>)
                xml+="<string"+name+">"+format_retirement_xml(item)+"</string>";
            else if constexpr(std::is_same_v<T,bool>)
                xml+="<boolean"+name+" value=\""+(item?"true":"false")+"\"/>";
            else if constexpr(std::is_same_v<T,std::int64_t>)
                xml+="<long"+name+" value=\""+std::to_string(item)+"\"/>";
            else if constexpr(std::is_same_v<T,std::vector<std::string>>) {
                xml+="<set"+name+">";
                for(const auto& entry:item)xml+="<string>"+format_retirement_xml(entry)+"</string>";
                xml+="</set>";
            }else throw std::runtime_error("Unsupported source-format QA preference type");
        },value);
        xml+="</map>";
        std::ofstream stream(state::native_io_path(root/"shared_prefs"/(domain+".xml")),
                             std::ios::binary|std::ios::trunc);
        stream.write(xml.data(),static_cast<std::streamsize>(xml.size()));
        check(stream.good(),"Could not write the isolated Android source-format export");
    }
}

void compare_format_retirement_adoption(const state::StateData& before,
        const state::StateData& after,const curriculum::Lesson& lesson,bool allow_choice_revisions=false) {
    using namespace winrt::Windows::Data::Json;
    const auto prefix="lesson_"+lesson.id;
    const auto choice_key=prefix+"_state_choice_revisions_v1_"+lesson.locale;
    if(allow_choice_revisions) {
        JsonArray expected;
        for(const auto& revision:lesson.state_layout.choice_revisions) {
            JsonArray row;
            row.Append(JsonValue::CreateStringValue(winrt::to_hstring(revision.id)));
            row.Append(JsonValue::CreateStringValue(winrt::to_hstring(revision.sha256)));
            row.Append(JsonValue::CreateStringValue(winrt::to_hstring(revision.representation_signature)));
            expected.Append(row);
        }
        const auto* stored=state::find(after,"learning_progress",choice_key);
        check((!stored&&expected.Size()==0)||(stored&&std::holds_alternative<std::string>(*stored)&&
            JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*stored))).Stringify()==expected.Stringify()),
            "Semantic retirement changed the exact authenticated choice revision provenance");
    }
    const std::array<std::string,2> allowed{{
        prefix+"_state_retired_v2_"+lesson.locale,
        prefix+"_state_retirement_history_v2_"+lesson.locale}};
    const auto is_provenance=[&](std::string_view domain,const std::string& key){
        return domain=="learning_progress"&&
            (std::find(allowed.begin(),allowed.end(),key)!=allowed.end()||
             (allow_choice_revisions&&key==choice_key));
    };
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values) {
        const auto* current=state::find(after,domain,key);
        check(current!=nullptr,"Retirement adoption erased an existing preference");
        if(is_provenance(domain,key)) {
            const auto old_values=JsonArray::Parse(winrt::to_hstring(std::get<std::string>(value)));
            const auto new_values=JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*current)));
            check(new_values.Size()>=old_values.Size(),"Retirement adoption truncated prior provenance");
            for(unsigned i=0;i<old_values.Size();++i)
                check(old_values.GetAt(i).Stringify()==new_values.GetAt(i).Stringify(),
                    "Retirement adoption rewrote a previous snapshot or retired owner");
        }else check(*current==value,
            "Retirement adoption rewrote an answer, draft, pause, coverage or preference");
    }
    for(const auto& [domain,values]:after.preferences)for(const auto& [key,value]:values)
        if(!state::find(before,domain,key))
            check(is_provenance(domain,key),
                "Retirement adoption created unrelated state or a new execution receipt");
}

std::pair<std::size_t,std::size_t> verify_format_retirement_state(const std::filesystem::path& qa,
        std::string_view name,const curriculum::Lesson& before,
        const curriculum::Lesson& after,bool include_retired=false) {
    state::StateData original;
    state::prepare_lesson_state(original,before);
    std::size_t answer_count=0,draft_count=0,prediction_count=0;
    std::size_t correct_count=0,reviewed_error_count=0;
    for(const auto& page:before.pages)for(const auto& block:page.blocks) {
        if(!include_retired&&!curriculum::stable_block_position(after,block.stable_id))continue;
        if(block.type==curriculum::BlockType::cpp_compiler) {
            state::Draft draft;
            draft.source="// borrador QA conservado\r\n"+block.initial_code+"\n";
            draft.input=" \t7\r\n";draft.selection_start=4;draft.selection_end=11;
            draft.indentation_json=state::make_indentation_metadata(draft.source,{});
            draft.has_saved=true;
            state::put_draft(original,curriculum::draft_key("programming",before,block),draft);
            ++draft_count;
        }else if(block.type==curriculum::BlockType::order_code||
                 block.type==curriculum::BlockType::output_prediction) {
            state::Response response;
            response.raw_json=R"({"incorrectAttempts":3,"opaque":{"qa":"format-retirement","cursor":17}})";
            if(block.type==curriculum::BlockType::order_code) {
                for(const auto index:block.correct_order)
                    response.order.push_back(static_cast<std::int64_t>(index));
                response.correct=true;
                check(curriculum::evaluate_order(block,block.correct_order).correct,
                    "The source-format QA original order is not correct");
            }else {
                // The first prediction is a reviewed error; subsequent ones
                // retain valid successes. Neither is regraded on adoption.
                check(!block.answers.empty(),"A source-format QA prediction has no authored answer");
                response.correct=prediction_count++!=0;
                response.answer=response.correct?block.answers.front():"__qa_incorrect_prediction__";
                check(curriculum::evaluate_text_answer(block,response.answer).correct==response.correct,
                    "The source-format QA prediction has the wrong original verdict");
                if(!response.correct)state::mark_response_reviewed(response,block);
            }
            state::put_response(original,before,block,response);
            ++answer_count;
            if(response.correct)++correct_count;
            else if(state::response_reviewed(response,block))++reviewed_error_count;
        }
    }
    check(answer_count>0&&draft_count>0,
        "Source-format retirement QA must exercise original answers and compiler drafts");
    // Visits build genuine reach metadata in order; no partial replacement of
    // read_blocks may silently move a saved bookmark backwards on reopening.
    for(std::size_t p=0;p<before.pages.size();++p)
        state::save_current_page(original,before,p);
    const auto saved_page=state::current_page(original,before);
    original.preferences["format_retirement_qa"]["opaque"]=std::string(" \t untouched\r\n");
    original.preferences["format_retirement_qa"]["enabled"]=true;
    original.preferences["format_retirement_qa"]["counter"]=std::int64_t{37};
    auto pause=narration::begin({},"format-retirement-first","qa-first",
        {0,173,0,false},before.id+"/source-first",before.pages.front().stable_id);
    pause=narration::begin(std::move(pause),"format-retirement-second","qa-second",
        {1,239,0,false},before.id+"/source-second",before.pages.back().stable_id);
    narration::write_history(original,pause);
    // These stored pause positions are synthetic metadata. No narration is
    // played, generated or credited by this parser/state conservation test.
    state::validate_lesson_state(original,before);
    const auto verify=[&](const state::StateData& data,const state::StateData& prior) {
        state::validate_lesson_state(data,after);
        check(narration::read_history(data)==pause&&
              state::current_page(data,after)==saved_page,
            "Source-format retirement lost a saved pause or historical page");
        for(const auto& page:before.pages)for(const auto& block:page.blocks) {
            const auto position=curriculum::stable_block_position(after,block.stable_id);
            if(!position&&!include_retired)continue;
            const auto retired=std::ranges::find(after.retired_blocks,block.stable_id,
                &curriculum::Block::stable_id);
            const auto* current=position?&after.pages.at(position->page).blocks.at(position->block)
                :retired!=after.retired_blocks.end()?&*retired:nullptr;
            check(current!=nullptr,"Retirement lost the exact owner of a stored answer or draft");
            const auto& now=*current;
            if(block.type==curriculum::BlockType::cpp_compiler) {
                const auto key=curriculum::draft_key("programming",before,block);
                const auto a=state::get_draft(prior,key,block.initial_code,block.standard_input.value_or(""));
                const auto b=state::get_draft(data,key,now.initial_code,now.standard_input.value_or(""));
                check(a.source==b.source&&a.input==b.input&&
                    a.indentation_json==b.indentation_json&&a.has_saved==b.has_saved&&
                    a.selection_start==b.selection_start&&a.selection_end==b.selection_end,
                    "Source-format retirement changed a draft, input, indentation or cursor");
            }else if(block.type==curriculum::BlockType::order_code||
                     block.type==curriculum::BlockType::output_prediction) {
                const auto a=state::get_response(prior,before,block);
                const auto b=state::get_response(data,after,now);
                check(a&&b&&a->raw_json==b->raw_json&&a->correct==b->correct&&
                    a->answer==b->answer&&a->selected==b->selected&&a->order==b->order&&
                    a->reviewed_answer_identity==b->reviewed_answer_identity&&
                    state::response_reviewed(*a,block)==state::response_reviewed(*b,now),
                    "Source-format retirement changed a correct/incorrect answer, attempts or review");
            }
        }
    };

    const auto native_root=qa/(std::string(name)+"-native");
    {
        state::StateStore store(native_root);
        check(store.load().preferences.empty(),"Source-format native QA profile is not new");
        store.save(original);
    }
    state::StateData adopted;
    {
        state::StateStore store(native_root);
        adopted=store.load();
        check(adopted==original,"Native source-format state changed during physical reopening");
        state::prepare_lesson_state(adopted,after);
        compare_format_retirement_adoption(original,adopted,after,include_retired);
        verify(adopted,original);
        store.save(adopted);
    }
    {
        state::StateStore store(native_root);
        const auto reopened=store.load();
        check(reopened==adopted,"Adopted source-format state changed during physical reopening");
        verify(reopened,original);
    }

    auto android=original;
    // Windows-only adoption/compilation metadata must never masquerade as an
    // Android export. There are no manufactured execution receipts here.
    android.preferences.erase("native_windows");
    const auto exported=qa/(std::string(name)+"-android-export");
    export_format_retirement_preferences(exported,android);
    const auto preview=state::inspect_android_export(exported);
    check(preview.data==android,"Android XML import changed original typed QA preferences");
    const auto imported_root=qa/(std::string(name)+"-imported");
    state::commit_android_import(preview,imported_root);
    state::StateData imported;
    {
        state::StateStore store(imported_root);
        imported=store.load();
        check(imported==android,"Committed Android import differs from the inspected preferences");
        state::prepare_lesson_state(imported,before);
        // Reintroduce only the original native provenance before measuring
        // the format/retirement transition; no completed compiler is invented.
        const auto before_adoption=imported;
        state::prepare_lesson_state(imported,after);
        compare_format_retirement_adoption(before_adoption,imported,after,include_retired);
        verify(imported,android);
        store.save(imported);
    }
    {
        state::StateStore store(imported_root);
        const auto reopened=store.load();
        check(reopened==imported,"Imported format/retirement state changed on reopening");
        verify(reopened,android);
    }
    for(const auto& file:preview.files)
        check(state::sha256(state::read_bytes(exported/file.relative_path))==file.sha256,
            "Native import changed an original Android export file");
    std::cout<<"PASS source-format retirement state "<<name
        <<": answers="<<answer_count<<", correct="<<correct_count
        <<", reviewed_errors="<<reviewed_error_count<<", drafts="<<draft_count
        <<", native/imported physical reopen, exact pauses and source export\n";
    return {correct_count,reviewed_error_count};
}

void verify_source_format_retirement(const std::filesystem::path& fixtures,
        const std::filesystem::path& qa_root) {
    using namespace winrt::Windows::Data::Json;
    const auto manifest=JsonObject::Parse(winrt::to_hstring(
        format_retirement_text(fixtures/"manifest.json")));
    const auto cases=manifest.GetNamedArray(L"cases");
    check(cases.Size()>=3,"Source-format retirement QA is missing its independent ordering cases");
    std::filesystem::create_directories(state::native_io_path(qa_root));
    const auto qa=qa_root/("source-format-retirement-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(state::native_io_path(qa)),
        "Source-format retirement QA must use a new isolated directory");
    std::size_t history_count=0,order_count=0,compiler_count=0,diagnostic_count=0;
    std::size_t correct_count=0,reviewed_error_count=0;
    for(const auto& entry:cases) {
        const auto sample=entry.GetObject();
        const auto name=winrt::to_string(sample.GetNamedString(L"name"));
        check(!name.empty()&&name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,
            "Invalid source-format retirement QA scenario name");
        const auto before_file=format_retirement_fixture(fixtures,sample.GetNamedString(L"before"));
        const auto after_file=format_retirement_fixture(fixtures,sample.GetNamedString(L"after"));
        const auto before_bytes=state::read_bytes(before_file);
        const auto after_bytes=state::read_bytes(after_file);
        const auto before=windows::load_lesson(before_file,"es");
        const auto after=windows::load_lesson(after_file,"es");
        const auto catalog=windows::load_lesson_progress_metadata(after_file,"es");
        check_format_retirement_ownership(before,after);
        check(catalog.state_layout.signatures==after.state_layout.signatures&&
            catalog.state_layout.lineage==after.state_layout.lineage&&
            catalog.state_layout.retirement_history.size()==after.state_layout.retirement_history.size(),
            "Full and progress loaders disagree about source-format retirement provenance");
        for(const auto& page:after.pages)for(const auto& block:page.blocks)
            if(block.type==curriculum::BlockType::order_code||
               curriculum::has_compiler_objective(block)) {
                const auto position=curriculum::stable_block_position(catalog,block.stable_id);
                check(position.has_value(),"The progress loader lost an active formatted objective");
                compare_format_history_block(after,block,catalog,
                    catalog.pages.at(position->page).blocks.at(position->block),false);
            }
        for(const auto& identity:sample.GetNamedArray(L"retired")) {
            const auto owner=winrt::to_string(identity.GetString());
            check(curriculum::retired_block(after,owner)&&
                  curriculum::retired_block(catalog,owner)&&
                  !curriculum::stable_block_position(after,owner),
                "An explicitly retired note remained active or lost its archived identity");
        }
        const auto histories=sample.GetNamedArray(L"history");
        check(histories.Size()>0,"A format/retirement case has no exact historical source comparison");
        for(const auto& value:histories) {
            const auto expected=value.GetObject();
            const auto index=static_cast<std::size_t>(expected.GetNamedNumber(L"index"));
            const auto source_file=format_retirement_fixture(fixtures,expected.GetNamedString(L"source"));
            const auto source_bytes=state::read_bytes(source_file);
            const auto source=windows::load_lesson(source_file,"es");
            for(const auto* loaded:{&after,&catalog}) {
                const auto& history=loaded->state_layout.retirement_history.at(index);
                check(history.pages.size()==source.pages.size()&&
                      history.compiler_puzzles_required==source.compiler_puzzles_required&&
                      history.reference_after.size()==source.reference_after.size()&&
                      history.activity_after.size()==source.activity_after.size(),
                    "A formatted retirement snapshot lost its original pages or visibility rules");
                for(std::size_t r=0;r<source.reference_after.size();++r)
                    check(history.reference_after[r].activity==source.reference_after[r].activity&&
                          history.reference_after[r].blocks==source.reference_after[r].blocks,
                        "A source-format retirement changed historical reference ownership");
                for(std::size_t r=0;r<source.activity_after.size();++r)
                    check(history.activity_after[r].activity==source.activity_after[r].activity&&
                          history.activity_after[r].prerequisites==source.activity_after[r].prerequisites,
                        "A source-format retirement changed historical activity prerequisites");
                for(std::size_t p=0;p<source.pages.size();++p) {
                    const auto& previous=source.pages[p];
                    const auto& archived=history.pages[p];
                    check(previous.stable_id==archived.stable_id&&
                          previous.narration_language==archived.narration_language&&
                          previous.blocks.size()==archived.blocks.size(),
                        "Historical formatted pages lost block order or stable page ownership");
                    for(std::size_t b=0;b<previous.blocks.size();++b) {
                        compare_format_history_block(source,previous.blocks[b],*loaded,archived.blocks[b]);
                        if(loaded==&after) {
                            const auto& block=archived.blocks[b];
                            order_count+=block.type==curriculum::BlockType::order_code&&
                                block.source_format_original_fragments.has_value();
                            compiler_count+=block.type==curriculum::BlockType::cpp_compiler&&
                                block.source_format_original_initial_code.has_value();
                            diagnostic_count+=block.compile_check.has_value()&&
                                block.source_format_original_initial_code.has_value();
                        }
                    }
                }
            }
            check(state::read_bytes(source_file)==source_bytes,
                "Inspecting retirement history changed its published source fixture");
            ++history_count;
        }
        const auto [correct,reviewed_errors]=verify_format_retirement_state(qa,name,before,after);
        correct_count+=correct;reviewed_error_count+=reviewed_errors;
        check(state::read_bytes(before_file)==before_bytes&&
              state::read_bytes(after_file)==after_bytes,
            "Source-format retirement QA overwrote a source fixture");
    }
    check(correct_count>0&&reviewed_error_count>0,
        "Retirement QA did not preserve both successes and reviewed incorrect answers");
    check(order_count>0&&compiler_count>0&&diagnostic_count>0,
        "Retirement QA did not exercise formatted order, compiler and compileCheck history");
    const auto invalid=manifest.GetNamedArray(L"invalid");
    check(invalid.Size()>=5,"Source-format retirement QA lacks adversarial source/history coverage");
    for(const auto& value:invalid) {
        const auto sample=value.GetObject();
        const auto file=format_retirement_fixture(fixtures,sample.GetNamedString(L"file"));
        const auto raw=format_retirement_text(file);
        const auto legacy=JsonObject::Parse(winrt::to_hstring(raw));
        const auto snapshots=legacy.GetNamedObject(L"stateLayout").GetNamedArray(L"retirementHistory");
        check(snapshots.Size()>0,"A retirement rejection fixture lacks a historical snapshot");
        for(const auto& snapshot:snapshots) {
            const auto object=snapshot.GetObject();
            check(object.HasKey(L"editorialPrefix"),
                "A retirement rejection fixture lacks exact editorial provenance");
            object.Remove(L"editorialPrefix");
        }
        // Strip provenance only in a separate legacy document. This is not an
        // accepted publication transition and must still reach the old guard.
        const auto legacy_raw=winrt::to_string(legacy.Stringify());
        const auto name=winrt::to_string(sample.GetNamedString(L"name"));
        const auto expected=sample.HasKey(L"message")
            ?winrt::to_string(sample.GetNamedString(L"message")):std::string{};
        const auto current_expected=
            name=="unpublished-whitespace"||name=="unpublished-guidance"||name=="borrowed-owner"
                ?std::string{"Historical reading source differs from its exact editorial prefix"}
            :name=="changed-token"||name=="changed-answer"||name=="changed-oracle"||
             name=="changed-order"||name=="changed-project-path"
                ?std::string{"Retirement editorial prefix differs from its complete published history"}
                :expected;
        for(const bool catalog:{false,true})for(const bool legacy_copy:{false,true}) {
            const auto& payload=legacy_copy?legacy_raw:raw;
            const auto& intended=legacy_copy?expected:current_expected;
            std::string error;
            try {
                if(catalog)(void)windows::parse_lesson_progress_metadata(payload,"es");
                else(void)windows::parse_lesson_json(payload,"es");
            }catch(const std::exception& failure){error=failure.what();}
            const auto context=name+(catalog?" progress":" full")+
                (legacy_copy?" legacy":" authenticated");
            check(!error.empty(),"Windows admitted forged retirement history: "+context);
            check(intended.empty()||error.find(intended)!=std::string::npos,
                "Forged retirement history missed its intended guard: "+context+
                "; expected: "+intended+"; received: "+error);
            std::cout<<"PASS source-format retirement rejection "<<context<<": "<<error<<'\n';
        }
    }
    std::cout<<"PASS source-format retirement: "<<cases.Size()<<" cases, "
        <<history_count<<" exact historical presentations, "<<invalid.Size()
        <<" negative fixtures checked by full/progress loaders\n";
}


void verify_semantic_retirement(const std::filesystem::path& fixtures,
        const std::filesystem::path& qa_root) {
    using namespace winrt::Windows::Data::Json;
    const auto manifest=JsonObject::Parse(winrt::to_hstring(
        format_retirement_text(fixtures/"manifest.json")));
    const auto cases=manifest.GetNamedArray(L"cases");
    check(cases.Size()>=3,"Semantic retirement QA lacks independent version histories");
    std::filesystem::create_directories(state::native_io_path(qa_root));
    const auto qa=qa_root/("semantic-retirement-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(state::native_io_path(qa)),
        "Semantic retirement QA must use a new isolated directory");
    std::size_t authored=0,unmarked=0,legacy=0,marked_blocks=0,root_fields=0;
    std::size_t correct_count=0,reviewed_error_count=0;
    for(const auto& entry:cases) {
        const auto sample=entry.GetObject();
        const auto name=winrt::to_string(sample.GetNamedString(L"name"));
        check(!name.empty()&&name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,
            "Invalid semantic retirement QA scenario name");
        const auto before_file=format_retirement_fixture(fixtures,sample.GetNamedString(L"before"));
        const auto after_file=format_retirement_fixture(fixtures,sample.GetNamedString(L"after"));
        const auto before_bytes=state::read_bytes(before_file),after_bytes=state::read_bytes(after_file);
        const auto before=windows::load_lesson(before_file,"es");
        const auto after=windows::load_lesson(after_file,"es");
        const auto catalog=windows::load_lesson_progress_metadata(after_file,"es");
        check_format_retirement_ownership(before,after);
        check(after.text_semantics==catalog.text_semantics&&
            after.state_layout.raw_json==catalog.state_layout.raw_json&&
            after.retired_blocks.size()==catalog.retired_blocks.size(),
            "Full/progress loaders disagree about root meanings or retirement provenance");
        for(const auto& page:after.pages)for(const auto& block:page.blocks) {
            const auto at=curriculum::stable_block_position(catalog,block.stable_id);
            check(at.has_value(),"The progress loader lost an active semantic sibling");
            const auto& compact=catalog.pages.at(at->page).blocks.at(at->block);
            compare_format_history_block(after,block,catalog,compact,false);
            check(block.text_semantics==compact.text_semantics,
                "The progress loader changed an active owner's exact meaning or reading intent");
        }
        const auto histories=sample.GetNamedArray(L"history");
        check(histories.Size()==after.state_layout.retirement_history.size()&&
            histories.Size()==catalog.state_layout.retirement_history.size(),
            "Every semantic retirement snapshot needs its exact predecessor document");
        for(const auto& value:histories) {
            const auto expected=value.GetObject();
            const auto index=static_cast<std::size_t>(expected.GetNamedNumber(L"index"));
            const auto source_file=format_retirement_fixture(fixtures,expected.GetNamedString(L"source"));
            const auto source_bytes=state::read_bytes(source_file);
            const auto original=windows::load_lesson(source_file,"es");
            const auto original_json=JsonObject::Parse(winrt::to_hstring(original.raw_json));
            for(const auto* loaded:{&after,&catalog}) {
                const auto& snapshot=loaded->state_layout.retirement_history.at(index);
                const auto raw=JsonObject::Parse(winrt::to_hstring(snapshot.raw_json));
                check(snapshot.has_text_semantics==raw.HasKey(L"textSemantics"),
                    "The loader confused legacy absence with an explicit semantic snapshot");
                if(snapshot.has_text_semantics) {
                    const auto prior=original_json.HasKey(L"textSemantics")
                        ?original_json.GetNamedValue(L"textSemantics"):JsonValue::CreateNullValue();
                    check(raw.GetNamedValue(L"textSemantics").Stringify()==prior.Stringify(),
                        "Retirement changed the complete authored overlay of its predecessor");
                }
                check(snapshot.text_semantics==original.text_semantics&&
                    snapshot.pages.size()==original.pages.size()&&
                    snapshot.compiler_puzzles_required==original.compiler_puzzles_required&&
                    snapshot.reference_after.size()==original.reference_after.size()&&
                    snapshot.activity_after.size()==original.activity_after.size(),
                    "A semantic snapshot borrowed current root meanings or lost prior layout");
                for(std::size_t p=0;p<original.pages.size();++p) {
                    const auto& prior=original.pages[p];const auto& archived=snapshot.pages[p];
                    check(prior.stable_id==archived.stable_id&&
                        prior.narration_language==archived.narration_language&&
                        prior.blocks.size()==archived.blocks.size(),
                        "Semantic retirement changed historical page ownership or block order");
                    for(std::size_t b=0;b<prior.blocks.size();++b) {
                        compare_format_history_block(original,prior.blocks[b],*loaded,archived.blocks[b]);
                        check(prior.blocks[b].text_semantics==archived.blocks[b].text_semantics,
                            "A historical owner inherited a different semantic descriptor version");
                        if(loaded==&after&&!archived.blocks[b].text_semantics.empty())++marked_blocks;
                        if(std::ranges::find(snapshot.retired,archived.blocks[b].stable_id)!=snapshot.retired.end()) {
                            const auto retired=std::ranges::find(loaded->retired_blocks,
                                archived.blocks[b].stable_id,&curriculum::Block::stable_id);
                            check(retired!=loaded->retired_blocks.end(),"A semantic batch lost its own retired block");
                            compare_format_history_block(original,prior.blocks[b],*loaded,*retired);
                            check(retired->text_semantics==archived.blocks[b].text_semantics,
                                "A retired owner borrowed meanings from another retirement batch");
                        }
                    }
                }
                if(loaded==&after) {
                    if(!snapshot.has_text_semantics)++legacy;
                    else if(raw.GetNamedValue(L"textSemantics").ValueType()==JsonValueType::Null)++unmarked;
                    else ++authored;
                    root_fields+=snapshot.text_semantics.size();
                }
                const auto& full=after.state_layout.retirement_history.at(index);
                check(snapshot.sha256==full.sha256&&snapshot.raw_json==full.raw_json,
                    "Progress loading rewrote the authenticated semantic snapshot or its pin");
            }
            check(state::read_bytes(source_file)==source_bytes,
                "Semantic retirement verification changed a predecessor fixture");
        }
        for(const auto& identity:sample.GetNamedArray(L"retired")) {
            const auto owner=winrt::to_string(identity.GetString());
            check(curriculum::retired_block(after,owner)&&curriculum::retired_block(catalog,owner)&&
                !curriculum::stable_block_position(after,owner),
                "A semantically marked retired owner remained active or disappeared");
        }
        const auto state_file=sample.HasKey(L"stateSource")
            ?format_retirement_fixture(fixtures,sample.GetNamedString(L"stateSource")):before_file;
        const auto state_bytes=state::read_bytes(state_file);
        const auto state_source=windows::load_lesson(state_file,"es");
        const auto [correct,reviewed]=verify_format_retirement_state(qa,name,state_source,after,true);
        correct_count+=correct;reviewed_error_count+=reviewed;
        check(state::read_bytes(before_file)==before_bytes&&state::read_bytes(after_file)==after_bytes,
            "Semantic retirement state/import QA overwrote an authored fixture");
        check(state::read_bytes(state_file)==state_bytes,
            "Semantic retirement state/import QA changed its original answer source");
    }
    check(authored>0&&unmarked>0&&legacy>0&&marked_blocks>0&&root_fields>0,
        "Semantic retirement QA omitted authored, null, legacy or lesson-root history");
    check(correct_count>0&&reviewed_error_count>0,
        "Semantic retirement QA omitted saved successes or reviewed errors");
    const auto invalid=manifest.GetNamedArray(L"invalid");
    check(invalid.Size()>=5,"Semantic retirement QA lacks adversarial descriptor histories");
    for(const auto& value:invalid) {
        const auto sample=value.GetObject();
        const auto file=format_retirement_fixture(fixtures,sample.GetNamedString(L"file"));
        const auto raw=format_retirement_text(file);(void)JsonObject::Parse(winrt::to_hstring(raw));
        const auto name=winrt::to_string(sample.GetNamedString(L"name"));
        const auto expected=sample.HasKey(L"message")
            ?winrt::to_string(sample.GetNamedString(L"message")):std::string{};
        for(const bool catalog:{false,true}) {
            std::string error;
            try {
                if(catalog)(void)windows::parse_lesson_progress_metadata(raw,"es");
                else(void)windows::parse_lesson_json(raw,"es");
            }catch(const std::exception& failure){error=failure.what();}
            check(!error.empty(),"Windows admitted a forged semantic retirement history");
            check(expected.empty()||error.find(expected)!=std::string::npos,
                "A semantic forgery failed before reaching its intended guard");
            std::cout<<"PASS semantic retirement rejection "<<name
                <<(catalog?" progress: ":" full: ")<<error<<'\n';
        }
    }
    std::cout<<"PASS semantic retirement: "<<cases.Size()<<" cases, "<<authored
        <<" authored / "<<unmarked<<" unmarked / "<<legacy<<" legacy snapshots, "
        <<invalid.Size()<<" negative fixtures, exact full/progress meanings and native/imported state\n";
}

}

int main(int argc,char** argv) {
    try{
        if(argc!=7)throw std::invalid_argument("Expected source-format fixture directories and an isolated QA root");
        const std::filesystem::path dir(argv[1]);
        for(const auto locale:{"es","en","fr"})verify_locale(dir,locale);
        verify_source_format_retirement(std::filesystem::path(argv[4]),std::filesystem::path(argv[5]));
        verify_semantic_retirement(std::filesystem::path(argv[6]),std::filesystem::path(argv[5]));
        verify_visual_separators_keep_order_and_history(dir);
        verify_real_u7_question_and_spacing(dir);
        verify_real_u12_editorials(dir);
        verify_real_curricular_pilot(std::filesystem::path(argv[2]));
        verify_real_curricular_pilot(std::filesystem::path(argv[2])/"after-editorial");
        verify_real_u6_compiler_diagnostics(std::filesystem::path(argv[3]));
        verify_additional_real_compile_diagnostics(std::filesystem::path(argv[3]));
        for(const auto name:{"u6.forged-goal.json","u6.forged-archive.json",
                             "u6.forged-source.json"})
            must_reject(std::filesystem::path(argv[3])/name);
        must_reject(dir/"es.changed-answer.json");
        must_reject(dir/"es.forged-archive.json");
        std::cout<<"PASS native format loader: es/en/fr, real U4 and U7 questions, U6/U9 compileCheck, protected archives, drafts and forged-source rejection\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL native physical-format loader: "<<error.what()<<'\n';
        return 1;
    }
}

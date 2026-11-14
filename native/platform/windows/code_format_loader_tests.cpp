#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <stdexcept>
#include <string>
#include <string_view>

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

}

int main(int argc,char** argv) {
    try{
        if(argc!=4)throw std::invalid_argument("Expected synthetic, real decision and U6L2 fixture directories");
        const std::filesystem::path dir(argv[1]);
        for(const auto locale:{"es","en","fr"})verify_locale(dir,locale);
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

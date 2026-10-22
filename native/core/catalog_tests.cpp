#include "core/catalog.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "native_build_config.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void rejects(Action action,const char* message) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; } check(failed,message);
}
}
int main() {
    using namespace lgaed;
    using namespace curriculum;
    try {
        const auto root = std::filesystem::path(build::source_root) / "app/src/main/assets/content";
        CatalogLocation location;
        const auto routes = windows::load_catalog(root,location);
        check(routes.entries.size() == 2 && routes.entries[0].id == "programming" && routes.entries[1].id == "math_physics","Real route order changed");
        location = catalog_child(location,"programming"); const auto courses = windows::load_catalog(root,location);
        check(courses.entries.size() == 44 && courses.entries[0].id == "cpp_fundamentals" && courses.entries.back().number == 44,"Future indexed courses were hidden or reordered");
        check(!courses.entries.back().available,"A future course was invented as available");
        location = catalog_child(location,"cpp_fundamentals"); const auto units = windows::load_catalog(root,location);
        check(units.entries.size() == 12 && units.entries[0].id == "cpp_fundamentals__programas_y_ejecucion","Native course lost its indexed units");
        location = catalog_child(location,units.entries[0].id); const auto lessons = windows::load_catalog(root,location);
        check(lessons.entries.size() == 5 && lessons.entries[1].id == "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida","Lesson order differs from unit index");
        check(lessons.entries[1].available && lessons.entries[1].asset.filename() == "lesson_primer_programa_y_salida.json","An indexed lesson points to another source");
        auto first_lesson = location; first_lesson.lesson = lessons.entries.front().id;
        const auto same_unit = windows::next_catalog_lesson(root,first_lesson,false,false);
        check(same_unit && same_unit->lesson == lessons.entries[1].id && same_unit->unit == location.unit && same_unit->locale == location.locale,
            "Next lesson did not follow the actual indexed order and content language");
        auto unit_end = location; unit_end.lesson = lessons.entries.back().id;
        check(!windows::next_catalog_lesson(root,unit_end,false,false) && !windows::next_catalog_lesson(root,unit_end,false,true),
            "Automatic continuation crossed an unapproved unit or skipped it to another course");
        const auto next_unit = windows::next_catalog_lesson(root,unit_end,true,false);
        check(next_unit && next_unit->course == location.course && next_unit->unit == units.entries[1].id && next_unit->route == location.route,
            "Unit continuation skipped its indexed parent or changed routes");
        auto course_end = catalog_parent(location); course_end = catalog_child(course_end,units.entries.back().id);
        const auto final_lessons = windows::load_catalog(root,course_end); course_end.lesson = final_lessons.entries.back().id;
        check(!windows::next_catalog_lesson(root,course_end,true,false),"Course continuation ignored its disabled setting");
        const auto next_course = windows::next_catalog_lesson(root,course_end,true,true);
        check(next_course && next_course->route == location.route && next_course->course == courses.entries[1].id && next_course->locale == location.locale,
            "Course continuation did not preserve indexed route/course ownership");
        const auto location_before_restore = location;
        const auto read_index = [&] {
            std::ifstream source(root / location.locale / catalog_index_path(location),std::ios::binary);
            check(source.good(),"Cannot read the original indexed lesson source");
            return std::string(std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>());
        };
        const auto original_index = read_index();
        const auto active = windows::active_catalog_lesson(root,location,lessons.entries[1].id);
        check(active && active->id == lessons.entries[1].id && active->asset == lessons.entries[1].asset &&
            active->asset_locale == lessons.entries[1].asset_locale && active->available,
            "Restoring an active lesson changed its exact published identity/asset");
        check(!windows::active_catalog_lesson(root,location,next_unit->lesson) &&
            !windows::active_catalog_lesson(root,location,next_course->lesson),
            "Restoring navigation searched another unit or course for the saved lesson");
        for (const auto id : {"","missing","../lesson.json","C:\\other\\lesson.json","a/b","a\\b","wrong__unit__lesson"})
            check(!windows::active_catalog_lesson(root,location,id),"An absent or malformed active ID escaped the current index");
        check(!windows::active_catalog_lesson(root,CatalogLocation{},lessons.entries[1].id) &&
            !windows::active_catalog_lesson(root,catalog_parent(location),lessons.entries[1].id),
            "Restoring an active lesson invented missing current course/unit parents");
        check(location == location_before_restore && read_index() == original_index,
            "Reading the active lesson mutated current navigation or the curricular index");
        auto unpublished = first_lesson; unpublished.lesson = unpublished.unit + "__unindexed";
        rejects([&] { (void)windows::next_catalog_lesson(root,unpublished,true,true); },"Automatic continuation activated an unindexed lesson");
        check(catalog_parent(location).course == "cpp_fundamentals" && catalog_parent(catalog_parent(location)).route == "programming","Back navigation lost real parents");
        check(catalog_parent(catalog_parent(catalog_parent(location))) == CatalogLocation{},"Home does not preserve the route stack");
        check(catalog_lesson_path(location,lessons.entries[1].id).generic_string() == "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json","Historical lesson path was renamed");
        for (const auto locale : {"es","en","fr"}) {
            auto translated = location; translated.locale = locale;
            const auto page = windows::load_catalog(root,translated);
            check(!page.entries.empty() && page.entries[1].id == lessons.entries[1].id,"Translation changed lesson ownership");
            const auto restored = windows::active_catalog_lesson(root,translated,lessons.entries[1].id);
            check(restored && restored->asset == page.entries[1].asset && restored->asset_locale == page.entries[1].asset_locale &&
                translated.locale == locale,"Restoring active navigation substituted another content language");
        }
        auto math = catalog_child(CatalogLocation{},"math_physics");
        const auto math_courses = windows::load_catalog(root,math);
        check(math_courses.entries.front().id == "math_course_01","Mathematics route missing");
        math = catalog_child(math,math_courses.entries.front().id);
        const auto math_units = windows::load_catalog(root,math);
        check(!math_units.entries.empty(),"Mathematics indexed units hidden");
        math = catalog_child(math,math_units.entries.front().id);
        check(!windows::load_catalog(root,math).entries.empty(),"Mathematics indexed lessons hidden");
        for (const auto id : {"..","a/b","a\\b","C:bad","a.json","","x y"})
            rejects([&] { (void)catalog_child(CatalogLocation{"es","programming",{},{}},id); },"Unsafe course path accepted");
        rejects([&] { (void)catalog_child(CatalogLocation{},"unknown"); },"Unknown route accepted");
        rejects([&] { (void)catalog_child(CatalogLocation{"es","programming","cpp_fundamentals",{}},"other__unit"); },"Cross-course unit accepted");
        rejects([&] { (void)catalog_lesson_path(location,"other__unit__lesson"); },"Cross-unit lesson accepted");
        rejects([&] { (void)windows::load_catalog(root,{"es","programming","unindexed",{}}); },"An unpublished directory can activate a draft course");
        rejects([&] { (void)windows::load_catalog(root,{"es","programming","cpp_fundamentals","cpp_fundamentals__unindexed"}); },"An unpublished unit can activate drafts");
        rejects([&] { (void)windows::load_catalog(root,{"de",{},{},{}}); },"Unsupported locale silently reinterpreted");
        state::StateData progress;
        check(!catalog_lesson_started(progress,lessons.entries[1].id),"A catalog visit marked a lesson started");
        state::set_saved_page(progress,lessons.entries[1].id,0);
        check(catalog_lesson_started(progress,lessons.entries[1].id) && !catalog_lesson_completed(progress,lessons.entries[1].id),"Opening page zero became completion or lost started status");
        progress.preferences["learning_progress"]["lesson_" + lessons.entries[1].id + "_completed"] = true;
        check(catalog_lesson_completed(progress,lessons.entries[1].id),"Original imported completion identity was ignored");
        const auto scope = windows::catalog_scope(root,location);
        check(scope.lessons.size() == 5 && scope.courses == std::vector<std::string>{"cpp_fundamentals"} && scope.units.size() == 1,"Reset scope differs from published index");
        const auto lesson_id = lessons.entries[1].id;
        progress.preferences["learning_progress"]["exercise_" + lesson_id + "_page_4_block_2"] = std::string("saved answer");
        progress.preferences["learning_progress"]["exercise_" + lesson_id + "_state_v2_" + std::string(64,'a') + "_page_6_block_1"] = std::string("added answer");
        progress.preferences["learning_progress"]["lesson_" + lesson_id + "_state_lineage_v2_es"] = std::string("immutable ownership");
        progress.preferences["learning_progress"]["lesson_" + lesson_id + "_read_pages_v2_es"] = std::vector<std::string>{"page_0000"};
        progress.preferences["learning_progress"]["lesson_" + lesson_id + "_extra_completed"] = true;
        progress.preferences["learning_progress"]["exercise_" + lesson_id + "_extra_page_0_block_0"] = std::string("other lesson sharing prefix");
        progress.preferences["learning_progress"]["lesson_math_course_01__numeros_y_operaciones__otra_completed"] = true;
        progress.preferences["cpp_lesson_drafts"]["programming/" + lesson_id + "/4/2/source"] = std::string("user source intact");
        progress.preferences["narration_settings"]["voice"] = std::string("ef_dora");
        progress.preferences["native_windows"]["reference-proof/v1/es/programming/" + lesson_id + "/4/2"] = std::string("old receipt");
        const auto original_progress = progress;
        check(catalog_progress(progress,scope).completed == 1,"Real completion summary does not reflect imported data");
        check(reset_catalog_progress(progress,scope) >= 5 && !catalog_lesson_started(progress,lesson_id),"Reset did not clear its real lesson status");
        check(state::find(progress,"learning_progress","exercise_" + lesson_id + "_page_4_block_2") == nullptr &&
            state::find(progress,"learning_progress","exercise_" + lesson_id + "_state_v2_" + std::string(64,'a') + "_page_6_block_1") == nullptr,"Reset retained scoped responses");
        check(state::string_value(progress,"learning_progress","lesson_" + lesson_id + "_state_lineage_v2_es") == "immutable ownership" &&
            std::get<std::vector<std::string>>(*state::find(progress,"learning_progress","lesson_" + lesson_id + "_read_pages_v2_es")).empty(),"Reset corrupted state ownership or retained read coverage");
        check(state::find(progress,"learning_progress","lesson_" + lesson_id + "_extra_completed") != nullptr &&
            state::find(progress,"learning_progress","exercise_" + lesson_id + "_extra_page_0_block_0") != nullptr &&
            state::find(progress,"learning_progress","lesson_math_course_01__numeros_y_operaciones__otra_completed") != nullptr,"Reset crossed its exact lesson/route boundary");
        check(progress.preferences.at("cpp_lesson_drafts") == original_progress.preferences.at("cpp_lesson_drafts") &&
            progress.preferences.at("narration_settings") == original_progress.preferences.at("narration_settings"),"Reset altered personal drafts or voice preferences");
        std::cout << "Catalog real indices, both routes, three languages, historical paths/order, safe parents and read-only progress: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

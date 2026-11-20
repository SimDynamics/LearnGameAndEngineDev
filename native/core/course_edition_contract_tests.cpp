// A portable core-only check of the real course-edition identity contract.
// No synthetic learner data is written and no Windows-only reader is started.
// This is not a substitute for the separately pinned full Windows integration.
#include "core/course_edition.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using lgaed::curriculum::CourseEdition;
using lgaed::curriculum::course_edition_frame;
using lgaed::curriculum::course_edition_identifier;
using lgaed::curriculum::course_edition_lesson_id;
using lgaed::curriculum::course_edition_prefix;
using lgaed::curriculum::course_edition_slug;
using lgaed::curriculum::course_edition_unit_id;
using lgaed::curriculum::uses_course_edition_namespace;
using lgaed::curriculum::validate_course_edition;

void require(bool condition, std::string_view message) {
    if(!condition) throw std::runtime_error(std::string(message));
}
template<class Action>
void rejects(Action action, std::string_view message) {
    try {
        action();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void published_generation_and_scope() {
    const CourseEdition first{"cpp_fundamentals", "es", "revision_desde_inicio", 1};
    const CourseEdition second{"cpp_fundamentals", "es", "segunda_revision", 2};
    const CourseEdition french{"cpp_fundamentals", "fr", "revision_desde_inicio", 1};
    const CourseEdition other{"programming_practice_tools", "es", "revision_desde_inicio", 1};
    const std::string unit = "cpp_fundamentals__edition_es_1__programas";
    const std::string lesson = unit + "__primer_programa";
    require(course_edition_prefix(first) == "cpp_fundamentals__edition_es_1__",
        "The initial Spanish edition prefix is not stable");
    require(course_edition_unit_id(first,unit) &&
        course_edition_lesson_id(first,lesson),
        "The actual new Spanish unit/lesson identity was not recognized");
    require(!course_edition_unit_id(second,unit) &&
        !course_edition_lesson_id(second,lesson) &&
        !course_edition_unit_id(french,unit) &&
        !course_edition_lesson_id(other,lesson),
        "Progress from another generation, language or course was aliased");
    require(course_edition_frame(first) != course_edition_frame(second) &&
        course_edition_frame(first) != course_edition_frame(french) &&
        course_edition_frame(first) != course_edition_frame(other) &&
        course_edition_frame(first) !=
            course_edition_frame(CourseEdition{"cpp_fundamentals","es","otro_texto",1}),
        "An edition receipt frame reused another authored identity");
    require(!uses_course_edition_namespace("cpp_fundamentals__programas__primer_programa") &&
         uses_course_edition_namespace(lesson),
        "The pre-edition namespace cannot impersonate a new course");
}

void descriptor_boundaries() {
    for(const auto& valid : {
            CourseEdition{"cpp_fundamentals","es","p1",1},
            CourseEdition{"cpp_fundamentals","en","review_01",2},
            CourseEdition{"cpp_fundamentals","fr","revision-v2",1000000},
            CourseEdition{"QA_Edition-2","es","v1",123}}) {
        validate_course_edition(valid);
        require(!course_edition_frame(valid).empty(),
            "A valid edition descriptor cannot produce its own receipt frame");
    }
    auto invalid = CourseEdition{"cpp_fundamentals","es","revision",1};
    invalid.course_id = "";
    rejects([&]{validate_course_edition(invalid);},"An empty parent course was accepted");
    invalid = {"cpp_fundamentals__edition_es_1","es","revision",1};
    rejects([&]{validate_course_edition(invalid);},"Reserved course namespace was reused");
    invalid = {"cpp_fundamentals","es","legacy",1};
    rejects([&]{validate_course_edition(invalid);},"The legacy edition ID was reused");
    invalid = {"cpp_fundamentals","pt","revision",1};
    rejects([&]{validate_course_edition(invalid);},"An undeclared locale was accepted");
    invalid = {"cpp_fundamentals","es","revision",0};
    rejects([&]{validate_course_edition(invalid);},"Generation zero was accepted");
    invalid = {"cpp_fundamentals","es","revision",1000001};
    rejects([&]{validate_course_edition(invalid);},"An out-of-range generation was accepted");
    invalid = {"cpp_fundamentals","es",std::string(65,'a'),1};
    rejects([&]{validate_course_edition(invalid);},"An oversized edition label was accepted");
    invalid = {std::string(257,'c'),"es","revision",1};
    rejects([&]{validate_course_edition(invalid);},"An oversized parent course was accepted");
    invalid = {"cpp_fundamentals","es","revision/1",1};
    rejects([&]{validate_course_edition(invalid);},"An unsafe edition label was accepted");
}

void published_id_boundaries() {
    const CourseEdition edition{"cpp_fundamentals","es","revision",1};
    const auto prefix = course_edition_prefix(edition);
    const auto valid_unit = prefix + "textos_y_secuencias";
    const auto valid_lesson = valid_unit + "__texto_inicial";
    require(course_edition_unit_id(edition,valid_unit) &&
            course_edition_lesson_id(edition,valid_lesson),
        "A newly authored lesson with descriptive slugs was rejected");
    for(const auto& invalid : {
            prefix, prefix + "_inicio", prefix + "inicio_",
            prefix + "texto__extra", prefix + "inicio/otro",
            prefix + "inicio__otra__rama",
            std::string("cpp_fundamentals__edition_es_01__inicio"),
            std::string("cpp_fundamentals__edition_es_2__inicio")}) {
        require(!course_edition_unit_id(edition, invalid),
            "An invalid or cross-generation unit was accepted");
    }
    for(const auto& invalid : {
            valid_unit, valid_unit + "__", valid_unit + "___mal",
            valid_unit + "__mal_", valid_unit + "__mal__sufijo",
            prefix + "otra__primera__extra",
            std::string("cpp_fundamentals__edition_fr_1__inicio__texto"),
            std::string("cpp_fundamentals__edition_es_2__inicio__texto")}) {
        require(!course_edition_lesson_id(edition, invalid),
            "An invalid or cross-generation lesson was accepted");
    }
    require(course_edition_unit_id(edition,prefix + "Z") &&
            course_edition_lesson_id(edition,prefix + "Z__9"),
        "A short valid unit and lesson slug cannot be opened");
    require(!course_edition_slug("_pagina") && !course_edition_slug("pagina_") &&
            !course_edition_slug("pagina__otra") && !course_edition_slug("") &&
            course_edition_slug("pagina_1"),
        "The published single-slug grammar was weakened");
    require(course_edition_identifier(std::string(256,'c'),256) &&
           !course_edition_identifier(std::string(257,'c'),256) &&
           !course_edition_identifier("curso/externo",256),
        "An unsafe or oversized course identifier was accepted");
    // The combined ID has a 256-byte ceiling, but each individual
    // unit/lesson slug also has its own 128-byte ceiling. Choose a longer
    // valid course ID so the complete boundary is tested without violating
    // the independent slug rule.
    const CourseEdition long_course{std::string(130,'c'),"es","revision",1};
    const auto long_prefix = course_edition_prefix(long_course);
    require(long_prefix.size()<256 && 256-long_prefix.size()<=128,
        "The long-course boundary fixture is not a valid slug");
    const auto unit_boundary = long_prefix + std::string(256-long_prefix.size(),'x');
    require(course_edition_unit_id(long_course,unit_boundary) &&
        !course_edition_unit_id(long_course,unit_boundary+'x'),
        "An edition unit exceeded the fixed historical ID limit");
    const auto lesson_prefix = long_prefix + "u__";
    const auto lesson_boundary = lesson_prefix + std::string(256-lesson_prefix.size(),'x');
    require(course_edition_lesson_id(long_course,lesson_boundary) &&
        !course_edition_lesson_id(long_course,lesson_boundary+'x'),
        "An edition lesson exceeded the fixed historical ID limit");
}
} // namespace

int main() {
    try {
        published_generation_and_scope();
        descriptor_boundaries();
        published_id_boundaries();
        std::cout << "PASS portable native course editions: scoped IDs, epochs, "
                     "namespaces and independent progress frames.\n";
    } catch(const std::exception& error) {
        std::cerr << "FAIL portable native course editions: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

"""One-time guarded reader-focus repair and isolated native verification setup."""
from pathlib import Path
import argparse
import os
import re

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'native/presentation/windows'
QA = Path(os.environ['RUNNER_TEMP']) / 'reader-arrow-focus'


def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Changed or ambiguous repair anchor: ' + old[:120])
    return source.replace(old, new, 1)


def old_guard(source):
    start = source.index('bool owns_arrow_keys(HWND target,HWND view,HWND bar,const MSG& message){')
    end = source.index('\n}\n}\nstruct LessonReader::Impl', start) + 2
    return source[start:end]


def prepare():
    QA.mkdir(parents=True, exist_ok=True)
    baseline = QA / 'baseline/presentation/windows/reader_shortcut_focus.hpp'
    baseline.parent.mkdir(parents=True, exist_ok=True)
    original = old_guard((NATIVE / 'lesson_reader.cpp').read_text(encoding='utf-8-sig'))
    original = once(original, 'const MSG& message){', 'const MSG& message,const ReadingText&){')
    baseline.write_text('#pragma once\n#include <windows.h>\n#include <richedit.h>\n#include <cwchar>\n#include <initializer_list>\nnamespace lgaed::presentation::reader_shortcuts {\ntemplate<class ReadingText>\n' + original + '\n}\n', encoding='utf-8')
    config = (ROOT / 'cmake/native_build_config.hpp.in').read_text(encoding='utf-8-sig')
    config = config.replace('@CMAKE_SOURCE_DIR@', ROOT.as_posix()).replace('@LGAED_DEFAULT_PROFILE@', 'Codex')
    (QA / 'native_build_config.hpp').write_text(re.sub(r'@[A-Z_]+@', '', config), encoding='utf-8')
    cmake = '''cmake_minimum_required(VERSION 3.25)
project(ReaderArrowFocusQA LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
add_executable(focus_baseline "@ROOT@/native/presentation/windows/reader_shortcut_focus_tests.cpp")
target_include_directories(focus_baseline PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/baseline" "@ROOT@/native")
target_link_libraries(focus_baseline PRIVATE user32 comctl32)
add_executable(focus_fixed "@ROOT@/native/presentation/windows/reader_shortcut_focus_tests.cpp")
target_include_directories(focus_fixed PRIVATE "@ROOT@/native")
target_link_libraries(focus_fixed PRIVATE user32 comctl32)
add_library(reader_compile_check OBJECT "@ROOT@/native/presentation/windows/lesson_reader.cpp")
target_include_directories(reader_compile_check PRIVATE "@ROOT@/native" "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(reader_tests_compile_check OBJECT "@ROOT@/native/presentation/windows/lesson_reader_tests.cpp")
target_include_directories(reader_tests_compile_check PRIVATE "@ROOT@/native" "${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_test(NAME native.reader_shortcut_focus COMMAND focus_fixed)
'''.replace('@ROOT@', ROOT.as_posix())
    (QA / 'CMakeLists.txt').write_text(cmake, encoding='utf-8')


def fix():
    path = NATIVE / 'lesson_reader.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = once(source, '#include "presentation/windows/lesson_reader.hpp"', '#include "presentation/windows/lesson_reader.hpp"\n#include "presentation/windows/reader_shortcut_focus.hpp"')
    source = once(source, old_guard(source), '')
    source = once(source, '''        // Arrow aliases yield to native cursor/selection and interactive controls,
        // including readonly text. Function-key shortcuts keep their existing scope.
        if(arrow_key&&(owns_arrow_keys(GetFocus(),view,window,message)||owns_arrow_keys(message.hwnd,view,window,message)))return false;''', '''        // Lesson prose is also RichEdit, but it is not an answer/code editor.
        // Only unselected live narration targets may yield their arrow keys;
        // native inputs, source viewers and interactive controls retain them.
        if(arrow_key){
            const auto reading_text=[&](HWND target){return std::ranges::any_of(regions,
                [target](const auto& region){return region.target==target;});};
            if(reader_shortcuts::owns_arrow_keys(GetFocus(),view,window,message,reading_text)||
                reader_shortcuts::owns_arrow_keys(message.hwnd,view,window,message,reading_text))return false;
        }''')
    path.write_text(source, encoding='utf-8', newline='\n')
    path = NATIVE / 'lesson_reader_tests.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = once(source, '''    DestroyWindow(list);DestroyWindow(math);
    std::cout<<"PASS F5/Left -3''', '''    // Normal reading commonly leaves focus on a paragraph or the header's
    // reader button, not on the floating playback controls.
    SendMessageW(readonly,EM_SETSEL,0,0);
    for(const auto target:{readonly,child(f.view,111),child(f.view,1111)}){
        SetFocus(target);check(GetFocus()==target,"Cannot focus an ordinary reading surface");
        for(const auto& [key,button]:mappings){const auto count=probe.commands.size();
            check(press(key,target)&&probe.commands.size()==count+1&&probe.commands.back()==button&&GetFocus()==target,
                "A real prose/header/compile-button focus blocked an existing seek action or changed focus");
            check(press(key,target,WM_KEYDOWN,1LL|(1LL<<30))&&probe.commands.size()==count+1,
                "An autorepeated prose seek activated the reader twice");
        }
    }
    check(edit_text(readonly)==readonly_text&&edit_text(source)==source_text,
        "Reading shortcuts rewrote curricular prose or source code");
    DestroyWindow(list);DestroyWindow(math);
    std::cout<<"PASS F5/Left -3''')
    path.write_text(source, encoding='utf-8', newline='\n')
    path = ROOT / 'CMakeLists.txt'
    source = path.read_text(encoding='utf-8-sig')
    source = once(source, 'if(BUILD_TESTING)\n', '''if(BUILD_TESTING)
    add_executable(lgaed_reader_shortcut_focus_tests native/presentation/windows/reader_shortcut_focus_tests.cpp)
    target_link_libraries(lgaed_reader_shortcut_focus_tests PRIVATE lgaed_host_options user32 comctl32)
    add_test(NAME native.reader_shortcut_focus COMMAND lgaed_reader_shortcut_focus_tests)
    set_tests_properties(native.reader_shortcut_focus PROPERTIES TIMEOUT 30)
''')
    path.write_text(source, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=['prepare', 'fix'])
    options = parser.parse_args()
    (prepare if options.stage == 'prepare' else fix)()
    print('Guarded reader arrow stage:', options.stage)

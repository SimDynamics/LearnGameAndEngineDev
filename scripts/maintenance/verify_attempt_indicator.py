"""One-time scoped Windows verification; removed after successful publication."""
from pathlib import Path
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
QA = Path(os.environ['RUNNER_TEMP']) / 'attempt-indicator-qa'
VIEW = ROOT / 'native/presentation/windows/lesson_view.cpp'
SETTINGS = ROOT / 'native/presentation/windows/settings_view.cpp'


def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Changed or ambiguous anchor: ' + old[:100])
    return source.replace(old, new)


def prepare():
    QA.mkdir(parents=True, exist_ok=True)
    header = (ROOT/'cmake/native_build_config.hpp.in').read_text(encoding='utf-8-sig')
    for key, value in {'LGAED_DEFAULT_PROFILE':'Codex', 'CMAKE_SOURCE_DIR':ROOT.as_posix(),
                       'LGAED_EDUCATIONAL_VC_ROOT':'', 'LGAED_EDUCATIONAL_SDK_ROOT':'',
                       'CMAKE_COMMAND':'cmake', 'CMAKE_MAKE_PROGRAM':'ninja', 'CMAKE_RC_COMPILER':'rc',
                       'CMAKE_CURRENT_BINARY_DIR':QA.as_posix()}.items():
        header = header.replace('@'+key+'@', value)
    if '@' in header:
        raise RuntimeError('Build configuration needs an unknown substitution')
    (QA/'native_build_config.hpp').write_text(header, encoding='utf-8')
    # Extract the original draw statements, so the pre-fix test uses precisely
    # the old white-under-accent painting rather than an invented approximation.
    settings = SETTINGS.read_text(encoding='utf-8-sig')
    first = settings.index('draw_icon(memory,slot,icon,RGB(255,255,255));')
    last = settings.index('RestoreDC(memory,saved);', first) + len('RestoreDC(memory,saved);')
    old_draw = settings[first:last].replace('const auto progress=std::clamp(current.reserve-index,0.,1.);',
                                           'progress=std::clamp(progress,0.,1.);')
    if 'current.' in old_draw or 'details->' in old_draw:
        raise RuntimeError('Baseline draw is no longer independently extractable')
    view = VIEW.read_text(encoding='utf-8-sig')
    timer = 'auto remaining=bounds;remaining.top=remaining.bottom-px(13);'
    if view.count(timer) != 1:
        raise RuntimeError('Original timer anchor moved')
    baseline = '#pragma once\n#include "presentation/windows/icons.hpp"\n#include <algorithm>\n#include <cmath>\n'
    baseline += 'namespace lgaed::presentation::attempt_indicator {\n'
    baseline += 'inline RECT recovery_bounds(RECT bounds,const RECT&) {\n'
    baseline += ' const int dpi=MulDiv(bounds.bottom-bounds.top,96,64); const auto px=[&](int value){return MulDiv(value,dpi,96);};\n'
    baseline += ' '+timer+' return remaining;\n}\n'
    baseline += 'inline void draw(HDC dc,const RECT& bounds,Icon icon,COLORREF accent,COLORREF,double progress) {\n'
    baseline += ' const auto memory=dc;const auto slot=bounds;const auto tint=accent;\n'+old_draw+'\n}\n}\n'
    target = QA/'baseline/presentation/windows/attempt_indicator.hpp'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(baseline, encoding='utf-8')
    cmake = r'''cmake_minimum_required(VERSION 3.24)
project(AttemptIndicatorQA LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "@ROOT@")
add_library(options INTERFACE)
target_compile_definitions(options INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(options INTERFACE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
target_include_directories(options INTERFACE "${ROOT}/native" "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(name baseline fixed)
  add_executable(${name} "${ROOT}/native/presentation/windows/attempt_indicator_tests.cpp" "${ROOT}/native/presentation/windows/icons.cpp")
  target_link_libraries(${name} PRIVATE options user32 gdi32 gdiplus)
endforeach()
target_include_directories(baseline BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/baseline")
add_library(views_compile OBJECT "${ROOT}/native/presentation/windows/lesson_view.cpp" "${ROOT}/native/presentation/windows/settings_view.cpp")
target_link_libraries(views_compile PRIVATE options)
enable_testing()
add_test(NAME native.attempt_indicator COMMAND fixed)
set_tests_properties(native.attempt_indicator PROPERTIES TIMEOUT 30)
'''.replace('@ROOT@', ROOT.as_posix())
    (QA/'CMakeLists.txt').write_text(cmake, encoding='utf-8')


def fix():
    view = VIEW.read_text(encoding='utf-8-sig')
    view = once(view, '#include "presentation/windows/icons.hpp"\n',
                '#include "presentation/windows/icons.hpp"\n#include "presentation/windows/attempt_indicator.hpp"\n')
    start = view.index('            const auto split=glyph.left+')
    end = view.index('            if(attempt_state.premium){', start)
    old = view[start:end]
    if old.count('draw_icon(') != 2 or 'IntersectClipRect' not in old:
        raise RuntimeError('Header painting scope changed')
    view = once(view, old, '            attempt_indicator::draw(item.hDC,glyph,route_icon,ink,\n'
                          '                colors.high_contrast?colors.text:RGB(255,255,255),progress);\n')
    view = once(view, 'auto remaining=bounds;remaining.top=remaining.bottom-px(13);',
                'auto remaining=attempt_indicator::recovery_bounds(bounds,glyph);')
    settings = SETTINGS.read_text(encoding='utf-8-sig')
    settings = once(settings, '#include "presentation/windows/icons.hpp"\n',
                    '#include "presentation/windows/icons.hpp"\n#include "presentation/windows/attempt_indicator.hpp"\n')
    old = '''            const auto inset=std::max(1,width/12);InflateRect(&slot,-inset,-inset);draw_icon(memory,slot,icon,RGB(255,255,255));
            const auto saved=SaveDC(memory);const auto progress=std::clamp(current.reserve-index,0.,1.);
            IntersectClipRect(memory,slot.left,slot.top,slot.left+static_cast<int>((slot.right-slot.left)*progress),slot.bottom);draw_icon(memory,slot,icon,tint);RestoreDC(memory,saved);'''
    settings = once(settings, old, '''            const auto inset=std::max(1,width/12);InflateRect(&slot,-inset,-inset);
            const auto progress=std::clamp(current.reserve-index,0.,1.);
            attempt_indicator::draw(memory,slot,icon,tint,RGB(255,255,255),progress);''')
    cmake_path = ROOT/'CMakeLists.txt'
    cmake = cmake_path.read_text(encoding='utf-8-sig')
    anchor = '    add_executable(lgaed_card_list_tests native/presentation/windows/card_list_tests.cpp)\n'
    cmake = once(cmake, anchor, '''    add_executable(lgaed_attempt_indicator_tests native/presentation/windows/attempt_indicator_tests.cpp
        native/presentation/windows/icons.cpp)
    target_link_libraries(lgaed_attempt_indicator_tests PRIVATE lgaed_host_options user32 gdi32 gdiplus)
    add_test(NAME native.attempt_indicator COMMAND lgaed_attempt_indicator_tests)
    set_tests_properties(native.attempt_indicator PROPERTIES TIMEOUT 30)
'''+anchor)
    # All anchors passed before writing any production file.
    for path, source in ((VIEW,view),(SETTINGS,settings),(cmake_path,cmake)):
        path.write_text(source, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    stage = sys.argv[1]
    {'prepare':prepare, 'fix':fix}[stage]()
    print('Attempt indicator stage:', stage)

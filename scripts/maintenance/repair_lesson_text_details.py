from pathlib import Path
import subprocess

ROOT = Path.cwd()
BASE = '885d6f536394d9980e97935cf0ae6119deba135e'
changed = []

def load(name):
    path = ROOT / name
    actual = path.read_text(encoding='utf-8-sig')
    previous = subprocess.check_output(['git', 'show', f'{BASE}:{name}']).decode('utf-8-sig')
    if actual != previous.replace('\r\n','\n'):
        raise RuntimeError(f'Concurrent source change: {name}')
    return actual

def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f'Non-unique repair anchor: {old[:100]!r}')
    return text.replace(old,new)

def save(name, text):
    (ROOT / name).write_text(text,encoding='utf-8',newline='\n')
    changed.append(name)

name = 'native/presentation/windows/rich_text.cpp'
s = load(name)
anchor = 'std::size_t inline_cpp_statement_end(std::wstring_view text,std::size_t initial) {'
helper = r'''// Only an explicitly introduced C++ literal is a string seed in prose.
// Ordinary quotation marks, unfinished quotes and paragraph punctuation are not.
std::vector<std::pair<std::size_t,std::size_t>> inline_string_literals(std::wstring_view text) {
    static const std::wregex cue(
        LR"((?:literal(?: de (?:cadena|texto))?|cadena(?: de (?:texto|caracteres))?)(?: exact[oa])?[ \t]+)",
        std::regex_constants::icase);
    std::vector<std::pair<std::size_t,std::size_t>> result;
    for(std::regex_iterator<std::wstring_view::const_iterator> match(text.begin(),text.end(),cue),last;match!=last;++match){
        const auto word=static_cast<std::size_t>(match->position());
        const auto begin=word+static_cast<std::size_t>(match->length());
        if((word&&!technical_mention_boundary(text[word-1]))||begin>=text.size()||text[begin]!=L'"')continue;
        if(!result.empty()&&begin<result.back().second)continue;
        for(auto end=begin+1;end<text.size()&&text[end]!=L'\r'&&text[end]!=L'\n';++end){
            if(text[end]==L'\\'){
                if(end+1==text.size()||text[end+1]==L'\r'||text[end+1]==L'\n')break;
                ++end;
            }else if(text[end]==L'"'){
                result.emplace_back(begin,end+1);break;
            }
        }
    }
    return result;
}
'''
s = replace(s,anchor,helper+anchor)
s = replace(s,'''    }
    for (const auto& range : curriculum::emphasis_ranges(text, emphasis)) {''', '''        // Last lexical layer: operator-looking characters inside an explicit
        // literal keep string ink. Do not change its face, text or source offsets.
        for(const auto& [begin,end]:inline_string_literals(source))
            format(edit,begin,end,accessible(edit,RGB(122,195,141)));
    }
    for (const auto& range : curriculum::emphasis_ranges(text, emphasis)) {''')
save(name,s)

name = 'native/presentation/windows/order_code_view.cpp'
s = load(name)
s = replace(s,'#include <richedit.h>','#include <richedit.h>\n#include <richole.h>\n#include <tom.h>\n#include <wrl/client.h>')
s = replace(s,'''        rect.right=std::max(rect.left+1,rect.right-px(16));rect.bottom=std::max(rect.top+1,rect.bottom-px(8));''', '''        // RichEdit scales both edge insets. Convert the right/bottom margins
        // too, instead of reserving a larger, clipping inset at non-100% zoom.
        rect.right=std::max(rect.left+1,rect.right-MulDiv(px(16),100,percentage));
        rect.bottom=std::max(rect.top+1,rect.bottom-MulDiv(px(8),100,percentage));''')
a = s.index('    SIZE text_size(HWND row,int fallback_line){')
b = s.index('    void layout(int w){',a)
s = s[:a]+r'''    SIZE text_size(HWND row,int fallback_line){
        // Measure the actual TextServices line boxes, not an auxiliary GDI
        // font's estimate. A single-line card has no next baseline from which
        // to infer its height, especially with zoom or font fallback.
        POINTL first{};SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
        const auto count=SendMessageW(row,EM_GETLINECOUNT,0,0);
        Microsoft::WRL::ComPtr<IRichEditOle> ole;
        SendMessageW(row,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;if(ole)(void)ole.As(&document);
        LONG right=first.x,bottom=first.y+fallback_line;
        constexpr LONG flags=tomStart|tomClientCoord|tomAllowOffClient;
        const auto include=[&](LONG character){
            Microsoft::WRL::ComPtr<ITextRange> range;
            LONG x=0,y=0;
            if(document&&document->Range(character,character,&range)==S_OK&&range){
                if(range->GetPoint(flags|TA_RIGHT|TA_BOTTOM,&x,&y)==S_OK){
                    right=std::max(right,x);bottom=std::max(bottom,y);
                }
            }
        };
        for(LRESULT line=0;line<count;++line){
            const auto start=SendMessageW(row,EM_LINEINDEX,static_cast<WPARAM>(line),0);
            if(start<0)continue;
            const auto length=SendMessageW(row,EM_LINELENGTH,static_cast<WPARAM>(start),0);
            POINTL end{},position{};
            SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),start+length);
            SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&position),start);
            if(end.y==position.y)right=std::max(right,end.x);
            bottom=std::max(bottom,position.y+fallback_line);
            include(static_cast<LONG>(start));
            if(length>0)include(static_cast<LONG>(start+length-1));
        }
        // Two device pixels cover final glyph overhang/rounding; all original
        // text, compact spaces, syntax colors and selection remain untouched.
        return {std::max(1L,right-first.x)+px(32)+2,std::max(1L,bottom-first.y)+px(16)+2};
    }
'''+s[b:]
s = replace(s,'''            SendMessageW(row,EM_REQUESTRESIZE,0,0);sizes[i]=text_size(row,line);''','''            POINT origin{};SendMessageW(row,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&origin));
            SendMessageW(row,EM_REQUESTRESIZE,0,0);sizes[i]=text_size(row,line);''')
save(name,s)

name = 'native/narration/worker/build_worker.py'
s = load(name)
needle = '    if name == "SpanishTechnicalSpeech":\n'
adaptations = [
    ('                    result += "abre $name"',
     '                    result += if (char == \'"\') name else "abre $name"'),
    ('                    result += "cierra $name"',
     '                    result += if (char == \'"\') name else "cierra $name"'),
    ('    private val codeOperators = listOf(',
     '    private val codeOperators = listOf(\n        "//" to "doble diagonal",'),
    (r"""text[index] == '"' && (inside.contains('\\')""",
     r"""text[index] == '"' && (inside.isEmpty() || inside == "//" || inside.contains('\\')"""),
]
literal_anchor = """    private fun readLiteral(source: String): String = buildList {
        var index = 0
        while (index < source.length) {
            val char = source[index]"""
adaptations.append((literal_anchor, literal_anchor + """
            if (source.startsWith("//", index)) {
                add("doble diagonal"); index += 2; continue
            }"""))
addition = '        # Windows-only labels; retain canonical parsing and source ownership.\n'
addition += ''.join(f'        code = exact(code, {old!r}, {new!r})\n' for old,new in adaptations)
s = replace(s,needle,needle+addition)
save(name,s)

name='CMakeLists.txt'
s=load(name)
s+='''
if(BUILD_TESTING)
    add_executable(lgaed_lesson_text_display_tests native/presentation/windows/lesson_text_display_tests.cpp)
    target_link_libraries(lgaed_lesson_text_display_tests PRIVATE lgaed_presentation_windows)
    set_target_properties(lgaed_lesson_text_display_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    add_test(NAME native.lesson_text_color COMMAND lgaed_lesson_text_display_tests color)
    add_test(NAME native.order_text_bounds COMMAND lgaed_lesson_text_display_tests geometry)
endif()
'''
save(name,s)
print('Patched:', ', '.join(changed))

from pathlib import Path
import os, subprocess, re, json

root = Path.cwd()
base = '85f1b10c22c55ddef466a395af133209cd63afbf'
maintenance = 'scripts/maintenance/repair_inline_ui.py'
workflow = '.github/workflows/one-time-inline-ui.yml'
changed = subprocess.check_output(['git','diff','--name-only',base,'HEAD'], text=True).splitlines()
if set(changed) - {maintenance, workflow, 'native/presentation/windows/inline_presentation_tests.cpp'}:
    raise RuntimeError('Source advanced since the reviewed base; refusing to overwrite it: '+repr(changed))
files = {}
def load(name):
    if name not in files: files[name] = (root/name).read_text(encoding='utf-8')
    return files[name]
def replace(name, before, after, count=1):
    text = load(name)
    if text.count(before) != count:
        raise RuntimeError(f'{name}: expected {count} exact anchors, got {text.count(before)}: {before[:100]}')
    files[name] = text.replace(before, after)
def insert_before(name, anchor, addition): replace(name, anchor, addition+anchor)
P = 'native/presentation/windows/'
# The hint remains an overlay. No placeholder characters enter the document.
replace(P+'rich_text.hpp', 'void set_edit_hint(HWND', 'void set_edit_hint(HWND', 1)
h = load(P+'rich_text.hpp')
pattern = r'(void set_edit_hint\([^;]+)(\);)'
m = re.search(pattern,h)
if not m: raise RuntimeError('Missing edit hint declaration')
files[P+'rich_text.hpp'] = h[:m.start()] + m.group(1) + ', bool centered = false' + m.group(2) + h[m.end():]
replace(P+'rich_text.cpp', '    bool normalizing_selection=false;', '    bool normalizing_selection=false,centered=false;')
replace(P+'rich_text.cpp', '        IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);',
'''        // Center the hint in the same native formatting box as the empty
        // insertion point. Its baseline still comes from TextServices.
        if(centered){bounds.left=viewport.left;bounds.right=viewport.right;}
        IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);''')
replace(P+'rich_text.cpp', 'DT_LEFT|DT_TOP|wrapping|DT_NOPREFIX);', '(centered?DT_CENTER:DT_LEFT)|DT_TOP|wrapping|DT_NOPREFIX);')
replace(P+'rich_text.cpp', 'void set_edit_hint(HWND edit,std::string_view text,COLORREF color) {',
        'void set_edit_hint(HWND edit,std::string_view text,COLORREF color,bool centered) {')
replace(P+'rich_text.cpp', 'hint->text=wide(text);hint->color=color;', 'hint->text=wide(text);hint->color=color;hint->centered=centered;', 2)
# Output snippets are data, not a new C++ translation unit. Preserve content.
replace(P+'rich_text.cpp', '            apply_cpp(edit, fragment, begin_wide, true);',
'''            // Explicit output prose gives this backtick span data semantics.
            // In particular, "La salida es `Carga lista`" must not lex its
            // words as two identifiers. Other code spans keep the C++ lexer.
            static const std::wregex output_cue(
                LR"((?:la salida es|salida(?: exacta)?[ :=]+|the output is|output[ :=]+|la sortie est|sortie[ :=]+)[ \\t]*$)",
                std::regex_constants::icase);
            const auto prefix=std::wstring_view(source).substr(0,begin_wide);
            const auto tail=prefix.substr(prefix.size()>128?prefix.size()-128:0);
            if(std::regex_search(tail.begin(),tail.end(),output_cue))
                format(edit,begin_wide,begin_wide+fragment.size(),accessible(edit,RGB(122,195,141)));
            else apply_cpp(edit, fragment, begin_wide, true);''')
# Shared completion overlay. Its source-over result is exactly the original
# (29,78,52) on the Android catalogue base (32,33,38), without opaque repainting.
insert_before(P+'reader_theme.hpp', 'void primary_button(HWND button, bool primary = true);',
'''// Source-over completion tint, calibrated to the existing Android catalogue.
[[nodiscard]] COLORREF completion_background(COLORREF surface);
void completed_control(HWND window, bool completed);
''')
replace(P+'reader_theme.cpp', 'constexpr wchar_t primary_property[]',
'''constexpr wchar_t completed_property[] = L"LGAED.ReaderCompleted";
constexpr wchar_t primary_property[]''')
replace(P+'reader_theme.cpp', '        RemovePropW(window,icon_property);',
        '        RemovePropW(window,completed_property);RemovePropW(window,icon_property);')
replace(P+'reader_theme.cpp', '''COLORREF background(HWND window) {
    return applied(window) ? color(palette(window), static_cast<Surface>(surface_index(window))) : GetSysColor(COLOR_WINDOW);
}''', '''COLORREF completion_background(COLORREF surface) {
    // Alpha 56/255 over the actual local surface, not a layered child HWND.
    // On catalogue RGB(32,33,38) this rounds to the historic RGB(29,78,52).
    constexpr unsigned alpha=56;
    const auto over=[](unsigned source,unsigned destination){return (source*alpha+destination*(255-alpha)+127)/255;};
    return RGB(over(18,GetRValue(surface)),over(238,GetGValue(surface)),over(102,GetBValue(surface)));
}
COLORREF background(HWND window) {
    const auto colors=palette(window);
    const auto surface=applied(window)?color(colors,static_cast<Surface>(surface_index(window))):GetSysColor(COLOR_WINDOW);
    return !colors.high_contrast&&GetPropW(window,completed_property)?completion_background(surface):surface;
}
void completed_control(HWND window,bool completed) {
    if(!IsWindow(window))return;
    if((GetPropW(window,completed_property)!=nullptr)==completed)return;
    if(completed)SetPropW(window,completed_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    else RemovePropW(window,completed_property);
    if(rich_edit(window))SendMessageW(window,EM_SETBKGNDCOLOR,0,background(window));
    InvalidateRect(window,nullptr,FALSE);
}''')
replace(P+'reader_theme.cpp', '    result = reinterpret_cast<LRESULT>(brush(static_cast<Surface>(surface_index(window))));',
'''    if(GetPropW(window,completed_property)&&!palette(window).high_contrast){
        SetDCBrushColor(dc,background(window));result=reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }else result = reinterpret_cast<LRESULT>(brush(static_cast<Surface>(surface_index(window))));''')
replace(P+'reader_theme.cpp', '    const bool premium = GetPropW(item.hwndItem,premium_property) != nullptr;',
'''    const bool premium = GetPropW(item.hwndItem,premium_property) != nullptr;
    const bool completed = GetPropW(item.hwndItem,completed_property) != nullptr;''')
replace(P+'reader_theme.cpp', '    const auto saved = SaveDC(item.hDC);',
'''    // Completed is distinct from disabled/busy. Keep native disabled click
    // semantics, but do not paint a solved exercise as an unavailable action.
    if(completed&&!icon&&!colors.high_contrast){
        fill=background(item.hwndItem);ink=RGB(105,220,145);
        if(contrast_ratio(ink,fill)<4.5)ink=colors.text;
    }
    const auto saved = SaveDC(item.hDC);''')
replace(P+'reader_theme.cpp', '    auto bounds = item.rcItem; InflateRect(&bounds, -7, -3);',
'''    auto bounds = item.rcItem;
    const bool action=GetPropW(item.hwndItem,primary_property)!=nullptr;
    InflateRect(&bounds,action?-px(12):-7,action?-px(4):-3);''')
replace(P+'catalog_view.cpp', 'CreateSolidBrush(RGB(29,78,52))',
        'CreateSolidBrush(reader_theme::completion_background(RGB(32,33,38)))')
# Compact inline fields, preserving the CodeEditor object, undo and lexer.
replace(P+'cpp_fill_view.cpp', '#include <richedit.h>', '#include <richedit.h>\n#include <richole.h>\n#include <tom.h>\n#include <wrl/client.h>')
replace(P+'cpp_fill_view.cpp', '    std::vector<RECT> numbered_lines;',
'''    std::vector<RECT> numbered_lines;
    std::vector<LONG> inline_baselines;
    bool completed=false;''')
replace(P+'cpp_fill_view.cpp', 'WS_TABSTOP|ES_AUTOHSCROLL|(drag()?ES_READONLY:0)',
        'WS_TABSTOP|ES_AUTOHSCROLL|(drag()?ES_READONLY:(ES_MULTILINE|ES_CENTER))')
replace(P+'cpp_fill_view.cpp', '''                    style_cpp(item);
                }
                SetWindowSubclass(item,child_proc''', '''                    style_cpp(item);
                    PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);
                    paragraph.dwMask=PFM_ALIGNMENT|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;
                    paragraph.wAlignment=PFA_CENTER;paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;
                    SendMessageW(item,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
                    set_edit_hint(item,"...",RGB(125,135,145),true);
                }
                SetWindowSubclass(item,child_proc''')
inline_layout = r'''    struct InlineMetrics { int top=0,baseline=0,bottom=0; };
    InlineMetrics inline_metrics(HWND item)const{
        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);TEXTMETRICW fallback{};
        GetTextMetricsW(dc,&fallback);SelectObject(dc,old);ReleaseDC(host,dc);
        InlineMetrics result{0,static_cast<int>(fallback.tmAscent),text_height()};
        Microsoft::WRL::ComPtr<IRichEditOle> ole;
        SendMessageW(item,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> range;
        if(!ole||FAILED(ole.As(&document))||document->Range(0,0,&range)!=S_OK||!range)return result;
        LONG x=0,top=0,baseline=0,bottom=0;constexpr LONG flags=tomStart|tomClientCoord|tomAllowOffClient;
        if(range->GetPoint(flags|TA_TOP|TA_LEFT,&x,&top)==S_OK&&
            range->GetPoint(flags|TA_BASELINE|TA_LEFT,&x,&baseline)==S_OK&&
            range->GetPoint(flags|TA_BOTTOM|TA_LEFT,&x,&bottom)==S_OK&&bottom>top)
            result={static_cast<int>(top),static_cast<int>(baseline),static_cast<int>(bottom)};
        return result;
    }
    void inline_rect(HWND item,int width,int height,bool hole){
        // EM_SETRECT uses unzoomed insets. Left/right and top/bottom must be
        // converted separately; scaling the entire right edge clips text.
        const auto natural=inline_metrics(item);const int glyph=std::max(1,natural.bottom-natural.top);
        const int horizontal_pad=hole?px(6):0,vertical_pad=std::max(0,(height-glyph)/2);
        RECT rect{MulDiv(horizontal_pad,100,text_scale),MulDiv(vertical_pad,100,text_scale),
            width-MulDiv(horizontal_pad,100,text_scale),height-MulDiv(vertical_pad,100,text_scale)};
        rect.right=std::max(rect.left+1,rect.right);rect.bottom=std::max(rect.top+1,rect.bottom);
        SendMessageW(item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&rect));
    }
    void layout_inline(int width){
        if(laying_out)return;laying_out=true;
        struct Guard{bool& flag;~Guard(){flag=false;}} guard{laying_out};
        const auto count=1+static_cast<std::size_t>(std::ranges::count(block.code_template,'\n'));
        gutter_width=std::max(px(14),measure(std::string(std::to_string(count).size(),'0'))+px(3));
        const int start=gutter_width+px(6),right=std::max(start+px(12),width-px(6));
        const int available=right-start,line=std::max(MulDiv(px(22),text_scale,100),text_height()+px(2));
        struct Placement{HWND item;int x,y,w,h,baseline;bool hole;};std::vector<Placement> positions;
        int x=start,y=px(8),ascent=0,descent=0,logical_top=y,logical_baseline=-1;
        std::size_t first=0;content_width=width;numbered_lines.clear();inline_baselines.clear();
        const auto finish_row=[&]{
            const int h=std::max(line,ascent+descent),extra=(h-ascent-descent)/2;
            for(auto n=first;n<positions.size();++n)positions[n].y=y+extra+ascent-positions[n].baseline;
            if(logical_baseline<0){
                if(first<positions.size())logical_baseline=y+extra+ascent;
                else {const auto dc=GetDC(host);const auto old=SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);
                    SelectObject(dc,old);ReleaseDC(host,dc);logical_baseline=y+(h-tm.tmHeight)/2+tm.tmAscent;}
            }
            content_width=std::max(content_width,x+px(6));y+=h;x=start;ascent=descent=0;first=positions.size();
        };
        const auto finish_line=[&]{finish_row();numbered_lines.push_back({0,logical_top,gutter_width,y});
            inline_baselines.push_back(logical_baseline);logical_top=y;logical_baseline=-1;};
        for(const auto& piece:pieces){
            if(piece.newline){finish_line();continue;}
            std::string value=piece.source;
            if(piece.hole){const auto slot=static_cast<std::size_t>(std::ranges::find(holes,piece.item)-holes.begin());value=editors[slot]->document().text;}
            int w=piece.hole?std::max(MulDiv(px(42),text_scale,100),measure(value)+px(12)):static_cast<int>(token_size(piece.item,value).cx);
            if(wrap&&x>start&&x+w>right)finish_row();
            if(wrap)w=std::min(w,available);
            int h=piece.hole?line:text_height();
            SendMessageW(piece.item,EM_SETTARGETDEVICE,0,!piece.hole&&wrap?0:1);
            SetWindowPos(piece.item,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
            inline_rect(piece.item,w,h,piece.hole);
            if(!piece.hole&&wrap){SendMessageW(piece.item,EM_REQUESTRESIZE,0,0);
                if(const auto found=requested_heights.find(piece.item);found!=requested_heights.end())h=std::max(h,found->second);
                SetWindowPos(piece.item,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
                // Wrapped source keeps its first baseline, not a centered paragraph.
                RECT rect{0,0,w,h};SendMessageW(piece.item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&rect));}
            const auto metrics=inline_metrics(piece.item);h=std::max(h,metrics.bottom);
            positions.push_back({piece.item,x,y,w,h,metrics.baseline,piece.hole});
            ascent=std::max(ascent,metrics.baseline);descent=std::max(descent,h-metrics.baseline);x+=w;
        }
        finish_line();
        if(wrap){content_width=width;horizontal=0;}else horizontal=std::clamp(horizontal,0,std::max(0,content_width-width));
        for(const auto& p:positions){const int left=p.x-horizontal;
            SetWindowPos(p.item,nullptr,left,p.y,p.w,p.h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
            const auto clip=CreateRectRgn(std::clamp(gutter_width-left,0,p.w),0,p.w,p.h);
            if(clip&&p.hole){const auto rounded=CreateRoundRectRgn(0,0,p.w+1,p.h+1,px(6),px(6));
                if(rounded){CombineRgn(clip,clip,rounded,RGN_AND);DeleteObject(rounded);}}
            if(clip&&!SetWindowRgn(p.item,clip,FALSE))DeleteObject(clip);
            InvalidateRect(p.item,nullptr,FALSE);
        }
        const bool overflow=content_width>width;
        height=y+px(8)+(overflow?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_width-1),static_cast<UINT>(std::max(1,width)),horizontal,0};
        SetScrollInfo(host,SB_HORZ,&info,FALSE);ShowScrollBar(host,SB_HORZ,overflow);reader_theme::refresh_scrollbars(host);
        InvalidateRect(host,nullptr,FALSE);
    }
'''
insert_before(P+'cpp_fill_view.cpp','    void layout(int width){',inline_layout)
replace(P+'cpp_fill_view.cpp','    void layout(int width){\n        if(laying_out)return;',
        '    void layout(int width){\n        if(!drag()){layout_inline(width);return;}\n        if(laying_out)return;')
replace(P+'cpp_fill_view.cpp', '''        for(std::size_t i=0;i<numbered_lines.size();++i){auto rect=numbered_lines[i];rect.left=0;rect.right-=px(3);
            const auto label=std::to_wstring(i+1);DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&rect,
                DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}''', '''        for(std::size_t i=0;i<numbered_lines.size();++i){auto rect=numbered_lines[i];rect.left=0;rect.right-=px(3);
            const auto label=std::to_wstring(i+1);
            if(!drag()&&i<inline_baselines.size()){
                SetTextAlign(dc,TA_LEFT|TA_BASELINE);TextOutW(dc,0,inline_baselines[i],label.c_str(),static_cast<int>(label.size()));
            }else DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}''')
replace(P+'cpp_fill_view.cpp','frame(item,dc,filled?4:3,filled?colors.accent:(colors.high_contrast?colors.outline:RGB(106,106,106)),reader_theme::background(item));',
'frame(item,dc,filled?4:3,completed&&!colors.high_contrast?RGB(105,220,145):filled?colors.accent:(colors.high_contrast?colors.outline:RGB(106,106,106)),reader_theme::background(item));')
# Text-length queries remain inside EditHint's already-guarded path.
old = '''        if(self.drag()){
            if(message==WM_ENABLE&&id>=100&&id<1000&&!wparam&&!reader_theme::palette(item).high_contrast){'''
new = '''        if(!self.drag()&&id>=100&&id<1000&&(message==WM_PAINT||message==WM_PRINTCLIENT)){
            const auto result=DefSubclassProc(item,message,wparam,lparam);
            const auto dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wparam):GetDC(item);
            self.paint_slot(item,dc);if(message==WM_PAINT&&dc)ReleaseDC(item,dc);return result;
        }
        if(message==WM_ENABLE&&id>=100&&id<1000&&!wparam&&!reader_theme::palette(item).high_contrast){
            SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));InvalidateRect(item,nullptr,FALSE);return 0;
        }
        if(self.drag()){
            if(message==WM_ENABLE&&id>=100&&id<1000&&!wparam&&!reader_theme::palette(item).high_contrast){'''
replace(P+'cpp_fill_view.cpp',old,new)
replace(P+'cpp_fill_view.cpp','style_cpp(self->holes[i]);self->updating=false;if(self->changed)',
        'style_cpp(self->holes[i]);self->updating=false;self->relayout();if(self->changed)')
replace(P+'cpp_fill_view.hpp','    void set_enabled(bool);','    void set_enabled(bool);\n    void set_completed(bool);')
insert_before(P+'cpp_fill_view.cpp','void CppFillView::set_enabled(bool value)',
'''void CppFillView::set_completed(bool value){
    impl_->completed=value;for(const auto item:impl_->holes)reader_theme::completed_control(item,value);
    InvalidateRect(impl_->host,nullptr,FALSE);
}
''')
replace(P+'cpp_fill_view.cpp','void CppFillView::reset(){impl_->release();', 'void CppFillView::reset(){set_completed(false);impl_->release();')
# Solved ordering cards share the same semantic style without changing order.
replace(P+'order_code_view.hpp','    void set_enabled(bool);','    void set_enabled(bool);\n    void set_completed(bool);')
insert_before(P+'order_code_view.cpp','void OrderCodeView::set_enabled(bool value)',
'''void OrderCodeView::set_completed(bool value){
    for(const auto row:impl_->rows)reader_theme::completed_control(row,value);
    InvalidateRect(impl_->host,nullptr,FALSE);
}
''')
replace(P+'order_code_view.cpp','void OrderCodeView::reset(){impl_->ordered=', 'void OrderCodeView::reset(){set_completed(false);impl_->ordered=')
# Completion is driven by the actual verdict, never by "disabled" alone.
replace(P+'lesson_view.cpp','        if(vm.fill)vm.fill->set_enabled(editable);',
'''        reader_theme::completed_control(check,vm.response.correct);
        if(vm.answer)reader_theme::completed_control(vm.answer,vm.response.correct);
        if(vm.fill){vm.fill->set_enabled(editable);vm.fill->set_completed(vm.response.correct);}''')
replace(P+'lesson_view.cpp','        if(vm.order_view)vm.order_view->set_enabled(editable);',
        '        if(vm.order_view){vm.order_view->set_enabled(editable);vm.order_view->set_completed(vm.response.correct);}')
replace(P+'lesson_view.cpp','        controls.at(index).feedback_source = std::string(text); controls.at(index).feedback_correct = correct;',
'''        controls.at(index).feedback_source = std::string(text); controls.at(index).feedback_correct = correct;
        if(controls.at(index).compile&&block(index).output_check)
            reader_theme::completed_control(controls.at(index).compile,correct&&!text.empty());''')
replace(P+'lesson_view.cpp','        rows.push_back({{check}, {0}, 48, false}); create_feedback(index);',
'''        rows.push_back({{check}, {0}, 48, false});
        rows.back().padding_before=12;rows.back().horizontal_inset=4;
        reader_theme::primary_button(check);create_feedback(index);''')
replace(P+'lesson_view.cpp','            rows.push_back({{check},{0},48,false});rows.back().horizontal_inset=6;rows.back().padding_after=0;create_feedback(index);return;',
'''            rows.push_back({{check},{0},48,false});rows.back().horizontal_inset=6;rows.back().padding_before=12;
            rows.back().padding_after=0;reader_theme::primary_button(check);create_feedback(index);return;''')
replace(P+'lesson_view.cpp','            if(!laboratory()){rows.back().padding_before=8;rows.back().padding_after=0;}',
'''            reader_theme::primary_button(vm.compile);
            if(!laboratory()){rows.back().padding_before=12;rows.back().padding_after=4;rows.back().horizontal_inset=4;}''')
# Write only after all reviewed exact anchors matched.
for name,text in files.items():
    (root/name).write_text(text,encoding='utf-8',newline='\n')
print('Patched',len(files),'source files')
print('\n'.join(files))
cmake=root/'CMakeLists.txt'
c=cmake.read_text(encoding='utf-8')
if 'lgaed_inline_presentation_tests' in c: raise RuntimeError('Duplicate test target')
c += """
if(BUILD_TESTING)
    add_executable(lgaed_inline_presentation_tests native/presentation/windows/inline_presentation_tests.cpp)
    target_link_libraries(lgaed_inline_presentation_tests PRIVATE lgaed_presentation_windows)
    set_target_properties(lgaed_inline_presentation_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    add_test(NAME native.inline_presentation COMMAND lgaed_inline_presentation_tests)
endif()
"""
cmake.write_text(c,encoding='utf-8',newline='\n')

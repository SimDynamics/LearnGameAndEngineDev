#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/editor_render_transaction.hpp"
#include "presentation/windows/programming_technical_ranges.hpp"
#include "presentation/windows/curricular_text_roles.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr UINT_PTR hint_subclass_id=0x4c474854;
constexpr UINT_PTR semantic_background_subclass_id=0x4c475342;
constexpr UINT_PTR reference_ranges_subclass_id=0x4c475246;
struct ReferenceRanges {
    std::wstring text;
    std::vector<std::pair<std::size_t,std::size_t>> ranges;
    static LRESULT CALLBACK proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
        auto* self=reinterpret_cast<ReferenceRanges*>(raw);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(edit,proc,reference_ranges_subclass_id);delete self;return DefSubclassProc(edit,message,wp,lp);}
        // Read-only controls reject typing/paste/Undo without changing their
        // document. Their reference ownership must survive those attempts.
        // The getter also checks the complete current text for actual edits.
        if(message==WM_SETTEXT||message==EM_STREAMIN||message==EM_SETTEXTEX||message==EM_REPLACESEL){self->text.clear();self->ranges.clear();}
        return DefSubclassProc(edit,message,wp,lp);
    }
};
void publish_references(HWND edit,std::wstring_view text,std::vector<std::pair<std::size_t,std::size_t>> ranges){
    DWORD_PTR attached=0;
    ReferenceRanges* state=nullptr;
    if(GetWindowSubclass(edit,ReferenceRanges::proc,reference_ranges_subclass_id,&attached))state=reinterpret_cast<ReferenceRanges*>(attached);
    else if(!ranges.empty()){
        auto created=std::make_unique<ReferenceRanges>();
        if(!SetWindowSubclass(edit,ReferenceRanges::proc,reference_ranges_subclass_id,reinterpret_cast<DWORD_PTR>(created.get())))
            throw std::runtime_error("Cannot attach curricular reference metadata");
        state=created.release();
    }
    if(state){state->text=text;state->ranges=std::move(ranges);}
}
struct SemanticBackground {
    COLORREF color=0;
    bool painting=false;
    static DWORD pixel(COLORREF value) {
        return (static_cast<DWORD>(GetRValue(value))<<16)|(static_cast<DWORD>(GetGValue(value))<<8)|GetBValue(value);
    }
    void correct(HWND edit,DWORD* pixels,const RECT& dirty,LONG width) const {
        Microsoft::WRL::ComPtr<IRichEditOle> rich;
        SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;
        if(!rich||FAILED(rich.As(&document)))return;
        RECT viewport{};GetClientRect(edit,&viewport);
        Microsoft::WRL::ComPtr<ITextDocument2> extended;
        if(SUCCEEDED(rich.As(&extended))){RECT measured{};
            if(extended->GetClientRect(tomClientCoord|tomIncludeInset,&measured.left,&measured.top,&measured.right,&measured.bottom)==S_OK)
                viewport=measured;
        }
        RECT clip{};if(!IntersectRect(&clip,&dirty,&viewport))return;
        GETTEXTLENGTHEX request{GTL_PRECISE|GTL_NUMCHARS,1200};
        const auto length=static_cast<LONG>(SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&request),0));
        if(length<=0)return;
        constexpr long coordinates=tomStart|tomClientCoord|tomAllowOffClient;
        const auto point=[&](LONG at,LONG align,POINTL& value){
            Microsoft::WRL::ComPtr<ITextRange> range;
            return document->Range(at,at,&range)==S_OK&&range&&range->GetPoint(coordinates|align,&value.x,&value.y)==S_OK;
        };
        int numerator=0,denominator=0;
        SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
        const auto dpi=GetDpiForWindow(edit);
        // One logical pixel of breathing room, scaled like the text. The
        // font's real descent already includes p/q/g; this is not a glyph-
        // dependent pad or an alteration of RichEdit's paragraph geometry.
        const auto margin=std::max<LONG>(1,MulDiv(static_cast<int>(dpi),numerator,96*denominator));
        CHARRANGE selection{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        const auto metrics_dc=GetDC(edit);
        struct ReleaseMetrics {HWND edit;HDC dc;~ReleaseMetrics(){if(dc)ReleaseDC(edit,dc);}} release{edit,metrics_dc};
        const auto paint_line=[&](LONG line){
            const auto line_start=static_cast<LONG>(SendMessageW(edit,EM_LINEINDEX,line,0));
            auto line_end=static_cast<LONG>(SendMessageW(edit,EM_LINEINDEX,line+1,0));
            if(line_start<0||line_start>=length)return;
            if(line_end<0)line_end=length;
            Microsoft::WRL::ComPtr<ITextRange> row;
            Microsoft::WRL::ComPtr<ITextPara> paragraph;
            if(document->Range(line_start,line_end,&row)!=S_OK||!row||FAILED(row->GetPara(&paragraph))||!paragraph)return;
            long rule=0;float factor=0,before=0,after=0;
            if(FAILED(paragraph->GetLineSpacingRule(&rule))||rule!=tomLineSpaceMultiple||
                FAILED(paragraph->GetLineSpacing(&factor))||!std::isfinite(factor)||factor<=0||
                FAILED(paragraph->GetSpaceBefore(&before))||FAILED(paragraph->GetSpaceAfter(&after))||before!=0||after!=0)return;
            // A built-up OfficeMath/OLE row has its own vertical layout. Keep
            // its native backdrop; a prose font's metrics do not describe it.
            Microsoft::WRL::ComPtr<ITextFont> font;
            Microsoft::WRL::ComPtr<ITextFont2> math_font;
            long math=tomFalse;
            if(FAILED(row->GetFont(&font))||!font||FAILED(font.As(&math_font))||
                FAILED(math_font->GetMathZone(&math))||math!=tomFalse)return;
            for(LONG object=0,count=rich->GetObjectCount();object<count;++object){REOBJECT entry{};entry.cbStruct=sizeof(entry);
                if(FAILED(rich->GetObject(object,&entry,REO_GETOBJ_NO_INTERFACES))||
                    (entry.cp>=line_start&&entry.cp<line_end))return;
            }
            POINTL first_top{},first_bottom{},baseline{};
            if(!point(line_start,TA_TOP|TA_LEFT,first_top)||!point(line_start,TA_BOTTOM|TA_RIGHT,first_bottom)||
                !point(line_start,TA_BASELINE|TA_LEFT,baseline))return;
            LONG top=first_top.y,natural_bottom=first_top.y+
                static_cast<LONG>(std::ceil((first_bottom.y-first_top.y)/static_cast<double>(factor)));
            LONG ascent=0,descent=0;
            std::vector<std::pair<LONG,LONG>> backed;
            for(auto at=line_start;at<line_end;){
                Microsoft::WRL::ComPtr<ITextRange> run;Microsoft::WRL::ComPtr<ITextFont> run_font;
                if(document->Range(at,at+1,&run)!=S_OK||!run||FAILED(run->GetFont(&run_font))||!run_font)return;
                long background=tomAutoColor,end=at+1,delta=0;
                if(FAILED(run_font->GetBackColor(&background)))return;
                if(SUCCEEDED(run->EndOf(tomCharFormat,tomExtend,&delta)))run->GetEnd(&end);
                end=std::clamp<LONG>(end,at+1,line_end);
                if(background==static_cast<long>(color))backed.emplace_back(at,end);
                POINTL run_top{},run_bottom{};
                if(point(at,TA_TOP|TA_LEFT,run_top)&&point(at,TA_BOTTOM|TA_RIGHT,run_bottom)){
                    top=std::min(top,run_top.y);
                    natural_bottom=std::max(natural_bottom,run_top.y+
                        static_cast<LONG>(std::ceil((run_bottom.y-run_top.y)/static_cast<double>(factor))));
                }
                float points=0;BSTR face=nullptr;long bold=tomFalse,italic=tomFalse;
                if(metrics_dc&&SUCCEEDED(run_font->GetSize(&points))&&std::isfinite(points)&&points>0&&
                    SUCCEEDED(run_font->GetName(&face))){
                    run_font->GetBold(&bold);run_font->GetItalic(&italic);
                    const auto height=std::max<LONG>(1,static_cast<LONG>(std::lround(points*dpi*numerator/(72.0*denominator))));
                    const auto measured=CreateFontW(-height,0,0,0,bold==tomTrue?FW_BOLD:FW_NORMAL,italic==tomTrue,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,face);
                    if(measured){const auto previous=SelectObject(metrics_dc,measured);TEXTMETRICW metrics{};
                        if(GetTextMetricsW(metrics_dc,&metrics)){ascent=std::max(ascent,metrics.tmAscent);descent=std::max(descent,metrics.tmDescent);}
                        SelectObject(metrics_dc,previous);DeleteObject(measured);
                    }
                }
                SysFreeString(face);at=end;
            }
            if(backed.empty())return;
            // Every sample on this visual row uses the same ascent/descent,
            // including fonts introduced by emphasis or fallback. Never infer
            // its height from the final token's font or reading-state ink.
            const auto fill_top=std::min(top,baseline.y-ascent)-margin;
            const auto fill_bottom=std::max(natural_bottom,baseline.y+descent)+margin;
            const auto backdrop=pixel(color),surface=pixel(reader_theme::background(edit));
            for(const auto& [first,last]:backed)for(auto at=first;at<last;++at){
                // Selection remains entirely native, including its backdrop.
                if(at>=selection.cpMin&&at<selection.cpMax)continue;
                POINTL left{},right{};
                if(!point(at,TA_TOP|TA_LEFT,left)||!point(at,TA_BOTTOM|TA_RIGHT,right)||right.y<=left.y)continue;
                const auto start=std::max(clip.left,std::min(left.x,right.x)),finish=std::min(clip.right,std::max(left.x,right.x));
                const auto first_y=std::max(clip.top,std::min(left.y,fill_top)),last_y=std::min(clip.bottom,std::max(right.y,fill_bottom));
                for(auto y=first_y;y<last_y;++y)for(auto x=start;x<finish;++x){auto& value=pixels[static_cast<std::size_t>(y-dirty.top)*width+x-dirty.left];
                    // Never paint a glyph/antialias pixel, native caret or
                    // selection. Only the two exact flat backdrop colors may
                    // change, so text and its natural descenders remain native.
                    const auto rgb=value&0xffffffu;
                    if(y>=fill_top&&y<fill_bottom){if(rgb==surface)value=(value&0xff000000u)|backdrop;}
                    else if(rgb==backdrop)value=(value&0xff000000u)|surface;
                }
            }
        };
        // Include a neighboring row for the small overhang into line spacing
        // and for partial-row scrolling. Dirty clipping remains caller-owned.
        POINTL hit{viewport.left,clip.top};
        const auto first_hit=std::clamp<LONG>(static_cast<LONG>(SendMessageW(edit,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&hit))),0,length-1);
        const auto first_line=SendMessageW(edit,EM_EXLINEFROMCHAR,0,first_hit);
        hit={viewport.right,clip.bottom-1};
        const auto last_hit=std::clamp<LONG>(static_cast<LONG>(SendMessageW(edit,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&hit))),0,length-1);
        const auto last_line=SendMessageW(edit,EM_EXLINEFROMCHAR,0,last_hit);
        for(auto line=std::max<LRESULT>(0,first_line-1);line<=last_line+1;++line)paint_line(static_cast<LONG>(line));
    }
    static LRESULT CALLBACK proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<SemanticBackground*>(raw);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(edit,proc,semantic_background_subclass_id);delete self;return DefSubclassProc(edit,message,wp,lp);}
        // In particular, never query TOM/text from its own synchronous read
        // messages, and never format/freeze/invalidate from a paint callback.
        if((message!=WM_PAINT&&message!=WM_PRINTCLIENT)||self->painting||reader_theme::palette().high_contrast)
            return DefSubclassProc(edit,message,wp,lp);
        if(message==WM_PRINTCLIENT&&lp!=(PRF_CLIENT|PRF_ERASEBKGND))return DefSubclassProc(edit,message,wp,lp);
        RECT client{},dirty{};GetClientRect(edit,&client);
        if(message==WM_PAINT){if(!GetUpdateRect(edit,&dirty,FALSE))return DefSubclassProc(edit,message,wp,lp);}
        else{if(!wp||GetClipBox(reinterpret_cast<HDC>(wp),&dirty)==ERROR)return DefSubclassProc(edit,message,wp,lp);}
        RECT bounds{};if(!IntersectRect(&bounds,&client,&dirty))return DefSubclassProc(edit,message,wp,lp);
        const auto width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        const auto memory=CreateCompatibleDC(nullptr);if(!memory)return DefSubclassProc(edit,message,wp,lp);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;const auto bitmap=CreateDIBSection(memory,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);DeleteDC(memory);return DefSubclassProc(edit,message,wp,lp);}
        const auto previous=SelectObject(memory,bitmap);SetViewportOrgEx(memory,-bounds.left,-bounds.top,nullptr);
        PAINTSTRUCT paint{};const bool restore_caret=message==WM_PAINT&&GetFocus()==edit&&HideCaret(edit)!=FALSE;
        const auto target=message==WM_PAINT?BeginPaint(edit,&paint):reinterpret_cast<HDC>(wp);
        const auto fill=CreateSolidBrush(reader_theme::background(edit));if(fill){FillRect(memory,&client,fill);DeleteObject(fill);}
        self->painting=true;
        const auto result=DefSubclassProc(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_ERASEBKGND);
        DWORD_PTR attached=0;
        if(GetWindowSubclass(edit,proc,semantic_background_subclass_id,&attached)&&attached==raw){
            GdiFlush();self->correct(edit,static_cast<DWORD*>(pixels),bounds,width);self->painting=false;
            if(target)BitBlt(target,bounds.left,bounds.top,width,height,memory,bounds.left,bounds.top,SRCCOPY);
        }
        if(message==WM_PAINT){EndPaint(edit,&paint);if(restore_caret)ShowCaret(edit);}
        SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);return result;
    }
};
void semantic_background(HWND edit,COLORREF color,bool enabled){
    DWORD_PTR attached=0;
    if(GetWindowSubclass(edit,SemanticBackground::proc,semantic_background_subclass_id,&attached)){
        auto* state=reinterpret_cast<SemanticBackground*>(attached);
        if(enabled)state->color=color;
        else{RemoveWindowSubclass(edit,SemanticBackground::proc,semantic_background_subclass_id);delete state;}
    }else if(enabled){auto state=std::make_unique<SemanticBackground>();state->color=color;
        if(!SetWindowSubclass(edit,SemanticBackground::proc,semantic_background_subclass_id,reinterpret_cast<DWORD_PTR>(state.get())))
            throw std::runtime_error("Cannot attach the semantic background renderer");
        state.release();
    }
}
struct EditHint {
    std::wstring text;
    COLORREF color=RGB(140,145,155);
    bool normalizing_selection=false,centered=false;
    bool showing(HWND edit) const {
        return !text.empty()&&GetWindowTextLengthW(edit)==0&&!(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY);
    }
    void collapse_empty_selection(HWND edit) {
        if(normalizing_selection||!showing(edit))return;
        CHARRANGE selected{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
        if(selected.cpMin==0&&selected.cpMax==0)return;
        // An empty RichEdit still has a selectable, synthetic final CR.
        // Keep that mark from appearing as selected text under the overlay.
        normalizing_selection=true;CHARRANGE insertion{};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&insertion));
        normalizing_selection=false;
    }
    void paint(HWND edit,HDC dc) const {
        if(!showing(edit)||!dc)return;
        RECT format{},client{};SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));GetClientRect(edit,&client);
        CHARFORMAT2W style{};style.cbSize=sizeof(style);
        SendMessageW(edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&style));
        int numerator=0,denominator=0;SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        if(!numerator||!denominator){numerator=1;denominator=1;}
        // EM_GETRECT retains all four unzoomed insets. TextServices exposes
        // the actual viewport; scale each inset separately as the fallback.
        RECT viewport{MulDiv(format.left,numerator,denominator),MulDiv(format.top,numerator,denominator),
            client.right-MulDiv(client.right-format.right,numerator,denominator),
            client.bottom-MulDiv(client.bottom-format.bottom,numerator,denominator)};
        Microsoft::WRL::ComPtr<IRichEditOle> rich;
        SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument2> extended;
        if(rich&&SUCCEEDED(rich.As(&extended))){
            RECT measured{};
            if(extended->GetClientRect(tomClientCoord|tomIncludeInset,&measured.left,&measured.top,&measured.right,&measured.bottom)==S_OK)
                viewport=measured;
        }
        RECT clip{};if(!IntersectRect(&clip,&client,&viewport))return;
        auto bounds=viewport;
        const auto height=MulDiv(MulDiv(style.yHeight,static_cast<int>(GetDpiForWindow(edit)),1440),numerator,denominator);
        const auto font=CreateFontW(-std::max(1,height),0,0,0,(style.dwEffects&CFE_BOLD)?FW_BOLD:FW_NORMAL,
            (style.dwEffects&CFE_ITALIC)!=0,FALSE,FALSE,style.bCharSet,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,style.bPitchAndFamily,style.szFaceName[0]?style.szFaceName:L"Roboto");
        if(!font)return;
        const auto saved=SaveDC(dc);if(!saved){DeleteObject(font);return;}
        SelectObject(dc,font);
        // EM_GETRECT keeps the unzoomed formatting origin. Ask TextServices
        // for the displayed insertion point so hint, text and caret share
        // the same position after text zoom, DPI changes and scrolling.
        POINTL origin{viewport.left,viewport.top};
        SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&origin),0);
        bounds.left=origin.x;bounds.top=origin.y;
        Microsoft::WRL::ComPtr<ITextDocument> document;
        Microsoft::WRL::ComPtr<ITextRange> range;
        TEXTMETRICW metrics{};LONG baseline_x=0,baseline_y=0;
        if(rich&&SUCCEEDED(rich.As(&document))&&document->Range(0,0,&range)==S_OK&&range&&
            range->GetPoint(tomStart|tomClientCoord|tomAllowOffClient|TA_BASELINE|TA_LEFT,&baseline_x,&baseline_y)==S_OK&&
            GetTextMetricsW(dc,&metrics)){
            bounds.left=baseline_x;bounds.top=baseline_y-metrics.tmAscent;
        }
        // Center the hint in the same native formatting box as the empty
        // insertion point. Its baseline still comes from TextServices.
        if(centered){bounds.left=viewport.left;bounds.right=viewport.right;}
        IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,reader_theme::palette(edit).high_contrast?reader_theme::foreground(edit):color);
        const auto wrapping=(GetWindowLongPtrW(edit,GWL_STYLE)&ES_AUTOHSCROLL)?DT_SINGLELINE:DT_WORDBREAK;
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,(centered?DT_CENTER:DT_LEFT)|DT_TOP|wrapping|DT_NOPREFIX);
        RestoreDC(dc,saved);DeleteObject(font);
    }
    static LRESULT CALLBACK proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<EditHint*>(raw);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(edit,proc,hint_subclass_id);delete self;return DefSubclassProc(edit,message,wp,lp);}
        // showing() asks this HWND for WM_GETTEXTLENGTH. Only consult it for
        // selection writes, or that synchronous query reenters this branch.
        if((message==EM_SETSEL||(message==EM_EXSETSEL&&lp))&&
            !self->normalizing_selection&&self->showing(edit)){
            if(message==EM_SETSEL)return DefSubclassProc(edit,message,0,0);
            if(message==EM_EXSETSEL&&lp){CHARRANGE insertion{};return DefSubclassProc(edit,message,0,reinterpret_cast<LPARAM>(&insertion));}
        }
        const bool selects=message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_LBUTTONUP||
            message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK||message==WM_RBUTTONUP||
            (message==WM_MOUSEMOVE&&(wp&(MK_LBUTTON|MK_RBUTTON)))||message==WM_KEYDOWN||message==WM_CHAR||message==WM_SETFOCUS||
            message==WM_COPY||message==WM_CUT||message==WM_CLEAR||message==WM_PASTE||message==EM_PASTESPECIAL||message==WM_SETTEXT||
            message==EM_STREAMIN||message==EM_SETTEXTEX||message==EM_REPLACESEL||message==EM_UNDO||message==WM_UNDO||
            message==EM_REDO||message==EM_SETREADONLY;
        // Collapse before native mouse-down can begin dragging an existing
        // CR selection, and after native selection changes. Forward the full
        // gesture sequence so lesson panning/capture and real text still work.
        if(selects)self->collapse_empty_selection(edit);
        const auto result=DefSubclassProc(edit,message,wp,lp);
        // Enter can submit a response and synchronously rebuild the lesson.
        // Do not touch this overlay after a lower handler destroyed its HWND.
        DWORD_PTR attached=0;
        if(!GetWindowSubclass(edit,proc,hint_subclass_id,&attached)||attached!=raw)return result;
        if(selects)self->collapse_empty_selection(edit);
        if(message==WM_PAINT&&GetWindowTextLengthW(edit)==0){
            // Native WM_PAINT has finished (including its caret handling).
            // Balance this extra overlay draw without replacing that caret.
            const bool restore_caret=GetFocus()==edit&&HideCaret(edit)!=FALSE;
            const auto dc=GetDC(edit);self->paint(edit,dc);if(dc)ReleaseDC(edit,dc);
            if(restore_caret)ShowCaret(edit);
        }
        else if(message==WM_PRINTCLIENT)self->paint(edit,reinterpret_cast<HDC>(wp));
        else if(message==WM_CHAR||message==WM_PASTE||message==EM_PASTESPECIAL||message==WM_CUT||message==WM_CLEAR||message==WM_SETTEXT||
            message==EM_STREAMIN||message==EM_SETTEXTEX||message==EM_REPLACESEL||message==EM_UNDO||message==WM_UNDO||
            message==EM_REDO||message==EM_SETREADONLY||message==EM_SETCHARFORMAT||message==EM_SETZOOM||
            message==WM_SETFONT||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_SIZE)
            InvalidateRect(edit,nullptr,FALSE);
        return result;
    }
};
constexpr UINT_PTR plain_input_subclass_id=0x4c475054;
class PlainTextInput final : public IRichEditOleCallback {
    LONG references=1;
    HWND window;
    bool writable() const {
        return IsWindow(window)&&IsWindowEnabled(window)&&!(GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY);
    }
public:
    explicit PlainTextInput(HWND edit):window(edit){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        if(iid!=IID_IUnknown&&iid!=IID_IRichEditOleCallback)return E_NOINTERFACE;
        *result=static_cast<IRichEditOleCallback*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return static_cast<ULONG>(InterlockedIncrement(&references));}
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining=static_cast<ULONG>(InterlockedDecrement(&references));
        if(!remaining)delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetNewStorage(LPSTORAGE* storage) override {
        if(storage)*storage=nullptr;return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetInPlaceContext(LPOLEINPLACEFRAME*,LPOLEINPLACEUIWINDOW*,LPOLEINPLACEFRAMEINFO) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShowContainerUI(BOOL) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInsertObject(LPCLSID,LPSTORAGE,LONG) override {return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE DeleteObject(LPOLEOBJECT) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryAcceptData(LPDATAOBJECT data,CLIPFORMAT* format,DWORD,BOOL,HGLOBAL) override {
        if(!data||!format)return E_INVALIDARG;
        if(!writable())return E_ACCESSDENIED;
        // RichEdit consults this for keyboard/menu paste, Paste Special and
        // OLE drops. Force the text representation even when RTF, HTML, or
        // an embedded image is the source's preferred format.
        for(const auto candidate:{CF_UNICODETEXT,CF_TEXT}){
            FORMATETC requested{static_cast<CLIPFORMAT>(candidate),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
            if(data->QueryGetData(&requested)==S_OK){*format=requested.cfFormat;return S_OK;}
        }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetClipboardData(CHARRANGE*,DWORD,LPDATAOBJECT*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDragDropEffect(BOOL,DWORD,LPDWORD effect) override {
        // Preserve RichEdit's move/copy decision for real text. Only the
        // accepted data format changes; this callback does not start a drag.
        return effect?S_OK:E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE GetContextMenu(WORD,LPOLEOBJECT,CHARRANGE*,HMENU* menu) override {
        if(menu)*menu=nullptr;return E_NOTIMPL;
    }
    static LRESULT CALLBACK proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
        // Paste Special fixes its requested format before QueryAcceptData runs;
        // changing that explicit RTF format there can leave the paste empty.
        // Negotiate from zero through the same native paste path, whose callback
        // selects Unicode/ANSI text and retains native selection/Undo handling.
        if(message==EM_PASTESPECIAL)return DefSubclassProc(edit,WM_PASTE,0,0);
        if(message==WM_NCDESTROY){
            auto* self=reinterpret_cast<PlainTextInput*>(raw);self->window=nullptr;
            RemoveWindowSubclass(edit,proc,plain_input_subclass_id);
            self->Release();
        }
        return DefSubclassProc(edit,message,wp,lp);
    }
};
struct FormatTransaction {
    editor_render::Scope rendering;
    HWND edit;
    CHARRANGE selection{};
    POINT scroll{};
    LRESULT events = 0, modified = 0;
    ITextDocument* document = nullptr;
    bool suspended = false, frozen = false, redraw = false;
    explicit FormatTransaction(HWND target) : rendering(target), edit(target) {
        SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        SendMessageW(edit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        events = SendMessageW(edit, EM_GETEVENTMASK, 0, 0);
        modified = SendMessageW(edit, EM_GETMODIFY, 0, 0);
        SendMessageW(edit, EM_SETEVENTMASK, 0, 0);
        IRichEditOle* rich = nullptr;
        SendMessageW(edit, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&rich));
        if (rich) {
            rich->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void**>(&document)); rich->Release();
            if (document) {
                long count = 0;
                frozen = SUCCEEDED(document->Freeze(&count));
                suspended = SUCCEEDED(document->Undo(tomSuspend, &count));
            }
        }
        // WM_SETREDRAW changes WS_VISIBLE. A syntax pass on every edit must
        // not briefly remove a live child from its parent's composition.
        if (!frozen) {
            redraw = (GetWindowLongPtrW(edit,GWL_STYLE) & WS_VISIBLE) != 0;
            if (redraw) SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        }
    }
    ~FormatTransaction() {
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        SendMessageW(edit, EM_SETMODIFY, static_cast<WPARAM>(modified), 0);
        if (document) {
            long count = 0;
            if (suspended) document->Undo(tomResume, &count);
            if (frozen) document->Unfreeze(&count);
            document->Release();
        }
        // Native unfreezing can finish layout and round the viewport. Restore
        // its exact saved position after that layout, as the reader shade does.
        SendMessageW(edit, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(edit, EM_SETEVENTMASK, 0, events);
        if (redraw) SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(edit, nullptr, FALSE);
    }
};
void select(HWND edit, std::size_t start, std::size_t end) {
    if (end > static_cast<std::size_t>(std::numeric_limits<LONG>::max())) throw std::length_error("Rich text is too long");
    CHARRANGE range{static_cast<LONG>(start), static_cast<LONG>(end)};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
}
double luminance(COLORREF color) {
    const auto channel = [](unsigned value) { const auto scaled = value / 255.0; return scaled <= .04045 ? scaled / 12.92 : std::pow((scaled + .055) / 1.055, 2.4); };
    return .2126 * channel(GetRValue(color)) + .7152 * channel(GetGValue(color)) + .0722 * channel(GetBValue(color));
}
COLORREF accessible(HWND edit, COLORREF original) {
    if (reader_theme::applied(edit) && reader_theme::palette().high_contrast)
        return reader_theme::foreground(edit);
    const auto background = luminance(reader_theme::background(edit));
    // The Android console palette is retained on a dark system surface.
    if (background < .25) return original;
    auto color = original;
    for (int i = 0; i < 16; ++i) {
        const auto light = luminance(color);
        if ((background + .05) / (light + .05) >= 4.5) return color;
        color = RGB(GetRValue(color) * 4 / 5, GetGValue(color) * 4 / 5, GetBValue(color) * 4 / 5);
    }
    return reader_theme::foreground(edit);
}
void format(HWND edit, std::size_t start, std::size_t end, COLORREF color, const wchar_t* font = nullptr) {
    if (start >= end) return;
    select(edit, start, end);
    CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_COLOR; style.crTextColor = color;
    if (font) { style.dwMask |= CFM_FACE; wcscpy_s(style.szFaceName, font); }
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&style));
}
bool identifier_start(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_'; }
bool identifier_part(wchar_t c) { return identifier_start(c) || (c >= L'0' && c <= L'9'); }
struct Ink { std::size_t start, end; COLORREF color; };
enum class CppInk { identifier, keyword, string, literal, preprocessor, comment };
COLORREF cpp_color(HWND edit, CppInk category, bool semantic_background=false) {
    const auto readable=[&](COLORREF original){return semantic_background&&!reader_theme::palette().high_contrast?original:accessible(edit,original);};
    switch (category) {
    case CppInk::identifier: return readable(RGB(171,141,227));
    case CppInk::keyword: return readable(RGB(231,155,186));
    case CppInk::string: return readable(RGB(122,195,141));
    case CppInk::literal: return readable(RGB(224,151,109));
    case CppInk::preprocessor: return readable(RGB(92,156,231));
    case CppInk::comment: return readable(RGB(172,172,172));
    }
    return reader_theme::foreground(edit);
}
std::vector<Ink> cpp_ink(HWND edit, std::wstring_view text,bool semantic_background=false) {
    const auto identifier_color = cpp_color(edit,CppInk::identifier,semantic_background);
    const auto keyword_color = cpp_color(edit,CppInk::keyword,semantic_background);
    const auto string_color = cpp_color(edit,CppInk::string,semantic_background);
    const auto literal_color = cpp_color(edit,CppInk::literal,semantic_background);
    const auto preprocessor_color = cpp_color(edit,CppInk::preprocessor,semantic_background);
    const auto comment_color = cpp_color(edit,CppInk::comment,semantic_background);
    static const std::set<std::wstring_view> keywords{L"alignas",L"alignof",L"asm",L"break",L"case",L"catch",L"class",L"concept",L"const",L"consteval",L"constexpr",L"constinit",L"continue",L"co_await",L"co_return",L"co_yield",L"default",L"delete",L"do",L"else",L"enum",L"explicit",L"export",L"extern",L"for",L"friend",L"goto",L"if",L"inline",L"mutable",L"namespace",L"new",L"noexcept",L"operator",L"private",L"protected",L"public",L"register",L"requires",L"return",L"sizeof",L"static",L"static_assert",L"struct",L"switch",L"template",L"throw",L"try",L"typedef",L"typename",L"union",L"using",L"virtual",L"volatile",L"while",L"import",L"module"};
    std::vector<Ink> result;
    auto index = std::size_t{0};
    bool include_header = false;
    while (index < text.size()) {
        const auto start = index;
        if (text.substr(index).starts_with(L"//")) {
            const auto end = text.find_first_of(L"\r\n", index + 2); index = end == text.npos ? text.size() : end;
            result.push_back({start,index,comment_color});
        } else if (text.substr(index).starts_with(L"/*")) {
            const auto end = text.find(L"*/", index + 2); index = end == text.npos ? text.size() : end + 2;
            result.push_back({start,index,comment_color});
        } else if (include_header && text[index] == L'<') {
            const auto end = text.find_first_of(L">\r\n", index + 1); index = end == text.npos ? text.size() : end + (text[end] == L'>');
            result.push_back({start,index,string_color}); include_header = false;
        } else if (text[index] == L'"' || text[index] == L'\'') {
            const auto quote = text[index++];
            while (index < text.size()) { const auto c = text[index++]; if (c == L'\\' && index < text.size()) ++index; else if (c == quote) break; }
            result.push_back({start,index,string_color}); include_header = false;
        } else if (text[index] == L'#') {
            ++index; while (index < text.size() && (text[index] == L' ' || text[index] == L'\t')) ++index;
            const auto directive = index; while (index < text.size() && identifier_part(text[index])) ++index;
            include_header = text.substr(directive, index - directive) == L"include";
            result.push_back({start,index,preprocessor_color});
        } else if (identifier_start(text[index])) {
            ++index; while (index < text.size() && identifier_part(text[index])) ++index;
            const auto word = text.substr(start,index-start);
            result.push_back({start,index,keywords.contains(word) ? keyword_color :
                (word == L"true" || word == L"false" || word == L"nullptr" ? literal_color : identifier_color)});
            include_header = false;
        } else if (text[index] >= L'0' && text[index] <= L'9') {
            ++index;
            while (index < text.size()) {
                const auto c = text[index];
                if (identifier_part(c) || c == L'.' || c == L'\'') ++index;
                else if ((c == L'+' || c == L'-') && std::wstring_view(L"eEpP").find(text[index-1]) != text.npos) ++index;
                else break;
            }
            result.push_back({start,index,literal_color}); include_header = false;
        } else {
            ++index;
            if (std::wstring_view(L"+-*/%<>!&|^~:=").find(text[start]) != text.npos) result.push_back({start,index,keyword_color});
            if (text[start] == L'\n' || text[start] == L'\r') include_header = false;
        }
    }
    return result;
}
void apply_cpp(HWND edit, std::wstring_view source, std::size_t offset = 0, bool monospaced = false,bool semantic_background=false) {
    for (const auto& ink : cpp_ink(edit, source,semantic_background)) format(edit, offset + ink.start, offset + ink.end, ink.color, monospaced ? reader_theme::code_font_face() : nullptr);
}
bool technical_mention_boundary(wchar_t value) {
    // Positive prose boundaries keep accented words and identifiers intact,
    // independent of the process locale used by std::wregex's ASCII \b.
    return std::wstring_view(L" \t\r\n\u00a0\u202f.,;:!?()[]{}\"'`«»“”‘’…/\\+-*=<>|&—–").find(value)!=std::wstring_view::npos;
}
enum class MentionKind { syntax, qualified, language };
struct TechnicalMention {std::size_t start,end;MentionKind kind;};
std::vector<TechnicalMention> technical_mentions(std::wstring_view source){
    std::vector<TechnicalMention> result;
    for(std::size_t index=0;index<source.size();++index){
        const auto rest=source.substr(index);const bool boundary=!index||!technical_prose::identifier(source[index-1]);
        if(boundary&&(rest.starts_with(L"C++")||rest.starts_with(L"c++"))){auto end=index+3;while(end<source.size()&&technical_prose::digit(source[end]))++end;
            result.push_back({index,end,MentionKind::language});index=end-1;continue;}
        if(boundary&&source[index]==L'#'){auto name=index+1;while(name<source.size()&&technical_prose::space(source[name]))++name;
            if(source.substr(name).starts_with(L"include")&&(name+7==source.size()||!technical_prose::identifier(source[name+7]))){
                auto end=name+7;const auto argument=technical_prose::skip_spaces(source,end);
                if(argument<source.size()&&source[argument]==L'<'){const auto close=source.find_first_of(L">\r\n",argument+1);if(close!=source.npos&&source[close]==L'>')end=close+1;}
                else if(argument<source.size()&&source[argument]==L'"'){const auto close=technical_prose::closed_quote_end(source,argument,source.size());if(close!=source.npos&&source.substr(argument,close-argument).find_first_of(L"\r\n")==source.npos)end=close;}
                result.push_back({index,end,MentionKind::syntax});index=end-1;continue;
            }
        }
        if(boundary&&rest.starts_with(L"std")){const auto colon=technical_prose::skip_spaces(source,index+3);if(source.substr(colon).starts_with(L"::")){
            auto end=colon+2;while(end<source.size()&&(identifier_part(source[end])||source[end]==L':'))++end;
            result.push_back({index,end,MentionKind::qualified});index=end-1;continue;}}
        if(boundary&&rest.starts_with(L"main")&&(index+4==source.size()||!technical_prose::identifier(source[index+4]))){const auto end=index+4+(source.substr(index+4).starts_with(L"()")?2:0);
            result.push_back({index,end,MentionKind::syntax});index=end-1;continue;}
        if(boundary&&rest.starts_with(L"return")&&index+6<source.size()&&(source[index+6]==L' '||source[index+6]==L'\t')){auto number=index+6;while(number<source.size()&&(source[number]==L' '||source[number]==L'\t'))++number;
            auto end=number;while(end<source.size()&&technical_prose::digit(source[end]))++end;
            if(end>number){end=technical_prose::skip_spaces(source,end);if(end<source.size()&&source[end]==L';')++end;result.push_back({index,end,MentionKind::syntax});index=end-1;continue;}}
        if(rest.starts_with(L"<<")||rest.starts_with(L">>")){result.push_back({index,index+2,MentionKind::syntax});++index;}
    }return result;
}
std::size_t escape_mention_end(std::wstring_view text,std::size_t start) {
    if(start>=text.size()||text[start]!=L'\\')return start;
    auto end=start;
    while(end<text.size()&&text[end]==L'\\')++end;
    if(end<text.size()&&std::wstring_view(L"ntr0abfv\"'").find(text[end])!=text.npos)++end;
    else if(end-start!=2)return start;
    return end==text.size()||technical_mention_boundary(text[end])?end:start;
}
bool named_string_character(std::wstring_view text,std::size_t start,std::size_t end) {
    if(end-start!=1)return false;
    // A letter is not globally a string. Only its explanation as part of a
    // literal/escape or a visible control-character spelling supplies that role.
    auto begin=text.find_last_of(L".!?\r\n",start);
    begin=begin==text.npos?0:begin+1;
    auto limit=text.find_first_of(L".!?\r\n",end);
    if(limit==text.npos)limit=text.size();
    const auto sentence=text.substr(begin,limit-begin);
    static const std::wregex string_context(LR"((literal|escape|secuencia de control|string literal|s[eé]quence d.[eé]chappement))",std::regex_constants::icase);
    static const std::wregex slash_context(LR"((barra(?:s)? (?:inversa(?:s)?|invertida(?:s)?)|backslash))",std::regex_constants::icase);
    static const std::wregex control_context(LR"((salto(?:s)?|control|tabulaci[oó]n|newline|line break))",std::regex_constants::icase);
    const auto contains=[&](const std::wregex& expression){
        for(std::regex_iterator<std::wstring_view::const_iterator> match(sentence.begin(),sentence.end(),expression),last;
                match!=last;++match){
            const auto at=static_cast<std::size_t>(match->position()),after=at+static_cast<std::size_t>(match->length());
            if((at==0||technical_mention_boundary(sentence[at-1]))&&
                    (after==sentence.size()||technical_mention_boundary(sentence[after])))return true;
        }
        return false;
    };
    return contains(string_context)||(contains(slash_context)&&contains(control_context));
}
std::vector<technical_prose::Range> quoted_prose_ranges(std::wstring_view text) {
    std::vector<technical_prose::Range> result;
    for(std::size_t at=0;at<text.size();++at){
        const auto quote=text[at];
        if(quote!=L'"'&&quote!=L'\''&&quote!=L'“'&&quote!=L'«')continue;
        auto end=technical_prose::closed_quote_end(text,at,text.size());
        if(quote==L'“'||quote==L'«'){
            const auto closing=text.find(quote==L'“'?L'”':L'»',at+1);
            end=closing==text.npos?text.npos:closing+1;
        }
        if(end==text.npos)continue;
        result.emplace_back(at,end);at=end-1;
    }
    return result;
}
std::vector<technical_prose::Range> unambiguous_cpp_types(std::wstring_view text,std::string_view locale) {
    std::vector<technical_prose::Range> result;
    if(technical_prose::canonical_locale(locale)!="es")return result;
    const auto quoted=quoted_prose_ranges(text);
    for(std::size_t at=0;at<text.size();){
        if(!identifier_start(text[at])){++at;continue;}
        const auto begin=at++;while(at<text.size()&&identifier_part(text[at]))++at;
        const auto word=text.substr(begin,at-begin);
        // These exact C++ type spellings have no ordinary Spanish homonym.
        // auto/final/do and arbitrary names from visible code remain contextual.
        if((word==L"int"||word==L"char")&&technical_prose::boundary(text,begin,at)&&
            !technical_prose::overlaps({begin,at},quoted))result.emplace_back(begin,at);
    }
    return result;
}
std::vector<technical_prose::Range> direct_cpp_initializers(std::wstring_view text) {
    std::vector<technical_prose::Range> result;
    const auto quoted=quoted_prose_ranges(text);
    for(std::size_t at=0;at<text.size();){
        if(!identifier_start(text[at])){++at;continue;}
        const auto begin=at++;while(at<text.size()&&identifier_part(text[at]))++at;
        if(at==text.size()||text[at]!=L'{'||!technical_prose::boundary(text,begin,at)||technical_prose::overlaps({begin,at},quoted))continue;
        const auto end=technical_prose::balanced_end(text,at,L'{',L'}');
        if(end==at)continue;
        auto argument=at+1;bool literal=false,valid=true;
        while(argument<end-1){
            while(argument<end-1&&technical_prose::space(text[argument]))++argument;
            if(argument==end-1)break;
            if(text[argument]==L'"'||text[argument]==L'\''){
                const auto close=technical_prose::closed_quote_end(text,argument,end-1);
                if(close==text.npos){valid=false;break;}argument=close;literal=true;
            }else if(identifier_start(text[argument])){
                const auto first=argument++;while(argument<end-1&&identifier_part(text[argument]))++argument;
                const auto word=text.substr(first,argument-first);
                literal=literal||word==L"true"||word==L"false"||word==L"nullptr";
            }else{
                if(text[argument]==L'+'||text[argument]==L'-')++argument;
                if(argument==end-1||!technical_prose::digit(text[argument])){valid=false;break;}
                ++argument;
                while(argument<end-1){
                    const auto value=text[argument];
                    if(identifier_part(value)||value==L'.'||value==L'\'')++argument;
                    else if((value==L'+'||value==L'-')&&std::wstring_view(L"eEpP").find(text[argument-1])!=text.npos)++argument;
                    else break;
                }
                literal=true;
            }
            while(argument<end-1&&technical_prose::space(text[argument]))++argument;
            if(argument==end-1)break;
            if(text[argument++]!=L','){valid=false;break;}
        }
        // A closed literal-bearing initializer is its own local code cue.
        // A bare known name, prose in braces or an unfinished group is not.
        if(valid&&literal&&(end==text.size()||technical_mention_boundary(text[end])))result.emplace_back(begin,end);
    }
    return result;
}
std::vector<technical_prose::Range> contextual_string_characters(HWND edit,std::wstring_view source,RichTextContext context) {
    static const std::wregex descriptive_letter(
        LR"((?:letras?|car[aáÁ]cter(?:es)?)[ \t]+([A-Za-z])|barra(?:s)?[ \t]+(?:inversa(?:s)?|invertida(?:s)?)[ \t]+y[ \t]+(?:la[ \t]+letra[ \t]+)?([A-Za-z]))",
        std::regex_constants::icase);
    // Feedback/status updates commonly contain no character description. Do
    // not lex a large visible program for those unrelated formatting passes.
    if((context.prose.empty()&&context.code.empty())||!std::regex_search(source.begin(),source.end(),descriptive_letter))return {};
    std::set<wchar_t> literal_letters;
    const auto observe=[&](std::wstring_view text,bool literal){
        for(std::size_t at=0;at+1<text.size();++at)if(text[at]==L'\\'){
            if(literal){
                // C++ escapes consume pairs: "\\\\n" is a backslash followed by n,
                // whereas "Entrada\\nSalida" contains an actual newline escape.
                if(std::wstring_view(L"ntr0abfv").find(text[at+1])!=std::wstring_view::npos)literal_letters.insert(text[at+1]);
                ++at;continue;
            }
            const auto end=escape_mention_end(text,at);
            if(end>at+1&&std::wstring_view(L"ntr0abfv").find(text[end-1])!=std::wstring_view::npos&&
                named_string_character(text,end-1,end))literal_letters.insert(text[end-1]);
            if(end>at)at=end-1;
        }
    };
    observe(wide(context.prose),false);
    const auto code=wide(context.code);
    for(const auto& ink:cpp_ink(edit,code))if(code[ink.start]==L'"'||code[ink.start]==L'\''){
        if(code[ink.start]==L'"'&&technical_prose::has(technical_prose::tail(code,ink.start,4),LR"((?:u8|u|U|L)?R$)"))continue;
        observe(std::wstring_view(code).substr(ink.start,ink.end-ink.start),true);
    }
    std::vector<technical_prose::Range> result;
    if(literal_letters.empty())return result;
    // The option names the visible spelling of an escape; a bare variable n
    // or a same-named identifier elsewhere never inherits a literal's ink.
    const auto quoted=quoted_prose_ranges(source);
    for(std::regex_iterator<std::wstring_view::const_iterator> match(source.begin(),source.end(),descriptive_letter),last;
            match!=last;++match){
        const auto group=(*match)[1].matched?1:2;
        const auto cue=static_cast<std::size_t>(match->position());
        const auto begin=static_cast<std::size_t>(match->position(group)),end=begin+1;
        if((cue==0||technical_mention_boundary(source[cue-1]))&&(end==source.size()||technical_mention_boundary(source[end]))&&
            literal_letters.contains(source[begin])&&!technical_prose::overlaps({begin,end},quoted))result.emplace_back(begin,end);
    }
    return result;
}
std::vector<std::pair<std::size_t,std::size_t>> inline_character_mentions(std::wstring_view text) {
    std::vector<std::pair<std::size_t,std::size_t>> result;
    // A standalone escape in prose describes the contents of a literal. Its
    // full spelling uses the same ink as that spelling inside actual C++ code.
    for(std::size_t index=0;index+1<text.size();++index){
        if(text[index]==L'/'&&text[index+1]==L'/'){
            // A prose mention is just the marker, not a comment extending to
            // the end of the explanatory paragraph. Do not match URLs, paths,
            // longer slash runs, or quote-delimited string contents here.
            if((index==0||(technical_mention_boundary(text[index-1])&&std::wstring_view(L"/\\:\"'").find(text[index-1])==std::wstring_view::npos))&&
                (index+2==text.size()||(technical_mention_boundary(text[index+2])&&std::wstring_view(L"/\\\"'").find(text[index+2])==std::wstring_view::npos)))
                result.emplace_back(index,index+2);
            continue;
        }
        if(text[index]!=L'\\'||(index&&(!technical_mention_boundary(text[index-1])||text[index-1]==L'\\')))continue;
        const auto end=escape_mention_end(text,index);
        if(end>index){result.emplace_back(index,end);index=end-1;}
    }
    static const std::wregex named_letter(LR"((?:letras?|car[aáÁ]cter(?:es)?|letters?|characters?|lettres?)[ \t\r\n]+([A-Za-z]))",std::regex_constants::icase);
    for(std::regex_iterator<std::wstring_view::const_iterator> match(text.begin(),text.end(),named_letter),end;match!=end;++match){
        const auto cue=static_cast<std::size_t>(match->position());
        const auto letter=static_cast<std::size_t>(match->position(1));
        if((cue==0||technical_mention_boundary(text[cue-1]))&&
            (letter+1==text.size()||technical_mention_boundary(text[letter+1])))result.emplace_back(letter,letter+1);
    }
    std::sort(result.begin(),result.end());
    return result;
}
// Only an explicitly introduced C++ literal is a string seed in prose.
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
std::size_t inline_cpp_statement_end(std::wstring_view text,std::size_t initial) {
    auto start=initial;
    while(start<text.size()&&(text[start]==L' '||text[start]==L'\t'))++start;
    const auto tail=text.substr(start);
    if(!tail.starts_with(L"<<")&&!tail.starts_with(L">>")&&!tail.starts_with(L"+=")&&!tail.starts_with(L"-=")&&
        !tail.starts_with(L"*=")&&!tail.starts_with(L"/=")&&!tail.starts_with(L"=")&&!tail.starts_with(L"++")&&!tail.starts_with(L"--"))return initial;
    // Android's ProgrammingTechnicalRanges extends an already recognized
    // C++ name to its statement terminator. Quotes and escapes belong to that
    // fragment; ordinary quoted prose is never a technical seed by itself.
    static const std::wregex prose_joiner(LR"(\s+(?:y|o|pero|porque|and|or|but|because|which|et|ou|mais|car|puis|qui|que)\b)",std::regex_constants::icase);
    const auto limit=std::min(text.size(),start+512);
    std::vector<wchar_t> closing;
    for(auto index=start;index<limit;++index){
        const auto current=text[index];
        if(current==L'"'||current==L'\''){
            const auto quote=current;
            for(++index;index<limit&&text[index]!=quote;++index)if(text[index]==L'\\')++index;
            if(index>=limit)return initial;
            continue;
        }
        if(current==L';'&&closing.empty())return index+1;
        const bool member_dot=current==L'.'&&index+1<limit&&
            ((text[index+1]>=L'0'&&text[index+1]<=L'9')||
                (index>0&&(identifier_part(text[index-1])||text[index-1]==L')'||text[index-1]==L']')&&identifier_part(text[index+1])));
        if(closing.empty()&&(current==L'\r'||current==L'\n'||current==L'!'||current==L'?'||(current==L'.'&&!member_dot)||
            ((current==L' '||current==L'\t')&&std::regex_search(text.begin()+index,text.begin()+limit,prose_joiner,std::regex_constants::match_continuous))))return initial;
        if(current==L'(')closing.push_back(L')');
        else if(current==L'[')closing.push_back(L']');
        else if(current==L'{')closing.push_back(L'}');
        else if(current==L')'||current==L']'||current==L'}'){
            if(closing.empty()||closing.back()!=current)return initial;
            closing.pop_back();
        }
    }
    return initial;
}
struct Stream { const BYTE* bytes; std::size_t size, offset = 0; };
DWORD CALLBACK stream_text(DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG* copied) {
    auto& source = *reinterpret_cast<Stream*>(cookie);
    const auto count = std::min(source.size - source.offset, static_cast<std::size_t>(requested));
    std::memcpy(buffer, source.bytes + source.offset, count); source.offset += count; *copied = static_cast<LONG>(count); return 0;
}
void default_format(HWND edit, bool code, RichTextRole role) {
    select(edit, 0, static_cast<std::size_t>(GetWindowTextLengthW(edit)));
    CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    style.yHeight = rich_text_height(role); style.crTextColor = reader_theme::foreground(edit);
    if(role!=RichTextRole::body&&!reader_theme::palette().high_contrast)style.crTextColor=RGB(255,255,255);
    // Capture the authored/base tint before lexical colors and reading ink.
    // Android's body tint differs from the generic Windows UI foreground.
    reader_theme::set_reference_tint(edit,!reader_theme::palette(edit).high_contrast&&role==RichTextRole::body&&
        style.crTextColor==reader_theme::palette(edit).text?RGB(205,208,215):style.crTextColor);
    style.dwEffects = CFE_AUTOBACKCOLOR | (role == RichTextRole::body ? 0 : CFE_BOLD);
    wcscpy_s(style.szFaceName, code ? reader_theme::code_font_face() : L"Roboto");
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&style));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&style));
    PARAFORMAT2 paragraph{}; paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_TABSTOPS | PFM_LINESPACING;
    paragraph.dySpaceAfter = 0; paragraph.cTabCount = MAX_TAB_STOPS;
    paragraph.bLineSpacingRule = 5;
    paragraph.dyLineSpacing = !code && role == RichTextRole::body ? 25 : 20;
    // Preserve the default tab positions. Editable code replaces these with
    // measured tab stops, preserving real tabs and indentation units.
    for (int tab = 0; tab < MAX_TAB_STOPS; ++tab) paragraph.rgxTabs[tab] = (tab + 1) * 792;
    SendMessageW(edit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraph));
}
}

std::wstring wide(std::string_view source) {
    if (source.empty()) return {};
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Text too large");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), nullptr, 0);
    if (!length) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), result.data(), length); return result;
}
std::string utf8(std::wstring_view source) {
    if (source.empty()) return {};
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Text too large");
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Invalid UTF-16 text");
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), result.data(), length, nullptr, nullptr); return result;
}
void set_edit_hint(HWND edit,std::string_view text,COLORREF color,bool centered) {
    if(!IsWindow(edit))throw std::invalid_argument("An edit hint requires a live RichEdit");
    DWORD_PTR existing=0;
    if(GetWindowSubclass(edit,EditHint::proc,hint_subclass_id,&existing)){
        auto* hint=reinterpret_cast<EditHint*>(existing);hint->text=wide(text);hint->color=color;hint->centered=centered;
    }
    else {
        auto hint=std::make_unique<EditHint>();hint->text=wide(text);hint->color=color;hint->centered=centered;
        if(!SetWindowSubclass(edit,EditHint::proc,hint_subclass_id,reinterpret_cast<DWORD_PTR>(hint.get())))
            throw std::runtime_error("Cannot attach an empty edit hint");
        hint.release();
    }
    InvalidateRect(edit,nullptr,FALSE);
}
void use_plain_text_input(HWND edit) {
    if(!IsWindow(edit))throw std::invalid_argument("Plain text input requires a live RichEdit");
    DWORD_PTR existing=0;
    if(GetWindowSubclass(edit,PlainTextInput::proc,plain_input_subclass_id,&existing))return;
    auto* callback=new PlainTextInput(edit);
    if(!SetWindowSubclass(edit,PlainTextInput::proc,plain_input_subclass_id,reinterpret_cast<DWORD_PTR>(callback))){
        callback->Release();throw std::runtime_error("Cannot attach plain text input");
    }
    if(!SendMessageW(edit,EM_SETOLECALLBACK,0,reinterpret_cast<LPARAM>(static_cast<IRichEditOleCallback*>(callback)))){
        RemoveWindowSubclass(edit,PlainTextInput::proc,plain_input_subclass_id);
        callback->Release();throw std::runtime_error("Cannot restrict RichEdit to text input");
    }
    // The subclass keeps the initial reference; RichEdit added its own.
    // Both are released by native destruction, regardless of callback order.
}
void compact_code_spaces(HWND edit) {
    GETTEXTLENGTHEX length{GTL_PRECISE|GTL_NUMCHARS,1200};
    const auto count=SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    if(count<=0)return;
    if(count>16*1024*1024)throw std::length_error("Code display text is too long");
    std::wstring source(static_cast<std::size_t>(count)+1,L'\0');
    GETTEXTEX get{static_cast<DWORD>(source.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    const auto copied=SendMessageW(edit,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&get),reinterpret_cast<LPARAM>(source.data()));
    if(copied<0)throw std::runtime_error("Cannot read code display text");source.resize(static_cast<std::size_t>(copied));
    FormatTransaction transaction(edit);
    // Unlike EM_SETCHARFORMAT, TOM refuses format changes on a readonly
    // display. Unlock only within this synchronous UI-thread transaction,
    // with redraw/events/Undo suspended, then restore before any repaint.
    struct ReadonlyFormat {
        HWND edit;bool readonly;
        explicit ReadonlyFormat(HWND target):edit(target),readonly((GetWindowLongPtrW(target,GWL_STYLE)&ES_READONLY)!=0){
            if(readonly)SendMessageW(edit,EM_SETREADONLY,FALSE,0);
        }
        ~ReadonlyFormat(){if(readonly)SendMessageW(edit,EM_SETREADONLY,TRUE,0);}
    } readonly(edit);
    if(!transaction.document)throw std::runtime_error("Cannot format compact code spaces");
    const auto scale=[&](LONG first,LONG last,LONG percent){
        Microsoft::WRL::ComPtr<ITextRange> range;
        Microsoft::WRL::ComPtr<ITextFont> font;
        Microsoft::WRL::ComPtr<ITextFont2> extended;
        if(FAILED(transaction.document->Range(first,last,&range))||FAILED(range->GetFont(&font))||
            FAILED(font.As(&extended))||FAILED(extended->SetScaling(percent)))
            throw std::runtime_error("Cannot scale native code display spaces");
    };
    // CHARFORMAT2::sSpacing is compatibility metadata: RichEdit does not draw
    // it. TOM2 horizontal scaling changes actual advances without changing
    // characters, height, syntax spans or the text undo history.
    scale(0,static_cast<LONG>(source.size()),100);
    for(std::size_t first=0;first<source.size();){
        if(source[first]!=L' '){++first;continue;}
        auto last=first+1;while(last<source.size()&&source[last]==L' ')++last;
        scale(static_cast<LONG>(first),static_cast<LONG>(last),80);first=last;
    }
}
bool rich_text_hit_test(HWND edit,POINT point) {
    const auto px=[&](int dip){return MulDiv(dip,static_cast<int>(GetDpiForWindow(edit)),96);};
    POINTL location{point.x,point.y};
    const auto character=static_cast<LONG>(SendMessageW(edit,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&location)));
    if(character<0)return false;
    wchar_t value[2]{};TEXTRANGEW range{{character,character+1},value};
    SendMessageW(edit,EM_GETTEXTRANGE,0,reinterpret_cast<LPARAM>(&range));
    if(!value[0]||value[0]==L'\r'||value[0]==L'\n'||value[0]==L' '||value[0]==L'\t')return false;
    POINTL start{},end{};
    SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&start),character);
    SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),character+1);
    // Read this character's actual font without selecting it. Selecting a
    // range would change the caret/history while deciding gesture ownership.
    int height=px(24),advance=px(14);
    Microsoft::WRL::ComPtr<IRichEditOle> ole;
    SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument> document;
    Microsoft::WRL::ComPtr<ITextRange> text_range;
    Microsoft::WRL::ComPtr<ITextFont> text_font;
    if(ole&&SUCCEEDED(ole.As(&document))&&SUCCEEDED(document->Range(character,character+1,&text_range))
        &&SUCCEEDED(text_range->GetFont(&text_font))){
        float points=18;BSTR face=nullptr;
        text_font->GetSize(&points);text_font->GetName(&face);
        int numerator=0,denominator=0;
        SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        const int zoom=denominator>0?MulDiv(numerator,100,denominator):100;
        const auto font=CreateFontW(-MulDiv(static_cast<int>(points*20+.5f)*zoom,
            static_cast<int>(GetDpiForWindow(edit)),1440*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,face?face:L"Segoe UI");
        if(font){
            const auto dc=GetDC(edit);const auto previous=SelectObject(dc,font);TEXTMETRICW metrics{};SIZE glyph{};
            if(GetTextMetricsW(dc,&metrics))height=metrics.tmHeight+metrics.tmExternalLeading;
            if(GetTextExtentPoint32W(dc,value,1,&glyph))advance=glyph.cx;
            SelectObject(dc,previous);ReleaseDC(edit,dc);DeleteObject(font);
        }
        SysFreeString(face);
    }
    const int right=end.y==start.y&&end.x>start.x?end.x:start.x+advance;
    return point.x>=start.x&&point.x<right&&point.y>=start.y&&point.y<start.y+height;
}
std::string edit_text(HWND edit) {
    GETTEXTLENGTHEX length{GTL_PRECISE | GTL_NUMCHARS, 1200};
    const auto count = SendMessageW(edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length), 0);
    if (count < 0 || count > 16 * 1024 * 1024) throw std::length_error("Editor text length is invalid");
    std::wstring raw(static_cast<std::size_t>(count) + 1, L'\0');
    GETTEXTEX get{static_cast<DWORD>(raw.size() * sizeof(wchar_t)), GT_RAWTEXT, 1200, nullptr, nullptr};
    const auto copied = SendMessageW(edit, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&get), reinterpret_cast<LPARAM>(raw.data()));
    if (copied < 0) throw std::runtime_error("Cannot read editor text");
    raw.resize(static_cast<std::size_t>(copied));
    std::wstring normalized; normalized.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\r') { normalized += L'\n'; if (i + 1 < raw.size() && raw[i + 1] == L'\n') ++i; }
        else normalized += raw[i];
    }
    return utf8(normalized);
}
std::vector<std::pair<std::size_t,std::size_t>> rich_text_reference_ranges(HWND edit){
    DWORD_PTR attached=0;
    if(!IsWindow(edit)||!GetWindowSubclass(edit,ReferenceRanges::proc,reference_ranges_subclass_id,&attached))return {};
    const auto* state=reinterpret_cast<const ReferenceRanges*>(attached);
    if(state->ranges.empty()||state->text!=wide(edit_text(edit)))return {};
    return state->ranges;
}
void map_rich_text_reference_ranges(HWND edit,
        std::span<const std::pair<std::size_t,std::size_t>> source_ranges,
        std::span<const std::size_t> source_to_native){
    if(!IsWindow(edit))throw std::invalid_argument("The transformed reference control is unavailable");
    const auto native=wide(edit_text(edit));
    if(source_to_native.empty()||source_to_native.front()!=0||source_to_native.back()!=native.size()||
            !std::ranges::is_sorted(source_to_native))
        throw std::invalid_argument("The transformed reference map is not a complete monotone UTF-16 map: boundaries="+
            std::to_string(source_to_native.size())+", first="+(source_to_native.empty()?"empty":std::to_string(source_to_native.front()))+
            ", last="+(source_to_native.empty()?"empty":std::to_string(source_to_native.back()))+", native="+std::to_string(native.size())+
            ", sorted="+std::to_string(std::ranges::is_sorted(source_to_native)));
    std::vector<std::pair<std::size_t,std::size_t>> mapped;
    std::size_t previous_end=0;
    for(const auto& [first,last]:source_ranges){
        if(first>=last||first<previous_end||last>=source_to_native.size())
            throw std::invalid_argument("An original reference range is outside its UTF-16 map");
        previous_end=last;
        const auto begin=source_to_native[first],end=source_to_native[last];
        const auto split=[&](std::size_t at){return at>0&&at<native.size()&&IS_HIGH_SURROGATE(native[at-1])&&IS_LOW_SURROGATE(native[at]);};
        if(split(begin)||split(end))throw std::invalid_argument("A transformed reference range splits a Unicode character");
        if(begin==end)continue;
        if(!mapped.empty()&&mapped.back().second>=begin)mapped.back().second=std::max(mapped.back().second,end);
        else mapped.emplace_back(begin,end);
    }
    publish_references(edit,native,std::move(mapped));
}
void set_edit_text(HWND edit, std::string_view text) {
    auto source = wide(text); if (source.find(L'\0') != source.npos) throw std::runtime_error("Editor text contains a null character");
    std::wstring native;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == L'\r') { native += L'\r'; if (i + 1 < source.size() && source[i + 1] == L'\n') ++i; }
        else native += source[i] == L'\n' ? L'\r' : source[i];
    }
    Stream source_stream{reinterpret_cast<const BYTE*>(native.data()), native.size() * sizeof(wchar_t)};
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&source_stream), 0, stream_text};
    // SF_TEXT explicitly prevents code beginning with {\\rtf from becoming RTF.
    SendMessageW(edit, EM_STREAMIN, SF_TEXT | SF_UNICODE, reinterpret_cast<LPARAM>(&stream));
    if (stream.dwError) throw std::runtime_error("Cannot set editor text");
}
void style_cpp(HWND edit) {
    const auto source = wide(edit_text(edit));
    FormatTransaction transaction(edit);
    // Editing controls own their font, measured tab stops and paragraph layout.
    // Recoloring must not replace those settings after every keystroke.
    CHARFORMAT2W base{}; base.cbSize = sizeof(base); base.dwMask = CFM_COLOR;
    base.crTextColor = reader_theme::foreground(edit);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&base));
    apply_cpp(edit, source);
}
bool rich_text_naming_choice_samples(std::string_view question,std::span<const std::string> options) {
    // An invalid identifier is still a sample in a naming exercise. Require
    // the public naming question AND a whole set of name-like alternatives,
    // so adjacent explanatory multiple-choice sentences retain ordinary ink.
    const auto prompt=wide(question);
    if(options.empty()||!technical_prose::has(prompt,
        LR"(\b(?:nombre|identificador|name|identifier|nom|identifiant)\b)",true)||
        !technical_prose::has(prompt,LR"(\b(?:v[aá]lid[oa]s?|valid(?:e|es|s)?|convenci[oó]n|convention|variable|identificador|identifier|identifiant)\b)",true))return false;
    for(const auto& option:options){
        const auto sample=wide(option);std::size_t words=0;bool in_word=false;
        if(sample.empty()||technical_prose::space(sample.front())||technical_prose::space(sample.back()))return false;
        for(const auto value:sample){
            if(value==L' '||value==L'\t'){in_word=false;continue;}
            if(!technical_prose::letter(value)&&!technical_prose::digit(value)&&value!=L'_'&&value!=L'-')return false;
            if(!in_word){in_word=true;if(++words>3)return false;}
        }
    }
    return true;
}
RichTextReferenceRole rich_text_choice_reference_role(std::string_view question,std::string_view sample) {
    const auto prompt=wide(question),value=wide(sample);
    if(value.empty())return RichTextReferenceRole::automatic;
    const bool output=technical_prose::has(prompt,
        LR"((?:qu[eé]|cu[aá]l(?:es)?)\s+(?:es\s+|son\s+|ser[ií]a\s+)?(?:la\s+)?salida|\bsalida(?:\s+de\s+texto)?\s+(?:produce|muestra)|\bmensaje\s+(?:produce|muestra))",true);
    const bool composed=technical_prose::has(prompt,
        LR"(\bqu[eé]\s+debe\s+contener\s+(?:la\s+|una\s+)?(?:(?:primera|segunda|nueva|resultante)\s+)?cadena\b)",true);
    if(composed&&technical_prose::curricular_role_detail::composed_data_sample(value))return RichTextReferenceRole::output;
    if(output){
        // Alternatives that explain a cause are prose, rather than purported
        // output. Punctuation, escapes and line breaks can all be data. A
        // visible source statement or a causal explanation remains distinct;
        // no solution or correct-index information is consulted.
        auto sample_text=std::wstring_view(value);
        while(!sample_text.empty()&&technical_prose::space(sample_text.front()))sample_text.remove_prefix(1);
        const bool explanation=technical_prose::has(sample_text,
            LR"(^(?:porque\b|debido\s+a\b|ya\s+que\b|el\s+programa\b|no\s+(?:muestra|produce|compila|hay|se\s+muestra)\b|se\s+|ninguna?\s+salida\b))",true);
        const bool statement=technical_prose::has(sample_text,
            LR"(^(?:#\s*(?:include|define)\b|(?:std::)?(?:cout|cerr|clog)\s*<<|return\b[^\n]*;\s*$|(?:const\s+)?(?:int|char|bool|float|double|std::string|auto)\s+[*&]*\s*[A-Za-z_][A-Za-z_0-9]*\s*(?:[=({]|;\s*$)))");
        if(!explanation&&!statement)
            return RichTextReferenceRole::output;
    }
    const auto end=value.front()==L'"'?technical_prose::closed_quote_end(value,0,value.size()):value.npos;
    if(end==value.size()&&technical_prose::has(prompt,
        LR"(\b(?:literal(?:\s+de\s+(?:cadena|texto))?|cadena(?:s)?|etiquetas?\s+de\s+texto|concaten\w*|std::string)\b)",true))
        return RichTextReferenceRole::string;
    return RichTextReferenceRole::automatic;
}
void set_rich_text(HWND edit, std::string_view text, std::span<const curriculum::Emphasis> emphasis, bool code, RichTextRole role,
    std::string_view locale,std::span<const std::pair<std::size_t,std::size_t>> mathematics,RichTextContext context) {
    std::string displayed;
    std::vector<std::size_t> mapping(text.size() + 1);
    std::vector<std::pair<std::size_t,std::size_t>> fragments;
    const auto original_source=wide(text);
    const auto trusted=code?technical_prose::InlineCodeSpans{}:technical_prose::inline_code_spans(original_source,mathematics);
    std::vector<std::size_t> byte_boundaries(original_source.size()+1,std::string_view::npos);
    for(std::size_t byte=0,unit=0;byte<text.size();){byte_boundaries[unit]=byte;const auto lead=static_cast<unsigned char>(text[byte]);
        const auto count=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;byte+=count;unit+=count==4?2:1;byte_boundaries[unit]=byte;}
    byte_boundaries.back()=text.size();
    std::vector<technical_prose::Range> wrappers;for(const auto& [first,last]:trusted.wrappers)wrappers.emplace_back(byte_boundaries[first],byte_boundaries[last]);
    std::size_t next_wrapper=0;
    for (std::size_t i = 0; i < text.size();) {
        mapping[i] = displayed.size();
        if(next_wrapper<wrappers.size()&&wrappers[next_wrapper].first==i){const auto last=wrappers[next_wrapper++].second;
            for(auto hidden=i;hidden<=last;++hidden)mapping[hidden]=displayed.size();i=last;continue;}
        // RichEdit stores a single paragraph mark. Keep display-formatting
        // offsets in that same unit while the original UTF-8 source/map stays
        // authoritative for emphasis, narration and CRLF preservation.
        if(text[i]=='\r'){if(i+1<text.size()&&text[i+1]=='\n')mapping[++i]=displayed.size();displayed+='\n';++i;continue;}
        displayed += text[i++];
    }
    mapping[text.size()] = displayed.size();
    for(const auto& [first,last]:trusted.fragments)fragments.emplace_back(mapping[byte_boundaries[first]],mapping[byte_boundaries[last]]);
    set_edit_text(edit, displayed);
    FormatTransaction transaction(edit);
    default_format(edit, code, role);
    const auto source = wide(displayed);
    // A language name in curriculum prose is emphasis, not C++ source ink.
    // Track only its displayed offsets, without changing lesson/narration text.
    std::vector<std::pair<std::size_t,std::size_t>> language_names;
    std::vector<std::pair<std::size_t,std::size_t>> cpp_statements;
    std::vector<std::pair<std::size_t,std::size_t>> quoted_literals;
    std::vector<std::pair<std::size_t,std::size_t>> explicit_strings;
    std::vector<std::pair<std::size_t,std::size_t>> inline_fragments;
    for(const auto& [start,end]:fragments){const auto begin=wide(std::string_view(displayed).substr(0,start)).size();
        inline_fragments.emplace_back(begin,begin+wide(std::string_view(displayed).substr(start,end-start)).size());}
    std::vector<technical_prose::Range> displayed_mathematics;
    if(!mathematics.empty()){for(const auto& [first,last]:mathematics)if(first<last&&last<=original_source.size()){
        const auto begin_byte=byte_boundaries[first],end_byte=byte_boundaries[last];
        if(begin_byte==std::string_view::npos||end_byte==std::string_view::npos)throw std::invalid_argument("A mathematical range splits a UTF-16 character");
        displayed_mathematics.emplace_back(wide(std::string_view(displayed).substr(0,mapping[begin_byte])).size(),wide(std::string_view(displayed).substr(0,mapping[end_byte])).size());}}
    const bool curricular=!code&&role!=RichTextRole::lesson_title;
    auto semantic=curricular?technical_prose::analyze(source,locale,inline_fragments,displayed_mathematics):technical_prose::Analysis{};
    const auto contextual_strings=curricular?contextual_string_characters(edit,source,context):std::vector<technical_prose::Range>{};
    if(curricular){
        for(const auto& range:unambiguous_cpp_types(source,locale))
            if(!technical_prose::overlaps(range,semantic.protected_ranges))semantic.fragments.push_back(range);
        for(const auto& range:direct_cpp_initializers(source))
            if(!technical_prose::overlaps(range,semantic.protected_ranges))semantic.fragments.push_back(range);
        for(const auto& range:contextual_strings)
            if(!technical_prose::overlaps(range,semantic.protected_ranges))semantic.fragments.push_back(range);
        std::ranges::sort(semantic.fragments);
    }
    // Resolve prose/code boundaries before any lexical color pass. A classic
    // analyzer can otherwise expand an explicitly isolated operator into the
    // surrounding natural-language words once its backticks are hidden.
    auto roles=curricular?technical_prose::curricular_roles(
        source,locale,semantic,wide(context.prose),wide(context.code),inline_fragments):technical_prose::CurricularRoles{};
    if(curricular&&!source.empty()&&context.reference_role!=RichTextReferenceRole::automatic){
        const technical_prose::Range whole{0,source.size()};
        if(!technical_prose::overlaps(whole,semantic.protected_ranges)){
            roles.code.push_back(whole);
            if(context.reference_role==RichTextReferenceRole::output)roles.outputs.push_back(whole);
            else {roles.strings.push_back(whole);technical_prose::curricular_role_detail::subtract(roles.neutral_numbers,std::span<const technical_prose::Range>(&whole,1));}
            technical_prose::curricular_role_detail::merge(roles.code);
        }
    }
    if(!code)semantic.fragments=roles.code;
    const bool has_background=!reader_theme::palette().high_contrast;
    const auto backed=[&](std::size_t start,std::size_t end){return has_background&&std::ranges::any_of(semantic.fragments,[&](const auto& part){return part.first<=start&&part.second>=end;});};
    const auto protected_text=[&](std::size_t start,std::size_t end){return technical_prose::overlaps({start,end},semantic.protected_ranges);};
    std::vector<std::pair<std::size_t,std::size_t>> prose_language_names;
    if (code) apply_cpp(edit, source);
    else {
        // Android applies syntax and a native-layout underlay over semantic
        // ranges, without introducing metric-affecting spans. Preserve every
        // existing Windows face/size; the added technical ranges change ink only.
        for(const auto& [start,end]:semantic.fragments){
            format(edit,start,end,has_background?RGB(216,216,216):reader_theme::foreground(edit));
            apply_cpp(edit,std::wstring_view(source).substr(start,end-start),start,false,has_background);
        }
        for (const auto& mention:technical_mentions(source)) {
            const auto start=mention.start,count=mention.end-start;
            // A language name is never an identifier C plus two operators.
            if(mention.kind==MentionKind::language){language_names.emplace_back(start,start+count);continue;}
            if(protected_text(start,start+count)||!technical_prose::overlaps({start,start+count},semantic.fragments))continue;
            format(edit, start, start + count, backed(start,start+count)?RGB(216,216,216):reader_theme::foreground(edit), reader_theme::code_font_face());
            apply_cpp(edit, std::wstring_view(source).substr(start,count), start, true,backed(start,start+count));
            if(mention.kind==MentionKind::qualified&&(cpp_statements.empty()||start>=cpp_statements.back().second)){
                const auto statement_end=inline_cpp_statement_end(source,start+count);
                if(statement_end>start+count)cpp_statements.emplace_back(start,statement_end);
            }
        }
        const auto character_mentions=inline_character_mentions(source);
        for(const auto& [start,end]:character_mentions) {
            if(protected_text(start,end))continue;
            if(source[start]==L'\\'||named_string_character(source,start,end)){
                format(edit,start,end,cpp_color(edit,CppInk::string,backed(start,end)));
                explicit_strings.emplace_back(start,end);
            }
            else apply_cpp(edit,std::wstring_view(source).substr(start,end-start),start,false,backed(start,end));
        }
        // Apply complete fragments after individual technical mentions so a
        // word such as std::cout or << inside a literal keeps the string ink.
        // Match Android's color-only treatment: preserve existing fonts,
        // source positions and paragraph measurements.
        for(const auto& [start,end]:cpp_statements)apply_cpp(edit,std::wstring_view(source).substr(start,end-start),start,false,backed(start,end));
        for (const auto& [start,end] : fragments) {
            const auto begin_wide = wide(std::string_view(displayed).substr(0,start)).size();
            const auto fragment = wide(std::string_view(displayed).substr(start,end-start));
            if(protected_text(begin_wide,begin_wide+fragment.size()))continue;
            format(edit, begin_wide, begin_wide + fragment.size(), backed(begin_wide,begin_wide+fragment.size())?RGB(216,216,216):reader_theme::foreground(edit), reader_theme::code_font_face());
            // Explicit output prose gives this backtick span data semantics.
            // In particular, "La salida es `Carga lista`" must not lex its
            // words as two identifiers. Other code spans keep the C++ lexer.
            static const std::wregex output_cue(
                LR"((?:la salida es|salida(?: exacta)?[ :=]+|the output is|output[ :=]+|la sortie est|sortie[ :=]+)[ \t]*$)",
                std::regex_constants::icase);
            const auto prefix=std::wstring_view(source).substr(0,begin_wide);
            const auto tail=prefix.substr(prefix.size()>128?prefix.size()-128:0);
            const bool named_string=fragment.size()==1&&
                std::ranges::find(character_mentions,std::pair{begin_wide,begin_wide+fragment.size()})!=character_mentions.end()&&
                named_string_character(source,begin_wide,begin_wide+fragment.size());
            if(std::regex_search(tail.begin(),tail.end(),output_cue)||
                    (fragment.starts_with(L"\\")&&escape_mention_end(fragment,0)==fragment.size())||named_string){
                format(edit,begin_wide,begin_wide+fragment.size(),cpp_color(edit,CppInk::string,backed(begin_wide,begin_wide+fragment.size())));
                explicit_strings.emplace_back(begin_wide,begin_wide+fragment.size());
            }
            else apply_cpp(edit, fragment, begin_wide, true,backed(begin_wide,begin_wide+fragment.size()));
        }
        // Last lexical layer: operator-looking characters inside an explicit
        // literal keep string ink. Do not change its face, text or source offsets.
        quoted_literals=inline_string_literals(source);
        for(const auto& [begin,end]:quoted_literals)
            if(!protected_text(begin,end))format(edit,begin,end,cpp_color(edit,CppInk::string,backed(begin,end)));
        for(const auto& [begin,end]:contextual_strings)
            if(!protected_text(begin,end))format(edit,begin,end,cpp_color(edit,CppInk::string,backed(begin,end)));
    }
    for (const auto& range : curriculum::emphasis_ranges(text, emphasis)) {
        const auto start = wide(std::string_view(displayed).substr(0, mapping[range.start])).size();
        const auto end = wide(std::string_view(displayed).substr(0, mapping[range.end])).size();
        select(edit,start,end);
        CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_BOLD | CFM_ITALIC;
        style.dwEffects = (role != RichTextRole::body || range.style == "bold" || range.style == "bold_italic" ? CFE_BOLD : 0) |
                          (range.style == "italic" || range.style == "bold_italic" ? CFE_ITALIC : 0);
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&style));
    }
    if(!code)for(const auto& [start,end]:language_names) {
        // Match a complete language name, never a suffix of an identifier,
        // an operator run or the contents of an actual C++ string expression.
        if((start&&identifier_part(source[start-1]))||
            (end<source.size()&&(identifier_part(source[end])||source[end]==L'+')))continue;
        const auto intersects=[&](const auto& intervals){
            return std::any_of(intervals.begin(),intervals.end(),[&](const auto& part){
                return start<part.second&&part.first<end;
            });
        };
        if(intersects(cpp_statements)||intersects(quoted_literals)||intersects(roles.outputs)||intersects(roles.strings))continue;
        bool standalone_inline=false,embedded_inline=false;
        for(const auto& [first,last]:inline_fragments)if(start<last&&first<end){
            if(first==start&&last==end)standalone_inline=true;
            else embedded_inline=true;
        }
        if(embedded_inline)continue;
        prose_language_names.emplace_back(start,end);
        select(edit,start,end);
        CHARFORMAT2W style{};style.cbSize=sizeof(style);
        style.dwMask=CFM_COLOR|CFM_BOLD;
        style.crTextColor=role!=RichTextRole::body&&!reader_theme::palette().high_contrast
            ?RGB(255,255,255):reader_theme::foreground(edit);
        style.dwEffects=CFE_BOLD; // Preserve authored italics and other spans.
        if(standalone_inline){style.dwMask|=CFM_FACE;wcscpy_s(style.szFaceName,L"Roboto");}
        SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));
    }
    COLORREF semantic_color=0;
    if(!code&&has_background){
        const auto background=reader_theme::reference_background(edit);
        semantic_color=background;
        const auto paint_background=[&](std::size_t first,std::size_t last){if(first>=last)return;select(edit,first,last);
            CHARFORMAT2W style{};style.cbSize=sizeof(style);style.dwMask=CFM_BACKCOLOR;style.crBackColor=background;
            SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));};
        for(const auto& [first,last]:semantic.fragments){auto begin=first;
            for(const auto& [name_first,name_last]:prose_language_names){if(name_last<=begin||name_first>=last)continue;
                paint_background(begin,std::min(name_first,last));begin=std::min(last,name_last);}
            paint_background(begin,last);
        }
    }
    std::vector<technical_prose::Range> references;
    if(curricular){
        // The editor and prose references use the same lexer and exact ink
        // categories. No face/size/emphasis or adjacent prose is reformatted.
        for(const auto& [first,last]:roles.code){
            if(first>=last||last>source.size())continue;
            for(const auto& ink:cpp_ink(edit,std::wstring_view(source).substr(first,last-first)))
                format(edit,first+ink.start,first+ink.end,ink.color);
        }
        // Context can identify string contents without their C++ quote marks
        // (an escape's named character, or a displayed output such as Carga
        // lista). Relexing those contents as identifiers must not erase the
        // already established string role. Numeric outputs still win below.
        const auto string_roles=[&](const auto& ranges){for(const auto& [first,last]:ranges){
            if(first>=last||last>source.size()||protected_text(first,last))continue;
            format(edit,first,last,cpp_color(edit,CppInk::string));
        }};
        string_roles(explicit_strings);string_roles(quoted_literals);string_roles(contextual_strings);
        string_roles(roles.strings);
        for(const auto& [first,last]:prose_language_names){
            format(edit,first,last,role==RichTextRole::body||reader_theme::palette().high_contrast
                ?reader_theme::foreground(edit):RGB(255,255,255));
        }
        // Bibliographic numbers stay ordinary text. Actual source literals
        // are excluded by the role classifier, even when their value is a year.
        for(const auto& [first,last]:roles.citations){
            if(first>=last||last>source.size())continue;
            format(edit,first,last,role==RichTextRole::body||reader_theme::palette().high_contrast
                ?reader_theme::foreground(edit):RGB(255,255,255));
        }
        // Last ink wins over legacy code/output heuristics, including explicit
        // numeric spans. Use the existing mathematical foreground policy.
        for(const auto& [first,last]:roles.neutral_numbers){
            if(first>=last||last>source.size()||technical_prose::overlaps({first,last},roles.citations))continue;
            format(edit,first,last,reader_theme::foreground(edit));
        }
        // A result is displayed data, even when its spelling also occurs
        // inside a C++ literal elsewhere. Its complete existing backdrop is
        // retained; only the ink returns to the neutral mathematical tint.
        for(const auto& [first,last]:roles.outputs)
            if(first<last&&last<=source.size())format(edit,first,last,reader_theme::foreground(edit));
        // Background highlighting and reference ownership are independent of
        // typography. Never introduce underline lines or category-colored fills.
        references=roles.code;
        const auto append_references=[&](const auto& extra){references.insert(references.end(),extra.begin(),extra.end());};
        append_references(explicit_strings);append_references(quoted_literals);append_references(contextual_strings);
        technical_prose::curricular_role_detail::merge(references);
        technical_prose::curricular_role_detail::subtract(references,prose_language_names);
        // Keep a language name's ordinary ink/bold treatment while allowing
        // its version digits (C++20) the same neutral numeric background.
        append_references(roles.numbers);
        if(context.technical_sample&&!source.empty())references.emplace_back(0,source.size());
        technical_prose::curricular_role_detail::merge(references);
        technical_prose::curricular_role_detail::subtract(references,roles.citations);
        technical_prose::curricular_role_detail::subtract(references,semantic.protected_ranges);
        std::vector<technical_prose::Range> tokens;
        for(const auto& [first,last]:references){
            if(first>=last||last>source.size())continue;
            // Spaces can be the deliberate error in a candidate name. Keep
            // the entire sample's backdrop, while reference ownership below
            // still excludes whitespace exactly like other curricular text.
            if(context.technical_sample&&has_background){select(edit,first,last);CHARFORMAT2W style{};style.cbSize=sizeof(style);
                style.dwMask=CFM_BACKCOLOR;style.crBackColor=semantic_color;
                SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));}
            auto at=first;
            while(at<last){
                while(at<last&&technical_prose::space(source[at]))++at;
                const auto begin=at;
                while(at<last&&!technical_prose::space(source[at]))++at;
                if(begin==at)continue;
                tokens.emplace_back(begin,at);
                if(has_background){select(edit,begin,at);CHARFORMAT2W style{};style.cbSize=sizeof(style);
                    style.dwMask=CFM_BACKCOLOR;style.crBackColor=semantic_color;
                    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));}
            }
        }
        references=std::move(tokens);
    }
    publish_references(edit,source,references);
    semantic_background(edit,semantic_color,!code&&has_background&&!references.empty());
}
} // namespace lgaed::presentation

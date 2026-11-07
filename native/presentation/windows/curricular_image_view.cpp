#include "presentation/windows/curricular_image_view.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace lgaed::presentation {
namespace {
using Microsoft::WRL::ComPtr;
constexpr int detail_fit=101,detail_actual=102,detail_less=103,detail_more=104,detail_back=105,detail_canvas=106;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int pixels(HWND item,int value){return MulDiv(value,static_cast<int>(GetDpiForWindow(item)),96);}
struct Handle { HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
void prepare_images(){static const auto token=[](){ULONG_PTR startup_token{};Gdiplus::GdiplusStartupInput input;
    require(Gdiplus::GdiplusStartup(&startup_token,&input,nullptr)==Gdiplus::Ok,"No se pudo iniciar el lector de imágenes.");return startup_token;}();(void)token;}
std::wstring final_name(HANDLE file){const auto size=GetFinalPathNameByHandleW(file,nullptr,0,FILE_NAME_NORMALIZED);
    require(size>0,"No se pudo comprobar la ubicación de la imagen.");std::wstring result(size,L'\0');
    const auto used=GetFinalPathNameByHandleW(file,result.data(),size,FILE_NAME_NORMALIZED);
    require(used>0&&used<size,"No se pudo comprobar la ubicación de la imagen.");result.resize(used);return result;}
std::filesystem::path content_root(std::filesystem::path lesson){
    for(auto parent=std::filesystem::absolute(lesson).parent_path();!parent.empty();){
        if(parent.filename()==L"content")return parent;const auto next=parent.parent_path();if(next==parent)break;parent=next;
    }throw std::runtime_error("La imagen requiere una lección dentro de content/.");
}
std::shared_ptr<Gdiplus::Bitmap> load_image(const std::filesystem::path& lesson,const curriculum::ImageResource& resource){
    curriculum::validate_image_resource(resource);prepare_images();const auto root=content_root(lesson);
    Handle directory{CreateFileW(state::native_io_path(root).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr)};
    require(directory.value!=INVALID_HANDLE_VALUE,"No se pudo abrir la raíz del contenido.");
    const auto path=root/std::filesystem::path(wide(resource.src));
    Handle file{CreateFileW(state::native_io_path(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    require(file.value!=INVALID_HANDLE_VALUE,"La imagen no está disponible: falta el archivo o no se puede leer.");
    auto base=final_name(directory.value);if(!base.ends_with(L'\\'))base+=L'\\';const auto actual=final_name(file.value);
    require(actual.size()>base.size()&&_wcsnicmp(actual.data(),base.data(),base.size())==0,"La imagen está fuera de la raíz del contenido.");
    BY_HANDLE_FILE_INFORMATION info{};require(GetFileType(file.value)==FILE_TYPE_DISK&&GetFileInformationByHandle(file.value,&info)&&
        !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"El recurso no es un archivo de imagen local.");
    LARGE_INTEGER length{};require(GetFileSizeEx(file.value,&length)&&length.QuadPart>0&&static_cast<std::uint64_t>(length.QuadPart)<=curriculum::image_max_bytes,
        "El archivo de imagen está vacío o supera 64 MiB.");
    std::vector<BYTE> bytes(static_cast<std::size_t>(length.QuadPart));DWORD read{};
    require(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size(),"No se pudo leer la imagen completa.");
    ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(bytes.data(),static_cast<UINT>(bytes.size())));
    require(bool(stream),"No se pudo preparar la imagen.");
    auto source=std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromStream(stream.Get(),FALSE));
    require(source&&source->GetLastStatus()==Gdiplus::Ok,"El recurso no contiene una imagen válida.");
    GUID format{};require(source->GetRawFormat(&format)==Gdiplus::Ok,"No se pudo reconocer el formato de imagen.");
    auto extension=path.extension().wstring();for(auto& value:extension)value=static_cast<wchar_t>(towlower(value));
    require((extension==L".png"&&format==Gdiplus::ImageFormatPNG)||
        ((extension==L".jpg"||extension==L".jpeg")&&format==Gdiplus::ImageFormatJPEG),"El formato de imagen no coincide con la extensión PNG/JPEG.");
    const auto width=source->GetWidth(),height=source->GetHeight();
    require(width&&height&&width<=curriculum::image_max_dimension&&height<=curriculum::image_max_dimension&&
        static_cast<std::uint64_t>(width)*height<=curriculum::image_max_pixels,"Las dimensiones de la imagen exceden el límite admitido.");
    auto result=std::make_shared<Gdiplus::Bitmap>(width,height,PixelFormat32bppPARGB);
    require(result->GetLastStatus()==Gdiplus::Ok,"No hay memoria para abrir la imagen.");
    // Copy pixels while the stream is alive. The detached image holds no file
    // handle, so editing the author's source never depends on closing a lesson.
    Gdiplus::Graphics copy(result.get());copy.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    require(copy.DrawImage(source.get(),0,0,width,height)==Gdiplus::Ok,"No se pudo decodificar la imagen completa.");return result;
}
void paint_image(HDC dc,const RECT& area,const std::shared_ptr<Gdiplus::Bitmap>& image,const ImageViewport& viewport){
    FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::canvas));if(!image)return;
    const auto width=static_cast<int>(std::lround(image->GetWidth()*viewport.scale)),height=static_cast<int>(std::lround(image->GetHeight()*viewport.scale));
    const auto x=width<area.right?(area.right-width)/2:-viewport.x,y=height<area.bottom?(area.bottom-height)/2:-viewport.y;
    Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(viewport.scale==1?Gdiplus::InterpolationModeNearestNeighbor:Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);graphics.DrawImage(image.get(),x,y,width,height);
}
struct Detail {
    HWND window{},canvas{};
    HFONT font{};
    ~Detail(){if(font)DeleteObject(font);}
    std::shared_ptr<Gdiplus::Bitmap> image;
    std::wstring description;
    ImageViewport viewport;
    POINT pressed{};int old_x=0,old_y=0;bool dragging=false,owned=false;
    void fonts(){const auto next=CreateFontW(-pixels(window,18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");if(!next)return;
        for(int id=detail_fit;id<=detail_canvas;++id)SendMessageW(GetDlgItem(window,id),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;}
    void scrollbars(){RECT area{};GetClientRect(canvas,&area);viewport.layout(image->GetWidth(),image->GetHeight(),area.right,area.bottom);
        for(const auto bar:{SB_HORZ,SB_VERT}){const bool horizontal=bar==SB_HORZ;SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
            info.nMax=std::max(0,static_cast<int>(std::lround((horizontal?image->GetWidth():image->GetHeight())*viewport.scale))-1);
            info.nPage=horizontal?area.right:area.bottom;info.nPos=horizontal?viewport.x:viewport.y;SetScrollInfo(canvas,bar,&info,TRUE);}InvalidateRect(canvas,nullptr,FALSE);}
    void resize(){RECT area{};GetClientRect(window,&area);const auto h=pixels(window,44),gap=pixels(window,6);
        const auto button_width=std::min(pixels(window,95),static_cast<int>(std::max(1L,area.right-gap*2)));int x=gap,y=gap;
        for(const auto id:{detail_fit,detail_actual,detail_less,detail_more,detail_back}){if(x>gap&&x+button_width>area.right-gap){x=gap;y+=h;}
            MoveWindow(GetDlgItem(window,id),x,y,button_width,h-gap*2,TRUE);x+=button_width+gap;}
        const auto top=y+h-gap;MoveWindow(canvas,0,top,std::max(1L,area.right),std::max(1L,area.bottom-top),TRUE);scrollbars();}
    static LRESULT CALLBACK canvas_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){auto& self=*reinterpret_cast<Detail*>(raw);
        // STATIC normally returns HTTRANSPARENT; preserve the actual client
        // and scrollbar hit regions without moving the real mouse cursor.
        if(message==WM_NCHITTEST)return DefWindowProcW(item,message,wp,lp);
        if(message==WM_GETDLGCODE)return DLGC_WANTARROWS;
        if(message==WM_PAINT||message==WM_PRINTCLIENT){PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(item,&paint):reinterpret_cast<HDC>(wp);
            RECT area{};GetClientRect(item,&area);paint_image(dc,area,self.image,self.viewport);if(message==WM_PAINT)EndPaint(item,&paint);return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_SIZE){self.scrollbars();return 0;}
        if(message==WM_VSCROLL||message==WM_HSCROLL){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(item,message==WM_HSCROLL?SB_HORZ:SB_VERT,&info);
            auto& at=message==WM_HSCROLL?self.viewport.x:self.viewport.y;switch(LOWORD(wp)){
            case SB_LINEUP:at-=pixels(item,30);break;case SB_LINEDOWN:at+=pixels(item,30);break;
            case SB_PAGEUP:at-=info.nPage;break;case SB_PAGEDOWN:at+=info.nPage;break;
            case SB_THUMBTRACK:case SB_THUMBPOSITION:at=info.nTrackPos;break;case SB_TOP:at=0;break;case SB_BOTTOM:at=info.nMax;break;}
            self.scrollbars();return 0;}
        if(message==WM_MOUSEWHEEL){self.viewport.y-=GET_WHEEL_DELTA_WPARAM(wp)*pixels(item,60)/WHEEL_DELTA;self.scrollbars();return 0;}
        if(message==WM_KEYDOWN){RECT area{};GetClientRect(item,&area);switch(wp){
            case VK_LEFT:self.viewport.x-=pixels(item,30);break;case VK_RIGHT:self.viewport.x+=pixels(item,30);break;
            case VK_UP:self.viewport.y-=pixels(item,30);break;case VK_DOWN:self.viewport.y+=pixels(item,30);break;
            case VK_PRIOR:self.viewport.y-=area.bottom;break;case VK_NEXT:self.viewport.y+=area.bottom;break;
            case VK_HOME:self.viewport.y=0;break;case VK_END:self.viewport.y=static_cast<int>(self.image->GetHeight()*self.viewport.scale);break;
            case VK_ESCAPE:SendMessageW(self.window,WM_CLOSE,0,0);return 0;default:return DefSubclassProc(item,message,wp,lp);}
            self.scrollbars();return 0;}
        if(message==WM_LBUTTONDOWN){self.dragging=true;self.pressed={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};self.old_x=self.viewport.x;self.old_y=self.viewport.y;SetCapture(item);return 0;}
        if(message==WM_MOUSEMOVE&&self.dragging){self.viewport.x=self.old_x-(GET_X_LPARAM(lp)-self.pressed.x);self.viewport.y=self.old_y-(GET_Y_LPARAM(lp)-self.pressed.y);self.scrollbars();return 0;}
        if(message==WM_LBUTTONUP||message==WM_CAPTURECHANGED){self.dragging=false;if(GetCapture()==item)ReleaseCapture();return 0;}
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Detail*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Detail*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wp,lp);
        if(message==WM_CREATE){const std::array<const wchar_t*,5> labels{L"Ajustar",L"100 %",L"−",L"+",L"Volver"};
            for(int id=detail_fit;id<=detail_back;++id){const auto button=CreateWindowExW(0,L"BUTTON",labels[id-detail_fit],WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);reader_theme::apply(button,reader_theme::Surface::header);}
            const auto description=L"Detalle de la imagen: "+self->description;
            self->canvas=CreateWindowExW(0,L"STATIC",description.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_HSCROLL|WS_VSCROLL,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail_canvas)),GetModuleHandleW(nullptr),nullptr);
            SetWindowSubclass(self->canvas,canvas_proc,1,reinterpret_cast<DWORD_PTR>(self));self->fonts();self->resize();return 0;}
        if(message==WM_DPICHANGED_AFTERPARENT){self->fonts();self->resize();return 0;}
        if(message==WM_SIZE){self->resize();return 0;}
        if(message==WM_COMMAND){const auto id=LOWORD(wp);if(id==detail_back||id==IDCANCEL){SendMessageW(window,WM_CLOSE,0,0);return 0;}
            RECT area{};GetClientRect(self->canvas,&area);if(id==detail_fit){self->viewport.fit=true;self->viewport.x=self->viewport.y=0;}
            else if(id>=detail_actual&&id<=detail_more)self->viewport.zoom(id==detail_actual?1:self->viewport.scale*(id==detail_less?.8:1.25),self->image->GetWidth(),self->image->GetHeight(),area.right,area.bottom);
            self->scrollbars();return 0;}
        if(message==WM_ERASEBKGND){RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::header));return 1;}
        if(message==WM_DRAWITEM)return reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        if(message==WM_NCDESTROY){SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;}
        return DefWindowProcW(window,message,wp,lp);
    }
};
}
void ImageViewport::layout(unsigned image_width,unsigned image_height,int width,int height){
    if(!image_width||!image_height)return;if(fit)scale=std::min(static_cast<double>(std::max(1,width))/image_width,static_cast<double>(std::max(1,height))/image_height);
    x=std::clamp(x,0,std::max(0,static_cast<int>(std::lround(image_width*scale))-std::max(1,width)));
    y=std::clamp(y,0,std::max(0,static_cast<int>(std::lround(image_height*scale))-std::max(1,height)));
}
void ImageViewport::zoom(double next,unsigned image_width,unsigned image_height,int width,int height){
    if(!std::isfinite(next))return;const auto old=scale;fit=false;scale=std::clamp(next,.05,8.);
    x=static_cast<int>(std::lround((x+width/2.)*scale/std::max(old,.00001)-width/2.));
    y=static_cast<int>(std::lround((y+height/2.)*scale/std::max(old,.00001)-height/2.));layout(image_width,image_height,width,height);
}
struct CurricularImageView::Impl {
    HWND item{};std::shared_ptr<Gdiplus::Bitmap> image;std::string failure,description;
    HFONT font{};
    ~Impl(){if(font)DeleteObject(font);}
    void fonts(){const auto next=CreateFontW(-pixels(item,18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");if(next){if(font)DeleteObject(font);font=next;}}
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){auto& self=*reinterpret_cast<Impl*>(raw);
        if(message==WM_DPICHANGED_AFTERPARENT){self.fonts();InvalidateRect(item,nullptr,FALSE);return 0;}
        if(message==WM_PAINT||message==WM_PRINTCLIENT){PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(item,&paint):reinterpret_cast<HDC>(wp);RECT area{};GetClientRect(item,&area);
            const auto previous=self.font?SelectObject(dc,self.font):nullptr;
            RECT picture=area;if(self.image)picture.bottom=std::max(1L,picture.bottom-pixels(item,28));
            ImageViewport viewport;viewport.layout(self.image?self.image->GetWidth():0,self.image?self.image->GetHeight():0,picture.right,picture.bottom);paint_image(dc,picture,self.image,viewport);
            if(self.image){RECT label{0,picture.bottom,area.right,area.bottom};FillRect(dc,&label,reader_theme::brush(reader_theme::Surface::card_header));
                SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reader_theme::foreground(item));DrawTextW(dc,L"Ver detalle de la imagen",-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
            if(!self.image){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reader_theme::foreground(item));const auto text=wide(self.failure);InflateRect(&area,-12,-12);DrawTextW(dc,text.c_str(),-1,&area,DT_WORDBREAK|DT_NOPREFIX);}
            if(GetFocus()==item){auto focus=area;InflateRect(&focus,-2,-2);DrawFocusRect(dc,&focus);}
            if(previous)SelectObject(dc,previous);if(message==WM_PAINT)EndPaint(item,&paint);return 0;}
        if(message==WM_ERASEBKGND)return 1;return DefSubclassProc(item,message,wp,lp);
    }
};
CurricularImageView::CurricularImageView(HWND parent,int id,const std::filesystem::path& lesson,const curriculum::ImageResource& resource):impl_(std::make_unique<Impl>()){
    impl_->description=resource.alt;
    try{impl_->image=load_image(lesson,resource);}catch(const std::exception& error){impl_->failure=std::string("Imagen no disponible. ")+error.what();}
    const auto name=wide(impl_->image?"Ver detalle de la imagen: "+resource.alt:impl_->failure+" "+resource.alt);
    impl_->item=CreateWindowExW(0,L"BUTTON",name.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    require(impl_->item!=nullptr,"No se pudo crear la imagen curricular.");impl_->fonts();SetWindowSubclass(impl_->item,Impl::proc,1,reinterpret_cast<DWORD_PTR>(impl_.get()));
}
CurricularImageView::~CurricularImageView(){if(IsWindow(impl_->item)){RemoveWindowSubclass(impl_->item,Impl::proc,1);DestroyWindow(impl_->item);}}
HWND CurricularImageView::window()const{return impl_->item;}
bool CurricularImageView::loaded()const{return bool(impl_->image);}
unsigned CurricularImageView::image_width()const{return impl_->image?impl_->image->GetWidth():0;}
unsigned CurricularImageView::image_height()const{return impl_->image?impl_->image->GetHeight():0;}
const std::string& CurricularImageView::error()const{return impl_->failure;}
int CurricularImageView::height_for_width(int width)const{return !impl_->image?pixels(impl_->item,100):
    std::clamp(static_cast<int>(static_cast<double>(width)*impl_->image->GetHeight()/impl_->image->GetWidth())+pixels(impl_->item,28),pixels(impl_->item,80),pixels(impl_->item,520));}
void CurricularImageView::show_detail(HWND owner){if(!impl_->image)return;static const auto registered=[](){WNDCLASSEXW type{sizeof(type)};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=Detail::proc;
    type.lpszClassName=L"LGAED.CurricularImageDetail";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);return RegisterClassExW(&type);}();
    require(registered||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"No se pudo registrar el visor de detalle.");
    auto state=std::make_unique<Detail>();state->image=impl_->image;state->description=wide(impl_->description);
    const auto child=create_internal_window(owner,L"LGAED.CurricularImageDetail",L"Detalle de la imagen",state.get());
    require(child!=nullptr,"No se pudo abrir el detalle de imagen.");state->owned=true;state.release();ShowWindow(child,SW_SHOWNOACTIVATE);
    if(IsWindowVisible(GetAncestor(owner,GA_ROOT))&&GetForegroundWindow()==GetAncestor(owner,GA_ROOT))SetFocus(GetDlgItem(child,detail_fit));
}
}
